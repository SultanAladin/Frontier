/*==========================================================================================================================================
                                                 NATIVEVEHICLEINSPECTORVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Headless C++ validation & CPU-rasterized PNG capture of VehicleRecord, TyreRecord, VehicleInspector, and TyreInspector.

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/IconPresentation.h"
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

    TyreCornerConfiguration& T = TyreRec->AccessConfiguration();
    T.CornerCode               = "FL";
    T.CompoundIndex            = 0;   // Soft
    T.ColdPressureKpa          = 192.0f;
    T.PuncturedCondition       = true;

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
        Verify(LV.VehicleName == "GT-04 Evo" && std::abs(LV.KerbMassKg - 1310.0f) < 1.0e-3f &&
                   LV.RaceTuneEnabled && LV.SuperchargerTypeIndex == 2 &&
                   std::abs(LV.FemYoungsModulusMpa - 3.4f) < 1.0e-3f &&
                   LV.OuterTickHz == 120 && LV.SubstepsPerOuterTick == 3 &&
                   !LV.Corners[2].EnclosedCondition,
               "Reloaded VehicleRecord preserves supercharger, 3D FEM, substepping, and pit-lane corner enclosure");
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
                                                                 ProjectDir));
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

            // Capture Imperial mode + Live Telemetry Dock + TyreInspector
            VehParams.UnitDisplayImperial = true;
            VehPage.ConfigureSection(1);
            Verify(Raster.CapturePng(VehPage, OutputDir / "VehicleEngine-Imperial.png"),
                   "Rendered native C++ VehicleInspector Engine & Supercharger in Imperial units");

            VehParams.UnitDisplayImperial = false;
            VehParams.InspectorModeIndex  = 1;
            Verify(Raster.CapturePng(VehPage, OutputDir / "VehicleTelemetryDock.png"),
                   "Rendered native C++ VehicleInspector Live Telemetry Dock");

            TyreCornerConfiguration TyreParams{};
            TyreInspector           TyrePage(TyreParams);
            Verify(Raster.CapturePng(TyrePage, OutputDir / "TyreInspector-FL.png"),
                   "Rendered native C++ TyreInspector corner page");
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
