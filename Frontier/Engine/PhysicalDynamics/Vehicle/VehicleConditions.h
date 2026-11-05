/*==========================================================================================================================================
                                                            VEHICLECONDITIONS.H
==========================================================================================================================================*/
// 🧩 The pieces Project Drive left out, added for Frontier (2026-11-04): tyre wear, traction control, fuel, engine coolant and oil,
//    and the supercharger's charge-air temperature. Every specification is off by default, so a configuration that leaves them
//    alone runs exactly as the source does. Parameter names and defaults follow GRIT's component sheets (TireSpecifications.h,
//    FuelSpecifications.h, CoolantSpecifications.h, EngineOilSpecifications.h, SuperchargerSpecifications.h); GRIT's solver is not
//    in its repository, so each law below is stated where it is evaluated (VehicleSolver.cpp, Drivetrain.cpp).

#pragma once

#include <algorithm>
#include <cmath>

namespace Frontier::Vehicle
{

// 📝 Tread wear from the sliding energy the tyre dissipates (GRIT FTireWearProperties): depth grows by BaseWearRate per megajoule,
//    scaled by (load / ReferenceLoad)^(LoadWearExponent − 1) and by ThermalWearMultiplier while the tread is over
//    OverheatTemperature; each worn millimetre takes GripLossPerMM of the grip, down to MinimumGripFactor.
struct TyreWearSpecification
{
    bool  Enabled               = false;
    float InitialTreadDepth_mm  = 8.0f;      // [mm]
    float BaseWearRate_mm_MJ    = 0.0012f;   // [mm/MJ] - sliding energy into worn depth
    float ThermalWearMultiplier = 2.5f;      // [-]     - while the tread is overheated
    float OverheatTemperature_K = 393.15f;   // [K]     - 120 °C
    float LoadWearExponent      = 1.3f;      // [-]     - wear power ∝ Load^n at the same slip speed
    float ReferenceLoad_N       = 3200.0f;   // [N]     - load the base rate is quoted at
    float GripLossPerMM         = 0.08f;     // [1/mm]
    float MinimumGripFactor     = 0.6f;      // [-]
};

// 📝 Traction control: when a driven wheel's slip ratio passes SlipTarget above MinimumSpeed, the throttle is cut in proportion to
//    the overshoot (Gain per unit slip), never below FloorThrottle of the driver's demand, and restored at RecoveryRate.
struct TractionControlSpecification
{
    bool  Enabled       = false;
    float SlipTarget    = 0.12f;   // [-]
    float Gain          = 6.0f;    // [-]   throttle share removed per unit slip over the target
    float MinimumSpeed  = 1.0f;    // [m/s] inactive below (launch from rest)
    float FloorThrottle = 0.15f;   // [-]   share of the demand always kept
    float RecoveryRate  = 3.0f;    // [1/s] how fast a cut throttle returns
};

// 📝 Fuel (GRIT FFuelSpecifications): flow = indicated power (brake + friction + pumping) × SpecificConsumption + IdleFlow;
//    an empty tank stops the engine's torque. Indicated efficiency is roughly constant over load, so part-load fuel rises
//    with the friction share (the Willans approximation). The fuel's mass rides in the chassis, so the host lowers the
//    body's mass as it burns.
struct FuelSpecification
{
    bool  Enabled                    = false;
    float CapacityLitres             = 68.0f;    // [L]
    float LoadLitres                 = 60.0f;    // [L]       at the start
    float DensityKgPerLitre          = 0.745f;   // [kg/L]    petrol
    float SpecificConsumption_gPerKWh = 270.0f;  // [g/kWh]   brake-specific fuel consumption
    float IdleFlow_gPerS             = 0.25f;    // [g/s]
    float LowerHeatingValue_MJkg     = 44.0f;    // [MJ/kg]   petrol (energy audits: fuel mass × LHV)
};

// 📝 Engine coolant and oil, two lumped heat capacities (GRIT CoolantSpecifications, EngineOilSpecifications): the share of fuel heat
//    that is neither work nor exhaust enters the coolant; the thermostat opens the radiator between ThermostatOpening and
//    ThermostatFull, whose conductance grows with airflow; the oil follows the coolant through OilCoolantConductance and takes the
//    friction heat. Cold oil is thicker: engine friction scales by 1 + ColdFrictionGain × (OilTarget − T) / (OilTarget − Ambient).
struct EngineFluidSpecification
{
    bool  Enabled                = false;
    float AmbientTemperature_K   = 293.15f;   // [K]
    float CoolantCapacity_JK     = 42000.0f;  // [J/K]  ~10 L of 50/50 coolant
    float OilCapacity_JK         = 14000.0f;  // [J/K]  ~7 L of oil
    float ThermalEfficiency      = 0.32f;     // [-]    brake work ÷ fuel heat
    float CoolantHeatShare       = 0.30f;     // [-]    fuel heat into the coolant
    float OilHeatShare           = 0.06f;     // [-]    fuel heat into the oil
    float ThermostatOpening_K    = 357.15f;   // [K]    84 °C
    float ThermostatFull_K       = 365.15f;   // [K]    92 °C
    float RadiatorConductance_WK = 900.0f;    // [W/K]  fully open, in still air
    float RadiatorAirflowGain    = 0.06f;     // [s/m]  conductance × (1 + gain × airspeed)
    float BlockLoss_WK           = 18.0f;     // [W/K]  the block's own loss to ambient
    float OilCoolantConductance  = 160.0f;    // [W/K]
    float OilTarget_K            = 381.15f;   // [K]    108 °C
    float ColdFrictionGain       = 0.6f;      // [-]
};

// 📝 Charge air (GRIT FSuperchargerStateVector::ChargerInletTemp): the compressor outlet after adiabatic compression at
//    CompressorEfficiency, T₂ = T₁ (1 + ((p₂/p₁)^((γ−1)/γ) − 1) / η), less what the intercooler removes, T = T₂ − ε (T₂ − T₁).
//    With DensityCorrection the boost's torque gain scales by ReferenceTemperature / T (warmer charge, thinner air).
struct ChargeAirSpecification
{
    bool  Enabled                  = false;
    float AmbientTemperature_K     = 293.15f;   // [K]
    float AmbientPressure_Bar      = 1.01325f;  // [bar]
    float IntercoolerEffectiveness = 0.75f;     // [-]
    float ReferenceTemperature_K   = 318.15f;   // [K] charge temperature the boost curves were measured at
    bool  DensityCorrection        = false;
};

// 📝 Intake manifold (mean-value filling dynamics): the throttle meters air into the plenum through a compressible orifice
//    (choked below the critical pressure ratio), the engine gulps it by speed-density, and the manifold density relative
//    to upstream sets the torque ratio — vacuum, not the pedal, meters the air at part throttle. Off by default; on, the
//    pumping loss follows the vacuum instead of (1 − throttle). Upstream is ambient on NA engines, ambient + boost with a
//    blow-through throttle on forced induction (density at ambient temperature; the charge-air correction stays separate).
struct IntakeManifoldSpecification
{
    bool  Enabled                 = false;
    float PlenumVolume_L          = 3.5f;    // [L]   plenum + runners
    float ThrottleBore_mm         = 65.0f;   // [mm]
    float DischargeCoefficient    = 0.75f;   // [-]
    float IdleThrottle            = 0.012f;  // [-]   bypass opening at zero pedal (the idle governor trims around it)
    float Displacement_L          = 3.8f;    // [L]   swept volume (4-stroke gulp)
    float VolumetricEfficiency    = 0.85f;   // [-]   at VolumetricPeakRPM
    float VolumetricPeakRPM       = 4500.0f; // [rpm]
    float VolumetricFalloff       = 0.15f;   // [-]   fractional loss at ±4000 rpm from the peak (quadratic)
};

// 📝 Traction motor (permanent-magnet synchronous): torque cap, power cap past the base speed, overspeed ceiling,
//    drivability slew, copper (T²) + iron (ω²) + friction (|ω|) losses, and recuperation opposing motion with the brake.
//    Single-speed reduction to the driven wheels; reverse is out of scope.
struct ElectricMotorParameters
{
    float PeakTorque_Nm          = 400.0f;    // [Nm]
    float PeakPower_W            = 150000.0f; // [W]
    float RegenerativeTorque_Nm  = 250.0f;    // [Nm]   recuperation cap (motor shaft)
    float MaxRPM                 = 16000.0f;  // [rpm]
    float ReductionRatio         = 8.5f;      // [-]    single-speed
    float DrivetrainEff          = 0.92f;     // [-]
    float RotorInertia           = 0.05f;     // [kg·m²]
    float TorqueRateLimit_NmPerS = 3000.0f;   // [Nm/s] drivability slew
    float CopperLoss             = 0.05f;     // [W/Nm²]  P = c·T²
    float IronLoss               = 0.012f;    // [W·s²/rad²] P = c·ω²
    float FrictionLoss           = 0.5f;      // [W·s/rad]  P = c·|ω|
};

// 📝 Traction battery: open-circuit voltage linear in charge, series resistance (the load sag), current limits that clip
//    motor torque, coulomb counting, and a lumped temperature that derates the motor when hot.
struct BatteryParameters
{
    float Capacity_kWh           = 75.0f;   // [kWh]
    float NominalVoltage_V       = 350.0f;  // [V]   capacity reference
    float OpenCircuitEmpty_V     = 320.0f;  // [V]
    float OpenCircuitFull_V      = 380.0f;  // [V]
    float InternalResistance_Ohm = 0.08f;   // [Ω]
    float MaxDischargeCurrent_A  = 600.0f;  // [A]
    float MaxChargeCurrent_A     = 300.0f;  // [A]
    float InitialCharge          = 0.6f;    // [-]   0..1 at Reset
    float ThermalCapacity_JK     = 80000.0f;// [J/K]
    float CoolingConductance_WK  = 25.0f;   // [W/K] to ambient
    float AmbientTemperature_K   = 293.15f; // [K]
    float DerateStart_K          = 318.15f; // [K]   full power below
    float DerateEnd_K            = 333.15f; // [K]   no power above
};

// 📝 Compressor outlet and intercooled charge temperatures for a gauge boost (bar) at one compressor efficiency.
inline float ResolveChargeTemperature(const ChargeAirSpecification& Charge, float BoostBar, float CompressorEfficiency) noexcept
{
    constexpr float γ        = 1.4f;
    const float     Ratio    = std::max(1.0f, (Charge.AmbientPressure_Bar + std::max(0.0f, BoostBar)) / Charge.AmbientPressure_Bar);
    const float     η        = std::clamp(CompressorEfficiency, 0.2f, 1.0f);
    const float     Outlet   = Charge.AmbientTemperature_K * (1.0f + (std::pow(Ratio, (γ - 1.0f) / γ) - 1.0f) / η);
    const float     Removed  = std::clamp(Charge.IntercoolerEffectiveness, 0.0f, 1.0f) * (Outlet - Charge.AmbientTemperature_K);
    return Outlet - Removed;
}

} // namespace Frontier::Vehicle
