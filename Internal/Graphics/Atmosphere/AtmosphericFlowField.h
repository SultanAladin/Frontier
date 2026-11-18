/*==============================================================================================================================================
                                                          ATMOSPHERICFLOWFIELD.H
==============================================================================================================================================*/
// 🧩 Physical 3D atmospheric force-field velocity synthesis, Ekman-sheared synoptic wind, analytic divergence-free curl eddies, and RK4
//    semi-Lagrangian backward characteristic tracing driving volumetric clouds, upper-sky aerosol/cirrus transport, and orbital cyclones.

#pragma once
#ifndef FRONTIER_GRAPHICS_ATMOSPHERE_ATMOSPHERICFLOWFIELD_H
#define FRONTIER_GRAPHICS_ATMOSPHERE_ATMOSPHERICFLOWFIELD_H

#include "EngineContext/Math/LinearAlgebra_Float32.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t MaximumFlowSourceCount = 8u;   // [-] - Simultaneous 3D force-field operators packed into std140 UBO

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class FlowSourceCategory : uint32_t
{
    DirectionalJet     = 0u,   // [-] - Localized directional jet-stream corridor with cross-stream shear
    CyclonicSwirl      = 1u,   // [-] - Rankine cyclone circulation with spiral inflow, eyewall lift, and calm eye subsidence
    RadialOutflow      = 2u,   // [-] - Divergent outflow plume clearing an inner cavity into a compressed arcus ring
    BuoyantUpdraft     = 3u,   // [-] - Thermal convective column lifting cloud altitude with low-level inflow convergence
    IncompressibleCurl = 4u,   // [-] - Localized divergence-free 3D fluid eddy volume (curl of C2 vector potential)
    AtmosphericWave    = 5u,   // [-] - Gravity / lee-wave train modulating vertical streamline lift and condensation crests
};

//------------------------------------------------------------------------------------------------------------------------
//                                                           STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One positioned 3D force-field operator in scene world metres (+Z up). Couples smoothly across cloud and upper-sky altitudes.
struct FlowSourceDescriptor
{
    FlowSourceCategory Category               = FlowSourceCategory::DirectionalJet; // [-]   - Physical operator mechanism
    bool               ActivationEnabled      = false;                              // [-]   - Participates in velocity sum when true
    bool               ProjectAcrossAltitude  = true;                               // [-]   - Couples ground force sources up into cloud strata
    Vector3f           WorldCentreMetres      { 0.0f, 0.0f, 2400.0f };              // [m]   - 3D operator centre (+Z up)
    Vector3f           PrimaryAxis            { 1.0f, 0.0f, 0.0f };                 // [-]   - Unit jet/wave/rotation axis
    float              OuterRadiusMetres      = 9000.0f;                            // [m]   - Smooth C2 influence radius
    float              InnerEyeRadiusMetres   = 1600.0f;                            // [m]   - Rankine cyclone eye / outflow cavity radius
    float              VerticalHalfSpanMetres = 3500.0f;                            // [m]   - Altitude half-thickness around centre
    float              PeakSpeedMetresPerSec  = 28.0f;                              // [m/s] - Characteristic flow speed
    float              RadialInflowFraction   = 0.24f;                              // [-]   - Spiral inflow (+) or outflow (-) fraction
    float              VerticalLiftSpeed      = 6.0f;                               // [m/s] - Updraft (+) or eye subsidence (-) speed
    float              CondensationGain       = 0.45f;                              // [-]   - Eyewall/updraft/outflow density coupling
    float              WaveLengthMetres       = 3200.0f;                            // [m]   - Spatial wavelength for wave / curl sources
};

// 📝 Planetary boundary-layer & free-troposphere synoptic wind specification (+Z up world metres).
struct SynopticWindProfile
{
    bool     WindEnabled                 = true;                // [-]   - Enables background synoptic advection
    Vector3f ReferenceVelocity           { 14.0f, 4.5f, 0.0f }; // [m/s] - Geostrophic velocity at ReferenceAltitudeMetres
    float    ReferenceAltitudeMetres     = 2000.0f;             // [m]   - Calibration altitude for ReferenceVelocity
    float    AltitudeShearExponent       = 0.22f;               // [-]   - Power-law vertical wind-speed profile exponent
    float    EkmanVeerRadians            = 0.28f;               // [rad] - Directional veer across tropospheric depth
    float    CurlSpeedMetresPerSec       = 4.2f;                // [m/s] - Background divergence-free 3D eddy amplitude
    float    CurlWavelengthMetres        = 2800.0f;             // [m]   - Dominant eddy spatial scale
    float    ForceCouplingScale          = 1.0f;                // [-]   - Master gain on FlowSourceDescriptor sum
    float    MaximumAdvectionSpanSeconds = 600.0f;              // [s]   - Safety cap on continuous Lagrangian integration
    float    SkyVeilCouplingScale        = 1.0f;                // [-]   - High-altitude atmospheric veil flow gain
};

