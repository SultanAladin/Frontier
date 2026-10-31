/*==========================================================================================================================================
                                             ENVIRONMENT REALM ENSEMBLE DEFINITION
==========================================================================================================================================*/
// 🧩 Multi-realm ray portal routing and 16-byte float4 GPU storage buffer serialization.

#include "Engine/Rendering/EnvironmentRealmEnsemble.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{

inline float Dot3(std::array<float, 3> A, std::array<float, 3> B) noexcept
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

inline std::array<float, 3> Normalize3(std::array<float, 3> V) noexcept
{
    const float L2 = Dot3(V, V);
    if (L2 <= 1.0e-20f)
    {
        return {0.0f, 1.0f, 0.0f};
    }
    const float Inv = 1.0f / std::sqrt(L2);
    return {V[0] * Inv, V[1] * Inv, V[2] * Inv};
}

inline std::array<float, 3> FromVec3(Vec3 V) noexcept
{
    return {V.X, V.Y, V.Z};
}

inline std::array<float, 3> FromSpatial(SpatialVector V) noexcept
{
    return {static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z)};
}

bool RaySphereNearestHit(std::array<float, 3> O, std::array<float, 3> D, std::array<float, 3> C, float R, float& OutT) noexcept
{
    const std::array<float, 3> Oc  = {O[0] - C[0], O[1] - C[1], O[2] - C[2]};
    const float                B   = Dot3(Oc, D);
    const float                C0  = Dot3(Oc, Oc) - R * R;
    const float                Det = B * B - C0;
    if (Det < 0.0f)
    {
        return false;
    }
    const float SqrtD = std::sqrt(Det);
    const float T0    = -B - SqrtD;
    const float T1    = -B + SqrtD;
    if (T0 > 1.0e-3f)
    {
        OutT = T0;
        return true;
    }
    if (T1 > 1.0e-3f)
    {
        OutT = T1;
        return true;
    }
    return false;
}

float NearestRealmSolidDistance(const EnvironmentRealm& Realm, std::array<float, 3> O, std::array<float, 3> D) noexcept
{
    float BestT = 1.0e30f;
    if (D[1] < -1.0e-5f)
    {
        const float Tg = (Realm.GroundPlaneY - O[1]) / D[1];
        if (Tg > 1.0e-3f)
        {
            BestT = std::min(BestT, Tg);
        }
    }
    for (const RealmSphereObstacle& Sph : Realm.SolidOccluders)
    {
        float Ts = 0.0f;
        if (RaySphereNearestHit(O, D, Sph.CenterMeters, Sph.RadiusMeters, Ts))
        {
            BestT = std::min(BestT, Ts);
        }
    }
    return BestT;
}

} // namespace

