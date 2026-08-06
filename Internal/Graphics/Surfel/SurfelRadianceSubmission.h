/*==============================================================================================================================================
                                                        SURFELRADIANCESUBMISSION.H
==============================================================================================================================================*/
// 🧩 The frame's traced rays, recorded as ONE compute dispatch over the claimed ray budget: every ray the lifecycle's census asked for is walked
//    through the software two-level BVH, shaded against the sun and the analytic sky, bounced, and written back into the shared outcome pool for
//    the integrate to reweight. Ported 1:1 from W298/SurfelGI's SurfelRayTrace — whose raygen / closesthit / miss shaders collapse into a single
//    `.comp` here because Frontier has no ray-tracing pipeline and no VK_KHR_ray_query (plan §5). Owns one pipeline and two descriptor sets;
//    BORROWS every buffer, from SurfelStore on set 0 and from the acceleration units on set 1.
//
// 🔴 TWO SETS, AND THE SPLIT IS NOT COSMETIC. Set 0 is the store — the same twelve bindings, at the same ordinals, that
//    Shaders/SurfelStoreBindings.glsl declares and that SurfelLifecycleSubmission builds — so the trace reads the cell grid and the ray pool the
//    lifecycle just wrote without a second copy of anything. Set 1 is the SCENE: the seven read-only streams TwoLevelTrace.glsl walks. They are
//    separated because they have different lifetimes: the store is allocated once and resident forever, while the scene's buffers are rebuilt
//    whenever the geometry arena or the instance tree is repacked. ⚠️ Merging them into one set would mean rewriting twelve descriptors every time
//    a mesh is added.
//
// 🔴 THE SET-1 HANDLES ARE NOT DISCOVERED HERE, THEY ARE HANDED IN. This unit does not know how to reach the arena or the tree — RenderExtension
//    does, through RetrieveGeometryArenaBuffers / RetrieveInstanceTreeBuffers — so the caller supplies all seven in a SurfelSceneBinding and this
//    unit only writes them. 📝 That keeps the dependency pointing one way: Surfel depends on nothing in Acceleration, at compile time or link time.
//
// ⚠️ THE DISPATCH IS SIZED FROM THE RAY BUDGET, NOT FROM THE FRAME'S CLAIM. How many rays the census actually claimed lives in the counter buffer
//    on the device and is not knowable host-side without a stall, so the whole budget is dispatched and the shader early-outs against the counter
//    it reads itself (SurfelRadianceTrace.comp's entry). Upstream does the same. 💡 8192 groups of 64 that return on their first instruction cost
//    far less than the readback that would size them exactly, and the dispatch can never disagree with the counter.
//
// 📝 The caller owns the barrier on BOTH sides. The lifecycle's scatter must be visible before this reads the cell grid, and this pass's outcome
//    writes must be visible before the integrate reads them — neither fence belongs to a unit that knows only its own half.

