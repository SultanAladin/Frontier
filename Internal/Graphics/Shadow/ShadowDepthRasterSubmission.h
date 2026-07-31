/*==============================================================================================================================================
                                                    SHADOWDEPTHRASTERSUBMISSION.H
==============================================================================================================================================*/
// 🧩 S7 — the caster depth raster, the pass that finally puts real depth in the page atlas. Owns one graphics pipeline
//    (ShadowDepthRaster.vert/.frag) and the descriptor set binding the atlas storage image, the per-tile page mapping, and the caster instance
//    buffer. Everything before this decided WHICH pages exist (S1/S3/S5) and PRIMED them to the atomic-min identity (S6); this unit rasterizes the
//    casters into them, one draw per clipmap level.
//
// 🔴 A RENDER-TARGET-LESS RASTER: NO COLOUR ATTACHMENT, NO DEPTH ATTACHMENT, AND NO HARDWARE DEPTH TEST. The atlas is R32_UINT with STORAGE|SAMPLED
//    usage and so CANNOT be an attachment of either kind. Occlusion is resolved entirely by `imageAtomicMin` in the fragment stage — min is
//    commutative, so any fragment interleaving converges on the nearest caster. A plain store would keep whichever fragment landed last, which reads
//    as shadows flickering between casters rather than as a missing atomic. The pipeline therefore declares zero attachments and the record path
//    opens a rendering scope purely to establish a VIEWPORT.
//
// 🔴 THE VIEWPORT IS A LIGHT-SPACE TILE WINDOW, NOT THE ATLAS. One draw covers tiles whose pages sit anywhere in the 16x16 page grid in whatever
//    order the allocator handed them out, so no affine map takes a clip position to an atlas texel and gl_FragCoord CANNOT be the write address. The
//    fragment stage resolves its own texel per fragment: light position -> light tile -> toroidal slot -> page index (the mapping SSBO) -> page
//    origin -> texel in page. This is the reason the mapping upload (S5) is a hard prerequisite rather than an optimization.
//
// 🔴 ONE DRAW PER LEVEL, AND THE LEVEL IS A PUSH CONSTANT, NOT AN INSTANCE ATTRIBUTE. Each LOD has its own tile metres, toroidal origin and window
//    centre, so a single draw cannot serve two levels; the alternative (a level index per instance) would multiply the instance buffer by LOD count
//    for no gain.
//
// ⚠️ DEPTH IS ENCODED INCREASING WITH DISTANCE FROM THE SUN and the top code (ShadowPageClearIdentity) is RESERVED, so a real caster can never encode
//    to the value that means "nothing here". Both properties live in the fragment stage; see its banner. Any sign flip turns the min into a max and
//    the image comes out inside-out rather than blank.
//
// 🔴 ONE DESCRIPTOR SET PER CASTER MESH, NOT ONE PER PASS, AND THAT IS A CORRECTNESS REQUIREMENT. The scene has TWO caster meshes with TWO separate
//    instance buffers (the heads in VisibilityRasterization::InstanceBuffer, the floor slab in FloorRaster's), and BOTH must cast or the shadow is
//    wrong — a floor that receives but does not cast loses its own contact shadow, and heads that do not cast lose the shadow entirely. Re-pointing a
//    single shared set between two draws in one command buffer would mutate a descriptor a queued draw still references, which is undefined and shows
//    up as one mesh casting with the other's transforms. So each instance buffer gets its OWN set and the set is a parameter of the record call, exactly
//    as VisibilityRasterization::DrawVisibilityMesh takes an InstanceSet through its shared pipeline layout.
//
// 📝 Borrows the VulkanHost, the ShadowPageAtlas, the caster meshes (PolygonBufferAllocations) and the instance buffers; owns only the pipeline and
//    descriptor plumbing. Extent-independent — the atlas is a fixed 4096², so nothing here rebuilds on resize.

#pragma once
#ifndef FRONTIER_GRAPHICS_SHADOW_SHADOWDEPTHRASTERSUBMISSION_H
#define FRONTIER_GRAPHICS_SHADOW_SHADOWDEPTHRASTERSUBMISSION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Render/Resources/BufferAllocation.h"
#include "Graphics/Shadow/ShadowPageAtlas.h"
#include "Graphics/Shadow/SunShadowClipmap.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The light-space depth span the encoded uint covers, and the origin mapped to code 0. The sun is treated as an orthographic camera whose slab
//    spans this range about the world origin, which comfortably contains the 100 m floor slab and every authored scene.
// ⚠️ A caster outside the slab is CLAMPED, not dropped — a dropped caster stops occluding, which is a missing shadow; a clamped one is merely
//    imprecise at the extreme. See the fragment stage.
constexpr float ShadowDepthRangeMetres  = 512.0f;   // [m] - encoded span
constexpr float ShadowDepthOriginMetres = -256.0f;  // [m] - light-space depth that encodes to 0

