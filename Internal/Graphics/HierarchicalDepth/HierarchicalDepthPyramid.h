/*==============================================================================================================================================
                                                          HIERARCHICALDEPTHPYRAMID.H
==============================================================================================================================================*/
// 🧩 The hierarchical depth pyramid (HiZ): a full mip chain of R32_SFLOAT where each level holds the CONSERVATIVE-FARTHEST (max) depth of the
//    2x2 footprint below it, reducing the renderer-owned scene depth (VisibilityDepth) down to a 1x1 apex. It is the structure the GPU-driven
//    two-pass occlusion cull samples: a partition is occluded when its nearest depth is farther than the pyramid level covering its screen
//    footprint. Built once at a viewport extent (pipeline + descriptors + storage image + per-mip views), rebuilt on resize, reduced each frame
//    by one compute dispatch per mip. Wired onto VulkanHost (borrowed). At this phase there is no cull consumer yet — the pyramid is produced
//    and its apex is validation-checkable against a CPU max-reduce of the depth buffer, which is the P1 gate.

#pragma once
#ifndef FRONTIER_GRAPHICS_HIERARCHICALDEPTH_HIERARCHICALDEPTHPYRAMID_H
#define FRONTIER_GRAPHICS_HIERARCHICALDEPTH_HIERARCHICALDEPTHPYRAMID_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Visibility/VisibilityDepth.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The pyramid storage format. R32_SFLOAT matches the D32 depth's precision as a storage image (D32 itself is not storage-capable on Pascal),
//    so the reduce reads sampled depth and writes float storage without precision loss.
constexpr VkFormat HierarchicalDepthFormat = VK_FORMAT_R32_SFLOAT;

// The reduce compute shader's local workgroup edge (must match local_size_x/y in HierarchicalDepthReduce.comp).
constexpr uint32_t HierarchicalDepthWorkgroupEdge = 8;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The pyramid. One R32_SFLOAT image with a full mip chain, a per-mip storage view (compute write target) and a per-mip sampled view (read
//    source for the next level), the reduce pipeline + layout + descriptor pool + per-mip descriptor sets, and a point sampler. LevelCount is
//    floor(log2(max(W,H))) + 1. Host is borrowed. ReadyCondition gates recording. Rebuilt whenever the viewport extent changes.
struct HierarchicalDepthPyramid
{
    VulkanHost*                  Host            = nullptr;          // [-]  - not owned; supplies device / physical device / allocator

    VkImage                      PyramidImage    = VK_NULL_HANDLE;   // [-]  - R32_SFLOAT, STORAGE | SAMPLED, full mip chain, device-local
    VkDeviceMemory               PyramidMemory   = VK_NULL_HANDLE;   // [-]  - backing allocation for PyramidImage
    std::vector<VkImageView>     StorageViews;                       // [-]  - one single-mip storage view per level (compute write)
    std::vector<VkImageView>     SampledViews;                       // [-]  - one single-mip sampled view per level (read as next level's source)
    VkSampler                    PointSampler    = VK_NULL_HANDLE;   // [-]  - nearest / clamp; the reduce fetches exact texels

    VkDescriptorSetLayout        DescriptorLayout = VK_NULL_HANDLE;  // [-]  - { sampler2D source, storage image destination }
    VkPipelineLayout             PipelineLayout   = VK_NULL_HANDLE;  // [-]  - descriptor layout + ReduceConstants push range
    VkPipeline                   ReducePipeline   = VK_NULL_HANDLE;  // [-]  - the HierarchicalDepthReduce.comp compute pipeline
    VkDescriptorPool             DescriptorPool   = VK_NULL_HANDLE;  // [-]  - sized for one set per level
    std::vector<VkDescriptorSet> LevelSets;                          // [-]  - per-level { source = level-1 (or depth), destination = level }
    VkImageView                  BoundDepthView   = VK_NULL_HANDLE;  // [-]  - the external depth view LevelSets[0] binding 0 points at; the reduce rewrites only on change

    uint32_t                     Width           = 0;                // [px] - level-0 extent width
    uint32_t                     Height          = 0;                // [px] - level-0 extent height
    uint32_t                     LevelCount      = 0;                // [-]  - mip levels (floor(log2(max(W,H))) + 1)
    VkImageLayout                CurrentLayout   = VK_IMAGE_LAYOUT_UNDEFINED; // [-] - tracked layout across the whole mip chain
    bool                         ReadyCondition  = false;            // [-]  - true once image + views + pipeline + sets are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the reduce pipeline (size-independent) and the storage image / views / descriptor sets at Width x Height. ShaderDirectory locates the
// compiled HierarchicalDepthReduce.comp.spv. Returns false (ReadyCondition stays false, handles null) on any failure or a zero dimension.
// Host must be provisioned. Pair with FinalizeHierarchicalDepthPyramid.
bool InitializeHierarchicalDepthPyramid(HierarchicalDepthPyramid& Pyramid,
                                        VulkanHost&               Host,
                                        uint32_t                  Width,
                                        uint32_t                  Height,
                                        const char*               ShaderDirectory);

// Resize to Width x Height: tear down the image / views / descriptor sets and rebuild them (the pipeline + layout + pool persist where they
// can). A no-op when the extent already matches (returns true) or a zero dimension (returns false). The device must be idle.
bool ReconfigureHierarchicalDepthPyramid(HierarchicalDepthPyramid& Pyramid, uint32_t Width, uint32_t Height);

// Reduce the supplied scene depth into the pyramid: level 0 from the sampled depth, then each higher level from the one below, one compute
// dispatch per level with a storage-write→shader-read barrier between them. Depth must already be in SHADER_READ_ONLY (see
// TransitionVisibilityDepthForSampling). Leaves the whole pyramid in SHADER_READ_ONLY. A no-op when either side is not ready. Records into
// CommandBuffer OUTSIDE any active rendering scope (belongs in the substrate preamble).
void ReduceHierarchicalDepthPyramid(HierarchicalDepthPyramid& Pyramid, const VisibilityDepth& Depth, VkCommandBuffer CommandBuffer);

// Destroy the sets / pool / pipeline / layout / views / sampler / image / memory, then reset to empty. The device must be idle. Safe on a
// never-initialized value.
void FinalizeHierarchicalDepthPyramid(HierarchicalDepthPyramid& Pyramid);

} // namespace Frontier

#endif
