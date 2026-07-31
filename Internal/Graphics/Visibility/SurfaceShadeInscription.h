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
#include "Graphics/Shadow/SunShadowClipmap.h"   // ShadowTilemapLodCount / ShadowBaseTileMetres size the trace block below
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
    uint32_t SunShadowEnabled          = 0;                                        // [-] - 1 traces the sun shadow atlas, 0 leaves every surface fully lit (P6.5)
};

// 🔴 FloorShadeEnabled is a CORRECTNESS gate, not a preference. The floor's three descriptors are always written — when no floor document loaded they
//    are aliased onto the heads' own buffers, because a descriptor that is merely left unwritten is undefined memory rather than a safely empty one
//    (the same trap ComponentOverlayInscription documents for its authored tables). Aliased, those bindings report a healthy non-zero length while
//    holding HEAD data, so a shader that trusted the length alone would reconstruct floor pixels from head triangles. Only this flag distinguishes
//    "the floor buffers are real" from "the floor buffers are a safe alias", so it must stay the single thing the shader branches on.

// 🔴 SunShadowEnabled is the SAME class of gate as FloorShadeEnabled, for the same reason: b8 is ALIASED onto the visibility image when no shadow
//    atlas exists. Both are R32_UINT sampled images, so the alias type-checks and samples cleanly — it simply returns packed visibility IDs where the
//    tracer expects encoded depth. Those IDs are small integers, i.e. depths very close to the sun, so an ungated trace would read almost every
//    surface as occluded and the scene would go uniformly black. That failure looks like a lighting bug, not a binding one, which is exactly why the
//    flag has to be the single thing the shader branches on. ⚠️ The caller must leave it 0 whenever ShadowAtlasBound is false.
//
// 📝 Why the light basis lives in the UBO rather than the push block: the block is already 112 bytes, and a basis (3 x 16) plus six toroidal origins
//    (6 x 16) would carry it to ~272 — past the 128-byte guaranteed minimum, so the pass would fail to build on a conformant device that reports it.
//    ShadowTileMarkingSubmission already solved this exact problem with a std140 uniform block at a binding, so this follows that precedent instead of
//    inventing a second mechanism. The push block therefore grows by ZERO bytes here: SunShadowEnabled reuses the tail pad.

// 📝 What the shade paints instead of lit colour when the sun-shadow debug view is on. The three views exist because the visible symptom — a fully lit
//    scene — has three causes that a shaded image cannot tell apart, and each is fixed somewhere different:
//
//      ResolvedLevel  no page resident anywhere (paints the no-level colour) means the mapping the SHADE reads disagrees with what the allocator
//                     filled, i.e. a descriptor or upload-ordering fault on the read side.
//      DepthMargin    a page resolves but its margin sits pinned at exactly +1.0 means the page holds the CLEAR IDENTITY — the tap found no stored
//                     depth at all, so S7's imageAtomicMin never landed in the memory the shade samples.
//      Occlusion      the tap does report occlusion, which moves the fault downstream of the trace entirely.
//
// 🔴 These are diagnostic views, not a shading feature: each REPLACES the surface colour outright rather than tinting it, because a debug signal
//    multiplied into a lit surface is unreadable exactly where it matters (a dark region could be the signal or could be the surface).
enum class SunShadowDebugView : uint32_t
{
    Disabled      = 0,   // shade normally
    ResolvedLevel = 1,   // false-colour the level the walk settled on; distinct colour when no level was resident
    DepthMargin   = 2,   // signed margin: occluded red, lit green, clear-identity saturated white
    Occlusion     = 3,   // flat black/white on the resolved visibility alone
};

