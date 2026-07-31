/*==============================================================================================================================================
                                                    SHADOWTILEMARKINGSUBMISSION.H
==============================================================================================================================================*/
// 🧩 The GPU side of the sun-shadow marking chain: the unit that owns the three pipelines S1/S2/S3 need and records them against a ShadowTileStore.
//    S1 (MarkVisibleShadowPages.comp) reads the id buffer and raises Used|Direct on every tile a receiver samples; S2
//    (ShadowTileTagInscription.vert/.frag) rasterizes each caster's light-space footprint and raises Update on the tiles whose depth is now wrong;
//    S3 (ShadowTileLevelPropagate.comp) carries demand fine -> coarse as Used|Coarse. ShadowTileStore owns the table these write; this unit owns the
//    pipelines, the shared descriptor set, and the per-level origin uniform block all three address through.
//
// 🔴 THE ORIGIN UNIFORM BLOCK MUST BE REFRESHED AFTER RefreshSunShadowClipmap AND BEFORE ANY DISPATCH. Every shader resolves a tile through the
//    per-level ToroidalOrigin at binding 3. Uploading a window the clipmap has not yet scrolled makes all three passes address the PREVIOUS image's
//    lattice, so every marked tile is off by the frame's scroll — which reads as shadows lagging the camera, not as a stale upload.
//
// 🔴 S3 IS RECORDED AS ONE DISPATCH PER LEVEL, ASCENDING, WITH A BARRIER BETWEEN, AND THAT ORDERING CANNOT MOVE INTO THE SHADER. Level N's inherited
//    demand must be visible when level N+1 runs or LOD 0's demand stops at LOD 1. Vulkan orders nothing between workgroups and `barrier()` is
//    intra-workgroup only, so a single 1 x 1 x LevelCount dispatch propagates one level deep, nondeterministically. RecordShadowTilePropagation is
//    the only place that ordering exists.
//
// ⚠️ S2 is a raster unit with NO colour attachment — its whole output is the atomicOr side effect from the fragment stage, so rasterizerDiscardEnable
//    must stay FALSE and both depth test and depth write must stay OFF. It needs fragmentStoresAndAtomics, which VulkanHost enables.
//
// 📝 Pipelines + descriptors are built once and are extent-independent (the tilemap is a fixed 32² x 6 lattice, unrelated to the swapchain). The id
//    buffer / depth bindings are re-pointed when those views rebuild on resize. Borrows the VulkanHost and the store; owns the origin buffer.

#pragma once
#ifndef FRONTIER_GRAPHICS_SHADOW_SHADOWTILEMARKINGSUBMISSION_H
#define FRONTIER_GRAPHICS_SHADOW_SHADOWTILEMARKINGSUBMISSION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Shadow/ShadowTileStore.h"
#include "Graphics/Shadow/SunShadowClipmap.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 S3's local workgroup edge — must match local_size_x/y in ShadowTileLevelPropagate.comp. 32 x 32 = 1024 invocations, exactly one per tile of one
//    level, and exactly Pascal's maxComputeWorkGroupInvocations floor. 🔴 There is no headroom to sweep two levels per group.
constexpr uint32_t ShadowPropagateWorkgroupEdge = 32;

// 📝 The conservative-raster expansion S2's vertex stage applies, in tilemap texels. Emulated in the shader because
//    VK_EXT_conservative_rasterization is not core and Pascal does not expose it. Outward-only, so it can only ever OVER-tag.
constexpr float ShadowTagEdgeExpandTexels = 1.0f;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 S1's push block, matching MarkVisibleShadowPages.comp's MarkConstants byte-for-byte. Assembled per image from the camera + the light basis.
// ⚠️ The trailing scalars are ordered exactly as the shader declares them; std430 push layout gives ivec2 an 8-byte alignment, which the ScreenExtent
//    pair satisfies at offset 112 only because the three vec4s above it are 16-aligned. Reordering these fields silently shifts every one of them.
struct ShadowMarkConstants
{
    float    InverseViewProjection[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };   // [-]  - column-major clip -> world
    float    LightRightAxis[4]         = { 1,0,0,0 };                             // [-]  - light-space +X in world (w unused)
    float    LightUpAxis[4]            = { 0,1,0,0 };                             // [-]  - light-space +Y in world (w unused)
    float    ObserverPosition[4]       = { 0,0,0,0 };                             // [m]  - eye, for the LOD distance test (w unused)
    int32_t  ScreenExtentX             = 0;                                       // [px] - id-buffer width
    int32_t  ScreenExtentY             = 0;                                       // [px] - id-buffer height
    float    BaseTileMetres            = ShadowBaseTileMetres;                     // [m]  - LOD 0 tile edge; doubles per level
    uint32_t LevelCount                = ShadowTilemapLodCount;                    // [-]  - LODs to consider
    uint32_t EmptySentinel             = 0xFFFFFFFFu;                              // [-]  - the id value meaning "nothing rasterized here"
    float    LodDistanceScale          = 8.0f;                                     // [m]  - distance at which LOD 0 gives way to LOD 1; doubles per level
};

