/*==========================================================================================================================================
                                                        TYRECARCASSVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The three tyre carcasses side by side: Spring (Project Drive's radial spring), XPBD (the lattice) and 3D FEM (Hex8 with the
//    inflation's initial stress). Each is loaded statically at four pressures and six deflections against the spring law it is
//    calibrated to; the soft models are checked for their own physics (the lattice's enclosed air and its rim reaction against the
//    road's push, the solid's unilateral contact, its symmetry and its rigid-motion invariance); each carries the whole car on its
//    Jolt chassis, settles and drives; and a step of each is timed.
//    Usage: TyreCarcassValidation <exhibit folder>  (writes Results/TyreCarcass.txt and Results/TyreCarcassCurves.csv)

#include "Frontier/Extensions/Physics/PhysicsExtension.h"

#include "Frontier/Engine/PhysicalDynamics/Vehicle/TraceContactTyre.h"
#include "Frontier/Engine/PhysicalDynamics/Vehicle/TyreCarcassLattice.h"
#include "Frontier/Engine/PhysicalDynamics/Vehicle/TyreCarcassSolid.h"
#include "Frontier/Engine/PhysicalDynamics/Vehicle/VehicleSpecification.h"
#include "Frontier/Engine/Scene/WorldSpace.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

using namespace Frontier;
namespace V = Frontier::Vehicle;

namespace
{

int                Passed = 0, Failed = 0;
std::ostringstream Log;

void Report(bool Success, const std::string& Line)
{
    (Success ? Passed : Failed)++;
    const std::string Text = std::string(Success ? "  🟢 " : "  🔴 ") + Line;
    std::printf("%s\n", Text.c_str());
    Log << Text << "\n";
}

void Heading(const char* Title)
{
    std::printf("\n%s\n", Title);
    Log << "\n" << Title << "\n";
}

std::string Format(const char* Pattern, ...)
{
    char    Text[512];
    va_list Arguments;
    va_start(Arguments, Pattern);
    std::vsnprintf(Text, sizeof(Text), Pattern, Arguments);
    va_end(Arguments);
    return Text;
}

constexpr float Radius = 0.50855f, Rim = 0.3446f, Width = 0.3958f;

V::CarcassContact Pressed(double Deflection, double Pressure)
{
    V::CarcassContact Contact;
    Contact.HubPosition   = { 0.0f, 0.0f, static_cast<float>(Radius - Deflection) };
    Contact.HubRotation   = { 0.0f, 0.0f, 0.0f, 1.0f };
    Contact.GroundPoint   = { 0.0f, 0.0f, 0.0f };
    Contact.GroundNormal  = { 0.0f, 0.0f, 1.0f };
    Contact.Ground        = true;
    Contact.GaugePressure = static_cast<float>(Pressure);
    Contact.Seconds       = 1.0f / 240.0f;
    return Contact;
}

// 📝 A carcass settled under a static hub: the mean of the last quarter of 240 steps.
double Settle(V::TyreCarcass& Carcass, const V::CarcassContact& Contact, V::CarcassResponse& Last)
{
    Carcass.Reset(Contact);
    double Sum = 0.0;
    int    Count = 0;
    for (int Step = 0; Step < 240; ++Step)
    {
        Last = Carcass.Advance(Contact);
        if (Step >= 180)
        {
            Sum += Last.NormalLoad;
            ++Count;
        }
    }
    return Sum / Count;
}

void ValidateCurves(const std::filesystem::path& Results)
{
    Heading("A · Load against deflection, all three carcasses (the spring law is the reference they are calibrated to)");
    const V::VehicleSpecification       Specification = V::ConstructControlVehicleSpecification();
    const V::VehicleSolverConfiguration Configuration = V::ResolveSolverConfiguration(Specification);
    V::TyreCarcassParameters            Tuning        = Configuration.CarcassTuning;
    std::ofstream Curves(Results / "TyreCarcassCurves.csv");
    Curves << "pressure_kpa,deflection_mm,spring_n,xpbd_n,fem_n,xpbd_volume_m3,fem_air_m3\n";
    const double Pressures[] = { 0.0, 55000.0, 110000.0, 220000.0 };
    const double Deflections[] = { 0.005, 0.010, 0.015, 0.020, 0.030, 0.040 };
    bool   Ordered = true;
    double Operating[2] = { 0.0, 0.0 };
    for (double Pressure : Pressures)
    {
        double Previous[2] = { 0.0, 0.0 };
        for (double Deflection : Deflections)
        {
            const double Spring = V::ResolveCarcassStiffness(Tuning, Pressure) * Deflection;
            V::TyreCarcassLattice Lattice(Tuning, Radius, Rim, Width);
            V::TyreCarcassSolid   Solid(Tuning, Radius, Rim, Width);
            V::CarcassResponse    LatticeLast, SolidLast;
            const double Xpbd = Settle(Lattice, Pressed(Deflection, Pressure), LatticeLast);
            const double Fem  = Settle(Solid, Pressed(Deflection, Pressure), SolidLast);
            Curves << Pressure * 0.001 << "," << Deflection * 1000.0 << "," << Spring << "," << Xpbd << "," << Fem << "," << LatticeLast.EnclosedVolume << ","
                   << SolidLast.EnclosedVolume << "\n";
            Ordered &= Xpbd > Previous[0] && Fem > Previous[1];
            Previous[0] = Xpbd;
            Previous[1] = Fem;
            if (Pressure == Tuning.ReferencePressure && Deflection == 0.015)
            {
                Operating[0] = Xpbd / Spring;
                Operating[1] = Fem / Spring;
            }
        }
    }
    Report(Ordered, "every curve rises with deflection (no softening branch inside 5 – 40 mm)");
    Report(std::fabs(Operating[0] - 1.0) < 0.1 && std::fabs(Operating[1] - 1.0) < 0.1,
           Format("at the car's operating point (15 mm, %.0f kPa) XPBD carries %.3f and FEM %.3f of the spring's load", Tuning.ReferencePressure * 0.001, Operating[0],
                  Operating[1]));
    // 📝 Pressure stiffens all three at the operating deflection.
    double Last[3] = { 0.0, 0.0, 0.0 };
    bool   Stiffer = true;
    for (double Pressure : Pressures)
    {
        V::TyreCarcassLattice Lattice(Tuning, Radius, Rim, Width);
        V::TyreCarcassSolid   Solid(Tuning, Radius, Rim, Width);
        V::CarcassResponse    Dummy;
        const double Loads[3] = { V::ResolveCarcassStiffness(Tuning, Pressure) * 0.015, Settle(Lattice, Pressed(0.015, Pressure), Dummy),
                                  Settle(Solid, Pressed(0.015, Pressure), Dummy) };
        for (int Category = 0; Category < 3; ++Category)
        {
            Stiffer &= Loads[Category] > Last[Category];
            Last[Category] = Loads[Category];
        }
    }
    Report(Stiffer, "more pressure, more load at the same deflection, for every model (0, 55, 110, 220 kPa)");
}

void ValidateLattice()
{
    Heading("B · XPBD lattice: its own physics");
    V::TyreCarcassParameters Tuning;
    V::TyreCarcassLattice    Lattice(Tuning, Radius, Rim, Width);
    V::CarcassResponse       Last;
    const double Load = Settle(Lattice, Pressed(0.02, 110000.0), Last);
    const double Change = Last.EnclosedVolume / Lattice.QueryRestVolume() - 1.0;
    Report(std::fabs(Change) < 0.02, Format("the enclosed air holds its volume under 20 mm: %.3f %% change (the air's bulk compliance)", Change * 100.0));
    // 📝 The road's push (positional corrections as impulses) against the spokes' reaction on the rim, at equilibrium.
    double Sum = 0.0;
    int    Count = 0;
    for (int Step = 0; Step < 60; ++Step)
    {
        Last = Lattice.Advance(Pressed(0.02, 110000.0));
        Sum += Last.NormalLoad;
        ++Count;
    }
    Report(std::fabs(Sum / Count / Load - 1.0) < 0.02 && Last.ContactCount >= 3,
           Format("the rim reaction is steady at rest (%.0f N, %u tread particles touching)", Sum / Count, Last.ContactCount));
    V::TyreCarcassLattice Free(Tuning, Radius, Rim, Width);
    V::CarcassContact     Airborne = Pressed(-0.05, 110000.0);
    V::CarcassResponse    Hanging;
    const double FreeLoad = Settle(Free, Airborne, Hanging);
    Report(FreeLoad < 1.0 && Hanging.ContactCount == 0, Format("off the road it carries nothing (%.3f N, no contact)", FreeLoad));

    V::TyreCarcassParameters SofterSpokes = Tuning;
    SofterSpokes.XpbdSpokeCompliance      = Tuning.XpbdSpokeCompliance * 3.0;
    V::TyreCarcassLattice SoftLattice(SofterSpokes, Radius, Rim, Width);
    V::CarcassResponse    SoftLast;
    const double          SoftLoad = Settle(SoftLattice, Pressed(0.02, 110000.0), SoftLast);
    Report(SoftLoad > 500.0 && SoftLoad < Load * 0.96,
           Format("authored XpbdSpokeCompliance ×3 softens the lattice load (%.0f N → %.0f N at 20 mm)", Load, SoftLoad));
}

void ValidateSolid()
{
    Heading("C · Hex8 finite elements: its own physics");
    V::TyreCarcassParameters Tuning;
    V::TyreCarcassSolid      Solid(Tuning, Radius, Rim, Width);
    Report(Solid.QueryReady(), "compliance tables built (material + initial-stress stiffness, 5 pressures, block-circulant)");
    V::CarcassResponse Last;
    (void)Settle(Solid, Pressed(0.025, 110000.0), Last);
    // 📝 Signorini: contact forces only push, touching nodes sit on the road, the others stay above it.
    double WorstGap = 0.0, Pulling = 0.0, Complement = 0.0, Mirror = 0.0;
    for (int Sector = 0; Sector < V::TyreCarcassSolid::SectorCount; ++Sector)
        for (int Ring = 0; Ring < V::TyreCarcassSolid::TreadRings; ++Ring)
        {
            const double Height = Solid.QueryTreadNode(Sector, Ring)[2] + (Radius - 0.025);
            const double Force  = Solid.QueryTreadForce(Sector, Ring);
            WorstGap   = std::min(WorstGap, Height);
            Pulling    = std::min(Pulling, Force);
            Complement = std::max(Complement, Force * std::max(0.0, Height));
            Mirror     = std::max(Mirror, std::fabs(Solid.QueryTreadForce(Sector, Ring) - Solid.QueryTreadForce(Sector, V::TyreCarcassSolid::TreadRings - 1 - Ring)));
        }
    Report(WorstGap > -2.0e-4 && Pulling >= 0.0 && Complement < 1.0e-2,
           Format("unilateral contact: deepest tread %.3g m into the road, no pulling force, λ · gap ≤ %.2g N·m", WorstGap, Complement));
    Report(Mirror < 1.0e-3 * std::max(1.0, static_cast<double>(Last.NormalLoad)),
           Format("left–right symmetric patch (largest shoulder force difference %.3g N under %.0f N)", Mirror, Last.NormalLoad));
    // 📝 Rigid motion: the same press seen from a moved and turned hub gives the same load (frame invariance of the solution).
    V::CarcassContact Moved = Pressed(0.025, 110000.0);
    const V::Quat     Turn = V::Quat::AxisAngle({ 0.0f, 0.0f, 1.0f }, 0.7f);
    Moved.HubPosition = Moved.HubPosition + V::Vec3{ 12.0f, -3.0f, 0.0f };
    Moved.GroundPoint = Moved.GroundPoint + V::Vec3{ 12.0f, -3.0f, 0.0f };
    Moved.HubRotation = Turn;
    V::TyreCarcassSolid Again(Tuning, Radius, Rim, Width);
    V::CarcassResponse  Shifted;
    const double Load = Settle(Again, Moved, Shifted);
    Report(std::fabs(Load / Last.NormalLoad - 1.0) < 1.0e-4, Format("moved 12 m and turned 40°, the same press carries %.2f N (%.2f N)", Load, Last.NormalLoad));
    // 📝 The patch follows the road under a rolling hub: the field's lowest point stays at the bottom whatever the spin.
    double Lowest = 0.0;
    for (float Spin : { 0.0f, 0.5f, 1.9f, 3.3f })
    {
        V::CarcassField Field;
        Solid.SampleField(Spin, Field);
        double Down = 0.0;
        for (uint32_t Point = 0; Point < V::CarcassFieldPoints; ++Point)
        {
            const float Material = 2.0f * 3.14159265f * static_cast<float>(Point / V::CarcassRingCount) / V::CarcassSectorCount;
            const float Wheel    = Material - Spin;
            if (std::fabs(std::sin(Wheel) + 1.0f) < 0.02f) Down = std::max(Down, static_cast<double>(std::hypot(Field[Point * 3], Field[Point * 3 + 2])));
        }
        Lowest = Spin == 0.0f ? Down : std::min(Lowest, Down);
    }
    Report(Lowest > 0.01, Format("the deformation stays at the bottom while the wheel turns (≥ %.1f mm displacement at the bottom at four spin angles)", Lowest * 1000.0));

    // 📝 Coulomb-bounded 3D Hex8 tangential traction: Pacejka lateral/longitudinal road traction shears the contact patch laterally and longitudinally.
    V::TyreCarcassParameters ShearTuning = Tuning;
    ShearTuning.Friction                 = 1.15;
    V::CarcassContact LateralTraction    = Pressed(0.025, 110000.0);
    LateralTraction.TangentialForce      = { -2400.0f, -3800.0f, 0.0f };
    V::TyreCarcassSolid ShearedSolid(ShearTuning, Radius, Rim, Width);
    V::CarcassResponse  ShearedResponse;
    (void)Settle(ShearedSolid, LateralTraction, ShearedResponse);
    V::CarcassField RestField{}, ShearedField{};
    Solid.SampleField(0.0f, RestField);
    ShearedSolid.SampleField(0.0f, ShearedField);
    double PeakLateralShear = 0.0, PeakForeAftShear = 0.0;
    for (uint32_t Point = 0; Point < V::CarcassFieldPoints; ++Point)
    {
        PeakForeAftShear = std::max(PeakForeAftShear, static_cast<double>(std::fabs(ShearedField[Point * 3 + 0] - RestField[Point * 3 + 0])));
        PeakLateralShear = std::max(PeakLateralShear, static_cast<double>(std::fabs(ShearedField[Point * 3 + 1] - RestField[Point * 3 + 1])));
    }
    // Verify Coulomb saturation: 2× and 20× the friction limit produce identical bounded shear.
    V::CarcassContact SaturatedA = Pressed(0.025, 110000.0), SaturatedB = Pressed(0.025, 110000.0);
    SaturatedA.TangentialForce = { 0.0f, -25000.0f, 0.0f };
    SaturatedB.TangentialForce = { 0.0f, -250000.0f, 0.0f };
    V::TyreCarcassSolid SatSolidA(ShearTuning, Radius, Rim, Width), SatSolidB(ShearTuning, Radius, Rim, Width);
    V::CarcassResponse  SatRespA, SatRespB;
    (void)Settle(SatSolidA, SaturatedA, SatRespA);
    (void)Settle(SatSolidB, SaturatedB, SatRespB);
    V::CarcassField SatFieldA{}, SatFieldB{};
    SatSolidA.SampleField(0.0f, SatFieldA);
    SatSolidB.SampleField(0.0f, SatFieldB);
    double SaturationDiff = 0.0;
    for (size_t Entry = 0; Entry < SatFieldA.size(); ++Entry)
        SaturationDiff = std::max(SaturationDiff, static_cast<double>(std::fabs(SatFieldA[Entry] - SatFieldB[Entry])));
    Report(PeakLateralShear > 0.0015 && PeakForeAftShear > 0.0010 && SaturationDiff < 1.0e-5,
           Format("3D Hex8 tangential shear: patch shifts %.1f mm laterally & %.1f mm longitudinally, Coulomb-clamped (Δ = %.2g m)",
                  PeakLateralShear * 1000.0, PeakForeAftShear * 1000.0, SaturationDiff));

    V::TyreCarcassParameters StifferRubber = Tuning;
    StifferRubber.FemYoungModulus          = Tuning.FemYoungModulus * 1.5;
    V::TyreCarcassSolid StiffSolid(StifferRubber, Radius, Rim, Width);
    V::CarcassResponse  StiffResponse;
    const double        StiffLoad = Settle(StiffSolid, Pressed(0.025, 110000.0), StiffResponse);
    Report(StiffLoad > Last.NormalLoad * 1.03,
           Format("authored FemYoungModulus ×1.5 stiffens 3D Hex8 load (%.0f N → %.0f N at 25 mm)", Last.NormalLoad, StiffLoad));
}

void ValidateCars(const std::filesystem::path& Results)
{
    Heading("D · The whole car on each carcass (Jolt chassis, 240 Hz)");
    std::ofstream Timing(Results / "TyreCarcassTiming.csv");
    Timing << "carcass,settled_deflection_mm,step_ms,speed_after_4s_mps\n";
    double SpringDeflection = 0.0;
    for (int Category = 0; Category < 3; ++Category)
    {
        WorldSpace        World;
        const RecordToken Token = World.ConstructRecord("Vehicle", "Control vehicle");
        auto* Record = dynamic_cast<VehicleRecord*>(World.FindRecord(Token));
        Record->AccessConfiguration().Spatial.PositionMetres[0] = 0.0f;
        Record->AccessConfiguration().Spatial.PositionMetres[1] = 0.0f;
        Record->AccessConfiguration().Specification.Solver.Carcass = static_cast<V::TyreCarcassCategory>(Category);
        PhysicsExtension Physics;
        PhysicsExtensionSettings Settings;
        Settings.WriteRegister = false;
        Physics.Begin(World, Settings);
        const auto T0 = std::chrono::steady_clock::now();
        Physics.AdvanceSteps(720);
        const double StepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count() / 720.0;
        const V::VehicleTelemetry Settled = Physics.QueryVehicle(0)->QuerySolver().Telemetry();
        double Deflection = 0.0;
        for (uint32_t Wheel = 0; Wheel < 4; ++Wheel) Deflection += Settled.Wheels[Wheel].TyreDeflection * 0.25;
        if (Category == 0) SpringDeflection = Deflection;
        Physics.AssignVehicleInput(PhysicsVehicleInput{ 0.7f, 0.0f, 0.0f, false });
        Physics.AdvanceSteps(960);
        const V::VehicleTelemetry Driven = Physics.QueryVehicle(0)->QuerySolver().Telemetry();
        bool Finite = std::isfinite(Driven.Chassis.Position.x) && std::isfinite(Driven.Chassis.Position.z);
        const char* Name = V::TyreCarcassName(static_cast<V::TyreCarcassCategory>(Category));
        Timing << Name << "," << Deflection * 1000.0 << "," << StepMs << "," << Driven.ForwardSpeed << "\n";
        Report(Finite && Settled.WheelsInContact == 4 && Settled.Chassis.LinearVelocity.Length() < 0.05f &&
                   std::fabs(Deflection / SpringDeflection - 1.0) < 0.25 && Driven.ForwardSpeed > 5.0f,
               Format("%s: settles at %.1f mm mean deflection (spring %.1f mm), drives to %.1f m/s in 4 s; %.3f ms per vehicle step", Name, Deflection * 1000.0,
                      SpringDeflection * 1000.0, Driven.ForwardSpeed, StepMs));
        Physics.End(World);
    }
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    const std::filesystem::path Exhibit = ArgumentCount > 1 ? std::filesystem::path(Arguments[1]) : std::filesystem::path("Exhibits/VehiclePhysics");
    const std::filesystem::path Results = Exhibit / "Results";
    std::filesystem::create_directories(Results);
    ValidateCurves(Results);
    ValidateLattice();
    ValidateSolid();
    ValidateCars(Results);
    const std::string Summary = Format("\n%s %d / %d assertions\n", Failed == 0 ? "🟢" : "🔴", Passed, Passed + Failed);
    std::printf("%s", Summary.c_str());
    Log << Summary;
    std::ofstream(Results / "TyreCarcass.txt") << Log.str();
    return Failed == 0 ? 0 : 1;
}