// 📝 The per-image sun-shadow parameters the tracer reads, as one std140 uniform block at b10. Rebuilt and re-uploaded every image, because the
//    toroidal origins scroll with the observer and the basis rotates with the sun — a stale copy addresses last image's lattice.
// 🔴 ivec4 per level, not ivec2: std140 rounds every array element up to 16 bytes. Declaring ivec2 here would pack the mirror at 8-byte stride while
//    the shader reads at 16, so levels 1+ would read halves of two different origins. Same note ShadowTileMarkingSubmission's block carries.
// ⚠️ The three axes are vec4 for the same std140 reason, and .w is unused padding rather than a homogeneous coordinate.
struct SunShadowTraceBlock
{
    float   LightRightAxis[4]   = { 1,0,0,0 };   // [-]     - light-space +X basis vector (.w pad)
    float   LightUpAxis[4]      = { 0,1,0,0 };   // [-]     - light-space +Y basis vector (.w pad)
    float   LightForwardAxis[4] = { 0,0,1,0 };   // [-]     - light-space +Z, ALONG the light's travel (.w pad)
    int32_t ToroidalOrigins[ShadowTilemapLodCount][4] = {}; // [tile] - per level, ADD form (see SunShadowClipmap); .zw pad
    float   BaseTileMetres      = ShadowBaseTileMetres;     // [m]  - level 0 tile edge; level N is this << N
    float   DepthOriginMetres   = 0.0f;          // [m]     - near end of the encoded depth range, matching the writer
    float   DepthRangeMetres    = 1.0f;          // [m]     - span of the encoded depth range, matching the writer
    float   DepthBias           = 0.0f;          // [m]     - SUBTRACTED from the receiver, never added to the stored depth
    uint32_t LevelCount         = 0;             // [-]     - LODs the tracer may walk
    // 🔴 The debug mode lives HERE rather than in the push block, and that is a hard constraint not a preference: SurfaceShadeConstants is already at
    //    the 112 bytes its own note describes, with SunShadowEnabled holding the last tail pad. Growing it would carry the pass toward the 128-byte
    //    guaranteed push-constant minimum for the sake of a diagnostic. This block is a UBO with three spare pads, so the field costs zero bytes.
    uint32_t SunShadowDebugMode = 0;             // [-]     - SunShadowDebugView; 0 shades normally (see the enum for what each view paints)
    uint32_t Pad1               = 0;             // [-]     - std140 tail pad to a 16-byte boundary
    uint32_t Pad2               = 0;
};

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
    VkDescriptorSetLayout SetLayout      = VK_NULL_HANDLE;   // [-] - set 0: b0 id, b1/b2/b3 mesh, b4 materials, b5-b7 floor, b8 shadow atlas, b9 page mapping, b10 trace UBO
    VkDescriptorPool      DescriptorPool = VK_NULL_HANDLE;   // [-] - pool sized for one shade set
    VkDescriptorSet       ShadeSet       = VK_NULL_HANDLE;   // [-] - the bound set
    VkSampler             PointSampler   = VK_NULL_HANDLE;   // [-] - nearest / clamp; a filtered id is a wrong id

    VkBuffer              MaterialBuffer   = VK_NULL_HANDLE; // [-] - owned host-visible UBO: SurfacePresetCount x SurfacePresetParameters
    VkDeviceMemory        MaterialMemory   = VK_NULL_HANDLE; // [-] - backing allocation for MaterialBuffer (mapped at upload)

    // The owned per-image trace UBO (b10). Persistently mapped HOST_VISIBLE | HOST_COHERENT, following ShadowTileMarkingSubmission's origin buffer:
    // it is rewritten every image, so map-once beats a map/unmap pair, and coherent memory means the omitted vkFlushMappedMemoryRanges is a whole
    // bug class avoided rather than an oversight.
    VkBuffer              TraceBuffer      = VK_NULL_HANDLE; // [-] - owned host-visible UBO holding one SunShadowTraceBlock
    VkDeviceMemory        TraceMemory      = VK_NULL_HANDLE; // [-] - backing allocation for TraceBuffer
    void*                 TraceMapping     = nullptr;        // [-] - persistent mapping; coherent, no explicit flush

    VkSampler             ShadowSampler    = VK_NULL_HANDLE; // [-] - nearest / clamp for b8; 🔴 the atlas holds a depth ENCODING and must NEVER be filtered

    VkImageView           BoundIdView      = VK_NULL_HANDLE; // [-] - the view b0 currently points at; Refresh writes only on change
    VkBuffer              BoundVertexBuffer = VK_NULL_HANDLE;// [-] - the borrowed mesh vertex SSBO b1 currently points at
    VkBuffer              BoundIndexBuffer = VK_NULL_HANDLE; // [-] - the borrowed mesh index SSBO b2 currently points at
    VkBuffer              BoundInstanceBuffer = VK_NULL_HANDLE; // [-] - the borrowed instance SSBO b3 currently points at
    VkBuffer              BoundFloorVertexBuffer   = VK_NULL_HANDLE; // [-] - the borrowed floor vertex SSBO b5 currently points at
    VkBuffer              BoundFloorIndexBuffer    = VK_NULL_HANDLE; // [-] - the borrowed floor index SSBO b6 currently points at
    VkBuffer              BoundFloorInstanceBuffer = VK_NULL_HANDLE; // [-] - the borrowed floor instance SSBO b7 currently points at
    VkImageView           BoundShadowAtlasView = VK_NULL_HANDLE; // [-] - the view b8 currently points at (the atlas, or the id-image alias)
    VkBuffer              BoundShadowMappingBuffer = VK_NULL_HANDLE; // [-] - the borrowed tile->page mapping SSBO b9 currently points at
    VkBuffer              BoundShadowCoverageBuffer = VK_NULL_HANDLE; // [-] - the borrowed per-page coverage SSBO b11 currently points at
    bool                  FloorGeometryBound = false;        // [-] - true when b5-b7 hold the REAL floor buffers rather than the head-buffer alias
    bool                  ShadowAtlasBound = false;          // [-] - true when b8/b9/b11 hold the REAL atlas + mapping + coverage rather than the alias
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
//
// ShadowAtlasView / ShadowMappingBuffer / ShadowCoverageBuffer are the sun shadow atlas's sampled view, its tile->page mapping SSBO and its per-page
// coverage SSBO (P6.5), all three BORROWED from ShadowPageAtlas. Pass VK_NULL_HANDLE for all three when no atlas exists: b8 is then aliased onto the
// visibility image (both are R32_UINT, so the alias is type-correct) and b9/b11 onto the index buffer, ShadowAtlasBound stays false, and the caller
// MUST leave SunShadowEnabled at 0. 🔴 Tracing against the alias would read visibility IDs as encoded depth and darken the whole scene — see the
// flag's note.
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
                                    VkDeviceSize             FloorInstanceBytes  = 0,
                                    VkImageView              ShadowAtlasView     = VK_NULL_HANDLE,
                                    VkBuffer                 ShadowMappingBuffer = VK_NULL_HANDLE,
                                    VkDeviceSize             ShadowMappingBytes  = 0,
                                    VkBuffer                 ShadowCoverageBuffer = VK_NULL_HANDLE,
                                    VkDeviceSize             ShadowCoverageBytes  = 0);

