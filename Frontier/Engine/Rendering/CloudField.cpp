/*==========================================================================================================================================
                                                    CLOUD FIELD DEFINITION
==========================================================================================================================================*/

#include "CloudField.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SpatialVector Mul(SpatialVector A, double S) noexcept { return {A.X * S, A.Y * S, A.Z * S}; }
double        Dot(SpatialVector A, SpatialVector B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }

// Integer lattice hash (PCG output function over mixed coordinates) -> [0, 1). Integer-only path.
double HashLattice(int32_t X, int32_t Y, int32_t Z, uint64_t Seed) noexcept
{
    uint64_t H = Seed + static_cast<uint64_t>(static_cast<uint32_t>(X)) * 0x9E3779B97F4A7C15ull;
    H ^= static_cast<uint64_t>(static_cast<uint32_t>(Y)) * 0xC2B2AE3D27D4EB4Full;
    H ^= static_cast<uint64_t>(static_cast<uint32_t>(Z)) * 0x165667B19E3779F9ull;
    H ^= H >> 29u;
    H *= 0xBF58476D1CE4E5B9ull;
    H ^= H >> 32u;
    return static_cast<double>(static_cast<uint32_t>(H >> 32u)) * (1.0 / 4294967296.0);
}

double Smooth(double T) noexcept { return T * T * (3.0 - 2.0 * T); }

double ValueNoise(SpatialVector P, uint64_t Seed) noexcept
{
    const int32_t X0 = static_cast<int32_t>(std::floor(P.X));
    const int32_t Y0 = static_cast<int32_t>(std::floor(P.Y));
    const int32_t Z0 = static_cast<int32_t>(std::floor(P.Z));
    const double  FX = Smooth(P.X - X0), FY = Smooth(P.Y - Y0), FZ = Smooth(P.Z - Z0);
    const auto    L = [](double A, double B, double T) { return A + (B - A) * T; };
    const double C00 = L(HashLattice(X0, Y0, Z0, Seed), HashLattice(X0 + 1, Y0, Z0, Seed), FX);
    const double C10 = L(HashLattice(X0, Y0 + 1, Z0, Seed), HashLattice(X0 + 1, Y0 + 1, Z0, Seed), FX);
    const double C01 = L(HashLattice(X0, Y0, Z0 + 1, Seed), HashLattice(X0 + 1, Y0, Z0 + 1, Seed), FX);
    const double C11 = L(HashLattice(X0, Y0 + 1, Z0 + 1, Seed), HashLattice(X0 + 1, Y0 + 1, Z0 + 1, Seed), FX);
    return L(L(C00, C10, FY), L(C01, C11, FY), FZ);
}

double Fbm(SpatialVector P, uint64_t Seed) noexcept
{
    double Sum = 0.0, Amplitude = 0.5, Total = 0.0;
    for (uint32_t Octave = 0; Octave < 4; ++Octave)
    {
        Sum += Amplitude * ValueNoise(P, Seed + Octave * 131u);
        Total += Amplitude;
        P = {P.X * 2.03 + 11.3, P.Y * 2.03 + 7.7, P.Z * 2.03 + 5.1};
        Amplitude *= 0.5;
    }
    return Sum / Total;
}

} // namespace

double CloudDensity(const CloudParameters& P, SpatialVector Point) noexcept
{
    if (Point.Z < P.BaseHeightM || Point.Z > P.TopHeightM || P.TopHeightM <= P.BaseHeightM)
    {
        return 0.0;
    }
    if (P.UniformDensityOverride >= 0.0)
    {
        return P.UniformDensityOverride * P.DensityScale;
    }
    const double HeightFraction = (Point.Z - P.BaseHeightM) / (P.TopHeightM - P.BaseHeightM);
    // Vertical profile: rounded bottom, fuller middle, wispy top.
    const double Profile = std::clamp(std::min(HeightFraction * 4.0, 1.0) *
                                          (1.0 - std::max(HeightFraction - 0.65, 0.0) / 0.35 * 0.85),
                                      0.0, 1.0);
    const SpatialVector NoisePoint{(Point.X + P.WindXMps * P.WindTimeS) / 1400.0,
                                   (Point.Y + P.WindYMps * P.WindTimeS) / 1400.0, Point.Z / 700.0};
    const double Base = Fbm(NoisePoint, P.Seed);
    const double Coverage = std::clamp(P.Coverage, 0.0, 1.0);
    const double Shaped = std::clamp((Base - (1.0 - Coverage)) / std::max(Coverage, 1e-3), 0.0, 1.5);
    return Shaped * Profile * P.DensityScale;
}

