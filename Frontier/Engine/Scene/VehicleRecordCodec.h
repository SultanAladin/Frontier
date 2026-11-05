/*==========================================================================================================================================
                                                           VEHICLERECORDCODEC.H
==========================================================================================================================================*/
// 🧩 The Vehicle and Tyre records in the levelspace: placement and switches, then every figure of the specification in its
//    section table (the VehicleSpecification registry), the authored curves as [x, y] pairs and the forward gear ratios.
//    Figures absent from a file keep the Control vehicle's defaults, so an old level loads with today's vehicle.

#pragma once

#include "Engine/Scene/EngineConfigurations.h"

namespace toml
{
inline namespace v3
{
class table;
}
} // namespace toml

namespace Frontier
{

void EncodeVehicleConfiguration(const VehicleConfiguration& Configuration, toml::table& OutTable);
void DecodeVehicleConfiguration(const toml::table& InTable, VehicleConfiguration& Configuration);
void EncodeTyreConfiguration(const TyreConfiguration& Configuration, toml::table& OutTable);
void DecodeTyreConfiguration(const toml::table& InTable, TyreConfiguration& Configuration);

} // namespace Frontier
