/*==========================================================================================================================================
                                                         SUSPENSIONKINEMATICS.H
==========================================================================================================================================*/
// 🧩 Implicitly damped progressive coilover strut and axle anti-roll bar force evaluation ported from GRIT & ProjectDrive.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/SuspensionSpecification.h"

namespace Frontier::Vehicle
{

struct StrutReading
{
    float LengthMetres        = 0.34f;   // [m] current strut length
    float CompressionMetres   = 0.0f;    // [m] RestLength - Length (+ in jounce)
    float CompressionRateMps  = 0.0f;    // [m/s] + compressing, - extending
    float SpringForceNewtons  = 0.0f;    // [N] coil + progressive + preload force
    float DamperForceNewtons  = 0.0f;    // [N] digressive shock force
    float StopForceNewtons    = 0.0f;    // [N] elastomer bump/droop stop force
    float AntiRollForceNewtons= 0.0f;    // [N] axle anti-roll bar transfer force
    float TotalStrutForceN    = 0.0f;    // [N] total upward strut reaction on sprung chassis
    float DynamicCamberRad    = 0.0f;    // [rad] static camber + kinematic bump camber gain
};

class SuspensionKinematics
{
  public:
    [[nodiscard]] static float EvaluateDigressiveDamper(const StrutSpecification& Spec, float CompressionRateMps) noexcept
    {
        const float Speed = std::fabs(CompressionRateMps);
        const float Knee  = std::max(Spec.DigressiveKneeMps, 1.0e-3f);
        const float LowC  = CompressionRateMps >= 0.0f ? Spec.BumpDampingLowNsm : Spec.ReboundDampingLowNsm;
        const float HighC = CompressionRateMps >= 0.0f ? Spec.BumpDampingHighNsm : Spec.ReboundDampingHighNsm;

        const float Magnitude = Speed <= Knee ? (LowC * Speed) : (LowC * Knee + HighC * (Speed - Knee));
        return SignScalar(CompressionRateMps) * Magnitude;
    }

    [[nodiscard]] static StrutReading EvaluateCorner(const StrutSpecification& Spec, float CurrentLengthMetres,
                                                     float CompressionRateMps, float OppositeCompressionMetres,
                                                     float AntiRollStiffnessNpm, float EffectiveCornerMassKg,
                                                     float DeltaSeconds, bool LeftSide) noexcept
    {
        StrutReading Out{};
        const float  ClampedLength = ClampScalar(CurrentLengthMetres, Spec.MinLengthMetres - 0.02f, Spec.MaxLengthMetres + 0.02f);
        const float  Compression   = Spec.RestLengthMetres - ClampedLength;

        // 1. Progressive coil spring + preload.
        const float SpringForce = Spec.PreloadForceNewtons +
                                  Spec.SpringRateNpm * Compression +
                                  Spec.CubicProgressiveRate * Compression * Compression * Compression;

        // 2. Implicitly stabilized digressive shock absorber: scale by 1 / (1 + c_eff * dt / m_eff).
        const float RawDamper      = EvaluateDigressiveDamper(Spec, CompressionRateMps);
        const float EffectiveSlope = std::fabs(CompressionRateMps) > 1.0e-4f
                                         ? (std::fabs(RawDamper) / std::fabs(CompressionRateMps))
                                         : Spec.BumpDampingLowNsm;
        const float ImplicitGain   = 1.0f / (1.0f + (EffectiveSlope * std::max(DeltaSeconds, 1.0e-5f)) /
                                                        std::max(EffectiveCornerMassKg, 40.0f));
        const float DamperForce    = RawDamper * ImplicitGain;

        // 3. Elastomer bump stop & rebound droop limiter.
        float StopForce = 0.0f;
        const float BumpClearance = ClampedLength - Spec.MinLengthMetres;
        if (BumpClearance < Spec.BumpStopZoneMetres)
        {
            const float Depth = BumpClearance < 0.0f ? (Spec.BumpStopZoneMetres - BumpClearance) : (Spec.BumpStopZoneMetres - BumpClearance);
            StopForce += Spec.BumpStopRateNpm * Depth * (1.0f + 8.0f * Depth);
        }
        if (ClampedLength > Spec.MaxLengthMetres)
        {
            StopForce -= Spec.DroopStopRateNpm * (ClampedLength - Spec.MaxLengthMetres);
        }

        // 4. Anti-roll bar force opposing roll difference across the axle.
        const float AntiRollForce = AntiRollStiffnessNpm * (Compression - OppositeCompressionMetres);

        Out.LengthMetres         = ClampedLength;
        Out.CompressionMetres    = Compression;
        Out.CompressionRateMps   = CompressionRateMps;
        Out.SpringForceNewtons   = SpringForce;
        Out.DamperForceNewtons   = DamperForce;
        Out.StopForceNewtons     = StopForce;
        Out.AntiRollForceNewtons = AntiRollForce;
        Out.TotalStrutForceN     = std::max(0.0f, SpringForce + DamperForce + StopForce + AntiRollForce);

        const float CamberSign   = LeftSide ? 1.0f : -1.0f;
        Out.DynamicCamberRad     = CamberSign * (Spec.StaticCamberRadians + Spec.CamberGainPerMetre * Compression);
        return Out;
    }
};

} // namespace Frontier::Vehicle
