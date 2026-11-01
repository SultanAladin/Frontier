/*==========================================================================================================================================
                                             SPATIOTEMPORAL ILLUMINATION
                                     ------------------------------------------------------------------
             Phase 3C GTX-first Vulkan 1.3 compute illumination renderer (zero hardware RTX cores):
               1. Two-level TLAS + 8-wide quantized CWBVH primary visibility & curved 2-interface Snell glass
               2. ReSTIR Direct (Solar + analytical softbox/point + flux-weighted Emissive Facet CDF + temporal)
               3. True ReSTIR GI over 4-cascade clipmap .sdfspace + CWBVH + GPU SurfaceCard multi-bounce radiosity
               4. Jacobian-determinant spatial GI reuse + Heitz (2018) VNDF anisotropic/clearcoat reflections
               5. Immutable ping-pong history + split diffuse/specular à-trous denoiser (>= 6/255 albedo floor)
               6. AtmosphereSkyLut + lunar corona haze + volumetric clouds + ForceField dust fog + lens flare +
                  15 RenderInspectionModes + zero-copy RGBA8 ImGui viewport presentation target.
==========================================================================================================================================*/
// 🧩 Phase 3C GTX-first Vulkan 1.3 ReSTIR DI+GI, 8-wide CWBVH, 4-cascade SDF, SurfaceCards, & VNDF renderer.

#pragma once

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/Geometry/GeometryProcessing/BoundingVolumeAtlas.h"
#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"
#include "Engine/Rendering/SurfaceCardRadiosity.h"
#include "Engine/Rendering/SurfaceScatteringAtlas.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

enum class AnalyticalLuminaireKind : uint32_t
{
    DirectionalSolar = 0u, // DirectionOrPosition = unit direction toward sun, AxisU.x = angular half-radius [rad]
    RectangularArea  = 1u, // DirectionOrPosition = center [m], AxisU = half-edge U [m], AxisV = half-edge V [m]
    OmniPoint        = 2u, // DirectionOrPosition = position [m], AxisU.x = emitter radius [m]
};

struct AnalyticalLuminaire
{
    AnalyticalLuminaireKind Kind                = AnalyticalLuminaireKind::DirectionalSolar;
    std::array<float, 3>    DirectionOrPosition = {0.42f, 0.76f, 0.50f};
    std::array<float, 3>    AxisU               = {0.00465f, 0.0f, 0.0f};
    std::array<float, 3>    AxisV               = {0.0f, 0.0f, 1.0f};
    std::array<float, 3>    EmittedRadianceRgb  = {3.2f, 3.0f, 2.8f};
};

struct SpatiotemporalCamera
{
    std::array<float, 3> OriginMeters   = {3.25f, 1.48f, 4.25f};
    std::array<float, 3> TargetMeters   = {0.0f, 0.72f, 0.0f};
    std::array<float, 3> UpUnit         = {0.0f, 1.0f, 0.0f};
    float                VerticalFovRad = 0.76f;
    uint32_t             WidthPx        = 800u;
    uint32_t             HeightPx       = 450u;
};

struct SpatiotemporalRenderConfig
{
    std::array<float, 3> ZenithSkyRgb             = {0.11f, 0.24f, 0.56f};
    std::array<float, 3> HorizonSkyRgb            = {0.52f, 0.64f, 0.80f};
    std::array<float, 3> GroundBounceTintRgb      = {0.22f, 0.23f, 0.25f};
    uint32_t             RisCandidateCount        = 8u;
    uint32_t             SpatialNeighbors         = 4u;
    uint32_t             SdfIndirectRays          = 4u;
    uint32_t             AtrousIterations         = 2u;
    bool                 EnableTemporalReuse      = true;
    bool                 EnableSpatialReuse       = true;
    bool                 EnableReflections        = true;
    float                ExposureScale            = 1.08f;

