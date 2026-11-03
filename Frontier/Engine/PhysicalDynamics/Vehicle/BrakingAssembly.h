/*==========================================================================================================================================
                                                             BRAKINGASSEMBLY.H
==========================================================================================================================================*/
// 🧩 Hydraulic front/rear bias, ventilated disc rotor thermal/fade model, 4-channel ABS, handbrake, and GRIT line-lock burnout circuit.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/VehicleMath.h"

#include <array>

namespace Frontier::Vehicle
{

struct BrakeAssemblyParameters
{
    float MaxPedalTorqueFrontNm    = 2850.0f;   // [N*m] per-wheel peak front hydraulic clamping torque
    float MaxPedalTorqueRearNm     = 1950.0f;   // [N*m] per-wheel peak rear hydraulic clamping torque
    float HandbrakeRearTorqueNm    = 2600.0f;   // [N*m] mechanical rear parking/drift brake torque
    float HydraulicBuildRatePerS   = 24.0f;     // [1/s] master-cylinder line pressure response

    // 📝 Ventilated cast-iron / carbon-ceramic rotor thermal parameters.
    bool  ThermalEnabled           = true;      // [-] integrate rotor heat & high-temp pad fade
    float AmbientTemperatureK      = 293.15f;   // [K] ambient cooling air
    float RotorMassKg              = 7.8f;      // [kg] ventilated disc thermal mass
    float RotorSpecificHeatJkgK    = 490.0f;    // [J/(kg*K)] grey iron specific heat
    float ConvectiveBaseWK         = 14.0f;     // [W/K] still-air rotor cooling
    float ConvectiveVanePerMpsWK   = 2.6f;      // [W/(K*m/s)] speed-pumped vane airflow cooling
    float RotorAreaSquareMetres    = 0.16f;     // [m^2] dual-face radiative area
    float FadeOnsetTempK           = 820.0f;    // [K] pad outgassing fade onset (~547 degC)
    float FadeFloorFactor          = 0.52f;     // [-] minimum faded friction retention

    // 📝 4-channel Anti-Lock Braking System (ABS).
    bool  AbsEnabled               = true;      // [-] modulate individual calipers above slip threshold
    float AbsSlipThreshold         = 0.15f;     // [-] |kappa| threshold triggering ABS pressure relief
    float AbsReleaseFactor         = 0.32f;     // [-] pressure floor during ABS dump phase
    float AbsCycleFrequencyHz      = 15.0f;     // [Hz] solenoid pump modulation rate
    float AbsMinVehicleSpeedMps    = 2.2f;      // [m/s] low-speed threshold allowing final stop lock
};

struct CornerBrakeReading
{
    float AppliedTorqueNm  = 0.0f;      // [N*m] net brake clamping torque delivered to hub
    float RotorTempK       = 293.15f;   // [K] ventilated disc temperature
    float FadeMultiplier   = 1.0f;      // [-] thermal pad friction retention [FadeFloor..1]
    float PressureFraction = 0.0f;      // [0..1] hydraulic caliper pressure state
    bool  AbsActive        = false;     // [-] true while ABS solenoid dumps/pulses this corner
};

class BrakingAssembly
{
  public:
    void Reset(const BrakeAssemblyParameters& Params) noexcept
    {
        PhaseRadians = 0.0f;
        for (CornerBrakeReading& Corner : Corners)
        {
            Corner                = CornerBrakeReading{};
            Corner.RotorTempK     = Params.AmbientTemperatureK;
            Corner.FadeMultiplier = 1.0f;
        }
    }