CloudMarchResult MarchClouds(const CloudParameters& P, SpatialVector Origin, SpatialVector Direction,
                             SpatialVector SunDirection, const SpectrumRgb& SunRadiance, double MaxDistanceM,
                             uint32_t Steps, uint32_t SunSteps) noexcept
{
    CloudMarchResult Result;
    if (Steps == 0 || MaxDistanceM <= 0.0)
    {
        return Result;
    }
    // Intersect the ray with the slab planes to bound the march.
    double TEnter = 0.0, TExit = MaxDistanceM;
    if (std::abs(Direction.Z) < 1e-9)
    {
        if (Origin.Z < P.BaseHeightM || Origin.Z > P.TopHeightM)
        {
            return Result;   // parallel and outside: no cloud on this ray
        }
    }
    else
    {
        const double TA = (P.BaseHeightM - Origin.Z) / Direction.Z;
        const double TB = (P.TopHeightM - Origin.Z) / Direction.Z;
        TEnter = std::max(std::min(TA, TB), 0.0);
        TExit  = std::min(std::max(TA, TB), MaxDistanceM);
        if (TExit <= TEnter)
        {
            return Result;
        }
    }

    const double Dt = (TExit - TEnter) / static_cast<double>(Steps);
    const double Phase = HenyeyGreensteinPhase(Dot(Direction, SunDirection), P.PhaseG);
    double Transmittance = 1.0;
    SpectrumRgb Radiance{};

    for (uint32_t I = 0; I < Steps; ++I)
    {
        const double        T = TEnter + Dt * (static_cast<double>(I) + 0.5);
        const SpatialVector At = Add(Origin, Mul(Direction, T));
        const double        Density = CloudDensity(P, At);
        if (Density <= 0.0)
        {
            continue;
        }
        const double SigmaT = P.ExtinctionPerM * Density;
        const double StepTransmittance = std::exp(-SigmaT * Dt);
        if (Transmittance < 1e-4)
        {
            // Radiance can no longer change measurably, but transmittance must stay exact:
            // keep multiplying it through the remaining steps, skip only the scattering work.
            Transmittance *= StepTransmittance;
            continue;
        }

        // Sun visibility: short march toward the sun through the same field.
        double SunOpticalDepth = 0.0;
        if (SunSteps > 0)
        {
            const double SunDt = 220.0;   // [m] fixed shadow step
            for (uint32_t S = 0; S < SunSteps; ++S)
            {
                const SpatialVector ShadowAt = Add(At, Mul(SunDirection, SunDt * (static_cast<double>(S) + 0.5)));
                SunOpticalDepth += P.ExtinctionPerM * CloudDensity(P, ShadowAt) * SunDt;
                if (ShadowAt.Z > P.TopHeightM && SunDirection.Z > 0.0)
                {
                    break;
                }
            }
        }
        const double SunTransmittance = std::exp(-SunOpticalDepth);
        const double ScatterAmount = Transmittance * (1.0 - StepTransmittance) * Phase * SunTransmittance;
        const double AmbientAmount = Transmittance * (1.0 - StepTransmittance);
        Radiance.R += ScatterAmount * SunRadiance.R + AmbientAmount * P.AmbientRadiance.R;
        Radiance.G += ScatterAmount * SunRadiance.G + AmbientAmount * P.AmbientRadiance.G;
        Radiance.B += ScatterAmount * SunRadiance.B + AmbientAmount * P.AmbientRadiance.B;
        Transmittance *= StepTransmittance;
    }
    Result.Radiance      = Radiance;
    Result.Transmittance = Transmittance;
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                      MULTI-VOLUME PERLIN-WORLEY CLOUDS, BEERS-POWDER & ORTHO SHADOW PROJECTION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

double WorleyCellular3D(SpatialVector P, uint64_t Seed) noexcept
{
    const int32_t CX = static_cast<int32_t>(std::floor(P.X));
    const int32_t CY = static_cast<int32_t>(std::floor(P.Y));
    const int32_t CZ = static_cast<int32_t>(std::floor(P.Z));
    double        MinDist2 = 8.0;
    for (int32_t DZ = -1; DZ <= 1; ++DZ)
    {
        for (int32_t DY = -1; DY <= 1; ++DY)
        {
            for (int32_t DX = -1; DX <= 1; ++DX)
            {
                const int32_t NX = CX + DX;
                const int32_t NY = CY + DY;
                const int32_t NZ = CZ + DZ;
                const double  JX = static_cast<double>(NX) + HashLattice(NX, NY, NZ, Seed ^ 0xA173ull);
                const double  JY = static_cast<double>(NY) + HashLattice(NX, NY, NZ, Seed ^ 0xB295ull);
                const double  JZ = static_cast<double>(NZ) + HashLattice(NX, NY, NZ, Seed ^ 0xC3D7ull);
                const double  Ox = P.X - JX;
                const double  Oy = P.Y - JY;
                const double  Oz = P.Z - JZ;
                MinDist2         = std::min(MinDist2, Ox * Ox + Oy * Oy + Oz * Oz);
            }
        }
    }
    return std::clamp(1.0 - std::sqrt(MinDist2), 0.0, 1.0);
}

double WorleyFbm3D(SpatialVector P, uint64_t Seed) noexcept
{
    const double W0 = WorleyCellular3D(P, Seed);
    const double W1 = WorleyCellular3D({P.X * 2.0 + 3.7, P.Y * 2.0 + 1.9, P.Z * 2.0 + 5.3}, Seed + 79u);
    const double W2 = WorleyCellular3D({P.X * 4.0 + 7.1, P.Y * 4.0 + 4.3, P.Z * 4.0 + 2.9}, Seed + 193u);
    return 0.625 * W0 + 0.25 * W1 + 0.125 * W2;
}

bool IntersectRayBounds(SpatialVector Origin,
                        SpatialVector Dir,
                        SpatialVector MinB,
                        SpatialVector MaxB,
                        double        MaxDist,
                        double&       OutEnter,
                        double&       OutExit) noexcept
{
    double TMin = 0.0;
    double TMax = MaxDist;
    const double Orig[3] = {Origin.X, Origin.Y, Origin.Z};
    const double D[3]    = {Dir.X, Dir.Y, Dir.Z};
    const double Lo[3]   = {MinB.X, MinB.Y, MinB.Z};
    const double Hi[3]   = {MaxB.X, MaxB.Y, MaxB.Z};
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        if (std::abs(D[Axis]) < 1e-10)
        {
            if (Orig[Axis] < Lo[Axis] || Orig[Axis] > Hi[Axis])
            {
                return false;
            }
        }
        else
        {
            const double A = (Lo[Axis] - Orig[Axis]) / D[Axis];
            const double B = (Hi[Axis] - Orig[Axis]) / D[Axis];
            TMin           = std::max(TMin, std::min(A, B));
            TMax           = std::min(TMax, std::max(A, B));
            if (TMax <= TMin)
            {
                return false;
            }
        }
    }
    OutEnter = TMin;
    OutExit  = TMax;
    return TMax > TMin;
}

} // namespace

