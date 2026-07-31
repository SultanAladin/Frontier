/*==============================================================================================================================================
                                                        PAINTCARDSPECIFICATION.H
==============================================================================================================================================*/
// 🧩 The paint card's resolved palette + metrics, and the widget vocabulary its parameter rows are authored in. Carries NO drawing code: the
//    components in this folder render whatever they are handed. Every number here is Documentation/Prototypes/PaintToolMenu.html's own CSS,
//    extracted by _ClaudeScratch/tmp/ProbePaintMetrics.js rather than read off by eye.
//
//    🔴 This is STANDALONE, deliberately not ToolCardSpecification. The paint card disagrees with the modelling card on three things that are not
//       reconcilable by adding a field: its box is 560x420 rather than 523x396, its parameter kinds are a different set (it has a dropdown and no
//       swatch strip / axis picker / snap chips), and it carries three tokens the shared palette has no concept of — the nib well's ring, the
//       slider's bright outline, and the PALE PAPER the two preview surfaces invert to. Widening the shared types to cover all of that would put
//       paint-only fields in front of every modelling and drafting author. Validation app, so it lives here.

#pragma once
#ifndef FRONTIER_VALIDATION_PAINTTOOL_PAINTCARDSPECIFICATION_H
#define FRONTIER_VALIDATION_PAINTTOOL_PAINTCARDSPECIFICATION_H

#include "imgui.h"

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Fixed table ceilings, MEASURED off the prototype by evaluating its own SchemaFor over all 102 instruments
//    (_ClaudeScratch/tmp/ProbePaintSchema.js), not estimated. A control row is a plain aggregate so a whole schema can be a
//    constexpr table with no allocation; a table that exceeds one of these trips a static assert at its definition site.
//    Measured peaks: segmented 3, select 21, controls in one group 7, visible rows 10, groups 2.
constexpr int PaintSegmentedOptionLimit = 4;    // [idx] - widest segmented strip (peak 3: Fine/Medium/Broad)
constexpr int PaintSelectOptionLimit    = 24;   // [idx] - longest dropdown (peak 21: the graphite grade scale 9H..9B+Custom)
constexpr int PaintGroupControlLimit    = 8;    // [idx] - most controls one group declares (peak 7, Stroke + 3 family extras)
constexpr int PaintGroupLimit           = 2;    // [idx] - a schema is a family group plus a Stroke group, always
constexpr int PaintVisibleControlLimit  = 10;   // [idx] - most rows one instrument shows at once


//------------------------------------------------------------------------------------------------------------------------
//                                                             TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which widget a parameter row draws. Four kinds, which is the prototype's whole vocabulary — and NOT the modelling card's six:
//    Select is new here (no shared card has a dropdown) and Swatches/AxisChoice/SnapTargets do not occur at all. A fifth kind is a
//    change to both the schema and the options column, and naming them here keeps the two in step.
enum class PaintControlCategory
{
    Slider,        // split value pill + track
    Segmented,     // exclusive option strip
    Select,        // dropdown; the one widget with no shared-card precedent
    Switch,        // on/off track + nub
};