#pragma once
#ifndef FRONTIER_GRAPHICS_SURFEL_SURFELRADIANCESUBMISSION_H
#define FRONTIER_GRAPHICS_SURFEL_SURFELRADIANCESUBMISSION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Surfel/SurfelStore.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 THE HOST MIRROR OF THE PUSH BLOCK IN Shaders/SurfelRadianceTrace.comp, FIELD FOR FIELD IN THE SAME ORDER. Scalars only, for the same reason
//    every record in SurfelTypes.h is scalar: a vec3 in the GLSL block would carry 16-byte alignment and shift every field after it — which reads
//    as a plausible sun in the wrong place rather than as an error. The asserts at the foot of this header pin every offset.
// ⚠️ SunRadiance IS PREMULTIPLIED colour x intensity, matching SurfaceShadeConstants::SunRadiance. The trace has no second intensity scalar, so a
//    caller that pushes an unmultiplied colour here lights the bounce at a different exposure than the direct pass and the two never agree.
struct SurfelRadianceConstants
{
    float    CameraX = 0.0f, CameraY = 0.0f, CameraZ = 0.0f;                  // [m]       - the frame's camera origin; the cell grid is relative to it
    float    SunDirectionX = 0.0f, SunDirectionY = 1.0f, SunDirectionZ = 0.0f;// [-]       - unit vector TOWARD the sun, matching the shade's LightDirection
    float    SunRadianceR = 0.0f, SunRadianceG = 0.0f, SunRadianceB = 0.0f;   // [W/m²/sr] - key-light radiance, PREMULTIPLIED colour x intensity
    float    SkyHorizonR = 0.0f, SkyHorizonG = 0.0f, SkyHorizonB = 0.0f;      // [W/m²/sr] - miss radiance at the horizon
    float    SkyZenithR = 0.0f, SkyZenithG = 0.0f, SkyZenithB = 0.0f;         // [W/m²/sr] - miss radiance at the zenith
    float    SkyIntensity = 1.0f;                                             // [-]       - scales the whole miss term; 0 makes an interior go black
    float    AlbedoScale = 1.0f;                                              // [-]       - multiplies the instance Tint stand-in for the missing BRDF
    uint32_t FrameOrdinal = 0u;                                               // [-]       - seeds the per-ray generator; MUST advance every frame
    uint32_t BounceLimit = 1u;                                                // [-]       - scatter bounces per ray (upstream gRayStep)
    uint32_t StepCeiling = 4u;                                                // [-]       - hard loop bound (upstream gMaxStep); the shader caps it again
    uint32_t InstanceCount = 0u;                                              // [-]       - TLAS leaves; the trace's TraceInstanceCount
    uint32_t SliceCount = 0u;                                                 // [-]       - slice table entries; the trace's TraceSliceCount
};

// 📝 The seven scene streams set 1 points at, in BINDING ORDER — index i of the write array below is binding i. Every one is borrowed: this unit
//    never allocates, never frees and never maps them. The names are the roles the shader reads them under, not the owning unit's names, because
//    that is the only thing both sides agree on.
// ⚠️ A null handle in any field is refused rather than written. Vulkan does not permit a partially-written set at dispatch, and a set with one
//    binding left unwritten is undefined behaviour that most drivers do not report.
struct SurfelSceneBinding
{
    VkBuffer Instances      = VK_NULL_HANDLE;   // [-] - binding 0, SceneInstance[] (Model, NormalBasis, Tint, MeshOrdinal, InverseModel)
    VkBuffer Slices         = VK_NULL_HANDLE;   // [-] - binding 1, GeometryArenaSlice[] (stride 32; NodeOffset is in WORDS)
    VkBuffer ArenaNodes     = VK_NULL_HANDLE;   // [-] - binding 2, the packed bottom-level node words
    VkBuffer ArenaPrimitives = VK_NULL_HANDLE;  // [-] - binding 3, the bottom-level primitive ordinals
    VkBuffer TreeNodes      = VK_NULL_HANDLE;   // [-] - binding 4, the packed top-level node words
    VkBuffer MeshIndices    = VK_NULL_HANDLE;   // [-] - binding 5, the shared index stream
    VkBuffer Vertices       = VK_NULL_HANDLE;   // [-] - binding 6, the shared vertex stream (RenderVertex, stride 32)
};

// 📝 One pipeline and the plumbing it needs. ShaderDirectory is not retained — the pipeline is built once at initialize and the directory has no
//    meaning afterwards, exactly as in SurfelLifecycleSubmission.
struct SurfelRadianceSubmission
{
    VulkanHost* Host = nullptr;                                   // [-] - not owned; supplies device / queue / allocator

    VkDescriptorSetLayout StoreSetLayout  = VK_NULL_HANDLE;       // [-] - set 0: the twelve store bindings
    VkDescriptorSetLayout SceneSetLayout  = VK_NULL_HANDLE;       // [-] - set 1: the seven scene streams
    VkPipelineLayout      PipelineLayout  = VK_NULL_HANDLE;       // [-] - both sets + the SurfelRadianceConstants push range
    VkDescriptorPool      DescriptorPool  = VK_NULL_HANDLE;       // [-] - holds both sets
    VkDescriptorSet       StoreSet        = VK_NULL_HANDLE;       // [-] - the store's buffers, written when the handles change
    VkDescriptorSet       SceneSet        = VK_NULL_HANDLE;       // [-] - the scene's buffers, written by RefreshSurfelRadianceSceneBinding

