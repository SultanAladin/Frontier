/*==========================================================================================================================================
                                             SPATIOTEMPORAL ILLUMINATION
                                     ------------------------------------------------------------------
             GTX-first Vulkan 1.3 compute illumination renderer (zero hardware RTX raytracing cores):
               1. Software SAH BoundingVolumeAtlas primary visibility + curved 2-interface Snell glass refraction
               2. Spatiotemporal Reservoir Resampling (ReSTIR Direct: RIS + M-clamped temporal + Jacobian spatial)
               3. Headless-baked .sdfspace VolumeDistanceField diffuse GI + 5-tap SDF ambient occlusion
               4. Software BoundingVolumeAtlas glossy/clearcoat reflections + TextureWorkspace ScatteringSequence
               5. Edge-stopping variance-guided à-trous reconstruction & ACES display resolve.
==========================================================================================================================================*/
// 🧩 GTX-first Vulkan 1.3 ReSTIR + .sdfspace SDF GI + Software SAH-BVH renderer.

#pragma once

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/Geometry/GeometryProcessing/BoundingVolumeAtlas.h"
#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"
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

struct SpatiotemporalRenderConfig
{
    std::array<float, 3> ZenithSkyRgb        = {0.11f, 0.24f, 0.56f};
    std::array<float, 3> HorizonSkyRgb       = {0.52f, 0.64f, 0.80f};
    std::array<float, 3> GroundBounceTintRgb = {0.22f, 0.23f, 0.25f};
    uint32_t             RisCandidateCount   = 8u;
    uint32_t             SpatialNeighbors    = 4u;
    uint32_t             SdfIndirectRays     = 4u;
    uint32_t             AtrousIterations    = 2u;
    bool                 EnableTemporalReuse = true;
    bool                 EnableSpatialReuse  = true;
    bool                 EnableReflections   = true;
    float                ExposureScale       = 1.08f;
};

struct SpatiotemporalFrameTelemetry
{
    uint32_t BvhCellCount      = 0u;
    uint32_t BvhTriangleCount  = 0u;
    uint32_t LuminaireCount    = 0u;
    bool     SdfVolumeActive   = false;
    uint32_t FrameIndex        = 0u;
};

class SpatiotemporalIllumination
{
  public:
    bool Initialize(VulkanComputeExchange& Exchange,
                    const std::string&     ShaderBinaryDirectory,
                    std::string*           OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeExchange& Exchange) noexcept;

    bool UploadSceneGeometryAndMaterials(VulkanComputeExchange&        Exchange,
                                         const BoundingVolumeAtlas&    BvhAtlas,
                                         const SurfaceScatteringAtlas& MaterialAtlas,
                                         const VolumeDistanceField*    DistanceVolume);

    void ResetTemporalHistory() noexcept { HistoryValid_ = false; FrameCounter_ = 0u; }

    bool RenderFrame(VulkanComputeExchange&                  Exchange,
                     const SpatiotemporalCamera&             Camera,
                     const std::vector<AnalyticalLuminaire>& Luminaires,
                     const SpatiotemporalRenderConfig&       Config,
                     SpatiotemporalFrameTelemetry*           OutTelemetry = nullptr);

    bool ReadbackLinearRgba(VulkanComputeExchange& Exchange, std::vector<float>& OutLinearRgbaFloats);
    bool ReadbackTonemappedSrgb8(VulkanComputeExchange& Exchange,
                                 float                  ExposureScale,
                                 std::vector<uint8_t>&  OutSrgbRgbaBytes);

    [[nodiscard]] const DeviceImageArraySlot& OutputImage() const noexcept { return OutputImage_; }

  private:
    ComputeProgramSlot   PrimaryProgram_{};
    ComputeProgramSlot   DirectRisProgram_{};
    ComputeProgramSlot   SpatialGiProgram_{};
    ComputeProgramSlot   AtrousProgram_{};

    DeviceMemorySpan     FrameHeaderSpan_{};
    DeviceMemorySpan     BvhCellSpan_{};
    DeviceMemorySpan     BvhTriangleSpan_{};
    DeviceMemorySpan     MaterialSpan_{};
    DeviceMemorySpan     DistanceVolumeSpan_{};
    DeviceMemorySpan     ReservoirCurrSpan_{};
    DeviceMemorySpan     ReservoirPrevSpan_{};

    DeviceImageArraySlot SurfaceAtlas_{};     // 4 layers rgba16f: Pos+T, Norm+Class, GlassTr+ReflW, GlassReflRgb
    DeviceImageArraySlot IlluminationWork_{}; // 2 layers rgba16f: ping-pong noisy/atrous diffuse+spec radiance
    DeviceImageArraySlot OutputImage_{};      // 1 layer rgba16f: final resolved linear HDR frame

    uint32_t             UploadedBvhCells_ = 0u;
    uint32_t             UploadedBvhTris_  = 0u;
    bool                 HasSdfVolume_     = false;
    bool                 HistoryValid_     = false;
    uint32_t             FrameCounter_     = 0u;
};

} // namespace Frontier
