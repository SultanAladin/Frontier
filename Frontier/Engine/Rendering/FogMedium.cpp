/*==========================================================================================================================================
                                                    FOG MEDIUM DEFINITION
==========================================================================================================================================*/

#include "FogMedium.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

// Integral of exp(-max(z(t) - base, 0)/H) dt along z(t) = Z0 + DZ*t, piecewise at the base crossing.
double ProfileIntegral(double Z0, double DZ, double T0, double T1, double Base, double H) noexcept
{
    if (T1 <= T0)
    {
        return 0.0;
    }
    const auto AboveIntegral = [&](double A, double B) {
        // Both endpoints at/above base (in the exponential region).
        const double ZA = Z0 + DZ * A;
        if (std::abs(DZ) < 1e-12)
        {
            return std::exp(-std::max(ZA - Base, 0.0) / H) * (B - A);
        }
        const double ZB = Z0 + DZ * B;
        const double Length = B - A, Ratio = std::abs(DZ) * Length / H;
        return std::exp(-(std::min(ZA, ZB) - Base) / H) * Length * (-std::expm1(-Ratio) / Ratio);
    };
    const double ZA = Z0 + DZ * T0;
    const double ZB = Z0 + DZ * T1;
    if (ZA <= Base && ZB <= Base)
    {
        return T1 - T0;   // full density throughout
    }
    if (ZA >= Base && ZB >= Base)
    {
        return AboveIntegral(T0, T1);
    }
    const double TCross = T0 + (Base - ZA) / DZ;
    if (ZA < Base)   // rising through the base
    {
        return (TCross - T0) + AboveIntegral(TCross, T1);
    }
    return AboveIntegral(T0, TCross) + (T1 - TCross);   // descending into it
}

} // namespace

double FogTransmittance(const FogParameters& P, SpatialVector Origin, SpatialVector Direction, double DistanceM) noexcept
{
    if (DistanceM <= 0.0 || P.DensityAtBase <= 0.0)
    {
        return 1.0;
    }
    const double H = std::max(P.FalloffHeightM, 1e-6);
    const double OpticalDepth = P.DensityAtBase * ProfileIntegral(Origin.Z, Direction.Z, 0.0, DistanceM, P.BaseHeightM, H);
    return std::exp(-std::max(OpticalDepth, 0.0));
}

SpectrumRgb ApplyFog(const FogParameters& P, SpectrumRgb Radiance, double Transmittance) noexcept
{
    const double T = std::clamp(Transmittance, 0.0, 1.0);
    return {Radiance.R * T + P.Colour.R * (1.0 - T), Radiance.G * T + P.Colour.G * (1.0 - T),
            Radiance.B * T + P.Colour.B * (1.0 - T)};
}