    VkPipeline TracePipeline = VK_NULL_HANDLE;                    // [-] - SurfelRadianceTrace.comp

    VkBuffer BoundRecordBuffer = VK_NULL_HANDLE;                  // [-] - the store handle set 0 was last written against; guards a stale set
    VkBuffer BoundVertexBuffer = VK_NULL_HANDLE;                  // [-] - the scene handle set 1 was last written against; same guard
    uint32_t RayBudget         = 0u;                              // [-] - outcome-pool entries the dispatch is sized from
    bool     SceneCondition    = false;                           // [-] - true once set 1 has been written at least once; the record is a no-op until
    bool     ReadyCondition    = false;                           // [-] - true only once both layouts, both sets and the pipeline exist
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build both set layouts, the pipeline layout with the push range, the descriptor pool and its two sets, and the compute pipeline from
// SurfelRadianceTrace.comp.spv in ShaderDirectory. Writes set 0 from Store; leaves set 1 UNWRITTEN.
//
// Two-phase, no-partial-submission contract matching InitializeSurfelLifecycleSubmission: Submission is reset to its empty value first and
// ReadyCondition is raised only once every step succeeded; any failure releases what was claimed and returns false with all handles null. Records
// nothing and submits nothing. Pair with FinalizeSurfelRadianceSubmission.
//
// ⚠️ A submission that returns true still records NOTHING until RefreshSurfelRadianceSceneBinding has succeeded once. The scene's buffers do not
//    exist yet at the moment the renderer builds its pipelines, so demanding them here would order the two units the wrong way round.
bool InitializeSurfelRadianceSubmission(SurfelRadianceSubmission& Submission,
                                        VulkanHost&               Host,
                                        const SurfelStore&        Store,
                                        const char*               ShaderDirectory);

// Point set 1 at the seven scene streams and raise SceneCondition. Returns false, changing nothing, if any handle is null or the submission is not
// ready. Cheap enough to call every frame, but it is a descriptor write and not free — the caller should call it when the acceleration units
// republish, not unconditionally.
// ⚠️ The device must not be executing a frame that reads this set. Vulkan forbids updating a descriptor set that is in use by a pending command
//    buffer, and this is the one write in this unit that can plausibly happen mid-flight.
bool RefreshSurfelRadianceSceneBinding(SurfelRadianceSubmission& Submission, const SurfelSceneBinding& Binding);

// Record the single trace dispatch onto CommandBuffer: bind both sets, push the constants, dispatch the ray budget. Records only — no submit, no
// fence, no wait; this rides the caller's frame command buffer.
//
// 🔴 THE CALLER OWNS THE BARRIER ON BOTH SIDES. Before: the lifecycle's scatter and counter writes must be visible to this pass's reads. After: this
//    pass's outcome writes must be visible to the integrate. Neither belongs here — this unit knows only one end of each hazard.
// 📝 A no-op (recording nothing at all) when the submission is not ready, the scene has never been bound, or the store is not ready — so a failed
//    pipeline build or a scene that has not published yet degrades to "no traced rays this frame" rather than to a dispatch against null descriptors.
void RecordSurfelRadianceTrace(SurfelRadianceSubmission&      Submission,
                               const SurfelStore&             Store,
                               const SurfelRadianceConstants& Constants,
                               VkCommandBuffer                CommandBuffer);

// Destroy the pipeline, both layouts and the descriptor pool, then reset Submission to its empty value. Null-guarded on every branch, so it is safe
// on an empty or partially-built submission and is idempotent. ⚠️ The device must be idle, or the caller must otherwise guarantee no frame in flight
// still references this pipeline.
void FinalizeSurfelRadianceSubmission(SurfelRadianceSubmission& Submission);

//------------------------------------------------------------------------------------------------------------------------
//                                                        LAYOUT ASSERTS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The push block is the one place this unit and its shader exchange values without a descriptor to check them. std430 scalars have 4-byte
//    alignment and no interior padding on either side, so these offsets are the whole contract — and a reordering that leaves sizeof() unchanged is
//    exactly what they exist to catch. Every field is listed, not every fourth, because the failure of a shifted colour triple is a plausible image.
static_assert(offsetof(SurfelRadianceConstants, CameraX)       ==  0, "Push CameraX must sit at 0");
static_assert(offsetof(SurfelRadianceConstants, CameraY)       ==  4, "Push CameraY must sit at 4");
static_assert(offsetof(SurfelRadianceConstants, CameraZ)       ==  8, "Push CameraZ must sit at 8");
static_assert(offsetof(SurfelRadianceConstants, SunDirectionX) == 12, "Push SunDirectionX must sit at 12");
static_assert(offsetof(SurfelRadianceConstants, SunDirectionY) == 16, "Push SunDirectionY must sit at 16");
static_assert(offsetof(SurfelRadianceConstants, SunDirectionZ) == 20, "Push SunDirectionZ must sit at 20");
static_assert(offsetof(SurfelRadianceConstants, SunRadianceR)  == 24, "Push SunRadianceR must sit at 24");
static_assert(offsetof(SurfelRadianceConstants, SunRadianceG)  == 28, "Push SunRadianceG must sit at 28");
static_assert(offsetof(SurfelRadianceConstants, SunRadianceB)  == 32, "Push SunRadianceB must sit at 32");
static_assert(offsetof(SurfelRadianceConstants, SkyHorizonR)   == 36, "Push SkyHorizonR must sit at 36");
static_assert(offsetof(SurfelRadianceConstants, SkyHorizonG)   == 40, "Push SkyHorizonG must sit at 40");
static_assert(offsetof(SurfelRadianceConstants, SkyHorizonB)   == 44, "Push SkyHorizonB must sit at 44");
static_assert(offsetof(SurfelRadianceConstants, SkyZenithR)    == 48, "Push SkyZenithR must sit at 48");
static_assert(offsetof(SurfelRadianceConstants, SkyZenithG)    == 52, "Push SkyZenithG must sit at 52");
static_assert(offsetof(SurfelRadianceConstants, SkyZenithB)    == 56, "Push SkyZenithB must sit at 56");
static_assert(offsetof(SurfelRadianceConstants, SkyIntensity)  == 60, "Push SkyIntensity must sit at 60");
static_assert(offsetof(SurfelRadianceConstants, AlbedoScale)   == 64, "Push AlbedoScale must sit at 64");
static_assert(offsetof(SurfelRadianceConstants, FrameOrdinal)  == 68, "Push FrameOrdinal must sit at 68");
static_assert(offsetof(SurfelRadianceConstants, BounceLimit)   == 72, "Push BounceLimit must sit at 72");
static_assert(offsetof(SurfelRadianceConstants, StepCeiling)   == 76, "Push StepCeiling must sit at 76");
static_assert(offsetof(SurfelRadianceConstants, InstanceCount) == 80, "Push InstanceCount must sit at 80");
static_assert(offsetof(SurfelRadianceConstants, SliceCount)    == 84, "Push SliceCount must sit at 84");
static_assert(sizeof(SurfelRadianceConstants) == 88, "The push block is 88 bytes; the GLSL block must match exactly");

// 📝 Vulkan guarantees only 128 bytes of push range, so this must stay inside it — it does, at 88, with 40 bytes of headroom. ⚠️ That headroom is
//    what Phase 7's ray-guiding fields will spend; a block that grows past 128 needs a uniform buffer instead, not a bigger push.
static_assert(sizeof(SurfelRadianceConstants) <= 128, "Push blocks above 128 B are not portably available");

} // namespace Frontier

#endif
