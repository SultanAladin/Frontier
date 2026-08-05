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
    float    SunRadiance[4]            = { 3.0f, 2.94f, 2.85f, 0.0f };             // [-] - key-light radiance (colour x intensity, PREMULTIPLIED from the F10 Sun card); .w unused. Default = old 3.0*(1,0.98,0.95)
    uint32_t CompositeFeatureMask      = 0;                                        // [-] - SurfaceFeatureBit set, overrides the Composite record only
    uint32_t FloorPartitionBase        = 0;                                        // [-] - partition ordinals >= this belong to the floor mesh
    uint32_t FloorShadeEnabled         = 0;                                        // [-] - 1 shades the floor from its own buffers, 0 discards it (P6.3a)
    uint32_t FloorIndexBase            = 0;                                        // [-] - first INDEX of the floor's run in b6 (elements, not bytes)

    // 🚧 The Phase-3 surfel GI gather fields went with the webgiya strip, removed here and in SurfaceShade.frag's ShadeConstants in ONE edit. The W298
    //    port re-adds its tail to BOTH in one edit too: the two byte-match with no diagnostic, so a one-sided change reads every later scalar from the
    //    wrong offset — the sun-shadow block below would silently take its knobs from GI bytes.

    // ---- Primary sun shadow (area-sampled BVH ray; set 2) — must byte-match the six-scalar tail of the frag's ShadeConstants ----
    float    SunAngularRadius          = 0.03f;                                    // [rad] - sun-disc half-angle; 0 hard, larger softens the penumbra (real sun ~0.0047)
    uint32_t ShadowSampleCount         = 8;                                        // [-] - jittered rays across the disc per pixel; more = smoother, noisier wants temporal
    uint32_t ShadowEnabled             = 0;                                        // [-] - 1 traces the sun-visibility gate; the record forces 0 when set 2 is not ready
    uint32_t ShadowFrame               = 0;                                        // [-] - frame index; rotates the per-pixel jitter so a temporal pass can average
    uint32_t ShadowInstanceCount       = 0;                                        // [-] - TLAS leaves (TraceInstanceCount for the shadow trace)
    uint32_t ShadowSliceCount          = 0;                                        // [-] - slice table entries (TraceSliceCount for the shadow trace)
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

    // ---- The RESERVED descriptor set (set 1) — an empty layout holding index 1 open ----
    // 🚧 This slot held the webgiya surfel GI cache (seven storage buffers) and is where the W298 port's irradiance atlas lands. It survives the strip as
    //    a ZERO-BINDING layout because the sun-shadow BVH below is declared `set = 2` in SurfaceShade.frag, and Vulkan binds sets by contiguous index —
    //    a set at index 2 is illegal without a real layout at index 1. Keeping it empty avoids renumbering the frag's set-2 declarations down to 1 now
    //    and back up to 2 when the atlas arrives. The set carries no descriptors, so nothing points at it and nothing reads it.
    VkDescriptorSetLayout ReservedSetLayout = VK_NULL_HANDLE; // [-] - set 1: zero bindings; the W298 irradiance atlas fills it
    VkDescriptorSet       ReservedSet       = VK_NULL_HANDLE; // [-] - allocated from DescriptorPool; bound at index 1 so set 2 stays reachable

    // ---- Primary sun shadow: the BVH descriptor set (set 2) — the acceleration buffers the shade never had ----
    // Same two-level BVH SurfelIntegrate.comp reads, but the shade REUSES set 0's instance SSBO + merged vertex/index streams (the trace's Instances/
    // MeshIndices/PositionForVertex resolve to those), so set 2 carries only the four buffers set 0 lacks: Slices / ArenaNodeWords / ArenaPrimitives /
    // TreeNodeWords. All BORROWED (GeometryArena + InstanceTree), bound ONCE (scene static after load). Best-effort: a set-2 build failure leaves
    // ShadowSet null and ShadowSetReady false; the record then forces ShadowEnabled=0 and the shade runs unshadowed rather than reading undefined memory.
    VkDescriptorSetLayout ShadowSetLayout  = VK_NULL_HANDLE; // [-] - set 2: b0 Slices, b1 ArenaNodeWords, b2 ArenaPrimitives, b3 TreeNodeWords (all ro storage)
    VkDescriptorSet       ShadowSet        = VK_NULL_HANDLE; // [-] - the bound BVH set (allocated from DescriptorPool alongside ShadeSet / SurfelSet)
    VkBuffer              BoundSliceBuffer        = VK_NULL_HANDLE; // [-] - b0 (GeometryArena slice table)
    VkBuffer              BoundArenaNodeBuffer    = VK_NULL_HANDLE; // [-] - b1 (GeometryArena bottom-level node blob)
    VkBuffer              BoundArenaPrimBuffer    = VK_NULL_HANDLE; // [-] - b2 (GeometryArena primitive-order table)
    VkBuffer              BoundTreeNodeBuffer     = VK_NULL_HANDLE; // [-] - b3 (InstanceTree top-level node words)
    bool                  ShadowSetReady   = false;          // [-] - true once the BVH layout + set exist AND all four buffers are pointed

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

// 🚧 RefreshSurfaceShadeSurfelBindings went with the webgiya strip: set 1 is now an empty reserved layout with no buffers to point at. The W298 port
//    re-adds a Refresh here for its irradiance atlas, against the same set index.

// Primary sun shadow: point the BVH descriptor set (set 2) at the borrowed GeometryArena + InstanceTree buffers so the shade's shadow ray can trace
// the scene. Writes the four whole-buffer bindings (Slices / ArenaNodeWords / ArenaPrimitives / TreeNodeWords), re-pointing only on a handle change.
// Idempotent and cheap; a no-op when the shade's BVH layout is not built — ShadowSetReady stays false and the record forces ShadowEnabled=0. The
// device must be idle (an in-flight frame may still read the set). These handles are stable after load (scene static), so this need run only once.
void RefreshSurfaceShadeBvhBindings(SurfaceShadeInscription& Shade,
                                    VkBuffer                 SliceBuffer,
                                    VkBuffer                 ArenaNodeBuffer,
                                    VkBuffer                 ArenaPrimitiveBuffer,
                                    VkBuffer                 TreeNodeBuffer);

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