    void Step(const BrakeAssemblyParameters& Params, float BrakePedal, bool Handbrake, bool LineLockFrontOnly,
              float VehicleSpeedMps, const std::array<float, 4>& WheelSlipRatios,
              const std::array<float, 4>& WheelOmegas, const std::array<float, 4>& EscExtraTorquesNm,
              float DeltaSeconds) noexcept
    {
        const float Dt    = std::max(DeltaSeconds, 1.0e-5f);
        const float Pedal = ClampScalar(BrakePedal, 0.0f, 1.0f);
        PhaseRadians      = WrapAngleRadians(PhaseRadians + TwoPiConstant * Params.AbsCycleFrequencyHz * Dt);
        const float Pulse = 0.5f + 0.5f * std::sin(PhaseRadians);

        for (size_t Idx = 0; Idx < 4; ++Idx)
        {
            const bool          Front  = Idx < 2;
            CornerBrakeReading& Corner = Corners[Idx];

            // Line-lock holds full front hydraulic pressure while freeing rear wheels for standing burnouts.
            float TargetPressure = LineLockFrontOnly ? (Front ? 1.0f : 0.0f) : Pedal;
            const float Alpha    = ClampScalar(Dt * Params.HydraulicBuildRatePerS, 0.0f, 1.0f);
            Corner.PressureFraction += (TargetPressure - Corner.PressureFraction) * Alpha;

            const float MaxHydraulic = Front ? Params.MaxPedalTorqueFrontNm : Params.MaxPedalTorqueRearNm;
            float       HydraulicNm  = Corner.PressureFraction * MaxHydraulic;

            // 4-channel ABS modulation when braking forward above crawl speed.
            Corner.AbsActive = false;
            if (Params.AbsEnabled && !LineLockFrontOnly && !(Handbrake && !Front) &&
                HydraulicNm > 25.0f && std::fabs(VehicleSpeedMps) > Params.AbsMinVehicleSpeedMps &&
                WheelSlipRatios[Idx] < -Params.AbsSlipThreshold)
            {
                Corner.AbsActive = true;
                const float Mod  = LerpScalar(Params.AbsReleaseFactor, 0.88f, Pulse);
                HydraulicNm     *= Mod;
            }

            const float MechanicalRear = (!Front && Handbrake) ? Params.HandbrakeRearTorqueNm : 0.0f;

            // Rotor thermal integration & high-temp pad fade.
            if (Params.ThermalEnabled)
            {
                const float TotalRawNm  = HydraulicNm + MechanicalRear + std::max(0.0f, EscExtraTorquesNm[Idx]);
                const float HeatInW     = TotalRawNm * std::fabs(WheelOmegas[Idx]);
                const float ConvectW    = (Params.ConvectiveBaseWK + Params.ConvectiveVanePerMpsWK * std::fabs(VehicleSpeedMps)) *
                                       (Corner.RotorTempK - Params.AmbientTemperatureK);
                const float T2          = Corner.RotorTempK * Corner.RotorTempK;
                const float Amb2        = Params.AmbientTemperatureK * Params.AmbientTemperatureK;
                const float RadiativeW  = StefanBoltzmannConstant * 0.78f * Params.RotorAreaSquareMetres * (T2 * T2 - Amb2 * Amb2);
                const float CapacityJK  = std::max(Params.RotorMassKg * Params.RotorSpecificHeatJkgK, 200.0f);

                Corner.RotorTempK += ((HeatInW - ConvectW - RadiativeW) / CapacityJK) * Dt;
                Corner.RotorTempK  = ClampScalar(Corner.RotorTempK, Params.AmbientTemperatureK, 1450.0f);

                if (Corner.RotorTempK > Params.FadeOnsetTempK)
                {
                    const float Excess    = (Corner.RotorTempK - Params.FadeOnsetTempK) / 320.0f;
                    Corner.FadeMultiplier = LerpScalar(1.0f, Params.FadeFloorFactor, ClampScalar(Excess, 0.0f, 1.0f));
                }
                else
                {
                    Corner.FadeMultiplier = 1.0f;
                }
            }
            else
            {
                Corner.FadeMultiplier = 1.0f;
            }

            Corner.AppliedTorqueNm = (HydraulicNm + MechanicalRear + std::max(0.0f, EscExtraTorquesNm[Idx])) * Corner.FadeMultiplier;
        }
    }

    [[nodiscard]] const std::array<CornerBrakeReading, 4>& QueryCorners() const noexcept { return Corners; }

  private:
    std::array<CornerBrakeReading, 4> Corners{};
    float                             PhaseRadians = 0.0f;
};

} // namespace Frontier::Vehicle
