/*==============================================================================================================================================
                                                          GLYPHTEXTUREATLAS.CPP
==============================================================================================================================================*/
// 🧩 Deferred stub. The non-square strip store previews a WHOLE authored instrument (paint's 300x60 barrel drawn upright), and no console workspace
//    proved so far draws one — the construction catalogue and the modelling grid both draw square glyphs, which the placeholder InscribeConsoleGlyph
//    already covers without any texture upload. So the three entry points are honest no-ops for now: Initialize reports it did not start, Resolve
//    returns null so every caller takes its documented square-glyph fallback, and Finalize has nothing to release. The full rasterize-and-upload body
//    (folding in PaintToolValidation's PaintIconStore over the SVG engine + Vulkan host) lands with the paint migration, when a strip preview first
//    has a draw site — wiring the SVG engine and a descriptor set here before then would drag thorvg + Vulkan into a module nothing yet asks it for.

#include "GlyphTextureAtlas.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeGlyphTextureAtlas(GlyphTextureAtlas& Atlas, VulkanHost& Host, uint32_t ShortEdgePixels)
{
    // Record the intent (host + short edge) so a later real init reads consistent state, but start nothing: no SVG engine reference is taken and no
    // strip is uploaded. Reporting false is the contract's "could not start" — a caller keeps drawing square glyphs, which is exactly the intent here.
    Atlas.Host           = &Host;
    Atlas.StripShortEdge = ShortEdgePixels;
    Atlas.IndexToStrip.clear();
    return false;
}


const GlyphStripTexture* ResolveGlyphStrip(GlyphTextureAtlas& Atlas, int ItemIndex, const char* SvgDocument)
{
    // Null is the documented "raster or upload failed" answer, and every caller falls back to the square glyph art on it. Until the store is wired,
    // that fallback IS the behaviour, so a strip is never rasterized and null is always returned.
    (void)Atlas;
    (void)ItemIndex;
    (void)SvgDocument;
    return nullptr;
}


void FinalizeGlyphTextureAtlas(GlyphTextureAtlas& Atlas)
{
    // Nothing was ever uploaded, so there is nothing to wait idle for or destroy; just return the atlas to its empty, re-initializable state.
    Atlas.IndexToStrip.clear();
    Atlas.Host           = nullptr;
    Atlas.StripShortEdge = 0u;
}

} // namespace Frontier
