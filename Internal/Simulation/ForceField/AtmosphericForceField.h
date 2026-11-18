/*==============================================================================================================================================
                                                       ATMOSPHERICFORCEFIELD.H
==============================================================================================================================================*/
// 🧩 Analytical 3D atmospheric force-field velocity solver: geostrophic Ekman shear, Lamb-Oseen cyclonic circulation, convective plumes,
//    divergence-free curl turbulence, and semi-Lagrangian backward streamline integration driving both volumetric clouds and sky aerosol/cirrus.

#pragma once
#ifndef FRONTIER_SIMULATION_FORCEFIELD_ATMOSPHERICFORCEFIELD_H
#define FRONTIER_SIMULATION_FORCEFIELD_ATMOSPHERICFORCEFIELD_H

#include "EngineContext/Math/LinearAlgebra_Float32.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ForceSourceCategory : uint8_t
{
    DirectionalJet      = 0u,   // [-] - Collimated atmospheric jet stream with cross-stream shear
    CyclonicCirculation = 1u,   // [-] - Lamb-Oseen tangential circulation with radial inflow + eyewall updraft
    RadialPlume         = 2u,   // [-] - Buoyant convective thermal plume with upper-tropospheric divergence
    ShearWaveStratum    = 3u,   // [-] - Tropospheric/stratospheric gravity-wave undulation sculpting cirrus/haze
};

enum class AdvectionMediumTarget : uint8_t
{
    CloudStratum     = 0u,      // [-] - Lower/mid-tropospheric cloud deck coupling
    SkyAerosolCirrus = 1u,      // [-] - Upper-sky ice-crystal cirrus + boundary-layer Mie haze coupling
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One spatial wind/force contributor in world Z-up coordinates (metres). Coupling gains control how strongly the source advects
//    volumetric cloud formations versus clear-sky Mie aerosol and cirrus veils.
struct ForceSourceDescriptor
{
    ForceSourceCategory Category                    = ForceSourceCategory::DirectionalJet; // [-]   - Physical flow mechanism
    bool                ActivationEnabled           = true;                                // [-]   - Participates when true
    bool                AtmosphereCouplingEnabled   = true;                                // [-]   - Drives clouds + sky media when true
    bool                AltitudeProjectionEnabled   = true;                                // [-]   - Projects ground-placed fields up into cloud strata
    bool                SoloCondition               = false;                               // [-]   - Isolates soloed sources when any entry enables it
    Vector3f            OriginMetres                { 0.0f, 0.0f, 1800.0f };               // [m]   - 3D center in scene Z-up coordinates
    Vector3f            AxisDirection               { 1.0f, 0.0f, 0.0f };                  // [-]   - Jet direction or circulation spin axis
    float               PeakVelocityMetresPerSecond = 24.0f;                               // [m/s] - Characteristic flow speed at InnerRadiusMetres
    float               InnerRadiusMetres           = 2400.0f;                             // [m]   - Lamb-Oseen eye radius or jet half-width
    float               OuterRadiusMetres           = 14000.0f;                            // [m]   - Smooth C2 radial support cutoff
    float               VerticalScaleMetres         = 6500.0f;                             // [m]   - Vertical e-folding coupling scale
    float               RadialInflowRatio           = 0.22f;                               // [-]   - Boundary-layer inward convergence fraction
    float               UpdraftPeakMetresPerSecond  = 4.5f;                                // [m/s] - Vertical eyewall / thermal velocity
    float               CloudAdvectionGain          = 1.0f;                                // [-]   - Coupling multiplier on volumetric cloud strata
    float               SkyAerosolCouplingGain      = 1.0f;                                // [-]   - Coupling multiplier on sky cirrus + Mie haze
};

// 📝 Pointwise evaluated wind kinematics, curl vorticity, and induced aerosol/condensation concentration modulation.
struct WindSampleEvaluation
{
    Vector3f VelocityMetresPerSecond  { 0.0f, 0.0f, 0.0f };   // [m/s]   - Resolved 3D velocity vector (Z-up)
    Vector3f VorticityPerSecond       { 0.0f, 0.0f, 0.0f };   // [1/s]   - Local curl (del x u)
    float    SpeedMetresPerSecond     = 0.0f;                 // [m/s]   - Euclidean speed |u|
    float    UpdraftMetresPerSecond   = 0.0f;                 // [m/s]   - Vertical velocity component u.z
    float    ConvergenceScalar        = 0.0f;                 // [-1..1] - Positive inside cyclonic eyewalls / thermal cores
};

// 📝 Low-resolution 3D Lagrangian advection-offset volume (Nubis / Frostbite wind-offset volume): the backward streamline
//    displacement field is baked once per integration on a coarse Cartesian grid, then sampled trilinearly per raymarch step so the
//    exact semi-Lagrangian characteristic never has to be re-integrated inside every volumetric cloud sample.
struct AdvectionOffsetVolume
{
    static constexpr uint32_t DimensionX = 96u;   // [-] - Horizontal grid columns
    static constexpr uint32_t DimensionY = 96u;   // [-] - Horizontal grid rows
    static constexpr uint32_t DimensionZ = 10u;   // [-] - Vertical grid layers across the cloud stratum

