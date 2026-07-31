/*==============================================================================================================================================
                                                            ICONPACKTOOLMENU.H
==============================================================================================================================================*/
// 🧩 The embedded tool-menu glyph tier: every band, tool, parameter and stratum-badge mark the modelling / drafting / texture-paint tool cards
//    draw, as SVG documents registered under the "tool-" key prefix. Ported 1:1 from the S/D/F/O/B stroke builders in
//    Documentation/Prototypes/ModellingToolMenu.html, so the rasterized art is the prototype's art rather than a lookalike.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_ICONS_ICONPACKTOOLMENU_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_ICONS_ICONPACKTOOLMENU_H

#include <string>

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                           CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The single master raster edge every tool glyph is uploaded at, then minified by the registry's linear sampler to whichever
//    of the card's seven draw sizes the site needs. Rasterizing at the draw size instead is measurably worse: a 1.4-wide stroke
//    at 13 px lands NO fully-opaque texel, so the mark reads as a smudge. Coverage converges by this edge — the opaque fraction
//    matches a 128 px reference to within 0.2% — so further pixels buy nothing but memory.
constexpr unsigned int ToolGlyphMasterEdge = 64u;

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Register every tool-menu glyph and stratum badge into Registry. Each mark registers twice — "tool-<name>" at the live ink and
// "tool-<name>-gated" at the faint — because a gated tile in the prototype is the same art recoloured by CSS, and a rasterized
// texture cannot be recoloured after upload. Returns false when any single registration failed.
bool RegisterToolMenuIconPack(SvgIconRegistry& Registry);

// Resolve a glyph name ("PrimitiveBox") to its registry key, honouring the gated variant. Central so no draw site hand-builds a
// key and silently misses on a typo.
[[nodiscard]] std::string ResolveToolGlyphKey(const char* GlyphName, bool GatedCondition);

} // namespace Frontier

#endif
