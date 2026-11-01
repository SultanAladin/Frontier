/*==========================================================================================================================================
                                               SURFACE SCATTERING ATLAS
                                     ------------------------------------------------------------------
             Fixed-stride GPU & CPU surface-optics atlas encoding TextureWorkspace / TextureStudio
             multi-strata materials (Opaque, Metallic, Anisotropic Stretched-Ellipsoid GGX, Dielectric
             Clearcoat, Automotive 3D Voronoi Flake Paint, Volumetric Transmission, Subsurface, Sheen,
             Emissive, and 2D-Array Albedo/Normal/ORM PBR textures) with 3-tier Material LOD metrics.
==========================================================================================================================================*/
// 🧩 Authored .material loader, PBR 2D-array texture baker, Heitz VNDF CPU oracle, & 8x float4 GPU atlas.

#pragma once

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/Geometry/GeometryProcessing/ScreenSpaceClusterRefinement.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

enum class MaterialDetailTier : uint8_t
{
    FullStrata      = 0, // 64B effective strata + textures + 3D Voronoi flakes
    FilteredToksvig = 1, // 24B Toksvig-filtered base + clearcoat without Voronoi cell evaluation
    CompactProxy    = 2, // 16B distant single-lobe energy-preserving proxy
};

struct ScatteringSequenceSlot
{
    std::string          PresetName            = "NeutralDielectric";
    std::array<float, 3> BaseAlbedo            = {0.62f, 0.63f, 0.65f};
    float                BaseRoughness         = 0.38f;
    float                Metallic              = 0.0f;
    float                BaseRefractiveIndex   = 1.50f;
    float                Anisotropy            = 0.0f;
    float                AnisotropyAngleDeg    = 0.0f;
    float                SheenWeight           = 0.0f;
    std::array<float, 3> CoatTint              = {1.0f, 1.0f, 1.0f};
    float                CoatCoverage          = 0.0f;
    float                CoatRoughness         = 0.06f;
    float                CoatRefractiveIndex   = 1.50f;
    float                TransmissiveFraction  = 0.0f;
    float                SubsurfaceWrap        = 0.0f;
    std::array<float, 3> EmissionRadiance      = {0.0f, 0.0f, 0.0f};
    float                EmissionStrength      = 0.0f;
    std::array<float, 3> AbsorptionPerMeter    = {0.0f, 0.0f, 0.0f};
    float                MicroDetailScale      = 0.0f;
    std::array<float, 2> UvScale               = {1.0f, 1.0f};
    uint32_t             TextureLayerIndex     = 0u;
    uint32_t             TextureChannelMask    = 0u; // bit0=Albedo, bit1=ORM, bit2=Normal
    bool                 AutomotiveFlakeActive = false;
    float                FlakeDensity          = 350.0f;
    float                FlakeCoverage         = 0.55f;
    float                FlakeTiltSpread       = 0.22f;
    float                FlakeRoughness        = 0.14f;
    std::array<float, 3> FlakeSecondaryTint    = {0.85f, 0.45f, 0.20f};
};

// Evaluates CPU white-furnace directional hemispherical reflectance integral ∫ f_spec(V, L) (N·L) / p_VNDF(L|V) dP
// for deterministic CTest verification of Heitz VNDF importance sampling & energy conservation.
[[nodiscard]] float IntegrateCpuVndfWhiteFurnace(float CosThetaV,
                                                 float Roughness,
                                                 float Anisotropy,
                                                 uint32_t SampleCount = 512u) noexcept;

// Uploaded 2D-Array PBR texture residency (Layer 0 = neutral; Layers 1..N = authored PNG maps).
struct SurfaceScatteringGpuTextures
{
    DeviceImageArraySlot AlbedoSrgbArray{};
    DeviceImageArraySlot NormalUnormArray{};
    DeviceImageArraySlot OrmUnormArray{};

    [[nodiscard]] bool Ready() const noexcept
    {
        return AlbedoSrgbArray.ArrayView != VK_NULL_HANDLE &&
               NormalUnormArray.ArrayView != VK_NULL_HANDLE &&
               OrmUnormArray.ArrayView != VK_NULL_HANDLE;
    }
};

// Classifies active 3-tier Material LOD at world distance d [m] using Near/Far range & MaterialQualityScale.
[[nodiscard]] MaterialDetailTier ClassifyMaterialDetailTier(float DistanceMeters,
                                                            float NearDetailMeters,
                                                            float FarProxyMeters,
                                                            float MaterialQualityScale = 1.0f) noexcept;

class SurfaceScatteringAtlas
{
  public:
    static constexpr uint32_t MaximumClassificationSlots = 64u;
    static constexpr uint32_t Float4StridePerSlot        = 8u;
    static constexpr uint32_t TextureAtlasDimension      = 256u;
    static constexpr uint32_t MaximumTextureArrayLayers  = 16u;

    SurfaceScatteringAtlas();

    // Seeds authored ControlVehicle + proving-ground materials (loading real TextureStudio .material & PNG maps when found).
    void LoadControlVehicleDefaults(const std::string& ContentSearchRoot = "");

    // Overwrites slots matching authored ClusterMaterialProfile classifications.
    void ApplyRefinementProfiles(const ScreenSpaceClusterRefinement& Refinement);

    // Parses an authored TextureStudio Format=1 .material file into ClassificationId (registering any referenced PNG maps).
    bool LoadAuthoredMaterialFile(uint32_t           ClassificationId,
                                  const std::string& MaterialFilePath,
                                  std::string*       OutDiagnostic = nullptr);

    void SetSlot(uint32_t ClassificationId, const ScatteringSequenceSlot& Slot);
    [[nodiscard]] const ScatteringSequenceSlot& QuerySlot(uint32_t ClassificationId) const noexcept;

    // Packs 64 × 8 float4 records (8,192 bytes) ready for GPU StructuredBuffer<float4> upload.
    void PackForGpu(std::vector<std::array<float, 4>>& OutMaterialFloat4Table) const;

    // Uploads 2D-Array PBR textures (Layer 0 = neutral white/flat-normal/white-ORM; Layers 1..N = authored PNG textures).
    bool UploadTextureArraysToGpu(VulkanComputeExchange& Device,
                                  DeviceImageArraySlot&  OutAlbedoSrgbArray,
                                  DeviceImageArraySlot&  OutNormalUnormArray,
                                  DeviceImageArraySlot&  OutOrmUnormArray) const;

    [[nodiscard]] uint32_t ActiveTextureLayerCount() const noexcept { return static_cast<uint32_t>(AlbedoLayersRgba_.size()); }

  private:
    uint32_t RegisterPbrTextureSet(const std::string& BaseDirectory,
                                   const std::string& AlbedoRelPath,
                                   const std::string& OrmRelPath,
                                   const std::string& NormalRelPath,
                                   uint32_t&          OutChannelMask);

    std::array<ScatteringSequenceSlot, MaximumClassificationSlots> Slots_{};
    std::vector<std::vector<uint8_t>>                              AlbedoLayersRgba_{};
    std::vector<std::vector<uint8_t>>                              NormalLayersRgba_{};
    std::vector<std::vector<uint8_t>>                              OrmLayersRgba_{};
};

} // namespace Frontier
