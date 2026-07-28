/*==============================================================================================================================================
                                                                ICONPACKGLOBAL.H
==============================================================================================================================================*/
// 🧩 Registers the global icon tier ("g-" keys) into an SvgIconRegistry. These glyphs are shared by every outliner and chrome surface — a folder,
//    the eye visibility toggle, and the twisty chevrons — so they carry no CAD-specific meaning and are registered independently of any workspace.
//    The SVG document bytes are embedded string literals compiled into the binary, so registration needs no asset folder at runtime.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_ICONS_ICONPACKGLOBAL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_ICONS_ICONPACKGLOBAL_H

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Rasterize and upload every global glyph under its "g-" key into the registry at the registry's default edge. The registry must
// already be initialized against a Vulkan host. Returns false when any single glyph failed to rasterize or upload; the registry
// keeps whatever glyphs succeeded. Registering twice is idempotent (content-hash dedup rebinds each key to the same texture).
bool RegisterGlobalIconPack(SvgIconRegistry& Registry);

} // namespace Frontier

#endif
