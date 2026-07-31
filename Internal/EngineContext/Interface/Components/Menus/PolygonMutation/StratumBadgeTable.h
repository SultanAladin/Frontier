/*==============================================================================================================================================
                                                            STRATUMBADGETABLE.H
==============================================================================================================================================*/
// 🧩 The header artwork for the Topology Action Menu — one small pictogram per TopologyStratum, drawn on its own black rounded tile. Separate from
//    PolygonGlyphTable because these are not row glyphs: a badge is TWO-TONE (a bright accent form over a dim supporting form) so a reader
//    distinguishes Edge Loop from Edge Ring at 26 px, whereas a row glyph is a single-tint silhouette that must read against three severity tints.
//    Expressing that in GlyphStrokeSet would mean widening every run with a tint field to serve seven shapes, so the badges draw themselves.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_POLYGONMUTATION_STRATUMBADGETABLE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_POLYGONMUTATION_STRATUMBADGETABLE_H

#include "imgui.h"

#include "SelectionPredicate.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Draw the badge for one stratum into a square box: black rounded tile, then the stratum's own two-tone pictogram. The tile is
//    part of the badge rather than the caller's job, because the artwork is authored against black and reads as mush over the
//    header fill without it. Safe against a null draw list and an out-of-range stratum (which draws the tile alone).
void InscribeStratumBadge(ImDrawList* DrawList, TopologyStratum Stratum, ImVec2 BoxOrigin, float BoxSide);

}   // namespace Frontier

#endif
