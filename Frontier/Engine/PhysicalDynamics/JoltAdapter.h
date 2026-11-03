/*==========================================================================================================================================
                                                               JOLTADAPTER.H
==========================================================================================================================================*/
// 🧩 World-wide Jolt Physics v5.2.0 integration layer coupling static, kinematic, dynamic, fracture-ready, force-zone, and vehicle occupants.

#pragma once

#include "Engine/PhysicalDynamics/RigidBodySolver.h"
#include "Engine/PhysicalDynamics/Vehicle/VehicleJoltRig.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                             QUATERNION & POSE INTERPOLATION
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Vehicle::Quat InterpolateOrientationSlerp(const Vehicle::Quat& Start,
                                                        const Vehicle::Quat& Target,
                                                        float                Fraction) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                           WORLD OCCUPANT & FORCE ZONE SPECS
//------------------------------------------------------------------------------------------------------------------------

enum class AdapterForceZoneCategory : uint8_t
{
    UniformWind    = 0,   // [-] directional airflow velocity (e.g. crosswind / tunnel draft)
    VortexFunnel   = 1,   // [-] Burgers-Rott swirl + radial inflow + updraft around AxisOrigin
    RadialImpulse  = 2    // [-] outward spherical pressure wave from AxisOrigin within OuterRadiusMetres
};

struct AdapterForceZone
{
    uint64_t                 ZoneKey                = 0u;                                        // [-] caller identifier
    AdapterForceZoneCategory Category               = AdapterForceZoneCategory::UniformWind;     // [-] aerodynamic mechanism
    Vehicle::Vec3            OriginMetres           {0.0f, 0.0f, 0.0f};                          // [m] zone anchor in world space
    Vehicle::Vec3            FlowVelocityMps        {0.0f, 0.0f, 0.0f};                          // [m/s] wind vector for UniformWind
    float                    CoreRadiusMetres       = 8.0f;                                      // [m] peak-tangential vortex radius
    float                    OuterRadiusMetres      = 45.0f;                                     // [m] cutoff radius
    float                    TangentialSpeedMps     = 28.0f;                                     // [m/s] peak swirl speed (VortexFunnel)
    float                    RadialInflowMps        = 10.0f;                                     // [m/s] inward suction speed
    float                    AxialUpdraftMps        = 16.0f;                                     // [m/s] +Z updraft speed
    float                    PeakAccelerationMps2   = 35.0f;                                     // [m/s^2] peak blast acceleration
    float                    AirDensityKgPerM3      = 1.225f;                                    // [kg/m^3] ambient fluid density
    bool                     EnabledCondition       = true;                                      // [-] active switch
};

struct AdapterOccupantSpecification
{
    uint64_t               OccupantKey                = 0u;                       // [-] stable world correlation key (e.g. RecordToken.Value)
    RigidBodySpecification Body                       {};                         // [-] Jolt shape, motion category, mass, friction, pose
    int32_t                RenderOccupantIndex        = -1;                       // [-] IlluminationHost::Scene.Occupants index (-1 if none)
    float                  AerodynamicDragAreaM2      = 0.35f;                    // [m^2] Cd * A exposed to AdapterForceZone airflow
    float                  FractureImpulseThresholdNs = 0.0f;                     // [N*s] >0 marks FracturedCondition when contact impulse exceeds
    bool                   ForceZoneCoupled           = true;                     // [-] receives aerodynamic / vortex / impulse zone loads
};

