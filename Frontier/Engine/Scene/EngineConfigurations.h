/*==========================================================================================================================================
                                                      ENGINECONFIGURATIONS.H
==========================================================================================================================================*/
// 🧩 Modular runtime configurations for Render Pipeline, Environment, Spatial, Physics/Mass, Thermal, Chemical, and Surface domains.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    REVISION TRACKER
//------------------------------------------------------------------------------------------------------------------------

struct RevisionCounter
{
    uint64_t Revision = 0;

    void AdvanceRevision() noexcept
    {
        ++Revision;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDER ENGINE CONFIGURATIONS
//------------------------------------------------------------------------------------------------------------------------

struct DiagnosticsConfiguration : RevisionCounter
{
    float    FrameTimeMs          = 16.2f;
    float    FramesPerSecond      = 61.7f;
    float    GpuRayQueryMs        = 3.4f;
    float    GpuLightingMs        = 4.1f;
    float    GpuVolumetricMs      = 2.6f;
    uint32_t ActiveDrawCount      = 184;
    bool     TimingOverlayEnabled = true;
    bool     MemoryOverlayEnabled = false;
    std::array<float, 64> FrameHistoryMs{};

    DiagnosticsConfiguration() noexcept
    {
        for (size_t Index = 0; Index < FrameHistoryMs.size(); ++Index)
        {
            FrameHistoryMs[Index] = 15.8f + static_cast<float>(Index % 5u) * 0.25f;
        }
    }
};

struct RenderPipelineConfiguration : RevisionCounter
{
    bool  Enabled                = true;
    bool  HardwareRayQueryTier   = true;   // true: RTX VK_KHR_ray_query + ReSTIR GI | false: GTX 4-Cascade SDF + SAH-2 BVH
    int   QualityTier            = 2;      // 0 Low · 1 Medium · 2 Ultra
    int   InspectionModeIndex    = 0;      // 0..14 viewport inspection buffer modes
    int   InspectionDistanceMode = 2;      // 0 Near · 1 Medium · 2 Far
    float ExposureCompensationEv = 0.15f;
    float BloomIntensity         = 0.28f;
    float RestirTemporalHistory  = 20.0f;
    float RestirSpatialNeighbors = 5.0f;
    bool  TemporalAntiAliasing   = true;
    bool  GlobalIllumination     = true;
    bool  ScreenSpaceReflections = true;
    bool  AcesFilmicTonemap      = true;
};

struct DistanceFieldConfiguration : RevisionCounter
{
    bool     Enabled                 = true;
    float    CascadeZeroRadiusMetres = 16.0f;
    float    CascadeOneRadiusMetres  = 48.0f;
    float    CascadeTwoRadiusMetres  = 128.0f;
    float    CascadeThreeRadiusMetres = 320.0f;
    uint32_t ClipmapVoxelResolution  = 64;
    float    ConeTraceStepScale      = 1.0f;
    uint32_t MaxConeSteps            = 48;
    bool     AnalyticalPrimitives    = true;
    bool     TrilinearCascadeBlend   = true;
};

struct SurfaceCardConfiguration : RevisionCounter
{
    bool     Enabled             = true;
    uint32_t AtlasResolutionPx   = 1024;
    float    IndirectBounceGain  = 1.15f;
    uint32_t CardsPerObject      = 6;
    float    SkyVisibilityWeight = 0.85f;
    bool     TemporalAccumulate  = true;
    bool     IrradianceProbes    = true;
};

struct PolygonClusterConfiguration : RevisionCounter
{
    bool     Enabled              = true;
    uint32_t HierarchyDepth       = 6;
    uint32_t MaxClusterTriangles  = 128;
    uint32_t LodErrorThresholdPx  = 1;
    uint32_t VisibleClusterCount  = 248;
    uint32_t CulledClusterCount   = 112;
    bool     HiZOcclusionCulling  = true;
    bool     FreezeCullingFrustum = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENVIRONMENT CONFIGURATIONS
//------------------------------------------------------------------------------------------------------------------------

struct SolarConfiguration : RevisionCounter
{
    bool  Enabled              = true;
    float ElevationDegrees     = 38.0f;
    float AzimuthDegrees       = 132.0f;
    float IlluminanceKilolux   = 98.0f;
    float AngularDiameterDeg   = 0.53f;
    float KelvinTemperature    = 5778.0f;
    bool  CastRaytracedShadow  = true;
    bool  VolumetricLightShaft = true;
};

struct OpticalFlareConfiguration : RevisionCounter
{
    bool     Enabled              = true;
    float    Intensity            = 0.65f;
    float    LuminanceThreshold   = 1.80f;
    uint32_t ApertureBladeCount   = 7;
    float    HaloRadius           = 0.42f;
    float    ChromaticDispersion  = 0.18f;
    bool     AnamorphicStreak     = true;
    bool     DiffractionStarburst = true;
};

struct AtmosphereConfiguration : RevisionCounter
{
    bool  Enabled               = true;
    float RayleighScattering    = 1.0f;
    float MieTurbidity          = 2.4f;
    float MieAnisotropyG        = 0.80f;
    float OzoneAbsorptionScale  = 1.0f;
    float PlanetRadiusKm        = 6360.0f;
    float AtmosphereThicknessKm = 80.0f;
    bool  MultiScatteringLut    = true;
    bool  AerialPerspective     = true;
};

struct CloudConfiguration : RevisionCounter
{
    bool  Enabled              = true;
    bool  LocalVolume          = false;
    float Coverage             = 0.44f;
    float ExtinctionDensity    = 0.85f;
    float BaseAltitudeMetres   = 1650.0f;
    float LayerThicknessMetres = 1400.0f;
    float WindDriftSpeed       = 12.0f;
    float SilverLiningPower    = 0.72f;
    bool  TemporalReprojection = true;
    bool  CastGroundShadows    = true;
};

struct StarConfiguration : RevisionCounter
{
    bool     Enabled                = true;
    uint32_t HipparcosCount         = 5000;
    float    MagnitudeGain          = 1.15f;
    float    MilkyWayLuminance      = 0.55f;
    float    SiderealSpeed          = 1.0f;
    float    AtmosphericScintillate = 0.35f;
};

struct MoonConfiguration : RevisionCounter
{
    bool  Enabled               = true;
    float PrimaryPhase          = 0.68f;
    float PrimaryOrbitDegrees   = 214.0f;
    float PrimaryAlbedo         = 0.12f;
    bool  SecondaryMoonEnabled  = true;
    float SecondaryOrbitDegrees = 64.0f;
    float EarthshineLuminance   = 0.22f;
};

struct FogConfiguration : RevisionCounter
{
    bool  Enabled             = true;
    bool  LocalVolume         = false;
    float BaseDensity         = 0.014f;
    float HeightFalloff       = 0.12f;
    float BaseAltitudeMetres  = 0.0f;
    float PhaseAnisotropy     = 0.55f;
    float StartDistanceMetres = 8.0f;
    bool  VolumetricFroxelGrid = true;
    bool  ReceiveRayShadow    = true;
};

struct LightningConfiguration : RevisionCounter
{
    bool     Enabled              = true;
    float    StrikesPerMinute     = 4.5f;
    float    BranchingFactor      = 0.62f;
    float    PeakCurrentKiloamps  = 32.0f;
    float    CloudSheetLuminance  = 2.8f;
    bool     AutoStormDischarge   = false;
    bool     AcousticShockwave    = true;
    uint32_t TriggeredStrikeCount = 0;
};

struct WindConfiguration : RevisionCounter
{
    bool  Enabled              = true;
    float DirectionDegrees     = 45.0f;
    float SpeedMetresPerSecond = 6.8f;
    float GustAmplitude        = 3.4f;
    float CurlNoiseTurbulence  = 0.48f;
    float CurlFrequencyHz      = 0.25f;
    bool  AdvectClouds         = true;
    bool  AdvectPlumes         = true;
    bool  AdvectParticles      = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTINUUM & PARTICLE SOLVERS
//------------------------------------------------------------------------------------------------------------------------

struct FluidPlumeConfiguration : RevisionCounter
{
    bool     Enabled              = true;
    uint32_t LatticeResolutionX   = 128;
    uint32_t LatticeResolutionY   = 64;
    uint32_t LatticeResolutionZ   = 128;
    float    BuoyancyLift         = 2.15f;
    float    VorticityConfinement = 1.85f;
    float    DissipationRate      = 0.22f;
    float    FuelInjectionRate    = 1.40f;
    float    FlameIntensity       = 1.65f;
    float    SootExtinction       = 1.10f;
    float    BlackbodyCoreKelvin  = 1850.0f;
    bool     CombustionEnabled    = true;
    bool     SelfShadowing        = true;
};

struct FractureConfiguration : RevisionCounter
{
    bool     Enabled                 = true;
    uint32_t VoronoiCellCount        = 32;
    float    ImpulseThresholdNs      = 42.0f;
    float    ImpactRadialBias        = 0.65f;
    float    DebrisLifetimeSeconds   = 18.0f;
    float    SleepLinearVelocity     = 0.15f;
    bool     FractureArmed           = true;
    bool     ImpactDustBurst         = true;
    uint32_t PrefractureTriggerCount = 0;
};

struct ParticleConfiguration : RevisionCounter
{
    bool  Enabled                 = true;
    float SparkRatePerSecond      = 280.0f;
    float RainDropDensity         = 0.0f;
    float SnowFlakeDensity        = 0.0f;
    float GravityMultiplier       = 1.0f;
    float AerodynamicDrag         = 0.32f;
    float BounceRestitution       = 0.45f;
    bool  GpuWeatherPrecipitation = true;
    bool  DepthBufferCollisions   = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                        COMPOSABLE OBJECT PROPERTY CONFIGURATIONS
//------------------------------------------------------------------------------------------------------------------------
// Any scene RecordEntry can attach zero, one, or multiple of these independent property configurations:
//   - SpatialConfiguration       : 3D position, orientation, and non-uniform scale
//   - PhysicsConfiguration       : MassConfiguration (mass, density, volume + helpers), velocity, friction, restitution
//   - ThermalConfiguration       : Temperature (K / °C), specific heat, conductivity, emissivity, radiative flux
//   - ChemicalConfiguration      : Molar mass, moles, concentration, activation energy, Arrhenius rate, enthalpy
//   - SurfaceOpticsConfiguration : Albedo, roughness, metalness, index of refraction (only for rendered surfaces)

struct SpatialConfiguration : RevisionCounter
{
    float PositionMetres[3]     = {0.0f, 1.0f, 0.0f};
    float OrientationDegrees[3] = {0.0f, 0.0f, 0.0f};
    float Scale[3]              = {1.0f, 1.0f, 1.0f};

    [[nodiscard]] float ComputeEuclideanDistance(const SpatialConfiguration& Other) const noexcept
    {
        const float Dx = PositionMetres[0] - Other.PositionMetres[0];
        const float Dy = PositionMetres[1] - Other.PositionMetres[1];
        const float Dz = PositionMetres[2] - Other.PositionMetres[2];
        return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    }
};

struct MassConfiguration
{
    float MassKilograms          = 12.5f;     // [kg]
    float DensityKgPerCubicMetre = 2700.0f;   // [kg/m^3]
    float VolumeCubicMetres      = 0.00463f;  // [m^3]

    void SyncMassFromDensityAndVolume() noexcept
    {
        MassKilograms = std::max(1.0e-6f, DensityKgPerCubicMetre * VolumeCubicMetres);
    }

    void SyncVolumeFromMassAndDensity() noexcept
    {
        VolumeCubicMetres = MassKilograms / std::max(1.0e-4f, DensityKgPerCubicMetre);
    }

    [[nodiscard]] float ComputeKineticEnergyJoules(float LinearSpeedMps) const noexcept
    {
        return 0.5f * MassKilograms * LinearSpeedMps * LinearSpeedMps;
    }

    [[nodiscard]] float ComputeLinearMomentumNs(float LinearSpeedMps) const noexcept
    {
        return MassKilograms * LinearSpeedMps;
    }

    [[nodiscard]] float ComputeSolidSphereInertia(float RadiusMetres) const noexcept
    {
        return 0.4f * MassKilograms * RadiusMetres * RadiusMetres;
    }
};

struct PhysicsConfiguration : RevisionCounter
{
    bool              Enabled                  = true;
    MassConfiguration Mass{};
    float             LinearVelocityMps[3]     = {0.0f, 0.0f, 0.0f};
    float             AngularVelocityRadSec[3] = {0.0f, 0.0f, 0.0f};
    float             Restitution              = 0.42f;
    float             StaticFriction           = 0.58f;
    float             DynamicFriction          = 0.44f;
    float             LinearDamping            = 0.04f;
    bool              DynamicBody              = true;
    bool              GravityCoupled           = true;

    [[nodiscard]] float ComputeLinearSpeedMps() const noexcept
    {
        return std::sqrt(LinearVelocityMps[0] * LinearVelocityMps[0] +
                         LinearVelocityMps[1] * LinearVelocityMps[1] +
                         LinearVelocityMps[2] * LinearVelocityMps[2]);
    }
};

struct ThermalConfiguration : RevisionCounter
{
    static constexpr float StefanBoltzmannConstant = 5.670374419e-8f; // [W / (m^2 * K^4)]

    bool  Enabled                      = true;
    float TemperatureKelvin            = 293.15f; // [K]
    float SpecificHeatJPerKgK          = 897.0f;  // [J / (kg * K)]
    float ThermalConductivityWPerMK    = 205.0f;  // [W / (m * K)]
    float SurfaceEmissivity            = 0.82f;   // [0..1]
    float ConvectionCoefficientWPerM2K = 14.0f;   // [W / (m^2 * K)]
    bool  ConductiveExchange           = true;
    bool  RadiativeExchange            = true;

    [[nodiscard]] float QueryCelsius() const noexcept
    {
        return TemperatureKelvin - 273.15f;
    }

    void ConfigureCelsius(float Celsius) noexcept
    {
        TemperatureKelvin = std::max(0.01f, Celsius + 273.15f);
    }

    [[nodiscard]] float ComputeSensibleHeatJoules(float MassKilograms, float ReferenceKelvin = 293.15f) const noexcept
    {
        return MassKilograms * SpecificHeatJPerKgK * (TemperatureKelvin - ReferenceKelvin);
    }

    [[nodiscard]] float ComputeRadiativeFluxWPerM2() const noexcept
    {
        const float T2 = TemperatureKelvin * TemperatureKelvin;
        return std::clamp(SurfaceEmissivity, 0.0f, 1.0f) * StefanBoltzmannConstant * T2 * T2;
    }
};

struct ChemicalConfiguration : RevisionCounter
{
    static constexpr float UniversalGasConstant = 8.314462618f; // [J / (mol * K)]

    bool  Enabled                    = true;
    float MolarMassGramsPerMole      = 18.015f; // [g/mol]
    float AmountMoles                = 2.0f;    // [mol]
    float ConcentrationMolPerLitre   = 1.0f;    // [mol/L]
    float ActivationEnergyKjPerMol   = 48.5f;   // [kJ/mol]
    float PreExponentialFactorPerSec = 1.0e5f;  // [1/s]
    float EnthalpyDeltaKjPerMol      = -57.1f;  // [kJ/mol] (negative = exothermic)
    int   MatterPhaseIndex           = 1;       // 0 Solid · 1 Liquid · 2 Gas · 3 Plasma
    bool  ReversibleEquilibrium      = true;
    bool  CatalyzedPathway           = false;

    [[nodiscard]] float ComputeSubstanceMassKilograms() const noexcept
    {
        return std::max(0.0f, AmountMoles * MolarMassGramsPerMole * 1.0e-3f);
    }

    [[nodiscard]] float ComputeArrheniusRateConstant(float TemperatureKelvin) const noexcept
    {
        const float SafeKelvin  = std::max(1.0f, TemperatureKelvin);
        const float EffectiveEa = (CatalyzedPathway ? ActivationEnergyKjPerMol * 0.65f : ActivationEnergyKjPerMol) * 1000.0f;
        return PreExponentialFactorPerSec * std::exp(-EffectiveEa / (UniversalGasConstant * SafeKelvin));
    }

    [[nodiscard]] float ComputeReactionHeatKilojoules() const noexcept
    {
        return AmountMoles * EnthalpyDeltaKjPerMol;
    }
};

struct SurfaceOpticsConfiguration : RevisionCounter
{
    bool  Enabled                   = true;
    float AlbedoRgb[3]              = {0.74f, 0.77f, 0.81f};
    float Roughness                 = 0.38f;
    float Metallic                  = 0.08f;
    float IndexOfRefraction         = 1.50f;
    bool  CastShadows               = true;
    bool  ReceiveGlobalIllumination = true;
};

} // namespace Frontier
