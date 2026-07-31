/*==============================================================================================================================================
                                                            POLYGONGLYPHTABLE.H
==============================================================================================================================================*/
// 🧩 The stroke paths that draw each PolygonGlyph. Every glyph is a short run of line segments and circles in a 0-1 unit square, resolved to
//    pixels by the generic inscription unit — so one definition draws at any row height and on any ImGui backend (the engine's SvgIconRegistry is
//    Vulkan-bound and unusable from a Direct3D validation host). The identities themselves live in PolygonGlyphIdentity, which carries no ImGui
//    dependency; this header is the drawing half, and only a renderer needs it.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_POLYGONMUTATION_POLYGONGLYPHTABLE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_POLYGONMUTATION_POLYGONGLYPHTABLE_H

#include "PolygonGlyphIdentity.h"
#include "../GlyphInscription.h"

namespace Frontier
{


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The stroke definition for one glyph, ready to hand to InscribeGlyph. Every identity resolves to something — an unmapped
//    glyph yields an empty path rather than a null, so a catalogue entry with a stale glyph draws blank instead of crashing.
[[nodiscard]] GlyphStrokeSet ResolvePolygonGlyph(PolygonGlyph Glyph);

// 📝 Draw one polygon glyph centred in a square box of the given side, in the given tint. Thin convenience over
//    ResolvePolygonGlyph + InscribeGlyph, so a menu row records an icon in one call.
void InscribePolygonGlyph(ImDrawList* DrawList, PolygonGlyph Glyph, ImVec2 BoxOrigin, float BoxSide, ImU32 Tint, float StrokeWidth);

}   // namespace Frontier

#endif
