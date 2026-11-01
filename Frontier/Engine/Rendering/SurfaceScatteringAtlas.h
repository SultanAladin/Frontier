/*==========================================================================================================================================
                                                SURFACE SCATTERING ATLAS
                                     ------------------------------------------------------------------
             Bridges TextureWorkspace / TextureStudio authored .material TOML + PBR texture maps,
             ScatteringSequence multi-stratum clearcoat/sheen, anisotropic Smith-GGX, and AutomotivePaint
             micro-flakes into a contiguous GPU float4 table + RGBA8 texture array for GTX ReSTIR compute.
==========================================================================================================================================*/
// 🧩 TextureWorkspace + TextureStudio authored multi-stratum PBR material atlas, PNG map compiler, & GPU float4 packer.

#pragma once

#include "Engine/Render/AutomotivePaint.h"
#include "Engine/Render/ScatteringSequence.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

struct CompiledSurfaceOptics
{
    std::array<float, 3> BaseAlbedo   = {0.5f, 0.5f, 0.5f};    // [-]
    std::array<float, 3> SelfRadiance = {0.0f, 0.0f, 0.0f};    // [W/(m^2*sr)]
};

struct SurfaceScatteringEntry
{
    uint32_t             ClassificationId          = 0u;
    PreviewScattering    Scattering{};
    AutomotivePaint      Paint{};
    std::array<float, 3> EmissiveRadianceRgb       = {0.0f, 0.0f, 0.0f};
    bool                 TransmissiveGlass         = false;
    float                GlassIor                  = 1.52f;
    std::array<float, 3> GlassAbsorptionPerM       = {0.12f, 0.04f, 0.06f};
    float                Anisotropy                = 0.0f;       // [-0.9..0.9] - signed tangent/bitangent spread
    float                AnisotropyRotationRadians = 0.0f;       // [rad]
    float                UvTilingScale             = 1.0f;       // [-]
    float                NormalStrength            = 1.0f;       // [0..4]
    int32_t              AlbedoTextureLayer        = -1;         // [-] - >= 0 indexes TextureStudio array
    int32_t              NormalTextureLayer        = -1;         // [-]
    int32_t              OrmTextureLayer           = -1;         // [-]
};

class SurfaceScatteringAtlas
{
  public:
    static constexpr uint32_t kMaxClassificationSlots = 64u;
    static constexpr uint32_t kFloat4StridePerSlot    = 8u;
    static constexpr uint32_t TextureMapResolution    = 256u;

    SurfaceScatteringAtlas();

    bool RegisterScatteringEntry(const SurfaceScatteringEntry& Entry);
    void RegisterControlVehicleScatteringPresets();

    // Parses an authored .material TOML preset + resamples referenced Albedo / Normal / Orm PNGs into 256x256 GPU layers.
    bool LoadAuthoredMaterialFile(uint32_t           ClassificationId,
                                  const std::string& MaterialTomlPath,
                                  const std::string& TextureContentDirectory);

    // Loads authored TextureStudio materials for proving-ground stone, concrete, rims, tyres, carbon, leather, & crystal glass.
    uint32_t LoadProvingGroundAuthoredMaterials(const std::string& RootOrContentDirectory);

    [[nodiscard]] const SurfaceScatteringEntry& FindEntry(uint32_t ClassificationId) const noexcept;
    [[nodiscard]] CompiledSurfaceOptics         Slot(uint32_t ClassificationId) const noexcept;
    [[nodiscard]] uint32_t                      AuthoredTextureLayerCount() const noexcept { return TextureLayerCount_; }

    // CPU reference evaluation delegating directly to Engine/Render/ScatteringSequence.h::EvaluateScatteringSequence
    [[nodiscard]] ScatteringSpectrum EvaluateBrdfCpu(uint32_t             ClassificationId,
                                                     std::array<float, 3> SurfaceNormal,
                                                     std::array<float, 3> ViewDirectionToEye,
                                                     std::array<float, 3> LightDirectionToSource) const noexcept;

    // Packs 64 classification slots x 8 float4 records (= 512 float4s) for Slang GPU evaluation:
    //   Slot + 0: {BaseAlbedo.rgb, BaseRoughness}
    //   Slot + 1: {Metallic, BaseIor, ClearcoatCoverage, ClearcoatRoughness}
    //   Slot + 2: {CoatAbsorptionPerM * Thickness (rgb), ClearcoatIor}
    //   Slot + 3: {SheenTint.rgb, SheenIntensity}
    //   Slot + 4: {SelfRadiance.rgb, SheenRoughness}
    //   Slot + 5: {FlakeCoverage, FlakeFrequency, GlassFlag (1 or 0), GlassIor}
    //   Slot + 6: {Anisotropy, AnisotropyRotationRadians, FlakeTiltSpread, UvTilingScale}
    //   Slot + 7: {float(AlbedoLayer), float(NormalLayer), float(OrmLayer), NormalStrength}
    void PackForGpu(std::vector<std::array<float, 4>>& OutMaterialFloat4Table) const;

    // Outputs contiguous 256x256 RGBA8 array bytes (at least 1 neutral layer guaranteed for safe descriptor binding).
    void PackTextureMapsForGpu(uint32_t&             OutWidth,
                               uint32_t&             OutHeight,
                               uint32_t&             OutLayerCount,
                               std::vector<uint8_t>& OutRgbaBytes) const;

  private:
    int32_t LoadAndResampleTextureLayer(const std::string& FullPngPath, bool EncodeLinearForHardwareSrgb);

    std::array<SurfaceScatteringEntry, kMaxClassificationSlots> Entries_{};
    std::vector<std::string>                                    LoadedTextureKeys_{};
    std::vector<uint8_t>                                        PackedTextureBytes_{};
    uint32_t                                                    TextureLayerCount_ = 0u;
};

} // namespace Frontier
