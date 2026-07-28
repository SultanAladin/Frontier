/*==============================================================================================================================================
                                                                SVGICONREGISTRY.H
==============================================================================================================================================*/
// 🧩 The keyed, content-hash-cached store of rasterized SVG icons as Vulkan textures ready for ImGui. Two tiers register into one registry: a
//    global tier (keys prefixed "g-", shared by every outliner and chrome surface) and a local CAD tier (keys prefixed "cad-", the parametric
//    sketch glyphs). Each icon is rasterized once by the SvgRasterizer, uploaded to a VkImage, and bound through ImGui_ImplVulkan_AddTexture to a
//    VkDescriptorSet used as the ImTextureID. A content hash keys the texture cache so two keys sharing identical bytes+size upload only once.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_ICONS_SVGICONREGISTRY_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_ICONS_SVGICONREGISTRY_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "imgui.h"

namespace Frontier
{

struct VulkanHost;

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One uploaded icon texture: the Vulkan image chain plus the ImGui descriptor set that names it as a texture. IconTextureId
//    is that descriptor set reinterpreted as an ImTextureID — the value a row hands to ImGui::Image. Several registry keys may
//    point at the same texture (deduplicated by content hash), so the texture owns a reference count and is torn down only when
//    the last key referencing it is dropped.
struct SvgIconTexture
{
    VkImage        Image          = VK_NULL_HANDLE;   // [-]  - device image holding the RGBA8 texels
    VkDeviceMemory ImageMemory    = VK_NULL_HANDLE;   // [-]  - backing allocation for Image
    VkImageView    ImageView      = VK_NULL_HANDLE;   // [-]  - view bound into the descriptor set
    VkSampler      Sampler        = VK_NULL_HANDLE;   // [-]  - linear clamp sampler
    VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;   // [-]  - ImGui_ImplVulkan_AddTexture result
    ImTextureID    IconTextureId  = 0;                // [-]  - DescriptorSet as an ImTextureID for ImGui::Image
    uint32_t       EdgePixels     = 0u;               // [px] - square texture edge
    uint32_t       ReferenceCount = 0u;               // [-]  - keys pointing at this texture; teardown at zero
};

// 📝 The registry. KeyToHash maps a stable icon key ("cad-datum-plane") to the content hash of the bytes+size it was registered
//    with; HashToTexture holds one uploaded texture per distinct content hash. The two-level indirection is what lets the global
//    and local tiers share a glyph (same bytes → same hash → one texture) while each keeps its own name. Holds the VulkanHost by
//    pointer for teardown; the host must outlive the registry.
struct SvgIconRegistry
{
    VulkanHost*                                       Host = nullptr;   // [-] - device handles; borrowed, not owned
    std::unordered_map<std::string, uint64_t>         KeyToHash;        // [-] - icon key -> content hash
    std::unordered_map<uint64_t, SvgIconTexture>      HashToTexture;    // [-] - content hash -> uploaded texture
    uint32_t                                          DefaultEdgePixels = 32u; // [px] - raster edge when a key omits one
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bind the registry to a Vulkan host and bring the SVG engine up. Must precede any Register call. The host provides the device,
// queue, and ImGui descriptor pool the uploads use. Returns false when the SVG engine could not start.
bool InitializeSvgIconRegistry(SvgIconRegistry& Registry, VulkanHost& Host);

// Rasterize IconKey's SVG source to EdgePixels (0 == the registry default) and upload it, or reuse an existing texture when the
// same bytes+size were already uploaded under any key (content-hash dedup). Re-registering an existing key rebinds it. Returns
// false when the raster or upload failed; the key is then left unbound and resolves to a null texture.
bool RegisterSvgIcon(SvgIconRegistry&   Registry,
                     const std::string& IconKey,
                     const char*        SvgByteSource,
                     uint32_t           SvgByteCount,
                     uint32_t           EdgePixels);

// Resolve a key to its ImGui texture id, or 0 when the key is unregistered / failed to upload. The terse form a row uses each
// paint; never uploads, never mutates the registry.
[[nodiscard]] ImTextureID ResolveIconTexture(const SvgIconRegistry& Registry, const std::string& IconKey);

// True when a key resolves to a live uploaded texture.
[[nodiscard]] bool IconRegistered(const SvgIconRegistry& Registry, const std::string& IconKey);

// Destroy every uploaded texture and release the SVG engine reference. Leaves the registry empty and re-initializable. The
// Vulkan device is waited idle before textures are destroyed so no in-flight frame references them.
void FinalizeSvgIconRegistry(SvgIconRegistry& Registry);

} // namespace Frontier

#endif
