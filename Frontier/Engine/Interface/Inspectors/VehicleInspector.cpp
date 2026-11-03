/*==========================================================================================================================================
                                                           VEHICLEINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 Native C++ VehicleInspector: dyno hero, metric/imperial conversion, monotone curves, 10 physics sections, and live telemetry dock.

#include "Engine/Interface/Inspectors/VehicleInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

constexpr float VehiclePi = 3.14159265358979323846f;

[[nodiscard]] std::vector<VehicleCurveControlPoint> SortedControlPoints(const std::vector<VehicleCurveControlPoint>& Source)
{
    std::vector<VehicleCurveControlPoint> Ordered = Source;
    std::sort(Ordered.begin(), Ordered.end(), [](const VehicleCurveControlPoint& A, const VehicleCurveControlPoint& B) {
        return A.X < B.X;
    });
    return Ordered;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                        FRITSCH-CARLSON MONOTONE CURVE & PERFORMANCE
//------------------------------------------------------------------------------------------------------------------------

float EvaluateMonotoneVehicleCurve(const std::vector<VehicleCurveControlPoint>& Points,
                                   bool                                         SmoothMonotone,
                                   float                                        QueryX) noexcept
{
    if (Points.empty()) return 0.0f;
    if (Points.size() == 1u) return Points[0].Y;

    const std::vector<VehicleCurveControlPoint> Ordered = SortedControlPoints(Points);
    if (QueryX <= Ordered.front().X) return Ordered.front().Y;
    if (QueryX >= Ordered.back().X) return Ordered.back().Y;

    size_t Upper = 1u;
    while (Upper + 1u < Ordered.size() && Ordered[Upper].X < QueryX)
    {
        ++Upper;
    }
    const size_t Lower = Upper - 1u;
    const float  Span  = std::max(1.0e-6f, Ordered[Upper].X - Ordered[Lower].X);
    const float  Share = (QueryX - Ordered[Lower].X) / Span;

    if (!SmoothMonotone)
    {
        return Ordered[Lower].Y + Share * (Ordered[Upper].Y - Ordered[Lower].Y);
    }

    const size_t       Count = Ordered.size();
    std::vector<float> Slopes(Count - 1u, 0.0f);
    for (size_t Idx = 0u; Idx + 1u < Count; ++Idx)
    {
        Slopes[Idx] = (Ordered[Idx + 1u].Y - Ordered[Idx].Y) / std::max(1.0e-6f, Ordered[Idx + 1u].X - Ordered[Idx].X);
    }

    std::vector<float> Tangents(Count, 0.0f);
    Tangents[0]         = Slopes[0];
    Tangents[Count - 1] = Slopes[Count - 2u];
    for (size_t Idx = 1u; Idx + 1u < Count; ++Idx)
    {
        if (Slopes[Idx - 1u] * Slopes[Idx] <= 0.0f)
        {
            Tangents[Idx] = 0.0f;
            continue;
        }
        const float WLeft  = 2.0f * (Ordered[Idx + 1u].X - Ordered[Idx].X) + (Ordered[Idx].X - Ordered[Idx - 1u].X);
        const float WRight = (Ordered[Idx + 1u].X - Ordered[Idx].X) + 2.0f * (Ordered[Idx].X - Ordered[Idx - 1u].X);
        Tangents[Idx]      = (WLeft + WRight) / (WLeft / Slopes[Idx - 1u] + WRight / Slopes[Idx]);
    }

    const float Share2 = Share * Share;
    const float Share3 = Share2 * Share;
    const float Basis0 = 2.0f * Share3 - 3.0f * Share2 + 1.0f;
    const float Basis1 = Share3 - 2.0f * Share2 + Share;
    const float Basis2 = -2.0f * Share3 + 3.0f * Share2;
    const float Basis3 = Share3 - Share2;

    return Basis0 * Ordered[Lower].Y + Basis1 * Span * Tangents[Lower] + Basis2 * Ordered[Upper].Y +
           Basis3 * Span * Tangents[Upper];
}

VehiclePerformanceSummary EvaluateVehiclePerformance(const VehicleConfiguration& Configuration) noexcept
{
    VehiclePerformanceSummary Out{};
    Out.PeakTorqueNm  = 0.0f;
    Out.PeakTorqueRpm = Configuration.IdleSpeedRpm;
    Out.PeakPowerKw   = 0.0f;
    Out.PeakPowerRpm  = Configuration.IdleSpeedRpm;

    const float BoostScale =
        Configuration.RaceTuneEnabled ? 1.18f : (Configuration.InductionModeIndex == 0 ? 0.86f : 1.0f);

    for (int Step = 0; Step <= 96; ++Step)
    {
        const float Rpm    = Configuration.IdleSpeedRpm +
                             (Configuration.RedlineRpm - Configuration.IdleSpeedRpm) * (static_cast<float>(Step) / 96.0f);
        const float Torque = std::max(0.0f, EvaluateMonotoneVehicleCurve(
                                                Configuration.EngineTorqueCurve, Configuration.CurveSmoothMonotone, Rpm) *
                                                BoostScale);
        const float Power  = Torque * Rpm * (2.0f * VehiclePi / 60.0f) / 1000.0f;
        if (Torque > Out.PeakTorqueNm)
        {
            Out.PeakTorqueNm  = Torque;
            Out.PeakTorqueRpm = Rpm;
        }
        if (Power > Out.PeakPowerKw)
        {
            Out.PeakPowerKw  = Power;
            Out.PeakPowerRpm = Rpm;
        }
    }

    const float MassKg        = std::max(400.0f, Configuration.KerbMassKg);
    Out.PowerToWeightKwPerT   = Out.PeakPowerKw / (MassKg / 1000.0f);
    Out.SpecificTorqueNmPerL  = Out.PeakTorqueNm / std::max(0.1f, Configuration.DisplacementLitres);
    Out.EnclosedTyreCount     = 0;
    for (const TyreCornerConfiguration& Corner : Configuration.Corners)
    {
        if (Corner.EnclosedCondition) ++Out.EnclosedTyreCount;
    }

    const int   GearCount   = std::clamp(Configuration.ForwardGearCount, 1, 8);
    const float RadiusM     = std::max(0.18f, Configuration.LoadedWheelRadiusMm * 0.001f);
    const float WheelbaseM  = std::max(1.2f, Configuration.WheelbaseMm * 0.001f);
    const float CgHeightM   = Configuration.CentreOfGravityHeightMm * 0.001f;
    const float Efficiency  = std::clamp(Configuration.DrivelineEfficiencyPct * 0.01f, 0.5f, 1.0f);
    const float RearShare   = std::clamp(Configuration.StaticRearMassPercent * 0.01f, 0.25f, 0.75f);

    auto EngineSpeedFor = [&](float SpeedKmh, float GearRatio) noexcept -> float {
        return (SpeedKmh / 3.6f) / RadiusM * GearRatio * Configuration.FinalDriveRatio * 60.0f / (2.0f * VehiclePi);
    };
    auto WheelForceFor = [&](float SpeedKmh, float GearRatio) noexcept -> float {
        const float Rpm = EngineSpeedFor(SpeedKmh, GearRatio);
        if (Rpm > Configuration.RedlineRpm) return 0.0f;
        const float ClampedRpm = std::max(Configuration.IdleSpeedRpm, Rpm);
        const float Torque =
            EvaluateMonotoneVehicleCurve(Configuration.EngineTorqueCurve, Configuration.CurveSmoothMonotone, ClampedRpm) *
            BoostScale;
        return std::max(0.0f, Torque) * GearRatio * Configuration.FinalDriveRatio * Efficiency / RadiusM;
    };
    auto TractionLimitFor = [&](float SpeedKmh, float AccelMps2) noexcept -> float {
        const float Downforce = Configuration.DownforceAt200KmhN * (SpeedKmh / 200.0f) * (SpeedKmh / 200.0f);
        const float Transfer  = MassKg * AccelMps2 * CgHeightM / WheelbaseM;
        float       Share     = 1.0f;
        if (Configuration.DriveLayoutIndex == 1) Share = RearShare;
        else if (Configuration.DriveLayoutIndex == 0) Share = 1.0f - RearShare;
        const float Normal =
            std::max(0.0f, (MassKg * 9.81f + Downforce) * Share + (Configuration.DriveLayoutIndex == 0 ? -Transfer : Transfer));
        const float EnclosureFactor = static_cast<float>(Out.EnclosedTyreCount) * 0.25f;
        return Configuration.PeakTyreFriction * Normal * std::max(0.25f, EnclosureFactor);
    };
    auto ResistanceFor = [&](float SpeedKmh) noexcept -> float {
        const float AirMps = SpeedKmh / 3.6f;
        const float Drag   = 0.5f * 1.225f * Configuration.DragCoefficient * Configuration.FrontalAreaM2 * AirMps * AirMps;
        return Drag + Configuration.RollingResistance * MassKg * 9.81f;
    };

    // 0-100 km/h forward integration
    float SpeedKmh = 0.0f;
    float Elapsed  = 0.0f;
    float Accel    = 0.0f;
    float DistM    = 0.0f;
    int   GearIdx  = 0;
    int   Shifts   = 0;
    while (Elapsed < 30.0f && SpeedKmh < 100.0f)
    {
        while (GearIdx < GearCount - 1 &&
               EngineSpeedFor(std::max(SpeedKmh, 1.0f), Configuration.GearRatios[static_cast<size_t>(GearIdx)]) >
                   Configuration.RedlineRpm)
        {
            ++GearIdx;
            ++Shifts;
        }
        const float WheelF = WheelForceFor(std::max(SpeedKmh, 2.0f), Configuration.GearRatios[static_cast<size_t>(GearIdx)]);
        const float LimitF = TractionLimitFor(SpeedKmh, Accel);
        const float NetF   = std::min(WheelF, LimitF) - ResistanceFor(SpeedKmh);
        Accel              = NetF / MassKg;
        if (Accel <= 0.0f && SpeedKmh > 1.0f) break;
        SpeedKmh = std::max(0.0f, SpeedKmh + Accel * 3.6f * 0.005f);
        DistM   += (SpeedKmh / 3.6f) * 0.005f;
        Elapsed += 0.005f;
    }
    Out.ZeroToHundredSec     = Elapsed;
    Out.LaunchDistanceMetres = DistM;
    Out.ShiftsToHundred      = Shifts;

    // Top speed equilibrium
    float TopKmh = 40.0f;
    for (float Candidate = 20.0f; Candidate <= 440.0f; Candidate += 1.0f)
    {
        float BestWheelF = 0.0f;
        for (int G = 0; G < GearCount; ++G)
        {
            BestWheelF = std::max(BestWheelF, WheelForceFor(Candidate, Configuration.GearRatios[static_cast<size_t>(G)]));
        }
        if (std::min(BestWheelF, TractionLimitFor(Candidate, 0.0f)) < ResistanceFor(Candidate)) break;
        TopKmh = Candidate;
    }
    Out.TopSpeedKmh = TopKmh;

    // 100-0 km/h braking distance
    const float Downforce100 = Configuration.DownforceAt200KmhN * 0.25f;
    const float Decel        = std::max(1.0f, Configuration.PeakTyreFriction * (MassKg * 9.81f + Downforce100) / MassKg);
    const float V100         = 100.0f / 3.6f;
    Out.BrakingDistance100M  = (V100 * V100) / (2.0f * Decel);
    return Out;
}

} // namespace Frontier

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/DiagramControls.h"
#include "Engine/Interface/InspectorControls.h"

namespace Frontier
{

namespace
{

constexpr int UpperTabCount = 5;
constexpr const char* SectionTabLabels[VehicleInspector::SectionCount] = {
    "Chassis", "Engine", "Suspension", "Soft-body", "Curves",
    "Thermal", "Tyres",  "Aero",       "Physics",   "Performance"};

void BeginVehicleCard(ControlColumn& Page, const char* Title, const char* Glyph, const char* Status, const char* Caption = nullptr)
{
    CardLook Look;
    Look.Radius        = 22.0f;
    Look.PaddingTop    = 16.0f;
    Look.PaddingSide   = 14.0f;
    Look.PaddingBottom = 8.0f;
    Look.MarginBottom  = 16.0f;
    Look.HeadingGlyph  = Glyph;
    Page.BeginCard(Look, Title, Status);
    if (Caption != nullptr && Caption[0] != '\0')
    {
        CaptionText::Construct(Page, Caption, 0.0f, 10.0f);
    }
}

void SubHeadingLabel(ControlColumn& Page, const char* Title)
{
    const ImVec2 Top = Page.Space(12.0f, 7.0f, 9.0f);
    char         Upper[64]{};
    for (size_t Idx = 0; Idx + 1u < sizeof(Upper) && Title[Idx] != '\0'; ++Idx)
    {
        Upper[Idx] = static_cast<char>(Title[Idx] >= 'a' && Title[Idx] <= 'z' ? Title[Idx] - 32 : Title[Idx]);
    }
    (void)GlyphRun::Construct(Page, Page.QueryFaces().Regular, 9.0f * Page.QueryScale(),
                              ImVec2(Top.x, Top.y + GlyphRun::EmAscent * 9.0f * Page.QueryScale()),
                              Page.QueryTheme().Eyebrow, Upper, 1.2f * Page.QueryScale());
}

bool IntSliderRow(ControlColumn& Page, const char* Id, const char* Label, int& Quantity, int MinVal, int MaxVal, const char* Unit = "")
{
    uint32_t UnsignedVal = static_cast<uint32_t>(std::clamp(Quantity, std::max(0, MinVal), std::max(0, MaxVal)));
    if (!SliderRow::ConstructInteger(Page, Id, Label, UnsignedVal, static_cast<uint32_t>(std::max(0, MinVal)),
                                     static_cast<uint32_t>(std::max(0, MaxVal)), Unit))
    {
        return false;
    }
    Quantity = static_cast<int>(UnsignedVal);
    return true;
}

} // namespace

std::string VehicleInspector::QueryDirectoryReadout() const
{
    int Enclosed = 0;
    for (const TyreCornerConfiguration& Corner : Parameters.Corners)
    {
        if (Corner.EnclosedCondition) ++Enclosed;
    }
    char Text[96]{};
    if (Parameters.UnitDisplayImperial)
    {
        std::snprintf(Text, sizeof(Text), "%.0f lb \xC2\xB7 %d/4 tyres", Parameters.KerbMassKg * 2.20462262f, Enclosed);
    }
    else
    {
        std::snprintf(Text, sizeof(Text), "%.0f kg \xC2\xB7 %d/4 tyres", Parameters.KerbMassKg, Enclosed);
    }
    return Text;
}

void VehicleInspector::ConfigureSection(int SectionIndex) noexcept
{
    ActiveSection        = std::clamp(SectionIndex, 0, SectionCount - 1);
    PendingScrollSection = ActiveSection;
}

void VehicleInspector::ConstructSectionTabs(ControlColumn& Page)
{
    int Upper = ActiveSection < UpperTabCount ? ActiveSection : -1;
    if (TabStrip::Construct(Page, "VehSectionsUpper", SectionTabLabels, UpperTabCount, Upper) && Upper >= 0)
    {
        ActiveSection = Upper;
    }
    int Lower = ActiveSection >= UpperTabCount ? ActiveSection - UpperTabCount : -1;
    if (TabStrip::Construct(Page, "VehSectionsLower", SectionTabLabels + UpperTabCount, SectionCount - UpperTabCount, Lower) &&
        Lower >= 0)
    {
        ActiveSection = UpperTabCount + Lower;
    }
}

void VehicleInspector::ConstructHeroAndChips(ControlColumn& Page, const VehiclePerformanceSummary& Summary, bool& Changed)
{
    const bool  Imp        = Parameters.UnitDisplayImperial;
    const float TorqueScale= Imp ? 0.73756215f : 1.0f;
    const float PowerScale = Imp ? 1.34102209f : 1.0f;
    const float MassScale  = Imp ? 2.20462262f : 1.0f;
    const float SpeedScale = Imp ? 0.62137119f : 1.0f;

    char StatusBuf[64]{};
    std::snprintf(StatusBuf, sizeof(StatusBuf), "%.0f %s peak", Summary.PeakPowerKw * PowerScale, Imp ? "hp" : "kW");

    BeginVehicleCard(Page, "Dyno sheet \xC2\xB7 torque & derived power", "Gauge", StatusBuf,
                     "Solid: Fritsch-Carlson monotone engine torque. Secondary: derived power = torque x angular speed.");

    char PeakT[32]{}, PeakP[32]{};
    std::snprintf(PeakT, sizeof(PeakT), "%.0f", Summary.PeakTorqueNm * TorqueScale);
    std::snprintf(PeakP, sizeof(PeakP), "%.0f", Summary.PeakPowerKw * PowerScale);
    const MetricTile DynoTiles[2] = {
        {"Peak torque", PeakT, Imp ? "lb\xC2\xB7" "ft" : "Nm"},
        {"Peak power", PeakP, Imp ? "hp" : "kW"}};
    MetricTiles::Construct(Page, DynoTiles, 2, 0);

    std::array<float, 48> NormTorque{};
    std::array<float, 48> NormPower{};
    const float           CeilTorque = std::max(400.0f, Summary.PeakTorqueNm * 1.15f);
    const float           CeilPower  = std::max(200.0f, Summary.PeakPowerKw * 1.15f);
    for (size_t Idx = 0; Idx < NormTorque.size(); ++Idx)
    {
        const float Rpm = Parameters.IdleSpeedRpm +
                          (Parameters.RedlineRpm - Parameters.IdleSpeedRpm) *
                              (static_cast<float>(Idx) / static_cast<float>(NormTorque.size() - 1u));
        const float Tq  = EvaluateMonotoneVehicleCurve(Parameters.EngineTorqueCurve, Parameters.CurveSmoothMonotone, Rpm);
        const float Pw  = Tq * Rpm * (2.0f * VehiclePi / 60.0f) / 1000.0f;
        NormTorque[Idx] = std::clamp(Tq / CeilTorque, 0.0f, 1.0f);
        NormPower[Idx]  = std::clamp(Pw / CeilPower, 0.0f, 1.0f);
    }
    const InspectorPalette Palette = InspectorPalette::Resolve();
    DiagramControls::ConstructCurvePlot(Palette, Page.QueryFaces(), "Torque (amber) & Power (ice) vs RPM",
                                        NormTorque.data(), NormTorque.size(), IM_COL32(232, 168, 74, 255),
                                        NormPower.data(), NormPower.size(), IM_COL32(159, 180, 214, 255));
    Page.EndCard();

    char MassStr[32]{}, PtwStr[32]{}, AccelStr[32]{}, TopStr[32]{};
    std::snprintf(MassStr, sizeof(MassStr), "%.0f", Parameters.KerbMassKg * MassScale);
    std::snprintf(PtwStr, sizeof(PtwStr), "%.0f", Summary.PowerToWeightKwPerT * PowerScale);
    std::snprintf(AccelStr, sizeof(AccelStr), "%.2f", Summary.ZeroToHundredSec);
    std::snprintf(TopStr, sizeof(TopStr), "%.0f", Summary.TopSpeedKmh * SpeedScale);
    const MetricTile Kpis[4] = {
        {"Kerb mass", MassStr, Imp ? "lb" : "kg"},
        {"Power / weight", PtwStr, Imp ? "hp/ton" : "kW/t"},
        {"0\xE2\x80\x93" "100 km/h", AccelStr, "s"},
        {"Top speed", TopStr, Imp ? "mph" : "km/h"}};
    MetricTiles::Construct(Page, Kpis, 4, 2);

    const SwitchTile AidTiles[6] = {
        {"TCS", &Parameters.TractionControlEnabled, TileGlyph::Sun, false, false, "Closed-loop slip target", "Target"},
        {"ESC", &Parameters.StabilityControlEnabled, TileGlyph::Sun, false, false, "Yaw-rate brake intervention", "Shield"},
        {"ABS", &Parameters.AntiLockBrakesEnabled, TileGlyph::Sun, false, false, "4-channel anti-lock braking", "Aperture"},
        {"Launch", &Parameters.LaunchControlEnabled, TileGlyph::Sun, false, false, "Standstill launch RPM hold", "Zap"},
        {"DRS", &Parameters.DrsAidEnabled, TileGlyph::Sun, false, false, "Rear-wing drag reduction", "Wind"},
        {"Telemetry", &Parameters.TelemetryEnabled, TileGlyph::Sun, false, false, "Read-only telemetry dock", "Activity"}};
    Changed |= SwitchTiles::Construct(Page, "VehQuickAids", AidTiles, 6, 3);
}

void VehicleInspector::ConstructChassisSection(ControlColumn& Page, bool& Changed)
{
    const bool  Imp    = Parameters.UnitDisplayImperial;
    const float MScale = Imp ? 2.20462262f : 1.0f;
    const float LScale = Imp ? 0.03937008f : 1.0f;
    const float VScale = Imp ? 0.26417205f : 1.0f;
    const char* MUnit  = Imp ? "lb" : "kg";
    const char* LUnit  = Imp ? "in" : "mm";
    const char* VUnit  = Imp ? "gal" : "L";

    BeginVehicleCard(Page, "Chassis and identity", "Car", Parameters.ClassificationCode.c_str(),
                     "Mass distribution, wheelbase, front/rear track, CG height, fuel tank, and drive layout.");
    SubHeadingLabel(Page, "Mass and dimensions");
    Changed |= SliderRow::Construct(Page, "KerbMass", "Kerb mass", Parameters.KerbMassKg, 900.0f, 1800.0f, 1.0f, MUnit, MScale);
    Changed |= SliderRow::Construct(Page, "Wheelbase", "Wheelbase", Parameters.WheelbaseMm, 2200.0f, 3200.0f, 5.0f, LUnit, LScale);
    Changed |= SliderRow::Construct(Page, "TrackF", "Front track", Parameters.TrackFrontMm, 1400.0f, 1800.0f, 5.0f, LUnit, LScale);
    Changed |= SliderRow::Construct(Page, "TrackR", "Rear track", Parameters.TrackRearMm, 1400.0f, 1800.0f, 5.0f, LUnit, LScale);
    Changed |= SliderRow::Construct(Page, "CgHeight", "Centre of gravity height", Parameters.CentreOfGravityHeightMm, 260.0f, 620.0f, 2.0f, LUnit, LScale);
    Changed |= SliderRow::Construct(Page, "RideHeight", "Static ride height", Parameters.RideHeightMm, 60.0f, 220.0f, 2.0f, LUnit, LScale);
    Changed |= SliderRow::Construct(Page, "FuelCap", "Fuel tank capacity", Parameters.FuelCapacityLitres, 30.0f, 120.0f, 1.0f, VUnit, VScale);
    Changed |= SliderRow::Construct(Page, "FuelLoad", "Loaded session fuel", Parameters.FuelLoadLitres, 0.0f, 120.0f, 0.5f, VUnit, VScale);
    Changed |= SliderRow::Construct(Page, "RearShare", "Static rear mass share", Parameters.StaticRearMassPercent, 40.0f, 65.0f, 1.0f, "%");

    static constexpr const char* Layouts[3] = {"Front (FWD)", "Rear (RWD)", "All-wheel (AWD)"};
    Changed |= ChoiceRow::Construct(Page, "DriveLayout", "Drive layout", Layouts, 3, Parameters.DriveLayoutIndex);
    Page.EndCard();
}

void VehicleInspector::ConstructEngineSection(ControlColumn& Page, const VehiclePerformanceSummary& Summary, bool& Changed)
{
    const bool  Imp    = Parameters.UnitDisplayImperial;
    const float TScale = Imp ? 0.73756215f : 1.0f;
    const float BScale = Imp ? 14.5037738f : 1.0f;
    const char* TUnit  = Imp ? "lb\xC2\xB7" "ft" : "Nm";
    const char* BUnit  = Imp ? "psi" : "bar";

    char LimiterText[48]{};
    std::snprintf(LimiterText, sizeof(LimiterText), "%.0f rpm", Parameters.RedlineRpm);
    BeginVehicleCard(Page, "Engine, supercharger & drivetrain", "Zap", LimiterText,
                     "Crankshaft map, forced induction (NA / Turbo / Supercharger), gearbox, clutch, and 4-mode differential.");

    char SpecTq[32]{}, PwrRpm[32]{}, ShiftCnt[16]{};
    std::snprintf(SpecTq, sizeof(SpecTq), "%.0f", Summary.SpecificTorqueNmPerL * TScale);
    std::snprintf(PwrRpm, sizeof(PwrRpm), "%.0f", Summary.PeakPowerRpm);
    std::snprintf(ShiftCnt, sizeof(ShiftCnt), "%d", Summary.ShiftsToHundred);
    const MetricTile EngMetrics[3] = {
        {"Specific torque", SpecTq, TUnit},
        {"Peak power speed", PwrRpm, "rpm"},
        {"Shifts to 100", ShiftCnt, ""}};
    MetricTiles::Construct(Page, EngMetrics, 3, MetricTiles::NoLead);

    SubHeadingLabel(Page, "Combustion engine");
    Changed |= SliderRow::Construct(Page, "EngDisp", "Displacement", Parameters.DisplacementLitres, 1.0f, 6.5f, 0.1f, "L");
    Changed |= SliderRow::Construct(Page, "EngIdle", "Idle speed", Parameters.IdleSpeedRpm, 600.0f, 1600.0f, 25.0f, "rpm");
    Changed |= SliderRow::Construct(Page, "EngRed", "Rev limiter", Parameters.RedlineRpm, 5000.0f, 9500.0f, 50.0f, "rpm");
    Changed |= SliderRow::Construct(Page, "EngRated", "Rated peak torque", Parameters.RatedTorqueNm, 200.0f, 900.0f, 5.0f, TUnit, TScale);
    Changed |= SliderRow::Construct(Page, "EngInert", "Rotating inertia", Parameters.EngineInertiaKgm2, 0.08f, 0.80f, 0.01f, "kg\xC2\xB7m\xC2\xB2");
    Changed |= ToggleRow::Construct(Page, "EngLimiter", "Fuel-cut rev limiter active", Parameters.RevLimiterEnabled);

    SubHeadingLabel(Page, "Forced induction and supercharger");
    static constexpr const char* InductionLabels[3] = {"Naturally aspirated", "Turbocharged", "Supercharged"};
    Changed |= ChoiceRow::Construct(Page, "IndMode", "Induction architecture", InductionLabels, 3, Parameters.InductionModeIndex);
    Changed |= ToggleRow::Construct(Page, "RaceTune", "Race tune (+33% boost & aggressive timing)", Parameters.RaceTuneEnabled);
    static constexpr const char* CompressorLabels[3] = {"Roots", "Twin-screw", "Centrifugal"};
    Changed |= ChoiceRow::Construct(Page, "ScType", "Supercharger compressor", CompressorLabels, 3, Parameters.SuperchargerTypeIndex);
    Changed |= SliderRow::Construct(Page, "ScRatio", "Pulley drive ratio", Parameters.SuperchargerDriveRatio, 1.0f, 3.8f, 0.05f, "\xC3\x97");
    Changed |= SliderRow::Construct(Page, "ScDisp", "Compressor displacement", Parameters.SuperchargerDisplacementCc, 600.0f, 3400.0f, 25.0f, "cm\xC2\xB3");
    Changed |= SliderRow::Construct(Page, "ScBoost", "Supercharger peak boost", Parameters.SuperchargerMaxBoostBar, 0.1f, 2.2f, 0.05f, BUnit, BScale);
    Changed |= SliderRow::Construct(Page, "ScDrag", "WOT parasitic drag", Parameters.SuperchargerPeakParasiticNm, 4.0f, 110.0f, 1.0f, TUnit, TScale);
    Changed |= SliderRow::Construct(Page, "ScBypShare", "Bypass parasitic share", Parameters.SuperchargerBypassParasiticPct, 0.0f, 40.0f, 1.0f, "%");
    Changed |= SliderRow::Construct(Page, "ScBypThr", "Bypass valve threshold", Parameters.SuperchargerBypassThrottlePct, 2.0f, 50.0f, 1.0f, "%");
    Changed |= SliderRow::Construct(Page, "TbBoost", "Turbo wastegate boost", Parameters.TurboBoostBar, 0.0f, 2.5f, 0.05f, BUnit, BScale);
    Changed |= SliderRow::Construct(Page, "TbUp", "Turbo spool-up rate", Parameters.TurboSpoolUpRate, 0.5f, 12.0f, 0.1f, "Hz");
    Changed |= SliderRow::Construct(Page, "TbDown", "Turbo blow-off rate", Parameters.TurboSpoolDownRate, 1.0f, 16.0f, 0.1f, "Hz");
    Changed |= SliderRow::Construct(Page, "TqGain", "Torque gain per bar", Parameters.TorqueGainPerBar, 0.15f, 1.10f, 0.01f, "\xC3\x97");

    SubHeadingLabel(Page, "Transmission and differential");
    Changed |= IntSliderRow(Page, "GearCnt", "Forward gears", Parameters.ForwardGearCount, 4, 8, "");
    Changed |= SliderRow::Construct(Page, "FinalDrv", "Final drive ratio", Parameters.FinalDriveRatio, 2.5f, 4.6f, 0.01f, "\xC3\x97");
    Changed |= SliderRow::Construct(Page, "DrvEff", "Driveline efficiency", Parameters.DrivelineEfficiencyPct, 70.0f, 100.0f, 1.0f, "%");
    Changed |= SliderRow::Construct(Page, "ShiftMs", "Shift cut duration", Parameters.ShiftDurationMs, 25.0f, 350.0f, 5.0f, "ms");
    Changed |= SliderRow::Construct(Page, "ClutchCap", "Clutch torque capacity", Parameters.ClutchCapacityNm, 350.0f, 1800.0f, 10.0f, TUnit, TScale);
    static constexpr const char* DiffLabels[4] = {"Open", "Locked", "1.5-way LSD", "Torsen"};
    Changed |= ChoiceRow::Construct(Page, "DiffType", "Differential mechanism", DiffLabels, 4, Parameters.DifferentialTypeIndex);
    Changed |= SliderRow::Construct(Page, "LsdPre", "LSD preload torque", Parameters.LsdPreloadTorqueNm, 0.0f, 320.0f, 5.0f, TUnit, TScale);
    Changed |= SliderRow::Construct(Page, "LsdPwr", "Power ramp lock", Parameters.LsdPowerLockPercent, 0.0f, 100.0f, 1.0f, "%");
    Changed |= SliderRow::Construct(Page, "LsdCst", "Coast ramp lock", Parameters.LsdCoastLockPercent, 0.0f, 100.0f, 1.0f, "%");
    Changed |= SliderRow::Construct(Page, "Tbr", "Torsen bias ratio", Parameters.TorsenBiasRatio, 1.2f, 5.5f, 0.1f, "\xC3\x97");
    Changed |= SliderRow::Construct(Page, "AwdF", "AWD front torque split", Parameters.AwdFrontTorquePercent, 0.0f, 60.0f, 1.0f, "%");
    Page.EndCard();
}

void VehicleInspector::ConstructSuspensionSection(ControlColumn& Page, bool& Changed)
{
    const bool  Imp    = Parameters.UnitDisplayImperial;
    const float KScale = Imp ? 5.71014716f : 1.0f;
    const float LScale = Imp ? 0.03937008f : 1.0f;
    const char* KUnit  = Imp ? "lbf/in" : "kN/m";
    const char* LUnit  = Imp ? "in" : "mm";

    BeginVehicleCard(Page, "Suspension, steering & stability", "SlidersHorizontal", "4-corner kinematics",
                     "Progressive coilovers, digressive two-slope dampers, anti-roll bars, Ackermann rack, and ESC/TCS/ABS.");

    SubHeadingLabel(Page, "Coilover springs and travel");
    Changed |= SliderRow::Construct(Page, "SprF", "Front spring stiffness", Parameters.FrontSpringRateKnpm, 15.0f, 140.0f, 1.0f, KUnit, KScale);
    Changed |= SliderRow::Construct(Page, "SprR", "Rear spring stiffness", Parameters.RearSpringRateKnpm, 15.0f, 140.0f, 1.0f, KUnit, KScale);
    Changed |= SliderRow::Construct(Page, "SprProg", "Progressive hardening", Parameters.ProgressiveSpringFactor, 0.0f, 1.5f, 0.02f, "\xC3\x97");
    Changed |= SliderRow::Construct(Page, "RestF", "Front unloaded strut", Parameters.FrontRestLengthMm, 240.0f, 460.0f, 2.0f, LUnit, LScale);
    Changed |= SliderRow::Construct(Page, "RestR", "Rear unloaded strut", Parameters.RearRestLengthMm, 240.0f, 460.0f, 2.0f, LUnit, LScale);
    Changed |= SliderRow::Construct(Page, "BumpStop", "Bump-stop stiffness", Parameters.BumpStopStiffnessKnpm, 60.0f, 400.0f, 5.0f, KUnit, KScale);

    SubHeadingLabel(Page, "Digressive dampers, camber and anti-roll");
    Changed |= SliderRow::Construct(Page, "DmpBF", "Front bump damping", Parameters.FrontBumpDampingNsm, 1200.0f, 12000.0f, 100.0f, "N\xC2\xB7s/m");
    Changed |= SliderRow::Construct(Page, "DmpRF", "Front rebound damping", Parameters.FrontReboundDampingNsm, 1500.0f, 15000.0f, 100.0f, "N\xC2\xB7s/m");
    Changed |= SliderRow::Construct(Page, "DmpBR", "Rear bump damping", Parameters.RearBumpDampingNsm, 1200.0f, 12000.0f, 100.0f, "N\xC2\xB7s/m");
    Changed |= SliderRow::Construct(Page, "DmpRR", "Rear rebound damping", Parameters.RearReboundDampingNsm, 1500.0f, 15000.0f, 100.0f, "N\xC2\xB7s/m");
    Changed |= SliderRow::Construct(Page, "DmpHi", "High-speed blow-off ratio", Parameters.HighSpeedDampingRatio, 0.15f, 1.0f, 0.02f, "\xC3\x97");
    Changed |= SliderRow::Construct(Page, "ArbF", "Front anti-roll bar", Parameters.FrontAntiRollKnpm, 0.0f, 65.0f, 1.0f, KUnit, KScale);
    Changed |= SliderRow::Construct(Page, "ArbR", "Rear anti-roll bar", Parameters.RearAntiRollKnpm, 0.0f, 65.0f, 1.0f, KUnit, KScale);
    Changed |= SliderRow::Construct(Page, "CamF", "Front static camber", Parameters.FrontStaticCamberDeg, -5.0f, 1.0f, 0.1f, "\xC2\xB0");
    Changed |= SliderRow::Construct(Page, "CamR", "Rear static camber", Parameters.RearStaticCamberDeg, -5.0f, 1.0f, 0.1f, "\xC2\xB0");

    SubHeadingLabel(Page, "Ackermann steering and active stability");
    Changed |= SliderRow::Construct(Page, "SteerMax", "Max inner wheel lock", Parameters.MaxInnerSteerDeg, 20.0f, 52.0f, 1.0f, "\xC2\xB0");
    Changed |= SliderRow::Construct(Page, "SteerRat", "Steering rack ratio", Parameters.SteeringRackRatio, 8.5f, 20.0f, 0.1f, "\xC3\x97");
    Changed |= SliderRow::Construct(Page, "AckPct", "Ackermann geometry", Parameters.AckermannPercent, 0.0f, 120.0f, 1.0f, "%");
    Changed |= SliderRow::Construct(Page, "SpdFloor", "High-speed steer floor", Parameters.SpeedSensitiveSteerFloorPct, 15.0f, 100.0f, 1.0f, "%");
    Changed |= SliderRow::Construct(Page, "TrailMm", "Caster mechanical trail", Parameters.MechanicalTrailMm, 5.0f, 65.0f, 1.0f, LUnit, LScale);
    Changed |= SliderRow::Construct(Page, "EscGain", "ESC yaw-rate gain", Parameters.EscYawGain, 0.0f, 2.0f, 0.05f, "\xC3\x97");
    Changed |= SliderRow::Construct(Page, "TcsSlip", "TCS target slip", Parameters.TcsSlipThresholdPct, 4.0f, 28.0f, 1.0f, "%");
    Changed |= SliderRow::Construct(Page, "AbsSlip", "ABS target slip", Parameters.AbsSlipThresholdPct, 6.0f, 30.0f, 1.0f, "%");
    Page.EndCard();
}

void VehicleInspector::ConstructSoftBodySection(ControlColumn& Page, bool& Changed)
{
    static constexpr const char* FormNames[3] = {"Spring", "XPBD", "3D Hex8 FEM"};
    static constexpr const char* SlipNames[3] = {"GRIT implicit", "Relaxation", "Quasi-static"};

    BeginVehicleCard(Page, "Soft-body tyre continuum & slip", "Torus",
                     FormNames[std::clamp(Parameters.SoftBodyFormulationIndex, 0, 2)],
                     "Selects Radial Spring, XPBD pneumatic carcass, or 3D Hex8 Neo-Hookean FEM plus transient slip dynamics.");

    Changed |= ChoiceRow::Construct(Page, "SbForm", "Soft-body formulation", FormNames, 3, Parameters.SoftBodyFormulationIndex);
    Changed |= ChoiceRow::Construct(Page, "SlipMode", "Transient slip solver", SlipNames, 3, Parameters.SlipSolverModeIndex);

    SubHeadingLabel(Page, "Carcass discretization and XPBD");
    Changed |= IntSliderRow(Page, "SbNodes", "Circumferential vertices", Parameters.CircumferentialNodeCount, 12, 32, "");
    Changed |= IntSliderRow(Page, "SbRings", "Lateral tread rings", Parameters.LateralRingCount, 2, 5, "");
    Changed |= SliderRow::Construct(Page, "SbVertK", "Reference radial stiffness", Parameters.VerticalStiffnessKnpm, 90.0f, 380.0f, 5.0f, "kN/m");
    Changed |= SliderRow::Construct(Page, "SbPressK", "Pneumatic stiffness gain", Parameters.PressureStiffnessGain, 0.2f, 2.0f, 0.05f, "\xC3\x97");
    Changed |= SliderRow::Construct(Page, "SbVolW", "Pneumatic volume preservation", Parameters.PneumaticVolumeWeightPct, 0.0f, 100.0f, 1.0f, "%");
    Changed |= IntSliderRow(Page, "XpbdSub", "XPBD substeps", Parameters.XpbdSubsteps, 1, 12, "");
    Changed |= IntSliderRow(Page, "XpbdItr", "XPBD constraint iterations", Parameters.XpbdIterations, 1, 12, "");
    Changed |= SliderRow::Construct(Page, "SbBulge", "Sidewall contact bulge", Parameters.MaxContactBulgePct, 0.0f, 45.0f, 1.0f, "%");

    SubHeadingLabel(Page, "3D Hex8 Neo-Hookean finite elements");
    Changed |= IntSliderRow(Page, "FemSub", "FEM explicit substeps", Parameters.FemSubsteps, 4, 32, "");
    Changed |= SliderRow::Construct(Page, "FemE", "Carcass Young modulus", Parameters.FemYoungsModulusMpa, 0.8f, 8.0f, 0.1f, "MPa");
    Changed |= SliderRow::Construct(Page, "FemNu", "Rubber Poisson ratio", Parameters.FemPoissonRatio, 0.30f, 0.47f, 0.005f, "");
    Changed |= SliderRow::Construct(Page, "FemMass", "FEM lumped vertex mass", Parameters.FemVertexMassKg, 0.05f, 0.45f, 0.01f, "kg");
    Changed |= SliderRow::Construct(Page, "FemDmp", "Rayleigh damping rate", Parameters.FemDampingRateHz, 2.0f, 45.0f, 1.0f, "Hz");
    Page.EndCard();
}

void VehicleInspector::ConstructCurvesSection(ControlColumn& Page, bool& Changed)
{
    static constexpr const char* CurveLabels[5] = {"Torque", "Brake", "Coolant", "Oil", "Tyre"};
    BeginVehicleCard(Page, "Monotone curve profiles", "Activity", "5 editable",
                     "Fritsch-Carlson monotone cubic Hermite splines guarantee zero overshoot between authored control points.");

    Changed |= ChoiceRow::Construct(Page, "CurveSel", "Active profile", CurveLabels, 5, Parameters.SelectedCurveProfile);
    Changed |= ToggleRow::Construct(Page, "CurveMono", "Smooth Fritsch-Carlson monotone tangents", Parameters.CurveSmoothMonotone);

    auto ResolveCurveVector = [&]() -> std::vector<VehicleCurveControlPoint>& {
        switch (std::clamp(Parameters.SelectedCurveProfile, 0, 4))
        {
            case 1: return Parameters.BrakeTorqueCurve;
            case 2: return Parameters.CoolantTempCurve;
            case 3: return Parameters.OilTempCurve;
            case 4: return Parameters.TyreSurfaceTempCurve;
            default: return Parameters.EngineTorqueCurve;
        }
    };

    std::vector<VehicleCurveControlPoint>& ActivePoints = ResolveCurveVector();
    if (ActivePoints.size() < 2u)
    {
        ActivePoints = {{0.0f, 100.0f}, {1000.0f, 200.0f}};
    }
    Parameters.SelectedCurvePoint = std::clamp(Parameters.SelectedCurvePoint, 0, static_cast<int>(ActivePoints.size()) - 1);

    const float XMin = ActivePoints.front().X;
    const float XMax = std::max(XMin + 1.0f, ActivePoints.back().X);
    float       YMax = 1.0f;
    for (const auto& Pt : ActivePoints) YMax = std::max(YMax, Pt.Y * 1.15f);

    std::array<float, 48> Samples{};
    for (size_t Idx = 0; Idx < Samples.size(); ++Idx)
    {
        const float X = XMin + (XMax - XMin) * (static_cast<float>(Idx) / static_cast<float>(Samples.size() - 1u));
        Samples[Idx]  = std::clamp(EvaluateMonotoneVehicleCurve(ActivePoints, Parameters.CurveSmoothMonotone, X) / YMax, 0.0f, 1.0f);
    }
    const InspectorPalette Palette = InspectorPalette::Resolve();
    DiagramControls::ConstructCurvePlot(Palette, Page.QueryFaces(), CurveLabels[std::clamp(Parameters.SelectedCurveProfile, 0, 4)],
                                        Samples.data(), Samples.size(), IM_COL32(232, 168, 74, 255));

    SubHeadingLabel(Page, "Selected control point");
    Changed |= IntSliderRow(Page, "CurvePtIdx", "Control point index", Parameters.SelectedCurvePoint, 0,
                            static_cast<int>(ActivePoints.size()) - 1, "");
    VehicleCurveControlPoint& Chosen = ActivePoints[static_cast<size_t>(Parameters.SelectedCurvePoint)];
    const float LowerX = Parameters.SelectedCurvePoint == 0
                             ? 0.0f
                             : ActivePoints[static_cast<size_t>(Parameters.SelectedCurvePoint - 1)].X + 1.0f;
    const float UpperX = Parameters.SelectedCurvePoint + 1 >= static_cast<int>(ActivePoints.size())
                             ? std::max(10000.0f, XMax)
                             : ActivePoints[static_cast<size_t>(Parameters.SelectedCurvePoint + 1)].X - 1.0f;
    Changed |= SliderRow::Construct(Page, "CurvePtX", "Point X", Chosen.X, LowerX, std::max(LowerX + 1.0f, UpperX), 5.0f, "");
    Changed |= SliderRow::Construct(Page, "CurvePtY", "Point Y", Chosen.Y, 0.0f, std::max(200.0f, YMax * 1.25f), 1.0f, "");

    const ActionEntry Actions[2] = {
        {"AddPt", "+ Insert midpoint", true, "Inserts a control point in the widest interval"},
        {"RemPt", "- Remove point", ActivePoints.size() > 2u, "A curve retains at least two points"}};
    const int Clicked = ActionRow::Construct(Page, Actions);
    if (Clicked == 0)
    {
        size_t BestGapIdx = 0u;
        float  BestGap    = 0.0f;
        for (size_t Idx = 0u; Idx + 1u < ActivePoints.size(); ++Idx)
        {
            const float Gap = ActivePoints[Idx + 1u].X - ActivePoints[Idx].X;
            if (Gap > BestGap) { BestGap = Gap; BestGapIdx = Idx; }
        }
        const float MidX = 0.5f * (ActivePoints[BestGapIdx].X + ActivePoints[BestGapIdx + 1u].X);
        const float MidY = EvaluateMonotoneVehicleCurve(ActivePoints, Parameters.CurveSmoothMonotone, MidX);
        ActivePoints.insert(ActivePoints.begin() + static_cast<std::ptrdiff_t>(BestGapIdx + 1u), {MidX, MidY});
        Parameters.SelectedCurvePoint = static_cast<int>(BestGapIdx + 1u);
        Changed = true;
    }
    else if (Clicked == 1 && ActivePoints.size() > 2u)
    {
        ActivePoints.erase(ActivePoints.begin() + Parameters.SelectedCurvePoint);
        Parameters.SelectedCurvePoint = std::clamp(Parameters.SelectedCurvePoint, 0, static_cast<int>(ActivePoints.size()) - 1);
        Changed = true;
    }
    Page.EndCard();
}

void VehicleInspector::ConstructThermalSection(ControlColumn& Page, bool& Changed)
{
    static constexpr const char* OilGrades[4] = {"0W-20", "5W-30", "5W-40", "10W-60"};
    BeginVehicleCard(Page, "Thermal & engine fluids", "Thermometer", OilGrades[std::clamp(Parameters.OilViscosityGradeIndex, 0, 3)],
                     "Coolant thermostat & radiator airflow, oil kinematic viscosity grade, limp derate, and BSFC.");

    Changed |= ChoiceRow::Construct(Page, "OilGrade", "Oil viscosity grade", OilGrades, 4, Parameters.OilViscosityGradeIndex);
    Changed |= SliderRow::Construct(Page, "CoolTgt", "Coolant target", Parameters.CoolantTargetC, 70.0f, 120.0f, 1.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "OilTgt", "Oil target", Parameters.OilTargetC, 80.0f, 150.0f, 1.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "BrkTgt", "Brake rotor target", Parameters.BrakeTargetC, 300.0f, 780.0f, 5.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "AmbTmp", "Ambient air", Parameters.AmbientTemperatureC, -10.0f, 48.0f, 1.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "CoolCap", "Radiator cooling capacity", Parameters.CoolingCapacityKw, 10.0f, 140.0f, 1.0f, "kW");
    Changed |= SliderRow::Construct(Page, "ThermOpn", "Thermostat opening", Parameters.ThermostatOpeningC, 60.0f, 100.0f, 1.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "FanCut", "Electric fan cut-in", Parameters.RadiatorFanThresholdC, 78.0f, 112.0f, 1.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "LimpTmp", "Overheat limp derate", Parameters.OverheatDerateStartC, 102.0f, 138.0f, 1.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "HeatCap", "Coolant heat capacity", Parameters.CoolantHeatCapacityKjK, 8.0f, 60.0f, 0.5f, "kJ/K");
    Changed |= SliderRow::Construct(Page, "Bsfc", "Brake-specific fuel burn", Parameters.SpecificFuelBurnGkwh, 185.0f, 360.0f, 1.0f, "g/kWh");
    Page.EndCard();
}

void VehicleInspector::ConstructTyresSection(ControlColumn& Page, bool& Changed)
{
    int Enclosed = 0;
    for (const auto& Corner : Parameters.Corners) if (Corner.EnclosedCondition) ++Enclosed;
    char Status[48]{};
    std::snprintf(Status, sizeof(Status), "%d enclosed \xC2\xB7 %d pit", Enclosed, 4 - Enclosed);

    BeginVehicleCard(Page, "Corner tyres & pit-lane enclosure", "CircleDot", Status,
                     "Each tyre is an independent record enclosed by the vehicle; detaching parks it in the pit lane.");

    static constexpr const char* CornerNames[4]   = {"FL", "FR", "RL", "RR"};
    static constexpr const char* CompoundNames[5] = {"Soft", "Medium", "Hard", "Inter", "Wet"};
    Changed |= ChoiceRow::Construct(Page, "CornerSel", "Selected corner", CornerNames, 4, Parameters.SelectedCornerIndex);

    TyreCornerConfiguration& Corner = Parameters.Corners[static_cast<size_t>(std::clamp(Parameters.SelectedCornerIndex, 0, 3))];
    Changed |= ToggleRow::Construct(Page, "CornerEnc", "Enclosed by vehicle (false = Pit Lane)", Corner.EnclosedCondition);
    Changed |= ToggleRow::Construct(Page, "CornerPunc", "Punctured carcass (rapid deflation)", Corner.PuncturedCondition);
    Changed |= ChoiceRow::Construct(Page, "CornerCmp", "Compound", CompoundNames, 5, Corner.CompoundIndex);
    Changed |= SliderRow::Construct(Page, "CornerColdP", "Cold inflation pressure", Corner.ColdPressureKpa, 120.0f, 260.0f, 1.0f, "kPa");
    Changed |= SliderRow::Construct(Page, "CornerSurfT", "Surface temperature", Corner.SurfaceTemperatureC, 20.0f, 160.0f, 1.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "CornerBeltT", "Belt temperature", Corner.BeltTemperatureC, 20.0f, 160.0f, 1.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "CornerGasT", "Internal gas temperature", Corner.GasTemperatureC, 20.0f, 150.0f, 1.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "CornerWear", "Consumed wear", Corner.WearPercent, 0.0f, 100.0f, 1.0f, "%");
    Changed |= SliderRow::Construct(Page, "CornerPcx", "Pacejka longitudinal shape", Corner.PacejkaLongitudinalShape, 1.0f, 2.3f, 0.02f, "");
    Changed |= SliderRow::Construct(Page, "CornerPcy", "Pacejka lateral shape", Corner.PacejkaLateralShape, 0.9f, 2.0f, 0.02f, "");

    const ActionEntry PitActions[3] = {
        {"FitSoft", "Fit Set B \xC2\xB7 Soft", true, "Replaces all 4 corners with fresh Soft slicks"},
        {"DetachAll", "Detach all 4", Enclosed > 0, "Moves all 4 corners into Pit Lane"},
        {"Reattach", "Re-enclose all", Enclosed < 4, "Re-attaches pit-lane tyres to vehicle"}};
    const int PitAction = ActionRow::Construct(Page, PitActions);
    if (PitAction == 0)
    {
        for (size_t Idx = 0; Idx < 4; ++Idx)
        {
            Parameters.Corners[Idx].CompoundIndex      = 0;
            Parameters.Corners[Idx].SetIdentifier      = "SetB";
            Parameters.Corners[Idx].WearPercent        = 0.0f;
            Parameters.Corners[Idx].StintLaps          = 0;
            Parameters.Corners[Idx].EnclosedCondition  = true;
            Parameters.Corners[Idx].PuncturedCondition = false;
        }
        Changed = true;
    }
    else if (PitAction == 1)
    {
        for (auto& C : Parameters.Corners) C.EnclosedCondition = false;
        Changed = true;
    }
    else if (PitAction == 2)
    {
        for (auto& C : Parameters.Corners) C.EnclosedCondition = true;
        Changed = true;
    }
    Page.EndCard();
}

void VehicleInspector::ConstructAeroSection(ControlColumn& Page, const VehiclePerformanceSummary& Summary, bool& Changed)
{
    char CdText[32]{};
    std::snprintf(CdText, sizeof(CdText), "Cd %.2f", Parameters.DragCoefficient);
    BeginVehicleCard(Page, "Active aerodynamics & brakes", "Wind", CdText,
                     "Drag, downforce, Venturi ground-effect suction & stall choke, DRS/airbrake, and rotor thermal fade.");

    char DfStr[32]{}, BrkStr[32]{}, StopStr[32]{};
    std::snprintf(DfStr, sizeof(DfStr), "%.0f", Parameters.DownforceAt200KmhN);
    std::snprintf(BrkStr, sizeof(BrkStr), "%.0f", Parameters.TotalBrakeTorqueNm);
    std::snprintf(StopStr, sizeof(StopStr), "%.1f", Summary.BrakingDistance100M);
    const MetricTile AeroTiles[3] = {
        {"Downforce @ 200", DfStr, "N"},
        {"Brake torque", BrkStr, "Nm"},
        {"100\xE2\x80\x93" "0 stop", StopStr, "m"}};
    MetricTiles::Construct(Page, AeroTiles, 3, MetricTiles::NoLead);

    Changed |= SliderRow::Construct(Page, "AeroCd", "Drag coefficient (Cd)", Parameters.DragCoefficient, 0.18f, 0.60f, 0.01f, "");
    Changed |= SliderRow::Construct(Page, "AeroArea", "Frontal area", Parameters.FrontalAreaM2, 1.2f, 3.2f, 0.01f, "m\xC2\xB2");
    Changed |= SliderRow::Construct(Page, "AeroDf", "Downforce at 200 km/h", Parameters.DownforceAt200KmhN, 0.0f, 2000.0f, 10.0f, "N");
    Changed |= SliderRow::Construct(Page, "AeroCop", "Front aero balance (CoP)", Parameters.AeroBalanceFrontPct, 30.0f, 65.0f, 1.0f, "%");
    Changed |= SliderRow::Construct(Page, "DrsDrag", "DRS drag reduction", Parameters.DrsDragReductionPct, 0.0f, 45.0f, 1.0f, "%");
    Changed |= SliderRow::Construct(Page, "DrsSpd", "DRS minimum speed", Parameters.DrsActivationSpeedKmh, 60.0f, 220.0f, 5.0f, "km/h");
    Changed |= SliderRow::Construct(Page, "AirBrk", "Airbrake extra Cd", Parameters.AirbrakeExtraCd, 0.0f, 0.65f, 0.01f, "");
    Changed |= SliderRow::Construct(Page, "VentOpt", "Venturi optimal height", Parameters.VenturiOptimalHeightMm, 35.0f, 140.0f, 1.0f, "mm");
    Changed |= SliderRow::Construct(Page, "VentGain", "Venturi peak suction gain", Parameters.VenturiPeakGain, 1.0f, 2.2f, 0.02f, "\xC3\x97");
    Changed |= SliderRow::Construct(Page, "VentChk", "Diffuser choke height", Parameters.VenturiChokeHeightMm, 10.0f, 55.0f, 1.0f, "mm");

    SubHeadingLabel(Page, "Braking and thermal fade");
    Changed |= SliderRow::Construct(Page, "BrkTot", "Total brake torque", Parameters.TotalBrakeTorqueNm, 800.0f, 5000.0f, 25.0f, "Nm");
    Changed |= SliderRow::Construct(Page, "BrkBal", "Front brake bias", Parameters.BrakeBalanceFrontPct, 45.0f, 75.0f, 1.0f, "%");
    Changed |= SliderRow::Construct(Page, "HandBrk", "Rear handbrake torque", Parameters.HandbrakeTorqueNm, 400.0f, 3800.0f, 25.0f, "Nm");
    Changed |= SliderRow::Construct(Page, "FadeOn", "Pad fade onset", Parameters.BrakeFadeStartC, 380.0f, 820.0f, 5.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "FadeMax", "Peak thermal fade loss", Parameters.MaxBrakeFadePct, 0.0f, 80.0f, 1.0f, "%");
    Changed |= ToggleRow::Construct(Page, "LineLock", "Front line-lock staging", Parameters.LineLockFrontOnly);
    Page.EndCard();
}

void VehicleInspector::ConstructSimulationSection(ControlColumn& Page, bool& Changed)
{
    const int InnerHz = Parameters.OuterTickHz * Parameters.SubstepsPerOuterTick;
    char      InnerText[48]{};
    std::snprintf(InnerText, sizeof(InnerText), "%d Hz inner", InnerHz);

    BeginVehicleCard(Page, "JoltAdapter & PhysicsExecutionThread", "Cpu", InnerText,
                     "Dedicated C++20 background physics thread, N-way substepping, SLERP pose blending, and force-zone coupling.");

    char OuterStr[24]{}, InnerStr[24]{}, DtStr[24]{};
    std::snprintf(OuterStr, sizeof(OuterStr), "%d", Parameters.OuterTickHz);
    std::snprintf(InnerStr, sizeof(InnerStr), "%d", InnerHz);
    std::snprintf(DtStr, sizeof(DtStr), "%.2f", 1000.0f / static_cast<float>(std::max(1, InnerHz)));
    const MetricTile SimTiles[3] = {
        {"Outer tick", OuterStr, "Hz"},
        {"Substep rate", InnerStr, "Hz"},
        {"Substep dt", DtStr, "ms"}};
    MetricTiles::Construct(Page, SimTiles, 3, 1);

    Changed |= IntSliderRow(Page, "SimOuter", "Outer fixed tick rate", Parameters.OuterTickHz, 30, 240, "Hz");
    Changed |= IntSliderRow(Page, "SimSubs", "Substeps per outer tick", Parameters.SubstepsPerOuterTick, 1, 8, "");
    Changed |= IntSliderRow(Page, "SimCatch", "Maximum catch-up outer steps", Parameters.MaximumCatchUpSteps, 1, 16, "");
    Changed |= ToggleRow::Construct(Page, "SimHold", "Coupled Coulomb-cone slope stationarity hold", Parameters.StationarityHoldEnabled);
    Changed |= ToggleRow::Construct(Page, "SimZones", "World force-zone aerodynamic coupling", Parameters.ForceZoneCouplingEnabled);
    Page.EndCard();
}

void VehicleInspector::ConstructPerformanceSection(ControlColumn& Page, const VehiclePerformanceSummary& Summary)
{
    char Status[32]{};
    std::snprintf(Status, sizeof(Status), "%.2f s 0-100", Summary.ZeroToHundredSec);
    BeginVehicleCard(Page, "Longitudinal performance estimate", "TrendingUp", Status,
                     "Forward launch integration across the authored torque curve, gear train, weight transfer, and aero drag.");

    char T100[24]{}, Dist[24]{}, Top[24]{}, Stop[24]{};
    std::snprintf(T100, sizeof(T100), "%.2f", Summary.ZeroToHundredSec);
    std::snprintf(Dist, sizeof(Dist), "%.1f", Summary.LaunchDistanceMetres);
    std::snprintf(Top, sizeof(Top), "%.0f", Summary.TopSpeedKmh);
    std::snprintf(Stop, sizeof(Stop), "%.1f", Summary.BrakingDistance100M);
    const MetricTile PerfTiles[4] = {
        {"0\xE2\x80\x93" "100 km/h", T100, "s"},
        {"Launch distance", Dist, "m"},
        {"Top speed", Top, "km/h"},
        {"100\xE2\x80\x93" "0 braking", Stop, "m"}};
    MetricTiles::Construct(Page, PerfTiles, 4, 0);
    Page.EndCard();
}

void VehicleInspector::ConstructTelemetryDock(ControlColumn& Page)
{
    if (!Parameters.TelemetryEnabled)
    {
        BeginVehicleCard(Page, "Live telemetry dock detached", "Activity", "OFF",
                         "Telemetry is disabled on this vehicle record. Enable the Telemetry aid to attach the read-only dock.");
        if (NextButton::Construct(Page, "AttachTel", "Enable telemetry aid", true))
        {
            Parameters.TelemetryEnabled = true;
            Parameters.AdvanceRevision();
        }
        Page.EndCard();
        return;
    }

    if (!TelemetryPaused)
    {
        TelemetryElapsedSec += 1.0f / 60.0f;
    }

    const float Phase     = std::fmod(TelemetryElapsedSec * 0.42f, 2.0f * VehiclePi);
    const float SpeedKmh  = 148.0f + 92.0f * std::sin(Phase);
    const float Rpm       = std::clamp(3200.0f + 4100.0f * (0.5f + 0.5f * std::sin(Phase * 1.7f)),
                                       Parameters.IdleSpeedRpm, Parameters.RedlineRpm);
    const int   Gear      = std::clamp(2 + static_cast<int>((SpeedKmh - 55.0f) / 42.0f), 1, Parameters.ForwardGearCount);
    const float BoostBar  = Parameters.InductionModeIndex == 0
                                ? 0.0f
                                : Parameters.SuperchargerMaxBoostBar * (0.55f + 0.45f * std::max(0.0f, std::sin(Phase * 1.7f)));
    const float DeflectMm = 18.0f + 14.0f * (0.5f + 0.5f * std::cos(Phase * 2.1f));

    BeginVehicleCard(Page, "Live telemetry dock \xC2\xB7 read-only", "Activity", TelemetryPaused ? "PAUSED" : "LIVE",
                     "Read-only runtime instrumentation: watching telemetry never mutates the authored vehicle record.");

    char Spd[24]{}, RpmStr[24]{}, GrStr[16]{}, BstStr[24]{};
    std::snprintf(Spd, sizeof(Spd), "%.0f", SpeedKmh);
    std::snprintf(RpmStr, sizeof(RpmStr), "%.0f", Rpm);
    std::snprintf(GrStr, sizeof(GrStr), "%d", Gear);
    std::snprintf(BstStr, sizeof(BstStr), "%.2f", BoostBar);
    const MetricTile LiveTiles[4] = {
        {"Road speed", Spd, "km/h"},
        {"Engine speed", RpmStr, "rpm"},
        {"Engaged gear", GrStr, ""},
        {"Manifold boost", BstStr, "bar"}};
    MetricTiles::Construct(Page, LiveTiles, 4, 0);

    char DefStr[24]{}, FlStr[24]{}, CoolStr[24]{};
    std::snprintf(DefStr, sizeof(DefStr), "%.1f", DeflectMm);
    std::snprintf(FlStr, sizeof(FlStr), "%.1f", std::max(0.0f, Parameters.FuelLoadLitres - TelemetryElapsedSec * 0.018f));
    std::snprintf(CoolStr, sizeof(CoolStr), "%.1f", Parameters.CoolantTargetC + 2.4f * std::sin(Phase * 0.5f));
    const MetricTile SubTiles[3] = {
        {"Peak tyre deflection", DefStr, "mm"},
        {"Remaining fuel", FlStr, "L"},
        {"Coolant", CoolStr, "\xC2\xB0" "C"}};
    MetricTiles::Construct(Page, SubTiles, 3, MetricTiles::NoLead);

    const ActionEntry DockActions[2] = {
        {"TelPause", TelemetryPaused ? "Resume stream" : "Pause stream", true, nullptr},
        {"TelBack", "Return to authoring inspector", true, nullptr}};
    const int Act = ActionRow::Construct(Page, DockActions);
    if (Act == 0) TelemetryPaused = !TelemetryPaused;
    if (Act == 1) Parameters.InspectorModeIndex = 0;
    Page.EndCard();
}

void VehicleInspector::ConstructInspector()
{
    const auto    Theme = ControlTheme::ResolveNative();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    bool          Changed = false;

    const std::string Title = "Vehicle \xC2\xB7 " + Parameters.VehicleName;
    if (PageHeading::Construct(Page, "VehicleHeader", "VEHICLE / CHASSIS & POWERTRAIN", Title.c_str(), "Reset baseline",
                               "Custom vehicle record with 4 enclosed tyres, monotone curves, 3-mode soft-body tyres, and Jolt substepping.",
                               "Restore every GT-04 vehicle & corner parameter to baseline defaults"))
    {
        VehicleConfiguration Defaults{};
        Defaults.Revision = Parameters.Revision;
        Parameters        = std::move(Defaults);
        Changed           = true;
    }

    static constexpr const char* ModeOptions[2] = {"Inspector \xC2\xB7 Authoring", "Telemetry \xC2\xB7 Live Dock"};
    (void)ChoiceRow::Construct(Page, "VehMode", "Workspace mode", ModeOptions, 2, Parameters.InspectorModeIndex);

    int UnitIdx = Parameters.UnitDisplayImperial ? 1 : 0;
    static constexpr const char* UnitOptions[2] = {"Metric \xC2\xB7 kg \xC2\xB7 kW \xC2\xB7 Nm", "Imperial \xC2\xB7 lb \xC2\xB7 hp \xC2\xB7 lb\xC2\xB7" "ft"};
    if (ChoiceRow::Construct(Page, "VehUnits", "Display units", UnitOptions, 2, UnitIdx))
    {
        Parameters.UnitDisplayImperial = (UnitIdx == 1);
    }

    if (Parameters.InspectorModeIndex == 1)
    {
        ConstructTelemetryDock(Page);
        FooterRow::Construct(Page, "Levelspace record \xC2\xB7 Vehicle", "Read-only telemetry dock");
        Page.Finalize();
        return;
    }

    const VehiclePerformanceSummary Summary = EvaluateVehiclePerformance(Parameters);
    ConstructHeroAndChips(Page, Summary, Changed);
    ConstructSectionTabs(Page);

    switch (ActiveSection)
    {
        case 0: ConstructChassisSection(Page, Changed); break;
        case 1: ConstructEngineSection(Page, Summary, Changed); break;
        case 2: ConstructSuspensionSection(Page, Changed); break;
        case 3: ConstructSoftBodySection(Page, Changed); break;
        case 4: ConstructCurvesSection(Page, Changed); break;
        case 5: ConstructThermalSection(Page, Changed); break;
        case 6: ConstructTyresSection(Page, Changed); break;
        case 7: ConstructAeroSection(Page, Summary, Changed); break;
        case 8: ConstructSimulationSection(Page, Changed); break;
        default: ConstructPerformanceSection(Page, Summary); break;
    }

    FooterRow::Construct(Page, "Levelspace record \xC2\xB7 Vehicle", "Saved with the level");
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
    (void)PendingScrollSection;
    (void)SectionOffsets;
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
std::string VehicleInspector::QueryDirectoryReadout() const { return {}; }
void        VehicleInspector::ConfigureSection(int) noexcept {}
void        VehicleInspector::ConstructInspector() {}
} // namespace Frontier

#endif
