/*==============================================================================================================================================
                                                         PAINTPREVIEWCOLUMN.H
==============================================================================================================================================*/
// 🧩 The card's left column on slide 2: the stroke ribbon, the single dab, the ink swatches, the standing instrument, and the spec rows.
//    Ported from Documentation/Prototypes/PaintToolMenu.html's `.pv-*` CSS and its `PaintPreview()` painter.
//
//    🔴 The two preview surfaces are CANVASES in the prototype, not styled boxes — `<canvas width="360" height="92">` and
//       `<canvas width="92" height="92">`, both at 2x their CSS size. So this is a procedural stamp compositor, not a ribbon shape with a
//       fill: `PaintPreview()` lays down 220 overlapping circles along an eased sine path, each with its own alpha, radius, and
//       (under Grain/Scatter) its own jitter. Drawing a tapered quad instead would land the right SILHOUETTE with none of the tooth,
//       and the tooth is the entire point — it is what distinguishes a dry brush from a marker at these parameters.
//
//    🔴 The stamp jitter must be DETERMINISTIC per (instrument, swatch, parameters), which is why a seeded generator is threaded through
//       rather than calling rand(). The prototype paints once per parameter change onto a retained canvas; ImGui has no retained surface
//       and re-runs this every frame, so live `Math.random()` would make the grain crawl and the stroke boil. Same inputs must give the
//       same speckle, and only a parameter change may reshuffle it.
//
//    📝 Both surfaces are PALE (#f4f1ea) — the one place the card inverts. Pigment is only legible against paper; on a dark swatch a
//       stroke reads as a hole. Anything drawn over these takes PaperInk, never Ink.

#pragma once
#ifndef FRONTIER_VALIDATION_PAINTTOOL_PAINTPREVIEWCOLUMN_H
#define FRONTIER_VALIDATION_PAINTTOOL_PAINTPREVIEWCOLUMN_H

#include "PaintCardShell.h"
#include "PaintCardSpecification.h"
#include "PaintCatalogue.h"

#include "imgui.h"

namespace Frontier
{

struct SvgIconRegistry;
struct PaintIconStore;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The six parameters the painter actually reads, pulled out of the live parameter set by the caller. Named after what they MEAN to
//    the stroke rather than after their schema keys, because several keys map onto one meaning: the prototype reads
//    `grain ?? bristleTx`, `scatter ?? spread`, and derives softness as `100 - hardness` when `softness` is absent. Resolving that
//    aliasing at the boundary keeps the painter from knowing the schema.
struct PaintStrokeParameters
{
    float SizePixels = 8.0f;    // [px]  - `size`, the nib width before the ribbon's own scaling
    float Opacity    = 1.0f;    // [0-1] - `opacity` / 100
    float Flow       = 0.9f;    // [0-1] - `flow` / 100
    float Grain      = 0.0f;    // [0-1] - `grain` ?? `bristleTx`, the tooth that bites alpha out of stamps
    float Scatter    = 0.0f;    // [0-1] - `scatter` ?? `spread`, the per-stamp positional jitter
    float Softness   = 0.0f;    // [0-1] - `softness` ?? (1 - `hardness`), the dab's radial falloff
};


// 📝 What the column needs beyond geometry. The swatch index lives here rather than in the caller's parameter set because it is not a
//    schema parameter — the prototype keeps `SwatchIndex` as its own global, reset per instrument.
struct PaintPreviewState
{
    int SwatchIndex = 0;        // [idx] - which of the family's 1..5 inks the preview paints with
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Resolve the ink the preview paints with: the active family's swatch at the state's index, falling back to the prototype's own
// "#101014" when a family somehow carries none.
// 📝 Exposed because the swatch chips and both canvases must agree on the colour, and re-deriving it at each site is how they drift.
[[nodiscard]] ImU32 ResolvePaintPreviewInk(int InstrumentIndex, const PaintPreviewState& State, ImU32 Fallback);

// Draw the stroke ribbon: 220 stamps along an eased sine, under a pressure envelope, with grain and scatter applied per stamp.
// 🔴 Takes the instrument index and the swatch index as the SEED basis, so the speckle is stable across frames.
void ConstructPaintStrokeRibbon(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                const PaintStrokeParameters& Parameters, ImU32 Ink,
                                int InstrumentIndex, int SwatchIndex,
                                ImVec2 StripMinimum, float StripWidth);

// Draw the single dab: one circle under the same opacity and flow, radially faded when softness is above the prototype's 0.02 floor,
// then bitten by `round(Grain * 240)` clear specks.
void ConstructPaintDab(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                       const PaintStrokeParameters& Parameters, ImU32 Ink,
                       int InstrumentIndex, int SwatchIndex, ImVec2 WellMinimum);

// Draw the family's ink chips and report which was clicked, or -1. Hover grows a chip by 1.12 about its own centre.
[[nodiscard]] int ConstructPaintSwatchStrip(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                            int InstrumentIndex, int SelectedSwatchIndex,
                                            ImVec2 StripMinimum, float StripWidth);

// Draw the standing instrument: the landscape strip rotated -90 degrees and grown 1.5x inside a 96 px well.
// 🔴 Rotated, so this cannot use AddImage — the quad is built by hand. Falls back to the nib crop when the strip has not uploaded.
void ConstructPaintInstrumentStand(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                   SvgIconRegistry* Registry, PaintIconStore* StripStore,
                                   int InstrumentIndex, ImVec2 StandMinimum, float StandWidth);

// Draw the whole left column for slide 2, in the prototype's order, and report a clicked swatch index or -1.
[[nodiscard]] int ConstructPaintPreviewColumn(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                              const PaintPaneRegion& Region, const PaintPreviewState& State,
                                              const PaintStrokeParameters& Parameters,
                                              SvgIconRegistry* Registry, PaintIconStore* StripStore,
                                              int InstrumentIndex, int VisibleGroupCount, int ParameterCount);

} // namespace Frontier

#endif
