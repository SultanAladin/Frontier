/*==============================================================================================================================================
                                                         PAINTINSTRUMENTGRID.H
==============================================================================================================================================*/
// 🧩 The card's right column on slide 1: the selected family's instruments as a 4-column grid of tiles, each a round well holding the nib crop,
//    plus the pinned tally foot beneath. Ported from Documentation/Prototypes/PaintToolMenu.html's `.grid` / `.tile` / `.grid-foot` / `RenderGrid()`.
//
//    🔴 The WELL is the idea this pane rests on, and the prototype says so in its own comment: each instrument is authored on a 300x60 landscape
//       box with the writing tip at the RIGHT end, and the well is a round window showing only that tip. Drawing whole instruments instead would
//       put 23 landscape strips per family into a 363 px column, which is the unreadable layout the crop exists to avoid.
//
//    🔴 Two art modes, and they are two RASTERS, not two samplings of one. The prototype crops by SUBSTITUTING the viewBox —
//       `NibArt` rewrites `viewBox="0 0 300 60"` to `viewBox="188 6 48 48"` — so the nib is the same document re-rasterized through a
//       48x48 window, at ~6x the effective resolution of the same region inside a 300x60 raster. Cropping with UV coordinates on the
//       landscape raster would land the right REGION at a visibly softer resolution, and stroke widths resolve against the viewBox too.
//       So the switch picks between the square nib texture (registry, uploaded up front) and the landscape strip (PaintIconStore, lazy).
//
//    📝 The tiles are hit-tested manually against the shell's clip, like the rail's rows, because this pane draws with the window draw list
//       inside the shell's pushed clip rather than opening a child window. A tile on the off-card slide must not take clicks.

#pragma once
#ifndef FRONTIER_VALIDATION_PAINTTOOL_PAINTINSTRUMENTGRID_H
#define FRONTIER_VALIDATION_PAINTTOOL_PAINTINSTRUMENTGRID_H

#include "PaintCardShell.h"
#include "PaintCardSpecification.h"
#include "PaintCatalogue.h"

#include "imgui.h"

namespace Frontier
{

struct SvgIconRegistry;
struct PaintIconStore;

//------------------------------------------------------------------------------------------------------------------------
//                                                             TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which art each well shows. NOT in the prototype, which only ever calls NibArt at this site — this is the switch the card
//    gains here so the crop can be compared against the whole instrument on screen. Named after what the well SHOWS rather
//    than after the texture source, because the sources differ in more than aspect (see the header note).
enum class PaintWellArtMode
{
    NibCrop = 0,   // the 48x48 window on the writing tip — the prototype's own choice
    FullStrip,     // the whole authored 300x60 instrument, letterboxed into the well
};


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The grid's animation state. Only the family-swap fade is stored: a tile's own hover and press are per-frame CSS
//    transitions that ImGui's hover test reproduces without a phase, exactly as in the rail.
struct PaintGridState
{
    // 📝 0 = just swapped, 1 = settled. `@keyframes fade` runs on the WHOLE grid, not per tile, because the prototype
    //    toggles one class on the container — so one phase is correct here where the rail needed one per row.
    float SwapPhase = 1.0f;                                  // [-] - family-swap fade progress
    // 🔴 Which family the phase belongs to. Without this the fade cannot be RESTARTED on a swap, and the prototype
    //    restarts it deliberately: it removes the class, forces a reflow (`void Grid.offsetWidth`), then re-adds it.
    int   FadedFamilyIndex = -1;                             // [idx] - family the current fade was started for
};


// 📝 What one grid pass needs that is not geometry. Bundled rather than passed as six parameters, because the icon sources
//    and the art mode travel together and a caller should not be able to hand the strip store without the mode.
struct PaintGridArtSources
{
    SvgIconRegistry* Registry  = nullptr;                     // [-] - square nib crops, registered up front
    PaintIconStore*  StripStore = nullptr;                    // [-] - landscape strips, uploaded lazily
    PaintWellArtMode ArtMode   = PaintWellArtMode::NibCrop;   // [-] - which of the two each well shows
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Advance the family-swap fade by one frame.
void AdvancePaintGrid(PaintGridState& State, const PaintCardMetrics& Metrics, float DeltaSeconds);

// Restart the swap fade when the family has CHANGED. Idempotent for the family already faded, so calling it every frame
// cannot freeze the grid at full transparency — which is exactly what a naive "reset the phase on render" would do.
// 📝 Separate from AdvancePaintGrid because the prototype's restart is an explicit act at the click site (`RenderGrid(true)`),
//    not something the animation infers.
void RestartPaintGridFade(PaintGridState& State, int FamilyIndex);

// Replay the swap fade unconditionally, including for the family already showing. This is the prototype's behaviour on a
// click — it rebuilds the grid and re-adds the class whether or not the band changed.
// 🔴 The guarded restart above is the one a render path may call; this one is not, and the split exists because a per-frame
//    call to this would hold the grid at zero opacity forever.
void RequestPaintGridReplay(PaintGridState& State, int FamilyIndex);

// Draw one family's tiles into a pane region and report which instrument index was clicked, or -1 for none. The index is into
// the WHOLE catalogue table, not into the family's slice, so a caller never has to re-add the family's base.
[[nodiscard]] int ConstructPaintInstrumentGrid(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                               const PaintGridState& State, const PaintPaneRegion& Region,
                                               PaintGridArtSources& Art,
                                               int FamilyIndex, int SelectedInstrumentIndex, float ScrollOffset);

// Resolve how tall the current family's grid is, so a caller can clamp its own scroll. Cheap arithmetic, no drawing.
[[nodiscard]] float ResolvePaintGridContentHeight(const PaintCardMetrics& Metrics, int FamilyIndex);

// Draw the pinned foot: the catalogue total with its accent number, and the active instrument's name.
// 📝 Takes the label rather than the index so the foot can say "No instrument selected" without the catalogue — the prototype
//    writes that string when nothing is picked, and it is not an instrument name with a special value.
void ConstructPaintGridFoot(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                            const char* ActiveInstrumentLabel, int CatalogueTotal,
                            ImVec2 FootMinimum, float PaneWidth);

} // namespace Frontier

#endif
