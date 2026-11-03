/*==========================================================================================================================================
                                                              AERODYNAMICS.H
==========================================================================================================================================*/
// 🧩 Closed-loop GT3 aerodynamic package: splitter, canards, Venturi/diffuser ground-effect + stall, skirts, and DRS/Airbrake rear wing.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/VehicleMath.h"

namespace Frontier::Vehicle
{

struct AerodynamicParameters
{
    bool  Enabled                    = true;      // [-] enable aerodynamic drag & downforce
    float AirDensityKgPerM3          = 1.225f;    // [kg/m^3] sea-level ISA density
    float FrontalAreaSquareMetres    = 2.08f;     // [m^2] projected frontal area
    float BaseDragCoefficient        = 0.33f;     // [-] body drag coefficient Cd0

    // 📝 Front aero: splitter + dive-plane canards.
    float SplitterClNominal          = 0.36f;     // [-] front splitter downforce coefficient
    float CanardsClNominal           = 0.14f;     // [-] front dive-plane canards Cl
    float OptimalFrontRideHeightM    = 0.085f;    // [m] peak ground-effect front ride height

    // 📝 Underbody Venturi tunnels + rear diffuser + side skirts + vortex generators.
    float VenturiDiffuserClNominal   = 0.62f;     // [-] underbody ground-effect Cl
    float CriticalStallRideHeightM   = 0.032f;    // [m] diffuser choke/stall clearance threshold
    float SideSkirtSealFactor        = 1.14f;     // [-] skirt suction amplification
    float VortexGeneratorStallGuard  = 0.55f;     // [-] VG retention fraction during diffuser stall

    // 📝 Active rear wing (Normal / DRS low-drag / Airbrake high-downforce).
    bool  ActiveWingEnabled          = true;      // [-] enable automatic DRS & Airbrake actuation
    float WingClNormal               = 0.52f;     // [-] nominal wing downforce coefficient
    float WingCdNormal               = 0.085f;    // [-] nominal wing induced drag
    float WingClDrs                  = 0.18f;     // [-] DRS flattened wing downforce
    float WingCdDrs                  = 0.025f;    // [-] DRS drag reduction
    float WingClAirbrake             = 0.96f;     // [-] high-AoA airbrake wing downforce
    float WingCdAirbrake             = 0.240f;    // [-] airbrake drag increment
    float DrsMinSpeedMps             = 32.0f;     // [m/s] ~115 km/h minimum DRS speed
    float ActuatorRatePerSecond      = 7.5f;      // [1/s] hydraulic wing flap slew rate
};

struct AerodynamicInputs
{
    Vec3  ChassisVelocityWorld   {0.0f, 0.0f, 0.0f};   // [m/s] vehicle linear velocity
    Vec3  AmbientWindWorld       {0.0f, 0.0f, 0.0f};   // [m/s] environmental wind vector
    Quat  ChassisOrientation     = Quat::Identity();   // [-] chassis world orientation
    float FrontRideHeightMetres  = 0.11f;              // [m] front splitter ground clearance
    float RearRideHeightMetres   = 0.12f;              // [m] rear diffuser ground clearance
    float Throttle               = 0.0f;               // [0..1] driver throttle for DRS eligibility
    float Brake                  = 0.0f;               // [0..1] driver brake for Airbrake deployment
    float SteerRadians           = 0.0f;               // [rad] front steer angle (disables DRS in corners)
    bool  DrsRequested           = false;              // [-] explicit DRS request (or auto straightline)
    float DeltaSeconds           = 1.0f / 240.0f;      // [s] timestep
};

struct AerodynamicOutputs
{
    Vec3  DragForceWorld         {0.0f, 0.0f, 0.0f};   // [N] aerodynamic drag vector opposing airspeed
    float DragMagnitudeNewtons   = 0.0f;               // [N] scalar aerodynamic drag
    float FrontDownforceNewtons  = 0.0f;               // [N] downward aero load on front axle
    float RearDownforceNewtons   = 0.0f;               // [N] downward aero load on rear axle
    float TotalDownforceNewtons  = 0.0f;               // [N] Front + Rear downforce
    float DynamicPressurePa      = 0.0f;               // [Pa] 0.5 * rho * V_air^2
    float WingStateBlend         = 0.0f;               // [-1..+1] -1 = full DRS, 0 = normal, +1 = full Airbrake
    bool  DrsActive              = false;              // [-] true when wing is in DRS low-drag state
    bool  AirbrakeActive         = false;              // [-] true when wing is in Airbrake high-drag state
    bool  DiffuserStalled        = false;              // [-] true when ride height dropped below stall clearance
};

class AerodynamicsSolver
{
  public:
    void Reset() noexcept { WingBlend = 0.0f; }

    [[nodiscard]] AerodynamicOutputs Step(const AerodynamicParameters& Params, const AerodynamicInputs& Inputs) noexcept;

  private:
    float WingBlend = 0.0f;   // [-1..+1] -1 = DRS, 0 = Normal, +1 = Airbrake
};

} // namespace Frontier::Vehicle
