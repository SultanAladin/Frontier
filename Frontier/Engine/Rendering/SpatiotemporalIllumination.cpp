/*==========================================================================================================================================
                                          SPATIOTEMPORAL ILLUMINATION DEFINITION
==========================================================================================================================================*/
// 🧩 GTX-first Option A (Phase 3C-1 + 3C-2) Vulkan 1.3 compute orchestrator:
//    GPU Surface Cards multi-bounce radiosity, Two-level 8-wide OctetVolumeAtlas, 4-cascade RangeDistanceAtlas,
//    LuminousFacetAtlas CDF, motion-reprojected ReSTIR DI & GI, Heitz VNDF reflections, & Split Diffuse/Specular À-Trous.

#include "Engine/Rendering/SpatiotemporalIllumination.h"

#include "Engine/PlanetaryAtmosphere/PlanetaryEnsemble.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace Frontier
{
namespace
{

struct SpatiotemporalPushConstants
{
    uint32_t Counts[4]; // {Width, Height, FrameOrIter, StepOrFlags}
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

// Builds exact world-to-NDC rows matching GenerateCameraRay:
//   ViewPos = (dot(Right, P - O), dot(Up, P - O), dot(Fwd, P - O))
//   NdcX = ViewPos.x / (ViewPos.z * TanHalfH),  NdcY = ViewPos.y / (ViewPos.z * TanHalfV)
std::array<float, 16> ComputeCameraWorldToNdcRows(const std::array<float, 3>& Origin,
                                                  const std::array<float, 3>& Right,
                                                  const std::array<float, 3>& Up,
                                                  const std::array<float, 3>& Fwd,
                                                  float                       TanHalfH,
                                                  float                       TanHalfV) noexcept
{
    const float InvTanH = 1.0f / std::max(1.0e-5f, TanHalfH);
    const float InvTanV = 1.0f / std::max(1.0e-5f, TanHalfV);

    const float R0X = Right[0] * InvTanH;
    const float R0Y = Right[1] * InvTanH;
    const float R0Z = Right[2] * InvTanH;
    const float R0W = -DotVec(Right, Origin) * InvTanH;

    const float R1X = Up[0] * InvTanV;
    const float R1Y = Up[1] * InvTanV;
    const float R1Z = Up[2] * InvTanV;
    const float R1W = -DotVec(Up, Origin) * InvTanV;

    const float R2X = Fwd[0];
    const float R2Y = Fwd[1];
    const float R2Z = Fwd[2];
    const float R2W = -DotVec(Fwd, Origin);

    return {
        R0X, R0Y, R0Z, R0W,
        R1X, R1Y, R1Z, R1W,
        R2X, R2Y, R2Z, R2W,
        R2X, R2Y, R2Z, R2W,
    };
}

} // namespace

bool SpatiotemporalIllumination::Initialize(VulkanComputeExchange& Exchange,
                                            const std::string&     ShaderBinaryDirectory,
                                            std::string*           OutDiagnostic)
{
    Shutdown(Exchange);

    const std::array<VkDescriptorType, 17> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 0:  FrameHeader
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 1:  BvhOrTopCells
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 2:  BvhTriangles
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 3:  MaterialTable
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 4:  DistanceOrRangeTable
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 5:  OctetInstances
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 6:  OctetBlocks
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 7:  LuminousFacets
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 8:  SurfaceCardPack
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 9:  DirectReservoirCurr
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 10: DirectReservoirPrev
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 11: IndirectReservoirCurr
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 12: IndirectReservoirPrev
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 13: MaterialMaps (256x256 sRGB/RGBA8 array)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 14: SurfaceAndWorkAtlas (10 layers rgba16f)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 15: OutputImage (1 layer rgba16f)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 16: ViewportDisplayTarget (2D rgba8)
    }};

    auto RecordStats = [&](const ComputeProgramSlot& Slot)
    {
        CompileStats_.push_back({
            Slot.EntryName,
            Slot.SpirvFingerprintFnv64,
            Slot.SpirvByteCount,
            Slot.ShaderModuleMilliseconds,
            Slot.PipelineCompileMilliseconds,
        });
    };

    CompileStats_.clear();
    if (!Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/SolveSurfaceRadiosity.spv",
                                     "SolveSurfaceRadiosityMain",
                                     Bindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     RadiosityProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/TracePrimarySurface.spv",
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
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/SampleIndirectReservoirs.spv",
                                     "SampleIndirectReservoirsMain",
                                     Bindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     IndirectGiProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/ReuseSpatialReservoirs.spv",
                                     "ReuseSpatialReservoirsMain",
                                     Bindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     SpatialReflectProgram_,
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

    RecordStats(RadiosityProgram_);
    RecordStats(PrimaryProgram_);
    RecordStats(DirectRisProgram_);
    RecordStats(IndirectGiProgram_);
    RecordStats(SpatialReflectProgram_);
    RecordStats(AtrousProgram_);

    // Allocate neutral placeholder spans and fallback textures so every binding is non-null prior to scene upload.
    const std::array<float, 36> ZeroFloats{};
    if (!Exchange.UploadMemorySpan(OctetInstanceSpan_, ZeroFloats.data(), sizeof(ZeroFloats)) ||
        !Exchange.UploadMemorySpan(OctetBlockSpan_, ZeroFloats.data(), sizeof(ZeroFloats)) ||
        !Exchange.UploadMemorySpan(LuminousFacetSpan_, ZeroFloats.data(), sizeof(ZeroFloats)) ||
        !Exchange.UploadMemorySpan(SurfaceCardSpan_, ZeroFloats.data(), sizeof(ZeroFloats)))
    {
        return false;
    }

    std::vector<uint8_t> WhiteTex(16u * 16u * 4u, 255u);
    if (!Exchange.AllocateSrgb8Array(16u, 16u, 1u, MaterialTextureArray_) ||
        !Exchange.UploadSrgb8Array(MaterialTextureArray_, WhiteTex))
    {
        return false;
    }

    return Exchange.AllocateRgba16fArray(16u, 16u, 10u, SurfaceAndWorkAtlas_) &&
           Exchange.AllocateRgba16fArray(16u, 16u, 1u, OutputImage_) &&
           Exchange.AllocateViewportImage2D(16u, 16u, FallbackViewport2D_);
}

void SpatiotemporalIllumination::Shutdown(VulkanComputeExchange& Exchange) noexcept
{
    Exchange.ReleaseViewportImage2D(FallbackViewport2D_);
    Exchange.ReleaseImage(OutputImage_);
    Exchange.ReleaseImage(SurfaceAndWorkAtlas_);
    Exchange.ReleaseImage(MaterialTextureArray_);

    Exchange.ReleaseMemorySpan(IndirectReservoirPrevSpan_);
    Exchange.ReleaseMemorySpan(IndirectReservoirCurrSpan_);
    Exchange.ReleaseMemorySpan(DirectReservoirPrevSpan_);
    Exchange.ReleaseMemorySpan(DirectReservoirCurrSpan_);
    Exchange.ReleaseMemorySpan(SurfaceCardSpan_);
    Exchange.ReleaseMemorySpan(LuminousFacetSpan_);
    Exchange.ReleaseMemorySpan(OctetBlockSpan_);
    Exchange.ReleaseMemorySpan(OctetInstanceSpan_);
    Exchange.ReleaseMemorySpan(DistanceOrRangeSpan_);
    Exchange.ReleaseMemorySpan(MaterialSpan_);
    Exchange.ReleaseMemorySpan(BvhTriangleSpan_);
    Exchange.ReleaseMemorySpan(BvhOrTopCellSpan_);
    for (DeviceMemorySpan& RingSpan : FrameHeaderRing_)
    {
        Exchange.ReleaseMemorySpan(RingSpan);
    }
    Exchange.ReleaseMemorySpan(FrameHeaderSpan_);

    Exchange.ReleaseProgram(AtrousProgram_);
    Exchange.ReleaseProgram(SpatialReflectProgram_);
    Exchange.ReleaseProgram(IndirectGiProgram_);
    Exchange.ReleaseProgram(DirectRisProgram_);
    Exchange.ReleaseProgram(PrimaryProgram_);
    Exchange.ReleaseProgram(RadiosityProgram_);

    UploadedBvhCells_       = 0u;
    UploadedBvhTris_        = 0u;
    UploadedBottomArchives_ = 0u;
    UploadedInstances_      = 0u;
    UploadedOctetBlocks_    = 0u;
    UploadedLuminousFacets_ = 0u;
    UploadedSurfacePatches_ = 0u;
    UploadedTextureLayers_  = 0u;
    TwoLevelOctetActive_    = false;
    HasSdfVolume_           = false;
    RangeClipmapActive_     = false;
    HistoryValid_           = false;
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

    uint32_t             TexW = 0u, TexH = 0u, TexLayers = 0u;
    std::vector<uint8_t> TexBytes;
    MaterialAtlas.PackTextureMapsForGpu(TexW, TexH, TexLayers, TexBytes);
    if (!Exchange.AllocateSrgb8Array(TexW, TexH, TexLayers, MaterialTextureArray_) ||
        !Exchange.UploadSrgb8Array(MaterialTextureArray_, TexBytes))
    {
        return false;
    }

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

    // Build luminous triangle CDF automatically from packed BVH triangles so self-radiant softboxes/headlamps participate!
    std::vector<PackedRayTriangle> ExtractedTris;
    ExtractedTris.reserve(BvhAtlas.TriangleCount());
    for (size_t Base = 0u; Base + 4u < TriTable.size(); Base += 5u)
    {
        PackedRayTriangle T{};
        T.Vertex0          = {TriTable[Base + 0u][0], TriTable[Base + 0u][1], TriTable[Base + 0u][2]};
        T.Edge1            = {TriTable[Base + 1u][0], TriTable[Base + 1u][1], TriTable[Base + 1u][2]};
        T.Edge2            = {TriTable[Base + 2u][0], TriTable[Base + 2u][1], TriTable[Base + 2u][2]};
        T.ClassificationId = std::bit_cast<uint32_t>(TriTable[Base + 2u][3]) & 0x7FFFFFFFu;
        ExtractedTris.push_back(T);
    }
    LuminousFacetAtlas AutoLuminous{};
    AutoLuminous.BuildFromWorldTriangles(ExtractedTris, MaterialAtlas);
    std::vector<std::array<float, 4>> LumFacetTable;
    AutoLuminous.PackForGpu(LumFacetTable);

    if (!Exchange.UploadMemorySpan(BvhOrTopCellSpan_, CellTable.data(), CellTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(BvhTriangleSpan_, TriTable.data(), TriTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(MaterialSpan_, MatTable.data(), MatTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(DistanceOrRangeSpan_, SdfTable.data(), SdfTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(LuminousFacetSpan_, LumFacetTable.data(), LumFacetTable.size() * sizeof(float) * 4u))
    {
        return false;
    }

    UploadedBvhCells_       = BvhAtlas.CellCount();
    UploadedBvhTris_        = BvhAtlas.TriangleCount();
    UploadedBottomArchives_ = 1u;
    UploadedInstances_      = 1u;
    UploadedOctetBlocks_    = 0u;
    UploadedLuminousFacets_ = AutoLuminous.FacetCount();
    UploadedSurfacePatches_ = 0u;
    UploadedTextureLayers_  = MaterialAtlas.AuthoredTextureLayerCount();
    TwoLevelOctetActive_    = false;
    RangeClipmapActive_     = false;
    HistoryValid_           = false;
    return true;
}

bool SpatiotemporalIllumination::UploadTwoLevelScene(VulkanComputeExchange&        Exchange,
                                                     const OctetVolumeAtlas&       TwoLevelAtlas,
                                                     const SurfaceScatteringAtlas& MaterialAtlas,
                                                     const RangeDistanceAtlas*     RangeClipmap,
                                                     const SurfaceRadiosityAtlas*  RadiosityCards,
                                                     const LuminousFacetAtlas*     LuminousFacets)
{
    std::vector<std::array<float, 4>> TopTable;
    std::vector<std::array<float, 4>> InstTable;
    std::vector<std::array<float, 4>> OctetTable;
    std::vector<std::array<float, 4>> TriTable;
    TwoLevelAtlas.PackForGpu(TopTable, InstTable, OctetTable, TriTable);

    std::vector<std::array<float, 4>> MatTable;
    MaterialAtlas.PackForGpu(MatTable);

    uint32_t             TexW = 0u, TexH = 0u, TexLayers = 0u;
    std::vector<uint8_t> TexBytes;
    MaterialAtlas.PackTextureMapsForGpu(TexW, TexH, TexLayers, TexBytes);
    if (!Exchange.AllocateSrgb8Array(TexW, TexH, TexLayers, MaterialTextureArray_) ||
        !Exchange.UploadSrgb8Array(MaterialTextureArray_, TexBytes))
    {
        return false;
    }

    std::vector<std::array<float, 4>> RangeTable;
    if (RangeClipmap != nullptr && !RangeClipmap->Empty())
    {
        RangeClipmap->PackForGpu(RangeTable);
        RangeClipmapActive_ = true;
        HasSdfVolume_       = true;
    }
    else
    {
        VolumeDistanceField Empty{};
        PackVolumeDistanceForGpu(Empty, RangeTable);
        RangeClipmapActive_ = false;
        HasSdfVolume_       = false;
    }

    std::vector<std::array<float, 4>> CardGeoTable;
    std::vector<std::array<float, 4>> CardRadTable;
    if (RadiosityCards != nullptr && RadiosityCards->PatchCount() > 0u)
    {
        RadiosityCards->PackForGpu(CardGeoTable, CardRadTable);
        UploadedSurfacePatches_ = RadiosityCards->PatchCount();
    }
    else
    {
        CardGeoTable.resize(4u, {0.0f, 0.0f, 0.0f, 0.0f});
        CardRadTable.resize(1u, {0.0f, 0.0f, 0.0f, 0.0f});
        UploadedSurfacePatches_ = 0u;
    }
    std::vector<std::array<float, 4>> CombinedCardSpan;
    CombinedCardSpan.reserve(CardGeoTable.size() + CardRadTable.size());
    CombinedCardSpan.insert(CombinedCardSpan.end(), CardGeoTable.begin(), CardGeoTable.end());
    CombinedCardSpan.insert(CombinedCardSpan.end(), CardRadTable.begin(), CardRadTable.end());

    std::vector<std::array<float, 4>> LumFacetTable;
    if (LuminousFacets != nullptr && !LuminousFacets->Empty())
    {
        LuminousFacets->PackForGpu(LumFacetTable);
        UploadedLuminousFacets_ = LuminousFacets->FacetCount();
    }
    else
    {
        LumFacetTable.resize(4u, {0.0f, 0.0f, 0.0f, 0.0f});
        UploadedLuminousFacets_ = 0u;
    }

    if (!Exchange.UploadMemorySpan(BvhOrTopCellSpan_, TopTable.data(), TopTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(OctetInstanceSpan_, InstTable.data(), InstTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(OctetBlockSpan_, OctetTable.data(), OctetTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(BvhTriangleSpan_, TriTable.data(), TriTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(MaterialSpan_, MatTable.data(), MatTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(DistanceOrRangeSpan_, RangeTable.data(), RangeTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(SurfaceCardSpan_, CombinedCardSpan.data(), CombinedCardSpan.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(LuminousFacetSpan_, LumFacetTable.data(), LumFacetTable.size() * sizeof(float) * 4u))
    {
        return false;
    }

    UploadedBvhCells_       = TwoLevelAtlas.TopCellCount();
    UploadedBvhTris_        = TwoLevelAtlas.TotalTriangleCount();
    UploadedBottomArchives_ = TwoLevelAtlas.BottomArchiveCount();
    UploadedInstances_      = TwoLevelAtlas.InstanceCount();
    UploadedOctetBlocks_    = TwoLevelAtlas.TotalOctetBlockCount();
    UploadedTextureLayers_  = MaterialAtlas.AuthoredTextureLayerCount();
    TwoLevelOctetActive_    = true;
    HistoryValid_           = false;
    return true;
}

bool SpatiotemporalIllumination::UpdateDynamicTopLevelInstances(VulkanComputeExchange&  Exchange,
                                                                const OctetVolumeAtlas& TwoLevelAtlas)
{
    if (!TwoLevelOctetActive_ || TwoLevelAtlas.Empty())
    {
        return false;
    }
    std::vector<std::array<float, 4>> TopTable;
    std::vector<std::array<float, 4>> InstTable;
    std::vector<std::array<float, 4>> UnusedOctet;
    std::vector<std::array<float, 4>> UnusedTri;
    TwoLevelAtlas.PackForGpu(TopTable, InstTable, UnusedOctet, UnusedTri);

    if (!Exchange.UploadMemorySpan(BvhOrTopCellSpan_, TopTable.data(), TopTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(OctetInstanceSpan_, InstTable.data(), InstTable.size() * sizeof(float) * 4u))
    {
        return false;
    }
    UploadedBvhCells_  = TwoLevelAtlas.TopCellCount();
    UploadedInstances_ = TwoLevelAtlas.InstanceCount();
    return true;
}

bool SpatiotemporalIllumination::UpdateRangeClipmap(VulkanComputeExchange&    Exchange,
                                                    const RangeDistanceAtlas& RangeClipmap)
{
    if (RangeClipmap.Empty())
    {
        return false;
    }
    std::vector<std::array<float, 4>> RangeTable;
    RangeClipmap.PackForGpu(RangeTable);
    if (!Exchange.UploadMemorySpan(DistanceOrRangeSpan_, RangeTable.data(), RangeTable.size() * sizeof(float) * 4u))
    {
        return false;
    }
    RangeClipmapActive_ = true;
    HasSdfVolume_       = true;
    return true;
}

bool SpatiotemporalIllumination::RenderFrame(VulkanComputeExchange&                  Exchange,
                                             const SpatiotemporalCamera&             Camera,
                                             const std::vector<AnalyticalLuminaire>& Luminaires,
                                             const SpatiotemporalRenderConfig&       Config,
                                             SpatiotemporalFrameTelemetry*           OutTelemetry,
                                             DeviceViewportImageSlot*                OptionalViewportTarget)
{
    const auto     CpuFrameStart = std::chrono::steady_clock::now();
    const uint32_t W             = std::max(1u, Camera.WidthPx);
    const uint32_t H             = std::max(1u, Camera.HeightPx);

    if (OutputImage_.Width != W || OutputImage_.Height != H)
    {
        if (!Exchange.AllocateRgba16fArray(W, H, 10u, SurfaceAndWorkAtlas_) ||
            !Exchange.AllocateRgba16fArray(W, H, 1u, OutputImage_))
        {
            return false;
        }
        HistoryValid_ = false;
    }

    DeviceViewportImageSlot* ViewportSlot = OptionalViewportTarget;
    if (ViewportSlot == nullptr)
    {
        if (FallbackViewport2D_.Width != W || FallbackViewport2D_.Height != H)
        {
            if (!Exchange.AllocateViewportImage2D(W, H, FallbackViewport2D_))
            {
                return false;
            }
        }
        ViewportSlot = &FallbackViewport2D_;
    }
    if (!Exchange.PrepareViewportImageForCompute(*ViewportSlot))
    {
        return false;
    }

    // 2 x float4 (= 32 B) per pixel reservoir for BOTH Direct DI and Indirect GI
    const size_t ReservoirBytes = static_cast<size_t>(W) * H * 2u * sizeof(float) * 4u;
    if (DirectReservoirCurrSpan_.Capacity < ReservoirBytes || DirectReservoirPrevSpan_.Capacity < ReservoirBytes ||
        IndirectReservoirCurrSpan_.Capacity < ReservoirBytes || IndirectReservoirPrevSpan_.Capacity < ReservoirBytes)
    {
        std::vector<float> ZeroRes(static_cast<size_t>(W) * H * 8u, 0.0f);
        if (!Exchange.UploadMemorySpan(DirectReservoirCurrSpan_, ZeroRes.data(), ReservoirBytes) ||
            !Exchange.UploadMemorySpan(DirectReservoirPrevSpan_, ZeroRes.data(), ReservoirBytes) ||
            !Exchange.UploadMemorySpan(IndirectReservoirCurrSpan_, ZeroRes.data(), ReservoirBytes) ||
            !Exchange.UploadMemorySpan(IndirectReservoirPrevSpan_, ZeroRes.data(), ReservoirBytes))
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

    const std::array<float, 16> CurrentViewProjRows =
        ComputeCameraWorldToNdcRows(Camera.OriginMeters, CamRight, CamUp, CamFwd, TanHalfH, TanHalfV);
    const std::array<float, 16> PrevRows = HistoryValid_ ? PreviousViewProjRows_ : CurrentViewProjRows;

    const uint32_t LumCount = static_cast<uint32_t>(std::min<size_t>(Luminaires.size(), 32u));
    const auto&    Env      = Config.Environment;

    // 20-slot fixed header + 4 * LumCount luminaire records
    std::vector<std::array<float, 4>> Header;
    Header.reserve(20u + std::max(1u, LumCount) * 4u);
    Header.push_back({Camera.OriginMeters[0], Camera.OriginMeters[1], Camera.OriginMeters[2], TanHalfV});                  // [0]
    Header.push_back({CamRight[0], CamRight[1], CamRight[2], TanHalfH});                                                   // [1]
    Header.push_back({CamUp[0], CamUp[1], CamUp[2], static_cast<float>(UploadedBvhCells_)});                               // [2]
    Header.push_back({CamFwd[0], CamFwd[1], CamFwd[2], static_cast<float>(LumCount)});                                     // [3]
    Header.push_back({Config.ZenithSkyRgb[0], Config.ZenithSkyRgb[1], Config.ZenithSkyRgb[2], static_cast<float>(std::max(1u, Config.RisCandidateCount))}); // [4]
    Header.push_back({Config.HorizonSkyRgb[0], Config.HorizonSkyRgb[1], Config.HorizonSkyRgb[2], static_cast<float>(Config.SpatialNeighbors)});             // [5]
    Header.push_back({Config.GroundBounceTintRgb[0], Config.GroundBounceTintRgb[1], Config.GroundBounceTintRgb[2], static_cast<float>(Config.SdfIndirectRays)}); // [6]
    Header.push_back({Config.EnableReflections ? 1.0f : 0.0f,
                      Config.EnableSpatialReuse ? 1.0f : 0.0f,
                      HasSdfVolume_ ? 1.0f : 0.0f,
                      Config.EnableIndirectGi ? 1.0f : 0.0f});                                                             // [7]
    Header.push_back({PrevRows[0], PrevRows[1], PrevRows[2], PrevRows[3]});                                                // [8]
    Header.push_back({PrevRows[4], PrevRows[5], PrevRows[6], PrevRows[7]});                                                // [9]
    Header.push_back({PrevRows[8], PrevRows[9], PrevRows[10], PrevRows[11]});                                              // [10]
    Header.push_back({PrevRows[12], PrevRows[13], PrevRows[14], PrevRows[15]});                                            // [11]
    Header.push_back({TwoLevelOctetActive_ ? 1.0f : 0.0f,
                      RangeClipmapActive_ ? 1.0f : 0.0f,
                      static_cast<float>(UploadedLuminousFacets_),
                      static_cast<float>(UploadedSurfacePatches_)});                                                       // [12]
    Header.push_back({static_cast<float>(Config.InspectionMode),
                      static_cast<float>(std::clamp(Config.TemporalHistoryLimit, 1u, 64u)),
                      Config.SurfaceCardGain,
                      Config.SurfaceCardSkyWeight});                                                                       // [13]
    Header.push_back({Env.CloudsEnabled ? Env.CloudCoverage : -1.0f,
                      Env.CloudDensity,
                      Env.CloudBottomMeters,
                      Env.LunarEnabled ? Env.DaytimeVeilStrength : -1.0f});                                                // [14]
    Header.push_back({Env.PrimaryMoonDir[0], Env.PrimaryMoonDir[1], Env.PrimaryMoonDir[2], Env.PrimaryMoonRadius});        // [15]
    Header.push_back({Env.SecondaryMoonDir[0], Env.SecondaryMoonDir[1], Env.SecondaryMoonDir[2], Env.SecondaryMoonRadius});// [16]
    Header.push_back({Env.FogInscatteringRgb[0], Env.FogInscatteringRgb[1], Env.FogInscatteringRgb[2], Env.FogEnabled ? Env.FogDensity : -1.0f}); // [17]
    Header.push_back({Env.FogHeightFalloff, static_cast<float>(std::min(Config.RadiosityBounces, 8u)), 0.0f, 0.0f});       // [18]
    Header.push_back({0.0f, 0.0f, 0.0f, 0.0f});                                                                            // [19]

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
        Header.resize(24u, {0.0f, 0.0f, 0.0f, 0.0f});
    }

    if (!Exchange.UploadMemorySpan(FrameHeaderSpan_, Header.data(), Header.size() * sizeof(float) * 4u))
    {
        return false;
    }

    const bool                                      UseTemporal = Config.EnableTemporalReuse && HistoryValid_;
    const std::array<ComputeDescriptorArgument, 17> Args{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &FrameHeaderSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhOrTopCellSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhTriangleSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &MaterialSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &DistanceOrRangeSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &OctetInstanceSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &OctetBlockSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &LuminousFacetSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &SurfaceCardSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &DirectReservoirCurrSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &DirectReservoirPrevSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &IndirectReservoirCurrSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &IndirectReservoirPrevSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &MaterialTextureArray_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &SurfaceAndWorkAtlas_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &OutputImage_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, nullptr, ViewportSlot},
    }};

    const uint32_t GroupsX = (W + 7u) / 8u;
    const uint32_t GroupsY = (H + 7u) / 8u;

    const SpatiotemporalPushConstants BasePush{
        {W, H, FrameCounter_, 0u},
        {Config.ExposureScale, 1.0f, 10.0f, UseTemporal ? 1.0f : 0.0f},
    };

    float RadiosityMs = 0.0f;
    if (UploadedSurfacePatches_ > 0u)
    {
        if (!Exchange.DispatchCompute(RadiosityProgram_, Args, &BasePush, sizeof(BasePush), 1u, 1u, 1u))
        {
            return false;
        }
        RadiosityMs = Exchange.QueryLastDispatchGpuMilliseconds();
    }

    if (!Exchange.DispatchCompute(PrimaryProgram_, Args, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u))
    {
        return false;
    }
    const float PrimaryMs = Exchange.QueryLastDispatchGpuMilliseconds();

    if (!Exchange.DispatchCompute(DirectRisProgram_, Args, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u))
    {
        return false;
    }
    const float DirectRisMs = Exchange.QueryLastDispatchGpuMilliseconds();

    if (!Exchange.DispatchCompute(IndirectGiProgram_, Args, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u))
    {
        return false;
    }
    const float IndirectGiMs = Exchange.QueryLastDispatchGpuMilliseconds();

    if (!Exchange.DispatchCompute(SpatialReflectProgram_, Args, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u))
    {
        return false;
    }
    const float SpatialReflMs = Exchange.QueryLastDispatchGpuMilliseconds();

    float          AtrousMs   = 0.0f;
    const uint32_t Iterations = std::clamp(Config.AtrousIterations, 1u, 4u);
    for (uint32_t Iter = 0u; Iter < Iterations; ++Iter)
    {
        const uint32_t SrcParity = Iter & 1u;
        const uint32_t FinalIter = (Iter + 1u == Iterations) ? 1u : 0u;
        const SpatiotemporalPushConstants AtrousPush{
            {W, H, SrcParity, FinalIter},
            {Config.ExposureScale, static_cast<float>(1u << Iter), 10.0f, 0.0f},
        };
        if (!Exchange.DispatchCompute(AtrousProgram_, Args, &AtrousPush, sizeof(AtrousPush), GroupsX, GroupsY, 1u))
        {
            return false;
        }
        AtrousMs += Exchange.QueryLastDispatchGpuMilliseconds();
    }

    if (OptionalViewportTarget != nullptr)
    {
        Exchange.PrepareViewportImageForSampling(*OptionalViewportTarget);
    }

    // Spatial reuse kernel reads *Curr and writes *Prev, so *Prev already holds next frame's temporal history.
    PreviousViewProjRows_ = CurrentViewProjRows;
    HistoryValid_         = true;
    ++FrameCounter_;

    const float CpuSubmitMs =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - CpuFrameStart).count();

    if (OutTelemetry)
    {
        OutTelemetry->BvhCellCount          = UploadedBvhCells_;
        OutTelemetry->BvhTriangleCount      = UploadedBvhTris_;
        OutTelemetry->BottomArchiveCount    = UploadedBottomArchives_;
        OutTelemetry->TopInstanceCount      = UploadedInstances_;
        OutTelemetry->OctetBlockCount       = UploadedOctetBlocks_;
        OutTelemetry->LuminaireCount        = LumCount;
        OutTelemetry->LuminousFacetCount    = UploadedLuminousFacets_;
        OutTelemetry->SurfacePatchCount     = UploadedSurfacePatches_;
        OutTelemetry->AuthoredTextureLayers = UploadedTextureLayers_;
        OutTelemetry->SdfVolumeActive       = HasSdfVolume_;
        OutTelemetry->RangeClipmapActive    = RangeClipmapActive_;
        OutTelemetry->FrameIndex            = FrameCounter_;
        OutTelemetry->RadiosityGpuMs        = RadiosityMs;
        OutTelemetry->PrimaryGpuMs          = PrimaryMs;
        OutTelemetry->DirectRisGpuMs        = DirectRisMs;
        OutTelemetry->IndirectGiGpuMs       = IndirectGiMs;
        OutTelemetry->SpatialReflectGpuMs   = SpatialReflMs;
        OutTelemetry->AtrousResolveGpuMs    = AtrousMs;
        OutTelemetry->TotalGpuMs = RadiosityMs + PrimaryMs + DirectRisMs + IndirectGiMs + SpatialReflMs + AtrousMs;
        OutTelemetry->CpuSubmissionMs       = CpuSubmitMs;
    }
    return true;
}

bool SpatiotemporalIllumination::RecordFrameIntoExternalStream(
    VulkanComputeExchange&                  Exchange,
    VkCommandBuffer                         ExternalCommandBuffer,
    uint32_t                                FrameSlotIndex,
    const SpatiotemporalCamera&             Camera,
    const std::vector<AnalyticalLuminaire>& Luminaires,
    const SpatiotemporalRenderConfig&       Config,
    DeviceViewportImageSlot&                ViewportTarget,
    SpatiotemporalFrameTelemetry*           OutTelemetry)
{
    if (ExternalCommandBuffer == VK_NULL_HANDLE || ViewportTarget.Image == VK_NULL_HANDLE)
    {
        return false;
    }
    const auto     CpuFrameStart = std::chrono::steady_clock::now();
    const uint32_t W             = std::max(1u, ViewportTarget.Width);
    const uint32_t H             = std::max(1u, ViewportTarget.Height);
    const uint32_t SlotIdx       = FrameSlotIndex & 3u;

    // Harvest non-blocking GPU timestamp timings from this fence-waited slot's previous execution cycle.
    std::array<float, VulkanComputeExchange::MaxTimedDispatchesSlot> PrevTimings{};
    const bool HasPrevTimings = Exchange.HarvestExternalSlotTimings(SlotIdx, PrevTimings);

    if (OutputImage_.Width != W || OutputImage_.Height != H)
    {
        if (!Exchange.AllocateRgba16fArray(W, H, 10u, SurfaceAndWorkAtlas_) ||
            !Exchange.AllocateRgba16fArray(W, H, 1u, OutputImage_))
        {
            return false;
        }
        HistoryValid_ = false;
    }

    const size_t ReservoirBytes = static_cast<size_t>(W) * H * 2u * sizeof(float) * 4u;
    if (DirectReservoirCurrSpan_.Capacity < ReservoirBytes || DirectReservoirPrevSpan_.Capacity < ReservoirBytes ||
        IndirectReservoirCurrSpan_.Capacity < ReservoirBytes || IndirectReservoirPrevSpan_.Capacity < ReservoirBytes)
    {
        std::vector<float> ZeroRes(static_cast<size_t>(W) * H * 8u, 0.0f);
        if (!Exchange.UploadMemorySpan(DirectReservoirCurrSpan_, ZeroRes.data(), ReservoirBytes) ||
            !Exchange.UploadMemorySpan(DirectReservoirPrevSpan_, ZeroRes.data(), ReservoirBytes) ||
            !Exchange.UploadMemorySpan(IndirectReservoirCurrSpan_, ZeroRes.data(), ReservoirBytes) ||
            !Exchange.UploadMemorySpan(IndirectReservoirPrevSpan_, ZeroRes.data(), ReservoirBytes))
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

    const std::array<float, 16> CurrentViewProjRows =
        ComputeCameraWorldToNdcRows(Camera.OriginMeters, CamRight, CamUp, CamFwd, TanHalfH, TanHalfV);
    const std::array<float, 16> PrevRows = HistoryValid_ ? PreviousViewProjRows_ : CurrentViewProjRows;

    const uint32_t LumCount = static_cast<uint32_t>(std::min<size_t>(Luminaires.size(), 32u));
    const auto&    Env      = Config.Environment;

    std::vector<std::array<float, 4>> Header;
    Header.reserve(20u + std::max(1u, LumCount) * 4u);
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
                      Config.EnableIndirectGi ? 1.0f : 0.0f});
    Header.push_back({PrevRows[0], PrevRows[1], PrevRows[2], PrevRows[3]});
    Header.push_back({PrevRows[4], PrevRows[5], PrevRows[6], PrevRows[7]});
    Header.push_back({PrevRows[8], PrevRows[9], PrevRows[10], PrevRows[11]});
    Header.push_back({PrevRows[12], PrevRows[13], PrevRows[14], PrevRows[15]});
    Header.push_back({TwoLevelOctetActive_ ? 1.0f : 0.0f,
                      RangeClipmapActive_ ? 1.0f : 0.0f,
                      static_cast<float>(UploadedLuminousFacets_),
                      static_cast<float>(UploadedSurfacePatches_)});
    Header.push_back({static_cast<float>(Config.InspectionMode),
                      static_cast<float>(std::clamp(Config.TemporalHistoryLimit, 1u, 64u)),
                      Config.SurfaceCardGain,
                      Config.SurfaceCardSkyWeight});
    Header.push_back({Env.CloudsEnabled ? Env.CloudCoverage : -1.0f,
                      Env.CloudDensity,
                      Env.CloudBottomMeters,
                      Env.LunarEnabled ? Env.DaytimeVeilStrength : -1.0f});
    Header.push_back({Env.PrimaryMoonDir[0], Env.PrimaryMoonDir[1], Env.PrimaryMoonDir[2], Env.PrimaryMoonRadius});
    Header.push_back({Env.SecondaryMoonDir[0], Env.SecondaryMoonDir[1], Env.SecondaryMoonDir[2], Env.SecondaryMoonRadius});
    Header.push_back({Env.FogInscatteringRgb[0], Env.FogInscatteringRgb[1], Env.FogInscatteringRgb[2], Env.FogEnabled ? Env.FogDensity : -1.0f});
    Header.push_back({Env.FogHeightFalloff, static_cast<float>(std::min(Config.RadiosityBounces, 8u)), 0.0f, 0.0f});
    Header.push_back({0.0f, 0.0f, 0.0f, 0.0f});

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
        Header.resize(24u, {0.0f, 0.0f, 0.0f, 0.0f});
    }

    DeviceMemorySpan& RingHeaderSpan = FrameHeaderRing_[SlotIdx];
    if (!Exchange.UploadMemorySpan(RingHeaderSpan, Header.data(), Header.size() * sizeof(float) * 4u))
    {
        return false;
    }

    const bool                                      UseTemporal = Config.EnableTemporalReuse && HistoryValid_;
    const std::array<ComputeDescriptorArgument, 17> Args{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &RingHeaderSpan, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhOrTopCellSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhTriangleSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &MaterialSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &DistanceOrRangeSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &OctetInstanceSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &OctetBlockSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &LuminousFacetSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &SurfaceCardSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &DirectReservoirCurrSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &DirectReservoirPrevSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &IndirectReservoirCurrSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &IndirectReservoirPrevSpan_, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &MaterialTextureArray_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &SurfaceAndWorkAtlas_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &OutputImage_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, nullptr, &ViewportTarget},
    }};

    const uint32_t GroupsX = (W + 7u) / 8u;
    const uint32_t GroupsY = (H + 7u) / 8u;

    const SpatiotemporalPushConstants BasePush{
        {W, H, FrameCounter_, 0u},
        {Config.ExposureScale, 1.0f, 10.0f, UseTemporal ? 1.0f : 0.0f},
    };

    Exchange.BeginExternalCommandStream(ExternalCommandBuffer, SlotIdx);
    (void)Exchange.PrepareViewportImageForCompute(ViewportTarget);

    if (UploadedSurfacePatches_ > 0u)
    {
        (void)Exchange.DispatchCompute(RadiosityProgram_, Args, &BasePush, sizeof(BasePush), 1u, 1u, 1u);
    }
    (void)Exchange.DispatchCompute(PrimaryProgram_, Args, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u);
    (void)Exchange.DispatchCompute(DirectRisProgram_, Args, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u);
    (void)Exchange.DispatchCompute(IndirectGiProgram_, Args, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u);
    (void)Exchange.DispatchCompute(SpatialReflectProgram_, Args, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u);

    const uint32_t Iterations = std::clamp(Config.AtrousIterations, 1u, 4u);
    for (uint32_t Iter = 0u; Iter < Iterations; ++Iter)
    {
        const uint32_t SrcParity = Iter & 1u;
        const uint32_t FinalIter = (Iter + 1u == Iterations) ? 1u : 0u;
        const SpatiotemporalPushConstants AtrousPush{
            {W, H, SrcParity, FinalIter},
            {Config.ExposureScale, static_cast<float>(1u << Iter), 10.0f, 0.0f},
        };
        (void)Exchange.DispatchCompute(AtrousProgram_, Args, &AtrousPush, sizeof(AtrousPush), GroupsX, GroupsY, 1u);
    }

    (void)Exchange.PrepareViewportImageForSampling(ViewportTarget);
    Exchange.EndExternalCommandStream();

    // Spatial reuse kernel reads *Curr and writes *Prev, so *Prev already holds next frame's temporal history.
    PreviousViewProjRows_ = CurrentViewProjRows;
    HistoryValid_         = true;
    ++FrameCounter_;

    const float CpuSubmitMs =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - CpuFrameStart).count();

    if (OutTelemetry)
    {
        float RadMs = 0.0f, PrimMs = 0.0f, DirMs = 0.0f, IndMs = 0.0f, SpatMs = 0.0f, AtrMs = 0.0f;
        if (HasPrevTimings)
        {
            size_t Cursor = 0u;
            if (UploadedSurfacePatches_ > 0u && Cursor < PrevTimings.size())
            {
                RadMs = PrevTimings[Cursor++];
            }
            if (Cursor < PrevTimings.size()) PrimMs = PrevTimings[Cursor++];
            if (Cursor < PrevTimings.size()) DirMs  = PrevTimings[Cursor++];
            if (Cursor < PrevTimings.size()) IndMs  = PrevTimings[Cursor++];
            if (Cursor < PrevTimings.size()) SpatMs = PrevTimings[Cursor++];
            while (Cursor < PrevTimings.size())
            {
                AtrMs += PrevTimings[Cursor++];
            }
        }
        OutTelemetry->BvhCellCount          = UploadedBvhCells_;
        OutTelemetry->BvhTriangleCount      = UploadedBvhTris_;
        OutTelemetry->BottomArchiveCount    = UploadedBottomArchives_;
        OutTelemetry->TopInstanceCount      = UploadedInstances_;
        OutTelemetry->OctetBlockCount       = UploadedOctetBlocks_;
        OutTelemetry->LuminaireCount        = LumCount;
        OutTelemetry->LuminousFacetCount    = UploadedLuminousFacets_;
        OutTelemetry->SurfacePatchCount     = UploadedSurfacePatches_;
        OutTelemetry->AuthoredTextureLayers = UploadedTextureLayers_;
        OutTelemetry->SdfVolumeActive       = HasSdfVolume_;
        OutTelemetry->RangeClipmapActive    = RangeClipmapActive_;
        OutTelemetry->FrameIndex            = FrameCounter_;
        OutTelemetry->RadiosityGpuMs        = RadMs;
        OutTelemetry->PrimaryGpuMs          = PrimMs;
        OutTelemetry->DirectRisGpuMs        = DirMs;
        OutTelemetry->IndirectGiGpuMs       = IndMs;
        OutTelemetry->SpatialReflectGpuMs   = SpatMs;
        OutTelemetry->AtrousResolveGpuMs    = AtrMs;
        OutTelemetry->TotalGpuMs            = RadMs + PrimMs + DirMs + IndMs + SpatMs + AtrMs;
        OutTelemetry->CpuSubmissionMs       = CpuSubmitMs;
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

bool SpatiotemporalIllumination::DownloadSolvedSurfaceRadiosity(
    VulkanComputeExchange&             Exchange,
    std::vector<std::array<float, 3>>& OutSolvedRadiosityRgb) const
{
    OutSolvedRadiosityRgb.clear();
    if (UploadedSurfacePatches_ == 0u)
    {
        return false;
    }
    const size_t                      TotalQuads = static_cast<size_t>(UploadedSurfacePatches_) * 5u;
    std::vector<std::array<float, 4>> Raw(TotalQuads);
    if (!Exchange.DownloadMemorySpan(SurfaceCardSpan_, Raw.data(), TotalQuads * sizeof(float) * 4u))
    {
        return false;
    }
    OutSolvedRadiosityRgb.resize(UploadedSurfacePatches_);
    const size_t RadBase = static_cast<size_t>(UploadedSurfacePatches_) * 4u;
    for (uint32_t I = 0u; I < UploadedSurfacePatches_; ++I)
    {
        OutSolvedRadiosityRgb[I] = {Raw[RadBase + I][0], Raw[RadBase + I][1], Raw[RadBase + I][2]};
    }
    return true;
}

} // namespace Frontier
