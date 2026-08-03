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
    float    ViewProjection[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };   // [-] - column-major world -> clip
    uint32_t CullActive         = 0;                                        // [-] - 0 = plain draw (gl_InstanceIndex direct); 1 = remap through the survivor list
    uint32_t Pad0               = 0;                                        // [-] - std430 push tail pad
    uint32_t Pad1               = 0;                                        // [-] - std430 push tail pad
    uint32_t Pad2               = 0;                                        // [-] - std430 push tail pad
};

// 📝 The visibility raster's owned device resources. Pipeline + layout + the descriptor plumbing for the per-instance storage buffer; the storage
//    buffer itself is host-visible + mapped (written once per scene upload — the instance count is tiny). Mesh geometry is borrowed. ReadyCondition
//    gates recording: false leaves every handle null and the record path a no-op. InstanceCount is the second argument to vkCmdDrawIndexed.
struct VisibilityRasterization
{
    VulkanHost*           Host             = nullptr;          // [-] - not owned; supplies device / physical device / allocator
    VkPipeline            Pipeline         = VK_NULL_HANDLE;   // [-] - visibility raster graphics pipeline (dynamic rendering)
    VkPipelineLayout      PipelineLayout   = VK_NULL_HANDLE;   // [-] - one storage-buffer set + the ViewProjection push range
    VkDescriptorSetLayout SetLayout        = VK_NULL_HANDLE;   // [-] - set 0: b0 = instance storage buffer, b1 = survivor list (both vertex stage)
    VkDescriptorPool      DescriptorPool   = VK_NULL_HANDLE;   // [-] - pool sized for one instance set
    VkDescriptorSet       InstanceSet      = VK_NULL_HANDLE;   // [-] - the bound instance + survivor storage-buffer set
    VkBuffer              InstanceBuffer   = VK_NULL_HANDLE;   // [-] - host-visible storage buffer of SuzanneSceneInstance
    VkDeviceMemory        InstanceMemory   = VK_NULL_HANDLE;   // [-] - backing allocation for InstanceBuffer (host-visible, mapped)
    VkDeviceSize          InstanceCapacity = 0;                // [B] - allocated instance-buffer size
    VkBuffer              BoundSurvivorBuffer = VK_NULL_HANDLE; // [-] - the cull's survivor buffer bound at b1 (re-pointed on change; borrowed)
    uint32_t              InstanceCount    = 0;                // [-] - live instance count (plain-path vkCmdDrawIndexed instanceCount)
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
// Convenience wrapper for the single-mesh case: BeginVisibilityScope → DrawVisibilityMesh (plain) → EndVisibilityScope.
void RecordVisibilityRasterization(VisibilityRasterization&         Raster,
                                   VisibilityImage&                 Image,
                                   VisibilityDepth&                 Depth,
                                   const PolygonBufferAllocation&   Mesh,
                                   const VisibilityRasterConstants& Constants,
                                   VkCommandBuffer                  CommandBuffer);

//------------------------------------------------------------------------------------------------------------------------
//                                          MULTI-MESH SCOPE (the Unreal-shaped path: one clear, N meshes)
//------------------------------------------------------------------------------------------------------------------------

// Open the shared visibility scope ONCE: transition the id + depth images to their attachment layouts, begin the colour(Image)+depth(Depth)
// dynamic-rendering scope, CLEAR the id buffer to VisibilityEmptySentinel and depth to the far plane, and set the viewport / scissor. After this the
// caller issues one or more DrawVisibilityMesh calls (heads, floor, …), each depth-testing against the shared buffer so every mesh occludes and is
// occluded correctly, then closes with EndVisibilityScope. A no-op when the raster / image / depth is not ready. CommandBuffer must be recording,
// OUTSIDE any rendering scope. This is how a modern renderer fills one visibility buffer: the clear is a frame event, not a per-mesh event.
//   PreserveContents == false (default) CLEARs both id + depth, opening a fresh buffer — the ordinary first-fill of the frame.
//   PreserveContents == true instead LOADs both, re-opening a scope over id + depth a prior fill already wrote, so the meshes drawn here append and
//   depth-test against what is already there. This is the single primitive the two-pass late cull (append late survivors onto the early buffer) and
//   the software-raster floor (composite the hardware floor onto the compute-written id + depth) both stand on. The caller guarantees the images are
//   already in their attachment layouts from an earlier fill this frame — the layout barriers stay identical either way.
void BeginVisibilityScope(VisibilityRasterization& Raster,
                          VisibilityImage&         Image,
                          VisibilityDepth&         Depth,
                          VkCommandBuffer          CommandBuffer,
                          bool                     PreserveContents = false);

// Draw ONE mesh into the already-open shared scope: bind the pipeline, the given instance descriptor set, the borrowed mesh, push Constants, and issue
// the draw. Indirect == false issues vkCmdDrawIndexed of InstanceCount instances; Indirect == true issues vkCmdDrawIndexedIndirect from ArgumentBuffer
// (Constants.CullActive must be 1, the survivor buffer bound). InstanceSet lets a caller draw distinct meshes with distinct per-instance buffers
// through the shared pipeline layout (heads use Raster.InstanceSet; the floor its own set). A no-op when the mesh is empty / not ready. Must be called
// between BeginVisibilityScope and EndVisibilityScope.
//
// 📝 FirstIndex / IndexCount name a SUB-RANGE of Mesh's index buffer, which is what lets several meshes share one merged buffer (see
//    GeometryStreamConcatenation): each draws only the indices it contributed. The default IndexCount of 0 means "the whole buffer" — the
//    single-mesh case every existing caller wants — so a merged caller passes its placement's IndexOffset / IndexCount and nobody else changes.
//    The indices are already rebased at append time, so vertexOffset stays 0 rather than carrying the placement's VertexOffset; adding it here
//    would apply the rebase twice.
// ⚠️ Ignored entirely when Indirect is true: the argument buffer carries its own firstIndex / indexCount, and the cull that fills it is the only
//    thing that knows the survivor count. A merged sub-range under an indirect draw must be encoded into the argument buffer, not here.
void DrawVisibilityMesh(VisibilityRasterization&         Raster,
                        VkDescriptorSet                  InstanceSet,
                        const PolygonBufferAllocation&   Mesh,
                        uint32_t                         InstanceCount,
                        const VisibilityRasterConstants& Constants,
                        bool                             Indirect,
                        VkBuffer                         ArgumentBuffer,
                        VkCommandBuffer                  CommandBuffer,
                        uint32_t                         FirstIndex = 0u,
                        uint32_t                         IndexCount = 0u);

// Close the shared visibility scope opened by BeginVisibilityScope and record the resulting image / depth layouts (both left in their attachment
// layouts, matching the single-mesh path). Pair one EndVisibilityScope with each BeginVisibilityScope. A no-op when the raster / image / depth is not
// ready. CommandBuffer must be the same recording buffer the Begin used.
void EndVisibilityScope(VisibilityRasterization& Raster,
                        VisibilityImage&         Image,
                        VisibilityDepth&         Depth,
                        VkCommandBuffer          CommandBuffer);

// Point set 0 binding 1 at the GPU cull's survivor buffer so the indirect draw can remap gl_InstanceIndex through it. Bound once by the caller when
// the cull is built (and re-pointed only if the buffer changes — cached in BoundSurvivorBuffer). SurvivorBytes is the survivor buffer's whole size.
// A no-op when not ready or the buffer already matches. Must not run while a frame referencing InstanceSet is in flight (call at build / idle).
void BindVisibilitySurvivorBuffer(VisibilityRasterization& Raster, VkBuffer SurvivorBuffer, VkDeviceSize SurvivorBytes);

// Record the visibility raster with a GPU-driven indirect draw: identical scope / clear / bind to RecordVisibilityRasterization, but the instance
// count comes from ArgumentBuffer (the cull's VkDrawIndexedIndirectCommand) via vkCmdDrawIndexedIndirect, and the vertex stage remaps gl_InstanceIndex
// through the survivor list (Constants.CullActive must be 1). The survivor buffer must have been bound with BindVisibilitySurvivorBuffer, and both the
// argument + survivor buffers must be in the cull's post-dispatch barrier state. A no-op when the raster / image / depth / mesh is not ready.
void RecordVisibilityRasterizationIndirect(VisibilityRasterization&         Raster,
                                           VisibilityImage&                 Image,
                                           VisibilityDepth&                 Depth,
                                           const PolygonBufferAllocation&   Mesh,
                                           const VisibilityRasterConstants& Constants,
                                           VkBuffer                         ArgumentBuffer,
                                           VkCommandBuffer                  CommandBuffer);

// Destroy the pipeline, layout, descriptor plumbing, and instance buffer, then reset to empty. The device must be idle. Safe on a never-initialized
// value. Does NOT free the borrowed mesh — the caller owns that.
void FinalizeVisibilityRasterization(VisibilityRasterization& Raster);

} // namespace Frontier

#endif
