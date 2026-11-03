/*==========================================================================================================================================
                                                              DRIVETRAIN.CPP
==========================================================================================================================================*/
// 🧩 Implementation of NA, 16-substep Turbo, Roots/TwinScrew/Centrifugal Supercharger, Coolant/Oil/Fuel circuits, AMT Gearbox, and Diffs.

#include "Engine/PhysicalDynamics/Vehicle/Drivetrain.h"

namespace Frontier::Vehicle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                SUPERCHARGER PRESETS
//------------------------------------------------------------------------------------------------------------------------

SuperchargerParameters SuperchargerParameters::DefaultRoots() noexcept
{
    SuperchargerParameters Params{};
    Params.Type                  = SuperchargerType::Roots;
    Params.DriveRatio            = 2.65f;
    Params.RotorInertiaKgm2      = 0.0110f;
    Params.MaxChargerRpm         = 18500.0f;
    Params.MaxBoostBaseBar       = 0.72f;
    Params.MaxBoostRaceBar       = 1.05f;
    Params.AdiabaticEfficiency   = 0.62f;
    Params.PeakParasiticPowerKw  = 44.0f;
    Params.BoostTorqueGainPerBar = 0.44f;
    return Params;
}

SuperchargerParameters SuperchargerParameters::DefaultTwinScrew() noexcept
{
    SuperchargerParameters Params{};
    Params.Type                  = SuperchargerType::TwinScrew;
    Params.DriveRatio            = 3.20f;
    Params.RotorInertiaKgm2      = 0.0085f;
    Params.MaxChargerRpm         = 23000.0f;
    Params.MaxBoostBaseBar       = 0.80f;
    Params.MaxBoostRaceBar       = 1.20f;
    Params.AdiabaticEfficiency   = 0.74f;
    Params.PeakParasiticPowerKw  = 36.0f;
    Params.BoostTorqueGainPerBar = 0.48f;
    return Params;
}

