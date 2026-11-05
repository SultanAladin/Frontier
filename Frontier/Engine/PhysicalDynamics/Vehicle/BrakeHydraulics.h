/*==========================================================================================================================================
                                                             BRAKEHYDRAULICS.H
==========================================================================================================================================*/
// 🧩 disk-brake hydraulics + thermal fade + ABS
//
//    Port of GRIT's FBrakingSpecifications (source-only Components/BrakingSpecifications.h): a disk brake whose torque
//    comes from hydraulic clamping force on a temperature-dependent friction coefficient, with heat generation, cooling,
//    and thermal FADE. Plus an anti-lock (ABS) regulator layered on top — GRIT ships no ABS, so this is a standard
//    slip-regulating modulator that keeps each wheel near the peak-grip slip target by pulsing the line pressure.
//
//    Physics (from the GRIT header):
//      T_brake = 2 · μ(T) · (P · A_piston) · R_eff              [N·m]   (both pad faces)
//      μ(T)    = lerp(μ_cold, μ_hot) across [FadeStart, FadeEnd] [-]     (fade above ~300 °C)
//      Q̇_gen   = T_brake · |ω_wheel|                           [W]
//      Q̇_cool  = h·(1 + 0.5·√(v/30)) · A_disk · (T − T_amb)    [W]     (airspeed-boosted convection)
//      dT/dt   = (Q̇_gen − Q̇_cool) / (m_disk · c_p)            [K/s]
//    Hydraulic line pressure slews toward the commanded pressure at PressureRiseRate / PressureFallRate [Pa/s].

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Frontier::Vehicle
{

//------------------------------------------------------------------------------------------------------------------------
struct BrakingParameters
{
    // Disk geometry
    float DiskOuterRadius = 0.16f;   // [m]
    float DiskInnerRadius = 0.09f;   // [m]
    int   NumPistons      = 4;       // [-]
    float PistonDiameter  = 0.044f;  // [m]

    // Friction / thermal fade
    float FrictionCoeffCold = 0.42f; // [-]
    float FrictionCoeffHot  = 0.38f; // [-]
    float FadeStartTemp     = 573.15f; // [K] 300 °C
    float FadeEndTemp       = 873.15f; // [K] 600 °C

    // Hydraulics
    float MaxBrakePressure     = 12.0e6f; // [Pa] 120 bar
    float MaxHandbrakePressure = 9.5e6f;  // [Pa] 95 bar
    float PressureRiseRate     = 50.0e6f; // [Pa/s]
    float PressureFallRate     = 80.0e6f; // [Pa/s]

    // Thermal
    float DiskMass            = 6.5f;   // [kg]
    float SpecificHeatCapacity= 460.0f; // [J/kg/K] cast iron
    float ConvectionCoeff     = 85.0f;  // [W/m²/K]
    float DiskSurfaceArea     = 0.095f; // [m²]
    float AmbientTemp         = 293.15f;// [K]

    [[nodiscard]] float PistonArea() const noexcept
    {
        const float r = 0.5f * PistonDiameter;
        return 3.14159265358979f * r * r * static_cast<float>(NumPistons);
    }
    [[nodiscard]] float EffectiveRadius() const noexcept { return 0.5f * (DiskOuterRadius + DiskInnerRadius); }

    [[nodiscard]] float FrictionCoefficient(float T) const noexcept
    {
        if (T <= FadeStartTemp) return FrictionCoeffCold;
        if (T >= FadeEndTemp)   return FrictionCoeffHot;
        const float a = (T - FadeStartTemp) / (FadeEndTemp - FadeStartTemp);
        return FrictionCoeffCold + a * (FrictionCoeffHot - FrictionCoeffCold);
    }
    [[nodiscard]] float BrakeTorque(float pressure_Pa, float T) const noexcept
    {
        return 2.0f * FrictionCoefficient(T) * (pressure_Pa * PistonArea()) * EffectiveRadius();
    }
    [[nodiscard]] float HeatGeneration(float torque_Nm, float omega) const noexcept
    {
        return torque_Nm * std::fabs(omega);
    }
    [[nodiscard]] float CoolingRate(float T, float airspeed) const noexcept
    {
        const float airFactor = 1.0f + 0.5f * std::sqrt(std::max(0.0f, airspeed / 30.0f));
        return ConvectionCoeff * airFactor * DiskSurfaceArea * (T - AmbientTemp);
    }

    static BrakingParameters DefaultGT3() { return BrakingParameters{}; }  // header defaults already GT3-class
};

//------------------------------------------------------------------------------------------------------------------------
// Anti-lock braking. A wheel that locks under braking has slip ratio κ → −1 and loses both braking AND steering grip.
//   This is a standard slip-REGULATING loop: while the pedal is down and the car is moving, it continuously scales
//   the commanded line pressure down in proportion to how far the measured slip exceeds the peak-grip target, holding the
//   wheel near the top of its μ–slip curve instead of letting it lock. (A proportional regulator tracks the target far
//   better than a bang-bang dump/re-apply cycle, which lets the wheel lock during each re-apply phase.)
struct AbsParameters
{
    bool  Enabled     = true;
    float SlipTarget  = 0.12f;   // [-] target |slip| (near the μ–slip peak for a race tyre)
    float Gain        = 8.0f;    // [-] pressure-cut gain per unit slip overshoot
    float MinSpeed    = 2.0f;    // [m/s] ABS inactive below this (let the car stop / stiction hold)
    float DumpFloor   = 0.05f;   // [-] minimum pressure fraction the regulator will bleed to
    // 📝 Cycled ABS: while over the target the pressure scale dumps at DumpRate, otherwise it reapplies at
    //    ReapplyRate — the dump/hold/reapply rhythm of a production modulator, which holds slip at the target instead
    //    of hunting through it. 0 keeps the proportional loop (the source behaviour).
    float ReapplyRate = 0.0f;    // [1/s] reapply ramp (0: proportional loop)
    float DumpRate    = 20.0f;   // [1/s] dump ramp while over the target
    float SlipBand    = 0.05f;   // [-] hold band around the target (dump above, reapply below, hold inside)
    float RegulationFloor = 0.22f;   // [-] lowest scale the regulating dump reaches (deep enough to recover, shallow enough
                                     //      that the climb back stays inside the flat zone; only a true lock blows past it)
};

struct BrakeWheelReading
{
    float Pressure_Pa   = 0.0f;
    float Temperature_K = 293.15f;
    float Torque_Nm     = 0.0f;
    float FrictionCoeff = 0.42f;
    float HeatGen_W     = 0.0f;
    float Cooling_W     = 0.0f;
    bool  AbsActive     = false;   // currently cutting pressure to regulate slip
    float AbsScale      = 1.0f;    // [-] cycled-ABS pressure scale reading (1 while the loop is proportional)
    bool  AbsLatched    = false;   // cycled ABS was over the band last step (entry-cut edge trigger)
};

struct BrakeWheelInput
{
    float PedalCommand = 0.0f;   // [0..1] foot-brake fraction for this wheel
    float WheelOmega   = 0.0f;   // [rad/s]
    float SlipRatio    = 0.0f;   // κ (negative under braking)
    float Airspeed     = 0.0f;   // [m/s] for convective cooling
    bool  Braked       = true;
};

struct BrakeWheelOutput
{
    float BrakeTorque_Nm = 0.0f;  // magnitude, opposes spin at the caller
    float Pressure_Pa    = 0.0f;
    float Temperature_K  = 293.15f;
    float FrictionCoeff  = 0.42f;
    bool  AbsActive      = false;
};

//------------------------------------------------------------------------------------------------------------------------
class BrakeHydraulics
{
public:
    void Configure(std::size_t nWheels, const BrakingParameters& bp, const AbsParameters& abs)
    {
        Parameters = bp; AntilockParameters = abs;
        WheelRecords.assign(std::max<std::size_t>(1, nWheels), BrakeWheelReading{});
        for (auto& w : WheelRecords) w.Temperature_K = bp.AmbientTemp;
    }
    void Reset()
    {
        for (auto& w : WheelRecords) { w = BrakeWheelReading{}; w.Temperature_K = Parameters.AmbientTemp; }
    }

    [[nodiscard]] const BrakingParameters& Params() const noexcept { return Parameters; }
    [[nodiscard]] const BrakeWheelReading&   Reading(std::size_t i) const noexcept
    {
        return WheelRecords[std::min(i, WheelRecords.size() - 1)];
    }

    BrakeWheelOutput Step(std::size_t i, const BrakeWheelInput& in, float dt)
    {
        BrakeWheelReading& w = WheelRecords[std::min(i, WheelRecords.size() - 1)];
        BrakeWheelOutput out{};
        if (!in.Braked) { out.Pressure_Pa = w.Pressure_Pa; out.Temperature_K = w.Temperature_K; return CoolOnly(w, in, dt, out); }

        const float pedal = std::clamp(in.PedalCommand, 0.0f, 1.0f);

        // ── ABS: proportional slip regulator ───────────────────────────────────────────────────────────────────────
        //   Cut the commanded pressure in proportion to how far |slip| overshoots the target, clamped to [DumpFloor, 1].
        //   This holds the wheel near the μ–slip peak instead of letting it lock.
        float absScale = 1.0f;
        const bool absEligible = AntilockParameters.Enabled && pedal > 0.05f && (in.Airspeed > AntilockParameters.MinSpeed);
        const float over = std::fabs(in.SlipRatio) - AntilockParameters.SlipTarget;
        if (AntilockParameters.ReapplyRate > 0.0f)
        {
            // 📝 Three-state cycled ABS: dump above the band, reapply below it, HOLD inside — the hold phase parks the
            //    pressure where slip ≈ target instead of limit-cycling through it. Crossing the band edge trims the pressure
            //    at once; while still over it keeps trimming in small steps. The regulating dump never goes below
            //    RegulationFloor: crashing to the floor every cycle forces a full climb back through the linear falloff
            //    (most of the cycle wasted under the cap); parking shallow keeps the whole cycle inside the flat zone.
            //    Only a true deep lock (past twice the target) blows past the floor with the full dump rate.
            const bool overBand = absEligible && over > AntilockParameters.SlipBand;
            if (overBand && !w.AbsLatched) w.AbsScale = std::max(AntilockParameters.RegulationFloor, w.AbsScale * 0.98f);
            else if (overBand && over > AntilockParameters.SlipTarget) w.AbsScale -= AntilockParameters.DumpRate * dt;
            else if (overBand) w.AbsScale = std::max(AntilockParameters.RegulationFloor, w.AbsScale - AntilockParameters.ReapplyRate * 1.5f * dt);
            else if (!absEligible || over < -AntilockParameters.SlipBand) w.AbsScale += AntilockParameters.ReapplyRate * dt;
            w.AbsScale = std::clamp(w.AbsScale, AntilockParameters.DumpFloor, 1.0f);
            w.AbsLatched = overBand;
            absScale = absEligible ? w.AbsScale : 1.0f;
        }
        else if (absEligible)
        {
            if (over > 0.0f) absScale = std::clamp(1.0f - AntilockParameters.Gain * over, AntilockParameters.DumpFloor, 1.0f);
        }
        w.AbsActive = absEligible && (absScale < 0.95f);

        const float commanded = absScale * pedal * Parameters.MaxBrakePressure;

        // Hydraulic line dynamics: slew toward the command at rise/fall rate.
        if (commanded > w.Pressure_Pa)
            w.Pressure_Pa = std::min(commanded, w.Pressure_Pa + Parameters.PressureRiseRate * dt);
        else
            w.Pressure_Pa = std::max(commanded, w.Pressure_Pa - Parameters.PressureFallRate * dt);

        // Torque from pressure × temperature-faded friction.
        w.FrictionCoeff = Parameters.FrictionCoefficient(w.Temperature_K);
        w.Torque_Nm     = Parameters.BrakeTorque(w.Pressure_Pa, w.Temperature_K);

        // Thermal refresh: generation from the actual dissipated power, convective cooling boosted by airspeed.
        w.HeatGen_W = Parameters.HeatGeneration(w.Torque_Nm, in.WheelOmega);
        w.Cooling_W = Parameters.CoolingRate(w.Temperature_K, in.Airspeed);
        const float dT = (w.HeatGen_W - w.Cooling_W) / (Parameters.DiskMass * Parameters.SpecificHeatCapacity);
        w.Temperature_K = std::max(Parameters.AmbientTemp, w.Temperature_K + dT * dt);

        out.BrakeTorque_Nm = w.Torque_Nm;
        out.Pressure_Pa    = w.Pressure_Pa;
        out.Temperature_K  = w.Temperature_K;
        out.FrictionCoeff  = w.FrictionCoeff;
        out.AbsActive      = w.AbsActive;
        return out;
    }

private:
    BrakeWheelOutput CoolOnly(BrakeWheelReading& w, const BrakeWheelInput& in, float dt, BrakeWheelOutput& out)
    {
        // Pedal released: bleed pressure off and cool the disk.
        w.Pressure_Pa = std::max(0.0f, w.Pressure_Pa - Parameters.PressureFallRate * dt);
        w.AbsActive = false;
        w.Torque_Nm = Parameters.BrakeTorque(w.Pressure_Pa, w.Temperature_K);
        w.HeatGen_W = Parameters.HeatGeneration(w.Torque_Nm, in.WheelOmega);
        w.Cooling_W = Parameters.CoolingRate(w.Temperature_K, in.Airspeed);
        const float dT = (w.HeatGen_W - w.Cooling_W) / (Parameters.DiskMass * Parameters.SpecificHeatCapacity);
        w.Temperature_K = std::max(Parameters.AmbientTemp, w.Temperature_K + dT * dt);
        out.BrakeTorque_Nm = w.Torque_Nm;
        out.Pressure_Pa    = w.Pressure_Pa;
        out.Temperature_K  = w.Temperature_K;
        out.FrictionCoeff  = Parameters.FrictionCoefficient(w.Temperature_K);
        out.AbsActive      = false;
        return out;
    }

    BrakingParameters Parameters{};
    AbsParameters     AntilockParameters{};
    std::vector<BrakeWheelReading> WheelRecords;
};

} // namespace Frontier::Vehicle
