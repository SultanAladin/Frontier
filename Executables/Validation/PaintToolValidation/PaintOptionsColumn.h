/*==============================================================================================================================================
                                                         PAINTOPTIONSCOLUMN.H
==============================================================================================================================================*/
// 🧩 The card's right column on slide 2: the visible parameter rows as sliders / segmented strips / dropdowns / switches, the group titles above
//    them, and the pinned foot with its live tally and its two buttons. Ported from Documentation/Prototypes/PaintToolMenu.html's `.options-body`,
//    its four `Build*` control builders, `RenderOptions()`, `.opt-foot` and `.toast`.
//
//    🔴 An edit here can CHANGE WHICH ROWS EXIST, and that is the constraint the whole interface is shaped around. Two of the schema's
//       conditions are live-state (`grade === "Custom"`, `binder === "Watercolour"`), so committing a segmented or select value can add or
//       remove a row below the one just clicked. The prototype handles it by rebuilding the pane wholesale — `RenderOptions()` from inside
//       the click handler — which it can do because its rows are DOM nodes it is free to discard mid-handler.
//       ImGui cannot: this column is emitting draw commands into a frame that is already half-built, and re-walking the list from inside
//       itself would draw the rows above twice. So an edit is REPORTED, not applied — the caller commits it and the next frame's walk sees
//       the new shape. The one-frame lag is invisible (the click's own frame still shows the pre-click rows, exactly as the prototype's
//       does before its handler runs) and it is the only ordering that keeps a rebuild off the draw path.
//
//    🔴 A slider reports EVERY frame of a drag, not just on release. The prototype commits inside `pointermove` and calls `PaintPreview()`
//       from `Commit()`, so the stroke ribbon re-paints continuously as the knob travels. Reporting only on mouse-up would leave the preview
//       frozen mid-drag, which is precisely the confirmation the pane exists to give.
//
//    📝 The value pill is DRAWN, not an InputText. The prototype's `<input type="number">` is click-to-type, and reproducing that faithfully
//       needs a focus-owning text field per row; that is a keyboard-state problem, not a drawing one, and it is recorded as unported rather
//       than half-built. The pill still shows the live reading and its unit segment, so it reads identically at rest.

#pragma once
#ifndef FRONTIER_VALIDATION_PAINTTOOL_PAINTOPTIONSCOLUMN_H
#define FRONTIER_VALIDATION_PAINTTOOL_PAINTOPTIONSCOLUMN_H

#include "PaintCardShell.h"
#include "PaintCardSpecification.h"
#include "PaintSchema.h"

#include "imgui.h"

namespace Frontier
{

struct SvgIconRegistry;
struct PaintIconStore;

//------------------------------------------------------------------------------------------------------------------------
//                                                             TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the column is asking the caller to do. Named for the ACT rather than for the widget, because two widgets produce the same act:
//    a segmented pill and a dropdown entry both choose an option, and the caller commits them identically.
enum class PaintOptionsRequest
{
    None = 0,          // nothing touched this frame
    SetReading,        // a slider moved            -> Reading is the new value, already clamped + quantised
    ChooseOption,      // a pill or entry picked    -> ChosenOption is the new index
    ToggleActivation,  // a switch flipped          -> Activated is the new state
    ResetToDefaults,   // the Reset button          -> caller re-seeds from the schema
    ApplyInstrument,   // the Select button         -> caller flashes + closes
};


// 📝 The dropdown's open state. It lives OUTSIDE the column because an open dropdown outlives the frame that opened it, and because it
//    must be closable by the caller — leaving a menu open across a slide change or an instrument swap would float a list over the wrong pane.
//    🔴 Which ROW is open is stored as the value index, not as a row position: the visible list re-collapses as live-state conditions
//       change, so a positional index can silently come to name a different parameter between frames.
struct PaintOptionsState
{
    int   OpenSelectValueIndex = -1;   // [idx] - value index of the row whose dropdown is open, or -1
    float ToastSeconds         = 0.0f; // [s]   - remaining toast dwell; the prototype's 1500 ms setTimeout
    char  ToastMessage[128]    = {};   // [-]   - the flashed text, owned so the caller need not keep it alive

