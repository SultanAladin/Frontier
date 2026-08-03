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
#include "Graphics/Surfel/SurfelPool.h"
#include "Graphics/Surfel/SurfelGridSlotting.h"
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
    uint32_t FloorIndexBase            = 0;                                        // [-] - first INDEX of the floor's run in b6 (elements, not bytes)

    // ---- Phase 3 surfel GI gather (must byte-match the ShadeConstants tail in SurfaceShade.frag: two vec4 rows, then four uint scalars) ----
    float    GridOrigin[4]             = { 0,0,0,0 };                              // [-] - snapped grid origin, SAME source as slotting/integrate (.w unused)
    float    OcclusionParams[4]        = { 1.2f, 0.2f, 0.25f, 0.15f };            // [-] - (shadowStrength, bleedReduction, grazingBiasScale, varianceBleedScale)
    uint32_t SurfelReadOffsetElements  = 0;                                        // [-] - moments read-half ELEMENT base = MomentsParity*Capacity (post-swap)
    uint32_t SurfelGiEnabled           = 0;                                        // [-] - 1 gathers surfel GI, 0 falls back to the flat AmbientColour (A/B)
    uint32_t SurfelCapacity            = 0;                                        // [-] - pool capacity (carried for parity + future bounds)
    float    TuneCellDiameter          = 1.0f;                                     // [m] - live base cell edge (F10 window); the gather's cell must match spawn/slotting/integrate
    float    TuneBaseRadius            = 1.2f;                                     // [m] - live cascade-0 disc radius (F10 window)
    float    TuneNearFieldBias         = 1.0f;                                     // [-] - live near-field bias (F10 window; layout parity, unused by the gather)
    uint32_t PushPad0                  = 0;                                        // [-] - keep the block 16-byte aligned (matches the frag's PushPad0)
};

// 🔴 FloorIndexBase exists because gl_PrimitiveID is per-DRAW while b6 is now a MERGED index buffer. Since the floor and heads share one allocation
//    (see GeometryStreamConcatenation), the floor's triangles begin at its placement's IndexOffset, but the raster stamped each one with a primitive
//    ordinal restarting at 0 for the floor's own draw call. Reconstructing at Primitive * 3 alone therefore reads the HEADS' first triangles: a floor
//    pixel would take its normal from a Suzanne face — a lit, plausible surface with the wrong shading, not a visibly missing one. The base restores
//    the mapping from draw-local ordinal to absolute element. It stays 0 whenever the floor's run genuinely starts at 0, so a caller that never
//    merged is unaffected.

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

    // ---- Phase 3: the surfel GI descriptor set (set 1) — mirrors SurfelIntegrate.comp's set 1 exactly ----
    // Separate layout + set so the GATHER reads the live surfel cache without disturbing set 0. Best-effort: a surfel-set build failure leaves
    // SurfelSet null and SurfelSetReady false; the record then forces SurfelGiEnabled=0 in the push block and binds a safe set 1, so the shade still
    // runs the flat-ambient path. The seven buffers are BORROWED (owned by SurfelPool / SurfelGridSlotting); Refresh re-points only on handle change.
    VkDescriptorSetLayout SurfelSetLayout  = VK_NULL_HANDLE; // [-] - set 1: b0 Surfels, b1 Moments, b2 Offsets, b3 List, b4 Guiding, b5 Depth, b6 Touched
    VkDescriptorSet       SurfelSet        = VK_NULL_HANDLE; // [-] - the bound surfel set (allocated from DescriptorPool alongside ShadeSet)
    VkBuffer              BoundSurfelBuffer   = VK_NULL_HANDLE; // [-] - b0 currently points at (SurfelPool.SurfelBuffer)
    VkBuffer              BoundMomentsBuffer  = VK_NULL_HANDLE; // [-] - b1 (SurfelPool.MomentsBuffer, both ping-pong halves)
    VkBuffer              BoundOffsetsBuffer  = VK_NULL_HANDLE; // [-] - b2 (SurfelGridSlotting.OffsetsBuffer)
    VkBuffer              BoundListBuffer     = VK_NULL_HANDLE; // [-] - b3 (SurfelGridSlotting.ListBuffer)
    VkBuffer              BoundGuidingBuffer  = VK_NULL_HANDLE; // [-] - b4 (SurfelPool.GuidingBuffer)
    VkBuffer              BoundSurfelDepthBuffer = VK_NULL_HANDLE; // [-] - b5 (SurfelPool.SurfelDepthBuffer)
    VkBuffer              BoundTouchedBuffer  = VK_NULL_HANDLE; // [-] - b6 (SurfelPool.TouchedBuffer)
    bool                  SurfelSetReady   = false;          // [-] - true once the surfel layout + set exist AND all seven buffers are pointed

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
// The Floor* triple is the checkered floor's own geometry (P6.3a), which writes identities based at FloorPartitionBase. Pass VK_NULL_HANDLE for all
// three when no floor document loaded: the bindings are then aliased onto the head buffers so no descriptor is left undefined, FloorGeometryBound
// stays false, and the caller must leave FloorShadeEnabled at 0 so the shader discards floor pixels.
//
// 📝 The geometry pair passed here NO LONGER has to differ from the heads'. Since the streams were merged the floor's vertices and indices sit in the
//    same allocation, so b5/b6 legitimately receive the same two handles as b1/b2 — which is not the aliasing hazard the gate was written against,
//    because the floor's triangles genuinely are in there. A caller handing over the merged pair must also set FloorIndexBase; the handles alone no
//    longer say where the floor's run begins.
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

// Phase 3: point the surfel descriptor set (set 1) at the live surfel cache so the shade's gather can read it. Writes the seven whole-buffer bindings
// from SurfelPool (Surfels / Moments / Guiding / SurfelDepth / Touched) and SurfelGridSlotting (Offsets / List), re-pointing only on handle change.
// Idempotent and cheap; a no-op when the shade's surfel layout, the pool, or the slotting is not ready — SurfelSetReady stays false and the caller must
// leave SurfelGiEnabled at 0. The device must be idle (an in-flight frame may still read the set). Surfel buffers do NOT rebuild on resize (only the id
// view does), so unlike the set-0 Refresh this need only run once both the pool and slotting are first ready.
void RefreshSurfaceShadeSurfelBindings(SurfaceShadeInscription& Shade,
                                       const SurfelPool&        Pool,
                                       const SurfelGridSlotting& Slotting);

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