    bool                  ReadyCondition    = false;                    // [-] - True once the displacement field is baked
    float                 BakedSequenceSeconds = 0.0f;                  // [s] - Simulation clock the field was baked at
    float                 AdvectionSpanSeconds = 0.0f;                  // [s] - Backward integration window used for the bake
    Vector3f              OriginMetres      { -24000.0f, -24000.0f, 0.0f }; // [m] - Center of cell (0, 0, 0)
    Vector3f              CellSizeMetres    {   500.0f,   500.0f, 500.0f }; // [m] - Cartesian cell spacing
    std::vector<Vector3f> DisplacementsMetres {};                       // [m] - X0 - X per grid cell (Z-up scene basis)
    std::vector<float>    ConvergenceScalars  {};                       // [-] - Precomputed per-cell horizontal convergence (-div)
};

// 📝 Complete atmospheric flow configuration shared across VolumetricCloudEvaluation and RadiometricSkyEvaluation.
struct AtmosphericWindField
{
    bool                               FieldEnabled              = true;                        // [-]   - Master gate for all wind + force advection
    bool                               CurlTurbulenceEnabled     = true;                        // [-]   - Adds analytic divergence-free 3D curl eddies
    Vector3f                           GeostrophicVelocity       { 8.5f, 2.5f, 0.0f };          // [m/s] - Synoptic background wind at reference altitude
    float                              ReferenceAltitudeMetres   = 1800.0f;                     // [m]   - Reference altitude for Ekman power-law shear
    float                              ShearExponent             = 0.18f;                       // [-]   - Tropospheric vertical wind-speed exponent
    float                              EkmanVeeringRadians       = 0.32f;                       // [rad] - Surface-to-aloft wind-direction turning angle
    float                              CurlAmplitudeMetresPerSec = 3.2f;                        // [m/s] - Sub-grid divergence-free eddy speed
    float                              CurlSpatialScaleMetres    = 1650.0f;                     // [m]   - Characteristic eddy wavelength
    std::vector<ForceSourceDescriptor> SourceEntries             {};                            // [-]   - Active spatial force-field contributors
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Reset the field to a clean physical synoptic boundary-layer wind with empty spatial force sources.
void ConfigureDefaultWindField(AtmosphericWindField& Field) noexcept;

// Append one spatial force source (jet, cyclone, thermal plume, or gravity-wave shear stratum) to the field.
void AppendForceSource(AtmosphericWindField& Field, const ForceSourceDescriptor& Source);

// Evaluate the 3D divergence-controlled velocity, vorticity curl, and convergence at WorldPositionMetres and SequenceSeconds.
[[nodiscard]] WindSampleEvaluation EvaluateWindSample(const AtmosphericWindField& Field,
                                                      const Vector3f&             WorldPositionMetres,
                                                      float                       SequenceSeconds,
                                                      AdvectionMediumTarget       TargetMedium = AdvectionMediumTarget::CloudStratum) noexcept;

// Trace a semi-Lagrangian backward characteristic dX/dtau = -u(X, t - tau) over AdvectionSpanSeconds using midpoint RK2 substeps,
// returning the Lagrangian material origin X0 that arrived at WorldPositionMetres at time SequenceSeconds.
[[nodiscard]] Vector3f IntegrateStreamlineBackward(const AtmosphericWindField& Field,
                                                   const Vector3f&             WorldPositionMetres,
                                                   float                       SequenceSeconds,
                                                   float                       AdvectionSpanSeconds,
                                                   uint32_t                    IntegrationStepCount = 4u,
                                                   AdvectionMediumTarget       TargetMedium         = AdvectionMediumTarget::CloudStratum) noexcept;

// Bake the coarse Lagrangian displacement volume for one medium over the given Cartesian bounds at SequenceSeconds.
void BakeAdvectionOffsetVolume(AdvectionOffsetVolume&      Volume,
                               const AtmosphericWindField& Field,
                               AdvectionMediumTarget       TargetMedium,
                               float                       SequenceSeconds,
                               float                       AdvectionSpanSeconds,
                               uint32_t                    IntegrationStepCount,
                               const Vector3f&             OriginMetres,
                               const Vector3f&             CellSizeMetres) noexcept;

// Trilinearly sample the baked displacement field plus its divergence-derived convergence scalar (clamped at the border).
[[nodiscard]] bool SampleAdvectionOffset(const AdvectionOffsetVolume& Volume,
                                         const Vector3f&             WorldPositionMetres,
                                         Vector3f&                   OutDisplacementMetres,
                                         float&                      OutConvergenceScalar) noexcept;

} // namespace Frontier

#endif
