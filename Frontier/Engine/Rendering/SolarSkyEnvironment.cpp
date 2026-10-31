/*==========================================================================================================================================
                                               SOLAR SKY ENVIRONMENT DEFINITION
==========================================================================================================================================*/
// 🧩 Multi-planet, multi-sun coupled atmospheric sky, 6-pt Gauss-Legendre NEE, and FP64 reference oracle.

#include "SolarSkyEnvironment.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Frontier
{
namespace
{

constexpr double Pi = 3.14159265358979323846;

double DotVec(SpatialVector A, SpatialVector B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

double LenVec(SpatialVector V) noexcept
{
    return std::sqrt(DotVec(V, V));
}

SpatialVector NormalizeChecked(SpatialVector V)
{
    const double L = LenVec(V);
    if (!std::isfinite(L) || L <= 1e-9)
    {
        throw std::invalid_argument("Solar/sky vector must be finite and non-zero");
    }
    return {V.X / L, V.Y / L, V.Z / L};
}

SpatialVector CrossVec(SpatialVector A, SpatialVector B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}

void ConstructOrthonormalAxes(SpatialVector Axis, SpatialVector& Tangent, SpatialVector& Bitangent) noexcept
{
    const SpatialVector Reference = std::abs(Axis.Z) < 0.999 ? SpatialVector{0.0, 0.0, 1.0} : SpatialVector{1.0, 0.0, 0.0};
    Tangent                       = CrossVec(Reference, Axis);
    const double TLen             = LenVec(Tangent);
    Tangent   = TLen > 1e-12 ? SpatialVector{Tangent.X / TLen, Tangent.Y / TLen, Tangent.Z / TLen} : SpatialVector{1.0, 0.0, 0.0};
    Bitangent = CrossVec(Axis, Tangent);
}

void MapToCanonicalPlanetSpace(SpatialVector  LocalUp,
                               SpatialVector  Direction,
                               SpatialVector  SolarDirection,
                               double         RadiusKm,
                               SpatialVector& CanonicalPosKm,
                               SpatialVector& CanonicalDir,
                               SpatialVector& CanonicalSun) noexcept
{
    SpatialVector East{}, North{};
    ConstructOrthonormalAxes(LocalUp, East, North);
    CanonicalPosKm = {0.0, RadiusKm, 0.0};
    CanonicalDir   = {DotVec(Direction, East), DotVec(Direction, LocalUp), DotVec(Direction, North)};
    CanonicalSun   = {DotVec(SolarDirection, East), DotVec(SolarDirection, LocalUp), DotVec(SolarDirection, North)};
}

double RaySphereForwardKm(SpatialVector PosKm, SpatialVector Dir, double RadiusKm) noexcept
{
    const double B    = DotVec(PosKm, Dir);
    const double C    = DotVec(PosKm, PosKm) - RadiusKm * RadiusKm;
    const double Disc = B * B - C;
    if (Disc < 0.0)
    {
        return -1.0;
    }
    const double Root = std::sqrt(Disc);
    const double Near = -B - Root;
    return Near >= 0.0 ? Near : (-B + Root >= 0.0 ? -B + Root : -1.0);
}

SpectrumRgb ExpNegRgb(SpectrumRgb Tau) noexcept
{
    return {std::exp(-Tau.R), std::exp(-Tau.G), std::exp(-Tau.B)};
}

SpectrumRgb WarpedOpticalDepth6Pt(const AtmosphereParameters& P, SpatialVector StartKm, SpatialVector EndKm) noexcept
{
    const SpatialVector Delta{EndKm.X - StartKm.X, EndKm.Y - StartKm.Y, EndKm.Z - StartKm.Z};
    const double        LengthKm = LenVec(Delta);
    if (LengthKm <= 0.0)
    {
        return {};
    }
    static constexpr double Nodes[6]   = {0.03376524, 0.16939531, 0.38069041, 0.61930959, 0.83060469, 0.96623476};
    static constexpr double Weights[6] = {0.08566225, 0.18038079, 0.23395697, 0.23395697, 0.18038079, 0.08566225};
    double                  TauR = 0.0, TauM = 0.0, TauO = 0.0;
    for (int I = 0; I < 6; ++I)
    {
        const double        Frac = Nodes[I] * Nodes[I];
        const double        Jac  = (2.0 * Nodes[I] * Weights[I]) * LengthKm;
        const SpatialVector At{StartKm.X + Delta.X * Frac, StartKm.Y + Delta.Y * Frac, StartKm.Z + Delta.Z * Frac};
        const double        AltKm = std::max(LenVec(At) - P.GroundRadiusKm, 0.0);
        TauR += RayleighDensity(P, AltKm) * Jac;
        TauM += MieDensity(P, AltKm) * Jac;
        TauO += OzoneDensity(P, AltKm) * Jac;
    }
    const double MieExt = P.MieScattering + P.MieAbsorption;
    return {P.RayleighScattering.R * TauR + MieExt * TauM + P.OzoneAbsorption.R * TauO,
            P.RayleighScattering.G * TauR + MieExt * TauM + P.OzoneAbsorption.G * TauO,
            P.RayleighScattering.B * TauR + MieExt * TauM + P.OzoneAbsorption.B * TauO};
}

SpectrumRgb SimpsonSegmentOpticalDepth(const AtmosphereParameters& P, SpatialVector StartKm, SpatialVector EndKm) noexcept
{
    const SpatialVector Delta{EndKm.X - StartKm.X, EndKm.Y - StartKm.Y, EndKm.Z - StartKm.Z};
    const double        LengthKm = LenVec(Delta);
    if (LengthKm <= 0.0)
    {
        return {};
    }
    const SpatialVector Mid{0.5 * (StartKm.X + EndKm.X), 0.5 * (StartKm.Y + EndKm.Y), 0.5 * (StartKm.Z + EndKm.Z)};
    const double        Alt0 = std::max(LenVec(StartKm) - P.GroundRadiusKm, 0.0);
    const double        AltM = std::max(LenVec(Mid) - P.GroundRadiusKm, 0.0);
    const double        Alt1 = std::max(LenVec(EndKm) - P.GroundRadiusKm, 0.0);
    const double        TauR = (RayleighDensity(P, Alt0) + 4.0 * RayleighDensity(P, AltM) + RayleighDensity(P, Alt1)) * (LengthKm / 6.0);
    const double        TauM = (MieDensity(P, Alt0) + 4.0 * MieDensity(P, AltM) + MieDensity(P, Alt1)) * (LengthKm / 6.0);
    const double        TauO = (OzoneDensity(P, Alt0) + 4.0 * OzoneDensity(P, AltM) + OzoneDensity(P, Alt1)) * (LengthKm / 6.0);
    const double        MieExt = P.MieScattering + P.MieAbsorption;
    return {P.RayleighScattering.R * TauR + MieExt * TauM + P.OzoneAbsorption.R * TauO,
            P.RayleighScattering.G * TauR + MieExt * TauM + P.OzoneAbsorption.G * TauO,
            P.RayleighScattering.B * TauR + MieExt * TauM + P.OzoneAbsorption.B * TauO};
}

SpectrumRgb WarpedTransmittanceToTop6Pt(const AtmosphereParameters& P, double RadiusKm, double CosUp) noexcept
{
    const double        ClampedMu = std::clamp(CosUp, -1.0, 1.0);
    const double        SinMu     = std::sqrt(std::max(1.0 - ClampedMu * ClampedMu, 0.0));
    const SpatialVector PosKm{0.0, RadiusKm, 0.0};
    const SpatialVector Dir{0.0, ClampedMu, SinMu};
    const double        ToGround = RaySphereForwardKm(PosKm, Dir, P.GroundRadiusKm);
    const double        ToTop    = RaySphereForwardKm(PosKm, Dir, P.TopRadiusKm);
    double              LengthKm = ToTop;
    if (ToGround > 1e-4 && (LengthKm < 0.0 || ToGround < LengthKm))
    {
        LengthKm = ToGround;
    }
    if (LengthKm < 0.0)
    {
        return {1.0, 1.0, 1.0};
    }
    const SpatialVector EndKm{PosKm.X + Dir.X * LengthKm, PosKm.Y + Dir.Y * LengthKm, PosKm.Z + Dir.Z * LengthKm};
    return ExpNegRgb(WarpedOpticalDepth6Pt(P, PosKm, EndKm));
}

double SegmentMeanRatio(double TauComp, double TauTotal, double SegT) noexcept
{
    return TauTotal > 1e-7 ? (TauComp / TauTotal) * (1.0 - SegT) : TauComp;
}

double SmoothStepVal(double Edge0, double Edge1, double X) noexcept
{
    if (Edge1 <= Edge0)
    {
        return X >= Edge1 ? 1.0 : 0.0;
    }
    const double T = std::clamp((X - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

double EvaluateCloudPattern(double HorizontalU, double HorizontalV, double Coverage) noexcept
{
    if (Coverage <= 0.0)
    {
        return 0.0;
    }
    const double WaveA     = 0.5 + 0.5 * std::sin(HorizontalU * 2.10 + 0.45) * std::cos(HorizontalV * 1.85 - 0.30);
    const double WaveB     = 0.5 + 0.5 * std::sin((HorizontalU * 0.72 - HorizontalV * 0.68) * 4.30 + 1.15);
    const double Field     = 0.65 * WaveA + 0.35 * WaveB;
    const double Threshold = 1.0 - std::clamp(Coverage, 0.0, 1.0);
    return SmoothStepVal(Threshold, std::min(1.0, Threshold + 0.28), Field);
}

void RequireDomain(bool Condition, const char* Message)
{
    if (!Condition)
    {
        throw std::invalid_argument(Message);
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           PAYLOAD COMPILATION AND VALIDATION
//------------------------------------------------------------------------------------------------------------------------

std::array<float, SolarSkyPayloadFloats> CompileSolarSkyPayload(const SolarSkySpecification& Specification,
                                                                std::size_t                  SourceIndex)
{
    RequireDomain(!Specification.SolarSources.empty() && SourceIndex < Specification.SolarSources.size(),
                  "Solar/sky specification must contain the requested solar source index");
    const SolarSkySourceProfile& Src     = Specification.SolarSources[SourceIndex];
    const SpatialVector          Sun     = NormalizeChecked(Src.SolarDirection);
    const SpatialVector          LocalUp = NormalizeChecked(Specification.LocalUpDirection);

    RequireDomain(std::isfinite(Src.SolarAngularRadius) && Src.SolarAngularRadius >= 5e-4 && Src.SolarAngularRadius <= 0.35,
                  "Solar angular radius lies outside [0.0005, 0.35] radians");
    RequireDomain(std::isfinite(Specification.ObserverAltitudeKm) && Specification.ObserverAltitudeKm >= 0.0 &&
                      Specification.ObserverAltitudeKm <= 120.0,
                  "Observer altitude lies outside [0, 120] km");
    RequireDomain(std::isfinite(Src.SolarIrradiance.R) && Src.SolarIrradiance.R >= 0.0 &&
                      std::isfinite(Src.SolarIrradiance.G) && Src.SolarIrradiance.G >= 0.0 &&
                      std::isfinite(Src.SolarIrradiance.B) && Src.SolarIrradiance.B >= 0.0,
                  "Solar top-of-atmosphere irradiance must be finite and non-negative");
    RequireDomain(std::isfinite(Src.LimbDarkeningU1) && std::isfinite(Src.LimbDarkeningU2) && Src.LimbDarkeningU1 >= 0.0 &&
                      Src.LimbDarkeningU2 >= 0.0 && Src.LimbDarkeningU1 + Src.LimbDarkeningU2 <= 0.98,
                  "Solar limb-darkening coefficients violate positivity on the disc limb");
    RequireDomain(std::isfinite(Specification.MultipleScatterScale) && Specification.MultipleScatterScale >= 0.0 &&
                      Specification.MultipleScatterScale <= 4.0,
                  "Multiple-scattering scale lies outside [0, 4]");
    RequireDomain(std::isfinite(Specification.CloudBottomKm) && std::isfinite(Specification.CloudTopKm) &&
                      Specification.CloudBottomKm >= 0.05 && Specification.CloudTopKm > Specification.CloudBottomKm &&
                      Specification.CloudTopKm <= 50.0,
                  "Cloud slab altitude bounds are invalid");
    RequireDomain(std::isfinite(Specification.CloudExtinctionKm) && Specification.CloudExtinctionKm >= 0.0 &&
                      Specification.CloudExtinctionKm <= 50.0 && std::isfinite(Specification.CloudCoverage) &&
                      Specification.CloudCoverage >= 0.0 && Specification.CloudCoverage <= 1.0,
                  "Cloud slab optical parameters are invalid");

    std::array<float, SolarSkyPayloadFloats> Payload{};
    Payload[0]  = static_cast<float>(Sun.X);
    Payload[1]  = static_cast<float>(Sun.Y);
    Payload[2]  = static_cast<float>(Sun.Z);
    Payload[3]  = static_cast<float>(Src.SolarAngularRadius);

    Payload[4]  = static_cast<float>(LocalUp.X);
    Payload[5]  = static_cast<float>(LocalUp.Y);
    Payload[6]  = static_cast<float>(LocalUp.Z);
    Payload[7]  = static_cast<float>(Specification.ObserverAltitudeKm);

    Payload[8]  = static_cast<float>(Src.SolarIrradiance.R);
    Payload[9]  = static_cast<float>(Src.SolarIrradiance.G);
    Payload[10] = static_cast<float>(Src.SolarIrradiance.B);
    Payload[11] = static_cast<float>(Specification.MultipleScatterScale);

    Payload[12] = static_cast<float>(Src.LimbDarkeningU1);
    Payload[13] = static_cast<float>(Src.LimbDarkeningU2);
    Payload[14] = static_cast<float>(std::clamp(Specification.Atmosphere.GroundAlbedo, 0.0, 1.0));
    Payload[15] = static_cast<float>(std::clamp(Specification.Atmosphere.MiePhaseG, -0.99, 0.99));

    Payload[16] = static_cast<float>(Specification.CloudBottomKm);
    Payload[17] = static_cast<float>(Specification.CloudTopKm);
    Payload[18] = static_cast<float>(Specification.CloudExtinctionKm);
    Payload[19] = static_cast<float>(Specification.CloudCoverage);
    return Payload;
}

void ValidateSolarSkyPayload(std::span<const float> Payload)
{
    RequireDomain(Payload.size() >= SolarSkyPayloadFloats, "Solar/sky payload requires twenty floats");
    for (std::uint32_t I = 0; I < SolarSkyPayloadFloats; ++I)
    {
        RequireDomain(std::isfinite(Payload[I]), "Solar/sky payload contains non-finite float");
    }
    const double SunLen = std::sqrt(double(Payload[0]) * Payload[0] + double(Payload[1]) * Payload[1] + double(Payload[2]) * Payload[2]);
    const double UpLen  = std::sqrt(double(Payload[4]) * Payload[4] + double(Payload[5]) * Payload[5] + double(Payload[6]) * Payload[6]);
    RequireDomain(std::abs(SunLen - 1.0) <= 1e-3 && std::abs(UpLen - 1.0) <= 1e-3,
                  "Solar/sky unit axes are not normalized");
    RequireDomain(Payload[3] >= 5e-4f && Payload[3] <= 0.35f, "Solar/sky angular radius is out of bounds");
    RequireDomain(Payload[7] >= 0.0f && Payload[7] <= 120.0f, "Solar/sky observer altitude is out of bounds");
    RequireDomain(Payload[8] >= 0.0f && Payload[9] >= 0.0f && Payload[10] >= 0.0f && Payload[11] >= 0.0f && Payload[11] <= 4.0f,
                  "Solar/sky irradiance or multiple-scatter scale is out of bounds");
    RequireDomain(Payload[12] >= 0.0f && Payload[13] >= 0.0f && (Payload[12] + Payload[13]) <= 0.985f &&
                      Payload[14] >= 0.0f && Payload[14] <= 1.0f && std::abs(Payload[15]) < 0.995f,
                  "Solar/sky limb-darkening or phase parameters are out of bounds");
    RequireDomain(Payload[16] >= 0.05f && Payload[17] > Payload[16] && Payload[17] <= 50.0f &&
                      Payload[18] >= 0.0f && Payload[18] <= 50.0f && Payload[19] >= 0.0f && Payload[19] <= 1.0f,
                  "Solar/sky cloud slab parameters are out of bounds");
}

//------------------------------------------------------------------------------------------------------------------------
//                                    FP64 ORACLE & 6-POINT GAUSS-LEGENDRE QUADRATURE
//------------------------------------------------------------------------------------------------------------------------

double EvaluateSolarSkyCloudTransmittance(const SolarSkySpecification& Specification,
                                          SpatialVector                OriginMeters,
                                          SpatialVector                Direction) noexcept
{
    if (Specification.CloudExtinctionKm <= 0.0 || Specification.CloudCoverage <= 0.0)
    {
        return 1.0;
    }
    const double UpLen  = LenVec(Specification.LocalUpDirection);
    const double DirLen = LenVec(Direction);
    if (UpLen <= 1e-9 || DirLen <= 1e-9)
    {
        return 1.0;
    }
    const SpatialVector LocalUp = {Specification.LocalUpDirection.X / UpLen,
                                   Specification.LocalUpDirection.Y / UpLen,
                                   Specification.LocalUpDirection.Z / UpLen};
    const SpatialVector UnitDir = {Direction.X / DirLen, Direction.Y / DirLen, Direction.Z / DirLen};
    const double        CosUp   = DotVec(UnitDir, LocalUp);
    if (CosUp <= 1e-3)
    {
        return 1.0;
    }

    const double OriginAltKm = Specification.ObserverAltitudeKm + 1e-3 * DotVec(OriginMeters, LocalUp);
    if (OriginAltKm >= Specification.CloudTopKm)
    {
        return 1.0;
    }
    const double EntryAltKm  = std::max(Specification.CloudBottomKm, OriginAltKm);
    const double EntryDistKm = (EntryAltKm - OriginAltKm) / CosUp;
    const double ExitDistKm  = (Specification.CloudTopKm - OriginAltKm) / CosUp;
    const double SegmentKm   = std::max(0.0, ExitDistKm - EntryDistKm);
    if (SegmentKm <= 0.0)
    {
        return 1.0;
    }

    SpatialVector East{}, North{};
    ConstructOrthonormalAxes(LocalUp, East, North);
    const SpatialVector OriginKm{1e-3 * OriginMeters.X, 1e-3 * OriginMeters.Y, 1e-3 * OriginMeters.Z};

    static constexpr double Nodes[3]   = {0.11270167, 0.50000000, 0.88729833};
    static constexpr double Weights[3] = {0.27777778, 0.44444444, 0.27777778};
    double                  OpticalDepth = 0.0;
    for (int Tap = 0; Tap < 3; ++Tap)
    {
        const double        DistKm = EntryDistKm + Nodes[Tap] * SegmentKm;
        const SpatialVector SampleKm{
            OriginKm.X + UnitDir.X * DistKm, OriginKm.Y + UnitDir.Y * DistKm, OriginKm.Z + UnitDir.Z * DistKm};
        const double AltKm = OriginAltKm + DistKm * CosUp;
        const double Frac  = std::clamp(
            (AltKm - Specification.CloudBottomKm) / std::max(Specification.CloudTopKm - Specification.CloudBottomKm, 1e-4), 0.0, 1.0);
        const double VertEnv = 4.0 * Frac * (1.0 - Frac);
        const double Horiz   = EvaluateCloudPattern(DotVec(SampleKm, East), DotVec(SampleKm, North), Specification.CloudCoverage);
        OpticalDepth += Specification.CloudExtinctionKm * VertEnv * Horiz * (SegmentKm * Weights[Tap]);
    }
    return std::exp(-OpticalDepth);
}

SpectrumRgb EvaluateSolarSkyOracleContinuum(const SolarSkySpecification& Specification,
                                            SpatialVector                OriginMeters,
                                            SpatialVector                Direction,
                                            std::uint32_t                Steps) noexcept
{
    const double UpLen  = LenVec(Specification.LocalUpDirection);
    const double DirLen = LenVec(Direction);
    if (UpLen <= 1e-9 || DirLen <= 1e-9 || Specification.SolarSources.empty())
    {
        return {};
    }
    const SpatialVector LocalUp = {Specification.LocalUpDirection.X / UpLen,
                                   Specification.LocalUpDirection.Y / UpLen,
                                   Specification.LocalUpDirection.Z / UpLen};
    const SpatialVector UnitDir = {Direction.X / DirLen, Direction.Y / DirLen, Direction.Z / DirLen};
    const double        LocalAltKm = std::max(0.0, Specification.ObserverAltitudeKm + 1e-3 * DotVec(OriginMeters, LocalUp));
    const double        RadiusKm   = Specification.Atmosphere.GroundRadiusKm + std::max(LocalAltKm, 1e-3);
    const double        CloudT     = EvaluateSolarSkyCloudTransmittance(Specification, OriginMeters, UnitDir);

    TransmittanceLut HighPrecisionTrans{};
    (void)HighPrecisionTrans.Bake(Specification.Atmosphere, 256u, 64u, std::max(Steps, 96u));

    SpectrumRgb Total{};
    for (const SolarSkySourceProfile& Src : Specification.SolarSources)
    {
        const double SunLen = LenVec(Src.SolarDirection);
        if (SunLen <= 1e-9)
        {
            continue;
        }
        const SpatialVector UnitSun{Src.SolarDirection.X / SunLen, Src.SolarDirection.Y / SunLen, Src.SolarDirection.Z / SunLen};
        SpatialVector       CanonicalObs{}, CanonicalDir{}, CanonicalSun{};
        MapToCanonicalPlanetSpace(LocalUp, UnitDir, UnitSun, RadiusKm, CanonicalObs, CanonicalDir, CanonicalSun);

        const SpectrumRgb Single = SingleScattering(
            Specification.Atmosphere, CanonicalObs, CanonicalDir, CanonicalSun, Src.SolarIrradiance, &HighPrecisionTrans, Steps);
        const SpectrumRgb Multi = MultipleScatteringApprox(
            Specification.Atmosphere, CanonicalObs, CanonicalDir, CanonicalSun, Single, Src.SolarIrradiance);

        const double      CloudSunlit    = std::max(0.0, DotVec(UnitSun, LocalUp));
        const double      SunGround      = RaySphereForwardKm(CanonicalObs, CanonicalSun, Specification.Atmosphere.GroundRadiusKm);
        const SpectrumRgb SunTop         = (SunGround < 0.0 || SunGround <= 1e-4)
                                               ? TransmittanceToTop(Specification.Atmosphere, RadiusKm, DotVec(UnitSun, LocalUp), 64u)
                                               : SpectrumRgb{};
        const double      CloudInscatter = (1.0 - CloudT) * (0.18 + 0.52 * CloudSunlit) / Pi;

        const SpectrumRgb ClearSky{Single.R + Specification.MultipleScatterScale * Multi.R,
                                   Single.G + Specification.MultipleScatterScale * Multi.G,
                                   Single.B + Specification.MultipleScatterScale * Multi.B};
        Total.R += ClearSky.R * CloudT + Src.SolarIrradiance.R * SunTop.R * CloudInscatter;
        Total.G += ClearSky.G * CloudT + Src.SolarIrradiance.G * SunTop.G * CloudInscatter;
        Total.B += ClearSky.B * CloudT + Src.SolarIrradiance.B * SunTop.B * CloudInscatter;
    }
    return Total;
}

SpectrumRgb EvaluateSolarSkyGaussLegendre6Pt(const SolarSkySpecification& Specification,
                                             SpatialVector                OriginMeters,
                                             SpatialVector                Direction) noexcept
{
    const double UpLen  = LenVec(Specification.LocalUpDirection);
    const double DirLen = LenVec(Direction);
    if (UpLen <= 1e-9 || DirLen <= 1e-9 || Specification.SolarSources.empty())
    {
        return {};
    }
    const AtmosphereParameters& P       = Specification.Atmosphere;
    const SpatialVector         LocalUp = {Specification.LocalUpDirection.X / UpLen,
                                           Specification.LocalUpDirection.Y / UpLen,
                                           Specification.LocalUpDirection.Z / UpLen};
    const SpatialVector         UnitDir = {Direction.X / DirLen, Direction.Y / DirLen, Direction.Z / DirLen};
    const double                LocalAltKm = std::max(0.0, Specification.ObserverAltitudeKm + 1e-3 * DotVec(OriginMeters, LocalUp));
    const double                RadiusKm   = P.GroundRadiusKm + std::max(LocalAltKm, 1e-3);
    const double                CloudT     = EvaluateSolarSkyCloudTransmittance(Specification, OriginMeters, UnitDir);

    TransmittanceLut TransLut{};
    (void)TransLut.Bake(P, kTransmittanceLutWidth, kTransmittanceLutHeight, 48u);

    SpectrumRgb Total{};
    static constexpr double Edges[7] = {0.0, 0.025, 0.09, 0.22, 0.44, 0.72, 1.0};

    for (const SolarSkySourceProfile& Src : Specification.SolarSources)
    {
        const double SunLen = LenVec(Src.SolarDirection);
        if (SunLen <= 1e-9)
        {
            continue;
        }
        const SpatialVector UnitSun{Src.SolarDirection.X / SunLen, Src.SolarDirection.Y / SunLen, Src.SolarDirection.Z / SunLen};
        SpatialVector       CanonicalPos{}, CanonicalDir{}, CanonicalSun{};
        MapToCanonicalPlanetSpace(LocalUp, UnitDir, UnitSun, RadiusKm, CanonicalPos, CanonicalDir, CanonicalSun);

        const double ToGround = RaySphereForwardKm(CanonicalPos, CanonicalDir, P.GroundRadiusKm);
        const double ToTop    = RaySphereForwardKm(CanonicalPos, CanonicalDir, P.TopRadiusKm);
        double       LengthKm = ToTop;
        if (ToGround > 1e-4 && (LengthKm < 0.0 || ToGround < LengthKm))
        {
            LengthKm = ToGround;
        }

        SpectrumRgb Single{};
        if (LengthKm > 0.0)
        {
            const double CosScatter = DotVec(CanonicalDir, CanonicalSun);
            const double PhaseR     = RayleighPhase(CosScatter);
            const double PhaseM     = CornetteShanksPhase(CosScatter, P.MiePhaseG);
            SpectrumRgb  ViewT{1.0, 1.0, 1.0};

            // Locate exact Earth-umbra exit t_lit when observer is in twilight shadow so 6-segment quadrature never straddles a step
            double LitStartKm = 0.0;
            const auto IsSunBlocked = [&](double DistKm) noexcept -> bool {
                const SpatialVector Pt{
                    CanonicalPos.X + CanonicalDir.X * DistKm,
                    CanonicalPos.Y + CanonicalDir.Y * DistKm,
                    CanonicalPos.Z + CanonicalDir.Z * DistKm};
                return RaySphereForwardKm(Pt, CanonicalSun, P.GroundRadiusKm) > 1e-4;
            };
            if (IsSunBlocked(0.0))
            {
                if (IsSunBlocked(LengthKm))
                {
                    LitStartKm = LengthKm;
                }
                else
                {
                    double Lo = 0.0, Hi = LengthKm;
                    for (int B = 0; B < 12; ++B)
                    {
                        const double MidT = 0.5 * (Lo + Hi);
                        if (IsSunBlocked(MidT))
                        {
                            Lo = MidT;
                        }
                        else
                        {
                            Hi = MidT;
                        }
                    }
                    LitStartKm = Hi;
                    const SpatialVector LitEntry{
                        CanonicalPos.X + CanonicalDir.X * LitStartKm,
                        CanonicalPos.Y + CanonicalDir.Y * LitStartKm,
                        CanonicalPos.Z + CanonicalDir.Z * LitStartKm};
                    ViewT = ExpNegRgb(WarpedOpticalDepth6Pt(P, CanonicalPos, LitEntry));
                }
            }

            const double ActiveSpanKm = std::max(0.0, LengthKm - LitStartKm);
            if (ActiveSpanKm > 1e-5)
            {
                static constexpr double Nodes[6]   = {0.03376524, 0.16939531, 0.38069041, 0.61930959, 0.83060469, 0.96623476};
                static constexpr double Weights[6] = {0.08566225, 0.18038079, 0.23395697, 0.23395697, 0.18038079, 0.08566225};
                SpatialVector           PrevPt{
                    CanonicalPos.X + CanonicalDir.X * LitStartKm,
                    CanonicalPos.Y + CanonicalDir.Y * LitStartKm,
                    CanonicalPos.Z + CanonicalDir.Z * LitStartKm};

                const double WarpExp = (LitStartKm > 0.5) ? 1.0 : 2.22;
                for (int Tap = 0; Tap < 6; ++Tap)
                {
                    const double        U   = Nodes[Tap];
                    const double        Upw = std::pow(U, WarpExp);
                    const double        S   = LitStartKm + ActiveSpanKm * Upw;
                    const double        WDs = ActiveSpanKm * (WarpExp * std::pow(U, WarpExp - 1.0) * Weights[Tap]);
                    const SpatialVector CurPt{
                        CanonicalPos.X + CanonicalDir.X * S,
                        CanonicalPos.Y + CanonicalDir.Y * S,
                        CanonicalPos.Z + CanonicalDir.Z * S};
                    const SpectrumRgb SegT = ExpNegRgb(WarpedOpticalDepth6Pt(P, PrevPt, CurPt));
                    ViewT                  = {ViewT.R * SegT.R, ViewT.G * SegT.G, ViewT.B * SegT.B};
                    PrevPt                 = CurPt;

                    const double SunGround = RaySphereForwardKm(CurPt, CanonicalSun, P.GroundRadiusKm);
                    if (SunGround < 0.0 || SunGround <= 1e-4)
                    {
                        const double      CurRadius = LenVec(CurPt);
                        const double      AltKm     = std::max(CurRadius - P.GroundRadiusKm, 0.0);
                        const double      CosSun    = DotVec({CurPt.X / CurRadius, CurPt.Y / CurRadius, CurPt.Z / CurRadius}, CanonicalSun);
                        const SpectrumRgb SunT      = TransLut.Sample(P, CurRadius, CosSun);
                        const double      DnsR      = RayleighDensity(P, AltKm);
                        const double      DnsM      = MieDensity(P, AltKm);
                        const SpectrumRgb SigmaPhase{
                            (PhaseR * P.RayleighScattering.R * DnsR + PhaseM * P.MieScattering * DnsM) * WDs,
                            (PhaseR * P.RayleighScattering.G * DnsR + PhaseM * P.MieScattering * DnsM) * WDs,
                            (PhaseR * P.RayleighScattering.B * DnsR + PhaseM * P.MieScattering * DnsM) * WDs};

                        Single.R += ViewT.R * SunT.R * Src.SolarIrradiance.R * SigmaPhase.R;
                        Single.G += ViewT.G * SunT.G * Src.SolarIrradiance.G * SigmaPhase.G;
                        Single.B += ViewT.B * SunT.B * Src.SolarIrradiance.B * SigmaPhase.B;
                    }
                }
            }
        }

        const SpectrumRgb Multi = MultipleScatteringApprox(
            P, CanonicalPos, CanonicalDir, CanonicalSun, Single, Src.SolarIrradiance);
        const double      ObsGround = RaySphereForwardKm(CanonicalPos, CanonicalSun, P.GroundRadiusKm);
        const SpectrumRgb SunObsT   = (ObsGround < 0.0 || ObsGround <= 1e-4)
                                          ? WarpedTransmittanceToTop6Pt(P, RadiusKm, CanonicalSun.Y)
                                          : SpectrumRgb{};
        const SpectrumRgb ClearSky{
            Single.R + Specification.MultipleScatterScale * Multi.R,
            Single.G + Specification.MultipleScatterScale * Multi.G,
            Single.B + Specification.MultipleScatterScale * Multi.B};

        const double CloudSunlit    = std::max(0.0, DotVec(UnitSun, LocalUp));
        const double CloudInscatter = (1.0 - CloudT) * (0.18 + 0.52 * CloudSunlit) / Pi;
        Total.R += std::max(0.0, ClearSky.R * CloudT + Src.SolarIrradiance.R * SunObsT.R * CloudInscatter);
        Total.G += std::max(0.0, ClearSky.G * CloudT + Src.SolarIrradiance.G * SunObsT.G * CloudInscatter);
        Total.B += std::max(0.0, ClearSky.B * CloudT + Src.SolarIrradiance.B * SunObsT.B * CloudInscatter);
    }
    return Total;
}

SpectrumRgb EvaluateSolarSkyDiscRadiance(const SolarSkySpecification& Specification,
                                         SpatialVector                OriginMeters,
                                         SpatialVector                Direction,
                                         double*                      OutPrimarySolidAnglePdf,
                                         std::uint32_t                Steps) noexcept
{
    if (OutPrimarySolidAnglePdf)
    {
        *OutPrimarySolidAnglePdf = 0.0;
    }
    const double UpLen  = LenVec(Specification.LocalUpDirection);
    const double DirLen = LenVec(Direction);
    if (UpLen <= 1e-9 || DirLen <= 1e-9 || Specification.SolarSources.empty())
    {
        return {};
    }
    const SpatialVector LocalUp = {Specification.LocalUpDirection.X / UpLen,
                                   Specification.LocalUpDirection.Y / UpLen,
                                   Specification.LocalUpDirection.Z / UpLen};
    const SpatialVector UnitDir = {Direction.X / DirLen, Direction.Y / DirLen, Direction.Z / DirLen};
    const double        CosUp   = DotVec(UnitDir, LocalUp);
    if (CosUp <= -0.02)
    {
        return {};
    }
    const double LocalAltKm = std::max(0.0, Specification.ObserverAltitudeKm + 1e-3 * DotVec(OriginMeters, LocalUp));
    const double RadiusKm   = Specification.Atmosphere.GroundRadiusKm + std::max(LocalAltKm, 1e-3);
    const SpectrumRgb AtmosT = TransmittanceToTop(Specification.Atmosphere, RadiusKm, CosUp, Steps);
    const double      CloudT = EvaluateSolarSkyCloudTransmittance(Specification, OriginMeters, UnitDir);

    SpectrumRgb Total{};
    for (std::size_t Index = 0; Index < Specification.SolarSources.size(); ++Index)
    {
        const SolarSkySourceProfile& Src    = Specification.SolarSources[Index];
        const double                 SunLen = LenVec(Src.SolarDirection);
        if (SunLen <= 1e-9)
        {
            continue;
        }
        const SpatialVector UnitSun{Src.SolarDirection.X / SunLen, Src.SolarDirection.Y / SunLen, Src.SolarDirection.Z / SunLen};
        const double        CosEdge      = std::cos(Src.SolarAngularRadius);
        const double        CosAngle     = DotVec(UnitDir, UnitSun);
        const double        OneMinusEdge = std::max(1.0 - CosEdge, 1e-12);
        const double        SolidAngle   = 2.0 * Pi * OneMinusEdge;
        if (CosAngle < CosEdge)
        {
            continue;
        }
        if (Index == 0u && OutPrimarySolidAnglePdf)
        {
            *OutPrimarySolidAnglePdf = 1.0 / SolidAngle;
        }
        const double CapFraction = std::clamp((CosAngle - CosEdge) / OneMinusEdge, 0.0, 1.0);
        const double MuLimb      = std::sqrt(CapFraction);
        const double OneMinusMu  = 1.0 - MuLimb;
        const double LimbProfile =
            std::max(0.0, 1.0 - Src.LimbDarkeningU1 * OneMinusMu - Src.LimbDarkeningU2 * OneMinusMu * OneMinusMu);
        const double MeanLimb = std::max(1e-6, 1.0 - Src.LimbDarkeningU1 / 3.0 - Src.LimbDarkeningU2 / 6.0);
        const double Scale    = ((LimbProfile / MeanLimb) / SolidAngle) * CloudT;

        Total.R += Src.SolarIrradiance.R * AtmosT.R * Scale;
        Total.G += Src.SolarIrradiance.G * AtmosT.G * Scale;
        Total.B += Src.SolarIrradiance.B * AtmosT.B * Scale;
    }
    return Total;
}

SpectrumRgb EvaluateSolarSkyTotalRadiance(const SolarSkySpecification& Specification,
                                          SpatialVector                OriginMeters,
                                          SpatialVector                Direction) noexcept
{
    const SpectrumRgb Sky  = EvaluateSolarSkyGaussLegendre6Pt(Specification, OriginMeters, Direction);
    const SpectrumRgb Disc = EvaluateSolarSkyDiscRadiance(Specification, OriginMeters, Direction, nullptr, 32u);
    return {Sky.R + Disc.R, Sky.G + Disc.G, Sky.B + Disc.B};
}

SpectrumRgb IntegrateSolarSkyDiscIrradiance(const SolarSkySpecification& Specification,
                                            SpatialVector                OriginMeters,
                                            SpatialVector                SurfaceNormal,
                                            std::uint32_t                RadialRings,
                                            std::uint32_t                AzimuthSectors) noexcept
{
    const double NormLen = LenVec(SurfaceNormal);
    if (NormLen <= 1e-9 || RadialRings == 0 || AzimuthSectors == 0)
    {
        return {};
    }
    const SpatialVector Normal{SurfaceNormal.X / NormLen, SurfaceNormal.Y / NormLen, SurfaceNormal.Z / NormLen};

    SpectrumRgb Sum{};
    for (std::size_t Index = 0; Index < Specification.SolarSources.size(); ++Index)
    {
        const SolarSkySourceProfile& Src    = Specification.SolarSources[Index];
        const double                 SunLen = LenVec(Src.SolarDirection);
        if (SunLen <= 1e-9)
        {
            continue;
        }
        const SpatialVector UnitSun{Src.SolarDirection.X / SunLen, Src.SolarDirection.Y / SunLen, Src.SolarDirection.Z / SunLen};
        SpatialVector       Tangent{}, Bitangent{};
        ConstructOrthonormalAxes(UnitSun, Tangent, Bitangent);

        const double CosEdge        = std::cos(Src.SolarAngularRadius);
        const double OneMinusEdge   = std::max(1.0 - CosEdge, 1e-12);
        const double CellSolidAngle = (2.0 * Pi * OneMinusEdge) / static_cast<double>(RadialRings * AzimuthSectors);

        SolarSkySpecification SingleSpec = Specification;
        SingleSpec.SolarSources          = {Src};

        for (std::uint32_t Ring = 0; Ring < RadialRings; ++Ring)
        {
            const double CapU     = (static_cast<double>(Ring) + 0.5) / static_cast<double>(RadialRings);
            const double CosTheta = CosEdge + CapU * OneMinusEdge;
            const double SinTheta = std::sqrt(std::max(0.0, 1.0 - CosTheta * CosTheta));
            for (std::uint32_t Sector = 0; Sector < AzimuthSectors; ++Sector)
            {
                const double        Phi    = 2.0 * Pi * (static_cast<double>(Sector) + 0.5) / static_cast<double>(AzimuthSectors);
                const double        CosPhi = std::cos(Phi);
                const double        SinPhi = std::sin(Phi);
                const SpatialVector Omega{UnitSun.X * CosTheta + (Tangent.X * CosPhi + Bitangent.X * SinPhi) * SinTheta,
                                          UnitSun.Y * CosTheta + (Tangent.Y * CosPhi + Bitangent.Y * SinPhi) * SinTheta,
                                          UnitSun.Z * CosTheta + (Tangent.Z * CosPhi + Bitangent.Z * SinPhi) * SinTheta};
                const double        CosSurface = std::max(0.0, DotVec(Normal, Omega));
                if (CosSurface <= 0.0)
                {
                    continue;
                }
                const SpectrumRgb Disc = EvaluateSolarSkyDiscRadiance(SingleSpec, OriginMeters, Omega, nullptr, 48u);
                Sum.R += Disc.R * CosSurface * CellSolidAngle;
                Sum.G += Disc.G * CosSurface * CellSolidAngle;
                Sum.B += Disc.B * CosSurface * CellSolidAngle;
            }
        }
    }
    return Sum;
}

} // namespace Frontier
