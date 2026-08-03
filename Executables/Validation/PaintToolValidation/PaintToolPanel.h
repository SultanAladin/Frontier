/*==============================================================================================================================================
                                                           PAINTTOOLPANEL.H
==============================================================================================================================================*/
// 🧩 The whole paint card, assembled on the SHARED console. This is the one file that owns paint's DOMAIN state — which family and instrument
//    are selected, the resolved schema and its live value array, the preview ink — and drives ConstructWorkspaceContextConsole with it. The
//    console owns everything that used to be a fork: the two-slide carousel, the rail, the grid, the parameter widgets, both pinned footers.
//    What paint keeps is only what the console has no vocabulary for and cannot own on paint's behalf.
//
//    🔴 The rebuild ordering the fork was shaped around still governs, for the same reason: an options edit can change which rows exist
//       (`grade -> Custom` reveals a hardness slider), so the per-frame order is fixed — resolve the visible list from LAST frame's values,
//       hand it to the console, fold the console's edits back into the values, and let the next frame re-resolve. The console reports its
//       parameter edits in place in a ParameterBlock the panel folds back; applying an edit and continuing to draw the same frame would emit
//       the rows above it twice.
//
//    🔴 The schema is re-resolved on an INSTRUMENT change, not per frame. ResolvePaintSchema bakes the instrument's own defaults into the rows
//       it returns, and the visible list points INTO the stored schema — so the schema must outlive the visible list, and re-resolving it
//       mid-frame would dangle every pointer the arena is built from.
//
//    📝 The two surfaces the console cannot express — the slide-2 stroke preview and each tile's nib well — are drawn by paint's own painters
//       through the console's SurfaceBinding, wired in PaintConsoleBridge. The toast the prototype floats over the whole card is likewise paint's
//       own: the console never clips the foreground list, so paint keeps drawing it after the console returns.

#pragma once
#ifndef FRONTIER_VALIDATION_PAINTTOOL_PAINTTOOLPANEL_H
#define FRONTIER_VALIDATION_PAINTTOOL_PAINTTOOLPANEL_H

#include "PaintCardSpecification.h"
#include "PaintConsoleBridge.h"
#include "PaintOptionsColumn.h"
#include "PaintPreviewColumn.h"
#include "PaintSchema.h"

#include "EngineContext/Interface/WorkspaceContextConsole/Pane/WorkspaceContextConsolePane.h"
#include "EngineContext/Interface/WorkspaceContextConsole/Pane/ParameterPane.h"

#include "imgui.h"

namespace Frontier
{

struct SvgIconRegistry;
struct PaintIconStore;
struct ThemeConfiguration;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The card's entire state. One struct so a host owns exactly one object. The console-owned members (Focus, Carousel, Parameters) are held
//    HERE because the console reads and writes them across frames but does not own their storage — the same contract modelling and construction
//    keep. Everything else is paint's own domain, which the console has no concept of.
struct PaintToolPanelState
{
    //---- console-owned, panel-held ----
    ConsoleFocus      Focus      = { 0, -1 };   // [-]  - which cluster's grid + which action's options the console shows
    ConsoleCarousel   Carousel   = {};          // [-]  - slide position + open-pop age, stepped every frame
    ParameterBlock    Parameters = {};          // [-]  - the open action's live readings, folded back into Values each frame
    bool              CardOpen   = false;        // [-]  - whether the console is drawn this frame

    //---- the bridge context the two painters + the options arena read ----
    PaintConsoleContext Bridge = {};            // [-]  - refreshed every frame before composing the descriptor

    //---- paint's own domain ----
    PaintPreviewState Preview;                   // [-]  - which ink the preview paints with

    int               FamilyIndex     = 0;       // [idx]- selected family, into the family table
    int               InstrumentIndex = -1;      // [idx]- selected instrument, into the whole catalogue; -1 for none

    // 🔴 The resolved schema is STORED, not recomputed per frame: the visible list points into it. Re-resolved only when the
    //    instrument changes, which is also the only time its contents can differ.
    PaintSchema       Schema;                                        // [-]  - every row the instrument declares
    PaintControlValue Values[PaintSchemaValueLimit] = {};            // [-]  - live values, hidden rows included
    int               ValueCount = 0;                                // [idx]- how many were seeded

    // 📝 The art switch, carried here so one control at the host toggles every well on the card at once.
    PaintWellArtMode  ArtMode = PaintWellArtMode::NibCrop;   // [-] - nib crop or the full instrument strip

    // 📝 Kept only for the toast now — the console owns the switch nubs and the open dropdown the rest of this struct used to carry.
    //    ToastSeconds / ToastMessage are the sole live fields; FlashPaintToast writes them and ConstructPaintToast draws them.
    PaintOptionsState Toast;                     // [-]  - the flashed message and its dwell
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Open the card at a family, seeding the first instrument's schema so slide 2 is valid before it is ever shown.
void OpenPaintToolPanel(PaintToolPanelState& State, int FamilyIndex);

// Close the card. The console's carousel travels back down from its own phase, so this is not an instant hide.
void ClosePaintToolPanel(PaintToolPanelState& State);

// Select an instrument: re-resolve its schema, re-seed its values, reset the preview's ink to the family's first swatch, and point the
// console's focus at it.
// 🔴 The one place a schema change may happen, and it must not be called mid-draw — see the header note on dangling pointers.
void SelectPaintInstrument(PaintToolPanelState& State, int InstrumentIndex);

// Draw the whole card through the shared console and service every interaction it reports. Advances the console carousel from ImGui's delta.
// 📝 Takes the registry and strip store rather than resolving them, so the host owns upload lifetime and this stays drawing-only.
void ConstructPaintToolPanel(PaintToolPanelState& State, const ThemeConfiguration& Theme, const PaintCardPalette& Palette,
                             const PaintCardMetrics& Metrics, SvgIconRegistry* Registry, PaintIconStore* StripStore,
                             ImVec2 CardCentre);

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