RoutedRealmRay RouteRayAcrossPortals(const EnvironmentRealmEnsemble& Ensemble,
                                     uint32_t                        StartRealmIndex,
                                     std::array<float, 3>            RayOriginMeters,
                                     std::array<float, 3>            RayDirectionUnit) noexcept
{
    RoutedRealmRay Out{};
    if (Ensemble.Realms.empty())
    {
        Out.RayOriginMeters  = RayOriginMeters;
        Out.RayDirectionUnit = Normalize3(RayDirectionUnit);
        return Out;
    }

    const uint32_t StartIdx = std::min<uint32_t>(StartRealmIndex, static_cast<uint32_t>(Ensemble.Realms.size() - 1u));
    const auto     Dir      = Normalize3(RayDirectionUnit);
    Out.RealmIndex          = StartIdx;
    Out.RayOriginMeters     = RayOriginMeters;
    Out.RayDirectionUnit    = Dir;

    const float NearestSolidT = NearestRealmSolidDistance(Ensemble.Realms[StartIdx], RayOriginMeters, Dir);
    float       BestPortalT   = NearestSolidT;

    for (size_t PIdx = 0u; PIdx < Ensemble.Portals.size(); ++PIdx)
    {
        const PortalAperture& P = Ensemble.Portals[PIdx];
        if (P.SourceRealmIndex != StartIdx || P.TargetRealmIndex >= Ensemble.Realms.size())
        {
            continue;
        }
        const float Denom = Dot3(Dir, P.NormalUnit);
        if (std::abs(Denom) <= 1.0e-5f)
        {
            continue;
        }
        const std::array<float, 3> Delta = {
            P.CenterMeters[0] - RayOriginMeters[0],
            P.CenterMeters[1] - RayOriginMeters[1],
            P.CenterMeters[2] - RayOriginMeters[2],
        };
        const float T = Dot3(Delta, P.NormalUnit) / Denom;
        if (T <= 1.0e-3f || T >= BestPortalT)
        {
            continue;
        }

        const std::array<float, 3> Hit = {
            RayOriginMeters[0] + Dir[0] * T,
            RayOriginMeters[1] + Dir[1] * T,
            RayOriginMeters[2] + Dir[2] * T,
        };
        const std::array<float, 3> Rel = {
            Hit[0] - P.CenterMeters[0],
            Hit[1] - P.CenterMeters[1],
            Hit[2] - P.CenterMeters[2],
        };
        const float LocalX = Dot3(Rel, P.RightUnit);
        const float LocalY = Dot3(Rel, P.UpUnit);

        const float OuterW = P.HalfWidthMeters + P.FrameWidthMeters;
        const float OuterH = P.HalfHeightMeters + P.FrameWidthMeters;
        if (std::abs(LocalX) > OuterW || std::abs(LocalY) > OuterH)
        {
            continue;
        }

        BestPortalT         = T;
        Out.PortalHitMeters = T;

        if (std::abs(LocalX) > P.HalfWidthMeters || std::abs(LocalY) > P.HalfHeightMeters)
        {
            Out.RealmIndex       = StartIdx;
            Out.PortalIndex      = static_cast<int32_t>(PIdx);
            Out.HitPortalFrame   = true;
            Out.FrameAlbedoRgb   = P.FrameAlbedoRgb;
            Out.RayOriginMeters  = RayOriginMeters;
            Out.RayDirectionUnit = Dir;
        }
        else
        {
            const std::array<float, 3> InwardNormal = {-P.NormalUnit[0], -P.NormalUnit[1], -P.NormalUnit[2]};
            const float                DirRight     = Dot3(Dir, P.RightUnit);
            const float                DirUp        = Dot3(Dir, P.UpUnit);
            const float                DirForward   = Dot3(Dir, InwardNormal);

            Out.RealmIndex     = P.TargetRealmIndex;
            Out.PortalIndex    = static_cast<int32_t>(PIdx);
            Out.HitPortalFrame = false;
            Out.RayOriginMeters = {
                P.TargetOriginMeters[0] + P.TargetRightUnit[0] * LocalX + P.TargetUpUnit[0] * LocalY,
                P.TargetOriginMeters[1] + P.TargetRightUnit[1] * LocalX + P.TargetUpUnit[1] * LocalY,
                P.TargetOriginMeters[2] + P.TargetRightUnit[2] * LocalX + P.TargetUpUnit[2] * LocalY,
            };
            Out.RayDirectionUnit = Normalize3({
                P.TargetRightUnit[0] * DirRight + P.TargetUpUnit[0] * DirUp + P.TargetForwardUnit[0] * DirForward,
                P.TargetRightUnit[1] * DirRight + P.TargetUpUnit[1] * DirUp + P.TargetForwardUnit[1] * DirForward,
                P.TargetRightUnit[2] * DirRight + P.TargetUpUnit[2] * DirUp + P.TargetForwardUnit[2] * DirForward,
            });
        }
    }

    return Out;
}

bool ProjectDirectionToScreenUv(const EnvironmentCameraView& View,
                                std::array<float, 3>         WorldDirectionUnit,
                                std::array<float, 2>&        OutUv) noexcept
{
    const auto  Dir  = Normalize3(WorldDirectionUnit);
    const float CamZ = Dot3(Dir, View.ForwardUnit);
    if (CamZ <= 1.0e-4f)
    {
        return false;
    }
    const float TanHalfY = std::tan(0.5f * View.VerticalFovRad);
    const float Aspect   = static_cast<float>(std::max(1u, View.ViewportWidth)) /
                         static_cast<float>(std::max(1u, View.ViewportHeight));
    const float CamX     = Dot3(Dir, View.RightUnit);
    const float CamY     = Dot3(Dir, View.UpUnit);
    const float NdcX     = (CamX / CamZ) / std::max(Aspect * TanHalfY, 1.0e-5f);
    const float NdcY     = (CamY / CamZ) / std::max(TanHalfY, 1.0e-5f);
    OutUv[0]             = 0.5f + 0.5f * NdcX;
    OutUv[1]             = 0.5f - 0.5f * NdcY;
    return true;
}