    // Phase 3C Environment, Material LOD, Radiosity, & Inspection extensions
    std::array<float, 3> MoonDirection            = {-0.38f, 0.62f, -0.68f};
    float                MoonCoronaStrength       = 0.0f;
    float                NearMaterialMeters       = 12.0f;
    float                FarMaterialMeters        = 55.0f;
    float                MaterialQualityScale     = 1.0f;
    float                TemporalHistoryCap       = 18.0f;
    float                CloudCoverage            = 0.35f;
    float                CloudAltitudeMeters      = 1250.0f;
    float                FogBaseDensity           = 0.0f;
    float                FogDustBoost             = 0.0f;
    float                FlareIntensity           = 0.0f;
    uint32_t             InspectionMode           = 0u; // 0..14 matching RenderInspectionModes
    bool                 EnableIndirectGi         = true;
    bool                 EnableAtmosphereLut      = false;
    bool                 EnableClouds             = false;
    bool                 EnableFog                = false;
    bool                 EnableLensFlare          = false;
    bool                 EnableSecondMirrorBounce = false;
};

struct SpatiotemporalFrameTelemetry
{
    uint32_t BvhCellCount        = 0u;
    uint32_t BvhTriangleCount    = 0u;
    uint32_t WideCellCount       = 0u;
    uint32_t InstanceCount       = 0u;
    uint32_t LuminaireCount      = 0u;
    uint32_t EmissiveFacetCount  = 0u;
    uint32_t SurfaceCardCount    = 0u;
    bool     SdfVolumeActive     = false;
    bool     FourCascadeActive   = false;
    uint32_t FrameIndex          = 0u;

    // Per-stage GPU hardware timestamp & CPU submission timings [ms]
    double   RadiosityGpuMs      = 0.0;
    double   PrimaryGpuMs        = 0.0;
    double   DirectRisGpuMs      = 0.0;
    double   IndirectGiGpuMs     = 0.0;
    double   SpatialVndfGpuMs    = 0.0;
    double   AtrousGpuMs         = 0.0;
    double   ResolveGpuMs        = 0.0;
    double   TotalGpuMs          = 0.0;
    double   TotalCpuMs          = 0.0;
    double   PipelineCompileMs   = 0.0;
    uint64_t AllocatedVramBytes  = 0u;
};

class SpatiotemporalIllumination
{
  public:
    bool Initialize(VulkanComputeExchange& Exchange,
                    const std::string&     ShaderBinaryDirectory,
                    std::string*           OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeExchange& Exchange) noexcept;

    // Single-mesh convenience uploader (wraps BvhAtlas into a 1-instance TLAS + 8-wide CWBVH + SurfaceCards).
    bool UploadSceneGeometryAndMaterials(VulkanComputeExchange&        Exchange,
                                         const BoundingVolumeAtlas&    BvhAtlas,
                                         const SurfaceScatteringAtlas& MaterialAtlas,
                                         const VolumeDistanceField*    DistanceVolume);

    // Full Phase 3C two-level instanced TLAS/8-wide CWBVH + 4-cascade clipmap SDF + SurfaceCards uploader.
    bool UploadInstancedSceneAndMaterials(VulkanComputeExchange&            Exchange,
                                          const InstancedBoundingScene&     InstancedScene,
                                          const SurfaceScatteringAtlas&     MaterialAtlas,
                                          const FourCascadeDistanceVolume*  FourCascadeVolume,
                                          std::span<const SurfaceCardPatch> CustomSurfaceCards = {},
                                          std::span<const EmissiveFacetRecord> CustomEmissiveFacets = {});

    // Updates dynamic instance transforms + TLAS (e.g. rolling wheels / moving vehicles) without rebuilding BLASes.
    bool RefreshDynamicInstances(VulkanComputeExchange& Exchange, const InstancedBoundingScene& InstancedScene);

    // Optional external AtmosphereProjection SkyView LUT binding (falls back to neutral 1x1 sky texture if unset).
    void BindAtmosphereSkyLut(const DeviceImageArraySlot* ExternalSkyViewArray) noexcept
    {
        ExternalAtmosphereSkyLut_ = ExternalSkyViewArray;
    }

    void ResetTemporalHistory() noexcept
    {
        HistoryValid_ = false;
        FrameCounter_ = 0u;
    }

    bool RenderFrame(VulkanComputeExchange&                  Exchange,
                     const SpatiotemporalCamera&             Camera,
                     const std::vector<AnalyticalLuminaire>& Luminaires,
                     const SpatiotemporalRenderConfig&       Config,
                     SpatiotemporalFrameTelemetry*           OutTelemetry            = nullptr,
                     bool                                    TransitionDisplayForGui = false);

