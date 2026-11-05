/*==========================================================================================================================================
                                                           VEHICLEDEPLOYMENT.H
==========================================================================================================================================*/
// 🧩 A vehicle's own deployment function: it asks PhysicsSpace where the ground is under its whole footprint, stands the chassis on it
//    with that surface's pitch and roll, and hands the solver the spawn pedal state the session asked for. Slope is never a special
//    case — a banked grid, a kerb, a hill and a flat studio floor go through the same survey and the same stand.

#pragma once

#include "Frontier/Engine/PhysicalDynamics/Vehicle/VehicleSpecification.h"
#include "Frontier/Engine/Session/SpawnPolicy.h"

#include <array>
#include <string>

namespace Frontier
{

class PhysicsSpace;

// 📝 What one deployment reports back: the stand the survey resolved, the brake state handed over, and the driven wheels fitted.
struct VehicleDeploymentReading
{
    SpawnPose            Stand{};                          // [-] the resolved surface stand
    ParkedBrakeState     ParkedBrakes{};                   // [-] the pedal state the session asked for at spawn
    std::array<bool, 4>  DrivesWheels{ false, false, true, true }; // [-] FL, FR, RL, RR
    float                ChassisCentreMetres = 0.0f;       // [m] the chassis body's centre height above the datum
    bool                 Supported           = false;      // [-] the surface stood the car
    std::string          Remark{};                         // [-] a one-line note for the register
};

// 📝 Surveys the ground under the footprint, resolves the stand, and returns it — nothing in the space is touched yet.
[[nodiscard]] VehicleDeploymentReading SurveyVehicleDeployment(PhysicsSpace&                       Space,
                                                               const Vehicle::VehicleGeometry&     Geometry,
                                                               const SpawnPolicy&                  Policy,
                                                               const std::array<double, 3>&        StartGroundPointMetres,
                                                               double                              StartYawDegrees);

// 📝 The stand expressed in the space's own frame: the chassis centre (centre of mass above the surface) and its quaternion.
struct VehicleStandTransform
{
    std::array<double, 3> PositionMetres{ 0.0, 0.0, 0.0 };
    std::array<double, 4> Orientation{ 0.0, 0.0, 0.0, 1.0 };
};

[[nodiscard]] VehicleStandTransform ResolveStandTransform(const SpawnPose&                Stand,
                                                          const Vehicle::VehicleGeometry& Geometry,
                                                          double                          CentreOfMassHeightMetres);

// 📝 Fits the spec's wheel mounts to the policy's driven-wheel rule (front, rear or all).
void FitDrivenWheels(Vehicle::VehicleSolverConfiguration& Configuration, DrivenWheelRule Rule);

} // namespace Frontier