// 📝 Why a row may be absent. The prototype writes these as `when:` closures; here they are a closed enumeration, because a
//    plain enum keeps PaintControlDescriptor trivially copyable (a schema is built per instrument and copied by value) and
//    keeps all eight conditions listed in one place instead of scattered across ten family tables.
//
//    🔴 The two KINDS are not interchangeable, and the split is the thing to preserve:
//      * LIVE-STATE  (GradeIsCustom, BinderIsWatercolour) re-read the seeded value array on every edit, so the row appears
//        and disappears as the user works. The prototype writes them `P => P.grade === "Custom"`.
//      * INSTRUMENT  (the rest) read the selected instrument and are fixed for as long as it stays selected.
//    Treating a live-state condition as instrument-keyed freezes it at its seed-time answer — picking the Custom grade would
//    then never reveal the hardness slider, silently.
enum class PaintRowCondition
{
    Always = 0,                 // no `when:` at all
    GradeIsCustom,              // live       - pencil `hardness`        <- P.grade === "Custom"
    BinderIsWatercolour,        // live       - coloured `wet` + `bleed`  <- P.binder === "Watercolour"
    InstrumentMechanical,       // instrument - pencil `lead`            <- Tool.mechanical === true
    InstrumentNotHighlighter,   // instrument - marker `ink`             <- Tool.highlighter !== true
    InstrumentNotAirbrush,      // instrument - spray `nozzle`           <- Tool.airbrush !== true
    DryHasVariants,             // instrument - dry `variant`            <- Array.isArray(Tool.variants) || Tool.variant !== undefined
    DryHasTone,                 // instrument - dry `tone`               <- Tool.tone !== undefined
    DryHasEdge,                 // instrument - dry `edge`               <- Tool.edge !== undefined
};


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One parameter row. A single aggregate covers all four kinds rather than a union: the schema is authored by hand and an author
//    editing a row should not have to know which arm of a variant it lives in. Unused members stay zero — Boundary/Unit mean
//    nothing to a Switch, OptionLabels mean nothing to a Slider.
struct PaintControlDescriptor
{
    PaintControlCategory Category;                                // [-]  - which widget draws
    // 🔴 The prototype's `k` — the parameter's IDENTITY, and it is carried rather than inferred from the label because the two do not
    //    correspond: `hardness` is labelled "Hardness" on one family and "Custom hardness" on another, while `grain` and `bristleTx`
    //    are distinct keys that both mean tooth to the preview. Matching on Label would have to know both spellings of hardness and
    //    would break silently the day a caption is reworded — a wrong stroke preview with every row still looking correct.
    const char*          Key;                                     // [-]  - schema key, the prototype's `k` ("size", "bristleTx")
    const char*          GlyphName;                               // [-]  - leading mark, prototype identifier ("ParamSize")
    const char*          Label;                                   // [-]  - row caption

    float                MinimumBoundary;                         // [-]  - Slider: travel floor
    float                MaximumBoundary;                         // [-]  - Slider: travel ceiling
    float                InitialReading;                          // [-]  - Slider: authored default
    const char*          Unit;                                    // [-]  - Slider: unit segment text

    // 📝 Options are a BORROWED pointer + count, not an inline array. The widest list is the 21-entry graphite grade scale, and
    //    inlining that ceiling into every row would cost 24 pointers on all ~70 rows to serve one. It also lets the two lists the
    //    prototype shares (the grade scale, the marker nib sets) exist once.
    const char* const*   OptionLabels;                            // [-]  - Segmented / Select: options (borrowed); null for neither
    int                  OptionCount;                             // [idx]- how many options
    // 🔴 The authored default is carried BY LABEL, not by index. The prototype writes `def:Tool.nib||"Medium"` and the catalogue
    //    stores its overrides as strings, so an index would mean a label->index conversion at every seed site. Worse, the two
    //    marker nib sets have DIFFERENT orders — ["Chisel","Bullet"] for a highlighter vs ["Bullet","Chisel","Brush"] for a
    //    marker — so index 0 does not name the same option in both. Resolving by label makes that mix-up impossible.
    const char*          InitialOptionLabel;                      // [-]  - Segmented / Select: authored default, by label

    bool                 InitialActivation;                       // [-]  - Switch: authored default

    // 📝 Absent rather than greyed when this is false — see PaintRowCondition. Always for the ~50 unconditional rows.
    PaintRowCondition    Condition;                               // [-]  - when this row is drawn
};


