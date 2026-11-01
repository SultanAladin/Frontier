/*==========================================================================================================================================
                                SPATIOTEMPORAL ILLUMINATION DEFINITION
==========================================================================================================================================*/
// 🧩 Phase 3C GTX-first Vulkan 1.3 six-kernel ReSTIR DI+GI, 8-wide CWBVH, 4-cascade SDF, & SurfaceCard orchestrator.

#include "Engine/Rendering/SpatiotemporalIllumination.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Frontier
{
namespace
{

constexpr uint32_t GaussianBufferLayerCount    = 6u;
constexpr uint32_t ReservoirLayerCount         = 3u;
constexpr uint32_t RadianceWorkLayerCount      = 2u;
constexpr uint32_t FrameUniformFloat4Count     = 14u;
constexpr uint32_t DispatchPushByteCount       = 16u;
constexpr uint32_t MaximumAnalyticalLuminaires = 32u;

[[nodiscard]] double LuminanceOf(std::array<float, 3> Rgb) noexcept
{
    return 0.2126 * static_cast<double>(Rgb[0]) + 0.7152 * static_cast<double>(Rgb[1]) +
           0.0722 * static_cast<double>(Rgb[2]);
}

[[nodiscard]] std::array<float, 3> NormalizeVec(std::array<float, 3> V) noexcept
{
    const float Len = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
    if (Len <= 1.0e-9f)
    {
        return {0.0f, 0.0f, 1.0f};
    }
    return {V[0] / Len, V[1] / Len, V[2] / Len};
}

[[nodiscard]] std::array<float, 3> CrossVec(std::array<float, 3> A, std::array<float, 3> B) noexcept
{
    return {A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]};
}

[[nodiscard]] float DotVec(std::array<float, 3> A, std::array<float, 3> B) noexcept
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

[[nodiscard]] std::array<float, 3> SubVec(std::array<float, 3> A, std::array<float, 3> B) noexcept
{
    return {A[0] - B[0], A[1] - B[1], A[2] - B[2]};
}

struct DispatchPushGpu
{
    uint32_t Width;
    uint32_t Height;
    uint32_t FrameIndex;
    uint32_t AtrousStepWidth;
};
static_assert(sizeof(DispatchPushGpu) == DispatchPushByteCount, "Dispatch push constants must occupy 16 bytes.");

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            TARGET ALLOCATION & LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

bool SpatiotemporalIllumination::EnsureViewportTargets(VulkanComputeExchange& Exchange, uint32_t Width, uint32_t Height)
{
    if (TargetWidth_ == Width && TargetHeight_ == Height && GBufferPingPong_[0].Image != VK_NULL_HANDLE &&
        DisplayRgba8Target_.Image != VK_NULL_HANDLE)
    {
        return true;
    }

    for (auto& Slot : GBufferPingPong_)
    {
        Exchange.ReleaseImage(Slot);
    }
    for (auto& Slot : ReservoirPingPong_)
    {
        Exchange.ReleaseImage(Slot);
    }
    for (auto& Slot : RadiancePingPong_)
    {
        Exchange.ReleaseImage(Slot);
    }
    Exchange.ReleaseImage(DisplayRgba8Target_);

    for (auto& Slot : GBufferPingPong_)
    {
        if (!Exchange.AllocateRgba16fArray(Width, Height, GaussianBufferLayerCount, Slot))
        {
            return false;
        }
    }
    for (auto& Slot : ReservoirPingPong_)
    {
        if (!Exchange.AllocateRgba16fArray(Width, Height, ReservoirLayerCount, Slot))
        {
            return false;
        }
    }
    for (auto& Slot : RadiancePingPong_)
    {
        if (!Exchange.AllocateRgba16fArray(Width, Height, RadianceWorkLayerCount, Slot))
        {
            return false;
        }
    }
    if (!Exchange.AllocateDisplayRgba8Target(Width, Height, DisplayRgba8Target_))
    {
        return false;
    }

    TargetWidth_            = Width;
    TargetHeight_           = Height;
    DisplayInShaderReadLayout_ = false;
    HistoryValid_           = false;
    return true;
}

bool SpatiotemporalIllumination::Initialize(VulkanComputeExchange& Exchange,
                                           const std::string&     ShaderBinaryDirectory,
                                           std::string*           OutDiagnostic)
{
    Shutdown(Exchange);

    const std::array<VkDescriptorType, 22> Bindings = {
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        // 0  TlasCells
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        // 1  Instances
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        // 2  WideCells
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        // 3  WideTriangles
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        // 4  MaterialTable
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        // 5  DistanceVolume
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        // 6  Luminaires
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        // 7  EmissiveFacets
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        // 8  CardDescriptors
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        // 9  CardExitance
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 10 AlbedoArray
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 11 NormalArray
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 12 OrmArray
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 13 AtmosphereSkyLut
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 14 GBufferCurr
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 15 GBufferPrev
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 16 ReservoirRead
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 17 ReservoirWrite
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 18 RadianceRead
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 19 RadianceWrite
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 20 DisplayRgba8
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 21 FrameUniformBuffer
    };

    struct KernelSpecification
    {
        const char*        BinaryName;
        const char*        EntryPoint;
        ComputeProgramSlot* Destination;
    };

    const std::array<KernelSpecification, 6> Kernels = {{
        {"TracePrimarySurface.spv", "TracePrimarySurfaceMain", &PrimaryProgram_},
        {"SampleDirectReservoirs.spv", "SampleDirectReservoirsMain", &DirectRisProgram_},
        {"SampleIndirectReservoirs.spv", "SampleIndirectReservoirsMain", &IndirectGiProgram_},
        {"ReuseSpatialReservoirs.spv", "ReuseSpatialReservoirsMain", &SpatialReuseProgram_},
        {"FilterSpatiotemporalAtrous.spv", "FilterSpatiotemporalAtrousMain", &AtrousProgram_},
        {"ResolveSpatiotemporalDisplay.spv", "ResolveSpatiotemporalDisplayMain", &ResolveDisplayProgram_},
    }};

    TotalPipelineCompileMs_ = 0.0;
    for (const KernelSpecification& Spec : Kernels)
    {
        if (!Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/" + Spec.BinaryName,
                                         Spec.EntryPoint,
                                         Bindings,
                                         DispatchPushByteCount,
                                         *Spec.Destination,
                                         OutDiagnostic))
        {
            Shutdown(Exchange);
            return false;
        }
        TotalPipelineCompileMs_ += Spec.Destination->CompileMilliseconds;
    }

    // 1x1 neutral sky LUT stand-in so the atmosphere sampler binding is always valid.
    if (!Exchange.AllocateSrgb8Array(1u, 1u, 1u, FallbackSkyLut_))
    {
        Shutdown(Exchange);
        return false;
    }
    const std::array<uint8_t, 4> NeutralSky = {118u, 142u, 186u, 255u};
    if (!Exchange.UploadSrgb8Array(FallbackSkyLut_, NeutralSky))
    {
        Shutdown(Exchange);
        return false;
    }

    if (!RadiositySolver_.Initialize(Exchange, ShaderBinaryDirectory, OutDiagnostic))
    {
        Shutdown(Exchange);
        return false;
    }
    TotalPipelineCompileMs_ += RadiositySolver_.TotalCompileMilliseconds();

    // Seed every span with a valid allocation so descriptor sets can always be written.
    const std::array<float, 16> SeedFloats{};
    const bool                Seeded =
        Exchange.UploadMemorySpan(TlasSpan_, SeedFloats.data(), sizeof(SeedFloats)) &&
        Exchange.UploadMemorySpan(InstanceSpan_, SeedFloats.data(), sizeof(SeedFloats)) &&
        Exchange.UploadMemorySpan(WideCellSpan_, SeedFloats.data(), sizeof(SeedFloats)) &&
        Exchange.UploadMemorySpan(WideTriangleSpan_, SeedFloats.data(), sizeof(SeedFloats)) &&
        Exchange.UploadMemorySpan(MaterialSpan_, SeedFloats.data(), sizeof(SeedFloats)) &&
        Exchange.UploadMemorySpan(DistanceVolumeSpan_, SeedFloats.data(), sizeof(SeedFloats)) &&
        Exchange.UploadMemorySpan(LuminaireSpan_, SeedFloats.data(), sizeof(SeedFloats)) &&
        Exchange.UploadMemorySpan(EmissiveFacetSpan_, SeedFloats.data(), sizeof(SeedFloats)) &&
        Exchange.UploadMemorySpan(FrameUniformSpan_, SeedFloats.data(), sizeof(SeedFloats));
    if (!Seeded)
    {
        Shutdown(Exchange);
        return false;
    }

    return true;
}

