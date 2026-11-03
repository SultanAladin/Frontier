/*==========================================================================================================================================
                                                         VEHICLESCENEPUBLISHER.H
==========================================================================================================================================*/
// 🧩 Publishes VehicleSolver chassis/wheel transforms and 10,448-byte soft-body VehicleProfile into SceneViewport / IlluminationHost.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/VehicleSolver.h"
#include "Engine/Rendering/SceneViewport/SceneViewport.h"

namespace Frontier::Vehicle
{

void PublishVehicleToHost(GlobalIllumination::IlluminationHost& Host, const VehicleSolver& Solver) noexcept;
void PublishVehicleToViewport(GlobalIllumination::SceneViewport& Viewport, const VehicleSolver& Solver) noexcept;

} // namespace Frontier::Vehicle