// 📝 The tracer's depth bias, in light-space METRES (P6.5). A receiver compares its own depth minus this against the stored caster depth, so the bias is
//    the slack that absorbs the quantization between a 128²-texel page and the surface inside it.
// ⚠️ IT SCALES WITH ShadowPageResolution, so the two are coupled: halving the page edge doubles the world size of a texel and therefore the depth error
//    a flat surface accumulates across one. At 128² over L0's 0.5 m tile a texel is ~4 mm and 0.05 m is ~12x that, which is still ample slack — which is
//    why the 256 -> 128 change on 2026-07-30 left this value alone. 🔴 Lower the page resolution further and re-derive it rather than assuming it holds.
// 🔴 SUBTRACTED FROM THE RECEIVER, NEVER ADDED TO THE STORED DEPTH. Adding to a stored depth sitting at the clear identity (0xFFFFFFFF) wraps it to 0 —
//    the NEAREST code — so every texel no caster ever touched would occlude everything in its page. Pulling the receiver toward the sun cannot wrap: it
//    saturates at 0 through the encode's clamp. Measured, not assumed: the add-to-stored form read 0 of 4096 untouched texels as lit.
// ⚠️ CONSTANT ACROSS LEVELS, which is a known compromise rather than a tuned value. A coarse page covers 2^Level times more world per texel, so the same
//    bias is proportionally weaker there — acceptable while every tap is hard, and the reason the SMRT follow-up scales it per level. Too small reads as
//    surface acne (a surface shadowing itself in stipple), too large as peter-panning (a shadow detached from its caster's contact point).
constexpr float SunShadowDepthBiasMetres = 0.05f;   // [m] - receiver-side slack; see the sign rule above

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 S7's push block, matching ShadowDepthRaster.vert/.frag's ShadowDepthConstants field-for-field — 96 bytes, offsets verified against the compiled
//    SPIR-V of BOTH stages (they declare the block independently, so a divergence would silently shift every field).
// 🔴 THE THREE AXES ARE vec4, NOT vec3, AND THE PADDING IS LOAD-BEARING. std430 aligns a vec3 to 16 bytes but SIZES it 12, so three packed vec3s
//    would put the CPU and the shader two words out of step from the second field onward — the light basis would arrive scrambled and the shadows
//    would land in the wrong place with nothing looking syntactically wrong. The trailing `Padding` keeps the block a multiple of 4 words.
struct ShadowDepthRasterConstants
{
    float    LightRightAxis[4]   = { 1.0f, 0.0f, 0.0f, 0.0f };   // [-]     - light +X in world (w unused)
    float    LightUpAxis[4]      = { 0.0f, 1.0f, 0.0f, 0.0f };   // [-]     - light +Y in world (w unused)
    float    LightForwardAxis[4] = { 0.0f, 0.0f, 1.0f, 0.0f };   // [-]     - light +Z in world, ALONG the light's travel (w unused)
    float    WindowCentreTile[2] = { 0.0f, 0.0f };               // [tile]  - light tile at this level's window centre
    int32_t  ToroidalOrigin[2]   = { 0, 0 };                     // [tile]  - this level's origin, ADD form
    float    BaseTileMetres      = ShadowBaseTileMetres;          // [m]     - LOD 0 tile edge; doubles per level
    uint32_t Level               = 0;                            // [-]     - the LOD this draw targets
    float    DepthRangeMetres    = ShadowDepthRangeMetres;        // [m]     - encoded depth span
    float    DepthOriginMetres   = ShadowDepthOriginMetres;       // [m]     - depth mapped to code 0
    uint32_t PageResolution      = ShadowPageResolution;          // [texel] - page edge
    uint32_t AtlasPageEdge       = ShadowPageAtlasPageEdge;       // [page]  - pages per atlas edge
    uint32_t ClearValue          = ShadowPageClearIdentity;       // [-]     - the reserved identity; a caster never encodes to it
    uint32_t Padding             = 0;                            // [-]     - std430 push tail pad
};

// 📝 How many distinct caster meshes S7 can carry sets for. Two today (heads + floor slab); the pool is sized from this, so raising it is the only
//    change a third caster mesh needs.
constexpr uint32_t ShadowCasterSetCapacity = 4;

// 📝 The raster's owned device state. The instance buffers are BORROWED (the visibility rasters own them) so a caster cannot be placed differently for
//    its shadow than for its shading — one transform source, two consumers.
struct ShadowDepthRasterSubmission
{
    VulkanHost*           Host            = nullptr;          // [-] - not owned
    VkDescriptorSetLayout SetLayout       = VK_NULL_HANDLE;    // [-] - b0 = atlas image, b1 = page mapping, b2 = caster instances
    VkDescriptorPool      DescriptorPool  = VK_NULL_HANDLE;    // [-] - sized for ShadowCasterSetCapacity sets
    VkPipelineLayout      RasterLayout    = VK_NULL_HANDLE;    // [-] - set + ShadowDepthRasterConstants push range
    VkPipeline            RasterPipeline  = VK_NULL_HANDLE;    // [-] - ShadowDepthRaster.vert/.frag, zero attachments

