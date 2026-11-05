/*==========================================================================================================================================
                                                             PHYSICSVEHICLE.H
==========================================================================================================================================*/
// 🧩 A vehicle in the PhysicsSpace: Project Drive's solver hosted on a Jolt chassis. The chassis is a moving body with the sprung
//    mass and the authored inertia about the centre of mass (a hull of the ControlVehicle's body), so walls, cones and other
//    bodies meet it; the wheels are the solver's own (its contact fan and soft carcasses ask the space where the road is by ray).
//    Each fixed step the solver reads the chassis, applies strut, tyre and aero forces at their points, and the space integrates.
//    Runs on the physics thread; the view it fills is what the editor draws (chassis, wheels, tyre deformation, telemetry).

#pragma once

#include "Frontier/Extensions/Physics/PhysicsSpace.h"

#include "Frontier/Engine/PhysicalDynamics/Vehicle/VehicleSpecification.h"
#include "Frontier/Engine/Scene/RecordToken.h"
#include "Frontier/Engine/Session/SpawnPolicy.h"
#include "Frontier/Extensions/Physics/VehicleDeployment.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace Frontier
{

// 📝 Driver controls for one fixed step.
struct PhysicsVehicleInput
{
    float Throttle  = 0.0f;   // [0, 1]
    float Brake     = 0.0f;   // [0, 1]
    float Steer     = 0.0f;   // [−1, 1] +1 turns left
    bool  Handbrake = false;
};

// 📝 One vehicle as a refresh sees it. Transforms are column-major 4 × 4 (world from local): the body in the ControlVehicle's
//    authored axes (+x nose, +y left, +z up, origin where the model's is), each wheel in its material axes (hub, steer, spin).
struct PhysicsVehicleView
{
    RecordToken                              Record{};
    Vehicle::VehicleTelemetry                Telemetry{};
    std::array<float, 16>                    Body{};
    std::array<std::array<float, 16>, 4>     Wheels{};
    std::array<Vehicle::CarcassField, 4>     Fields{};
    std::array<bool, 4>                      FieldValid{};
    std::array<float, 4>                     ContactAngle{};    // [rad] where the road meets the tyre, material axes (atan2 z, x)
    std::array<float, 4>                     Deflection{};      // [m]
    std::array<float, 4>                     GaugePressure{};   // [Pa]
    std::array<float, 4>                     LateralShift{};    // [m] the analytic profile's patch shear
    PhysicsVehicleInput                      Input{};
};

class PhysicsVehicle
{
  public:
    PhysicsVehicle();
    ~PhysicsVehicle();

    // 📝 Adds the chassis body at Start (ground point under the centre of mass, yaw from OrientationDegrees[2]) and builds the
    //    solver. Returns false (with Error) when the space refuses the body.
    bool Construct(PhysicsSpace& Space, RecordToken Record, const Vehicle::VehicleSpecification& Specification,
                   const Vehicle::VehicleSolverConfiguration& Configuration, const std::array<double, 3>& StartMetres, double StartYawDegrees,
                   std::string* Error = nullptr);

    // 📝 The same stand with the session's SpawnPolicy: the footprint survey, the surface's pitch and roll, the spawn brake rules, and the
    //    driven wheels all come from the deployment instead of the level's authored Z.
    bool Construct(PhysicsSpace& Space, RecordToken Record, const Vehicle::VehicleSpecification& Specification,
                   const Vehicle::VehicleSolverConfiguration& Configuration, const std::array<double, 3>& StartMetres, double StartYawDegrees,
                   const SpawnPolicy& DeploymentPolicy, std::string* Error = nullptr);

    void AssignInput(const PhysicsVehicleInput& Input) noexcept { Controls = Input; }
    // 📝 The solver's step: reads the chassis, applies its forces to the body; the space's step follows.
    void Step(double Seconds);
    // 📝 Back to the start, at rest, the solver rebuilt.
    void Reset();
    void ReadView(PhysicsVehicleView& View) const;

    [[nodiscard]] uint32_t                       QueryBody() const noexcept { return Body; }
    [[nodiscard]] RecordToken                    QueryRecord() const noexcept { return Record; }
    [[nodiscard]] const Vehicle::VehicleSolver&  QuerySolver() const noexcept { return *Solver; }
    [[nodiscard]] Vehicle::ChassisKinematics     ReadChassis() const;
    [[nodiscard]] double                         QueryAuthoredOriginBelowCentre() const noexcept { return OriginBelowCentre; }
    [[nodiscard]] const VehicleDeploymentReading& QueryDeployment() const noexcept { return Deployment; }
    [[nodiscard]] const SpawnPose&               QueryDeployedStand() const noexcept { return Deployed; }
    [[nodiscard]] bool                           IsHandbrakeLatched() const noexcept { return HandbrakeLatched; }

  private:
    void BuildSolver(const Vehicle::ChassisKinematics& Start);
    [[nodiscard]] bool QueryGround(const Vehicle::Vec3& Point, Vehicle::Vec3& Surface, Vehicle::Vec3& Normal) const;

    PhysicsSpace*                            Space = nullptr;
    RecordToken                              Record{};
    uint32_t                                 Body = PhysicsNoBody;
    Vehicle::VehicleSolverConfiguration      Configuration{};
    std::unique_ptr<Vehicle::VehicleSolver>  Solver;
    Vehicle::ChassisKinematics               Start{};
    PhysicsVehicleInput                      Controls{};
    double                                   OriginBelowCentre = 0.0;   // [m] the authored model's origin below the centre of mass
    float                                    LastMass = 0.0f;           // [kg] chassis mass the body last carried (fuel burns)

    VehicleDeploymentReading                 Deployment{};              // [-] the footprint survey, stand and spawn brake state
    SpawnPose                                Deployed{};                // [-] the stand the survey resolved
    SpawnPolicy                              SpawnRules{};              // [-] brake rules and driven wheels the session asked for
    ParkedBrakeState                         ParkedBrakes{};            // [-] the pedal state at handover
    float                                    SecondsSinceDeployment = 0.0f; // [s] spawn rules outrank the pedals for this long
    bool                                     HandbrakeLatched = false;  // [-] a parked car stays parked until its driver asks for drive
    float                                    ParkedPedalShare = 0.0f;   // [-] foot brake held while parked
};

} // namespace Frontier
