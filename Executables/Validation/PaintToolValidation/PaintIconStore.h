/*==============================================================================================================================================
                                                            PAINTICONSTORE.H
==============================================================================================================================================*/
// 🧩 The paint card's non-square texture store: rasterizes an instrument's authored 5:1 landscape strip at its true aspect and uploads it as a
//    Vulkan texture ImGui can sample. One store serves the single draw site that shows a whole instrument — the standing preview, which draws the
//    strip rotated -90° so a 300x60 barrel reads as an upright pen.
//
//    🔴 Why this exists at all, rather than another RegisterSvgIcon call: SvgIconRegistry is square-only by contract. Its rasterizer calls
//       Picture::size(Edge, Edge), which STRETCHES any viewBox to a square, and its image/copy extents are {Edge, Edge, 1}. Handing a 300x60
//       document to it yields a 5x vertically squashed instrument, silently — a wrong picture, not a failure. The paint card is the first surface
//       that needs a rectangle, and this app is validation, so the rectangle lives HERE and the shared registry keeps its square contract intact.
//       Nothing in Internal/ is touched, so the modelling and drafting cards cannot be perturbed.
//
//    📝 Lazily populated. The strip has exactly one draw site and only the selected instrument is ever previewed, so uploading all 102 up front
//       would spend 102 descriptor sets and ~10 MiB to show one. A strip is rasterized the first time it is asked for and then cached forever.

#pragma once
#ifndef FRONTIER_VALIDATION_PAINTTOOL_PAINTICONSTORE_H
#define FRONTIER_VALIDATION_PAINTTOOL_PAINTICONSTORE_H

#include <cstdint>
#include <unordered_map>

#include <vulkan/vulkan.h>

#include "imgui.h"

namespace Frontier
{

struct VulkanHost;

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One uploaded strip. Width and height are carried separately — that is the whole point of this store — so a draw site can
//    size its quad from the texture's real aspect instead of assuming a square.
struct PaintStripTexture
{
    VkImage         Image         = VK_NULL_HANDLE;   // [-]  - device image holding the RGBA8 texels
    VkDeviceMemory  ImageMemory   = VK_NULL_HANDLE;   // [-]  - backing allocation for Image
    VkImageView     ImageView     = VK_NULL_HANDLE;   // [-]  - view bound into the descriptor set
    VkSampler       Sampler       = VK_NULL_HANDLE;   // [-]  - linear clamp sampler
    VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;   // [-]  - ImGui_ImplVulkan_AddTexture result
    ImTextureID     StripTextureId = 0;               // [-]  - DescriptorSet as an ImTextureID for ImGui::Image
    uint32_t        PixelWidth    = 0u;               // [px] - raster width  (the long axis, ~320)
    uint32_t        PixelHeight   = 0u;               // [px] - raster height (the short axis, ~64)
};

// 📝 The store. Keyed by catalogue instrument index rather than by label: two instruments can carry the same display name in
//    different families, and a colliding key would rebind the first one's texture. Holds the host by pointer for teardown; the
//    host must outlive the store.
struct PaintIconStore
{
    VulkanHost*                                    Host = nullptr;   // [-] - device handles; borrowed, not owned
    std::unordered_map<int, PaintStripTexture>     IndexToStrip;     // [-] - instrument index -> uploaded strip
    uint32_t                                       StripShortEdge = 0u; // [px] - the short-axis raster height every strip uses
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bind the store to a Vulkan host and take a reference on the SVG engine. Must precede any Resolve call. ShortEdgePixels is the
// raster height every strip is rendered at; its width follows from the document's own aspect. Returns false when the SVG engine
// could not start.
bool InitializePaintIconStore(PaintIconStore& Store, VulkanHost& Host, uint32_t ShortEdgePixels);

// Resolve an instrument's strip texture, rasterizing and uploading it on first request. Returns null when the index is out of
// range or the raster/upload failed; a caller that gets null should fall back to the square nib art rather than draw nothing.
// 📝 Not const: this is the lazy-population entry point. Call it outside a frame's tight inner loops — the first call for an
//    instrument blocks on a fence while the transfer completes, which is a one-off cost per instrument previewed.
const PaintStripTexture* ResolvePaintStrip(PaintIconStore& Store, int InstrumentIndex);

// Destroy every uploaded strip and release the SVG engine reference. Leaves the store empty and re-initializable. The device is
// waited idle first so no in-flight frame still references a texture.
void FinalizePaintIconStore(PaintIconStore& Store);

} // namespace Frontier

#endif
