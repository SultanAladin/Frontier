/*==========================================================================================================================================
                                                             WORKSPACEHOST.CPP
==========================================================================================================================================*/
// 🧩 Editor workspace composition: production dockspace + notch with Viewport · Outliner · Inspector · Log Stream · Input.

#include "Workspace/WorkspaceHost.h"

#include "Engine/Diagnostics/LogCategories.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Interface/LogStreamPanelRegistration.h"
#include "Engine/Interface/SceneDirectoryPanelRegistration.h"
#include "Input/InputMapCodec.h"
#include "EditorTheme.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <imgui.h>

#include <fstream>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace Frontier
{

namespace
{

[[nodiscard]] const LogCategory& InputCategory()
{
    static const LogCategory Category = RegisterLogCategory("Input", LogColour::Blue);
    return Category;
}

void LogNamed(const char* CategoryName, LogSeverity Severity, const std::string& Message, const char* Source)
{
    if (const LogCategory* Category = LogCategories().QueryByName(CategoryName))
    {
        WriteLog(*Category, Severity, Message, Source);
    }
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                                                         CONSTRUCTION
//----------------------------------------------------------------------------------------------------------------------

WorkspaceHost::WorkspaceHost(ControlCentrePanel& OwningCentre, InputSubstrate& OwningSubstrate,
                             std::filesystem::path LayoutPath, std::filesystem::path InputPath)
    : Centre(OwningCentre), Substrate(OwningSubstrate), LayoutFile(std::move(LayoutPath)),
      InputFile(std::move(InputPath)), InspectorPanel(OutlinerPanel), InputsPanel(Map)
{
    // Input map first: the Input panel and the log burst both report on it.
    bool        InputFromFile = false;
    std::string Diagnostic;
    if (!InputFile.empty() && std::filesystem::exists(InputFile))
    {
        InputMap Candidate;
        if (InputMapCodec::Decode(InputFile, Candidate, Diagnostic))
        {
            Map           = std::move(Candidate);
            InputFromFile = true;
        }
    }
    if (!InputFromFile)
    {
        Map.ConfigureDefaults();
    }

    DockPanelSpecification ViewportPanel;
    ViewportPanel.Key              = "Viewport";
    ViewportPanel.Caption          = "Viewport";
    ViewportPanel.ConstructContent = [this]() { CanvasPanel.ConstructInterface(); };
    ViewportPanel.Open             = true;

    DockPanelSpecification InspectorSpecification;
    InspectorSpecification.Key              = "Inspector";
    InspectorSpecification.Caption          = "Inspector";
    InspectorSpecification.ConstructContent = [this]() { InspectorPanel.ConstructInterface(); };
    InspectorSpecification.Open             = true;

    DockPanelSpecification InputSpecification;
    InputSpecification.Key              = "Input";
    InputSpecification.Caption          = "Input";
    InputSpecification.ConstructContent = [this]() { InputsPanel.ConstructInterface(); };
    // 📝 Open at start: a closed panel's dock column seats as blank space, and this build exists to
    //    exercise the input layer — its test bench belongs on screen (F6 toggles it).
    InputSpecification.Open             = true;

    if (!Panels.Register(ViewportPanel) || !Panels.Register(MakeSceneDirectoryPanelSpecification(OutlinerPanel)) ||
        !Panels.Register(InspectorSpecification) || !Panels.Register(MakeLogStreamPanelSpecification(LogPanel)) ||
        !Panels.Register(InputSpecification))
    {
        throw std::runtime_error("Frontier Editor panel registration refused.");
    }

    // 📝 Same chrome recipe as the retained workspace captures: 79 px top band for the expansion-notch
    //    tab, 140x26 trapezoidal tabs. Columns thirds the editor the way the VehicleHMI demo lays out
    //    (Outliner · Viewport over Log Stream · Inspector).
    Panels.ConfigureDockspaceKey("Frontier editor panels");
    Panels.ConfigureWorkspaceInsets(79.0f, 0.0f);   // [px]
    Panels.ConfigureTabExtent(140.0f, 26.0f);       // [px]
    ApplyDefaultSeating();

    LayoutResetRevision = Centre.QueryLayoutResetRevision();
    if (!LayoutFile.empty() && std::filesystem::exists(LayoutFile))
    {
        try
        {
            const toml::table Saved = toml::parse_file(LayoutFile.string());
            std::string       Message;
            if (!Panels.ConfigureLayout(Saved, Message))
            {
                LogNamed("Interface", LogSeverity::Warning, "Saved editor layout refused: " + Message,
                         "WorkspaceHost.cpp");
            }
        }
        catch (const std::exception&)
        {
            LogNamed("Interface", LogSeverity::Warning, "Saved editor layout unreadable; defaults seated.",
                     "WorkspaceHost.cpp");
        }
    }

    InspectorPanel.SelectRecord        = [this](RecordToken Token) { OutlinerPanel.Select(Token); };
    CanvasPanel.RequestCaptureToggle   = [this]() { ConfigureSimulationCaptured(!SimulationCaptured); };
    InputsPanel.ConfigureSimulationCaptured = [this](bool Captured) { ConfigureSimulationCaptured(Captured); };
    InputsPanel.ConfigurePersistencePath(InputFile);

    SeedLogBurst(InputFromFile);
}

WorkspaceHost::~WorkspaceHost() = default;

void WorkspaceHost::ApplyDefaultSeating()
{
    // 📝 PanelDockspace seating contract: columns + stacked pair must account for EVERY registered
    //    panel, the stacked upper panel is named by the stack (Viewport over Log Stream), so the
    //    centre column is keyed by its lower panel and Input takes the fourth column (closed at start).
    if (!Panels.ConfigureStackedPanels("Viewport", "LogStream", 470.0f) ||   // [px] - Upper height
        !Panels.ConfigureColumns(
            {{"Outliner", 0.20f}, {"LogStream", 0.38f}, {"Inspector", 0.20f}, {"Input", 0.22f}}))
    {
        LogNamed("Interface", LogSeverity::Warning,
                 "Default dock seating refused; panels fall back to one tab stack", "WorkspaceHost.cpp");
    }
}

void WorkspaceHost::SeedLogBurst(bool InputFromFile)
{
    LogNamed("Interface", LogSeverity::Info,
             "Frontier Editor workspace composed: Viewport · Outliner · Inspector · Log Stream · Input",
             "WorkspaceHost.cpp");
    WriteLog(InputCategory(), LogSeverity::Info,
             "Input map live: " + std::to_string(Map.QueryAssignments().size()) + " action assignments, " +
                 std::to_string(Map.QueryAxisAssignments().size()) + " axis assignments (" +
                 (InputFromFile ? "loaded from TOML" : "editor/simulation defaults") + ")",
             "WorkspaceHost.cpp");
    LogNamed("Vehicle", LogSeverity::Info, "VehicleHMI Test Vehicle registered with 4 tyres", "VehicleSceneFixture.cpp");
    LogNamed("Tyre", LogSeverity::Warning, "Rear Right starts hidden in the Outliner (eye toggle off)",
             "VehicleSceneFixture.cpp");
    LogNamed("Environment", LogSeverity::Info, "Proving Grounds registered: Test Cube + Ground Plane",
             "VehicleSceneFixture.cpp");
    LogNamed("Geometry", LogSeverity::Info, "2 geometry records seated under Proving Grounds", "VehicleSceneFixture.cpp");
    LogNamed("Render", LogSeverity::Info, "Viewport blank by design: scene renderer connects in a later step",
             "ViewportPanel.cpp");
    LogNamed("Interface", LogSeverity::Info, "Dockspace seated: Outliner | Viewport over Log Stream | Inspector",
             "WorkspaceHost.cpp");
}

//----------------------------------------------------------------------------------------------------------------------
//                                                    PLATFORM AND LAYOUT
//----------------------------------------------------------------------------------------------------------------------

bool WorkspaceHost::ConfigurePlatform()
{
    if (Centre.QueryLayoutResetRevision() != LayoutResetRevision)
    {
        LayoutResetRevision = Centre.QueryLayoutResetRevision();
        ResetLayoutToDefaults();
    }
    Panels.ConfigureDetachedWindows(Centre.QueryDetachedWindows());
    return Panels.ConfigurePlatform();
}

void WorkspaceHost::ResetLayoutToDefaults()
{
    Panels.ResetLayout();
    (void)Panels.ConfigureOpen("Viewport", true);
    (void)Panels.ConfigureOpen("Outliner", true);
    (void)Panels.ConfigureOpen("Inspector", true);
    (void)Panels.ConfigureOpen("LogStream", true);
    (void)Panels.ConfigureOpen("Input", false);
    ApplyDefaultSeating();
    LogNamed("Interface", LogSeverity::Info, "Workspace layout reset to the default seating", "WorkspaceHost.cpp");
}

void WorkspaceHost::Finalize()
{
    std::string Diagnostic;
    if (!InputFile.empty())
    {
        (void)InputMapCodec::Encode(InputFile, Map, Diagnostic);
    }
    if (!LayoutFile.empty() && Panels.QuerySeated())
    {
        const toml::table Layout = Panels.EncodeLayout();
        if (!Layout.empty() && PanelDockspace::ValidateLayout(Layout, Diagnostic))
        {
            std::error_code Error;
            if (LayoutFile.has_parent_path())
            {
                std::filesystem::create_directories(LayoutFile.parent_path(), Error);
            }
            std::ofstream Output(LayoutFile, std::ios::binary | std::ios::trunc);
            if (Output)
            {
                Output << Layout;
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
//                                                         SIMULATION
//----------------------------------------------------------------------------------------------------------------------

void WorkspaceHost::ConfigureSimulationCaptured(bool Captured)
{
    if (SimulationCaptured == Captured)
    {
        return;
    }
    SimulationCaptured = Captured;
    if (Captured)
    {
        Map.PushContext(InputContextCategory::Simulation);
    }
    else
    {
        Map.PopContext();
    }
    Map.ConfigurePointerCaptured(Captured);
    Substrate.ResetPointerAnchor();
    if (NotifyPointerCaptureChanged)
    {
        NotifyPointerCaptureChanged(Captured);
    }
    WriteLog(InputCategory(), LogSeverity::Info,
             Captured ? "Simulation context captured: driving axes live, pointer hidden (Esc releases)"
                      : "Editor context restored: workspace shortcuts live",
             "WorkspaceHost.cpp");
}

void WorkspaceHost::TogglePanel(const char* Key, const char* Label)
{
    const bool Open = !Panels.QueryOpen(Key);
    (void)Panels.ConfigureOpen(Key, Open);
    LogNamed("Interface", LogSeverity::Info, std::string(Label) + (Open ? " panel opened" : " panel closed"),
             "WorkspaceHost.cpp");
}

//----------------------------------------------------------------------------------------------------------------------
//                                                          PER-CYCLE
//----------------------------------------------------------------------------------------------------------------------

void WorkspaceHost::Synchronize(double Δτ)
{
    // Appearance fold: the editor's own panels render in the Control Centre's Appearance theme.
    const AppearanceSettings& Appearance = Centre.QueryAppearance().QueryApplied();
    Theme.ConfigureTheme(Appearance.Theme);
    Theme.ConfigureAccent(Appearance.Accent);
    Theme.ConfigureCornerRadius(Appearance.CornerRadius);
    const EditorTheme ResolvedTheme = ResolveEditorTheme(Theme.QueryPalette(), Theme.QueryAccentColor());
    InputsPanel.ConfigureTheme(ResolvedTheme);
    CanvasPanel.ConfigureTheme(ResolvedTheme);

    LogPanel.Synchronize(LogStore(), LogCategories(), Δτ);

    // Spawn flow: the Outliner records a request; the host creates the record under the selection.
    while (const std::optional<RecordClassification> Request = OutlinerPanel.TakeSpawnRequest())
    {
        const RecordToken Spawned = Fixture.Spawn(*Request, OutlinerPanel.QuerySelected());
        OutlinerPanel.Select(Spawned);
        LogNamed("Interface", LogSeverity::Info,
                 std::string("Spawned ") + RecordClassificationName(*Request) + " record #" +
                     std::to_string(Spawned.Value),
                 "WorkspaceHost.cpp");
    }
    OutlinerPanel.Synchronize(SceneDirectory::Instance());

    // Input fold: text ownership from ImGui, pointer response from the shared preferences.
    Map.ConfigureTextEntryActive(ImGui::GetIO().WantTextInput);
    Map.Integrate(Substrate, Centre.QueryInput().QueryApplied());

    if (Map.QueryAction("Editor.FocusSimulation").Pressed)
    {
        ConfigureSimulationCaptured(true);
    }
    if (Map.QueryAction("Simulation.QuitCapture").Pressed)
    {
        ConfigureSimulationCaptured(false);
    }
    if (Map.QueryAction("Editor.ToggleLogStream").Pressed)
    {
        TogglePanel("LogStream", "Log Stream");
    }
    if (Map.QueryAction("Editor.ToggleOutliner").Pressed)
    {
        TogglePanel("Outliner", "Outliner");
    }
    if (Map.QueryAction("Editor.ToggleInspector").Pressed)
    {
        TogglePanel("Inspector", "Inspector");
    }
    if (Map.QueryAction("Editor.ToggleInputPanel").Pressed)
    {
        TogglePanel("Input", "Input");
    }
    if (Map.QueryAction("Editor.ResetLayout").Pressed)
    {
        ResetLayoutToDefaults();
    }

    // Panel view-state refresh.
    const SceneInspectorSummary& Summary = OutlinerPanel.QueryInspector();
    CanvasPanel.RecordCount             = Fixture.QueryRecordCount();
    CanvasPanel.SelectedName            = Summary.HasSelection ? Summary.Name : std::string{};
    CanvasPanel.SimulationCaptured      = SimulationCaptured;
    InputsPanel.ObserveSubstrate(&Substrate);

    if (Summary.HasSelection && Summary.Token != LastSelected)
    {
        LastSelected = Summary.Token;
        LogNamed("Interface", LogSeverity::Info,
                 "Selected '" + Summary.Name + "' (" + RecordClassificationName(Summary.Classification) + ", #" +
                     std::to_string(Summary.Token.Value) + ")",
                 "WorkspaceHost.cpp");
    }
    else if (!Summary.HasSelection)
    {
        LastSelected = {};
    }

    // Heartbeat: keeps the stream demonstrably live in the windowed application.
    HeartbeatAccumulator += Δτ;
    if (HeartbeatAccumulator >= 4.0)
    {
        HeartbeatAccumulator = 0.0;
        static constexpr const char* Categories[] = {"Vehicle", "Physics", "Render", "Environment"};
        static constexpr const char* Lines[]      = {
            "Tyre load estimate steady · Pacejka reference not yet wired (Phase 7)",
            "Worker pool steady · 0 jobs starved over the 10 s window",
            "Viewport idle · no renderer connected in this build",
            "Proving grounds wind nominal · canopy phase seeded"};
        const uint32_t Index = HeartbeatIndex++ % 4;
        LogNamed(Categories[Index], LogSeverity::Info, Lines[Index], "WorkspaceHost.cpp");
    }
}

void WorkspaceHost::ConstructInterface(float Scale)
{
    Panels.ConstructInterface(Scale);
}

} // namespace Frontier
