#pragma once
/*==========================================================================================================================================
                                                    VOLUMETRICFLOWSOLVER.H
==========================================================================================================================================*/
// 🧩 Host-side uniform transcription and multigrid dispatch planning for the retained flow solver.
//    This describes GPU work but does not allocate volumes, submit commands, or render the editor.

#include "VolumetricFlowAuthoring.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier
{

inline constexpr std::size_t FlowUniformFieldCount = 47;
inline constexpr std::size_t FlowUniformFloatCount = 48; // WGSL uniform buffer rounds 47 floats up to 192 bytes

struct alignas(16) FlowGpuParameters
{
    std::array<float, FlowUniformFloatCount> Fields{};
};
static_assert(sizeof(FlowGpuParameters) == 192);

struct FlowUniformTranscription
{
    FlowGpuParameters Gpu;
    uint32_t SubstepCount = 1;
    uint64_t DemandedCount = 1;
    double ResidualCourant = 0.0;
};

// 📝 The authoring values must include the retained solver's 47 finite fields; missing or invalid
//    input is rejected. VoxelExtent and ElapsedInterval come from the running simulation, not dials.
[[nodiscard]] FlowUniformTranscription TranscribeFlowParameters(const FlowParameters& Parameters,
                                                                uint32_t VoxelExtent, double ElapsedInterval);

struct alignas(16) FlowLevelUniform
{
    float LevelExtent = 0.0f;
    float CoarseExtent = 0.0f;
    float SmoothWeight = 2.0f / 3.0f;
    float Padding = 0.0f;
};
static_assert(sizeof(FlowLevelUniform) == 16);

enum class FlowProjectionKernel : uint8_t
{
    RelaxPressure,
    EvaluateResidual,
    RestrictResidual,
    ClearPressureLevel,
    ProlongateCorrection
};

enum class FlowVolumeKind : uint8_t
{
    Velocity,
    Scalar,
    Pressure,
    RightHand,
    Residual,
    DivergenceIdle
};

// A named resource identity, not a Vulkan handle. For ping-pong resources Slot is 0 or 1;
// Level is used only by the per-level pressure, right-hand and residual volumes.
struct FlowVolumeReference
{
    FlowVolumeKind Kind = FlowVolumeKind::Velocity;
    uint32_t Level = 0;
    uint8_t Slot = 0;

    [[nodiscard]] bool operator==(const FlowVolumeReference& Other) const noexcept
    {
        return Kind == Other.Kind && Level == Other.Level && Slot == Other.Slot;
    }
};

struct FlowProjectionDispatch
{
    FlowProjectionKernel Kernel = FlowProjectionKernel::RelaxPressure;
    uint32_t Level = 0; // binding 7; RestrictResidual dispatches over the next coarser extent
    std::array<uint32_t, 3> Workgroups{};
    // Image bindings 1,2,3,4,5,6,8, in that order. Bindings 0 and 7 are the flow and level uniforms.
    std::array<FlowVolumeReference, 7> Volumes{};
};

struct FlowProjectionPlan
{
    std::vector<FlowLevelUniform> Levels;
    std::vector<FlowProjectionDispatch> Dispatches;
    std::vector<uint8_t> FinalPressureReadSlots;
};

[[nodiscard]] std::vector<uint32_t> DeriveFlowLevelExtents(uint32_t VoxelExtent);
// 📝 InitialPressureReadSlots is either empty (all slot 0) or one 0/1 entry per level.
//    The returned slots must be carried into the next frame. The reference UI limits cycles to 1..8;
//    this planner permits 1..1024 after the reference's rounding, then rejects unreasonable allocations.
[[nodiscard]] FlowProjectionPlan PlanFlowPressureSolve(uint32_t VoxelExtent, double RequestedCycles,
                                                       const std::vector<uint8_t>& InitialPressureReadSlots = {});

enum class FlowSolverKernel : uint8_t
{
    InjectScalarSource, AccumulateInfluence, AdvectVelocityCarrier, RestoreVorticity, EvaluateDivergence,
    RelaxPressure, EvaluateResidual, RestrictResidual, ClearPressureLevel, ProlongateCorrection,
    SubtractPressureGradient, AdvectScalarCarrier
};

enum class FlowBindingLayout : uint8_t { Transport, Projection };

struct FlowSolverReadSlots
{
    uint8_t Velocity = 0;
    uint8_t Scalar = 0;
    std::vector<uint8_t> Pressure; // empty means slot 0 at every level
};

struct FlowSolverDispatch
{
    FlowSolverKernel Kernel = FlowSolverKernel::InjectScalarSource;
    FlowBindingLayout Layout = FlowBindingLayout::Transport;
    uint32_t Level = 0; // ignored for transport; selects the projection level uniform otherwise
    std::array<uint32_t, 3> Workgroups{};
    // Transport: bindings 1,2,4,5 occupy the first four entries. Projection: bindings 1,2,3,4,5,6,8.
    std::array<FlowVolumeReference, 7> Volumes{};
};

struct FlowSolverStepPlan
{
    FlowUniformTranscription Uniform;
    std::vector<FlowLevelUniform> Levels;
    std::vector<FlowSolverDispatch> Dispatches;
    FlowSolverReadSlots FinalReadSlots;
};

// 📝 Produces the reference's complete source → forces → substepped velocity → vorticity → divergence →
//    V-cycles → gradient → substepped scalar sequence. These are descriptors only, not GPU submissions.
[[nodiscard]] FlowSolverStepPlan PlanFlowSolverStep(const FlowParameters& Parameters, uint32_t VoxelExtent,
                                                    double ElapsedInterval, const FlowSolverReadSlots& Initial = {});

struct FlowViewCamera
{
    double Azimuth = 0.62;
    double Pitch = 0.24;
    double OrbitRadius = 2.05;
    double FieldFactor = 0.62;
    std::array<double, 3> ShadowDirection = { -0.42, 0.80, 0.43 };
};

struct alignas(16) FlowViewGpuParameters
{
    std::array<float, 52> Fields{};
};
static_assert(sizeof(FlowViewGpuParameters) == 208);

// 📝 Mirrors the retained orbit camera and 49 populated view slots; the last three slots remain zero.
[[nodiscard]] FlowViewGpuParameters TranscribeFlowView(const FlowParameters& View, const FlowViewCamera& Camera,
                                                        uint32_t PixelWidth, uint32_t PixelHeight, double ElapsedInterval);

struct FlowIrradianceProfile
{
    uint32_t CacheExtent = 32;
    uint32_t PropagationCount = 4;
    double EmissionStrength = 1.35;
    double SunStrength = 1.0;
    double GroundStrength = 0.45;
    double PropagationGain = 0.55;
    double ScatterAlbedo = 0.92;
    std::array<double, 3> SunDirection = { -0.42, 0.80, 0.43 };
    std::array<double, 3> SkyColour = { 0.34, 0.42, 0.58 };
    std::array<double, 3> GroundColour = { 0.32, 0.26, 0.20 };
};

struct alignas(16) FlowIrradianceGpuParameters
{
    std::array<float, 20> Fields{};
};
static_assert(sizeof(FlowIrradianceGpuParameters) == 80);

enum class FlowIrradianceUnit : uint8_t { Seed, Propagate };
struct FlowIrradianceDispatch
{
    FlowIrradianceUnit Unit = FlowIrradianceUnit::Seed;
    std::array<uint32_t, 3> Workgroups{};
    uint8_t ScalarReadSlot = 0;
    uint8_t CacheReadSlot = 0;
    uint8_t CacheWriteSlot = 1;
};
struct FlowIrradiancePlan
{
    FlowIrradianceGpuParameters Uniform;
    std::vector<FlowIrradianceDispatch> Dispatches;
    uint8_t FinalCacheReadSlot = 0;
};

// 📝 Uses the same normalized key-light direction as the retained raymarch. Descriptors only; no GPU submission.
[[nodiscard]] FlowIrradiancePlan PlanFlowIrradiance(const FlowParameters& View, const FlowIrradianceProfile& Profile = {},
                                                     uint8_t ScalarReadSlot = 0, uint8_t CacheReadSlot = 0);

} // namespace Frontier
