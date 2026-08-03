/*==============================================================================================================================================
                                                       SURFELINTEGRATESUBMISSION.H
==============================================================================================================================================*/
// 🧩 Phase 2's whole payload: the per-surfel INTEGRATE compute. One dispatch, one lane per surfel slot — shoot SLG-guided cosine-hemisphere rays
//    through TraceTwoLevel, shade each hit (albedo stand-in + a sun shadow ray + a one-bounce grid gather), fold the frame estimate into the MSME
//    moments (RTG ch.25 firefly clamp), learn the radial-depth occlusion tile, and write the surfel's new state into the moments WRITE half. In
//    webgiya the trace is inline (bvhIntersectFirstHit) — there is no standalone trace pass — so this unit records the single SurfelIntegrate.comp
//    dispatch and nothing else. Produces NO on-screen GI (that is Phase 3's gather at SurfaceShade.frag:547); its output is the moments write half,
//    made readable by the caller's SwapSurfelMoments after this records.
//
//    🔴 TWO DESCRIPTOR SETS (fact 5). Set 0 is the BVH, spelled EXACTLY like TwoLevelTraceProbe.comp: b0 Instances, b1 Slices, b2 ArenaNodeWords,
//       b3 ArenaPrimitives, b4 TreeNodeWords, b5 MeshIndices, b6 Vertices — all readonly, all BORROWED (GeometryArena + InstanceTree), bound ONCE at
//       load because the scene is static after load (re-binding an in-flight set is undefined; same bind-once contract as the #26 TLAS wiring). Set 1
//       is the surfel state: b0 Surfels (ro), b1 Moments (rw, one buffer both halves), b2 Offsets (ro), b3 List (ro), b4 Guiding (rw), b5 SurfelDepth
//       (rw), b6 Touched (rw atomic) — all BORROWED from SurfelPool + SurfelGridSlotting.
//
//    🔴 DIRECT DISPATCH, NO INDIRECT (fact 2). ceil(Capacity/64) groups over the whole pool; the .comp's `if (Age >= SURFEL_TTL) return;` is the bounds
//       guard. No IntegratorArgs buffer — one lane owns one slot, so the guiding RMW is safe by construction (settles F8).
//
//    🔴 MOMENTS OFFSETS ARE ELEMENT INDICES (fact 4). AssembleSurfelIntegrateConstants sets ReadOffsetElements / WriteOffsetElements to
//       MomentsParity*Capacity and (1-MomentsParity)*Capacity — the ELEMENT counts, NOT the BYTE offsets SurfelMomentsReadOffset/WriteOffset return.
//       The .comp indexes Moments[index + offset] over the 5-vec4 struct array; a byte offset there corrupts silently.
//
//    📝 POD struct + free functions, mirroring SurfelLifecycleSubmission. Bound* cache the borrowed handles so a descriptor is re-pointed only on
//       change. ReadyCondition gates every record. The whole BVH set + the two #define aliases (InstanceCount/SliceCount) ride the push block.

#pragma once
#ifndef FRONTIER_GRAPHICS_SURFEL_SURFELINTEGRATESUBMISSION_H
#define FRONTIER_GRAPHICS_SURFEL_SURFELINTEGRATESUBMISSION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Surfel/SurfelPool.h"
#include "Graphics/Surfel/SurfelGridSlotting.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 MUST match local_size_x in SurfelIntegrate.comp (64). The dispatch is ceil(Capacity / this) groups over the whole pool.
constexpr uint32_t SurfelIntegrateWorkgroupEdge = 64;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 Byte-compatible with SurfelIntegrate.comp's push_constant block. Seven vec4 rows (112 B) then twelve scalars (48 B) = 160 B, well under 256.
//    The vec4 rows are xyz-meaningful, w as noted; SkyZenith.w carries the sky intensity. The two element offsets are parity*Capacity (fact 4). Field
//    order + padding MUST mirror the .comp exactly or every read past CameraPosition mis-aligns.
struct SurfelIntegrateConstants
{
    float CameraPosition[4]  = { 0, 0, 0, 0 };   // xyz raw eye; w unused
    float GridOrigin[4]      = { 0, 0, 0, 0 };   // xyz snapped grid origin (matches slotting); w unused
    float SunDirection[4]    = { 0, 1, 0, 0 };   // xyz sun direction (shade LightDirection); w unused
    float SunColour[4]       = { 0, 0, 0, 0 };   // xyz LightColour*LightIntensity; w unused
    float SkyGround[4]       = { 0, 0, 0, 0 };   // xyz miss ambient at the horizon; w unused
    float SkyZenith[4]       = { 0, 0, 0, 0 };   // xyz miss ambient at the zenith; w = sky intensity
    float OcclusionParams[4] = { 1.2f, 0.2f, 0.25f, 0.15f };  // (shadowStrength, bleedReduction, grazingBiasScale, varianceBleedScale)

