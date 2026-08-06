/*==============================================================================================================================================
                                                      SURFELIRRADIANCESUBMISSION.H
==============================================================================================================================================*/
// 🧩 Phase 7's two dispatches, owned together: the INTEGRATE, which turns each surfel's traced rays into filtered radiance and writes both atlases,
//    and the SPAWN, which reads the visibility buffer and decides where new surfels go and which over-covered ones die. Ported 1:1 from W298/SurfelGI
//    (SurfelIntegratePass + SurfelGenerationPass). Owns two pipelines and four descriptor sets; BORROWS every buffer and both images.
//
// 🔴 THE TWO PASSES SHARE ONE UNIT BECAUSE THEY SHARE A FRAME-ORDERING CONTRACT, NOT BECAUSE THEY SHARE CODE. The spawn must run AFTER the integrate:
//    a surfel seeded this frame inherits its neighbours' radiance (SurfelSpawnRasterization.comp ⑥), and those neighbours' radiance is exactly what
//    the integrate just published. Recording them from one unit is what makes that order visible at the call site instead of implied by two
//    independent Record* calls a caller could reorder without noticing. ⚠️ The caller still owns the BARRIER between them — see the record functions.
//
// 🔴 FOUR SETS, TWO PER PASS, AND THEY CANNOT BE MERGED. Both passes take the store's twelve bindings at set 0 (the same layout
//    Shaders/SurfelStoreBindings.glsl declares), but their set 1s are completely different resources: the integrate's is the two ATLAS IMAGES it
//    writes, and the spawn's is the VISIBILITY BUFFER plus six geometry streams it reads. A pipeline layout names its sets positionally, so each pass
//    needs its own pair — and the store layout, being identical, is created once and handed to both.
//
// 🔴 THE SPAWN'S SET 1 IS NOT WRITTEN AT INITIALIZE, FOR THE SAME REASON THE TRACE'S SCENE SET IS NOT. The visibility buffer is resized with the
//    swapchain and the floor streams appear only when a floor document loads, so both are republished during the renderer's life. RefreshSurfelSpawnSurfaceBinding
//    fills it and raises SpawnCondition; RecordSurfelSpawn records NOTHING until that has succeeded once. 📝 The integrate's atlas set IS written at
//    initialize, because the store owns both images for its whole life and they never change handle.
//
// ⚠️ BOTH ATLASES MUST BE IN VK_IMAGE_LAYOUT_GENERAL WHEN THE INTEGRATE RUNS. InitializeSurfelStore leaves them there and nothing in this chain moves
//    them, so in the steady state this costs nothing.
//    📝 RESOLVED — the shade's read does NOT transition them. SurfaceShadeInscription writes its depth-atlas descriptor with imageLayout GENERAL and
//       samples through it, which a combined-image-sampler descriptor is entitled to do, so the atlases stay in GENERAL for their whole life and the
//       per-frame round trip this note warned about never exists. 🔴 Any future consumer that DOES want SHADER_READ_ONLY_OPTIMAL owns transitioning them
//       back before the next frame's integrate — the cheaper answer above is the reason nobody has had to.

#pragma once
#ifndef FRONTIER_GRAPHICS_SURFEL_SURFELIRRADIANCESUBMISSION_H
#define FRONTIER_GRAPHICS_SURFEL_SURFELIRRADIANCESUBMISSION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Surfel/SurfelStore.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 THE HOST MIRROR OF THE PUSH BLOCK IN Shaders/SurfelRadianceIntegrate.comp, FIELD FOR FIELD IN THE SAME ORDER. Scalars only, for the reason every
//    record in SurfelTypes.h is scalar: a vec3 in the GLSL block carries 16-byte alignment and shifts every field after it. The asserts at the foot of
//    this header pin every offset.
struct SurfelIrradianceConstants
{
    float    CameraX = 0.0f, CameraY = 0.0f, CameraZ = 0.0f;   // [m] - 🔴 MUST be the same bytes the lifecycle was pushed; see below
    float    ShortMeanWindow = 0.08f;                          // [-] - the MSME's fast-window blend (upstream gShortMeanWindow)
    float    VarianceSensitivity = 1.0f;                       // [-] - how hard a noisy surfel leans on its neighbours; 0 disables sharing's effect
    uint32_t FrameOrdinal = 0u;                                // [-] - carried for parity with the other passes
    uint32_t GuidingCondition = 1u;                             // [-] - non-zero writes the guiding atlas (upstream gUseRayGuiding)
    uint32_t DepthCondition = 1u;                               // [-] - non-zero writes the depth-moment atlas (upstream gUseSurfelDepth)
    uint32_t SharingCondition = 1u;                             // [-] - non-zero enables the neighbour blend (upstream gUseIrradianceSharing)
};