    bool ReadbackLinearRgba(VulkanComputeExchange& Exchange, std::vector<float>& OutLinearRgbaFloats);
    bool ReadbackTonemappedSrgb8(VulkanComputeExchange& Exchange,
                                 float                  ExposureScale,
                                 std::vector<uint8_t>&  OutSrgbRgbaBytes);

    [[nodiscard]] const DeviceImageArraySlot& OutputImage() const noexcept { return RadiancePingPong_[FinalRadianceIndex_]; }
    [[nodiscard]] const DeviceImageArraySlot& DisplayRgba8Target() const noexcept { return DisplayRgba8Target_; }
    [[nodiscard]] double                      PipelineCompileMilliseconds() const noexcept { return TotalPipelineCompileMs_; }

  private:
    bool EnsureViewportTargets(VulkanComputeExchange& Exchange, uint32_t Width, uint32_t Height);

    ComputeProgramSlot          PrimaryProgram_{};
    ComputeProgramSlot          DirectRisProgram_{};
    ComputeProgramSlot          IndirectGiProgram_{};
    ComputeProgramSlot          SpatialReuseProgram_{};
    ComputeProgramSlot          AtrousProgram_{};
    ComputeProgramSlot          ResolveDisplayProgram_{};
    SurfaceCardRadiositySolver  RadiositySolver_{};

    DeviceMemorySpan            TlasSpan_{};
    DeviceMemorySpan            InstanceSpan_{};
    DeviceMemorySpan            WideCellSpan_{};
    DeviceMemorySpan            WideTriangleSpan_{};
    DeviceMemorySpan            MaterialSpan_{};
    DeviceMemorySpan            DistanceVolumeSpan_{};
    DeviceMemorySpan            LuminaireSpan_{};
    DeviceMemorySpan            EmissiveFacetSpan_{};
    DeviceMemorySpan            FrameUniformSpan_{};
    SurfaceScatteringGpuTextures MaterialTextures_{};
    DeviceImageArraySlot        FallbackSkyLut_{};
    const DeviceImageArraySlot* ExternalAtmosphereSkyLut_ = nullptr;

    // Ping-pong G-Buffers (6 layers), Reservoirs (3 layers), Radiance work (2 layers), & RGBA8 Display target
    DeviceImageArraySlot        GBufferPingPong_[2]{};
    DeviceImageArraySlot        ReservoirPingPong_[2]{};
    DeviceImageArraySlot        RadiancePingPong_[2]{};
    DeviceImageArraySlot        DisplayRgba8Target_{};
    bool                        DisplayInShaderReadLayout_ = false;

    std::vector<SurfaceCardPatch> ActiveSurfaceCards_{};
    std::array<float, 3>        CachedRadiositySunDir_     = {0.0f, 0.0f, 0.0f};
    float                       CachedRadiositySunLux_     = -1.0f;

    std::array<float, 3>        PrevCamOrigin_             = {0.0f, 1.5f, 4.0f};
    std::array<float, 3>        PrevCamRight_              = {1.0f, 0.0f, 0.0f};
    std::array<float, 3>        PrevCamUp_                 = {0.0f, 1.0f, 0.0f};
    std::array<float, 3>        PrevCamForward_            = {0.0f, 0.0f, -1.0f};

    uint32_t                    TargetWidth_               = 0u;
    uint32_t                    TargetHeight_              = 0u;
    uint32_t                    FinalRadianceIndex_        = 0u;
    uint32_t                    UploadedBvhCells_          = 0u;
    uint32_t                    UploadedBvhTris_           = 0u;
    uint32_t                    UploadedWideCells_         = 0u;
    uint32_t                    UploadedInstances_         = 0u;
    uint32_t                    UploadedTlasCells_         = 0u;
    uint32_t                    UploadedEmissiveFacets_    = 0u;
    bool                        HasSdfVolume_              = false;
    bool                        FourCascadeActive_         = false;
    bool                        HistoryValid_              = false;
    uint32_t                    FrameCounter_              = 0u;
    double                      TotalPipelineCompileMs_    = 0.0;
};

} // namespace Frontier