double EvaluateCloudVolumeDensity(const CloudVolumeProfile& Volume, SpatialVector PointMeters) noexcept
{
    const CloudParameters& P = Volume.Slab;
    double Envelope = 1.0;
    double HeightFraction = 0.5;
    double ScaleMeters = 1400.0;

    if (Volume.Domain == CloudVolumeDomain::PlanetarySlab)
    {
        if (PointMeters.Z < P.BaseHeightM || PointMeters.Z > P.TopHeightM || P.TopHeightM <= P.BaseHeightM)
        {
            return 0.0;
        }
        HeightFraction = (PointMeters.Z - P.BaseHeightM) / (P.TopHeightM - P.BaseHeightM);
        Envelope = std::clamp(std::min(HeightFraction * 4.0, 1.0) *
                                  (1.0 - std::max(HeightFraction - 0.65, 0.0) / 0.35 * 0.85),
                              0.0, 1.0);
    }
    else
    {
        const double Dx = std::abs(PointMeters.X - Volume.BoxCenterM.X);
        const double Dy = std::abs(PointMeters.Y - Volume.BoxCenterM.Y);
        const double Dz = std::abs(PointMeters.Z - Volume.BoxCenterM.Z);
        if (Dx > Volume.BoxHalfExtentsM.X || Dy > Volume.BoxHalfExtentsM.Y || Dz > Volume.BoxHalfExtentsM.Z)
        {
            return 0.0;
        }
        const double Soft = std::max(Volume.BoxEdgeFalloffM, 1.0);
        const double Fx   = Smooth(std::clamp((Volume.BoxHalfExtentsM.X - Dx) / Soft, 0.0, 1.0));
        const double Fy   = Smooth(std::clamp((Volume.BoxHalfExtentsM.Y - Dy) / Soft, 0.0, 1.0));
        const double Fz   = Smooth(std::clamp((Volume.BoxHalfExtentsM.Z - Dz) / Soft, 0.0, 1.0));
        Envelope          = Fx * Fy * Fz;
        HeightFraction    = std::clamp(0.5 + 0.5 * (PointMeters.Z - Volume.BoxCenterM.Z) / std::max(Volume.BoxHalfExtentsM.Z, 1.0), 0.0, 1.0);
        ScaleMeters       = std::max(180.0, 0.85 * std::max({Volume.BoxHalfExtentsM.X, Volume.BoxHalfExtentsM.Y, Volume.BoxHalfExtentsM.Z}));
    }

    if (P.UniformDensityOverride >= 0.0)
    {
        return P.UniformDensityOverride * P.DensityScale * Envelope;
    }

    const SpatialVector Advected{(PointMeters.X + P.WindXMps * P.WindTimeS) / ScaleMeters,
                                 (PointMeters.Y + P.WindYMps * P.WindTimeS) / ScaleMeters,
                                 PointMeters.Z / (0.5 * ScaleMeters)};
    double BaseNoise = Fbm(Advected, P.Seed);
    if (Volume.WorleyErosionStrength > 0.0)
    {
        const double Worley = WorleyFbm3D({Advected.X * 2.2, Advected.Y * 2.2, Advected.Z * 2.2}, P.Seed ^ 0x55AA11ull);
        const double Eroded = std::clamp(BaseNoise - Volume.WorleyErosionStrength * (1.0 - Worley) * (0.35 + 0.65 * HeightFraction), 0.0, 1.0);
        BaseNoise           = Eroded / std::max(1.0 - 0.35 * Volume.WorleyErosionStrength, 0.25);
    }
    const double Coverage = std::clamp(P.Coverage, 0.0, 1.0);
    const double Shaped   = std::clamp((BaseNoise - (1.0 - Coverage)) / std::max(Coverage, 1e-3), 0.0, 1.5);
    return Shaped * Envelope * P.DensityScale;
}

