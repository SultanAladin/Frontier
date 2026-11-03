/*==========================================================================================================================================
                                                              ATMOSPHERICFLOW.H
==========================================================================================================================================*/
// 🧩 Scene wind coefficients feeding the existing atmospheric force field, with explicit Z-up/Y-up conversion.

#pragma once

#include "Engine/PhysicalDynamics/ForceField.h"
#include "Engine/Scene/EngineConfigurations.h"
#include <array>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE FLOW
//------------------------------------------------------------------------------------------------------------------------

void                                ConfigureAtmosphericFlow(ForceFieldSpace               &Field,
                                                             const ForceFieldConfiguration &Controls,
                                                             const WindConfiguration       &Wind,
                                                             double                         ElapsedSeconds);
[[nodiscard]] std::array<double, 3> EvaluateWorldFlow(const ForceFieldSpace       &Field,
                                                      const std::array<double, 3> &Position,
                                                      bool                         Turbulence = true);

} // namespace Frontier
