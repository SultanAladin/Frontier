/*==========================================================================================================================================
                                                           STEERINGANDASSISTS.H
==========================================================================================================================================*/
// 🧩 GRIT Ackermann steering geometry, speed-sensitive variable ratio, SAT column feedback, TCS, ESC, and Launch Control assists.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/VehicleMath.h"

#include <array>

namespace Frontier::Vehicle
{

struct SteeringParameters
{
    float MaxSteerAngleRadians   = 0.58f;     // [rad] ~33.2 deg full-lock front steer angle
    float SteeringColumnRatio    = 13.5f;     // [-] steering wheel rad per road-wheel rad
    float AckermannFraction      = 0.82f;     // [0..1] 0 = parallel steer, 1 = pure kinematic Ackermann
    float SteeringRateRadPerSec  = 2.8f;      // [rad/s] hydraulic/EPS rack rate limit
    bool  SpeedSensitiveEnabled  = true;      // [-] reduce max lock smoothly with speed (GRIT SteeringAssembly)
    float LowSpeedLimitMps       = 8.33f;     // [m/s] 30 km/h full-authority threshold
    float HighSpeedLimitMps      = 41.67f;    // [m/s] 150 km/h reduced-authority threshold
    float HighSpeedAuthority     = 0.42f;     // [-] fraction of MaxSteerAngleRadians retained at >= 150 km/h
};

struct DriverAssistParameters
{
    bool  TractionControlEnabled = false;     // [-] TCS throttle trim above TargetDriveSlip
    float TargetDriveSlip        = 0.14f;     // [-] optimal peak-traction slip ratio target
    float TractionCutGain        = 3.4f;      // [-] proportional throttle reduction per unit excess slip

    bool  StabilityControlEnabled= false;     // [-] ESC yaw-rate error selective corner braking
    float YawErrorDeadbandRadS   = 0.10f;     // [rad/s] benign slip yaw-rate deadband
    float StabilityBrakeGainNm   = 2200.0f;   // [N*m/(rad/s)] corrective single-wheel brake gain

    bool  LaunchControlEnabled   = false;     // [-] hold launch RPM / optimal launch slip from standstill
};

struct SteeringAndAssistOutputs
{
    float                CenterSteerRadians      = 0.0f;                      // [rad] virtual bicycle-model front steer (+left)
    float                FrontLeftSteerRadians   = 0.0f;                      // [rad] FL Ackermann steer angle
    float                FrontRightSteerRadians  = 0.0f;                      // [rad] FR Ackermann steer angle
    float                SteeringWheelRadians    = 0.0f;                      // [rad] cabin steering column visual angle
    float                SteeringColumnTorqueNm  = 0.0f;                      // [N*m] driver-felt self-aligning feedback torque
    float                ModulatedThrottle       = 0.0f;                      // [0..1] post-TCS / Launch-Control engine throttle
    std::array<float, 4> EscCornerBrakeTorquesNm {0.0f, 0.0f, 0.0f, 0.0f};    // [N*m] selective ESC corrective brake torques
    bool                 TractionControlActive   = false;                     // [-] true when TCS trims throttle
    bool                 StabilityControlActive  = false;                     // [-] true when ESC applies yaw brake
};

class SteeringAndAssists
{
  public:
    void Reset() noexcept { CurrentRackRadians = 0.0f; }

