/*==========================================================================================================================================
                                                             VEHICLEJOLTRIG.H
==========================================================================================================================================*/
// 🧩 Couples VehicleSolver to RigidBodySolver (Jolt v5.2.0): dynamic chassis body, raycast ground query, and SceneViewport relocation.

#pragma once

#include "Engine/PhysicalDynamics/RigidBodySolver.h"
#include "Engine/PhysicalDynamics/Vehicle/VehicleSolver.h"

namespace Frontier::Vehicle
{

class VehicleJoltRig
{
  public:
    VehicleJoltRig() noexcept = default;

    bool Construct(RigidBodySolver& PhysicsWorld, const VehicleSolverConfiguration& Configuration,
                   const Vec3& InitialPositionWorld, const Quat& InitialOrientationWorld = Quat::Identity()) noexcept;
    void Reclaim(RigidBodySolver& PhysicsWorld) noexcept;

    void ApplySubstepForces(RigidBodySolver& PhysicsWorld, float DeltaSeconds) noexcept;
    void StepCoupled(RigidBodySolver& PhysicsWorld, float DeltaSeconds) noexcept;

    [[nodiscard]] RigidBodyIdentity        QueryChassisBody() const noexcept { return ChassisBody; }
    [[nodiscard]] VehicleSolver&           AccessVehicle() noexcept { return Solver; }
    [[nodiscard]] const VehicleSolver&     QueryVehicle() const noexcept { return Solver; }
    [[nodiscard]] ChassisKinematicReading  ReadChassisFromJolt(const RigidBodySolver& PhysicsWorld) const noexcept;

  private:
    RigidBodyIdentity ChassisBody = InvalidRigidBody;
    VehicleSolver     Solver{};
};

} // namespace Frontier::Vehicle