// 📝 The colours the card draws with. Most come from the shared theme; the rest are card-specific tokens the theme has no field
//    for. Resolved once per cycle from a ThemeConfiguration so a palette change still flows from one place.
struct PaintCardPalette
{
    ImU32 CardFill;          // [-] - --menu        card + rail background
    ImU32 PaneFill;          // [-] - --menu-2      grid + options pane, one notch darker
    ImU32 RailSelectedFill;  // [-] - .rail-item.active
    ImU32 TileFill;          // [-] - --tile        tile rest
    ImU32 TileHoverFill;     // [-] - --tile-hi     tile under pointer
    ImU32 TileChosenFill;    // [-] - .tile.on      accent at .08 over the pane
    ImU32 Hairline;          // [-] - --hair        pane rules
    ImU32 HairlineStrong;    // [-] - --hair-strong card border
    ImU32 Accent;            // [-] - --accent      active marker, live tallies
    ImU32 Ink;               // [-] - --ink         primary text + live glyph
    ImU32 Muted;             // [-] - --muted       secondary text
    ImU32 Faint;             // [-] - --faint       tertiary text + gated glyph
    ImU32 ValueBlack;        // [-] - --value-black numeric pill centre
    ImU32 ValueUnitFill;     // [-] - --value-unit  unit segment
    ImU32 ValueInk;          // [-] - --text-value  the reading itself
    ImU32 TrackFill;         // [-] - --track-bg    switch track, off
    ImU32 TrackTravelled;    // [-] - --track-fill  slider travelled portion
    ImU32 KnobFill;          // [-] - --knob        slider knob, selected segment, switch nub
    ImU32 KnobInk;           // [-] - #1b1b1e       text over a knob-filled segment

    // 📝 The tokens with no shared-card counterpart at all.
    ImU32 WellRing;          // [-] - --well-hair #262629   the nib window's ring
    ImU32 WellRingHover;     // [-] - #46464c               that ring under the pointer
    ImU32 SliderOutline;     // [-] - --outline .22 alpha   the slider's 1.5 px border

    // 🔴 The one place the card inverts, and it inverts because the subject demands it: the instruments lay down pigment, and pigment
    //    is only legible against paper. On a dark swatch a stroke reads as a HOLE rather than as ink. Both preview surfaces — the
    //    stroke strip and the dot well — are this pale, so anything drawn over them takes PaperInk, never Ink.
    ImU32 PaperFill;         // [-] - #f4f1ea               .pv-stroke + .pv-dot-wrap
    ImU32 PaperInk;          // [-] - #1b1b1e               text/marks over paper
};


// 📝 The card's fixed geometry, straight from the prototype's CSS. Stated once here because several numbers are load bearing in a
//    way a re-derivation would silently break — see the width comment on the resolver.
struct PaintCardMetrics
{
    // ── the box ──
    float CardWidth         = 560.0f;   // [px] - 196 rail + 363 grid + 1 hairline; the prototype states this arithmetic itself
    float CardHeight        = 420.0f;   // [px] - min(420px, 78vh); fixed on BOTH axes, panes scroll inside
    float LeftColumnWidth   = 196.0f;   // [px] - rail, and the preview column that replaces it. PINNED, not intrinsic
    float RightColumnWidth  = 363.0f;   // [px] - grid, and the options column that replaces it
    float CardRounding      =  18.0f;   // [px] - --r-menu

    // ── pane bands ──
    float HeaderHeight      =  53.0f;   // [px] - .pane-head, one rule for BOTH panes so the seam never steps
    float GridFootHeight    =  27.0f;   // [px] - .grid-foot
    float OptionsFootHeight =  44.0f;   // [px] - .opt-foot

    // ── rail ──
    float RailPadding       =   7.0f;   // [px] - .rail-body
    float RailRowHeight     =  32.0f;   // [px] - --row-h
    float RailRowRounding   =   9.0f;   // [px]
    float RailRowGap        =   1.0f;   // [px]
    float RailDotDiameter   =   8.0f;   // [px] - .r-dot; a family carries a colour dot, not a glyph
    float RailDotChosenGrow =   1.28f;  // [-]  - .rail-item.active .r-dot scale
    float RailMarkerWidth   =   3.0f;   // [px] - .rail-item.active::before
    float RailMarkerHeight  =  15.0f;   // [px]

    // ── grid ──
    float GridPadding       =   9.0f;   // [px] - .grid-body
    float GridColumnCount   =   4.0f;   // [-]  - repeat(4, 1fr)
    float TileGap           =   6.0f;   // [px]
    float TileRounding      =   9.0f;   // [px]
    float WellDiameter      =  46.0f;   // [px] - .well; the round window the nib crop lands in
    float NibArtEdge        =  46.0f;   // [px] - .well svg at 100%, and .h-ic.nib svg
    float HeaderIconEdge    =  24.0f;   // [px] - .h-ic