SuperchargerParameters SuperchargerParameters::DefaultCentrifugal() noexcept
{
    SuperchargerParameters Params{};
    Params.Type                  = SuperchargerType::Centrifugal;
    Params.DriveRatio            = 6.40f;
    Params.RotorInertiaKgm2      = 0.0032f;
    Params.MaxChargerRpm         = 52000.0f;
    Params.MaxBoostBaseBar       = 0.85f;
    Params.MaxBoostRaceBar       = 1.30f;
    Params.AdiabaticEfficiency   = 0.78f;
    Params.PeakParasiticPowerKw  = 31.0f;
    Params.BoostTorqueGainPerBar = 0.47f;
    return Params;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIFECYCLE & RESET
//------------------------------------------------------------------------------------------------------------------------

Drivetrain::Drivetrain() noexcept
{
    Reset(Engine.IdleRpm, true);
}

void Drivetrain::Reset(float InitialRpm, bool WarmFluids) noexcept
{
    EngineOmegaRad      = std::max(InitialRpm, 0.0f) * RpmToRadiansPerSecond;
    TurboOmegaRad       = 1200.0f;
    TurboBoostBar       = 0.0f;
    BovPosition         = 0.0f;
    WastegatePosition   = 0.0f;
    SuperBoostBar       = 0.0f;
    CoolantTempK        = WarmFluids ? ThermalFuel.OptimalCoolantTempK : ThermalFuel.AmbientTemperatureK;
    OilTempK            = WarmFluids ? 363.15f : ThermalFuel.AmbientTemperatureK;
    FuelMassRemainingKg = std::max(0.0f, ThermalFuel.InitialFuelLitres * ThermalFuel.FuelDensityKgPerLitre);
    CurrentGearIndex    = Trans.FirstForwardIdx;
    ShiftTimerSeconds   = 0.0f;
    ShiftCooldownSec    = 0.0f;
    LastOutputs         = DrivetrainOutputs{};
    LastOutputs.EngineRpm      = InitialRpm;
    LastOutputs.CoolantTempK   = CoolantTempK;
    LastOutputs.OilTempK       = OilTempK;
    LastOutputs.FuelMassKg     = FuelMassRemainingKg;
    LastOutputs.FuelRemainingL = FuelMassRemainingKg / std::max(ThermalFuel.FuelDensityKgPerLitre, 0.1f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              TURBOCHARGER & SUPERCHARGER
//------------------------------------------------------------------------------------------------------------------------

void Drivetrain::StepTurbocharger(float EngineRpm, float Throttle, float Dt) noexcept
{
    constexpr float CpExhaust    = 1150.0f;   // [J/(kg*K)] exhaust specific heat
    constexpr float GammaExhaust = 1.34f;     // [-] exhaust adiabatic exponent
    constexpr float CpAir        = 1005.0f;   // [J/(kg*K)] intake air specific heat
    constexpr float GammaAir     = 1.40f;     // [-] intake air adiabatic exponent

    const float RpmNorm          = ClampScalar(EngineRpm / std::max(Engine.RedlineRpm, 1000.0f), 0.05f, 1.15f);
    const float ExhaustFlowKgSec = (0.025f + 0.34f * RpmNorm) * (0.18f + 0.82f * Throttle);
    const float ExhaustTempK     = 620.0f + 480.0f * Throttle * (0.4f + 0.6f * RpmNorm);
    const float ExpansionRatio   = 1.05f + 1.85f * Throttle * RpmNorm * (1.0f - 0.45f * WastegatePosition);

    const float IsentropicTurbineWork = CpExhaust * ExhaustTempK *
                                        (1.0f - std::pow(1.0f / ExpansionRatio, (GammaExhaust - 1.0f) / GammaExhaust));
    const float InvInertia            = 1.0f / std::max(Turbo.RotorInertiaKgm2, 1.0e-6f);

    constexpr int SubSteps = 16;
    const float   SubDt    = Dt / static_cast<float>(SubSteps);
    for (int Sub = 0; Sub < SubSteps; ++Sub)
    {
        const float RotorRpm        = TurboOmegaRad * RadiansPerSecondToRpm;
        const float TargetBoost     = Turbo.BoostCurveBar.Sample(RotorRpm);
        const float CompRatio       = 1.0f + TargetBoost;
        const float IsentropicCompW = CpAir * ThermalFuel.AmbientTemperatureK *
                                      (std::pow(CompRatio, (GammaAir - 1.0f) / GammaAir) - 1.0f);

        const float TurbinePowerW   = ExhaustFlowKgSec * IsentropicTurbineWork * Turbo.TurbineEfficiency;
        const float CompPowerW      = (ExhaustFlowKgSec * IsentropicCompW) / std::max(Turbo.CompressorEfficiency, 0.25f);
        const float BearingDragNm   = 1.8e-5f * TurboOmegaRad + 2.2e-9f * TurboOmegaRad * TurboOmegaRad;
        const float NetPowerW       = (TurbinePowerW - CompPowerW) * Turbo.MechanicalEfficiency - BearingDragNm * TurboOmegaRad;
        const float NetTorqueNm     = NetPowerW / std::max(TurboOmegaRad, 25.0f);

        TurboOmegaRad = ClampScalar(TurboOmegaRad + NetTorqueNm * InvInertia * SubDt, 800.0f, 18500.0f);
    }

    const float RotorRpm      = TurboOmegaRad * RadiansPerSecondToRpm;
    const float PotentialBar  = Turbo.BoostCurveBar.Sample(RotorRpm);
    const float SpeedNorm     = ClampScalar(RotorRpm / 110000.0f, 0.1f, 1.2f);
    const float PlenumTau     = std::max(0.08f, 0.55f - 0.35f * SpeedNorm);
    const float PlenumAlpha   = ClampScalar(Dt / PlenumTau, 0.0f, 1.0f);
    TurboBoostBar            += (PotentialBar * (0.12f + 0.88f * Throttle) - TurboBoostBar) * PlenumAlpha;

    // Blow-off valve relief off-throttle.
    if (Throttle < 0.10f && TurboBoostBar > 0.12f)
    {
        BovPosition   = std::min(1.0f, BovPosition + Dt * 6.0f);
        TurboBoostBar = std::max(0.0f, TurboBoostBar - Turbo.BovReleaseRateBarPerS * BovPosition * Dt);
    }
    else
    {
        BovPosition = std::max(0.0f, BovPosition - Dt * 8.0f);
    }

    // Wastegate overboost ceiling.
    const float MaxBoost = RaceTune ? Turbo.WastegateMaxBoostBar * 1.20f : Turbo.WastegateMaxBoostBar;
    if (TurboBoostBar > MaxBoost)
    {
        WastegatePosition = ClampScalar((TurboBoostBar - MaxBoost) * 2.5f, 0.0f, 1.0f);
        TurboBoostBar     = MaxBoost;
    }
    else
    {
        WastegatePosition = std::max(0.0f, WastegatePosition - Dt * 5.0f);
    }
}

float Drivetrain::StepSupercharger(float EngineRpm, float Throttle, float Dt, float& OutParasiticTorqueNm) noexcept
{
    const float ChargerRpm  = Super.ChargerRpm(EngineRpm);
    const float SpeedRatio  = ClampScalar(ChargerRpm / std::max(Super.MaxChargerRpm, 1000.0f), 0.0f, 1.15f);
    const float MaxBoostCap = RaceTune ? Super.MaxBoostRaceBar : Super.MaxBoostBaseBar;

    // 📝 Positive-displacement vs Centrifugal delivery curve (GRIT SuperchargerSpecifications.h).
    float NormalizedDelivery = 0.0f;
    switch (Super.Type)
    {
        case SuperchargerType::Roots:
            NormalizedDelivery = ClampScalar(1.22f * SpeedRatio - 0.12f * SpeedRatio * SpeedRatio, 0.0f, 1.0f);
            break;
        case SuperchargerType::TwinScrew:
            NormalizedDelivery = ClampScalar(0.18f + 0.92f * SpeedRatio, 0.0f, 1.0f);
            break;
        case SuperchargerType::Centrifugal:
            NormalizedDelivery = ClampScalar(SpeedRatio * SpeedRatio, 0.0f, 1.0f);
            break;
    }

    // Throttle-actuated bypass recirculation valve bleeds boost off-throttle and slashes parasitic work.
    const float BypassClosedFraction = ClampScalar((Throttle - 0.05f) / 0.95f, 0.0f, 1.0f);
    const float TargetBoostBar       = MaxBoostCap * NormalizedDelivery * BypassClosedFraction;
    const float PlenumAlpha          = ClampScalar(Dt * Super.ResponseRatePerSecond, 0.0f, 1.0f);
    SuperBoostBar                   += (TargetBoostBar - SuperBoostBar) * PlenumAlpha;
    SuperBoostBar                    = ClampScalar(SuperBoostBar, 0.0f, MaxBoostCap);

    // Parasitic crank load = P_parasitic / omega_engine, modulated by bypass valve & adiabatic efficiency.
    const float EngineOmega          = std::max(EngineRpm * RpmToRadiansPerSecond, 40.0f);
    const float LoadFraction         = LerpScalar(Super.BypassIdleDragRatio, 1.0f, BypassClosedFraction);
    const float EfficiencyScale      = 0.74f / std::max(Super.AdiabaticEfficiency * Super.MechanicalEfficiency, 0.30f);
    const float ParasiticWatts       = Super.PeakParasiticPowerKw * 1000.0f * (SpeedRatio * SpeedRatio) * LoadFraction * EfficiencyScale;
    OutParasiticTorqueNm             = ClampScalar(ParasiticWatts / EngineOmega, 0.0f, 165.0f);

    // Charge-air thermal efficiency trim at high boost.
    const float ChargeHeatTrim       = 1.0f - 0.04f * std::max(0.0f, SuperBoostBar - 0.7f);
    return 1.0f + SuperBoostBar * Super.BoostTorqueGainPerBar * ChargeHeatTrim;
}

//------------------------------------------------------------------------------------------------------------------------
//                                    GRIT COOLANT, ENGINE OIL & FUEL CONSUMPTION PASS
//------------------------------------------------------------------------------------------------------------------------

void Drivetrain::StepThermalAndFuel(float MechanicalPowerW, float VehicleSpeedMps, float Dt) noexcept
{
    const float PositivePowerW = std::max(0.0f, MechanicalPowerW);
    const float Efficiency     = ClampScalar(Engine.ThermalEfficiency, 0.15f, 0.50f);

    // 1. Fuel mass flow & tank consumption (GRIT Fuel Consumption Pass).
    float FuelFlowKgSec = 0.0f;
    if (PositivePowerW > 0.0f && ThermalFuel.FuelEnergyDensityJkg > 1.0f)
    {
        FuelFlowKgSec = (PositivePowerW / Efficiency) / ThermalFuel.FuelEnergyDensityJkg;
    }
    else
    {
        // Idle maintenance injection (~0.28 g/s).
        FuelFlowKgSec = 0.00028f;
    }
    FuelMassRemainingKg          = std::max(0.0f, FuelMassRemainingKg - FuelFlowKgSec * Dt);
    LastOutputs.FuelFlowKgPerSec = FuelFlowKgSec;
    LastOutputs.FuelMassKg       = FuelMassRemainingKg;
    LastOutputs.FuelRemainingL   = FuelMassRemainingKg / std::max(ThermalFuel.FuelDensityKgPerLitre, 0.1f);

    // 2. Engine coolant heat rejection & thermostat-regulated speed-assisted radiator cooling (GRIT Thermal Pass).
    const float WasteHeatW        = PositivePowerW * ((1.0f - Efficiency) / Efficiency) * 0.55f + 650.0f;
    const float ThermostatOpen    = ClampScalar((CoolantTempK - ThermalFuel.ThermostatOpenTempK) / 7.0f, 0.08f, 1.0f);
    const float SpeedAirflowGain  = 1.0f + std::fabs(VehicleSpeedMps) * 0.12f;
    const float RadiatorCoolW     = ThermalFuel.RadiatorCoolingWK * ThermostatOpen * SpeedAirflowGain *
                                (CoolantTempK - ThermalFuel.AmbientTemperatureK);
    CoolantTempK += ((WasteHeatW - RadiatorCoolW) / std::max(ThermalFuel.EngineThermalMassJK, 5000.0f)) * Dt;
    CoolantTempK  = ClampScalar(CoolantTempK, ThermalFuel.AmbientTemperatureK, ThermalFuel.MaxCoolantTempK);

    // 3. Engine oil sump thermal balance & kinematic viscosity (GRIT Engine Oil Pass).
    const float BlockToOilW    = (CoolantTempK - OilTempK) * 42.0f + PositivePowerW * 0.035f;
    const float SumpCoolingW   = (OilTempK - ThermalFuel.AmbientTemperatureK) * (10.0f + 5.0f * std::fabs(VehicleSpeedMps));
    const float OilMassKg      = (ThermalFuel.OilSumpCapacityLitres * 0.001f) * ThermalFuel.OilDensityKgPerM3;
    const float OilCapacityJK  = std::max(OilMassKg * ThermalFuel.OilSpecificHeatJkgK, 500.0f);
    OilTempK                  += ((BlockToOilW - SumpCoolingW) / OilCapacityJK) * Dt;
    OilTempK                   = ClampScalar(OilTempK, ThermalFuel.AmbientTemperatureK, ThermalFuel.OilDamageTempK + 20.0f);

    // Log-linear Walther/ASTM interpolation of 5W-40 viscosity between 40 degC (313.15 K) and 100 degC (373.15 K).
    const float TempBlend         = ClampScalar((OilTempK - 313.15f) / 60.0f, -0.4f, 1.5f);
    const float LogVisc           = LerpScalar(std::log(ThermalFuel.OilViscosity40C_cSt), std::log(ThermalFuel.OilViscosity100C_cSt), TempBlend);
    LastOutputs.OilViscosityCst   = ClampScalar(std::exp(LogVisc), 4.5f, 260.0f);
    LastOutputs.CoolantTempK      = CoolantTempK;
    LastOutputs.OilTempK          = OilTempK;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 DIFFERENTIAL SPLIT
//------------------------------------------------------------------------------------------------------------------------

void Drivetrain::SplitAxleDifferential(float AxleTorqueNm, float LeftWheelRpm, float RightWheelRpm,
                                       float& OutLeftNm, float& OutRightNm) const noexcept
{
    const float HalfTorque = 0.5f * AxleTorqueNm;
    const float SpeedDiff  = LeftWheelRpm - RightWheelRpm;

    switch (Diff.Mode)
    {
        case DifferentialMode::Open:
        default:
            OutLeftNm  = HalfTorque;
            OutRightNm = HalfTorque;
            break;

        case DifferentialMode::Locked:
        {
            const float Transfer = ClampScalar(SpeedDiff / 80.0f, -1.0f, 1.0f) * std::fabs(HalfTorque);
            OutLeftNm            = HalfTorque - Transfer;
            OutRightNm           = HalfTorque + Transfer;
            break;
        }

        case DifferentialMode::LimitedSlip:
        {
            const float Bias = ClampScalar(SpeedDiff / 180.0f, -1.0f, 1.0f) * Diff.LockingFactor * std::fabs(HalfTorque) +
                               Diff.PreloadNm * ClampScalar(SpeedDiff / 25.0f, -1.0f, 1.0f);
            OutLeftNm        = HalfTorque - Bias;
            OutRightNm       = HalfTorque + Bias;
            break;
        }

        case DifferentialMode::TorqueVectoring:
        {
            const float Cmd = ClampScalar(Diff.VectoringBias, -1.0f, 1.0f);
            OutLeftNm       = AxleTorqueNm * (0.5f - 0.5f * Cmd);
            OutRightNm      = AxleTorqueNm * (0.5f + 0.5f * Cmd);
            break;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  INTEGRATION STEP
//------------------------------------------------------------------------------------------------------------------------

DrivetrainOutputs Drivetrain::Step(const DrivetrainInputs& Inputs) noexcept
{
    const float Dt       = ClampScalar(Inputs.DeltaSeconds, 1.0e-5f, 1.0f / 30.0f);
    const float Throttle = ClampScalar(Inputs.Throttle, 0.0f, 1.0f);
    const float StartRpm = EngineOmegaRad * RadiansPerSecondToRpm;

    // 1. Gear selection & automatic AMT sequential shift logic.
    if (ShiftTimerSeconds > 0.0f) ShiftTimerSeconds = std::max(0.0f, ShiftTimerSeconds - Dt);
    if (ShiftCooldownSec > 0.0f) ShiftCooldownSec = std::max(0.0f, ShiftCooldownSec - Dt);

    if (Inputs.RequestedGear >= 0 && Inputs.RequestedGear < static_cast<int>(Trans.GearRatios.size()) &&
        Inputs.RequestedGear != CurrentGearIndex)
    {
        CurrentGearIndex  = Inputs.RequestedGear;
        ShiftTimerSeconds = Trans.ShiftDurationS;
        ShiftCooldownSec  = 0.25f;
    }
    else if (Trans.AutomaticShift && CurrentGearIndex >= Trans.FirstForwardIdx && ShiftCooldownSec <= 0.0f)
    {
        const int MaxGearIdx = static_cast<int>(Trans.GearRatios.size()) - 1;
        if (StartRpm >= Trans.UpshiftRpm && CurrentGearIndex < MaxGearIdx && Throttle > 0.25f)
        {
            ++CurrentGearIndex;
            ShiftTimerSeconds = Trans.ShiftDurationS;
            ShiftCooldownSec  = 0.30f;
        }
        else if (StartRpm <= Trans.DownshiftRpm && CurrentGearIndex > Trans.FirstForwardIdx)
        {
            --CurrentGearIndex;
            ShiftTimerSeconds = Trans.ShiftDurationS * 0.75f;
            ShiftCooldownSec  = 0.28f;
        }
    }

    const bool  ShiftingNow = ShiftTimerSeconds > 0.0f;
    const float EffThrottle = ShiftingNow ? 0.0f : Throttle;

    // 2. Forced induction pass (NA / Turbo / Supercharger).
    float BoostMultiplier = 1.0f;
    float ParasiticDragNm = 0.0f;
    float ReportBoostBar  = 0.0f;
    float ReportForcedRpm = 0.0f;

    if (Induction == InductionType::Turbocharged)
    {
        StepTurbocharger(StartRpm, EffThrottle, Dt);
        BoostMultiplier = Turbo.TorqueMultiplierCurve.Sample(TurboBoostBar);
        ReportBoostBar  = TurboBoostBar;
        ReportForcedRpm = TurboOmegaRad * RadiansPerSecondToRpm;
    }
    else if (Induction == InductionType::Supercharged)
    {
        BoostMultiplier = StepSupercharger(StartRpm, EffThrottle, Dt, ParasiticDragNm);
        ReportBoostBar  = SuperBoostBar;
        ReportForcedRpm = Super.ChargerRpm(StartRpm);
    }

    // 3. Driven axle speed & clutch slip coupling.
    const float GearRatio   = Trans.GearRatios[static_cast<size_t>(CurrentGearIndex)];
    const bool  NeutralGear = (CurrentGearIndex == Trans.NeutralIndex) || std::fabs(GearRatio) < 1.0e-4f;
    const float TotalRatio  = GearRatio * Trans.FinalDriveRatio;

    const float FrontWheelRpm = 0.5f * (Inputs.WheelOmegaRadPerSec[0] + Inputs.WheelOmegaRadPerSec[1]) * RadiansPerSecondToRpm;
    const float RearWheelRpm  = 0.5f * (Inputs.WheelOmegaRadPerSec[2] + Inputs.WheelOmegaRadPerSec[3]) * RadiansPerSecondToRpm;
    float       DrivenRpm     = RearWheelRpm;
    if (Diff.Layout == DriveLayout::FrontWheelDrive) DrivenRpm = FrontWheelRpm;
    else if (Diff.Layout == DriveLayout::AllWheelDrive)
    {
        DrivenRpm = LerpScalar(RearWheelRpm, FrontWheelRpm, Diff.FrontTorqueShare);
    }

    const float GearboxInputRpm = NeutralGear ? StartRpm : (DrivenRpm * TotalRatio);
    const float SlipRpm         = StartRpm - GearboxInputRpm;

    // Automatic launch clutch modulation keeps engine from stalling near idle while smoothly locking up above 1,800 RPM.
    float Engagement = 1.0f;
    if (NeutralGear || ShiftingNow)
    {
        Engagement = 0.0f;
    }
    else if (StartRpm < Engine.IdleRpm + 950.0f)
    {
        const float RpmRise = ClampScalar((StartRpm - Engine.IdleRpm) / 950.0f, 0.0f, 1.0f);
        Engagement          = ClampScalar(0.15f + 0.85f * std::max(RpmRise, EffThrottle * 0.85f), 0.0f, 1.0f);
    }

    const float ClutchCap   = Clutch.MaxTorqueCapacityNm * Engagement;
    const float ClutchSlope = ClutchCap / std::max(Clutch.BreakawaySlipRpm, 5.0f);
    const float ClutchNm    = NeutralGear ? 0.0f : ClampScalar(SlipRpm * ClutchSlope, -ClutchCap, ClutchCap);

    // 4. Engine combustion torque + idle governor + cold/hot oil viscous friction + overheat limp + crank-reflected SC inertia.
    const float BaseCurveNm    = Engine.TorqueCurveNm.Sample(StartRpm);
    const float IdleDeficit    = std::max(0.0f, Engine.IdleRpm - StartRpm);
    const float IdleGovernor   = ClampScalar(IdleDeficit / 260.0f, 0.0f, 0.35f);
    const float FuelStarved    = (ThermalFuel.EnforceFuelStarvation && FuelMassRemainingKg <= 1.0e-5f) ? 0.0f : 1.0f;
    const float LimpDerate     = CoolantTempK > ThermalFuel.OverheatLimpTempK
                                     ? ClampScalar(1.0f - (CoolantTempK - ThermalFuel.OverheatLimpTempK) / 30.0f, 0.45f, 1.0f)
                                     : 1.0f;
    const float RevCut         = StartRpm >= Engine.HardCutRpm
                                     ? 0.0f
                                     : (StartRpm > Engine.RedlineRpm
                                            ? ClampScalar(1.0f - (StartRpm - Engine.RedlineRpm) / (Engine.HardCutRpm - Engine.RedlineRpm), 0.0f, 1.0f)
                                            : 1.0f);

    const float ColdOilMult    = OilTempK < 353.15f ? (1.0f + 0.0045f * (353.15f - OilTempK)) : 1.0f;
    const float HotOilMult     = OilTempK > 403.15f ? (1.0f + 0.0080f * (OilTempK - 403.15f)) : 1.0f;
    const float FrictionMult   = ColdOilMult * HotOilMult;

    const float Demand         = std::max(EffThrottle, IdleGovernor) * FuelStarved * LimpDerate * RevCut;
    const float GrossTorqueNm  = BaseCurveNm * BoostMultiplier * Demand;
    const float EngineBrakeNm  = (Engine.StaticFrictionNm + Engine.ViscousFrictionNms * EngineOmegaRad) *
                                FrictionMult * (1.0f - 0.72f * Demand);
    const float NetFlywheelNm  = GrossTorqueNm - EngineBrakeNm - ParasiticDragNm;

    float EffectiveCrankInertia = Engine.FlywheelInertiaKgm2;
    if (Induction == InductionType::Supercharged)
    {
        EffectiveCrankInertia += Super.RotorInertiaKgm2 * Super.DriveRatio * Super.DriveRatio;
    }

    EngineOmegaRad += ((NetFlywheelNm - ClutchNm) / std::max(EffectiveCrankInertia, 0.05f)) * Dt;
    EngineOmegaRad  = ClampScalar(EngineOmegaRad, Engine.IdleRpm * 0.65f * RpmToRadiansPerSecond, (Engine.HardCutRpm + 150.0f) * RpmToRadiansPerSecond);

    // 5. Thermal & fuel pass.
    StepThermalAndFuel(std::max(0.0f, NetFlywheelNm) * EngineOmegaRad, Inputs.VehicleSpeedMps, Dt);

    // 6. Distribute clutch torque through gearbox, final drive, and axle differentials.
    DrivetrainOutputs Out     = LastOutputs;
    Out.WheelDriveTorqueNm    = {0.0f, 0.0f, 0.0f, 0.0f};
    const float PropshaftNm   = NeutralGear ? 0.0f : (ClutchNm * TotalRatio * Trans.Efficiency);

    if (!NeutralGear)
    {
        const float FLRpm = Inputs.WheelOmegaRadPerSec[0] * RadiansPerSecondToRpm;
        const float FRRpm = Inputs.WheelOmegaRadPerSec[1] * RadiansPerSecondToRpm;
        const float RLRpm = Inputs.WheelOmegaRadPerSec[2] * RadiansPerSecondToRpm;
        const float RRRpm = Inputs.WheelOmegaRadPerSec[3] * RadiansPerSecondToRpm;

        if (Diff.Layout == DriveLayout::RearWheelDrive)
        {
            SplitAxleDifferential(PropshaftNm, RLRpm, RRRpm, Out.WheelDriveTorqueNm[2], Out.WheelDriveTorqueNm[3]);
        }
        else if (Diff.Layout == DriveLayout::FrontWheelDrive)
        {
            SplitAxleDifferential(PropshaftNm, FLRpm, FRRpm, Out.WheelDriveTorqueNm[0], Out.WheelDriveTorqueNm[1]);
        }
        else
        {
            const float FrontShare = ClampScalar(Diff.FrontTorqueShare, 0.0f, 1.0f);
            SplitAxleDifferential(PropshaftNm * FrontShare, FLRpm, FRRpm, Out.WheelDriveTorqueNm[0], Out.WheelDriveTorqueNm[1]);
            SplitAxleDifferential(PropshaftNm * (1.0f - FrontShare), RLRpm, RRRpm, Out.WheelDriveTorqueNm[2], Out.WheelDriveTorqueNm[3]);
        }
    }

    Out.EngineRpm          = EngineOmegaRad * RadiansPerSecondToRpm;
    Out.EngineTorqueNm     = NetFlywheelNm;
    Out.BoostPressureBar   = ReportBoostBar;
    Out.BoostMultiplier    = BoostMultiplier;
    Out.ForcedInductionRpm = ReportForcedRpm;
    Out.ParasiticDragNm    = ParasiticDragNm;
    Out.ActiveGearIndex    = CurrentGearIndex;
    Out.Shifting           = ShiftingNow;
    Out.ClutchLocked       = !NeutralGear && !ShiftingNow && (std::fabs(SlipRpm) < Clutch.LockupSlipRpm);

    LastOutputs = Out;
    return Out;
}

} // namespace Frontier::Vehicle
