/*==========================================================================================================================================
                                                            TYRESLIPDYNAMICS.H
==========================================================================================================================================*/
// 🧩 Transient tyre slip solvers: GRIT implicit wheel-spin Newton + relaxation lag, stretched-string exponential filter, and quasi-static root.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/PacejkaMagicFormula.h"

namespace Frontier::Vehicle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SLIP SOLVER MODES
//------------------------------------------------------------------------------------------------------------------------

enum class SlipSolverMode : uint8_t
{
    GritImplicitNewton = 0,   // [-] exact GRIT SolveContactSlip: 12-iter 2x2 implicit wheel-spin Newton + MF6.1 relaxation lag
    RelaxationLength   = 1,   // [-] ProjectDrive stretched-string exponential filter (du/dt = Vsx - (|Vx|/sigma)*u)
    QuasiStaticNewton  = 2    // [-] steady-state 2x2 root-finder oracle for analytical equilibrium verification
};

struct TyreSlipInputs
{
    float LongitudinalSpeed   = 0.0f;     // [m/s] wheel-frame ground forward velocity Vx
    float LateralSpeed        = 0.0f;     // [m/s] wheel-frame ground lateral velocity Vy (+left)
    float WheelOmega          = 0.0f;     // [rad/s] wheel spin rate (+forward rolling)
    float EffectiveRadius     = 0.5085f;  // [m] loaded rolling radius
    float WheelInertia        = 1.25f;    // [kg*m^2] rotational inertia about axle
    float DriveTorqueNm       = 0.0f;     // [N*m] propulsion shaft torque applied to hub
    float BrakeTorqueNm       = 0.0f;     // [N*m] non-negative brake clamping torque magnitude
    float NormalLoadNewtons   = 3200.0f;  // [N] tyre vertical contact load Fz
    float CamberRadians       = 0.0f;     // [rad] wheel inclination relative to surface normal
    float GripMultiplier      = 1.0f;     // [-] compound temperature & wear grip multiplier
    float DeltaSeconds        = 1.0f / 240.0f;   // [s] integration step
};

struct TyreSlipVariables
{
    float DeflectionLongitudinal = 0.0f;   // [m] stretched-string longitudinal bristle deflection u
    float DeflectionLateral      = 0.0f;   // [m] stretched-string lateral bristle deflection v
    float SlipRatio              = 0.0f;   // [-] current solved longitudinal slip ratio kappa
    float SlipAngleRadians       = 0.0f;   // [rad] current solved lateral slip angle alpha
    float TransientFxNewtons     = 0.0f;   // [N] lagged longitudinal force
    float TransientFyNewtons     = 0.0f;   // [N] lagged lateral force
    float AccumulatedSlipMj      = 0.0f;   // [MJ] cumulative frictional energy dissipated
};

struct TyreSlipOutputs
{
    float LongitudinalForceNewtons = 0.0f;    // [N] tyre road force along wheel +X
    float LateralForceNewtons      = 0.0f;    // [N] tyre road force along wheel +Y
    float AligningTorqueNm         = 0.0f;    // [N*m] self-aligning torque Mz
    float PneumaticTrailMetres     = 0.0f;    // [m] effective pneumatic trail
    float SlipRatio                = 0.0f;    // [-] solved longitudinal slip ratio
    float SlipAngleRadians         = 0.0f;    // [rad] solved lateral slip angle
    float NextWheelOmega           = 0.0f;    // [rad/s] implicit wheel spin prediction
    float SlipPowerWatts           = 0.0f;    // [W] instantaneous frictional heat generation
    bool  WheelLocked              = false;   // [-] true when |omega| < 0.1 rad/s under load
    int   NewtonIterations         = 0;       // [-] iterations executed by Newton modes
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYRE SLIP SOLVER
//------------------------------------------------------------------------------------------------------------------------

class TyreSlipSolver
{
  public:
    float ReferenceSpeedFloor     = 1.0f;     // [m/s] denominator floor guarding low-speed singularity
    float DeflectionCeilingMetres = 0.45f;    // [m] bristle deflection bound at standstill

    void                        Reset(TyreSlipVariables& Variables) const noexcept { Variables = TyreSlipVariables{}; }
    [[nodiscard]] TyreSlipOutputs Step(SlipSolverMode Mode, const PacejkaMagicFormula& Formula, const TyreSlipInputs& Inputs,
                                       TyreSlipVariables& Variables) const noexcept;
};

} // namespace Frontier::Vehicle