double EvaluateBeersPowderEnergy(double OpticalDepth,
                                 double CosAngle,
                                 double ForwardPhaseG,
                                 double BackscatterPhaseG,
                                 double PowderStrength,
                                 double MultiScatterStrength) noexcept
{
    const double Tau    = std::max(0.0, OpticalDepth);
    const double Beer0  = std::exp(-Tau);
    const double Powder = 1.0 - PowderStrength * std::exp(-2.0 * Tau) * 0.55;
    const double Phase0 = 0.72 * HenyeyGreensteinPhase(CosAngle, ForwardPhaseG) +
                          0.28 * HenyeyGreensteinPhase(CosAngle, BackscatterPhaseG);

    // Second-octave Hillaire/Schneider multi-scattering approximation (attenuated extinction & softer phase)
    const double Beer1  = std::exp(-0.35 * Tau);
    const double Phase1 = 0.55 * HenyeyGreensteinPhase(CosAngle, 0.5 * ForwardPhaseG) +
                          0.45 * (1.0 / (4.0 * 3.14159265358979323846));
    return Beer0 * Powder * Phase0 + MultiScatterStrength * Beer1 * Phase1;
}

VolumetricCloudSample MarchCloudVolumes(std::span<const CloudVolumeProfile> Volumes,
                                        SpatialVector                       OriginMeters,
                                        SpatialVector                       DirectionUnit,
                                        std::span<const CloudSolarBeam>     SolarBeams,
                                        const SpectrumRgb&                  SkyAmbientRadiance,
                                        double                              MaxDistanceM,
                                        uint32_t                            Steps,
                                        uint32_t                            ShadowSteps) noexcept
{
    VolumetricCloudSample Result;
    if (Volumes.empty() || Steps == 0 || MaxDistanceM <= 0.0)
    {
        return Result;
    }

    // Union the ray intervals across all intersected volumes so bounded local clouds & high cirrus share coherent marching!
    double TGlobalEnter = MaxDistanceM;
    double TGlobalExit  = 0.0;
    bool   AnyHit       = false;
    for (const CloudVolumeProfile& Vol : Volumes)
    {
        double Enter = 0.0, Exit = 0.0;
        if (Vol.Domain == CloudVolumeDomain::PlanetarySlab)
        {
            if (std::abs(DirectionUnit.Z) < 1e-9)
            {
                if (OriginMeters.Z >= Vol.Slab.BaseHeightM && OriginMeters.Z <= Vol.Slab.TopHeightM)
                {
                    Enter  = 0.0;
                    Exit   = MaxDistanceM;
                    AnyHit = true;
                }
            }
            else
            {
                const double A = (Vol.Slab.BaseHeightM - OriginMeters.Z) / DirectionUnit.Z;
                const double B = (Vol.Slab.TopHeightM - OriginMeters.Z) / DirectionUnit.Z;
                Enter          = std::max(std::min(A, B), 0.0);
                Exit           = std::min(std::max(A, B), MaxDistanceM);
                if (Exit > Enter)
                {
                    AnyHit = true;
                }
            }
        }
        else
        {
            const SpatialVector Lo{Vol.BoxCenterM.X - Vol.BoxHalfExtentsM.X,
                                   Vol.BoxCenterM.Y - Vol.BoxHalfExtentsM.Y,
                                   Vol.BoxCenterM.Z - Vol.BoxHalfExtentsM.Z};
            const SpatialVector Hi{Vol.BoxCenterM.X + Vol.BoxHalfExtentsM.X,
                                   Vol.BoxCenterM.Y + Vol.BoxHalfExtentsM.Y,
                                   Vol.BoxCenterM.Z + Vol.BoxHalfExtentsM.Z};
            if (IntersectRayBounds(OriginMeters, DirectionUnit, Lo, Hi, MaxDistanceM, Enter, Exit))
            {
                AnyHit = true;
            }
        }
        if (Exit > Enter)
        {
            TGlobalEnter = std::min(TGlobalEnter, Enter);
            TGlobalExit  = std::max(TGlobalExit, Exit);
        }
    }

    if (!AnyHit || TGlobalExit <= TGlobalEnter)
    {
        return Result;
    }

    const double Dt            = (TGlobalExit - TGlobalEnter) / static_cast<double>(Steps);
    double       Transmittance = 1.0;
    double       WeightedDist  = 0.0;
    double       WeightSum     = 0.0;
    SpectrumRgb  Radiance{};

    for (uint32_t Step = 0; Step < Steps; ++Step)
    {
        const double        DistM = TGlobalEnter + Dt * (static_cast<double>(Step) + 0.5);
        const SpatialVector At    = Add(OriginMeters, Mul(DirectionUnit, DistM));

        double      SigmaT          = 0.0;
        double      ForwardG        = 0.35;
        double      BackG           = -0.25;
        double      Powder          = 0.85;
        double      MultiGain       = 0.55;
        SpectrumRgb LocalAmbient    = SkyAmbientRadiance;
        double      DominantSigma   = 0.0;

        for (const CloudVolumeProfile& Vol : Volumes)
        {
            const double Dns = EvaluateCloudVolumeDensity(Vol, At);
            if (Dns <= 0.0)
            {
                continue;
            }
            const double Ext = Vol.Slab.ExtinctionPerM * Dns;
            SigmaT += Ext;
            if (Ext > DominantSigma)
            {
                DominantSigma = Ext;
                ForwardG      = Vol.Slab.PhaseG;
                BackG         = Vol.BackscatterPhaseG;
                Powder        = Vol.PowderStrength;
                MultiGain     = Vol.MultiScatterStrength;
                LocalAmbient  = {SkyAmbientRadiance.R + Vol.Slab.AmbientRadiance.R,
                                 SkyAmbientRadiance.G + Vol.Slab.AmbientRadiance.G,
                                 SkyAmbientRadiance.B + Vol.Slab.AmbientRadiance.B};
            }
        }

        if (SigmaT <= 1e-8)
        {
            continue;
        }

        const double StepT       = std::exp(-SigmaT * Dt);
        const double OpacityWeight = Transmittance * (1.0 - StepT);
        WeightedDist += OpacityWeight * DistM;
        WeightSum    += OpacityWeight;

        if (Transmittance < 1e-4)
        {
            Transmittance *= StepT;
            continue;
        }

        SpectrumRgb StepScatter = LocalAmbient;
        for (const CloudSolarBeam& Beam : SolarBeams)
        {
            const double BeamLen = std::sqrt(Dot(Beam.Direction, Beam.Direction));
            if (BeamLen <= 1e-9)
            {
                continue;
            }
            const SpatialVector SunDir = Mul(Beam.Direction, 1.0 / BeamLen);
            double              OpticalDepth = 0.0;
            if (ShadowSteps > 0)
            {
                const double ShadowDt = 165.0;
                for (uint32_t S = 0; S < ShadowSteps; ++S)
                {
                    const SpatialVector ShadowPos = Add(At, Mul(SunDir, ShadowDt * (static_cast<double>(S) + 0.5)));
                    for (const CloudVolumeProfile& Vol : Volumes)
                    {
                        OpticalDepth += Vol.Slab.ExtinctionPerM * EvaluateCloudVolumeDensity(Vol, ShadowPos) * ShadowDt;
                    }
                }
            }
            const double Energy = EvaluateBeersPowderEnergy(
                OpticalDepth, Dot(DirectionUnit, SunDir), ForwardG, BackG, Powder, MultiGain);
            StepScatter.R += Beam.Radiance.R * Energy;
            StepScatter.G += Beam.Radiance.G * Energy;
            StepScatter.B += Beam.Radiance.B * Energy;
        }

        Radiance.R += OpacityWeight * StepScatter.R;
        Radiance.G += OpacityWeight * StepScatter.G;
        Radiance.B += OpacityWeight * StepScatter.B;
        Transmittance *= StepT;
    }

    Result.Radiance         = Radiance;
    Result.Transmittance    = Transmittance;
    Result.MeanHitDistanceM = WeightSum > 1e-6 ? (WeightedDist / WeightSum) : 0.0;
    return Result;
}

