/*==========================================================================================================================================
                                                     VEHICLEWORKSPACECAPTURE.CPP
==========================================================================================================================================*/
// 🧩 End-to-end Everest workspace + Vulkan/SwiftShader SceneViewport capture for VehiclePhysics:
//      1. Writes Default.WorldRegister.md, constructs Control vehicle + 4 enclosed Tyre records (FL/FR/RL/RR), and saves
//         ProvingGroundVehicle.levelspace + ProvingGroundVehicle.WorldRegister.md (leaving Default.levelspace untouched).
//      2. Renders 1920 × 1080 EverestWorkspace screenshots (Overview, Engine Curve Editor, Tyre Bay, TyreInspector, and
//         Live 240 Hz Telemetry while driving under W + A with Chase Camera) plus full-height Inspector section captures.
//      3. Renders real GPU SceneViewport frame sequences for VehicleRunningProvingGround.gif (launch → slalom → ABS brake)
//         and TyreDeformationAllThree.gif (close-up front-left tyre & contact patch for Spring, XPBD, and 3D Hex8 FEM).

#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/VehicleReadings.h"
#include "Engine/Scene/WorldRegister.h"
#include "EverestWorkspace.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "Frontier/Extensions/Physics/PhysicsExtension.h"
#include "Frontier/Extensions/Rendering/RenderingExtension.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <imgui.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

constexpr int      CaptureWidth  = 1920;
constexpr int      CaptureHeight = 1080;
constexpr uint64_t ViewportTag   = 0x5CE7E5u;