    // 🔴 `.nub{ transition:left .15s }` — the switch nub SLIDES, and a snapped nub reads as a different control. The phase must live
    //    out here for the same reason the open dropdown does: it outlives the frame that started it, and the column reports rather
    //    than mutates. Indexed by VALUE index, not by row position, because the visible list re-collapses as live-state rows appear
    //    and a positional index would carry one switch's travel onto another parameter's nub.
    //    📝 One phase per value slot rather than per switch: the switches are a handful of the rows but nothing identifies a slot as
    //       a switch without walking the schema, and a float per slot is cheaper than that walk plus a second index mapping.
    float SwitchPhase[PaintSchemaValueLimit] = {};   // [0..1] - 0 at the off end, 1 at the on end
};


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One frame's outcome. A single struct rather than an out-parameter per widget kind, so a caller cannot forget one: the request tag says
//    which member is meaningful, and ValueIndex says which row it belongs to.
struct PaintOptionsOutcome
{
    PaintOptionsRequest Request      = PaintOptionsRequest::None;  // [-]  - what happened
    int                 ValueIndex   = -1;                         // [idx]- which row, as a value index
    float               Reading      = 0.0f;                       // [-]  - SetReading
    int                 ChosenOption = 0;                          // [idx]- ChooseOption
    bool                Activated    = false;                      // [-]  - ToggleActivation
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Advance the toast's dwell and every switch nub's travel by one frame. Separate from drawing so a caller that skips a frame's draw
// still expires the toast and still settles the nubs.
// 📝 The values are taken because the nub's TARGET is the switch's committed state: the phase chases `Activated`, so an edit applied
//    after last frame's draw is what this frame's travel is animating toward. Passing null (or a zero count) advances the toast only,
//    which is what a caller with no instrument selected has to hand.
void AdvancePaintOptions(PaintOptionsState& State, const PaintControlValue* Values, int ValueCount, float DeltaSeconds);

// Place every switch nub AT its value's state with no travel.
// 🔴 For the transitions that REPLACE the value set — an instrument swap and a reset. The prototype rebuilds the pane's DOM there, so
//    the new switches mount already at their position and no transition runs; letting the phases chase instead would slide a nub from
//    the outgoing instrument's state to the incoming one's, animating a change the user never made.
void SettlePaintSwitches(PaintOptionsState& State, const PaintControlValue* Values, int ValueCount);

// Raise the toast with a message, replacing whatever it was showing and restarting the dwell.
// 📝 Mirrors the prototype's Flash(), including the RESTART: it calls clearTimeout before setting a new one, so a second flash inside the
//    dwell shows the new text for a full 1500 ms rather than inheriting the remainder of the first.
void FlashPaintToast(PaintOptionsState& State, const char* Message);

// Clamp and quantise a reading the way the prototype's Commit() does: clamp to the boundaries, snap to the step, then round to 4 decimals.
// Exposed because the caller applies the value and must not re-derive this arithmetic.
//
// 🔴 The step is 1 for EVERY slider, and that is measured, not assumed: all 38 `type:"slider"` rows in the prototype carry `step:1`, with
//    38 `step:` occurrences total, so no row omits it and none disagrees. PaintControlDescriptor therefore has no Step field — adding one
//    would put a field in front of every schema author that only ever holds a single value. The constant lives at the one site below
//    instead, named, so a future row with a fractional step has one obvious place to become a field.
//    📝 Stated here rather than buried in the .cpp because it is a claim about the SCHEMA, and a reader checking whether a step was
//       dropped should not have to open the painter to find out.
//
// 🔴 The ORDER is load bearing — clamp first, then snap. Snapping first can push a value back outside a boundary that is not itself a
//    multiple of the step, and it would then stick there: opacity and flow both run 1..100, so their span of 99 is not a whole number of
//    steps away from the floor at either end.
[[nodiscard]] float QuantisePaintReading(const PaintControlDescriptor& Control, float Value);

// Resolve how tall the visible rows are, so a caller can clamp its own scroll. Cheap arithmetic, no drawing.
[[nodiscard]] float ResolvePaintOptionsContentHeight(const PaintCardMetrics& Metrics,
                                                     const PaintVisibleControl* Visible, int VisibleCount);

// Draw the visible parameter rows into a pane region and report the one edit this frame, if any.
// 🔴 Reports rather than applies — see the header note. The caller must commit the outcome before the next frame's walk, and must re-resolve
//    the visible list afterwards, because a committed option can add or remove rows.
// 📝 The registry is taken so each row can draw its leading glyph (`.param-label .name svg`). Passing null is legal and draws the rows
//    without their marks, which is what a caller that has not registered the parameter pack gets — a missing mark, not a missing row.
[[nodiscard]] PaintOptionsOutcome ConstructPaintOptionsColumn(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                                              const PaintPaneRegion& Region, PaintOptionsState& State,
                                                              SvgIconRegistry* Registry,
                                                              const PaintVisibleControl* Visible, int VisibleCount,
                                                              const PaintControlValue* Values, int ValueCount,
                                                              float ScrollOffset);

// Draw the pinned foot: the live "<n> live parameters · <n> groups" tally and the Reset / Select buttons. Reports a button press.
// 📝 Takes both counts rather than deriving them, because the group count is the count AFTER the empty-group collapse and only the
//    caller's visible list knows it.
[[nodiscard]] PaintOptionsRequest ConstructPaintOptionsFoot(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                                            int LiveParameterCount, int GroupCount,
                                                            ImVec2 FootMinimum, float PaneWidth);

// Draw the toast, centred on the card and floating above it, while its dwell lasts.
// 🔴 Drawn OUTSIDE the shell's clip, with the foreground draw list: the prototype's `.toast` is `position:fixed` with `z-index:120`, so it
//    sits over the card rather than inside a pane. Drawing it into the pane's list would clip it to the options column.
void ConstructPaintToast(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                         const PaintOptionsState& State, float CardCentreX, float CardBottomY);

} // namespace Frontier

#endif
