/*==============================================================================================================================================
                                                     SHADOWPAGECLEARSUBMISSION.H
==============================================================================================================================================*/
// 🧩 S6 — the GPU pass that primes pages for the depth raster. Owns one compute pipeline (ShadowPageClear.comp), the descriptor set binding the atlas
//    image plus a render-list SSBO, and the per-image upload of that list. ShadowPageAtlas decides WHICH pages need redrawing; this unit carries that
//    decision to the device and writes ShadowPageClearIdentity into exactly those pages.
//
// 🔴 THE LIST IS THE POINT. Clearing the whole 64 MiB atlas would be CORRECT-LOOKING AND WRONG: it wipes every cached page, so every page returns stale,
//    S7 redraws all 1024 every image, and the cache the pool exists to provide is dead. The image would be identical — the cost shows up only as frame
//    time, which is why this is stated here rather than left to be noticed. A static scene must clear ZERO pages, and that is the gate to test.
//
// 🔴 THE CLEAR VALUE IS ShadowPageClearIdentity AND IS PUSHED FROM C++, NOT HARDCODED IN THE SHADER. It is simultaneously S7's imageAtomicMin identity;
//    two definitions that can drift would make a texel with "no caster" lose the min against real depth. One constant, one owner.
//
// ⚠️ Records OUTSIDE any rendering scope, with the atlas in VK_IMAGE_LAYOUT_GENERAL. Barriers its own writes against S7's atomics before returning, so
//    the caller does not have to know this pass wrote an image.
//
// 📝 Borrows the VulkanHost and the ShadowPageAtlas; owns the pipeline, descriptors, and the list buffer pair. Extent-independent — the atlas is a fixed
//    4096², unrelated to the swapchain, so nothing here rebuilds on resize.

#pragma once
#ifndef FRONTIER_GRAPHICS_SHADOW_SHADOWPAGECLEARSUBMISSION_H
#define FRONTIER_GRAPHICS_SHADOW_SHADOWPAGECLEARSUBMISSION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Shadow/ShadowPageAtlas.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 S6's push block, matching ShadowPageClear.comp's ClearConstants field-for-field. All five are plain uints, so std430 packs them contiguously and
//    there is no alignment subtlety of the kind ShadowMarkConstants documents.
struct ShadowPageClearConstants
{
    uint32_t PageCount      = 0;                              // [page]  - entries in the list; also the dispatch's z extent
    uint32_t PageResolution = ShadowPageResolution;            // [texel] - page edge
    uint32_t AtlasPageEdge  = ShadowPageAtlasPageEdge;         // [page]  - pages per atlas edge
    uint32_t ClearValue     = ShadowPageClearIdentity;         // [-]     - the atomic-min identity; see the header
    uint32_t PageCapacity   = ShadowPageCapacity;              // [page]  - containment bound for a list entry
};

// 📝 The clear pass's owned device state. The list buffer is sized for the worst case (every page needing a render at once) and never grows, so the
//    upload is a memcpy of PageCount entries into a persistently mapped staging buffer plus one copy.
struct ShadowPageClearSubmission
{
    VulkanHost*           Host            = nullptr;          // [-] - not owned
    VkDescriptorSetLayout SetLayout       = VK_NULL_HANDLE;    // [-] - binding 0 = atlas storage image, 1 = render list
    VkDescriptorPool      DescriptorPool  = VK_NULL_HANDLE;    // [-] - sized for one set
    VkDescriptorSet       ClearSet        = VK_NULL_HANDLE;    // [-] - the one set this pass binds
    VkPipelineLayout      ClearLayout     = VK_NULL_HANDLE;    // [-] - set + ShadowPageClearConstants push range
    VkPipeline            ClearPipeline   = VK_NULL_HANDLE;    // [-] - ShadowPageClear.comp

    VkBuffer              ListBuffer      = VK_NULL_HANDLE;    // [-] - device-local SSBO; ShadowPageCapacity uints
    VkDeviceMemory        ListMemory      = VK_NULL_HANDLE;    // [-] - backing allocation
    VkBuffer              ListStaging     = VK_NULL_HANDLE;    // [-] - host-visible upload source
    VkDeviceMemory        ListStagingMemory = VK_NULL_HANDLE;  // [-] - backing allocation
    void*                 ListStagingMapping = nullptr;        // [-] - persistently mapped; coherent, no explicit flush

    uint32_t              LastClearedCount = 0;                // [page] - pages cleared on the most recent record; 0 on a static scene
    bool                  ReadyCondition   = false;            // [-] - true once pipeline + descriptors + list buffers are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the set layout, pipeline, pool + one set, and the list buffer pair, pointing binding 0 at the atlas's storage view. ShaderDirectory locates
// ShadowPageClear.comp.spv. The atlas must already be initialized (its storage view is baked into the descriptor here). Returns false with
// ReadyCondition false and handles null on any failure — the caller degrades to no shadows rather than failing outright.
bool InitializeShadowPageClearSubmission(ShadowPageClearSubmission& Clear,
                                         VulkanHost&                Host,
                                         const ShadowPageAtlas&     Atlas,
                                         const char*                ShaderDirectory);

// S6: clear every page ShadowPageNeedsRender selects, and nothing else. Builds the list from the atlas's records, uploads it, and dispatches
// 4 x 4 x PageCount workgroups (a 128-texel page is exactly 4 groups of 32 across).
// 🔴 The atlas must be in VK_IMAGE_LAYOUT_GENERAL and the command buffer must be OUTSIDE any rendering scope. Returns the number of pages cleared —
//    ZERO on a static scene, which is the correctness signal, not an idle result.
// ⚠️ Does NOT clear ContentStale. S7 owns MarkShadowPageRendered; a page cleared but not yet rasterized still holds wrong depth (it holds NO depth), so
//    declaring it rendered here would be the silent-wrong-shadow bug with an extra step.
uint32_t RecordShadowPageClear(ShadowPageClearSubmission& Clear, const ShadowPageAtlas& Atlas, VkCommandBuffer CommandBuffer);

// Destroy the pipeline, layout, descriptors and list buffers, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeShadowPageClearSubmission(ShadowPageClearSubmission& Clear);

} // namespace Frontier

#endif
