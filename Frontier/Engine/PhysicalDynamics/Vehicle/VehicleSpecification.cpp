/*==========================================================================================================================================
                                                         VEHICLESPECIFICATION.CPP
==========================================================================================================================================*/
// 🧩 The Control vehicle's defaults, the resolution into a solver configuration, and the registry of every authored figure.

#include "VehicleSpecification.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Vehicle
{

namespace
{

constexpr double Degrees  = 57.29577951308232;   // shown per radian
constexpr double Kilo     = 0.001;               // kPa per Pa
constexpr double Celsius  = -273.15;             // °C offset from K

} // namespace

VehicleSpecification ConstructControlVehicleSpecification()
{
    VehicleSpecification Specification;
    VehicleSolverConfiguration& Solver = Specification.Solver;
    Solver.ActiveScheme        = DrivingScheme::PacejkaDrivetrain;
    Solver.SlipSolverSelection = SlipSolver::RelaxationLength;
    // 📝 Tyres: thermal state with its cold window, the gas law on the live volume, the MF6.1 pressure terms of TNO's 205/60 R15
    //    (Delft-Tyre MF-Tyre 6.2, PPX1–4, PPY1, PPY3–4, PPZ1; its PPY2 shapes a load curve this formula does not have).
    Solver.TyreThermal.Enabled         = true;
    Solver.TyreThermal.ColdGripEnabled = true;
    Solver.PressureSensitiveTyre       = true;
    Solver.TyrePacejka.NominalPressure = Solver.Tyre.InflationPressure;
    Solver.TyrePacejka.ppx1 = -0.3489f; Solver.TyrePacejka.ppx2 = 0.382f;
    Solver.TyrePacejka.ppx3 = -0.09634f; Solver.TyrePacejka.ppx4 = 0.06447f;
    Solver.TyrePacejka.ppy1 = -0.6249f; Solver.TyrePacejka.ppy2 = 0.0f;
    Solver.TyrePacejka.ppy3 = -0.1668f; Solver.TyrePacejka.ppy4 = 0.2834f;
    Solver.TyrePacejka.ppz1 = -0.4402f;
    // 📝 Load sensitivity above Fz0 only, and MF6.1's combined-slip weighting (VehicleDynamicsValidation, section A).
    Solver.TyrePacejka.LoadSensitivityAboveReference = true;
    Solver.TyrePacejka.CombinedSlipMf61              = true;
    Solver.TyreWear.Enabled = true;
    // 📝 The source's turbo presets cannot spool: the friction (2e-3·ω + 5e-5·ω²) absorbs ~100 kW by 12k rpm, and the
    //    0.02 kg·m² shaft inertia is ~100× a GT-R-class cartridge, so boost never passes 0.1 bar. Physical presets (a few
    //    kW of friction at 100k rpm, 2e-4 kg·m²) let it spool in ~1 s and regulate on the wastegate (section C1b).
    Solver.Turbo.TurboFrictionCoeff     = 5.0e-5f;
    Solver.Turbo.TurboFrictionQuadratic = 1.0e-9f;
    Solver.Turbo.TurboInertia           = 2.0e-4f;
    // 📝 Cycled ABS (dump fast, reapply in small steps, hold inside the band) instead of the hunting proportional loop.
    //    The dump is solenoid-fast (~5 ms: these brakes lock a wheel in ~10 ms past the peak) while the reapply creeps at
    //    ~3 %/step — a coarse reapply overshoots the narrow stable band and lock-recovery wastes the stop (F6).
    Solver.Abs.DumpRate = 200.0f;
    Solver.Abs.ReapplyRate = 2.0f;
    // 📝 Motorsport-modulator hydraulics to match: the stock line slew (500/800 bar/s) takes ~50 ms to move 40 bar, so the
    //    caliper could never follow the modulator — every cycle sank into a deep lock and its recovery wasted the stop.
    //    A GT3 dump valve opens the caliper to the reservoir in ~10 ms and the pump rebuilds in ~20 ms (F6).
    Solver.Brakes.PressureRiseRate = 200.0e6f;
    Solver.Brakes.PressureFallRate = 400.0e6f;
    // 📝 Brakes, aids and the engine's conditions.
    Solver.BrakeThermalEnabled     = true;
    Solver.Abs.Enabled             = true;
    Solver.TractionControl.Enabled    = true;
    Solver.TractionControl.SlipTarget = 0.25f;   // [-] just under the sheet's longitudinal peak (κ ≈ 0.30 at the static load)
    Solver.Fuel.Enabled            = true;
    Solver.Fluids.Enabled          = true;
    Solver.ChargeAir.Enabled       = true;
    Solver.AckermannShare          = 0.8f;
    Solver.Carcass                 = TyreCarcassCategory::Spring;
    return Specification;
}

VehicleSolverConfiguration ResolveSolverConfiguration(const VehicleSpecification& Specification)
{
    VehicleSolverConfiguration Configuration = Specification.Solver;
    ApplyGeometry(Configuration, Specification.Geometry);
    // 📝 Figures the solver reads as reciprocals or references, kept in step with the authored ones.
    Configuration.Engine.InvEngineInertia = 1.0f / std::max(1.0e-3f, Configuration.Engine.EngineInertia);
    Configuration.Turbo.InvTurboInertia   = 1.0f / std::max(1.0e-4f, Configuration.Turbo.TurboInertia);
    Configuration.CarcassTuning.ReferencePressure  = Configuration.TraceContact.ReferencePressure;
    Configuration.CarcassTuning.ReferenceStiffness = Configuration.TraceContact.VerticalStiffness;
    Configuration.CarcassTuning.PressureShare      = Configuration.TraceContact.PressureShare;
    if (!(Configuration.TyrePacejka.NominalPressure > 1.0f)) Configuration.TyrePacejka.NominalPressure = Configuration.Tyre.InflationPressure;
    return Configuration;
}

std::vector<float> QueryForwardRatios(const VehicleSpecification& Specification)
{
    const TransmissionParameters& Gearbox = Specification.Solver.Transmission;
    std::vector<float>            Forward;
    for (size_t Index = static_cast<size_t>(Gearbox.NeutralIndex) + 1; Index < Gearbox.GearRatios.size(); ++Index) Forward.push_back(Gearbox.GearRatios[Index]);
    return Forward;
}

void AssignForwardRatios(VehicleSpecification& Specification, const std::vector<float>& Ratios)
{
    TransmissionParameters& Gearbox = Specification.Solver.Transmission;
    std::vector<float>      Laid(Gearbox.GearRatios.begin(), Gearbox.GearRatios.begin() + std::min<size_t>(Gearbox.GearRatios.size(), static_cast<size_t>(Gearbox.NeutralIndex) + 1));
    for (const float Ratio : Ratios)
        if (Ratio > 0.01f) Laid.push_back(Ratio);
    if (Laid.size() > static_cast<size_t>(Gearbox.NeutralIndex) + 1) Gearbox.GearRatios = Laid;
}

std::vector<VehicleFigure> EnumerateVehicleFigures(VehicleSpecification& S)
{
    std::vector<VehicleFigure> Figures;
    Figures.reserve(320);
    const auto Add = [&](const char* Section, const char* Key, const char* Label, const char* Unit, VehicleFigureCategory Category, double Minimum,
                         double Maximum, std::function<double()> Read, std::function<void(double)> Write, double Display = 1.0, double Offset = 0.0,
                         std::vector<std::string> Choices = {})
    {
        VehicleFigure Figure;
        Figure.Section  = Section;
        Figure.Key      = Key;
        Figure.Label    = Label;
        Figure.Unit     = Unit;
        Figure.Category = Category;
        Figure.Minimum  = Minimum;
        Figure.Maximum  = Maximum;
        Figure.Display  = Display;
        Figure.Offset   = Offset;
        Figure.Choices  = std::move(Choices);
        Figure.Read     = std::move(Read);
        Figure.Write    = std::move(Write);
        Figures.push_back(std::move(Figure));
    };
    const auto Real = [&](const char* Section, const char* Key, const char* Label, const char* Unit, double Minimum, double Maximum, float& Target,
                          double Display = 1.0, double Offset = 0.0)
    {
        Add(Section, Key, Label, Unit, VehicleFigureCategory::Real, Minimum, Maximum, [&Target] { return static_cast<double>(Target); },
            [&Target](double Assigned) { Target = static_cast<float>(Assigned); }, Display, Offset);
    };
    const auto Wide = [&](const char* Section, const char* Key, const char* Label, const char* Unit, double Minimum, double Maximum, double& Target,
                          double Display = 1.0, double Offset = 0.0)
    {
        Add(Section, Key, Label, Unit, VehicleFigureCategory::Real, Minimum, Maximum, [&Target] { return Target; }, [&Target](double Assigned) { Target = Assigned; },
            Display, Offset);
    };
    const auto Switch = [&](const char* Section, const char* Key, const char* Label, bool& Target)
    {
        Add(Section, Key, Label, "", VehicleFigureCategory::Switch, 0.0, 1.0, [&Target] { return Target ? 1.0 : 0.0; },
            [&Target](double Assigned) { Target = Assigned > 0.5; });
    };
    const auto Whole = [&](const char* Section, const char* Key, const char* Label, const char* Unit, double Minimum, double Maximum, auto& Target)
    {
        Add(Section, Key, Label, Unit, VehicleFigureCategory::Whole, Minimum, Maximum, [&Target] { return static_cast<double>(Target); },
            [&Target, Minimum, Maximum](double Assigned)
            {
                using Stored = std::remove_reference_t<decltype(Target)>;
                Target = static_cast<Stored>(std::llround(std::clamp(Assigned, Minimum, Maximum)));
            });
    };
    const auto Choose = [&](const char* Section, const char* Key, const char* Label, std::vector<std::string> Choices, auto& Target)
    {
        const double Last = static_cast<double>(Choices.size()) - 1.0;
        Add(Section, Key, Label, "", VehicleFigureCategory::Choice, 0.0, Last, [&Target] { return static_cast<double>(static_cast<int>(Target)); },
            [&Target, Last](double Assigned)
            {
                using Stored = std::remove_reference_t<decltype(Target)>;
                Target = static_cast<Stored>(static_cast<int>(std::llround(std::clamp(Assigned, 0.0, Last))));
            },
            1.0, 0.0, std::move(Choices));
    };

    VehicleGeometry&            G = S.Geometry;
    VehicleSolverConfiguration& C = S.Solver;

    // 📝 Chassis and identity (VehicleGeometry; the wheel and strut positions are the ControlVehicle's sockets)
    Real("geometry", "mass_kg", "Mass", "kg", 400.0, 3000.0, G.Mass);
    Real("geometry", "front_weight_share", "Front weight share", "%", 30.0, 70.0, G.FrontWeightFraction, 100.0);
    Real("geometry", "com_height_m", "Centre of mass height", "m", 0.25, 1.2, G.CoMHeight);
    Real("geometry", "ride_height_m", "Ride height", "m", 0.01, 0.30, G.StaticRideHeight);
    Real("geometry", "unsprung_mass_kg", "Unsprung mass per corner", "kg", 8.0, 80.0, G.UnsprungMassPerWheel);
    Real("geometry", "roll_gyration_m", "Roll radius of gyration", "m", 0.2, 1.5, G.RollRadiusGyration);
    Real("geometry", "pitch_gyration_m", "Pitch radius of gyration", "m", 0.5, 3.0, G.PitchRadiusGyration);
    Real("geometry", "yaw_gyration_m", "Yaw radius of gyration", "m", 0.5, 3.0, G.YawRadiusGyration);
    Real("geometry", "tyre_radius_m", "Tyre radius", "m", 0.25, 0.70, G.TyreRadius);
    Real("geometry", "rim_radius_m", "Rim radius", "m", 0.15, 0.55, G.TyreRimRadius);
    Real("geometry", "tyre_width_m", "Tyre width", "m", 0.12, 0.50, G.TyreWidth);
    // 📝 Suspension (GRIT's strut, rated for the corner's sprung mass)
    for (int Axle = 0; Axle < 2; ++Axle)
    {
        SuspensionSpecification& Strut = Axle == 0 ? G.Front : G.Rear;
        const char* Section = Axle == 0 ? "suspension.front" : "suspension.rear";
        Real(Section, "frequency_hz", "Ride frequency", "Hz", 0.8, 5.0, Strut.NaturalFrequency);
        Real(Section, "damping_ratio", "Damping ratio", "", 0.1, 1.5, Strut.DampingRatio);
        Real(Section, "max_travel_m", "Stroke limit", "m", 0.05, 0.6, Strut.MaxTravel);
        Real(Section, "bump_travel_m", "Bump travel", "m", 0.005, 0.2, Strut.BumpTravel);
        Real(Section, "droop_travel_m", "Droop travel", "m", 0.01, 0.3, Strut.DroopTravel);
        Real(Section, "static_preload_m", "Static preload", "m", 0.0, 0.2, Strut.StaticPreload);
        Switch(Section, "progressive", "Progressive spring", Strut.Progressive);
        Choose(Section, "curve", "Spring curve", { "Linear", "Progressive", "Digressive", "Dual rate" }, Strut.Curve);
        Real(Section, "hardening", "Hardening at full travel", "×", 0.5, 4.0, Strut.HardeningFactor);
        AntiRollBar& Bar = Axle == 0 ? G.FrontBar : G.RearBar;
        const char* BarSection = Axle == 0 ? "bars.front" : "bars.rear";
        Switch(BarSection, "enabled", "Anti-roll bar", Bar.Enabled);
        Real(BarSection, "stiffness_npm", "Bar stiffness", "kN/m", 0.0, 400.0, Bar.Stiffness, Kilo);
        Real(BarSection, "damping_nspm", "Bar damping", "N·s/m", 0.0, 10000.0, Bar.Damping);
    }
    Switch("suspension", "enabled", "Struts (off welds the hubs)", C.SuspensionEnabled);
    // 📝 Tyre, contact fan and carcass
    Real("tyre", "pressure_pa", "Cold pressure", "kPa", 30.0, 400.0, C.Tyre.InflationPressure, Kilo);
    Real("tyre", "mass_kg", "Tyre mass", "kg", 3.0, 40.0, C.Tyre.TotalMass);
    Real("tyre", "atmosphere_pa", "Atmosphere", "kPa", 50.0, 110.0, C.Tyre.AtmosphericPressure, Kilo);
    Real("tyre", "effective_radius_m", "Rolling radius (0: tyre radius)", "m", 0.0, 0.7, C.EffectiveRadius);
    Real("tyre", "grip", "Grip coefficient", "μ", 0.3, 2.5, C.GripCoefficient);
    Real("tyre", "rolling_resistance", "Rolling resistance", "", 0.0, 0.05, C.RollingResistance);
    Switch("tyre", "pressure_sensitive", "Pressure enters the formula", C.PressureSensitiveTyre);
    Real("contact", "stiffness_npm", "Radial stiffness", "kN/m", 50.0, 800.0, C.TraceContact.VerticalStiffness, Kilo);
    Real("contact", "reference_pressure_pa", "Stiffness reference pressure", "kPa", 30.0, 400.0, C.TraceContact.ReferencePressure, Kilo);
    Real("contact", "pressure_share", "Air share of the stiffness", "", 0.0, 1.0, C.TraceContact.PressureShare);
    Real("contact", "damping_nspm", "Radial damping", "N·s/m", 0.0, 10000.0, C.TraceContact.VerticalDamping);
    Real("contact", "max_deflection_m", "Deflection limit", "m", 0.01, 0.2, C.TraceContact.MaxDeflection);
    Real("contact", "fan_half_angle_rad", "Fan half angle", "°", 5.0, 80.0, C.TraceContact.FanHalfAngleRad, Degrees);
    Real("contact", "fan_step_rad", "Fan step", "°", 1.0, 20.0, C.TraceContact.FanStepRad, Degrees);
    Whole("contact", "slices", "Slices across the tread", "", 1.0, 7.0, C.TraceContact.LateralCount);
    Choose("carcass", "formulation", "Carcass formulation", { "Spring", "XPBD", "3D FEM" }, C.Carcass);
    Wide("carcass", "reference_pressure_pa", "Calibration gauge pressure", "kPa", 40.0, 350.0, C.CarcassTuning.ReferencePressure, 1.0e-3);
    Wide("carcass", "reference_stiffness_npm", "Calibration radial stiffness", "kN/m", 60.0, 600.0, C.CarcassTuning.ReferenceStiffness, 1.0e-3);
    Wide("carcass", "pressure_share", "Pneumatic stiffness share", "", 0.0, 1.0, C.CarcassTuning.PressureShare);
    Wide("carcass", "tread_mass_kg", "Moving tread mass", "kg", 1.0, 30.0, C.CarcassTuning.TreadMass);
    Wide("carcass", "damping_per_s", "Carcass hysteresis", "1/s", 0.0, 120.0, C.CarcassTuning.DampingPerSecond);
    Wide("carcass", "friction", "Carcass contact friction", "μ", 0.0, 2.0, C.CarcassTuning.Friction);
    Whole("carcass", "substeps", "Lattice sub-steps", "", 1.0, 8.0, C.CarcassTuning.Substeps);
    Wide("carcass", "xpbd_pressure_share", "XPBD outward pressure share", "", 0.05, 1.5, C.CarcassTuning.XpbdPressureForceFactor);
    Wide("carcass", "xpbd_spoke_compliance", "XPBD spoke compliance", "µm/N", 2.0, 200.0, C.CarcassTuning.XpbdSpokeCompliance, 1.0e6);
    Wide("carcass", "xpbd_belt_compliance", "XPBD belt compliance", "µm/N", 0.1, 40.0, C.CarcassTuning.XpbdBeltCompliance, 1.0e6);
    Wide("carcass", "xpbd_lateral_compliance", "XPBD lateral compliance", "µm/N", 0.1, 40.0, C.CarcassTuning.XpbdLateralCompliance, 1.0e6);
    Wide("carcass", "xpbd_shear_compliance", "XPBD shear compliance", "µm/N", 0.1, 40.0, C.CarcassTuning.XpbdShearCompliance, 1.0e6);
    Whole("carcass", "xpbd_iterations", "XPBD iterations per sub-step", "", 2.0, 32.0, C.CarcassTuning.XpbdIterations);
    Wide("carcass", "fem_young_modulus_pa", "3D FEM Young's modulus", "MPa", 0.5, 25.0, C.CarcassTuning.FemYoungModulus, 1.0e-6);
    Wide("carcass", "fem_poisson_ratio", "3D FEM Poisson ratio", "", 0.10, 0.48, C.CarcassTuning.FemPoissonRatio);
    Wide("carcass", "fem_prestress_scale", "3D FEM follower-pressure scale", "", 0.5, 25.0, C.CarcassTuning.FemPrestressScale);
    Wide("carcass", "fem_cord_modulus_pa", "3D FEM cord tensile modulus", "MPa", 40.0, 1500.0, C.CarcassTuning.FemCordModulus, 1.0e-6);
    Wide("carcass", "fem_damping_nspm", "3D FEM radial damping", "N·s/m", 0.0, 8000.0, C.CarcassTuning.FemDampingRate);
    // 📝 Magic Formula (Project Drive's GRIT sheet) and MF6.1 pressure terms
    {
        PacejkaParameters& P = C.TyrePacejka;
        Real("pacejka", "fz0_kn", "Reference load", "kN", 1.0, 15.0, P.Fz0);
        Real("pacejka", "lx", "Longitudinal scale", "", 0.2, 3.0, P.Lx);
        Real("pacejka", "ly", "Lateral scale", "", 0.2, 3.0, P.Ly);
        Real("pacejka", "ltr", "Aligning scale", "", 0.0, 3.0, P.Ltr);
        Real("pacejka", "mu_load_long", "μ load sensitivity, longitudinal", "", 0.0, 0.5, P.MuLoadSensitivityLong);
        Real("pacejka", "mu_load_lat", "μ load sensitivity, lateral", "", 0.0, 0.5, P.MuLoadSensitivityLat);
        Real("pacejka", "mu_load_floor", "μ load floor", "", 0.1, 1.0, P.MuLoadMinFactor);
        struct Coefficient { const char* Key; float* Target; double Minimum, Maximum; };
        const Coefficient Coefficients[] = {
            { "pbx1", &P.pBx1, 0.0, 60.0 }, { "pbx2", &P.pBx2, -60.0, 60.0 }, { "pbx3", &P.pBx3, -5.0, 5.0 }, { "pcx1", &P.pCx1, 0.5, 3.0 },
            { "pdx1", &P.pDx1, 0.3, 3.0 }, { "pdx2", &P.pDx2, -1.0, 1.0 }, { "pdx3", &P.pDx3, -5.0, 5.0 }, { "pex1", &P.pEx1, -5.0, 1.0 },
            { "pex2", &P.pEx2, -5.0, 5.0 }, { "pex3", &P.pEx3, -5.0, 5.0 }, { "pex4", &P.pEx4, -1.0, 1.0 }, { "phx1", &P.pHx1, -0.1, 0.1 },
            { "phx2", &P.pHx2, -0.1, 0.1 }, { "pvx1", &P.pVx1, -0.5, 0.5 }, { "pvx2", &P.pVx2, -0.5, 0.5 }, { "pby1", &P.pBy1, 0.0, 60.0 },
            { "pby2", &P.pBy2, -60.0, 60.0 }, { "pby3", &P.pBy3, -5.0, 5.0 }, { "pcy1", &P.pCy1, 0.5, 3.0 }, { "pdy1", &P.pDy1, 0.3, 3.0 },
            { "pdy2", &P.pDy2, -1.0, 1.0 }, { "pdy3", &P.pDy3, -10.0, 10.0 }, { "pey1", &P.pEy1, -5.0, 1.0 }, { "pey2", &P.pEy2, -5.0, 5.0 },
            { "pey3", &P.pEy3, -5.0, 5.0 }, { "phy1", &P.pHy1, -0.1, 0.1 }, { "phy2", &P.pHy2, -0.1, 0.1 }, { "pvy1", &P.pVy1, -0.5, 0.5 },
            { "pvy2", &P.pVy2, -0.5, 0.5 }, { "pvy3", &P.pVy3, -2.0, 2.0 }, { "pvy4", &P.pVy4, -2.0, 2.0 }, { "pcamber1", &P.pCamber1, -2.0, 2.0 },
            { "pcamber2", &P.pCamber2, -2.0, 2.0 }, { "qdz1", &P.qDz1, -1.0, 1.0 }, { "qdz2", &P.qDz2, -1.0, 1.0 }, { "qdz3", &P.qDz3, -5.0, 5.0 },
            { "qbz1", &P.qBz1, 0.0, 40.0 }, { "qbz2", &P.qBz2, -10.0, 10.0 }, { "qbz3", &P.qBz3, -10.0, 10.0 }, { "qbz5", &P.qBz5, -5.0, 5.0 },
            { "qcz1", &P.qCz1, 0.5, 3.0 }, { "qez1", &P.qEz1, -10.0, 5.0 }, { "qez2", &P.qEz2, -10.0, 10.0 }, { "qez3", &P.qEz3, -10.0, 10.0 },
            { "qez4", &P.qEz4, -5.0, 5.0 }, { "qez5", &P.qEz5, -10.0, 10.0 }, { "rbx1", &P.rBx1, 0.0, 40.0 }, { "rbx2", &P.rBx2, 0.0, 40.0 },
            { "rbx3", &P.rBx3, -5.0, 5.0 }, { "rcx1", &P.rCx1, 0.1, 3.0 }, { "rex1", &P.rEx1, -5.0, 5.0 }, { "rex2", &P.rEx2, -5.0, 5.0 },
            { "rhx1", &P.rHx1, -0.1, 0.1 }, { "rby1", &P.rBy1, 0.0, 40.0 }, { "rby2", &P.rBy2, 0.0, 40.0 }, { "rby3", &P.rBy3, -5.0, 5.0 },
            { "rby4", &P.rBy4, -5.0, 5.0 }, { "rcy1", &P.rCy1, 0.1, 3.0 }, { "rey1", &P.rEy1, -5.0, 5.0 }, { "rey2", &P.rEy2, -5.0, 5.0 },
            { "rhy1", &P.rHy1, -0.1, 0.1 }, { "rhy2", &P.rHy2, -0.1, 0.1 }, { "rvy1", &P.rVy1, -1.0, 1.0 }, { "rvy2", &P.rVy2, -1.0, 1.0 },
            { "rvy3", &P.rVy3, -2.0, 2.0 }, { "rvy4", &P.rVy4, -100.0, 100.0 }, { "rvy5", &P.rVy5, -10.0, 10.0 }, { "rvy6", &P.rVy6, -100.0, 100.0 },
            { "ppx1", &P.ppx1, -2.0, 2.0 }, { "ppx2", &P.ppx2, -2.0, 2.0 }, { "ppx3", &P.ppx3, -2.0, 2.0 }, { "ppx4", &P.ppx4, -2.0, 2.0 },
            { "ppy1", &P.ppy1, -2.0, 2.0 }, { "ppy2", &P.ppy2, -2.0, 2.0 }, { "ppy3", &P.ppy3, -2.0, 2.0 }, { "ppy4", &P.ppy4, -2.0, 2.0 },
            { "ppz1", &P.ppz1, -2.0, 2.0 } };
        for (const Coefficient& Entry : Coefficients) Real("pacejka", Entry.Key, Entry.Key, "", Entry.Minimum, Entry.Maximum, *Entry.Target);
        Real("pacejka", "nominal_pressure_pa", "Formula's nominal pressure", "kPa", 30.0, 400.0, P.NominalPressure, Kilo);
        Switch("pacejka", "load_sensitivity_above_reference", "Grip falls only above the reference load", P.LoadSensitivityAboveReference);
        Switch("pacejka", "combined_slip_mf61", "MF6.1 combined-slip weighting", P.CombinedSlipMf61);
        Real("pacejka", "relaxation_long_m", "Relaxation length, longitudinal", "m", 0.005, 0.5, P.RelaxationLengthLong);
        Real("pacejka", "relaxation_lat_m", "Relaxation length, lateral", "m", 0.005, 0.5, P.RelaxationLengthLat);
        Real("pacejka", "relaxation_long_exponent", "Relaxation load exponent, longitudinal", "", 0.0, 1.5, P.RelaxationLoadExponentLong);
        Real("pacejka", "relaxation_lat_exponent", "Relaxation load exponent, lateral", "", 0.0, 1.5, P.RelaxationLoadExponentLat);
    }
    Choose("slip", "solver", "Slip solver", { "Newton (GRIT reference)", "Relaxation length" }, C.SlipSolverSelection);
    Real("slip", "spin_damping", "Spin slip damping", "N·s/m", 0.0, 10000.0, C.SpinDampingCoefficient);
    Real("slip", "lateral_damping", "Lateral slip damping", "N·s/m", 0.0, 10000.0, C.LateralDampingCoefficient);
    Real("slip", "damping_fade_mps", "Damping fade speed", "m/s", 0.1, 40.0, C.ContactDampingFadeSpeed);
    // 📝 Engine, forced induction and charge air
    Real("engine", "idle_rpm", "Idle", "rpm", 300.0, 2500.0, C.Engine.IdleRPM);
    Real("engine", "redline_rpm", "Redline", "rpm", 3000.0, 20000.0, C.Engine.RedlineRPM);
    Real("engine", "rise_rpm_per_s", "Rev rise limit", "rpm/s", 100.0, 20000.0, C.Engine.MaxRPMIncrease);
    Real("engine", "fall_rpm_per_s", "Rev fall limit", "rpm/s", 100.0, 20000.0, C.Engine.MaxRPMDecrease);
    Real("engine", "friction_coefficient", "Friction", "N·m·s/rad", 0.0, 1.0, C.Engine.EngineBrakingCoeff);
    Real("engine", "pumping_coefficient", "Pumping loss", "N·m·s²/rad²", 0.0, 0.01, C.Engine.EngineBrakingQuadratic);
    Real("engine", "inertia_kgm2", "Engine inertia", "kg·m²", 0.02, 3.0, C.Engine.EngineInertia);
    Choose("induction", "category", "Induction", { "Naturally aspirated", "Turbocharged", "Supercharged" }, C.Induction);
    Switch("induction", "race_tune", "Race fuel tune", C.RaceTune);
    Real("turbo", "inertia_kgm2", "Turbo inertia", "kg·m²", 0.00005, 0.2, C.Turbo.TurboInertia);
    Real("turbo", "friction", "Shaft friction", "", 0.0, 0.05, C.Turbo.TurboFrictionCoeff);
    Real("turbo", "friction_quadratic", "Shaft friction, quadratic", "", 0.0, 0.001, C.Turbo.TurboFrictionQuadratic);
    Real("turbo", "wastegate_bar", "Wastegate limit", "bar", 0.2, 4.0, C.Turbo.WastegateMaxBoost);
    Real("turbo", "blowoff_bar_per_s", "Blow-off rate", "bar/s", 0.5, 60.0, C.Turbo.BovPressureReleaseRate);
    Real("turbo", "turbine_efficiency", "Turbine efficiency", "", 0.2, 1.0, C.Turbo.TurbineEfficiency);
    Real("turbo", "compressor_efficiency", "Compressor efficiency", "", 0.2, 1.0, C.Turbo.CompressorEfficiency);
    Real("turbo", "mechanical_efficiency", "Mechanical efficiency", "", 0.5, 1.0, C.Turbo.MechanicalEfficiency);
    Choose("supercharger", "drive", "Blower", { "Roots", "Twin screw", "Centrifugal" }, C.Supercharger.DriveType);
    Real("supercharger", "inertia_kgm2", "Charger inertia", "kg·m²", 0.001, 0.2, C.Supercharger.ChargerInertia);
    Real("supercharger", "friction", "Charger friction", "", 0.0, 0.05, C.Supercharger.ChargerFrictionCoeff);
    Real("supercharger", "friction_quadratic", "Charger friction, quadratic", "", 0.0, 0.001, C.Supercharger.ChargerFrictionQuadratic);
    Real("supercharger", "drive_ratio", "Pulley ratio", "×", 0.5, 8.0, C.Supercharger.DriveRatio);
    Real("supercharger", "pump_boost_bar", "Boost ceiling, pump fuel", "bar", 0.1, 3.0, C.Supercharger.MaxBoost_Pump);
    Real("supercharger", "race_boost_bar", "Boost ceiling, race fuel", "bar", 0.1, 3.0, C.Supercharger.MaxBoost_Race);
    Real("supercharger", "bypass_bar_per_s", "Bypass valve rate", "bar/s", 0.5, 60.0, C.Supercharger.BypassValveRate);
    Real("supercharger", "compressor_efficiency", "Compressor efficiency", "", 0.2, 1.0, C.Supercharger.CompressorEfficiency);
    Real("supercharger", "parasitic_floor", "Bypassed drag share", "", 0.0, 1.0, C.Supercharger.ParasiticLossFactor);
    Switch("charge", "enabled", "Charge-air temperature", C.ChargeAir.Enabled);
    Real("charge", "ambient_k", "Air temperature", "°C", -30.0, 50.0, C.ChargeAir.AmbientTemperature_K, 1.0, Celsius);
    Real("charge", "ambient_bar", "Air pressure", "bar", 0.5, 1.2, C.ChargeAir.AmbientPressure_Bar);
    Real("charge", "intercooler", "Intercooler effectiveness", "", 0.0, 1.0, C.ChargeAir.IntercoolerEffectiveness);
    Real("charge", "reference_k", "Curves' charge temperature", "°C", 0.0, 120.0, C.ChargeAir.ReferenceTemperature_K, 1.0, Celsius);
    Switch("charge", "density_correction", "Warm charge thins the boost", C.ChargeAir.DensityCorrection);
    // 📝 Driveline
    Real("gearbox", "final_drive", "Final drive", "×", 1.5, 8.0, C.Transmission.FinalDriveRatio);
    Real("gearbox", "efficiency", "Driveline efficiency", "", 0.5, 1.0, C.Transmission.DriveEfficiency);
    Real("gearbox", "upshift_rpm", "Upshift", "rpm", 1500.0, 20000.0, C.UpshiftRPM);
    Real("gearbox", "downshift_rpm", "Downshift", "rpm", 800.0, 15000.0, C.DownshiftRPM);
    Real("gearbox", "shift_seconds", "Shift dwell", "s", 0.05, 2.0, C.ShiftCooldownSeconds);
    Real("clutch", "capacity_nm", "Clutch capacity", "N·m", 100.0, 4000.0, C.Clutch.MaxTorqueCapacity);
    Real("clutch", "breakaway_rpm", "Breakaway slip", "rpm", 10.0, 2000.0, C.Clutch.BreakawaySlipRPM);
    Real("clutch", "lockup_engagement", "Lock-up engagement", "", 0.5, 1.0, C.Clutch.LockupEngagement);
    Real("clutch", "lockup_rpm", "Lock-up slip", "rpm", 1.0, 500.0, C.Clutch.LockupSlipRPM);
    Choose("clutch", "category", "Clutch", { "Single plate", "Dual clutch", "Torque converter" }, C.Clutch.Category);
    Real("clutch", "shift_s", "Staged shift time (0: instant)", "s", 0.0, 2.0, C.Clutch.ShiftDuration);
    Real("clutch", "handoff_s", "DCT handoff overlap", "s", 0.05, 1.0, C.Clutch.HandoffSeconds);
    Real("clutch", "launch_s", "Launch ramp", "s", 0.1, 3.0, C.Clutch.LaunchSeconds);
    Real("clutch", "converter_k", "Converter K factor", "rpm/√Nm", 20.0, 400.0, C.Clutch.ConverterKFactor);
    Real("clutch", "converter_stall_ratio", "Converter stall ratio", "×", 1.0, 3.0, C.Clutch.ConverterStallRatio);
    Real("clutch", "converter_coupling", "Converter coupling point", "", 0.5, 1.0, C.Clutch.ConverterCouplingPoint);
    Real("clutch", "converter_lockup_rpm", "Converter lock-up speed", "rpm", 400.0, 6000.0, C.Clutch.LockupRPM);
    Real("clutch", "converter_lockup_drop_rpm", "Lock-up hysteresis", "rpm", 50.0, 2000.0, C.Clutch.LockupDropRPM);
    Real("clutch", "converter_lockup_rate", "Lock-up rate", "1/s", 0.5, 20.0, C.Clutch.LockupRate);
    Choose("propulsion", "type", "Propulsion", { "Combustion", "Electric" }, C.Propulsion);
    Choose("differential", "mode", "Differential", { "Open", "Locked", "Limited slip", "Torque vectoring" }, C.Differential.Mode);
    Real("differential", "locking", "Locking factor", "", 0.0, 1.0, C.Differential.LockingFactor);
    Real("differential", "preload_nm", "Preload", "N·m", 0.0, 500.0, C.Differential.Preload_Nm);
    Real("differential", "vectoring", "Vectoring bias", "", -1.0, 1.0, C.Differential.VectoringBias);
    // 📝 Brakes, ABS and traction control
    Switch("brakes", "hydraulic", "Hydraulic discs with fade", C.BrakeThermalEnabled);
    Real("brakes", "torque_nm", "Foot brake torque (simple)", "N·m", 100.0, 10000.0, C.MaxBrakeTorquePerWheel);
    Real("brakes", "handbrake_nm", "Handbrake torque", "N·m", 0.0, 12000.0, C.HandbrakeTorque);
    Real("brakes", "disc_outer_m", "Disc outer radius", "m", 0.08, 0.30, C.Brakes.DiskOuterRadius);
    Real("brakes", "disc_inner_m", "Disc inner radius", "m", 0.04, 0.25, C.Brakes.DiskInnerRadius);
    Whole("brakes", "pistons", "Pistons", "", 1.0, 12.0, C.Brakes.NumPistons);
    Real("brakes", "piston_m", "Piston diameter", "m", 0.01, 0.08, C.Brakes.PistonDiameter);
    Real("brakes", "friction_cold", "Pad friction, cold", "", 0.1, 0.9, C.Brakes.FrictionCoeffCold);
    Real("brakes", "friction_hot", "Pad friction, faded", "", 0.1, 0.9, C.Brakes.FrictionCoeffHot);
    Real("brakes", "fade_start_k", "Fade starts", "°C", 100.0, 900.0, C.Brakes.FadeStartTemp, 1.0, Celsius);
    Real("brakes", "fade_end_k", "Fade complete", "°C", 200.0, 1200.0, C.Brakes.FadeEndTemp, 1.0, Celsius);
    Real("brakes", "pressure_pa", "Line pressure", "bar", 10.0, 300.0, C.Brakes.MaxBrakePressure, 1.0e-5);
    Real("brakes", "handbrake_pa", "Handbrake pressure", "bar", 10.0, 300.0, C.Brakes.MaxHandbrakePressure, 1.0e-5);
    Real("brakes", "rise_pa_per_s", "Pressure rise", "bar/s", 10.0, 4000.0, C.Brakes.PressureRiseRate, 1.0e-5);
    Real("brakes", "fall_pa_per_s", "Pressure fall", "bar/s", 10.0, 4000.0, C.Brakes.PressureFallRate, 1.0e-5);
    Real("brakes", "disc_mass_kg", "Disc mass", "kg", 1.0, 25.0, C.Brakes.DiskMass);
    Real("brakes", "disc_heat_jkgk", "Disc specific heat", "J/(kg·K)", 200.0, 1500.0, C.Brakes.SpecificHeatCapacity);
    Real("brakes", "convection", "Disc convection", "W/(m²·K)", 5.0, 400.0, C.Brakes.ConvectionCoeff);
    Real("brakes", "disc_area_m2", "Disc surface", "m²", 0.01, 0.5, C.Brakes.DiskSurfaceArea);
    Real("brakes", "ambient_k", "Disc ambient", "°C", -30.0, 50.0, C.Brakes.AmbientTemp, 1.0, Celsius);
    Switch("abs", "enabled", "ABS", C.Abs.Enabled);
    Real("abs", "slip_target", "ABS slip target", "", 0.02, 0.4, C.Abs.SlipTarget);
    Real("abs", "gain", "ABS gain", "", 0.5, 40.0, C.Abs.Gain);
    Real("abs", "minimum_mps", "ABS minimum speed", "m/s", 0.0, 10.0, C.Abs.MinSpeed);
    Real("abs", "floor", "ABS pressure floor", "", 0.0, 1.0, C.Abs.DumpFloor);
    Real("abs", "reapply_per_s", "Cycled reapply rate (0: proportional)", "1/s", 0.0, 20.0, C.Abs.ReapplyRate);
    Real("abs", "dump_per_s", "Cycled dump rate", "1/s", 1.0, 60.0, C.Abs.DumpRate);
    Real("abs", "slip_band", "Cycled hold band", "", 0.0, 0.15, C.Abs.SlipBand);
    Real("abs", "regulation_floor", "Cycled dump floor", "", 0.05, 0.6, C.Abs.RegulationFloor);
    Switch("traction", "enabled", "Traction control", C.TractionControl.Enabled);
    Real("traction", "slip_target", "Slip target", "", 0.02, 0.5, C.TractionControl.SlipTarget);
    Real("traction", "gain", "Throttle cut per slip", "", 0.5, 40.0, C.TractionControl.Gain);
    Real("traction", "minimum_mps", "Active above", "m/s", 0.0, 10.0, C.TractionControl.MinimumSpeed);
    Real("traction", "floor", "Throttle kept", "", 0.0, 1.0, C.TractionControl.FloorThrottle);
    Real("traction", "recovery_per_s", "Recovery rate", "1/s", 0.1, 20.0, C.TractionControl.RecoveryRate);
    // 📝 Steering
    Real("steering", "lock_rad", "Steering lock", "°", 5.0, 60.0, C.MaxSteerAngleRad, Degrees);
    Real("steering", "rate_per_s", "Steering rate", "1/s", 0.5, 40.0, C.SteerRatePerSecond);
    Real("steering", "ackermann", "Ackermann share", "", 0.0, 1.0, C.AckermannShare);
    // 📝 Aerodynamics (GRIT's package)
    {
        AerodynamicPackage& A = C.Aero;
        Switch("aero", "enabled", "Aerodynamics", A.Enabled);
        Real("aero", "air_density", "Air density", "kg/m³", 0.5, 1.5, A.AirDensity_kgm3);
        Real("aero", "air_brake_threshold", "Air-brake pedal threshold", "", 0.0, 1.0, A.AeroBrakeThreshold);
        Real("aero.body", "frontal_area_m2", "Frontal area", "m²", 0.5, 6.0, A.VehicleBody.FrontalArea_m2);
        Real("aero.body", "side_area_m2", "Side area", "m²", 1.0, 12.0, A.VehicleBody.SideArea_m2);
        Real("aero.body", "drag", "Drag coefficient", "Cd", 0.1, 1.5, A.VehicleBody.CoeffDrag);
        Real("aero.body", "lift", "Lift coefficient", "Cl", -2.0, 2.0, A.VehicleBody.CoeffLift);
        Real("aero.body", "side", "Side-force coefficient", "", 0.0, 2.0, A.VehicleBody.CoeffSideForce);
        Switch("aero.wing", "enabled", "Rear wing", A.RearWing.Enabled);
        Real("aero.wing", "area_m2", "Wing area", "m²", 0.1, 4.0, A.RearWing.Area_m2);
        Real("aero.wing", "lift", "Wing lift coefficient", "", -6.0, 0.0, A.RearWing.NominalCoeffLift);
        Real("aero.wing", "drag", "Wing drag coefficient", "", 0.0, 2.0, A.RearWing.NominalCoeffDrag);
        Real("aero.wing", "aspect", "Aspect ratio", "", 0.5, 10.0, A.RearWing.AspectRatio);
        Real("aero.wing", "angle_deg", "Wing angle", "°", 0.0, 40.0, A.RearWing.CurrentAngle_deg);
        Real("aero.wing", "minimum_deg", "Wing angle minimum", "°", 0.0, 40.0, A.RearWing.MinAngle_deg);
        Real("aero.wing", "maximum_deg", "Wing angle maximum", "°", 0.0, 40.0, A.RearWing.MaxAngle_deg);
        Switch("aero.wing", "adaptive", "Adaptive wing", A.RearWing.IsAdaptive);
        Real("aero.wing", "adaptive_mps", "Adapts above", "m/s", 5.0, 100.0, A.RearWing.AdaptiveSpeedThreshold_ms);
        Real("aero.wing", "air_brake_deg", "Air-brake angle", "°", 0.0, 30.0, A.RearWing.BrakeDeployAngle_deg);
        Switch("aero.splitter", "enabled", "Front splitter", A.FrontSplitter.Enabled);
        Real("aero.splitter", "area_m2", "Splitter area", "m²", 0.1, 3.0, A.FrontSplitter.Area_m2);
        Real("aero.splitter", "pressure", "Splitter pressure coefficient", "", 0.0, 3.0, A.FrontSplitter.CoeffPressure);
        Real("aero.splitter", "dam_height_m", "Air-dam height", "m", 0.0, 0.3, A.FrontSplitter.AirDamHeight_m);
        Real("aero.splitter", "dam_drag", "Air-dam drag", "", 0.0, 1.0, A.FrontSplitter.AirDamDragCoeff);
        Real("aero.splitter", "ride_sensitivity", "Ride-height sensitivity", "", 0.0, 10.0, A.FrontSplitter.RideHeightSensitivity);
        Switch("aero.diffuser", "enabled", "Floor and diffuser", A.FloorDiffuser.Enabled);
        Real("aero.diffuser", "floor_m2", "Floor area", "m²", 0.5, 8.0, A.FloorDiffuser.FloorArea_m2);
        Real("aero.diffuser", "diffuser_m2", "Diffuser area", "m²", 0.1, 4.0, A.FloorDiffuser.DiffuserArea_m2);
        Real("aero.diffuser", "angle_deg", "Diffuser angle", "°", 0.0, 30.0, A.FloorDiffuser.DiffuserAngle_deg);
        Real("aero.diffuser", "efficiency", "Diffuser efficiency", "", 0.0, 1.0, A.FloorDiffuser.DiffuserEfficiency);
        Real("aero.diffuser", "optimum_m", "Optimum ride height", "m", 0.005, 0.2, A.FloorDiffuser.RideHeightOptimum_m);
        Real("aero.diffuser", "critical_m", "Critical ride height", "m", 0.001, 0.1, A.FloorDiffuser.RideHeightCritical_m);
        Real("aero.diffuser", "drag", "Diffuser drag", "", 0.0, 1.0, A.FloorDiffuser.DiffuserDragCoeff);
        for (int Side = 0; Side < 2; ++Side)
        {
            AeroSideSkirt& Skirt   = Side == 0 ? A.LeftSideSkirt : A.RightSideSkirt;
            const char*    Section = Side == 0 ? "aero.skirt_left" : "aero.skirt_right";
            Switch(Section, "enabled", "Side skirt", Skirt.Enabled);
            Real(Section, "length_m", "Skirt length", "m", 0.2, 4.0, Skirt.Length_m);
            Real(Section, "height_m", "Skirt height", "m", 0.02, 0.5, Skirt.Height_m);
            Real(Section, "pressure", "Skirt pressure coefficient", "", -2.0, 0.0, Skirt.CoeffPressure);
            Real(Section, "sealing", "Underbody sealing", "", 0.0, 1.0, Skirt.UnderbodySealingEfficiency);
            Real(Section, "ride_sensitivity", "Ride-height sensitivity", "", 0.0, 10.0, Skirt.RideHeightSensitivity);
            Real(Section, "drag", "Skirt drag", "", 0.0, 1.0, Skirt.DragCoeff);
        }
        Switch("aero.fins", "enabled", "Eddy fins", A.EddyFins.Enabled);
        Real("aero.fins", "area_m2", "Fin area", "m²", 0.0, 1.0, A.EddyFins.TotalArea_m2);
        Real("aero.fins", "strength", "Eddy strength", "", 0.0, 1.0, A.EddyFins.EddyStrength);
        Real("aero.fins", "diffuser_gain", "Diffuser gain", "×", 1.0, 2.0, A.EddyFins.DiffuserEnhancementFactor);
        Real("aero.fins", "drag", "Fin drag", "", 0.0, 0.5, A.EddyFins.DragPenalty);
    }
    // 📝 Tyre temperature and pressure (three heat capacities, the gas law, the grip window)
    {
        ThermalSpecification& T = C.TyreThermal;
        Switch("thermal", "enabled", "Tyre temperature", T.Enabled);
        Wide("thermal", "ambient_k", "Air temperature", "°C", -30.0, 50.0, T.AmbientTemperature, 1.0, Celsius);
        Wide("thermal", "road_k", "Road temperature", "°C", -30.0, 70.0, T.RoadTemperature, 1.0, Celsius);
        Wide("thermal", "surface_capacity", "Tread heat capacity", "J/K", 100.0, 20000.0, T.SurfaceHeatCapacity);
        Wide("thermal", "carcass_capacity", "Carcass heat capacity", "J/K", 1000.0, 80000.0, T.CarcassHeatCapacity);
        Wide("thermal", "surface_carcass", "Tread to carcass", "W/K", 0.0, 200.0, T.SurfaceCarcassConductance);
        Wide("thermal", "carcass_gas", "Carcass to air inside", "W/K", 0.0, 50.0, T.CarcassGasConductance);
        Wide("thermal", "surface_air", "Tread to air", "W/K", 0.0, 100.0, T.SurfaceAirConductance);
        Wide("thermal", "carcass_air", "Carcass to air", "W/K", 0.0, 50.0, T.CarcassAirConductance);
        Wide("thermal", "road", "Tread to road", "W/K", 0.0, 100.0, T.RoadConductance);
        Wide("thermal", "sliding_share", "Sliding heat into the tyre", "", 0.0, 1.0, T.SlidingHeatShare);
        Wide("thermal", "optimum_k", "Window top", "°C", 40.0, 150.0, T.OptimumTemperature, 1.0, Celsius);
        Wide("thermal", "fade_k", "Grip fully faded", "°C", 80.0, 300.0, T.GripFadeTemperature, 1.0, Celsius);
        Wide("thermal", "minimum_grip", "Hot grip floor", "", 0.1, 1.0, T.MinimumGripFactor);
        Wide("thermal", "pressure_grip", "Grip loss per pressure deviation", "", 0.0, 2.0, T.PressureGripSensitivity);
        Switch("thermal", "cold_window", "Cold tread grips less", T.ColdGripEnabled);
        Wide("thermal", "cold_k", "Window bottom", "°C", 0.0, 120.0, T.ColdWindowTemperature, 1.0, Celsius);
        Wide("thermal", "cold_sensitivity", "Cold grip loss per kelvin", "1/K", 0.0, 0.05, T.ColdGripSensitivity);
        Wide("thermal", "cold_floor", "Cold grip floor", "", 0.1, 1.0, T.ColdGripFloor);
        Switch("thermal", "ignition", "Overheated rubber may ignite", T.IgnitionEnabled);
        Wide("thermal", "ignition_k", "Ignition temperature", "°C", 150.0, 600.0, T.IgnitionTemperature, 1.0, Celsius);
        Wide("thermal", "ignition_s", "Ignition exposure", "s", 0.1, 60.0, T.IgnitionExposureSeconds);
        Wide("thermal", "extinction_k", "Extinction temperature", "°C", 100.0, 500.0, T.ExtinctionTemperature, 1.0, Celsius);
    }
    // 📝 Wear, fuel and the engine's fluids
    Switch("wear", "enabled", "Tread wear", C.TyreWear.Enabled);
    Real("wear", "tread_mm", "New tread depth", "mm", 1.0, 20.0, C.TyreWear.InitialTreadDepth_mm);
    Real("wear", "rate_mm_per_mj", "Wear per sliding megajoule", "mm/MJ", 0.0, 0.05, C.TyreWear.BaseWearRate_mm_MJ);
    Real("wear", "hot_multiplier", "Overheated wear multiplier", "×", 1.0, 10.0, C.TyreWear.ThermalWearMultiplier);
    Real("wear", "overheat_k", "Overheat temperature", "°C", 60.0, 250.0, C.TyreWear.OverheatTemperature_K, 1.0, Celsius);
    Real("wear", "load_exponent", "Load exponent", "", 0.5, 3.0, C.TyreWear.LoadWearExponent);
    Real("wear", "reference_load_n", "Reference load", "N", 500.0, 10000.0, C.TyreWear.ReferenceLoad_N);
    Real("wear", "grip_loss_per_mm", "Grip loss per worn millimetre", "1/mm", 0.0, 0.3, C.TyreWear.GripLossPerMM);
    Real("wear", "minimum_grip", "Worn grip floor", "", 0.1, 1.0, C.TyreWear.MinimumGripFactor);
    Switch("fuel", "enabled", "Fuel", C.Fuel.Enabled);
    Real("fuel", "capacity_l", "Tank", "L", 5.0, 200.0, C.Fuel.CapacityLitres);
    Real("fuel", "load_l", "Fuel at the start", "L", 0.0, 200.0, C.Fuel.LoadLitres);
    Real("fuel", "density", "Fuel density", "kg/L", 0.5, 1.0, C.Fuel.DensityKgPerLitre);
    Real("fuel", "bsfc", "Specific consumption", "g/kWh", 150.0, 500.0, C.Fuel.SpecificConsumption_gPerKWh);
    Real("fuel", "idle_gps", "Idle flow", "g/s", 0.0, 3.0, C.Fuel.IdleFlow_gPerS);
    Real("fuel", "lhv_mjkg", "Lower heating value", "MJ/kg", 20.0, 60.0, C.Fuel.LowerHeatingValue_MJkg);
    Switch("intake", "enabled", "Intake manifold filling", C.Intake.Enabled);
    Real("intake", "plenum_l", "Plenum volume", "L", 0.5, 12.0, C.Intake.PlenumVolume_L);
    Real("intake", "throttle_mm", "Throttle bore", "mm", 20.0, 120.0, C.Intake.ThrottleBore_mm);
    Real("intake", "discharge", "Discharge coefficient", "", 0.3, 1.0, C.Intake.DischargeCoefficient);
    Real("intake", "idle_throttle", "Idle bypass", "", 0.0, 0.1, C.Intake.IdleThrottle);
    Real("intake", "displacement_l", "Swept volume", "L", 0.5, 10.0, C.Intake.Displacement_L);
    Real("intake", "volumetric_efficiency", "Volumetric efficiency", "", 0.4, 1.2, C.Intake.VolumetricEfficiency);
    Real("motor", "peak_torque_nm", "Peak torque", "N·m", 50.0, 1500.0, C.ElectricMotor.PeakTorque_Nm);
    Real("motor", "peak_power_kw", "Peak power", "kW", 20.0, 600.0, C.ElectricMotor.PeakPower_W, Kilo);
    Real("motor", "regen_nm", "Regen cap", "N·m", 0.0, 800.0, C.ElectricMotor.RegenerativeTorque_Nm);
    Real("motor", "max_rpm", "Overspeed ceiling", "rpm", 4000.0, 30000.0, C.ElectricMotor.MaxRPM);
    Real("motor", "reduction", "Reduction ratio", "×", 2.0, 20.0, C.ElectricMotor.ReductionRatio);
    Real("motor", "rotor_inertia", "Rotor inertia", "kg·m²", 0.005, 0.5, C.ElectricMotor.RotorInertia);
    Real("battery", "capacity_kwh", "Capacity", "kWh", 5.0, 200.0, C.Battery.Capacity_kWh);
    Real("battery", "resistance_ohm", "Internal resistance", "Ω", 0.005, 0.5, C.Battery.InternalResistance_Ohm);
    Real("battery", "charge_current_a", "Max charge current", "A", 20.0, 800.0, C.Battery.MaxChargeCurrent_A);
    Real("battery", "initial_charge", "Charge at reset", "", 0.0, 1.0, C.Battery.InitialCharge);
    Switch("fluids", "enabled", "Coolant and oil", C.Fluids.Enabled);
    Real("fluids", "ambient_k", "Air temperature", "°C", -30.0, 50.0, C.Fluids.AmbientTemperature_K, 1.0, Celsius);
    Real("fluids", "coolant_capacity", "Coolant heat capacity", "kJ/K", 5.0, 200.0, C.Fluids.CoolantCapacity_JK, Kilo);
    Real("fluids", "oil_capacity", "Oil heat capacity", "kJ/K", 2.0, 100.0, C.Fluids.OilCapacity_JK, Kilo);
    Real("fluids", "efficiency", "Thermal efficiency", "", 0.1, 0.5, C.Fluids.ThermalEfficiency);
    Real("fluids", "coolant_share", "Fuel heat into coolant", "", 0.0, 0.8, C.Fluids.CoolantHeatShare);
    Real("fluids", "oil_share", "Fuel heat into oil", "", 0.0, 0.4, C.Fluids.OilHeatShare);
    Real("fluids", "thermostat_k", "Thermostat opens", "°C", 60.0, 110.0, C.Fluids.ThermostatOpening_K, 1.0, Celsius);
    Real("fluids", "thermostat_full_k", "Thermostat fully open", "°C", 62.0, 120.0, C.Fluids.ThermostatFull_K, 1.0, Celsius);
    Real("fluids", "radiator", "Radiator conductance", "W/K", 50.0, 5000.0, C.Fluids.RadiatorConductance_WK);
    Real("fluids", "radiator_airflow", "Radiator airflow gain", "s/m", 0.0, 0.5, C.Fluids.RadiatorAirflowGain);
    Real("fluids", "block_loss", "Block loss", "W/K", 0.0, 200.0, C.Fluids.BlockLoss_WK);
    Real("fluids", "oil_coolant", "Oil to coolant", "W/K", 0.0, 1000.0, C.Fluids.OilCoolantConductance);
    Real("fluids", "oil_target_k", "Oil working temperature", "°C", 60.0, 150.0, C.Fluids.OilTarget_K, 1.0, Celsius);
    Real("fluids", "cold_friction", "Cold oil friction gain", "", 0.0, 3.0, C.Fluids.ColdFrictionGain);
    return Figures;
}

std::vector<VehicleCurve> EnumerateVehicleCurves(VehicleSpecification& S)
{
    VehicleSolverConfiguration& C = S.Solver;
    return {
        { "engine", "torque_curve", "Engine torque", "rpm", "N·m", &C.Engine.TorqueCurve, 0.0, 12000.0, 0.0, 1500.0 },
        { "engine", "exhaust_flow_curve", "Exhaust flow", "rpm", "kg/s", &C.Engine.ExhaustFlowCurve, 0.0, 12000.0, 0.0, 1.0 },
        { "engine", "exhaust_temperature_curve", "Exhaust temperature", "rpm", "K", &C.Engine.ExhaustTempCurve, 0.0, 12000.0, 300.0, 1400.0 },
        { "turbo", "boost_curve", "Turbo boost", "rpm", "bar", &C.Turbo.BoostPressureCurve, 0.0, 250000.0, 0.0, 3.0 },
        { "turbo", "multiplier_curve", "Turbo torque gain", "bar", "×", &C.Turbo.TorqueMultiplierCurve, 0.0, 3.0, 0.5, 3.0 },
        { "supercharger", "boost_curve", "Supercharger boost", "rpm", "bar", &C.Supercharger.BoostPressureCurve, 0.0, 40000.0, 0.0, 3.0 },
        { "supercharger", "multiplier_curve", "Supercharger torque gain", "bar", "×", &C.Supercharger.TorqueMultiplierCurve, 0.0, 3.0, 0.5, 3.0 },
        { "supercharger", "drag_curve", "Supercharger drag", "rpm", "N·m", &C.Supercharger.ParasiticDragCurve, 0.0, 40000.0, 0.0, 200.0 },
    };
}

} // namespace Frontier::Vehicle
