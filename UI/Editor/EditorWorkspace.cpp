/*==========================================================================================================================================
                                                        EDITORWORKSPACE.CPP
==========================================================================================================================================*/
// 🧩 Host the original Content Browser and optional Notification Stream through callbacks into their original owners.

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
    if (!Panels.Register({"content", "Content Browser",
                          [&]()
                          {
                              Centre.QueryContentBrowser().ConstructHostedInterface(
                                  Centre.QueryAppearance().QueryApplied().InterfaceScale / 100.0f,
                                  (!Centre.QueryQuickOpen() ||
                                   ImGui::GetWindowViewport() != ImGui::GetMainViewport()) &&
                                      !Centre.QueryConfirmation().QueryOpen());
                          },
                          DockableBrowser, [&]() { Centre.QueryContentBrowser().SuspendInteraction(); },
                          [&]() { return Centre.QueryDockableContentBrowser(); }}) ||
        !Panels.Register(
            {"notifications", "Notification Stream", [&]() { Centre.ConstructHostedNotificationStream(); }, false}))
        throw std::runtime_error("Panel registration refused.");
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
                if (!DockableBrowser)
                    Centre.QueryContentBrowser().ConfigureOpen(
                        (*Layout.as_table())["browser_drawer_open"].value_or(false));
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
    Centre.QueryContentBrowser().SuspendInteraction();
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
        (void)Panels.ConfigureOpen("content", Centre.QueryDockableContentBrowser());
        (void)Panels.ConfigureOpen("notifications", false);
        LayoutBlocked = false;
        SavedLayout.clear();
        ObservedLayout.clear();
        ObservedSnapshot.clear();
        PersistenceFrames = 0;
        RetrySeconds = StableSeconds = 0;
        ConfigureLayoutDiagnostic("Layout reset; saves automatically.");
    }
    if (DockableBrowser != Centre.QueryDockableContentBrowser())
    {
        const bool Visible = Panels.QueryOpen("content");
        DockableBrowser    = Centre.QueryDockableContentBrowser();
        Centre.QueryContentBrowser().SuspendInteraction();
        (void)Panels.ConfigureOpen("content", DockableBrowser);
        Centre.QueryContentBrowser().ConfigureOpen(!DockableBrowser && Visible);
    }
    Centre.ConfigureDockedPanels(DockableBrowser, Panels.QueryOpen("notifications"));
    Panels.ConstructInterface(Centre.QueryAppearance().QueryApplied().InterfaceScale / 100.0f);
    Rendered = true;
    Centre.ConfigureDockedPanels(DockableBrowser, Panels.QueryOpen("notifications"));
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
    if (!Layout.empty())
        Layout.insert_or_assign("browser_drawer_open", Centre.QueryContentBrowser().QueryExpansionTab().QueryOpen());
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
