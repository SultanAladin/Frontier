/*==========================================================================================================================================
                                                             AERODYNAMICS.CPP
==========================================================================================================================================*/
// 🧩 Implementation of closed-loop GT3 ground-effect, diffuser stall, and active DRS / Airbrake aerodynamics.

#include "Engine/PhysicalDynamics/Vehicle/Aerodynamics.h"

namespace Frontier::Vehicle
{

AerodynamicOutputs AerodynamicsSolver::Step(const AerodynamicParameters& Params, const AerodynamicInputs& Inputs) noexcept
{
    AerodynamicOutputs Out{};
    if (!Params.Enabled) return Out;

    const float Dt          = std::max(Inputs.DeltaSeconds, 1.0e-5f);
    const Vec3  RelativeAir = Inputs.ChassisVelocityWorld - Inputs.AmbientWindWorld;
    const float AirSpeedSq  = RelativeAir.LengthSquared();
    const float AirSpeed    = std::sqrt(AirSpeedSq);
    if (AirSpeed < 0.25f) return Out;

    const Vec3  ForwardWorld = Inputs.ChassisOrientation.Rotate({1.0f, 0.0f, 0.0f});
    const float ForwardAir   = Dot(RelativeAir, ForwardWorld);
    const float DynPressure  = 0.5f * Params.AirDensityKgPerM3 * AirSpeedSq;
    const float DynArea      = DynPressure * Params.FrontalAreaSquareMetres;
    Out.DynamicPressurePa    = DynPressure;

    // 1. Active Rear Wing state machine (-1 = DRS, 0 = Normal, +1 = Airbrake).
    float TargetWing = 0.0f;
    if (Params.ActiveWingEnabled)
    {
        if (Inputs.Brake > 0.45f && ForwardAir > 18.0f)
        {
            TargetWing         = 1.0f;
            Out.AirbrakeActive = true;
        }
        else if ((Inputs.DrsRequested || Inputs.Throttle > 0.90f) &&
                 Inputs.Brake < 0.05f &&
                 std::fabs(Inputs.SteerRadians) < 0.055f &&
                 ForwardAir >= Params.DrsMinSpeedMps)
        {
            TargetWing    = -1.0f;
            Out.DrsActive = true;
        }
    }

    const float SlewStep = Params.ActuatorRatePerSecond * Dt;
    WingBlend           += ClampScalar(TargetWing - WingBlend, -SlewStep, SlewStep);
    Out.WingStateBlend   = WingBlend;

    float WingCl = Params.WingClNormal;
    float WingCd = Params.WingCdNormal;
    if (WingBlend < 0.0f)
    {
        WingCl = LerpScalar(Params.WingClNormal, Params.WingClDrs, -WingBlend);
        WingCd = LerpScalar(Params.WingCdNormal, Params.WingCdDrs, -WingBlend);
    }
    else if (WingBlend > 0.0f)
    {
        WingCl = LerpScalar(Params.WingClNormal, Params.WingClAirbrake, WingBlend);
        WingCd = LerpScalar(Params.WingCdNormal, Params.WingCdAirbrake, WingBlend);
    }

    // 2. Ride-height ground-effect sensitivity & diffuser choke/stall.
    const float FrontClearance = std::max(Inputs.FrontRideHeightMetres, 0.01f);
    const float RearClearance  = std::max(Inputs.RearRideHeightMetres, 0.01f);
    const float MeanClearance  = 0.5f * (FrontClearance + RearClearance);

    const float FrontGroundGain = std::exp(-4.5f * std::fabs(FrontClearance - Params.OptimalFrontRideHeightM));
    float       VenturiGain     = Params.SideSkirtSealFactor * std::exp(-5.0f * std::max(0.0f, MeanClearance - 0.075f));

    if (FrontClearance < Params.CriticalStallRideHeightM || RearClearance < Params.CriticalStallRideHeightM)
    {
        Out.DiffuserStalled = true;
        VenturiGain        *= Params.VortexGeneratorStallGuard;
    }

    // Rake angle (+nose down boosts diffuser extraction up to ~1.8 deg).
    const float RakeMetres = ClampScalar(RearClearance - FrontClearance, -0.04f, 0.06f);
    const float RakeBonus  = 1.0f + 3.2f * RakeMetres;

    const float FrontCl    = (Params.SplitterClNominal * FrontGroundGain + Params.CanardsClNominal) *
                          (ForwardAir >= 0.0f ? 1.0f : 0.15f);
    const float DiffuserCl = Params.VenturiDiffuserClNominal * VenturiGain * RakeBonus *
                             (ForwardAir >= 0.0f ? 1.0f : 0.15f);
    const float RearCl     = WingCl * (ForwardAir >= 0.0f ? 1.0f : 0.20f) + 0.58f * DiffuserCl;
    const float FrontTotal = FrontCl + 0.42f * DiffuserCl;

    Out.FrontDownforceNewtons = std::max(0.0f, DynArea * FrontTotal);
    Out.RearDownforceNewtons  = std::max(0.0f, DynArea * RearCl);
    Out.TotalDownforceNewtons = Out.FrontDownforceNewtons + Out.RearDownforceNewtons;

    const float TotalCd       = Params.BaseDragCoefficient + WingCd + 0.045f * DiffuserCl;
    Out.DragMagnitudeNewtons  = DynArea * TotalCd;
    Out.DragForceWorld        = -RelativeAir.Normalized() * Out.DragMagnitudeNewtons;
    return Out;
}

} // namespace Frontier::Vehicle