struct AdapterOccupantSnapshot
{
    uint64_t                OccupantKey          = 0u;                                  // [-] correlation key
    RigidBodyIdentity       BodyIdentity         = InvalidRigidBody;                    // [-] Jolt body handle
    int32_t                 RenderOccupantIndex  = -1;                                  // [-] GPU scene occupant index
    RigidBodyMotionCategory Motion               = RigidBodyMotionCategory::Static;     // [-] motion domain
    Vehicle::Vec3           PreviousPosition     {0.0f, 0.0f, 0.0f};                    // [m] pose before outer step
    Vehicle::Quat           PreviousOrientation  = Vehicle::Quat::Identity();           // [-] orientation before outer step
    Vehicle::Vec3           CurrentPosition      {0.0f, 0.0f, 0.0f};                    // [m] pose after outer step
    Vehicle::Quat           CurrentOrientation   = Vehicle::Quat::Identity();           // [-] orientation after outer step
    Vehicle::Vec3           InterpolatedPosition {0.0f, 0.0f, 0.0f};                    // [m] blended pose at InterpolationFraction
    Vehicle::Quat           InterpolatedOrient   = Vehicle::Quat::Identity();           // [-] SLERP orientation at InterpolationFraction
    Vehicle::Vec3           LinearVelocityMps    {0.0f, 0.0f, 0.0f};                    // [m/s] world linear velocity
    Vehicle::Vec3           AngularVelocityRps   {0.0f, 0.0f, 0.0f};                    // [rad/s] world angular velocity
    float                   PeakImpulseNs        = 0.0f;                                // [N*s] largest impulse observed across step
    float                   KineticEnergyJoules  = 0.0f;                                // [J] translational kinetic energy
    bool                    AwakeCondition       = false;                               // [-] active in Jolt island
    bool                    FracturedCondition   = false;                               // [-] impulse exceeded FractureImpulseThresholdNs
};

struct AdapterVehicleSnapshot
{
    uint64_t                                             VehicleKey           = 0u;
    RigidBodyIdentity                                    ChassisBody          = InvalidRigidBody;
    Vehicle::Vec3                                        PreviousChassisPos   {0.0f, 0.0f, 0.0f};
    Vehicle::Quat                                        PreviousChassisOrient= Vehicle::Quat::Identity();
    Vehicle::Vec3                                        CurrentChassisPos    {0.0f, 0.0f, 0.0f};
    Vehicle::Quat                                        CurrentChassisOrient = Vehicle::Quat::Identity();
    Vehicle::Vec3                                        InterpolatedPos      {0.0f, 0.0f, 0.0f};
    Vehicle::Quat                                        InterpolatedOrient   = Vehicle::Quat::Identity();
    std::array<Vehicle::Vec3, 4>                         PreviousWheelPos     {};
    std::array<Vehicle::Quat, 4>                         PreviousWheelOrient  {};
    std::array<Vehicle::Vec3, 4>                         CurrentWheelPos      {};
    std::array<Vehicle::Quat, 4>                         CurrentWheelOrient   {};
    std::array<Vehicle::Vec3, 4>                         InterpolatedWheelPos {};
    std::array<Vehicle::Quat, 4>                         InterpolatedWheelOrn {};
    std::array<bool, 4>                                  WheelEnclosed        {true, true, true, true};
    Vehicle::VehicleTelemetryReading                     Telemetry            {};
    alignas(16) std::array<uint8_t, Vehicle::SceneVehicleProfileByteCount> EncodedGpuProfile{};
};

struct WorldPhysicsSnapshot
{
    uint64_t                             OuterTickIndex        = 0u;     // [-] completed outer ticks
    uint64_t                             TotalSubstepCount     = 0u;     // [-] completed inner substeps
    float                                SimulatedSeconds      = 0.0f;   // [s] cumulative simulated world time
    float                                OuterStepSeconds      = 0.0f;   // [s] outer step duration
    uint32_t                             SubstepsPerOuterTick  = 1u;     // [-] inner substeps per outer tick
    float                                InterpolationFraction = 1.0f;   // [-] [0..1] accumulator blend factor
    uint32_t                             ResidentBodyCount     = 0u;     // [-] total Jolt bodies
    uint32_t                             AwakeBodyCount        = 0u;     // [-] active Jolt bodies
    float                                StepWallMilliseconds  = 0.0f;   // [ms] wall duration of latest outer tick
    std::vector<AdapterOccupantSnapshot> Occupants             {};       // [-] all registered world occupants
    std::vector<AdapterVehicleSnapshot>  Vehicles              {};       // [-] all registered vehicles
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      JOLT ADAPTER
//------------------------------------------------------------------------------------------------------------------------

class JoltAdapter
{
  public:
    JoltAdapter() noexcept  = default;
    ~JoltAdapter() noexcept = default;

    JoltAdapter(const JoltAdapter&)            = delete;
    JoltAdapter& operator=(const JoltAdapter&) = delete;

    bool Initialize(const RigidBodyWorldSettings& WorldSettings = {}) noexcept;
    void Finalize() noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool                   Ready() const noexcept { return WorldSolver.Ready(); }
    [[nodiscard]] RigidBodySolver&       AccessSolver() noexcept { return WorldSolver; }
    [[nodiscard]] const RigidBodySolver& QuerySolver() const noexcept { return WorldSolver; }

