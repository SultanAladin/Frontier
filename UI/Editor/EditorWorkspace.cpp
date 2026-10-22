/*==========================================================================================================================================
                                                        EDITORWORKSPACE.CPP
==========================================================================================================================================*/
// 🧩 Copied 1:1 from the TextureWorkspace system (arena/db2f9953-frontier); the ONLY change is the panel
//    registration below: this workspace registers exactly one panel — the Log Stream. Top bar, layout
//    persistence and the Control Centre (notch) interaction are the original code, untouched.

#include "EditorWorkspace.h"
#include <cmath>
#include <imgui.h>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace Frontier
{
EditorWorkspace::EditorWorkspace(ControlCentrePanel &ControlCentre)
    : Centre(ControlCentre), DockableBrowser(ControlCentre.QueryDockableContentBrowser())
{
    if (!LogPanel.Initialize() || !LogPanel.Activate())
        throw std::runtime_error("Log Stream panel activation failed.");
    // 🪵 ONE panel only: the Log Stream. No Content Browser, no Notification Stream, no Viewport.
    if (!Panels.Register({"logstream", "Log Stream", [&]() { LogPanel.ConstructHostedInterface(); }, true}))
        throw std::runtime_error("Panel registration refused.");
    // 📝 The top bar owns the first 52 unscaled pixels; the dockspace starts below it.
    Panels.ConfigureWorkspaceInsets(52, 0);
    ImGui::GetIO().IniFilename = nullptr;
    ResetRevision              = Centre.QueryLayoutResetRevision();
    const auto &Path           = Centre.QueryPersonalProjectPath();
    if (Path.extension() != ".projectspace")
        ConfigureLayoutDiagnostic("Layout is session-only: no personal projectspace.");
    if (Path.extension() == ".projectspace" && std::filesystem::exists(Path))
    {
        ProjectSpaceDocument Document;
        std::string          Message;
        if (!ProjectSpaceCodec::Decode(Path, Document, Message))
        {
            LayoutBlocked = true;
            ConfigureLayoutDiagnostic("Layout read refused: " + Message);
        }
        else if (const auto Layout = Document.Sections["workspace"]["layout"]; Layout)
        {
            if (Layout.is_table() && Panels.ConfigureLayout(*Layout.as_table(), Message))
            {
                LayoutRestored = true;
            }
            else
            {
                LayoutBlocked = true;
                ConfigureLayoutDiagnostic("Saved layout refused; defaults active. Reset saved layout to replace it.");
            }
        }
    }
}

EditorWorkspace::~EditorWorkspace()
{
    try
    {
        if (!FinalizeLayout() && !LayoutDiagnostic.empty())
            std::cerr << LayoutDiagnostic << '\n';
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "Layout save failed: " << Failure.what() << '\n';
    }
    Centre.ConfigureDockedPanels(false, false);
}

bool EditorWorkspace::ConfigurePlatform() noexcept
{
    Panels.ConfigureDetachedWindows(Centre.QueryDetachedWindows());
    return Panels.ConfigurePlatform();
}

void EditorWorkspace::ConstructInterface()
{
    if (ResetRevision != Centre.QueryLayoutResetRevision())
    {
        ResetRevision = Centre.QueryLayoutResetRevision();
        Panels.ResetLayout();
        (void)Panels.ConfigureOpen("logstream", true);
        LayoutBlocked = false;
        SavedLayout.clear();
        ObservedLayout.clear();
        ObservedSnapshot.clear();
        PersistenceFrames = 0;
        RetrySeconds = StableSeconds = 0;
        ConfigureLayoutDiagnostic("Layout reset; saves automatically.");
    }
    const float Scale = Centre.QueryAppearance().QueryApplied().InterfaceScale / 100.0f;
    Centre.ConfigureDockedPanels(false, false);
    ConstructTopBar(Scale);
    Panels.ConstructInterface(Scale);
    Rendered = true;
    Centre.ConfigureDockedPanels(false, false);
}

void EditorWorkspace::ConstructTopBar(float Scale)
{
    const auto *Main = ImGui::GetMainViewport();
    if (Main->WorkSize.x <= 0 || Main->WorkSize.y <= 0) return;
    ImGui::SetNextWindowPos(Main->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(Main->WorkSize.x, 52 * Scale));
    ImGui::SetNextWindowViewport(Main->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * Scale, 0));
    ImGui::Begin("Frontier editor top bar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus);
    const float CentreY = (52 * Scale - ImGui::GetFrameHeight()) * .5f;
    ImGui::SetCursorPosY(CentreY);
    if (ImGui::Button("< Back"))
    {
        if (EvaluateReturnToBrowser) EvaluateReturnToBrowser();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Return to the Project Browser");
    ImGui::SameLine();
    ImGui::SetCursorPosY(CentreY);
    ImGui::TextUnformatted(Project.Name.c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosY(CentreY);
    ImGui::TextDisabled("%zu levels · %zu catalogue entries", Project.Levels, Project.Catalogue);
    const char *Actions[] = {"Control centre", "Theme", "Settings"};
    float ActionsWidth = 0;
    for (const auto *Action : Actions)
        ActionsWidth += ImGui::CalcTextSize(Action).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(Main->WorkSize.x - ActionsWidth - 12 * Scale);
    ImGui::SetCursorPosY(CentreY);
    if (ImGui::Button(Actions[0])) Centre.ActivateSettingsPage(ControlCentrePageCategory::QuickControls);
    ImGui::SameLine();
    ImGui::SetCursorPosY(CentreY);
    if (ImGui::Button(Actions[1])) Centre.ActivatePage(AppearancePageCategory::Theme);
    ImGui::SameLine();
    ImGui::SetCursorPosY(CentreY);
    if (ImGui::Button(Actions[2])) Centre.ActivateSettingsPage(ControlCentrePageCategory::Settings);
    ImGui::End();
    ImGui::PopStyleVar();
}

namespace
{
std::string EncodeText(const toml::table &Layout)
{
    std::ostringstream Text;
    Text << Layout;
    return Text.str();
}
} // namespace

void EditorWorkspace::ConfigureLayoutDiagnostic(std::string Message)
{
    LayoutDiagnostic = std::move(Message);
    Centre.ConfigureLayoutDiagnostic(LayoutDiagnostic);
}

toml::table EditorWorkspace::CaptureLayout() const
{
    auto Layout = Panels.EncodeLayout();
    return Layout;
}

bool EditorWorkspace::FinalizeLayout()
{
    if (!Rendered || !Panels.QuerySeated() || LayoutBlocked)
        return !LayoutBlocked;
    const auto &Path = Centre.QueryPersonalProjectPath();
    if (Path.extension() != ".projectspace")
        return true;
    const auto &IO     = ImGui::GetIO();
    const auto  Layout = IO.DisplaySize.x > 0 && IO.DisplaySize.y > 0 ? CaptureLayout() : ObservedSnapshot;
    if (Layout.empty())
        return true;
    std::string Message;
    if (!PanelDockspace::ValidateLayout(Layout, Message))
    {
        ConfigureLayoutDiagnostic("Layout save refused: " + Message);
        return false;
    }
    const auto Text = EncodeText(Layout);
    if (Text == SavedLayout)
        return true;
    if (!Centre.PersistWorkspaceLayout(Layout, Message))
    {
        ConfigureLayoutDiagnostic("Layout save failed: " + Message);
        return false;
    }
    SavedLayout = Text;
    ConfigureLayoutDiagnostic("Layout saved in personal projectspace.");
    return true;
}

void EditorWorkspace::IntegrateLayoutPersistence(double ElapsedSeconds)
{
    if (!Rendered || LayoutBlocked || !std::isfinite(ElapsedSeconds) || ElapsedSeconds < 0 ||
        Centre.QueryPersonalProjectPath().extension() != ".projectspace")
        return;
    // 📝 Call after Render: tabs, splitters and platform geometry must have completed the current frame.
    if (++PersistenceFrames < 8 || ImGui::GetIO().DisplaySize.x <= 0 || ImGui::GetIO().DisplaySize.y <= 0)
        return;
    const auto Snapshot = CaptureLayout();
    const auto Current  = EncodeText(Snapshot);
    RetrySeconds        = std::max(0.0, RetrySeconds - ElapsedSeconds);
    if (Current != ObservedLayout)
    {
        ObservedLayout   = Current;
        ObservedSnapshot = Snapshot;
        StableSeconds    = 0;
        return;
    }
    StableSeconds += std::min(ElapsedSeconds, .25);
    if (Current != SavedLayout && StableSeconds >= 2 && RetrySeconds <= 0 && !ImGui::IsMouseDown(0) &&
        !ImGui::IsMouseDown(1) &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        if (!FinalizeLayout())
            RetrySeconds = 10;
}
} // namespace Frontier
