/*==========================================================================================================================================
                                                   VOLUMETRICFLOWSOLVER.CPP
==========================================================================================================================================*/

#include "VolumetricFlowSolver.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Frontier
{
namespace
{
constexpr std::array<std::string_view, FlowUniformFieldCount> UniformFields = {
    "Δτ", "VoxelExtent", "ProjectionIterations", "VorticityStrength",
    "DensityBuoyancy", "TemperatureBuoyancy", "AmbientTemperature", "DensityDissipation",
    "TemperatureDissipation", "VelocityDissipation", "RadiationCoefficient", "InjectionRadius",
    "InjectionDensity", "InjectionTemperature", "WindLongitudinal", "WindGustStrength",
    "ElapsedInterval", "MacCormackEnabled", "VorticityEnabled", "SourceLongitude",
    "SourceAltitude", "SourceLatitude", "WindLateral", "WindVertical",
    "GustFrequency", "FuelYield", "SootYield", "CarrierCeiling",
    "CombustionEnabled", "IgnitionTemperature", "BurnRate", "HeatRelease",
    "SootRelease", "FuelDissipation", "CoreThickness", "CoreLuminosity",
    "AdvectionDeltatau", "AdvectionSubstepCount", "VelocityCeiling", "BoundaryDamping",
    "SourceShape", "SourceMinorScale", "NoiseGain", "NoiseScale",
    "NoiseDrift", "SwirlStrength", "InjectionVelocity"
};

FlowVolumeReference Velocity(uint8_t Slot) { return { FlowVolumeKind::Velocity, 0, Slot }; }
FlowVolumeReference Scalar(uint8_t Slot) { return { FlowVolumeKind::Scalar, 0, Slot }; }
FlowVolumeReference Pressure(uint32_t Level, uint8_t Slot) { return { FlowVolumeKind::Pressure, Level, Slot }; }
FlowVolumeReference RightHand(uint32_t Level) { return { FlowVolumeKind::RightHand, Level, 0 }; }
FlowVolumeReference Residual(uint32_t Level) { return { FlowVolumeKind::Residual, Level, 0 }; }
FlowVolumeReference Idle() { return { FlowVolumeKind::DivergenceIdle, 0, 0 }; }
uint32_t DivideRoundUp(uint32_t Value, uint32_t Divisor) { return Value / Divisor + (Value % Divisor != 0); }
}

FlowUniformTranscription TranscribeFlowParameters(const FlowParameters& Parameters,
                                                  uint32_t VoxelExtent, double ElapsedInterval)
{
    if (VoxelExtent == 0 || !std::isfinite(ElapsedInterval))
        throw std::invalid_argument("Invalid flow grid extent or elapsed interval");
    std::array<double, FlowUniformFieldCount> Values{};
    for (std::size_t Index = 0; Index < UniformFields.size(); ++Index)
    {
        const auto Found = Parameters.find(UniformFields[Index]);
        if (Found == Parameters.end() || !std::isfinite(Found->second))
            throw std::invalid_argument("Missing or non-finite flow field: " + std::string(UniformFields[Index]));
        Values[Index] = Found->second;
    }
    const double Courant = 0.35 * Values[0] * VoxelExtent;
    if (!std::isfinite(Courant)) throw std::invalid_argument("Non-finite flow Courant number");
    const double Demand = std::max(1.0, std::ceil(Courant / 0.9));
    if (!std::isfinite(Demand) || Demand >= static_cast<double>(std::numeric_limits<uint64_t>::max()))
        throw std::invalid_argument("Flow timestep demands too many substeps");
    FlowUniformTranscription Result;
    Result.DemandedCount = static_cast<uint64_t>(Demand);
    Result.SubstepCount = static_cast<uint32_t>(std::min<uint64_t>(16, Result.DemandedCount));
    Result.ResidualCourant = Courant / Result.SubstepCount;
    Values[1] = VoxelExtent;
    Values[16] = ElapsedInterval;
    Values[36] = Values[0] / Result.SubstepCount;
    Values[37] = Result.SubstepCount;
    for (std::size_t Index = 0; Index < Values.size(); ++Index)
        Result.Gpu.Fields[Index] = static_cast<float>(Values[Index]);
    if (Result.SubstepCount > 1)
    {
        for (const std::size_t Index : { 7u, 8u, 9u, 33u })
        {
            if (Values[Index] < 0.0) throw std::invalid_argument("Negative flow retention cannot be substepped");
            Result.Gpu.Fields[Index] = static_cast<float>(std::pow(Values[Index], 1.0 / Result.SubstepCount));
        }
    }
    return Result;
}

std::vector<uint32_t> DeriveFlowLevelExtents(uint32_t VoxelExtent)
{
    if (VoxelExtent == 0) throw std::invalid_argument("Flow grid extent must be positive");
    std::vector<uint32_t> Extents = { VoxelExtent };
    while (VoxelExtent % 2 == 0 && VoxelExtent / 2 >= 8)
    {
        VoxelExtent /= 2;
        Extents.push_back(VoxelExtent);
    }
    return Extents;
}

FlowProjectionPlan PlanFlowPressureSolve(uint32_t VoxelExtent, double RequestedCycles,
                                        const std::vector<uint8_t>& InitialPressureReadSlots)
{
    if (!std::isfinite(RequestedCycles)) throw std::invalid_argument("Non-finite projection cycle count");
    const double RoundedCycles = std::max(1.0, std::floor(RequestedCycles + 0.5));
    if (RoundedCycles > 1024.0) throw std::invalid_argument("Projection cycle count exceeds planning limit");
    const auto Extents = DeriveFlowLevelExtents(VoxelExtent);
    if (!InitialPressureReadSlots.empty() && InitialPressureReadSlots.size() != Extents.size())
        throw std::invalid_argument("Pressure slots must cover every level");
    FlowProjectionPlan Plan;
    Plan.FinalPressureReadSlots = InitialPressureReadSlots.empty() ? std::vector<uint8_t>(Extents.size(), 0) : InitialPressureReadSlots;
    if (std::any_of(Plan.FinalPressureReadSlots.begin(), Plan.FinalPressureReadSlots.end(), [](uint8_t Slot) { return Slot > 1; }))
        throw std::invalid_argument("Pressure read slot must be zero or one");
    for (std::size_t Level = 0; Level < Extents.size(); ++Level)
        Plan.Levels.push_back({ static_cast<float>(Extents[Level]),
                                static_cast<float>(Level + 1 < Extents.size() ? Extents[Level + 1] : Extents[Level]),
                                2.0f / 3.0f, 0.0f });

    auto MakeDispatch = [&](FlowProjectionKernel Kernel, uint32_t Level, uint32_t DispatchExtent)
    {
        const uint8_t Read = Plan.FinalPressureReadSlots[Level], Write = 1u - Read;
        return FlowProjectionDispatch{ Kernel, Level,
            { DivideRoundUp(DispatchExtent, 8), DivideRoundUp(DispatchExtent, 8), DivideRoundUp(DispatchExtent, 4) },
            { Velocity(0), Pressure(Level, Read), RightHand(Level), Velocity(1), Pressure(Level, Write), Idle(), Residual(Level) } };
    };
    auto Exchange = [&](uint32_t Level) { Plan.FinalPressureReadSlots[Level] ^= 1u; };
    auto Smooth = [&](uint32_t Level, uint32_t Count)
    {
        for (uint32_t Sweep = 0; Sweep < Count; ++Sweep)
        {
            Plan.Dispatches.push_back(MakeDispatch(FlowProjectionKernel::RelaxPressure, Level, Extents[Level]));
            Exchange(Level);
        }
    };
    const auto Descend = [&](auto&& Recurse, uint32_t Level) -> void
    {
        if (Level + 1 == Extents.size())
        {
            Smooth(Level, 40);
            return;
        }
        Smooth(Level, 2);
        auto Evaluate = MakeDispatch(FlowProjectionKernel::EvaluateResidual, Level, Extents[Level]);
        Evaluate.Volumes[5] = Residual(Level); // binding 6 writes residual
        Evaluate.Volumes[6] = Idle();          // binding 8 cannot alias that storage image
        Plan.Dispatches.push_back(Evaluate);
        auto Restrict = MakeDispatch(FlowProjectionKernel::RestrictResidual, Level, Extents[Level + 1]);
        Restrict.Volumes[5] = RightHand(Level + 1);
        Plan.Dispatches.push_back(Restrict);
        Plan.Dispatches.push_back(MakeDispatch(FlowProjectionKernel::ClearPressureLevel, Level + 1, Extents[Level + 1]));
        Exchange(Level + 1);
        Recurse(Recurse, Level + 1);
        auto Prolongate = MakeDispatch(FlowProjectionKernel::ProlongateCorrection, Level, Extents[Level]);
        Prolongate.Volumes[6] = Pressure(Level + 1, Plan.FinalPressureReadSlots[Level + 1]);
        Plan.Dispatches.push_back(Prolongate);
        Exchange(Level);
        Smooth(Level, 2);
    };
    for (uint32_t Cycle = 0; Cycle < static_cast<uint32_t>(RoundedCycles); ++Cycle)
        Descend(Descend, 0);
    return Plan;
}

FlowSolverStepPlan PlanFlowSolverStep(const FlowParameters& Parameters, uint32_t VoxelExtent,
                                      double ElapsedInterval, const FlowSolverReadSlots& Initial)
{
    if (Initial.Velocity > 1 || Initial.Scalar > 1)
        throw std::invalid_argument("Carrier read slot must be zero or one");
    FlowSolverStepPlan Plan;
    Plan.Uniform = TranscribeFlowParameters(Parameters, VoxelExtent, ElapsedInterval);
    const auto PressurePlan = PlanFlowPressureSolve(VoxelExtent, Parameters.at("ProjectionIterations"), Initial.Pressure);
    Plan.Levels = PressurePlan.Levels;
    Plan.FinalReadSlots = Initial;
    if (Plan.FinalReadSlots.Pressure.empty()) Plan.FinalReadSlots.Pressure.assign(Plan.Levels.size(), 0);
    const std::array<uint32_t, 3> Groups = { DivideRoundUp(VoxelExtent, 8), DivideRoundUp(VoxelExtent, 8), DivideRoundUp(VoxelExtent, 4) };
    auto Transport = [&](FlowSolverKernel Kernel)
    {
        FlowSolverDispatch Entry;
        Entry.Kernel = Kernel;
        Entry.Layout = FlowBindingLayout::Transport;
        Entry.Workgroups = Groups;
        Entry.Volumes = { Velocity(Plan.FinalReadSlots.Velocity), Scalar(Plan.FinalReadSlots.Scalar),
                          Velocity(1u - Plan.FinalReadSlots.Velocity), Scalar(1u - Plan.FinalReadSlots.Scalar), {}, {}, {} };
        Plan.Dispatches.push_back(Entry);
    };
    auto Projection = [&](FlowSolverKernel Kernel)
    {
        const uint8_t VelocityRead = Plan.FinalReadSlots.Velocity;
        const uint8_t PressureRead = Plan.FinalReadSlots.Pressure[0];
        FlowSolverDispatch Entry;
        Entry.Kernel = Kernel;
        Entry.Layout = FlowBindingLayout::Projection;
        Entry.Workgroups = Groups;
        Entry.Volumes = { Velocity(VelocityRead), Pressure(0, PressureRead), RightHand(0),
                          Velocity(1u - VelocityRead), Pressure(0, 1u - PressureRead), Idle(), Residual(0) };
        if (Kernel == FlowSolverKernel::EvaluateDivergence)
        {
            Entry.Volumes[2] = Idle();
            Entry.Volumes[5] = RightHand(0);
        }
        Plan.Dispatches.push_back(Entry);
    };
    Transport(FlowSolverKernel::InjectScalarSource);
    Plan.FinalReadSlots.Scalar ^= 1u;
    Transport(FlowSolverKernel::AccumulateInfluence);
    Plan.FinalReadSlots.Velocity ^= 1u;
    for (uint32_t Substep = 0; Substep < Plan.Uniform.SubstepCount; ++Substep)
    {
        Transport(FlowSolverKernel::AdvectVelocityCarrier);
        Plan.FinalReadSlots.Velocity ^= 1u;
    }
    Projection(FlowSolverKernel::RestoreVorticity);
    Plan.FinalReadSlots.Velocity ^= 1u;
    Projection(FlowSolverKernel::EvaluateDivergence);
    for (const auto& Dispatch : PressurePlan.Dispatches)
    {
        FlowSolverKernel Kernel = FlowSolverKernel::RelaxPressure;
        switch (Dispatch.Kernel)
        {
            case FlowProjectionKernel::RelaxPressure: Kernel = FlowSolverKernel::RelaxPressure; break;
            case FlowProjectionKernel::EvaluateResidual: Kernel = FlowSolverKernel::EvaluateResidual; break;
            case FlowProjectionKernel::RestrictResidual: Kernel = FlowSolverKernel::RestrictResidual; break;
            case FlowProjectionKernel::ClearPressureLevel: Kernel = FlowSolverKernel::ClearPressureLevel; break;
            case FlowProjectionKernel::ProlongateCorrection: Kernel = FlowSolverKernel::ProlongateCorrection; break;
        }
        FlowSolverDispatch Entry{ Kernel, FlowBindingLayout::Projection, Dispatch.Level, Dispatch.Workgroups, Dispatch.Volumes };
        Entry.Volumes[0] = Velocity(Plan.FinalReadSlots.Velocity);
        Entry.Volumes[3] = Velocity(1u - Plan.FinalReadSlots.Velocity);
        Plan.Dispatches.push_back(Entry);
    }
    Plan.FinalReadSlots.Pressure = PressurePlan.FinalPressureReadSlots;
    Projection(FlowSolverKernel::SubtractPressureGradient);
    Plan.FinalReadSlots.Velocity ^= 1u;
    for (uint32_t Substep = 0; Substep < Plan.Uniform.SubstepCount; ++Substep)
    {
        Transport(FlowSolverKernel::AdvectScalarCarrier);
        Plan.FinalReadSlots.Scalar ^= 1u;
    }
    return Plan;
}

FlowViewGpuParameters TranscribeFlowView(const FlowParameters& View, const FlowViewCamera& Camera,
                                          uint32_t PixelWidth, uint32_t PixelHeight, double ElapsedInterval)
{
    const auto Read = [&](std::string_view Field)
    {
        const auto Found = View.find(Field);
        if (Found == View.end() || !std::isfinite(Found->second))
            throw std::invalid_argument("Missing or non-finite flow view field: " + std::string(Field));
        return Found->second;
    };
    if (!std::isfinite(Camera.Azimuth) || !std::isfinite(Camera.Pitch) || !std::isfinite(Camera.OrbitRadius) ||
        !std::isfinite(Camera.FieldFactor) || !std::isfinite(ElapsedInterval))
        throw std::invalid_argument("Non-finite flow view camera or clock");
    const double PitchCosine = std::cos(Camera.Pitch);
    const std::array<double, 3> Origin = {
        0.5 + Camera.OrbitRadius * PitchCosine * std::sin(Camera.Azimuth),
        0.5 + Camera.OrbitRadius * std::sin(Camera.Pitch),
        0.5 + Camera.OrbitRadius * PitchCosine * std::cos(Camera.Azimuth)
    };
    std::array<double, 3> Forward = { 0.5 - Origin[0], 0.5 - Origin[1], 0.5 - Origin[2] };
    const double ForwardLength = std::hypot(Forward[0], Forward[1], Forward[2]);
    for (double& Component : Forward) Component /= ForwardLength > 0.0 ? ForwardLength : 1.0;
    std::array<double, 3> Right = { Forward[2], 0.0, -Forward[0] };
    const double RightLength = std::hypot(Right[0], Right[1], Right[2]);
    for (double& Component : Right) Component /= RightLength > 0.0 ? RightLength : 1.0;
    const std::array<double, 3> Up = { Right[1] * Forward[2] - Right[2] * Forward[1],
                                       Right[2] * Forward[0] - Right[0] * Forward[2],
                                       Right[0] * Forward[1] - Right[1] * Forward[0] };
    const double ShadowLength = std::hypot(Camera.ShadowDirection[0], Camera.ShadowDirection[1], Camera.ShadowDirection[2]);
    if (!std::isfinite(ShadowLength) || ShadowLength == 0.0)
        throw std::invalid_argument("Flow view key direction must be finite and nonzero");
    const std::array<double, 49> Components = {
        Origin[0], Origin[1], Origin[2], Read("MarchStepCount"),
        Right[0] * Camera.FieldFactor, Right[1] * Camera.FieldFactor, Right[2] * Camera.FieldFactor, Read("DensityScale"),
        Up[0] * Camera.FieldFactor, Up[1] * Camera.FieldFactor, Up[2] * Camera.FieldFactor, Read("RadianceScale"),
        Forward[0], Forward[1], Forward[2], static_cast<double>(PixelWidth) / std::max(1u, PixelHeight),
        Camera.ShadowDirection[0] / ShadowLength, Camera.ShadowDirection[1] / ShadowLength,
        Camera.ShadowDirection[2] / ShadowLength, Read("ShadowStepCount"),
        Read("ScatterAnisotropy"), Read("ChannelSelector"), Read("ExposureScale"), Read("SootOpacity"),
        Read("TemperatureFloor"), Read("TemperatureSpan"), Read("EmissionThreshold"), Read("SmokeAlbedo"),
        Read("AbsorptionTintR"), Read("AbsorptionTintG"), Read("AbsorptionTintB"), Read("SpectralStrength"),
        Read("CoreThickness"), Read("CoreLuminosity"), Read("SaturationGain"), Read("HighlightShoulder"),
        Read("PaletteBlend"), Read("SmokeRampScale"), Read("IrradianceBlend"), Read("TurbulenceStrength"),
        Read("TurbulenceDetail"), Read("TurbulenceBand"), Read("TurbulenceDrift"), ElapsedInterval,
        Read("ScatterBandCount"), Read("ScatterExtinctionDecay"), Read("ScatterContributionDecay"),
        Read("ScatterAnisotropyDecay"), Read("MultiScatterAmbient")
    };
    FlowViewGpuParameters Result;
    for (std::size_t Index = 0; Index < Components.size(); ++Index)
    {
        if (!std::isfinite(Components[Index])) throw std::invalid_argument("Non-finite flow view staging field");
        Result.Fields[Index] = static_cast<float>(Components[Index]);
        if (!std::isfinite(Result.Fields[Index])) throw std::invalid_argument("Flow view field exceeds float range");
    }
    return Result;
}

FlowIrradiancePlan PlanFlowIrradiance(const FlowParameters& View, const FlowIrradianceProfile& Profile,
                                      uint8_t ScalarReadSlot, uint8_t CacheReadSlot)
{
    if (Profile.CacheExtent == 0 || Profile.PropagationCount > 1024 || ScalarReadSlot > 1 || CacheReadSlot > 1)
        throw std::invalid_argument("Invalid irradiance extent, pass count or carrier slot");
    const auto Read = [&](std::string_view Field)
    {
        const auto Found = View.find(Field);
        if (Found == View.end() || !std::isfinite(Found->second))
            throw std::invalid_argument("Missing or non-finite irradiance view field: " + std::string(Field));
        return Found->second;
    };
    const double Length = std::hypot(Profile.SunDirection[0], Profile.SunDirection[1], Profile.SunDirection[2]);
    if (!std::isfinite(Length)) throw std::invalid_argument("Non-finite irradiance key direction");
    const double Divisor = Length > 0.0 ? Length : 1.0;
    const std::array<double, 20> Components = {
        static_cast<double>(Profile.CacheExtent), Read("DensityScale"), Read("SootOpacity"), Profile.EmissionStrength,
        Profile.SunDirection[0] / Divisor, Profile.SunDirection[1] / Divisor, Profile.SunDirection[2] / Divisor, Profile.SunStrength,
        Profile.SkyColour[0], Profile.SkyColour[1], Profile.SkyColour[2], Profile.GroundStrength,
        Profile.GroundColour[0], Profile.GroundColour[1], Profile.GroundColour[2], Profile.PropagationGain,
        Read("TemperatureFloor"), Read("TemperatureSpan"), Read("EmissionThreshold"), Profile.ScatterAlbedo
    };
    FlowIrradiancePlan Plan;
    for (std::size_t Index = 0; Index < Components.size(); ++Index)
    {
        if (!std::isfinite(Components[Index])) throw std::invalid_argument("Non-finite irradiance profile field");
        Plan.Uniform.Fields[Index] = static_cast<float>(Components[Index]);
        if (!std::isfinite(Plan.Uniform.Fields[Index])) throw std::invalid_argument("Irradiance field exceeds float range");
    }
    const uint32_t Group = DivideRoundUp(Profile.CacheExtent, 4);
    const std::array<uint32_t, 3> Groups = { Group, Group, Group };
    Plan.FinalCacheReadSlot = CacheReadSlot;
    for (uint32_t Index = 0; Index <= Profile.PropagationCount; ++Index)
    {
        Plan.Dispatches.push_back({ Index == 0 ? FlowIrradianceUnit::Seed : FlowIrradianceUnit::Propagate,
                                    Groups, ScalarReadSlot, Plan.FinalCacheReadSlot,
                                    static_cast<uint8_t>(1u - Plan.FinalCacheReadSlot) });
        Plan.FinalCacheReadSlot ^= 1u;
    }
    return Plan;
}

} // namespace Frontier
