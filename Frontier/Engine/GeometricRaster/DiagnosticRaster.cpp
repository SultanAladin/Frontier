/*==========================================================================================================================================
                                              DIAGNOSTIC RASTER DEFINITION
==========================================================================================================================================*/
// 🧩 Editor & Development ONLY GPU Vulkan 1.3 tile-binned cluster diagnostic rasterizer (zero code in Shipping).

#include "Engine/GeometricRaster/DiagnosticRaster.h"

#if FRONTIER_EDITOR

#include "Engine/PlanetaryAtmosphere/PlanetaryEnsemble.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{
namespace
{

struct DiagnosticPushConstants
{
    uint32_t Counts[4];
    float    Solar[4];
};

inline ClusterFloat3 Sub3(ClusterFloat3 A, ClusterFloat3 B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
inline ClusterFloat3 Mul3(ClusterFloat3 A, float Scalar) noexcept { return {A.X * Scalar, A.Y * Scalar, A.Z * Scalar}; }
inline float         Dot3(ClusterFloat3 A, ClusterFloat3 B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
inline ClusterFloat3 Cross3(ClusterFloat3 A, ClusterFloat3 B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}
inline ClusterFloat3 Normalize3(ClusterFloat3 V) noexcept
{
    const float L = std::sqrt(Dot3(V, V));
    return (L > 1.0e-12f) ? Mul3(V, 1.0f / L) : ClusterFloat3{0.0f, 1.0f, 0.0f};
}

} // namespace

bool DiagnosticRaster::Initialize(VulkanComputeExchange& Exchange,
                                  const std::string&     ShaderBinaryDirectory,
                                  std::string*           OutDiagnostic)
{
    const std::array<VkDescriptorType, 4> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // ClusterSceneHeader
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // TriangleGeometry
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // TileClusterBins
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  // DiagnosticTarget
    }};

    if (!Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/BinDiagnosticTiles.spv",
                                     "BinDiagnosticTilesMain",
                                     Bindings,
                                     sizeof(DiagnosticPushConstants),
                                     TileBinProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/ResolveDiagnosticView.spv",
                                     "ResolveDiagnosticViewMain",
                                     Bindings,
                                     sizeof(DiagnosticPushConstants),
                                     ResolveProgram_,
                                     OutDiagnostic))
    {
        return false;
    }

    return Exchange.AllocateRgba16fArray(16u, 16u, 1u, TargetImage_);
}

void DiagnosticRaster::Shutdown(VulkanComputeExchange& Exchange) noexcept
{
    Exchange.ReleaseImage(TargetImage_);
    Exchange.ReleaseMemorySpan(TileBinsSpan_);
    Exchange.ReleaseMemorySpan(TriangleSpan_);
    Exchange.ReleaseMemorySpan(SceneHeaderSpan_);
    Exchange.ReleaseProgram(ResolveProgram_);
    Exchange.ReleaseProgram(TileBinProgram_);
}

