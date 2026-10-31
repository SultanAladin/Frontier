/*==========================================================================================================================================
                                          SPATIOTEMPORAL ILLUMINATION DEFINITION
==========================================================================================================================================*/
// 🧩 GTX-first Vulkan 1.3 compute orchestrator for ReSTIR Direct + .sdfspace SDF GI + Software SAH-BVH reflections & glass.

#include "Engine/Rendering/SpatiotemporalIllumination.h"

#include "Engine/PlanetaryAtmosphere/PlanetaryEnsemble.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Frontier
{
namespace
{

struct SpatiotemporalPushConstants
{
    uint32_t Counts[4]; // {Width, Height, FrameIndex, StepOrFlags}
    float    Params[4]; // {Exposure, AtrousStepSize, SpatialRadiusPx, HistoryFlag}
};

std::array<float, 3> SubVec(std::array<float, 3> A, std::array<float, 3> B) noexcept
{
    return {A[0] - B[0], A[1] - B[1], A[2] - B[2]};
}
float DotVec(std::array<float, 3> A, std::array<float, 3> B) noexcept
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}
std::array<float, 3> CrossVec(std::array<float, 3> A, std::array<float, 3> B) noexcept
{
    return {A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]};
}
std::array<float, 3> NormVec(std::array<float, 3> V) noexcept
{
    const float L = std::sqrt(DotVec(V, V));
    return (L > 1.0e-12f) ? std::array<float, 3>{V[0] / L, V[1] / L, V[2] / L} : std::array<float, 3>{0.0f, 1.0f, 0.0f};
}

} // namespace

bool SpatiotemporalIllumination::Initialize(VulkanComputeExchange& Exchange,
                                            const std::string&     ShaderBinaryDirectory,
                                            std::string*           OutDiagnostic)
{
    const std::array<VkDescriptorType, 10> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 0: FrameHeaderAndLuminaires
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 1: BvhCells
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 2: BvhTriangles
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 3: MaterialTable
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 4: DistanceVolumeTable
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 5: ReservoirCurr
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 6: ReservoirPrev
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  // 7: SurfaceAtlas (4 layers)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  // 8: IlluminationWork (2 layers)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  // 9: OutputImage (1 layer)
    }};

    if (!Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/TracePrimarySurface.spv",
                                     "TracePrimarySurfaceMain",
                                     Bindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     PrimaryProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/SampleDirectReservoirs.spv",
                                     "SampleDirectReservoirsMain",
                                     Bindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     DirectRisProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/ReuseSpatialReservoirs.spv",
                                     "ReuseSpatialReservoirsMain",
                                     Bindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     SpatialGiProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/FilterSpatiotemporalAtrous.spv",
                                     "FilterSpatiotemporalAtrousMain",
                                     Bindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     AtrousProgram_,
                                     OutDiagnostic))
    {
        return false;
    }

    return Exchange.AllocateRgba16fArray(16u, 16u, 4u, SurfaceAtlas_) &&
           Exchange.AllocateRgba16fArray(16u, 16u, 2u, IlluminationWork_) &&
           Exchange.AllocateRgba16fArray(16u, 16u, 1u, OutputImage_);
}

void SpatiotemporalIllumination::Shutdown(VulkanComputeExchange& Exchange) noexcept
{
    Exchange.ReleaseImage(OutputImage_);
    Exchange.ReleaseImage(IlluminationWork_);
    Exchange.ReleaseImage(SurfaceAtlas_);
    Exchange.ReleaseMemorySpan(ReservoirPrevSpan_);
    Exchange.ReleaseMemorySpan(ReservoirCurrSpan_);
    Exchange.ReleaseMemorySpan(DistanceVolumeSpan_);
    Exchange.ReleaseMemorySpan(MaterialSpan_);
    Exchange.ReleaseMemorySpan(BvhTriangleSpan_);
    Exchange.ReleaseMemorySpan(BvhCellSpan_);
    Exchange.ReleaseMemorySpan(FrameHeaderSpan_);
    Exchange.ReleaseProgram(AtrousProgram_);
    Exchange.ReleaseProgram(SpatialGiProgram_);
    Exchange.ReleaseProgram(DirectRisProgram_);
    Exchange.ReleaseProgram(PrimaryProgram_);
    UploadedBvhCells_ = 0u;
    UploadedBvhTris_  = 0u;
    HasSdfVolume_     = false;
    HistoryValid_     = false;
}

