/*==========================================================================================================================================
                                                             TYRESLIPDYNAMICS.H
==========================================================================================================================================*/
// 🧩 transient slip solvers for the Pacejka tyre
//
//    Two interchangeable ways to turn wheel/hub kinematics into the (slip-ratio κ, slip-angle α) that feed PacejkaMagicFormula:
//
//      • Solver A — NewtonReference: Project Drive's reconstruction of GRIT's SolveContactSlip (not a bit-faithful copy). Each substep runs a
//        finite-difference Newton iteration (up to 12 iters, 24 Magic-Formula force evaluations per wheel) that solves the
//        implicit contact-patch slip equation. This is the reference we validate everything against.
//
//      • Solver B — RelaxationLength: the textbook transient stretched-string law (Pacejka & Besselink 1997; PAC2002).
//        The slips are integrated as first-order lags of the instantaneous kinematic slip, with the relaxation length
//        σ setting the time constant τ = σ/|Vx|. At standstill (Vx→0) the lag equations reduce to a spring on the contact
//        deflection, so the tyre produces a restoring force at zero speed instead of the singular κ = Vsx/Vx that makes the
//        raw Magic Formula diverge. It needs no Newton loop (≈1 MF eval/wheel), so it is both faster and unconditionally
//        stable at low speed — provided a high-rate integrator (Frontier's Phase-0 physics thread supplies one).
//
//    References: Pacejka & Besselink (1997); Pacejka (2012) ch. 7 "Single Contact Point Transient Tyre formula";
//    PAC2002/MF-Tyre manual (relaxation-length & contact-mass options).

#pragma once

#include "PacejkaMagicFormula.h"

namespace Frontier::Vehicle
{

enum class SlipSolver
{
    NewtonReference,   // Solver A — GRIT SolveContactSlip port
    RelaxationLength,  // Solver B — transient stretched-string ODE
};

//------------------------------------------------------------------------------------------------------------------------
// Kinematic inputs at the contact patch for one wheel, one step.
//------------------------------------------------------------------------------------------------------------------------
struct WheelKinematics
{
    float Vx = 0.0f;         // [m/s] hub longitudinal velocity (wheel forward axis), road axes
    float Vsy = 0.0f;        // [m/s] lateral slip velocity of contact point (= lateral hub velocity)
    float OmegaR = 0.0f;     // [m/s] wheel spin surface speed = angular vel × effective radius
    float CamberRad = 0.0f;  // [rad]
    float Fz_N = 0.0f;       // [N] vertical load
    float PressureDeviation = 0.0f;   // [-] (p − p_nominal) / p_nominal for the formula's MF6.1 pressure terms
};

// Persistent per-wheel slip deflection (only used by the relaxation solver; the Newton solver keeps nothing between calls).
struct SlipDeflection
{
    float u = 0.0f;   // [m] longitudinal contact deflection (→ slip ratio κ = u/σx behaviour)
    float v = 0.0f;   // [m] lateral contact deflection
    float Kappa = 0.0f;
    float AlphaRad = 0.0f;
    // First-order force lag (optional, used when LagForces=true)
    float FxLagged = 0.0f;
    float FyLagged = 0.0f;
};

struct SlipResult
{
    float Kappa = 0.0f;
    float AlphaRad = 0.0f;
    TyreForces Forces;
    int   Iterations = 0;   // Newton iterations actually used (0 for relaxation solver)
    int   ForceEvals = 0;   // number of Magic-Formula evaluations spent
};

//------------------------------------------------------------------------------------------------------------------------
class TyreSlipDynamics
{
public:
    explicit TyreSlipDynamics(const PacejkaMagicFormula& Formula) noexcept : Tyre(Formula) {}

    void AssignSolver(SlipSolver S) noexcept { Solver = S; }
    [[nodiscard]] SlipSolver QuerySolver() const noexcept { return Solver; }

    // When true, force outputs are additionally handed through a first-order lag (relaxation solver only) — represents the
    // build-up of the friction force itself, on top of the slip lag. Off by default (the slip lag already captures it).
    void AssignForceLag(bool Enable) noexcept { LagForces = Enable; }

    // Advance one wheel by dt. Reading is read/written for the relaxation solver; ignored by the Newton solver.
    SlipResult Step(const WheelKinematics& K, SlipDeflection& Reading, float dt) const noexcept;

    // Direct settled kinematic slip (no transient), useful for validation oracles.
    static void KinematicSlip(const WheelKinematics& K, float VxFloor, float& OutKappa, float& OutAlphaRad) noexcept;

private:
    SlipResult StepNewton(const WheelKinematics& K, float dt) const noexcept;
    SlipResult StepRelaxation(const WheelKinematics& K, SlipDeflection& Reading, float dt) const noexcept;

    const PacejkaMagicFormula& Tyre;
    SlipSolver Solver = SlipSolver::NewtonReference;
    bool LagForces = false;
};

} // namespace Frontier::Vehicle
