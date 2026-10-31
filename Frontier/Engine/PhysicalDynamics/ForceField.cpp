/*==========================================================================================================================================
                                                   FORCE FIELD DEFINITION
==========================================================================================================================================*/
// 🧩 Analytic Ekman wind + gust fast path, 6-tap divergence-free curl swirl, Burgers-Rott curved-spine tornado
//    vortex + cyclostrophic Δp condensation funnel, haboob gust fronts, microbursts, and multi-species particle drag.

#include "Engine/PhysicalDynamics/ForceField.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{

constexpr double π      = 3.14159265358979323846;
constexpr double Twoπ   = 6.28318530717958647692;
constexpr double ρAir0  = 1.225; // [kg/m³] sea-level standard air density
constexpr double GravG  = 9.80665;

inline double Smooth01(double Value) noexcept
{
    const double T = std::clamp(Value, 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

uint32_t HashMix(uint32_t Word) noexcept
{
    Word ^= Word >> 16u;
    Word *= 0x7FEB352Du;
    Word ^= Word >> 15u;
    Word *= 0x846CA68Bu;
    Word ^= Word >> 16u;
    return Word;
}

double HashLattice(int32_t X, int32_t Y, int32_t Z, uint32_t Seed) noexcept
{
    const uint32_t Wx = static_cast<uint32_t>(X) * 1597334677u + Seed;
    const uint32_t Wy = static_cast<uint32_t>(Y) * 3812015801u + (Seed ^ 0x9E3779B9u);
    const uint32_t Wz = static_cast<uint32_t>(Z) * 2654435761u;
    return static_cast<double>(HashMix(Wx ^ Wy ^ Wz)) * (1.0 / 4294967296.0);
}

double Quintic(double T) noexcept
{
    const double C = std::clamp(T, 0.0, 1.0);
    return C * C * C * (C * (C * 6.0 - 15.0) + 10.0);
}

double ScalarNoise3D(SpatialVector Point, uint32_t Seed) noexcept
{
    const int32_t X0 = static_cast<int32_t>(std::floor(Point.X));
    const int32_t Y0 = static_cast<int32_t>(std::floor(Point.Y));
    const int32_t Z0 = static_cast<int32_t>(std::floor(Point.Z));
    const double  Fx = Quintic(Point.X - static_cast<double>(X0));
    const double  Fy = Quintic(Point.Y - static_cast<double>(Y0));
    const double  Fz = Quintic(Point.Z - static_cast<double>(Z0));
    const auto    Lerp = [](double A, double B, double T) noexcept { return A + (B - A) * T; };

    const double N00 = Lerp(HashLattice(X0, Y0, Z0, Seed), HashLattice(X0 + 1, Y0, Z0, Seed), Fx);
    const double N10 = Lerp(HashLattice(X0, Y0 + 1, Z0, Seed), HashLattice(X0 + 1, Y0 + 1, Z0, Seed), Fx);
    const double N01 = Lerp(HashLattice(X0, Y0, Z0 + 1, Seed), HashLattice(X0 + 1, Y0, Z0 + 1, Seed), Fx);
    const double N11 = Lerp(HashLattice(X0, Y0 + 1, Z0 + 1, Seed), HashLattice(X0 + 1, Y0 + 1, Z0 + 1, Seed), Fx);
    return 2.0 * Lerp(Lerp(N00, N10, Fy), Lerp(N01, N11, Fy), Fz) - 1.0;
}

SpatialVector VectorPotential3D(SpatialVector ScaledPoint, uint32_t Seed) noexcept
{
    return {
        ScalarNoise3D(ScaledPoint, Seed + 17u),
        ScalarNoise3D({ScaledPoint.X + 31.7, ScaledPoint.Y + 19.3, ScaledPoint.Z + 7.1}, Seed + 131u),
        ScalarNoise3D({ScaledPoint.X - 13.9, ScaledPoint.Y + 41.2, ScaledPoint.Z - 23.5}, Seed + 509u),
    };
}

double ComputeActivationEnvelope(const ActiveForceSource& Source) noexcept
{
    double Env = 1.0;
    if (Source.Timing.RampInSeconds > 1.0e-4)
    {
        Env = Smooth01(Source.AgeSeconds / Source.Timing.RampInSeconds);
    }

    if (Source.ReleaseRequested)
    {
        const double RelElapsed = std::max(0.0, Source.AgeSeconds - Source.ReleaseStartAgeS);
        const double OutScale =
            (Source.Timing.RampOutSeconds > 1.0e-4) ? (1.0 - Smooth01(RelElapsed / Source.Timing.RampOutSeconds)) : 0.0;
        Env *= OutScale;
    }
    else if (Source.Timing.HoldSeconds > 0.0)
    {
        const double DecayStart = Source.Timing.RampInSeconds + Source.Timing.HoldSeconds;
        if (Source.AgeSeconds > DecayStart)
        {
            const double Over = Source.AgeSeconds - DecayStart;
            const double OutScale =
                (Source.Timing.RampOutSeconds > 1.0e-4) ? (1.0 - Smooth01(Over / Source.Timing.RampOutSeconds)) : 0.0;
            Env *= OutScale;
        }
    }
    return std::clamp(Env, 0.0, 1.0);
}

struct SpeciesPhysicalProperties
{
    double DensityKgPerM3       = 1000.0;
    double DragCoefficient      = 0.47;
    double DefaultDiameterM     = 0.0024;
    double SaltationThresholdMs = 999.0; // Threshold air speed near ground to loft resting grains
    double BounceRestitution    = 0.0;
};

SpeciesPhysicalProperties QuerySpeciesProperties(AerodynamicParticleSpecies Species) noexcept
{
    switch (Species)
    {
        case AerodynamicParticleSpecies::RainDrop:
            return {1000.0, 0.48, 0.0024, 999.0, 0.0};
        case AerodynamicParticleSpecies::HailStone:
            return {890.0, 0.44, 0.0120, 999.0, 0.28};
        case AerodynamicParticleSpecies::SnowFlake:
            return {115.0, 1.35, 0.0040, 999.0, 0.0};
        case AerodynamicParticleSpecies::FineDust:
            return {2550.0, 1.10, 0.00012, 6.5, 0.15};
        case AerodynamicParticleSpecies::QuartzSand:
            return {2650.0, 0.65, 0.00032, 8.2, 0.42};
        case AerodynamicParticleSpecies::LoftedDebris:
            return {620.0, 0.82, 0.0350, 22.0, 0.25};
    }
    return {};
}

} // namespace

BeaufortWindClassification ClassifyBeaufortWind(double SpeedMetersPerSecond) noexcept
{
    const double Speed = std::max(0.0, SpeedMetersPerSecond);
    if (Speed < 0.5) return {0u, "Calm"};
    if (Speed < 1.6) return {1u, "Light Air"};
    if (Speed < 3.4) return {2u, "Light Breeze"};
    if (Speed < 5.5) return {3u, "Gentle Breeze"};
    if (Speed < 8.0) return {4u, "Moderate Breeze"};
    if (Speed < 10.8) return {5u, "Fresh Breeze"};
    if (Speed < 13.9) return {6u, "Strong Breeze"};
    if (Speed < 17.2) return {7u, "Near Gale"};
    if (Speed < 20.8) return {8u, "Gale"};
    if (Speed < 24.5) return {9u, "Strong Gale"};
    if (Speed < 28.5) return {10u, "Storm"};
    if (Speed < 32.7) return {11u, "Violent Storm"};
    return {12u, "Hurricane Force"};
}

ForceSourceHandle ForceFieldSpace::TriggerTornado(const VortexColumnParameters& Parameters, TriggerEnvelopeTiming Timing)
{
    ActiveForceSource Entry{};
    Entry.Handle             = NextHandle_++;
    Entry.Kind               = ForceSourceKind::VortexColumn;
    Entry.Timing             = Timing;
    Entry.Vortex             = Parameters;
    Entry.ActivationEnvelope = (Timing.RampInSeconds <= 1.0e-4) ? 1.0 : 0.0;
    Sources_.push_back(Entry);
    return Entry.Handle;
}

ForceSourceHandle ForceFieldSpace::TriggerDustDevil(SpatialVector GroundAnchorMeters,
                                                    double        HeightMeters,
                                                    double        PeakSwirlMps,
                                                    double        HoldSeconds)
{
    VortexColumnParameters Devil{};
    Devil.GroundAnchorMeters      = GroundAnchorMeters;
    Devil.TranslationVelocityMps  = {2.2, 0.0, 0.8};
    Devil.SpineLeanXMeters        = 9.0;
    Devil.SpineLeanZMeters        = -5.0;
    Devil.ColumnHeightMeters      = std::max(25.0, HeightMeters);
    Devil.GroundCoreRadiusMeters  = 4.5;
    Devil.CollarCoreRadiusMeters  = 14.0;
    Devil.TangentialPeakSpeedMps  = PeakSwirlMps;
    Devil.RadialInflowSpeedMps    = 0.38 * PeakSwirlMps;
    Devil.AxialUpdraftSpeedMps    = 0.52 * PeakSwirlMps;
    Devil.CondensationStrength    = 0.0; // Dry thermal vortex: visible via lofted dust/sand rather than cloud droplets
    Devil.DebrisSkirtHeightMeters = std::max(25.0, HeightMeters * 0.78);
    Devil.DebrisExtinctionPerM    = 0.055;
    Devil.DebrisAlbedoRgb         = {0.72, 0.58, 0.40};
    return TriggerTornado(Devil, {0.8, HoldSeconds, 1.5});
}

ForceSourceHandle ForceFieldSpace::TriggerGustFront(const GustFrontParameters& Parameters, TriggerEnvelopeTiming Timing)
{
    ActiveForceSource Entry{};
    Entry.Handle             = NextHandle_++;
    Entry.Kind               = ForceSourceKind::TravelingGustFront;
    Entry.Timing             = Timing;
    Entry.GustFront          = Parameters;
    Entry.ActivationEnvelope = (Timing.RampInSeconds <= 1.0e-4) ? 1.0 : 0.0;
    Sources_.push_back(Entry);
    return Entry.Handle;
}

ForceSourceHandle ForceFieldSpace::TriggerMicroburst(const MicroburstParameters& Parameters, TriggerEnvelopeTiming Timing)
{
    ActiveForceSource Entry{};
    Entry.Handle             = NextHandle_++;
    Entry.Kind               = ForceSourceKind::RadialMicroburst;
    Entry.Timing             = Timing;
    Entry.Microburst         = Parameters;
    Entry.ActivationEnvelope = (Timing.RampInSeconds <= 1.0e-4) ? 1.0 : 0.0;
    Sources_.push_back(Entry);
    return Entry.Handle;
}

bool ForceFieldSpace::ReleaseTrigger(ForceSourceHandle Handle) noexcept
{
    for (ActiveForceSource& Source : Sources_)
    {
        if (Source.Handle == Handle && !Source.ReleaseRequested)
        {
            Source.ReleaseRequested = true;
            Source.ReleaseStartAgeS = Source.AgeSeconds;
            return true;
        }
    }
    return false;
}

void ForceFieldSpace::Step(double DeltaSeconds) noexcept
{
    const double Dt = std::max(0.0, DeltaSeconds);
    ElapsedSeconds_ += Dt;

    for (size_t Index = 0u; Index < Sources_.size();)
    {
        ActiveForceSource& Source = Sources_[Index];
        Source.AgeSeconds += Dt;
        Source.ActivationEnvelope = ComputeActivationEnvelope(Source);

        if (Source.Kind == ForceSourceKind::VortexColumn)
        {
            Source.Vortex.GroundAnchorMeters.X += Source.Vortex.TranslationVelocityMps.X * Dt;
            Source.Vortex.GroundAnchorMeters.Y += Source.Vortex.TranslationVelocityMps.Y * Dt;
            Source.Vortex.GroundAnchorMeters.Z += Source.Vortex.TranslationVelocityMps.Z * Dt;
            const double CoreRef = std::max(2.0, Source.Vortex.GroundCoreRadiusMeters);
            const double ωCore   = (Source.Vortex.TangentialPeakSpeedMps * Source.ActivationEnvelope) / CoreRef;
            Source.SwirlPhaseRadians += ωCore * Dt;
        }

        const bool ExpiredFromRelease =
            Source.ReleaseRequested &&
            (Source.AgeSeconds >= Source.ReleaseStartAgeS + std::max(0.0, Source.Timing.RampOutSeconds)) &&
            Source.ActivationEnvelope <= 1.0e-4;
        const bool ExpiredFromHold =
            (Source.Timing.HoldSeconds > 0.0) &&
            (Source.AgeSeconds >= Source.Timing.RampInSeconds + Source.Timing.HoldSeconds + Source.Timing.RampOutSeconds) &&
            Source.ActivationEnvelope <= 1.0e-4;

        if (ExpiredFromRelease || ExpiredFromHold)
        {
            Sources_.erase(Sources_.begin() + static_cast<std::ptrdiff_t>(Index));
        }
        else
        {
            ++Index;
        }
    }
}

SpatialVector ForceFieldSpace::SampleStep(SpatialVector WorldPointMeters) const noexcept
{
    return EvaluateStepSample(WorldPointMeters, false).AirVelocityMps;
}

SpatialVector ForceFieldSpace::SampleSwirl(SpatialVector WorldPointMeters) const noexcept
{
    if (Wind_.SwirlStrength <= 1.0e-5 || Wind_.SpeedMetersPerSecond <= 1.0e-5)
    {
        return {};
    }
    const double Scale   = std::max(4.0, Wind_.SwirlScaleMeters);
    const double InvS    = 1.0 / Scale;
    const double Drift   = 0.35 * Wind_.SpeedMetersPerSecond * ElapsedSeconds_;
    const SpatialVector P = {
        (WorldPointMeters.X - std::cos(Wind_.BearingRadians) * Drift) * InvS,
        WorldPointMeters.Y * InvS,
        (WorldPointMeters.Z - std::sin(Wind_.BearingRadians) * Drift) * InvS,
    };

    constexpr double Eps    = 0.08;
    const auto       PxPlus = VectorPotential3D({P.X + Eps, P.Y, P.Z}, Wind_.Seed);
    const auto       PxMin  = VectorPotential3D({P.X - Eps, P.Y, P.Z}, Wind_.Seed);
    const auto       PyPlus = VectorPotential3D({P.X, P.Y + Eps, P.Z}, Wind_.Seed);
    const auto       PyMin  = VectorPotential3D({P.X, P.Y - Eps, P.Z}, Wind_.Seed);
    const auto       PzPlus = VectorPotential3D({P.X, P.Y, P.Z + Eps}, Wind_.Seed);
    const auto       PzMin  = VectorPotential3D({P.X, P.Y, P.Z - Eps}, Wind_.Seed);

    const double Inv2E = 1.0 / (2.0 * Eps);
    const double CurlX = (PyPlus.Z - PyMin.Z) * Inv2E - (PzPlus.Y - PzMin.Y) * Inv2E;
    const double CurlY = (PzPlus.X - PzMin.X) * Inv2E - (PxPlus.Z - PxMin.Z) * Inv2E;
    const double CurlZ = (PxPlus.Y - PxMin.Y) * Inv2E - (PyPlus.X - PyMin.X) * Inv2E;

    const double Gain = Wind_.SwirlStrength * Wind_.SpeedMetersPerSecond * 0.45;
    return {CurlX * Gain, CurlY * Gain * 0.45, CurlZ * Gain};
}

ForceFieldSample ForceFieldSpace::EvaluateStepSample(SpatialVector WorldPointMeters, bool IncludeCurlSwirl) const noexcept
{
    ForceFieldSample Out{};

    // 1. Base Ekman altitude-sheared wind + coherent multi-frequency gust wave (trig-only fast path)
    const double AltAboveGroundM = std::max(0.0, WorldPointMeters.Y - GroundPlaneYMeters_);
    const double AltKm           = AltAboveGroundM * 1.0e-3;
    const double Bearing         = Wind_.BearingRadians + Wind_.VeerRadiansPerKilometer * AltKm;
    const double DirX            = std::cos(Bearing);
    const double DirZ            = std::sin(Bearing);
    const double ShearGain       = std::max(0.15, 1.0 + Wind_.ShearPerKilometer * std::min(AltKm, 12.0));

    const double AlongWindCoord = WorldPointMeters.X * DirX + WorldPointMeters.Z * DirZ;
    const double Phase          = Twoπ * Wind_.GustFrequencyHz * ElapsedSeconds_ - 0.011 * AlongWindCoord;
    const double GustWave       = 0.62 * std::sin(Phase) + 0.38 * std::sin(2.17 * Phase + 1.13);
    const double LocalWindSpeed = Wind_.SpeedMetersPerSecond * ShearGain * (1.0 + Wind_.GustAmplitude * GustWave);

    Out.AirVelocityMps = {DirX * LocalWindSpeed, 0.0, DirZ * LocalWindSpeed};
    if (IncludeCurlSwirl)
    {
        const SpatialVector Eddy = SampleSwirl(WorldPointMeters);
        Out.AirVelocityMps.X += Eddy.X;
        Out.AirVelocityMps.Y += Eddy.Y;
        Out.AirVelocityMps.Z += Eddy.Z;
    }

    double WeightAlbedoSum = 0.0;
    SpectrumRgb WeightedAlbedo{0.0, 0.0, 0.0};

    // 2. Superpose active coherent 3D force sources
    for (const ActiveForceSource& Source : Sources_)
    {
        const double Env = Source.ActivationEnvelope;
        if (Env <= 1.0e-5)
        {
            continue;
        }

        if (Source.Kind == ForceSourceKind::VortexColumn)
        {
            const VortexColumnParameters& V      = Source.Vortex;
            const double                  RelY   = WorldPointMeters.Y - V.GroundAnchorMeters.Y;
            const double                  SpanH  = std::max(10.0, V.ColumnHeightMeters);
            if (RelY < -15.0 || RelY > SpanH * 1.30)
            {
                continue;
            }
            const double HNorm      = std::clamp(RelY / SpanH, 0.0, 1.0);
            const double TopTaper   = (RelY <= SpanH) ? 1.0 : (1.0 - Smooth01((RelY - SpanH) / (0.30 * SpanH)));
            const double SpineX     = V.GroundAnchorMeters.X + V.SpineLeanXMeters * (HNorm * HNorm);
            const double SpineZ     = V.GroundAnchorMeters.Z + V.SpineLeanZMeters * (HNorm * HNorm);
            const double Dx         = WorldPointMeters.X - SpineX;
            const double Dz         = WorldPointMeters.Z - SpineZ;
            const double Radius     = std::sqrt(Dx * Dx + Dz * Dz);
            const double CoreRadius = V.GroundCoreRadiusMeters +
                                      (V.CollarCoreRadiusMeters - V.GroundCoreRadiusMeters) * Smooth01(HNorm);
            const double SafeCore   = std::max(1.0, CoreRadius);
            const double ρNorm      = Radius / SafeCore;

            // Burgers-Rott tangential velocity: peaks at ρ_norm = 1 with v_θ = V_max
            const double VortexProfile = (2.0 * ρNorm) / (1.0 + ρNorm * ρNorm);
            const double vTangential   = Env * TopTaper * V.TangentialPeakSpeedMps * VortexProfile;

            // Near-ground boundary-layer radial inflow transitioning to upper-collar divergence
            const double InflowBand    = std::exp(-RelY / std::max(12.0, 0.16 * SpanH)) -
                                         0.35 * Smooth01((HNorm - 0.72) / 0.28);
            const double vRadial       = -Env * TopTaper * V.RadialInflowSpeedMps * InflowBand * VortexProfile;

            // Helical axial updraft concentrated inside and around the funnel wall
            const double vUpdraft      = Env * TopTaper * V.AxialUpdraftSpeedMps *
                                         std::exp(-0.42 * ρNorm * ρNorm) * Smooth01(RelY / 8.0 + 0.15);

            double UnitRadX = 1.0;
            double UnitRadZ = 0.0;
            if (Radius > 1.0e-5)
            {
                UnitRadX = Dx / Radius;
                UnitRadZ = Dz / Radius;
            }
            // Counter-clockwise cyclonic tangent in XZ: (-UnitRadZ, +UnitRadX)
            const double TangX = -UnitRadZ;
            const double TangZ = UnitRadX;

            Out.AirVelocityMps.X += TangX * vTangential + UnitRadX * vRadial + V.TranslationVelocityMps.X * Env * 0.25;
            Out.AirVelocityMps.Y += vUpdraft;
            Out.AirVelocityMps.Z += TangZ * vTangential + UnitRadZ * vRadial + V.TranslationVelocityMps.Z * Env * 0.25;

            // Cyclostrophic pressure deficit Δp(r) = ρ_air * V_max² / (1 + ρ_norm²) and radial gradient ∂p/∂r = ρ_air * v_θ² / r
            const double VMaxEff  = Env * TopTaper * V.TangentialPeakSpeedMps;
            const double ΔpLocal  = ρAir0 * (VMaxEff * VMaxEff) / (1.0 + ρNorm * ρNorm);
            Out.PressureDeficitPascals += ΔpLocal;
            if (Radius > 0.25)
            {
                const double dPdR = ρAir0 * (vTangential * vTangential) / Radius;
                Out.RadialPressureGradientPaPerM.X += UnitRadX * dPdR;
                Out.RadialPressureGradientPaPerM.Z += UnitRadZ * dPdR;
            }

            // Adiabatic condensation funnel potential & helical twist angle
            const double FunnelPotential = Env * TopTaper * V.CondensationStrength * std::exp(-0.78 * ρNorm * ρNorm);
            Out.FunnelCondensationDensity = std::max(Out.FunnelCondensationDensity, FunnelPotential);
            Out.HelicalTwistRadians +=
                (Source.SwirlPhaseRadians + 2.4 * HNorm) * Env * TopTaper / (1.0 + 0.28 * ρNorm * ρNorm);

            // Swirling surface saltation debris/dust skirt around touch-down foot
            if (V.DebrisSkirtHeightMeters > 0.5 && RelY >= 0.0 && RelY <= V.DebrisSkirtHeightMeters)
            {
                const double SkirtV   = 1.0 - Smooth01(RelY / V.DebrisSkirtHeightMeters);
                const double SheathR  = std::exp(-0.32 * (ρNorm - 0.85) * (ρNorm - 0.85));
                const double σDebris  = Env * V.DebrisExtinctionPerM * SkirtV * SheathR;
                Out.ParticulateExtinctionPerM += σDebris;
                WeightedAlbedo.R += σDebris * V.DebrisAlbedoRgb.R;
                WeightedAlbedo.G += σDebris * V.DebrisAlbedoRgb.G;
                WeightedAlbedo.B += σDebris * V.DebrisAlbedoRgb.B;
                WeightAlbedoSum += σDebris;
            }
        }
        else if (Source.Kind == ForceSourceKind::TravelingGustFront)
        {
            const GustFrontParameters& G   = Source.GustFront;
            const double DirLen            = std::hypot(G.PropagationDirectionXZ.X, G.PropagationDirectionXZ.Z);
            const double PropX             = (DirLen > 1.0e-6) ? (G.PropagationDirectionXZ.X / DirLen) : 1.0;
            const double PropZ             = (DirLen > 1.0e-6) ? (G.PropagationDirectionXZ.Z / DirLen) : 0.0;
            const double FrontTravel       = G.FrontSpeedMps * Source.AgeSeconds;
            const double NoseX             = G.OriginMeters.X + PropX * FrontTravel;
            const double NoseZ             = G.OriginMeters.Z + PropZ * FrontTravel;
            const double SignedDistBehind  = (NoseX - WorldPointMeters.X) * PropX + (NoseZ - WorldPointMeters.Z) * PropZ;
            const double CrestDepth        = std::max(5.0, G.CrestDepthMeters);
            const double RelY              = WorldPointMeters.Y - G.OriginMeters.Y;

            if (SignedDistBehind >= -0.25 * CrestDepth && SignedDistBehind <= 2.4 * CrestDepth &&
                RelY >= 0.0 && RelY <= G.VerticalExtentMeters * 1.25)
            {
                const double NoseRise  = Smooth01((SignedDistBehind + 0.25 * CrestDepth) / (0.35 * CrestDepth));
                const double WakeDecay = std::exp(-std::max(0.0, SignedDistBehind - 0.1 * CrestDepth) / CrestDepth);
                const double HeightEnv = 1.0 - Smooth01(RelY / std::max(5.0, G.VerticalExtentMeters));
                const double Surge     = Env * NoseRise * WakeDecay * HeightEnv;
                const double NoseLift  = Env * NoseRise * std::exp(-std::abs(SignedDistBehind) / (0.28 * CrestDepth)) *
                                         HeightEnv * (0.28 * G.PeakGustSpeedMps);

                Out.AirVelocityMps.X += PropX * (G.PeakGustSpeedMps * Surge);
                Out.AirVelocityMps.Y += NoseLift;
                Out.AirVelocityMps.Z += PropZ * (G.PeakGustSpeedMps * Surge);

                const double σDust = Surge * G.LoftedDustExtinctionM;
                Out.ParticulateExtinctionPerM += σDust;
                WeightedAlbedo.R += σDust * G.DustAlbedoRgb.R;
                WeightedAlbedo.G += σDust * G.DustAlbedoRgb.G;
                WeightedAlbedo.B += σDust * G.DustAlbedoRgb.B;
                WeightAlbedoSum += σDust;
            }
        }
        else if (Source.Kind == ForceSourceKind::RadialMicroburst)
        {
            const MicroburstParameters& M    = Source.Microburst;
            const double                Dx   = WorldPointMeters.X - M.CoreCenterMeters.X;
            const double                Dz   = WorldPointMeters.Z - M.CoreCenterMeters.Z;
            const double                Rad  = std::hypot(Dx, Dz);
            const double                RelY = WorldPointMeters.Y - M.GroundPlaneYMeters;
            const double                ColR = std::max(5.0, M.ColumnRadiusMeters);
            if (RelY >= 0.0 && RelY <= std::max(40.0, M.CoreCenterMeters.Y * 1.4))
            {
                const double DownCore = std::exp(-(Rad * Rad) / (ColR * ColR)) * Smooth01(RelY / 35.0);
                const double OutRing  = (Rad / std::max(10.0, M.OutflowRingRadiusMeters)) *
                                        std::exp(-0.5 * std::pow(Rad / std::max(10.0, M.OutflowRingRadiusMeters), 2.0)) *
                                        std::exp(-RelY / 42.0);
                const double InvR     = (Rad > 1.0e-4) ? (1.0 / Rad) : 0.0;
                Out.AirVelocityMps.X += Env * M.OutflowPeakSpeedMps * OutRing * (Dx * InvR);
                Out.AirVelocityMps.Y -= Env * M.DowndraftSpeedMps * DownCore;
                Out.AirVelocityMps.Z += Env * M.OutflowPeakSpeedMps * OutRing * (Dz * InvR);

                const double σDust = Env * M.LoftedDustExtinctionM * OutRing;
                Out.ParticulateExtinctionPerM += σDust;
                WeightedAlbedo.R += σDust * 0.64;
                WeightedAlbedo.G += σDust * 0.52;
                WeightedAlbedo.B += σDust * 0.38;
                WeightAlbedoSum += σDust;
            }
        }
    }

    if (WeightAlbedoSum > 1.0e-7)
    {
        Out.ParticulateScatteringAlbedo = {
            WeightedAlbedo.R / WeightAlbedoSum,
            WeightedAlbedo.G / WeightAlbedoSum,
            WeightedAlbedo.B / WeightAlbedoSum,
        };
    }

    return Out;
}

void ForceFieldSpace::StepParticles(std::span<AerodynamicParticle> Particles, double DeltaSeconds) const noexcept
{
    const double Dt = std::clamp(DeltaSeconds, 0.0, 0.1);
    if (Dt <= 0.0)
    {
        return;
    }

    for (AerodynamicParticle& Particle : Particles)
    {
        const SpeciesPhysicalProperties Props = QuerySpeciesProperties(Particle.Species);
        const double                    Diam  = (Particle.DiameterMeters > 1.0e-6f)
                                                    ? static_cast<double>(Particle.DiameterMeters)
                                                    : Props.DefaultDiameterM;
        const double Radius = 0.5 * Diam;
        const double Volume = (4.0 / 3.0) * π * Radius * Radius * Radius;
        const double Area   = π * Radius * Radius;
        const double Mass   = std::max(1.0e-12, Props.DensityKgPerM3 * Volume);

        const ForceFieldSample Field = EvaluateStepSample(Particle.PositionMeters, true);
        const double           AirSpeedHoriz = std::hypot(Field.AirVelocityMps.X, Field.AirVelocityMps.Z);

        // Bagnold surface saltation lofting: resting sand, dust, or debris lifts off when near-surface air speed exceeds threshold
        if (Particle.RestingOnGround)
        {
            if (AirSpeedHoriz > Props.SaltationThresholdMs)
            {
                Particle.RestingOnGround = false;
                const double Excess      = AirSpeedHoriz - Props.SaltationThresholdMs;
                const double HopJitter   = 0.75 + 0.5 * HashLattice(
                    static_cast<int32_t>(Particle.PositionMeters.X * 13.0),
                    static_cast<int32_t>(Particle.AgeSeconds * 60.0),
                    static_cast<int32_t>(Particle.PositionMeters.Z * 13.0),
                    Particle.Seed);
                Particle.VelocityMps.X = 0.35 * Field.AirVelocityMps.X;
                Particle.VelocityMps.Y = std::min(14.0, (0.28 * Excess + 0.45 * std::max(0.0, Field.AirVelocityMps.Y)) * HopJitter);
                Particle.VelocityMps.Z = 0.35 * Field.AirVelocityMps.Z;
                Particle.PositionMeters.Y = GroundPlaneYMeters_ + 0.02;
            }
            else
            {
                Particle.AgeSeconds += static_cast<float>(Dt);
                continue;
            }
        }

        // Sub-step stiff aerodynamic drag for small dust/snow/rain grains so explicit integration stays unconditionally stable
        const int    SubSteps = (Diam < 5.0e-4) ? 4 : 1;
        const double SubDt    = Dt / static_cast<double>(SubSteps);

        for (int Sub = 0; Sub < SubSteps; ++Sub)
        {
            SpatialVector RelVel = {
                Field.AirVelocityMps.X - Particle.VelocityMps.X,
                Field.AirVelocityMps.Y - Particle.VelocityMps.Y,
                Field.AirVelocityMps.Z - Particle.VelocityMps.Z,
            };
            if (Particle.Species == AerodynamicParticleSpecies::SnowFlake)
            {
                const double FlutterPhase = 5.5 * static_cast<double>(Particle.AgeSeconds) + 0.17 * static_cast<double>(Particle.Seed);
                RelVel.X += 0.55 * std::sin(FlutterPhase);
                RelVel.Z += 0.55 * std::cos(0.83 * FlutterPhase);
            }

            const double RelSpeed = std::sqrt(RelVel.X * RelVel.X + RelVel.Y * RelVel.Y + RelVel.Z * RelVel.Z);
            // Stokes linear + Newton quadratic drag blend so micron dust and cm hailstones both hit physical terminal velocities
            const double νKinematic = 1.5e-5;
            const double Re         = std::max(1.0e-3, RelSpeed * Diam / νKinematic);
            const double CdEff      = Props.DragCoefficient + (24.0 / Re);
            const double DragRate   = (0.5 * ρAir0 * CdEff * Area * RelSpeed) / Mass;

            // Semi-implicit velocity update: v_{n+1} = (v_n + dt * (DragRate * u_air + a_ext)) / (1 + dt * DragRate)
            const double BuoyancyG  = GravG * (1.0 - ρAir0 / Props.DensityKgPerM3);
            const double PressAccX  = -(Volume / Mass) * Field.RadialPressureGradientPaPerM.X;
            const double PressAccZ  = -(Volume / Mass) * Field.RadialPressureGradientPaPerM.Z;
            const double InvDenom   = 1.0 / (1.0 + SubDt * DragRate);

            Particle.VelocityMps.X = (Particle.VelocityMps.X + SubDt * (DragRate * (Particle.VelocityMps.X + RelVel.X) + PressAccX)) * InvDenom;
            Particle.VelocityMps.Y = (Particle.VelocityMps.Y + SubDt * (DragRate * (Particle.VelocityMps.Y + RelVel.Y) - BuoyancyG)) * InvDenom;
            Particle.VelocityMps.Z = (Particle.VelocityMps.Z + SubDt * (DragRate * (Particle.VelocityMps.Z + RelVel.Z) + PressAccZ)) * InvDenom;

            Particle.PositionMeters.X += Particle.VelocityMps.X * SubDt;
            Particle.PositionMeters.Y += Particle.VelocityMps.Y * SubDt;
            Particle.PositionMeters.Z += Particle.VelocityMps.Z * SubDt;
        }

        if (Particle.PositionMeters.Y <= GroundPlaneYMeters_)
        {
            Particle.PositionMeters.Y = GroundPlaneYMeters_;
            if (Props.BounceRestitution > 0.0 && AirSpeedHoriz > Props.SaltationThresholdMs &&
                std::abs(Particle.VelocityMps.Y) > 0.35)
            {
                Particle.VelocityMps.Y = std::abs(Particle.VelocityMps.Y) * Props.BounceRestitution;
                Particle.VelocityMps.X *= 0.72;
                Particle.VelocityMps.Z *= 0.72;
            }
            else
            {
                Particle.VelocityMps     = {};
                Particle.RestingOnGround = true;
            }
        }

        Particle.AgeSeconds += static_cast<float>(Dt);
    }
}

uint32_t ForceFieldSpace::AppendGpuForceRecords(std::vector<std::array<float, 4>>& OutFloat4Table) const
{
    uint32_t Written = 0u;
    for (const ActiveForceSource& Source : Sources_)
    {
        if (Written >= kMaxGpuForceSourcesRealm)
        {
            break;
        }
        const float Env = static_cast<float>(Source.ActivationEnvelope);
        if (Env <= 1.0e-4f)
        {
            continue;
        }

        if (Source.Kind == ForceSourceKind::VortexColumn)
        {
            const VortexColumnParameters& V = Source.Vortex;
            OutFloat4Table.push_back({static_cast<float>(V.GroundAnchorMeters.X),
                                      static_cast<float>(V.GroundAnchorMeters.Y),
                                      static_cast<float>(V.GroundAnchorMeters.Z),
                                      1.0f});
            OutFloat4Table.push_back({static_cast<float>(V.ColumnHeightMeters),
                                      static_cast<float>(V.GroundCoreRadiusMeters),
                                      static_cast<float>(V.CollarCoreRadiusMeters),
                                      static_cast<float>(V.SpineLeanXMeters)});
            OutFloat4Table.push_back({static_cast<float>(V.TangentialPeakSpeedMps) * Env,
                                      static_cast<float>(V.RadialInflowSpeedMps) * Env,
                                      static_cast<float>(V.AxialUpdraftSpeedMps) * Env,
                                      static_cast<float>(V.SpineLeanZMeters)});
            OutFloat4Table.push_back({Env * static_cast<float>(V.CondensationStrength),
                                      static_cast<float>(V.DebrisSkirtHeightMeters),
                                      Env * static_cast<float>(V.DebrisExtinctionPerM),
                                      static_cast<float>(Source.SwirlPhaseRadians)});
            ++Written;
        }
        else if (Source.Kind == ForceSourceKind::TravelingGustFront)
        {
            const GustFrontParameters& G      = Source.GustFront;
            const double               DirLen = std::hypot(G.PropagationDirectionXZ.X, G.PropagationDirectionXZ.Z);
            const float                PropX  = (DirLen > 1.0e-6) ? static_cast<float>(G.PropagationDirectionXZ.X / DirLen) : 1.0f;
            const float                PropZ  = (DirLen > 1.0e-6) ? static_cast<float>(G.PropagationDirectionXZ.Z / DirLen) : 0.0f;
            const float                Travel = static_cast<float>(G.FrontSpeedMps * Source.AgeSeconds);
            const float                NoseX  = static_cast<float>(G.OriginMeters.X) + PropX * Travel;
            const float                NoseZ  = static_cast<float>(G.OriginMeters.Z) + PropZ * Travel;

            OutFloat4Table.push_back({NoseX, static_cast<float>(G.OriginMeters.Y), NoseZ, 2.0f});
            OutFloat4Table.push_back({static_cast<float>(G.VerticalExtentMeters),
                                      static_cast<float>(G.CrestDepthMeters),
                                      PropX,
                                      PropZ});
            OutFloat4Table.push_back({static_cast<float>(G.PeakGustSpeedMps) * Env,
                                      static_cast<float>(G.DustAlbedoRgb.R),
                                      static_cast<float>(G.DustAlbedoRgb.G),
                                      static_cast<float>(G.DustAlbedoRgb.B)});
            OutFloat4Table.push_back({0.0f,
                                      static_cast<float>(G.VerticalExtentMeters),
                                      Env * static_cast<float>(G.LoftedDustExtinctionM),
                                      0.0f});
            ++Written;
        }
        else if (Source.Kind == ForceSourceKind::RadialMicroburst)
        {
            const MicroburstParameters& M = Source.Microburst;
            OutFloat4Table.push_back({static_cast<float>(M.CoreCenterMeters.X),
                                      static_cast<float>(M.GroundPlaneYMeters),
                                      static_cast<float>(M.CoreCenterMeters.Z),
                                      3.0f});
            OutFloat4Table.push_back({static_cast<float>(M.CoreCenterMeters.Y),
                                      static_cast<float>(M.ColumnRadiusMeters),
                                      static_cast<float>(M.OutflowRingRadiusMeters),
                                      0.0f});
            OutFloat4Table.push_back({static_cast<float>(M.OutflowPeakSpeedMps) * Env,
                                      static_cast<float>(M.DowndraftSpeedMps) * Env,
                                      0.64f,
                                      0.52f});
            OutFloat4Table.push_back({0.0f,
                                      48.0f,
                                      Env * static_cast<float>(M.LoftedDustExtinctionM),
                                      0.38f});
            ++Written;
        }
    }
    return Written;
}

} // namespace Frontier