    uint32_t Frame              = 0;   // [-] - frame index (depth-probe bin + noise)
    uint32_t ReadOffsetElements = 0;   // [-] - parity*Capacity (moments read half)
    uint32_t WriteOffsetElements = 0;  // [-] - (1-parity)*Capacity (moments write half)
    uint32_t Capacity           = 0;   // [-] - pool capacity (the dispatch bound)

    uint32_t BaseSampleCount    = 32;  // [-] - samples/surfel before warmup/boost
    uint32_t InstanceCount      = 0;   // [-] - TLAS leaves (TraceInstanceCount)
    uint32_t SliceCount         = 0;   // [-] - slice table entries (TraceSliceCount)
    float    AlbedoBoost        = 1.0f;// [-] - albedo remap strength

    float    GiFromDirect       = 1.0f;// [-] - direct-light gain at a hit
    float    GiFromIndirect     = 1.0f;// [-] - one-bounce gather gain at a hit
    float    TuneCellDiameter   = 1.0f;// [m] - live base cell edge (F10 window); the one-bounce gather's cell must match spawn/slotting
    float    TuneBaseRadius     = 1.2f;// [m] - live cascade-0 disc radius (F10 window)
    float    TuneNearFieldBias  = 1.0f;// [-] - live near-field bias (F10 window; layout parity, unused by integrate)
    float    Pad0               = 0.0f;
};

// 📝 The integrate unit's owned handles. Two set layouts (BVH set 0, surfel set 1), one pipeline layout (both sets + the push range), one pipeline
//    (SurfelIntegrate.comp), a descriptor pool with the two sets. No owned buffers — every binding is borrowed. Bound* cache the borrowed handles so a
//    descriptor is re-pointed only on a handle change. ReadyCondition latches once every layout / pipeline / set is live.
struct SurfelIntegrateSubmission
{
    VulkanHost* Host = nullptr;                              // [-] - not owned

    VkDescriptorSetLayout BvhLayout        = VK_NULL_HANDLE; // [-] - set 0: b0-b6 (all readonly storage)
    VkDescriptorSetLayout SurfelLayout     = VK_NULL_HANDLE; // [-] - set 1: b0-b6 surfel state
    VkPipelineLayout      PipelineLayout   = VK_NULL_HANDLE; // [-] - both sets + SurfelIntegrateConstants push range
    VkPipeline            Pipeline         = VK_NULL_HANDLE; // [-] - SurfelIntegrate.comp
    VkDescriptorSet       BvhSet           = VK_NULL_HANDLE; // [-] - set 0 (bound once at load)
    VkDescriptorSet       SurfelSet        = VK_NULL_HANDLE; // [-] - set 1 (bound once at load)
    VkDescriptorPool      DescriptorPool   = VK_NULL_HANDLE; // [-] - sized for the two sets

    // --- cached borrowed handles (set 0 — BVH) ---
    VkBuffer BoundInstances       = VK_NULL_HANDLE;
    VkBuffer BoundSlices          = VK_NULL_HANDLE;
    VkBuffer BoundArenaNodeWords  = VK_NULL_HANDLE;
    VkBuffer BoundArenaPrimitives = VK_NULL_HANDLE;
    VkBuffer BoundTreeNodeWords   = VK_NULL_HANDLE;
    VkBuffer BoundMeshIndices     = VK_NULL_HANDLE;
    VkBuffer BoundVertices        = VK_NULL_HANDLE;

