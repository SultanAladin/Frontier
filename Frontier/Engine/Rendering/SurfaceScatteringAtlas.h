/*==========================================================================================================================================
                                                SURFACE SCATTERING ATLAS
                                     ------------------------------------------------------------------
             Bridges TextureWorkspace / TextureBake material contracts (Engine/Render/ScatteringSequence.h
             ScatteringStratum / PreviewScattering + Engine/Render/AutomotivePaint.h) into a contiguous
             GPU float4 material table for GTX-first ReSTIR + SDF + Software BVH illumination.
==========================================================================================================================================*/
// 🧩 TextureWorkspace ScatteringSequence + AutomotivePaint material atlas and GPU float4 table packer.

#pragma once

#include "Engine/Render/AutomotivePaint.h"
#include "Engine/Render/ScatteringSequence.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier
{

struct SurfaceScatteringEntry
{
    uint32_t                ClassificationId    = 0u;
    PreviewScattering       Scattering{};
    AutomotivePaint         Paint{};
    std::array<float, 3>    EmissiveRadianceRgb = {0.0f, 0.0f, 0.0f};
    bool                    TransmissiveGlass   = false;
    float                   GlassIor            = 1.52f;
    std::array<float, 3>    GlassAbsorptionPerM = {0.12f, 0.04f, 0.06f};
};

class SurfaceScatteringAtlas
{
  public:
    static constexpr uint32_t kMaxClassificationSlots = 64u;
    static constexpr uint32_t kFloat4StridePerSlot    = 6u;

    SurfaceScatteringAtlas();

    bool RegisterScatteringEntry(const SurfaceScatteringEntry& Entry);
    void RegisterControlVehicleScatteringPresets();

    [[nodiscard]] const SurfaceScatteringEntry& FindEntry(uint32_t ClassificationId) const noexcept;

    // CPU reference evaluation delegating directly to Engine/Render/ScatteringSequence.h::EvaluateScatteringSequence
    [[nodiscard]] ScatteringSpectrum EvaluateBrdfCpu(uint32_t             ClassificationId,
                                                     std::array<float, 3> SurfaceNormal,
                                                     std::array<float, 3> ViewDirectionToEye,
                                                     std::array<float, 3> LightDirectionToSource) const noexcept;

    // Packs 64 classification slots x 6 float4 records (= 384 float4s) for Slang GPU evaluation:
    //   Slot + 0: {BaseAlbedo.rgb, BaseRoughness}
    //   Slot + 1: {Metallic, BaseIor, ClearcoatCoverage, ClearcoatRoughness}
    //   Slot + 2: {CoatAbsorptionPerM * Thickness (rgb), ClearcoatIor}
    //   Slot + 3: {SheenTint.rgb, SheenIntensity}
    //   Slot + 4: {EmissiveRadiance.rgb, SheenRoughness}
    //   Slot + 5: {FlakeCoverage, FlakeFrequency, GlassFlag (1 or 0), GlassIor}
    void PackForGpu(std::vector<std::array<float, 4>>& OutMaterialFloat4Table) const;

  private:
    std::array<SurfaceScatteringEntry, kMaxClassificationSlots> Entries_{};
};

} // namespace Frontier