PackedEnsembleGpuPayload PackEnsembleGpuPayload(const EnvironmentRealmEnsemble& Ensemble,
                                                const EnvironmentCameraView&    View,
                                                uint32_t                        HipparcosStarCount)
{
    PackedEnsembleGpuPayload Out{};
    auto&                    Slots = Out.Float4Slots;
    Slots.resize(kEnsembleHeaderFloat4Count, {0.0f, 0.0f, 0.0f, 0.0f});

    const uint32_t RealmCount  = static_cast<uint32_t>(Ensemble.Realms.size());
    const uint32_t PortalCount = static_cast<uint32_t>(Ensemble.Portals.size());

    std::vector<std::array<float, 4>> SunSlots;
    std::vector<std::array<float, 4>> MoonSlots;
    std::vector<std::array<float, 4>> PlanetSlots;
    std::vector<std::array<float, 4>> CloudSlots;
    std::vector<std::array<float, 4>> FogBoxSlots;
    std::vector<std::array<float, 4>> LuminaireSlots;
    std::vector<std::array<float, 4>> LightningSlots;
    std::vector<std::array<float, 4>> FacetSlots;
    std::vector<std::array<float, 4>> SphereSlots;
    std::vector<std::array<float, 4>> GlassSlots;
    std::vector<std::array<float, 4>> PortalSlots;
    std::vector<std::array<float, 4>> FlareSourceSlots;
    std::vector<std::array<float, 4>> CustomStarSlots;
    std::vector<std::array<float, 4>> RealmSlots;
    RealmSlots.reserve(RealmCount * kRealmRecordFloat4Stride);

    std::vector<uint32_t> RealmFirstSunGlobalSlot(RealmCount, 0u);

    for (uint32_t RIdx = 0u; RIdx < RealmCount; ++RIdx)
    {
        const EnvironmentRealm& R = Ensemble.Realms[RIdx];

        const uint32_t SunFirst     = static_cast<uint32_t>(SunSlots.size() / kSunRecordFloat4Stride);
        RealmFirstSunGlobalSlot[RIdx] = SunFirst;
        for (const RealmSolarSource& S : R.Suns)
        {
            const auto           Dir = Normalize3(S.DirectionUnit);
            std::array<float, 2> Uv{0.5f, 0.5f};
            (void)ProjectDirectionToScreenUv(View, Dir, Uv);

            SunSlots.push_back({Dir[0], Dir[1], Dir[2], static_cast<float>(S.Disc.AngularRadiusRad)});
            SunSlots.push_back({S.DiscIrradiance[0],
                                S.DiscIrradiance[1],
                                S.DiscIrradiance[2],
                                static_cast<float>(S.Disc.LimbLinear)});
            SunSlots.push_back({Uv[0], Uv[1], S.MasterFlareGain, static_cast<float>(S.Flare.GhostStrength)});
            SunSlots.push_back({S.ChromaticShift,
                                static_cast<float>(S.Flare.HaloStrength),
                                S.HaloRadiusNdc,
                                S.HaloWidthNdc});
            SunSlots.push_back({static_cast<float>(S.Flare.StreakStrength),
                                static_cast<float>(S.Flare.AnamorphicLengthNdc),
                                static_cast<float>(S.Flare.StreakAngleRad),
                                static_cast<float>(S.Flare.AnamorphicStrength)});
            SunSlots.push_back({static_cast<float>(S.Flare.StarburstStrength),
                                static_cast<float>(S.Flare.ApertureBlades),
                                S.ApertureRoundness,
                                static_cast<float>(S.Flare.VeilingStrength)});
        }

        const uint32_t MoonFirst = static_cast<uint32_t>(MoonSlots.size() / kMoonRecordFloat4Stride);
        for (const LunarBodyDescriptor& M : R.Moons)
        {
            const auto Dir = Normalize3(FromVec3(M.DirectionUnit));
            MoonSlots.push_back({Dir[0], Dir[1], Dir[2], M.AngularRadiusRad});
            MoonSlots.push_back({1.0f, 1.0f, 1.0f, 0.24f * M.SurfaceBrightness});
            MoonSlots.push_back({M.EarthshineStrength, 0.8f, M.SpinLongitudeRad, M.LibrationLatitudeRad});
            MoonSlots.push_back({static_cast<float>(static_cast<uint32_t>(M.Preset)), M.SurfaceBrightness, 1.0f, 0.0f});
        }

        const uint32_t PlanetFirst = static_cast<uint32_t>(PlanetSlots.size() / kPlanetRecordFloat4Stride);
        for (const PlanetaryBodyDescriptor& P : R.Planets)
        {
            const auto Dir  = Normalize3(FromVec3(P.DirectionUnit));
            const auto Axis = Normalize3(FromVec3(P.SpinAxisUnit));
            std::array<float, 3> SurfTint = {0.90f, 0.80f, 0.64f};
            if (P.Archetype == PlanetarySurfaceArchetype::OceanicTerrestrial)
            {
                SurfTint = {0.16f, 0.44f, 0.86f};
            }
            else if (P.Archetype == PlanetarySurfaceArchetype::RustyDesertWorld)
            {
                SurfTint = {0.82f, 0.42f, 0.26f};
            }
            else if (P.Archetype == PlanetarySurfaceArchetype::MethaneIceGiant)
            {
                SurfTint = {0.32f, 0.78f, 0.94f};
            }

            const std::array<float, 3> AtmoTint = {0.42f, 0.68f, 1.0f};
            const float RingOpacity = P.Rings.Enabled ? std::clamp(1.0f - std::exp(-P.Rings.PeakOpticalDepth), 0.0f, 0.96f) : 0.0f;

            PlanetSlots.push_back({Dir[0], Dir[1], Dir[2], P.AngularRadiusRad});
            PlanetSlots.push_back({Axis[0], Axis[1], Axis[2], P.SpinPhaseRad});
            PlanetSlots.push_back({SurfTint[0], SurfTint[1], SurfTint[2], 0.55f * P.SurfaceAlbedoScale});
            PlanetSlots.push_back({AtmoTint[0],
                                   AtmoTint[1],
                                   AtmoTint[2],
                                   P.Atmosphere.Enabled ? P.Atmosphere.ShellHeightFraction : 0.0f});
            PlanetSlots.push_back({1.6f,
                                   static_cast<float>(static_cast<uint32_t>(P.Archetype)),
                                   P.Rings.Enabled ? P.Rings.InnerRadiusRatio : 0.0f,
                                   P.Rings.Enabled ? P.Rings.OuterRadiusRatio : 0.0f});
            PlanetSlots.push_back({RingOpacity,
                                   P.Rings.OuterTintRgb.X,
                                   P.Rings.OuterTintRgb.Y,
                                   P.Rings.OuterTintRgb.Z});
        }

        const uint32_t CloudFirst = static_cast<uint32_t>(CloudSlots.size() / kCloudRecordFloat4Stride);
        for (const CloudVolumeProfile& C : R.CloudVolumes)
        {
            const float LowerAlt  = static_cast<float>(C.Slab.BaseHeightM);
            const float Thickness = static_cast<float>(std::max(0.0, C.Slab.TopHeightM - C.Slab.BaseHeightM));
            const auto  BoxCenter = FromSpatial(C.BoxCenterM);
            const auto  BoxHalf   = FromSpatial(C.BoxHalfExtentsM);

            CloudSlots.push_back({LowerAlt,
                                  Thickness,
                                  static_cast<float>(C.Slab.Coverage),
                                  static_cast<float>(C.Slab.DensityScale)});
            CloudSlots.push_back({static_cast<float>(C.WorleyErosionStrength),
                                  static_cast<float>(C.Slab.ExtinctionPerM),
                                  0.92f,
                                  static_cast<float>(C.Slab.PhaseG)});
            CloudSlots.push_back({static_cast<float>(C.BackscatterPhaseG),
                                  0.72f,
                                  static_cast<float>(C.PowderStrength),
                                  C.Domain == CloudVolumeDomain::BoundedLocalBox ? 1.0f : 0.0f});
            CloudSlots.push_back({BoxCenter[0], BoxCenter[1], BoxCenter[2], static_cast<float>(C.BoxEdgeFalloffM)});
            CloudSlots.push_back({BoxHalf[0],
                                  BoxHalf[1],
                                  BoxHalf[2],
                                  static_cast<float>(C.Slab.WindXMps) * R.CloudWindSeconds});
        }

        const uint32_t FogBoxFirst = static_cast<uint32_t>(FogBoxSlots.size() / kFogBoxRecordFloat4Stride);
        for (const LocalFogVolumeProfile& F : R.LocalFogVolumes)
        {
            const auto Cent = FromSpatial(F.BoxCenterM);
            const auto Half = FromSpatial(F.BoxHalfExtentsM);
            const float Freq = (F.NoiseScaleM > 1.0e-3) ? static_cast<float>(1.0 / F.NoiseScaleM) : 0.1f;
            FogBoxSlots.push_back({Cent[0], Cent[1], Cent[2], static_cast<float>(F.ExtinctionPerM)});
            FogBoxSlots.push_back({Half[0], Half[1], Half[2], static_cast<float>(F.ScatteringAlbedo)});
            FogBoxSlots.push_back({static_cast<float>(F.NoiseModulation),
                                   Freq,
                                   static_cast<float>(F.PhaseAsymmetryG),
                                   static_cast<float>(F.BoxEdgeFalloffM)});
        }

        const uint32_t LumFirst = static_cast<uint32_t>(LuminaireSlots.size() / kLuminaireRecordFloat4Stride);
        for (const PhotometricFogLuminaire& L : R.Luminaires)
        {
            const auto Pos   = FromSpatial(L.PositionM);
            const auto Ax    = Normalize3(FromSpatial(L.AxisUnit));
            const float Peak = static_cast<float>(std::max({L.PeakIntensity.R, L.PeakIntensity.G, L.PeakIntensity.B, 1.0e-4}));
            LuminaireSlots.push_back({Pos[0], Pos[1], Pos[2], Peak});
            LuminaireSlots.push_back({Ax[0], Ax[1], Ax[2], static_cast<float>(L.OuterConeCos)});
            LuminaireSlots.push_back({static_cast<float>(L.PeakIntensity.R) / Peak,
                                      static_cast<float>(L.PeakIntensity.G) / Peak,
                                      static_cast<float>(L.PeakIntensity.B) / Peak,
                                      static_cast<float>(L.RangeMeters)});
        }

        const uint32_t BoltFirst = static_cast<uint32_t>(LightningSlots.size() / kLightningRecordFloat4Stride);
        for (const LightningFogChannel& B : R.LightningChannels)
        {
            const auto A    = FromSpatial(B.SegmentStartM);
            const auto E    = FromSpatial(B.SegmentEndM);
            const float Pk  = static_cast<float>(std::max({B.LineRadiance.R, B.LineRadiance.G, B.LineRadiance.B, 1.0e-4}));
            LightningSlots.push_back({A[0], A[1], A[2], Pk});
            LightningSlots.push_back({E[0], E[1], E[2], static_cast<float>(B.CoreRadiusM)});
            LightningSlots.push_back({static_cast<float>(B.LineRadiance.R) / Pk,
                                      static_cast<float>(B.LineRadiance.G) / Pk,
                                      static_cast<float>(B.LineRadiance.B) / Pk,
                                      0.65f});
        }

        const uint32_t FacetFirst = static_cast<uint32_t>(FacetSlots.size() / kFacetRecordFloat4Stride);
        for (const RadiantFogFacet& Fc : R.RadiantFacets)
        {
            const auto V0 = FromSpatial(Fc.CornerA);
            const auto V1 = FromSpatial(Fc.CornerB);
            const auto V2 = FromSpatial(Fc.CornerC);
            const std::array<float, 3> Cent = {
                (V0[0] + V1[0] + V2[0]) / 3.0f,
                (V0[1] + V1[1] + V2[1]) / 3.0f,
                (V0[2] + V1[2] + V2[2]) / 3.0f,
            };
            const std::array<float, 3> E1 = {V1[0] - V0[0], V1[1] - V0[1], V1[2] - V0[2]};
            const std::array<float, 3> E2 = {V2[0] - V0[0], V2[1] - V0[1], V2[2] - V0[2]};
            const std::array<float, 3> Cr = {
                E1[1] * E2[2] - E1[2] * E2[1],
                E1[2] * E2[0] - E1[0] * E2[2],
                E1[0] * E2[1] - E1[1] * E2[0],
            };
            const float Area = 0.5f * std::sqrt(Dot3(Cr, Cr));
            const auto  Nrm  = Normalize3(Cr);
            FacetSlots.push_back({Cent[0], Cent[1], Cent[2], Area});
            FacetSlots.push_back({Nrm[0], Nrm[1], Nrm[2], 16.0f});
            FacetSlots.push_back({static_cast<float>(Fc.SurfaceRadiance.R),
                                  static_cast<float>(Fc.SurfaceRadiance.G),
                                  static_cast<float>(Fc.SurfaceRadiance.B),
                                  1.0f});
        }

        const uint32_t SphFirst = static_cast<uint32_t>(SphereSlots.size() / kSphereRecordFloat4Stride);
        for (const RealmSphereObstacle& Sp : R.SolidOccluders)
        {
            SphereSlots.push_back({Sp.CenterMeters[0], Sp.CenterMeters[1], Sp.CenterMeters[2], Sp.RadiusMeters});
            SphereSlots.push_back({Sp.AlbedoRgb[0], Sp.AlbedoRgb[1], Sp.AlbedoRgb[2], Sp.Roughness});
        }

        const uint32_t GlsFirst = static_cast<uint32_t>(GlassSlots.size() / kGlassRecordFloat4Stride);
        for (const RealmGlassPane& Gl : R.GlassPanes)
        {
            const auto Nrm = Normalize3(Gl.NormalUnit);
            GlassSlots.push_back({Gl.CenterMeters[0], Gl.CenterMeters[1], Gl.CenterMeters[2], Gl.HalfWidthMeters});
            GlassSlots.push_back({Nrm[0], Nrm[1], Nrm[2], Gl.HalfHeightMeters});
            GlassSlots.push_back({Gl.TintTransmittance[0], Gl.TintTransmittance[1], Gl.TintTransmittance[2], Gl.FresnelF0});
        }

        const uint32_t CStarFirst = static_cast<uint32_t>(CustomStarSlots.size() / kCustomStarRecordFloat4Stride);
        for (const CustomStarDescriptor& Cs : R.CustomStars)
        {
            const auto Dir  = Normalize3(FromVec3(Cs.DirectionUnit));
            const Vec3 Tint = CelestialDomeModel::KelvinToBlackbodyChromaticity(Cs.TemperatureKelvin);
            const float Mag = -2.5f * std::log10(std::max(Cs.RadianceGain * 0.1f, 1.0e-4f));
            CustomStarSlots.push_back({Dir[0], Dir[1], Dir[2], Mag});
            CustomStarSlots.push_back({Tint.X, Tint.Y, Tint.Z, Cs.CoronaGlowStrength});
        }

        const AtmosphereParameters& A      = R.Atmosphere;
        const float                 Falloff = (R.HeightFog.FalloffHeightM > 1.0e-3)
                                                  ? static_cast<float>(1.0 / R.HeightFog.FalloffHeightM)
                                                  : 0.04f;
        RealmSlots.push_back({static_cast<float>(A.GroundRadiusKm),
                              static_cast<float>(A.TopRadiusKm),
                              R.ObserverAltitudeKm,
                              static_cast<float>(A.GroundAlbedo)});
        RealmSlots.push_back({static_cast<float>(A.RayleighScattering.R),
                              static_cast<float>(A.RayleighScattering.G),
                              static_cast<float>(A.RayleighScattering.B),
                              static_cast<float>(A.RayleighScaleHeightKm)});
        RealmSlots.push_back({static_cast<float>(A.MieScattering),
                              static_cast<float>(A.MieScattering + A.MieAbsorption),
                              static_cast<float>(A.MieScaleHeightKm),
                              static_cast<float>(A.MiePhaseG)});
        RealmSlots.push_back({static_cast<float>(A.OzoneAbsorption.R),
                              static_cast<float>(A.OzoneAbsorption.G),
                              static_cast<float>(A.OzoneAbsorption.B),
                              static_cast<float>(A.OzonePeakKm)});
        RealmSlots.push_back({static_cast<float>(A.OzoneWidthKm),
                              R.GroundPlaneY,
                              R.GroundAlbedoRgb[0],
                              R.GroundAlbedoRgb[1]});
        RealmSlots.push_back({R.GroundAlbedoRgb[2],
                              R.StarBrightnessScale,
                              R.ScintillationStrength,
                              R.MilkyWayIntensity});
        RealmSlots.push_back({static_cast<float>(R.HeightFog.DensityAtBase),
                              Falloff,
                              static_cast<float>(R.HeightFog.BaseHeightM),
                              0.92f});
        RealmSlots.push_back({0.0f,
                              R.HeightFogPhaseG,
                              R.HeightFogScatterAlbedo,
                              R.EnableAerialPerspective ? 1.0f : 0.0f});
        RealmSlots.push_back({static_cast<float>(SunFirst),
                              static_cast<float>(R.Suns.size()),
                              static_cast<float>(MoonFirst),
                              static_cast<float>(R.Moons.size())});
        RealmSlots.push_back({static_cast<float>(PlanetFirst),
                              static_cast<float>(R.Planets.size()),
                              static_cast<float>(CloudFirst),
                              static_cast<float>(R.CloudVolumes.size())});
        RealmSlots.push_back({static_cast<float>(FogBoxFirst),
                              static_cast<float>(R.LocalFogVolumes.size()),
                              static_cast<float>(LumFirst),
                              static_cast<float>(R.Luminaires.size())});
        RealmSlots.push_back({static_cast<float>(BoltFirst),
                              static_cast<float>(R.LightningChannels.size()),
                              static_cast<float>(SphFirst),
                              static_cast<float>(R.SolidOccluders.size())});
        RealmSlots.push_back({static_cast<float>(GlsFirst),
                              static_cast<float>(R.GlassPanes.size()),
                              static_cast<float>(FacetFirst),
                              static_cast<float>(R.RadiantFacets.size())});
        RealmSlots.push_back({static_cast<float>(CStarFirst),
                              static_cast<float>(R.CustomStars.size()),
                              R.CloudShadowHalfSpanM,
                              R.CloudWindSeconds});
        RealmSlots.push_back({R.StarBasisRow0[0], R.StarBasisRow0[1], R.StarBasisRow0[2], R.TwilightStrength});
        RealmSlots.push_back({R.StarBasisRow1[0], R.StarBasisRow1[1], R.StarBasisRow1[2], R.StarLimitingMagnitude});
    }

    for (const PortalAperture& P : Ensemble.Portals)
    {
        PortalSlots.push_back({static_cast<float>(P.SourceRealmIndex),
                               static_cast<float>(P.TargetRealmIndex),
                               P.HalfWidthMeters,
                               P.HalfHeightMeters});
        PortalSlots.push_back({P.CenterMeters[0], P.CenterMeters[1], P.CenterMeters[2], P.FrameWidthMeters});
        PortalSlots.push_back({P.NormalUnit[0], P.NormalUnit[1], P.NormalUnit[2], P.FrameAlbedoRgb[0]});
        PortalSlots.push_back({P.RightUnit[0], P.RightUnit[1], P.RightUnit[2], P.FrameAlbedoRgb[1]});
        PortalSlots.push_back({P.UpUnit[0], P.UpUnit[1], P.UpUnit[2], P.FrameAlbedoRgb[2]});
        PortalSlots.push_back({P.TargetOriginMeters[0], P.TargetOriginMeters[1], P.TargetOriginMeters[2], 0.0f});
        PortalSlots.push_back({P.TargetRightUnit[0], P.TargetRightUnit[1], P.TargetRightUnit[2], 0.0f});
        PortalSlots.push_back({P.TargetUpUnit[0], P.TargetUpUnit[1], P.TargetUpUnit[2], 0.0f});
        PortalSlots.push_back({P.TargetForwardUnit[0], P.TargetForwardUnit[1], P.TargetForwardUnit[2], 0.0f});
    }

    if (View.ActiveRealmIndex < RealmCount)
    {
        const EnvironmentRealm& ActiveR = Ensemble.Realms[View.ActiveRealmIndex];
        for (uint32_t SIdx = 0u; SIdx < ActiveR.Suns.size(); ++SIdx)
        {
            const RealmSolarSource& S = ActiveR.Suns[SIdx];
            if (!S.EnableLensFlare || S.MasterFlareGain <= 0.0f)
            {
                continue;
            }
            const auto           Dir = Normalize3(S.DirectionUnit);
            std::array<float, 2> Uv{0.5f, 0.5f};
            if (!ProjectDirectionToScreenUv(View, Dir, Uv))
            {
                continue;
            }
            const uint32_t GlobalSun = RealmFirstSunGlobalSlot[View.ActiveRealmIndex] + SIdx;
            Out.FlareCandidates.push_back({View.ActiveRealmIndex, SIdx, -1, GlobalSun, Dir, Uv, S});

            FlareSourceSlots.push_back({static_cast<float>(View.ActiveRealmIndex),
                                        static_cast<float>(SIdx),
                                        0.0f,
                                        static_cast<float>(GlobalSun)});
            FlareSourceSlots.push_back({Dir[0], Dir[1], Dir[2], static_cast<float>(S.Disc.AngularRadiusRad)});
            FlareSourceSlots.push_back({Uv[0], Uv[1], S.DiscIrradiance[0], S.DiscIrradiance[1]});
            FlareSourceSlots.push_back({S.DiscIrradiance[2], static_cast<float>(S.Disc.LimbLinear), 0.0f, 0.0f});
        }

        for (uint32_t PIdx = 0u; PIdx < PortalCount; ++PIdx)
        {
            const PortalAperture& P = Ensemble.Portals[PIdx];
            if (P.SourceRealmIndex != View.ActiveRealmIndex || P.TargetRealmIndex >= RealmCount)
            {
                continue;
            }
            const EnvironmentRealm&    TargetR      = Ensemble.Realms[P.TargetRealmIndex];
            const std::array<float, 3> InwardNormal = {-P.NormalUnit[0], -P.NormalUnit[1], -P.NormalUnit[2]};

            for (uint32_t SIdx = 0u; SIdx < TargetR.Suns.size(); ++SIdx)
            {
                const RealmSolarSource& S = TargetR.Suns[SIdx];
                if (!S.EnableLensFlare || S.MasterFlareGain <= 0.0f)
                {
                    continue;
                }
                const auto  TargetDir = Normalize3(S.DirectionUnit);
                const float Dr        = Dot3(TargetDir, P.TargetRightUnit);
                const float Du        = Dot3(TargetDir, P.TargetUpUnit);
                const float Df        = Dot3(TargetDir, P.TargetForwardUnit);
                if (Df <= 1.0e-3f)
                {
                    continue;
                }
                const auto CamWorldDir = Normalize3({
                    P.RightUnit[0] * Dr + P.UpUnit[0] * Du + InwardNormal[0] * Df,
                    P.RightUnit[1] * Dr + P.UpUnit[1] * Du + InwardNormal[1] * Df,
                    P.RightUnit[2] * Dr + P.UpUnit[2] * Du + InwardNormal[2] * Df,
                });
                std::array<float, 2> Uv{0.5f, 0.5f};
                if (!ProjectDirectionToScreenUv(View, CamWorldDir, Uv))
                {
                    continue;
                }
                const uint32_t GlobalSun = RealmFirstSunGlobalSlot[P.TargetRealmIndex] + SIdx;
                Out.FlareCandidates.push_back(
                    {P.TargetRealmIndex, SIdx, static_cast<int32_t>(PIdx), GlobalSun, CamWorldDir, Uv, S});

                FlareSourceSlots.push_back({static_cast<float>(P.TargetRealmIndex),
                                            static_cast<float>(SIdx),
                                            static_cast<float>(PIdx + 1u),
                                            static_cast<float>(GlobalSun)});
                FlareSourceSlots.push_back({CamWorldDir[0], CamWorldDir[1], CamWorldDir[2], static_cast<float>(S.Disc.AngularRadiusRad)});
                FlareSourceSlots.push_back({Uv[0], Uv[1], S.DiscIrradiance[0], S.DiscIrradiance[1]});
                FlareSourceSlots.push_back({S.DiscIrradiance[2], static_cast<float>(S.Disc.LimbLinear), 0.0f, 0.0f});
            }
        }
    }

    auto AppendTable = [&](const std::vector<std::array<float, 4>>& Table) -> uint32_t {
        const uint32_t Offset = static_cast<uint32_t>(Slots.size());
        Slots.insert(Slots.end(), Table.begin(), Table.end());
        return Offset;
    };

    const uint32_t RealmOffset       = AppendTable(RealmSlots);
    const uint32_t SunOffset         = AppendTable(SunSlots);
    const uint32_t MoonOffset        = AppendTable(MoonSlots);
    const uint32_t PlanetOffset      = AppendTable(PlanetSlots);
    const uint32_t CloudOffset       = AppendTable(CloudSlots);
    const uint32_t FogBoxOffset      = AppendTable(FogBoxSlots);
    const uint32_t LuminaireOffset   = AppendTable(LuminaireSlots);
    const uint32_t LightningOffset   = AppendTable(LightningSlots);
    const uint32_t FacetOffset       = AppendTable(FacetSlots);
    const uint32_t SphereOffset      = AppendTable(SphereSlots);
    const uint32_t GlassOffset       = AppendTable(GlassSlots);
    const uint32_t PortalOffset      = AppendTable(PortalSlots);
    const uint32_t FlareSourceOffset = AppendTable(FlareSourceSlots);
    const uint32_t CustomStarOffset  = AppendTable(CustomStarSlots);

    const float TanHalfY = std::tan(0.5f * View.VerticalFovRad);
    const float Aspect   = static_cast<float>(std::max(1u, View.ViewportWidth)) /
                         static_cast<float>(std::max(1u, View.ViewportHeight));

    Slots[0] = {static_cast<float>(RealmCount),
                static_cast<float>(PortalCount),
                static_cast<float>(Out.FlareCandidates.size()),
                static_cast<float>(View.ActiveRealmIndex)};
    Slots[1] = {View.PositionMeters[0], View.PositionMeters[1], View.PositionMeters[2], TanHalfY};
    Slots[2] = {View.RightUnit[0], View.RightUnit[1], View.RightUnit[2], Aspect};
    Slots[3] = {View.UpUnit[0], View.UpUnit[1], View.UpUnit[2], View.ElapsedSeconds};
    Slots[4] = {View.ForwardUnit[0], View.ForwardUnit[1], View.ForwardUnit[2], View.ExposureScale};
    Slots[5] = {static_cast<float>(RealmOffset),
                static_cast<float>(SunOffset),
                static_cast<float>(MoonOffset),
                static_cast<float>(PlanetOffset)};
    Slots[6] = {static_cast<float>(CloudOffset),
                static_cast<float>(FogBoxOffset),
                static_cast<float>(LuminaireOffset),
                static_cast<float>(LightningOffset)};
    Slots[7] = {static_cast<float>(FacetOffset),
                static_cast<float>(SphereOffset),
                static_cast<float>(GlassOffset),
                static_cast<float>(PortalOffset)};
    Slots[8] = {static_cast<float>(FlareSourceOffset),
                static_cast<float>(CustomStarOffset),
                static_cast<float>(HipparcosStarCount),
                static_cast<float>(View.FrameIndex)};
    Slots[9] = {static_cast<float>(View.ViewportWidth),
                static_cast<float>(View.ViewportHeight),
                0.0f,
                0.0f};

    return Out;
}

} // namespace Frontier