// 🔴 THE HOST MIRROR OF Shaders/SurfelSpawnRasterization.comp's PUSH BLOCK. The mat4 leads because std430 gives it 16-byte alignment either way; every
//    field after it is a scalar. ⚠️ THIS BLOCK IS EXACTLY 128 BYTES — THE WHOLE PORTABLE PUSH BUDGET, WITH NOTHING LEFT. Adding one more field puts it
//    over Vulkan's guaranteed maxPushConstantsSize; the fix then is to move InverseViewProjection into a uniform buffer, NOT to assume the 256 bytes
//    every desktop driver Frontier targets happens to report. The static_assert at the foot of this header is what will tell you.
struct SurfelSpawnConstants
{
    float    InverseViewProjection[16] = {};   // [-]   - clip -> world; the SAME matrix SurfaceShade.frag is pushed, or surfels land off-surface

    float    CameraX = 0.0f, CameraY = 0.0f, CameraZ = 0.0f;   // [m]   - the frame's camera origin
    float    VerticalFieldOfView = 0.0f;                       // [rad] - vertical FOV, for the screen-projected radius
    float    TargetArea = 40000.0f;                            // [px²] - screen area one surfel aims to cover

    float    PlacementThreshold = 24000.0f;   // [px²] - a pixel covered by more than this is already explained; no spawn
    float    RemovalThreshold = 48000.0f;     // [px²] - a pixel covered by more than this is over-served; its most redundant surfel is marked dead
    float    ChanceMultiply = 0.005f;         // [-]   - scales the spawn probability (upstream gChanceMultiply)
    float    ChancePower = 1.0f;              // [-]   - the depth exponent in that probability (upstream gChancePower)

    // 🔴 BOTH AXES IN ONE WORD, LOW 16 BITS X AND HIGH 16 BITS Y — NOT TWO FIELDS, AND THE PACKING IS NOT A MICRO-OPTIMIZATION. Spelled as two uints
    //    this block is 132 bytes, and Vulkan GUARANTEES only 128; every desktop driver Frontier targets reports 256, but a block that depends on
    //    exceeding the guarantee is one unusual device away from silently truncated constants. ⚠️ Both axes must stay under 65536, which they are by
    //    several orders of magnitude for any render target. Use ComposeSurfelSpawnExtent below rather than shifting by hand.
    uint32_t ResolutionPacked = 0u;                // [px] - 🔴 the SAME extent the lifecycle was pushed; see the shader's note
    uint32_t FrameOrdinal = 0u;                    // [-]  - seeds the per-pixel generator; MUST advance every frame
    uint32_t PerCellLimit = 64u;                   // [-]  - a cell holding this many surfels accepts no more
    uint32_t FloorPartitionBase = 0u;              // [-]  - partition ordinals at or above this belong to the floor mesh
    uint32_t FloorCondition = 0u;                  // [-]  - non-zero means the floor streams are real, not the head-buffer alias
    uint32_t FloorIndexBase = 0u;                  // [-]  - first index of the floor's run in the merged index stream
    uint32_t LockEnabled = 0u;                     // [-]  - non-zero freezes the field: the tile still tallies, nothing spawns or dies
};

// The one way to fill ResolutionPacked. ⚠️ An axis at or above 65536 cannot be represented and is CLAMPED rather than allowed to wrap into the other
// axis' half of the word — a wrapped extent would read as a plausible-but-wrong resolution and quietly mis-scale every screen-space radius in the pass.
// 📝 Mirrors ResolveSurfelSpawnExtent() in the shader; the two must be edited together.
inline uint32_t ComposeSurfelSpawnExtent(uint32_t ResolutionX, uint32_t ResolutionY)
{
    const uint32_t ClampedX = ResolutionX > 0xFFFFu ? 0xFFFFu : ResolutionX;
    const uint32_t ClampedY = ResolutionY > 0xFFFFu ? 0xFFFFu : ResolutionY;
    return (ClampedY << 16u) | ClampedX;
}

