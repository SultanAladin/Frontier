/*==========================================================================================================================================
                                                 NATIVEVEHICLEINSPECTORVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Headless C++ validation & CPU-rasterized PNG capture of VehicleRecord, TyreRecord, VehicleInspector, and TyreInspector.

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Interface/Inspectors/TyreInspector.h"
#include "Engine/Interface/Inspectors/VehicleInspector.h"
#include "Engine/Scene/SceneWorld.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <imgui_internal.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

int PassedCount = 0;
int FailedCount = 0;

void Verify(bool Condition, const std::string& Label)
{
    if (Condition)
    {
        ++PassedCount;
        std::printf("  [PASS] %s\n", Label.c_str());
    }
    else
    {
        ++FailedCount;
        std::fprintf(stderr, "  [FAIL] %s\n", Label.c_str());
    }
}

class InspectorFrameRasterizer
{
  public:
    int Width         = 460;
    int Height        = 2400;
    float ContentBottom = 0.0f;

    explicit InspectorFrameRasterizer(ControlCentrePanel& InCentre) noexcept : Centre(InCentre) {}

    void ConstructFrame(const std::function<void()>& BuildPage)
    {
        ImGuiIO& Io    = ImGui::GetIO();
        Io.DisplaySize = {static_cast<float>(Width), static_cast<float>(Height)};
        Centre.PrepareInterface();
        ImGui::NewFrame();
        (void)Centre.Integrate(1.0 / 60.0);
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(Io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
        ImGui::Begin("VehicleInspectorCapture", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
        BuildPage();
        ContentBottom = ImGui::GetCursorScreenPos().y;
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::Render();
        FrontierExhibit::AcknowledgeTextures();
    }

    bool CapturePng(RecordInspector& Inspector, const std::filesystem::path& TargetPng)
    {
        Height    = 4200;
        auto Page = [&] { Inspector.ConstructInspector(); };
        for (int Warm = 0; Warm < 3; ++Warm) ConstructFrame(Page);
        Height = std::clamp(static_cast<int>(std::ceil(ContentBottom)) + 12, 420, 4200);
        ConstructFrame(Page);
        ConstructFrame(Page);

        const ImDrawData*    DrawData = ImGui::GetDrawData();
        std::vector<uint8_t> Pixels(static_cast<size_t>(Width) * static_cast<size_t>(Height) * 3u, 14u);
        for (const ImDrawList* CmdList : DrawData->CmdLists)
        {
            for (const ImDrawVert& Vert : CmdList->VtxBuffer)
            {
                if (!std::isfinite(Vert.pos.x) || !std::isfinite(Vert.pos.y))
                {
                    return false;
                }
            }
            FrontierExhibit::RasterizeCommands(CmdList, Pixels.data(), Width, Height,
                                               DrawData->DisplayPos, DrawData->FramebufferScale);
        }
        std::filesystem::create_directories(TargetPng.parent_path());
        return stbi_write_png(TargetPng.string().c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0;
    }

    bool CaptureDirectory(SceneDirectoryPanel& Directory, const std::filesystem::path& TargetPng)
    {
        Height = 900;
        auto Page = [&] { Directory.ConstructInterface(); };
        for (int Warm = 0; Warm < 4; ++Warm) ConstructFrame(Page);
        const ImDrawData* Rendered = ImGui::GetDrawData();
        std::vector<uint8_t> Pixels(static_cast<size_t>(Width) * static_cast<size_t>(Height) * 3u, 14u);
        for (const ImDrawList* Commands : Rendered->CmdLists)
            FrontierExhibit::RasterizeCommands(Commands, Pixels.data(), Width, Height,
                                               Rendered->DisplayPos, Rendered->FramebufferScale);
        return stbi_write_png(TargetPng.string().c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0;
    }

  private:
    ControlCentrePanel& Centre;
};

void ValidateCurvesAndPerformance()
{
    VehicleConfiguration Base{};
    const float          Tq4500 = EvaluateMonotoneVehicleCurve(Base.EngineTorqueCurve, true, 4500.0f);
    Verify(std::abs(Tq4500 - 640.0f) < 1.0e-3f, "Monotone curve passes exactly through authored 4500 rpm / 640 Nm knot");

    bool NoOvershoot = true;
    for (size_t Idx = 0; Idx + 1u < Base.EngineTorqueCurve.size(); ++Idx)
    {
        const float Lo = std::min(Base.EngineTorqueCurve[Idx].Y, Base.EngineTorqueCurve[Idx + 1u].Y) - 1.0e-3f;
        const float Hi = std::max(Base.EngineTorqueCurve[Idx].Y, Base.EngineTorqueCurve[Idx + 1u].Y) + 1.0e-3f;
        for (int Sub = 0; Sub <= 16; ++Sub)
        {
            const float X = Base.EngineTorqueCurve[Idx].X +
                            (Base.EngineTorqueCurve[Idx + 1u].X - Base.EngineTorqueCurve[Idx].X) * (Sub / 16.0f);
            const float Y = EvaluateMonotoneVehicleCurve(Base.EngineTorqueCurve, true, X);
            if (Y < Lo || Y > Hi) NoOvershoot = false;
        }
    }
    Verify(NoOvershoot, "Fritsch-Carlson monotone curve exhibits zero overshoot across all intervals");

    const VehiclePerformanceSummary PerfBase = EvaluateVehiclePerformance(Base);
    Verify(PerfBase.ZeroToHundredSec > 2.4f && PerfBase.ZeroToHundredSec < 5.2f && PerfBase.TopSpeedKmh > 250.0f,
           "GT-04 performance estimate produces physically realistic 0-100 km/h and top speed");

    VehicleConfiguration Race = Base;
    Race.RaceTuneEnabled      = true;
    const VehiclePerformanceSummary PerfRace = EvaluateVehiclePerformance(Race);
    Verify(PerfRace.ZeroToHundredSec < PerfBase.ZeroToHundredSec && PerfRace.PeakPowerKw > PerfBase.PeakPowerKw,
           "Enabling RaceTune increases peak power and reduces 0-100 km/h time");
}

void ValidateVehicleAssembly(const std::filesystem::path& ScratchDir)
{
    SceneWorld World;
    const RecordToken VehicleToken = World.ConstructVehicleAssembly("Road vehicle");
    auto* Vehicle = dynamic_cast<VehicleRecord*>(World.FindRecord(VehicleToken));
    const auto Tyres = World.FindAllByType("Tyre");
    const auto Rims = World.FindAllByType("Geometry");
    Verify(Vehicle != nullptr && Tyres.size() == 4 && Rims.size() == 4,
           "Vehicle assembly constructs four tyre entries and four geometry rims");
    bool NestingValid = Vehicle != nullptr && Tyres.size() == 4 && Rims.size() == 4;
    for (const auto* Entry : Tyres)
    {
        if (Entry->QueryParentToken() != VehicleToken ||
            SceneDirectory::Instance().QueryParent(Entry->QueryToken()) != VehicleToken) NestingValid = false;
        int RimCount = 0;
        for (const auto* Rim : Rims) if (Rim->QueryParentToken() == Entry->QueryToken()) ++RimCount;
        if (RimCount != 1) NestingValid = false;
    }
    Verify(NestingValid, "Outliner nests every tyre beneath the vehicle and one rim beneath each tyre");
    Verify(InspectorRuntime::Instance().FindInspector(VehicleToken) != nullptr &&
               !Tyres.empty() && dynamic_cast<TyreInspector*>(InspectorRuntime::Instance().FindInspector(Tyres[0]->QueryToken())) != nullptr &&
               !Rims.empty() && dynamic_cast<GeometryInspector*>(InspectorRuntime::Instance().FindInspector(Rims[0]->QueryToken())) != nullptr,
           "Vehicle, tyre and rim entries have the appropriate native inspectors");
    SceneDirectoryPanel DirectoryPanel;
    DirectoryPanel.Synchronize(SceneDirectory::Instance());
    int TyreRows = 0;
    int RimRows = 0;
    for (const SceneDirectoryRow& Row : DirectoryPanel.QueryRows())
    {
        for (const auto* Tyre : Tyres) if (Row.Token == Tyre->QueryToken() && Row.DepthTier == 1) ++TyreRows;
        for (const auto* Rim : Rims) if (Row.Token == Rim->QueryToken() && Row.DepthTier == 2) ++RimRows;
    }
    Verify(TyreRows == 4 && RimRows == 4, "Outliner projection exposes tyre and rim entries at the correct depths");
    Verify(SceneDirectory::Instance().QueryMetadata(VehicleToken).GlyphIdentifier == "VehicleSilhouette" &&
               !Tyres.empty() && SceneDirectory::Instance().QueryMetadata(Tyres[0]->QueryToken()).GlyphIdentifier == "TyreTread" &&
               !Rims.empty() && SceneDirectory::Instance().QueryMetadata(Rims[0]->QueryToken()).GlyphIdentifier == "RimSpokes",
           "Vehicle, tyre and rim use their distinct glyphs");
    if (Vehicle == nullptr || Tyres.empty()) return;
    auto* FirstTyre = dynamic_cast<TyreRecord*>(World.FindRecord(Tyres[0]->QueryToken()));
    if (FirstTyre == nullptr) return;
    FirstTyre->AccessConfiguration().ColdPressureKpa = 213.0f;
    Verify(Vehicle->QueryConfiguration().Corners[0].ColdPressureKpa == 213.0f,
           "Tyre inspector and vehicle corner use the same configuration");
    Vehicle->AccessConfiguration().Corners[0].WearPercent = 37.0f;
    Verify(FirstTyre->QueryConfiguration().WearPercent == 37.0f,
           "Vehicle corner edits are visible to the fitted tyre entry");
    const auto LevelPath = ScratchDir / "VehicleAssembly.levelspace";
    std::string Diagnostic;
    Verify(World.SaveLevelSpace(LevelPath, &Diagnostic), "Vehicle assembly saves its nested entries");
    SceneWorld Reloaded;
    Verify(Reloaded.LoadLevelSpace(LevelPath, &Diagnostic), "Vehicle assembly reloads its nested entries");
    auto* ReloadedVehicle = dynamic_cast<VehicleRecord*>(Reloaded.FindFirstByType("Vehicle"));
    auto* ReloadedTyre = dynamic_cast<TyreRecord*>(Reloaded.FindFirstByType("Tyre"));
    Verify(ReloadedVehicle != nullptr && ReloadedTyre != nullptr && Reloaded.QueryRecordCount() == 9 &&
               ReloadedTyre->QueryParentToken() == ReloadedVehicle->QueryToken() &&
               &ReloadedTyre->AccessConfiguration() == &ReloadedVehicle->AccessConfiguration().Corners[0] &&
               ReloadedTyre->QueryConfiguration().WearPercent == 37.0f,
           "Reload restores nesting and the fitted tyre configuration link without duplicate entries");
    if (ReloadedVehicle != nullptr && ReloadedTyre != nullptr)
    {
        const RecordToken SavedTyre = ReloadedTyre->QueryToken();
        Verify(Reloaded.DestroyRecord(ReloadedVehicle->QueryToken()), "Vehicle entry can be removed");
        ReloadedTyre = dynamic_cast<TyreRecord*>(Reloaded.FindRecord(SavedTyre));
        Verify(ReloadedTyre != nullptr && !ReloadedTyre->QueryParentToken().IsValid() &&
                   ReloadedTyre->QueryConfiguration().WearPercent == 37.0f,
               "Removing the vehicle preserves the tyre settings independently");
    }
}

void ValidateLevelSpacePersistence(const std::filesystem::path& ScratchDir)
{
    const std::filesystem::path LevelFile = ScratchDir / "VehicleTest.levelspace";
    SceneWorld                  Authored;
    const RecordToken           VehTok = Authored.ConstructRecord("Vehicle", "GT-04 Works");
    const RecordToken           FlTok  = Authored.ConstructRecord("Tyre", "Tyre FL", VehTok);
    Verify(VehTok.IsValid() && FlTok.IsValid(), "SceneWorld constructs native Vehicle and Tyre records");

    auto* VehRec  = dynamic_cast<VehicleRecord*>(Authored.FindRecord(VehTok));
    auto* TyreRec = dynamic_cast<TyreRecord*>(Authored.FindRecord(FlTok));
    Verify(VehRec != nullptr && TyreRec != nullptr, "Constructed records resolve as VehicleRecord and TyreRecord");

    VehicleConfiguration& V = VehRec->AccessConfiguration();
    V.VehicleName                 = "GT-04 Evo";
    V.KerbMassKg                  = 1310.0f;
    V.RaceTuneEnabled             = true;
    V.SuperchargerTypeIndex       = 2;   // Centrifugal
    V.SuperchargerMaxBoostBar     = 1.15f;
    V.SoftBodyFormulationIndex    = 2;   // Fem3D
    V.FemYoungsModulusMpa         = 3.4f;
    V.OuterTickHz                 = 120;
    V.SubstepsPerOuterTick        = 3;
    V.Corners[2].EnclosedCondition= false;   // RL detached in pit lane!

    V.UnitDisplayImperial = !V.UnitDisplayImperial;
    V.InspectorModeIndex += 1;
    V.FuelReservePercent += 0.125f;
    V.FuelBurnKgPerHour += 0.125f;
    V.ThermostatOpeningC += 0.125f;
    V.SuperchargerBypassParasiticPct += 0.125f;
    V.SuperchargerBypassThrottlePct += 0.125f;
    V.TurboSpoolUpRate += 0.125f;
    V.TurboSpoolDownRate += 0.125f;
    V.TorqueGainPerBar += 0.125f;
    V.DrivelineEfficiencyPct += 0.125f;
    V.LoadedWheelRadiusMm += 0.125f;
    V.PeakTyreFriction += 0.125f;
    V.LsdPowerLockPercent += 0.125f;
    V.LsdCoastLockPercent += 0.125f;
    V.AwdFrontTorquePercent += 0.125f;
    V.ShiftDurationMs += 0.125f;
    V.ClutchCapacityNm += 0.125f;
    V.ProgressiveSpringFactor += 0.125f;
    V.FrontRestLengthMm += 0.125f;
    V.RearRestLengthMm += 0.125f;
    V.BumpStopStiffnessKnpm += 0.125f;
    V.FrontBumpDampingNsm += 0.125f;
    V.FrontReboundDampingNsm += 0.125f;
    V.RearBumpDampingNsm += 0.125f;
    V.RearReboundDampingNsm += 0.125f;
    V.HighSpeedDampingRatio += 0.125f;
    V.DamperKneeVelocityMps += 0.125f;
    V.FrontAntiRollKnpm += 0.125f;
    V.RearAntiRollKnpm += 0.125f;
    V.FrontStaticCamberDeg += 0.125f;
    V.RearStaticCamberDeg += 0.125f;
    V.MaxInnerSteerDeg += 0.125f;
    V.SteeringRackRatio += 0.125f;
    V.SpeedSensitiveSteerFloorPct += 0.125f;
    V.MechanicalTrailMm += 0.125f;
    V.LaunchControlEnabled = !V.LaunchControlEnabled;
    V.DrsAidEnabled = !V.DrsAidEnabled;
    V.RevLimiterEnabled = !V.RevLimiterEnabled;
    V.TelemetryEnabled = !V.TelemetryEnabled;
    V.EscYawGain += 0.125f;
    V.TcsSlipThresholdPct += 0.125f;
    V.AbsSlipThresholdPct += 0.125f;
    V.CircumferentialNodeCount += 1;
    V.LateralRingCount += 1;
    V.VerticalStiffnessKnpm += 0.125f;
    V.PressureStiffnessGain += 0.125f;
    V.PneumaticVolumeWeightPct += 0.125f;
    V.XpbdSubsteps += 1;
    V.XpbdIterations += 1;
    V.MaxContactBulgePct += 0.125f;
    V.FemVertexMassKg += 0.125f;
    V.FemDampingRateHz += 0.125f;
    V.OilTargetC += 0.125f;
    V.BrakeTargetC += 0.125f;
    V.AmbientTemperatureC += 0.125f;
    V.CoolingCapacityKw += 0.125f;
    V.CoolantHeatCapacityKjK += 0.125f;
    V.RadiatorAirflowGain += 0.125f;
    V.RadiatorFanThresholdC += 0.125f;
    V.OverheatDerateStartC += 0.125f;
    V.SpecificFuelBurnGkwh += 0.125f;
    V.FrontalAreaM2 += 0.125f;
    V.TotalBrakeTorqueNm += 0.125f;
    V.BrakeBalanceFrontPct += 0.125f;
    V.RollingResistance += 0.125f;
    V.AeroBalanceFrontPct += 0.125f;
    V.PitchAeroSensitivity += 0.125f;
    V.DrsDragReductionPct += 0.125f;
    V.DrsDownforceLossPct += 0.125f;
    V.DrsActivationSpeedKmh += 0.125f;
    V.AirbrakeExtraCd += 0.125f;
    V.AirbrakeDownforceN += 0.125f;
    V.VenturiOptimalHeightMm += 0.125f;
    V.VenturiChokeHeightMm += 0.125f;
    V.HandbrakeTorqueNm += 0.125f;
    V.BrakeFadeStartC += 0.125f;
    V.MaxBrakeFadePct += 0.125f;
    V.MaximumCatchUpSteps += 1;
    V.TelemetrySampleRateHz += 1;
    V.SelectedCurveProfile += 1;
    V.SelectedCurvePoint += 1;
    V.SelectedCornerIndex += 1;

    TyreCornerConfiguration& T = TyreRec->AccessConfiguration();
    T.CornerCode               = "FL";
    T.CompoundIndex            = 0;   // Soft
    T.ColdPressureKpa          = 192.0f;
    T.PuncturedCondition       = true;

    T.UnitDisplayImperial = !T.UnitDisplayImperial;
    T.OptimalSurfaceTempC += 0.125f;
    T.ThermalWindowHalfWidthC += 0.125f;

    V.OilTempCurve[1].Y = 123.5f;
    V.TyreSurfaceTempCurve[1].Y = 67.5f;

    std::string SaveErr;
    Verify(Authored.SaveLevelSpace(LevelFile, &SaveErr), "Vehicle + Tyre .levelspace saves cleanly: " + SaveErr);

    SceneWorld  Reloaded;
    std::string LoadErr;
    Verify(Reloaded.LoadLevelSpace(LevelFile, &LoadErr), "Vehicle + Tyre .levelspace reloads cleanly: " + LoadErr);

    auto* LoadedVeh  = dynamic_cast<VehicleRecord*>(Reloaded.FindFirstByType("Vehicle"));
    auto* LoadedTyre = dynamic_cast<TyreRecord*>(Reloaded.FindFirstByType("Tyre"));
    Verify(LoadedVeh != nullptr && LoadedTyre != nullptr, "Reloaded .levelspace contains VehicleRecord and TyreRecord");
    if (LoadedVeh && LoadedTyre)
    {
        const VehicleConfiguration&    LV = LoadedVeh->QueryConfiguration();
        const TyreCornerConfiguration& LT = LoadedTyre->QueryConfiguration();
        Verify(LV.UnitDisplayImperial == V.UnitDisplayImperial, "Round-trip UnitDisplayImperial");
        Verify(LV.InspectorModeIndex == V.InspectorModeIndex, "Round-trip InspectorModeIndex");
        Verify(std::abs(LV.FuelReservePercent - V.FuelReservePercent) < 1.0e-3f, "Round-trip FuelReservePercent");
        Verify(std::abs(LV.FuelBurnKgPerHour - V.FuelBurnKgPerHour) < 1.0e-3f, "Round-trip FuelBurnKgPerHour");
        Verify(std::abs(LV.ThermostatOpeningC - V.ThermostatOpeningC) < 1.0e-3f, "Round-trip ThermostatOpeningC");
        Verify(std::abs(LV.SuperchargerBypassParasiticPct - V.SuperchargerBypassParasiticPct) < 1.0e-3f, "Round-trip SuperchargerBypassParasiticPct");
        Verify(std::abs(LV.SuperchargerBypassThrottlePct - V.SuperchargerBypassThrottlePct) < 1.0e-3f, "Round-trip SuperchargerBypassThrottlePct");
        Verify(std::abs(LV.TurboSpoolUpRate - V.TurboSpoolUpRate) < 1.0e-3f, "Round-trip TurboSpoolUpRate");
        Verify(std::abs(LV.TurboSpoolDownRate - V.TurboSpoolDownRate) < 1.0e-3f, "Round-trip TurboSpoolDownRate");
        Verify(std::abs(LV.TorqueGainPerBar - V.TorqueGainPerBar) < 1.0e-3f, "Round-trip TorqueGainPerBar");
        Verify(std::abs(LV.DrivelineEfficiencyPct - V.DrivelineEfficiencyPct) < 1.0e-3f, "Round-trip DrivelineEfficiencyPct");
        Verify(std::abs(LV.LoadedWheelRadiusMm - V.LoadedWheelRadiusMm) < 1.0e-3f, "Round-trip LoadedWheelRadiusMm");
        Verify(std::abs(LV.PeakTyreFriction - V.PeakTyreFriction) < 1.0e-3f, "Round-trip PeakTyreFriction");
        Verify(std::abs(LV.LsdPowerLockPercent - V.LsdPowerLockPercent) < 1.0e-3f, "Round-trip LsdPowerLockPercent");
        Verify(std::abs(LV.LsdCoastLockPercent - V.LsdCoastLockPercent) < 1.0e-3f, "Round-trip LsdCoastLockPercent");
        Verify(std::abs(LV.AwdFrontTorquePercent - V.AwdFrontTorquePercent) < 1.0e-3f, "Round-trip AwdFrontTorquePercent");
        Verify(std::abs(LV.ShiftDurationMs - V.ShiftDurationMs) < 1.0e-3f, "Round-trip ShiftDurationMs");
        Verify(std::abs(LV.ClutchCapacityNm - V.ClutchCapacityNm) < 1.0e-3f, "Round-trip ClutchCapacityNm");
        Verify(std::abs(LV.ProgressiveSpringFactor - V.ProgressiveSpringFactor) < 1.0e-3f, "Round-trip ProgressiveSpringFactor");
        Verify(std::abs(LV.FrontRestLengthMm - V.FrontRestLengthMm) < 1.0e-3f, "Round-trip FrontRestLengthMm");
        Verify(std::abs(LV.RearRestLengthMm - V.RearRestLengthMm) < 1.0e-3f, "Round-trip RearRestLengthMm");
        Verify(std::abs(LV.BumpStopStiffnessKnpm - V.BumpStopStiffnessKnpm) < 1.0e-3f, "Round-trip BumpStopStiffnessKnpm");
        Verify(std::abs(LV.FrontBumpDampingNsm - V.FrontBumpDampingNsm) < 1.0e-3f, "Round-trip FrontBumpDampingNsm");
        Verify(std::abs(LV.FrontReboundDampingNsm - V.FrontReboundDampingNsm) < 1.0e-3f, "Round-trip FrontReboundDampingNsm");
        Verify(std::abs(LV.RearBumpDampingNsm - V.RearBumpDampingNsm) < 1.0e-3f, "Round-trip RearBumpDampingNsm");
        Verify(std::abs(LV.RearReboundDampingNsm - V.RearReboundDampingNsm) < 1.0e-3f, "Round-trip RearReboundDampingNsm");
        Verify(std::abs(LV.HighSpeedDampingRatio - V.HighSpeedDampingRatio) < 1.0e-3f, "Round-trip HighSpeedDampingRatio");
        Verify(std::abs(LV.DamperKneeVelocityMps - V.DamperKneeVelocityMps) < 1.0e-3f, "Round-trip DamperKneeVelocityMps");
        Verify(std::abs(LV.FrontAntiRollKnpm - V.FrontAntiRollKnpm) < 1.0e-3f, "Round-trip FrontAntiRollKnpm");
        Verify(std::abs(LV.RearAntiRollKnpm - V.RearAntiRollKnpm) < 1.0e-3f, "Round-trip RearAntiRollKnpm");
        Verify(std::abs(LV.FrontStaticCamberDeg - V.FrontStaticCamberDeg) < 1.0e-3f, "Round-trip FrontStaticCamberDeg");
        Verify(std::abs(LV.RearStaticCamberDeg - V.RearStaticCamberDeg) < 1.0e-3f, "Round-trip RearStaticCamberDeg");
        Verify(std::abs(LV.MaxInnerSteerDeg - V.MaxInnerSteerDeg) < 1.0e-3f, "Round-trip MaxInnerSteerDeg");
        Verify(std::abs(LV.SteeringRackRatio - V.SteeringRackRatio) < 1.0e-3f, "Round-trip SteeringRackRatio");
        Verify(std::abs(LV.SpeedSensitiveSteerFloorPct - V.SpeedSensitiveSteerFloorPct) < 1.0e-3f, "Round-trip SpeedSensitiveSteerFloorPct");
        Verify(std::abs(LV.MechanicalTrailMm - V.MechanicalTrailMm) < 1.0e-3f, "Round-trip MechanicalTrailMm");
        Verify(LV.LaunchControlEnabled == V.LaunchControlEnabled, "Round-trip LaunchControlEnabled");
        Verify(LV.DrsAidEnabled == V.DrsAidEnabled, "Round-trip DrsAidEnabled");
        Verify(LV.RevLimiterEnabled == V.RevLimiterEnabled, "Round-trip RevLimiterEnabled");
        Verify(LV.TelemetryEnabled == V.TelemetryEnabled, "Round-trip TelemetryEnabled");
        Verify(std::abs(LV.EscYawGain - V.EscYawGain) < 1.0e-3f, "Round-trip EscYawGain");
        Verify(std::abs(LV.TcsSlipThresholdPct - V.TcsSlipThresholdPct) < 1.0e-3f, "Round-trip TcsSlipThresholdPct");
        Verify(std::abs(LV.AbsSlipThresholdPct - V.AbsSlipThresholdPct) < 1.0e-3f, "Round-trip AbsSlipThresholdPct");
        Verify(LV.CircumferentialNodeCount == V.CircumferentialNodeCount, "Round-trip CircumferentialNodeCount");
        Verify(LV.LateralRingCount == V.LateralRingCount, "Round-trip LateralRingCount");
        Verify(std::abs(LV.VerticalStiffnessKnpm - V.VerticalStiffnessKnpm) < 1.0e-3f, "Round-trip VerticalStiffnessKnpm");
        Verify(std::abs(LV.PressureStiffnessGain - V.PressureStiffnessGain) < 1.0e-3f, "Round-trip PressureStiffnessGain");
        Verify(std::abs(LV.PneumaticVolumeWeightPct - V.PneumaticVolumeWeightPct) < 1.0e-3f, "Round-trip PneumaticVolumeWeightPct");
        Verify(LV.XpbdSubsteps == V.XpbdSubsteps, "Round-trip XpbdSubsteps");
        Verify(LV.XpbdIterations == V.XpbdIterations, "Round-trip XpbdIterations");
        Verify(std::abs(LV.MaxContactBulgePct - V.MaxContactBulgePct) < 1.0e-3f, "Round-trip MaxContactBulgePct");
        Verify(std::abs(LV.FemVertexMassKg - V.FemVertexMassKg) < 1.0e-3f, "Round-trip FemVertexMassKg");
        Verify(std::abs(LV.FemDampingRateHz - V.FemDampingRateHz) < 1.0e-3f, "Round-trip FemDampingRateHz");
        Verify(std::abs(LV.OilTargetC - V.OilTargetC) < 1.0e-3f, "Round-trip OilTargetC");
        Verify(std::abs(LV.BrakeTargetC - V.BrakeTargetC) < 1.0e-3f, "Round-trip BrakeTargetC");
        Verify(std::abs(LV.AmbientTemperatureC - V.AmbientTemperatureC) < 1.0e-3f, "Round-trip AmbientTemperatureC");
        Verify(std::abs(LV.CoolingCapacityKw - V.CoolingCapacityKw) < 1.0e-3f, "Round-trip CoolingCapacityKw");
        Verify(std::abs(LV.CoolantHeatCapacityKjK - V.CoolantHeatCapacityKjK) < 1.0e-3f, "Round-trip CoolantHeatCapacityKjK");
        Verify(std::abs(LV.RadiatorAirflowGain - V.RadiatorAirflowGain) < 1.0e-3f, "Round-trip RadiatorAirflowGain");
        Verify(std::abs(LV.RadiatorFanThresholdC - V.RadiatorFanThresholdC) < 1.0e-3f, "Round-trip RadiatorFanThresholdC");
        Verify(std::abs(LV.OverheatDerateStartC - V.OverheatDerateStartC) < 1.0e-3f, "Round-trip OverheatDerateStartC");
        Verify(std::abs(LV.SpecificFuelBurnGkwh - V.SpecificFuelBurnGkwh) < 1.0e-3f, "Round-trip SpecificFuelBurnGkwh");
        Verify(std::abs(LV.FrontalAreaM2 - V.FrontalAreaM2) < 1.0e-3f, "Round-trip FrontalAreaM2");
        Verify(std::abs(LV.TotalBrakeTorqueNm - V.TotalBrakeTorqueNm) < 1.0e-3f, "Round-trip TotalBrakeTorqueNm");
        Verify(std::abs(LV.BrakeBalanceFrontPct - V.BrakeBalanceFrontPct) < 1.0e-3f, "Round-trip BrakeBalanceFrontPct");
        Verify(std::abs(LV.RollingResistance - V.RollingResistance) < 1.0e-3f, "Round-trip RollingResistance");
        Verify(std::abs(LV.AeroBalanceFrontPct - V.AeroBalanceFrontPct) < 1.0e-3f, "Round-trip AeroBalanceFrontPct");
        Verify(std::abs(LV.PitchAeroSensitivity - V.PitchAeroSensitivity) < 1.0e-3f, "Round-trip PitchAeroSensitivity");
        Verify(std::abs(LV.DrsDragReductionPct - V.DrsDragReductionPct) < 1.0e-3f, "Round-trip DrsDragReductionPct");
        Verify(std::abs(LV.DrsDownforceLossPct - V.DrsDownforceLossPct) < 1.0e-3f, "Round-trip DrsDownforceLossPct");
        Verify(std::abs(LV.DrsActivationSpeedKmh - V.DrsActivationSpeedKmh) < 1.0e-3f, "Round-trip DrsActivationSpeedKmh");
        Verify(std::abs(LV.AirbrakeExtraCd - V.AirbrakeExtraCd) < 1.0e-3f, "Round-trip AirbrakeExtraCd");
        Verify(std::abs(LV.AirbrakeDownforceN - V.AirbrakeDownforceN) < 1.0e-3f, "Round-trip AirbrakeDownforceN");
        Verify(std::abs(LV.VenturiOptimalHeightMm - V.VenturiOptimalHeightMm) < 1.0e-3f, "Round-trip VenturiOptimalHeightMm");
        Verify(std::abs(LV.VenturiChokeHeightMm - V.VenturiChokeHeightMm) < 1.0e-3f, "Round-trip VenturiChokeHeightMm");
        Verify(std::abs(LV.HandbrakeTorqueNm - V.HandbrakeTorqueNm) < 1.0e-3f, "Round-trip HandbrakeTorqueNm");
        Verify(std::abs(LV.BrakeFadeStartC - V.BrakeFadeStartC) < 1.0e-3f, "Round-trip BrakeFadeStartC");
        Verify(std::abs(LV.MaxBrakeFadePct - V.MaxBrakeFadePct) < 1.0e-3f, "Round-trip MaxBrakeFadePct");
        Verify(LV.MaximumCatchUpSteps == V.MaximumCatchUpSteps, "Round-trip MaximumCatchUpSteps");
        Verify(LV.TelemetrySampleRateHz == V.TelemetrySampleRateHz, "Round-trip TelemetrySampleRateHz");
        Verify(LV.SelectedCurveProfile == V.SelectedCurveProfile, "Round-trip SelectedCurveProfile");
        Verify(LV.SelectedCurvePoint == V.SelectedCurvePoint, "Round-trip SelectedCurvePoint");
        Verify(LV.SelectedCornerIndex == V.SelectedCornerIndex, "Round-trip SelectedCornerIndex");
        Verify(LV.OilTempCurve[1].Y == 123.5f && LV.TyreSurfaceTempCurve[1].Y == 67.5f,
               "Oil and tyre temperature curves survive level save and reload");
        Verify(LV.VehicleName == "GT-04 Evo" && std::abs(LV.KerbMassKg - 1310.0f) < 1.0e-3f &&
                   LV.RaceTuneEnabled && LV.SuperchargerTypeIndex == 2 &&
                   std::abs(LV.FemYoungsModulusMpa - 3.4f) < 1.0e-3f &&
                   LV.OuterTickHz == 120 && LV.SubstepsPerOuterTick == 3 &&
                   !LV.Corners[2].EnclosedCondition,
               "Reloaded VehicleRecord preserves supercharger, 3D FEM, substepping, and pit-lane corner enclosure");
        Verify(LT.UnitDisplayImperial == T.UnitDisplayImperial, "Round-trip UnitDisplayImperial");
        Verify(std::abs(LT.OptimalSurfaceTempC - T.OptimalSurfaceTempC) < 1.0e-3f, "Round-trip OptimalSurfaceTempC");
        Verify(std::abs(LT.ThermalWindowHalfWidthC - T.ThermalWindowHalfWidthC) < 1.0e-3f, "Round-trip ThermalWindowHalfWidthC");
        Verify(LT.CompoundIndex == 0 && std::abs(LT.ColdPressureKpa - 192.0f) < 1.0e-3f && LT.PuncturedCondition,
               "Reloaded TyreRecord preserves compound, pressure, and puncture state");
    }
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 4)
    {
        std::fprintf(stderr, "Usage: NativeVehicleInspectorValidation <EngineContent> <EverestProject> <OutputDir>\n");
        return 2;
    }

    try
    {
        const std::filesystem::path ContentDir = Arguments[1];
        const std::filesystem::path ProjectDir = Arguments[2];
        const std::filesystem::path OutputDir  = Arguments[3];
        const std::filesystem::path ScratchDir = OutputDir / "scratch";
        std::filesystem::create_directories(ScratchDir);

        ValidateCurvesAndPerformance();
        ValidateLevelSpacePersistence(ScratchDir);
        ValidateVehicleAssembly(ScratchDir);

        ImGui::CreateContext();
        ImGuiIO& Io    = ImGui::GetIO();
        Io.IniFilename = nullptr;
        Io.DeltaTime   = 1.0f / 60.0f;
        Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(ContentDir / "Icons");
        {
            ActivationOwnership Ownership(new ControlCentrePanel(ContentDir / "FontArchives",
                                                                 ScratchDir / "Personal.projectspace",
                                                                 ProjectDir / "Everest.projectspace"));
            auto& Centre = *static_cast<ControlCentrePanel*>(Ownership.get());
            if (!Centre.Initialize() || !Centre.Activate())
            {
                throw std::runtime_error("ControlCentrePanel failed to initialize");
            }

            InspectorFrameRasterizer Raster(Centre);
            for (int Warm = 0; Warm < 3; ++Warm) Raster.ConstructFrame([] {});

            VehicleConfiguration VehParams{};
            VehicleInspector     VehPage(VehParams);
            for (int Sec = 0; Sec < VehPage.QuerySectionCount(); ++Sec)
            {
                VehPage.ConfigureSection(Sec);
                Verify(Raster.CapturePng(VehPage, OutputDir / ("VehicleSection-" + std::to_string(Sec) + ".png")),
                       "Rendered native C++ VehicleInspector section " + std::to_string(Sec));
            }

            // The fixed capture dimensions place the selected torque knot at this pixel.
            VehPage.ConfigureSection(4);
            Raster.Height = 1835;
            const auto CurvePage = [&] { VehPage.ConstructInspector(); };
            Io.AddMousePosEvent(-100.0f, -100.0f);
            Raster.ConstructFrame(CurvePage);
            const float OriginalTorque = VehParams.EngineTorqueCurve[4].Y;
            Io.AddMousePosEvent(236.0f, 1310.0f);
            Raster.ConstructFrame(CurvePage);
            Io.AddMouseButtonEvent(0, true);
            Raster.ConstructFrame(CurvePage);
            Io.AddMousePosEvent(236.0f, 1322.0f);
            Raster.ConstructFrame(CurvePage);
            Io.AddMouseButtonEvent(0, false);
            Raster.ConstructFrame(CurvePage);
            Verify(VehParams.EngineTorqueCurve[4].Y < OriginalTorque - 20.0f,
                   "Pointer drag changes the selected torque control point");
            Io.AddMousePosEvent(-100.0f, -100.0f);

            VehParams.SelectedCurveProfile = 5;
            Verify(Raster.CapturePng(VehPage, OutputDir / "VehiclePowerCurve.png"),
                   "Rendered the derived power profile");
            VehParams.SelectedCurveProfile = 0;

            {
                SceneWorld EvidenceWorld;
                const RecordToken VehicleToken = EvidenceWorld.ConstructVehicleAssembly("GT-04");
                SceneDirectoryPanel DirectoryPanel;
                DirectoryPanel.Synchronize(SceneDirectory::Instance());
                DirectoryPanel.Select(VehicleToken);
                Verify(Raster.CaptureDirectory(DirectoryPanel, OutputDir / "VehicleOutliner.png"),
                       "Rendered the vehicle, tyre and rim entries in the native outliner");
            }

            // Capture Imperial mode + Live Telemetry Dock + TyreInspector
            VehParams.UnitDisplayImperial = true;
            VehPage.ConfigureSection(1);
            Verify(Raster.CapturePng(VehPage, OutputDir / "VehicleEngine-Imperial.png"),
                   "Rendered native C++ VehicleInspector Engine & Supercharger in Imperial units");

            VehParams.UnitDisplayImperial = false;
            VehParams.InspectorModeIndex  = 1;
            Verify(Raster.CapturePng(VehPage, OutputDir / "VehicleTelemetryDock.png"),
                   "Rendered native C++ VehicleInspector telemetry preview");

            TyreCornerConfiguration TyreParams{};
            TyreInspector           TyrePage(TyreParams);
            Verify(Raster.CapturePng(TyrePage, OutputDir / "TyreInspector-FL.png"),
                   "Rendered native C++ TyreInspector corner page");
            TyreParams.UnitDisplayImperial = true;
            Verify(Raster.CapturePng(TyrePage, OutputDir / "TyreInspector-Imperial.png"),
                   "Rendered native tyre inspector with imperial pressure, dimensions and temperatures");
        }
        ImGui::DestroyContext();

        std::printf("NativeVehicleInspectorValidation: %d passed, %d failed\n", PassedCount, FailedCount);
        return FailedCount == 0 ? 0 : 1;
    }
    catch (const std::exception& Error)
    {
        std::fprintf(stderr, "Exception: %s\n", Error.what());
        return 2;
    }
}
