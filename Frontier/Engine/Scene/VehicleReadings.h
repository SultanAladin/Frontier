/*==========================================================================================================================================
                                                            VEHICLEREADINGS.H
==========================================================================================================================================*/
// 🧩 The live readings of every simulated vehicle, by record: the physics extension publishes each refresh's snapshot, the vehicle
//    and tyre inspectors and the telemetry dock read it. Read-only for the readers; nothing here ever writes a record.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/VehicleSolver.h"
#include "Engine/Scene/RecordToken.h"

#include <array>
#include <cstdint>

namespace Frontier
{

struct VehicleReading
{
    bool                                    Simulating       = false;
    Vehicle::VehicleTelemetry               Telemetry{};
    double                                  SimulatedSeconds = 0.0;   // [s]
    uint64_t                                Steps            = 0;     // [-] fixed physics steps
    double                                  StepHz           = 0.0;   // [Hz]
    double                                  StepMicroseconds = 0.0;   // [µs] mean wall time of a physics step
    double                                  RealtimeRatio    = 1.0;   // [-]
    std::array<float, 4>                    Throttle{};               // [-] driver input as the physics took it: throttle, brake, steer, handbrake
    Vehicle::TyreCarcassCategory            Carcass          = Vehicle::TyreCarcassCategory::Spring;
    bool                                    VisualValid      = false; // [-] BodyTransform and WheelTransforms hold live matrices
    std::array<float, 16>                   BodyTransform{};          // [-] column-major world-from-local of ControlVehicle body
    std::array<std::array<float, 16>, 4>    WheelTransforms{};        // [-] column-major world-from-local of FL, FR, RL, RR wheels
    std::array<Vehicle::CarcassField, 4>    CornerFields{};           // [-] 32 × 5 × 3 nodal displacements (XPBD and 3D FEM)
    std::array<bool, 4>                     CornerFieldValid{};       // [-] true when CornerFields[w] holds a soft-body field
    std::array<bool, 4>                     CornerFitted{ true, true, true, true };
    std::array<float, 4>                    ContactAngle{};           // [rad] road contact angle in spinning material axes
    std::array<float, 4>                    Deflection{};             // [m]   radial contact compression
    std::array<float, 4>                    GaugePressure{};          // [Pa]  live corner gauge pressure
    std::array<float, 4>                    LateralShift{};           // [m]   analytical spring patch lateral shear
    float                                   SteeringColumnAngle = 0.0f; // [rad] steering wheel rotation about steering column
};

void PublishVehicleReading(RecordToken Vehicle, const VehicleReading& Reading);
[[nodiscard]] bool QueryVehicleReading(RecordToken Vehicle, VehicleReading& Reading);
void ClearVehicleReadings();

} // namespace Frontier
