/*==========================================================================================================================================
                                                            THERMALINTEGRATOR.H
==========================================================================================================================================*/
// 🧩 Three-node tyre thermodynamic + ideal-gas pressure + compound wear model (tread surface, carcass bulk, enclosed cavity gas).

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/VehicleMath.h"

namespace Frontier::Vehicle
{

//------------------------------------------------------------------------------------------------------------------------
//                                             TYRE THERMAL & PRESSURE PARAMETERS
//------------------------------------------------------------------------------------------------------------------------

struct TyreThermalParameters
{
    bool  Enabled                     = true;        // [-] active by default on every corner
    float AmbientTemperatureK         = 293.15f;     // [K] 20 degC ambient air
    float TrackTemperatureK           = 298.15f;     // [K] 25 degC asphalt surface
    float ColdGaugePressurePa         = 220000.0f;   // [Pa] 2.20 bar cold inflation gauge pressure
    float ReferenceVolumeCubicMetres  = 0.042f;      // [m^3] unloaded enclosed pneumatic cavity volume

    float TreadMassKg                 = 0.58f;       // [kg] outer 1.2 mm flash tread skin thermal mass
    float CarcassMassKg               = 4.80f;       // [kg] belt + plies + sidewall bulk mass
    float RubberSpecificHeat          = 1850.0f;     // [J/(kg*K)] vulcanized rubber specific heat
    float GasConstantVolumeHeat       = 718.0f;      // [J/(kg*K)] dry air / N2 constant-volume c_v

    float SlipHeatRubberShare         = 0.72f;       // [-] fraction of slip friction power entering tread
    float RollingResistanceCoeff      = 0.0115f;     // [-] carcass hysteresis rolling loss coefficient
    float FlexionDampingHeatFactor    = 950.0f;      // [N*s/m] sidewall radial flexion heating factor

    float TreadToCarcassConductance   = 82.0f;       // [W/K] tread <-> carcass thermal conductance
    float CarcassToGasConductance     = 26.0f;       // [W/K] inner liner <-> inflation gas conductance
    float RoadContactConductance      = 42.0f;       // [W/K] contact patch <-> asphalt conductance at nominal load
    float ConvectionBaseConductance   = 18.0f;       // [W/K] natural convection in still air
    float ConvectionSpeedCoefficient  = 4.2f;        // [W/(K*(m/s)^0.8)] forced airflow coefficient
    float RadiativeAreaSquareMetres   = 0.58f;       // [m^2] exposed tyre surface area
    float Emissivity                  = 0.92f;       // [-] carbon-black rubber infrared emissivity

