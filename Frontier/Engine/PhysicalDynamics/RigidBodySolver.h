/*==========================================================================================================================================
                                                             RIGIDBODYSOLVER.H
==========================================================================================================================================*/
// 🧩 Jolt Physics v5.2.0 rigid-body world, fixed-step accumulator, ray/sphere queries, and point-force coupling behind an opaque seam.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/VehicleMath.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 RIGID BODY CATEGORIES
//------------------------------------------------------------------------------------------------------------------------

enum class RigidBodyMotionCategory : uint8_t
{
    Static    = 0,   // [-] immovable collider in the static broad-phase tree
    Kinematic = 1,   // [-] prescribed pose, pushes dynamic bodies
    Dynamic   = 2    // [-] simulated mass, inertia, gravity, impulses, and contacts
};

enum class CollisionShapeCategory : uint8_t
{
    Plane       = 0,   // [-] static half-space bounded in broad-phase by PlaneHalfExtent
    Box         = 1,   // [-] oriented box with HalfExtents and optional CenterOfMassOffset
    Sphere      = 2,   // [-] sphere of Radius
    Capsule     = 3,   // [-] Z-aligned capsule of HalfHeight and Radius
    Cylinder    = 4,   // [-] Z-aligned cylinder of HalfHeight and Radius
    HeightField = 5    // [-] regular square height grid (SampleCount x SampleCount)
};

//------------------------------------------------------------------------------------------------------------------------
//                                               RIGID BODY SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

struct CollisionShapeSpecification
{
    CollisionShapeCategory Category           = CollisionShapeCategory::Box;
    Vehicle::Vec3          HalfExtents        {0.5f, 0.5f, 0.5f};   // [m] Box half dimensions
    float                  Radius             = 0.5f;               // [m] Sphere / Capsule / Cylinder radius
    float                  HalfHeight         = 0.5f;               // [m] Capsule / Cylinder half-length
    Vehicle::Vec3          PlaneNormal        {0.0f, 0.0f, 1.0f};   // [-] Plane outward unit normal
    float                  PlaneOffset        = 0.0f;               // [m] Plane signed distance along normal
    float                  PlaneHalfExtent    = 500.0f;             // [m] Plane broad-phase half-extent
    Vehicle::Vec3          ShapeLocalOffset   {0.0f, 0.0f, 0.0f};   // [m] optional shape offset relative to CoM
    uint32_t               HeightSampleCount  = 0u;                 // [-] HeightField side sample count (>= 2)
    Vehicle::Vec3          HeightOrigin       {0.0f, 0.0f, 0.0f};   // [m] HeightField world/local origin
    Vehicle::Vec3          HeightScale        {1.0f, 1.0f, 1.0f};   // [m] HeightField cell spacing (X, Y) and height scale (Z)
    std::vector<float>     HeightSamples      {};                   // [m] row-major Z heights (SampleCount * SampleCount)
};

struct RigidBodySpecification
{
    std::string                 Name                {};                                  // [-] diagnostic body label
    CollisionShapeSpecification Shape               {};                                  // [-] collision primitive
    RigidBodyMotionCategory     Motion              = RigidBodyMotionCategory::Dynamic;  // [-] motion domain
    Vehicle::Vec3               Position            {0.0f, 0.0f, 0.0f};                  // [m] world-space centre of mass
    Vehicle::Quat               Orientation         = Vehicle::Quat::Identity();         // [-] world-space orientation
    Vehicle::Vec3               LinearVelocity      {0.0f, 0.0f, 0.0f};                  // [m/s] initial linear velocity
    Vehicle::Vec3               AngularVelocity     {0.0f, 0.0f, 0.0f};                  // [rad/s] initial angular velocity
    float                       MassKilograms       = 0.0f;                              // [kg] 0 = shape volume * 1000 kg/m^3
    Vehicle::Vec3               InertiaDiagonal     {0.0f, 0.0f, 0.0f};                  // [kg*m^2] optional explicit principal inertia
    float                       Friction            = 0.6f;                              // [-] Coulomb surface friction
    float                       Restitution         = 0.1f;                              // [-] coefficient of restitution
    float                       LinearDamping       = 0.02f;                             // [1/s] linear velocity damping
    float                       AngularDamping      = 0.04f;                             // [1/s] angular velocity damping
    float                       GravityFactor       = 1.0f;                              // [-] multiplier on world gravity
    bool                        AllowSleeping       = true;                              // [-] permit island deactivation at rest
    bool                        ContinuousCollision = false;                             // [-] linear-cast CCD for fast bodies
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  RIGID BODY READINGS
//------------------------------------------------------------------------------------------------------------------------

using RigidBodyIdentity = uint32_t;
inline constexpr RigidBodyIdentity InvalidRigidBody = 0xFFFFFFFFu;

struct RigidBodyPose
{
    RigidBodyIdentity       Identity        = InvalidRigidBody;                  // [-] solver handle
    Vehicle::Vec3           Position        {0.0f, 0.0f, 0.0f};                  // [m] world-space centre of mass
    Vehicle::Quat           Orientation     = Vehicle::Quat::Identity();         // [-] world-space rotation
    Vehicle::Vec3           LinearVelocity  {0.0f, 0.0f, 0.0f};                  // [m/s] world-space velocity
    Vehicle::Vec3           AngularVelocity {0.0f, 0.0f, 0.0f};                  // [rad/s] world-space angular velocity
    RigidBodyMotionCategory Motion          = RigidBodyMotionCategory::Dynamic;  // [-] motion category
    CollisionShapeCategory  Shape           = CollisionShapeCategory::Box;       // [-] shape category
    bool                    Awake           = false;                             // [-] true while body is active in Jolt
};

struct RayCastReading
{
    bool              Hit      = false;                  // [-] true when ray intersected a collider
    RigidBodyIdentity Body     = InvalidRigidBody;       // [-] hit body handle
    float             Fraction = 1.0f;                   // [-] [0..1] fraction along ray segment
    Vehicle::Vec3     Position {0.0f, 0.0f, 0.0f};       // [m] world contact point
    Vehicle::Vec3     Normal   {0.0f, 0.0f, 1.0f};       // [-] unit outward surface normal
};

struct RigidBodyWorldSettings
{
    Vehicle::Vec3 Gravity                   {0.0f, 0.0f, -9.80665f};   // [m/s^2] +Z up gravity vector
    float         FixedStepSeconds          = 1.0f / 240.0f;           // [s] physics integration step
    uint32_t      MaxStepsPerAdvance        = 12u;                     // [-] spiral-of-death cap
    uint32_t      CollisionStepsPerUpdate   = 1u;                      // [-] Jolt collision substeps
    uint32_t      MaxBodies                 = 4096u;                   // [-] maximum resident bodies
    uint32_t      MaxBodyPairs              = 8192u;                   // [-] broad-phase pair pool
    uint32_t      MaxContactConstraints     = 8192u;                   // [-] contact manifold pool
    uint32_t      WorkerThreads             = 1u;                      // [-] Jolt worker pool threads
    uint32_t      TemporaryAllocationBytes  = 16u * 1024u * 1024u;     // [B] Jolt scratch arena
    float         SpeculativeContactDistance = 0.02f;                  // [m] speculative contact look-ahead
    float         PenetrationSlop           = 0.01f;                   // [m] permitted resting slop
    float         TimeBeforeSleepSeconds    = 0.5f;                    // [s] idle threshold before sleep
};

struct RigidBodyMetrics
{
    uint32_t BodyCount            = 0u;     // [-] resident bodies in world
    uint32_t ActiveBodyCount      = 0u;     // [-] awake dynamic/kinematic bodies
    uint64_t StepCount            = 0u;     // [-] fixed steps executed since BringUp
    uint32_t StepsLastAdvance     = 0u;     // [-] steps executed on most recent Advance
    float    LastStepMilliseconds = 0.0f;   // [ms] wall time of last step
    float    AccumulatorSeconds   = 0.0f;   // [s] residual unconsumed time
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   RIGID BODY SOLVER
//------------------------------------------------------------------------------------------------------------------------

class RigidBodySolver
{
  public:
    RigidBodySolver() noexcept;
    ~RigidBodySolver() noexcept;