    [[nodiscard]] SteeringAndAssistOutputs Step(const SteeringParameters& SteerParams, const DriverAssistParameters& AssistParams,
                                                float RawSteerInput, float RawThrottleInput, float RawBrakeInput,
                                                float ForwardSpeedMps, float ChassisYawRateRadS,
                                                float WheelbaseMetres, float FrontTrackMetres,
                                                const std::array<float, 4>& WheelSlipRatios,
                                                float FrontAligningTorqueSumNm, float DeltaSeconds) noexcept
    {
        SteeringAndAssistOutputs Out{};
        const float              Dt       = std::max(DeltaSeconds, 1.0e-5f);
        const float              SteerCmd = ClampScalar(RawSteerInput, -1.0f, 1.0f);

        // 1. Speed-sensitive authority & rack rate limiter (+left steer convention: SteerInput > 0 turns right in UI or +left;
        //    keep consistent right-handed +Z up: positive CenterSteerRadians yaws +Z = left).
        float Authority = 1.0f;
        if (SteerParams.SpeedSensitiveEnabled)
        {
            const float SpeedSpan = std::max(SteerParams.HighSpeedLimitMps - SteerParams.LowSpeedLimitMps, 1.0f);
            const float T         = ClampScalar((std::fabs(ForwardSpeedMps) - SteerParams.LowSpeedLimitMps) / SpeedSpan, 0.0f, 1.0f);
            Authority             = LerpScalar(1.0f, SteerParams.HighSpeedAuthority, T);
        }

        const float TargetRack = SteerCmd * SteerParams.MaxSteerAngleRadians * Authority;
        const float MaxStep    = SteerParams.SteeringRateRadPerSec * Dt;
        CurrentRackRadians    += ClampScalar(TargetRack - CurrentRackRadians, -MaxStep, MaxStep);

        // 2. Kinematic Ackermann inner/outer front wheel geometry: cot(delta_outer) - cot(delta_inner) = Track / Wheelbase.
        float LeftAngle  = CurrentRackRadians;
        float RightAngle = CurrentRackRadians;
        if (std::fabs(CurrentRackRadians) > 1.0e-4f && WheelbaseMetres > 0.2f)
        {
            const float TanCenter  = std::tan( std::fabs(CurrentRackRadians));
            const float HalfRatio  = (0.5f * FrontTrackMetres) / WheelbaseMetres;
            const float InnerAck   = std::atan(TanCenter / std::max(1.0f - HalfRatio * TanCenter, 0.15f));
            const float OuterAck   = std::atan(TanCenter / (1.0f + HalfRatio * TanCenter));
            const float InnerBlend = LerpScalar(std::fabs(CurrentRackRadians), InnerAck, SteerParams.AckermannFraction);
            const float OuterBlend = LerpScalar(std::fabs(CurrentRackRadians), OuterAck, SteerParams.AckermannFraction);

            if (CurrentRackRadians > 0.0f)
            {
                // Turning left (+Z): FL is inner wheel, FR is outer wheel.
                LeftAngle  = InnerBlend;
                RightAngle = OuterBlend;
            }
            else
            {
                // Turning right (-Z): FR is inner wheel, FL is outer wheel.
                LeftAngle  = -OuterBlend;
                RightAngle = -InnerBlend;
            }
        }

        Out.CenterSteerRadians     = CurrentRackRadians;
        Out.FrontLeftSteerRadians  = LeftAngle;
        Out.FrontRightSteerRadians = RightAngle;
        Out.SteeringWheelRadians   = CurrentRackRadians * SteerParams.SteeringColumnRatio;
        Out.SteeringColumnTorqueNm = FrontAligningTorqueSumNm / std::max(SteerParams.SteeringColumnRatio, 1.0f);

        // 3. Traction Control (TCS) & Launch Control.
        float ModThrottle = ClampScalar(RawThrottleInput, 0.0f, 1.0f);
        if (AssistParams.LaunchControlEnabled && std::fabs(ForwardSpeedMps) < 1.2f && RawBrakeInput > 0.2f && ModThrottle > 0.5f)
        {
            ModThrottle = 0.58f;
        }
        if (AssistParams.TractionControlEnabled && ModThrottle > 0.02f)
        {
            const float PeakSpin = std::max({WheelSlipRatios[0], WheelSlipRatios[1], WheelSlipRatios[2], WheelSlipRatios[3]});
            if (PeakSpin > AssistParams.TargetDriveSlip)
            {
                const float Excess          = PeakSpin - AssistParams.TargetDriveSlip;
                const float Cut             = ClampScalar(Excess * AssistParams.TractionCutGain, 0.0f, 0.88f);
                ModThrottle                *= (1.0f - Cut);
                Out.TractionControlActive   = true;
            }
        }
        Out.ModulatedThrottle = ModThrottle;

        // 4. Electronic Stability Control (ESC): compare actual yaw rate to kinematic bicycle target yaw rate.
        if (AssistParams.StabilityControlEnabled && ForwardSpeedMps > 5.0f && WheelbaseMetres > 0.5f)
        {
            const float UndersteerGrad = 0.0022f;
            const float TargetYawRate  = (ForwardSpeedMps * std::tan(CurrentRackRadians)) /
                                        (WheelbaseMetres * (1.0f + UndersteerGrad * ForwardSpeedMps * ForwardSpeedMps));
            const float YawError       = ChassisYawRateRadS - TargetYawRate;

            if (std::fabs(YawError) > AssistParams.YawErrorDeadbandRadS)
            {
                Out.StabilityControlActive = true;
                const float ExcessYaw      = std::fabs(YawError) - AssistParams.YawErrorDeadbandRadS;
                const float BrakeNm        = ClampScalar(ExcessYaw * AssistParams.StabilityBrakeGainNm, 0.0f, 1800.0f);

                if (YawError * CurrentRackRadians >= 0.0f)
                {
                    // Oversteer (yawing faster than commanded): brake outer front wheel to create counter-yaw moment.
                    if (ChassisYawRateRadS > 0.0f) Out.EscCornerBrakeTorquesNm[1] = BrakeNm;   // turning left -> brake FR
                    else                           Out.EscCornerBrakeTorquesNm[0] = BrakeNm;   // turning right -> brake FL
                }
                else
                {
                    // Understeer (ploughing wide): brake inner rear wheel to help turn the nose in.
                    if (CurrentRackRadians > 0.0f) Out.EscCornerBrakeTorquesNm[2] = BrakeNm * 0.75f;   // turning left -> brake RL
                    else                           Out.EscCornerBrakeTorquesNm[3] = BrakeNm * 0.75f;   // turning right -> brake RR
                }
            }
        }
        return Out;
    }

  private:
    float CurrentRackRadians = 0.0f;
};

} // namespace Frontier::Vehicle
