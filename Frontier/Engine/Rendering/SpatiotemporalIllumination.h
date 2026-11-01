/*==========================================================================================================================================
                                             SPATIOTEMPORAL ILLUMINATION
                                     ------------------------------------------------------------------
             GTX-first Vulkan 1.3 compute illumination renderer (zero hardware RTX raytracing cores):
               1. SolveSurfaceRadiosityMain      - GPU 6-axis surface card multi-bounce radiosity (B = R_0 + rho * F * B)
               2. TracePrimarySurfaceMain        - Two-level 8-wide OctetVolumeAtlas primary ray + 2-interface Snell/TIR glass + motion UV
               3. SampleDirectReservoirsMain     - ReSTIR DI over analytic luminaires & LuminousFacetAtlas CDF + motion temporal reuse
               4. SampleIndirectReservoirsMain   - ReSTIR GI over 4-cascade RangeDistanceAtlas + SurfaceCards + Jacobian spatial/temporal
               5. ReuseSpatialReservoirsMain     - Direct Jacobian spatial reuse + Heitz VNDF anisotropic/clearcoat reflections + split BRDF
               6. FilterSpatiotemporalAtrousMain - Split diffuse/specular ping-pong à-trous + 15 inspection modes + zero-copy RGBA8 2D view
==========================================================================================================================================*/
// 🧩 GTX-first Option A (Phase 3C-1 + 3C-2) Vulkan 1.3 ReSTIR DI/GI + 8-wide CWBVH + 4-cascade SDF + Split Denoiser.

#pragma once

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/Geometry/GeometryProcessing/BoundingVolumeAtlas.h"
#include "Engine/Geometry/GeometryProcessing/OctetVolumeAtlas.h"
#include "Engine/Geometry/GeometryProcessing/RangeDistanceAtlas.h"
#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"
#include "Engine/Rendering/LuminousFacetAtlas.h"
#include "Engine/Rendering/SurfaceRadiosityAtlas.h"
#include "Engine/Rendering/SurfaceScatteringAtlas.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

enum class AnalyticalLuminaireKind : uint32_t
{
    DirectionalSolar = 0, // DirectionOrPosition = unit direction toward sun, AxisU.x = angular half-radius [rad]
    RectangularArea  = 1, // DirectionOrPosition = center [m], AxisU = half-edge U [m], AxisV = half-edge V [m]
    OmniPoint        = 2, // DirectionOrPosition = position [m], AxisU.x = emitter radius [m]
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

struct CoupledEnvironmentParameters
{
    bool                 CloudsEnabled       = false;
    float                CloudCoverage       = 0.42f;
    float                CloudDensity        = 0.045f;
    float                CloudBottomMeters   = 1500.0f;
    bool                 LunarEnabled        = false;
    std::array<float, 3> PrimaryMoonDir      = {-0.38f, 0.52f, -0.76f};
    float                PrimaryMoonRadius   = 0.014f;
    std::array<float, 3> SecondaryMoonDir    = {-0.62f, 0.34f, -0.70f};
    float                SecondaryMoonRadius = 0.008f;
    float                DaytimeVeilStrength = 0.55f;
    bool                 FogEnabled          = false;
    float                FogDensity          = 0.004f;
    float                FogHeightFalloff    = 0.06f;
    std::array<float, 3> FogInscatteringRgb  = {0.58f, 0.68f, 0.82f};
};

struct SpatiotemporalRenderConfig
{
    std::array<float, 3>         ZenithSkyRgb         = {0.11f, 0.24f, 0.56f};
    std::array<float, 3>         HorizonSkyRgb        = {0.52f, 0.64f, 0.80f};
    std::array<float, 3>         GroundBounceTintRgb  = {0.22f, 0.23f, 0.25f};
    uint32_t                     RisCandidateCount    = 8u;
    uint32_t                     SpatialNeighbors     = 4u;
    uint32_t                     SdfIndirectRays      = 4u;
    uint32_t                     AtrousIterations     = 2u;
    uint32_t                     TemporalHistoryLimit = 20u;
    uint32_t                     RadiosityBounces     = 3u;
    float                        SurfaceCardGain      = 1.0f;
    float                        SurfaceCardSkyWeight = 0.35f;
    uint32_t                     InspectionMode       = 0u; // [0..14] matches RenderInspectionModes
    bool                         EnableTemporalReuse  = true;
    bool                         EnableSpatialReuse   = true;
    bool                         EnableIndirectGi     = true;
    bool                         EnableReflections    = true;
    float                        ExposureScale        = 1.08f;
    CoupledEnvironmentParameters Environment{};
};

struct SpatiotemporalProgramCompileInfo
{
    std::string EntryName{};
    uint64_t    SpirvFingerprintFnv64       = 0u;
    uint32_t    SpirvByteCount              = 0u;
    float       ShaderModuleMilliseconds    = 0.0f;
    float       PipelineCompileMilliseconds = 0.0f;
};

struct SpatiotemporalFrameTelemetry
{
    uint32_t BvhCellCount            = 0u;
    uint32_t BvhTriangleCount        = 0u;
    uint32_t BottomArchiveCount      = 0u;
    uint32_t TopInstanceCount        = 0u;
    uint32_t OctetBlockCount         = 0u;
    uint32_t LuminaireCount          = 0u;
    uint32_t LuminousFacetCount      = 0u;
    uint32_t SurfacePatchCount       = 0u;
    uint32_t AuthoredTextureLayers   = 0u;
    bool     SdfVolumeActive         = false;
    bool     RangeClipmapActive      = false;
    uint32_t FrameIndex              = 0u;
    float    RadiosityGpuMs          = 0.0f;
    float    PrimaryGpuMs            = 0.0f;
    float    DirectRisGpuMs          = 0.0f;
    float    IndirectGiGpuMs         = 0.0f;
    float    SpatialReflectGpuMs     = 0.0f;
    float    AtrousResolveGpuMs      = 0.0f;
    float    TotalGpuMs              = 0.0f;
    float    CpuSubmissionMs         = 0.0f;
};

class SpatiotemporalIllumination
{
  public:
    bool Initialize(VulkanComputeExchange& Exchange,
                    const std::string&     ShaderBinaryDirectory,
                    std::string*           OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeExchange& Exchange) noexcept;

