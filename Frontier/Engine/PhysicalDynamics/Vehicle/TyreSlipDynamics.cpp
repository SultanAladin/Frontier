/*==========================================================================================================================================
                                                           TYRESLIPDYNAMICS.CPP
==========================================================================================================================================*/
// 🧩 Implementation of GRIT's implicit wheel-spin Newton solver + MF6.1 relaxation lag and ProjectDrive's stretched-string filter.

#include "Engine/PhysicalDynamics/Vehicle/TyreSlipDynamics.h"

namespace Frontier::Vehicle
{

namespace
{

[[nodiscard]] float ResolveRelaxationLength(float BaseLength, float Exponent, float NormalLoadNewtons, float ReferenceLoadKn) noexcept
{
    const float LoadRatio = std::max((NormalLoadNewtons * 0.001f) / std::max(ReferenceLoadKn, 0.1f), 0.1f);
    return std::max(0.02f, BaseLength * std::pow(LoadRatio, Exponent));
}

} // namespace

TyreSlipOutputs TyreSlipSolver::Step(SlipSolverMode Mode, const PacejkaMagicFormula& Formula, const TyreSlipInputs& Inputs,
                                     TyreSlipVariables& Variables) const noexcept
{
    TyreSlipOutputs Outputs{};
    const float     Dt = std::max(Inputs.DeltaSeconds, 1.0e-6f);

    // 📝 Airborne / unloaded early exit (matches GRIT Phase 1 threshold < 10 N).
    if (Inputs.NormalLoadNewtons < 10.0f)
    {
        Variables.DeflectionLongitudinal *= std::exp(-Dt * 12.0f);
        Variables.DeflectionLateral      *= std::exp(-Dt * 12.0f);
        Variables.SlipRatio               = 0.0f;
        Variables.SlipAngleRadians        = 0.0f;
        Variables.TransientFxNewtons      = 0.0f;
        Variables.TransientFyNewtons      = 0.0f;

        const float NetSpinTorque = Inputs.DriveTorqueNm - Inputs.BrakeTorqueNm * SignScalar(Inputs.WheelOmega);
        Outputs.NextWheelOmega    = Inputs.WheelOmega + (NetSpinTorque / std::max(Inputs.WheelInertia, 0.05f)) * Dt;
        return Outputs;
    }

    const PacejkaParameters& Params      = Formula.QueryParameters();
    const float              Radius      = std::max(Inputs.EffectiveRadius, 0.05f);
    const float              Inertia     = std::max(Inputs.WheelInertia, 0.05f);
    const float              Vx          = Inputs.LongitudinalSpeed;
    const float              Vy          = Inputs.LateralSpeed;
    const float              ContactSpd  = std::max(std::sqrt(Vx * Vx + Vy * Vy), 0.5f);
    const float              SigmaLong   = ResolveRelaxationLength(
        Params.RelaxationLengthLong, Params.RelaxationLoadExponentLong, Inputs.NormalLoadNewtons, Params.Fz0);
    const float              SigmaLat    = ResolveRelaxationLength(
        Params.RelaxationLengthLat, Params.RelaxationLoadExponentLat, Inputs.NormalLoadNewtons, Params.Fz0);

    if (Mode == SlipSolverMode::GritImplicitNewton)
    {
        //----------------------------------------------------------------------------------------------------------------
        //   EXACT GRIT SolveContactSlip: IMPLICIT WHEEL-SPIN NEWTON-RAPHSON + MF6.1 RELAXATION LAG
        //----------------------------------------------------------------------------------------------------------------
        float Kappa = Variables.SlipRatio;
        float Alpha = Variables.SlipAngleRadians;

        constexpr float FiniteStep       = 1.0e-4f;
        constexpr int   MaxIterations    = 12;
        constexpr float ConvergenceLimit = 5.0e-5f;
        constexpr float KappaBrakeMin    = -1.0f;
        constexpr float KappaDriveMax    = 1.5f;
        constexpr float AlphaLimit       = 1.48353f;   // [rad] 85 deg

        const float MinSpeed = std::max(ReferenceSpeedFloor, 0.5f);

        auto PredictWheelOmega = [&](float CandidateFx) noexcept -> float {
            // 📝 Apply Coulomb brake opposition that clamps through zero without sign-chatter reversal.
            const float UnbrakedOmega = Inputs.WheelOmega + ((Inputs.DriveTorqueNm - CandidateFx * Radius) / Inertia) * Dt;
            const float MaxBrakeDelta = (std::max(0.0f, Inputs.BrakeTorqueNm) / Inertia) * Dt;
            if ( std::fabs(UnbrakedOmega) <= MaxBrakeDelta) return 0.0f;
            return UnbrakedOmega - SignScalar(UnbrakedOmega) * MaxBrakeDelta;
        };

        int ExecutedIterations = 0;
        for (int Iter = 0; Iter < MaxIterations; ++Iter)
        {
            ExecutedIterations    = Iter + 1;
            const float KappaSafe = ClampScalar(Kappa, KappaBrakeMin, KappaDriveMax);
            const float AlphaSafe = ClampScalar(Alpha, -AlphaLimit, AlphaLimit);

            const CombinedPacejkaResult Steady = Formula.CombinedForces(
                KappaSafe, AlphaSafe, Inputs.CamberRadians, Inputs.NormalLoadNewtons, Inputs.GripMultiplier);
            const float OmegaNext      = PredictWheelOmega(Steady.LongitudinalForceNewtons);
            const float WheelSpeedNext = OmegaNext * Radius;
            const float Denom          = std::max({std::fabs(Vx), std::fabs(WheelSpeedNext), MinSpeed});
            const float KappaTarget    = (WheelSpeedNext - Vx) / Denom;
            const float AlphaTarget    = -std::atan2(Vy, std::max(std::fabs(Vx), MinSpeed));

            const float ResidualK = Kappa - KappaTarget;
            const float ResidualA = Alpha - AlphaTarget;
            if (std::fabs(ResidualK) < ConvergenceLimit && std::fabs(ResidualA) < ConvergenceLimit) break;

            const float KappaPerturb = ClampScalar(Kappa + FiniteStep, KappaBrakeMin, KappaDriveMax);
            const auto  ForcesK      = Formula.CombinedForces(
                KappaPerturb, AlphaSafe, Inputs.CamberRadians, Inputs.NormalLoadNewtons, Inputs.GripMultiplier);
            const float WheelSpeedK  = PredictWheelOmega(ForcesK.LongitudinalForceNewtons) * Radius;
            const float DenomK       = std::max({std::fabs(Vx), std::fabs(WheelSpeedK), MinSpeed});
            const float TargetK      = (WheelSpeedK - Vx) / DenomK;
            const float JacKK        = 1.0f - (TargetK - KappaTarget) / FiniteStep;

            const float AlphaPerturb = ClampScalar(Alpha + FiniteStep, -AlphaLimit, AlphaLimit);
            const auto  ForcesA      = Formula.CombinedForces(
                KappaSafe, AlphaPerturb, Inputs.CamberRadians, Inputs.NormalLoadNewtons, Inputs.GripMultiplier);
            const float WheelSpeedA  = PredictWheelOmega(ForcesA.LongitudinalForceNewtons) * Radius;
            const float DenomA       = std::max({std::fabs(Vx), std::fabs(WheelSpeedA), MinSpeed});
            const float TargetA      = (WheelSpeedA - Vx) / DenomA;
            const float JacKA        = -(TargetA - KappaTarget) / FiniteStep;

            float DeltaK = 0.0f;
            float DeltaA = -ResidualA;
            if (std::fabs(JacKK) < 1.0e-9f)
            {
                const float Regularized = (JacKK >= 0.0f ? 1.0f : -1.0f) * std::max(std::fabs(JacKK), 1.0e-4f);
                DeltaK                  = -ResidualK / Regularized;
            }
            else
            {
                DeltaK = (-ResidualK - (-ResidualA * JacKA)) / JacKK;
            }

            Kappa += ClampScalar(DeltaK, -0.15f, 0.15f);
            Alpha += ClampScalar(DeltaA, -0.05f, 0.05f);
        }

        Kappa = ClampScalar(Kappa, KappaBrakeMin, KappaDriveMax);
        Alpha = ClampScalar(Alpha, -AlphaLimit, AlphaLimit);

        const CombinedPacejkaResult Steady = Formula.CombinedForces(
            Kappa, Alpha, Inputs.CamberRadians, Inputs.NormalLoadNewtons, Inputs.GripMultiplier);

        // 📝 GRIT Phase 5 first-order relaxation length integration (tau = sigma / max(V_contact, 0.5)).
        const float BlendLong = ClampScalar(Dt / (SigmaLong / ContactSpd), 0.0f, 1.0f);
        const float BlendLat  = ClampScalar(Dt / (SigmaLat / ContactSpd), 0.0f, 1.0f);

        Variables.TransientFxNewtons += BlendLong * (Steady.LongitudinalForceNewtons - Variables.TransientFxNewtons);
        Variables.TransientFyNewtons += BlendLat * (Steady.LateralForceNewtons - Variables.TransientFyNewtons);
        Variables.SlipRatio           = Kappa;
        Variables.SlipAngleRadians    = Alpha;

        Outputs.LongitudinalForceNewtons = Variables.TransientFxNewtons;
        Outputs.LateralForceNewtons      = Variables.TransientFyNewtons;
        Outputs.AligningTorqueNm         = Formula.SelfAligningTorque(
            Alpha,
            Kappa,
            Outputs.LateralForceNewtons,
            Inputs.CamberRadians,
            Inputs.NormalLoadNewtons,
            &Outputs.PneumaticTrailMetres);
        Outputs.SlipRatio        = Kappa;
        Outputs.SlipAngleRadians = Alpha;
        Outputs.NextWheelOmega   = PredictWheelOmega(Outputs.LongitudinalForceNewtons);
        Outputs.NewtonIterations = ExecutedIterations;
    }
    else if (Mode == SlipSolverMode::RelaxationLength)
    {
        //----------------------------------------------------------------------------------------------------------------
        //   STRETCHED-STRING EXPONENTIAL INTEGRATOR (du/dt = Vsx - (|Vx|/sigma)*u)
        //----------------------------------------------------------------------------------------------------------------
        const float WheelSpeed = Inputs.WheelOmega * Radius;
        const float SlipVelX   = WheelSpeed - Vx;
        const float SlipVelY   = -Vy;
        const float DenomLong  = std::max({std::fabs(Vx), std::fabs(WheelSpeed), ReferenceSpeedFloor});
        const float DenomLat   = std::max(std::fabs(Vx), ReferenceSpeedFloor);

        auto AdvanceBristle = [&](float Current, float SlipVel, float AdvectSpeed, float Sigma) noexcept -> float {
            const float Rate = AdvectSpeed / Sigma;
            if (Rate * Dt < 1.0e-5f) return Current + SlipVel * Dt;
            const float Decay = std::exp(-Rate * Dt);
            return Current * Decay + (SlipVel / Rate) * (1.0f - Decay);
        };

        Variables.DeflectionLongitudinal = ClampScalar(
            AdvanceBristle(Variables.DeflectionLongitudinal, SlipVelX, DenomLong, SigmaLong),
            -DeflectionCeilingMetres,
            DeflectionCeilingMetres);
        Variables.DeflectionLateral = ClampScalar(
            AdvanceBristle(Variables.DeflectionLateral, SlipVelY, DenomLat, SigmaLat),
            -DeflectionCeilingMetres,
            DeflectionCeilingMetres);

        const float SolvedKappa    = Variables.DeflectionLongitudinal / SigmaLong;
        const float SolvedAlpha    = std::atan(Variables.DeflectionLateral / SigmaLat);
        Variables.SlipRatio        = SolvedKappa;
        Variables.SlipAngleRadians = SolvedAlpha;

        const CombinedPacejkaResult Combined = Formula.CombinedForces(
            SolvedKappa, SolvedAlpha, Inputs.CamberRadians, Inputs.NormalLoadNewtons, Inputs.GripMultiplier);

        Variables.TransientFxNewtons     = Combined.LongitudinalForceNewtons;
        Variables.TransientFyNewtons     = Combined.LateralForceNewtons;
        Outputs.LongitudinalForceNewtons = Combined.LongitudinalForceNewtons;
        Outputs.LateralForceNewtons      = Combined.LateralForceNewtons;
        Outputs.AligningTorqueNm         = Combined.AligningTorqueNm;
        Outputs.PneumaticTrailMetres     = Combined.PneumaticTrailMetres;
        Outputs.SlipRatio                = SolvedKappa;
        Outputs.SlipAngleRadians         = SolvedAlpha;

        const float UnbrakedOmega = Inputs.WheelOmega + ((Inputs.DriveTorqueNm - Outputs.LongitudinalForceNewtons * Radius) / Inertia) * Dt;
        const float MaxBrakeDelta = (std::max(0.0f, Inputs.BrakeTorqueNm) / Inertia) * Dt;
        Outputs.NextWheelOmega    = (std::fabs(UnbrakedOmega) <= MaxBrakeDelta)
                                     ? 0.0f
                                     : (UnbrakedOmega - SignScalar(UnbrakedOmega) * MaxBrakeDelta);
    }
    else
    {
        //----------------------------------------------------------------------------------------------------------------
        //   QUASI-STATIC EQUILIBRIUM ORACLE
        //----------------------------------------------------------------------------------------------------------------
        const float WheelSpeed     = Inputs.WheelOmega * Radius;
        const float Denom          = std::max({std::fabs(Vx), std::fabs(WheelSpeed), ReferenceSpeedFloor});
        const float TargetKappa    = ClampScalar((WheelSpeed - Vx) / Denom, -0.92f, 1.50f);
        const float TargetAlpha    = ClampScalar(-std::atan2(Vy, std::max(std::fabs(Vx), ReferenceSpeedFloor)), -1.30f, 1.30f);

        Variables.SlipRatio              = TargetKappa;
        Variables.SlipAngleRadians       = TargetAlpha;
        Variables.DeflectionLongitudinal = TargetKappa * SigmaLong;
        Variables.DeflectionLateral      = std::tan(TargetAlpha) * SigmaLat;

        const CombinedPacejkaResult Combined = Formula.CombinedForces(
            TargetKappa, TargetAlpha, Inputs.CamberRadians, Inputs.NormalLoadNewtons, Inputs.GripMultiplier);
        Outputs.LongitudinalForceNewtons = Combined.LongitudinalForceNewtons;
        Outputs.LateralForceNewtons      = Combined.LateralForceNewtons;
        Outputs.AligningTorqueNm         = Combined.AligningTorqueNm;
        Outputs.PneumaticTrailMetres     = Combined.PneumaticTrailMetres;
        Outputs.SlipRatio                = TargetKappa;
        Outputs.SlipAngleRadians         = TargetAlpha;
        Outputs.NewtonIterations         = 1;

        const float UnbrakedOmega = Inputs.WheelOmega + ((Inputs.DriveTorqueNm - Outputs.LongitudinalForceNewtons * Radius) / Inertia) * Dt;
        const float MaxBrakeDelta = (std::max(0.0f, Inputs.BrakeTorqueNm) / Inertia) * Dt;
        Outputs.NextWheelOmega    = (std::fabs(UnbrakedOmega) <= MaxBrakeDelta)
                                     ? 0.0f
                                     : (UnbrakedOmega - SignScalar(UnbrakedOmega) * MaxBrakeDelta);
    }

    // 📝 GRIT Phase 6 thermodynamic slip power dissipation.
    const float SlipSpeedLong    = Outputs.NextWheelOmega * Radius - Vx;
    const float SlipSpeedLat     = -Vy;
    const float RawPower         = Outputs.LongitudinalForceNewtons * SlipSpeedLong + Outputs.LateralForceNewtons * SlipSpeedLat;
    Outputs.SlipPowerWatts       = std::max(0.0f, std::fabs(Outputs.LongitudinalForceNewtons * SlipSpeedLong) +
                                                  std::fabs(Outputs.LateralForceNewtons * SlipSpeedLat));
    (void)RawPower;
    Variables.AccumulatedSlipMj += (Outputs.SlipPowerWatts * Dt) * 1.0e-6f;
    Outputs.WheelLocked          = std::fabs(Outputs.NextWheelOmega) < 0.1f;
    return Outputs;
}

} // namespace Frontier::Vehicle