void Require(bool Condition, const std::string& Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

void AimFlightAt(Everest::SceneViewportFlight& Flight, const std::array<double, 3>& Eye, const std::array<double, 3>& Target, double FovRad = 0.88)
{
    Flight.Position            = Eye;
    const double Dx            = Target[0] - Eye[0];
    const double Dy            = Target[1] - Eye[1];
    const double Dz            = Target[2] - Eye[2];
    const double Horizontal    = std::max(1.0e-6, std::hypot(Dx, Dy));
    Flight.Heading             = std::atan2(Dy, Dx) * (180.0 / 3.14159265358979323846);
    Flight.Elevation           = std::atan2(Dz, Horizontal) * (180.0 / 3.14159265358979323846);
    Flight.PivotDistance       = std::hypot(Horizontal, Dz);
    Flight.VerticalFieldOfView = FovRad;
    Flight.Velocity            = { 0.0, 0.0, 0.0 };
}

void WriteRgbFlipOrDirect(const std::filesystem::path& Target, const std::vector<uint8_t>& Rgba, uint32_t Width, uint32_t Height)
{
    std::vector<uint8_t> Rgb(static_cast<size_t>(Width) * Height * 3u);
    for (size_t Pixel = 0; Pixel < static_cast<size_t>(Width) * Height; ++Pixel)
    {
        Rgb[Pixel * 3u + 0u] = Rgba[Pixel * 4u + 0u];
        Rgb[Pixel * 3u + 1u] = Rgba[Pixel * 4u + 1u];
        Rgb[Pixel * 3u + 2u] = Rgba[Pixel * 4u + 2u];
    }
    std::filesystem::create_directories(Target.parent_path());
    Require(stbi_write_png(Target.string().c_str(), static_cast<int>(Width), static_cast<int>(Height), 3, Rgb.data(), static_cast<int>(Width * 3u)) != 0,
            "Failed to write " + Target.string());
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;
    try
    {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        Require(ArgumentCount >= 5,
                "usage: VehicleWorkspaceCapture <EngineContent> <Projects/Everest> <ShaderRoot> <ExhibitDir>");
        const std::filesystem::path ContentDir  = std::filesystem::absolute(Arguments[1]);
        const std::filesystem::path EverestRoot = std::filesystem::absolute(Arguments[2]);
        const std::filesystem::path ShaderRoot  = std::filesystem::absolute(Arguments[3]);
        const std::filesystem::path ExhibitDir  = std::filesystem::absolute(Arguments[4]);
        const bool                  UiOnly      = ArgumentCount > 5 && std::string(Arguments[5]) == "--ui-only";
        const std::filesystem::path ResultsDir  = ExhibitDir / "Results";
        const std::filesystem::path FramesDir   = ExhibitDir / "Results" / "Frames";
        std::filesystem::create_directories(ResultsDir);
        std::filesystem::create_directories(FramesDir / "ProvingGround");
        std::filesystem::create_directories(FramesDir / "Carcass");

        ImGui::CreateContext();
        ImGuiIO& Io    = ImGui::GetIO();
        Io.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
        Io.DeltaTime   = 1.0f / 30.0f;
        Io.IniFilename = nullptr;
        Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(ContentDir / "Icons");

        const std::filesystem::path Preferences = ResultsDir / "Personal.projectspace";
        std::filesystem::remove(Preferences);
        ActivationOwnership Ownership(new ControlCentrePanel(ContentDir / "FontArchives", Preferences, EverestRoot / "Everest.projectspace"));
        auto*               Centre = static_cast<ControlCentrePanel*>(Ownership.get());
        Require(Centre->Initialize() && Centre->Activate(), "ControlCentrePanel initialization failed");

        Everest::EverestWorkspace    Workspace(*Centre, EverestRoot);
        Frontier::RenderingExtension Bridge(EverestRoot, ShaderRoot, nullptr);
        std::vector<uint8_t>         ViewportPixels;
        std::array<uint32_t, 2>      ViewportExtent = { 0, 0 };
        std::string                  LastStatus;

        Workspace.PresentSceneViewport = [&](const Frontier::SceneViewportRequest& Request) {
            const auto                           Start   = std::chrono::steady_clock::now();
            const Frontier::SceneViewportRefresh Outcome = Bridge.Render(Request, Workspace.QueryWorldSpace(), Workspace.QueryElapsedSeconds());
            Frontier::SceneViewportImage         Image;
            Image.Status  = Outcome.Status;
            Image.Readout = Outcome.Readout;
            LastStatus    = Outcome.Status;
            if (Outcome.View != VK_NULL_HANDLE && Bridge.AccessViewport()->ReadPresented(ViewportPixels))
            {
                ViewportExtent = Outcome.Extent;
                Image.Texture  = ViewportTag;
            }
            std::printf("  [Viewport] %u x %u in %.2f s — %s\n", Outcome.Extent[0], Outcome.Extent[1],
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count(), Outcome.Status.c_str());
            return Image;
        };

        // 1. Write Default.WorldRegister.md BEFORE adding the vehicle, keeping Default.levelspace untouched.
        const std::filesystem::path DefaultLevelPath = EverestRoot / "Content" / "Levels" / "Default.levelspace";
        {
            WorldRegister DefaultRegister;
            DefaultRegister.Survey(Workspace.QueryWorldSpace());
            Require(DefaultRegister.WriteMarkdown(WorldRegister::ResolveDocumentPath(DefaultLevelPath)),
                    "Failed to write Default.WorldRegister.md");
            std::printf("[VehicleWorkspaceCapture] Wrote %s (%zu entries)\n",
                        WorldRegister::ResolveDocumentPath(DefaultLevelPath).string().c_str(),
                        DefaultRegister.QueryEntries().size());
        }

        // 2. Construct Control vehicle via EverestWorkspace::ServiceConstructOrder (which auto-encloses 4 Tyre records!).
        AimFlightAt(Workspace.AccessViewportFlight(), { -46.2, -5.2, 2.35 }, { -40.0, 0.0, 0.0 }, 0.86);
        {
            Frontier::ConstructOrder Order;
            Order.Entry.Identifier = "Vehicle";
            Order.Entry.Caption    = "Vehicle";
            Order.Entry.Glyph      = "Car";
            Order.Entry.Group      = "Physics";
            Order.Entry.Shape      = false;
            Order.Name             = "Control vehicle";
            Order.Visible          = true;
            Workspace.ServiceConstructOrder(Order);
        }

        auto* VehicleEntry = dynamic_cast<VehicleRecord*>(Workspace.QueryWorldSpace().FindFirstByType("Vehicle"));
        Require(VehicleEntry != nullptr, "VehicleRecord was not constructed");
        const RecordToken VehicleToken = VehicleEntry->QueryToken();
        {
            VehicleConfiguration& Authored                    = VehicleEntry->AccessConfiguration();
            Authored.Spatial.PositionMetres[0]                = -40.0f;
            Authored.Spatial.PositionMetres[1]                = 0.0f;
            Authored.Spatial.PositionMetres[2]                = 0.0f;
            Authored.Spatial.OrientationDegrees[0]            = 0.0f;
            Authored.Spatial.OrientationDegrees[1]            = 0.0f;
            Authored.Spatial.OrientationDegrees[2]            = 0.0f;
            Authored.Possessed                                = true;
            Authored.ChaseCamera                              = true;
            Authored.ThreadedPhysics                          = false; // deterministic inline stepping during offscreen capture
            Authored.PhysicsStepHz                            = 240.0f;
            Authored.CollisionSteps                           = 1;
            Authored.SolverSubsteps                           = 1;
            Authored.Specification.Solver.Carcass             = Vehicle::TyreCarcassCategory::Fem;
            Authored.Specification.Solver.Induction           = Vehicle::InductionType::Supercharged;
            Authored.Specification.Solver.TyreThermal.Enabled = true;
            Authored.Specification.Solver.TyreWear.Enabled    = true;
            Authored.Specification.Solver.Fuel.Enabled        = true;
            Authored.Specification.Solver.Fluids.Enabled      = true;
            Authored.Specification.Solver.ChargeAir.Enabled   = true;
            Authored.Specification.Solver.TractionControl.Enabled = true;
            Authored.AdvanceRevision();
        }
        const auto TyreRecords = Workspace.QueryWorldSpace().FindAllByType("Tyre");
        Require(TyreRecords.size() == 4u, "Expected 4 auto-enclosed Tyre records, got " + std::to_string(TyreRecords.size()));

        // Save ProvingGroundVehicle.levelspace + ProvingGroundVehicle.WorldRegister.md (withThreadedPhysics = true on disk!).
        const std::filesystem::path VehicleLevelPath = EverestRoot / "Content" / "Levels" / "ProvingGroundVehicle.levelspace";
        {
            VehicleEntry->AccessConfiguration().ThreadedPhysics = true;
            std::string SaveDiag;
            Require(Workspace.QueryWorldSpace().SaveLevelSpace(VehicleLevelPath, &SaveDiag),
                    "Failed to save ProvingGroundVehicle.levelspace: " + SaveDiag);
            PhysicsExtension         SurveyPhysics;
            PhysicsExtensionSettings SurveySettings;
            SurveySettings.Threaded      = false;
            SurveySettings.WriteRegister = false;
            SurveyPhysics.Begin(Workspace.QueryWorldSpace(), SurveySettings);
            Require(SurveyPhysics.QueryRegister().WriteMarkdown(WorldRegister::ResolveDocumentPath(VehicleLevelPath)),
                    "Failed to write ProvingGroundVehicle.WorldRegister.md");
            SurveyPhysics.End(Workspace.QueryWorldSpace());
            VehicleEntry->AccessConfiguration().ThreadedPhysics = false;
            std::printf("[VehicleWorkspaceCapture] Saved %s + WorldRegister.md\n", VehicleLevelPath.string().c_str());
        }

        // Helper to rasterize full 1920x1080 EverestWorkspace window.
        const auto WriteWorkspaceCapture = [&](const std::filesystem::path& Target) {
            const ImDrawData*          DrawData = ImGui::GetDrawData();
            std::vector<unsigned char> Pixels(static_cast<size_t>(CaptureWidth) * CaptureHeight * 3u, 14u);
            const std::function<FrontierExhibit::Image(ImTextureID)> Resolve = [&](ImTextureID Texture) {
                return Texture == ViewportTag
                           ? FrontierExhibit::Image{ ViewportPixels.data(), static_cast<int>(ViewportExtent[0]),
                                                     static_cast<int>(ViewportExtent[1]), 4 }
                           : FrontierExhibit::Image{};
            };
            for (const ImDrawList* List : DrawData->CmdLists)
                FrontierExhibit::RasterizeCommands(List, Pixels.data(), CaptureWidth, CaptureHeight, DrawData->DisplayPos,
                                                   DrawData->FramebufferScale, nullptr, &Resolve);
            Require(stbi_write_png(Target.string().c_str(), CaptureWidth, CaptureHeight, 3, Pixels.data(), CaptureWidth * 3) != 0,
                    "PNG write failed: " + Target.string());
            std::printf("[VehicleWorkspaceCapture] Wrote %s\n", Target.string().c_str());
        };

        const auto PumpWorkspace = [&](int Ticks, double Dt = 1.0 / 30.0) {
            Io.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
            Io.DeltaTime   = static_cast<float>(Dt);
            for (int Tick = 0; Tick < Ticks; ++Tick)
            {
                Centre->PrepareInterface();
                ImGui::NewFrame();
                (void)Centre->Integrate(Dt);
                (void)Workspace.ConfigurePlatform();
                Workspace.ConstructInterface(Dt);
                Centre->ConstructInterface();
                ImGui::Render();
                FrontierExhibit::AcknowledgeTextures();
            }
        };

        // 3. Capture EverestWorkspace with ReSTIR Global Illumination on the Proving Ground start line!
        Workspace.AccessViewportOverlays().ReadoutVisible = true;
        Workspace.AccessViewportNavigation().Mode         = Everest::SceneCameraMode::Free;
        AimFlightAt(Workspace.AccessViewportFlight(), { -45.8, -4.9, 2.15 }, { -39.6, 0.0, 0.72 }, 0.84);
        Workspace.QueryOutliner().Select(VehicleToken);

        RecordInspector* VehiclePage = InspectorRuntime::Instance().FindInspector(VehicleToken);
        Require(VehiclePage != nullptr, "VehicleRecord has no bound inspector");

        VehiclePage->ConfigureSection(0); // Overview
        PumpWorkspace(4);
        Require(!ViewportPixels.empty(), "SceneViewport produced no pixels: " + LastStatus);
        WriteWorkspaceCapture(ResultsDir / "EverestVehicleOverview.png");

        auto* PipelineRec = dynamic_cast<RenderPipelineRecord*>(Workspace.QueryWorldSpace().FindFirstByType("RenderPipeline"));
        Require(PipelineRec != nullptr, "No RenderPipelineRecord");
        PipelineRec->AccessConfiguration().LiveRefresh = false;
        PipelineRec->AccessConfiguration().AdvanceRevision();

        VehiclePage->ConfigureSection(2); // Engine & Interactive Curve Editor
        PumpWorkspace(3);
        WriteWorkspaceCapture(ResultsDir / "EverestVehicleEngineCurves.png");

        // Filter Outliner to "Control vehicle" so the enclosing Vehicle + its 4 child Tyre records (FL/FR/RL/RR) are prominently shown!
        Workspace.QueryOutliner().ConfigureTextFilter("Control vehicle");
        VehiclePage->ConfigureSection(4); // Tyres & 4-Corner Bay
        PumpWorkspace(3);
        WriteWorkspaceCapture(ResultsDir / "EverestVehicleTyreBay.png");

        // Select Front-Left TyreRecord to capture TyreInspector!
        const RecordToken FirstTyreToken = TyreRecords.front()->QueryToken();
        Workspace.QueryOutliner().Select(FirstTyreToken);
        PumpWorkspace(3);
        WriteWorkspaceCapture(ResultsDir / "EverestTyreInspector.png");
        Workspace.QueryOutliner().ConfigureTextFilter("");

        // 4. Re-enable LiveRefresh + AnalyticIllumination, start LIVE Simulation, hold W + slight A steer for 2.4 s,
        //    and capture EverestVehicleTelemetryLive.png!
        Workspace.QueryOutliner().Select(VehicleToken);
        VehiclePage->ConfigureSection(8); // Live Telemetry Dock
        PipelineRec->AccessConfiguration().LiveRefresh          = true;
        PipelineRec->AccessConfiguration().AnalyticIllumination = true;
        PipelineRec->AccessConfiguration().AdvanceRevision();

        Workspace.ToggleSimulation();
        Require(Workspace.AccessPhysics() != nullptr && Workspace.AccessPhysics()->QueryRunning(), "Physics failed to start");

        Io.AddKeyEvent(ImGuiKey_W, true);
        for (int StepTick = 0; StepTick < 72; ++StepTick)
        {
            Io.AddKeyEvent(ImGuiKey_A, StepTick >= 32 && StepTick < 58);
            PumpWorkspace(1, 1.0 / 30.0);
        }
        Io.AddKeyEvent(ImGuiKey_W, false);
        Io.AddKeyEvent(ImGuiKey_A, false);
        WriteWorkspaceCapture(ResultsDir / "EverestVehicleTelemetryLive.png");

        // Capture full-height unclipped Inspector sections (0 Overview, 2 Engine Curves, 4 Tyre Bay, 8 Live Telemetry)
        // WHILE simulation is still live so Section 8 shows live telemetry!
        {
            constexpr int ColumnWidth = 480, ColumnHeight = 2800;
            for (const int Section : { 0, 2, 4, 8 })
            {
                VehiclePage->ConfigureSection(Section);
                for (int Settle = 0; Settle < 3; ++Settle)
                {
                    Io.DisplaySize = ImVec2(static_cast<float>(ColumnWidth), static_cast<float>(ColumnHeight));
                    Centre->PrepareInterface();
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(ColumnWidth), static_cast<float>(ColumnHeight)));
                    ImGui::Begin("##VehicleSectionCapture", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
                    bool DeleteRequested = false;
                    InspectorRuntime::Instance().InspectRecord(VehicleToken, DeleteRequested);
                    ImGui::End();
                    ImGui::Render();
                    FrontierExhibit::AcknowledgeTextures();
                }
                const ImDrawData*          DrawData = ImGui::GetDrawData();
                std::vector<unsigned char> Pixels(static_cast<size_t>(ColumnWidth) * ColumnHeight * 3u, 14u);
                for (const ImDrawList* List : DrawData->CmdLists)
                    FrontierExhibit::RasterizeCommands(List, Pixels.data(), ColumnWidth, ColumnHeight, DrawData->DisplayPos,
                                                       DrawData->FramebufferScale, nullptr, nullptr);
                const int  Reference  = ColumnHeight - 12;
                const auto RowDiffers = [&](int Row) {
                    for (int Column = 24 * 3; Column < (ColumnWidth - 24) * 3; ++Column)
                        if (std::abs(int(Pixels[static_cast<size_t>(Row) * ColumnWidth * 3 + Column]) -
                                     int(Pixels[static_cast<size_t>(Reference) * ColumnWidth * 3 + Column])) > 6)
                            return true;
                    return false;
                };
                int Used = Reference;
                while (Used > 0 && !RowDiffers(Used)) --Used;
                Used = std::min(ColumnHeight, std::max(400, Used + 24));
                const std::filesystem::path SectionPath = ResultsDir / ("VehicleInspector-Section-" + std::to_string(Section) + ".png");
                Require(stbi_write_png(SectionPath.string().c_str(), ColumnWidth, Used, 3, Pixels.data(), ColumnWidth * 3) != 0,
                        "Section PNG write failed");
                std::printf("[VehicleWorkspaceCapture] Wrote %s (%d px tall)\n", SectionPath.string().c_str(), Used);
            }
        }

        // Stop workspace simulation cleanly so records return to authored start pose.
        Workspace.ToggleSimulation();

        if (UiOnly)
        {
            Workspace.PresentSceneViewport = nullptr;
            Ownership.reset();
            FrontierExhibit::AcknowledgeTextures();
            ImGui::GetIO().BackendFlags = 0;
            ImGui::DestroyContext();
            std::printf("[VehicleWorkspaceCapture] COMPLETE (--ui-only).\n");
            return 0;
        }

        // 5. Render Sequence A: 32 GPU frames (960 x 540) of Control vehicle driving Proving Ground
        //    (hard launch → sweeping S-bend cornering → threshold ABS braking) on 3D Hex8 FEM tyres!
        std::printf("[VehicleWorkspaceCapture] Rendering Proving Ground driving sequence (32 frames)...\n");
        {
            VehicleEntry->AccessConfiguration().Specification.Solver.Carcass = Vehicle::TyreCarcassCategory::Fem;
            VehicleEntry->AccessConfiguration().AdvanceRevision();

            PhysicsExtension         DrivePhysics;
            PhysicsExtensionSettings Settings;
            Settings.Threaded      = false;
            Settings.StepHz        = 240.0f;
            Settings.WriteRegister = false;
            DrivePhysics.Begin(Workspace.QueryWorldSpace(), Settings);

            // Settle suspension for 0.6 s before launch
            for (int Warm = 0; Warm < 18; ++Warm) (void)DrivePhysics.Step(Workspace.QueryWorldSpace(), 1.0 / 30.0);

            constexpr int TotalFrames = 32;
            for (int FrameIdx = 0; FrameIdx < TotalFrames; ++FrameIdx)
            {
                const float TimeSec = static_cast<float>(FrameIdx) * 0.20f;
                PhysicsVehicleInput Command{};
                if (TimeSec < 2.2f)
                {
                    Command.Throttle = 1.0f;
                    Command.Steer    = 0.0f;
                }
                else if (TimeSec < 3.4f)
                {
                    Command.Throttle = 0.80f;
                    Command.Steer    = 0.14f; // gentle left weave inside main straight kerbs
                }
                else if (TimeSec < 4.8f)
                {
                    Command.Throttle = 0.78f;
                    Command.Steer    = -0.18f; // counter-weave back to centreline
                }
                else
                {
                    Command.Throttle = 0.0f;
                    Command.Brake    = 0.92f; // hard ABS stop
                    Command.Steer    = 0.06f;
                }
                DrivePhysics.AssignVehicleInput(Command);
                for (int Sub = 0; Sub < 6; ++Sub) (void)DrivePhysics.Step(Workspace.QueryWorldSpace(), 1.0 / 30.0);

                VehicleReading Reading{};
                Require(QueryVehicleReading(VehicleToken, Reading), "Missing live VehicleReading in sequence A");
                const auto& Pos = Reading.Telemetry.Chassis.Position;
                const auto& Rot = Reading.Telemetry.Chassis.Orientation;
                const auto  Fwd = Rot.Rotate({ 1.0f, 0.0f, 0.0f });
                const auto  Lft = Rot.Rotate({ 0.0f, 1.0f, 0.0f });

                // Dynamic three-quarter tracking camera sweeping around the car so wheels, cabin steering & chassis pitch/roll shine
                const double SweepAngle = -0.55 + 0.35 * std::sin(static_cast<double>(FrameIdx) * 0.16);
                const double CamDist    = 6.3;
                SceneViewportRequest Req{};
                Req.PanelWidth          = 960.0f;
                Req.PanelHeight         = 540.0f;
                Req.DeltaSeconds        = 0.20f;
                Req.VerticalFieldOfView = 0.80f;
                Req.CameraUp            = { 0.0f, 0.0f, 1.0f };
                Req.CameraPosition      = {
                    static_cast<float>(Pos.x - Fwd.x * CamDist * std::cos(SweepAngle) + Lft.x * CamDist * std::sin(SweepAngle)),
                    static_cast<float>(Pos.y - Fwd.y * CamDist * std::cos(SweepAngle) + Lft.y * CamDist * std::sin(SweepAngle)),
                    static_cast<float>(Pos.z + 1.85) };
                Req.CameraTarget        = { Pos.x + Fwd.x * 0.65f, Pos.y + Fwd.y * 0.65f, Pos.z + 0.32f };

                const auto Outcome = Bridge.Render(Req, Workspace.QueryWorldSpace(), static_cast<double>(FrameIdx) * 0.20);
                std::vector<uint8_t> Rgba;
                Require(Outcome.View != VK_NULL_HANDLE && Bridge.AccessViewport()->ReadPresented(Rgba), "ReadPresented failed in sequence A");
                char NameBuf[64];
                std::snprintf(NameBuf, sizeof(NameBuf), "frame_%02d.png", FrameIdx);
                WriteRgbFlipOrDirect(FramesDir / "ProvingGround" / NameBuf, Rgba, Outcome.Extent[0], Outcome.Extent[1]);
            }
            DrivePhysics.End(Workspace.QueryWorldSpace());
        }

        // 6. Render Sequence B: Close-up Front-Left Tyre & Contact Patch across ALL THREE Carcass Formulations
        //    (Spring, XPBD, 3D FEM) under vertical drop bounce + heavy cornering load + low-pressure soft bulge!
        std::printf("[VehicleWorkspaceCapture] Rendering all 3 carcass close-up sequences (Spring, XPBD, 3D FEM)...\n");
        static constexpr const char* CarcassTags[3] = { "Spring", "XPBD", "FEM" };
        for (int CategoryIdx = 0; CategoryIdx < 3; ++CategoryIdx)
        {
            VehicleConfiguration& Authored                    = VehicleEntry->AccessConfiguration();
            // Start slightly elevated (+0.045 m) with 70 kPa front cold pressure so the drop + cornering produces
            // unmistakable, physically authentic contact-patch flattening & sidewall bulge in the close-up camera!
            Authored.Spatial.PositionMetres[0]                = -40.0f;
            Authored.Spatial.PositionMetres[1]                = 0.0f;
            Authored.Spatial.PositionMetres[2]                = 0.045f;
            Authored.Spatial.OrientationDegrees[0]            = 0.0f;
            Authored.Spatial.OrientationDegrees[1]            = 0.0f;
            Authored.Spatial.OrientationDegrees[2]            = 0.0f;
            Authored.Specification.Solver.Carcass             = static_cast<Vehicle::TyreCarcassCategory>(CategoryIdx);
            Authored.Specification.Solver.Tyre.InflationPressure = 70000.0f;
            Authored.AdvanceRevision();
            for (const SceneRecordBase* TRec : Workspace.QueryWorldSpace().FindAllByType("Tyre"))
            {
                if (auto* MutableTyre = dynamic_cast<TyreRecord*>(Workspace.QueryWorldSpace().FindRecord(TRec->QueryToken())))
                {
                    MutableTyre->AccessConfiguration().ColdPressure = 70000.0f;
                    MutableTyre->AccessConfiguration().AdvanceRevision();
                }
            }

            PhysicsExtension         TyrePhysics;
            PhysicsExtensionSettings Settings;
            Settings.Threaded      = false;
            Settings.StepHz        = 240.0f;
            Settings.WriteRegister = false;
            TyrePhysics.Begin(Workspace.QueryWorldSpace(), Settings);

            constexpr int TyreFrames = 16;
            for (int FrameIdx = 0; FrameIdx < TyreFrames; ++FrameIdx)
            {
                PhysicsVehicleInput Command{};
                if (FrameIdx >= 5 && FrameIdx < 12)
                {
                    Command.Throttle = 0.72f;
                    Command.Steer    = -0.38f; // steer right -> loads Front-Left (outside) tyre heavily + lateral shear!
                }
                else if (FrameIdx >= 12)
                {
                    Command.Brake = 0.85f; // heavy front axle weight transfer onto FL tyre!
                    Command.Steer = -0.20f;
                }
                TyrePhysics.AssignVehicleInput(Command);
                for (int Sub = 0; Sub < 4; ++Sub) (void)TyrePhysics.Step(Workspace.QueryWorldSpace(), 1.0 / 30.0);

                VehicleReading Reading{};
                Require(QueryVehicleReading(VehicleToken, Reading), "Missing VehicleReading in sequence B");
                const auto& FlWheel = Reading.Telemetry.Wheels[0];
                const auto& Rot     = Reading.Telemetry.Chassis.Orientation;
                const auto  Fwd     = Rot.Rotate({ 1.0f, 0.0f, 0.0f });
                const auto  Lft     = Rot.Rotate({ 0.0f, 1.0f, 0.0f });

                // Low close-up camera aimed straight at the Front-Left wheel's outer sidewall, rim & road contact patch!
                SceneViewportRequest Req{};
                Req.PanelWidth          = 560.0f;
                Req.PanelHeight         = 420.0f;
                Req.DeltaSeconds        = 0.133f;
                Req.VerticalFieldOfView = 0.68f;
                Req.CameraUp            = { 0.0f, 0.0f, 1.0f };
                Req.CameraPosition      = {
                    FlWheel.HubPosition.x + Fwd.x * 1.15f + Lft.x * 1.68f,
                    FlWheel.HubPosition.y + Fwd.y * 1.15f + Lft.y * 1.68f,
                    0.44f };
                Req.CameraTarget        = {
                    FlWheel.HubPosition.x - Fwd.x * 0.04f,
                    FlWheel.HubPosition.y,
                    0.30f };

                const auto Outcome = Bridge.Render(Req, Workspace.QueryWorldSpace(), static_cast<double>(FrameIdx) * 0.133);
                std::vector<uint8_t> Rgba;
                Require(Outcome.View != VK_NULL_HANDLE && Bridge.AccessViewport()->ReadPresented(Rgba), "ReadPresented failed in sequence B");
                char NameBuf[64];
                std::snprintf(NameBuf, sizeof(NameBuf), "%s_%02d.png", CarcassTags[CategoryIdx], FrameIdx);
                WriteRgbFlipOrDirect(FramesDir / "Carcass" / NameBuf, Rgba, Outcome.Extent[0], Outcome.Extent[1]);
            }
            TyrePhysics.End(Workspace.QueryWorldSpace());
        }

        Workspace.PresentSceneViewport = nullptr;
        Ownership.reset();
        FrontierExhibit::AcknowledgeTextures();
        ImGui::GetIO().BackendFlags = 0;
        ImGui::DestroyContext();
        std::printf("[VehicleWorkspaceCapture] COMPLETE.\n");
        return 0;
    }
    catch (const std::exception& Failure)
    {
        std::fprintf(stderr, "[VehicleWorkspaceCapture] FAILED: %s\n", Failure.what());
        return 1;
    }
}
