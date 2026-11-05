/*==========================================================================================================================================
                                                           VEHICLEINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 The vehicle page: overview with dyno sheet, performance estimates, quick choices and physics-thread stepping; interactive curve
//    authoring across all eight engine/turbo/supercharger curves; four-corner tyre pit bay with live carcass & Pacejka previews;
//    complete figure registry tabs; and the read-only live telemetry dock (shift LEDs, pedals, G-bars, rolling trace, 4-corner cards).

#include "Engine/Interface/Inspectors/VehicleInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/ActionRow.h"
#include "Engine/Interface/Controls/CaptionText.h"
#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/ControlTheme.h"
#include "Engine/Interface/Controls/FooterRow.h"
#include "Engine/Interface/Controls/MetricTiles.h"
#include "Engine/Interface/Controls/PageHeading.h"
#include "Engine/Interface/Controls/SectionRows.h"
#include "Engine/Interface/Controls/SelectRow.h"
#include "Engine/Interface/Controls/TabStrip.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/TyreInspector.h"
#include "Engine/PhysicalDynamics/Vehicle/PacejkaMagicFormula.h"
#include "Engine/PhysicalDynamics/Vehicle/TraceContactTyre.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/VehicleReadings.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Frontier
{

namespace
{

constexpr const char* SectionLabels[VehicleInspector::SectionCount] = {
    "Overview", "Chassis", "Engine", "Driveline", "Tyres", "Brakes", "Aero", "Fluids", "Telemetry" };
constexpr int UpperTabs = 5;

struct SectionTitle
{
    const char* Section;
    const char* Title;
    const char* Glyph;
    const char* Caption;
};

constexpr SectionTitle SectionTitles[] = {
    { "geometry", "Chassis and identity", "Car", "Mass, centre of mass, inertia and wheel sizes (struts and wheels sit on the authored sockets)." },
    { "suspension.front", "Front struts", "SlidersHorizontal", "GRIT's strut, rated for the corner's sprung mass at its ride frequency." },
    { "suspension.rear", "Rear struts", "SlidersHorizontal", "GRIT's strut, rated for the corner's sprung mass at its ride frequency." },
    { "bars.front", "Front anti-roll bar", "Activity", "Resists differential compression between left and right front struts." },
    { "bars.rear", "Rear anti-roll bar", "Activity", "Resists differential compression between left and right rear struts." },
    { "suspension", "Suspension", "SlidersHorizontal", "" },
    { "steering", "Steering", "Compass", "Lock, rate and Ackermann inner-wheel tightening." },
    { "engine", "Engine", "Gauge", "Rev limits, internal friction, pumping loss and crankshaft rotating inertia." },
    { "induction", "Induction", "Wind", "Turbocharged (exhaust spool), supercharged (belt-driven positive displacement, zero lag, parasitic drag) or aspirated." },
    { "turbo", "Turbocharger", "Wind", "Turbine/compressor inertia, bearing friction, wastegate, blow-off valve and efficiencies." },
    { "supercharger", "Supercharger", "Wind", "Positive-displacement blower, pulley ratio, boost ceiling, bypass valve and parasitic crank drag." },
    { "charge", "Charge air", "Thermometer", "Compressor outlet after the intercooler; warm charge air reduces air density and boost torque." },
    { "gearbox", "Gearbox", "Cpu", "Final drive, mechanical efficiency and automated shift points." },
    { "clutch", "Clutch", "Cpu", "Torque capacity and engagement slip thresholds." },
    { "differential", "Differential", "Cpu", "Open, locked, Salisbury limited-slip or active torque vectoring." },
    { "tyre", "Tyre", "CircleDot", "Nominal cold inflation pressure, carcass mass, rolling radius, peak grip and rolling resistance." },
    { "contact", "Contact fan and radial spring", "Target", "Ray fan locates the road contact plane; radial spring carries load in Spring mode." },
    { "carcass", "Carcass soft body (Spring · XPBD · 3D FEM)", "GridLattice", "Selects Spring, 84-particle XPBD lattice or 320-node Hex8 Neo-Hookean 3D FEM, plus compliance and continuum moduli." },
    { "slip", "Transient slip", "TrendingUp", "Relaxation-length transient slip integration and low-speed contact damping." },
    { "thermal", "Tyre temperature and gas pressure", "Thermometer", "Three-layer thermal network (tread, carcass, enclosed gas), ideal-gas pressure coupling and compound temperature window." },
    { "wear", "Tread wear", "Timer", "Tread depth abraded by sliding power; grip degrades smoothly with worn depth." },
    { "pacejka", "Magic Formula (MF6.1)", "TrendingUp", "Project Drive Pacejka coefficients, combined-slip weighting and MF6.1 inflation-pressure sensitivity." },
    { "brakes", "Brakes", "Shield", "Hydraulic four-disc rotors with thermal fade, or simplified constant torque." },
    { "abs", "ABS", "Shield", "Per-wheel brake pressure modulation holding longitudinal slip near peak friction." },
    { "traction", "Traction control", "Shield", "Engine torque trim when driven-wheel slip exceeds target." },
    { "aero", "Aerodynamics", "Wind", "Body drag/lift, rear wing, front splitter, ground-effect floor diffuser, side skirts and eddy fins." },
    { "aero.body", "Body", "Car", "" },
    { "aero.wing", "Rear wing", "Wind", "" },
    { "aero.splitter", "Front splitter", "Wind", "" },
    { "aero.diffuser", "Floor and diffuser", "Wind", "" },
    { "aero.skirt_left", "Left side skirt", "Wind", "" },
    { "aero.skirt_right", "Right side skirt", "Wind", "" },
    { "aero.fins", "Eddy fins", "Wind", "" },
    { "fuel", "Fuel", "Droplets", "Brake-specific fuel consumption; burnt fuel reduces live Jolt chassis mass and inertia." },
    { "fluids", "Coolant and oil", "Thermometer", "Thermostat, speed-dependent airflow cooling and cold-oil viscous friction." },
};

const SectionTitle* ResolveTitle(const std::string& Section)
{
    for (const SectionTitle& Entry : SectionTitles)
        if (Section == Entry.Section) return &Entry;
    return nullptr;
}

float SampleNetEngineTorque(const Vehicle::VehicleSolverConfiguration& Solver, float Rpm)
{
    float Torque = Solver.Engine.TorqueCurve.Sample(Rpm);
    if (Solver.Induction == Vehicle::InductionType::Turbocharged)
    {
        const float Ceiling  = Solver.RaceTune ? Solver.Turbo.WastegateMaxBoost * 1.25f : Solver.Turbo.WastegateMaxBoost;
        const float SpoolRpm = Rpm * 18.0f;
        const float BoostBar = std::min(Ceiling, std::max(0.0f, Solver.Turbo.BoostPressureCurve.Sample(SpoolRpm)));
        const float Gain     = std::max(1.0f, Solver.Turbo.TorqueMultiplierCurve.Sample(BoostBar));
        Torque *= Gain;
    }
    else if (Solver.Induction == Vehicle::InductionType::Supercharged)
    {
        const float ChargerRpm = Solver.Supercharger.ChargerRPM(Rpm);
        const float Ceiling    = Solver.RaceTune ? Solver.Supercharger.MaxBoost_Race : Solver.Supercharger.MaxBoost_Pump;
        const float BoostBar   = std::min(Ceiling, std::max(0.0f, Solver.Supercharger.BoostPressureCurve.Sample(ChargerRpm)));
        const float Gain       = std::max(1.0f, Solver.Supercharger.TorqueMultiplierCurve.Sample(BoostBar));
        const float DragNm     = std::max(0.0f, Solver.Supercharger.ParasiticDragCurve.Sample(ChargerRpm));
        Torque                 = std::max(0.0f, Torque * Gain - DragNm);
    }
    return Torque;
}

void ResolveNetPeaks(const Vehicle::VehicleSolverConfiguration& Solver, float& PeakTorque, float& TorqueRpm, float& PeakPowerWatts, float& PowerRpm)
{
    PeakTorque = TorqueRpm = PeakPowerWatts = PowerRpm = 0.0f;
    const float Redline = std::max(1000.0f, Solver.Engine.RedlineRPM);
    for (int Step = 0; Step <= 200; ++Step)
    {
        const float Rpm   = Redline * static_cast<float>(Step) / 200.0f;
        const float Nm    = SampleNetEngineTorque(Solver, Rpm);
        const float Watts = Nm * Rpm * 0.104719755f;
        if (Nm > PeakTorque)
        {
            PeakTorque = Nm;
            TorqueRpm  = Rpm;
        }
        if (Watts > PeakPowerWatts)
        {
            PeakPowerWatts = Watts;
            PowerRpm       = Rpm;
        }
    }
}

struct PerformanceSummary
{
    float ZeroToHundredSeconds = 0.0f;
    float TopSpeedKmh          = 0.0f;
    float BrakeDistanceMetres  = 0.0f;
};

PerformanceSummary EstimatePerformance(const Vehicle::VehicleSpecification& Specification)
{
    PerformanceSummary                        Out{};
    const Vehicle::VehicleSolverConfiguration Solver      = Vehicle::ResolveSolverConfiguration(Specification);
    const Vehicle::VehicleGeometry&           Geometry    = Specification.Geometry;
    const float                               TotalMass   = std::max(300.0f, Geometry.Mass);
    const float                               Radius      = std::max(0.18f, Geometry.TyreRadius);
    const float                               Redline     = std::max(2000.0f, Solver.Engine.RedlineRPM);
    const std::vector<float>                  ForwardGears = Vehicle::QueryForwardRatios(Specification);
    const float                               FinalDrive   = std::max(0.5f, Solver.Transmission.FinalDriveRatio);
    const float                               Efficiency   = std::clamp(Solver.Transmission.DriveEfficiency, 0.5f, 1.0f);
    const float                               AirDensity   = std::max(0.5f, Solver.Aero.AirDensity_kgm3);
    const float                               CdA          = Solver.Aero.Enabled
                                                                 ? std::max(0.15f, Solver.Aero.VehicleBody.CoeffDrag * Solver.Aero.VehicleBody.FrontalArea_m2)
                                                                 : 0.64f;
    const float                               DragFactor   = 0.5f * AirDensity * CdA;
    const bool                                FrontDriven  = Solver.Wheels.size() >= 2u && (Solver.Wheels[0].Driven || Solver.Wheels[1].Driven);
    const bool                                RearDriven   = Solver.Wheels.size() >= 4u && (Solver.Wheels[2].Driven || Solver.Wheels[3].Driven);
    const float                               DrivenShare  = (FrontDriven && RearDriven)
                                                                 ? 1.0f
                                                                 : (FrontDriven ? Geometry.FrontWeightFraction
                                                                                : (1.0f - Geometry.FrontWeightFraction) + 0.12f);
    const float                               PeakMu       = std::clamp(Solver.GripCoefficient, 0.5f, 2.2f);
    const float                               TractionCap  = TotalMass * 9.81f * std::clamp(DrivenShare, 0.35f, 1.0f) * PeakMu;

    float Speed   = 0.0f;
    float Elapsed = 0.0f;
    const float Dt = 0.01f;
    while (Speed < 27.7778f && Elapsed < 30.0f)
    {
        float BestDriveForce = 0.0f;
        for (const float GearRatio : ForwardGears)
        {
            const float Overall = GearRatio * FinalDrive;
            const float WheelOmega = Speed / Radius;
            const float Rpm = std::clamp(WheelOmega * Overall * 9.5492966f, Solver.Engine.IdleRPM * 1.3f, Redline * 1.02f);
            if (Rpm > Redline) continue;
            const float Force = SampleNetEngineTorque(Solver, Rpm) * Overall * Efficiency / Radius;
            BestDriveForce    = std::max(BestDriveForce, Force);
        }
        const float DriveForce = std::min(BestDriveForce, TractionCap);
        const float DragForce  = DragFactor * Speed * Speed + Solver.RollingResistance * TotalMass * 9.81f;
        const float Accel      = std::max(0.25f, (DriveForce - DragForce) / TotalMass);
        Speed += Accel * Dt;
        Elapsed += Dt;
    }
    Out.ZeroToHundredSeconds = Elapsed + 0.18f * static_cast<float>(std::max<size_t>(1u, ForwardGears.size() > 2u ? 2u : 1u));

    float PeakWatts = 0.0f, UnusedA = 0.0f, UnusedB = 0.0f, UnusedC = 0.0f;
    ResolveNetPeaks(Solver, UnusedA, UnusedB, PeakWatts, UnusedC);
    const float EffectivePower = PeakWatts * Efficiency;
    float TopSpeedMps = 40.0f;
    for (int Iteration = 0; Iteration < 24; ++Iteration)
    {
        const float Drag = DragFactor * TopSpeedMps * TopSpeedMps + Solver.RollingResistance * TotalMass * 9.81f;
        const float Available = EffectivePower / std::max(5.0f, TopSpeedMps);
        TopSpeedMps = std::clamp(TopSpeedMps + 0.35f * (Available - Drag) / std::max(20.0f, Drag / TopSpeedMps), 10.0f, 140.0f);
    }
    if (!ForwardGears.empty())
    {
        const float TopGearRatio = ForwardGears.back() * FinalDrive;
        const float GearLimitedMps = (Redline / 9.5492966f) / std::max(0.1f, TopGearRatio) * Radius;
        TopSpeedMps = std::min(TopSpeedMps, GearLimitedMps);
    }
    Out.TopSpeedKmh = TopSpeedMps * 3.6f;

    const float BrakeDecelG     = std::clamp(PeakMu * (Solver.Abs.Enabled ? 0.98f : 0.86f), 0.45f, 2.0f);
    const float InitialBrakeMps = 27.7778f;
    Out.BrakeDistanceMetres     = (InitialBrakeMps * InitialBrakeMps) / (2.0f * BrakeDecelG * 9.81f);
    return Out;
}

ImU32 ParseHexColour(const char* Hex, uint8_t Alpha = 255)
{
    if (Hex == nullptr || Hex[0] != '#' || std::strlen(Hex) < 7) return IM_COL32(220, 180, 74, Alpha);
    unsigned int R = 220, G = 180, B = 74;
    std::sscanf(Hex + 1, "%02x%02x%02x", &R, &G, &B);
    return IM_COL32(R, G, B, Alpha);
}

void RasterizeProgressBar(ImDrawList* Draw, ImVec2 Origin, float Width, float Height, float Fraction, ImU32 FillColour, const char* LeftLabel, const char* RightLabel, float Scale)
{
    const float Clamped = std::clamp(Fraction, 0.0f, 1.0f);
    Draw->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + Height), IM_COL32(22, 24, 28, 255), 4.0f * Scale);
    if (Clamped > 0.002f)
        Draw->AddRectFilled(Origin, ImVec2(Origin.x + Width * Clamped, Origin.y + Height), FillColour, 4.0f * Scale);
    Draw->AddRect(Origin, ImVec2(Origin.x + Width, Origin.y + Height), IM_COL32(255, 255, 255, 22), 4.0f * Scale);
    if (LeftLabel != nullptr && LeftLabel[0] != '\0')
        Draw->AddText(ImVec2(Origin.x + 6.0f * Scale, Origin.y + 1.0f * Scale), IM_COL32(220, 224, 230, 240), LeftLabel);
    if (RightLabel != nullptr && RightLabel[0] != '\0')
    {
        const ImVec2 TextSize = ImGui::CalcTextSize(RightLabel);
        Draw->AddText(ImVec2(Origin.x + Width - TextSize.x - 6.0f * Scale, Origin.y + 1.0f * Scale), IM_COL32(185, 190, 198, 235), RightLabel);
    }
}

} // namespace

std::string VehicleInspector::QueryDirectoryReadout() const
{
    const auto& Solver = Parameters.Specification.Solver;
    const char* InductionTag = Solver.Induction == Vehicle::InductionType::Supercharged
                                   ? " · SC"
                                   : (Solver.Induction == Vehicle::InductionType::Turbocharged ? " · Turbo" : "");
    return std::string(Vehicle::TyreCarcassName(Solver.Carcass)) + InductionTag;
}

void VehicleInspector::ConstructCurveChart(ControlColumn& Page, const char* Title, const Vehicle::Curve& Torque, float RedlineRpm)
{
    const Vehicle::VehicleSolverConfiguration& Solver = Parameters.Specification.Solver;
    const bool   Boosted = Solver.Induction != Vehicle::InductionType::NaturallyAspirated;
    const float  Scale   = Page.QueryScale();
    const ImVec2 Origin  = Page.Space(176.0f, 4.0f, 10.0f);
    const float  Width   = Page.QueryWidth();
    const float  Height  = 176.0f * Scale;
    ImDrawList*  Draw    = ImGui::GetWindowDrawList();

    const ImU32 Card      = IM_COL32(26, 28, 33, 255);
    const ImU32 Guide     = IM_COL32(255, 255, 255, 18);
    const ImU32 BaseInk   = IM_COL32(232, 168, 74, Boosted ? 150 : 255);
    const ImU32 BoostInk  = IM_COL32(88, 214, 152, 255);
    const ImU32 PowerInk  = IM_COL32(142, 182, 232, 255);
    const ImU32 TextInk   = IM_COL32(156, 160, 168, 255);

    Draw->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + Height), Card, 12.0f * Scale);
    Draw->AddRect(Origin, ImVec2(Origin.x + Width, Origin.y + Height), IM_COL32(255, 255, 255, 20), 12.0f * Scale);

    const float Left = Origin.x + 36.0f * Scale, Right = Origin.x + Width - 36.0f * Scale;
    const float Top  = Origin.y + 28.0f * Scale, Bottom = Origin.y + Height - 22.0f * Scale;

    float PeakTorque = 0.0f, TorqueRpm = 0.0f, PeakPowerWatts = 0.0f, PowerRpm = 0.0f;
    ResolveNetPeaks(Solver, PeakTorque, TorqueRpm, PeakPowerWatts, PowerRpm);
    const float TorqueTop = std::max(100.0f, std::ceil(PeakTorque / 100.0f) * 100.0f);
    const float PowerTop  = std::max(50.0f, std::ceil((PeakPowerWatts * 0.001f) / 50.0f) * 50.0f);

    for (int Line = 1; Line <= 3; ++Line)
    {
        const float Y = Bottom - (Bottom - Top) * static_cast<float>(Line) / 4.0f;
        Draw->AddLine(ImVec2(Left, Y), ImVec2(Right, Y), Guide, 1.0f);
    }

    char Header[128];
    if (Boosted)
        std::snprintf(Header, sizeof(Header), "%s  ·  base (amber)  ·  boosted (mint)  ·  kW (blue)", Title);
    else
        std::snprintf(Header, sizeof(Header), "%s  ·  torque N·m (amber)  ·  power kW (blue)", Title);
    Draw->AddText(ImVec2(Origin.x + 12.0f * Scale, Origin.y + 7.0f * Scale), TextInk, Header);

    ImVec2 PreviousBase{}, PreviousBoost{}, PreviousPower{};
    for (int Step = 0; Step <= 120; ++Step)
    {
        const float Rpm    = std::max(1.0f, RedlineRpm) * static_cast<float>(Step) / 120.0f;
        const float BaseNm = Torque.Sample(Rpm);
        const float NetNm  = SampleNetEngineTorque(Solver, Rpm);
        const float NetKw  = NetNm * Rpm * 0.104719755f * 0.001f;
        const float X      = Left + (Right - Left) * static_cast<float>(Step) / 120.0f;

        const ImVec2 BasePoint(X, Bottom - (Bottom - Top) * std::clamp(BaseNm / TorqueTop, 0.0f, 1.0f));
        const ImVec2 BoostPoint(X, Bottom - (Bottom - Top) * std::clamp(NetNm / TorqueTop, 0.0f, 1.0f));
        const ImVec2 PowerPoint(X, Bottom - (Bottom - Top) * std::clamp(NetKw / PowerTop, 0.0f, 1.0f));
        if (Step > 0)
        {
            Draw->AddLine(PreviousBase, BasePoint, BaseInk, (Boosted ? 1.4f : 2.1f) * Scale);
            if (Boosted) Draw->AddLine(PreviousBoost, BoostPoint, BoostInk, 2.2f * Scale);
            Draw->AddLine(PreviousPower, PowerPoint, PowerInk, 1.7f * Scale);
        }
        PreviousBase  = BasePoint;
        PreviousBoost = BoostPoint;
        PreviousPower = PowerPoint;
    }

    char ScaleLabel[48];
    std::snprintf(ScaleLabel, sizeof(ScaleLabel), "%.0f", TorqueTop);
    Draw->AddText(ImVec2(Origin.x + 6.0f * Scale, Top - 6.0f * Scale), Boosted ? BoostInk : BaseInk, ScaleLabel);
    std::snprintf(ScaleLabel, sizeof(ScaleLabel), "%.0f", PowerTop);
    Draw->AddText(ImVec2(Right + 5.0f * Scale, Top - 6.0f * Scale), PowerInk, ScaleLabel);
    std::snprintf(ScaleLabel, sizeof(ScaleLabel), "0 – %.0f rpm  (peak %.0f N·m @ %.0f · %.0f kW @ %.0f)",
                  RedlineRpm, PeakTorque, TorqueRpm, PeakPowerWatts * 0.001f, PowerRpm);
    Draw->AddText(ImVec2(Left, Bottom + 4.0f * Scale), TextInk, ScaleLabel);
}

bool VehicleInspector::ConstructOverview(ControlColumn& Page)
{
    using namespace SectionRows;
    Vehicle::VehicleSolverConfiguration& Solver = Parameters.Specification.Solver;
    bool Changed = false;

    float PeakTorque = 0.0f, TorqueRpm = 0.0f, PeakPowerWatts = 0.0f, PowerRpm = 0.0f;
    ResolveNetPeaks(Solver, PeakTorque, TorqueRpm, PeakPowerWatts, PowerRpm);
    const PerformanceSummary Perf = EstimatePerformance(Parameters.Specification);

    char MassText[32], TorqueText[32], PowerText[32], RatioText[32];
    std::snprintf(MassText, sizeof(MassText), "%.0f", Parameters.Specification.Geometry.Mass);
    std::snprintf(TorqueText, sizeof(TorqueText), "%.0f", PeakTorque);
    std::snprintf(PowerText, sizeof(PowerText), "%.0f", PeakPowerWatts * 0.001f);
    std::snprintf(RatioText, sizeof(RatioText), "%.0f", PeakPowerWatts / std::max(1.0f, Parameters.Specification.Geometry.Mass));
    const std::array<MetricTile, 4> PrimaryTiles = { {
        { "Mass", MassText, "kg" },
        { "Peak torque", TorqueText, "N·m" },
        { "Peak power", PowerText, "kW" },
        { "Power / mass", RatioText, "W/kg" } } };
    MetricTiles::Construct(Page, PrimaryTiles.data(), static_cast<int>(PrimaryTiles.size()), MetricTiles::NoLead);

    char SprintText[32], TopSpeedText[32], BrakeText[32], BalanceText[32];
    const int FrontPct = static_cast<int>(std::lround(Parameters.Specification.Geometry.FrontWeightFraction * 100.0f));
    std::snprintf(SprintText, sizeof(SprintText), "%.2f", Perf.ZeroToHundredSeconds);
    std::snprintf(TopSpeedText, sizeof(TopSpeedText), "%.0f", Perf.TopSpeedKmh);
    std::snprintf(BrakeText, sizeof(BrakeText), "%.1f", Perf.BrakeDistanceMetres);
    std::snprintf(BalanceText, sizeof(BalanceText), "%d/%d", FrontPct, 100 - FrontPct);
    const std::array<MetricTile, 4> EstimateTiles = { {
        { "0–100 km/h", SprintText, "s" },
        { "Top speed", TopSpeedText, "km/h" },
        { "100–0 brake", BrakeText, "m" },
        { "Front / rear", BalanceText, "%" } } };
    MetricTiles::Construct(Page, EstimateTiles.data(), static_cast<int>(EstimateTiles.size()), MetricTiles::NoLead);

    ConstructCurveChart(Page, "Wide-open throttle dyno", Solver.Engine.TorqueCurve, Solver.Engine.RedlineRPM);

    BeginSection(Page, "Quick choices", "SlidersHorizontal", Vehicle::TyreCarcassName(Solver.Carcass),
                 "Soft-body tyre formulation, forced induction, electronic driver aids and thermal/fluid subsystems.");
    static constexpr const char* Carcasses[] = { "Spring", "XPBD", "3D FEM" };
    int SelectedCarcass = static_cast<int>(Solver.Carcass);
    if (SelectRow::Construct(Page, "VehicleCarcass", "Carcass formulation", Carcasses, 3, SelectedCarcass))
    {
        Solver.Carcass = static_cast<Vehicle::TyreCarcassCategory>(SelectedCarcass);
        Changed        = true;
    }
    static constexpr const char* Inductions[] = { "Naturally aspirated", "Turbocharged", "Supercharged" };
    int SelectedInduction = static_cast<int>(Solver.Induction);
    if (SelectRow::Construct(Page, "VehicleInduction", "Induction", Inductions, 3, SelectedInduction))
    {
        Solver.Induction = static_cast<Vehicle::InductionType>(SelectedInduction);
        Changed          = true;
    }
    Changed |= Switch(Page, "VehicleAbs", "ABS anti-lock braking", Solver.Abs.Enabled);
    Changed |= Switch(Page, "VehicleTraction", "Traction control", Solver.TractionControl.Enabled);
    Changed |= Switch(Page, "VehicleThermal", "Tyre temperature and gas-law pressure", Solver.TyreThermal.Enabled);
    Changed |= Switch(Page, "VehicleCharge", "Intercooler charge-air thermal", Solver.ChargeAir.Enabled);
    Changed |= Switch(Page, "VehicleWear", "Tread wear degradation", Solver.TyreWear.Enabled);
    Changed |= Switch(Page, "VehicleFuel", "Fuel burn and live chassis mass reduction", Solver.Fuel.Enabled);
    Changed |= Switch(Page, "VehicleFluids", "Coolant and oil thermal loop", Solver.Fluids.Enabled);
    Changed |= Switch(Page, "VehicleRace", "Race fuel calibration (+6% torque)", Solver.RaceTune);
    EndSection(Page);

    BeginSection(Page, "Simulation, driving and PhysicsThread", "Cpu", Parameters.ThreadedPhysics ? "240 Hz thread" : "inline",
                 "Possession, chase camera, dedicated fixed-cadence physics thread, Jolt collision substeps and start pose.");
    Changed |= Switch(Page, "VehicleVisible", "Visible in Scene Viewport", Parameters.Visible);
    Changed |= Switch(Page, "VehiclePossessed", "Possessed (W/S/A/D/Space/R drive while simulating)", Parameters.Possessed);
    Changed |= Switch(Page, "VehicleChase", "Chase camera follows behind vehicle (C toggles)", Parameters.ChaseCamera);
    Changed |= Switch(Page, "VehicleThreaded", "Run on dedicated fixed-rate PhysicsThread", Parameters.ThreadedPhysics);

    double StepHz = Parameters.PhysicsStepHz;
    if (Measure(Page, "VehicleStepHz", "Physics cadence", StepHz, 60.0, 1000.0, 10.0, "Hz"))
    {
        Parameters.PhysicsStepHz = static_cast<float>(StepHz);
        Changed                  = true;
    }
    Changed |= SectionRows::Count(Page, "VehicleCollisionSteps", "Jolt collision sub-steps", Parameters.CollisionSteps, 1, 8, "×");
    Changed |= SectionRows::Count(Page, "VehicleSolverSubsteps", "Vehicle solver sub-steps", Parameters.SolverSubsteps, 1, 8, "×");

    double StartX = Parameters.Spatial.PositionMetres[0];
    double StartY = Parameters.Spatial.PositionMetres[1];
    double StartZ = Parameters.Spatial.PositionMetres[2];
    double YawDeg = Parameters.Spatial.OrientationDegrees[2];
    if (Measure(Page, "VehicleStartX", "Start X", StartX, -500.0, 500.0, 0.5, "m"))
    {
        Parameters.Spatial.PositionMetres[0] = static_cast<float>(StartX);
        Parameters.Spatial.AdvanceRevision();
        Changed = true;
    }
    if (Measure(Page, "VehicleStartY", "Start Y", StartY, -500.0, 500.0, 0.5, "m"))
    {
        Parameters.Spatial.PositionMetres[1] = static_cast<float>(StartY);
        Parameters.Spatial.AdvanceRevision();
        Changed = true;
    }
    if (Measure(Page, "VehicleStartZ", "Start ground Z", StartZ, -50.0, 100.0, 0.1, "m"))
    {
        Parameters.Spatial.PositionMetres[2] = static_cast<float>(StartZ);
        Parameters.Spatial.AdvanceRevision();
        Changed = true;
    }
    if (Measure(Page, "VehicleStartYaw", "Start heading", YawDeg, -180.0, 180.0, 1.0, "°"))
    {
        Parameters.Spatial.OrientationDegrees[2] = static_cast<float>(YawDeg);
        Parameters.Spatial.AdvanceRevision();
        Changed = true;
    }
    EndSection(Page);
    return Changed;
}

bool VehicleInspector::ConstructCurveEditor(ControlColumn& Page)
{
    using namespace SectionRows;
    bool Changed = false;
    std::vector<Vehicle::VehicleCurve> Curves = Vehicle::EnumerateVehicleCurves(Parameters.Specification);
    if (Curves.empty()) return false;

    SelectedCurveIndex = std::clamp(SelectedCurveIndex, 0, static_cast<int>(Curves.size()) - 1);
    std::vector<const char*> CurveLabels;
    CurveLabels.reserve(Curves.size());
    for (const Vehicle::VehicleCurve& Entry : Curves) CurveLabels.push_back(Entry.Label.c_str());

    BeginSection(Page, "Interactive curve authoring", "TrendingUp", Curves[static_cast<size_t>(SelectedCurveIndex)].Section.c_str(),
                 "Drag handles to reshape any engine, turbocharger or supercharger map; double-click canvas to insert, right-click a handle to remove.");
    if (SelectRow::Construct(Page, "VehicleCurveSelect", "Authored map", CurveLabels.data(), static_cast<int>(CurveLabels.size()), SelectedCurveIndex))
    {
        SelectedCurvePoint = 0;
        DraggedCurvePoint  = -1;
    }

    Vehicle::VehicleCurve& ActiveEntry = Curves[static_cast<size_t>(SelectedCurveIndex)];
    Vehicle::Curve&        TargetCurve = *ActiveEntry.Target;
    if (TargetCurve.X.size() < 2u || TargetCurve.Y.size() < 2u)
    {
        TargetCurve = Vehicle::Curve{};
        TargetCurve.Add(0.0f, 1.0f);
        TargetCurve.Add(7000.0f, 1.0f);
    }
    SelectedCurvePoint = std::clamp(SelectedCurvePoint, 0, static_cast<int>(TargetCurve.X.size()) - 1);

    float MinX = TargetCurve.X.front(), MaxX = TargetCurve.X.back();
    if (MaxX - MinX < 1.0e-3f) MaxX = MinX + 1.0f;
    float MinY = 0.0f, MaxY = 1.0f;
    for (const float SampleY : TargetCurve.Y)
    {
        MinY = std::min(MinY, SampleY);
        MaxY = std::max(MaxY, SampleY);
    }
    const float SpanY = std::max(1.0e-2f, (MaxY - MinY) * 1.18f);
    MaxY = MinY + SpanY;

    const float  Scale  = Page.QueryScale();
    const ImVec2 Origin = Page.Space(172.0f, 4.0f, 8.0f);
    const float  Width  = Page.QueryWidth();
    const float  Height = 172.0f * Scale;
    ImDrawList*  Draw   = ImGui::GetWindowDrawList();

    Draw->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + Height), IM_COL32(24, 26, 31, 255), 10.0f * Scale);
    Draw->AddRect(Origin, ImVec2(Origin.x + Width, Origin.y + Height), IM_COL32(255, 255, 255, 22), 10.0f * Scale);

    const float Left = Origin.x + 38.0f * Scale, Right = Origin.x + Width - 18.0f * Scale;
    const float Top  = Origin.y + 24.0f * Scale, Bottom = Origin.y + Height - 22.0f * Scale;

    for (int Grid = 1; Grid <= 3; ++Grid)
    {
        const float Y = Bottom - (Bottom - Top) * static_cast<float>(Grid) / 4.0f;
        Draw->AddLine(ImVec2(Left, Y), ImVec2(Right, Y), IM_COL32(255, 255, 255, 16), 1.0f);
    }

    const auto ToScreen = [&](float X, float Y)
    {
        const float NormX = std::clamp((X - MinX) / (MaxX - MinX), 0.0f, 1.0f);
        const float NormY = std::clamp((Y - MinY) / (MaxY - MinY), 0.0f, 1.0f);
        return ImVec2(Left + (Right - Left) * NormX, Bottom - (Bottom - Top) * NormY);
    };

    ImGui::SetCursorScreenPos(Origin);
    ImGui::InvisibleButton("##VehicleCurveCanvas", ImVec2(Width, Height),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool   Hovered = ImGui::IsItemHovered();
    const ImVec2 Mouse   = ImGui::GetIO().MousePos;

    if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        int   BestPoint    = -1;
        float BestDistSq   = (12.0f * Scale) * (12.0f * Scale);
        for (size_t Index = 0; Index < TargetCurve.X.size(); ++Index)
        {
            const ImVec2 Handle = ToScreen(TargetCurve.X[Index], TargetCurve.Y[Index]);
            const float  Dx     = Mouse.x - Handle.x;
            const float  Dy     = Mouse.y - Handle.y;
            if (Dx * Dx + Dy * Dy <= BestDistSq)
            {
                BestDistSq = Dx * Dx + Dy * Dy;
                BestPoint  = static_cast<int>(Index);
            }
        }
        if (BestPoint >= 0)
        {
            SelectedCurvePoint = BestPoint;
            DraggedCurvePoint  = BestPoint;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) DraggedCurvePoint = -1;

    if (DraggedCurvePoint >= 0 && static_cast<size_t>(DraggedCurvePoint) < TargetCurve.X.size() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
    {
        const size_t PointIdx = static_cast<size_t>(DraggedCurvePoint);
        const float  NormX    = std::clamp((Mouse.x - Left) / std::max(1.0f, Right - Left), 0.0f, 1.0f);
        const float  NormY    = std::clamp((Bottom - Mouse.y) / std::max(1.0f, Bottom - Top), 0.0f, 1.0f);
        float        NewX     = MinX + NormX * (MaxX - MinX);
        const float  MarginX  = (MaxX - MinX) * 0.01f;
        if (PointIdx > 0u) NewX = std::max(NewX, TargetCurve.X[PointIdx - 1u] + MarginX);
        if (PointIdx + 1u < TargetCurve.X.size()) NewX = std::min(NewX, TargetCurve.X[PointIdx + 1u] - MarginX);
        if (PointIdx > 0u && PointIdx + 1u < TargetCurve.X.size()) TargetCurve.X[PointIdx] = NewX;
        TargetCurve.Y[PointIdx] = std::max(0.0f, MinY + NormY * (MaxY - MinY));
        Changed                 = true;
    }

    if (Hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && TargetCurve.X.size() < 20u)
    {
        const float NormX = std::clamp((Mouse.x - Left) / std::max(1.0f, Right - Left), 0.02f, 0.98f);
        const float NormY = std::clamp((Bottom - Mouse.y) / std::max(1.0f, Bottom - Top), 0.0f, 1.0f);
        const float NewX  = MinX + NormX * (MaxX - MinX);
        const float NewY  = std::max(0.0f, MinY + NormY * (MaxY - MinY));
        auto InsertAt = std::lower_bound(TargetCurve.X.begin(), TargetCurve.X.end(), NewX);
        const size_t Offset = static_cast<size_t>(std::distance(TargetCurve.X.begin(), InsertAt));
        if (Offset > 0u && Offset < TargetCurve.X.size() &&
            std::fabs(TargetCurve.X[Offset] - NewX) > (MaxX - MinX) * 0.01f &&
            std::fabs(TargetCurve.X[Offset - 1u] - NewX) > (MaxX - MinX) * 0.01f)
        {
            TargetCurve.X.insert(TargetCurve.X.begin() + static_cast<std::ptrdiff_t>(Offset), NewX);
            TargetCurve.Y.insert(TargetCurve.Y.begin() + static_cast<std::ptrdiff_t>(Offset), NewY);
            SelectedCurvePoint = static_cast<int>(Offset);
            Changed            = true;
        }
    }

    if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && TargetCurve.X.size() > 2u)
    {
        for (size_t Index = 0; Index < TargetCurve.X.size(); ++Index)
        {
            const ImVec2 Handle = ToScreen(TargetCurve.X[Index], TargetCurve.Y[Index]);
            const float  Dx     = Mouse.x - Handle.x;
            const float  Dy     = Mouse.y - Handle.y;
            if (Dx * Dx + Dy * Dy <= (12.0f * Scale) * (12.0f * Scale))
            {
                TargetCurve.X.erase(TargetCurve.X.begin() + static_cast<std::ptrdiff_t>(Index));
                TargetCurve.Y.erase(TargetCurve.Y.begin() + static_cast<std::ptrdiff_t>(Index));
                SelectedCurvePoint = std::clamp(SelectedCurvePoint, 0, static_cast<int>(TargetCurve.X.size()) - 1);
                Changed            = true;
                break;
            }
        }
    }

    ImVec2 PreviousSample{};
    for (int Step = 0; Step <= 100; ++Step)
    {
        const float  SampleX = MinX + (MaxX - MinX) * static_cast<float>(Step) / 100.0f;
        const float  SampleY = TargetCurve.Sample(SampleX);
        const ImVec2 Current = ToScreen(SampleX, SampleY);
        if (Step > 0) Draw->AddLine(PreviousSample, Current, IM_COL32(236, 174, 78, 255), 2.1f * Scale);
        PreviousSample = Current;
    }

    for (size_t Index = 0; Index < TargetCurve.X.size(); ++Index)
    {
        const ImVec2 Handle = ToScreen(TargetCurve.X[Index], TargetCurve.Y[Index]);
        const bool   Chosen = static_cast<int>(Index) == SelectedCurvePoint;
        Draw->AddCircleFilled(Handle, (Chosen ? 5.5f : 4.0f) * Scale,
                              Chosen ? IM_COL32(92, 220, 160, 255) : IM_COL32(236, 174, 78, 255));
        Draw->AddCircle(Handle, (Chosen ? 7.0f : 5.0f) * Scale, IM_COL32(16, 18, 22, 240), 0, 1.5f * Scale);
    }

    char Caption[128];
    std::snprintf(Caption, sizeof(Caption), "%s (%s → %s) · %zu knots",
                  ActiveEntry.Label.c_str(), ActiveEntry.AxisX.c_str(), ActiveEntry.AxisY.c_str(), TargetCurve.X.size());
    Draw->AddText(ImVec2(Origin.x + 12.0f * Scale, Origin.y + 5.0f * Scale), IM_COL32(170, 175, 184, 255), Caption);
    std::snprintf(Caption, sizeof(Caption), "%.2g – %.0f %s", MinX, MaxX, ActiveEntry.AxisX.c_str());
    Draw->AddText(ImVec2(Left, Bottom + 4.0f * Scale), IM_COL32(142, 146, 154, 255), Caption);
    std::snprintf(Caption, sizeof(Caption), "%.2g", MaxY);
    Draw->AddText(ImVec2(Origin.x + 6.0f * Scale, Top - 5.0f * Scale), IM_COL32(236, 174, 78, 255), Caption);

    SelectedCurvePoint = std::clamp(SelectedCurvePoint, 0, static_cast<int>(TargetCurve.X.size()) - 1);
    const size_t Knot  = static_cast<size_t>(SelectedCurvePoint);
    int KnotNumber     = SelectedCurvePoint + 1;
    if (SectionRows::Count(Page, "VehicleCurveKnotIdx", "Selected knot", KnotNumber, 1, static_cast<int>(TargetCurve.X.size()), ""))
        SelectedCurvePoint = std::clamp(KnotNumber - 1, 0, static_cast<int>(TargetCurve.X.size()) - 1);

    const double LowerX = Knot > 0u ? static_cast<double>(TargetCurve.X[Knot - 1u]) + 1.0e-3 : ActiveEntry.MinimumX;
    const double UpperX = Knot + 1u < TargetCurve.X.size() ? static_cast<double>(TargetCurve.X[Knot + 1u]) - 1.0e-3
                                                           : std::max(ActiveEntry.MaximumX, static_cast<double>(MaxX) * 1.25);
    double KnotX        = TargetCurve.X[Knot];
    double KnotY        = TargetCurve.Y[Knot];
    if (UpperX > LowerX + 1.0e-3 && Measure(Page, "VehicleCurveKnotX", "Knot X", KnotX, LowerX, UpperX, (UpperX - LowerX) / 250.0, ActiveEntry.AxisX.c_str()))
    {
        TargetCurve.X[Knot] = static_cast<float>(std::clamp(KnotX, LowerX, UpperX));
        Changed             = true;
    }
    const double UpperY = std::max(ActiveEntry.MaximumY, static_cast<double>(MaxY) * 1.4);
    if (Measure(Page, "VehicleCurveKnotY", "Knot Y", KnotY, ActiveEntry.MinimumY, UpperY, std::max(1.0e-3, (UpperY - ActiveEntry.MinimumY) / 300.0), ActiveEntry.AxisY.c_str()))
    {
        TargetCurve.Y[Knot] = static_cast<float>(std::max(ActiveEntry.MinimumY, KnotY));
        Changed             = true;
    }

    const std::array<ActionEntry, 2> KnotActions = { {
        { "##VehicleCurveInsert", "+ Insert knot after selected", TargetCurve.X.size() < 20u, nullptr },
        { "##VehicleCurveDelete", "- Remove selected knot", TargetCurve.X.size() > 2u, nullptr } } };
    const int KnotAction = ActionRow::Construct(Page, KnotActions, ActionRowLook{ .MarginTop = 8.0f, .MarginBottom = 10.0f });
    if (KnotAction == 0 && TargetCurve.X.size() < 20u)
    {
        const size_t LeftIdx  = Knot + 1u < TargetCurve.X.size() ? Knot : (TargetCurve.X.size() - 2u);
        const float  MidX     = 0.5f * (TargetCurve.X[LeftIdx] + TargetCurve.X[LeftIdx + 1u]);
        const float  MidY     = TargetCurve.Sample(MidX);
        TargetCurve.X.insert(TargetCurve.X.begin() + static_cast<std::ptrdiff_t>(LeftIdx + 1u), MidX);
        TargetCurve.Y.insert(TargetCurve.Y.begin() + static_cast<std::ptrdiff_t>(LeftIdx + 1u), MidY);
        SelectedCurvePoint = static_cast<int>(LeftIdx + 1u);
        Changed            = true;
    }
    else if (KnotAction == 1 && TargetCurve.X.size() > 2u)
    {
        TargetCurve.X.erase(TargetCurve.X.begin() + static_cast<std::ptrdiff_t>(Knot));
        TargetCurve.Y.erase(TargetCurve.Y.begin() + static_cast<std::ptrdiff_t>(Knot));
        SelectedCurvePoint = std::clamp(SelectedCurvePoint, 0, static_cast<int>(TargetCurve.X.size()) - 1);
        Changed            = true;
    }
    EndSection(Page);
    return Changed;
}

bool VehicleInspector::ConstructTyreBay(ControlColumn& Page)
{
    using namespace SectionRows;
    bool Changed = false;
    const RecordToken ThisVehicle = InspectorRuntime::Instance().QueryInspectedToken();

    struct CornerSlot
    {
        RecordToken    Token{};
        TyreInspector* Inspector = nullptr;
    };
    std::array<CornerSlot, 4> Bay{};
    int FoundCorners = 0;

    if (ThisVehicle.IsValid())
    {
        std::vector<RecordToken> AllTokens;
        SceneDirectory::Instance().CopyRegistrationOrder(AllTokens);
        for (const RecordToken& Candidate : AllTokens)
        {
            if (SceneDirectory::Instance().QueryParent(Candidate) != ThisVehicle) continue;
            if (auto* TyrePage = dynamic_cast<TyreInspector*>(InspectorRuntime::Instance().FindInspector(Candidate)))
            {
                const uint32_t Corner = TyrePage->QueryConfiguration().Corner & 3u;
                Bay[Corner]           = { Candidate, TyrePage };
            }
        }
        for (const CornerSlot& Slot : Bay)
            if (Slot.Inspector != nullptr) ++FoundCorners;
    }

    char BaySummary[32];
    std::snprintf(BaySummary, sizeof(BaySummary), "%d / 4 fitted", FoundCorners);
    BeginSection(Page, "Four-corner tyre bay and pit lane", "CircleDot", BaySummary,
                 "Enclosed Tyre records per corner: compound, cold inflation pressure and pit-lane detachment.");

    if (ThisVehicle.IsValid() && FoundCorners < 4)
    {
        const ActionEntry FitEntry{ "##FitFourTyres", "Fit 4-corner tyre set (FL · FR · RL · RR)", true, nullptr };
        if (ActionRow::Construct(Page, std::span<const ActionEntry>(&FitEntry, 1), ActionRowLook{ .MarginTop = 6.0f, .MarginBottom = 10.0f }) == 0)
        {
            InspectorRuntime::Instance().RequestWorkspaceAction("fit-tyres:" + std::to_string(ThisVehicle.Value));
        }
    }

    static constexpr const char* CornerTitles[4] = { "Front left (FL)", "Front right (FR)", "Rear left (RL)", "Rear right (RR)" };
    std::array<const char*, 5> CompoundNames{};
    for (size_t Index = 0; Index < CompoundNames.size(); ++Index) CompoundNames[Index] = TyreCompounds[Index].Name;

    for (uint32_t Corner = 0; Corner < 4u; ++Corner)
    {
        CornerSlot& Slot = Bay[Corner];
        SubHeading(Page, CornerTitles[Corner]);
        if (Slot.Inspector == nullptr)
        {
            CaptionText::Construct(Page, "No enclosed Tyre record on this corner (uses vehicle default tyre).", 2.0f, 8.0f);
            continue;
        }
        TyreConfiguration& Fit = Slot.Inspector->AccessConfiguration();
        bool CornerEdited      = false;
        const std::string Prefix = "VehicleBay" + std::to_string(Corner);
        CornerEdited |= Switch(Page, (Prefix + ".Fitted").c_str(), "Mounted on hub (off = detached on pit apron)", Fit.Enclosed);

        int CompoundIdx = 1;
        for (size_t Index = 0; Index < CompoundNames.size(); ++Index)
            if (Fit.Compound == CompoundNames[Index]) CompoundIdx = static_cast<int>(Index);
        if (SelectRow::Construct(Page, (Prefix + ".Compound").c_str(), "Compound", CompoundNames.data(), 5, CompoundIdx))
        {
            Fit.Compound = TyreCompounds[CompoundIdx].Name;
            CornerEdited = true;
        }
        double ColdKpa = Fit.ColdPressure * 0.001;
        if (Measure(Page, (Prefix + ".Cold").c_str(), "Cold pressure", ColdKpa, 30.0, 400.0, 1.0, "kPa"))
        {
            Fit.ColdPressure = static_cast<float>(ColdKpa * 1000.0);
            CornerEdited     = true;
        }
        if (CornerEdited)
        {
            Fit.AdvanceRevision();
            Changed = true;
        }
        const std::string ButtonId = "##SelectTyre" + std::to_string(Corner);
        const ActionEntry OpenEntry{ ButtonId.c_str(), "Open tyre record", true, nullptr };
        if (ActionRow::Construct(Page, std::span<const ActionEntry>(&OpenEntry, 1), ActionRowLook{ .MarginTop = 4.0f, .MarginBottom = 10.0f }) == 0)
        {
            InspectorRuntime::Instance().RequestWorkspaceAction("select:" + std::to_string(Slot.Token.Value));
        }
    }
    EndSection(Page);
    return Changed;
}

void VehicleInspector::ConstructTyreMechanicsPreview(ControlColumn& Page)
{
    const Vehicle::VehicleSolverConfiguration Solver   = Vehicle::ResolveSolverConfiguration(Parameters.Specification);
    const Vehicle::VehicleGeometry&           Geometry = Parameters.Specification.Geometry;

    const float  Scale  = Page.QueryScale();
    const ImVec2 Origin = Page.Space(164.0f, 4.0f, 10.0f);
    const float  Width  = Page.QueryWidth();
    const float  Height = 164.0f * Scale;
    ImDrawList*  Draw   = ImGui::GetWindowDrawList();

    Draw->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + Height), IM_COL32(25, 27, 32, 255), 12.0f * Scale);
    Draw->AddRect(Origin, ImVec2(Origin.x + Width, Origin.y + Height), IM_COL32(255, 255, 255, 20), 12.0f * Scale);

    const float MidX  = Origin.x + Width * 0.5f;
    const float LeftA = Origin.x + 14.0f * Scale, RightA = MidX - 10.0f * Scale;
    const float LeftB = MidX + 10.0f * Scale, RightB = Origin.x + Width - 14.0f * Scale;
    const float Top   = Origin.y + 26.0f * Scale, Bottom = Origin.y + Height - 20.0f * Scale;

    Draw->AddLine(ImVec2(MidX, Origin.y + 10.0f * Scale), ImVec2(MidX, Origin.y + Height - 10.0f * Scale), IM_COL32(255, 255, 255, 20), 1.0f);

    // Left plot: Radial Load vs Deflection at Authored Pressure vs 55 kPa Low Pressure
    const double AuthoredP  = Solver.Tyre.InflationPressure;
    const double StiffNom   = Vehicle::ResolveCarcassStiffness(Solver.CarcassTuning, AuthoredP);
    const double StiffLow   = Vehicle::ResolveCarcassStiffness(Solver.CarcassTuning, 55000.0);
    const float  MaxLoadN   = 9000.0f;
    const float  MaxDeflM   = 0.050f;
    ImVec2 PrevNom{}, PrevLow{};
    for (int Step = 0; Step <= 50; ++Step)
    {
        const float Defl  = MaxDeflM * static_cast<float>(Step) / 50.0f;
        const float LoadN = static_cast<float>(StiffNom * Defl);
        const float LoadL = static_cast<float>(StiffLow * Defl);
        const float X     = LeftA + (RightA - LeftA) * (Defl / MaxDeflM);
        const ImVec2 PtN(X, Bottom - (Bottom - Top) * std::clamp(LoadN / MaxLoadN, 0.0f, 1.0f));
        const ImVec2 PtL(X, Bottom - (Bottom - Top) * std::clamp(LoadL / MaxLoadN, 0.0f, 1.0f));
        if (Step > 0)
        {
            Draw->AddLine(PrevNom, PtN, IM_COL32(92, 214, 152, 255), 2.0f * Scale);
            Draw->AddLine(PrevLow, PtL, IM_COL32(232, 134, 88, 220), 1.6f * Scale);
        }
        PrevNom = PtN;
        PrevLow = PtL;
    }
    char Label[96];
    std::snprintf(Label, sizeof(Label), "Carcass Fz(δ) · %.0f vs 55 kPa", AuthoredP * 0.001);
    Draw->AddText(ImVec2(LeftA, Origin.y + 6.0f * Scale), IM_COL32(164, 168, 176, 255), Label);
    Draw->AddText(ImVec2(LeftA, Bottom + 3.0f * Scale), IM_COL32(138, 142, 150, 255), "0 – 50 mm deflection (9 kN)");

    // Right plot: Pacejka Fx(κ) and Fy(α) at Static Front Corner Load
    const float StaticCornerLoad = std::max(500.0f, 0.5f * Geometry.Mass * 9.81f * Geometry.FrontWeightFraction);
    const float ForceTop         = std::max(2000.0f, StaticCornerLoad * 1.45f);
    Vehicle::PacejkaMagicFormula Pacejka;
    Pacejka.AssignParameters(Solver.TyrePacejka);
    const float NominalPressure  = std::max(10000.0f, Solver.TyrePacejka.NominalPressure);
    const float PressureDev      = Solver.PressureSensitiveTyre
                                       ? static_cast<float>((AuthoredP - NominalPressure) / NominalPressure)
                                       : 0.0f;
    ImVec2 PrevFx{}, PrevFy{};
    for (int Step = 0; Step <= 60; ++Step)
    {
        const float Frac  = static_cast<float>(Step) / 60.0f;
        const float Kappa = Frac * 0.50f;
        const float Alpha = Frac * 0.35f;
        const float PureX = Pacejka.LongitudinalForce(Kappa, 0.0f, StaticCornerLoad, PressureDev);
        const float PureY = Pacejka.LateralForce(Alpha, 0.0f, StaticCornerLoad, PressureDev);
        const float X     = LeftB + (RightB - LeftB) * Frac;
        const ImVec2 PtX(X, Bottom - (Bottom - Top) * std::clamp(std::fabs(PureX) / ForceTop, 0.0f, 1.0f));
        const ImVec2 PtY(X, Bottom - (Bottom - Top) * std::clamp(std::fabs(PureY) / ForceTop, 0.0f, 1.0f));
        if (Step > 0)
        {
            Draw->AddLine(PrevFx, PtX, IM_COL32(236, 174, 78, 255), 2.0f * Scale);
            Draw->AddLine(PrevFy, PtY, IM_COL32(138, 184, 236, 255), 1.8f * Scale);
        }
        PrevFx = PtX;
        PrevFy = PtY;
    }
    Draw->AddText(ImVec2(LeftB, Origin.y + 6.0f * Scale), IM_COL32(164, 168, 176, 255), "Pacejka Fx / Fy slip curves");
    std::snprintf(Label, sizeof(Label), "Fz = %.0f N (amber Fx · blue Fy)", StaticCornerLoad);
    Draw->AddText(ImVec2(LeftB, Bottom + 3.0f * Scale), IM_COL32(138, 142, 150, 255), Label);
}

bool VehicleInspector::ConstructFigures(ControlColumn& Page, const char* const* Sections, int Count)
{
    using namespace SectionRows;
    bool Changed = false;
    std::vector<Vehicle::VehicleFigure> Figures = Vehicle::EnumerateVehicleFigures(Parameters.Specification);
    for (int Index = 0; Index < Count; ++Index)
    {
        const std::string   Section = Sections[Index];
        const SectionTitle* Title   = ResolveTitle(Section);
        const bool          Formula = Section == "pacejka";
        BeginSection(Page, Title != nullptr ? Title->Title : Section.c_str(), Title != nullptr ? Title->Glyph : "SlidersHorizontal", "",
                     Title != nullptr ? Title->Caption : "");
        if (Formula)
        {
            const ActionEntry FormulaEntry{ "##VehicleFormulaOpen", FormulaOpen ? "Hide MF6.1 coefficients" : "Show all MF6.1 coefficients", true, nullptr };
            if (ActionRow::Construct(Page, std::span<const ActionEntry>(&FormulaEntry, 1), ActionRowLook{ .MarginTop = 4.0f, .MarginBottom = 10.0f }) == 0)
                FormulaOpen = !FormulaOpen;
        }
        for (Vehicle::VehicleFigure& Figure : Figures)
        {
            if (Figure.Section != Section) continue;
            if (Formula && !FormulaOpen && Figure.Key.rfind("relaxation", 0) != 0 && Figure.Key != "nominal_pressure_pa" &&
                Figure.Category != Vehicle::VehicleFigureCategory::Switch) continue;
            const std::string Identifier = "Vehicle." + Figure.Section + "." + Figure.Key;
            switch (Figure.Category)
            {
                case Vehicle::VehicleFigureCategory::Switch:
                {
                    bool Enabled = Figure.Read() > 0.5;
                    if (Switch(Page, Identifier.c_str(), Figure.Label.c_str(), Enabled))
                    {
                        Figure.Write(Enabled ? 1.0 : 0.0);
                        Changed = true;
                    }
                    break;
                }
                case Vehicle::VehicleFigureCategory::Whole:
                {
                    int WholeQuantity = static_cast<int>(std::lround(Figure.Read()));
                    if (SectionRows::Count(Page, Identifier.c_str(), Figure.Label.c_str(), WholeQuantity,
                                           static_cast<int>(Figure.Minimum), static_cast<int>(Figure.Maximum), Figure.Unit.c_str()))
                    {
                        Figure.Write(WholeQuantity);
                        Changed = true;
                    }
                    break;
                }
                case Vehicle::VehicleFigureCategory::Choice:
                {
                    std::vector<const char*> Options;
                    for (const std::string& Choice : Figure.Choices) Options.push_back(Choice.c_str());
                    int SelectedChoice = static_cast<int>(std::lround(Figure.Read()));
                    if (SelectRow::Construct(Page, Identifier.c_str(), Figure.Label.c_str(), Options.data(),
                                             static_cast<int>(Options.size()), SelectedChoice))
                    {
                        Figure.Write(SelectedChoice);
                        Changed = true;
                    }
                    break;
                }
                case Vehicle::VehicleFigureCategory::Real:
                {
                    double       Shown = Figure.Read() * Figure.Display + Figure.Offset;
                    const double Step  = (Figure.Maximum - Figure.Minimum) / 400.0;
                    if (Measure(Page, Identifier.c_str(), Figure.Label.c_str(), Shown, Figure.Minimum, Figure.Maximum, Step, Figure.Unit.c_str()))
                    {
                        Figure.Write((Shown - Figure.Offset) / Figure.Display);
                        Changed = true;
                    }
                    break;
                }
            }
        }
        EndSection(Page);
    }
    return Changed;
}

bool VehicleInspector::ConstructRatios(ControlColumn& Page)
{
    using namespace SectionRows;
    bool               Changed = false;
    std::vector<float> Ratios  = Vehicle::QueryForwardRatios(Parameters.Specification);
    BeginSection(Page, "Gear ratios", "Cpu", "", "Forward gears, first to top (reverse and neutral keep their standard slots).");
    for (size_t Gear = 0; Gear < Ratios.size(); ++Gear)
    {
        char Identifier[48], Label[32];
        std::snprintf(Identifier, sizeof(Identifier), "Vehicle.gearbox.ratio%zu", Gear);
        std::snprintf(Label, sizeof(Label), "Gear %zu", Gear + 1);
        double GearRatio = Ratios[Gear];
        if (Measure(Page, Identifier, Label, GearRatio, 0.3, 6.0, 0.01, "×"))
        {
            Ratios[Gear] = static_cast<float>(GearRatio);
            Changed      = true;
        }
    }
    if (Changed) Vehicle::AssignForwardRatios(Parameters.Specification, Ratios);
    EndSection(Page);
    return Changed;
}

void VehicleInspector::ConstructTelemetry(ControlColumn& Page)
{
    using namespace SectionRows;
    VehicleReading Reading;
    const bool     Live = QueryVehicleReading(InspectorRuntime::Instance().QueryInspectedToken(), Reading) && Reading.Simulating;
    BeginSection(Page, "Telemetry dock", "Activity", Live ? "LIVE · READ-ONLY" : "STOPPED",
                 "Read-only snapshot from the physics thread. Press Simulate (Alt+S) and drive with W/S/A/D/Space/R; nothing here mutates the record.");
    if (!Live)
    {
        CaptionText::Construct(Page, "Standby: simulation is stopped. Press Play / Alt+S in the Scene Viewport to stream live 240 Hz telemetry.", 4.0f, 10.0f);
        EndSection(Page);
        return;
    }

    const Vehicle::VehicleTelemetry& T     = Reading.Telemetry;
    const float                      Scale = Page.QueryScale();

    if (Reading.Steps != LastTraceSteps)
    {
        LastTraceSteps               = Reading.Steps;
        SpeedTraceKmh[TraceCursor]   = std::fabs(T.ForwardSpeed) * 3.6f;
        RpmTrace[TraceCursor]        = T.EngineRPM;
        BoostTraceBar[TraceCursor]   = T.BoostBar;
        TraceCursor                  = (TraceCursor + 1u) % TelemetryTraceCount;
        TraceCount                   = std::min<size_t>(TraceCount + 1u, TelemetryTraceCount);
    }

    // ① 12-LED Shift Strip
    {
        const ImVec2 StripOrigin = Page.Space(22.0f, 2.0f, 6.0f);
        const float  StripWidth  = Page.QueryWidth();
        ImDrawList*  Draw        = ImGui::GetWindowDrawList();
        const float  Redline     = std::max(1000.0f, Parameters.Specification.Solver.Engine.RedlineRPM);
        const float  RpmFrac     = std::clamp(T.EngineRPM / Redline, 0.0f, 1.05f);
        const bool   Flash       = RpmFrac >= 0.96f && ((Reading.Steps / 12u) % 2u == 0u);
        const float  Gap         = 4.0f * Scale;
        const float  LedWidth    = (StripWidth - Gap * 11.0f) / 12.0f;
        for (int Led = 0; Led < 12; ++Led)
        {
            const float Threshold = 0.45f + 0.50f * (static_cast<float>(Led) / 11.0f);
            const bool  Lit       = Flash || RpmFrac >= Threshold;
            ImU32       Colour    = IM_COL32(34, 36, 42, 255);
            if (Lit)
            {
                if (Led < 7) Colour = IM_COL32(78, 212, 132, 255);
                else if (Led < 10) Colour = IM_COL32(236, 178, 66, 255);
                else Colour = IM_COL32(232, 84, 84, 255);
            }
            const float X0 = StripOrigin.x + static_cast<float>(Led) * (LedWidth + Gap);
            Draw->AddRectFilled(ImVec2(X0, StripOrigin.y), ImVec2(X0 + LedWidth, StripOrigin.y + 14.0f * Scale), Colour, 3.0f * Scale);
        }
    }

    // ② Hero Readout Tiles
    char SpeedText[32], GearText[16], RpmText[32], BoostText[32];
    std::snprintf(SpeedText, sizeof(SpeedText), "%.0f", std::fabs(T.ForwardSpeed) * 3.6f);
    if (T.GearIndex < 2) std::snprintf(GearText, sizeof(GearText), "R");
    else if (T.GearIndex == 2) std::snprintf(GearText, sizeof(GearText), "N");
    else std::snprintf(GearText, sizeof(GearText), "%d", T.GearIndex - 2);
    std::snprintf(RpmText, sizeof(RpmText), "%.0f", T.EngineRPM);
    std::snprintf(BoostText, sizeof(BoostText), "%+.2f", T.BoostBar);
    const std::array<MetricTile, 4> HeroTiles = { {
        { "Speed", SpeedText, "km/h" },
        { "Gear", GearText, "" },
        { "Engine", RpmText, "rpm" },
        { "Boost", BoostText, "bar" } } };
    MetricTiles::Construct(Page, HeroTiles.data(), static_cast<int>(HeroTiles.size()), 0);

    // ③ Pedals & Force Bars
    {
        const ImVec2 BarsOrigin = Page.Space(66.0f, 2.0f, 8.0f);
        const float  FullWidth  = Page.QueryWidth();
        const float  BarH       = 16.0f * Scale;
        ImDrawList*  Draw       = ImGui::GetWindowDrawList();

        char RightText[96];
        std::snprintf(RightText, sizeof(RightText), "%.0f%% (net %.0f%%%s)", Reading.Throttle[0] * 100.0f,
                      T.ThrottleApplied * 100.0f, T.TractionActive ? " · TC" : "");
        RasterizeProgressBar(Draw, BarsOrigin, FullWidth, BarH, T.ThrottleApplied, IM_COL32(82, 204, 136, 220), "Throttle", RightText, Scale);

        std::snprintf(RightText, sizeof(RightText), "%.0f%%%s", Reading.Throttle[1] * 100.0f, Reading.Throttle[3] > 0.5f ? " · HANDBRAKE" : "");
        RasterizeProgressBar(Draw, ImVec2(BarsOrigin.x, BarsOrigin.y + 21.0f * Scale), FullWidth, BarH, Reading.Throttle[1],
                        IM_COL32(228, 92, 88, 220), "Brake", RightText, Scale);

        float TotalLongForce = 0.0f, TotalLatForce = 0.0f;
        for (uint32_t WIdx = 0; WIdx < std::min<uint32_t>(4u, T.WheelCount); ++WIdx)
        {
            TotalLongForce += T.Wheels[WIdx].LongitudinalForce;
            TotalLatForce += T.Wheels[WIdx].LateralForce;
        }
        const float MassRef = std::max(300.0f, T.ChassisMass > 100.0f ? T.ChassisMass : Parameters.Specification.Geometry.Mass);
        const float LongG   = TotalLongForce / (MassRef * 9.81f);
        const float LatG    = TotalLatForce / (MassRef * 9.81f);
        std::snprintf(RightText, sizeof(RightText), "steer %+.0f%%  ·  long %+.2f g  ·  lat %+.2f g",
                      Reading.Throttle[2] * 100.0f, LongG, LatG);
        RasterizeProgressBar(Draw, ImVec2(BarsOrigin.x, BarsOrigin.y + 42.0f * Scale), FullWidth, BarH,
                        0.5f + 0.5f * std::clamp(Reading.Throttle[2], -1.0f, 1.0f), IM_COL32(132, 176, 232, 210), "Steering / G", RightText, Scale);
    }

    // ④ Rolling Telemetry Trace (Speed, RPM, Boost)
    if (TraceCount >= 2u)
    {
        const ImVec2 TraceOrigin = Page.Space(108.0f, 2.0f, 8.0f);
        const float  Width       = Page.QueryWidth();
        const float  Height      = 108.0f * Scale;
        ImDrawList*  Draw        = ImGui::GetWindowDrawList();
        Draw->AddRectFilled(TraceOrigin, ImVec2(TraceOrigin.x + Width, TraceOrigin.y + Height), IM_COL32(22, 24, 29, 255), 8.0f * Scale);
        Draw->AddRect(TraceOrigin, ImVec2(TraceOrigin.x + Width, TraceOrigin.y + Height), IM_COL32(255, 255, 255, 18), 8.0f * Scale);

        const float Left   = TraceOrigin.x + 10.0f * Scale, Right = TraceOrigin.x + Width - 10.0f * Scale;
        const float Top    = TraceOrigin.y + 20.0f * Scale, Bottom = TraceOrigin.y + Height - 8.0f * Scale;
        const float RedMax = std::max(1000.0f, Parameters.Specification.Solver.Engine.RedlineRPM);

        ImVec2 PrevSpeed{}, PrevRpm{}, PrevBoost{};
        const size_t Oldest = (TraceCursor + TelemetryTraceCount - TraceCount) % TelemetryTraceCount;
        for (size_t Sample = 0; Sample < TraceCount; ++Sample)
        {
            const size_t RingIdx = (Oldest + Sample) % TelemetryTraceCount;
            const float  X       = Left + (Right - Left) * static_cast<float>(Sample) / static_cast<float>(TelemetryTraceCount - 1u);
            const ImVec2 PtSpeed(X, Bottom - (Bottom - Top) * std::clamp(SpeedTraceKmh[RingIdx] / 260.0f, 0.0f, 1.0f));
            const ImVec2 PtRpm(X, Bottom - (Bottom - Top) * std::clamp(RpmTrace[RingIdx] / RedMax, 0.0f, 1.0f));
            const ImVec2 PtBoost(X, Bottom - (Bottom - Top) * std::clamp((BoostTraceBar[RingIdx] + 0.2f) / 2.2f, 0.0f, 1.0f));
            if (Sample > 0u)
            {
                Draw->AddLine(PrevSpeed, PtSpeed, IM_COL32(132, 186, 238, 255), 1.8f * Scale);
                Draw->AddLine(PrevRpm, PtRpm, IM_COL32(236, 174, 78, 220), 1.5f * Scale);
                Draw->AddLine(PrevBoost, PtBoost, IM_COL32(92, 214, 152, 220), 1.5f * Scale);
            }
            PrevSpeed = PtSpeed;
            PrevRpm   = PtRpm;
            PrevBoost = PtBoost;
        }
        Draw->AddText(ImVec2(Left, TraceOrigin.y + 4.0f * Scale), IM_COL32(156, 160, 168, 255),
                      "Live trace  ·  speed (blue)  ·  rpm (amber)  ·  boost (mint)");
    }

    // ⑤ Engine, Supercharger/Turbo, Thermal & Fuel Summary
    char Line[256];
    std::snprintf(Line, sizeof(Line),
                  "Power %.1f kW · clutch %.0f N·m · blower drag %.1f N·m · downforce %.0f N / drag %.0f N",
                  T.EnginePower_W * 0.001f, T.ClutchTorque_Nm, T.ParasiticDrag_Nm, T.Aero.TotalDownforce_N, T.Aero.TotalDrag_N);
    CaptionText::Construct(Page, Line, 4.0f, 5.0f);

    std::snprintf(Line, sizeof(Line),
                  "Charge air %.1f °C · coolant %.1f °C · oil %.1f °C · fuel %.2f L (%.2f g/s · burnt %.3f kg)",
                  T.ChargeTemperature_K - 273.15f, T.CoolantTemperature_K - 273.15f, T.OilTemperature_K - 273.15f,
                  T.FuelLitres, T.FuelFlow_gPerS, T.FuelBurnt);
    CaptionText::Construct(Page, Line, 2.0f, 5.0f);

    std::snprintf(Line, sizeof(Line),
                  "Physics %.0f Hz · %.0f us/step · realtime x%.2f · %.2f s (%llu steps) · carcass %s",
                  Reading.StepHz, Reading.StepMicroseconds, Reading.RealtimeRatio, Reading.SimulatedSeconds,
                  static_cast<unsigned long long>(Reading.Steps), Vehicle::TyreCarcassName(T.Carcass));
    CaptionText::Construct(Page, Line, 2.0f, 8.0f);

    // ⑥ Four-Corner Live Tyre, Carcass, Gas-Law Pressure, Slip & Brake Cards
    static constexpr const char* Corners[] = { "Front left (FL)", "Front right (FR)", "Rear left (RL)", "Rear right (RR)" };
    for (uint32_t Wheel = 0; Wheel < std::min<uint32_t>(4u, T.WheelCount); ++Wheel)
    {
        const Vehicle::WheelTelemetry& W = T.Wheels[Wheel];
        SubHeading(Page, Corners[Wheel]);
        std::snprintf(Line, sizeof(Line),
                      "Load %.0f N · deflection %.1f mm · air %.2f L · pressure %.1f kPa · strut %.1f mm",
                      W.VerticalLoad, W.TyreDeflection * 1000.0f, W.EnclosedVolume * 1000.0f,
                      W.Thermal.GaugePressure * 0.001, W.StrutCompression * 1000.0f);
        CaptionText::Construct(Page, Line, 2.0f, 4.0f);

        std::snprintf(Line, sizeof(Line),
                      "Slip long %+.3f / lat %+.1f° · Fx %+.0f N · Fy %+.0f N · tread %.0f °C · carcass %.0f °C · gas %.0f °C",
                      W.SlipRatio, W.SlipAngleRad * 57.2957795f, W.LongitudinalForce, W.LateralForce,
                      W.Thermal.SurfaceTemperature - 273.15, W.Thermal.CarcassTemperature - 273.15, W.Thermal.GasTemperature - 273.15);
        CaptionText::Construct(Page, Line, 1.0f, 4.0f);

        std::snprintf(Line, sizeof(Line),
                      "Grip x%.2f · wear %.2f mm · disc %.0f °C%s%s",
                      W.Thermal.GripFactor * W.WearGripFactor, W.WearDepth_mm, W.BrakeTemp_K - 273.15f,
                      W.AbsActive ? " · ABS" : "", !Reading.CornerFitted[Wheel] ? " · PIT LANE" : "");
        CaptionText::Construct(Page, Line, 1.0f, 8.0f);
    }
    EndSection(Page);
}

void VehicleInspector::ConstructInspector()
{
    const auto    Theme = ControlTheme::ResolveNative();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    bool          Changed = false;

    if (PageHeading::Construct(
            Page,
            "VehicleHeader",
            "VEHICLE / PROJECT DRIVE ON JOLT",
            Parameters.Specification.Name.c_str(),
            "Restore defaults",
            "ControlVehicle chassis on JoltPhysics, GRIT drivetrain, supercharger/turbo, thermal gas-law tyres and Spring / XPBD / 3D Hex8 FEM carcasses.",
            "Reset every specification figure to ControlVehicle defaults"))
    {
        Parameters.Specification = Vehicle::ConstructControlVehicleSpecification();
        Changed                  = true;
    }

    int Upper = ActiveSection < UpperTabs ? ActiveSection : -1;
    if (TabStrip::Construct(Page, "VehicleSectionsUpper", SectionLabels, UpperTabs, Upper) && Upper >= 0) ActiveSection = Upper;
    int Lower = ActiveSection >= UpperTabs ? ActiveSection - UpperTabs : -1;
    if (TabStrip::Construct(Page, "VehicleSectionsLower", SectionLabels + UpperTabs, SectionCount - UpperTabs, Lower) && Lower >= 0)
        ActiveSection = UpperTabs + Lower;

    switch (ActiveSection)
    {
        case 0: Changed |= ConstructOverview(Page); break;
        case 1:
        {
            static constexpr const char* Sections[] = { "geometry", "suspension.front", "suspension.rear", "bars.front", "bars.rear", "suspension", "steering" };
            Changed |= ConstructFigures(Page, Sections, 7);
            break;
        }
        case 2:
        {
            ConstructCurveChart(Page, "Engine dyno", Parameters.Specification.Solver.Engine.TorqueCurve,
                                Parameters.Specification.Solver.Engine.RedlineRPM);
            Changed |= ConstructCurveEditor(Page);
            static constexpr const char* Sections[] = { "engine", "induction", "turbo", "supercharger", "charge" };
            Changed |= ConstructFigures(Page, Sections, 5);
            break;
        }
        case 3:
        {
            static constexpr const char* Sections[] = { "gearbox", "clutch", "differential" };
            Changed |= ConstructFigures(Page, Sections, 3);
            Changed |= ConstructRatios(Page);
            break;
        }
        case 4:
        {
            ConstructTyreMechanicsPreview(Page);
            Changed |= ConstructTyreBay(Page);
            static constexpr const char* Sections[] = { "carcass", "tyre", "contact", "thermal", "wear", "slip", "pacejka" };
            Changed |= ConstructFigures(Page, Sections, 7);
            break;
        }
        case 5:
        {
            static constexpr const char* Sections[] = { "brakes", "abs", "traction" };
            Changed |= ConstructFigures(Page, Sections, 3);
            break;
        }
        case 6:
        {
            static constexpr const char* Sections[] = { "aero", "aero.body", "aero.wing", "aero.splitter", "aero.diffuser", "aero.skirt_left",
                                                        "aero.skirt_right", "aero.fins" };
            Changed |= ConstructFigures(Page, Sections, 8);
            break;
        }
        case 7:
        {
            static constexpr const char* Sections[] = { "fuel", "fluids" };
            Changed |= ConstructFigures(Page, Sections, 2);
            break;
        }
        default: ConstructTelemetry(Page); break;
    }

    FooterRow::Construct(Page, "Levelspace record \xC2\xB7 Vehicle", "Saved with the level");
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
std::string VehicleInspector::QueryDirectoryReadout() const { return {}; }
void        VehicleInspector::ConstructInspector() {}
} // namespace Frontier

#endif