// Write this image's sun-shadow parameters into the owned trace UBO (b10). Call once per image, AFTER RefreshSunShadowClipmap has scrolled the levels
// and BEFORE the shade records — the origins scroll and the basis rotates, so last image's copy addresses the wrong lattice. A no-op when not ready.
//
// 🔴 The block is rebuilt WHOLE rather than patched, and the tail beyond LevelCount is zeroed. A partial rewrite would leave a level the clipmap has
//    since shrunk past still holding its old origin, which the shader would happily walk. Cheap: one memcpy into a persistent coherent mapping, no
//    descriptor write and no map/unmap.
void UploadSurfaceShadeTraceBlock(SurfaceShadeInscription& Shade, const SunShadowTraceBlock& Block);

// Build a trace block from the clipmap's CURRENT state plus the depth-encoding parameters the writer used. Convenience over hand-filling the struct at
// the call site, and the single place the ADD-form origin convention is transcribed — 🔴 getting that sign backwards still yields in-range, distinct,
// plausible slots, so it produces shadows in the wrong place rather than any detectable error (proved by the differential probe, not merely asserted).
[[nodiscard]] SunShadowTraceBlock SolveSurfaceShadeTraceBlock(const SunShadowClipmap& Clipmap,
                                                             float                   DepthOriginMetres,
                                                             float                   DepthRangeMetres,
                                                             float                   DepthBias,
                                                             SunShadowDebugView      DebugView = SunShadowDebugView::Disabled);

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
