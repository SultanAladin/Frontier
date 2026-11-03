/*==========================================================================================================================================
                                                         PACEJKAMAGICFORMULA.CPP
==========================================================================================================================================*/
// 🧩 Pacejka MF6.1 implementation with precomputed derived table, combined-slip ellipse weighting, and GRIT compound presets.

#include "Engine/PhysicalDynamics/Vehicle/PacejkaMagicFormula.h"

namespace Frontier::Vehicle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    COMPOUND PRESETS
//------------------------------------------------------------------------------------------------------------------------

PacejkaParameters PacejkaParameters::DefaultRoad() noexcept
{
    return PacejkaParameters{};
}

PacejkaParameters PacejkaParameters::GritGt3SlickDry() noexcept
{
    PacejkaParameters Compound{};
    Compound.Fz0                    = 4.2f;
    Compound.pCx1                   = 1.72f;
    Compound.pDx1                   = 1.68f;
    Compound.pDx2                   = -0.07f;
    Compound.pBx1                   = 22.5f;
    Compound.pCy1                   = 1.42f;
    Compound.pDy1                   = 1.65f;
    Compound.pDy2                   = -0.09f;
    Compound.pBy1                   = 19.5f;
    Compound.RelaxationLengthLong   = 0.16f;
    Compound.RelaxationLengthLat    = 0.24f;
    Compound.MuLoadSensitivityLong  = 0.045f;
    Compound.MuLoadSensitivityLat   = 0.055f;
    return Compound;
}

PacejkaParameters PacejkaParameters::GritStreetSuperSticky() noexcept
{
    PacejkaParameters Compound{};
    Compound.pDx1                  = 1.42f;
    Compound.pDy1                  = 1.38f;
    Compound.pBx1                  = 18.5f;
    Compound.pBy1                  = 16.5f;
    Compound.RelaxationLengthLong  = 0.20f;
    Compound.RelaxationLengthLat   = 0.28f;
    return Compound;
}

PacejkaParameters PacejkaParameters::GritOffroadDirt() noexcept
{
    PacejkaParameters Compound{};
    Compound.pCx1                  = 1.40f;
    Compound.pDx1                  = 0.82f;
    Compound.pBx1                  = 9.5f;
    Compound.pCy1                  = 1.22f;
    Compound.pDy1                  = 0.78f;
    Compound.pBy1                  = 8.2f;
    Compound.RelaxationLengthLong  = 0.34f;
    Compound.RelaxationLengthLat   = 0.46f;
    return Compound;
}

