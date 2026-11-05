/*==========================================================================================================================================
                                                   FLOWSOLVERVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Emits native uniform and multigrid plans for direct comparison with the retained JavaScript.

#include "Engine/Render/VolumetricFlowSolver.h"

#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace Frontier;

namespace
{
void Require(bool Condition, const char* Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

template <typename Action>
void RequireFailure(Action Function, const char* Message)
{
    try { Function(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(Message);
}

const char* KernelName(FlowProjectionKernel Kernel)
{
    switch (Kernel)
    {
        case FlowProjectionKernel::RelaxPressure: return "RelaxPressure";
        case FlowProjectionKernel::EvaluateResidual: return "EvaluateResidual";
        case FlowProjectionKernel::RestrictResidual: return "RestrictResidual";
        case FlowProjectionKernel::ClearPressureLevel: return "ClearPressureLevel";
        case FlowProjectionKernel::ProlongateCorrection: return "ProlongateCorrection";
    }
    throw std::runtime_error("Unknown projection kernel");
}

const char* SolverKernelName(FlowSolverKernel Kernel)
{
    switch (Kernel)
    {
        case FlowSolverKernel::InjectScalarSource: return "InjectScalarSource";
        case FlowSolverKernel::AccumulateInfluence: return "AccumulateInfluence";
        case FlowSolverKernel::AdvectVelocityCarrier: return "AdvectVelocityCarrier";
        case FlowSolverKernel::RestoreVorticity: return "RestoreVorticity";
        case FlowSolverKernel::EvaluateDivergence: return "EvaluateDivergence";
        case FlowSolverKernel::RelaxPressure: return "RelaxPressure";
        case FlowSolverKernel::EvaluateResidual: return "EvaluateResidual";
        case FlowSolverKernel::RestrictResidual: return "RestrictResidual";
        case FlowSolverKernel::ClearPressureLevel: return "ClearPressureLevel";
        case FlowSolverKernel::ProlongateCorrection: return "ProlongateCorrection";
        case FlowSolverKernel::SubtractPressureGradient: return "SubtractPressureGradient";
        case FlowSolverKernel::AdvectScalarCarrier: return "AdvectScalarCarrier";
    }
    throw std::runtime_error("Unknown solver kernel");
}

std::string VolumeName(const FlowVolumeReference& Volume)
{
    switch (Volume.Kind)
    {
        case FlowVolumeKind::Velocity: return "V" + std::to_string(Volume.Slot);
        case FlowVolumeKind::Scalar: return "S" + std::to_string(Volume.Slot);
        case FlowVolumeKind::Pressure: return "P" + std::to_string(Volume.Level) + "_" + std::to_string(Volume.Slot);
        case FlowVolumeKind::RightHand: return "R" + std::to_string(Volume.Level);
        case FlowVolumeKind::Residual: return "E" + std::to_string(Volume.Level);
        case FlowVolumeKind::DivergenceIdle: return "D";
    }
    throw std::runtime_error("Unknown volume kind");
}

void ValidateNoAliasing(const FlowProjectionDispatch& Dispatch)
{
    for (const std::size_t Sampled : { 0u, 1u, 2u, 6u })
        for (const std::size_t Stored : { 3u, 4u, 5u })
            Require(!(Dispatch.Volumes[Sampled] == Dispatch.Volumes[Stored]), "sampled/storage image alias");
    Require(!(Dispatch.Volumes[3] == Dispatch.Volumes[4]) && !(Dispatch.Volumes[3] == Dispatch.Volumes[5]) &&
            !(Dispatch.Volumes[4] == Dispatch.Volumes[5]), "storage image alias");
}

void PrintFixture(uint32_t Extent, double Cycles, double Timestep, double Elapsed, uint32_t InitialMask)
{
    FlowParameters Parameters = FlowSolverDefaults();
    Parameters["Δτ"] = Timestep;
    Parameters["ProjectionIterations"] = Cycles;
    const auto Extents = DeriveFlowLevelExtents(Extent);
    std::vector<uint8_t> Slots(Extents.size());
    for (std::size_t Index = 0; Index < Slots.size(); ++Index)
        Slots[Index] = static_cast<uint8_t>((InitialMask >> Index) & 1u);
    const auto Uniform = TranscribeFlowParameters(Parameters, Extent, Elapsed);
    const auto Plan = PlanFlowPressureSolve(Extent, Cycles, Slots);
    FlowSolverReadSlots Initial{ static_cast<uint8_t>((InitialMask >> 16u) & 1u),
                                 static_cast<uint8_t>((InitialMask >> 17u) & 1u), Slots };
    const auto Step = PlanFlowSolverStep(Parameters, Extent, Elapsed, Initial);
    Require(Plan.Levels.size() == Extents.size(), "level count");
    std::cout << "{\"uniform\":[";
    for (std::size_t Index = 0; Index < Uniform.Gpu.Fields.size(); ++Index)
    {
        if (Index) std::cout << ',';
        std::cout << Uniform.Gpu.Fields[Index];
    }
    std::cout << "],\"substeps\":" << Uniform.SubstepCount << ",\"demanded\":" << Uniform.DemandedCount
              << ",\"courant\":" << Uniform.ResidualCourant << ",\"levels\":[";
    for (std::size_t Index = 0; Index < Plan.Levels.size(); ++Index)
    {
        if (Index) std::cout << ',';
        const auto& Level = Plan.Levels[Index];
        std::cout << '[' << Level.LevelExtent << ',' << Level.CoarseExtent << ',' << Level.SmoothWeight << ',' << Level.Padding << ']';
    }
    std::cout << "],\"dispatches\":[";
    for (std::size_t Index = 0; Index < Plan.Dispatches.size(); ++Index)
    {
        if (Index) std::cout << ',';
        const auto& Entry = Plan.Dispatches[Index];
        ValidateNoAliasing(Entry);
        std::cout << "[\"" << KernelName(Entry.Kernel) << "\"," << Entry.Level << ",[" << Entry.Workgroups[0] << ','
                  << Entry.Workgroups[1] << ',' << Entry.Workgroups[2] << "],[";
        for (std::size_t Binding = 0; Binding < Entry.Volumes.size(); ++Binding)
        {
            if (Binding) std::cout << ',';
            std::cout << '"' << VolumeName(Entry.Volumes[Binding]) << '"';
        }
        std::cout << "]]";
    }
    std::cout << "],\"finalSlots\":[";
    for (std::size_t Index = 0; Index < Plan.FinalPressureReadSlots.size(); ++Index)
    {
        if (Index) std::cout << ',';
        std::cout << unsigned(Plan.FinalPressureReadSlots[Index]);
    }
    std::cout << "],\"stepDispatches\":[";
    for (std::size_t Index = 0; Index < Step.Dispatches.size(); ++Index)
    {
        if (Index) std::cout << ',';
        const auto& Entry = Step.Dispatches[Index];
        const bool Transport = Entry.Layout == FlowBindingLayout::Transport;
        const std::size_t VolumeCount = Transport ? 4 : 7;
        if (Transport)
        {
            for (const std::size_t Sampled : { 0u, 1u })
                for (const std::size_t Stored : { 2u, 3u })
                    Require(!(Entry.Volumes[Sampled] == Entry.Volumes[Stored]), "transport sampled/storage alias");
        }
        else
        {
            for (const std::size_t Sampled : { 0u, 1u, 2u, 6u })
                for (const std::size_t Stored : { 3u, 4u, 5u })
                    Require(!(Entry.Volumes[Sampled] == Entry.Volumes[Stored]), "step sampled/storage alias");
        }
        std::cout << "[\"" << SolverKernelName(Entry.Kernel) << "\",\"" << (Transport ? "transport" : "projection")
                  << "\"," << Entry.Level << ",[" << Entry.Workgroups[0] << ',' << Entry.Workgroups[1] << ','
                  << Entry.Workgroups[2] << "],[";
        for (std::size_t Binding = 0; Binding < VolumeCount; ++Binding)
        {
            if (Binding) std::cout << ',';
            std::cout << '"' << VolumeName(Entry.Volumes[Binding]) << '"';
        }
        std::cout << "]]";
    }
    std::cout << "],\"stepSlots\":{" << "\"velocity\":" << unsigned(Step.FinalReadSlots.Velocity)
              << ",\"scalar\":" << unsigned(Step.FinalReadSlots.Scalar) << ",\"pressure\":[";
    for (std::size_t Index = 0; Index < Step.FinalReadSlots.Pressure.size(); ++Index)
    {
        if (Index) std::cout << ',';
        std::cout << unsigned(Step.FinalReadSlots.Pressure[Index]);
    }
    std::cout << "]}}\n";
}
}

int main()
{
    try
    {
        RequireFailure([] { (void)DeriveFlowLevelExtents(0); }, "zero extent must be rejected");
        RequireFailure([] { (void)PlanFlowPressureSolve(64, 1, {0}); }, "incomplete pressure slots must be rejected");
        RequireFailure([] { (void)PlanFlowPressureSolve(8, 1, {2}); }, "invalid pressure slot must be rejected");
        RequireFailure([] { (void)PlanFlowPressureSolve(8, 1025); }, "unbounded cycles must be rejected");
        RequireFailure([] { (void)PlanFlowSolverStep(FlowSolverDefaults(), 8, 0, { 2, 0, {} }); },
                       "invalid carrier read slot must be rejected");
        RequireFailure([] { auto Params = FlowSolverDefaults(); Params.erase("Δτ");
                            (void)TranscribeFlowParameters(Params, 64, 0); }, "missing uniform field must be rejected");
        RequireFailure([] { auto Params = FlowSolverDefaults(); Params["DensityDissipation"] = -1;
                            (void)TranscribeFlowParameters(Params, 64, 0); }, "negative retention must be rejected");
        RequireFailure([] { auto View = FlowViewDefaults(); View.erase("DensityScale");
                            (void)TranscribeFlowView(View, {}, 1280, 720, 0); }, "missing view field must be rejected");
        RequireFailure([] { FlowViewCamera Camera; Camera.ShadowDirection = { 0, 0, 0 };
                            (void)TranscribeFlowView(FlowViewDefaults(), Camera, 1280, 720, 0); }, "zero view light must be rejected");
        RequireFailure([] { auto View = FlowViewDefaults(); View["DensityScale"] = std::numeric_limits<double>::max();
                            (void)TranscribeFlowView(View, {}, 1, 1, 0); }, "view float overflow must be rejected");
        RequireFailure([] { FlowIrradianceProfile Profile; Profile.CacheExtent = 0;
                            (void)PlanFlowIrradiance(FlowViewDefaults(), Profile); }, "zero cache extent must be rejected");
        RequireFailure([] { FlowIrradianceProfile Profile; Profile.PropagationCount = 1025;
                            (void)PlanFlowIrradiance(FlowViewDefaults(), Profile); }, "excessive cache passes must be rejected");
        RequireFailure([] { FlowIrradianceProfile Profile; Profile.EmissionStrength = std::numeric_limits<double>::max();
                            (void)PlanFlowIrradiance(FlowViewDefaults(), Profile); }, "irradiance float overflow must be rejected");
        const auto Cache = PlanFlowIrradiance(FlowViewDefaults());
        Require(Cache.Dispatches.size() == 5 && Cache.FinalCacheReadSlot == 1, "default irradiance pass count and slot");
        for (const auto& Dispatch : Cache.Dispatches)
            Require(Dispatch.CacheReadSlot != Dispatch.CacheWriteSlot, "irradiance sampled/storage alias");
        const auto View = TranscribeFlowView(FlowViewDefaults(), {}, 1280, 720, 0);
        Require(View.Fields[49] == 0 && View.Fields[50] == 0 && View.Fields[51] == 0, "view padding must be zero");
        std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
        uint32_t Extent = 0, InitialMask = 0;
        double Cycles = 0, Timestep = 0, Elapsed = 0;
        while (std::cin >> Extent >> Cycles >> Timestep >> Elapsed >> InitialMask)
            PrintFixture(Extent, Cycles, Timestep, Elapsed, InitialMask);
        return std::cin.eof() ? 0 : 1;
    }
    catch (const std::exception& Failure)
    {
        std::cerr << "Flow solver validation: " << Failure.what() << '\n';
        return 1;
    }
}
