/*==========================================================================================================================================
                                                                DRIVETRAIN.H
==========================================================================================================================================*/
// 🧩 engine / turbo / AMT clutch / transmission / differential
//
//    Phase-1 port of GRIT's powertrain (VehicleSolver.cpp SolvePowertrain / differential lambda, plus the component spec
//    headers Engine/Turbocharger/Transmission/Clutch Specifications.h). Pure C++/scalar — no Unreal, no Jolt, no threading.
//    Defaults are the GRIT "preset-1 GTR" figures extracted from the source (torque curve, turbo maps, ratios, inertias).
//
//    Pipeline per step:  engine (RK4 on ω, torque-curve × turbo boost − engine braking) → AMT single clutch (slip/lock)
//                        → transmission (gear × final drive) → differential (Open / Locked / LSD / TorqueVectoring).
//
//    References (as cited in GRIT source): Heywood (1988) "Internal Combustion Engine Fundamentals"; Dixon (2013)
//    "The Shock Absorber Handbook" / driveline; Serrano et al. (2007) turbocharger lawling; Van Berkel (2014) & Walker
//    (2011) automated-manual-transmission clutch dynamics.

#pragma once

#include "VehicleConditions.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Frontier::Vehicle
{

//------------------------------------------------------------------------------------------------------------------------
// Monotonic-x lookup curve with binary-search + linear interpolation (mirrors GRIT SampleTorque / Eval* pattern).
//------------------------------------------------------------------------------------------------------------------------
struct Curve
{
    std::vector<float> X, Y;

    void Add(float x, float y) { X.push_back(x); Y.push_back(y); }

    [[nodiscard]] float Sample(float x) const noexcept
    {
        const std::size_t n = X.size();
        if (n == 0) return 0.0f;
        if (x <= X.front()) return Y.front();
        if (x >= X.back())  return Y.back();
        std::size_t lo = 0, hi = n - 1;
        while (hi - lo > 1) { const std::size_t mid = (lo + hi) >> 1; (X[mid] <= x ? lo : hi) = mid; }
        const float t = (X[hi] == X[lo]) ? 0.0f : (x - X[lo]) / (X[hi] - X[lo]);
        return Y[lo] + t * (Y[hi] - Y[lo]);   // FMath::Lerp
    }
};

//------------------------------------------------------------------------------------------------------------------------
struct EngineParameters
{
    Curve TorqueCurve;        // rpm → Nm (wide-open throttle)
    Curve ExhaustFlowCurve;   // rpm → kg/s
    Curve ExhaustTempCurve;   // rpm → K

    float IdleRPM = 700.0f;
    float RedlineRPM = 7200.0f;
    float MaxRPMIncrease = 1800.0f;   // [rpm/s]
    float MaxRPMDecrease = 2600.0f;   // [rpm/s]
    float EngineBrakingCoeff = 0.08f;      // [N·m·s/rad]
    float EngineBrakingQuadratic = 0.00012f;
    float EngineInertia = 0.38f;           // [kg·m²]
    float InvEngineInertia = 1.0f / 0.38f;

    static EngineParameters DefaultGTR();
};

struct TurbochargerParameters
{
    Curve BoostPressureCurve;      // turbo rpm → Bar
    Curve TorqueMultiplierCurve;   // Bar → multiplier

    float TurboInertia = 0.02f;            // [kg·m²]
    float InvTurboInertia = 50.0f;
    float TurboFrictionCoeff = 0.002f;
    float TurboFrictionQuadratic = 0.00005f;
    float WastegateMaxBoost = 1.2f;        // [Bar] (race: 1.6)
    float BovPressureReleaseRate = 10.0f;  // [Bar/s]
    float TurbineEfficiency = 0.65f;
    float CompressorEfficiency = 0.70f;
    float MechanicalEfficiency = 0.98f;

    static TurbochargerParameters DefaultGTR();
};

// Forced-induction selection. A supercharger is BELT-driven off the crank, so its boost tracks engine rpm with no lag
//   (unlike the turbo's exhaust-spool), and it robs crank torque as parasitic drag. A blown engine trades the turbo's
//   lag for an instant, RPM-proportional boost and a permanent parasitic tax.
enum class InductionType
{
    NaturallyAspirated,   // no forced induction (boostMult = 1)
    Turbocharged,         // exhaust-driven turbo (default, spool dynamics)
    Supercharged,         // belt-driven supercharger (instant boost, parasitic drag)
};

// Port of GRIT's FSuperchargerSpecifications (source-only Components/SuperchargerSpecifications.h). Positive-displacement
//   or centrifugal blower geared to the crank at DriveRatio; boost from charger-rpm curve, torque gain from a boost
//   curve, and a parasitic-drag curve that loads the crank. A bypass (recirculation) valve regulates boost to the tune
//   ceiling and vents off-throttle so the blower is not compressing against a closed throttle.
enum class SuperchargerDriveType { Roots, TwinScrew, Centrifugal };

struct SuperchargerParameters
{
    SuperchargerDriveType DriveType = SuperchargerDriveType::TwinScrew;

    Curve BoostPressureCurve;      // charger rpm → Bar
    Curve TorqueMultiplierCurve;   // Bar → multiplier
    Curve ParasiticDragCurve;      // charger rpm → N·m (crank load)

    float ChargerInertia          = 0.015f;   // [kg·m²]
    float ChargerFrictionCoeff    = 0.003f;   // [N·m·s/rad]
    float ChargerFrictionQuadratic= 0.00008f; // [N·m·s²/rad²]
    float DriveRatio              = 3.2f;     // charger rpm = engine rpm × DriveRatio
    float MaxBoost_Pump           = 0.8f;     // [Bar] pump-fuel tune ceiling
    float MaxBoost_Race           = 1.2f;     // [Bar] race-fuel tune ceiling
    float BypassValveRate         = 8.0f;     // [Bar/s] recirculation valve slew
    float CompressorEfficiency    = 0.72f;    // [-]
    float ParasiticLossFactor     = 0.08f;    // [-] residual drag fraction when fully bypassed (off-throttle)

    [[nodiscard]] float ChargerRPM(float engineRPM) const noexcept { return engineRPM * DriveRatio; }

    static SuperchargerParameters DefaultTwinScrew();
};

struct TransmissionParameters
{
    // Index layout: [0]=R2, [1]=R1, [2]=N(0), [3..]=1st..6th
    std::vector<float> GearRatios = {-3.8f, -2.0f, 0.0f, 4.2f, 3.1f, 2.5f, 1.9f, 1.5f, 1.2f};
    float FinalDriveRatio = 3.55f;
    float DriveEfficiency = 0.92f;
    int   NeutralIndex = 2;

    [[nodiscard]] float RatioAt(int gearIndex) const noexcept
    {
        if (gearIndex < 0 || gearIndex >= static_cast<int>(GearRatios.size())) return 0.0f;
        return GearRatios[gearIndex];
    }
};

// What couples the engine to the gearbox. SinglePlate is the source's AMT friction clutch (instant shifts unless
//   ShiftDuration stages them); DualClutch overlaps two plates through a handoff; TorqueConverter is a fluid automatic
//   with a lockup clutch. Reverse driving is out of scope for every category (no PRND selector is lawled).
enum class ClutchCategory
{
    SinglePlate,
    DualClutch,
    TorqueConverter,
};

enum class PropulsionType
{
    Combustion,   // engine → clutch → multi-speed gearbox (the source)
    Electric,     // traction motor → single-speed reduction (no gears, no clutch, no idle)
};

struct ClutchParameters
{
    float MaxTorqueCapacity = 1200.0f;   // [N·m] (preset: 1500)
    float BreakawaySlipRPM  = 300.0f;    // slip at which capacity is reached
    float LockupEngagement  = 0.98f;
    float LockupSlipRPM     = 50.0f;
    // 📝 Beyond Project Drive (all identities by default: SinglePlate, instant shifts, no converter).
    ClutchCategory Category       = ClutchCategory::SinglePlate;
    float ShiftDuration           = 0.0f;    // [s] staged single-plate shift (0: the source's instant shift)
    float HandoffSeconds          = 0.15f;   // [s] dual-clutch overlap
    float LaunchSeconds           = 0.8f;    // [s] staged single-plate launch ramp
    float ConverterKFactor        = 100.0f;  // [rpm/√Nm] pump torque = (N_pump / K)²
    float ConverterStallRatio     = 1.9f;    // [-] turbine/pump torque at stall
    float ConverterCouplingPoint  = 0.85f;   // [-] speed ratio where the ratio reaches 1
    float LockupRPM               = 3000.0f; // [rpm] turbine speed that engages the lockup clutch (above stall: locking earlier lugs the unspooled engine and the car never breaks through)
    float LockupDropRPM           = 300.0f;  // [rpm] hysteresis below LockupRPM that releases it
    float LockupRate              = 2.0f;    // [1/s] lockup engagement slew (a progressive ~0.5 s grab; slamming it chatters)
};

enum class DifferentialMode
{
    Open,            // equal torque split
    Locked,          // spool: equal speed, torque follows
    LimitedSlip,     // clutch-pack LSD with locking bias
    TorqueVectoring, // active bias toward the outer/faster or commanded wheel
};

struct DifferentialParameters
{
    DifferentialMode Mode = DifferentialMode::LimitedSlip;
    float LockingFactor = 0.5f;   // [0..1] LSD bias strength
    float Preload_Nm = 20.0f;     // LSD preload torque
    float VectoringBias = 0.0f;   // [-1..1] TV command (+ = bias right)
};

//------------------------------------------------------------------------------------------------------------------------
struct DrivetrainInputs
{
    float Throttle = 0.0f;    // [0..1]
    int   GearIndex = 3;      // index into GearRatios (3 = 1st gear)
    float dt = 1.0f / 240.0f; // [s]
    // Feedback from the wheels (average driven-wheel spin surface speed → gearbox output rpm reference).
    float DrivenWheelRPM = 0.0f;   // average driven wheel rpm (post-final-drive reference for clutch slip)
    float LeftWheelRPM = 0.0f;
    float RightWheelRPM = 0.0f;
    // 📝 Beyond Project Drive (Frontier): an empty tank stops the combustion torque; cold oil scales the engine's friction.
    bool  Fuelled       = true;   // [-]
    float FrictionScale = 1.0f;   // [-]
    // 📝 Staged gearboxes, the converter and the electric drive (< 0 / -1: unset — the source's behaviour).
    float ClutchEngagement    = -1.0f; // [-] staged single-plate engagement (0..1)
    int   PreselectGearIndex  = -1;    // dual-clutch preselected gear
    float HandoffBlend        = -1.0f; // [-] dual-clutch overlap 0..1
    bool  LockupAllowed       = true;  // converter lockup permission from the gearbox servo
    float Brake               = 0.0f;  // [0..1] brake pedal (regen authority on the electric drive)
    float RegenDemand_Nm      = -1.0f; // [Nm] motor-shaft regen the pedal asks for (< 0: pedal × the regen peak)
    float VehicleSpeed        = 0.0f;  // [m/s] (regen fades out near rest)
};

struct DrivetrainOutputs
{
    float EngineRPM = 0.0f;
    float EngineTorque_Nm = 0.0f;     // net engine torque after braking, before clutch
    float TurboRPM = 0.0f;            // turbo shaft rpm (0 when supercharged/NA) — also carries charger rpm when blown
    float BoostPressure_Bar = 0.0f;
    float BoostMultiplier = 1.0f;
    float ParasiticDrag_Nm = 0.0f;    // supercharger crank load (0 when NA/turbo)
    float ClutchTorque_Nm = 0.0f;     // torque transmitted through clutch
    bool  ClutchLocked = false;
    float ClutchEngagement = 0.0f;   // [-] 0 open … 1 rigid (the lockup level on the converter; the servo engagement staged)
    float GearboxOutputTorque_Nm = 0.0f;   // after gear × final drive × efficiency
    float LeftDriveTorque_Nm = 0.0f;       // differential outputs
    float RightDriveTorque_Nm = 0.0f;
    float ChargeTemperature_K = 0.0f;      // intercooled charge air (0 while ChargeAir is off)
    // 📝 Beyond Project Drive (intake manifold, converter, electric drive; 0/unset while off).
    float ManifoldPressure_Bar   = 0.0f;  // [bar] absolute plenum pressure
    float ConverterSlipRPM       = 0.0f;  // [rpm] pump − turbine (converter only)
    float MotorRPM               = 0.0f;  // [rpm] traction motor shaft
    float MotorTorque_Nm         = 0.0f;  // [Nm] motor shaft torque (+ motoring)
    float BatteryCharge          = 0.0f;  // [-] 0..1
    float BatteryVoltage_V       = 0.0f;  // [V] terminal voltage under load
    float BatteryPower_W         = 0.0f;  // [W] terminal power (+ discharging)
    float LeftRegenTorque_Nm     = 0.0f;  // [Nm] recuperation at the wheels (≤ 0)
    float RightRegenTorque_Nm    = 0.0f;  // [Nm]
};

//------------------------------------------------------------------------------------------------------------------------
class Drivetrain
{
public:
    Drivetrain();

    void AssignEngine(const EngineParameters& P) noexcept { Engine = P; }
    void AssignTurbo(const TurbochargerParameters& P) noexcept { Turbo = P; }
    void AssignTransmission(const TransmissionParameters& P) noexcept { Trans = P; }
    void AssignClutch(const ClutchParameters& P) noexcept { Clutch = P; }
    void AssignDifferential(const DifferentialParameters& P) noexcept { Diff = P; }
    void AssignInduction(InductionType t) noexcept { Induction = t; }
    void AssignSupercharger(const SuperchargerParameters& P) noexcept { Super = P; }
    void AssignRaceTune(bool race) noexcept { RaceTune = race; }
    void AssignChargeAir(const ChargeAirSpecification& P) noexcept { Charge = P; }
    void AssignPropulsion(PropulsionType p) noexcept { Propulsion = p; }
    void AssignIntake(const IntakeManifoldSpecification& P) noexcept { Intake = P; }
    void AssignElectricMotor(const ElectricMotorParameters& P) noexcept { Motor = P; }
    void AssignBattery(const BatteryParameters& P) noexcept { Battery = P; }

    void Reset(float engineRPM) noexcept;

    DrivetrainOutputs Step(const DrivetrainInputs& In) noexcept;

    [[nodiscard]] float EngineRPM() const noexcept { return EngineOmega * kRadToRpm; }
    [[nodiscard]] float TurboRPM()  const noexcept { return TurboOmegaRad * kRadToRpm; }

    [[nodiscard]] const EngineParameters&        QueryEngine() const noexcept { return Engine; }
    [[nodiscard]] const TurbochargerParameters&  QueryTurbo()  const noexcept { return Turbo; }
    [[nodiscard]] const TransmissionParameters&  QueryTransmission() const noexcept { return Trans; }
    [[nodiscard]] const ClutchParameters&        QueryClutch() const noexcept { return Clutch; }

    static constexpr float kRpmToRad = 0.104719755f;   // 2π/60
    static constexpr float kRadToRpm = 9.54929658f;    // 60/2π

private:
    float NetEngineTorque(float omega, float throttle, float boostMult, float clutchLoad) const noexcept;
    void  SpoolTurbo(float engineRPM, float throttle, float dt) noexcept;
    // Belt-driven supercharger: returns the torque multiplier for this step and writes the parasitic crank drag.
    float StepSupercharger(float engineRPM, float throttle, float dt, float& parasiticDrag_Nm) noexcept;
    void  Differentiate(float driveTorque, float leftRPM, float rightRPM, float& outL, float& outR) const noexcept;
    // 📝 Beyond Project Drive: plenum filling (returns the air ratio ρ_m/ρ_up), the fluid converter (pump load and
    //    turbine torque, blended with the lockup clutch), and the electric drive (motor + battery, full outputs).
    float StepIntake(float upstream_Bar, float engineRPM, float throttle, float dt) noexcept;
    void  StepConverter(float pumpRPM, float turbineRPM, bool lockupAllowed, float dt, float& pumpTorque_Nm, float& turbineTorque_Nm) noexcept;
    void  StepElectric(const DrivetrainInputs& In, float dt, DrivetrainOutputs& Out) noexcept;

    EngineParameters       Engine;
    TurbochargerParameters Turbo;
    SuperchargerParameters Super;
    TransmissionParameters Trans;
    ClutchParameters       Clutch;
    DifferentialParameters Diff;
    InductionType          Induction = InductionType::Turbocharged;
    bool                   RaceTune  = false;
    ChargeAirSpecification Charge{};
    PropulsionType         Propulsion = PropulsionType::Combustion;
    IntakeManifoldSpecification Intake{};
    ElectricMotorParameters     Motor{};
    BatteryParameters           Battery{};

    float EngineOmega   = 73.3f;   // rad/s (~700 rpm)
    float TurboOmegaRad = 0.0f;    // rad/s (turbo shaft speed)
    float BoostBar      = 0.0f;    // bar
    float BovPosition   = 0.0f;    // [0..1]
    float WastegatePosition = 0.0f;// [0..1]
    float SuperBoostBar = 0.0f;    // bar (supercharger, bypass-regulated)
    float FrictionScale = 1.0f;    // [-] the oil's friction multiplier for this step (DrivetrainInputs::FrictionScale)
    // 📝 Beyond Project Drive reading (intake manifold, converter lockup, electric drive).
    float ManifoldPressure_Bar = 1.01325f; // [bar] absolute plenum pressure
    float UpstreamPressure_Bar = 1.01325f; // [bar] throttle-upstream pressure this step (ambient + blow-through boost)
    float IdleServo            = 0.0f;     // [-] idle governor integrator trim
    float LockupEngagement     = 0.0f;     // [-] converter lockup clutch 0..1
    float MotorTorqueSlew_Nm   = 0.0f;     // [Nm] drivability-slewed motor demand
    float BatteryCharge        = 0.6f;     // [-] 0..1
    float BatteryTemperature_K = 293.15f;  // [K]
};

} // namespace Frontier::Vehicle
