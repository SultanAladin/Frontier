/*==============================================================================================================================================
                                                                ICONPACKCAD.H
==============================================================================================================================================*/
// 🧩 Registers the local CAD icon tier ("cad-" keys) into an SvgIconRegistry. These fourteen glyphs are the parametric-sketch vocabulary — the
//    document root, the three datum primitives and a coordinate frame, sketch profiles and curves, geometric and dimension constraints, a feature
//    operation, a component instance and its mate, and the two draughting glyphs. Every key here matches one authored in SketchClassificationRegistry.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_ICONS_ICONPACKCAD_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_ICONS_ICONPACKCAD_H

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Rasterize and upload every CAD glyph under its "cad-" key into the registry at the registry's default edge. The registry must
// already be initialized against a Vulkan host. Returns false when any single glyph failed to rasterize or upload; the registry
// keeps whatever glyphs succeeded. Safe to call alongside RegisterGlobalIconPack — the two tiers share the same registry and
// dedup against each other by content hash when a glyph happens to match.
bool RegisterCadIconPack(SvgIconRegistry& Registry);

} // namespace Frontier

#endif
