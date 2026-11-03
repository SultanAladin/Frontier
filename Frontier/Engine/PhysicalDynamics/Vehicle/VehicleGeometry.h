/*==========================================================================================================================================
                                                             VEHICLEGEOMETRY.H
==========================================================================================================================================*/
// 🧩 Exact ProvingGround.facetspace ControlVehicle dimensions, hardpoints, inertia tensor, and cabin steering-column calibration.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/VehicleMath.h"

#include <array>

namespace Frontier::Vehicle
{

struct VehicleGeometrySpecification
{
    float ChassisMassKg               = 1300.0f;                   // [kg] sprung + powertrain vehicle mass
    Vec3  ChassisHalfExtentsMetres    {2.18f, 0.96f, 0.42f};       // [m] collision box half-extents
    Vec3  SpawnPositionWorld          {-40.0f, 0.0f, 0.55f};       // [m] ProvingGround.facetspace occupant[0] origin
    float CenterOfMassHeightOffsetM   = -0.02f;                    // [m] CoM offset relative to visual body origin

    // 📝 Hardpoints matching ProvingGround.facetspace Wheel FL/FR/RL/RR:
    //    FL = (+1.7274, +1.0475), FR = (+1.7274, -1.0475), RL = (-1.6686, +1.0475), RR = (-1.6686, -1.0475).
    std::array<Vec3, 4> StrutTopHardpointsBody{
        Vec3{+1.7274f, +1.0475f, +0.2950f},
        Vec3{+1.7274f, -1.0475f, +0.2950f},
        Vec3{-1.6686f, +1.0475f, +0.2950f},
        Vec3{-1.6686f, -1.0475f, +0.2950f}};

    float WheelOuterRadiusMetres      = 0.50855f;                  // [m] ControlVehicleTyre outer radius
    float WheelRimRadiusMetres        = 0.34480f;                  // [m] ControlVehicleTyre rim radius
    float WheelTreadWidthMetres       = 0.39580f;                  // [m] ControlVehicleTyre contact width
    float WheelInertiaKgm2            = 1.32f;                     // [kg*m^2] wheel + tyre + brake disc polar inertia

    // 📝 Exact cabin articulation anchors from EngineContent/EnvironmentArchives/ProvingGround/VehicleProfile.bin.
    Vec3  SteeringColumnPivotBody     {0.6800000f, 0.4200000f, 0.7071500f};
    Vec3  SteeringColumnAxisBody      {-0.9758974f, 0.0f, 0.2182296f};
    Vec3  CabinFrameAnchor            {0.3000000f, 0.0f, 0.0271500f};
    Vec3  CabinTabletCenter           {0.9672167f, 0.0f, 0.5128586f};

    [[nodiscard]] float WheelbaseMetres() const noexcept
    {
        return StrutTopHardpointsBody[0].x - StrutTopHardpointsBody[2].x;
    }

    [[nodiscard]] float FrontTrackMetres() const noexcept
    {
        return StrutTopHardpointsBody[0].y - StrutTopHardpointsBody[1].y;
    }

    [[nodiscard]] Vec3 PrincipalInertiaKgm2() const noexcept;
    [[nodiscard]] Vec3 InversePrincipalInertia() const noexcept;
};

} // namespace Frontier::Vehicle