// 📝 S2's push block, matching ShadowTileTagInscription.vert's InscriptionConstants. One draw per level, so Level and WindowCentreTile change per draw.
struct ShadowTagConstants
{
    float    LightRightAxis[4]  = { 1,0,0,0 };            // [-] - light-space +X in world (w unused)
    float    LightUpAxis[4]     = { 0,1,0,0 };            // [-] - light-space +Y in world (w unused)
    float    WindowCentreTileX  = 0.0f;                   // [-] - light-tile coordinate at this level's window centre
    float    WindowCentreTileY  = 0.0f;                   // [-] - light-tile coordinate at this level's window centre
    float    BaseTileMetres     = ShadowBaseTileMetres;    // [m] - LOD 0 tile edge; doubles per level
    uint32_t Level              = 0;                       // [-] - the LOD this draw targets
    float    EdgeExpandTexels   = ShadowTagEdgeExpandTexels; // [-] - conservative-raster expansion, in tilemap texels
};

// 📝 S3's push block, matching ShadowTileLevelPropagate.comp's PropagateConstants. The level itself comes from baseGroupZ, not from here.
struct ShadowPropagateConstants
{
    uint32_t LevelCount = ShadowTilemapLodCount;   // [-] - LODs in the clipmap; the top level has no coarser parent and only reads
};

// 📝 The marking chain's owned device state. One descriptor set serves all three passes — the shaders deliberately share binding numbers (0 = tile
//    table, 1 = id buffer, 2 = depth, 3 = level origins, 4 = caster bounds) so no set has to be rebound between them.
// ⚠️ Bindings 1/2/4 point at views and buffers this unit does NOT own (the id buffer, the depth target, InstanceCull's record buffer). They are
//    re-pointed through RefreshShadowTileMarkingBindings whenever those rebuild; the Bound* fields exist so that refresh is idempotent.
struct ShadowTileMarkingSubmission
{
    VulkanHost*           Host                = nullptr;          // [-] - not owned
    VkDescriptorSetLayout SetLayout           = VK_NULL_HANDLE;   // [-] - the five shared bindings
    VkDescriptorPool      DescriptorPool      = VK_NULL_HANDLE;   // [-] - sized for one set
    VkDescriptorSet       MarkingSet          = VK_NULL_HANDLE;   // [-] - the one set all three passes bind

    VkPipelineLayout      MarkLayout          = VK_NULL_HANDLE;   // [-] - S1: set + ShadowMarkConstants push range
    VkPipeline            MarkPipeline        = VK_NULL_HANDLE;   // [-] - S1: MarkVisibleShadowPages.comp
    VkPipelineLayout      TagLayout           = VK_NULL_HANDLE;   // [-] - S2: set + ShadowTagConstants push range (vertex stage)
    VkPipeline            TagPipeline         = VK_NULL_HANDLE;   // [-] - S2: ShadowTileTagInscription.vert/.frag
    VkPipelineLayout      PropagateLayout     = VK_NULL_HANDLE;   // [-] - S3: set + ShadowPropagateConstants push range
    VkPipeline            PropagatePipeline   = VK_NULL_HANDLE;   // [-] - S3: ShadowTileLevelPropagate.comp

    VkBuffer              OriginBuffer        = VK_NULL_HANDLE;   // [-] - std140 ivec4[ShadowTilemapLodCount] uniform block at binding 3
    VkDeviceMemory        OriginMemory        = VK_NULL_HANDLE;   // [-] - backing allocation
    void*                 OriginMapping       = nullptr;          // [-] - persistently mapped; coherent, no explicit flush

    VkSampler             TargetSampler       = VK_NULL_HANDLE;   // [-] - point sampler for the id buffer + depth reads
    VkImageView           BoundVisibilityView = VK_NULL_HANDLE;   // [-] - id-buffer view binding 1 points at
    VkImageView           BoundDepthView      = VK_NULL_HANDLE;   // [-] - depth view binding 2 points at
    VkBuffer              BoundBoundsBuffer   = VK_NULL_HANDLE;   // [-] - caster-bounds SSBO binding 4 points at

