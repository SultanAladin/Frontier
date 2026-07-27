/*==============================================================================================================================================
                                                    PARAMETRICSKETCHSURFACEINSCRIPTION.H
==============================================================================================================================================*/
// 🧩 The lean chrome-matcap surface inscription for the parametric-sketch solid preview: a purpose-built Vulkan graphics pipeline that shades a
//    loft body with ONE lighting model — a matcap disc lookup — and nothing else (no lights, no GI, no PBR). It owns exactly TWO descriptor sets
//    built once (set 0 a per-frame camera UBO, set 1 the chrome matcap sampler), a stride-32 RenderVertex input, and depth test ON against the
//    ParametricSketchViewTarget's render pass. Modular, not standalone — the pipeline / layouts are built once at bring-up; RecordParametric-
//    SketchSurfaceInto composites ONE body per call and the caller loops for many, so extrude / revolve / sweep reuse it unchanged. Raw Vulkan,
//    no VMA (shader-module load, set-layout, pipeline layout, graphics pipeline, descriptor pool + set). Geometry is uploaded by the caller
//    through ConstructPolygonBufferAllocation. Wired onto the shared VulkanHost.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDER_SURFACE_PARAMETRICSKETCHSURFACEINSCRIPTION_H
#define FRONTIER_GRAPHICS_RENDER_SURFACE_PARAMETRICSKETCHSURFACEINSCRIPTION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Render/Resources/BufferAllocation.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The per-frame camera block bound at set 0 binding 0. Two column-major mat4s laid out exactly as ParametricSketchMatcap.vert declares them
//    (ViewProjection then ViewMatrix, index = col*4 + row, no transpose — matching CameraMatrix4). The caller fills these each frame from the
//    orbit CameraConfiguration (EvaluateProjectionMatrix · EvaluateViewMatrix for ViewProjection, EvaluateViewMatrix for ViewMatrix), casting
//    the doubles to float here at the GPU boundary.
struct ParametricSketchSurfaceCameraBlock
{
    float ViewProjection[16] = {};   // [-] - world -> clip (orbit camera), column-major
    float ViewMatrix[16]     = {};   // [-] - world -> camera (matcap normal-transform source), column-major
};

// 📝 One resident matcap surface pipeline: the two set layouts + pipeline layout + graphics pipeline built once, the two shader modules kept for
//    teardown, and a descriptor pool that vends BOTH the camera UBO set (host-visible, mapped, rewritten per frame) and the matcap sampler set
//    (written once at bring-up). The camera UBO is a single host-visible buffer this owns and maps persistently; the matcap image / sampler are
//    borrowed (owned by ParametricSketchMatcapTexture). ReadyStatus gates recording — a partial build leaves every handle null and the pane draws
//    its placeholder. Host is borrowed (not owned).
struct ParametricSketchSurfaceInscription
{
    VulkanHost*           Host              = nullptr;          // [-] - not owned; supplies device / queue / family / physical device

    VkShaderModule        VertModule        = VK_NULL_HANDLE;   // [-] - ParametricSketchMatcap.vert.spv
    VkShaderModule        FragModule        = VK_NULL_HANDLE;   // [-] - ParametricSketchMatcap.frag.spv

    VkDescriptorSetLayout CameraSetLayout   = VK_NULL_HANDLE;   // [-] - set 0: one uniform buffer, vertex stage
    VkDescriptorSetLayout MatcapSetLayout   = VK_NULL_HANDLE;   // [-] - set 1: one combined image sampler, fragment stage
    VkPipelineLayout      PipelineLayout    = VK_NULL_HANDLE;   // [-] - { CameraSetLayout, MatcapSetLayout }, no push ranges
    VkPipeline            Pipeline          = VK_NULL_HANDLE;   // [-] - the matcap graphics pipeline (depth test on)

    VkBuffer              CameraBuffer      = VK_NULL_HANDLE;   // [-] - host-visible UBO holding ParametricSketchSurfaceCameraBlock
    VkDeviceMemory        CameraMemory      = VK_NULL_HANDLE;   // [-] - backing allocation for CameraBuffer (persistently mapped)
    void*                 CameraMapped      = nullptr;          // [-] - persistent map of CameraMemory (write the block, no flush — coherent)

    VkDescriptorPool      DescriptorPool    = VK_NULL_HANDLE;   // [-] - vends the camera set + matcap set
    VkDescriptorSet       CameraSet         = VK_NULL_HANDLE;   // [-] - set 0 (bound to CameraBuffer)
    VkDescriptorSet       MatcapSet         = VK_NULL_HANDLE;   // [-] - set 1 (bound to the matcap view + sampler)

    bool                  ReadyStatus       = false;            // [-] - true once pipeline + both sets are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the matcap pipeline (both set layouts, pipeline layout, graphics pipeline against RenderPass subpass 0), the camera UBO + its set, and
// the matcap set bound to MatcapView / MatcapSampler. The pipeline expects a stride-32 RenderVertex vertex input (position @0, normal @12,
// texcoord @24) and depth test LESS_OR_EQUAL against ParametricSketchViewTarget's D32_SFLOAT depth. Returns false (ReadyStatus stays false, every
// handle null) on a missing .spv / any Vulkan failure — the caller logs and skips the preview. Host must be provisioned; RenderPass is the
// ParametricSketchViewTarget's RenderPass; MatcapView / MatcapSampler come from a ready ParametricSketchMatcapTexture. Pair with the Finalize below.
bool InitializeParametricSketchSurfaceInscription(ParametricSketchSurfaceInscription& Inscription,
                                                  VulkanHost&                         Host,
                                                  const char*                         VertSpvPath,
                                                  const char*                         FragSpvPath,
                                                  VkRenderPass                        RenderPass,
                                                  VkImageView                         MatcapView,
                                                  VkSampler                           MatcapSampler);

// Write Camera into the per-frame UBO (once per frame, before recording). No-op when not ready. Coherent memory — no flush needed.
void RefreshParametricSketchSurfaceCamera(ParametricSketchSurfaceInscription& Inscription, const ParametricSketchSurfaceCameraBlock& Camera);

// Record ONE loft body into the (already-begun) render pass on CommandBuffer: bind the pipeline + both sets, set the viewport / scissor to
// Width x Height, bind Body's vertex + index buffers, and draw its indices. The caller must have called vkCmdBeginRenderPass on the view target's
// framebuffer and RefreshParametricSketchSurfaceCamera this frame; the caller loops this over every visible body, then ends the pass. No-op when
// not ready or Body is empty (IndexCount == 0).
void RecordParametricSketchSurfaceInto(ParametricSketchSurfaceInscription& Inscription,
                                       VkCommandBuffer                     CommandBuffer,
                                       const PolygonBufferAllocation&      Body,
                                       uint32_t                            Width,
                                       uint32_t                            Height);

// Destroy the pipeline, layouts, shader modules, camera buffer, and descriptor pool, then reset to empty. The device must be idle. Safe on a
// never-initialized value.
void FinalizeParametricSketchSurfaceInscription(ParametricSketchSurfaceInscription& Inscription);

} // namespace Frontier

#endif