bool DiagnosticRaster::RenderView(VulkanComputeExchange&              Exchange,
                                  const ClusterHierarchy&             Hierarchy,
                                  const ClusterCameraView&            Camera,
                                  DiagnosticViewMode                  ViewMode,
                                  const DiagnosticAtmosphereBackdrop& Backdrop,
                                  DiagnosticFrameExecution*           OutExecution)
{
    const uint32_t W = std::max(1u, Camera.ViewportWidthPx);
    const uint32_t H = std::max(1u, Camera.ViewportHeightPx);
    if (TargetImage_.Width != W || TargetImage_.Height != H)
    {
        if (!Exchange.AllocateRgba16fArray(W, H, 1u, TargetImage_))
        {
            return false;
        }
    }

    const ClusterRuntimeFrameReport CutReport = Hierarchy.ExecuteCutAndTwoPhaseHiZ(Camera);
    const ClusterMemoryTelemetry    Memory    = Hierarchy.QueryMemoryTelemetry(CutReport, Camera.Detail.VramPageBudgetBytes);

    const ClusterFloat3 CamFwd   = Normalize3(Sub3(Camera.CameraTarget, Camera.CameraOrigin));
    const ClusterFloat3 RefUp    = (std::abs(Dot3(CamFwd, Camera.CameraUp)) > 0.98f)
                                       ? ClusterFloat3{0.0f, 0.0f, 1.0f}
                                       : Camera.CameraUp;
    const ClusterFloat3 CamRight = Normalize3(Cross3(CamFwd, RefUp));
    const ClusterFloat3 CamUp    = Normalize3(Cross3(CamRight, CamFwd));
    const float         TanHalfV = std::tan(0.5f * Camera.VerticalFovRad);
    const float         TanHalfH = TanHalfV * (static_cast<float>(W) / static_cast<float>(H));
    const double        ProjPx   = (0.5 * static_cast<double>(H)) / std::max(1.0e-4, static_cast<double>(TanHalfV));

    const auto& Sequence = Hierarchy.TierSequence();
    const auto& Records  = Hierarchy.ClusterRecords();
    const float MaxTier  = static_cast<float>(std::max<size_t>(1u, Sequence.Tiers.size() > 0u ? Sequence.Tiers.size() - 1u : 1u));

    std::vector<uint32_t> CombinedGlobals;
    CombinedGlobals.reserve(CutReport.SelectedOpaqueGlobals.size() + CutReport.SelectedGlassGlobals.size());
    CombinedGlobals.insert(CombinedGlobals.end(), CutReport.SelectedOpaqueGlobals.begin(), CutReport.SelectedOpaqueGlobals.end());
    CombinedGlobals.insert(CombinedGlobals.end(), CutReport.SelectedGlassGlobals.begin(), CutReport.SelectedGlassGlobals.end());

    std::vector<std::array<float, 4>> HeaderSlots;
    HeaderSlots.reserve(6u + CombinedGlobals.size() * 4u);
    HeaderSlots.push_back({Camera.CameraOrigin.X, Camera.CameraOrigin.Y, Camera.CameraOrigin.Z, TanHalfV});
    HeaderSlots.push_back({CamRight.X, CamRight.Y, CamRight.Z, TanHalfH});
    HeaderSlots.push_back({CamUp.X, CamUp.Y, CamUp.Z, Camera.Detail.BaseErrorThresholdPixels});
    HeaderSlots.push_back({CamFwd.X, CamFwd.Y, CamFwd.Z, MaxTier});
    HeaderSlots.push_back({Backdrop.ZenithSkyRgb.X, Backdrop.ZenithSkyRgb.Y, Backdrop.ZenithSkyRgb.Z, 1.0f});
    HeaderSlots.push_back({Backdrop.HorizonSkyRgb.X, Backdrop.HorizonSkyRgb.Y, Backdrop.HorizonSkyRgb.Z, 1.0f});

    std::vector<std::array<float, 4>> TriangleSlots;
    TriangleSlots.reserve(std::max<size_t>(4u, static_cast<size_t>(CutReport.ActiveTrianglesAfterCull) * 4u));

    uint32_t ActiveClusterCount = 0u;
    for (uint32_t GlobalId : CombinedGlobals)
    {
        if (GlobalId >= Records.size())
        {
            continue;
        }
        const ClusterHierarchyRecord& Rec  = Records[GlobalId];
        const ClusterTier&            Tier = Sequence.Tiers[Rec.TierIndex];
        const FacetCluster&           Fc   = Tier.Clusters[Rec.ClusterIndexInTier];
        const ClusterMaterialProfile& Mat  = Hierarchy.FindMaterial(Rec.ClassificationId);

        // Compute conservative screen-space pixel bounds of the cluster's AABB
        float SMinX = static_cast<float>(W);
        float SMinY = static_cast<float>(H);
        float SMaxX = 0.0f;
        float SMaxY = 0.0f;
        bool  AnyInFront = false;
        for (int Corner = 0; Corner < 8; ++Corner)
        {
            const ClusterFloat3 WorldPt{
                (Corner & 1) ? Rec.BoundsMax.X : Rec.BoundsMin.X,
                (Corner & 2) ? Rec.BoundsMax.Y : Rec.BoundsMin.Y,
                (Corner & 4) ? Rec.BoundsMax.Z : Rec.BoundsMin.Z,
            };
            const ClusterFloat3 Rel   = Sub3(WorldPt, Camera.CameraOrigin);
            const float         ViewZ = Dot3(Rel, CamFwd);
            if (ViewZ <= 0.02f)
            {
                // Cluster straddles near plane: conservatively span full viewport
                SMinX      = 0.0f;
                SMinY      = 0.0f;
                SMaxX      = static_cast<float>(W);
                SMaxY      = static_cast<float>(H);
                AnyInFront = true;
                break;
            }
            AnyInFront = true;
            const float Sx = (0.5f + 0.5f * (Dot3(Rel, CamRight) / (ViewZ * TanHalfH))) * static_cast<float>(W);
            const float Sy = (0.5f - 0.5f * (Dot3(Rel, CamUp) / (ViewZ * TanHalfV))) * static_cast<float>(H);
            SMinX = std::min(SMinX, Sx - 1.0f);
            SMinY = std::min(SMinY, Sy - 1.0f);
            SMaxX = std::max(SMaxX, Sx + 1.0f);
            SMaxY = std::max(SMaxY, Sy + 1.0f);
        }
        if (!AnyInFront)
        {
            continue;
        }

        const double Dx   = Rec.SphereCentre.X - static_cast<double>(Camera.CameraOrigin.X);
        const double Dy   = Rec.SphereCentre.Y - static_cast<double>(Camera.CameraOrigin.Y);
        const double Dz   = Rec.SphereCentre.Z - static_cast<double>(Camera.CameraOrigin.Z);
        const float  Dist = static_cast<float>(std::max(0.05, std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) - Rec.SphereRadiusMeters));
        const float  ErrP = static_cast<float>((Rec.SelfDeviationMeters * static_cast<double>(Rec.OpticalDeviationBoost) * ProjPx) / Dist);
        const EvaluatedClusterMaterial EvalMat = Hierarchy.EvaluateClusterMaterial(GlobalId, Dist, ErrP, Camera.Detail);

        const uint32_t TriStart    = static_cast<uint32_t>(TriangleSlots.size() / 4u);
        const float    AbsorbMean  = (Mat.SpectralAbsorptionPerMeter.X + Mat.SpectralAbsorptionPerMeter.Y + Mat.SpectralAbsorptionPerMeter.Z) / 3.0f;
        for (uint32_t Tri : Fc.Triangles)
        {
            const FacetPoint3D& P0 = Tier.Complex.Positions[Tier.Complex.Triangles[Tri * 3u + 0u]];
            const FacetPoint3D& P1 = Tier.Complex.Positions[Tier.Complex.Triangles[Tri * 3u + 1u]];
            const FacetPoint3D& P2 = Tier.Complex.Positions[Tier.Complex.Triangles[Tri * 3u + 2u]];

            std::array<double, 2> Uv0{0.0, 0.0}, Uv1{0.0, 0.0}, Uv2{0.0, 0.0};
            if (Tier.Complex.CarriesUVs && Tri * 3u + 2u < Tier.Complex.CornerUVs.size())
            {
                Uv0 = Tier.Complex.CornerUVs[Tri * 3u + 0u];
                Uv1 = Tier.Complex.CornerUVs[Tri * 3u + 1u];
                Uv2 = Tier.Complex.CornerUVs[Tri * 3u + 2u];
            }

            TriangleSlots.push_back({static_cast<float>(P0.X), static_cast<float>(P0.Y), static_cast<float>(P0.Z), static_cast<float>(Uv0[0])});
            TriangleSlots.push_back({static_cast<float>(P1.X), static_cast<float>(P1.Y), static_cast<float>(P1.Z), static_cast<float>(Uv0[1])});
            TriangleSlots.push_back({static_cast<float>(P2.X), static_cast<float>(P2.Y), static_cast<float>(P2.Z), static_cast<float>(Uv1[0])});
            TriangleSlots.push_back({static_cast<float>(Uv1[1]), static_cast<float>(Uv2[0]), static_cast<float>(Uv2[1]), AbsorbMean});
        }

        const uint32_t TriCount   = static_cast<uint32_t>(Fc.Triangles.size());
        const uint32_t PackedFlag = (Rec.TransmissiveGlass ? 1u : 0u) |
                                    (static_cast<uint32_t>(EvalMat.ActiveTier) << 1u);

        HeaderSlots.push_back({SMinX, SMinY, SMaxX, SMaxY});
        HeaderSlots.push_back({static_cast<float>(TriStart),
                               static_cast<float>(TriCount),
                               static_cast<float>(GlobalId),
                               static_cast<float>(Rec.TierIndex)});
        HeaderSlots.push_back({EvalMat.EffectiveAlbedoRgb.X + Mat.EmissiveRadianceRgb.X * 0.25f,
                               EvalMat.EffectiveAlbedoRgb.Y + Mat.EmissiveRadianceRgb.Y * 0.25f,
                               EvalMat.EffectiveAlbedoRgb.Z + Mat.EmissiveRadianceRgb.Z * 0.25f,
                               EvalMat.EffectiveRoughness});
        HeaderSlots.push_back({EvalMat.EffectiveMetallic,
                               Rec.TransmissiveGlass ? Rec.RefractiveIndexIor : EvalMat.EffectiveClearcoat,
                               static_cast<float>(PackedFlag),
                               ErrP});
        ++ActiveClusterCount;
    }

    if (TriangleSlots.empty())
    {
        TriangleSlots.resize(4u, {0.0f, 0.0f, 0.0f, 0.0f});
    }

    const uint32_t TilesX       = (W + 15u) / 16u;
    const uint32_t TilesY       = (H + 15u) / 16u;
    const size_t   TileBinBytes = static_cast<size_t>(TilesX) * TilesY * 64u * sizeof(uint32_t);
    if (TileBinsSpan_.Capacity < TileBinBytes)
    {
        std::vector<uint32_t> ZeroBins(static_cast<size_t>(TilesX) * TilesY * 64u, 0u);
        if (!Exchange.UploadMemorySpan(TileBinsSpan_, ZeroBins.data(), TileBinBytes))
        {
            return false;
        }
    }

    if (!Exchange.UploadMemorySpan(SceneHeaderSpan_, HeaderSlots.data(), HeaderSlots.size() * sizeof(float) * 4u) ||
        !Exchange.UploadMemorySpan(TriangleSpan_, TriangleSlots.data(), TriangleSlots.size() * sizeof(float) * 4u))
    {
        return false;
    }

    const ClusterFloat3           SunDir = Normalize3(Backdrop.SolarDirectionUnit);
    const DiagnosticPushConstants Push{
        {W, H, ActiveClusterCount, static_cast<uint32_t>(ViewMode)},
        {SunDir.X, SunDir.Y, SunDir.Z, Backdrop.ExposureScale},
    };
    const std::array<ComputeDescriptorArgument, 4> Args{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &SceneHeaderSpan_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &TriangleSpan_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &TileBinsSpan_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &TargetImage_},
    }};

    if (!Exchange.DispatchCompute(
            TileBinProgram_, Args, &Push, sizeof(Push), (TilesX + 7u) / 8u, (TilesY + 7u) / 8u, 1u) ||
        !Exchange.DispatchCompute(
            ResolveProgram_, Args, &Push, sizeof(Push), (W + 7u) / 8u, (H + 7u) / 8u, 1u))
    {
        return false;
    }

    if (OutExecution)
    {
        OutExecution->CutReport             = CutReport;
        OutExecution->Memory                = Memory;
        OutExecution->UploadedClusterCount  = ActiveClusterCount;
        OutExecution->UploadedTriangleCount = static_cast<uint32_t>(TriangleSlots.size() / 4u);
    }
    return true;
}

bool DiagnosticRaster::ReadbackLinearRgba(VulkanComputeExchange& Exchange, std::vector<float>& OutLinearRgbaFloats)
{
    return Exchange.DownloadRgba16fLayer(TargetImage_, 0u, OutLinearRgbaFloats);
}

bool DiagnosticRaster::ReadbackTonemappedSrgb8(VulkanComputeExchange& Exchange,
                                               float                  ExposureScale,
                                               std::vector<uint8_t>&  OutSrgbRgbaBytes)
{
    std::vector<float> LinearRgba;
    if (!ReadbackLinearRgba(Exchange, LinearRgba))
    {
        return false;
    }
    TonemapHdrToSrgb8(LinearRgba, TargetImage_.Width, TargetImage_.Height, ExposureScale, OutSrgbRgbaBytes);
    return true;
}

} // namespace Frontier

#endif // FRONTIER_EDITOR
