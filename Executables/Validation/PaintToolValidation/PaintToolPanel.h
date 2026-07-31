/*==============================================================================================================================================
                                                           PAINTTOOLPANEL.H
==============================================================================================================================================*/
// 🧩 The whole paint card, assembled: the shell's two slides, the four columns inside them, the parameter values they edit, and the navigation
//    between them. This is the one file that owns STATE — every component in this folder draws what it is handed and reports what was touched,
//    so the decisions those reports imply are all made here.
//
//    🔴 The rebuild ordering is the point of this file. An options edit can change which rows exist (`grade -> Custom` reveals a hardness
//       slider), and the column reports rather than applies precisely so that the re-resolve happens BETWEEN frames instead of inside a
//       half-drawn one. So the per-frame order is fixed: resolve the visible list from last frame's values, draw, apply what came back, and
//       let the next frame re-resolve. Applying an edit and then continuing to draw the same frame would emit the rows above it twice.
//
//    🔴 The schema is re-resolved on an INSTRUMENT change, not per frame. ResolvePaintSchema bakes the instrument's own defaults into the rows
//       it returns, and PaintVisibleControl points INTO the stored schema — so the schema must outlive the visible list, and re-resolving it
//       mid-frame would dangle every pointer the column is walking.
//
//    📝 The prototype is a right-click context menu over a viewport. Here it is a persistent card, because a validation app has no scene to
//       right-click on; the open/close animation and both slides are still exercised through the same state the menu drives.

#pragma once
#ifndef FRONTIER_VALIDATION_PAINTTOOL_PAINTTOOLPANEL_H
#define FRONTIER_VALIDATION_PAINTTOOL_PAINTTOOLPANEL_H

#include "PaintCardShell.h"
#include "PaintCardSpecification.h"
#include "PaintInstrumentGrid.h"
#include "PaintInstrumentRail.h"
#include "PaintOptionsColumn.h"
#include "PaintPreviewColumn.h"
#include "PaintSchema.h"

#include "imgui.h"

namespace Frontier
{

struct SvgIconRegistry;
struct PaintIconStore;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The card's entire state. One struct so a host owns exactly one object, and so the ordering constraints above are enforceable in one
//    function rather than spread across a host's own members.
struct PaintToolPanelState
{
    PaintCardShellState Shell;                     // [-]  - open + carousel animation
    PaintRailState      Rail;                      // [-]  - per-family dot growth
    PaintGridState      Grid;                      // [-]  - family-swap fade
    PaintOptionsState   Options;                   // [-]  - open dropdown + toast
    PaintPreviewState   Preview;                   // [-]  - which ink the preview paints with

    int                 FamilyIndex     = 0;       // [idx]- selected family, into the family table
    int                 InstrumentIndex = -1;      // [idx]- selected instrument, into the whole catalogue; -1 for none

    // 🔴 The resolved schema is STORED, not recomputed per frame: the visible list points into it. Re-resolved only when the
    //    instrument changes, which is also the only time its contents can differ.
    PaintSchema         Schema;                                        // [-]  - every row the instrument declares
    PaintControlValue   Values[PaintSchemaValueLimit] = {};            // [-]  - live values, hidden rows included
    int                 ValueCount = 0;                                // [idx]- how many were seeded

    // 📝 Scroll offsets are per pane and per slide, so returning to the grid restores where it was — the prototype's panes are
    //    separately scrolled divs and keep their own positions.
    float               GridScroll    = 0.0f;      // [px] - grid body scroll
    float               OptionsScroll = 0.0f;      // [px] - options body scroll

    // 📝 The art switch, carried here so one control at the host toggles every well on the card at once.
    PaintWellArtMode    ArtMode = PaintWellArtMode::NibCrop;   // [-] - nib crop or the full instrument strip
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Open the card at a family, seeding the first instrument's schema so slide 2 is valid before it is ever shown.
void OpenPaintToolPanel(PaintToolPanelState& State, int FamilyIndex);

// Close the card. The animation runs down from the shell's own phase, so this is not an instant hide.
void ClosePaintToolPanel(PaintToolPanelState& State);

// Select an instrument: re-resolve its schema, re-seed its values, and reset the preview's ink to the family's first swatch.
// 🔴 The one place a schema change may happen, and it must not be called mid-draw — see the header note on dangling pointers.
void SelectPaintInstrument(PaintToolPanelState& State, int InstrumentIndex);

// Draw the whole card and service every interaction it reports. Advances all four animations from ImGui's delta time.
// 📝 Takes the registry and strip store rather than resolving them, so the host owns upload lifetime and this stays drawing-only.
void ConstructPaintToolPanel(PaintToolPanelState& State, const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                             SvgIconRegistry* Registry, PaintIconStore* StripStore, ImVec2 CardCentre);

// Resolve the six stroke parameters the preview reads, out of the live value set.
// 🔴 This is where the prototype's parameter ALIASING lives — `grain ?? bristleTx`, `scatter ?? spread`, `softness ?? (100 - hardness)`. It
//    matches on PaintControlDescriptor::Key, the prototype's own `k`, and NOT on the row's caption. Matching by label was tried first and is
//    not merely fragile, it is UNRESOLVABLE: the mapping is many-to-many in both directions. Three keys wear two captions each (`hardness` is
//    "Hardness" and "Custom hardness", `bleed` is "Bleed" and "Feathering", `pressure` is "Pressure" and "Air pressure"), and — the case that
//    settles it — two pairs of DISTINCT keys share one caption, `nib`/`nibShape` both reading "Nib" and `etype`/`variant` both reading "Type".
//    No label match can tell those apart, so it would feed the preview the wrong parameter with every row still looking correct on screen.
[[nodiscard]] PaintStrokeParameters ResolvePaintStrokeParameters(const PaintSchema& Schema,
                                                                const PaintControlValue* Values, int ValueCount);

} // namespace Frontier

#endif