    //--------------------------------------------------------------------------------------------------------------------
    //   WORLD OCCUPANT REGISTRATION (STATIC, KINEMATIC, DYNAMIC, FRACTURE-READY)
    //--------------------------------------------------------------------------------------------------------------------
    RigidBodyIdentity RegisterOccupant(const AdapterOccupantSpecification& Specification) noexcept;
    bool              ReclaimOccupant(uint64_t OccupantKey) noexcept;
    [[nodiscard]] bool AuthenticateOccupant(uint64_t OccupantKey) const noexcept;
    [[nodiscard]] size_t QueryOccupantCount() const noexcept { return OccupantRecords.size(); }

    void ConfigureKinematicTarget(uint64_t             OccupantKey,
                                  const Vehicle::Vec3& TargetPosition,
                                  const Vehicle::Quat& TargetOrientation) noexcept;
    void TeleportOccupant(uint64_t             OccupantKey,
                          const Vehicle::Vec3& Position,
                          const Vehicle::Quat& Orientation) noexcept;
    void ApplyOccupantForce(uint64_t OccupantKey, const Vehicle::Vec3& ForceNewtons) noexcept;
    void ApplyOccupantImpulse(uint64_t OccupantKey, const Vehicle::Vec3& ImpulseNewtonSeconds) noexcept;

    //--------------------------------------------------------------------------------------------------------------------
    //   VEHICLE REGISTRATION & PIT-STOP TYRE ENCLOSURE
    //--------------------------------------------------------------------------------------------------------------------
    bool RegisterVehicle(uint64_t                                 VehicleKey,
                         const Vehicle::VehicleSolverConfiguration& Configuration,
                         const Vehicle::Vec3&                     InitialPosition,
                         const Vehicle::Quat&                     InitialOrientation = Vehicle::Quat::Identity()) noexcept;
    bool ReclaimVehicle(uint64_t VehicleKey) noexcept;
    [[nodiscard]] size_t QueryVehicleCount() const noexcept { return VehicleRecords.size(); }

    void ConfigureDriverInput(uint64_t VehicleKey, const Vehicle::DriverCommand& Command) noexcept;
    void ReconfigureVehicle(uint64_t VehicleKey, const Vehicle::VehicleSolverConfiguration& Configuration) noexcept;
    void ConfigureWheelEnclosure(uint64_t VehicleKey, uint32_t WheelIndex, bool EnclosedCondition) noexcept;

    [[nodiscard]] Vehicle::VehicleJoltRig*       AccessVehicleRig(uint64_t VehicleKey) noexcept;
    [[nodiscard]] const Vehicle::VehicleJoltRig* QueryVehicleRig(uint64_t VehicleKey) const noexcept;

    //--------------------------------------------------------------------------------------------------------------------
    //   AERODYNAMIC / VORTEX / IMPULSE FORCE ZONES
    //--------------------------------------------------------------------------------------------------------------------
    void RegisterForceZone(const AdapterForceZone& Zone) noexcept;
    bool ReclaimForceZone(uint64_t ZoneKey) noexcept;
    void ClearForceZones() noexcept;

    //--------------------------------------------------------------------------------------------------------------------
    //   SUBSTEPPED INTEGRATION & POSE INTERPOLATION
    //--------------------------------------------------------------------------------------------------------------------
    void ExecuteSubsteppedTick(float OuterStepSeconds, uint32_t SubstepCount = 4u) noexcept;

    [[nodiscard]] RayCastReading CastWorldRay(const Vehicle::Vec3& Origin,
                                              const Vehicle::Vec3& DirectionAndLength,
                                              RigidBodyIdentity    IgnoredBody = InvalidRigidBody) const noexcept;

    [[nodiscard]] bool SampleOccupantPose(uint64_t                 OccupantKey,
                                          float                    InterpolationFraction,
                                          AdapterOccupantSnapshot& OutSnapshot) const noexcept;
    [[nodiscard]] bool SampleVehicleSnapshot(uint64_t                VehicleKey,
                                             float                   InterpolationFraction,
                                             AdapterVehicleSnapshot& OutSnapshot) const noexcept;