FogRadianceIntegral IntegrateFogRadiance(
    const FogParameters& Medium,
    SpatialVector Origin,
    SpatialVector Direction,
    double DistanceM,
    uint32_t Steps,
    double ScatteringAlbedo,
    const std::function<SpectrumRgb(SpatialVector)>& Incident)
{
    FogRadianceIntegral Result;
    if (DistanceM <= 0 || Steps == 0 || Medium.DensityAtBase <= 0)
        return Result;
    const double StepM = DistanceM / Steps, Albedo = std::clamp(ScatteringAlbedo, 0., 1.);
    for (uint32_t I = 0; I < Steps; ++I)
    {
        double EndT = FogTransmittance(Medium, Origin, Direction, StepM * (I + 1));
        SpatialVector At{Origin.X + Direction.X * StepM * (I + .5),
                         Origin.Y + Direction.Y * StepM * (I + .5),
                         Origin.Z + Direction.Z * StepM * (I + .5)};
        auto Light = Incident(At);
        double Weight = (Result.Transmittance - EndT) * Albedo;
        Result.Radiance.R += Weight * Light.R;
        Result.Radiance.G += Weight * Light.G;
        Result.Radiance.B += Weight * Light.B;
        Result.Transmittance = EndT;
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//              COUPLED HEIGHT FOG, AERIAL PERSPECTIVE & BOUNDED 3D NOISE-MODULATED VOLUMETRIC FOG
//------------------------------------------------------------------------------------------------------------------------

namespace
{

double FogDot(SpatialVector A, SpatialVector B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

double FogLen(SpatialVector V) noexcept
{
    return std::sqrt(FogDot(V, V));
}

SpatialVector FogSub(SpatialVector A, SpatialVector B) noexcept
{
    return {A.X - B.X, A.Y - B.Y, A.Z - B.Z};
}

SpatialVector FogCross(SpatialVector A, SpatialVector B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}

double FogSmooth(double T) noexcept
{
    const double C = std::clamp(T, 0.0, 1.0);
    return C * C * (3.0 - 2.0 * C);
}

double FogHashLattice(int32_t X, int32_t Y, int32_t Z, uint64_t Seed) noexcept
{
    uint64_t H = Seed + static_cast<uint64_t>(static_cast<uint32_t>(X)) * 0x9E3779B97F4A7C15ull;
    H ^= static_cast<uint64_t>(static_cast<uint32_t>(Y)) * 0xC2B2AE3D27D4EB4Full;
    H ^= static_cast<uint64_t>(static_cast<uint32_t>(Z)) * 0x165667B19E3779F9ull;
    H ^= H >> 29u;
    H *= 0xBF58476D1CE4E5B9ull;
    H ^= H >> 32u;
    return static_cast<double>(static_cast<uint32_t>(H >> 32u)) * (1.0 / 4294967296.0);
}

double FogNoise3D(SpatialVector P, uint64_t Seed) noexcept
{
    const int32_t X0 = static_cast<int32_t>(std::floor(P.X));
    const int32_t Y0 = static_cast<int32_t>(std::floor(P.Y));
    const int32_t Z0 = static_cast<int32_t>(std::floor(P.Z));
    const double  FX = FogSmooth(P.X - X0);
    const double  FY = FogSmooth(P.Y - Y0);
    const double  FZ = FogSmooth(P.Z - Z0);
    const auto    L  = [](double A, double B, double T) { return A + (B - A) * T; };
    const double C00 = L(FogHashLattice(X0, Y0, Z0, Seed), FogHashLattice(X0 + 1, Y0, Z0, Seed), FX);
    const double C10 = L(FogHashLattice(X0, Y0 + 1, Z0, Seed), FogHashLattice(X0 + 1, Y0 + 1, Z0, Seed), FX);
    const double C01 = L(FogHashLattice(X0, Y0, Z0 + 1, Seed), FogHashLattice(X0 + 1, Y0, Z0 + 1, Seed), FX);
    const double C11 = L(FogHashLattice(X0, Y0 + 1, Z0 + 1, Seed), FogHashLattice(X0 + 1, Y0 + 1, Z0 + 1, Seed), FX);
    return L(L(C00, C10, FY), L(C01, C11, FY), FZ);
}

double FogFbm3D(SpatialVector P, uint64_t Seed) noexcept
{
    double Sum = 0.0, Amp = 0.5, Total = 0.0;
    for (uint32_t Oct = 0; Oct < 3; ++Oct)
    {
        Sum += Amp * FogNoise3D(P, Seed + Oct * 97u);
        Total += Amp;
        P = {P.X * 2.05 + 5.3, P.Y * 2.05 + 3.1, P.Z * 2.05 + 7.9};
        Amp *= 0.5;
    }
    return Sum / Total;
}

double SampleIesRing(const PhotometricFogLuminaire& Lum, SpatialVector LightToPointUnit) noexcept
{
    const double AxisLen = FogLen(Lum.AxisUnit);
    if (AxisLen <= 1e-9)
    {
        return 1.0;
    }
    const SpatialVector Axis{Lum.AxisUnit.X / AxisLen, Lum.AxisUnit.Y / AxisLen, Lum.AxisUnit.Z / AxisLen};
    const double        CosAngle = std::clamp(FogDot(Axis, LightToPointUnit), -1.0, 1.0);
    if (CosAngle < Lum.OuterConeCos)
    {
        return 0.0;
    }
    const double ConeSmooth = (Lum.InnerConeCos > Lum.OuterConeCos)
                                  ? FogSmooth((CosAngle - Lum.OuterConeCos) / (Lum.InnerConeCos - Lum.OuterConeCos))
                                  : 1.0;
    // Map CosAngle in [1, -1] -> polar table index [0, 7]
    const double   RingCoord = std::clamp((1.0 - CosAngle) * 0.5 * 7.0, 0.0, 7.0);
    const uint32_t I0        = std::min(6u, static_cast<uint32_t>(RingCoord));
    const double   T         = RingCoord - static_cast<double>(I0);
    const double   IesWeight = static_cast<double>(Lum.IesPolarWeights[I0]) * (1.0 - T) +
                               static_cast<double>(Lum.IesPolarWeights[I0 + 1]) * T;
    return ConeSmooth * std::max(0.0, IesWeight);
}

} // namespace

double EvaluateLocalFogExtinction(const LocalFogVolumeProfile& Volume, SpatialVector PointMeters) noexcept
{
    if (Volume.ExtinctionPerM <= 0.0)
    {
        return 0.0;
    }
    const double Dx = std::abs(PointMeters.X - Volume.BoxCenterM.X);
    const double Dy = std::abs(PointMeters.Y - Volume.BoxCenterM.Y);
    const double Dz = std::abs(PointMeters.Z - Volume.BoxCenterM.Z);
    if (Dx > Volume.BoxHalfExtentsM.X || Dy > Volume.BoxHalfExtentsM.Y || Dz > Volume.BoxHalfExtentsM.Z)
    {
        return 0.0;
    }
    const double Soft = std::max(Volume.BoxEdgeFalloffM, 0.25);
    const double Fx   = FogSmooth((Volume.BoxHalfExtentsM.X - Dx) / Soft);
    const double Fy   = FogSmooth((Volume.BoxHalfExtentsM.Y - Dy) / Soft);
    const double Fz   = FogSmooth((Volume.BoxHalfExtentsM.Z - Dz) / Soft);

    const double BaseZ      = Volume.BoxCenterM.Z - Volume.BoxHalfExtentsM.Z;
    const double HeightTerm = std::exp(-std::max(PointMeters.Z - BaseZ, 0.0) / std::max(Volume.HeightFalloffM, 0.5));

    double NoiseFactor = 1.0;
    if (Volume.NoiseModulation > 0.0)
    {
        const double Scale = std::max(Volume.NoiseScaleM, 0.5);
        const SpatialVector Advected{
            (PointMeters.X + Volume.WindVelocityMps.X * Volume.WindTimeS) / Scale,
            (PointMeters.Y + Volume.WindVelocityMps.Y * Volume.WindTimeS) / Scale,
            (PointMeters.Z + Volume.WindVelocityMps.Z * Volume.WindTimeS) / Scale};
        const double N = FogFbm3D(Advected, Volume.Seed);
        NoiseFactor    = std::max(0.0, (1.0 - Volume.NoiseModulation) + 1.85 * Volume.NoiseModulation * N);
    }
    return Volume.ExtinctionPerM * Fx * Fy * Fz * HeightTerm * NoiseFactor;
}

FogRadianceIntegral EvaluateUnifiedVolumetricFog(
    const UnifiedVolumetricFogSpecification& Specification,
    SpatialVector                            OriginMeters,
    SpatialVector                            DirectionUnit,
    double                                   SurfaceDistanceMeters,
    const std::function<bool(SpatialVector, SpatialVector, double)>& IsOccluded) noexcept
{
    FogRadianceIntegral Result;
    const double DirLen = FogLen(DirectionUnit);
    if (SurfaceDistanceMeters <= 0.0 || DirLen <= 1e-9)
    {
        return Result;
    }
    const SpatialVector Dir{DirectionUnit.X / DirLen, DirectionUnit.Y / DirLen, DirectionUnit.Z / DirLen};
    const double        MarchSpanM = std::min(SurfaceDistanceMeters, std::max(1.0, Specification.MaxFogMarchDistanceM));
    const uint32_t      Steps      = std::max(1u, Specification.FroxelSteps);
    // Froxel quadratic depth slicing concentrates samples near the camera while covering MarchSpanM!
    double      MediumTransmittance = 1.0;
    SpectrumRgb MediumRadiance{};

    for (uint32_t Step = 0; Step < Steps; ++Step)
    {
        const double U0    = static_cast<double>(Step) / static_cast<double>(Steps);
        const double U1    = static_cast<double>(Step + 1) / static_cast<double>(Steps);
        const double T0    = MarchSpanM * (0.35 * U0 + 0.65 * U0 * U0);
        const double T1    = MarchSpanM * (0.35 * U1 + 0.65 * U1 * U1);
        const double Dt    = T1 - T0;
        const double MidT  = 0.5 * (T0 + T1);
        const SpatialVector At{OriginMeters.X + Dir.X * MidT,
                               OriginMeters.Y + Dir.Y * MidT,
                               OriginMeters.Z + Dir.Z * MidT};

        // 1. Exact analytic segment transmittance for global exponential height fog over [T0, T1]
        const SpatialVector SegStart{OriginMeters.X + Dir.X * T0, OriginMeters.Y + Dir.Y * T0, OriginMeters.Z + Dir.Z * T0};
        const double HeightSegT = Specification.HeightFog.DensityAtBase > 0.0
                                      ? FogTransmittance(Specification.HeightFog, SegStart, Dir, Dt)
                                      : 1.0;
        const double HeightTau  = -std::log(std::max(HeightSegT, 1e-12));

        // 2. Bounded 3D noise-modulated local volumetric fog volumes at At
        double      LocalTau      = 0.0;
        double      LocalAlbedo   = Specification.HeightFogAlbedo;
        double      LocalPhaseG   = Specification.HeightFogPhaseG;
        SpectrumRgb LocalTint     = {1.0, 1.0, 1.0};
        double      PeakLocalExt  = 0.0;

        for (const LocalFogVolumeProfile& Vol : Specification.LocalVolumes)
        {
            const double Ext = EvaluateLocalFogExtinction(Vol, At);
            if (Ext <= 0.0)
            {
                continue;
            }
            LocalTau += Ext * Dt;
            if (Ext > PeakLocalExt)
            {
                PeakLocalExt = Ext;
                LocalAlbedo  = Vol.ScatteringAlbedo;
                LocalPhaseG  = Vol.PhaseAsymmetryG;
                LocalTint    = Vol.ScatteringTint;
            }
        }

        const double TotalSegTau = HeightTau + LocalTau;
        if (TotalSegTau <= 1e-9)
        {
            continue;
        }
        const double SegTrans = std::exp(-TotalSegTau);
        const double BlendLocal = LocalTau / TotalSegTau;
        const double EffAlbedo  = Specification.HeightFogAlbedo * (1.0 - BlendLocal) + LocalAlbedo * BlendLocal;
        const double EffPhaseG  = Specification.HeightFogPhaseG * (1.0 - BlendLocal) + LocalPhaseG * BlendLocal;
        const SpectrumRgb EffTint{
            Specification.HeightFog.Colour.R * (1.0 - BlendLocal) + LocalTint.R * BlendLocal,
            Specification.HeightFog.Colour.G * (1.0 - BlendLocal) + LocalTint.G * BlendLocal,
            Specification.HeightFog.Colour.B * (1.0 - BlendLocal) + LocalTint.B * BlendLocal};

        // Gather in-scattered radiance from Sky Ambient + Suns (w/ CloudShadowProjection) + IES + Lightning + Fire Facets!
        SpectrumRgb Incident = Specification.SkyAmbientRadiance;

        // (a) Multi-sun directional beams with CloudShadowProjection + geometry shadow
        for (const AtmosphereSolarSource& Sun : Specification.SolarSources)
        {
            const double SunLen = FogLen(Sun.Direction);
            if (SunLen <= 1e-9)
            {
                continue;
            }
            const SpatialVector SunDir{Sun.Direction.X / SunLen, Sun.Direction.Y / SunLen, Sun.Direction.Z / SunLen};
            if (SunDir.Z <= -0.02)
            {
                continue;
            }
            if (IsOccluded && IsOccluded(At, SunDir, 10000.0))
            {
                continue;
            }
            const double CloudTrans = Specification.CloudShadow ? Specification.CloudShadow->SampleTransmittance(At) : 1.0;
            if (CloudTrans <= 1e-4)
            {
                continue;
            }
            const double RadiusKm = Specification.Atmosphere.GroundRadiusKm +
                                    std::max(1e-3, Specification.ObserverAltitudeKm + 1e-3 * At.Z);
            const SpectrumRgb AtmosSunT = (Specification.Transmittance && Specification.Transmittance->Baked())
                                              ? Specification.Transmittance->Sample(Specification.Atmosphere, RadiusKm, SunDir.Z)
                                              : TransmittanceToTop(Specification.Atmosphere, RadiusKm, SunDir.Z, 16);
            const double Phase = HenyeyGreensteinPhase(FogDot(Dir, SunDir), EffPhaseG);
            Incident.R += Sun.Irradiance.R * AtmosSunT.R * CloudTrans * Phase;
            Incident.G += Sun.Irradiance.G * AtmosSunT.G * CloudTrans * Phase;
            Incident.B += Sun.Irradiance.B * AtmosSunT.B * CloudTrans * Phase;
        }

        // (b) Clustered / local photometric IES & spot luminaires
        for (const PhotometricFogLuminaire& Lum : Specification.Luminaires)
        {
            const SpatialVector Delta = FogSub(Lum.PositionM, At);
            const double        Dist2 = FogDot(Delta, Delta);
            const double        Dist  = std::sqrt(Dist2);
            if (Dist <= 0.05 || Dist > Lum.RangeMeters)
            {
                continue;
            }
            const SpatialVector TowardLight{Delta.X / Dist, Delta.Y / Dist, Delta.Z / Dist};
            const SpatialVector LightToPt{-TowardLight.X, -TowardLight.Y, -TowardLight.Z};
            const double        Ies = SampleIesRing(Lum, LightToPt);
            if (Ies <= 1e-5)
            {
                continue;
            }
            if (IsOccluded && IsOccluded(At, TowardLight, Dist - 0.05))
            {
                continue;
            }
            const double RangeWindow = FogSmooth(1.0 - Dist / Lum.RangeMeters);
            const double Atten       = (Ies * RangeWindow) / std::max(0.25, Dist2);
            const double Phase       = HenyeyGreensteinPhase(FogDot(Dir, TowardLight), EffPhaseG);
            Incident.R += Lum.PeakIntensity.R * Atten * Phase;
            Incident.G += Lum.PeakIntensity.G * Atten * Phase;
            Incident.B += Lum.PeakIntensity.B * Atten * Phase;
        }

        // (c) Transient lightning discharge channels (closest point on bolt segment)
        for (const LightningFogChannel& Bolt : Specification.LightningChannels)
        {
            const SpatialVector Seg  = FogSub(Bolt.SegmentEndM, Bolt.SegmentStartM);
            const double        Seg2 = std::max(FogDot(Seg, Seg), 1e-6);
            const double        Proj = std::clamp(FogDot(FogSub(At, Bolt.SegmentStartM), Seg) / Seg2, 0.0, 1.0);
            const SpatialVector Closest{Bolt.SegmentStartM.X + Seg.X * Proj,
                                        Bolt.SegmentStartM.Y + Seg.Y * Proj,
                                        Bolt.SegmentStartM.Z + Seg.Z * Proj};
            const SpatialVector Delta = FogSub(Closest, At);
            const double        Dist2 = FogDot(Delta, Delta);
            const double        Dist  = std::sqrt(Dist2);
            if (Dist > Bolt.ReachMeters)
            {
                continue;
            }
            const SpatialVector Toward = Dist > 1e-4 ? SpatialVector{Delta.X / Dist, Delta.Y / Dist, Delta.Z / Dist}
                                                     : SpatialVector{0.0, 0.0, 1.0};
            const double Phase  = HenyeyGreensteinPhase(FogDot(Dir, Toward), 0.35 * EffPhaseG);
            const double Falloff = FogSmooth(1.0 - Dist / Bolt.ReachMeters) /
                                   std::max(Bolt.CoreRadiusM * Bolt.CoreRadiusM, Dist2);
            Incident.R += Bolt.LineRadiance.R * Falloff * Phase;
            Incident.G += Bolt.LineRadiance.G * Falloff * Phase;
            Incident.B += Bolt.LineRadiance.B * Falloff * Phase;
        }

        // (d) Thermal fire / radiant area facets (midpoint solid-angle coupling)
        for (const RadiantFogFacet& Facet : Specification.RadiantFacets)
        {
            const SpatialVector Centroid{(Facet.CornerA.X + Facet.CornerB.X + Facet.CornerC.X) / 3.0,
                                         (Facet.CornerA.Y + Facet.CornerB.Y + Facet.CornerC.Y) / 3.0,
                                         (Facet.CornerA.Z + Facet.CornerB.Z + Facet.CornerC.Z) / 3.0};
            const SpatialVector Edge1  = FogSub(Facet.CornerB, Facet.CornerA);
            const SpatialVector Edge2  = FogSub(Facet.CornerC, Facet.CornerA);
            const SpatialVector NormalCross = FogCross(Edge1, Edge2);
            const double        DoubleArea  = FogLen(NormalCross);
            if (DoubleArea <= 1e-6)
            {
                continue;
            }
            const double        Area   = 0.5 * DoubleArea;
            const SpatialVector Normal = {NormalCross.X / DoubleArea, NormalCross.Y / DoubleArea, NormalCross.Z / DoubleArea};
            const SpatialVector Delta  = FogSub(Centroid, At);
            const double        Dist2  = FogDot(Delta, Delta);
            const double        Dist   = std::sqrt(Dist2);
            if (Dist <= 0.08)
            {
                continue;
            }
            const SpatialVector Toward{Delta.X / Dist, Delta.Y / Dist, Delta.Z / Dist};
            if (IsOccluded && IsOccluded(At, Toward, Dist - 0.06))
            {
                continue;
            }
            const double Geom  = std::abs(FogDot(Normal, Toward)) * Area / std::max(0.25, Dist2);
            const double Phase = HenyeyGreensteinPhase(FogDot(Dir, Toward), EffPhaseG);
            Incident.R += Facet.SurfaceRadiance.R * Geom * Phase;
            Incident.G += Facet.SurfaceRadiance.G * Geom * Phase;
            Incident.B += Facet.SurfaceRadiance.B * Geom * Phase;
        }

        const double ScatterWeight = MediumTransmittance * (1.0 - SegTrans) * EffAlbedo;
        MediumRadiance.R += ScatterWeight * Incident.R * EffTint.R;
        MediumRadiance.G += ScatterWeight * Incident.G * EffTint.G;
        MediumRadiance.B += ScatterWeight * Incident.B * EffTint.B;
        MediumTransmittance *= SegTrans;
    }

    // Beyond MarchSpanM up to SurfaceDistanceMeters, apply exact analytic tail transmittance of HeightFog!
    if (SurfaceDistanceMeters > MarchSpanM && Specification.HeightFog.DensityAtBase > 0.0)
    {
        const SpatialVector TailOrigin{OriginMeters.X + Dir.X * MarchSpanM,
                                       OriginMeters.Y + Dir.Y * MarchSpanM,
                                       OriginMeters.Z + Dir.Z * MarchSpanM};
        const double TailT = FogTransmittance(Specification.HeightFog, TailOrigin, Dir, SurfaceDistanceMeters - MarchSpanM);
        MediumRadiance.R += MediumTransmittance * (1.0 - TailT) * Specification.HeightFog.Colour.R * Specification.SkyAmbientRadiance.R;
        MediumRadiance.G += MediumTransmittance * (1.0 - TailT) * Specification.HeightFog.Colour.G * Specification.SkyAmbientRadiance.G;
        MediumRadiance.B += MediumTransmittance * (1.0 - TailT) * Specification.HeightFog.Colour.B * Specification.SkyAmbientRadiance.B;
        MediumTransmittance *= TailT;
    }

    // Couple planet-curved surface-distance Aerial Perspective when enabled!
    if (Specification.EnableAerialPerspective && !Specification.SolarSources.empty())
    {
        const AerialPerspectiveSample Aerial = EvaluateAerialPerspective(
            Specification.Atmosphere,
            Specification.Transmittance,
            Specification.MultiScatter,
            Specification.ObserverAltitudeKm,
            OriginMeters,
            Dir,
            SurfaceDistanceMeters,
            Specification.SolarSources,
            12);
        const double MeanAerialT = (Aerial.Transmittance.R + Aerial.Transmittance.G + Aerial.Transmittance.B) / 3.0;
        Result.Radiance          = {MediumRadiance.R * Aerial.Transmittance.R + Aerial.Inscatter.R * MediumTransmittance,
                                    MediumRadiance.G * Aerial.Transmittance.G + Aerial.Inscatter.G * MediumTransmittance,
                                    MediumRadiance.B * Aerial.Transmittance.B + Aerial.Inscatter.B * MediumTransmittance};
        Result.Transmittance     = MediumTransmittance * MeanAerialT;
    }
    else
    {
        Result.Radiance      = MediumRadiance;
        Result.Transmittance = MediumTransmittance;
    }

    return Result;
}

} // namespace Frontier
