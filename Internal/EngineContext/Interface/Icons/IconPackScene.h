/*==============================================================================================================================================
                                                                ICONPACKSCENE.H
==============================================================================================================================================*/
// 🧩 Registers the scene-outliner icon tier ("scene-" keys) into an SvgIconRegistry. These glyphs are the scene-directory vocabulary — the scene
//    root, a polygon mesh, the sun and area emitters, a camera, and an environment dome. They carry no CAD meaning, so they sit apart from the
//    "cad-" tier; folders reuse the shared global "g-folder" glyph. Every SVG is authored in the same house palette as the other tiers.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_ICONS_ICONPACKSCENE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_ICONS_ICONPACKSCENE_H

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Rasterize and upload every scene glyph under its "scene-" key into the registry at the registry's default edge. The registry
// must already be initialized against a Vulkan host. Returns false when any single glyph failed to rasterize or upload; the
// registry keeps whatever glyphs succeeded. Safe to call alongside RegisterGlobalIconPack — the two tiers share one registry and
// dedup against each other by content hash when a glyph happens to match.
bool RegisterSceneIconPack(SvgIconRegistry& Registry);

} // namespace Frontier

#endif