// 📝 The seven resources the spawn's set 1 points at, in BINDING ORDER. Every one is borrowed. ⚠️ When no floor document is loaded the caller ALIASES
//    the three floor handles onto the head handles rather than leaving them null — Vulkan forbids a partially-written set — and pushes
//    FloorCondition 0 so the shader knows they are not real. A null in any field is refused rather than written.
struct SurfelSpawnSurfaceBinding
{
    VkImageView VisibilityView = VK_NULL_HANDLE;    // [-] - binding 0, the R32_UINT identity buffer's view
    VkSampler   VisibilitySampler = VK_NULL_HANDLE; // [-] - binding 0's sampler; NEAREST, and the shader only ever texelFetch()es through it
    VkBuffer    Vertices = VK_NULL_HANDLE;          // [-] - binding 1, RenderVertex[] (stride 32)
    VkBuffer    MeshIndices = VK_NULL_HANDLE;       // [-] - binding 2, the shared index stream
    VkBuffer    Instances = VK_NULL_HANDLE;         // [-] - binding 3, SceneInstance[] (std140, stride 208)
    VkBuffer    FloorVertices = VK_NULL_HANDLE;     // [-] - binding 4, the floor's own vertices (or the head alias)
    VkBuffer    FloorIndices = VK_NULL_HANDLE;      // [-] - binding 5, the floor's own indices (or the head alias)
    VkBuffer    FloorInstances = VK_NULL_HANDLE;    // [-] - binding 6, the floor's own instances (or the head alias)
};

// 📝 Two pipelines and the plumbing they need. ShaderDirectory is not retained — both pipelines are built once at initialize.
struct SurfelIrradianceSubmission
{
    VulkanHost* Host = nullptr;                                     // [-] - not owned; supplies device / queue / allocator

    VkDescriptorSetLayout StoreSetLayout    = VK_NULL_HANDLE;       // [-] - the twelve store bindings; SHARED by both pipeline layouts
    VkDescriptorSetLayout AtlasSetLayout    = VK_NULL_HANDLE;       // [-] - the integrate's set 1: two storage images
    VkDescriptorSetLayout SurfaceSetLayout  = VK_NULL_HANDLE;       // [-] - the spawn's set 1: one sampled image + six storage buffers

    VkPipelineLayout      IntegrateLayout   = VK_NULL_HANDLE;       // [-] - store + atlas + SurfelIrradianceConstants
    VkPipelineLayout      SpawnLayout       = VK_NULL_HANDLE;       // [-] - store + surface + SurfelSpawnConstants

    VkDescriptorPool      DescriptorPool    = VK_NULL_HANDLE;       // [-] - holds all four sets
    VkDescriptorSet       IntegrateStoreSet = VK_NULL_HANDLE;       // [-] - the store, for the integrate
    VkDescriptorSet       SpawnStoreSet     = VK_NULL_HANDLE;       // [-] - the store, for the spawn
    VkDescriptorSet       AtlasSet          = VK_NULL_HANDLE;       // [-] - both atlases, written at initialize
    VkDescriptorSet       SurfaceSet        = VK_NULL_HANDLE;       // [-] - the visible surface, written by the refresh

    VkPipeline IntegratePipeline = VK_NULL_HANDLE;                  // [-] - SurfelRadianceIntegrate.comp
    VkPipeline SpawnPipeline     = VK_NULL_HANDLE;                  // [-] - SurfelSpawnRasterization.comp