    // 📝 One set per caster mesh. Each carries the SAME b0/b1 (atlas + mapping) and its OWN b2 (that mesh's instances), so a set is a complete binding
    //    for one caster and nothing is rewritten between draws.
    VkDescriptorSet       CasterSets[ShadowCasterSetCapacity]    = {};              // [-] - allocated up front, b2 written on demand
    VkBuffer              CasterBuffers[ShadowCasterSetCapacity] = {};              // [-] - the instance SSBO in each set (borrowed)
    uint32_t              CasterSetCount  = 0;                 // [-] - sets whose b2 has been pointed at a buffer

    uint32_t              LastDrawnLevels = 0;                 // [-]  - levels drawn on the most recent record
    uint32_t              LastPageCount   = 0;                 // [page] - pages S7 declared rendered most recently
    bool                  ReadyCondition  = false;             // [-] - true once pipeline + descriptors are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the set layout, the zero-attachment graphics pipeline, the pool + one set, and point b0/b1 at the atlas's storage view and mapping buffer.
// ShaderDirectory locates ShadowDepthRaster.vert.spv / .frag.spv. The atlas must already be initialized (its views and mapping buffer are baked into
// the descriptor here). Returns false with ReadyCondition false and handles null on any failure — the caller degrades to no shadows.
// ⚠️ Requires fragmentStoresAndAtomics; the caller must gate on VulkanHost::FragmentStoresAndAtomicsEnabled, exactly as S2 does.
bool InitializeShadowDepthRasterSubmission(ShadowDepthRasterSubmission& Raster,
                                           VulkanHost&                  Host,
                                           const ShadowPageAtlas&       Atlas,
                                           const char*                  ShaderDirectory);

// Acquire a descriptor set whose binding 2 points at InstanceBuffer, for use as RecordShadowDepthRaster's CasterSet. Returns the SAME set for a repeat
// call with the same handle (so calling this every image is free), or VK_NULL_HANDLE when not ready or the capacity is exhausted.
// 🔴 One call per caster mesh, and the returned set must be kept for that mesh. Passing two different buffers returns two different sets precisely so
//    that no draw's descriptor is rewritten while another draw referencing it is still queued.
// ⚠️ Allocating a NEW set must not run while a frame is in flight (call at build or while idle); the cached-hit path touches no descriptor and is safe
//    to call any time.
VkDescriptorSet AcquireShadowCasterSet(ShadowDepthRasterSubmission& Raster, VkBuffer InstanceBuffer, VkDeviceSize InstanceBytes);

// S7: rasterize InstanceCount casters of one mesh into the pages that need redrawing, one draw per clipmap level. Opens its own zero-attachment
// rendering scope sized to one level's tile window, pushes each level's basis / origin / window centre, and issues an instanced vkCmdDrawIndexed per
// level. CasterSet must come from AcquireShadowCasterSet for the instance buffer that matches Mesh.
// 📝 Call once per caster mesh. The passes accumulate: every mesh's atomics resolve into the same pages, and because min is commutative the order the
//    meshes are recorded in cannot change the result.
// 🔴 The atlas must be in VK_IMAGE_LAYOUT_GENERAL, S6 must have cleared this image's pages already, and the mapping upload (S5) must have landed —
//    rasterizing against an un-primed page resolves imageAtomicMin against uninitialized memory, which is garbage depth rather than "no caster".
// 🔴 Does NOT clear ContentStale. Marking pages rendered is a separate, explicit step so that the flag can only be cleared by a caller that knows every
//    caster mesh's draw was actually submitted — clearing after the first of two meshes would cache a half-drawn page.
// ⚠️ The command buffer must be OUTSIDE any rendering scope. Returns the number of level draws issued.
uint32_t RecordShadowDepthRaster(ShadowDepthRasterSubmission& Raster,
                                 const ShadowPageAtlas&       Atlas,
                                 const SunShadowClipmap&      Clipmap,
                                 VkDescriptorSet              CasterSet,
                                 const PolygonBufferAllocation& Mesh,
                                 uint32_t                     InstanceCount,
                                 VkCommandBuffer              CommandBuffer);

// Destroy the pipeline, layout and descriptors, then reset to empty. The device must be idle. Safe on a never-initialized value. Does NOT free the
// borrowed mesh or instance buffer.
void FinalizeShadowDepthRasterSubmission(ShadowDepthRasterSubmission& Raster);

} // namespace Frontier

#endif
