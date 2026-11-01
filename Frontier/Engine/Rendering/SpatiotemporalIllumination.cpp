/*==========================================================================================================================================
                                          SPATIOTEMPORAL ILLUMINATION DEFINITION
==========================================================================================================================================*/
// 🧩 Dual-Option Vulkan 1.3 Hybrid Raster / Hardware-3D-SDF GI / Raytraced Reflections + Selectable Full Raytracing Orchestrator:
//    - Mode 0 (Default Hybrid): Hardware SurfaceRaster + ForwardRaster (ddx/ddy mip & flake AA), 3-cascade texel-snapped ShadowRaster +
//      Karis area softboxes (zero DI noise), Half-Res (1/4 px) 1-ray Hardware Sampler3D SDF GI + SurfaceCards, Raytraced CWBVH Reflections,
//      and SVGF VarianceTemporal + VarianceWavelet + Joint-Bilateral Upsample.
//    - Mode 1 (Full Raytracing): Hardware SurfaceRaster + ForwardRaster primary G-buffer + Full CWBVH raytraced DI, GI, & Reflections.

#include "Engine/Rendering/SpatiotemporalIllumination.h"

#include "Engine/PlanetaryAtmosphere/PlanetaryEnsemble.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <utility>

namespace Frontier
{
namespace
{

constexpr uint32_t kLuminaireHeaderOffset = 34u;

struct SpatiotemporalPushConstants
{
    uint32_t Counts[4]; // {Width, Height, FrameOrParity, ModeFlag}
    float    Params[4]; // {Exposure, AtrousStepSize, ColourSigma, HistoryFlag}
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
        Origin[0], Origin[1], Origin[2], 1.0f,
    };
}

struct SolarCascadeHeaderPack
{
    bool                                                  Active = false;
    std::array<std::array<float, 4>, 12>                  ViewProjRows{};
    std::array<float, 3>                                  SplitDistances{12.0f, 36.0f, 110.0f};
    std::array<float, 3>                                  TexelWorldSizes{0.025f, 0.075f, 0.22f};
};

SolarCascadeHeaderPack BuildSolarShadowCascades(const std::array<float, 3>&             CamOrigin,
                                                const std::array<float, 3>&             CamFwd,
                                                const std::vector<AnalyticalLuminaire>& Luminaires,
                                                uint32_t                                ShadowResolution,
                                                bool                                    EnableHardwareShadows) noexcept
{
    SolarCascadeHeaderPack Out{};
    for (uint32_t C = 0u; C < 3u; ++C)
    {
        Out.ViewProjRows[C * 4u + 3u] = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    if (!EnableHardwareShadows || Luminaires.empty())
    {
        return Out;
    }

    const AnalyticalLuminaire* Solar = nullptr;
    for (const AnalyticalLuminaire& L : Luminaires)
    {
        if (L.Kind == AnalyticalLuminaireKind::DirectionalSolar)
        {
            Solar = &L;
            break;
        }
    }
    if (Solar == nullptr)
    {
        return Out;
    }

    const auto SunDir     = NormVec(Solar->DirectionOrPosition);
    const auto LightFwd   = std::array<float, 3>{-SunDir[0], -SunDir[1], -SunDir[2]};
    const auto RefUp      = (std::abs(LightFwd[1]) > 0.96f) ? std::array<float, 3>{0.0f, 0.0f, 1.0f} : std::array<float, 3>{0.0f, 1.0f, 0.0f};
    const auto LightRight = NormVec(CrossVec(LightFwd, RefUp));
    const auto LightUp    = NormVec(CrossVec(LightRight, LightFwd));

    Out.Active = true;
    for (uint32_t C = 0u; C < 3u; ++C)
    {
        const float Radius     = Out.SplitDistances[C];
        const float TexelWorld = (2.0f * Radius) / static_cast<float>(std::max(64u, ShadowResolution));
        Out.TexelWorldSizes[C] = TexelWorld;

        const float LeadDist = 0.42f * Radius;
        const std::array<float, 3> Focus{
            CamOrigin[0] + CamFwd[0] * LeadDist,
            CamOrigin[1] * 0.35f,
            CamOrigin[2] + CamFwd[2] * LeadDist,
        };

        // Snap cascade XY origin in light space to exact texel grid so shadows never swim during camera flight
        const float SnappedX      = std::floor(DotVec(LightRight, Focus) / TexelWorld) * TexelWorld;
        const float SnappedY      = std::floor(DotVec(LightUp, Focus) / TexelWorld) * TexelWorld;
        const float DepthHalfSpan = std::max(140.0f, Radius * 2.5f);
        const float NearLightZ    = DotVec(LightFwd, Focus) - DepthHalfSpan;
        const float InvRadius     = 1.0f / Radius;
        const float InvDepthRange = 1.0f / (2.0f * DepthHalfSpan);

        Out.ViewProjRows[C * 4u + 0u] = {
            LightRight[0] * InvRadius,
            LightRight[1] * InvRadius,
            LightRight[2] * InvRadius,
            -SnappedX * InvRadius,
        };
        Out.ViewProjRows[C * 4u + 1u] = {
            -LightUp[0] * InvRadius,
            -LightUp[1] * InvRadius,
            -LightUp[2] * InvRadius,
            SnappedY * InvRadius,
        };
        Out.ViewProjRows[C * 4u + 2u] = {
            LightFwd[0] * InvDepthRange,
            LightFwd[1] * InvDepthRange,
            LightFwd[2] * InvDepthRange,
            -NearLightZ * InvDepthRange,
        };
        Out.ViewProjRows[C * 4u + 3u] = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    return Out;
}

void BuildFrameHeaderVector(const SpatiotemporalCamera&             Camera,
                            uint32_t                                Width,
                            uint32_t                                Height,
                            uint32_t                                UploadedBvhCells,
                            uint32_t                                UploadedLuminousFacets,
                            uint32_t                                UploadedSurfacePatches,
                            bool                                    HasSdfVolume,
                            bool                                    TwoLevelOctetActive,
                            bool                                    RangeClipmapActive,
                            const std::array<float, 16>&            PrevRows,
                            const SolarCascadeHeaderPack&           Cascades,
                            const std::vector<AnalyticalLuminaire>& Luminaires,
                            const SpatiotemporalRenderConfig&       Config,
                            std::array<float, 16>&                  OutCurrentViewProjRows,
                            std::vector<std::array<float, 4>>&      OutHeader)
{
    const auto  CamFwd   = NormVec(SubVec(Camera.TargetMeters, Camera.OriginMeters));
    const auto  RefUp    = (std::abs(DotVec(CamFwd, Camera.UpUnit)) > 0.98f)
                               ? std::array<float, 3>{0.0f, 0.0f, 1.0f}
                               : Camera.UpUnit;
    const auto  CamRight = NormVec(CrossVec(CamFwd, RefUp));
    const auto  CamUp    = NormVec(CrossVec(CamRight, CamFwd));
    const float TanHalfV = std::tan(0.5f * Camera.VerticalFovRad);
    const float TanHalfH = TanHalfV * (static_cast<float>(Width) / static_cast<float>(Height));

    OutCurrentViewProjRows = ComputeCameraWorldToNdcRows(Camera.OriginMeters, CamRight, CamUp, CamFwd, TanHalfH, TanHalfV);

    const uint32_t LumCount     = static_cast<uint32_t>(std::min<size_t>(Luminaires.size(), 32u));
    const auto&    Env          = Config.Environment;
    // Use half-res (TraceScale=2 -> 1/4 pixels) in Hybrid mode whenever a 4-cascade clipmap is active (or >= 320x240)
    const uint32_t GiTraceScale = (!Config.FullRaytracingMode && HasSdfVolume && Width >= 160u && Height >= 120u) ? 2u : 1u;

    OutHeader.clear();
    OutHeader.reserve(kLuminaireHeaderOffset + std::max(1u, LumCount) * 4u);
    OutHeader.push_back({Camera.OriginMeters[0], Camera.OriginMeters[1], Camera.OriginMeters[2], TanHalfV});                                        // [0]
    OutHeader.push_back({CamRight[0], CamRight[1], CamRight[2], TanHalfH});                                                                         // [1]
    OutHeader.push_back({CamUp[0], CamUp[1], CamUp[2], static_cast<float>(UploadedBvhCells)});                                                      // [2]
    OutHeader.push_back({CamFwd[0], CamFwd[1], CamFwd[2], static_cast<float>(LumCount)});                                                           // [3]
    OutHeader.push_back({Config.ZenithSkyRgb[0], Config.ZenithSkyRgb[1], Config.ZenithSkyRgb[2], static_cast<float>(std::max(1u, Config.RisCandidateCount))}); // [4]
    OutHeader.push_back({Config.HorizonSkyRgb[0], Config.HorizonSkyRgb[1], Config.HorizonSkyRgb[2], static_cast<float>(Config.SpatialNeighbors)});  // [5]
    OutHeader.push_back({Config.GroundBounceTintRgb[0], Config.GroundBounceTintRgb[1], Config.GroundBounceTintRgb[2], static_cast<float>(std::max(1u, Config.SdfIndirectRays))}); // [6]
    OutHeader.push_back({Config.EnableReflections ? 1.0f : 0.0f,
                         Config.EnableSpatialReuse ? 1.0f : 0.0f,
                         HasSdfVolume ? 1.0f : 0.0f,
                         Config.EnableIndirectGi ? 1.0f : 0.0f});                                                                                   // [7]
    OutHeader.push_back({PrevRows[0], PrevRows[1], PrevRows[2], PrevRows[3]});                                                                      // [8]
    OutHeader.push_back({PrevRows[4], PrevRows[5], PrevRows[6], PrevRows[7]});                                                                      // [9]
    OutHeader.push_back({PrevRows[8], PrevRows[9], PrevRows[10], PrevRows[11]});                                                                    // [10]
    OutHeader.push_back({PrevRows[12], PrevRows[13], PrevRows[14], PrevRows[15]});                                                                  // [11]
    OutHeader.push_back({TwoLevelOctetActive ? 1.0f : 0.0f,
                         RangeClipmapActive ? 1.0f : 0.0f,
                         static_cast<float>(UploadedLuminousFacets),
                         static_cast<float>(UploadedSurfacePatches)});                                                                              // [12]
    OutHeader.push_back({static_cast<float>(Config.InspectionMode),
                         static_cast<float>(std::clamp(Config.TemporalHistoryLimit, 1u, 64u)),
                         Config.SurfaceCardGain,
                         Config.SurfaceCardSkyWeight});                                                                                             // [13]
    OutHeader.push_back({Env.CloudsEnabled ? Env.CloudCoverage : -1.0f,
                         Env.CloudDensity,
                         Env.CloudBottomMeters,
                         Env.LunarEnabled ? Env.DaytimeVeilStrength : -1.0f});                                                                      // [14]
    OutHeader.push_back({Env.PrimaryMoonDir[0], Env.PrimaryMoonDir[1], Env.PrimaryMoonDir[2], Env.PrimaryMoonRadius});                              // [15]
    OutHeader.push_back({Env.SecondaryMoonDir[0], Env.SecondaryMoonDir[1], Env.SecondaryMoonDir[2], Env.SecondaryMoonRadius});                      // [16]
    OutHeader.push_back({Env.FogInscatteringRgb[0], Env.FogInscatteringRgb[1], Env.FogInscatteringRgb[2], Env.FogEnabled ? Env.FogDensity : -1.0f});// [17]
    OutHeader.push_back({Env.FogHeightFalloff,
                         static_cast<float>(std::min(Config.RadiosityBounces, 8u)),
                         Config.FullRaytracingMode ? 1.0f : 0.0f,
                         static_cast<float>(GiTraceScale)});                                                                                        // [18]
    OutHeader.push_back({static_cast<float>(SpatiotemporalIllumination::ShadowMapResolution),
                         static_cast<float>(Cascades.Active ? SpatiotemporalIllumination::ShadowCascadeCount : 0u),
                         Config.GroundPlaneHeightMeters,
                         static_cast<float>(kLuminaireHeaderOffset)});                                                                              // [19]

    // Slots [20..31]: 3 Solar Shadow Cascade ViewProj matrices
    for (uint32_t R = 0u; R < 12u; ++R)
    {
        OutHeader.push_back(Cascades.ViewProjRows[R]);
    }
    // Slot [32]: {CascadeSplits.xyz, ActiveCascadeCount}
    OutHeader.push_back({Cascades.SplitDistances[0],
                         Cascades.SplitDistances[1],
                         Cascades.SplitDistances[2],
                         Cascades.Active ? static_cast<float>(SpatiotemporalIllumination::ShadowCascadeCount) : 0.0f});
    // Slot [33]: {CascadeTexelWorld.xyz, PrimarySunShadowEnabled}
    OutHeader.push_back({Cascades.TexelWorldSizes[0],
                         Cascades.TexelWorldSizes[1],
                         Cascades.TexelWorldSizes[2],
                         Cascades.Active ? 1.0f : 0.0f});

    // Slots [34 .. 34 + LumCount * 4]: Analytical Luminaires
    for (uint32_t Idx = 0u; Idx < LumCount; ++Idx)
    {
        const AnalyticalLuminaire& L = Luminaires[Idx];
        OutHeader.push_back({L.DirectionOrPosition[0], L.DirectionOrPosition[1], L.DirectionOrPosition[2], static_cast<float>(L.Kind)});
        OutHeader.push_back({L.AxisU[0], L.AxisU[1], L.AxisU[2], 0.0f});
        OutHeader.push_back({L.AxisV[0], L.AxisV[1], L.AxisV[2], 0.0f});
        OutHeader.push_back({L.EmittedRadianceRgb[0], L.EmittedRadianceRgb[1], L.EmittedRadianceRgb[2], 1.0f});
    }
    if (LumCount == 0u)
    {
        OutHeader.resize(kLuminaireHeaderOffset + 4u, {0.0f, 0.0f, 0.0f, 0.0f});
    }
}

void BuildPerFrameRasterItems(bool                                         TwoLevelActive,
                              std::span<const RasterInstanceDrawSpan>      TwoLevelSpans,
                              uint32_t                               SingleOpaqueTris,
                              uint32_t                               SingleGlassTris,
                              uint32_t                               SingleGlassOffset,
                              uint32_t                               Width,
                              uint32_t                               Height,
                              uint32_t                               FrameIndex,
                              std::vector<RasterDrawItem>&           OutOpaqueDraws,
                              std::vector<RasterDrawItem>&           OutGlassDraws,
                              std::array<std::vector<RasterDrawItem>, 3>& OutCascadeShadowDraws)
{
    OutOpaqueDraws.clear();
    OutGlassDraws.clear();
    for (auto& Vec : OutCascadeShadowDraws)
    {
        Vec.clear();
    }

    if (TwoLevelActive)
    {
        OutOpaqueDraws.reserve(TwoLevelSpans.size());
        for (uint32_t C = 0u; C < 3u; ++C)
        {
            OutCascadeShadowDraws[C].reserve(TwoLevelSpans.size());
        }
        for (const RasterInstanceDrawSpan& Span : TwoLevelSpans)
        {
            if (Span.OpaqueTriangleCount > 0u)
            {
                RasterDrawItem Item{};
                Item.VertexCount  = Span.OpaqueTriangleCount * 3u;
                Item.PushWords    = {Span.OpaqueTriangleOffset, Span.OrderedInstanceIndex, 0u, 1u, Width, Height, FrameIndex, 0u};
                OutOpaqueDraws.push_back(Item);

                for (uint32_t C = 0u; C < 3u; ++C)
                {
                    RasterDrawItem ShadowItem = Item;
                    ShadowItem.PushWords[2]   = C;
                    OutCascadeShadowDraws[C].push_back(ShadowItem);
                }
            }
            if (Span.GlassTriangleCount > 0u)
            {
                RasterDrawItem GItem{};
                GItem.VertexCount = Span.GlassTriangleCount * 3u;
                GItem.PushWords   = {Span.GlassTriangleOffset, Span.OrderedInstanceIndex, 1u, 1u, Width, Height, FrameIndex, 0u};
                OutGlassDraws.push_back(GItem);
            }
        }
        return;
    }

    if (SingleOpaqueTris > 0u)
    {
        RasterDrawItem Item{};
        Item.VertexCount = SingleOpaqueTris * 3u;
        Item.PushWords   = {0u, 0xFFFFFFFFu, 0u, 0u, Width, Height, FrameIndex, 0u};
        OutOpaqueDraws.push_back(Item);
        for (uint32_t C = 0u; C < 3u; ++C)
        {
            RasterDrawItem ShadowItem = Item;
            ShadowItem.PushWords[2]   = C;
            OutCascadeShadowDraws[C].push_back(ShadowItem);
        }
    }
    if (SingleGlassTris > 0u)
    {
        RasterDrawItem GItem{};
        GItem.VertexCount = SingleGlassTris * 3u;
        GItem.PushWords   = {SingleGlassOffset, 0xFFFFFFFFu, 1u, 0u, Width, Height, FrameIndex, 0u};
        OutGlassDraws.push_back(GItem);
    }
}

} // namespace

bool SpatiotemporalIllumination::Initialize(VulkanComputeExchange& Exchange,
                                            const std::string&     ShaderBinaryDirectory,
                                            std::string*           OutDiagnostic)
{
    Shutdown(Exchange);

    const std::array<VkDescriptorType, 19> ComputeBindings{{
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
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 11: MaterialMaps (mipmapped sRGB8 2DArray)
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 12: SdfClipmapVolume (3D R16_SFLOAT hardware volume)
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 13: ShadowDepthArray (1024x1024x3 D32_SFLOAT)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 14: TemporalHistoryPrev (6 layers rgba16f)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 15: TemporalHistoryCurr (6 layers rgba16f)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 16: SurfaceAndWorkAtlas (14 layers rgba16f)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 17: OutputImage (1 layer rgba16f)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 18: ViewportDisplayTarget (2D rgba8)
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
                                     ComputeBindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     RadiosityProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/TracePrimarySurface.spv",
                                     "TracePrimarySurfaceMain",
                                     ComputeBindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     PrimaryProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/EvaluateDirectRadiance.spv",
                                     "EvaluateDirectRadianceMain",
                                     ComputeBindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     DirectRadianceProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/TraceIndirectDiffuse.spv",
                                     "TraceIndirectDiffuseMain",
                                     ComputeBindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     IndirectGiProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/TraceSpecularReflections.spv",
                                     "TraceSpecularReflectionsMain",
                                     ComputeBindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     SpecularReflectProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/IntegrateTemporalVariance.spv",
                                     "IntegrateTemporalVarianceMain",
                                     ComputeBindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     TemporalVarianceProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/FilterSpatiotemporalAtrous.spv",
                                     "FilterSpatiotemporalAtrousMain",
                                     ComputeBindings,
                                     sizeof(SpatiotemporalPushConstants),
                                     AtrousProgram_,
                                     OutDiagnostic))
    {
        return false;
    }

    RecordStats(RadiosityProgram_);
    RecordStats(PrimaryProgram_);
    RecordStats(DirectRadianceProgram_);
    RecordStats(IndirectGiProgram_);
    RecordStats(SpecularReflectProgram_);
    RecordStats(TemporalVarianceProgram_);
    RecordStats(AtrousProgram_);

    // Initialize Hardware Rasterization Pipelines (ShadowRaster + SurfaceRaster + ForwardGlazing) whenever GraphicsSupported!
    HardwareRasterReady_ = false;
    if (Exchange.GraphicsSupported())
    {
        const std::array<VkDescriptorType, 7> RasterBindings{{
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 0: FrameHeader
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 1: BvhOrTopCells
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 2: BvhTriangles
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 3: MaterialTable
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 4: OctetInstances
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // 5: OctetBlocks
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 6: MaterialMaps
        }};

        if (Exchange.CreateShadowProcedureOnly(ShadowRenderProcedure_) &&
            Exchange.CreateSurfaceProcedureOnly(SurfaceRenderProcedure_) &&
            Exchange.AllocateDepthTargetArray(ShadowMapResolution, ShadowMapResolution, ShadowCascadeCount, ShadowDepthArray_) &&
            Exchange.CreateShadowFramebufferSet(ShadowRenderProcedure_, ShadowDepthArray_, ShadowTargets_) &&
            Exchange.LoadGraphicsProgram(ShaderBinaryDirectory + "/ProjectShadowCascadeVertex.spv",
                                         "ProjectShadowCascadeVertexMain",
                                         "",
                                         nullptr,
                                         RasterBindings,
                                         32u,
                                         ShadowRenderProcedure_,
                                         0u,
                                         VK_COMPARE_OP_LESS_OR_EQUAL,
                                         true,
                                         true,
                                         ShadowCascadeProgram_,
                                         OutDiagnostic) &&
            Exchange.LoadGraphicsProgram(ShaderBinaryDirectory + "/ProjectSurfaceVertex.spv",
                                         "ProjectSurfaceVertexMain",
                                         ShaderBinaryDirectory + "/EvaluateSurfaceFragment.spv",
                                         "EvaluateSurfaceFragmentMain",
                                         RasterBindings,
                                         32u,
                                         SurfaceRenderProcedure_,
                                         6u,
                                         VK_COMPARE_OP_GREATER_OR_EQUAL, // Reversed-Z!
                                         true,
                                         false,
                                         SurfaceOpaqueProgram_,
                                         OutDiagnostic) &&
            Exchange.LoadGraphicsProgram(ShaderBinaryDirectory + "/ProjectForwardGlazingVertex.spv",
                                         "ProjectForwardGlazingVertexMain",
                                         ShaderBinaryDirectory + "/EvaluateForwardGlazingFragment.spv",
                                         "EvaluateForwardGlazingFragmentMain",
                                         RasterBindings,
                                         32u,
                                         SurfaceRenderProcedure_,
                                         6u,
                                         VK_COMPARE_OP_GREATER, // Reversed-Z depth test against opaque, depthWrite=false
                                         false,
                                         false,
                                         ForwardGlazingProgram_,
                                         OutDiagnostic))
        {
            HardwareRasterReady_ = true;
            CompileStats_.push_back({"ProjectShadowCascadeVertexMain", 0u, ShadowCascadeProgram_.SpirvByteCount, 0.0f, ShadowCascadeProgram_.PipelineCompileMilliseconds});
            CompileStats_.push_back({"EvaluateSurfaceFragmentMain", 0u, SurfaceOpaqueProgram_.SpirvByteCount, 0.0f, SurfaceOpaqueProgram_.PipelineCompileMilliseconds});
            CompileStats_.push_back({"EvaluateForwardGlazingFragmentMain", 0u, ForwardGlazingProgram_.SpirvByteCount, 0.0f, ForwardGlazingProgram_.PipelineCompileMilliseconds});
        }
    }

    if (ShadowDepthArray_.Image == VK_NULL_HANDLE)
    {
        if (!Exchange.AllocateDepthTargetArray(16u, 16u, ShadowCascadeCount, ShadowDepthArray_))
        {
            return false;
        }
    }

    // Allocate neutral placeholder spans, 3D volume, and fallback textures so every binding is valid prior to scene upload.
    const std::array<float, 36> ZeroFloats{};
    if (!Exchange.UploadMemorySpan(OctetInstanceSpan_, ZeroFloats.data(), sizeof(ZeroFloats)) ||
        !Exchange.UploadMemorySpan(OctetBlockSpan_, ZeroFloats.data(), sizeof(ZeroFloats)) ||
        !Exchange.UploadMemorySpan(LuminousFacetSpan_, ZeroFloats.data(), sizeof(ZeroFloats)) ||
        !Exchange.UploadMemorySpan(SurfaceCardSpan_, ZeroFloats.data(), sizeof(ZeroFloats)))
    {
        return false;
    }

    const std::vector<float> NeutralVolume(4u * 4u * 4u, 10.0f);
    if (!Exchange.AllocateR16fVolume3D(4u, 4u, 4u, SdfClipmapVolume_) ||
        !Exchange.UploadR16fVolume3D(SdfClipmapVolume_, NeutralVolume))
    {
        return false;
    }

    std::vector<uint8_t> WhiteTex(16u * 16u * 4u, 255u);
    if (!Exchange.AllocateSrgb8Array(16u, 16u, 1u, MaterialTextureArray_) ||
        !Exchange.UploadSrgb8Array(MaterialTextureArray_, WhiteTex))
    {
        return false;
    }

    if (!Exchange.AllocateRgba16fArray(16u, 16u, 6u, TemporalHistoryA_) ||
        !Exchange.AllocateRgba16fArray(16u, 16u, 6u, TemporalHistoryB_) ||
        !Exchange.AllocateRgba16fArray(16u, 16u, 14u, SurfaceAndWorkAtlas_) ||
        !Exchange.AllocateRgba16fArray(16u, 16u, 1u, OutputImage_) ||
        !Exchange.AllocateViewportImage2D(16u, 16u, FallbackViewport2D_))
    {
        return false;
    }

    (void)Exchange.ClearRgba16fArray(TemporalHistoryA_);
    (void)Exchange.ClearRgba16fArray(TemporalHistoryB_);
    (void)Exchange.ClearRgba16fArray(SurfaceAndWorkAtlas_);
    (void)Exchange.ClearRgba16fArray(OutputImage_);
    return true;
}

void SpatiotemporalIllumination::Shutdown(VulkanComputeExchange& Exchange) noexcept
{
    Exchange.ReleaseSurfaceFramebufferSet(SurfaceTargets_);
    Exchange.ReleaseShadowFramebufferSet(ShadowTargets_);
    Exchange.ReleaseGraphicsProgram(ForwardGlazingProgram_);
    Exchange.ReleaseGraphicsProgram(SurfaceOpaqueProgram_);
    Exchange.ReleaseGraphicsProgram(ShadowCascadeProgram_);
    Exchange.ReleaseRenderProcedure(SurfaceRenderProcedure_);
    Exchange.ReleaseRenderProcedure(ShadowRenderProcedure_);
    HardwareRasterReady_ = false;

    Exchange.ReleaseViewportImage2D(FallbackViewport2D_);
    Exchange.ReleaseImage(OutputImage_);
    Exchange.ReleaseImage(SurfaceAndWorkAtlas_);
    Exchange.ReleaseImage(TemporalHistoryB_);
    Exchange.ReleaseImage(TemporalHistoryA_);
    Exchange.ReleaseDepthTarget(PrimaryDepthTarget_);
    Exchange.ReleaseDepthTarget(ShadowDepthArray_);
    Exchange.ReleaseVolume3D(SdfClipmapVolume_);
    Exchange.ReleaseImage(MaterialTextureArray_);

    Exchange.ReleaseMemorySpan(DirectReservoirB_);
    Exchange.ReleaseMemorySpan(DirectReservoirA_);
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
    Exchange.ReleaseProgram(TemporalVarianceProgram_);
    Exchange.ReleaseProgram(SpecularReflectProgram_);
    Exchange.ReleaseProgram(IndirectGiProgram_);
    Exchange.ReleaseProgram(DirectRadianceProgram_);
    Exchange.ReleaseProgram(PrimaryProgram_);
    Exchange.ReleaseProgram(RadiosityProgram_);

    CachedTwoLevelDrawSpans_.clear();
    SingleLevelOpaqueTris_  = 0u;
    SingleLevelGlassTris_   = 0u;
    SingleLevelGlassOffset_ = 0u;
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
    std::vector<std::array<float, 4>> RawTriTable;
    BvhAtlas.PackForGpu(CellTable, RawTriTable);

    // For single-level scenes: keep RawTriTable intact for BVH traversal, and check if any transmissive glass exists
    // (Note: BvhAtlas leaf indices reference RawTriTable indices, so we do NOT reorder RawTriTable; in SurfaceRaster,
    // ProjectSurfaceVertexMain skips transmissive triangles via IsOctetTransmissiveClass, and ProjectForwardGlazingVertexMain
    // skips opaque triangles! So passing the full triangle range [0 .. TriCount) to both draws works for arbitrary ordering!)
    uint32_t AnyOpaqueCount = 0u;
    uint32_t AnyGlassCount  = 0u;
    std::vector<PackedRayTriangle> ExtractedTris;
    ExtractedTris.reserve(BvhAtlas.TriangleCount());
    for (size_t Base = 0u; Base + 4u < RawTriTable.size(); Base += 5u)
    {
        const uint32_t PackedClass = std::bit_cast<uint32_t>(RawTriTable[Base + 2u][3]);
        if ((PackedClass & 0x80000000u) != 0u)
        {
            ++AnyGlassCount;
        }
        else
        {
            ++AnyOpaqueCount;
        }
        PackedRayTriangle T{};
        T.Vertex0          = {RawTriTable[Base + 0u][0], RawTriTable[Base + 0u][1], RawTriTable[Base + 0u][2]};
        T.Edge1            = {RawTriTable[Base + 1u][0], RawTriTable[Base + 1u][1], RawTriTable[Base + 1u][2]};
        T.Edge2            = {RawTriTable[Base + 2u][0], RawTriTable[Base + 2u][1], RawTriTable[Base + 2u][2]};
        T.ClassificationId = PackedClass & 0x7FFFFFFFu;
        ExtractedTris.push_back(T);
    }
    SingleLevelOpaqueTris_  = (AnyOpaqueCount > 0u) ? BvhAtlas.TriangleCount() : 0u;
    SingleLevelGlassTris_   = (AnyGlassCount > 0u) ? BvhAtlas.TriangleCount() : 0u;
    SingleLevelGlassOffset_ = 0u;
    CachedTwoLevelDrawSpans_.clear();

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
    if (DistanceVolume != nullptr && !DistanceVolume->Distances.empty() &&
        DistanceVolume->Dimensions[0] > 0u && DistanceVolume->Dimensions[1] > 0u && DistanceVolume->Dimensions[2] > 0u)
    {
        PackVolumeDistanceForGpu(*DistanceVolume, SdfTable);
        if (!Exchange.AllocateR16fVolume3D(
                DistanceVolume->Dimensions[0], DistanceVolume->Dimensions[1], DistanceVolume->Dimensions[2], SdfClipmapVolume_) ||
            !Exchange.UploadR16fVolume3D(SdfClipmapVolume_, DistanceVolume->Distances))
        {
            return false;
        }
        HasSdfVolume_ = true;
    }
    else
    {
        VolumeDistanceField Empty{};
        PackVolumeDistanceForGpu(Empty, SdfTable);
        HasSdfVolume_ = false;
    }

    LuminousFacetAtlas AutoLuminous{};
    AutoLuminous.BuildFromWorldTriangles(ExtractedTris, MaterialAtlas);
    std::vector<std::array<float, 4>> LumFacetTable;
    AutoLuminous.PackForGpu(LumFacetTable);

    if (!Exchange.UploadMemorySpan(BvhOrTopCellSpan_, CellTable.data(), CellTable.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(BvhTriangleSpan_, RawTriTable.data(), RawTriTable.size() * sizeof(float) * 4u) ||
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
    TwoLevelAtlas.QueryRasterDrawSpans(CachedTwoLevelDrawSpans_);
    SingleLevelOpaqueTris_  = 0u;
    SingleLevelGlassTris_   = 0u;
    SingleLevelGlassOffset_ = 0u;

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

    std::vector<std::array<float, 4>> RangeMetaTable;
    if (RangeClipmap != nullptr && !RangeClipmap->Empty())
    {
        uint32_t           VolW = 0u, VolH = 0u, VolD = 0u;
        std::vector<float> ScalarVolume;
        RangeClipmap->PackVolume3DForGpu(VolW, VolH, VolD, ScalarVolume);
        RangeClipmap->PackVolumeMetadataForGpu(RangeMetaTable);
        if (SdfClipmapVolume_.Width != VolW || SdfClipmapVolume_.Height != VolH || SdfClipmapVolume_.Depth != VolD)
        {
            if (!Exchange.AllocateR16fVolume3D(VolW, VolH, VolD, SdfClipmapVolume_))
            {
                return false;
            }
        }
        if (!Exchange.UploadR16fVolume3D(SdfClipmapVolume_, ScalarVolume))
        {
            return false;
        }
        RangeClipmapActive_ = true;
        HasSdfVolume_       = true;
    }
    else
    {
        VolumeDistanceField Empty{};
        PackVolumeDistanceForGpu(Empty, RangeMetaTable);
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
        !Exchange.UploadMemorySpan(DistanceOrRangeSpan_, RangeMetaTable.data(), RangeMetaTable.size() * sizeof(float) * 4u) ||
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
    TwoLevelAtlas.QueryRasterDrawSpans(CachedTwoLevelDrawSpans_);

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
    uint32_t                          VolW = 0u, VolH = 0u, VolD = 0u;
    std::vector<float>                ScalarVolume;
    std::vector<std::array<float, 4>> RangeMetaTable;
    RangeClipmap.PackVolume3DForGpu(VolW, VolH, VolD, ScalarVolume);
    RangeClipmap.PackVolumeMetadataForGpu(RangeMetaTable);

    if (SdfClipmapVolume_.Width != VolW || SdfClipmapVolume_.Height != VolH || SdfClipmapVolume_.Depth != VolD)
    {
        if (!Exchange.AllocateR16fVolume3D(VolW, VolH, VolD, SdfClipmapVolume_))
        {
            return false;
        }
    }
    if (!Exchange.UploadR16fVolume3D(SdfClipmapVolume_, ScalarVolume) ||
        !Exchange.UploadMemorySpan(DistanceOrRangeSpan_, RangeMetaTable.data(), RangeMetaTable.size() * sizeof(float) * 4u))
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

    if (Config.FullRaytracingMode != LastModeFullRaytracing_)
    {
        HistoryValid_           = false;
        LastModeFullRaytracing_ = Config.FullRaytracingMode;
    }

    if (OutputImage_.Width != W || OutputImage_.Height != H)
    {
        if (!Exchange.AllocateRgba16fArray(W, H, 6u, TemporalHistoryA_) ||
            !Exchange.AllocateRgba16fArray(W, H, 6u, TemporalHistoryB_) ||
            !Exchange.AllocateRgba16fArray(W, H, 14u, SurfaceAndWorkAtlas_) ||
            !Exchange.AllocateRgba16fArray(W, H, 1u, OutputImage_))
        {
            return false;
        }
        (void)Exchange.ClearRgba16fArray(TemporalHistoryA_);
        (void)Exchange.ClearRgba16fArray(TemporalHistoryB_);
        (void)Exchange.ClearRgba16fArray(SurfaceAndWorkAtlas_);
        (void)Exchange.ClearRgba16fArray(OutputImage_);

        if (HardwareRasterReady_)
        {
            if (!Exchange.AllocateDepthTargetArray(W, H, 1u, PrimaryDepthTarget_) ||
                !Exchange.CreateSurfaceFramebufferSet(SurfaceRenderProcedure_, SurfaceAndWorkAtlas_, PrimaryDepthTarget_, SurfaceTargets_))
            {
                return false;
            }
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

    const size_t DirectResBytes = static_cast<size_t>(W) * H * sizeof(float) * 4u;
    if (DirectReservoirA_.Capacity < DirectResBytes || DirectReservoirB_.Capacity < DirectResBytes)
    {
        std::vector<float> ZeroRes(static_cast<size_t>(W) * H * 4u, 0.0f);
        if (!Exchange.UploadMemorySpan(DirectReservoirA_, ZeroRes.data(), DirectResBytes) ||
            !Exchange.UploadMemorySpan(DirectReservoirB_, ZeroRes.data(), DirectResBytes))
        {
            return false;
        }
        HistoryValid_ = false;
    }

    const auto                   CamFwd   = NormVec(SubVec(Camera.TargetMeters, Camera.OriginMeters));
    const std::array<float, 16>  PrevRows = HistoryValid_ ? PreviousViewProjRows_ : PreviousViewProjRows_;
    const SolarCascadeHeaderPack Cascades = BuildSolarShadowCascades(
        Camera.OriginMeters, CamFwd, Luminaires, ShadowMapResolution, HardwareRasterReady_ && !Config.FullRaytracingMode);

    std::array<float, 16>             CurrentViewProjRows{};
    std::vector<std::array<float, 4>> Header;
    BuildFrameHeaderVector(Camera,
                           W,
                           H,
                           UploadedBvhCells_,
                           UploadedLuminousFacets_,
                           UploadedSurfacePatches_,
                           HasSdfVolume_,
                           TwoLevelOctetActive_,
                           RangeClipmapActive_,
                           HistoryValid_ ? PreviousViewProjRows_ : CurrentViewProjRows,
                           Cascades,
                           Luminaires,
                           Config,
                           CurrentViewProjRows,
                           Header);
    if (!HistoryValid_)
    {
        Header[8]  = {CurrentViewProjRows[0], CurrentViewProjRows[1], CurrentViewProjRows[2], CurrentViewProjRows[3]};
        Header[9]  = {CurrentViewProjRows[4], CurrentViewProjRows[5], CurrentViewProjRows[6], CurrentViewProjRows[7]};
        Header[10] = {CurrentViewProjRows[8], CurrentViewProjRows[9], CurrentViewProjRows[10], CurrentViewProjRows[11]};
        Header[11] = {CurrentViewProjRows[12], CurrentViewProjRows[13], CurrentViewProjRows[14], CurrentViewProjRows[15]};
    }

    if (!Exchange.UploadMemorySpan(FrameHeaderSpan_, Header.data(), Header.size() * sizeof(float) * 4u))
    {
        return false;
    }

    const bool                  EvenFrame   = ((FrameCounter_ & 1u) == 0u);
    const DeviceImageArraySlot& HistPrev    = EvenFrame ? TemporalHistoryA_ : TemporalHistoryB_;
    const DeviceImageArraySlot& HistCurr    = EvenFrame ? TemporalHistoryB_ : TemporalHistoryA_;
    const DeviceMemorySpan&     DirResPrev  = EvenFrame ? DirectReservoirA_ : DirectReservoirB_;
    const DeviceMemorySpan&     DirResCurr  = EvenFrame ? DirectReservoirB_ : DirectReservoirA_;
    const bool                  UseTemporal = Config.EnableTemporalReuse && HistoryValid_;

    const std::array<ComputeDescriptorArgument, 19> ComputeArgs{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &FrameHeaderSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhOrTopCellSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhTriangleSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &MaterialSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &DistanceOrRangeSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &OctetInstanceSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &OctetBlockSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &LuminousFacetSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &SurfaceCardSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &DirResCurr, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &DirResPrev, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &MaterialTextureArray_, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, nullptr, nullptr, &SdfClipmapVolume_, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, nullptr, nullptr, nullptr, &ShadowDepthArray_},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &HistPrev, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &HistCurr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &SurfaceAndWorkAtlas_, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &OutputImage_, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, nullptr, ViewportSlot, nullptr, nullptr},
    }};

    const std::array<ComputeDescriptorArgument, 7> RasterArgs{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &FrameHeaderSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhOrTopCellSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhTriangleSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &MaterialSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &OctetInstanceSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &OctetBlockSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &MaterialTextureArray_, nullptr, nullptr, nullptr},
    }};

    const uint32_t GroupsX      = (W + 7u) / 8u;
    const uint32_t GroupsY      = (H + 7u) / 8u;
    const uint32_t GiTraceScale = static_cast<uint32_t>(Header[18][3] + 0.5f);
    const uint32_t GiGroupsX    = (((W + GiTraceScale - 1u) / GiTraceScale) + 7u) / 8u;
    const uint32_t GiGroupsY    = (((H + GiTraceScale - 1u) / GiTraceScale) + 7u) / 8u;

    const SpatiotemporalPushConstants BasePush{
        {W, H, FrameCounter_, 0u},
        {Config.ExposureScale, 1.0f, 3.8f, UseTemporal ? 1.0f : 0.0f},
    };

    float RadiosityMs = 0.0f;
    if (UploadedSurfacePatches_ > 0u)
    {
        if (!Exchange.DispatchCompute(RadiosityProgram_, ComputeArgs, &BasePush, sizeof(BasePush), 1u, 1u, 1u))
        {
            return false;
        }
        RadiosityMs = Exchange.QueryLastDispatchGpuMilliseconds();
    }

    float PrimaryMs = 0.0f;
    float ShadowMs  = 0.0f;
    if (HardwareRasterReady_)
    {
        std::vector<RasterDrawItem>                OpaqueDraws;
        std::vector<RasterDrawItem>                GlassDraws;
        std::array<std::vector<RasterDrawItem>, 3> CascadeDraws;
        BuildPerFrameRasterItems(TwoLevelOctetActive_,
                                 CachedTwoLevelDrawSpans_,
                                 SingleLevelOpaqueTris_,
                                 SingleLevelGlassTris_,
                                 SingleLevelGlassOffset_,
                                 W,
                                 H,
                                 FrameCounter_,
                                 OpaqueDraws,
                                 GlassDraws,
                                 CascadeDraws);

        if (Cascades.Active)
        {
            for (uint32_t C = 0u; C < ShadowCascadeCount; ++C)
            {
                if (!Exchange.ExecuteShadowCascadeDraws(
                        ShadowCascadeProgram_, ShadowTargets_, ShadowDepthArray_, RasterArgs, C, CascadeDraws[C]))
                {
                    return false;
                }
                ShadowMs += Exchange.QueryLastDispatchGpuMilliseconds();
            }
        }

        if (!Exchange.ExecuteSurfaceAndForwardDraws(SurfaceOpaqueProgram_,
                                                    ForwardGlazingProgram_,
                                                    SurfaceTargets_,
                                                    SurfaceAndWorkAtlas_,
                                                    PrimaryDepthTarget_,
                                                    RasterArgs,
                                                    OpaqueDraws,
                                                    GlassDraws))
        {
            return false;
        }
        PrimaryMs = Exchange.QueryLastDispatchGpuMilliseconds();
    }
    else
    {
        if (!Exchange.DispatchCompute(PrimaryProgram_, ComputeArgs, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u))
        {
            return false;
        }
        PrimaryMs = Exchange.QueryLastDispatchGpuMilliseconds();
    }

    if (!Exchange.DispatchCompute(DirectRadianceProgram_, ComputeArgs, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u))
    {
        return false;
    }
    const float DirectMs = ShadowMs + Exchange.QueryLastDispatchGpuMilliseconds();

    if (!Exchange.DispatchCompute(IndirectGiProgram_, ComputeArgs, &BasePush, sizeof(BasePush), GiGroupsX, GiGroupsY, 1u))
    {
        return false;
    }
    const float IndirectGiMs = Exchange.QueryLastDispatchGpuMilliseconds();

    if (!Exchange.DispatchCompute(SpecularReflectProgram_, ComputeArgs, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u))
    {
        return false;
    }
    const float SpecReflMs = Exchange.QueryLastDispatchGpuMilliseconds();

    if (!Exchange.DispatchCompute(TemporalVarianceProgram_, ComputeArgs, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u))
    {
        return false;
    }
    float AtrousMs = Exchange.QueryLastDispatchGpuMilliseconds();

    const uint32_t WaveletPasses = Config.EnableSpatialReuse ? std::clamp(Config.AtrousIterations, 1u, 4u) : 0u;
    for (uint32_t Iter = 0u; Iter < WaveletPasses; ++Iter)
    {
        const SpatiotemporalPushConstants WaveletPush{
            {W, H, Iter & 1u, 0u},
            {Config.ExposureScale, static_cast<float>(1u << Iter), 3.8f, 0.0f},
        };
        if (!Exchange.DispatchCompute(AtrousProgram_, ComputeArgs, &WaveletPush, sizeof(WaveletPush), GroupsX, GroupsY, 1u))
        {
            return false;
        }
        AtrousMs += Exchange.QueryLastDispatchGpuMilliseconds();
    }

    // Final joint-bilateral half-res GI upsample + rough reflection EnvBRDF fallback + ACES display compose
    const SpatiotemporalPushConstants ComposePush{
        {W, H, WaveletPasses & 1u, 2u},
        {Config.ExposureScale, 1.0f, 3.8f, 0.0f},
    };
    if (!Exchange.DispatchCompute(AtrousProgram_, ComputeArgs, &ComposePush, sizeof(ComposePush), GroupsX, GroupsY, 1u))
    {
        return false;
    }
    AtrousMs += Exchange.QueryLastDispatchGpuMilliseconds();

    if (OptionalViewportTarget != nullptr)
    {
        Exchange.PrepareViewportImageForSampling(*OptionalViewportTarget);
    }

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
        OutTelemetry->LuminaireCount        = static_cast<uint32_t>(std::min<size_t>(Luminaires.size(), 32u));
        OutTelemetry->LuminousFacetCount    = UploadedLuminousFacets_;
        OutTelemetry->SurfacePatchCount     = UploadedSurfacePatches_;
        OutTelemetry->AuthoredTextureLayers = UploadedTextureLayers_;
        OutTelemetry->SdfVolumeActive       = HasSdfVolume_;
        OutTelemetry->RangeClipmapActive    = RangeClipmapActive_;
        OutTelemetry->HardwareRasterActive  = HardwareRasterReady_;
        OutTelemetry->FullRaytracingActive  = Config.FullRaytracingMode;
        OutTelemetry->GiTraceScale          = GiTraceScale;
        OutTelemetry->FrameIndex            = FrameCounter_;
        OutTelemetry->RadiosityGpuMs        = RadiosityMs;
        OutTelemetry->PrimaryGpuMs          = PrimaryMs;
        OutTelemetry->ShadowRasterGpuMs     = ShadowMs;
        OutTelemetry->DirectRisGpuMs        = DirectMs;
        OutTelemetry->IndirectGiGpuMs       = IndirectGiMs;
        OutTelemetry->SpatialReflectGpuMs   = SpecReflMs;
        OutTelemetry->AtrousResolveGpuMs    = AtrousMs;
        OutTelemetry->TotalGpuMs            = RadiosityMs + PrimaryMs + DirectMs + IndirectGiMs + SpecReflMs + AtrousMs;
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

    std::array<float, VulkanComputeExchange::MaxTimedDispatchesSlot> PrevTimings{};
    const bool               HasPrevTimings = Exchange.HarvestExternalSlotTimings(SlotIdx, PrevTimings);
    const RecordedSlotCounts PrevCounts     = ExternalSlotCounts_[SlotIdx];

    if (Config.FullRaytracingMode != LastModeFullRaytracing_)
    {
        HistoryValid_           = false;
        LastModeFullRaytracing_ = Config.FullRaytracingMode;
    }

    if (OutputImage_.Width != W || OutputImage_.Height != H)
    {
        if (!Exchange.AllocateRgba16fArray(W, H, 6u, TemporalHistoryA_) ||
            !Exchange.AllocateRgba16fArray(W, H, 6u, TemporalHistoryB_) ||
            !Exchange.AllocateRgba16fArray(W, H, 14u, SurfaceAndWorkAtlas_) ||
            !Exchange.AllocateRgba16fArray(W, H, 1u, OutputImage_))
        {
            return false;
        }
        (void)Exchange.ClearRgba16fArray(TemporalHistoryA_);
        (void)Exchange.ClearRgba16fArray(TemporalHistoryB_);
        (void)Exchange.ClearRgba16fArray(SurfaceAndWorkAtlas_);
        (void)Exchange.ClearRgba16fArray(OutputImage_);

        if (HardwareRasterReady_)
        {
            if (!Exchange.AllocateDepthTargetArray(W, H, 1u, PrimaryDepthTarget_) ||
                !Exchange.CreateSurfaceFramebufferSet(SurfaceRenderProcedure_, SurfaceAndWorkAtlas_, PrimaryDepthTarget_, SurfaceTargets_))
            {
                return false;
            }
        }
        HistoryValid_ = false;
    }

    const size_t DirectResBytes = static_cast<size_t>(W) * H * sizeof(float) * 4u;
    if (DirectReservoirA_.Capacity < DirectResBytes || DirectReservoirB_.Capacity < DirectResBytes)
    {
        std::vector<float> ZeroRes(static_cast<size_t>(W) * H * 4u, 0.0f);
        if (!Exchange.UploadMemorySpan(DirectReservoirA_, ZeroRes.data(), DirectResBytes) ||
            !Exchange.UploadMemorySpan(DirectReservoirB_, ZeroRes.data(), DirectResBytes))
        {
            return false;
        }
        HistoryValid_ = false;
    }

    const auto                   CamFwd   = NormVec(SubVec(Camera.TargetMeters, Camera.OriginMeters));
    const SolarCascadeHeaderPack Cascades = BuildSolarShadowCascades(
        Camera.OriginMeters, CamFwd, Luminaires, ShadowMapResolution, HardwareRasterReady_ && !Config.FullRaytracingMode);

    std::array<float, 16>             CurrentViewProjRows{};
    std::vector<std::array<float, 4>> Header;
    BuildFrameHeaderVector(Camera,
                           W,
                           H,
                           UploadedBvhCells_,
                           UploadedLuminousFacets_,
                           UploadedSurfacePatches_,
                           HasSdfVolume_,
                           TwoLevelOctetActive_,
                           RangeClipmapActive_,
                           PreviousViewProjRows_,
                           Cascades,
                           Luminaires,
                           Config,
                           CurrentViewProjRows,
                           Header);
    if (!HistoryValid_)
    {
        Header[8]  = {CurrentViewProjRows[0], CurrentViewProjRows[1], CurrentViewProjRows[2], CurrentViewProjRows[3]};
        Header[9]  = {CurrentViewProjRows[4], CurrentViewProjRows[5], CurrentViewProjRows[6], CurrentViewProjRows[7]};
        Header[10] = {CurrentViewProjRows[8], CurrentViewProjRows[9], CurrentViewProjRows[10], CurrentViewProjRows[11]};
        Header[11] = {CurrentViewProjRows[12], CurrentViewProjRows[13], CurrentViewProjRows[14], CurrentViewProjRows[15]};
    }

    DeviceMemorySpan& RingHeaderSpan = FrameHeaderRing_[SlotIdx];
    if (!Exchange.UploadMemorySpan(RingHeaderSpan, Header.data(), Header.size() * sizeof(float) * 4u))
    {
        return false;
    }

    const bool                  EvenFrame   = ((FrameCounter_ & 1u) == 0u);
    const DeviceImageArraySlot& HistPrev    = EvenFrame ? TemporalHistoryA_ : TemporalHistoryB_;
    const DeviceImageArraySlot& HistCurr    = EvenFrame ? TemporalHistoryB_ : TemporalHistoryA_;
    const DeviceMemorySpan&     DirResPrev  = EvenFrame ? DirectReservoirA_ : DirectReservoirB_;
    const DeviceMemorySpan&     DirResCurr  = EvenFrame ? DirectReservoirB_ : DirectReservoirA_;
    const bool                  UseTemporal = Config.EnableTemporalReuse && HistoryValid_;

    const std::array<ComputeDescriptorArgument, 19> ComputeArgs{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &RingHeaderSpan, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhOrTopCellSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhTriangleSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &MaterialSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &DistanceOrRangeSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &OctetInstanceSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &OctetBlockSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &LuminousFacetSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &SurfaceCardSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &DirResCurr, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &DirResPrev, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &MaterialTextureArray_, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, nullptr, nullptr, &SdfClipmapVolume_, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, nullptr, nullptr, nullptr, &ShadowDepthArray_},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &HistPrev, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &HistCurr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &SurfaceAndWorkAtlas_, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &OutputImage_, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, nullptr, &ViewportTarget, nullptr, nullptr},
    }};

    const std::array<ComputeDescriptorArgument, 7> RasterArgs{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &RingHeaderSpan, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhOrTopCellSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &BvhTriangleSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &MaterialSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &OctetInstanceSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &OctetBlockSpan_, nullptr, nullptr, nullptr, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &MaterialTextureArray_, nullptr, nullptr, nullptr},
    }};

    const uint32_t GroupsX      = (W + 7u) / 8u;
    const uint32_t GroupsY      = (H + 7u) / 8u;
    const uint32_t GiTraceScale = static_cast<uint32_t>(Header[18][3] + 0.5f);
    const uint32_t GiGroupsX    = (((W + GiTraceScale - 1u) / GiTraceScale) + 7u) / 8u;
    const uint32_t GiGroupsY    = (((H + GiTraceScale - 1u) / GiTraceScale) + 7u) / 8u;

    const SpatiotemporalPushConstants BasePush{
        {W, H, FrameCounter_, 0u},
        {Config.ExposureScale, 1.0f, 3.8f, UseTemporal ? 1.0f : 0.0f},
    };

    RecordedSlotCounts CurrCounts{};
    Exchange.BeginExternalCommandStream(ExternalCommandBuffer, SlotIdx);
    (void)Exchange.PrepareViewportImageForCompute(ViewportTarget);

    if (UploadedSurfacePatches_ > 0u)
    {
        (void)Exchange.DispatchCompute(RadiosityProgram_, ComputeArgs, &BasePush, sizeof(BasePush), 1u, 1u, 1u);
        CurrCounts.RadiosityDispatches = 1u;
    }

    if (HardwareRasterReady_)
    {
        std::vector<RasterDrawItem>                OpaqueDraws;
        std::vector<RasterDrawItem>                GlassDraws;
        std::array<std::vector<RasterDrawItem>, 3> CascadeDraws;
        BuildPerFrameRasterItems(TwoLevelOctetActive_,
                                 CachedTwoLevelDrawSpans_,
                                 SingleLevelOpaqueTris_,
                                 SingleLevelGlassTris_,
                                 SingleLevelGlassOffset_,
                                 W,
                                 H,
                                 FrameCounter_,
                                 OpaqueDraws,
                                 GlassDraws,
                                 CascadeDraws);

        if (Cascades.Active)
        {
            for (uint32_t C = 0u; C < ShadowCascadeCount; ++C)
            {
                (void)Exchange.ExecuteShadowCascadeDraws(
                    ShadowCascadeProgram_, ShadowTargets_, ShadowDepthArray_, RasterArgs, C, CascadeDraws[C]);
                ++CurrCounts.ShadowPasses;
            }
        }

        (void)Exchange.ExecuteSurfaceAndForwardDraws(SurfaceOpaqueProgram_,
                                                     ForwardGlazingProgram_,
                                                     SurfaceTargets_,
                                                     SurfaceAndWorkAtlas_,
                                                     PrimaryDepthTarget_,
                                                     RasterArgs,
                                                     OpaqueDraws,
                                                     GlassDraws);
        CurrCounts.PrimaryPasses = 1u;
    }
    else
    {
        (void)Exchange.DispatchCompute(PrimaryProgram_, ComputeArgs, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u);
        CurrCounts.PrimaryPasses = 1u;
    }

    (void)Exchange.DispatchCompute(DirectRadianceProgram_, ComputeArgs, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u);
    (void)Exchange.DispatchCompute(IndirectGiProgram_, ComputeArgs, &BasePush, sizeof(BasePush), GiGroupsX, GiGroupsY, 1u);
    (void)Exchange.DispatchCompute(SpecularReflectProgram_, ComputeArgs, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u);
    (void)Exchange.DispatchCompute(TemporalVarianceProgram_, ComputeArgs, &BasePush, sizeof(BasePush), GroupsX, GroupsY, 1u);

    const uint32_t WaveletPasses = Config.EnableSpatialReuse ? std::clamp(Config.AtrousIterations, 1u, 4u) : 0u;
    for (uint32_t Iter = 0u; Iter < WaveletPasses; ++Iter)
    {
        const SpatiotemporalPushConstants WaveletPush{
            {W, H, Iter & 1u, 0u},
            {Config.ExposureScale, static_cast<float>(1u << Iter), 3.8f, 0.0f},
        };
        (void)Exchange.DispatchCompute(AtrousProgram_, ComputeArgs, &WaveletPush, sizeof(WaveletPush), GroupsX, GroupsY, 1u);
    }

    const SpatiotemporalPushConstants ComposePush{
        {W, H, WaveletPasses & 1u, 2u},
        {Config.ExposureScale, 1.0f, 3.8f, 0.0f},
    };
    (void)Exchange.DispatchCompute(AtrousProgram_, ComputeArgs, &ComposePush, sizeof(ComposePush), GroupsX, GroupsY, 1u);

    (void)Exchange.PrepareViewportImageForSampling(ViewportTarget);
    Exchange.EndExternalCommandStream();

    ExternalSlotCounts_[SlotIdx] = CurrCounts;
    PreviousViewProjRows_        = CurrentViewProjRows;
    HistoryValid_                = true;
    ++FrameCounter_;

    const float CpuSubmitMs =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - CpuFrameStart).count();

    if (OutTelemetry)
    {
        float RadMs = 0.0f, ShdMs = 0.0f, PrimMs = 0.0f, DirMs = 0.0f, IndMs = 0.0f, SpecMs = 0.0f, AtrMs = 0.0f;
        if (HasPrevTimings)
        {
            size_t Cursor = 0u;
            for (uint32_t I = 0u; I < PrevCounts.RadiosityDispatches && Cursor < PrevTimings.size(); ++I)
            {
                RadMs += PrevTimings[Cursor++];
            }
            for (uint32_t I = 0u; I < PrevCounts.ShadowPasses && Cursor < PrevTimings.size(); ++I)
            {
                ShdMs += PrevTimings[Cursor++];
            }
            for (uint32_t I = 0u; I < PrevCounts.PrimaryPasses && Cursor < PrevTimings.size(); ++I)
            {
                PrimMs += PrevTimings[Cursor++];
            }
            if (Cursor < PrevTimings.size()) DirMs  = ShdMs + PrevTimings[Cursor++];
            if (Cursor < PrevTimings.size()) IndMs  = PrevTimings[Cursor++];
            if (Cursor < PrevTimings.size()) SpecMs = PrevTimings[Cursor++];
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
        OutTelemetry->LuminaireCount        = static_cast<uint32_t>(std::min<size_t>(Luminaires.size(), 32u));
        OutTelemetry->LuminousFacetCount    = UploadedLuminousFacets_;
        OutTelemetry->SurfacePatchCount     = UploadedSurfacePatches_;
        OutTelemetry->AuthoredTextureLayers = UploadedTextureLayers_;
        OutTelemetry->SdfVolumeActive       = HasSdfVolume_;
        OutTelemetry->RangeClipmapActive    = RangeClipmapActive_;
        OutTelemetry->HardwareRasterActive  = HardwareRasterReady_;
        OutTelemetry->FullRaytracingActive  = Config.FullRaytracingMode;
        OutTelemetry->GiTraceScale          = GiTraceScale;
        OutTelemetry->FrameIndex            = FrameCounter_;
        OutTelemetry->RadiosityGpuMs        = RadMs;
        OutTelemetry->PrimaryGpuMs          = PrimMs;
        OutTelemetry->ShadowRasterGpuMs     = ShdMs;
        OutTelemetry->DirectRisGpuMs        = DirMs;
        OutTelemetry->IndirectGiGpuMs       = IndMs;
        OutTelemetry->SpatialReflectGpuMs   = SpecMs;
        OutTelemetry->AtrousResolveGpuMs    = AtrMs;
        OutTelemetry->TotalGpuMs            = RadMs + PrimMs + DirMs + IndMs + SpecMs + AtrMs;
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
