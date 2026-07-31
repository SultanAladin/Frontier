/*==============================================================================================================================================
                                                          SOFTWARERASTERIZATION.H
==============================================================================================================================================*/
// 🧩 The software micro-raster of the hybrid visibility path (PLAN §6): a compute pass that rasterizes the instanced Suzanne scene into the R64
//    packed (depth|id) target VisibilityImage owns, followed by a fullscreen graphics resolve that copies that packed word out into the exact same
//    R32_UINT visibility buffer + D32 depth target the hardware raster fills. It exists to win the micro-triangle case Burns & Hunt / Nanite call
//    out — most sub-pixel triangles cost less through a compute edge-function walk than through the fixed-function setup — and it is A/B-verifiable
//    against the hardware raster because both write the identical (partition, primitive) identity pack, so the resolve (and everything downstream:
//    the inscription composite, the HiZ pyramid) is agnostic to which path produced the frame.
//
//    OWNERSHIP. Two pipelines: a compute pipeline (SoftwareRasterization.comp — one of two route variants, image or buffer) that atomicMaxes the
//    packed word, and a graphics pipeline (VisibilityInscription.vert fullscreen triangle + SoftwareResolve.frag — the matching route variant) that
//    unpacks it. One descriptor set feeds the compute raster (instances, survivors, vertex/index SSBOs, packed target); one feeds the resolve (the
//    packed target). The instance + survivor buffers are BORROWED from the hardware raster / cull so the two paths draw byte-identical scenes; the
//    vertex/index buffers are the BORROWED mesh (BufferAllocation added STORAGE usage for this). The packed target is BORROWED from VisibilityImage.
//    Gated: a device without int64 atomics builds nothing (ReadyCondition stays false) and the caller keeps the software path off. POD + free
//    functions; borrows the host and every scene resource, owns only its two pipelines + descriptor plumbing + push constants.

#pragma once
#ifndef FRONTIER_GRAPHICS_VISIBILITY_SOFTWARERASTERIZATION_H
#define FRONTIER_GRAPHICS_VISIBILITY_SOFTWARERASTERIZATION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Visibility/VisibilityImage.h"
#include "Graphics/Visibility/VisibilityDepth.h"
#include "Graphics/Render/Resources/BufferAllocation.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The software raster's flat workgroup lane count — must match local_size_x * local_size_y in SoftwareRasterization.comp (8 * 8). One lane per
//    triangle; the dispatch rounds the (instance * triangle) count up to this multiple.
constexpr uint32_t SoftwareRasterWorkgroupLanes = 64;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block the compute raster reads, matching SoftwareRasterization.comp's SoftwareRasterConstants (std430 push layout): the world -> clip
//    matrix, the target extent for the clip -> screen map, the instance + per-instance triangle counts that bound the dispatch, and the survivor-remap
//    selector. Filled each frame from the camera + scene before recording.
struct SoftwareRasterConstants
{
    float    ViewProjection[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };   // [-]  - column-major world -> clip
    float    ViewportExtentX    = 0.0f;                                     // [px] - target width for clip -> screen
    float    ViewportExtentY    = 0.0f;                                     // [px] - target height for clip -> screen
    uint32_t InstanceCount      = 0;                                        // [-]  - instances the dispatch spans (survivor count when culling)
    uint32_t TriangleCount      = 0;                                        // [-]  - triangles per instance (index count / 3)
    uint32_t CullActive         = 0;                                        // [-]  - 0 = slot IS the instance index; 1 = remap through survivors
    uint32_t Pad0               = 0;                                        // [-]  - std430 push tail pad
    uint32_t Pad1               = 0;                                        // [-]  - std430 push tail pad
    uint32_t Pad2               = 0;                                        // [-]  - std430 push tail pad
};

// 📝 The push block the resolve fragment stage reads, matching SoftwareResolve.frag's SoftwareResolveConstants: the target extent (the buffer route's
//    linear-index base; the image route ignores it). Filled from the target dimensions before the resolve draw.
struct SoftwareResolveConstants
{
    uint32_t TargetExtentX = 0;   // [px] - target width for the buffer route's linear index
    uint32_t TargetExtentY = 0;   // [px] - target height
    uint32_t Pad0          = 0;   // [-]  - push tail pad
    uint32_t Pad1          = 0;   // [-]  - push tail pad
};

// 📝 The software raster's owned device resources. Two pipelines (compute raster + graphics resolve) and their layouts + descriptor plumbing; the
//    route (image / buffer) is fixed at build from the host's int64-atomics features and picks which SPV variant loads and how the packed target
//    binds. BoundInstanceBuffer / BoundSurvivorBuffer / BoundMesh* / BoundPacked* cache what the descriptor sets currently point at so a rebind only
//    happens on change (scene re-upload, resize). ReadyCondition gates recording: false leaves both record paths a no-op.
struct SoftwareRasterization
{
    VulkanHost*           Host              = nullptr;          // [-] - not owned; supplies device / physical device / allocator
    VisibilityPackedRoute Route             = VisibilityPackedRoute::None; // [-] - image / buffer / none (fixed at build)