bool CloudShadowProjection::Bake(std::span<const CloudVolumeProfile> Volumes,
                                 SpatialVector                       AnchorMeters,
                                 SpatialVector                       SolarDirectionUnit,
                                 double                              HalfSpanMeters,
                                 uint32_t                            Resolution,
                                 uint32_t                            MarchSteps)
{
    const double SunLen = std::sqrt(Dot(SolarDirectionUnit, SolarDirectionUnit));
    if (Resolution < 2 || HalfSpanMeters <= 1.0 || MarchSteps == 0 || SunLen <= 1e-9)
    {
        return false;
    }
    Resolution_     = Resolution;
    HalfSpanM_      = HalfSpanMeters;
    AnchorM_        = AnchorMeters;
    SolarDirection_ = Mul(SolarDirectionUnit, 1.0 / SunLen);

    // Build orthonormal projection axes perpendicular to SolarDirection_
    const SpatialVector Ref = std::abs(SolarDirection_.Z) < 0.95 ? SpatialVector{0.0, 0.0, 1.0} : SpatialVector{1.0, 0.0, 0.0};
    const SpatialVector CrossU{Ref.Y * SolarDirection_.Z - Ref.Z * SolarDirection_.Y,
                               Ref.Z * SolarDirection_.X - Ref.X * SolarDirection_.Z,
                               Ref.X * SolarDirection_.Y - Ref.Y * SolarDirection_.X};
    const double ULen = std::sqrt(Dot(CrossU, CrossU));
    OrthoAxisU_       = Mul(CrossU, 1.0 / std::max(ULen, 1e-12));
    OrthoAxisV_       = {SolarDirection_.Y * OrthoAxisU_.Z - SolarDirection_.Z * OrthoAxisU_.Y,
                         SolarDirection_.Z * OrthoAxisU_.X - SolarDirection_.X * OrthoAxisU_.Z,
                         SolarDirection_.X * OrthoAxisU_.Y - SolarDirection_.Y * OrthoAxisU_.X};

    OpticalDepthTexels_.assign(static_cast<size_t>(Resolution) * Resolution, 0.0f);
    if (Volumes.empty() || SolarDirection_.Z <= 0.02)
    {
        return true;
    }

    double MaxCloudTop = 2500.0;
    for (const CloudVolumeProfile& Vol : Volumes)
    {
        if (Vol.Domain == CloudVolumeDomain::PlanetarySlab)
        {
            MaxCloudTop = std::max(MaxCloudTop, Vol.Slab.TopHeightM + 100.0);
        }
        else
        {
            MaxCloudTop = std::max(MaxCloudTop, Vol.BoxCenterM.Z + Vol.BoxHalfExtentsM.Z + 100.0);
        }
    }
    const double RaySpanM = std::min(30000.0, MaxCloudTop / std::max(SolarDirection_.Z, 0.08));
    const double Dt       = RaySpanM / static_cast<double>(MarchSteps);

    for (uint32_t Row = 0; Row < Resolution; ++Row)
    {
        const double V = ((static_cast<double>(Row) + 0.5) / static_cast<double>(Resolution) * 2.0 - 1.0) * HalfSpanMeters;
        for (uint32_t Col = 0; Col < Resolution; ++Col)
        {
            const double U = ((static_cast<double>(Col) + 0.5) / static_cast<double>(Resolution) * 2.0 - 1.0) * HalfSpanMeters;
            const SpatialVector BasePt = Add(AnchorMeters, Add(Mul(OrthoAxisU_, U), Mul(OrthoAxisV_, V)));

            double Tau = 0.0;
            for (uint32_t Step = 0; Step < MarchSteps; ++Step)
            {
                const SpatialVector SamplePt = Add(BasePt, Mul(SolarDirection_, Dt * (static_cast<double>(Step) + 0.5)));
                for (const CloudVolumeProfile& Vol : Volumes)
                {
                    Tau += Vol.Slab.ExtinctionPerM * EvaluateCloudVolumeDensity(Vol, SamplePt) * Dt;
                }
            }
            OpticalDepthTexels_[static_cast<size_t>(Row) * Resolution + Col] = static_cast<float>(Tau);
        }
    }
    return true;
}