    RigidBodySolver(const RigidBodySolver&)            = delete;
    RigidBodySolver& operator=(const RigidBodySolver&) = delete;

    bool        BringUp(const RigidBodyWorldSettings& Settings = {}) noexcept;
    void        Retire() noexcept;
    [[nodiscard]] bool Ready() const noexcept { return SolverReady; }

    [[nodiscard]] static const char* QueryBackendVersion() noexcept;
    [[nodiscard]] const std::string& QueryLastDiagnostic() const noexcept { return LastDiagnostic; }
    [[nodiscard]] RigidBodyMetrics   QueryMetrics() const noexcept { return Metrics; }

    RigidBodyIdentity CreateBody(const RigidBodySpecification& Specification) noexcept;
    void              DestroyBody(RigidBodyIdentity Identity) noexcept;
    void              DestroyAllBodies() noexcept;
    void              OptimizeBroadPhase() noexcept;

    // 📝 StepExactly advances one deterministic step of DeltaSeconds; Advance accumulates towards FixedStepSeconds.
    void StepExactly(float DeltaSeconds) noexcept;
    void Advance(float DeltaSeconds) noexcept;

    void ApplyForce(RigidBodyIdentity Identity, const Vehicle::Vec3& ForceNewtons) noexcept;
    void ApplyForceAtPoint(RigidBodyIdentity Identity, const Vehicle::Vec3& ForceNewtons, const Vehicle::Vec3& WorldPoint) noexcept;
    void ApplyTorque(RigidBodyIdentity Identity, const Vehicle::Vec3& TorqueNewtonMetres) noexcept;
    void ApplyImpulse(RigidBodyIdentity Identity, const Vehicle::Vec3& ImpulseNewtonSeconds) noexcept;
    void ApplyImpulseAtPoint(RigidBodyIdentity Identity, const Vehicle::Vec3& ImpulseNewtonSeconds, const Vehicle::Vec3& WorldPoint) noexcept;

    void AssignLinearVelocity(RigidBodyIdentity Identity, const Vehicle::Vec3& Velocity) noexcept;
    void AssignAngularVelocity(RigidBodyIdentity Identity, const Vehicle::Vec3& AngularVelocity) noexcept;
    void Teleport(RigidBodyIdentity Identity, const Vehicle::Vec3& Position, const Vehicle::Quat& Orientation) noexcept;
    void MoveKinematic(RigidBodyIdentity Identity, const Vehicle::Vec3& Position, const Vehicle::Quat& Orientation, float DeltaSeconds) noexcept;
    void Activate(RigidBodyIdentity Identity) noexcept;

    [[nodiscard]] bool           QueryPose(RigidBodyIdentity Identity, RigidBodyPose& OutPose) const noexcept;
    [[nodiscard]] RayCastReading CastRay(const Vehicle::Vec3& Origin, const Vehicle::Vec3& DirectionAndLength,
                                         RigidBodyIdentity IgnoredBody = InvalidRigidBody) const noexcept;

  private:
    struct JoltWorld;
    std::unique_ptr<JoltWorld> World{};
    RigidBodyWorldSettings     Settings{};
    RigidBodyMetrics           Metrics{};
    std::string                LastDiagnostic{};
    float                      Accumulator = 0.0f;
    bool                       SolverReady = false;
};

} // namespace Frontier
