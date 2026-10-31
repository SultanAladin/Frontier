/*==========================================================================================================================================
                                                     CLOUDDIAGNOSTIC.H
==========================================================================================================================================*/
// 🧩 CPU density diagnostic ported from VehicleHMI's CloudField.js.
#pragma once
#include "Experimental/EnvironmentInspectors/EnvironmentInspectors.h"
#include <vector>
#include <cstdint>
namespace Frontier::Experimental
{
float CloudProfile(float Height, int Type, float Anvil);
float CloudDensity(const CloudSettings& Settings, float X, float Y, float Z);
std::vector<uint32_t> CloudAtlas(const CloudSettings& Settings, int Width, int Height, float Aspect);
}