// 📝 Complete atmospheric flow configuration holding the synoptic profile and up to 8 simultaneous 3D force sources.
struct AtmosphericFlowField
{
    SynopticWindProfile  Synoptic                        {};   // [-]   - Background wind + Ekman shear + curl eddies
    FlowSourceDescriptor Sources[MaximumFlowSourceCount] {};   // [-]   - Active 3D force-field operators
    uint32_t             SourceCount                     = 0u; // [-]   - Populated slot count in [0..MaximumFlowSourceCount]
};

// 📝 Instantaneous Eulerian evaluation of velocity, vertical lift, vorticity magnitude, and condensation modulation at a point.
struct FlowVelocitySample
{
    Vector3f VelocityMetresPerSec { 0.0f, 0.0f, 0.0f }; // [m/s] - Total 3D velocity u(x, t) (+Z up)
    float    CondensationDelta    = 0.0f;               // [-]   - Signed cloud/veil modulation (-1 clears eye, +1 thickens)
    float    SwirlIntensity       = 0.0f;               // [1/s] - Local rotational shear magnitude
};

// 📝 Result of semi-Lagrangian RK4 backward characteristic integration from (x, t) back to emission coordinate X(x, 0).
struct FlowAdvectedSample
{
    Vector3f SourcePositionMetres { 0.0f, 0.0f, 0.0f }; // [m]   - Upstream Lagrangian material origin X(x, t)
    Vector3f LocalVelocity        { 0.0f, 0.0f, 0.0f }; // [m/s] - Instantaneous velocity u(x, t)
    float    IntegratedLiftMetres = 0.0f;               // [m]   - Cumulative vertical parcel displacement
    float    CondensationDelta    = 0.0f;               // [-]   - Path-accumulated eyewall/outflow/wave modulation
    float    DisplacementMetres   = 0.0f;               // [m]   - Total 3D Lagrangian travel distance ||x - X||
};

// 📝 Exact std140 GPU uniform mirror of Shaders/VolumetricCloudField.glsl::FlowFieldBlock (560 bytes).
struct FlowSourceUniformEntry
{
    float CentreOuterRadius[4];  // [m]     - xyz: world centre (+Z up); w: outer radius
    float AxisInnerRadius[4];    // [-,m]   - xyz: unit axis; w: inner eye radius
    float KinematicsPack[4];     // [m/s,-] - x: peak speed; y: radial inflow; z: vertical lift; w: vertical half-span
    float ModulationCategory[4]; // [-,m]   - x: condensation gain; y: wavelength; z: category index; w: flags (bit0=on, bit1=project)
};

struct FlowFieldUniformBlock
{
    float                  SynopticVelocityAltitude[4]; // [m/s,m]   - xyz: reference velocity; w: reference altitude
    float                  ShearVeerCurlPack[4];        // [-,rad,m] - x: shear exp; y: Ekman veer; z: curl speed; w: curl wavelength
    float                  ControlClockPack[4];         // [-,s]     - x: force scale; y: sky veil scale; z: sequence seconds; w: active count
    FlowSourceUniformEntry Sources[MaximumFlowSourceCount]; // [-] - 8 x 64 B = 512 B packed operators
};

static_assert(sizeof(FlowFieldUniformBlock) == 560u, "FlowFieldUniformBlock must remain 560 bytes for std140 parity.");

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Append a configured 3D force-field source to Field. Returns true when a slot was available.
bool RegisterFlowSource(AtmosphericFlowField& Field, const FlowSourceDescriptor& Descriptor) noexcept;

// Reset all registered 3D force-field sources while preserving the synoptic wind profile.
void ClearFlowSources(AtmosphericFlowField& Field) noexcept;

// Evaluate analytic divergence-free 3D fluid velocity ∇ × ψ(x, t) in world metres (verified ∇·u ≈ 0).
[[nodiscard]] Vector3f EvaluateDivergenceFreeCurl3D(
    const Vector3f& WorldPositionMetres,
    float           WavelengthMetres,
    float           SequenceSeconds) noexcept;

// Evaluate instantaneous Eulerian velocity, condensation coupling, and swirl intensity at (WorldPositionMetres, SequenceSeconds).
[[nodiscard]] FlowVelocitySample EvaluateAtmosphericFlow(
    const AtmosphericFlowField& Field,
    const Vector3f&             WorldPositionMetres,
    float                       SequenceSeconds) noexcept;

// Trace a material parcel backward along the 3D flow field via 4th-order Runge-Kutta semi-Lagrangian advection.
[[nodiscard]] FlowAdvectedSample TraceFlowCharacteristicBackward(
    const AtmosphericFlowField& Field,
    const Vector3f&             WorldPositionMetres,
    float                       SequenceSeconds,
    uint32_t                    SubstepCount = 4u) noexcept;

// Pack CPU AtmosphericFlowField into exact std140 FlowFieldUniformBlock for Vulkan UBO upload.
void EncodeFlowFieldUniformBlock(
    const AtmosphericFlowField& Field,
    float                       SequenceSeconds,
    FlowFieldUniformBlock&      OutUniforms) noexcept;

} // namespace Frontier

#endif
