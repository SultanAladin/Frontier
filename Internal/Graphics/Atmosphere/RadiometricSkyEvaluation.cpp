/*==============================================================================================================================================
                                                     RADIOMETRICSKYEVALUATION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of physical Hillaire-2020 spherical atmospheric radiative transfer, ForceField sky cirrus/aerosol advection,
//    Neckel-Labs solar limb darkening + continuous coronal/aureole falloff, and outer-space planetary limb/globe shading.

#include "Graphics/Atmosphere/RadiometricSkyEvaluation.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr float PiConstant  = 3.14159265358979323846f;
constexpr float TauConstant = 6.28318530717958647692f;

struct MediumCoefficients
{
    SpectralColor RayleighScattering {};
    float         MieScattering      = 0.0f;
    SpectralColor TotalScattering    {};
    SpectralColor TotalExtinction    {};
};

[[nodiscard]] MediumCoefficients SampleMediumAtAltitudeKm(const RadiometricSkyConfiguration& Cfg, float AltitudeKm) noexcept
{
    const float ClampedAlt  = std::max(0.0f, AltitudeKm);
    const float RayleighRho = std::exp(-ClampedAlt / std::max(0.1f, Cfg.RayleighScaleHeightKm));
    const float MieRho      = std::exp(-ClampedAlt / std::max(0.05f, Cfg.MieScaleHeightKm));
    const float OzoneRho    = std::max(0.0f, 1.0f - std::fabs(ClampedAlt - Cfg.OzonePeakAltitudeKm) / std::max(0.5f, Cfg.OzoneHalfWidthKm));

    MediumCoefficients Medium{};
    Medium.RayleighScattering = ScaleSpectral(Cfg.RayleighScatteringPerKm, RayleighRho);
    Medium.MieScattering      = Cfg.MieScatteringPerKm * MieRho;
    Medium.TotalScattering    = SpectralColor{
        Medium.RayleighScattering.Red   + Medium.MieScattering,
        Medium.RayleighScattering.Green + Medium.MieScattering,
        Medium.RayleighScattering.Blue  + Medium.MieScattering };

    const float MieExt = Cfg.MieExtinctionPerKm * MieRho;
    Medium.TotalExtinction = SpectralColor{
        Medium.RayleighScattering.Red   + MieExt + Cfg.OzoneAbsorptionPerKm.Red   * OzoneRho,
        Medium.RayleighScattering.Green + MieExt + Cfg.OzoneAbsorptionPerKm.Green * OzoneRho,
        Medium.RayleighScattering.Blue  + MieExt + Cfg.OzoneAbsorptionPerKm.Blue  * OzoneRho };
    return Medium;
}

[[nodiscard]] float EvaluateRayleighPhase(float CosineTheta) noexcept
{
    return (3.0f / (16.0f * PiConstant)) * (1.0f + CosineTheta * CosineTheta);
}

[[nodiscard]] float EvaluateCornetteShanksPhase(float CosineTheta, float Asymmetry) noexcept
{
    const float G2    = Asymmetry * Asymmetry;
    const float Scale = (3.0f / (8.0f * PiConstant)) * (1.0f - G2) / (2.0f + G2);
    const float Denom = std::pow(std::max(1.0e-4f, 1.0f + G2 - 2.0f * Asymmetry * CosineTheta), 1.5f);
    return Scale * (1.0f + CosineTheta * CosineTheta) / Denom;
}

[[nodiscard]] float EvaluateHenyeyGreensteinPhase(float CosineTheta, float Asymmetry) noexcept
{
    const float G2    = Asymmetry * Asymmetry;
    const float Denom = std::pow(std::max(1.0e-4f, 1.0f + G2 - 2.0f * Asymmetry * CosineTheta), 1.5f);
    return (1.0f - G2) / (4.0f * PiConstant * Denom);
}

// 📝 Positive exit distance from radius r along zenith cosine mu to top atmosphere sphere R_top.
[[nodiscard]] float TraceAtmosphereExitKm(float RadiusKm, float VerticalCosine, float TopRadiusKm) noexcept
{
    const float Discriminant = RadiusKm * RadiusKm * (VerticalCosine * VerticalCosine - 1.0f) + TopRadiusKm * TopRadiusKm;
    if (Discriminant < 0.0f)
        return -1.0f;
    return std::max(0.0f, -RadiusKm * VerticalCosine + std::sqrt(Discriminant));
}

// 📝 Nearest positive hit distance to solid/ocean planet sphere R_bottom, or -1 when ray misses planet.
[[nodiscard]] float TraceGroundContactKm(float RadiusKm, float VerticalCosine, float BottomRadiusKm) noexcept
{
    const float Discriminant = RadiusKm * RadiusKm * (VerticalCosine * VerticalCosine - 1.0f) + BottomRadiusKm * BottomRadiusKm;
    if (Discriminant < 0.0f)
        return -1.0f;
    const float Root = std::sqrt(Discriminant);
    const float Near = -RadiusKm * VerticalCosine - Root;
    if (Near > 0.0f)
        return Near;
    return -1.0f;
}

// 📝 Full 3D ray-sphere entry/exit pair [NearT, FarT] in kilometres around planet center (0,0,0).
[[nodiscard]] std::array<float, 2> IntersectPlanetCenteredSphereKm(const Vector3f& OriginKm,
                                                                   const Vector3f& Direction,
                                                                   float           SphereRadiusKm) noexcept
{
    const float ProjB        = DotVector(OriginKm, Direction);
    const float TermC        = DotVector(OriginKm, OriginKm) - SphereRadiusKm * SphereRadiusKm;
    const float Discriminant = ProjB * ProjB - TermC;
    if (Discriminant < 0.0f)
        return { -1.0f, -1.0f };
    const float Root = std::sqrt(Discriminant);
    return { -ProjB - Root, -ProjB + Root };
}

// 📝 Bruneton/Hillaire horizon-aware transmittance LUT mapping (r, mu) <-> (u, v).
[[nodiscard]] std::array<float, 2> EncodeTransmittanceUv(float RadiusKm,
                                                         float VerticalCosine,
                                                         float BottomRadiusKm,
                                                         float TopRadiusKm) noexcept
{
    const float ClampedRadius = std::clamp(RadiusKm, BottomRadiusKm + 1.0e-4f, TopRadiusKm - 1.0e-4f);
    const float GroundToTop   = std::sqrt(std::max(0.0f, TopRadiusKm * TopRadiusKm - BottomRadiusKm * BottomRadiusKm));
    const float Rho           = std::sqrt(std::max(0.0f, ClampedRadius * ClampedRadius - BottomRadiusKm * BottomRadiusKm));
    const float ExitDist      = TraceAtmosphereExitKm(ClampedRadius, VerticalCosine, TopRadiusKm);
    const float MinDist       = TopRadiusKm - ClampedRadius;
    const float MaxDist       = Rho + GroundToTop;
    const float UCoord        = std::clamp((ExitDist - MinDist) / std::max(1.0e-5f, MaxDist - MinDist), 0.0f, 1.0f);
    const float VCoord        = std::clamp(Rho / std::max(1.0e-5f, GroundToTop), 0.0f, 1.0f);
    return { UCoord, VCoord };
}

[[nodiscard]] std::array<float, 2> DecodeTransmittanceUv(float UCoord,
                                                         float VCoord,
                                                         float BottomRadiusKm,
                                                         float TopRadiusKm) noexcept
{
    const float GroundToTop = std::sqrt(std::max(0.0f, TopRadiusKm * TopRadiusKm - BottomRadiusKm * BottomRadiusKm));
    const float Rho         = VCoord * GroundToTop;
    const float RadiusKm    = std::sqrt(Rho * Rho + BottomRadiusKm * BottomRadiusKm);
    const float MinDist     = TopRadiusKm - RadiusKm;
    const float MaxDist     = Rho + GroundToTop;
    const float ExitDist    = MinDist + UCoord * (MaxDist - MinDist);
    float VerticalCosine    = 1.0f;
    if (ExitDist > 1.0e-5f)
    {
        VerticalCosine = (TopRadiusKm * TopRadiusKm - RadiusKm * RadiusKm - ExitDist * ExitDist)
                       / (2.0f * RadiusKm * ExitDist);
    }
    return { RadiusKm, std::clamp(VerticalCosine, -1.0f, 1.0f) };
}

[[nodiscard]] SpectralColor SampleBilinearTable(const std::vector<SpectralColor>& Texels,
                                                uint32_t                          Width,
                                                uint32_t                          Height,
                                                float                             UCoord,
                                                float                             VCoord,
                                                bool                              WrapHorizontal) noexcept
{
    if (Texels.size() < static_cast<std::size_t>(Width) * Height)
        return SpectralColor{ 1.0f, 1.0f, 1.0f };

    float NormalizedU = UCoord;
    if (WrapHorizontal)
        NormalizedU = NormalizedU - std::floor(NormalizedU);
    else
        NormalizedU = std::clamp(NormalizedU, 0.0f, 1.0f);

    const float ClampedV = std::clamp(VCoord, 0.0f, 1.0f);
    const float GridX    = NormalizedU * static_cast<float>(Width) - 0.5f;
    const float GridY    = ClampedV * static_cast<float>(Height) - 0.5f;

    const int32_t WInt = static_cast<int32_t>(Width);
    const int32_t HInt = static_cast<int32_t>(Height);

    int32_t X0 = static_cast<int32_t>(std::floor(GridX));
    int32_t X1 = X0 + 1;
    if (WrapHorizontal)
    {
        X0 = ((X0 % WInt) + WInt) % WInt;
        X1 = ((X1 % WInt) + WInt) % WInt;
    }
    else
    {
        X0 = std::clamp(X0, 0, WInt - 1);
        X1 = std::clamp(X1, 0, WInt - 1);
    }

    const int32_t Y0    = std::clamp(static_cast<int32_t>(std::floor(GridY)), 0, HInt - 1);
    const int32_t Y1    = std::clamp(Y0 + 1, 0, HInt - 1);
    const float   FracX = std::clamp(GridX - std::floor(GridX), 0.0f, 1.0f);
    const float   FracY = std::clamp(GridY - std::floor(GridY), 0.0f, 1.0f);

    auto Fetch = [&](int32_t Col, int32_t Row) noexcept -> const SpectralColor&
    {
        return Texels[static_cast<std::size_t>(Row) * Width + static_cast<std::size_t>(Col)];
    };

    auto MixSpectral = [](const SpectralColor& A, const SpectralColor& B, float T) noexcept -> SpectralColor
    {
        return SpectralColor{
            A.Red   + (B.Red   - A.Red)   * T,
            A.Green + (B.Green - A.Green) * T,
            A.Blue  + (B.Blue  - A.Blue)  * T };
    };

    const SpectralColor Row0 = MixSpectral(Fetch(X0, Y0), Fetch(X1, Y0), FracX);
    const SpectralColor Row1 = MixSpectral(Fetch(X0, Y1), Fetch(X1, Y1), FracX);
    return MixSpectral(Row0, Row1, FracY);
}

[[nodiscard]] SpectralColor SampleMultiScatterLut(const RadiometricSkyTables&        Tables,
                                                  const RadiometricSkyConfiguration& Cfg,
                                                  float                              RadiusKm,
                                                  float                              SolarVerticalCosine) noexcept
{
    if (!Tables.TablesReadyCondition)
        return SpectralColor{ 0.0f, 0.0f, 0.0f };

    const float UCoord = std::clamp(0.5f * (SolarVerticalCosine + 1.0f), 0.0f, 1.0f);
    const float VCoord = std::clamp((RadiusKm - Cfg.BottomRadiusKilometres)
                       / std::max(1.0e-3f, Cfg.TopRadiusKilometres - Cfg.BottomRadiusKilometres), 0.0f, 1.0f);
    return ScaleSpectral(
        SampleBilinearTable(
            Tables.MultiScatterTexels,
            RadiometricSkyTables::MultiScatterWidth,
            RadiometricSkyTables::MultiScatterHeight,
            UCoord,
            VCoord,
            false),
        Cfg.MultipleScatteringGain);
}

// 📝 Direct Beer-Lambert transmittance integration along arbitrary 3D segment [OriginKm, OriginKm + Direction * SegmentKm].
[[nodiscard]] SpectralColor IntegrateDirectTransmittanceKm(const RadiometricSkyConfiguration& Cfg,
                                                           const Vector3f&                    OriginKm,
                                                           const Vector3f&                    Direction,
                                                           float                              SegmentLengthKm,
                                                           uint32_t                           StepCount) noexcept
{
    if (SegmentLengthKm <= 1.0e-5f || StepCount == 0u)
        return SpectralColor{ 1.0f, 1.0f, 1.0f };

    const float StepKm = SegmentLengthKm / static_cast<float>(StepCount);
    SpectralColor OpticalDepth{ 0.0f, 0.0f, 0.0f };
    for (uint32_t Step = 0u; Step < StepCount; ++Step)
    {
        const float    TravelKm = (static_cast<float>(Step) + 0.5f) * StepKm;
        const Vector3f PosKm    = AddVector(OriginKm, ScaleVector(Direction, TravelKm));
        const float    AltKm    = std::max(0.0f, VectorLength(PosKm) - Cfg.BottomRadiusKilometres);
        const auto     Medium   = SampleMediumAtAltitudeKm(Cfg, AltKm);
        OpticalDepth.Red   += Medium.TotalExtinction.Red   * StepKm;
        OpticalDepth.Green += Medium.TotalExtinction.Green * StepKm;
        OpticalDepth.Blue  += Medium.TotalExtinction.Blue  * StepKm;
    }
    return SpectralColor{
        std::exp(-OpticalDepth.Red),
        std::exp(-OpticalDepth.Green),
        std::exp(-OpticalDepth.Blue) };
}

// 📝 3D domain-rotated spherical FBM used for high-altitude cirrus filaments, tropospheric aerosol streamers, and orbital planet surface.
[[nodiscard]] uint32_t HashSphericalLattice(int32_t XIdx, int32_t YIdx, int32_t ZIdx) noexcept
{
    uint32_t Word = static_cast<uint32_t>(XIdx) * 73856093u
                  ^ static_cast<uint32_t>(YIdx) * 19349663u
                  ^ static_cast<uint32_t>(ZIdx) * 83492791u;
    Word = (Word ^ (Word >> 15u)) * 2246822519u;
    Word = (Word ^ (Word >> 13u)) * 3266489917u;
    return Word ^ (Word >> 16u);
}

[[nodiscard]] float EvaluateSphericalNoise3(float XCoord, float YCoord, float ZCoord) noexcept
{
    const int32_t IX = static_cast<int32_t>(std::floor(XCoord));
    const int32_t IY = static_cast<int32_t>(std::floor(YCoord));
    const int32_t IZ = static_cast<int32_t>(std::floor(ZCoord));

    const float FX = XCoord - static_cast<float>(IX);
    const float FY = YCoord - static_cast<float>(IY);
    const float FZ = ZCoord - static_cast<float>(IZ);

    const float UX = FX * FX * (3.0f - 2.0f * FX);
    const float UY = FY * FY * (3.0f - 2.0f * FY);
    const float UZ = FZ * FZ * (3.0f - 2.0f * FZ);

    auto Corner = [&](int32_t DX, int32_t DY, int32_t DZ) noexcept -> float
    {
        return static_cast<float>(HashSphericalLattice(IX + DX, IY + DY, IZ + DZ) & 0xFFFFu) * (1.0f / 65535.0f);
    };

    auto Mix = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };
    const float Slice0 = Mix(Mix(Corner(0, 0, 0), Corner(1, 0, 0), UX), Mix(Corner(0, 1, 0), Corner(1, 1, 0), UX), UY);
    const float Slice1 = Mix(Mix(Corner(0, 0, 1), Corner(1, 0, 1), UX), Mix(Corner(0, 1, 1), Corner(1, 1, 1), UX), UY);
    return Mix(Slice0, Slice1, UZ);
}

[[nodiscard]] float EvaluateRotatedFbm3(float XCoord, float YCoord, float ZCoord, uint32_t OctaveCount) noexcept
{
    float Accumulator = 0.0f;
    float Amplitude   = 0.5f;
    float PX = XCoord, PY = YCoord, PZ = ZCoord;
    for (uint32_t Octave = 0u; Octave < OctaveCount; ++Octave)
    {
        Accumulator += Amplitude * EvaluateSphericalNoise3(PX, PY, PZ);
        const float RX =  0.80f * PX - 0.48f * PY + 0.36f * PZ;
        const float RY =  0.48f * PX + 0.86f * PY + 0.16f * PZ;
        const float RZ = -0.36f * PX + 0.04f * PY + 0.93f * PZ;
        PX = RX * 2.04f + 13.7f;
        PY = RY * 2.04f - 7.3f;
        PZ = RZ * 2.04f + 4.1f;
        Amplitude *= 0.5f;
    }
    return Accumulator;
}

// 📝 Limb-centered Sky-View UV <-> Z-up scene bearing. Concentrates half the vertical texels near the exact planetary limb angle.
[[nodiscard]] Vector3f DecodeSkyViewBearingScene(float UCoord,
                                                 float VCoord,
                                                 float ObserverRadiusKm,
                                                 float BottomRadiusKm) noexcept
{
    const float Azimuth   = (UCoord * 2.0f - 1.0f) * PiConstant;
    const float SinLimb   = std::sqrt(std::max(0.0f, ObserverRadiusKm * ObserverRadiusKm - BottomRadiusKm * BottomRadiusKm))
                          / std::max(1.0e-4f, ObserverRadiusKm);
    const float LimbAngle = std::acos(std::clamp(SinLimb, -1.0f, 1.0f)) - 0.5f * PiConstant;

    const float Centered  = VCoord * 2.0f - 1.0f;
    float       Elevation = 0.0f;
    if (Centered < 0.0f)
    {
        const float T = 1.0f + Centered;
        const float Fraction = 1.0f - T * T;
        Elevation = LimbAngle + (-0.5f * PiConstant - LimbAngle) * Fraction;
    }
    else
    {
        const float T = 1.0f - Centered;
        const float Fraction = 1.0f - T * T;
        Elevation = LimbAngle + (0.5f * PiConstant - LimbAngle) * Fraction;
    }

    const float CosElev = std::cos(Elevation);
    return Vector3f{ CosElev * std::cos(Azimuth), CosElev * std::sin(Azimuth), std::sin(Elevation) };
}

[[nodiscard]] std::array<float, 2> EncodeSkyViewUvScene(const Vector3f& BearingScene,
                                                        float           ObserverRadiusKm,
                                                        float           BottomRadiusKm) noexcept
{
    const float Azimuth   = std::atan2(BearingScene.YCoord, BearingScene.XCoord);
    const float Elevation = std::asin(std::clamp(BearingScene.ZCoord, -1.0f, 1.0f));
    const float SinLimb   = std::sqrt(std::max(0.0f, ObserverRadiusKm * ObserverRadiusKm - BottomRadiusKm * BottomRadiusKm))
                          / std::max(1.0e-4f, ObserverRadiusKm);
    const float LimbAngle = std::acos(std::clamp(SinLimb, -1.0f, 1.0f)) - 0.5f * PiConstant;

    float UCoord = Azimuth / TauConstant + 0.5f;
    UCoord       = UCoord - std::floor(UCoord);

    float VCoord = 0.5f;
    if (Elevation < LimbAngle)
    {
        const float Span     = std::max(1.0e-5f, LimbAngle - (-0.5f * PiConstant));
        const float Fraction = std::clamp((LimbAngle - Elevation) / Span, 0.0f, 1.0f);
        VCoord = 0.5f * std::sqrt(std::max(0.0f, 1.0f - Fraction));
    }
    else
    {
        const float Span     = std::max(1.0e-5f, 0.5f * PiConstant - LimbAngle);
        const float Fraction = std::clamp((Elevation - LimbAngle) / Span, 0.0f, 1.0f);
        VCoord = 1.0f - 0.5f * std::sqrt(std::max(0.0f, 1.0f - Fraction));
    }
    return { UCoord, std::clamp(VCoord, 0.0f, 1.0f) };
}

// 📝 Shade planetary globe surface from outer space (continents, polar caps, Cox-Munk ocean sun glint, ForceField cyclone cloud deck, night cities).
[[nodiscard]] SpectralColor EvaluateOrbitalPlanetSurface(const RadiometricSkyTables&        Tables,
                                                         const RadiometricSkyConfiguration& Cfg,
                                                         const Vector3f&                    SurfacePointKm,
                                                         const Vector3f&                    ViewDirection,
                                                         const Vector3f&                    SunDirection,
                                                         const AtmosphericWindField*        OptionalWindField) noexcept
{
    const Vector3f SurfaceNormal = NormalizeVector(SurfacePointKm);
    const float    RawSunDot     = DotVector(SurfaceNormal, SunDirection);
    const float    SunCosine     = std::max(0.0f, RawSunDot);
    const float    LatitudeAbs   = std::fabs(SurfaceNormal.ZCoord);

    // Solar transmittance reaching the planetary surface (naturally reddens along the sunset/sunrise terminator!).
    const SpectralColor SurfaceSunTrans = (RawSunDot > -0.06f)
        ? EvaluateTransmittanceToSpace(Tables, Cfg, Cfg.BottomRadiusKilometres + 0.02f, std::max(-0.04f, RawSunDot))
        : SpectralColor{ 0.0f, 0.0f, 0.0f };
    const SpectralColor SkyMultiBounce = SampleMultiScatterLut(
        Tables, Cfg, Cfg.BottomRadiusKilometres + 0.05f, RawSunDot);

    // Multi-scale continents, coastal shelves, forests, deserts, and polar ice caps.
    const float MacroContinent = EvaluateRotatedFbm3(
        SurfaceNormal.XCoord * 2.55f + 4.2f,
        SurfaceNormal.YCoord * 2.55f - 1.9f,
        SurfaceNormal.ZCoord * 2.55f + 0.8f,
        6u);
    const float LandFraction = std::clamp((MacroContinent - 0.495f) / 0.045f, 0.0f, 1.0f);
    const float ShelfMask    = std::clamp((MacroContinent - 0.455f) / 0.045f, 0.0f, 1.0f) * (1.0f - LandFraction);
    const float PolarIce     = std::clamp((LatitudeAbs - 0.79f) / 0.11f, 0.0f, 1.0f);

    SpectralColor Albedo{
        0.014f + 0.018f * ShelfMask,
        0.042f + 0.068f * ShelfMask,
        0.118f + 0.065f * ShelfMask };

    if (LandFraction > 0.0f)
    {
        const float AridityNoise = EvaluateRotatedFbm3(
            SurfaceNormal.XCoord * 6.8f - 2.1f,
            SurfaceNormal.YCoord * 6.8f + 4.7f,
            SurfaceNormal.ZCoord * 6.8f + 1.3f,
            4u);
        const float ReliefNoise  = EvaluateRotatedFbm3(
            SurfaceNormal.XCoord * 18.0f,
            SurfaceNormal.YCoord * 18.0f,
            SurfaceNormal.ZCoord * 18.0f,
            3u);
        const float AridWeight   = std::clamp((AridityNoise - 0.43f) / 0.24f, 0.0f, 1.0f) * (1.0f - 0.55f * LatitudeAbs);
        const float AlpineSnow   = std::clamp((ReliefNoise - 0.68f) / 0.12f, 0.0f, 1.0f);

        SpectralColor LandColor{
            0.055f + (0.285f - 0.055f) * AridWeight,
            0.125f + (0.215f - 0.125f) * AridWeight,
            0.046f + (0.132f - 0.046f) * AridWeight };
        LandColor.Red   = LandColor.Red   * (1.0f - AlpineSnow) + 0.68f * AlpineSnow;
        LandColor.Green = LandColor.Green * (1.0f - AlpineSnow) + 0.71f * AlpineSnow;
        LandColor.Blue  = LandColor.Blue  * (1.0f - AlpineSnow) + 0.76f * AlpineSnow;

        Albedo.Red   = Albedo.Red   * (1.0f - LandFraction) + LandColor.Red   * LandFraction;
        Albedo.Green = Albedo.Green * (1.0f - LandFraction) + LandColor.Green * LandFraction;
        Albedo.Blue  = Albedo.Blue  * (1.0f - LandFraction) + LandColor.Blue  * LandFraction;
    }

    if (PolarIce > 0.0f)
    {
        Albedo.Red   = Albedo.Red   * (1.0f - PolarIce) + 0.74f * PolarIce;
        Albedo.Green = Albedo.Green * (1.0f - PolarIce) + 0.78f * PolarIce;
        Albedo.Blue  = Albedo.Blue  * (1.0f - PolarIce) + 0.84f * PolarIce;
    }

    // Cox-Munk rough-sea Fresnel solar glint over ice-free ocean.
    const Vector3f ToEye      = ScaleVector(ViewDirection, -1.0f);
    const Vector3f HalfVector = NormalizeVector(AddVector(SunDirection, ToEye));
    const float    NormalHalf = std::max(0.0f, DotVector(SurfaceNormal, HalfVector));
    const float    ViewHover  = std::max(0.02f, DotVector(SurfaceNormal, ToEye));
    const float    Fresnel    = 0.020f + 0.980f * std::pow(1.0f - ViewHover, 5.0f);
    const float    RoughnessSq = 0.028f;
    const float    CosHalfSq  = std::max(1.0e-4f, NormalHalf * NormalHalf);
    const float    TanHalfSq  = std::max(0.0f, (1.0f - CosHalfSq) / CosHalfSq);
    const float    SlopeDist  = std::exp(-TanHalfSq / RoughnessSq) / (PiConstant * RoughnessSq * CosHalfSq * CosHalfSq);
    const float    OceanMask  = (1.0f - LandFraction) * (1.0f - PolarIce);
    const float    OceanGlint = OceanMask * std::min(2.2f, Fresnel * SlopeDist * 0.065f) * std::clamp(RawSunDot * 4.0f, 0.0f, 1.0f);

    // Orbital cloud deck advected by zonal rotation + AtmosphericWindField mesoscale cyclone swirl.
    float CloudOpacity = 0.0f;
    if (Cfg.PlanetaryCloudCoverage > 0.01f)
    {
        const float ZonalSpin = Cfg.SequenceSeconds * 0.0018f;
        const float CosZ      = std::cos(ZonalSpin);
        const float SinZ      = std::sin(ZonalSpin);
        Vector3f CloudSphereCoord{
            SurfaceNormal.XCoord * CosZ + SurfaceNormal.YCoord * SinZ,
            -SurfaceNormal.XCoord * SinZ + SurfaceNormal.YCoord * CosZ,
            SurfaceNormal.ZCoord };

        if (OptionalWindField != nullptr && OptionalWindField->FieldEnabled)
        {
            const Vector3f LocalTangentPos{
                CloudSphereCoord.XCoord * 12000.0f,
                CloudSphereCoord.YCoord * 12000.0f,
                2200.0f };
            const Vector3f AdvectedLocal = IntegrateStreamlineBackward(
                *OptionalWindField, LocalTangentPos, Cfg.SequenceSeconds, Cfg.AdvectionSpanSeconds, 4u);
            CloudSphereCoord.XCoord += (AdvectedLocal.XCoord - LocalTangentPos.XCoord) / 28000.0f;
            CloudSphereCoord.YCoord += (AdvectedLocal.YCoord - LocalTangentPos.YCoord) / 28000.0f;
        }

        // Synoptic frontal wave + mesoscale cyclonic spiral arm over mid-latitudes.
        const float Longitude = std::atan2(CloudSphereCoord.YCoord, CloudSphereCoord.XCoord);
        const float Latitude  = std::asin(std::clamp(CloudSphereCoord.ZCoord, -1.0f, 1.0f));
        const float EyeDeltaX = Longitude - 0.32f;
        const float EyeDeltaY = Latitude - 0.34f;
        const float EyeRadius = std::sqrt(EyeDeltaX * EyeDeltaX + EyeDeltaY * EyeDeltaY);
        if (EyeRadius < 0.52f)
        {
            const float SwirlAngle = (1.0f - EyeRadius / 0.52f) * 2.15f;
            const float CS = std::cos(SwirlAngle), SS = std::sin(SwirlAngle);
            CloudSphereCoord.XCoord += (EyeDeltaX * CS - EyeDeltaY * SS - EyeDeltaX) * 0.52f;
            CloudSphereCoord.YCoord += (EyeDeltaX * SS + EyeDeltaY * CS - EyeDeltaY) * 0.52f;
        }

        const float SynopticFbm = EvaluateRotatedFbm3(
            CloudSphereCoord.XCoord * 4.4f + 1.7f,
            CloudSphereCoord.YCoord * 4.4f - 2.3f,
            CloudSphereCoord.ZCoord * 4.4f + 3.1f,
            6u);
        const float EyeClear   = (EyeRadius < 0.048f) ? std::clamp(EyeRadius / 0.048f, 0.15f, 1.0f) : 1.0f;
        const float Threshold  = 0.64f - 0.28f * std::clamp(Cfg.PlanetaryCloudCoverage, 0.0f, 1.0f);
        CloudOpacity = std::clamp((SynopticFbm - Threshold) / 0.17f, 0.0f, 1.0f) * EyeClear;
    }

    const SpectralColor DirectIrradiance = MultiplySpectral(Cfg.ExtraterrestrialSolarFlux, SurfaceSunTrans);
    const SpectralColor SkyIrradiance    = MultiplySpectral(Cfg.ExtraterrestrialSolarFlux, SkyMultiBounce);
    const float         InvPi            = 1.0f / PiConstant;
    const float         CloudGroundShade = 1.0f - 0.62f * CloudOpacity;

    SpectralColor SurfaceRadiance{
        (Albedo.Red   * InvPi * SunCosine * CloudGroundShade + OceanGlint) * DirectIrradiance.Red   + Albedo.Red   * SkyIrradiance.Red   * 0.55f,
        (Albedo.Green * InvPi * SunCosine * CloudGroundShade + OceanGlint) * DirectIrradiance.Green + Albedo.Green * SkyIrradiance.Green * 0.55f,
        (Albedo.Blue  * InvPi * SunCosine * CloudGroundShade + OceanGlint) * DirectIrradiance.Blue  + Albedo.Blue  * SkyIrradiance.Blue  * 0.55f };

    // Night-hemisphere warm sodium/LED urban clusters along continental coasts.
    const float NightMask = std::clamp((-RawSunDot - 0.045f) / 0.12f, 0.0f, 1.0f) * LandFraction * (1.0f - PolarIce);
    if (NightMask > 0.005f)
    {
        const float UrbanCluster = EvaluateRotatedFbm3(
            SurfaceNormal.XCoord * 22.0f, SurfaceNormal.YCoord * 22.0f, SurfaceNormal.ZCoord * 22.0f, 4u);
        const float CoastBias    = 1.0f - std::clamp((MacroContinent - 0.50f) / 0.11f, 0.0f, 0.7f);
        const float CityFlux     = std::clamp((UrbanCluster - 0.61f) / 0.10f, 0.0f, 1.0f)
                                 * NightMask * CoastBias * (1.0f - 0.72f * CloudOpacity);
        SurfaceRadiance.Red   += CityFlux * 0.26f;
        SurfaceRadiance.Green += CityFlux * 0.15f;
        SurfaceRadiance.Blue  += CityFlux * 0.045f;
    }

    // Illuminated cloud top layer sitting at 4..9 km altitude (catches sunlight slightly past the ground terminator!).
    if (CloudOpacity > 0.002f)
    {
        const SpectralColor CloudSunTrans = (RawSunDot > -0.095f)
            ? EvaluateTransmittanceToSpace(Tables, Cfg, Cfg.BottomRadiusKilometres + 6.5f, std::max(-0.07f, RawSunDot))
            : SpectralColor{ 0.0f, 0.0f, 0.0f };
        const float CloudCosine = std::max(0.0f, RawSunDot + 0.035f);
        const SpectralColor CloudTopRadiance{
            (0.86f * InvPi * CloudCosine * CloudSunTrans.Red   + SkyMultiBounce.Red   * 0.65f) * Cfg.ExtraterrestrialSolarFlux.Red,
            (0.87f * InvPi * CloudCosine * CloudSunTrans.Green + SkyMultiBounce.Green * 0.65f) * Cfg.ExtraterrestrialSolarFlux.Green,
            (0.89f * InvPi * CloudCosine * CloudSunTrans.Blue  + SkyMultiBounce.Blue  * 0.65f) * Cfg.ExtraterrestrialSolarFlux.Blue };
        const float Cover = CloudOpacity * 0.90f;
        SurfaceRadiance.Red   = SurfaceRadiance.Red   * (1.0f - Cover) + CloudTopRadiance.Red   * Cover;
        SurfaceRadiance.Green = SurfaceRadiance.Green * (1.0f - Cover) + CloudTopRadiance.Green * Cover;
        SurfaceRadiance.Blue  = SurfaceRadiance.Blue  * (1.0f - Cover) + CloudTopRadiance.Blue  * Cover;
    }

    return SurfaceRadiance;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

RadiometricSkyConfiguration ConstructSkyConfigurationFromProfile(const AtmosphereUniformBlock& Profile) noexcept
{
    RadiometricSkyConfiguration Cfg{};
    Cfg.BottomRadiusKilometres = Profile.BottomRadius;
    Cfg.TopRadiusKilometres    = Profile.TopRadius;
    Cfg.RayleighScatteringPerKm = SpectralColor{
        Profile.RayleighScatteringBase[0] * 1000.0f,
        Profile.RayleighScatteringBase[1] * 1000.0f,
        Profile.RayleighScatteringBase[2] * 1000.0f };
    Cfg.RayleighScaleHeightKm = std::max(0.1f, Profile.RayleighProfileLayer0[1]);
    Cfg.MieScatteringPerKm    = Profile.MieScatteringBase[0] * 1000.0f;
    Cfg.MieExtinctionPerKm    = Profile.MieExtinctionBase[0] * 1000.0f;
    Cfg.MieScaleHeightKm      = std::max(0.05f, Profile.MieProfileLayer0[1]);
    Cfg.MieAsymmetry          = Profile.MieAsymmetry;
    Cfg.OzoneAbsorptionPerKm  = SpectralColor{
        Profile.OzoneAbsorptionBase[0] * 1000.0f,
        Profile.OzoneAbsorptionBase[1] * 1000.0f,
        Profile.OzoneAbsorptionBase[2] * 1000.0f };
    Cfg.MultipleScatteringGain = Profile.MultipleScatteringFactor;

    float SunX = 0.0f, SunY = 0.0f, SunZ = 1.0f;
    Atmosphere::ResolveSolarDirectionSceneFrame(Profile, SunX, SunY, SunZ);
    Cfg.SolarDirectionScene = NormalizeVector(Vector3f{ SunX, SunY, SunZ });
    Cfg.ExtraterrestrialSolarFlux = SpectralColor{
        Profile.SolarIlluminance[0],
        Profile.SolarIlluminance[1],
        Profile.SolarIlluminance[2] };
    if (Profile.SolarDirection[3] > 0.0f)
        Cfg.ObserverAltitudeMetres = Profile.SolarDirection[3] * 1000.0f;
    Cfg.SequenceSeconds = Profile.SolarIlluminance[3];
    return Cfg;
}

void BakeRadiometricSkyTables(RadiometricSkyTables&              Tables,
                              const RadiometricSkyConfiguration& Configuration)
{
    Tables.TablesReadyCondition = false;

    // ① 256x64 Horizon-Parameterized Transmittance LUT T(r, mu).
    Tables.TransmittanceTexels.assign(
        static_cast<std::size_t>(RadiometricSkyTables::TransmittanceWidth) * RadiometricSkyTables::TransmittanceHeight,
        SpectralColor{ 1.0f, 1.0f, 1.0f });

    for (uint32_t Row = 0u; Row < RadiometricSkyTables::TransmittanceHeight; ++Row)
    {
        const float VCoord = (static_cast<float>(Row) + 0.5f) / static_cast<float>(RadiometricSkyTables::TransmittanceHeight);
        for (uint32_t Col = 0u; Col < RadiometricSkyTables::TransmittanceWidth; ++Col)
        {
            const float UCoord = (static_cast<float>(Col) + 0.5f) / static_cast<float>(RadiometricSkyTables::TransmittanceWidth);
            const auto [RadiusKm, VerticalCosine] = DecodeTransmittanceUv(
                UCoord, VCoord, Configuration.BottomRadiusKilometres, Configuration.TopRadiusKilometres);

            const float ExitDistKm = TraceAtmosphereExitKm(RadiusKm, VerticalCosine, Configuration.TopRadiusKilometres);
            SpectralColor OpticalDepth{ 0.0f, 0.0f, 0.0f };
            if (ExitDistKm > 0.0f)
            {
                constexpr uint32_t StepCount = 48u;
                const float StepKm = ExitDistKm / static_cast<float>(StepCount);
                for (uint32_t Step = 0u; Step < StepCount; ++Step)
                {
                    const float TravelKm   = (static_cast<float>(Step) + 0.5f) * StepKm;
                    const float SampleRad  = std::sqrt(std::max(
                        RadiusKm * RadiusKm + TravelKm * TravelKm + 2.0f * RadiusKm * VerticalCosine * TravelKm, 0.0f));
                    const float SampleAlt  = std::max(0.0f, SampleRad - Configuration.BottomRadiusKilometres);
                    const auto  Medium     = SampleMediumAtAltitudeKm(Configuration, SampleAlt);
                    OpticalDepth.Red   += Medium.TotalExtinction.Red   * StepKm;
                    OpticalDepth.Green += Medium.TotalExtinction.Green * StepKm;
                    OpticalDepth.Blue  += Medium.TotalExtinction.Blue  * StepKm;
                }
            }

            Tables.TransmittanceTexels[static_cast<std::size_t>(Row) * RadiometricSkyTables::TransmittanceWidth + Col] = SpectralColor{
                std::exp(-OpticalDepth.Red),
                std::exp(-OpticalDepth.Green),
                std::exp(-OpticalDepth.Blue) };
        }
    }

    Tables.TablesReadyCondition = true;

    // ② 32x32 Isotropic Multiple-Scattering LUT Psi_ms(r, mu_s) via 64 Fibonacci Sphere Directions.
    Tables.MultiScatterTexels.assign(
        static_cast<std::size_t>(RadiometricSkyTables::MultiScatterWidth) * RadiometricSkyTables::MultiScatterHeight,
        SpectralColor{ 0.0f, 0.0f, 0.0f });

    constexpr uint32_t DirectionCount = 64u;
    constexpr uint32_t MarchStepCount = 24u;
    constexpr float    GoldenAngle    = 2.399963229728653f;
    constexpr float    IsotropicPhase = 1.0f / (4.0f * PiConstant);
    const SpectralColor CoolOceanAlbedo{ 0.018f, 0.038f, 0.078f };

    for (uint32_t Row = 0u; Row < RadiometricSkyTables::MultiScatterHeight; ++Row)
    {
        const float VCoord   = (static_cast<float>(Row) + 0.5f) / static_cast<float>(RadiometricSkyTables::MultiScatterHeight);
        const float RadiusKm = Configuration.BottomRadiusKilometres + 0.002f
                             + VCoord * (Configuration.TopRadiusKilometres - Configuration.BottomRadiusKilometres - 0.004f);

        for (uint32_t Col = 0u; Col < RadiometricSkyTables::MultiScatterWidth; ++Col)
        {
            const float UCoord      = (static_cast<float>(Col) + 0.5f) / static_cast<float>(RadiometricSkyTables::MultiScatterWidth);
            const float SolarCosine = UCoord * 2.0f - 1.0f;
            const Vector3f SunDir{ std::sqrt(std::max(0.0f, 1.0f - SolarCosine * SolarCosine)), 0.0f, SolarCosine };
            const Vector3f OriginKm{ 0.0f, 0.0f, RadiusKm };

            SpectralColor SecondOrderSum{ 0.0f, 0.0f, 0.0f };
            SpectralColor TransferSum{ 0.0f, 0.0f, 0.0f };

            for (uint32_t DirIdx = 0u; DirIdx < DirectionCount; ++DirIdx)
            {
                const float ZDir  = 1.0f - 2.0f * (static_cast<float>(DirIdx) + 0.5f) / static_cast<float>(DirectionCount);
                const float RPlan = std::sqrt(std::max(0.0f, 1.0f - ZDir * ZDir));
                const float Phi   = static_cast<float>(DirIdx) * GoldenAngle;
                const Vector3f ViewDir{ RPlan * std::cos(Phi), RPlan * std::sin(Phi), ZDir };

                const float GroundHit = TraceGroundContactKm(RadiusKm, ZDir, Configuration.BottomRadiusKilometres);
                const float TopExit   = TraceAtmosphereExitKm(RadiusKm, ZDir, Configuration.TopRadiusKilometres);
                const float PathKm    = (GroundHit > 0.0f) ? GroundHit : std::max(0.0f, TopExit);
                if (PathKm <= 0.0f)
                    continue;

                const float StepKm = PathKm / static_cast<float>(MarchStepCount);
                SpectralColor Throughput{ 1.0f, 1.0f, 1.0f };

                for (uint32_t Step = 0u; Step < MarchStepCount; ++Step)
                {
                    const float    TravelKm   = (static_cast<float>(Step) + 0.5f) * StepKm;
                    const Vector3f SamplePos  = AddVector(OriginKm, ScaleVector(ViewDir, TravelKm));
                    const float    SampleRad  = std::max(Configuration.BottomRadiusKilometres + 1.0e-4f, VectorLength(SamplePos));
                    const Vector3f LocalUp    = ScaleVector(SamplePos, 1.0f / SampleRad);
                    const float    LocalSunMu = DotVector(LocalUp, SunDir);
                    const auto     Medium     = SampleMediumAtAltitudeKm(Configuration, SampleRad - Configuration.BottomRadiusKilometres);

                    const SpectralColor StepTrans{
                        std::exp(-Medium.TotalExtinction.Red   * StepKm),
                        std::exp(-Medium.TotalExtinction.Green * StepKm),
                        std::exp(-Medium.TotalExtinction.Blue  * StepKm) };

                    const SpectralColor SunTrans = EvaluateTransmittanceToSpace(Tables, Configuration, SampleRad, LocalSunMu);
                    const SpectralColor StepIntegral{
                        (1.0f - StepTrans.Red)   / std::max(1.0e-6f, Medium.TotalExtinction.Red),
                        (1.0f - StepTrans.Green) / std::max(1.0e-6f, Medium.TotalExtinction.Green),
                        (1.0f - StepTrans.Blue)  / std::max(1.0e-6f, Medium.TotalExtinction.Blue) };

                    TransferSum.Red   += Throughput.Red   * Medium.TotalScattering.Red   * StepIntegral.Red;
                    TransferSum.Green += Throughput.Green * Medium.TotalScattering.Green * StepIntegral.Green;
                    TransferSum.Blue  += Throughput.Blue  * Medium.TotalScattering.Blue  * StepIntegral.Blue;

                    SecondOrderSum.Red   += Throughput.Red   * Medium.TotalScattering.Red   * IsotropicPhase * SunTrans.Red   * StepIntegral.Red;
                    SecondOrderSum.Green += Throughput.Green * Medium.TotalScattering.Green * IsotropicPhase * SunTrans.Green * StepIntegral.Green;
                    SecondOrderSum.Blue  += Throughput.Blue  * Medium.TotalScattering.Blue  * IsotropicPhase * SunTrans.Blue  * StepIntegral.Blue;

                    Throughput = MultiplySpectral(Throughput, StepTrans);
                }

                if (GroundHit > 0.0f)
                {
                    const Vector3f ContactPos  = AddVector(OriginKm, ScaleVector(ViewDir, GroundHit));
                    const Vector3f ContactUp   = NormalizeVector(ContactPos);
                    const float    ContactMuS  = DotVector(ContactUp, SunDir);
                    if (ContactMuS > 0.0f)
                    {
                        const SpectralColor GroundSunT = EvaluateTransmittanceToSpace(
                            Tables, Configuration, Configuration.BottomRadiusKilometres + 0.002f, ContactMuS);
                        const float DiffuseFactor = ContactMuS / PiConstant;
                        SecondOrderSum.Red   += Throughput.Red   * CoolOceanAlbedo.Red   * GroundSunT.Red   * DiffuseFactor;
                        SecondOrderSum.Green += Throughput.Green * CoolOceanAlbedo.Green * GroundSunT.Green * DiffuseFactor;
                        SecondOrderSum.Blue  += Throughput.Blue  * CoolOceanAlbedo.Blue  * GroundSunT.Blue  * DiffuseFactor;
                    }
                }
            }

            const float InvDirs = 1.0f / static_cast<float>(DirectionCount);
            SecondOrderSum = ScaleSpectral(SecondOrderSum, InvDirs);
            TransferSum    = ScaleSpectral(TransferSum, InvDirs);

            Tables.MultiScatterTexels[static_cast<std::size_t>(Row) * RadiometricSkyTables::MultiScatterWidth + Col] = SpectralColor{
                SecondOrderSum.Red   / std::max(1.0e-3f, 1.0f - TransferSum.Red),
                SecondOrderSum.Green / std::max(1.0e-3f, 1.0f - TransferSum.Green),
                SecondOrderSum.Blue  / std::max(1.0e-3f, 1.0f - TransferSum.Blue) };
        }
    }

    // ③ 384x192 Limb-Centered Clear-Sky Radiance LUT (for fast environment hemisphere queries).
    Tables.SkyViewTexels.assign(
        static_cast<std::size_t>(RadiometricSkyTables::SkyViewWidth) * RadiometricSkyTables::SkyViewHeight,
        SpectralColor{ 0.0f, 0.0f, 0.0f });

    RadiometricSkyConfiguration CleanLutCfg = Configuration;
    CleanLutCfg.HighAltitudeCirrusCoverage  = 0.0f;
    CleanLutCfg.TroposphericHazeStrength    = 0.0f;
    CleanLutCfg.OrbitalPlanetSurfaceEnabled = false;
    CleanLutCfg.ObserverAltitudeMetres      = std::clamp(
        Configuration.ObserverAltitudeMetres,
        2.0f,
        (Configuration.TopRadiusKilometres - Configuration.BottomRadiusKilometres - 0.5f) * 1000.0f);

    const float ObserverRadiusKm = CleanLutCfg.BottomRadiusKilometres + CleanLutCfg.ObserverAltitudeMetres * 0.001f;
    for (uint32_t Row = 0u; Row < RadiometricSkyTables::SkyViewHeight; ++Row)
    {
        const float VCoord = (static_cast<float>(Row) + 0.5f) / static_cast<float>(RadiometricSkyTables::SkyViewHeight);
        for (uint32_t Col = 0u; Col < RadiometricSkyTables::SkyViewWidth; ++Col)
        {
            const float    UCoord  = (static_cast<float>(Col) + 0.5f) / static_cast<float>(RadiometricSkyTables::SkyViewWidth);
            const Vector3f Bearing = DecodeSkyViewBearingScene(
                UCoord, VCoord, ObserverRadiusKm, CleanLutCfg.BottomRadiusKilometres);
            const SkyRaymarchResult Ray = EvaluateSphericalAtmosphereRay(
                Tables, CleanLutCfg, Vector3f{ 0.0f, 0.0f, 0.0f }, Bearing, nullptr);
            Tables.SkyViewTexels[static_cast<std::size_t>(Row) * RadiometricSkyTables::SkyViewWidth + Col] = Ray.InscatteredRadiance;
        }
    }
}

SpectralColor EvaluateTransmittanceToSpace(const RadiometricSkyTables&        Tables,
                                           const RadiometricSkyConfiguration& Configuration,
                                           float                              RadiusKilometres,
                                           float                              VerticalCosine) noexcept
{
    if (RadiusKilometres >= Configuration.TopRadiusKilometres)
        return SpectralColor{ 1.0f, 1.0f, 1.0f };

    // 🔴 True planetary shadow check: if the solar ray hits the solid planet sphere, direct solar transmittance is strictly zero.
    if (TraceGroundContactKm(RadiusKilometres, VerticalCosine, Configuration.BottomRadiusKilometres) > 0.0f)
        return SpectralColor{ 0.0f, 0.0f, 0.0f };

    if (!Tables.TablesReadyCondition)
    {
        const float ExitKm = TraceAtmosphereExitKm(RadiusKilometres, VerticalCosine, Configuration.TopRadiusKilometres);
        const Vector3f OriginKm{ 0.0f, 0.0f, RadiusKilometres };
        const Vector3f Dir{ std::sqrt(std::max(0.0f, 1.0f - VerticalCosine * VerticalCosine)), 0.0f, VerticalCosine };
        return IntegrateDirectTransmittanceKm(Configuration, OriginKm, Dir, ExitKm, 32u);
    }

    const auto [UCoord, VCoord] = EncodeTransmittanceUv(
        RadiusKilometres, VerticalCosine, Configuration.BottomRadiusKilometres, Configuration.TopRadiusKilometres);
    return SampleBilinearTable(
        Tables.TransmittanceTexels,
        RadiometricSkyTables::TransmittanceWidth,
        RadiometricSkyTables::TransmittanceHeight,
        UCoord,
        VCoord,
        false);
}

SpectralColor SampleSkyViewTable(const RadiometricSkyTables&        Tables,
                                 const RadiometricSkyConfiguration& Configuration,
                                 const Vector3f&                    RayDirectionScene) noexcept
{
    if (!Tables.TablesReadyCondition)
        return SpectralColor{ 0.0f, 0.0f, 0.0f };

    const float ClampedAltKm     = std::clamp(
        Configuration.ObserverAltitudeMetres * 0.001f,
        0.002f,
        Configuration.TopRadiusKilometres - Configuration.BottomRadiusKilometres - 0.5f);
    const float ObserverRadiusKm = Configuration.BottomRadiusKilometres + ClampedAltKm;
    const auto [UCoord, VCoord]  = EncodeSkyViewUvScene(
        NormalizeVector(RayDirectionScene), ObserverRadiusKm, Configuration.BottomRadiusKilometres);

    return SampleBilinearTable(
        Tables.SkyViewTexels,
        RadiometricSkyTables::SkyViewWidth,
        RadiometricSkyTables::SkyViewHeight,
        UCoord,
        VCoord,
        true);
}

void BakeSkyAdvectionOffsetField(SkyAdvectionOffsetField&           Field,
                                 const RadiometricSkyConfiguration& Configuration,
                                 const AtmosphericWindField*        OptionalWindField) noexcept
{
    Field = SkyAdvectionOffsetField{};
    if (OptionalWindField == nullptr || !OptionalWindField->FieldEnabled)
        return;

    const float SpanSeconds = Configuration.AdvectionSpanSeconds + std::max(0.0f, Configuration.SequenceSeconds);
    if (SpanSeconds <= 1.0e-3f)
        return;

    const float PlanetRadiusKm = Configuration.BottomRadiusKilometres;
    const float MidAltitudeKm  = 0.5f * (0.4f + 2.4f);
    const float CirrusAltKm    = 9.8f;

    for (uint32_t CellIdx = 0u; CellIdx < SkyAdvectionOffsetField::CellCount; ++CellIdx)
    {
        const float AzimuthRad = (static_cast<float>(CellIdx) + 0.5f)
                               * (2.0f * PiConstant / static_cast<float>(SkyAdvectionOffsetField::CellCount));
        const float DirX = std::cos(AzimuthRad);
        const float DirY = std::sin(AzimuthRad);

        // Probe the representative mass point of each stratum (~9 km downwind at typical 15 m/s) and store its Lagrangian offset.
        auto ResolveLag = [&](float AltitudeKm) noexcept -> Vector3f
        {
            const Vector3f ProbeMetres{ DirX * 9000.0f, DirY * 9000.0f, AltitudeKm * 1000.0f };
            const Vector3f Origin = IntegrateStreamlineBackward(
                *OptionalWindField,
                ProbeMetres,
                Configuration.SequenceSeconds,
                SpanSeconds,
                4u,
                AdvectionMediumTarget::SkyAerosolCirrus);
            return Vector3f{ Origin.XCoord - ProbeMetres.XCoord, Origin.YCoord - ProbeMetres.YCoord, 0.0f };
        };

        const Vector3f LowLag  = ResolveLag(MidAltitudeKm);
        const Vector3f HighLag = ResolveLag(CirrusAltKm);
        Field.LowLagX[CellIdx]  = LowLag.XCoord;
        Field.LowLagY[CellIdx]  = LowLag.YCoord;
        Field.HighLagX[CellIdx] = HighLag.XCoord;
        Field.HighLagY[CellIdx] = HighLag.YCoord;

        // Flag the field as live only if the flow actually displaces the aerosol mass at synoptic scales.
        if (std::fabs(LowLag.XCoord) + std::fabs(LowLag.YCoord)
          + std::fabs(HighLag.XCoord) + std::fabs(HighLag.YCoord) > 1.0f)
            Field.ReadyCondition = true;
    }

    Field.OuterRadiusKm = Configuration.TopRadiusKilometres + 20.0f;
    Field.LayerSplitKm  = 8.0f;
    (void)PlanetRadiusKm;
}

SkyRaymarchResult EvaluateSphericalAtmosphereRay(const RadiometricSkyTables&        Tables,
                                                 const RadiometricSkyConfiguration& Configuration,
                                                 const Vector3f&                    CameraSceneMetres,
                                                 const Vector3f&                    RayDirectionScene,
                                                 const AtmosphericWindField*        OptionalWindField,
                                                 const SkyAdvectionOffsetField*     OptionalAdvectionField) noexcept
{
    SkyRaymarchResult Result{};
    const Vector3f ViewDir = NormalizeVector(RayDirectionScene);
    const Vector3f SunDir  = NormalizeVector(Configuration.SolarDirectionScene);

    // Resolve planet-centered camera origin in kilometres (supports both scene-local Z-up and planetary orbital positions).
    const float RawCamMagMetres    = VectorLength(CameraSceneMetres);
    const bool  PlanetCenteredMode = (RawCamMagMetres > Configuration.BottomRadiusKilometres * 500.0f);
    const Vector3f CameraOriginKm  = PlanetCenteredMode
        ? ScaleVector(CameraSceneMetres, 0.001f)
        : Vector3f{
              CameraSceneMetres.XCoord * 0.001f,
              CameraSceneMetres.YCoord * 0.001f,
              Configuration.BottomRadiusKilometres + std::max(1.5f, Configuration.ObserverAltitudeMetres + CameraSceneMetres.ZCoord) * 0.001f };

    const float ObserverRadiusKm = VectorLength(CameraOriginKm);
    const bool  OrbitalRegime    = Configuration.OrbitalPlanetSurfaceEnabled
                                || (ObserverRadiusKm > Configuration.TopRadiusKilometres - 2.0f);

    const auto [AtmoNearKm, AtmoFarKm]     = IntersectPlanetCenteredSphereKm(CameraOriginKm, ViewDir, Configuration.TopRadiusKilometres);
    const auto [PlanetNearKm, PlanetFarKm] = IntersectPlanetCenteredSphereKm(CameraOriginKm, ViewDir, Configuration.BottomRadiusKilometres);
    (void)PlanetFarKm;

    const bool HitsPlanet = (PlanetNearKm > 0.0f);
    Result.PlanetSurfaceHitCondition = HitsPlanet;
    Result.PlanetContactDistanceKm   = HitsPlanet ? PlanetNearKm : -1.0f;

    if (AtmoFarKm <= 0.0f)
    {
        // Ray is completely outside the planetary atmosphere in deep space.
        return Result;
    }

    const float SegmentStartKm = std::max(0.0f, AtmoNearKm);
    const float SegmentEndKm   = HitsPlanet ? PlanetNearKm : AtmoFarKm;
    if (SegmentEndKm <= SegmentStartKm)
    {
        if (HitsPlanet)
            Result.ViewTransmittance = SpectralColor{ 0.0f, 0.0f, 0.0f };
        return Result;
    }

    const float CosineSunView = DotVector(ViewDir, SunDir);
    const float PhaseRayleigh = EvaluateRayleighPhase(CosineSunView);
    const float PhaseMie      = EvaluateCornetteShanksPhase(CosineSunView, Configuration.MieAsymmetry);
    const float PhaseIce      = 0.72f * EvaluateHenyeyGreensteinPhase(CosineSunView, 0.82f)
                              + 0.28f * EvaluateHenyeyGreensteinPhase(CosineSunView, -0.36f);

    // ① Baked azimuthal Lagrangian offset field: interpolate the haze + cirrus offsets once per ray so every march sample is a
    //    pair of scalar adds instead of a fresh semi-Lagrangian characteristic integration.
    struct SkyAdvectionStar
    {
        float LowLagXCell        = 0.0f;   // [m] - Boundary-layer haze Lagrangian X offset
        float LowLagYCell        = 0.0f;   // [m] - Boundary-layer haze Lagrangian Y offset
        float LowConvergenceCell = 0.0f;   // [-] - Local Mie aerosol convergence (lifts streamer contrast)
    };

    const bool BakedAdvectionActive =
        (OptionalAdvectionField != nullptr) && OptionalAdvectionField->ReadyCondition
     && (Configuration.TroposphericHazeStrength > 1.0e-3f || Configuration.HighAltitudeCirrusCoverage > 1.0e-3f);

    SkyAdvectionStar Star{};
    float InterpolatedHighLagX = 0.0f;
    float InterpolatedHighLagY = 0.0f;

    if (BakedAdvectionActive)
    {
        constexpr uint32_t CellCount = SkyAdvectionOffsetField::CellCount;
        const float AzimuthRad = std::atan2(ViewDir.YCoord, ViewDir.XCoord);
        const float CellCoordF = (AzimuthRad / (2.0f * PiConstant) + 0.5f) * static_cast<float>(CellCount);
        const float CellWrapped = CellCoordF - std::floor(CellCoordF / static_cast<float>(CellCount)) * static_cast<float>(CellCount);
        const uint32_t CellLow  = static_cast<uint32_t>(CellWrapped) % CellCount;
        const uint32_t CellHigh = (CellLow + 1u) % CellCount;
        const float    CellFrac = std::clamp(CellWrapped - std::floor(CellWrapped), 0.0f, 1.0f);

        Star.LowLagXCell = OptionalAdvectionField->LowLagX[CellLow] * (1.0f - CellFrac)
                         + OptionalAdvectionField->LowLagX[CellHigh] * CellFrac;
        Star.LowLagYCell = OptionalAdvectionField->LowLagY[CellLow] * (1.0f - CellFrac)
                         + OptionalAdvectionField->LowLagY[CellHigh] * CellFrac;
        InterpolatedHighLagX = OptionalAdvectionField->HighLagX[CellLow] * (1.0f - CellFrac)
                             + OptionalAdvectionField->HighLagX[CellHigh] * CellFrac;
        InterpolatedHighLagY = OptionalAdvectionField->HighLagY[CellLow] * (1.0f - CellFrac)
                             + OptionalAdvectionField->HighLagY[CellHigh] * CellFrac;
    }

    const uint32_t StepCount = OrbitalRegime ? 48u : 44u;
    const float    StepKm    = (SegmentEndKm - SegmentStartKm) / static_cast<float>(StepCount);

    SpectralColor Throughput{ 1.0f, 1.0f, 1.0f };
    SpectralColor AccumulatedScatter{ 0.0f, 0.0f, 0.0f };
    float         MieOpticalDepth = 0.0f;

    for (uint32_t Step = 0u; Step < StepCount; ++Step)
    {
        const float    TravelKm   = SegmentStartKm + (static_cast<float>(Step) + 0.5f) * StepKm;
        const Vector3f SamplePos  = AddVector(CameraOriginKm, ScaleVector(ViewDir, TravelKm));
        const float    SampleRad  = std::max(Configuration.BottomRadiusKilometres + 1.0e-4f, VectorLength(SamplePos));
        const float    SampleAlt  = SampleRad - Configuration.BottomRadiusKilometres;
        const Vector3f LocalUp    = ScaleVector(SamplePos, 1.0f / SampleRad);
        const float    LocalSunMu = DotVector(LocalUp, SunDir);

        MediumCoefficients Medium = SampleMediumAtAltitudeKm(Configuration, SampleAlt);
        float              CirrusExtinctionKm = 0.0f;

        // ForceField-coupled sky participation: boundary-layer Mie streamers (0.12..2.4 km) + high-altitude ice cirrus (8.4..11.2 km).
        if (!OrbitalRegime && TravelKm < 130.0f)
        {
            const Vector3f ScenePosMetres{
                SamplePos.XCoord * 1000.0f,
                SamplePos.YCoord * 1000.0f,
                SampleAlt * 1000.0f };

            if (Configuration.TroposphericHazeStrength > 1.0e-3f && SampleAlt < 2.8f)
            {
                Vector3f LagPos = ScenePosMetres;
                float    ConvergenceBoost = 0.0f;
                if (BakedAdvectionActive)
                {
                    // Baked azimuthal offset field: one interpolation instead of a per-sample characteristic integration.
                    LagPos.XCoord += Star.LowLagXCell;
                    LagPos.YCoord += Star.LowLagYCell;
                    ConvergenceBoost = Star.LowConvergenceCell;
                }
                else if (OptionalWindField != nullptr && OptionalWindField->FieldEnabled)
                {
                    LagPos = IntegrateStreamlineBackward(
                        *OptionalWindField,
                        ScenePosMetres,
                        Configuration.SequenceSeconds,
                        Configuration.AdvectionSpanSeconds,
                        4u,
                        AdvectionMediumTarget::SkyAerosolCirrus);
                    ConvergenceBoost = EvaluateWindSample(
                        *OptionalWindField,
                        ScenePosMetres,
                        Configuration.SequenceSeconds,
                        AdvectionMediumTarget::SkyAerosolCirrus).ConvergenceScalar;
                }

                const float StreamerFbm = EvaluateRotatedFbm3(
                    LagPos.XCoord * 0.00022f + 1.4f,
                    LagPos.YCoord * 0.00022f - 0.8f,
                    LagPos.ZCoord * 0.00075f + 2.1f,
                    4u);
                const float HazeModulation = std::clamp(
                    1.0f + Configuration.TroposphericHazeStrength * ((StreamerFbm - 0.46f) * 2.1f + 0.45f * ConvergenceBoost),
                    0.35f,
                    3.2f);
                const float MieDelta = Medium.MieScattering * (HazeModulation - 1.0f);
                Medium.MieScattering         += MieDelta;
                Medium.TotalScattering.Red   += MieDelta;
                Medium.TotalScattering.Green += MieDelta;
                Medium.TotalScattering.Blue  += MieDelta;
                Medium.TotalExtinction.Red   += MieDelta * 1.11f;
                Medium.TotalExtinction.Green += MieDelta * 1.11f;
                Medium.TotalExtinction.Blue  += MieDelta * 1.11f;
            }

            if (Configuration.HighAltitudeCirrusCoverage > 1.0e-3f && SampleAlt > 8.0f && SampleAlt < 11.6f)
            {
                Vector3f CirrusLagPos = ScenePosMetres;
                if (BakedAdvectionActive)
                {
                    CirrusLagPos.XCoord += InterpolatedHighLagX;
                    CirrusLagPos.YCoord += InterpolatedHighLagY;
                }
                else if (OptionalWindField != nullptr && OptionalWindField->FieldEnabled)
                {
                    CirrusLagPos = IntegrateStreamlineBackward(
                        *OptionalWindField,
                        ScenePosMetres,
                        Configuration.SequenceSeconds,
                        Configuration.AdvectionSpanSeconds * 1.25f,
                        4u,
                        AdvectionMediumTarget::SkyAerosolCirrus);
                }

                const float LayerCenter  = 1.0f - std::fabs(SampleAlt - 9.8f) / 1.8f;
                const float LayerProfile = std::clamp(LayerCenter, 0.0f, 1.0f);
                // Anisotropic sheared ice-crystal mares-tail streaks + curled wisps.
                const float StreakFbm    = EvaluateRotatedFbm3(
                    CirrusLagPos.XCoord * 0.000095f + CirrusLagPos.ZCoord * 0.00042f,
                    CirrusLagPos.YCoord * 0.000185f - 3.1f,
                    CirrusLagPos.ZCoord * 0.00065f + 0.7f,
                    5u);
                const float WispDetail   = EvaluateRotatedFbm3(
                    CirrusLagPos.XCoord * 0.00034f - 2.2f,
                    CirrusLagPos.YCoord * 0.00034f + 5.4f,
                    CirrusLagPos.ZCoord * 0.00090f,
                    3u);
                const float Threshold    = 0.63f - 0.24f * std::clamp(Configuration.HighAltitudeCirrusCoverage, 0.0f, 1.0f);
                const float CirrusMask   = std::clamp((StreakFbm * 0.78f + WispDetail * 0.22f - Threshold) / 0.19f, 0.0f, 1.0f);
                CirrusExtinctionKm       = CirrusMask * LayerProfile * LayerProfile * 0.048f * Configuration.HighAltitudeCirrusCoverage;

                Medium.TotalScattering.Red   += CirrusExtinctionKm * 0.98f;
                Medium.TotalScattering.Green += CirrusExtinctionKm * 0.98f;
                Medium.TotalScattering.Blue  += CirrusExtinctionKm * 0.98f;
                Medium.TotalExtinction.Red   += CirrusExtinctionKm;
                Medium.TotalExtinction.Green += CirrusExtinctionKm;
                Medium.TotalExtinction.Blue  += CirrusExtinctionKm;
            }
        }

        MieOpticalDepth += (Medium.MieScattering + CirrusExtinctionKm * 0.35f) * StepKm;

        const SpectralColor StepTrans{
            std::exp(-Medium.TotalExtinction.Red   * StepKm),
            std::exp(-Medium.TotalExtinction.Green * StepKm),
            std::exp(-Medium.TotalExtinction.Blue  * StepKm) };

        const SpectralColor SunTrans     = EvaluateTransmittanceToSpace(Tables, Configuration, SampleRad, LocalSunMu);
        const SpectralColor MultiScatter = SampleMultiScatterLut(Tables, Configuration, SampleRad, LocalSunMu);

        const float CombinedMieIceScatter = Medium.MieScattering * PhaseMie + CirrusExtinctionKm * 0.98f * PhaseIce;
        const SpectralColor PhasedSingle{
            (Medium.RayleighScattering.Red   * PhaseRayleigh + CombinedMieIceScatter) * SunTrans.Red,
            (Medium.RayleighScattering.Green * PhaseRayleigh + CombinedMieIceScatter) * SunTrans.Green,
            (Medium.RayleighScattering.Blue  * PhaseRayleigh + CombinedMieIceScatter) * SunTrans.Blue };

        const SpectralColor InScatter{
            PhasedSingle.Red   + Medium.TotalScattering.Red   * MultiScatter.Red,
            PhasedSingle.Green + Medium.TotalScattering.Green * MultiScatter.Green,
            PhasedSingle.Blue  + Medium.TotalScattering.Blue  * MultiScatter.Blue };

        const SpectralColor StepIntegral{
            (1.0f - StepTrans.Red)   / std::max(1.0e-6f, Medium.TotalExtinction.Red),
            (1.0f - StepTrans.Green) / std::max(1.0e-6f, Medium.TotalExtinction.Green),
            (1.0f - StepTrans.Blue)  / std::max(1.0e-6f, Medium.TotalExtinction.Blue) };

        AccumulatedScatter.Red   += Throughput.Red   * InScatter.Red   * StepIntegral.Red;
        AccumulatedScatter.Green += Throughput.Green * InScatter.Green * StepIntegral.Green;
        AccumulatedScatter.Blue  += Throughput.Blue  * InScatter.Blue  * StepIntegral.Blue;

        Throughput = MultiplySpectral(Throughput, StepTrans);
    }

    Result.InscatteredRadiance       = MultiplySpectral(AccumulatedScatter, Configuration.ExtraterrestrialSolarFlux);
    Result.IntegratedMieOpticalDepth = MieOpticalDepth;

    if (!HitsPlanet)
    {
        // Subtle nocturnal oxygen/sodium airglow at ~92 km so true night sky carries realistic astronomical airglow (~22 mag/arcsec^2).
        const float SolarDepression = std::clamp(-SunDir.ZCoord, 0.0f, 1.0f);
        if (!OrbitalRegime && SolarDepression > 0.04f)
        {
            const float GrazingFactor = 1.0f / std::sqrt(std::max(0.06f, ViewDir.ZCoord * ViewDir.ZCoord + 0.06f));
            const float AirglowScale  = 0.00038f * std::min(2.4f, GrazingFactor) * std::clamp((SolarDepression - 0.04f) * 6.0f, 0.0f, 1.0f);
            Result.InscatteredRadiance.Red   += AirglowScale * 0.62f;
            Result.InscatteredRadiance.Green += AirglowScale * 0.95f;
            Result.InscatteredRadiance.Blue  += AirglowScale * 1.45f;
        }
        Result.ViewTransmittance = Throughput;
        return Result;
    }

    // Ray intersected the planetary sphere R_bottom -> background space is occluded.
    Result.ViewTransmittance = SpectralColor{ 0.0f, 0.0f, 0.0f };
    const Vector3f ContactPosKm = AddVector(CameraOriginKm, ScaleVector(ViewDir, PlanetNearKm));

    if (OrbitalRegime)
    {
        const SpectralColor PlanetRadiance = EvaluateOrbitalPlanetSurface(
            Tables, Configuration, ContactPosKm, ViewDir, SunDir, OptionalWindField);
        Result.InscatteredRadiance = AddSpectral(Result.InscatteredRadiance, MultiplySpectral(PlanetRadiance, Throughput));
    }
    else
    {
        // 📝 Terrestrial lower-hemisphere boundary: Cox-Munk wind-roughened sea-state Schlick Fresnel reflection + pure-water
        //    molecular upwelling scatter (strong H2O vibrational red absorption above 600 nm!). At grazing incidence (CosIncidence -> 0)
        //    Fresnel -> 1 reflects the immediate limb seamlessly; below the limb, tilted capillary/gravity wave facets sample the
        //    cool cobalt upper sky dome + deep-blue upwelling water scatter — ZERO darkened-orange brown or beige band!
        const Vector3f ContactNormal  = NormalizeVector(ContactPosKm);
        const Vector3f ReflectedDir   = SubtractVector(ViewDir, ScaleVector(ContactNormal, 2.0f * DotVector(ViewDir, ContactNormal)));
        const float    CosIncidence   = std::clamp(-DotVector(ViewDir, ContactNormal), 0.0f, 1.0f);
        const float    WaterFresnel   = 0.020f + 0.980f * std::pow(1.0f - CosIncidence, 5.0f);

        SpectralColor ReflectedSky{ 0.0f, 0.0f, 0.0f };
        SpectralColor OverheadDome{ 0.0f, 0.0f, 0.0f };
        if (Tables.TablesReadyCondition)
        {
            const float FacetLiftZ    = std::clamp(std::fabs(ReflectedDir.ZCoord) + 0.48f * std::sqrt(CosIncidence), 0.006f, 0.96f);
            const Vector3f WaveMirror = NormalizeVector(Vector3f{ ReflectedDir.XCoord, ReflectedDir.YCoord, FacetLiftZ });
            const SpectralColor SpecularSample = SampleSkyViewTable(Tables, Configuration, WaveMirror);
            OverheadDome = SampleSkyViewTable(Tables, Configuration, Vector3f{ 0.0f, 0.0f, 0.88f });

            const float DomeBlend = std::clamp(CosIncidence * 2.8f, 0.0f, 0.68f);
            ReflectedSky = SpectralColor{
                SpecularSample.Red   * (1.0f - DomeBlend) + OverheadDome.Red   * DomeBlend,
                SpecularSample.Green * (1.0f - DomeBlend) + OverheadDome.Green * DomeBlend,
                SpecularSample.Blue  * (1.0f - DomeBlend) + OverheadDome.Blue  * DomeBlend };
        }

        const float ContactSunMu = DotVector(ContactNormal, SunDir);
        const SpectralColor SubsurfaceSunT = (ContactSunMu > 0.0f)
            ? EvaluateTransmittanceToSpace(Tables, Configuration, Configuration.BottomRadiusKilometres + 0.005f, ContactSunMu)
            : SpectralColor{ 0.0f, 0.0f, 0.0f };

        const SpectralColor DeepWaterAlbedo{ 0.005f, 0.028f, 0.088f };
        const float         DiffuseSun = std::max(0.0f, ContactSunMu) / PiConstant;
        const SpectralColor WaterBodyRadiance{
            DeepWaterAlbedo.Red   * (SubsurfaceSunT.Red   * DiffuseSun * Configuration.ExtraterrestrialSolarFlux.Red   + OverheadDome.Red   * 0.55f),
            DeepWaterAlbedo.Green * (SubsurfaceSunT.Green * DiffuseSun * Configuration.ExtraterrestrialSolarFlux.Green + OverheadDome.Green * 0.65f),
            DeepWaterAlbedo.Blue  * (SubsurfaceSunT.Blue  * DiffuseSun * Configuration.ExtraterrestrialSolarFlux.Blue  + OverheadDome.Blue  * 0.78f) };

        const SpectralColor TroughTint{
            1.0f - 0.34f * (1.0f - WaterFresnel),
            1.0f - 0.10f * (1.0f - WaterFresnel),
            1.0f };

        const SpectralColor BoundaryRadiance{
            ReflectedSky.Red   * WaterFresnel * TroughTint.Red   + WaterBodyRadiance.Red   * (1.0f - WaterFresnel),
            ReflectedSky.Green * WaterFresnel * TroughTint.Green + WaterBodyRadiance.Green * (1.0f - WaterFresnel),
            ReflectedSky.Blue  * WaterFresnel * TroughTint.Blue  + WaterBodyRadiance.Blue  * (1.0f - WaterFresnel) };

        Result.InscatteredRadiance = AddSpectral(Result.InscatteredRadiance, MultiplySpectral(BoundaryRadiance, Throughput));
    }

    return Result;
}

SpectralColor EvaluateSolarPhotosphereCorona(const RadiometricSkyConfiguration& Configuration,
                                             const SkyRaymarchResult&           AtmosphereAlongRay,
                                             const Vector3f&                    RayDirectionScene,
                                             float                              PixelAngularPitchRadians) noexcept
{
    if (AtmosphereAlongRay.PlanetSurfaceHitCondition || Configuration.SolarDiscRadianceMultiplier <= 0.0f)
        return SpectralColor{ 0.0f, 0.0f, 0.0f };

    const Vector3f ViewDir    = NormalizeVector(RayDirectionScene);
    const Vector3f SunDir     = NormalizeVector(Configuration.SolarDirectionScene);
    const float    ChordLen   = VectorLength(SubtractVector(ViewDir, SunDir));
    const float    AngleRad   = 2.0f * std::asin(std::clamp(0.5f * ChordLen, 0.0f, 1.0f));
    const float    SolarRad   = std::max(0.0010f, Configuration.SolarAngularRadiusRadians);
    const float    PixelPitch = std::max(0.00025f, PixelAngularPitchRadians);

    // ① Neckel & Labs (1994) wavelength-dependent photospheric limb darkening I_lambda(mu) = 1 - u_lambda * (1 - mu^0.65).
    SpectralColor Photosphere{ 0.0f, 0.0f, 0.0f };
    const float AntiAliasHalfWidth = 0.65f * PixelPitch;
    if (AngleRad < SolarRad + AntiAliasHalfWidth)
    {
        const float EdgeCoverage = std::clamp((SolarRad + AntiAliasHalfWidth - AngleRad) / (2.0f * AntiAliasHalfWidth), 0.0f, 1.0f);
        const float SmoothEdge   = EdgeCoverage * EdgeCoverage * (3.0f - 2.0f * EdgeCoverage);
        const float NormRadius   = std::min(0.9995f, AngleRad / SolarRad);
        const float MuPhotosphere = std::sqrt(std::max(0.0f, 1.0f - NormRadius * NormRadius));
        const float MuPower      = std::pow(MuPhotosphere, 0.65f);

        const float LimbRed      = 1.0f - 0.52f * (1.0f - MuPower);
        const float LimbGreen    = 1.0f - 0.66f * (1.0f - MuPower);
        const float LimbBlue     = 1.0f - 0.80f * (1.0f - MuPower);
        const float DiscScale    = 26.0f * Configuration.SolarDiscRadianceMultiplier * SmoothEdge;

        Photosphere = SpectralColor{ LimbRed * DiscScale, LimbGreen * DiscScale, LimbBlue * DiscScale };
    }

    // ② Continuous circumsolar envelope. Four physically distinct distributions share the same three components:
    //      · Thomson electron K-corona      — always present, ~r^-2.45 near the limb steepening to r^-5.2 further out
    //      · van de Hulst aerosol aureole    — small-angle forward diffraction, proportional to line-of-sight Mie optical depth
    //      · Extended diffuse halo           — the character-specific wing that a viewer reads as "soft" or "hard" sun
    const float OuterRatio    = std::max(1.0f, AngleRad / SolarRad);
    const float AerosolWeight = 1.0f - std::exp(-AtmosphereAlongRay.IntegratedMieOpticalDepth * 3.2f);
    const float DiscGain      = Configuration.SolarDiscRadianceMultiplier;

    float CoronaGain   = 1.0f;   // Thomson K-corona weight
    float CoronaSteep  = 1.0f;   // power-law steepening of the outer envelope
    float AureoleGain  = 1.0f;   // near-limb diffraction weight
    float AureoleWidth = 1.0f;   // angular width scale of the near-limb term
    float HaloGain     = 1.0f;   // broad diffuse wing weight
    float HaloWidth    = 1.0f;   // e-folding angle of the broad wing
    float GlareGain    = 1.0f;   // overall envelope gain: how far this sun's light spills into the sky around it
    float DiscEdgeSoften = 1.0f; // > 1 widens the anti-aliased photosphere edge (soft disk)

    switch (Configuration.SolarCharacter)
    {
        case SolarDiscCharacter::GoldenHaze:
            // Warm, low-contrast: the aerosol forward lobe dominates and the disk reads slightly larger and softer.
            CoronaGain = 0.72f; CoronaSteep = 1.15f;
            AureoleGain = 1.55f; AureoleWidth = 1.85f;
            HaloGain = 2.35f; HaloWidth = 2.60f;
            DiscEdgeSoften = 2.15f;
            GlareGain = 4.2f;
            break;

        case SolarDiscCharacter::DiamondCorona:
            // Cold, dry, high-altitude air: almost no aerosol diffraction, a compact steep corona and a razor disk edge.
            CoronaGain = 1.30f; CoronaSteep = 0.72f;
            AureoleGain = 0.30f; AureoleWidth = 0.72f;
            HaloGain = 0.34f; HaloWidth = 0.52f;
            DiscEdgeSoften = 0.55f;
            GlareGain = 2.1f;
            break;

        case SolarDiscCharacter::OvercastDiffuse:
            // Sun behind thin cloud: no resolved disk, the flux arrives as an extended forward-scattered patch.
            CoronaGain = 0.30f; CoronaSteep = 1.40f;
            AureoleGain = 0.85f; AureoleWidth = 3.40f;
            HaloGain = 3.10f; HaloWidth = 5.20f;
            DiscEdgeSoften = 5.20f;
            GlareGain = 9.5f;
            break;

        case SolarDiscCharacter::SolarPhotosphere:
        default:
            // The referenced physical sun, lifted so its aureole reads against the horizon haze rather than looking flat, while
            // keeping the crisp pixel-accurate limb that the limb-darkening audit depends on.
            GlareGain = 1.65f;
            break;
    }

    // Soften the photosphere limb. The ramp width is a FRACTION of the solar radius, never a pixel width: a sub-pixel edge cannot
    // change how the disk reads at a 5 deg crop, it only hides aliasing. DiamondCorona and SolarPhotosphere keep the crisp
    // pixel-level anti-aliased limb; the others melt inward so the disk loses its hard outline.
    if (DiscEdgeSoften > 1.0f && AngleRad < SolarRad * 1.05f)
    {
        const float EdgeWidth = SolarRad * std::clamp(0.085f * DiscEdgeSoften, 0.0f, 0.62f);
        const float Coverage  = std::clamp((SolarRad - AngleRad) / std::max(1.0e-6f, EdgeWidth), 0.0f, 1.0f);
        const float SoftEdge  = Coverage * Coverage * (3.0f - 2.0f * Coverage);
        // Radiance is redistributed rather than multiplied: the softened disk spreads the same flux over its blur, so the peak
        // falls instead of a soft edge merely trimming a still-oversaturated centre.
        const float PeakScale = 1.0f / (1.0f + 0.62f * DiscEdgeSoften);
        Photosphere = ScaleSpectral(Photosphere, PeakScale + (1.0f - PeakScale) * SoftEdge);
    }

    const float ThomsonCorona = CoronaGain
                              * (0.085f * std::pow(OuterRatio, -2.45f * CoronaSteep)
                               + 0.032f * std::pow(OuterRatio, -5.2f * CoronaSteep)) * DiscGain;

    const float AureoleInner = AureoleGain * 0.42f / std::pow(
        1.0f + (AngleRad / (SolarRad * 1.85f * AureoleWidth)) * (AngleRad / (SolarRad * 1.85f * AureoleWidth)), 1.35f);
    const float AureoleOuter = HaloGain * 0.065f * std::exp(-AngleRad / std::max(0.018f, SolarRad * 9.5f * HaloWidth));
    // The envelope is scaled as a whole: the near-disk components are referenced to the photosphere, so lifting only the broad
    // wing would leave a hard disk inside a detached haze ring.
    const float TotalCircumsolar = GlareGain
                                 * (ThomsonCorona + AerosolWeight * (AureoleInner + AureoleOuter) * DiscGain);

    const SpectralColor RawSolar{
        (Photosphere.Red   + TotalCircumsolar * 1.02f) * Configuration.ExtraterrestrialSolarFlux.Red,
        (Photosphere.Green + TotalCircumsolar * 0.99f) * Configuration.ExtraterrestrialSolarFlux.Green,
        (Photosphere.Blue  + TotalCircumsolar * 0.95f) * Configuration.ExtraterrestrialSolarFlux.Blue };

    return MultiplySpectral(RawSolar, AtmosphereAlongRay.ViewTransmittance);
}

SpectralColor ApplyPbrNeutralTonemap(const SpectralColor& LinearRadiance, float ExposureScale) noexcept
{
    const float SafeExposure = std::max(0.0f, ExposureScale);
    float Red   = std::max(0.0f, LinearRadiance.Red   * SafeExposure);
    float Green = std::max(0.0f, LinearRadiance.Green * SafeExposure);
    float Blue  = std::max(0.0f, LinearRadiance.Blue  * SafeExposure);

    constexpr float StartCompression = 0.8f - 0.04f;
    constexpr float Desaturation     = 0.15f;

    const float MinChannel = std::min({ Red, Green, Blue });
    const float Offset     = (MinChannel < 0.08f) ? (MinChannel - 6.25f * MinChannel * MinChannel) : 0.04f;
    Red   -= Offset;
    Green -= Offset;
    Blue  -= Offset;

    const float Peak = std::max({ Red, Green, Blue });
    if (Peak < StartCompression)
        return SpectralColor{ std::max(0.0f, Red), std::max(0.0f, Green), std::max(0.0f, Blue) };

    constexpr float SpanD   = 1.0f - StartCompression;
    const float     NewPeak = 1.0f - (SpanD * SpanD) / (Peak + SpanD - StartCompression);
    const float     Ratio   = NewPeak / std::max(1.0e-6f, Peak);
    Red   *= Ratio;
    Green *= Ratio;
    Blue  *= Ratio;

    const float WhiteMix = 1.0f - 1.0f / (Desaturation * (Peak - NewPeak) + 1.0f);
    return SpectralColor{
        Red   + (NewPeak - Red)   * WhiteMix,
        Green + (NewPeak - Green) * WhiteMix,
        Blue  + (NewPeak - Blue)  * WhiteMix };
}

} // namespace Frontier
