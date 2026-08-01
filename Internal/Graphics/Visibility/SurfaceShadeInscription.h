/*==============================================================================================================================================
                                                        SURFACESHADEINSCRIPTION.H
==============================================================================================================================================*/
// 🧩 The deferred SHADE of the visibility buffer — the pass that turns the packed (partition, primitive) ids into genuinely lit surfaces, replacing
//    the id-hash debug colour of VisibilityInscription. This is the second half of deferred texturing (Burns & Hunt 2013): the raster wrote only
//    identity, and everything a BRDF needs is reconstructed HERE from that identity rather than stored in a fat G-buffer. Per pixel: unpack the
//    identity, index the instance buffer by the partition ordinal to get the model transform + MaterialId, fetch that triangle's three vertices out
//    of the mesh SSBOs by the primitive ordinal, recover the barycentric weights by intersecting the pixel's view ray against the world-space
//    triangle, interpolate the normal, then shade through the SurfacePresetTable record and tonemap.
//
//    Interpolated (not face) normals are the load-bearing choice: a face normal makes Chrome a faceted mosaic, flattens clearcoat into per-polygon
//    fills, and degenerates Matcap to one texel per facet — the smooth-highlight materials are exactly the ones a flat normal destroys.
//
//    Shaped as a fullscreen-triangle FRAGMENT pass (cloning VisibilityInscription's plumbing) rather than a compute dispatch: it composites into the
//    substrate's already-open colour scope, so it needs no storage-image target and no extra layout transitions, and it inherits fixed-function
//    alpha-over blending — which is what lets the Glass preset read as transparent for free. Records inside that open scope, like GroundGridPass.
//    Built once; host, visibility image, and mesh buffers all BORROWED. Named for its mechanism (…Inscription = composites onto a target).

#pragma once
#ifndef FRONTIER_GRAPHICS_VISIBILITY_SURFACESHADEINSCRIPTION_H
#define FRONTIER_GRAPHICS_VISIBILITY_SURFACESHADEINSCRIPTION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Scene/SurfacePresetTable.h"
#include "Graphics/Visibility/VisibilityImage.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block the fragment stage reads, byte-compatible with the ShadeConstants block in SurfaceShade.comp. InverseViewProjection is what makes
//    the whole reconstruction possible: it turns this pixel's NDC position back into a world-space ray, and intersecting that ray with the unpacked
//    triangle yields the barycentric weights the raster never stored. CameraPosition is that ray's origin and also the BRDF's view vector.
//
//    CompositeFeatureMask overrides the Composite record's OWN mask at shade time, so toggling a lobe re-shades with no table re-upload. It applies
//    only to the Composite material; every other preset keeps the fixed mask it was built with.
struct SurfaceShadeConstants
{
    float    InverseViewProjection[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };  // [-] - column-major clip -> world
    float    CameraPosition[4]         = { 0,0,0,1 };                              // [-] - world-space eye (.w unused)
    float    LightDirection[4]         = { 0,0,1,0 };                              // [-] - world-space direction TOWARD the light (.w unused)
    uint32_t CompositeFeatureMask      = 0;                                        // [-] - SurfaceFeatureBit set, overrides the Composite record only
    uint32_t FloorPartitionBase        = 0;                                        // [-] - partition ordinals >= this belong to the floor mesh
    uint32_t FloorShadeEnabled         = 0;                                        // [-] - 1 shades the floor from its own buffers, 0 discards it (P6.3a)
};

// 🔴 FloorShadeEnabled is a CORRECTNESS gate, not a preference. The floor's three descriptors are always written — when no floor document loaded they
//    are aliased onto the heads' own buffers, because a descriptor that is merely left unwritten is undefined memory rather than a safely empty one
//    (the same trap ComponentOverlayInscription documents for its authored tables). Aliased, those bindings report a healthy non-zero length while
//    holding HEAD data, so a shader that trusted the length alone would reconstruct floor pixels from head triangles. Only this flag distinguishes
//    "the floor buffers are real" from "the floor buffers are a safe alias", so it must stay the single thing the shader branches on.

// 📝 The shade pass's owned device resources. Pipeline + layout, the descriptor plumbing for everything the reconstruction reads (the sampled
//    visibility image, the two mesh SSBOs, the instance SSBO, and the material table UBO), and a point sampler — the id must never be filtered, since
//    a blended identity is a different triangle, not a midpoint.
//
//    The visibility image, instance buffer, and mesh buffers are BORROWED; Refresh (re)points the set at them and must run again after every resize,
//    because ReconfigureVisibilityImage rebuilds the view and the old handle goes stale. The material UBO is the one buffer this unit OWNS, since the
//    table is small, immutable after upload, and belongs to nothing else.
struct SurfaceShadeInscription
{
    VulkanHost*           Host           = nullptr;          // [-] - not owned; supplies device / allocator
    VkPipeline            Pipeline       = VK_NULL_HANDLE;   // [-] - fullscreen-triangle shade pipeline (dynamic rendering, alpha-over)
    VkPipelineLayout      PipelineLayout = VK_NULL_HANDLE;   // [-] - one set + the ShadeConstants push range
    VkDescriptorSetLayout SetLayout      = VK_NULL_HANDLE;   // [-] - set 0: b0 id, b1/b2/b3 mesh, b4 materials, b5-b7 floor
    VkDescriptorPool      DescriptorPool = VK_NULL_HANDLE;   // [-] - pool sized for one shade set
    VkDescriptorSet       ShadeSet       = VK_NULL_HANDLE;   // [-] - the bound set
    VkSampler             PointSampler   = VK_NULL_HANDLE;   // [-] - nearest / clamp; a filtered id is a wrong id

