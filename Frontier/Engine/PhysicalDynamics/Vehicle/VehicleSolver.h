/*==========================================================================================================================================
                                                              VEHICLESOLVER.H
==========================================================================================================================================*/
// 🧩 Unified 4-corner vehicle dynamics solver: GRIT + ProjectDrive powertrain/aero/slip/brakes + 3 soft-body tyre physics models.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/Aerodynamics.h"
#include "Engine/PhysicalDynamics/Vehicle/BrakingAssembly.h"
#include "Engine/PhysicalDynamics/Vehicle/Drivetrain.h"
#include "Engine/PhysicalDynamics/Vehicle/PacejkaMagicFormula.h"
#include "Engine/PhysicalDynamics/Vehicle/SteeringAndAssists.h"
#include "Engine/PhysicalDynamics/Vehicle/SuspensionKinematics.h"
#include "Engine/PhysicalDynamics/Vehicle/ThermalIntegrator.h"
#include "Engine/PhysicalDynamics/Vehicle/TyreSlipDynamics.h"
#include "Engine/PhysicalDynamics/Vehicle/TyreSoftBody.h"
#include "Engine/PhysicalDynamics/Vehicle/VehicleGeometry.h"

#include <array>
#include <cstdint>
#include <functional>

namespace Frontier::Vehicle
{

//------------------------------------------------------------------------------------------------------------------------
//                                              DRIVER COMMANDS & CHASSIS POSE
//------------------------------------------------------------------------------------------------------------------------

struct DriverCommand
{
    float Throttle      = 0.0f;    // [0..1] accelerator pedal
    float Brake         = 0.0f;    // [0..1] hydraulic footbrake pedal
    float Steer         = 0.0f;    // [-1..+1] steering input (+1 = turn left/+Z, -1 = turn right/-Z)
    bool  Handbrake     = false;   // [-] mechanical rear handbrake
    bool  LineLock      = false;   // [-] GRIT front-only line-lock for standing rear burnouts
    bool  DrsRequested  = false;   // [-] explicit DRS wing flatten request
    bool  HeadlightsOn  = true;    // [-] cabin/luminaire beam state
    int   RequestedGear = -1;      // [-] -1 = automatic/keep; 0 = R, 1 = N, 2..7 = 1st..6th
};

struct ChassisKinematicReading
{
    Vec3 PositionWorld        {0.0f, 0.0f, 0.55f};     // [m] chassis centre of mass in world space
    Quat OrientationWorld     = Quat::Identity();      // [-] chassis orientation in world space
    Vec3 LinearVelocityWorld  {0.0f, 0.0f, 0.0f};      // [m/s] chassis linear velocity
    Vec3 AngularVelocityWorld {0.0f, 0.0f, 0.0f};      // [rad/s] chassis angular velocity
};

//------------------------------------------------------------------------------------------------------------------------
//                                             SOLVER CONFIGURATION & TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

struct VehicleSolverConfiguration
{
    VehicleGeometrySpecification    Geometry{};
    TyreFormulation                 SoftBodyTyreMode = TyreFormulation::Spring;
    SlipSolverMode                  SlipMode         = SlipSolverMode::GritImplicitNewton;
    TyreSoftBodyParameters          TyreSoftBody{};
    PacejkaParameters               TyreCompound     = PacejkaParameters::DefaultRoad();
    TyreThermalParameters           TyreThermal{};
    StrutSpecification              FrontStrut{};
    StrutSpecification              RearStrut{};
    AntiRollBarSpecification        AntiRoll{};
    SteeringParameters              Steering{};
    DriverAssistParameters          Assists{};
    InductionType                   Induction        = InductionType::Turbocharged;
    bool                            RaceTune         = false;
    EngineParameters                Engine{};
    TurbochargerParameters          Turbocharger{};
    SuperchargerParameters          Supercharger     = SuperchargerParameters::DefaultTwinScrew();
    EngineThermalAndFuelParameters  EngineFluids{};
    TransmissionParameters          Transmission{};
    DifferentialParameters          Differential{};
    BrakeAssemblyParameters         Brakes{};
    AerodynamicParameters           Aerodynamics{};
};

struct WheelCornerTelemetry
{
    Vec3                HubPositionWorld        {0.0f, 0.0f, 0.0f};       // [m] solved 3D wheel hub centre
    Quat                WheelOrientationWorld   = Quat::Identity();       // [-] chassis * steer * camber * spin
    Quat                CarrierOrientationWorld = Quat::Identity();       // [-] chassis * steer * camber (no spin)
    Vec3                ContactPointWorld       {0.0f, 0.0f, 0.0f};       // [m] road contact patch centre
    Vec3                ContactNormalWorld      {0.0f, 0.0f, 1.0f};       // [-] road unit normal
    bool                InContact               = false;                  // [-] true when tyre touches ground
    bool                WheelLocked             = false;                  // [-] true when wheel spin locked under load
    bool                AbsActive               = false;                  // [-] true when ABS pulses this corner
    bool                Shredded                = false;                  // [-] true when tread corded / blown out
    float               SteerAngleRadians       = 0.0f;                   // [rad] Ackermann steer angle
    float               SpinAngleRadians        = 0.0f;                   // [rad] cumulative wheel rotation angle
    float               WheelOmegaRadPerSec     = 0.0f;                   // [rad/s] wheel angular velocity
    float               StrutCompressionMetres  = 0.0f;                   // [m] suspension jounce travel
    float               StrutForceNewtons       = 0.0f;                   // [N] vertical suspension strut force
    float               NormalLoadNewtons       = 0.0f;                   // [N] tyre-road normal force Fz
    float               LongitudinalForceN      = 0.0f;                   // [N] tyre-road longitudinal force Fx
    float               LateralForceN           = 0.0f;                   // [N] tyre-road lateral force Fy
    float               AligningTorqueNm        = 0.0f;                   // [N*m] self-aligning torque Mz
    float               SlipRatio               = 0.0f;                   // [-] longitudinal slip ratio kappa
    float               SlipAngleRadians        = 0.0f;                   // [rad] lateral slip angle alpha
    float               SlipPowerWatts          = 0.0f;                   // [W] frictional heat dissipation
    float               BrakeTorqueNm           = 0.0f;                   // [N*m] applied brake torque
    float               BrakeRotorTempK         = 293.15f;                // [K] ventilated disc rotor temperature
    TyreSoftBodyReading SoftBody{};                                       // [-] soft-body deflection, bulge, volume, J, P
    TyreThermalReading  Thermal{};                                        // [-] 3-node temperatures, gas pressure, wear
};

struct VehicleTelemetryReading
{
    ChassisKinematicReading              Chassis{};
    float                                ForwardSpeedMps       = 0.0f;    // [m/s] body +X speed
    float                                LateralSpeedMps       = 0.0f;    // [m/s] body +Y speed
    float                                SpeedKilometresPerHr  = 0.0f;    // [km/h] total horizontal speed
    float                                YawRateRadPerSec      = 0.0f;    // [rad/s] body +Z angular rate
    float                                SteeringWheelRadians  = 0.0f;    // [rad] cabin steering wheel rotation
    float                                SteeringColumnTorqueNm= 0.0f;    // [N*m] SAT steering feedback torque
    bool                                 StationarityHoldActive= false;   // [-] coupled PGS slope/brake hold active
    bool                                 TractionControlActive = false;   // [-] TCS active
    bool                                 StabilityControlActive= false;   // [-] ESC active
    DrivetrainOutputs                    Powertrain{};
    AerodynamicOutputs                   Aero{};
    std::array<WheelCornerTelemetry, 4>  Wheels{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    VEHICLE SOLVER
//------------------------------------------------------------------------------------------------------------------------

inline constexpr size_t SceneVehicleProfileByteCount = 10448u;   // [B] exact size of VehicleVisual.slang VehicleProfile

class VehicleSolver
{
  public:
    struct WorldHooks
    {
        std::function<ChassisKinematicReading()>                     ReadChassis{};
        std::function<void(const Vec3& ForceN, const Vec3& PointW)>  ApplyForceAtPoint{};
        std::function<void(const Vec3& TorqueNm)>                    ApplyTorque{};
        GroundQuery                                                  Ground{};
        Vec3                                                         AmbientWindWorld{0.0f, 0.0f, 0.0f};
    };

    VehicleSolver() noexcept = default;

    void Build(const VehicleSolverConfiguration& Configuration, const WorldHooks& Hooks,
               const ChassisKinematicReading& InitialChassis) noexcept;
    void Reconfigure(const VehicleSolverConfiguration& Configuration) noexcept;
    void AssignSoftBodyFormulation(TyreFormulation Formulation) noexcept;
    void AssignSlipSolverMode(SlipSolverMode Mode) noexcept { Config.SlipMode = Mode; }
    void AssignCommand(const DriverCommand& Command) noexcept { ActiveCommand = Command; }
    void ConfigureWheelEnclosure(uint32_t WheelIndex, bool EnclosedCondition) noexcept
    {
        Corners[WheelIndex & 3u].EnclosedCondition = EnclosedCondition;
    }
    [[nodiscard]] bool QueryWheelEnclosed(uint32_t WheelIndex) const noexcept
    {
        return Corners[WheelIndex & 3u].EnclosedCondition;
    }
    void ConfigureAmbientWind(const Vec3& WindWorld) noexcept { Hooks.AmbientWindWorld = WindWorld; }
    void PunctureWheel(uint32_t WheelIndex) noexcept;

    void Step(float DeltaSeconds) noexcept;

    [[nodiscard]] const VehicleSolverConfiguration& QueryConfiguration() const noexcept { return Config; }
    [[nodiscard]] const VehicleTelemetryReading&    QueryTelemetry() const noexcept { return Telemetry; }
    [[nodiscard]] const TyreSoftBody&               QueryCornerSoftBody(uint32_t WheelIndex) const noexcept
    {
        return Corners[WheelIndex & 3u].SoftBody;
    }

    // 📝 Encodes the 10,448-byte VehicleProfile region consumed directly by Frontier/Engine/Shaders/GlobalIllumination/VehicleVisual.slang.
    void EncodeSceneVehicleProfile(void* Destination10448Bytes) const noexcept;

  private:
    struct CornerRuntime
    {
        TyreSoftBody        SoftBody{};
        TyreSlipVariables   Slip{};
        TyreThermalReading  Thermal{};
        float               StrutLengthMetres   = 0.34f;
        float               WheelOmegaRadPerSec = 0.0f;
        float               SpinAngleRadians    = 0.0f;
        float               PrevLateralForceN   = 0.0f;
        float               PrevLongForceN      = 0.0f;
        bool                EnclosedCondition   = true;
    };

    VehicleSolverConfiguration     Config{};
    WorldHooks                     Hooks{};
    DriverCommand                  ActiveCommand{};
    PacejkaMagicFormula            MagicFormula{};
    TyreSlipSolver                 SlipSolver{};
    Drivetrain                     Powertrain{};
    BrakingAssembly                Brakes{};
    SteeringAndAssists             Steering{};
    AerodynamicsSolver             Aero{};
    std::array<CornerRuntime, 4>   Corners{};
    VehicleTelemetryReading        Telemetry{};
};

} // namespace Frontier::Vehicle