    // ── preview ──
    float StrokeStripHeight =  46.0f;   // [px] - .pv-stroke
    float StandHeight       =  96.0f;   // [px] - .pv-stand
    float StandArtWidth     = 150.0f;   // [px] - .pv-stand svg, before the rotate
    float StandArtHeight    =  30.0f;   // [px]
    float StandArtGrow      =   1.5f;   // [-]  - scale(1.5) applied after rotate(-90deg)
    float SwatchDiameter    =  17.0f;   // [px] - .pv-sw
    float SwatchGap         =   5.0f;   // [px]

    // ── options ──
    float OptionsPaddingX   =  13.0f;   // [px] - .options-body
    float OptionsPaddingY   =  12.0f;   // [px]
    float OptionsRowGap     =  14.0f;   // [px] - between whole parameter rows
    float ControlGap        =   6.0f;   // [px] - .param, label to widget
    float SliderHeight      =  22.0f;   // [px] - .slider
    // 🔴 The value pill is TALLER than the track beside it — `.valuebox{ height:var(--row-h) }` is 32 px against the slider's 22, and
    //    `.slider-ctl{ align-items:center }` centres the short track against the tall pill. So a slider row is 32 px high, not 22, and
    //    the two are separate tokens: deriving one from the other (or from the rail's own 32 px --row-h, which scales independently)
    //    silently collapses the row to the track's height and crops the reading.
    float ValueBoxHeight    =  32.0f;   // [px] - .valuebox, --row-h
    float SliderKnobEdge    =  17.0f;   // [px] - .knob
    float SliderOutlineWide =   1.5f;   // [px] - .slider border
    float SegmentHeight     =  28.0f;   // [px] - .seg-opt
    float SegmentGap        =   5.0f;   // [px] - .segment
    float SegmentRounding   =   8.0f;   // [px]
    float SwitchWidth       =  38.0f;   // [px] - .switch
    float SwitchHeight      =  22.0f;   // [px]
    float SwitchNubEdge     =  16.0f;   // [px] - .nub
    float ButtonHeight      =  27.0f;   // [px] - .obtn
    float ButtonRounding    =   8.0f;   // [px]

    // ── animation ──
    // 📝 Durations are NOT scaled by UiScale: a bigger card does not take longer to slide. Scaling
    //    these was the first thing that felt wrong when the whole struct was multiplied blindly.
    float CarouselSeconds   =   0.34f;  // [s] - .tool-track, cubic-bezier(.5,.05,.2,1)
    float OpenSeconds       =   0.14f;  // [s] - @keyframes pop, cubic-bezier(.16,1,.3,1)
    float RailDotSeconds    =   0.16f;  // [s] - cubic-bezier(.34,1.56,.64,1), an overshoot
    float SwitchSeconds     =   0.15f;  // [s] - .nub left
    float GridSwapSeconds   =   0.13f;  // [s] - @keyframes fade, ease
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Resolve the card palette from the shared theme. Card-specific tokens the theme has no field for are stated as the prototype's own
// literals, so this function is the single place they exist.
[[nodiscard]] PaintCardPalette ResolvePaintCardPalette(const ThemeConfiguration& Theme);

// Resolve the card metrics, scaled by the theme's UiScale so one config knob resizes the whole card. Durations are left alone.
[[nodiscard]] PaintCardMetrics ResolvePaintCardMetrics(const ThemeConfiguration& Theme);

// Parse an authored CSS colour ("#3b82f6") into a packed ImU32 at full alpha. The catalogue stores every family's rail dot and every
// instrument's swatches as the prototype's own hex strings, so the conversion happens once, here.
// 📝 Returns FallbackColour for anything it cannot parse, rather than a garbage colour: a mistyped hex should read as obviously
//    wrong on screen, not as a plausible-but-different tint nobody notices.
[[nodiscard]] ImU32 ResolveAuthoredColour(const char* CssHex, ImU32 FallbackColour);

} // namespace Frontier

#endif