    VkBuffer BoundRecordBuffer = VK_NULL_HANDLE;                    // [-] - the store handle both store sets were last written against
    bool     SpawnCondition    = false;                             // [-] - true once the spawn's set 1 has been written; RecordSurfelSpawn no-ops until
    bool     ReadyCondition    = false;                             // [-] - true only once every layout, set and pipeline exists
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the three set layouts, both pipeline layouts, the descriptor pool and its four sets, and both compute pipelines from
// SurfelRadianceIntegrate.comp.spv and SurfelSpawnRasterization.comp.spv in ShaderDirectory. Writes both store sets and the atlas set from Store;
// leaves the spawn's surface set UNWRITTEN.
//
// Two-phase, no-partial-submission contract matching the other surfel units: Submission is reset to its empty value first and ReadyCondition is raised
// only once every step succeeded; any failure releases what was claimed and returns false with all handles null. Records nothing and submits nothing.
// Pair with FinalizeSurfelIrradianceSubmission.
bool InitializeSurfelIrradianceSubmission(SurfelIrradianceSubmission& Submission,
                                          VulkanHost&                 Host,
                                          const SurfelStore&          Store,
                                          const char*                 ShaderDirectory);

// Point the spawn's set 1 at the visibility buffer and the six geometry streams, and raise SpawnCondition. Returns false, changing nothing, if any
// handle is null or the submission is not ready.
// ⚠️ The device must not be executing a frame that reads this set — Vulkan forbids updating a descriptor set in use by a pending command buffer. Call it
//    when the swapchain resizes or the floor document changes, not unconditionally every frame.
bool RefreshSurfelSpawnSurfaceBinding(SurfelIrradianceSubmission& Submission, const SurfelSpawnSurfaceBinding& Binding);

// Record the integrate dispatch: bind the store and atlas sets, push, dispatch one group of 32 per SurfelTotalLimit slots. Records only.
//
// 🔴 THE CALLER OWNS THE BARRIER ON BOTH SIDES. Before: the trace's ray-outcome writes and the scatter's cell-list writes must be visible to this pass.
//    After: this pass's record writes must be visible to the spawn, which reads the very radiance this publishes in order to seed a new surfel from it.
// 📝 The dispatch covers the whole surfel limit rather than the frame's live count, for the reason every surfel pass does: the count lives on the device
//    and sizing from it host-side would mean a stall. The shader early-outs against the counter it reads itself.
void RecordSurfelIrradianceIntegrate(SurfelIrradianceSubmission&      Submission,
                                     const SurfelStore&               Store,
                                     const SurfelIrradianceConstants& Constants,
                                     VkCommandBuffer                  CommandBuffer);

// Record the spawn dispatch: bind the store and surface sets, push, dispatch one 16x16 group per screen tile. Records only.
//
// 🔴 MUST BE RECORDED AFTER RecordSurfelIrradianceIntegrate, WITH A BARRIER BETWEEN THEM. A surfel seeded this frame inherits its neighbours' radiance,
//    and that radiance is what the integrate just wrote; recording the spawn first seeds every new surfel from last frame's field, which is not wrong
//    so much as one frame stale in a way that compounds while the camera moves.
// 📝 A no-op when the submission is not ready, the surface has never been bound, or the extent in Constants is zero — so a swapchain that has not
//    published yet degrades to "no spawns this frame" rather than to a dispatch of zero groups against unwritten descriptors.
void RecordSurfelSpawn(SurfelIrradianceSubmission& Submission,
                       const SurfelStore&          Store,
                       const SurfelSpawnConstants& Constants,
                       VkCommandBuffer             CommandBuffer);

// Destroy both pipelines, both pipeline layouts, all three set layouts and the descriptor pool, then reset Submission to its empty value. Null-guarded
// on every branch, so it is safe on an empty or partially-built submission and is idempotent. ⚠️ The device must be idle, or the caller must otherwise
// guarantee no frame in flight still references these pipelines.
void FinalizeSurfelIrradianceSubmission(SurfelIrradianceSubmission& Submission);

//------------------------------------------------------------------------------------------------------------------------
//                                                        LAYOUT ASSERTS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The push blocks are the one place each pass and its shader exchange values without a descriptor to check them. std430 scalars have 4-byte
//    alignment and no interior padding on either side, so these offsets are the whole contract — and a reordering that leaves sizeof() unchanged is
//    exactly what they exist to catch.
static_assert(offsetof(SurfelIrradianceConstants, CameraX)             ==  0, "Integrate CameraX must sit at 0");
static_assert(offsetof(SurfelIrradianceConstants, CameraY)             ==  4, "Integrate CameraY must sit at 4");
static_assert(offsetof(SurfelIrradianceConstants, CameraZ)             ==  8, "Integrate CameraZ must sit at 8");
static_assert(offsetof(SurfelIrradianceConstants, ShortMeanWindow)     == 12, "Integrate ShortMeanWindow must sit at 12");
static_assert(offsetof(SurfelIrradianceConstants, VarianceSensitivity) == 16, "Integrate VarianceSensitivity must sit at 16");
static_assert(offsetof(SurfelIrradianceConstants, FrameOrdinal)        == 20, "Integrate FrameOrdinal must sit at 20");
static_assert(offsetof(SurfelIrradianceConstants, GuidingCondition)    == 24, "Integrate GuidingCondition must sit at 24");
static_assert(offsetof(SurfelIrradianceConstants, DepthCondition)      == 28, "Integrate DepthCondition must sit at 28");
static_assert(offsetof(SurfelIrradianceConstants, SharingCondition)    == 32, "Integrate SharingCondition must sit at 32");
static_assert(sizeof(SurfelIrradianceConstants) == 36, "The integrate push block is 36 bytes; the GLSL block must match exactly");

// ⚠️ The matrix occupies 0..63 as sixteen consecutive floats — a float[16], not a glm::mat4, so this header pulls in no maths dependency and the
//    COLUMN-MAJOR order the caller must fill it in is stated rather than assumed. GLSL's mat4 is column-major, so element [c*4 + r].
static_assert(offsetof(SurfelSpawnConstants, InverseViewProjection) ==   0, "Spawn InverseViewProjection must sit at 0");
static_assert(offsetof(SurfelSpawnConstants, CameraX)              ==  64, "Spawn CameraX must sit at 64");
static_assert(offsetof(SurfelSpawnConstants, CameraY)              ==  68, "Spawn CameraY must sit at 68");
static_assert(offsetof(SurfelSpawnConstants, CameraZ)              ==  72, "Spawn CameraZ must sit at 72");
static_assert(offsetof(SurfelSpawnConstants, VerticalFieldOfView)  ==  76, "Spawn VerticalFieldOfView must sit at 76");
static_assert(offsetof(SurfelSpawnConstants, TargetArea)           ==  80, "Spawn TargetArea must sit at 80");
static_assert(offsetof(SurfelSpawnConstants, PlacementThreshold)   ==  84, "Spawn PlacementThreshold must sit at 84");
static_assert(offsetof(SurfelSpawnConstants, RemovalThreshold)     ==  88, "Spawn RemovalThreshold must sit at 88");
static_assert(offsetof(SurfelSpawnConstants, ChanceMultiply)       ==  92, "Spawn ChanceMultiply must sit at 92");
static_assert(offsetof(SurfelSpawnConstants, ChancePower)          ==  96, "Spawn ChancePower must sit at 96");
static_assert(offsetof(SurfelSpawnConstants, ResolutionPacked)     == 100, "Spawn ResolutionPacked must sit at 100");
static_assert(offsetof(SurfelSpawnConstants, FrameOrdinal)         == 104, "Spawn FrameOrdinal must sit at 104");
static_assert(offsetof(SurfelSpawnConstants, PerCellLimit)         == 108, "Spawn PerCellLimit must sit at 108");
static_assert(offsetof(SurfelSpawnConstants, FloorPartitionBase)   == 112, "Spawn FloorPartitionBase must sit at 112");
static_assert(offsetof(SurfelSpawnConstants, FloorCondition)       == 116, "Spawn FloorCondition must sit at 116");
static_assert(offsetof(SurfelSpawnConstants, FloorIndexBase)       == 120, "Spawn FloorIndexBase must sit at 120");
static_assert(offsetof(SurfelSpawnConstants, LockEnabled)          == 124, "Spawn LockEnabled must sit at 124");
static_assert(sizeof(SurfelSpawnConstants) == 128, "The spawn push block is 128 bytes; the GLSL block must match exactly");

// 🔴 128 IS NOT A COMFORTABLE NUMBER HERE — IT IS THE ENTIRE PORTABLE BUDGET, MET EXACTLY. VkPhysicalDeviceLimits::maxPushConstantsSize is guaranteed to
//    be AT LEAST 128 bytes and nothing more, so this pair of asserts is the whole portability guarantee for both blocks and there is no headroom left in
//    the spawn's. ⚠️ A field added to SurfelSpawnConstants will trip the second assert; move InverseViewProjection into a uniform buffer at that point
//    rather than shrinking a field or leaning on the 256 bytes real drivers report.
static_assert(sizeof(SurfelIrradianceConstants) <= 128, "Push blocks above 128 B are not portably available");
static_assert(sizeof(SurfelSpawnConstants)      <= 128, "Push blocks above 128 B are not portably available");

} // namespace Frontier

#endif