    // --- cached borrowed handles (set 1 — surfel state) ---
    VkBuffer BoundSurfels     = VK_NULL_HANDLE;
    VkBuffer BoundMoments     = VK_NULL_HANDLE;
    VkBuffer BoundOffsets     = VK_NULL_HANDLE;
    VkBuffer BoundList        = VK_NULL_HANDLE;
    VkBuffer BoundGuiding     = VK_NULL_HANDLE;
    VkBuffer BoundSurfelDepth = VK_NULL_HANDLE;
    VkBuffer BoundTouched     = VK_NULL_HANDLE;

    bool ReadyCondition = false;   // [-] - true once every layout / pipeline / set is live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the two set layouts, the pipeline layout, the SurfelIntegrate.comp pipeline, and the descriptor pool + two sets. ShaderDirectory locates
// SurfelIntegrate.comp.spv. No bindings are pointed here — Refresh does that. Returns false (ReadyCondition stays false, handles null) on any failure.
// Pair with FinalizeSurfelIntegrateSubmission.
bool InitializeSurfelIntegrateSubmission(SurfelIntegrateSubmission& Integrate,
                                         VulkanHost&                Host,
                                         const char*                ShaderDirectory);

// Point set 0 (BVH) at the borrowed GeometryArena + InstanceTree buffers and set 1 at the borrowed SurfelPool + SurfelGridSlotting buffers. All handles
// are stable after load (scene static), so this is called ONCE right after init; it writes only on a handle change, so a re-call is cheap and safe.
// The device must be idle. Pass the arena node/primitive/slice buffers, the TLAS tree node buffer, and the merged vertex/index/instance buffers.
void RefreshSurfelIntegrateBindings(SurfelIntegrateSubmission& Integrate,
                                    const SurfelPool&          Pool,
                                    const SurfelGridSlotting&  Slotting,
                                    VkBuffer                   InstanceBuffer,
                                    VkBuffer                   SliceBuffer,
                                    VkBuffer                   ArenaNodeBuffer,
                                    VkBuffer                   ArenaPrimitiveBuffer,
                                    VkBuffer                   TreeNodeBuffer,
                                    VkBuffer                   IndexBuffer,
                                    VkBuffer                   VertexBuffer);

// Assemble the push block from the camera / snapped grid origin / sun / sky / occlusion inputs plus the pool's live parity + capacity. ReadOffsetElements
// / WriteOffsetElements are set to MomentsParity*Capacity and (1-MomentsParity)*Capacity (fact 4 — ELEMENT counts, not the byte offsets the pool returns).
// InstanceCount is the TLAS leaf count (TraceInstanceCount); SliceCount is the slice table size (TraceSliceCount). Frame drives the depth-probe stride.
SurfelIntegrateConstants AssembleSurfelIntegrateConstants(const SurfelPool& Pool,
                                                          const float       CameraPosition[3],
                                                          const float       GridOrigin[3],
                                                          const float       SunDirection[3],
                                                          const float       SunColour[3],
                                                          const float       SkyGround[3],
                                                          const float       SkyZenith[3],
                                                          float             SkyIntensity,
                                                          uint32_t          Frame,
                                                          uint32_t          InstanceCount,
                                                          uint32_t          SliceCount);

// Record the single integrate dispatch: bind both sets, push the constants, bind the pipeline, dispatch ceil(Capacity/64). A no-op when not ready or
// the pool is not ready. Must be recorded OUTSIDE any rendering scope (compute is illegal inside a dynamic-rendering scope). The caller inserts the
// COMPUTE->COMPUTE read barrier (B1) before and the write-visibility barrier (B3) + SwapSurfelMoments after.
void RecordSurfelIntegrate(SurfelIntegrateSubmission&      Integrate,
                           const SurfelPool&               Pool,
                           const SurfelIntegrateConstants& Constants,
                           VkCommandBuffer                 CommandBuffer);

// Destroy every layout / pipeline / descriptor and reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeSurfelIntegrateSubmission(SurfelIntegrateSubmission& Integrate);

} // namespace Frontier

#endif
