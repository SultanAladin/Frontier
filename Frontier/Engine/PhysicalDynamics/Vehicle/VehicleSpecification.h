/*==========================================================================================================================================
                                                          VEHICLESPECIFICATION.H
==========================================================================================================================================*/
// 🧩 The authored vehicle: its geometry (mass, dimensions, struts, wheel sizes; VehicleGeometry.h) and every tunable of the solver
//    (powertrain, tyres, carcass, brakes, aids, aero, conditions; VehicleSolverConfiguration). A Vehicle record stores one; the
//    physics resolves it into a solver configuration (geometry stamped on, derived figures recomputed). One figure registry
//    lists every authored number with its section, key, label, unit and range, so the record's TOML and the vehicle inspector
//    read and write the same list and nothing can be edited that is not saved.

#pragma once

#include "VehicleGeometry.h"
#include "VehicleSolver.h"

#include <functional>
#include <string>
#include <vector>

namespace Frontier::Vehicle
{

struct VehicleSpecification
{
    std::string                Name = "Control vehicle";
    VehicleGeometry            Geometry{};
    VehicleSolverConfiguration Solver{};
};

// 📝 The ControlVehicle of the proving ground with the production driving layer and Frontier's additions on: thermal tyres with
//    their cold window, the gas law on the live carcass volume, the MF6.1 pressure terms of a Delft-Tyre 205/60 R15
//    (TNO_car205_60R15.tir), wear, the hydraulic brakes with ABS, traction control, fuel, engine fluids, charge air and
//    Ackermann steering. The carcass starts as the spring.
[[nodiscard]] VehicleSpecification ConstructControlVehicleSpecification();

// 📝 The solver configuration a specification stands for: its tunables, the geometry stamped on (wheel mounts, inertia,
//    calibrated struts, tyre size, aero points), and the figures derived from others (inverse inertias).
[[nodiscard]] VehicleSolverConfiguration ResolveSolverConfiguration(const VehicleSpecification& Specification);

//------------------------------------------------------------------------------------------------------------------------
//                                                    FIGURE REGISTRY
//------------------------------------------------------------------------------------------------------------------------

enum class VehicleFigureCategory : uint8_t
{
    Real,     // a number on a range
    Whole,    // a counted number
    Switch,   // on or off
    Choice,   // one of Choices (stored as its index)
};

// 📝 One authored number. Shown = stored × Display + Offset (radians as degrees, pascals as kilopascals, kelvin as °C).
struct VehicleFigure
{
    std::string              Section;    // TOML table under the record's configuration ("engine", "suspension.front" …)
    std::string              Key;        // TOML key
    std::string              Label;      // inspector label
    std::string              Unit;       // shown unit
    VehicleFigureCategory    Category = VehicleFigureCategory::Real;
    double                   Minimum  = 0.0, Maximum = 1.0;   // shown range
    double                   Display  = 1.0, Offset = 0.0;
    std::vector<std::string> Choices;
    std::function<double()>      Read;   // stored value
    std::function<void(double)>  Write;  // stored value
};

// 📝 One authored curve (points sorted by X).
struct VehicleCurve
{
    std::string Section, Key, Label, AxisX, AxisY;
    Curve*      Target = nullptr;
    double      MinimumX = 0.0, MaximumX = 1.0, MinimumY = 0.0, MaximumY = 1.0;
};

[[nodiscard]] std::vector<VehicleFigure> EnumerateVehicleFigures(VehicleSpecification& Specification);
[[nodiscard]] std::vector<VehicleCurve>  EnumerateVehicleCurves(VehicleSpecification& Specification);
// 📝 The forward gear ratios (1st … top), the reverse ratios and neutral kept as the solver lays them out.
[[nodiscard]] std::vector<float> QueryForwardRatios(const VehicleSpecification& Specification);
void AssignForwardRatios(VehicleSpecification& Specification, const std::vector<float>& Ratios);

} // namespace Frontier::Vehicle
