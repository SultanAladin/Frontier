/*==========================================================================================================================================
                                                               DRIVETRAIN.H
==========================================================================================================================================*/
// 🧩 Complete GRIT + ProjectDrive powertrain: NA / Turbo / Roots-TwinScrew-Centrifugal Supercharger, Coolant, Oil, Fuel, Clutch, Gearbox, Diff.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/VehicleMath.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::Vehicle
{

//------------------------------------------------------------------------------------------------------------------------
//                                            INDUCTION & SUPERCHARGER TYPES
//------------------------------------------------------------------------------------------------------------------------

enum class InductionType : uint8_t
{
    NaturallyAspirated = 0,   // [-] atmospheric intake, unity boost multiplier, zero parasitic drag
    Turbocharged       = 1,   // [-] 16-substep exhaust-driven isentropic turbine/compressor + BOV + wastegate
    Supercharged       = 2    // [-] crank-driven Roots / TwinScrew / Centrifugal compressor + bypass + parasitic drag
};

enum class SuperchargerType : uint8_t
{
    Roots       = 0,   // [-] positive-displacement lobe blower: instant low-end boost, lower adiabatic efficiency
    TwinScrew   = 1,   // [-] internal-compression helical blower: flat boost curve, high efficiency
    Centrifugal = 2    // [-] impeller compressor: boost rises quadratically with crank speed
};

enum class DifferentialMode : uint8_t
{
    Open            = 0,   // [-] 50/50 torque split
    Locked          = 1,   // [-] spool: transfers torque to slower-spinning wheel
    LimitedSlip     = 2,   // [-] clutch-pack LSD with preload + locking factor
    TorqueVectoring = 3    // [-] active yaw-biasing split
};

enum class DriveLayout : uint8_t
{
    RearWheelDrive  = 0,   // [-] 100% rear axle propulsion
    FrontWheelDrive = 1,   // [-] 100% front axle propulsion
    AllWheelDrive   = 2    // [-] split torque across front and rear differentials
};

//------------------------------------------------------------------------------------------------------------------------
//                                              POWERTRAIN SPECIFICATIONS
//------------------------------------------------------------------------------------------------------------------------

struct EngineParameters
{
    PiecewiseCurve TorqueCurveNm{
        {800.0f, 310.0f},
        {2000.0f, 430.0f},
        {3600.0f, 540.0f},
        {5200.0f, 588.0f},
        {6400.0f, 565.0f},
        {7100.0f, 505.0f},
        {7500.0f, 380.0f}};                       // [N*m vs RPM] VR38-inspired base torque curve
    float IdleRpm               = 850.0f;         // [rpm] closed-throttle idle target
    float RedlineRpm            = 7200.0f;        // [rpm] soft fuel-cut onset
    float HardCutRpm            = 7450.0f;        // [rpm] hard fuel-cut ceiling
    float FlywheelInertiaKgm2   = 0.24f;          // [kg*m^2] crank + flywheel rotational inertia
    float ViscousFrictionNms    = 0.14f;          // [N*m*s/rad] pumping + bearing friction slope
    float StaticFrictionNm      = 24.0f;          // [N*m] closed-throttle engine braking base
    float ThermalEfficiency     = 0.31f;          // [-] brake thermal efficiency (GRIT Fuel Pass)
};

struct TurbochargerParameters
{
    PiecewiseCurve BoostCurveBar{
        {0.0f, 0.0f},
        {35000.0f, 0.12f},
        {70000.0f, 0.52f},
        {105000.0f, 0.96f},
        {135000.0f, 1.25f},
        {160000.0f, 1.35f}};                      // [bar vs turbo shaft RPM]
    PiecewiseCurve TorqueMultiplierCurve{
        {0.0f, 1.00f},
        {0.4f, 1.18f},
        {0.8f, 1.36f},
        {1.2f, 1.54f},
        {1.5f, 1.66f}};                           // [- vs boost bar]
    float RotorInertiaKgm2      = 1.2e-4f;        // [kg*m^2] turbine + shaft + compressor wheel inertia
    float TurbineEfficiency     = 0.74f;          // [-] peak radial turbine isentropic efficiency
    float CompressorEfficiency  = 0.76f;          // [-] peak centrifugal compressor efficiency
    float MechanicalEfficiency  = 0.96f;          // [-] journal/ball bearing mechanical efficiency
    float WastegateMaxBoostBar  = 1.20f;          // [bar] wastegate boost target ceiling
    float BovReleaseRateBarPerS = 4.20f;          // [bar/s] blow-off valve dump rate off-throttle
};

struct SuperchargerParameters
{
    SuperchargerType Type                  = SuperchargerType::TwinScrew;
    float            DriveRatio            = 3.20f;     // [-] pulley ratio (charger RPM / engine RPM)
    float            RotorInertiaKgm2      = 0.0085f;   // [kg*m^2] rotor inertia reflected to crank by DriveRatio^2
    float            MaxChargerRpm         = 23000.0f;  // [rpm] peak rated charger speed
    float            MaxBoostBaseBar       = 0.80f;     // [bar] 91-RON pump-fuel boost ceiling
    float            MaxBoostRaceBar       = 1.20f;     // [bar] 100-RON race-tune boost ceiling
    float            AdiabaticEfficiency   = 0.74f;     // [-] isentropic compression efficiency
    float            MechanicalEfficiency  = 0.92f;     // [-] belt & gear-case efficiency
    float            PeakParasiticPowerKw  = 38.0f;     // [kW] full-boost compressor shaft power at redline
    float            BypassIdleDragRatio   = 0.10f;     // [-] parasitic fraction when bypass valve is open off-throttle
    float            ResponseRatePerSecond = 26.0f;     // [1/s] manifold plenum fill rate (instant crank response)
    float            BoostTorqueGainPerBar = 0.46f;     // [1/bar] torque multiplier gain per bar of positive boost

    [[nodiscard]] float ChargerRpm(float EngineRpm) const noexcept { return std::max(0.0f, EngineRpm * DriveRatio); }

    [[nodiscard]] static SuperchargerParameters DefaultRoots() noexcept;
    [[nodiscard]] static SuperchargerParameters DefaultTwinScrew() noexcept;
    [[nodiscard]] static SuperchargerParameters DefaultCentrifugal() noexcept;
};

struct EngineThermalAndFuelParameters
{
    // 📝 GRIT CoolantSpecifications + EngineOilSpecifications + FuelControlSystem.
    float AmbientTemperatureK    = 293.15f;      // [K] 20 degC ambient air
    float ThermostatOpenTempK    = 361.15f;      // [K] 88 degC thermostat opening onset
    float OptimalCoolantTempK    = 365.15f;      // [K] 92 degC regulated coolant temperature
    float OverheatLimpTempK      = 398.15f;      // [K] 125 degC ECU torque-protection derate onset
    float MaxCoolantTempK        = 423.15f;      // [K] 150 degC pressurized cap boiling limit
    float EngineThermalMassJK    = 50000.0f;     // [J/K] block + head + coolant thermal mass
    float RadiatorCoolingWK      = 105.0f;       // [W/K] base radiator conductance at open thermostat

    float OilSumpCapacityLitres  = 5.5f;         // [L] 5W-40 sump capacity
    float OilDensityKgPerM3      = 850.0f;       // [kg/m^3] synthetic motor oil density
    float OilSpecificHeatJkgK    = 2000.0f;      // [J/(kg*K)] oil specific heat
    float OilViscosity40C_cSt    = 90.0f;        // [cSt] kinematic viscosity at 40 degC
    float OilViscosity100C_cSt   = 14.0f;        // [cSt] kinematic viscosity at 100 degC
    float OilDamageTempK         = 423.15f;      // [K] 150 degC oil film breakdown threshold

    float FuelTankCapacityLitres = 74.0f;        // [L] GRIT fuel tank volume
    float InitialFuelLitres      = 55.0f;        // [L] starting fuel load
    float FuelDensityKgPerLitre  = 0.745f;       // [kg/L] RON-98 gasoline density
    float FuelEnergyDensityJkg   = 43.5e6f;      // [J/kg] lower heating value (43.5 MJ/kg)
    bool  EnforceFuelStarvation  = false;        // [-] cut engine torque if tank runs completely dry
};

struct ClutchParameters
{
    float MaxTorqueCapacityNm = 920.0f;    // [N*m] clamped friction capacity
    float BreakawaySlipRpm    = 180.0f;    // [rpm] slip width for full capacity ramp
    float LockupSlipRpm       = 25.0f;     // [rpm] lockup threshold
};

struct TransmissionParameters
{
    // Gears: [0] = Reverse, [1] = Neutral, [2..7] = 1st..6th forward gears.
    std::vector<float> GearRatios      {-3.38f, 0.0f, 3.82f, 2.36f, 1.68f, 1.31f, 1.05f, 0.84f};
    int                ReverseIndex    = 0;
    int                NeutralIndex    = 1;
    int                FirstForwardIdx = 2;
    float              FinalDriveRatio = 3.70f;     // [-] ring & pinion ratio
    float              Efficiency      = 0.93f;     // [-] driveline mechanical efficiency
    float              ShiftDurationS  = 0.095f;    // [s] AMT torque-cut shift time
    float              UpshiftRpm      = 6850.0f;   // [rpm] automatic upshift threshold
    float              DownshiftRpm    = 3100.0f;   // [rpm] automatic downshift threshold
    bool               AutomaticShift  = true;      // [-] enable automatic sequential shifting
};

struct DifferentialParameters
{
    DifferentialMode Mode              = DifferentialMode::LimitedSlip;
    DriveLayout      Layout            = DriveLayout::AllWheelDrive;
    float            FrontTorqueShare  = 0.35f;    // [-] AWD front/rear bias (0.35 front / 0.65 rear)
    float            LockingFactor     = 0.42f;    // [-] LSD locking torque transfer fraction
    float            PreloadNm         = 65.0f;    // [N*m] LSD static clutch preload
    float            VectoringBias     = 0.0f;     // [-1..+1] active torque vectoring command (+right)
};

//------------------------------------------------------------------------------------------------------------------------
//                                            DRIVETRAIN INPUTS & TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

struct DrivetrainInputs
{
    float                 Throttle            = 0.0f;              // [0..1] driver / TCS-modulated pedal
    int                   RequestedGear       = -1;                // [-] -1 keeps current/auto gear; >=0 requests explicit index
    std::array<float, 4>  WheelOmegaRadPerSec {0.0f, 0.0f, 0.0f, 0.0f};   // [rad/s] FL, FR, RL, RR wheel spin rates
    float                 VehicleSpeedMps     = 0.0f;              // [m/s] forward airspeed for radiator & sump cooling
    float                 DeltaSeconds        = 1.0f / 240.0f;     // [s] timestep
};

struct DrivetrainOutputs
{
    std::array<float, 4> WheelDriveTorqueNm {0.0f, 0.0f, 0.0f, 0.0f};   // [N*m] propulsion torque at FL, FR, RL, RR
    float                EngineRpm          = 850.0f;                    // [rpm] crankshaft speed
    float                EngineTorqueNm     = 0.0f;                      // [N*m] net engine flywheel torque
    float                BoostPressureBar   = 0.0f;                      // [bar] positive manifold gauge boost
    float                BoostMultiplier    = 1.0f;                      // [-] torque boost multiplier
    float                ForcedInductionRpm = 0.0f;                      // [rpm] turbo shaft or supercharger rotor RPM
    float                ParasiticDragNm    = 0.0f;                      // [N*m] supercharger crank load
    float                CoolantTempK       = 358.15f;                   // [K] engine coolant temperature
    float                OilTempK           = 358.15f;                   // [K] engine sump oil temperature
    float                OilViscosityCst    = 18.0f;                     // [cSt] live kinematic oil viscosity
    float                FuelRemainingL     = 55.0f;                     // [L] remaining fuel volume
    float                FuelMassKg         = 40.975f;                   // [kg] remaining fuel mass
    float                FuelFlowKgPerSec   = 0.0f;                      // [kg/s] instantaneous fuel burn rate
    int                  ActiveGearIndex    = 2;                         // [-] current gear index
    bool                 Shifting           = false;                     // [-] true during shift cut
    bool                 ClutchLocked       = false;                     // [-] true when clutch slip < LockupSlipRpm
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    DRIVETRAIN SOLVER
//------------------------------------------------------------------------------------------------------------------------

class Drivetrain
{
  public:
    Drivetrain() noexcept;

    void AssignInduction(InductionType Type) noexcept { Induction = Type; }
    void AssignSupercharger(const SuperchargerParameters& Parameters) noexcept { Super = Parameters; }
    void AssignTurbocharger(const TurbochargerParameters& Parameters) noexcept { Turbo = Parameters; }
    void AssignRaceTune(bool Enabled) noexcept { RaceTune = Enabled; }
    void AssignDifferential(const DifferentialParameters& Parameters) noexcept { Diff = Parameters; }
    void AssignTransmission(const TransmissionParameters& Parameters) noexcept { Trans = Parameters; }
    void AssignEngine(const EngineParameters& Parameters) noexcept { Engine = Parameters; }
    void AssignThermalAndFuel(const EngineThermalAndFuelParameters& Parameters) noexcept { ThermalFuel = Parameters; }

    void Reset(float InitialRpm = 850.0f, bool WarmFluids = true) noexcept;

    [[nodiscard]] DrivetrainOutputs         Step(const DrivetrainInputs& Inputs) noexcept;
    [[nodiscard]] const DrivetrainOutputs&  QueryOutputs() const noexcept { return LastOutputs; }
    [[nodiscard]] InductionType             QueryInduction() const noexcept { return Induction; }
    [[nodiscard]] const SuperchargerParameters& QuerySupercharger() const noexcept { return Super; }

  private:
    void  StepTurbocharger(float EngineRpm, float Throttle, float Dt) noexcept;
    float StepSupercharger(float EngineRpm, float Throttle, float Dt, float& OutParasiticTorqueNm) noexcept;
    void  StepThermalAndFuel(float MechanicalPowerW, float VehicleSpeedMps, float Dt) noexcept;
    void  SplitAxleDifferential(float AxleTorqueNm, float LeftWheelRpm, float RightWheelRpm, float& OutLeftNm, float& OutRightNm) const noexcept;

    InductionType                  Induction   = InductionType::Turbocharged;
    bool                           RaceTune    = false;
    EngineParameters               Engine{};
    TurbochargerParameters         Turbo{};
    SuperchargerParameters         Super       = SuperchargerParameters::DefaultTwinScrew();
    EngineThermalAndFuelParameters ThermalFuel{};
    ClutchParameters               Clutch{};
    TransmissionParameters         Trans{};
    DifferentialParameters         Diff{};

    float             EngineOmegaRad      = 89.0f;     // [rad/s] crankshaft angular velocity
    float             TurboOmegaRad       = 1200.0f;   // [rad/s] turbocharger rotor speed
    float             TurboBoostBar       = 0.0f;      // [bar] manifold turbo boost
    float             BovPosition         = 0.0f;      // [0..1] blow-off valve opening
    float             WastegatePosition   = 0.0f;      // [0..1] wastegate actuator opening
    float             SuperBoostBar       = 0.0f;      // [bar] manifold supercharger boost
    float             CoolantTempK        = 358.15f;   // [K] coolant temperature
    float             OilTempK            = 358.15f;   // [K] oil sump temperature
    float             FuelMassRemainingKg = 40.975f;   // [kg] fuel mass in tank
    int               CurrentGearIndex    = 2;         // [-] 1st gear
    float             ShiftTimerSeconds   = 0.0f;      // [s] remaining shift cut duration
    float             ShiftCooldownSec    = 0.0f;      // [s] hysteresis guard between auto shifts
    DrivetrainOutputs LastOutputs{};
};

} // namespace Frontier::Vehicle