double CloudShadowProjection::SampleOpticalDepth(SpatialVector WorldPointMeters) const noexcept
{
    if (OpticalDepthTexels_.empty() || Resolution_ < 2)
    {
        return 0.0;
    }
    // Project WorldPointMeters along SolarDirection_ onto the anchor plane passing through AnchorM_
    const SpatialVector Rel{WorldPointMeters.X - AnchorM_.X,
                            WorldPointMeters.Y - AnchorM_.Y,
                            WorldPointMeters.Z - AnchorM_.Z};
    const double AlongSun = Dot(Rel, SolarDirection_);
    const SpatialVector Projected{Rel.X - SolarDirection_.X * AlongSun,
                                  Rel.Y - SolarDirection_.Y * AlongSun,
                                  Rel.Z - SolarDirection_.Z * AlongSun};
    const double U = (Dot(Projected, OrthoAxisU_) / HalfSpanM_) * 0.5 + 0.5;
    const double V = (Dot(Projected, OrthoAxisV_) / HalfSpanM_) * 0.5 + 0.5;
    if (U < 0.0 || U > 1.0 || V < 0.0 || V > 1.0)
    {
        return 0.0;
    }

    const double   Fx = std::clamp(U * static_cast<double>(Resolution_) - 0.5, 0.0, static_cast<double>(Resolution_ - 1));
    const double   Fy = std::clamp(V * static_cast<double>(Resolution_) - 0.5, 0.0, static_cast<double>(Resolution_ - 1));
    const uint32_t X0 = static_cast<uint32_t>(Fx);
    const uint32_t Y0 = static_cast<uint32_t>(Fy);
    const uint32_t X1 = std::min(X0 + 1, Resolution_ - 1);
    const uint32_t Y1 = std::min(Y0 + 1, Resolution_ - 1);
    const double   Tx = Fx - static_cast<double>(X0);
    const double   Ty = Fy - static_cast<double>(Y0);

    const auto At = [&](uint32_t X, uint32_t Y) -> double {
        return static_cast<double>(OpticalDepthTexels_[static_cast<size_t>(Y) * Resolution_ + X]);
    };
    const double Top = At(X0, Y0) + (At(X1, Y0) - At(X0, Y0)) * Tx;
    const double Bot = At(X0, Y1) + (At(X1, Y1) - At(X0, Y1)) * Tx;
    return Top + (Bot - Top) * Ty;
}

double CloudShadowProjection::SampleTransmittance(SpatialVector WorldPointMeters) const noexcept
{
    return std::exp(-SampleOpticalDepth(WorldPointMeters));
}

} // namespace Frontier
