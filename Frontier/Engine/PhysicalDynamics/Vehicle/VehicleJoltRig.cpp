/*==========================================================================================================================================
                                                            VEHICLEJOLTRIG.CPP
==========================================================================================================================================*/
// 🧩 Implementation of two-way Jolt Physics v5.2.0 <-> VehicleSolver coupling.

#include "Engine/PhysicalDynamics/Vehicle/VehicleJoltRig.h"

namespace Frontier::Vehicle
{

ChassisKinematicReading VehicleJoltRig::ReadChassisFromJolt(const RigidBodySolver& PhysicsWorld) const noexcept
{
    ChassisKinematicReading Out{};
    RigidBodyPose           Pose{};
    if (PhysicsWorld.QueryPose(ChassisBody, Pose))
    {
        Out.PositionWorld        = Pose.Position;
        Out.OrientationWorld     = Pose.Orientation;
        Out.LinearVelocityWorld  = Pose.LinearVelocity;
        Out.AngularVelocityWorld = Pose.AngularVelocity;
    }
    return Out;
}

bool VehicleJoltRig::Construct(RigidBodySolver& PhysicsWorld, const VehicleSolverConfiguration& Configuration,
                               const Vec3& InitialPositionWorld, const Quat& InitialOrientationWorld) noexcept
{
    if (!PhysicsWorld.Ready()) return false;

    RigidBodySpecification ChassisSpec{};
    ChassisSpec.Name                   = "ControlVehicleChassis";
    ChassisSpec.Motion                 = RigidBodyMotionCategory::Dynamic;
    ChassisSpec.Position               = InitialPositionWorld;
    ChassisSpec.Orientation            = InitialOrientationWorld;
    ChassisSpec.MassKilograms          = Configuration.Geometry.ChassisMassKg;
    ChassisSpec.InertiaDiagonal        = Configuration.Geometry.PrincipalInertiaKgm2();
    ChassisSpec.Friction               = 0.45f;
    ChassisSpec.Restitution            = 0.08f;
    ChassisSpec.LinearDamping          = 0.015f;
    ChassisSpec.AngularDamping         = 0.055f;
    ChassisSpec.AllowSleeping          = false;
    ChassisSpec.ContinuousCollision    = true;
    ChassisSpec.Shape.Category         = CollisionShapeCategory::Box;
    ChassisSpec.Shape.HalfExtents      = Configuration.Geometry.ChassisHalfExtentsMetres;
    ChassisSpec.Shape.ShapeLocalOffset = {0.0f, 0.0f, 0.14f};   // keep underbody box above suspension travel

    ChassisBody = PhysicsWorld.CreateBody(ChassisSpec);
    if (ChassisBody == InvalidRigidBody) return false;

    RigidBodySolver*          WorldPtr = &PhysicsWorld;
    VehicleSolver::WorldHooks Hooks{};
    Hooks.ReadChassis = [this, WorldPtr]() noexcept -> ChassisKinematicReading {
        return ReadChassisFromJolt(*WorldPtr);
    };
    Hooks.ApplyForceAtPoint = [this, WorldPtr](const Vec3& ForceN, const Vec3& PointW) noexcept {
        WorldPtr->ApplyForceAtPoint(ChassisBody, ForceN, PointW);
    };
    Hooks.ApplyTorque = [this, WorldPtr](const Vec3& TorqueNm) noexcept {
        WorldPtr->ApplyTorque(ChassisBody, TorqueNm);
    };
    Hooks.Ground = [this, WorldPtr](const Vec3& SampleWorld, Vec3& SurfaceWorld, Vec3& SurfaceNormal) noexcept -> bool {
        // Cast a downward probe from 0.65 m above SampleWorld down 1.60 m, ignoring the chassis itself.
        const Vec3           RayStart = SampleWorld + Vec3{0.0f, 0.0f, 0.65f};
        const RayCastReading Hit      = WorldPtr->CastRay(RayStart, Vec3{0.0f, 0.0f, -1.60f}, ChassisBody);
        if (!Hit.Hit) return false;
        SurfaceWorld  = Hit.Position;
        SurfaceNormal = Hit.Normal;
        return true;
    };

    Solver.Build(Configuration, Hooks, ReadChassisFromJolt(PhysicsWorld));
    return true;
}

void VehicleJoltRig::Reclaim(RigidBodySolver& PhysicsWorld) noexcept
{
    if (ChassisBody != InvalidRigidBody && PhysicsWorld.Ready())
    {
        PhysicsWorld.DestroyBody(ChassisBody);
    }
    ChassisBody = InvalidRigidBody;
}

void VehicleJoltRig::ApplySubstepForces(RigidBodySolver& PhysicsWorld, float DeltaSeconds) noexcept
{
    if (!PhysicsWorld.Ready() || ChassisBody == InvalidRigidBody) return;
    PhysicsWorld.Activate(ChassisBody);
    Solver.Step(DeltaSeconds);
}

void VehicleJoltRig::StepCoupled(RigidBodySolver& PhysicsWorld, float DeltaSeconds) noexcept
{
    if (!PhysicsWorld.Ready() || ChassisBody == InvalidRigidBody) return;
    ApplySubstepForces(PhysicsWorld, DeltaSeconds);
    PhysicsWorld.StepExactly(DeltaSeconds);
}

} // namespace Frontier::Vehicle