PacejkaParameters PacejkaParameters::GritDriftSemiSlick() noexcept
{
    PacejkaParameters Compound{};
    Compound.pDx1                  = 1.18f;
    Compound.pDy1                  = 1.06f;
    Compound.pEy1                  = -0.25f;
    Compound.pBy1                  = 12.8f;
    Compound.qDz1                  = 0.014f;
    return Compound;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CACHE POPULATION
//------------------------------------------------------------------------------------------------------------------------

PacejkaMagicFormula::PacejkaMagicFormula() noexcept
{
    AssignParameters(PacejkaParameters::DefaultRoad());
}

PacejkaMagicFormula::PacejkaMagicFormula(const PacejkaParameters& Parameters) noexcept
{
    AssignParameters(Parameters);
}

void PacejkaMagicFormula::AssignParameters(const PacejkaParameters& Parameters) noexcept
{
    Params            = Parameters;
    Derived.Fz0         = std::max(Params.Fz0, 0.001f);
    Derived.InvFz0      = 1.0f / Derived.Fz0;
    Derived.CxScaled    = Params.pCx1 * Params.Lx;
    Derived.CyScaled    = Params.pCy1 * Params.Ly;
    Derived.pDx1Lx      = Params.pDx1 * Params.Lx;
    Derived.pDx2Lx      = Params.pDx2 * Params.Lx;
    Derived.pBx1Lx      = Params.pBx1 * Params.Lx;
    Derived.pBx2Lx      = Params.pBx2 * Params.Lx;
    Derived.pEx1Lx      = Params.pEx1 * Params.Lx;
    Derived.pEx2Lx      = Params.pEx2 * Params.Lx;
    Derived.pEx3Lx      = Params.pEx3 * Params.Lx;
    Derived.pHx1Lx      = Params.pHx1 * Params.Lx;
    Derived.pHx2Lx      = Params.pHx2 * Params.Lx;
    Derived.pVx1Lx      = Params.pVx1 * Params.Lx;
    Derived.pVx2Lx      = Params.pVx2 * Params.Lx;
    Derived.pDy1Ly      = Params.pDy1 * Params.Ly;
    Derived.pDy2Ly      = Params.pDy2 * Params.Ly;
    Derived.pBy1Ly      = Params.pBy1 * Params.Ly;
    Derived.pBy2Ly      = Params.pBy2 * Params.Ly;
    Derived.pEy1Ly      = Params.pEy1 * Params.Ly;
    Derived.pEy2Ly      = Params.pEy2 * Params.Ly;
    Derived.pHy1Ly      = Params.pHy1 * Params.Ly;
    Derived.pHy2Ly      = Params.pHy2 * Params.Ly;
    Derived.pVy1Ly      = Params.pVy1 * Params.Ly;
    Derived.pVy2Ly      = Params.pVy2 * Params.Ly;
    Derived.pVy3Ly      = Params.pVy3 * Params.Ly;
    Derived.pVy4Ly      = Params.pVy4 * Params.Ly;
    Derived.pCamber1Ly  = Params.pCamber1 * Params.Ly;
    Derived.pCamber2Ly  = Params.pCamber2 * Params.Ly;
    Derived.qDz1Ltr     = Params.qDz1 * Params.Ltr;
    Derived.qDz2Ltr     = Params.qDz2 * Params.Ltr;
    Derived.Ready       = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PURE SLIP FORCES
//------------------------------------------------------------------------------------------------------------------------

float PacejkaMagicFormula::LongitudinalForce(float SlipRatio, float CamberRadians, float NormalLoadNewtons, float GripMultiplier) const noexcept
{
    if (!Derived.Ready || NormalLoadNewtons <= 1.0e-6f) return 0.0f;

    constexpr float MaxBrakeSlip = -0.92f;   // [-] GRIT lockup clamp
    constexpr float MaxDriveSlip = 1.50f;    // [-] GRIT drive clamp preventing sine wraparound
    SlipRatio                    = ClampScalar(SlipRatio, MaxBrakeSlip, MaxDriveSlip);

    const float LoadKilonewtons = NormalLoadNewtons * 0.001f;
    const float LoadDeviation   = (LoadKilonewtons - Derived.Fz0) * Derived.InvFz0;

    const float HorizontalShift = Derived.pHx1Lx + Derived.pHx2Lx * LoadDeviation;
    const float EffectiveSlip   = SlipRatio + HorizontalShift;

    const float NominalPeak = (Derived.pDx1Lx + Derived.pDx2Lx * LoadDeviation) * LoadKilonewtons *
                              (1.0f - Params.pDx3 * CamberRadians * CamberRadians);
    const float LoadFactor  = std::max(1.0f - Params.MuLoadSensitivityLong * std::fabs(LoadDeviation), Params.MuLoadMinFactor);
    const float PeakDx      = NominalPeak * LoadFactor * std::max(0.05f, GripMultiplier);
    const float ShapeCx     = Derived.CxScaled;
    const float StiffBcd    = (Derived.pBx1Lx + Derived.pBx2Lx * LoadDeviation) *
                           (1.0f - Params.pBx3 * std::fabs(CamberRadians)) * LoadKilonewtons;
    const float StiffBx     = StiffBcd / std::max(ShapeCx * PeakDx, 1.0e-8f);

    float CurvatureEx = (Derived.pEx1Lx + Derived.pEx2Lx * LoadDeviation + Derived.pEx3Lx * LoadDeviation * LoadDeviation) *
                        (1.0f - Params.pEx4 * SignScalar(EffectiveSlip));
    CurvatureEx = ClampScalar(CurvatureEx, -1.0f, 1.0f);

    const float VerticalShift = (Derived.pVx1Lx + Derived.pVx2Lx * LoadDeviation) * LoadKilonewtons;
    const float ScaledSlip    = StiffBx * EffectiveSlip;
    const float InnerTerm     = ScaledSlip - CurvatureEx * (ScaledSlip - std::atan(ScaledSlip));
    const float ForceKn       = PeakDx * std::sin(ShapeCx * std::atan(InnerTerm)) + VerticalShift;
    return ForceKn * 1000.0f;
}

float PacejkaMagicFormula::LateralForce(float SlipAngleRadians, float CamberRadians, float NormalLoadNewtons, float GripMultiplier) const noexcept
{
    if (!Derived.Ready || NormalLoadNewtons <= 1.0e-6f) return 0.0f;

    constexpr float MaxSlipAngle = 1.30f;   // [rad] GRIT 75-degree clamp
    SlipAngleRadians             = ClampScalar(SlipAngleRadians, -MaxSlipAngle, MaxSlipAngle);

    const float LoadKilonewtons = NormalLoadNewtons * 0.001f;
    const float LoadDeviation   = (LoadKilonewtons - Derived.Fz0) * Derived.InvFz0;

    const float ShiftVy0      = (Derived.pVy1Ly + Derived.pVy2Ly * LoadDeviation) * LoadKilonewtons;
    const float ShiftVyCamber = LoadKilonewtons * (Derived.pVy3Ly + Derived.pVy4Ly * LoadDeviation) * CamberRadians;
    const float VerticalShift = ShiftVy0 + ShiftVyCamber;

    const float CamberStiffness = (Derived.pCamber1Ly + Derived.pCamber2Ly * LoadDeviation) * LoadKilonewtons;
    const float NominalPeak     = (Derived.pDy1Ly + Derived.pDy2Ly * LoadDeviation) * LoadKilonewtons *
                              (1.0f - Params.pDy3 * CamberRadians * CamberRadians);
    const float LoadFactor      = std::max(1.0f - Params.MuLoadSensitivityLat * std::fabs(LoadDeviation), Params.MuLoadMinFactor);
    const float PeakDy          = NominalPeak * LoadFactor * std::max(0.05f, GripMultiplier);
    const float ShapeCy         = Derived.CyScaled;
    const float StiffBcd        = (Derived.pBy1Ly + Derived.pBy2Ly * LoadDeviation) *
                           (1.0f - Params.pBy3 * std::fabs(CamberRadians)) * LoadKilonewtons;
    const float StiffBy         = StiffBcd / std::max(ShapeCy * PeakDy, 1.0e-8f);

    const float ShiftHy0        = Derived.pHy1Ly + Derived.pHy2Ly * LoadDeviation;
    const float ShiftHyCamber   = (CamberStiffness * CamberRadians - ShiftVyCamber) / std::max(StiffBcd, 1.0e-8f);
    const float HorizontalShift = ShiftHy0 + ShiftHyCamber;
    const float EffectiveAlpha  = SlipAngleRadians + HorizontalShift;

    float CurvatureEy = (Derived.pEy1Ly + Derived.pEy2Ly * LoadDeviation) *
                        (1.0f - Params.pEy3 * SignScalar(EffectiveAlpha));
    CurvatureEy = ClampScalar(CurvatureEy, -1.0f, 1.0f);

    const float ScaledAlpha = StiffBy * EffectiveAlpha;
    const float InnerTerm   = ScaledAlpha - CurvatureEy * (ScaledAlpha - std::atan(ScaledAlpha));
    const float ForceKn     = PeakDy * std::sin(ShapeCy * std::atan(InnerTerm)) + VerticalShift;
    return ForceKn * 1000.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          SELF-ALIGNING TORQUE & COMBINED SLIP
//------------------------------------------------------------------------------------------------------------------------

float PacejkaMagicFormula::SelfAligningTorque(float SlipAngleRadians, float SlipRatio, float LateralForceNewtons, float CamberRadians,
                                              float NormalLoadNewtons, float* OutPneumaticTrailMetres) const noexcept
{
    if (OutPneumaticTrailMetres != nullptr) *OutPneumaticTrailMetres = 0.0f;
    if (!Derived.Ready || NormalLoadNewtons <= 1.0e-6f) return 0.0f;

    SlipAngleRadians = ClampScalar(SlipAngleRadians, -1.30f, 1.30f);
    SlipRatio        = ClampScalar(SlipRatio, -0.92f, 1.50f);

    const float LoadKilonewtons = NormalLoadNewtons * 0.001f;
    const float LoadDeviation   = (LoadKilonewtons - Derived.Fz0) * Derived.InvFz0;

    const float StiffKappa = std::max(
        (Derived.pBx1Lx + Derived.pBx2Lx * LoadDeviation) * (1.0f - Params.pBx3 * std::fabs(CamberRadians)) * LoadKilonewtons,
        1.0e-6f);
    const float StiffAlpha = std::max(
        (Derived.pBy1Ly + Derived.pBy2Ly * LoadDeviation) * (1.0f - Params.pBy3 * std::fabs(CamberRadians)) * LoadKilonewtons,
        1.0e-6f);

    // 📝 MF6.1 Eq. 80 equivalent slip angle with GRIT's elastic-domain tangent clamp.
    const float StiffnessRatio   = std::sqrt(StiffKappa / StiffAlpha);
    const float ClampedHalfSlip  = ClampScalar(SlipRatio * 0.5f, -1.5f, 1.5f);
    const float AngleCorrection  = std::atan(StiffnessRatio * std::tan(ClampedHalfSlip));
    const float EquivalentAlpha  = SlipAngleRadians + AngleCorrection * SignScalar(SlipAngleRadians);

    const float TrailPeakDt      = LoadKilonewtons * (Derived.qDz1Ltr + Derived.qDz2Ltr * LoadDeviation) *
                              (1.0f + Params.qDz3 * std::fabs(CamberRadians));
    const float TrailStiffBt     = (Params.qBz1 + Params.qBz2 * LoadDeviation + Params.qBz3 * LoadDeviation * LoadDeviation) *
                              (1.0f + Params.qBz5 * std::fabs(CamberRadians));
    const float TrailCurvEt      = (Params.qEz1 + Params.qEz2 * LoadDeviation + Params.qEz3 * LoadDeviation * LoadDeviation) *
                              (1.0f + Params.qEz4 * CamberRadians + Params.qEz5 * CamberRadians * CamberRadians);
    const float ScaledAlpha      = TrailStiffBt * EquivalentAlpha;
    const float InnerTerm        = ScaledAlpha - TrailCurvEt * (ScaledAlpha - std::atan(ScaledAlpha));
    const float NominalTrail     = TrailPeakDt * std::cos(Params.qCz1 * std::atan(InnerTerm)) * std::cos(SlipAngleRadians);

    // 📝 GRIT high-slip burnout/lockup pneumatic trail fade across combined slip [1.0 .. 2.5].
    const float TotalSlip        = std::sqrt(SlipAngleRadians * SlipAngleRadians + SlipRatio * SlipRatio);
    const float BurnoutFade      = 1.0f - ClampScalar((TotalSlip - 1.0f) / 1.5f, 0.0f, 1.0f);
    const float EffectiveTrail   = NominalTrail * BurnoutFade;

    if (OutPneumaticTrailMetres != nullptr) *OutPneumaticTrailMetres = EffectiveTrail;
    return -LateralForceNewtons * EffectiveTrail;
}

CombinedPacejkaResult PacejkaMagicFormula::CombinedForces(float SlipRatio, float SlipAngleRadians, float CamberRadians,
                                                          float NormalLoadNewtons, float GripMultiplier) const noexcept
{
    CombinedPacejkaResult Result{};
    if (!Derived.Ready || NormalLoadNewtons <= 1.0e-6f) return Result;

    const float PureFx = LongitudinalForce(SlipRatio, CamberRadians, NormalLoadNewtons, GripMultiplier);
    const float PureFy = LateralForce(SlipAngleRadians, CamberRadians, NormalLoadNewtons, GripMultiplier);

    const float ClampedKappa    = ClampScalar(SlipRatio, -0.92f, 1.50f);
    const float ClampedAlpha    = ClampScalar(SlipAngleRadians, -1.30f, 1.30f);
    const float LoadKilonewtons = NormalLoadNewtons * 0.001f;
    const float LoadDeviation   = (LoadKilonewtons - Derived.Fz0) * Derived.InvFz0;

    // 📝 MF6.1 ellipse-based combined-slip weighting using normalized perpendicular slips.
    const float KappaNorm       = ClampedKappa;
    const float AlphaNorm       = std::tan(ClampedAlpha);
    const float CombinedMag     = std::sqrt(KappaNorm * KappaNorm + AlphaNorm * AlphaNorm);

    float WeightFx = 1.0f;
    float WeightFy = 1.0f;
    if (CombinedMag > 1.0e-6f)
    {
        WeightFx = std::cos(Params.rCx1 * std::atan(Params.rBx1 * AlphaNorm));
        const float SlopeBy = Params.rBy1 * std::cos(std::atan(Params.rBy2 * (ClampedAlpha - Params.rBy4)));
        WeightFy            = std::cos(Params.rCy1 * std::atan(SlopeBy * ClampedKappa));
    }

    const float PlySteerPeak = (Params.rVy1 + Params.rVy2 * LoadDeviation + Params.rVy3 * CamberRadians) *
                               std::cos(std::atan(Params.rVy4 * ClampedAlpha));
    const float PlySteerN    = PlySteerPeak * std::sin(std::atan(Params.rVy5 * ClampedKappa)) * LoadKilonewtons * 1000.0f;

    Result.LongitudinalForceNewtons = PureFx * std::max(0.0f, WeightFx);
    Result.LateralForceNewtons      = PureFy * std::max(0.0f, WeightFy) + PlySteerN;
    Result.AligningTorqueNm         = SelfAligningTorque(
        ClampedAlpha,
        ClampedKappa,
        Result.LateralForceNewtons,
        CamberRadians,
        NormalLoadNewtons,
        &Result.PneumaticTrailMetres);
    return Result;
}

} // namespace Frontier::Vehicle
