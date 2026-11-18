/*==============================================================================================================================================
                                                        VOLUMETRICFOGSOLVER.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the atmospheric height fog + local fog banks: closed-form exponential height integrals, advected fBm modulation, dual-phase
//    bounded-age RK4 advection through the shared AtmosphericFlowField, exponentially distributed energy-conserving Beer-Lambert segments, dual-lobe
//    Henyey-Greenstein single scatter with SunTransmittanceMarch self-shadow, two-stream diffuse multiple scatter, and sky ambient.

#include "Graphics/Atmosphere/VolumetricFogSolver.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Atmosphere::EvaluateRotatedFbm3D;

    constexpr float    AdvectionStationSpacingKm = 0.25f;   // [km] - Minimum spacing of the advection stations along a primary ray
    constexpr uint32_t AdvectionStationLimit     = 24u;     // [-]  - Advection stations per primary ray at most (spacing grows to fit)
    constexpr uint32_t HeightNoiseSalt           = 7001u;   // [-]  - fBm salt of the height-fog thickness patches
    constexpr uint32_t VolumeNoiseSalt           = 7103u;   // [-]  - fBm salt of the first fog bank (+ 97 per bank)

    [[nodiscard]] float EvaluateSmoothStep(float EdgeLow, float EdgeHigh, float Parameter) noexcept
    {
        const float Ratio = std::clamp((Parameter - EdgeLow) / std::max(1.0e-6f, EdgeHigh - EdgeLow), 0.0f, 1.0f);
        return Ratio * Ratio * (3.0f - 2.0f * Ratio);
    }

    // Antiderivative of the normalized height profile f(h) = exp(-max(0, h - base) / H): F(h) = h - base below the base, H (1 - e^{-(h-base)/H})
    // above it. Continuous (F(base) = 0) and bounded above by H, so F(∞) - F(h) is the exact column above h.
    [[nodiscard]] float EvaluateHeightAntiderivative(const HeightFalloffProfile& Height, float AltitudeKm) noexcept
    {
        const float Relative    = AltitudeKm - Height.FloorAltitudeKm;
        const float ScaleHeight = std::max(1.0e-4f, Height.ScaleHeightKm);
        if (Relative <= 0.0f)
            return Relative;
        return ScaleHeight * (1.0f - std::exp(-Relative / ScaleHeight));
    }

    [[nodiscard]] float EvaluateHeightProfile(const HeightFalloffProfile& Height, float AltitudeKm) noexcept
    {
        const float Relative = AltitudeKm - Height.FloorAltitudeKm;
        return (Relative <= 0.0f) ? 1.0f : std::exp(-Relative / std::max(1.0e-4f, Height.ScaleHeightKm));
    }

    [[nodiscard]] float EvaluateHenyeyGreenstein(float CosineAngle, float Asymmetry) noexcept
    {
        const float G           = std::clamp(Asymmetry, -0.99f, 0.99f);
        const float G2          = G * G;
        const float Denominator = std::max(1.0e-4f, 1.0f + G2 - 2.0f * G * CosineAngle);
        return (1.0f - G2) / (4.0f * Atmosphere::PiConstant * Denominator * std::sqrt(Denominator));
    }

    [[nodiscard]] float EvaluatePhaseHenyeyGreenstein(const VolumetricFogConfiguration& Configuration, float CosineAngle, float EccentricityScale) noexcept
    {
        const float Weight = std::clamp(Configuration.ForwardLobeWeight, 0.0f, 1.0f);
        return Weight * EvaluateHenyeyGreenstein(CosineAngle, Configuration.ScatteringDistribution * EccentricityScale)
             + (1.0f - Weight) * EvaluateHenyeyGreenstein(CosineAngle, Configuration.BackwardDistribution * EccentricityScale);
    }

    // Conservative two-stream diffuse transmission 1 / (1 + ¾ (1 - g) τ): the multiply scattered light that still gets through τ.
    [[nodiscard]] float EvaluateDiffuseTransmission(const VolumetricFogConfiguration& Configuration, float OpticalDepth) noexcept
    {
        const float Weight    = std::clamp(Configuration.ForwardLobeWeight, 0.0f, 1.0f);
        const float Asymmetry = Weight * Configuration.ScatteringDistribution + (1.0f - Weight) * Configuration.BackwardDistribution;
        return 1.0f / (1.0f + 0.75f * (1.0f - std::clamp(Asymmetry, -0.9f, 0.9f)) * std::max(0.0f, OpticalDepth));
    }

    // 📝 Sun-beam source per unit illuminance after a sun-ward optical depth: Wrenninge (2013) octaves (a = b = c = ½ on energy, extinction and
    //    eccentricity) carry the forward-peaked multiple scatter that keeps a sunlit fog bank glowing toward the sun, and a two-stream diffusion
    //    tail (isotropic) carries what survives deep inside a bank once even the softened octaves have died out.
    [[nodiscard]] float EvaluateSunScatterSource(const VolumetricFogConfiguration& Configuration, float CosineAngle, float SunOpticalDepth) noexcept
    {
        float       Source      = 0.0f;
        float       Energy      = 1.0f;
        float       Attenuation = 1.0f;
        float       Eccentric   = 1.0f;
        for (uint32_t Octave = 0u; Octave < 4u; ++Octave)
        {
            Source      += Energy * std::exp(-SunOpticalDepth * Attenuation) * EvaluatePhaseHenyeyGreenstein(Configuration, CosineAngle, Eccentric);
            Energy      *= 0.5f;
            Attenuation *= 0.5f;
            Eccentric   *= 0.5f;
        }
        const float Tail = std::max(0.0f, EvaluateDiffuseTransmission(Configuration, SunOpticalDepth) - std::exp(-SunOpticalDepth * 0.125f));
        return Source + Tail / (4.0f * Atmosphere::PiConstant);
    }

    // Thickness modulation of the height fog at an advected parcel origin [m]: advected fBm patches (vertically squashed into layers) times the
    // flow condensation coupling. Exactly 1 with zero variation and no flow, so the analytic profile is untouched.
    [[nodiscard]] float EvaluateHeightModulation(const VolumetricFogConfiguration& Configuration, const Vector3f& SourceMetres, float Condensation) noexcept
    {
        const HeightFalloffProfile& Height     = Configuration.Height;
        float                       Modulation = 1.0f;
        if (Height.FlowVariation > 0.0f)
        {
            const float    ScaleMetres = std::max(1.0f, Height.VariationScaleKm * 1000.0f);
            const Vector3f Coord{ SourceMetres.XCoord / ScaleMetres, SourceMetres.YCoord / ScaleMetres, SourceMetres.ZCoord * 4.0f / ScaleMetres };
            const float    Noise = EvaluateRotatedFbm3D(Coord, 4u, HeightNoiseSalt);
            Modulation = std::clamp(1.0f + std::clamp(Height.FlowVariation, 0.0f, 1.0f) * (2.0f * Noise - 1.0f) * 2.0f, 0.0f, 2.0f);
        }
        return Modulation * std::max(0.0f, 1.0f + Configuration.FlowCouplingScale * Condensation);
    }

    // σ_t [1/km] of every fog bank at an advected parcel origin [m].
    [[nodiscard]] float EvaluateFogVolumeDensity(const VolumetricFogConfiguration& Configuration, const Vector3f& SourceMetres, float Condensation) noexcept
    {
        float          Total = 0.0f;
        const uint32_t Count = std::min(Configuration.VolumeCount, MaximumFogVolumeCount);
        for (uint32_t Index = 0u; Index < Count; ++Index)
        {
            const FogVolumeDescriptor& Volume = Configuration.Volumes[Index];
            if (!Volume.ActivationEnabled || Volume.ExtinctionPerKm <= 0.0f)
                continue;
            const Vector3f Relative{
                (SourceMetres.XCoord - Volume.WorldCentreMetres.XCoord) / std::max(1.0f, Volume.HalfExtentMetres.XCoord),
                (SourceMetres.YCoord - Volume.WorldCentreMetres.YCoord) / std::max(1.0f, Volume.HalfExtentMetres.YCoord),
                (SourceMetres.ZCoord - Volume.WorldCentreMetres.ZCoord) / std::max(1.0f, Volume.HalfExtentMetres.ZCoord) };
            const float Radius = VectorLength(Relative);
            const float Noise  = std::clamp(Volume.NoiseStrength, 0.0f, 1.0f);
            // The fBm can push the edge out by at most 40 %, so anything beyond 1.4 radii is empty without evaluating noise.
            if (Radius >= 1.0f + 0.4f * Noise)
                continue;
            const float    ScaleMetres = std::max(1.0f, Volume.NoiseScaleMetres);
            const Vector3f Coord{ SourceMetres.XCoord / ScaleMetres, SourceMetres.YCoord / ScaleMetres, SourceMetres.ZCoord * 2.5f / ScaleMetres };
            const float    Clump     = EvaluateRotatedFbm3D(Coord, 4u, VolumeNoiseSalt + 97u * Index);
            const float    Distorted = Radius * (1.0f - 0.8f * Noise * (Clump - 0.5f));
            const float    Softness  = std::clamp(Volume.EdgeSoftness, 0.02f, 1.0f);
            const float    Mask      = 1.0f - EvaluateSmoothStep(1.0f - Softness, 1.0f, Distorted);
            // Fog banks pool: the lower half stays dense, the crown thins into the clear air above.
            const float    Pooling   = 1.0f - 0.5f * EvaluateSmoothStep(-0.2f, 1.0f, Relative.ZCoord);
            const float    Texture   = std::clamp(1.0f + Noise * (2.0f * Clump - 1.0f) * 1.4f, 0.0f, 2.0f);
            Total += Volume.ExtinctionPerKm * Mask * Pooling * Texture;
        }
        return Total * std::max(0.0f, 1.0f + Configuration.FlowCouplingScale * Condensation);
    }

    // 📝 SunTransmittanceMarch: optical depth [-] of the fog banks from a parcel origin [m] along a unit world direction, quadratically spaced taps.
    [[nodiscard]] float SunTransmittanceMarch(
        const VolumetricFogConfiguration& Configuration,
        const Vector3f&                   SourceMetres,
        const Vector3f&                   WorldDirection,
        float                             Condensation,
        uint32_t                          TapCount) noexcept
    {
        float          ReachMetres = 0.0f;
        const uint32_t Count       = std::min(Configuration.VolumeCount, MaximumFogVolumeCount);
        for (uint32_t Index = 0u; Index < Count; ++Index)
            if (Configuration.Volumes[Index].ActivationEnabled)
            {
                const Vector3f& Extent = Configuration.Volumes[Index].HalfExtentMetres;
                ReachMetres = std::max(ReachMetres, 2.8f * std::max(Extent.XCoord, std::max(Extent.YCoord, Extent.ZCoord)));
            }
        if (ReachMetres <= 0.0f || TapCount == 0u)
            return 0.0f;
        ReachMetres = std::min(ReachMetres, 6000.0f);

        float OpticalDepth = 0.0f;
        float Previous     = 0.0f;
        for (uint32_t Tap = 0u; Tap < TapCount; ++Tap)
        {
            const float Edge     = static_cast<float>(Tap + 1u) / static_cast<float>(TapCount);
            const float Distance = ReachMetres * Edge * Edge;
            const float Middle   = 0.5f * (Previous + Distance);
            const float Density  = EvaluateFogVolumeDensity(Configuration, AddVector(SourceMetres, ScaleVector(WorldDirection, Middle)), Condensation);
            OpticalDepth += Density * (Distance - Previous) * 0.001f;
            Previous      = Distance;
        }
        return OpticalDepth;
    }

    [[nodiscard]] float ResolveFogTopAltitudeKm(const VolumetricFogConfiguration& Configuration) noexcept
    {
        float Top = -1.0e9f;
        if (Configuration.Height.HeightFogEnabled && Configuration.Height.GroundExtinctionPerKm > 0.0f)
            Top = Configuration.Height.FloorAltitudeKm + 12.0f * std::max(1.0e-4f, Configuration.Height.ScaleHeightKm)
                + ((Configuration.Height.FlowVariation > 0.0f) ? Configuration.Height.ScaleHeightKm * 0.7f : 0.0f);
        const uint32_t Count = std::min(Configuration.VolumeCount, MaximumFogVolumeCount);
        for (uint32_t Index = 0u; Index < Count; ++Index)
        {
            const FogVolumeDescriptor& Volume = Configuration.Volumes[Index];
            if (Volume.ActivationEnabled)
                Top = std::max(Top, (Volume.WorldCentreMetres.ZCoord + 1.4f * Volume.HalfExtentMetres.ZCoord) * 0.001f + 0.6f);   // + advection lift
        }
        return Top;
    }

    // Near / far roots of a ray from (0, ViewerRadius, 0) along unit Direction with a concentric sphere; false on miss.
    [[nodiscard]] bool IntersectFogSphere(float ViewerRadiusKm, const Vector3f& Direction, float SphereRadiusKm, float& OutNear, float& OutFar) noexcept
    {
        const float HalfB        = ViewerRadiusKm * Direction.YCoord;
        const float Discriminant = HalfB * HalfB - (ViewerRadiusKm * ViewerRadiusKm - SphereRadiusKm * SphereRadiusKm);
        if (Discriminant < 0.0f)
            return false;
        const float Root = std::sqrt(Discriminant);
        OutNear = -HalfB - Root;
        OutFar  = -HalfB + Root;
        return true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool RegisterFogVolume(VolumetricFogConfiguration& Configuration, const FogVolumeDescriptor& Descriptor) noexcept
{
    if (Configuration.VolumeCount >= MaximumFogVolumeCount)
        return false;
    Configuration.Volumes[Configuration.VolumeCount] = Descriptor;
    Configuration.Volumes[Configuration.VolumeCount].ActivationEnabled = true;
    ++Configuration.VolumeCount;
    return true;
}

float IntegrateHeightFogSegment(
    const HeightFalloffProfile& Height,
    float                       StartAltitudeKm,
    float                       EndAltitudeKm,
    float                       LengthKm) noexcept
{
    if (!Height.HeightFogEnabled || Height.GroundExtinctionPerKm <= 0.0f || LengthKm <= 0.0f)
        return 0.0f;
    const float Rise = EndAltitudeKm - StartAltitudeKm;
    // L / Δh · [F(h_b) - F(h_a)]; the antiderivative difference loses precision for a near-level segment, so use the midpoint value there.
    if (std::fabs(Rise) < 1.0e-4f)
        return Height.GroundExtinctionPerKm * LengthKm * EvaluateHeightProfile(Height, 0.5f * (StartAltitudeKm + EndAltitudeKm));
    const float Column = EvaluateHeightAntiderivative(Height, EndAltitudeKm) - EvaluateHeightAntiderivative(Height, StartAltitudeKm);
    return Height.GroundExtinctionPerKm * LengthKm * Column / Rise;
}

float EvaluateHeightFogSunOpticalDepth(
    const HeightFalloffProfile& Height,
    float                       AltitudeKm,
    float                       UpCosine) noexcept
{
    if (!Height.HeightFogEnabled || Height.GroundExtinctionPerKm <= 0.0f)
        return 0.0f;
    // Column above the point, F(∞) - F(h) = H - F(h), stretched by the slant 1 / μ (floored at 3° so a grazing sun stays finite —
    // the curved layer caps the real grazing path near that slant for kilometre-scale fog).
    const float Column = std::max(1.0e-4f, Height.ScaleHeightKm) - EvaluateHeightAntiderivative(Height, AltitudeKm);
    return Height.GroundExtinctionPerKm * Column / std::max(0.052f, UpCosine);
}

void ResolveFogAdvection(
    const AtmosphericFlowField* OptionalFlowField,
    const Vector3f&             WorldMetres,
    float                       SequenceSeconds,
    float                       PeriodSeconds,
    Vector3f&                   OutDisplacementMetres,
    float&                      OutCondensation) noexcept
{
    OutDisplacementMetres = Vector3f{ 0.0f, 0.0f, 0.0f };
    OutCondensation       = 0.0f;
    if (OptionalFlowField == nullptr)
        return;

    // Dual-phase bounded-age advection (same scheme as the clouds): two characteristics of age ≤ P, offset by P / 2, cross-faded with triangular
    // weights. The fog keeps its anchoring and texture scale while the wind, the eddies and every force-field operator visibly move through it.
    // Fog defaults to P = 90 s (clouds use 40 s): low-level fog is slow and smooth, and the longer age lets a vortex wind a bank into a spiral.
    const float              Period       = std::max(1.0f, PeriodSeconds);
    const float              Elapsed      = std::max(0.0f, SequenceSeconds);
    const float              PhaseFirst   = std::fmod(Elapsed, Period);
    const float              PhaseSecond  = std::fmod(Elapsed + 0.5f * Period, Period);
    const float              WeightFirst  = 1.0f - std::fabs(2.0f * PhaseFirst / Period - 1.0f);
    const float              WeightSecond = 1.0f - WeightFirst;
    const FlowAdvectedSample First        = TraceFlowCharacteristicBackward(*OptionalFlowField, WorldMetres, PhaseFirst, 4u);
    const FlowAdvectedSample Second       = TraceFlowCharacteristicBackward(*OptionalFlowField, WorldMetres, PhaseSecond, 4u);
    OutDisplacementMetres = AddVector(ScaleVector(SubtractVector(First.SourcePositionMetres, WorldMetres), WeightFirst),
                                      ScaleVector(SubtractVector(Second.SourcePositionMetres, WorldMetres), WeightSecond));
    const FlowVelocitySample Instantaneous = EvaluateAtmosphericFlow(*OptionalFlowField, WorldMetres, SequenceSeconds);
    OutCondensation = std::clamp(0.5f * (First.CondensationDelta + Second.CondensationDelta) + 0.5f * Instantaneous.CondensationDelta, -1.0f, 1.0f);
}

float EvaluateFogDensity(
    const VolumetricFogConfiguration& Configuration,
    const AtmosphericFlowField*       OptionalFlowField,
    const Vector3f&                   WorldMetres,
    float                             SequenceSeconds) noexcept
{
    if (!Configuration.FogEnabled)
        return 0.0f;
    Vector3f Displacement{};
    float    Condensation = 0.0f;
    ResolveFogAdvection(OptionalFlowField, WorldMetres, SequenceSeconds, Configuration.AdvectionPeriodSeconds, Displacement, Condensation);
    const Vector3f Source = AddVector(WorldMetres, Displacement);

    float Density = 0.0f;
    if (Configuration.Height.HeightFogEnabled)
        Density += Configuration.Height.GroundExtinctionPerKm * EvaluateHeightProfile(Configuration.Height, WorldMetres.ZCoord * 0.001f)
                 * EvaluateHeightModulation(Configuration, Source, Condensation);
    return Density + EvaluateFogVolumeDensity(Configuration, Source, Condensation);
}

FogLightingContext PrepareFogLighting(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile) noexcept
{
    FogLightingContext Lighting{};
    Lighting.SunDirection = NormalizeVector(Vector3f{ Profile.SolarDirection[0], Profile.SolarDirection[1], Profile.SolarDirection[2] });

    const float        ViewerRadiusKm = Profile.BottomRadius + std::max(0.005f, Atmosphere::RetrieveViewerAltitudeKm(Profile));
    const LinearColour SolarFlux{ Profile.SolarIlluminance[0], Profile.SolarIlluminance[1], Profile.SolarIlluminance[2] };
    if (!QueryGroundSphereIntersection(ViewerRadiusKm, Lighting.SunDirection.YCoord, Profile.BottomRadius))
        Lighting.SunIlluminance = MultiplyColour(SolarFlux, SampleTransmittanceToSpace(Tables, Profile, ViewerRadiusKm, Lighting.SunDirection.YCoord));

    // Mean radiance over the full sphere (sky above, lit ground / ocean below) on a 128-point Fibonacci lattice: the isotropic light a fog
    // droplet receives from its surroundings.
    constexpr uint32_t DirectionCount = 128u;
    const float        GoldenAngle    = Atmosphere::PiConstant * (3.0f - std::sqrt(5.0f));
    LinearColour       Sum{};
    for (uint32_t Index = 0u; Index < DirectionCount; ++Index)
    {
        const float    Height  = 1.0f - 2.0f * (static_cast<float>(Index) + 0.5f) / static_cast<float>(DirectionCount);
        const float    Ring    = std::sqrt(std::max(0.0f, 1.0f - Height * Height));
        const float    Azimuth = GoldenAngle * static_cast<float>(Index);
        const Vector3f Direction{ Ring * std::cos(Azimuth), Height, Ring * std::sin(Azimuth) };
        Sum = AddColour(Sum, SampleSkyViewRadiance(Tables, Profile, Direction));
    }
    Lighting.AmbientRadiance = ScaleColour(Sum, 1.0f / static_cast<float>(DirectionCount));
    return Lighting;
}

FogRayResult IntegrateVolumetricFog(
    const VolumetricFogConfiguration& Configuration,
    const FogLightingContext&         Lighting,
    const AtmosphereUniformBlock&     Profile,
    const AtmosphericFlowField*       OptionalFlowField,
    const Vector3f&                   LocalDirection,
    float                             SequenceSeconds,
    float                             SceneDistanceKm,
    uint32_t                          PixelX,
    uint32_t                          PixelY,
    uint32_t                          SampleOrdinal) noexcept
{
    FogRayResult Result{};
    if (!Configuration.FogEnabled || Configuration.PrimaryStepCount == 0u)
        return Result;

    // -- Trace interval: inside the fog top sphere, short of the ground, the trace cap and any scene surface ------------------------------------
    const float ViewerRadiusKm = Profile.BottomRadius + std::max(0.005f, Atmosphere::RetrieveViewerAltitudeKm(Profile));
    const float TopRadiusKm    = Profile.BottomRadius + ResolveFogTopAltitudeKm(Configuration);
    float       TopNear        = 0.0f;
    float       TopFar         = 0.0f;
    if (TopRadiusKm <= Profile.BottomRadius || !IntersectFogSphere(ViewerRadiusKm, LocalDirection, TopRadiusKm, TopNear, TopFar) || TopFar <= 0.0f)
        return Result;
    float TraceStart = std::max(0.0f, TopNear);
    float TraceEnd   = std::min(TopFar, std::max(0.0f, Configuration.MaximumTraceDistanceKm));
    float GroundNear = 0.0f;
    float GroundFar  = 0.0f;
    if (IntersectFogSphere(ViewerRadiusKm, LocalDirection, Profile.BottomRadius, GroundNear, GroundFar) && GroundNear > 0.0f)
        TraceEnd = std::min(TraceEnd, GroundNear);
    if (SceneDistanceKm > 0.0f)
        TraceEnd = std::min(TraceEnd, SceneDistanceKm);
    if (TraceEnd <= TraceStart)
        return Result;

    // -- Per-ray constants ----------------------------------------------------------------------------------------------------------------------
    const float    PhaseCosine = DotVector(LocalDirection, Lighting.SunDirection);
    const float    Albedo      = std::clamp(Configuration.SingleScatterAlbedo, 0.0f, 1.0f);
    const Vector3f SunWorld{ Lighting.SunDirection.XCoord, Lighting.SunDirection.ZCoord, Lighting.SunDirection.YCoord };
    const Vector3f UpWorld{ 0.0f, 0.0f, 1.0f };
    const LinearColour Ambient = ScaleColour(Lighting.AmbientRadiance, std::max(0.0f, Configuration.AmbientStrength));
    const bool     SunCondition = Lighting.SunIlluminance.Red + Lighting.SunIlluminance.Green + Lighting.SunIlluminance.Blue > 0.0f;

    float JitterX = 0.5f;
    float JitterY = 0.5f;
    Atmosphere::EvaluateOwenSobolPair(SampleOrdinal, Atmosphere::HashPixelSeed(PixelX, PixelY, 0xF06u), JitterX, JitterY);

    // Exponential segment distribution: fine near the eye where the fog's parallax and texture live, coarse toward the skyline. The segment edges
    // are fixed (the height fog integral is exact per segment); only the in-segment medium probe is jittered.
    const float    Span        = TraceEnd - TraceStart;
    const float    Growth      = std::clamp(std::log(1.0f + Span / 0.15f), 0.5f, 5.0f);
    const float    GrowthScale = Span / (std::exp(Growth) - 1.0f);
    const uint32_t Steps       = Configuration.PrimaryStepCount;
    const float    SunAxisUp   = Lighting.SunDirection.YCoord;

    float     Transmittance     = 1.0f;
    float     SegmentStart      = TraceStart;
    float     StartAltitudeKm   = std::sqrt(TraceStart * TraceStart + 2.0f * TraceStart * ViewerRadiusKm * LocalDirection.YCoord + ViewerRadiusKm * ViewerRadiusKm) - Profile.BottomRadius;
    LinearColour Radiance{};

    // Advection stations at fixed distances along the ray (independent of the jitter), linearly interpolated: the flow displacement varies on the
    // kilometre scale of the wind and its operators, so two RK4 solves per station interval reproduce it while a per-probe solve would cost ~4x.
    // Reusing one solve over a run of probes instead left the displacement piecewise constant, which tore the fog texture into jagged steps.
    const float StationSpacing    = std::max(AdvectionStationSpacingKm, Span / static_cast<float>(AdvectionStationLimit));
    int32_t     LowerStation      = -2;
    Vector3f    LowerShift        {};
    Vector3f    UpperShift        {};
    float       LowerCondensation = 0.0f;
    float       UpperCondensation = 0.0f;
    const auto  SolveStation      = [&](int32_t Station, Vector3f& OutShift, float& OutCondensation) noexcept
    {
        const float    Distance = TraceStart + StationSpacing * static_cast<float>(Station);
        const Vector3f Local{ LocalDirection.XCoord * Distance, ViewerRadiusKm + LocalDirection.YCoord * Distance, LocalDirection.ZCoord * Distance };
        const Vector3f World{ Local.XCoord * 1000.0f, Local.ZCoord * 1000.0f, (VectorLength(Local) - Profile.BottomRadius) * 1000.0f };
        ResolveFogAdvection(OptionalFlowField, World, SequenceSeconds, Configuration.AdvectionPeriodSeconds, OutShift, OutCondensation);
    };

    for (uint32_t Step = 0u; Step < Steps; ++Step)
    {
        const float Parameter     = static_cast<float>(Step + 1u) / static_cast<float>(Steps);
        const float SegmentEnd    = TraceStart + GrowthScale * (std::exp(Growth * Parameter) - 1.0f);
        const float SegmentLength = SegmentEnd - SegmentStart;
        const float EndAltitudeKm = std::sqrt(SegmentEnd * SegmentEnd + 2.0f * SegmentEnd * ViewerRadiusKm * LocalDirection.YCoord + ViewerRadiusKm * ViewerRadiusKm) - Profile.BottomRadius;

        // Medium probe inside the segment (planet-local km -> flow world metres, +Z up).
        const float    ProbeDistance = SegmentStart + ((Step == 0u) ? JitterX : (0.25f + 0.5f * JitterX)) * SegmentLength;
        const Vector3f ProbeLocal{ LocalDirection.XCoord * ProbeDistance, ViewerRadiusKm + LocalDirection.YCoord * ProbeDistance, LocalDirection.ZCoord * ProbeDistance };
        const float    ProbeAltitude = VectorLength(ProbeLocal) - Profile.BottomRadius;
        const Vector3f ProbeWorld{ ProbeLocal.XCoord * 1000.0f, ProbeLocal.ZCoord * 1000.0f, ProbeAltitude * 1000.0f };
        Vector3f Displacement{};
        float    Condensation = 0.0f;
        if (OptionalFlowField != nullptr)
        {
            const float   StationCoordinate = (ProbeDistance - TraceStart) / StationSpacing;
            const int32_t Station           = static_cast<int32_t>(std::floor(StationCoordinate));
            if (Station != LowerStation)
            {
                if (Station == LowerStation + 1)
                {
                    LowerShift        = UpperShift;
                    LowerCondensation = UpperCondensation;
                }
                else
                    SolveStation(Station, LowerShift, LowerCondensation);
                SolveStation(Station + 1, UpperShift, UpperCondensation);
                LowerStation = Station;
            }
            const float Blend = StationCoordinate - static_cast<float>(Station);
            Displacement = AddVector(ScaleVector(LowerShift, 1.0f - Blend), ScaleVector(UpperShift, Blend));
            Condensation = LowerCondensation + (UpperCondensation - LowerCondensation) * Blend;
        }
        const Vector3f Source = AddVector(ProbeWorld, Displacement);

        const float HeightModulation = Configuration.Height.HeightFogEnabled ? EvaluateHeightModulation(Configuration, Source, Condensation) : 0.0f;
        const float VolumeDensity    = EvaluateFogVolumeDensity(Configuration, Source, Condensation);
        const float OpticalDepth     = IntegrateHeightFogSegment(Configuration.Height, StartAltitudeKm, EndAltitudeKm, SegmentLength) * HeightModulation
                                     + VolumeDensity * SegmentLength;

        if (OpticalDepth > 1.0e-7f)
        {
            // Light reaching the probe: sun through the fog column above (exact height integral, half-weighted local modulation, banks marched)
            // as multiple-scatter octaves + diffusion tail; the sky ambient through the vertical column.
            const float ColumnModulation = 0.5f + 0.5f * HeightModulation;
            LinearColour InScatter{};
            if (SunCondition)
            {
                const float SunDepth   = EvaluateHeightFogSunOpticalDepth(Configuration.Height, ProbeAltitude, SunAxisUp) * ColumnModulation
                                       + SunTransmittanceMarch(Configuration, Source, SunWorld, Condensation, Configuration.SunTransmittanceTapCount);
                InScatter = ScaleColour(Lighting.SunIlluminance, EvaluateSunScatterSource(Configuration, PhaseCosine, SunDepth));
            }
            const float UpDepth = EvaluateHeightFogSunOpticalDepth(Configuration.Height, ProbeAltitude, 1.0f) * ColumnModulation
                                + SunTransmittanceMarch(Configuration, Source, UpWorld, Condensation, 3u);
            InScatter = AddColour(InScatter, ScaleColour(Ambient, EvaluateDiffuseTransmission(Configuration, UpDepth)));

            // Energy-conserving segment integral: σ_s J (1 - e^{-τ}) / σ_t = ω J (1 - e^{-τ}).
            const float SegmentTransmittance = std::exp(-OpticalDepth);
            Radiance      = AddColour(Radiance, ScaleColour(InScatter, Transmittance * Albedo * (1.0f - SegmentTransmittance)));
            Transmittance *= SegmentTransmittance;
            if (Transmittance < 1.0e-4f)
            {
                Transmittance = 0.0f;
                break;
            }
        }
        SegmentStart    = SegmentEnd;
        StartAltitudeKm = EndAltitudeKm;
    }

    Result.Radiance      = Radiance;
    Result.Transmittance = LinearColour{ Transmittance, Transmittance, Transmittance };
    return Result;
}

void EncodeFogFieldUniformBlock(
    const VolumetricFogConfiguration& Configuration,
    float                             SequenceSeconds,
    FogFieldUniformBlock&             OutUniforms) noexcept
{
    OutUniforms = FogFieldUniformBlock{};
    const HeightFalloffProfile& Height = Configuration.Height;
    Atmosphere::EncodeFloat4(OutUniforms.HeightPack, Height.FloorAltitudeKm, (Configuration.FogEnabled && Height.HeightFogEnabled) ? Height.GroundExtinctionPerKm : 0.0f,
                             Height.ScaleHeightKm, Height.FlowVariation);
    Atmosphere::EncodeFloat4(OutUniforms.OpticsPack, Height.VariationScaleKm, Configuration.FlowCouplingScale, Configuration.SingleScatterAlbedo, Configuration.AmbientStrength);
    Atmosphere::EncodeFloat4(OutUniforms.PhasePack, Configuration.ScatteringDistribution, Configuration.BackwardDistribution, Configuration.ForwardLobeWeight,
                             static_cast<float>(Configuration.FogEnabled ? Configuration.PrimaryStepCount : 0u));
    const uint32_t Count = std::min(Configuration.VolumeCount, MaximumFogVolumeCount);
    Atmosphere::EncodeFloat4(OutUniforms.QualityPack, static_cast<float>(Configuration.SunTransmittanceTapCount), Configuration.MaximumTraceDistanceKm,
                             SequenceSeconds, static_cast<float>(Count));
    Atmosphere::EncodeFloat4(OutUniforms.FlowPack, Configuration.AdvectionPeriodSeconds, 0.0f, 0.0f, 0.0f);
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        const FogVolumeDescriptor& Volume = Configuration.Volumes[Index];
        Atmosphere::EncodeFloat4(OutUniforms.Volumes[Index].CentreSoftness, Volume.WorldCentreMetres.XCoord, Volume.WorldCentreMetres.YCoord,
                                 Volume.WorldCentreMetres.ZCoord, Volume.EdgeSoftness);
        Atmosphere::EncodeFloat4(OutUniforms.Volumes[Index].ExtentExtinction, Volume.HalfExtentMetres.XCoord, Volume.HalfExtentMetres.YCoord,
                                 Volume.HalfExtentMetres.ZCoord, Volume.ExtinctionPerKm);
        Atmosphere::EncodeFloat4(OutUniforms.Volumes[Index].NoisePack, Volume.NoiseStrength, Volume.NoiseScaleMetres, Volume.ActivationEnabled ? 1.0f : 0.0f, 0.0f);
    }
}

} // namespace Frontier
