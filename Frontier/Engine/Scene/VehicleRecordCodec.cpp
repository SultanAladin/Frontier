/*==========================================================================================================================================
                                                          VEHICLERECORDCODEC.CPP
==========================================================================================================================================*/
// 🧩 Vehicle and Tyre records to and from TOML through the specification's figure registry.

#include "Engine/Scene/VehicleRecordCodec.h"

#include <toml++/toml.hpp>

#include <cmath>
#include <sstream>
#include <string>

namespace Frontier
{

namespace
{

// 📝 "suspension.front" → the nested table, created on the way down.
toml::table& ResolveSection(toml::table& Root, const std::string& Dotted)
{
    toml::table*      Current = &Root;
    std::stringstream Parts(Dotted);
    std::string       Part;
    while (std::getline(Parts, Part, '.'))
    {
        if (!Current->contains(Part) || !(*Current)[Part].is_table()) Current->insert_or_assign(Part, toml::table{});
        Current = (*Current)[Part].as_table();
    }
    return *Current;
}

const toml::table* FindSection(const toml::table& Root, const std::string& Dotted)
{
    const toml::table* Current = &Root;
    std::stringstream  Parts(Dotted);
    std::string        Part;
    while (std::getline(Parts, Part, '.'))
    {
        const toml::node* Next = Current->get(Part);
        if (Next == nullptr || !Next->is_table()) return nullptr;
        Current = Next->as_table();
    }
    return Current;
}

double Rounded(double Scalar)
{
    if (Scalar == 0.0 || !std::isfinite(Scalar)) return Scalar;
    const double Scale = std::pow(10.0, 6 - static_cast<int>(std::ceil(std::log10(std::fabs(Scalar)))));
    return std::round(Scalar * Scale) / Scale;
}

void WriteTriple(toml::table& Table, const char* Key, const float Coordinates[3])
{
    toml::array Array;
    for (int Axis = 0; Axis < 3; ++Axis) Array.push_back(Rounded(Coordinates[Axis]));
    Table.insert_or_assign(Key, std::move(Array));
}

void ReadTriple(const toml::table& Table, const char* Key, float Coordinates[3])
{
    const toml::array* Array = Table[Key].as_array();
    if (Array == nullptr || Array->size() != 3) return;
    for (size_t Axis = 0; Axis < 3; ++Axis)
        if (auto Coordinate = (*Array)[Axis].value<double>()) Coordinates[Axis] = static_cast<float>(*Coordinate);
}

} // namespace

void EncodeVehicleConfiguration(const VehicleConfiguration& Configuration, toml::table& OutTable)
{
    OutTable.insert_or_assign("visible", Configuration.Visible);
    OutTable.insert_or_assign("physics", Configuration.Physics);
    OutTable.insert_or_assign("possessed", Configuration.Possessed);
    OutTable.insert_or_assign("chase_camera", Configuration.ChaseCamera);
    OutTable.insert_or_assign("threaded_physics", Configuration.ThreadedPhysics);
    OutTable.insert_or_assign("physics_step_hz", Rounded(Configuration.PhysicsStepHz));
    OutTable.insert_or_assign("collision_steps", static_cast<int64_t>(Configuration.CollisionSteps));
    OutTable.insert_or_assign("solver_substeps", static_cast<int64_t>(Configuration.SolverSubsteps));
    OutTable.insert_or_assign("group", Configuration.GroupName);
    OutTable.insert_or_assign("vehicle_name", Configuration.Specification.Name);
    WriteTriple(OutTable, "position_m", Configuration.Spatial.PositionMetres);
    WriteTriple(OutTable, "orientation_deg", Configuration.Spatial.OrientationDegrees);
    // 📝 The registry reads through accessors bound to a specification it may also write, so it walks a copy.
    Vehicle::VehicleSpecification Specification = Configuration.Specification;
    for (const Vehicle::VehicleFigure& Figure : Vehicle::EnumerateVehicleFigures(Specification))
    {
        toml::table& Section = ResolveSection(OutTable, Figure.Section);
        const double Stored  = Figure.Read();
        switch (Figure.Category)
        {
            case Vehicle::VehicleFigureCategory::Switch: Section.insert_or_assign(Figure.Key, Stored > 0.5); break;
            case Vehicle::VehicleFigureCategory::Whole:
            case Vehicle::VehicleFigureCategory::Choice: Section.insert_or_assign(Figure.Key, static_cast<int64_t>(std::llround(Stored))); break;
            case Vehicle::VehicleFigureCategory::Real:   Section.insert_or_assign(Figure.Key, Rounded(Stored)); break;
        }
    }
    for (const Vehicle::VehicleCurve& Entry : Vehicle::EnumerateVehicleCurves(Specification))
    {
        toml::array Points;
        for (size_t Index = 0; Index < Entry.Target->X.size() && Index < Entry.Target->Y.size(); ++Index)
            Points.push_back(toml::array{ Rounded(Entry.Target->X[Index]), Rounded(Entry.Target->Y[Index]) });
        ResolveSection(OutTable, Entry.Section).insert_or_assign(Entry.Key, std::move(Points));
    }
    toml::array Ratios;
    for (const float Ratio : Vehicle::QueryForwardRatios(Specification)) Ratios.push_back(Rounded(Ratio));
    ResolveSection(OutTable, "gearbox").insert_or_assign("ratios", std::move(Ratios));
}

void DecodeVehicleConfiguration(const toml::table& InTable, VehicleConfiguration& Configuration)
{
    Configuration.Visible         = InTable["visible"].value_or(Configuration.Visible);
    Configuration.Physics         = InTable["physics"].value_or(Configuration.Physics);
    Configuration.Possessed       = InTable["possessed"].value_or(Configuration.Possessed);
    Configuration.ChaseCamera     = InTable["chase_camera"].value_or(Configuration.ChaseCamera);
    Configuration.ThreadedPhysics = InTable["threaded_physics"].value_or(Configuration.ThreadedPhysics);
    Configuration.PhysicsStepHz   = static_cast<float>(InTable["physics_step_hz"].value_or(static_cast<double>(Configuration.PhysicsStepHz)));
    Configuration.CollisionSteps  = static_cast<int>(InTable["collision_steps"].value_or(static_cast<int64_t>(Configuration.CollisionSteps)));
    Configuration.SolverSubsteps  = static_cast<int>(InTable["solver_substeps"].value_or(static_cast<int64_t>(Configuration.SolverSubsteps)));
    Configuration.GroupName       = InTable["group"].value_or(Configuration.GroupName);
    Configuration.Specification.Name = InTable["vehicle_name"].value_or(Configuration.Specification.Name);
    ReadTriple(InTable, "position_m", Configuration.Spatial.PositionMetres);
    ReadTriple(InTable, "orientation_deg", Configuration.Spatial.OrientationDegrees);
    for (const Vehicle::VehicleFigure& Figure : Vehicle::EnumerateVehicleFigures(Configuration.Specification))
    {
        const toml::table* Section = FindSection(InTable, Figure.Section);
        if (Section == nullptr) continue;
        const toml::node* StoredEntry = Section->get(Figure.Key);
        if (StoredEntry == nullptr && std::string(Figure.Section) == "carcass" && std::string(Figure.Key) == "formulation")
            StoredEntry = Section->get("model");
        if (StoredEntry == nullptr) continue;
        if (auto SwitchReading = StoredEntry->value<bool>(); SwitchReading && Figure.Category == Vehicle::VehicleFigureCategory::Switch)
            Figure.Write(*SwitchReading ? 1.0 : 0.0);
        else if (auto Number = StoredEntry->value<double>()) Figure.Write(*Number);
    }
    for (const Vehicle::VehicleCurve& Entry : Vehicle::EnumerateVehicleCurves(Configuration.Specification))
    {
        const toml::table* Section = FindSection(InTable, Entry.Section);
        const toml::array* Samples = Section != nullptr ? (*Section)[Entry.Key].as_array() : nullptr;
        if (Samples == nullptr || Samples->size() < 2) continue;
        Vehicle::Curve Loaded;
        for (const toml::node& Sample : *Samples)
        {
            const toml::array* Pair = Sample.as_array();
            if (Pair == nullptr || Pair->size() != 2) continue;
            const auto X = (*Pair)[0].value<double>(), Y = (*Pair)[1].value<double>();
            if (X && Y && (Loaded.X.empty() || *X > Loaded.X.back())) Loaded.Add(static_cast<float>(*X), static_cast<float>(*Y));
        }
        if (Loaded.X.size() >= 2) *Entry.Target = Loaded;
    }
    if (const toml::table* Gearbox = FindSection(InTable, "gearbox"))
        if (const toml::array* Ratios = (*Gearbox)["ratios"].as_array())
        {
            std::vector<float> Forward;
            for (const toml::node& Ratio : *Ratios)
                if (auto Scalar = Ratio.value<double>()) Forward.push_back(static_cast<float>(*Scalar));
            if (!Forward.empty()) Vehicle::AssignForwardRatios(Configuration.Specification, Forward);
        }
    Configuration.AdvanceRevision();
}

void EncodeTyreConfiguration(const TyreConfiguration& Configuration, toml::table& OutTable)
{
    OutTable.insert_or_assign("corner", static_cast<int64_t>(Configuration.Corner));
    OutTable.insert_or_assign("enclosed", Configuration.Enclosed);
    OutTable.insert_or_assign("compound", Configuration.Compound);
    OutTable.insert_or_assign("cold_pressure_pa", Rounded(Configuration.ColdPressure));
    OutTable.insert_or_assign("set", Configuration.SetName);
}

void DecodeTyreConfiguration(const toml::table& InTable, TyreConfiguration& Configuration)
{
    Configuration.Corner       = static_cast<uint32_t>(InTable["corner"].value_or(static_cast<int64_t>(Configuration.Corner)) & 3);
    Configuration.Enclosed     = InTable["enclosed"].value_or(Configuration.Enclosed);
    Configuration.Compound     = InTable["compound"].value_or(Configuration.Compound);
    Configuration.ColdPressure = static_cast<float>(InTable["cold_pressure_pa"].value_or(static_cast<double>(Configuration.ColdPressure)));
    Configuration.SetName      = InTable["set"].value_or(Configuration.SetName);
    Configuration.AdvanceRevision();
}

} // namespace Frontier