    // Backwards-compatible single-level upload (used by Stage 3B validation tests)
    bool UploadSceneGeometryAndMaterials(VulkanComputeExchange&        Exchange,
                                         const BoundingVolumeAtlas&    BvhAtlas,
                                         const SurfaceScatteringAtlas& MaterialAtlas,
                                         const VolumeDistanceField*    DistanceVolume);

    // Full Phase 3C-1 + 3C-2 two-level instanced 8-wide OctetVolumeAtlas + 4-cascade RangeDistanceAtlas + Cards + Luminous CDF
    bool UploadTwoLevelScene(VulkanComputeExchange&        Exchange,
                             const OctetVolumeAtlas&       TwoLevelAtlas,
                             const SurfaceScatteringAtlas& MaterialAtlas,
                             const RangeDistanceAtlas*     RangeClipmap,
                             const SurfaceRadiosityAtlas*  RadiosityCards,
                             const LuminousFacetAtlas*     LuminousFacets);

    // Fast dynamic instance TLAS refit upload (< 4 KB, preserves all BLASes & temporal history!)
    bool UpdateDynamicTopLevelInstances(VulkanComputeExchange&  Exchange,
                                        const OctetVolumeAtlas& TwoLevelAtlas);

    // Fast camera-centered 4-cascade SDF clipmap refresh
    bool UpdateRangeClipmap(VulkanComputeExchange&    Exchange,
                            const RangeDistanceAtlas& RangeClipmap);

    void ResetTemporalHistory() noexcept
    {
        HistoryValid_ = false;
        FrameCounter_ = 0u;
    }

    bool RenderFrame(VulkanComputeExchange&                  Exchange,
                     const SpatiotemporalCamera&             Camera,
                     const std::vector<AnalyticalLuminaire>& Luminaires,
                     const SpatiotemporalRenderConfig&       Config,
                     SpatiotemporalFrameTelemetry*           OutTelemetry           = nullptr,
                     DeviceViewportImageSlot*                OptionalViewportTarget = nullptr);