    uint32_t              CasterCount         = 0;                // [-] - instances S2 draws; 0 records no tag draw at all
    bool                  TagPipelineEnabled  = false;            // [-] - false when fragmentStoresAndAtomics is absent; S1/S3 still run
    bool                  ReadyCondition      = false;            // [-] - true once layout + S1/S3 pipelines + descriptors + origin buffer are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the shared set layout, the three pipelines, the pool + one set, the origin uniform buffer and the point sampler. ShaderDirectory locates the
// three .spv modules. Binding 0 is pointed at Store.TableBuffer here; bindings 1/2/4 stay unbound until RefreshShadowTileMarkingBindings.
// 🔴 S2's pipeline is skipped (TagPipelineEnabled stays false) when the host lacks fragmentStoresAndAtomics — a fragment stage that cannot write an
//    SSBO would be a validation error, and S1/S3 are still individually useful. Returns false only when S1/S3 could not be built.
// Store must already be initialized. Pair with FinalizeShadowTileMarkingSubmission.
bool InitializeShadowTileMarkingSubmission(ShadowTileMarkingSubmission& Marking,
                                           VulkanHost&                  Host,
                                           const ShadowTileStore&       Store,
                                           const char*                  ShaderDirectory);

// Re-point the bindings that reference resources this unit does not own: the id-buffer view (1), the depth view (2), and the caster-bounds SSBO (4).
// Idempotent — each binding is rewritten only when its handle actually changed, so this is safe to call every image and required after any resize.
// CasterCount bounds S2's instanced draw. A null handle leaves that binding as it was.
void RefreshShadowTileMarkingBindings(ShadowTileMarkingSubmission& Marking,
                                      VkImageView                  VisibilityView,
                                      VkImageView                  DepthView,
                                      VkBuffer                     CasterBoundsBuffer,
                                      uint32_t                     CasterCount);

// Write the per-level ToroidalOrigins into the binding-3 uniform block from the clipmap's CURRENT state.
// 🔴 Call after RefreshSunShadowClipmap and before any Record* below — see the header note. Levels beyond the clipmap's count are zeroed rather than
//    left stale, so a shrunk clipmap cannot leave a coarse level addressing a window that no longer exists.
void RefreshShadowTileOrigins(ShadowTileMarkingSubmission& Marking, const SunShadowClipmap& Clipmap);

// S1: dispatch receiver marking over the id buffer, ceil(extent / ShadowMarkWorkgroupEdge) groups. The visibility image and depth must both be in
// SHADER_READ_ONLY. Barriers the table for the passes that follow. A no-op when not ready or when either bound view is null.
// CommandBuffer must be recording, OUTSIDE any rendering scope.
void RecordShadowReceiverMarking(ShadowTileMarkingSubmission& Marking,
                                 const ShadowMarkConstants&   Constants,
                                 VkCommandBuffer              CommandBuffer);

// S2: rasterize the caster footprints into the 32² tilemap, one instanced 4-vertex strip draw per level, raising Update.
// ⚠️ Opens and closes its own dynamic-rendering scope with NO attachments — the output is purely the fragment stage's atomicOr.
// 🔴 Only the CURRENT bounds are drawn. The previous-bounds sub-draw the shader is written for has no data source yet, so a caster that MOVED will not
//    invalidate the page it vacated. Tracked in EngineDocs/Backlog.md.
// A no-op when TagPipelineEnabled is false, CasterCount is 0, or the bounds buffer is unbound. Must be OUTSIDE any rendering scope on entry.
void RecordShadowCasterTagging(ShadowTileMarkingSubmission& Marking,
                               const SunShadowClipmap&      Clipmap,
                               VkCommandBuffer              CommandBuffer);

// S3: propagate demand fine -> coarse. 🔴 Records LevelCount-1 separate 1 x 1 x 1 dispatches, ascending, each with baseGroupZ set to its level and a
//    SHADER_WRITE -> SHADER_READ barrier between consecutive dispatches. That loop IS the level ordering; collapsing it into one dispatch propagates a
//    single level deep, nondeterministically. Requires VK_KHR_device_group / Vulkan 1.1 vkCmdDispatchBase, which the 1.2 host provides.
// A no-op when not ready. CommandBuffer must be recording, OUTSIDE any rendering scope.
void RecordShadowTilePropagation(ShadowTileMarkingSubmission& Marking,
                                 uint32_t                     LevelCount,
                                 VkCommandBuffer              CommandBuffer);

// Destroy the three pipelines, both layouts, the descriptors, the origin buffer and the sampler, then reset to empty. The device must be idle.
// Safe on a never-initialized value.
void FinalizeShadowTileMarkingSubmission(ShadowTileMarkingSubmission& Marking);

} // namespace Frontier

#endif