void SpatiotemporalIllumination::Shutdown(VulkanComputeExchange& Exchange) noexcept
{
    RadiositySolver_.Shutdown(Exchange);
    Exchange.ReleaseProgram(ResolveDisplayProgram_);
    Exchange.ReleaseProgram(AtrousProgram_);
    Exchange.ReleaseProgram(SpatialReuseProgram_);
    Exchange.ReleaseProgram(IndirectGiProgram_);
    Exchange.ReleaseProgram(DirectRisProgram_);
    Exchange.ReleaseProgram(PrimaryProgram_);

    Exchange.ReleaseMemorySpan(TlasSpan_);
    Exchange.ReleaseMemorySpan(InstanceSpan_);
    Exchange.ReleaseMemorySpan(WideCellSpan_);
    Exchange.ReleaseMemorySpan(WideTriangleSpan_);
    Exchange.ReleaseMemorySpan(MaterialSpan_);
    Exchange.ReleaseMemorySpan(DistanceVolumeSpan_);
    Exchange.ReleaseMemorySpan(LuminaireSpan_);
    Exchange.ReleaseMemorySpan(EmissiveFacetSpan_);
    Exchange.ReleaseMemorySpan(FrameUniformSpan_);

    Exchange.ReleaseImage(MaterialTextures_.AlbedoSrgbArray);
    Exchange.ReleaseImage(MaterialTextures_.NormalUnormArray);
    Exchange.ReleaseImage(MaterialTextures_.OrmUnormArray);
    Exchange.ReleaseImage(FallbackSkyLut_);
    for (auto& Slot : GBufferPingPong_)
    {
        Exchange.ReleaseImage(Slot);
    }
    for (auto& Slot : ReservoirPingPong_)
    {
        Exchange.ReleaseImage(Slot);
    }
    for (auto& Slot : RadiancePingPong_)
    {
        Exchange.ReleaseImage(Slot);
    }
    Exchange.ReleaseImage(DisplayRgba8Target_);

    ExternalAtmosphereSkyLut_ = nullptr;
    ActiveSurfaceCards_.clear();
    CachedRadiositySunDir_ = {0.0f, 0.0f, 0.0f};
    CachedRadiositySunLux_ = -1.0f;
    TargetWidth_           = 0u;
    TargetHeight_          = 0u;
    FinalRadianceIndex_    = 0u;
    DisplayInShaderReadLayout_ = false;
    UploadedBvhCells_       = 0u;
    UploadedBvhTris_        = 0u;
    UploadedWideCells_      = 0u;
    UploadedInstances_      = 0u;
    UploadedTlasCells_      = 0u;
    UploadedEmissiveFacets_ = 0u;
    HasSdfVolume_           = false;
    FourCascadeActive_      = false;
    HistoryValid_           = false;
    FrameCounter_           = 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SCENE & MATERIAL UPLOAD
//------------------------------------------------------------------------------------------------------------------------

bool SpatiotemporalIllumination::UploadInstancedSceneAndMaterials(VulkanComputeExchange&           Exchange,
                                                                  const InstancedBoundingScene&    InstancedScene,
                                                                  const SurfaceScatteringAtlas&    MaterialAtlas,
                                                                  const FourCascadeDistanceVolume* FourCascadeVolume,
                                                                  std::span<const SurfaceCardPatch> CustomSurfaceCards,
                                                                  std::span<const EmissiveFacetRecord> CustomEmissiveFacets)
{
    if (!Exchange.Ready() || InstancedScene.Empty())
    {
        return false;
    }

    std::vector<std::array<float, 4>> TlasTable;
    std::vector<std::array<float, 4>> InstanceTable;
    std::vector<std::array<float, 4>> WideCellTable;
    std::vector<std::array<float, 4>> WideTriangleTable;
    InstancedScene.PackSceneForGpu(TlasTable, InstanceTable, WideCellTable, WideTriangleTable);

    std::vector<std::array<float, 4>> MaterialTable;
    MaterialAtlas.PackForGpu(MaterialTable);

    std::vector<std::array<float, 4>> SdfTable;
    FourCascadeActive_ = false;
    if (FourCascadeVolume != nullptr && !FourCascadeVolume->Cascades[0].Distances.empty())
    {
        PackFourCascadeDistanceForGpu(*FourCascadeVolume, SdfTable);
        FourCascadeActive_ = true;
    }
    else
    {
        VolumeDistanceField Empty{};
        PackVolumeDistanceForGpu(Empty, SdfTable);
    }
    HasSdfVolume_ = !SdfTable.empty() && SdfTable.size() > 3u && SdfTable[3][3] >= 0.5f;

    // Emissive facet CDF table (either caller-supplied or pre-resolved inside the instanced scene)
    std::vector<std::array<float, 4>> EmissiveTable;
    if (CustomEmissiveFacets.empty())
    {
        const std::span<const EmissiveFacetRecord> Records = InstancedScene.QueryEmissiveFacetRecords();
        EmissiveTable.reserve(std::max<size_t>(4u, Records.size() * 4u));
        for (const EmissiveFacetRecord& E : Records)
        {
            EmissiveTable.push_back({E.WorldVertex0[0], E.WorldVertex0[1], E.WorldVertex0[2], E.AreaM2});
            EmissiveTable.push_back({E.WorldEdge1[0], E.WorldEdge1[1], E.WorldEdge1[2], 0.0f});
            EmissiveTable.push_back({E.WorldEdge2[0], E.WorldEdge2[1], E.WorldEdge2[2], E.CdfUpper});
            EmissiveTable.push_back({E.RadianceRgb[0], E.RadianceRgb[1], E.RadianceRgb[2], E.SelectionProb});
        }
        UploadedEmissiveFacets_ = static_cast<uint32_t>(Records.size());
    }
    else
    {
        EmissiveTable.reserve(CustomEmissiveFacets.size() * 4u);
        for (const EmissiveFacetRecord& E : CustomEmissiveFacets)
        {
            EmissiveTable.push_back({E.WorldVertex0[0], E.WorldVertex0[1], E.WorldVertex0[2], E.AreaM2});
            EmissiveTable.push_back({E.WorldEdge1[0], E.WorldEdge1[1], E.WorldEdge1[2], 0.0f});
            EmissiveTable.push_back({E.WorldEdge2[0], E.WorldEdge2[1], E.WorldEdge2[2], E.CdfUpper});
            EmissiveTable.push_back({E.RadianceRgb[0], E.RadianceRgb[1], E.RadianceRgb[2], E.SelectionProb});
        }
        UploadedEmissiveFacets_ = static_cast<uint32_t>(CustomEmissiveFacets.size());
    }
    if (EmissiveTable.empty())
    {
        EmissiveTable.resize(4u, {0.0f, 0.0f, 0.0f, 0.0f});
    }

    if (!Exchange.UploadMemorySpan(TlasSpan_, TlasTable.data(), TlasTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(InstanceSpan_, InstanceTable.data(), InstanceTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(WideCellSpan_, WideCellTable.data(), WideCellTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(WideTriangleSpan_, WideTriangleTable.data(), WideTriangleTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(MaterialSpan_, MaterialTable.data(), MaterialTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(DistanceVolumeSpan_, SdfTable.data(), SdfTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(EmissiveFacetSpan_, EmissiveTable.data(), EmissiveTable.size() * sizeof(float) * 4u))
    {
        return false;
    }

    // 2D-Array PBR textures (neutral Layer 0 + authored TextureStudio maps)
    if (!MaterialAtlas.UploadTextureArraysToGpu(Exchange,
                                               MaterialTextures_.AlbedoSrgbArray,
                                               MaterialTextures_.NormalUnormArray,
                                               MaterialTextures_.OrmUnormArray))
    {
        return false;
    }

    // Surface cards: caller-supplied, otherwise default six-axis vehicle cards + ground radiosity grid.
    ActiveSurfaceCards_.assign(CustomSurfaceCards.begin(), CustomSurfaceCards.end());
    if (ActiveSurfaceCards_.empty())
    {
        const std::array<float, 3> SceneMin = {-2.4f, 0.0f, -2.4f};
        const std::array<float, 3> SceneMax = {2.4f, 1.6f, 2.4f};
        SurfaceCardRadiositySolver::AppendSixAxisObjectCards(
            SceneMin, SceneMax, {0.34f, 0.35f, 0.36f}, {0.0f, 0.0f, 0.0f}, 6u, ActiveSurfaceCards_);
        SurfaceCardRadiositySolver::AppendGroundRadiosityGrid(
            {-4.5f, -4.5f}, {4.5f, 4.5f}, 0.0f, 3u, {0.30f, 0.31f, 0.32f}, ActiveSurfaceCards_);
    }
    if (!RadiositySolver_.UploadPatches(Exchange, ActiveSurfaceCards_))
    {
        return false;
    }

    UploadedBvhCells_  = InstancedScene.TlasCellCount();
    UploadedTlasCells_ = InstancedScene.TlasCellCount();
    UploadedInstances_ = InstancedScene.InstanceCount();
    UploadedWideCells_ = InstancedScene.TotalWideCellCount();
    UploadedBvhTris_   = InstancedScene.TotalTriangleCount();
    HistoryValid_      = false;
    CachedRadiositySunLux_ = -1.0f;
    return true;
}

bool SpatiotemporalIllumination::UploadSceneGeometryAndMaterials(VulkanComputeExchange&        Exchange,
                                                                const BoundingVolumeAtlas&    BvhAtlas,
                                                                const SurfaceScatteringAtlas& MaterialAtlas,
                                                                const VolumeDistanceField*    DistanceVolume)
{
    InstancedBoundingScene Single{};
    if (!Single.BuildSinglePrototypeScene(BvhAtlas))
    {
        return false;
    }

    FourCascadeDistanceVolume Cascades{};
    const FourCascadeDistanceVolume* CascadePtr = nullptr;
    if (DistanceVolume != nullptr && !DistanceVolume->Distances.empty())
    {
        // Reuse the pre-baked single grid as cascade 0 and synthesize wider, coarser wrappers around it.
        const std::array<float, 3> Center = {
            0.5f * (DistanceVolume->Minimum[0] + DistanceVolume->Maximum[0]),
            0.5f * (DistanceVolume->Minimum[1] + DistanceVolume->Maximum[1]),
            0.5f * (DistanceVolume->Minimum[2] + DistanceVolume->Maximum[2]),
        };
        Cascades.Parameters.FocusCenterMeters = Center;
        for (uint32_t Index = 0u; Index < 4u; ++Index)
        {
            VolumeDistanceField Wrapper{};
            const float          Scale = 1.0f + 0.85f * static_cast<float>(Index);
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Wrapper.Minimum[Axis]    = Center[Axis] + (DistanceVolume->Minimum[Axis] - Center[Axis]) * Scale;
                Wrapper.Maximum[Axis]    = Center[Axis] + (DistanceVolume->Maximum[Axis] - Center[Axis]) * Scale;
                Wrapper.Step[Axis]       = DistanceVolume->Step[Axis] * Scale;
                Wrapper.Dimensions[Axis] = DistanceVolume->Dimensions[Axis];
            }
            Wrapper.Signed         = DistanceVolume->Signed;
            Wrapper.ClosedManifold = DistanceVolume->ClosedManifold;
            Wrapper.MinimumDistance = DistanceVolume->MinimumDistance;
            Wrapper.MaximumDistance = DistanceVolume->MaximumDistance;
            Wrapper.Distances       = DistanceVolume->Distances;
            Wrapper.Confidence      = DistanceVolume->Confidence;
            Cascades.Cascades[Index] = std::move(Wrapper);
        }
        Cascades.Parameters.TrilinearCascadeBlend = true;
        Cascades.Parameters.AnalyticalPrimitives  = false;
        Cascades.Parameters.IncludeGroundHalfSpace = false;
        CascadePtr = &Cascades;
    }

    return UploadInstancedSceneAndMaterials(Exchange, Single, MaterialAtlas, CascadePtr);
}

bool SpatiotemporalIllumination::RefreshDynamicInstances(VulkanComputeExchange& Exchange, const InstancedBoundingScene& InstancedScene)
{
    if (!Exchange.Ready() || InstancedScene.Empty())
    {
        return false;
    }
    std::vector<std::array<float, 4>> TlasTable;
    std::vector<std::array<float, 4>> InstanceTable;
    std::vector<std::array<float, 4>> WideCellTable;
    std::vector<std::array<float, 4>> WideTriangleTable;
    InstancedScene.PackSceneForGpu(TlasTable, InstanceTable, WideCellTable, WideTriangleTable);

    UploadedTlasCells_ = InstancedScene.TlasCellCount();
    UploadedInstances_ = InstancedScene.InstanceCount();
    return Exchange.UploadMemorySpan(TlasSpan_, TlasTable.data(), TlasTable.size() * sizeof(float) * 4u) &&
           Exchange.UploadMemorySpan(InstanceSpan_, InstanceTable.data(), InstanceTable.size() * sizeof(float) * 4u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 FRAME EXECUTION
//------------------------------------------------------------------------------------------------------------------------

bool SpatiotemporalIllumination::RenderFrame(VulkanComputeExchange&                  Exchange,
                                             const SpatiotemporalCamera&             Camera,
                                             const std::vector<AnalyticalLuminaire>& Luminaires,
                                             const SpatiotemporalRenderConfig&       Config,
                                             SpatiotemporalFrameTelemetry*           OutTelemetry,
                                             bool                                    TransitionDisplayForGui)
{
    const uint32_t Width  = std::max(1u, Camera.WidthPx);
    const uint32_t Height = std::max(1u, Camera.HeightPx);
    if (!EnsureViewportTargets(Exchange, Width, Height))
    {
        return false;
    }

    if (OutTelemetry != nullptr)
    {
        *OutTelemetry = SpatiotemporalFrameTelemetry{};
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 1. Camera frame & previous-frame basis
    //--------------------------------------------------------------------------------------------------------------------
    const std::array<float, 3> CamFwd = NormalizeVec(SubVec(Camera.TargetMeters, Camera.OriginMeters));
    const std::array<float, 3> RefUp =
        (std::abs(DotVec(CamFwd, Camera.UpUnit)) > 0.98f) ? std::array<float, 3>{0.0f, 0.0f, 1.0f} : Camera.UpUnit;
    const std::array<float, 3> CamRight = NormalizeVec(CrossVec(CamFwd, RefUp));
    const std::array<float, 3> CamUp    = NormalizeVec(CrossVec(CamRight, CamFwd));
    const float                Aspect   = static_cast<float>(Width) / static_cast<float>(Height);

    //--------------------------------------------------------------------------------------------------------------------
    // 2. Luminaire table & solar identification
    //--------------------------------------------------------------------------------------------------------------------
    const uint32_t LuminaireCount = static_cast<uint32_t>(std::min<size_t>(Luminaires.size(), MaximumAnalyticalLuminaires));
    std::vector<std::array<float, 4>> LuminaireTable;
    LuminaireTable.reserve(std::max<size_t>(3u, LuminaireCount * 3u));

    std::array<float, 3> SunDirection = NormalizeVec({0.42f, 0.76f, 0.50f});
    std::array<float, 3> SunTint      = {1.0f, 1.0f, 1.0f};
    float                SunIrradiance = 0.0f;
    bool                 SunFound      = false;

    for (uint32_t Index = 0u; Index < LuminaireCount; ++Index)
    {
        const AnalyticalLuminaire& L = Luminaires[Index];
        if (L.Kind == AnalyticalLuminaireKind::DirectionalSolar && !SunFound)
        {
            SunDirection = NormalizeVec(L.DirectionOrPosition);
            const float Peak = std::max({L.EmittedRadianceRgb[0], L.EmittedRadianceRgb[1], L.EmittedRadianceRgb[2]});
            if (Peak > 1.0e-4f)
            {
                SunTint       = {L.EmittedRadianceRgb[0] / Peak, L.EmittedRadianceRgb[1] / Peak, L.EmittedRadianceRgb[2] / Peak};
                SunIrradiance = Peak;
            }
            SunFound = true;
        }

        // GPU layout: Pos|Dir + Radius, Radiance + Category, Axis + CosOuter
        std::array<float, 4> Row0 = {L.DirectionOrPosition[0], L.DirectionOrPosition[1], L.DirectionOrPosition[2], 0.0f};
        std::array<float, 4> Row1 = {L.EmittedRadianceRgb[0], L.EmittedRadianceRgb[1], L.EmittedRadianceRgb[2], 1.0f};
        std::array<float, 4> Row2 = {L.AxisV[0], L.AxisV[1], L.AxisV[2], -1.0f};

        if (L.Kind == AnalyticalLuminaireKind::DirectionalSolar)
        {
            Row1[3] = 0.0f;
            Row0[3] = L.AxisU[0];
        }
        else if (L.Kind == AnalyticalLuminaireKind::RectangularArea)
        {
            Row0[3] = 0.5f * (L.AxisU[0] + L.AxisU[1] + L.AxisU[2]);
            Row2    = {L.AxisV[0], L.AxisV[1], L.AxisV[2], 1.0f};
        }
        else
        {
            Row0[3] = std::max(L.AxisU[0], 0.05f);
        }
        LuminaireTable.push_back(Row0);
        LuminaireTable.push_back(Row1);
        LuminaireTable.push_back(Row2);
    }
    if (LuminaireTable.empty())
    {
        LuminaireTable.resize(3u, {0.0f, 0.0f, 0.0f, 0.0f});
    }
    if (!Exchange.UploadMemorySpan(LuminaireSpan_, LuminaireTable.data(), LuminaireTable.size() * sizeof(float) * 4u))
    {
        return false;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 3. GPU SurfaceCard multi-bounce radiosity (re-solved only when the solar stimulus changes)
    //--------------------------------------------------------------------------------------------------------------------
    const bool SunChanged = std::abs(LuminanceOf(SunTint) * static_cast<double>(SunIrradiance) -
                                     static_cast<double>(CachedRadiositySunLux_)) > 1.0e-4 ||
                            DotVec(SunDirection, CachedRadiositySunDir_) < 0.9995f;

    SurfaceCardLightingParameters RadiosityParameters{};
    RadiosityParameters.IndirectBounceGain    = 1.15f;
    RadiosityParameters.SkyVisibilityWeight   = 0.85f;
    RadiosityParameters.BounceIterations      = 3u;
    RadiosityParameters.EnableVisibilityCones = HasSdfVolume_;
    RadiosityParameters.SunDirectionUnit      = SunDirection;
    RadiosityParameters.SunIlluminanceLux     = SunIrradiance * 4.0f;
    RadiosityParameters.SunRadianceTint       = SunTint;
    RadiosityParameters.SkyAmbientLuminance   = 0.35f;
    RadiosityParameters.SkyZenithRadiance     = Config.ZenithSkyRgb;
    RadiosityParameters.SkyHorizonRadiance    = Config.HorizonSkyRgb;

    SurfaceCardSolveReport RadiosityReport{};
    if (SunChanged || !HistoryValid_)
    {
        if (!RadiositySolver_.SolveGpuRadiosity(Exchange, DistanceVolumeSpan_, LuminaireSpan_, LuminaireCount,
                                                RadiosityParameters, &RadiosityReport))
        {
            return false;
        }
        CachedRadiositySunDir_ = SunDirection;
        CachedRadiositySunLux_ = static_cast<float>(LuminanceOf(SunTint) * static_cast<double>(SunIrradiance));
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 4. Frame uniform packing (14 float4 slots; inspection above the fixed push-constant budget)
    //--------------------------------------------------------------------------------------------------------------------
    const float SkyAmbientGain = [&]() -> float {
        const double HorizonLum = LuminanceOf(Config.HorizonSkyRgb);
        const double ZenithLum  = LuminanceOf(Config.ZenithSkyRgb);
        const double AverageLum = std::max(0.5 * (HorizonLum + ZenithLum), 1.0e-3);
        return static_cast<float>(AverageLum / 0.55);
    }();

    uint32_t FeatureFlags = 0u;
    FeatureFlags |= Config.EnableIndirectGi ? 1u : 0u;
    FeatureFlags |= Config.EnableSpatialReuse ? 2u : 0u;
    FeatureFlags |= Config.EnableReflections ? 4u : 0u;
    FeatureFlags |= (Config.EnableAtmosphereLut && ExternalAtmosphereSkyLut_ != nullptr) ? 8u : 0u;
    FeatureFlags |= Config.EnableClouds ? 16u : 0u;
    FeatureFlags |= Config.EnableFog ? 32u : 0u;
    FeatureFlags |= Config.EnableLensFlare ? 64u : 0u;
    FeatureFlags |= Config.EnableSecondMirrorBounce ? 128u : 0u;

    const uint32_t GBufferIndex  = FrameCounter_ & 1u;
    const uint32_t GBufferPrev   = 1u - GBufferIndex;

    std::vector<std::array<float, 4>> Uniforms(FrameUniformFloat4Count, {0.0f, 0.0f, 0.0f, 0.0f});
    Uniforms[0]  = {Camera.OriginMeters[0], Camera.OriginMeters[1], Camera.OriginMeters[2], std::tan(0.5f * Camera.VerticalFovRad)};
    Uniforms[1]  = {CamRight[0], CamRight[1], CamRight[2], Aspect};
    Uniforms[2]  = {CamUp[0], CamUp[1], CamUp[2], static_cast<float>(UploadedTlasCells_)};
    Uniforms[3]  = {CamFwd[0], CamFwd[1], CamFwd[2], static_cast<float>(LuminaireCount)};
    Uniforms[4]  = {PrevCamOrigin_[0], PrevCamOrigin_[1], PrevCamOrigin_[2], static_cast<float>(UploadedEmissiveFacets_)};
    Uniforms[5]  = {PrevCamRight_[0], PrevCamRight_[1], PrevCamRight_[2], static_cast<float>(RadiositySolver_.CardCount())};
    Uniforms[6]  = {PrevCamUp_[0], PrevCamUp_[1], PrevCamUp_[2], static_cast<float>(std::clamp(Config.RisCandidateCount, 1u, 16u))};
    Uniforms[7]  = {PrevCamForward_[0], PrevCamForward_[1], PrevCamForward_[2], static_cast<float>(std::clamp(Config.SpatialNeighbors, 0u, 8u))};
    Uniforms[8]  = {SunDirection[0], SunDirection[1], SunDirection[2], SunIrradiance};
    Uniforms[9]  = {SunTint[0], SunTint[1], SunTint[2], SkyAmbientGain};
    Uniforms[10] = {Config.MoonDirection[0], Config.MoonDirection[1], Config.MoonDirection[2], Config.MoonCoronaStrength};
    Uniforms[11] = {std::max(Config.NearMaterialMeters, 0.5f), std::max(Config.FarMaterialMeters, Config.NearMaterialMeters + 1.0f),
                    std::clamp(Config.MaterialQualityScale, 0.25f, 3.0f),
                    Config.EnableTemporalReuse ? std::clamp(Config.TemporalHistoryCap, 0.0f, 32.0f) : 0.0f};
    Uniforms[12] = {Config.CloudCoverage, Config.CloudAltitudeMeters, Config.FogBaseDensity, Config.FogDustBoost};
    Uniforms[13] = {Config.ExposureScale, Config.FlareIntensity, static_cast<float>(std::min(Config.InspectionMode, 14u)),
                    static_cast<float>(FeatureFlags)};

    if (!Exchange.UploadMemorySpan(FrameUniformSpan_, Uniforms.data(), Uniforms.size() * sizeof(float) * 4u))
    {
        return false;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 5. Descriptor argument sets (two reservoir parities + ping-pong radiance)
    //--------------------------------------------------------------------------------------------------------------------
    const DeviceImageArraySlot& SkySource =
        (ExternalAtmosphereSkyLut_ != nullptr &&
         (*ExternalAtmosphereSkyLut_).ArrayView != VK_NULL_HANDLE &&
         ExternalAtmosphereSkyLut_->Width > 1u ? *ExternalAtmosphereSkyLut_ : FallbackSkyLut_);

    const uint32_t GroupsX = (Width + 7u) / 8u;
    const uint32_t GroupsY = (Height + 7u) / 8u;

    std::array<ComputeDescriptorArgument, 22> Base = {{
        ComputeDescriptorArgument::Span(&TlasSpan_),
        ComputeDescriptorArgument::Span(&InstanceSpan_),
        ComputeDescriptorArgument::Span(&WideCellSpan_),
        ComputeDescriptorArgument::Span(&WideTriangleSpan_),
        ComputeDescriptorArgument::Span(&MaterialSpan_),
        ComputeDescriptorArgument::Span(&DistanceVolumeSpan_),
        ComputeDescriptorArgument::Span(&LuminaireSpan_),
        ComputeDescriptorArgument::Span(&EmissiveFacetSpan_),
        ComputeDescriptorArgument::Span(&RadiositySolver_.DescriptorSpan()),
        RadiositySolver_.SolvedExitanceArgument(),
        ComputeDescriptorArgument::SampledImage(&MaterialTextures_.AlbedoSrgbArray),
        ComputeDescriptorArgument::SampledImage(&MaterialTextures_.NormalUnormArray),
        ComputeDescriptorArgument::SampledImage(&MaterialTextures_.OrmUnormArray),
        ComputeDescriptorArgument::SampledImage(&SkySource),
        ComputeDescriptorArgument::StorageImage(&GBufferPingPong_[GBufferIndex]),
        ComputeDescriptorArgument::StorageImage(&GBufferPingPong_[GBufferPrev]),
        ComputeDescriptorArgument::StorageImage(&ReservoirPingPong_[0]),
        ComputeDescriptorArgument::StorageImage(&ReservoirPingPong_[1]),
        ComputeDescriptorArgument::StorageImage(&RadiancePingPong_[0]),
        ComputeDescriptorArgument::StorageImage(&RadiancePingPong_[1]),
        ComputeDescriptorArgument::StorageImage(&DisplayRgba8Target_),
        ComputeDescriptorArgument::Span(&FrameUniformSpan_),
    }};

    const bool ComputeReady = Exchange.TransitionForComputeStorage(DisplayRgba8Target_);
    (void)ComputeReady;
    DisplayInShaderReadLayout_ = false;

    DispatchPushGpu Push{Width, Height, FrameCounter_, 0u};
    double          TotalGpuMs = 0.0;
    double          TotalCpuMs = 0.0;
    DispatchTimingSample Timing{};

    auto RunKernel = [&](const ComputeProgramSlot&                 Program,
                         const std::array<ComputeDescriptorArgument, 22>& Arguments,
                         uint32_t                                  StepWidth) -> bool {
        Push.AtrousStepWidth = StepWidth;
        if (!Exchange.DispatchCompute(Program, Arguments, &Push, sizeof(Push), GroupsX, GroupsY, 1u, &Timing))
        {
            return false;
        }
        TotalGpuMs += Timing.GpuMilliseconds;
        TotalCpuMs += Timing.CpuMilliseconds;
        return true;
    };

    //--------------------------------------------------------------------------------------------------------------------
    // 6. Kernel 1 (primary) + kernels 2/3 (temporal DI & GI) reading the previous post-spatial reservoirs
    //--------------------------------------------------------------------------------------------------------------------
    if (!RunKernel(PrimaryProgram_, Base, 0u) || !RunKernel(DirectRisProgram_, Base, 0u) ||
        !RunKernel(IndirectGiProgram_, Base, 0u))
    {
        return false;
    }

    // Kernel 4 (spatial + VNDF + demodulation) reads this frame's temporal reservoirs and writes the other parity,
    // which then becomes next frame's immutable temporal history.
    // Radiance work starts in parity 0 so the à-trous chain and resolve consume it consistently.
    std::array<ComputeDescriptorArgument, 22> SpatialArgs = Base;
    SpatialArgs[16] = ComputeDescriptorArgument::StorageImage(&ReservoirPingPong_[1]);
    SpatialArgs[17] = ComputeDescriptorArgument::StorageImage(&ReservoirPingPong_[0]);
    SpatialArgs[19] = ComputeDescriptorArgument::StorageImage(&RadiancePingPong_[0]);

    if (!RunKernel(SpatialReuseProgram_, SpatialArgs, 0u))
    {
        return false;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 7. Kernel 5 à-trous iterations (radiance ping-pong)
    //--------------------------------------------------------------------------------------------------------------------
    uint32_t CurrentRadiance = 0u;
    const uint32_t Iterations = std::clamp(Config.AtrousIterations, 0u, 4u);
    for (uint32_t Iteration = 0u; Iteration < Iterations; ++Iteration)
    {
        std::array<ComputeDescriptorArgument, 22> AtrousArgs = Base;
        AtrousArgs[18] = ComputeDescriptorArgument::StorageImage(&RadiancePingPong_[CurrentRadiance]);
        AtrousArgs[19] = ComputeDescriptorArgument::StorageImage(&RadiancePingPong_[1u - CurrentRadiance]);
        if (!RunKernel(AtrousProgram_, AtrousArgs, 1u << Iteration))
        {
            return false;
        }
        CurrentRadiance = 1u - CurrentRadiance;
    }
    FinalRadianceIndex_ = CurrentRadiance;

    //--------------------------------------------------------------------------------------------------------------------
    // 8. Kernel 6 resolve (reads final radiance, writes linear HDR + dithered RGBA8 display)
    //--------------------------------------------------------------------------------------------------------------------
    std::array<ComputeDescriptorArgument, 22> ResolveArgs = Base;
    ResolveArgs[18] = ComputeDescriptorArgument::StorageImage(&RadiancePingPong_[FinalRadianceIndex_]);
    ResolveArgs[19] = ComputeDescriptorArgument::StorageImage(&RadiancePingPong_[1u - FinalRadianceIndex_]);
    if (!RunKernel(ResolveDisplayProgram_, ResolveArgs, 0u))
    {
        return false;
    }
    FinalRadianceIndex_ = 1u - FinalRadianceIndex_;

    if (TransitionDisplayForGui)
    {
        DisplayInShaderReadLayout_ = Exchange.TransitionForPresentationSampling(DisplayRgba8Target_);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 9. Frame bookkeeping
    //--------------------------------------------------------------------------------------------------------------------
    PrevCamOrigin_  = Camera.OriginMeters;
    PrevCamRight_   = CamRight;
    PrevCamUp_      = CamUp;
    PrevCamForward_ = CamFwd;
    HistoryValid_   = true;
    ++FrameCounter_;

    if (OutTelemetry != nullptr)
    {
        OutTelemetry->BvhCellCount       = UploadedTlasCells_;
        OutTelemetry->BvhTriangleCount   = UploadedBvhTris_;
        OutTelemetry->WideCellCount      = UploadedWideCells_;
        OutTelemetry->InstanceCount      = UploadedInstances_;
        OutTelemetry->LuminaireCount     = LuminaireCount;
        OutTelemetry->EmissiveFacetCount = UploadedEmissiveFacets_;
        OutTelemetry->SurfaceCardCount   = RadiositySolver_.CardCount();
        OutTelemetry->SdfVolumeActive    = HasSdfVolume_;
        OutTelemetry->FourCascadeActive  = FourCascadeActive_;
        OutTelemetry->FrameIndex         = FrameCounter_;
        OutTelemetry->RadiosityGpuMs     = RadiosityReport.GpuMilliseconds;
        OutTelemetry->TotalGpuMs         = TotalGpuMs + RadiosityReport.GpuMilliseconds;
        OutTelemetry->TotalCpuMs         = TotalCpuMs + RadiosityReport.CpuMilliseconds;
        OutTelemetry->PipelineCompileMs  = TotalPipelineCompileMs_;
        OutTelemetry->AllocatedVramBytes = Exchange.QueryAllocatedDeviceBytes();
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  FRAME READBACK
//------------------------------------------------------------------------------------------------------------------------

bool SpatiotemporalIllumination::ReadbackLinearRgba(VulkanComputeExchange& Exchange, std::vector<float>& OutLinearRgbaFloats)
{
    if (TargetWidth_ == 0u || TargetHeight_ == 0u)
    {
        return false;
    }
    // Layer 0 of the resolved radiance target holds the pre-tonemap linear HDR frame.
    return Exchange.DownloadRgba16fLayer(RadiancePingPong_[FinalRadianceIndex_], 0u, OutLinearRgbaFloats);
}

bool SpatiotemporalIllumination::ReadbackTonemappedSrgb8(VulkanComputeExchange& Exchange,
                                                         float                  ExposureScale,
                                                         std::vector<uint8_t>&  OutSrgbRgbaBytes)
{
    std::vector<float> Linear;
    if (!ReadbackLinearRgba(Exchange, Linear) || Linear.size() < static_cast<size_t>(TargetWidth_) * TargetHeight_ * 4u)
    {
        return false;
    }

    OutSrgbRgbaBytes.resize(static_cast<size_t>(TargetWidth_) * TargetHeight_ * 4u);
    const float ClampedExposure = std::max(ExposureScale, 0.02f);
    for (size_t Pixel = 0u; Pixel < static_cast<size_t>(TargetWidth_) * TargetHeight_; ++Pixel)
    {
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            const float LinearValue = std::max(Linear[Pixel * 4u + static_cast<size_t>(Channel)], 0.0f) * ClampedExposure;
            const float Numerator   = LinearValue * (2.51f * LinearValue + 0.03f);
            const float Denominator = LinearValue * (2.43f * LinearValue + 0.59f) + 0.14f;
            const float Tonemapped  = std::clamp(Numerator / std::max(Denominator, 1.0e-6f), 0.0f, 1.0f);
            const float Encoded     = (Tonemapped <= 0.0031308f) ? (Tonemapped * 12.92f)
                                                                 : (1.055f * std::pow(Tonemapped, 1.0f / 2.4f) - 0.055f);
            OutSrgbRgbaBytes[Pixel * 4u + static_cast<size_t>(Channel)] =
                static_cast<uint8_t>(std::clamp(std::lround(Encoded * 255.0f), 0l, 255l));
        }
        OutSrgbRgbaBytes[Pixel * 4u + 3u] = 255u;
    }
    return true;
}

} // namespace Frontier