    // Zero-stall external command buffer recording for live swapchain presentation (ConstructGpu before ImGui render pass).
    bool RecordFrameIntoExternalStream(VulkanComputeExchange&                  Exchange,
                                       VkCommandBuffer                         ExternalCommandBuffer,
                                       uint32_t                                FrameSlotIndex,
                                       const SpatiotemporalCamera&             Camera,
                                       const std::vector<AnalyticalLuminaire>& Luminaires,
                                       const SpatiotemporalRenderConfig&       Config,
                                       DeviceViewportImageSlot&                ViewportTarget,
                                       SpatiotemporalFrameTelemetry*           OutTelemetry = nullptr);

    bool ReadbackLinearRgba(VulkanComputeExchange& Exchange, std::vector<float>& OutLinearRgbaFloats);
    bool ReadbackTonemappedSrgb8(VulkanComputeExchange& Exchange,
                                 float                  ExposureScale,
                                 std::vector<uint8_t>&  OutSrgbRgbaBytes);
    bool DownloadSolvedSurfaceRadiosity(VulkanComputeExchange&              Exchange,
                                        std::vector<std::array<float, 3>>&  OutSolvedRadiosityRgb) const;

    [[nodiscard]] const DeviceImageArraySlot&                         OutputImage() const noexcept { return OutputImage_; }
    [[nodiscard]] std::span<const SpatiotemporalProgramCompileInfo>   ProgramCompileStats() const noexcept { return CompileStats_; }

  private:
    ComputeProgramSlot RadiosityProgram_{};
    ComputeProgramSlot PrimaryProgram_{};
    ComputeProgramSlot DirectRisProgram_{};
    ComputeProgramSlot IndirectGiProgram_{};
    ComputeProgramSlot SpatialReflectProgram_{};
    ComputeProgramSlot AtrousProgram_{};

    DeviceMemorySpan                FrameHeaderSpan_{};
    std::array<DeviceMemorySpan, 4> FrameHeaderRing_{};
    DeviceMemorySpan                BvhOrTopCellSpan_{};
    DeviceMemorySpan BvhTriangleSpan_{};
    DeviceMemorySpan MaterialSpan_{};
    DeviceMemorySpan DistanceOrRangeSpan_{};
    DeviceMemorySpan OctetInstanceSpan_{};
    DeviceMemorySpan OctetBlockSpan_{};
    DeviceMemorySpan LuminousFacetSpan_{};
    DeviceMemorySpan SurfaceCardSpan_{};
    DeviceMemorySpan DirectReservoirCurrSpan_{};
    DeviceMemorySpan DirectReservoirPrevSpan_{};
    DeviceMemorySpan IndirectReservoirCurrSpan_{};
    DeviceMemorySpan IndirectReservoirPrevSpan_{};

    DeviceImageArraySlot    MaterialTextureArray_{};
    DeviceImageArraySlot    SurfaceAndWorkAtlas_{}; // 10 layers rgba16f (GBuffer + Split Diff/Spec Ping-Pong)
    DeviceImageArraySlot    OutputImage_{};         // 1 layer rgba16f
    DeviceViewportImageSlot FallbackViewport2D_{};  // 16x16 fallback when no external viewport target is supplied

    std::array<float, 16> PreviousViewProjRows_{1.0f, 0.0f, 0.0f, 0.0f,
                                                0.0f, 1.0f, 0.0f, 0.0f,
                                                0.0f, 0.0f, 1.0f, 0.0f,
                                                0.0f, 0.0f, 0.0f, 1.0f};

    std::vector<SpatiotemporalProgramCompileInfo> CompileStats_{};

    uint32_t UploadedBvhCells_       = 0u;
    uint32_t UploadedBvhTris_        = 0u;
    uint32_t UploadedBottomArchives_ = 0u;
    uint32_t UploadedInstances_      = 0u;
    uint32_t UploadedOctetBlocks_    = 0u;
    uint32_t UploadedLuminousFacets_ = 0u;
    uint32_t UploadedSurfacePatches_ = 0u;
    uint32_t UploadedTextureLayers_  = 0u;
    bool     TwoLevelOctetActive_    = false;
    bool     HasSdfVolume_           = false;
    bool     RangeClipmapActive_     = false;
    bool     HistoryValid_           = false;
    uint32_t FrameCounter_           = 0u;
};

} // namespace Frontier