    VkPipeline            RasterPipeline    = VK_NULL_HANDLE;   // [-] - SoftwareRasterization.comp compute pipeline (route variant)
    VkPipelineLayout      RasterLayout      = VK_NULL_HANDLE;   // [-] - raster set layout + SoftwareRasterConstants push range
    VkDescriptorSetLayout RasterSetLayout   = VK_NULL_HANDLE;   // [-] - b0 instances, b1 survivors, b2 vertices, b3 indices, b4 packed target
    VkDescriptorSet       RasterSet         = VK_NULL_HANDLE;   // [-] - the bound raster set

    VkPipeline            ResolvePipeline   = VK_NULL_HANDLE;   // [-] - fullscreen resolve graphics pipeline (dynamic rendering, route variant)
    VkPipelineLayout      ResolveLayout     = VK_NULL_HANDLE;   // [-] - resolve set layout + SoftwareResolveConstants push range
    VkDescriptorSetLayout ResolveSetLayout  = VK_NULL_HANDLE;   // [-] - b0 packed target (read)
    VkDescriptorSet       ResolveSet        = VK_NULL_HANDLE;   // [-] - the bound resolve set

    VkDescriptorPool      DescriptorPool    = VK_NULL_HANDLE;   // [-] - sized for the two sets

    VkBuffer              BoundInstanceBuffer = VK_NULL_HANDLE; // [-] - instance SSBO bound at raster b0 (borrowed; re-pointed on change)
    VkBuffer              BoundSurvivorBuffer = VK_NULL_HANDLE; // [-] - survivor SSBO bound at raster b1 (borrowed)
    VkBuffer              BoundVertexBuffer   = VK_NULL_HANDLE; // [-] - mesh vertex SSBO bound at raster b2 (borrowed)
    VkBuffer              BoundIndexBuffer    = VK_NULL_HANDLE; // [-] - mesh index SSBO bound at raster b3 (borrowed)
    VkImageView           BoundPackedView     = VK_NULL_HANDLE; // [-] - packed storage-image view bound at raster b4 / resolve b0 (image route)
    VkBuffer              BoundPackedBuffer    = VK_NULL_HANDLE; // [-] - packed SSBO bound at raster b4 / resolve b0 (buffer route)

    bool                  ReadyCondition    = false;            // [-] - true once both pipelines + descriptors are live and a route exists
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build both pipelines (the compute raster + the fullscreen resolve), their layouts, descriptor set layouts, and the pool + two sets. The route is
// chosen from Host's int64-atomics feature flags (image preferred, buffer fallback); when neither is enabled this returns false and ReadyCondition
// stays false — the caller keeps the software path off. ColourFormat / DepthFormat are the resolve pipeline's dynamic-rendering attachment formats
// (VisibilityImageFormat / VisibilityDepthFormat). ShaderDirectory locates the four Software*.spv variants. Pair with FinalizeSoftwareRasterization.
bool InitializeSoftwareRasterization(SoftwareRasterization& Software,
                                     VulkanHost&            Host,
                                     VkFormat               ColourFormat,
                                     VkFormat               DepthFormat,
                                     const char*            ShaderDirectory);

// Point the raster set's scene bindings at the borrowed instance + survivor + mesh vertex/index buffers (b0..b3). Cheap and idempotent — only the
// bindings whose buffer changed are rewritten. Call once after the scene mesh + cull are built, and again whenever the mesh is re-uploaded. A no-op
// when not ready. Must not run while a frame referencing RasterSet is in flight (call at build / idle).
void BindSoftwareRasterScene(SoftwareRasterization& Software,
                             VkBuffer               InstanceBuffer,
                             VkBuffer               SurvivorBuffer,
                             VkBuffer               VertexBuffer,
                             VkBuffer               IndexBuffer);

// Point both sets' packed-target binding at the VisibilityImage's packed target for the built route (raster b4 = the write target, resolve b0 = the
// read source). Re-pointed on resize (the packed target is rebuilt then). A no-op when not ready, when the route disagrees with the image's route, or
// when the binding already matches. Must not run while a frame referencing either set is in flight (call at build / idle after a resize).
void BindSoftwarePackedTarget(SoftwareRasterization& Software, VisibilityImage& Image);

// Record the whole software path in one call: clear the packed target to the empty sentinel, transition it raster-writable, dispatch the compute
// raster (one lane per instance*triangle), barrier the raster's atomicMax writes before the resolve reads, then run the fullscreen resolve draw into
// Image (R32 id) + Depth (D32 via gl_FragDepth) — leaving both in their attachment layouts exactly as the hardware raster's scope does. Constants
// supplies the current-frame camera + dispatch bounds; ArgumentInstanceCount is the live instance/survivor count. A no-op when the software path,
// image, or depth is not ready. CommandBuffer must be recording, OUTSIDE any active rendering scope.
void RecordSoftwareRasterization(SoftwareRasterization&         Software,
                                 VisibilityImage&               Image,
                                 VisibilityDepth&               Depth,
                                 const SoftwareRasterConstants& Constants,
                                 VkCommandBuffer                CommandBuffer);

// Destroy both pipelines, their layouts, the descriptor plumbing, then reset to empty. The device must be idle. Safe on a never-initialized value.
// Does NOT free any borrowed buffer / packed target — the caller (hardware raster, cull, VisibilityImage) owns those.
void FinalizeSoftwareRasterization(SoftwareRasterization& Software);

} // namespace Frontier

#endif