    VkBuffer              MaterialBuffer   = VK_NULL_HANDLE; // [-] - owned host-visible UBO: SurfacePresetCount x SurfacePresetParameters
    VkDeviceMemory        MaterialMemory   = VK_NULL_HANDLE; // [-] - backing allocation for MaterialBuffer (mapped at upload)

    VkImageView           BoundIdView      = VK_NULL_HANDLE; // [-] - the view b0 currently points at; Refresh writes only on change
    VkBuffer              BoundVertexBuffer = VK_NULL_HANDLE;// [-] - the borrowed mesh vertex SSBO b1 currently points at
    VkBuffer              BoundIndexBuffer = VK_NULL_HANDLE; // [-] - the borrowed mesh index SSBO b2 currently points at
    VkBuffer              BoundInstanceBuffer = VK_NULL_HANDLE; // [-] - the borrowed instance SSBO b3 currently points at
    VkBuffer              BoundFloorVertexBuffer   = VK_NULL_HANDLE; // [-] - the borrowed floor vertex SSBO b5 currently points at
    VkBuffer              BoundFloorIndexBuffer    = VK_NULL_HANDLE; // [-] - the borrowed floor index SSBO b6 currently points at
    VkBuffer              BoundFloorInstanceBuffer = VK_NULL_HANDLE; // [-] - the borrowed floor instance SSBO b7 currently points at
    bool                  FloorGeometryBound = false;        // [-] - true when b5-b7 hold the REAL floor buffers rather than the head-buffer alias
    bool                  ReadyCondition   = false;          // [-] - true once pipeline + layout + descriptors + material UBO are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the pipeline, layout, descriptor set layout, pool, set (left unpointed until Refresh), point sampler, and the material UBO. ColourFormat is
// the swapchain colour format the dynamic-rendering pipeline composites into. Reads VisibilityInscription.vert.spv (the fullscreen triangle is
// shared verbatim — no reason for a second copy) and SurfaceShade.frag.spv from ShaderDirectory. Returns false (ReadyCondition stays false) if
// dynamic rendering is unavailable, a shader is missing, or any Vulkan step fails. Pair with Finalize.
bool InitializeSurfaceShadeInscription(SurfaceShadeInscription& Shade,
                                       VulkanHost&              Host,
                                       VkFormat                 ColourFormat,
                                       const char*              ShaderDirectory);

// Upload the resolved material table into the owned UBO. Call once after Initialize (the table is immutable — runtime lobe toggling goes through the
// push constant, not a re-upload). A no-op when not ready. Table must hold SurfacePresetCount entries.
void UploadSurfaceShadeMaterials(SurfaceShadeInscription& Shade, const SurfacePresetParameters* Table);

// Point the descriptor set at the borrowed visibility image view and the borrowed mesh / instance buffers. Call once everything is first ready and
// again after every resize (ReconfigureVisibilityImage rebuilds the view). A no-op when any side is not ready. The device must be idle — an in-flight
// frame may still read the set. Cheap: one vkUpdateDescriptorSets, and only when a handle actually changed.
//
// The Floor* triple is the checkered floor's own geometry (P6.3a), which lives in different buffers than the heads' and writes identities based at
// FloorPartitionBase. Pass VK_NULL_HANDLE for all three when no floor document loaded: the bindings are then aliased onto the head buffers so no
// descriptor is left undefined, FloorGeometryBound stays false, and the caller must leave FloorShadeEnabled at 0 so the shader discards floor pixels.
void RefreshSurfaceShadeInscription(SurfaceShadeInscription& Shade,
                                    const VisibilityImage&   Image,
                                    VkBuffer                 VertexBuffer,
                                    VkDeviceSize             VertexBytes,
                                    VkBuffer                 IndexBuffer,
                                    VkDeviceSize             IndexBytes,
                                    VkBuffer                 InstanceBuffer,
                                    VkDeviceSize             InstanceBytes,
                                    VkBuffer                 FloorVertexBuffer   = VK_NULL_HANDLE,
                                    VkDeviceSize             FloorVertexBytes    = 0,
                                    VkBuffer                 FloorIndexBuffer    = VK_NULL_HANDLE,
                                    VkDeviceSize             FloorIndexBytes     = 0,
                                    VkBuffer                 FloorInstanceBuffer = VK_NULL_HANDLE,
                                    VkDeviceSize             FloorInstanceBytes  = 0);

// Record one shade into an already-open dynamic-rendering colour scope: set viewport + scissor, bind the pipeline + set, push the constants, and draw
// the three-vertex fullscreen triangle. The visibility image must already be in SHADER_READ_ONLY (see TransitionVisibilityImageForSampling). A no-op
// when not ready. CommandBuffer must be INSIDE the colour scope.
void RecordSurfaceShadeInscription(const SurfaceShadeInscription& Shade,
                                   VkExtent2D                     Extent,
                                   const SurfaceShadeConstants&   Constants,
                                   VkCommandBuffer                CommandBuffer);

// Destroy the pipeline, layout, descriptor plumbing, sampler, and material UBO, then reset to empty. The device must be idle. Safe on a
// never-initialized value.
void FinalizeSurfaceShadeInscription(SurfaceShadeInscription& Shade);

} // namespace Frontier

#endif