    void BuildWorldSnapshot(WorldPhysicsSnapshot& OutSnapshot, float InterpolationFraction = 1.0f) const;
    static void InterpolateWorldSnapshot(WorldPhysicsSnapshot& InOutSnapshot, float InterpolationFraction) noexcept;

  private:
    struct OccupantSlot
    {
        AdapterOccupantSpecification Specification       {};
        RigidBodyIdentity            BodyIdentity        = InvalidRigidBody;
        float                        ResolvedMassKg      = 1.0f;
        Vehicle::Vec3                PreviousPosition    {0.0f, 0.0f, 0.0f};
        Vehicle::Quat                PreviousOrientation = Vehicle::Quat::Identity();
        Vehicle::Vec3                CurrentPosition     {0.0f, 0.0f, 0.0f};
        Vehicle::Quat                CurrentOrientation  = Vehicle::Quat::Identity();
        Vehicle::Vec3                KinematicStartPos   {0.0f, 0.0f, 0.0f};
        Vehicle::Quat                KinematicStartOrn   = Vehicle::Quat::Identity();
        Vehicle::Vec3                KinematicTargetPos  {0.0f, 0.0f, 0.0f};
        Vehicle::Quat                KinematicTargetOrn  = Vehicle::Quat::Identity();
        Vehicle::Vec3                LinearVelocityMps   {0.0f, 0.0f, 0.0f};
        Vehicle::Vec3                AngularVelocityRps  {0.0f, 0.0f, 0.0f};
        Vehicle::Vec3                PendingForceN       {0.0f, 0.0f, 0.0f};
        float                        PeakImpulseNs       = 0.0f;
        bool                         KinematicTargeted   = false;
        bool                         AwakeCondition      = false;
        bool                         FracturedCondition  = false;
    };

    struct VehicleSlot
    {
        uint64_t                                             VehicleKey           = 0u;
        std::unique_ptr<Vehicle::VehicleJoltRig>             Rig                  {};
        std::array<bool, 4>                                  WheelEnclosed        {true, true, true, true};
        Vehicle::Vec3                                        PreviousChassisPos   {0.0f, 0.0f, 0.0f};
        Vehicle::Quat                                        PreviousChassisOrient= Vehicle::Quat::Identity();
        Vehicle::Vec3                                        CurrentChassisPos    {0.0f, 0.0f, 0.0f};
        Vehicle::Quat                                        CurrentChassisOrient = Vehicle::Quat::Identity();
        std::array<Vehicle::Vec3, 4>                         PreviousWheelPos     {};
        std::array<Vehicle::Quat, 4>                         PreviousWheelOrient  {};
        std::array<Vehicle::Vec3, 4>                         CurrentWheelPos      {};
        std::array<Vehicle::Quat, 4>                         CurrentWheelOrient   {};
        alignas(16) std::array<uint8_t, Vehicle::SceneVehicleProfileByteCount> EncodedGpuProfile{};
    };

    [[nodiscard]] Vehicle::Vec3 EvaluateZoneAirflowAt(const Vehicle::Vec3& WorldPoint,
                                                      Vehicle::Vec3&       OutDirectAcceleration) const noexcept;
    static void CaptureVehicleWheelWorldPoses(const Vehicle::VehicleTelemetry& Telemetry,
                                              std::array<Vehicle::Vec3, 4>&    OutPositions,
                                              std::array<Vehicle::Quat, 4>&    OutOrientations) noexcept;

    RigidBodySolver                        WorldSolver{};
    RigidBodyWorldSettings                 ConfiguredSettings{};
    std::vector<OccupantSlot>              OccupantRecords{};
    std::unordered_map<uint64_t, size_t>   OccupantIndexByKey{};
    std::vector<VehicleSlot>               VehicleRecords{};
    std::unordered_map<uint64_t, size_t>   VehicleIndexByKey{};
    std::vector<AdapterForceZone>          ForceZones{};
    uint64_t                               CompletedOuterTicks = 0u;
    uint64_t                               CompletedSubsteps   = 0u;
    float                                  SimulatedSeconds    = 0.0f;
    float                                  LatestOuterStepSec  = 1.0f / 60.0f;
    uint32_t                               LatestSubstepCount  = 4u;
    float                                  LatestTickWallMs    = 0.0f;
};

} // namespace Frontier