    float OptimalTemperatureK         = 363.15f;     // [K] 90 degC compound peak-grip centre
    float PeakWindowHalfWidthK        = 15.0f;       // [K] +-15 K optimal plateau half-width
    float ColdGripFloor               = 0.80f;       // [-] minimum cold-tyre grip multiplier
    float OverheatGripFloor           = 0.72f;       // [-] minimum blistered/overheated grip multiplier
    float OverheatOnsetK              = 388.15f;     // [K] 115 degC accelerated wear & blister threshold
    float IgnitionTemperatureK        = 523.15f;     // [K] 250 degC rubber smoke/ignition threshold
    float WearPerMegajoule            = 0.0085f;     // [1/MJ] tread wear fraction per MJ of slip energy
};

struct TyreThermalReading
{
    float SurfaceTemperatureK  = 293.15f;     // [K] outer tread surface temperature
    float CarcassTemperatureK  = 293.15f;     // [K] belt/sidewall carcass bulk temperature
    float GasTemperatureK      = 293.15f;     // [K] enclosed inflation gas temperature
    float AbsolutePressurePa   = 321325.0f;   // [Pa] total cavity gas pressure (P = m*R*T/V)
    float GaugePressurePa      = 220000.0f;   // [Pa] P_abs - P_atm
    float EnclosedVolumeM3     = 0.042f;      // [m^3] live cavity volume from soft-body carcass
    float GasMassKg            = 0.0462f;     // [kg] sealed inflation gas mass
    float GripMultiplier       = 0.88f;       // [-] combined temperature & wear grip factor
    float WearFraction         = 0.0f;        // [-] [0..1] accumulated tread wear (1 = corded)
    bool  Punctured            = false;       // [-] true after puncture / rim-pinch / blowout
    bool  Ignited              = false;       // [-] true once tread exceeds IgnitionTemperatureK
    bool  Initialized          = false;       // [-] true once Reset() seeded sealed gas mass
};

struct TyreThermalStepInputs
{
    float SlipPowerWatts       = 0.0f;     // [W] frictional slip dissipation from TyreSlipSolver
    float NormalLoadNewtons    = 0.0f;     // [N] tyre vertical load Fz
    float RollingSpeedMps      = 0.0f;     // [m/s] hub/airflow speed
    float DeflectionRateMps    = 0.0f;     // [m/s] carcass radial compression rate
    float LiveVolumeCubicM     = 0.042f;   // [m^3] live deformed carcass cavity volume
    float DeltaSeconds         = 1.0f / 240.0f;   // [s] timestep
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 TYRE THERMAL INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class TyreThermalIntegrator
{
  public:
    static void Reset(const TyreThermalParameters& Params, TyreThermalReading& Reading) noexcept
    {
        Reading.SurfaceTemperatureK = Params.AmbientTemperatureK;
        Reading.CarcassTemperatureK = Params.AmbientTemperatureK;
        Reading.GasTemperatureK     = Params.AmbientTemperatureK;
        Reading.EnclosedVolumeM3    = std::max(Params.ReferenceVolumeCubicMetres, 1.0e-4f);
        Reading.GaugePressurePa     = std::max(0.0f, Params.ColdGaugePressurePa);
        Reading.AbsolutePressurePa  = AtmosphericPressurePa + Reading.GaugePressurePa;
        Reading.GasMassKg           = (Reading.AbsolutePressurePa * Reading.EnclosedVolumeM3) /
                            std::max(DryAirGasConstant * Reading.GasTemperatureK, 1.0f);
        Reading.WearFraction        = 0.0f;
        Reading.Punctured           = false;
        Reading.Ignited             = false;
        Reading.Initialized         = true;
        Reading.GripMultiplier      = EvaluateGripMultiplier(Params, Reading);
    }

    [[nodiscard]] static float EvaluateGripMultiplier(const TyreThermalParameters& Params, const TyreThermalReading& Reading) noexcept
    {
        if (!Params.Enabled) return 1.0f;
        if (Reading.Punctured) return 0.35f;

        // 📝 Effective compound temperature blends 65% surface tread + 35% carcass bulk.
        const float EffectiveTemp = 0.65f * Reading.SurfaceTemperatureK + 0.35f * Reading.CarcassTemperatureK;
        const float DeltaFromOpt  = EffectiveTemp - Params.OptimalTemperatureK;
        float       ThermalFactor = 1.0f;

        if (DeltaFromOpt < -Params.PeakWindowHalfWidthK)
        {
            const float ColdExcess = (-DeltaFromOpt - Params.PeakWindowHalfWidthK) / 55.0f;
            ThermalFactor          = LerpScalar(1.0f, Params.ColdGripFloor, ClampScalar(ColdExcess * ColdExcess, 0.0f, 1.0f));
        }
        else if (DeltaFromOpt > Params.PeakWindowHalfWidthK)
        {
            const float HotExcess = (DeltaFromOpt - Params.PeakWindowHalfWidthK) / 60.0f;
            ThermalFactor         = LerpScalar(1.0f, Params.OverheatGripFloor, ClampScalar(HotExcess * HotExcess, 0.0f, 1.0f));
        }

        // 📝 Inflation pressure deviation slightly trims contact-patch uniformity away from optimal hot pressure (~2.45 bar).
        const float OptimalHotPa   = std::max(Params.ColdGaugePressurePa * 1.10f, 50000.0f);
        const float PressureError  = std::fabs(Reading.GaugePressurePa - OptimalHotPa) / OptimalHotPa;
        const float PressureFactor = ClampScalar(1.0f - 0.14f * PressureError * PressureError, 0.72f, 1.02f);

        // 📝 Wear factor retains 100% until 40% worn, then smoothly decays to 78% at full wear.
        const float WearFactor = 1.0f - 0.22f * ClampScalar((Reading.WearFraction - 0.25f) / 0.75f, 0.0f, 1.0f);
        return ThermalFactor * PressureFactor * WearFactor;
    }

    static void Step(const TyreThermalParameters& Params, const TyreThermalStepInputs& Inputs, TyreThermalReading& Reading) noexcept
    {
        if (!Reading.Initialized) Reset(Params, Reading);
        if (!Params.Enabled) return;

        const float Dt           = std::max(Inputs.DeltaSeconds, 1.0e-6f);
        const float PrevVolume   = Reading.EnclosedVolumeM3;
        const float NextVolume   = std::clamp(
            Inputs.LiveVolumeCubicM > 1.0e-4f ? Inputs.LiveVolumeCubicM : Params.ReferenceVolumeCubicMetres,
            0.35f * Params.ReferenceVolumeCubicMetres,
            1.60f * Params.ReferenceVolumeCubicMetres);
        const float VolumeChange = NextVolume - PrevVolume;
        Reading.EnclosedVolumeM3 = NextVolume;

        if (Reading.Punctured)
        {
            Reading.GasMassKg *= std::exp(-Dt * 3.5f);
        }

        // 1. Heat sources: slip friction into tread, hysteresis + sidewall flexion into carcass, -P*dV work into gas.
        const float TreadFrictionW   = std::max(0.0f, Inputs.SlipPowerWatts) * Params.SlipHeatRubberShare;
        const float SpeedAbs         = std::fabs(Inputs.RollingSpeedMps);
        const float HysteresisW      = Params.RollingResistanceCoeff * std::max(0.0f, Inputs.NormalLoadNewtons) * SpeedAbs;
        const float FlexionW         = Params.FlexionDampingHeatFactor * Inputs.DeflectionRateMps * Inputs.DeflectionRateMps;
        const float CarcassInternalW = HysteresisW + FlexionW;
        const float GasCompressionW  = -Reading.AbsolutePressurePa * (VolumeChange / Dt);

        // 2. Heat exchange between nodes, asphalt contact patch, convective airflow, and radiation.
        const float ContactFraction  = ClampScalar(Inputs.NormalLoadNewtons / 4500.0f, 0.0f, 1.5f);
        const float ConductTreadCarc = Params.TreadToCarcassConductance * (Reading.SurfaceTemperatureK - Reading.CarcassTemperatureK);
        const float ConductCarcGas   = Params.CarcassToGasConductance * (Reading.CarcassTemperatureK - Reading.GasTemperatureK);
        const float ConductTreadRoad = Params.RoadContactConductance * ContactFraction * (Reading.SurfaceTemperatureK - Params.TrackTemperatureK);
        const float ConvectiveCoeff  = Params.ConvectionBaseConductance + Params.ConvectionSpeedCoefficient * std::pow(SpeedAbs + 0.1f, 0.8f);
        const float ConvectTreadAir  = ConvectiveCoeff * (Reading.SurfaceTemperatureK - Params.AmbientTemperatureK);

        const float SurfT2           = Reading.SurfaceTemperatureK * Reading.SurfaceTemperatureK;
        const float AmbT2            = Params.AmbientTemperatureK * Params.AmbientTemperatureK;
        const float RadiativeW       = StefanBoltzmannConstant * Params.Emissivity * Params.RadiativeAreaSquareMetres *
                                 (SurfT2 * SurfT2 - AmbT2 * AmbT2);

        const float TreadCapacity    = std::max(Params.TreadMassKg * Params.RubberSpecificHeat, 100.0f);
        const float CarcassCapacity  = std::max(Params.CarcassMassKg * Params.RubberSpecificHeat, 200.0f);
        const float GasCapacity      = std::max(Reading.GasMassKg * Params.GasConstantVolumeHeat, 5.0f);

        Reading.SurfaceTemperatureK += ((TreadFrictionW - ConductTreadCarc - ConductTreadRoad - ConvectTreadAir - RadiativeW) / TreadCapacity) * Dt;
        Reading.CarcassTemperatureK += ((CarcassInternalW + ConductTreadCarc - ConductCarcGas - 0.18f * ConvectiveCoeff *
                                         (Reading.CarcassTemperatureK - Params.AmbientTemperatureK)) / CarcassCapacity) * Dt;
        Reading.GasTemperatureK     += ((ConductCarcGas + GasCompressionW) / GasCapacity) * Dt;

        Reading.SurfaceTemperatureK = ClampScalar(Reading.SurfaceTemperatureK, 220.0f, 750.0f);
        Reading.CarcassTemperatureK = ClampScalar(Reading.CarcassTemperatureK, 220.0f, 650.0f);
        Reading.GasTemperatureK     = ClampScalar(Reading.GasTemperatureK, 220.0f, 600.0f);

        // 3. Ideal gas law: P_abs = m_gas * R * T_gas / V_live.
        Reading.AbsolutePressurePa = std::max(
            AtmosphericPressurePa,
            (Reading.GasMassKg * DryAirGasConstant * Reading.GasTemperatureK) / Reading.EnclosedVolumeM3);
        Reading.GaugePressurePa = Reading.Punctured ? 0.0f : std::max(0.0f, Reading.AbsolutePressurePa - AtmosphericPressurePa);

        // 4. Overheat-accelerated tread wear & ignition threshold.
        const float OverheatExcess  = std::max(0.0f, Reading.SurfaceTemperatureK - Params.OverheatOnsetK);
        const float OverheatAccel   = std::exp(std::min(0.05f * OverheatExcess, 3.0f));
        const float SlipMegajoules  = (std::max(0.0f, Inputs.SlipPowerWatts) * Dt) * 1.0e-6f;
        Reading.WearFraction        = ClampScalar(Reading.WearFraction + SlipMegajoules * Params.WearPerMegajoule * OverheatAccel, 0.0f, 1.0f);
        if (Reading.SurfaceTemperatureK >= Params.IgnitionTemperatureK) Reading.Ignited = true;

        Reading.GripMultiplier = EvaluateGripMultiplier(Params, Reading);
    }
};

} // namespace Frontier::Vehicle
