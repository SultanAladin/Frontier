/*============================================================================================================================================
                                                    MESHBAKEGPUMIRRORVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Pure-CPU proof, runnable with no Vulkan driver at all, that Engine/Render/MeshBakeGpuMirror.cpp - the exact
// structural transcription of Engine/Render/Shader/MeshBakeCompute.slang - reproduces Engine/ContentInterchange/
// BakeMesh.cpp's trusted RunMeshBake output texel for texel, for every implemented mesh map, on every built-in
// demo mesh. It does not and cannot prove the compiled SPIR-V executes correctly on an actual GPU; see that
// shader's own header comment for how this evidence combines with a separate, independent SPIR-V compile check.

#include "Engine/ContentInterchange/BakeMesh.h"
#include "Engine/Render/MeshBakeGpu.h"
#include "Engine/Render/MeshBakeGpuMirror.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace Frontier;

namespace
{
uint32_t Assertions = 0;
void Confirm(bool Condition, const std::string &Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Assertions;
}

void ValidateShape(const std::string &Shape, uint32_t Resolution)
{
    const auto Assets = BuildDemoBakeAssets(Shape);
    Confirm(!Assets.Low.Parts.empty(), Shape + ": demo low mesh is empty.");
    const auto &Target = Assets.Low.Parts.front();

    BakeSettings Settings;
    Settings.Resolution = Resolution;
    Settings.Samples = 10;       // small but nonzero so hemisphere sampling (AO/bent/curvature-adjacent paths) actually runs
    Settings.BevelSamples = 6;
    Settings.Channels = ImplementedMeshMapKeys();

    const std::vector<const BakePart *> Targets{&Target};
    const auto CpuResults = RunMeshBake(Targets, Assets.High, Settings, nullptr);
    Confirm(CpuResults.size() == 1, Shape + ": CPU reference did not return exactly one part result.");
    const auto &CpuMaps = CpuResults.front().Maps;
    Confirm(CpuMaps.size() == ImplementedMeshMapKeys().size(), Shape + ": CPU reference did not emit every requested channel.");

    const auto Plan = BuildMeshBakeGpuPlan(Target, Assets.High, Settings);
    Confirm(Plan.RequestedChannelKeys.size() == CpuMaps.size(), Shape + ": GPU plan channel count diverged from the CPU reference.");
    const auto MirrorMaps = RunMeshBakeGpuMirror(Plan);
    Confirm(MirrorMaps.size() == CpuMaps.size(), Shape + ": GPU mirror did not emit every requested channel.");

    uint64_t TotalTexelsChecked = 0, TotalBytesChecked = 0;
    for (const auto &CpuMap : CpuMaps)
    {
        const auto MirrorIt = std::find_if(MirrorMaps.begin(), MirrorMaps.end(),
            [&](const BakeMapImage &Candidate) { return Candidate.Key == CpuMap.Key; });
        Confirm(MirrorIt != MirrorMaps.end(), Shape + ": GPU mirror is missing channel \"" + CpuMap.Key + "\".");
        Confirm(MirrorIt->Rgba.size() == CpuMap.Rgba.size(), Shape + "/" + CpuMap.Key + ": byte length mismatch.");
        uint32_t WorstDelta = 0;
        size_t WorstIndex = 0;
        for (size_t Index = 0; Index < CpuMap.Rgba.size(); ++Index)
        {
            const int Delta = std::abs(int(CpuMap.Rgba[Index]) - int(MirrorIt->Rgba[Index]));
            if (uint32_t(Delta) > WorstDelta) { WorstDelta = uint32_t(Delta); WorstIndex = Index; }
        }
        // 📝 Tolerance of 1/255 absorbs harmless float-associativity rounding between this file's Vec3 struct
        // math and BakeMesh.cpp's raw float[3] math (same formulas, same operation order, different operand
        // packaging) - anything larger would mean the GPU kernel's math genuinely diverged from the reference.
        Confirm(WorstDelta <= 1, Shape + "/" + CpuMap.Key + ": byte " + std::to_string(WorstIndex) + " differs by " +
            std::to_string(WorstDelta) + " (channels must match the CPU reference within rounding).");
        TotalTexelsChecked += Resolution * Resolution;
        TotalBytesChecked += CpuMap.Rgba.size();
    }
    std::cout << "  " << Shape << ": " << CpuMaps.size() << " channels, " << TotalTexelsChecked << " texels, " << TotalBytesChecked
              << " bytes compared, 0 exceeded tolerance\n";
}
} // namespace

int main()
{
    try
    {
        std::cout << "MeshBakeGpuMirrorValidation: comparing Engine/Render/MeshBakeGpuMirror.cpp against "
                      "Engine/ContentInterchange/BakeMesh.cpp's RunMeshBake on every built-in demo mesh.\n";
        ValidateShape("Cube", 40);
        ValidateShape("Sphere", 40);
        ValidateShape("Teapot", 32);
        std::cout << "MeshBakeGpuMirrorValidation: " << Assertions << " checks passed\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "MeshBakeGpuMirrorValidation failed: " << Failure.what() << "\n";
        return 1;
    }
}
