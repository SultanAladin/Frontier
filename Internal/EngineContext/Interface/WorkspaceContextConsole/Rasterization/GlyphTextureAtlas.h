/*==============================================================================================================================================
                                                          GLYPHTEXTUREATLAS.H
==============================================================================================================================================*/
// 🧩 The console's non-square texture store: rasterizes an authored landscape strip at its TRUE aspect and uploads it as a Vulkan texture ImGui can
//    sample. Folds in PaintToolValidation's PaintIconStore, the store that exists because SvgIconRegistry is square-only by contract — its
//    rasterizer calls Picture::size(Edge, Edge), which STRETCHES any viewBox to a square, so a 300x60 document handed to it comes out 5x squashed,
//    silently. Any console workspace that previews a whole instrument (paint's standing preview draws a 300x60 barrel upright) needs a rectangle,
//    so the rectangle lives here, shared, and the square registry keeps its square contract intact — nothing in Icons/ is perturbed.
//
//    📝 Lazily populated and cached forever. A strip has one draw site and only the selected item is previewed, so uploading all up front would
//       spend a descriptor set and megabytes per item to show one. A strip is rasterized the first time it is asked for and then kept.
//    🔴 Keyed by the caller's own item index, not by label: two items can carry the same display name in different families, and a colliding key
//       would rebind the first one's texture. The workspace supplies the document bytes per index; the atlas does not know the catalogue.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_RASTERIZATION_GLYPHTEXTUREATLAS_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_RASTERIZATION_GLYPHTEXTUREATLAS_H

#include <cstdint>
#include <unordered_map>

#include <vulkan/vulkan.h>

#include "imgui.h"

namespace Frontier
{

struct VulkanHost;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One uploaded strip. Width and height are carried separately — that is the whole point of this store — so a draw site sizes its quad from the
//    texture's real aspect instead of assuming a square.
struct GlyphStripTexture
{
    VkImage         Image          = VK_NULL_HANDLE;   // [-]  - device image holding the RGBA8 texels
    VkDeviceMemory  ImageMemory    = VK_NULL_HANDLE;   // [-]  - backing allocation for Image
    VkImageView     ImageView      = VK_NULL_HANDLE;   // [-]  - view bound into the descriptor set
    VkSampler       Sampler        = VK_NULL_HANDLE;   // [-]  - linear clamp sampler
    VkDescriptorSet DescriptorSet  = VK_NULL_HANDLE;   // [-]  - ImGui_ImplVulkan_AddTexture result
    ImTextureID     StripTextureId = 0;                // [-]  - DescriptorSet as an ImTextureID for ImGui::Image
    uint32_t        PixelWidth     = 0u;               // [px] - raster width  (the long axis)
    uint32_t        PixelHeight    = 0u;               // [px] - raster height (the short axis)
};


// 📝 The store. Holds the host by pointer for teardown; the host must outlive the store. StripShortEdge is the short-axis raster height every strip
//    uses; a strip's width follows from its document's own aspect.
struct GlyphTextureAtlas
{
    VulkanHost*                                     Host = nullptr;      // [-] - device handles; borrowed, not owned
    std::unordered_map<int, GlyphStripTexture>      IndexToStrip;        // [-] - item index -> uploaded strip
    uint32_t                                        StripShortEdge = 0u; // [px] - the short-axis raster height every strip uses
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bind the atlas to a Vulkan host and take a reference on the SVG engine. Must precede any Resolve call. ShortEdgePixels is the raster height every
// strip is rendered at; its width follows from the document's aspect. Returns false when the SVG engine could not start.
bool InitializeGlyphTextureAtlas(GlyphTextureAtlas& Atlas, VulkanHost& Host, uint32_t ShortEdgePixels);

// Resolve an item's strip texture, rasterizing and uploading it on first request from the caller-supplied document. Returns null when the raster or
// upload failed; a caller that gets null should fall back to the square glyph art rather than draw nothing.
// 📝 Not const: this is the lazy-population entry point. Call it outside a frame's tight inner loops — the first call for an item blocks on a fence
//    while the transfer completes, a one-off cost per item previewed.
const GlyphStripTexture* ResolveGlyphStrip(GlyphTextureAtlas& Atlas, int ItemIndex, const char* SvgDocument);

// Destroy every uploaded strip and release the SVG engine reference. Leaves the atlas empty and re-initializable. The device is waited idle first
// so no in-flight frame still references a texture.
void FinalizeGlyphTextureAtlas(GlyphTextureAtlas& Atlas);

} // namespace Frontier

#endif