bool SpatiotemporalIllumination::UploadSceneGeometryAndMaterials(VulkanComputeExchange&        Exchange,
                                                                 const BoundingVolumeAtlas&    BvhAtlas,
                                                                 const SurfaceScatteringAtlas& MaterialAtlas,
                                                                 const VolumeDistanceField*    DistanceVolume)
{
    std::vector<std::array<float, 4>> CellTable;
    std::vector<std::array<float, 4>> TriTable;
    BvhAtlas.PackForGpu(CellTable, TriTable);

    std::vector<std::array<float, 4>> MatTable;
    MaterialAtlas.PackForGpu(MatTable);

    std::vector<std::array<float, 4>> SdfTable;
    if (DistanceVolume != nullptr && !DistanceVolume->Distances.empty())
    {
        PackVolumeDistanceForGpu(*DistanceVolume, SdfTable);
        HasSdfVolume_ = true;
    }
    else
    {
        VolumeDistanceField Empty{};
        PackVolumeDistanceForGpu(Empty, SdfTable);
        HasSdfVolume_ = false;
    }

    if (!Exchange.UploadMemorySpan(BvhCellSpan_, CellTable.data(), CellTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(BvhTriangleSpan_, TriTable.data(), TriTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(MaterialSpan_, MatTable.data(), MatTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(DistanceVolumeSpan_, SdfTable.data(), SdfTable.size() * sizeof(float) * 4u))
    {
        return false;
    }

    UploadedBvhCells_ = BvhAtlas.CellCount();
    UploadedBvhTris_  = BvhAtlas.TriangleCount();
    HistoryValid_     = false;
    return true;
}

bool SpatiotemporalIllumination::RenderFrame(VulkanComputeExchange&                  Exchange,
                                             const SpatiotemporalCamera&             Camera,
                                             const std::vector<AnalyticalLuminaire>& Luminaires,
                                             const SpatiotemporalRenderConfig&       Config,
                                             SpatiotemporalFrameTelemetry*           OutTelemetry)
{
    const uint32_t W = std::max(1u, Camera.WidthPx);
    const uint32_t H = std::max(1u, Camera.HeightPx);

    if (OutputImage_.Width != W || OutputImage_.Height != H)
    {
        if (!Exchange.AllocateRgba16fArray(W, H, 4u, SurfaceAtlas_) ||
            !Exchange.AllocateRgba16fArray(W, H, 2u, IlluminationWork_) ||
            !Exchange.AllocateRgba16fArray(W, H, 1u, OutputImage_))
        {
            return false;
        }
        HistoryValid_ = false;
    }

    // 2 x float4 (= 32 B) per pixel reservoir:
    //   Slot + 0: {SelectedLightIdx, SampleU, SampleV, WeightW}
    //   Slot + 1: {WeightSum, SampleCountM, TargetPHat, Pad}
    const size_t ReservoirBytes = static_cast<size_t>(W) * H * 2u * sizeof(float) * 4u;
    if (ReservoirCurrSpan_.Capacity < ReservoirBytes || ReservoirPrevSpan_.Capacity < ReservoirBytes)
    {
        std::vector<float> ZeroRes(static_cast<size_t>(W) * H * 8u, 0.0f);
        if (!Exchange.UploadMemorySpan(ReservoirCurrSpan_, ZeroRes.data(), ReservoirBytes) ||
            !Exchange.UploadMemorySpan(ReservoirPrevSpan_, ZeroRes.data(), ReservoirBytes))
        {
            return false;
        }
        HistoryValid_ = false;
    }

    const auto  CamFwd   = NormVec(SubVec(Camera.TargetMeters, Camera.OriginMeters));
    const auto  RefUp    = (std::abs(DotVec(CamFwd, Camera.UpUnit)) > 0.98f)
                               ? std::array<float, 3>{0.0f, 0.0f, 1.0f}
                               : Camera.UpUnit;
    const auto  CamRight = NormVec(CrossVec(CamFwd, RefUp));
    const auto  CamUp    = NormVec(CrossVec(CamRight, CamFwd));
    const float TanHalfV = std::tan(0.5f * Camera.VerticalFovRad);
    const float TanHalfH = TanHalfV * (static_cast<float>(W) / static_cast<float>(H));

    const uint32_t LumCount = static_cast<uint32_t>(std::min<size_t>(Luminaires.size(), 32u));

    // FrameHeader layout:
    //   [0]: {Origin.xyz, TanHalfV}
    //   [1]: {Right.xyz, TanHalfH}
    //   [2]: {Up.xyz, float(UploadedBvhCells_)}
    //   [3]: {Fwd.xyz, float(LumCount)}
    //   [4]: {ZenithSkyRgb.xyz, float(RisCandidateCount)}
    //   [5]: {HorizonSkyRgb.xyz, float(SpatialNeighbors)}
    //   [6]: {GroundBounceTintRgb.xyz, float(SdfIndirectRays)}
    //   [7]: {EnableReflections, EnableSpatialReuse, HasSdfVolume, 0}
    //   [8 + i*4 + 0..3]: Luminaire i (4 x float4)
    std::vector<std::array<float, 4>> Header;
    Header.reserve(8u + std::max(1u, LumCount) * 4u);
    Header.push_back({Camera.OriginMeters[0], Camera.OriginMeters[1], Camera.OriginMeters[2], TanHalfV});
    Header.push_back({CamRight[0], CamRight[1], CamRight[2], TanHalfH});
    Header.push_back({CamUp[0], CamUp[1], CamUp[2], static_cast<float>(UploadedBvhCells_)});
    Header.push_back({CamFwd[0], CamFwd[1], CamFwd[2], static_cast<float>(LumCount)});
    Header.push_back({Config.ZenithSkyRgb[0], Config.ZenithSkyRgb[1], Config.ZenithSkyRgb[2], static_cast<float>(std::max(1u, Config.RisCandidateCount))});
    Header.push_back({Config.HorizonSkyRgb[0], Config.HorizonSkyRgb[1], Config.HorizonSkyRgb[2], static_cast<float>(Config.SpatialNeighbors)});
    Header.push_back({Config.GroundBounceTintRgb[0], Config.GroundBounceTintRgb[1], Config.GroundBounceTintRgb[2], static_cast<float>(Config.SdfIndirectRays)});
    Header.push_back({Config.EnableReflections ? 1.0f : 0.0f,
                      Config.EnableSpatialReuse ? 1.0f : 0.0f,
                      HasSdfVolume_ ? 1.0f : 0.0f,
                      0.0f});

    for (uint32_t Idx = 0u; Idx < LumCount; ++Idx)
    {
        const AnalyticalLuminaire& L = Luminaires[Idx];
        Header.push_back({L.DirectionOrPosition[0], L.DirectionOrPosition[1], L.DirectionOrPosition[2], static_cast<float>(L.Kind)});
        Header.push_back({L.AxisU[0], L.AxisU[1], L.AxisU[2], 0.0f});
        Header.push_back({L.AxisV[0], L.AxisV[1], L.AxisV[2], 0.0f});
        Header.push_back({L.EmittedRadianceRgb[0], L.EmittedRadianceRgb[1], L.EmittedRadianceRgb[2], 1.0f});
    }
    if (LumCount == 0u)
    {
        Header.resize(12u, {0.0f, 0.0f, 0.0f, 0.0f});
    }

    if (!Exchange.UploadMemorySpan(FrameHeaderSpan_, Header.data(), Header.size() * sizeof(float) * 4u))
    {
        return false;
    }

    const bool UseTemporal = Config.EnableTemporalReuse && HistoryValid_;
    const std::array<ComputeDescriptorArgument, 10> Args{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &FrameHeaderSpan_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhCellSpan_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhTriangleSpan_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &MaterialSpan_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &DistanceVolumeSpan_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &ReservoirCurrSpan_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &ReservoirPrevSpan_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &SurfaceAtlas_},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &IlluminationWork_},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &OutputImage_},
    }};

    const uint32_t GroupsX = (W + 7u) / 8u;
    const uint32_t GroupsY = (H + 7u) / 8u;

    const SpatiotemporalPushConstants BasePush{
        {W, H, FrameCounter_, 0u},
        {Config.ExposureScale, 1.0f, 10.0f, UseTemporal ? 1.0f : 0.0f},
    };

    if (!Exchange.DispatchCompute(PrimaryProgram_, Args, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u) ||
        !Exchange.DispatchCompute(DirectRisProgram_, Args, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u) ||
        !Exchange.DispatchCompute(SpatialGiProgram_, Args, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u))
    {
        return false;
    }

    const uint32_t Iterations = std::clamp(Config.AtrousIterations, 1u, 4u);
    for (uint32_t Iter = 0u; Iter < Iterations; ++Iter)
    {
        const uint32_t SrcLayer  = Iter & 1u;
        const uint32_t FinalIter = (Iter + 1u == Iterations) ? 1u : 0u;
        const SpatiotemporalPushConstants AtrousPush{
            {W, H, SrcLayer, FinalIter},
            {Config.ExposureScale, static_cast<float>(1u << Iter), 10.0f, 0.0f},
        };
        if (!Exchange.DispatchCompute(AtrousProgram_, Args, &AtrousPush, sizeof(AtrousPush), GroupsX, GroupsY, 1u))
        {
            return false;
        }
    }

    std::swap(ReservoirCurrSpan_, ReservoirPrevSpan_);
    HistoryValid_ = true;
    ++FrameCounter_;

    if (OutTelemetry)
    {
        OutTelemetry->BvhCellCount     = UploadedBvhCells_;
        OutTelemetry->BvhTriangleCount = UploadedBvhTris_;
        OutTelemetry->LuminaireCount   = LumCount;
        OutTelemetry->SdfVolumeActive  = HasSdfVolume_;
        OutTelemetry->FrameIndex       = FrameCounter_;
    }
    return true;
}

bool SpatiotemporalIllumination::ReadbackLinearRgba(VulkanComputeExchange& Exchange,
                                                    std::vector<float>&    OutLinearRgbaFloats)
{
    return Exchange.DownloadRgba16fLayer(OutputImage_, 0u, OutLinearRgbaFloats);
}

bool SpatiotemporalIllumination::ReadbackTonemappedSrgb8(VulkanComputeExchange& Exchange,
                                                         float                  ExposureScale,
                                                         std::vector<uint8_t>&  OutSrgbRgbaBytes)
{
    std::vector<float> LinearRgba;
    if (!ReadbackLinearRgba(Exchange, LinearRgba))
    {
        return false;
    }
    TonemapHdrToSrgb8(LinearRgba, OutputImage_.Width, OutputImage_.Height, ExposureScale, OutSrgbRgbaBytes);
    return true;
}

} // namespace Frontier
