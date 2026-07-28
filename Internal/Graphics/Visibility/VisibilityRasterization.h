/*==============================================================================================================================================
                                                          VISIBILITYRASTERIZATION.H
==============================================================================================================================================*/
// 🧩 The hardware visibility raster: draws an instanced Suzanne scene into the R32_UINT visibility buffer (VisibilityImage) with hardware depth
//    testing against the paired D32 target (VisibilityDepth), writing one packed (partition, primitive) identity per covered pixel. It owns the
//    graphics pipeline (stride-32 RenderVertex input, dynamic rendering with the R32_UINT colour + D32 depth formats, back-face cull, depth
//    LESS_OR_EQUAL write) and one per-instance storage buffer (the uploaded SuzanneSceneInstance list, bound at set 0). The mesh geometry is a
//    borrowed PolygonBufferAllocation (uploaded once via ConstructPolygonBufferAllocation) — the raster binds it, not owns it. The record path
//    opens its OWN colour(visibility)+depth dynamic-rendering scope, clears the id buffer to the empty sentinel and depth to the far plane, and
//    issues one instanced vkCmdDrawIndexed. Built once, host + shaders borrowed; the scene may be re-uploaded when the choice changes.

#pragma once
#ifndef FRONTIER_GRAPHICS_VISIBILITY_VISIBILITYRASTERIZATION_H
#define FRONTIER_GRAPHICS_VISIBILITY_VISIBILITYRASTERIZATION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Visibility/VisibilityImage.h"
#include "Graphics/Visibility/VisibilityDepth.h"
#include "Graphics/Render/Resources/BufferAllocation.h"
#include "Graphics/Scene/SuzanneScene.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block the vertex stage reads: the world -> clip matrix for the current camera, a flat column-major float[16] (matches Matrix4f's
//    Column[c][r] with index col*4 + row, no transpose). Filled each frame from the orbit camera before recording.
struct VisibilityRasterConstants
{
    float ViewProjection[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };   // [-] - column-major world -> clip
};

// 📝 The visibility raster's owned device resources. Pipeline + layout + the descriptor plumbing for the per-instance storage buffer; the storage
//    buffer itself is host-visible + mapped (written once per scene upload — the instance count is tiny). Mesh geometry is borrowed. ReadyCondition
//    gates recording: false leaves every handle null and the record path a no-op. InstanceCount is the second argument to vkCmdDrawIndexed.
struct VisibilityRasterization
{
    VulkanHost*           Host             = nullptr;          // [-] - not owned; supplies device / physical device / allocator
    VkPipeline            Pipeline         = VK_NULL_HANDLE;   // [-] - visibility raster graphics pipeline (dynamic rendering)
    VkPipelineLayout      PipelineLayout   = VK_NULL_HANDLE;   // [-] - one storage-buffer set + the ViewProjection push range
    VkDescriptorSetLayout SetLayout        = VK_NULL_HANDLE;   // [-] - set 0: binding 0 = instance storage buffer (vertex stage)
    VkDescriptorPool      DescriptorPool   = VK_NULL_HANDLE;   // [-] - pool sized for one instance set
    VkDescriptorSet       InstanceSet      = VK_NULL_HANDLE;   // [-] - the bound instance storage-buffer set
    VkBuffer              InstanceBuffer   = VK_NULL_HANDLE;   // [-] - host-visible storage buffer of SuzanneSceneInstance
    VkDeviceMemory        InstanceMemory   = VK_NULL_HANDLE;   // [-] - backing allocation for InstanceBuffer (host-visible, mapped)
    VkDeviceSize          InstanceCapacity = 0;                // [B] - allocated instance-buffer size
    uint32_t              InstanceCount    = 0;                // [-] - live instance count (vkCmdDrawIndexed instanceCount)
    bool                  ReadyCondition   = false;            // [-] - true once pipeline + layout + descriptors are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the pipeline, layout, descriptor set layout, pool, and a MaxInstances-capacity host-visible instance storage buffer. ColourFormat /
// DepthFormat are the visibility + depth target formats the dynamic-rendering pipeline is created against (VisibilityImageFormat /
// VisibilityDepthFormat). Reads VisibilityRaster.vert.spv / .frag.spv from ShaderDirectory. Returns false (ReadyCondition stays false) if dynamic
// rendering is unavailable, a shader is missing, or any Vulkan step fails. Pair with FinalizeVisibilityRasterization.
bool InitializeVisibilityRasterization(VisibilityRasterization& Raster,
                                       VulkanHost&              Host,
                                       VkFormat                 ColourFormat,
                                       VkFormat                 DepthFormat,
                                       uint32_t                 MaxInstances,
                                       const char*              ShaderDirectory);

// Upload an instance list into the mapped storage buffer and bind it to the descriptor set. Truncates to the buffer's MaxInstances capacity
// (logging the drop). Sets InstanceCount for the draw. A no-op when ReadyCondition is false. Call once per scene selection (cheap; safe to repeat).
void UploadVisibilityScene(VisibilityRasterization& Raster, const std::vector<SuzanneSceneInstance>& Instances);

// Record the visibility raster: open a colour(Image)+depth(Depth) dynamic-rendering scope, clear the id buffer to VisibilityEmptySentinel and
// depth to the far plane, bind the pipeline + instance set + the borrowed mesh's vertex/index buffers, and issue one instanced vkCmdDrawIndexed.
// Leaves Image in COLOR_ATTACHMENT and Depth in DEPTH_STENCIL_ATTACHMENT (both CurrentLayouts updated). A no-op when the raster, image, depth, or
// mesh is not ready / empty. CommandBuffer must be recording but OUTSIDE any active rendering scope. Constants supplies the current-frame camera.
void RecordVisibilityRasterization(VisibilityRasterization&         Raster,
                                   VisibilityImage&                 Image,
                                   VisibilityDepth&                 Depth,
                                   const PolygonBufferAllocation&   Mesh,
                                   const VisibilityRasterConstants& Constants,
                                   VkCommandBuffer                  CommandBuffer);

// Destroy the pipeline, layout, descriptor plumbing, and instance buffer, then reset to empty. The device must be idle. Safe on a never-initialized
// value. Does NOT free the borrowed mesh — the caller owns that.
void FinalizeVisibilityRasterization(VisibilityRasterization& Raster);

} // namespace Frontier

#endif
