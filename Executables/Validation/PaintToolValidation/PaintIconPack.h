/*==============================================================================================================================================
                                                           PAINTICONPACK.H
==============================================================================================================================================*/
// 🧩 The paint card's glyph tier: every parameter mark and every instrument's nib art, registered into an SvgIconRegistry under the "paint-" key
//    prefix. Ported 1:1 from the S/D/O/B stroke builders and the art factories in Documentation/Prototypes/PaintToolMenu.html.
//    ⚠️ GENERATED — see PaintIconPack.cpp.
//
//    🔴 This pack is STANDALONE, in the validation app rather than Internal/EngineContext/Interface/Icons/. The paint card needs a non-square
//       raster for one draw site and a per-crop key, neither of which the shared IconPackToolMenu / SvgIconRegistry pair does. Keeping it here
//       means the modelling and drafting cards that already ship against that pair cannot be perturbed by anything the paint card needs.

#pragma once
#ifndef FRONTIER_VALIDATION_PAINTTOOL_PAINTICONPACK_H
#define FRONTIER_VALIDATION_PAINTTOOL_PAINTICONPACK_H

#include <string>

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                           CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The single master raster edge every paint glyph and nib is uploaded at, then minified by the registry's linear sampler to
//    whichever draw size the site needs. Matches IconPackToolMenu's measured edge: a 1.4-wide stroke rasterized at the 13 px draw
//    size lands no fully-opaque texel, and coverage stops improving past this edge.
constexpr unsigned int PaintGlyphMasterEdge = 64u;

constexpr int PaintParameterGlyphCount = 31;

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Register every paint parameter glyph and every instrument's nib art into Registry. Each parameter mark registers twice —
// "paint-<name>" at the live ink and "paint-<name>-gated" at the faint — because the prototype recolours a dimmed row in CSS and a
// rasterized texture cannot be recoloured after upload. Returns false when any single registration failed.
bool RegisterPaintIconPack(SvgIconRegistry& Registry);

// Resolve a parameter glyph name ("ParamSize") to its registry key, honouring the gated variant. Central so no draw site
// hand-builds a key and silently misses on a typo.
[[nodiscard]] std::string ResolvePaintGlyphKey(const char* GlyphName, bool GatedCondition);

// Resolve an instrument's nib-art key from its index in the catalogue table. Indexed rather than named because two instruments can
// share a label across families, whereas the index is unique by construction.
[[nodiscard]] std::string ResolvePaintNibKey(int InstrumentIndex);

// Resolve an instrument's full-strip key. The strip is the 5:1 landscape art drawn rotated -90° at the preview site, and is
// registered lazily by the store rather than up front — see PaintIconStore.
[[nodiscard]] std::string ResolvePaintStripKey(int InstrumentIndex);

} // namespace Frontier

#endif
