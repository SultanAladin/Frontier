/*====================================================================================================================================
                                                      WINDOWSFRONTIER.CPP
====================================================================================================================================*/
// 🧩 Frontier application, staged exactly as specified:
//      1. The application opens on the copied Project Browser (TextureWorkspace panel set + Control Centre notch).
//      2. Creating a Blank Project (or opening any .projectspace) hands the path to a NEW Frontier window
//         (a second process of this executable with --project), leaving the browser open.
//      3. The project window shows exactly ONE panel — the Log Stream — plus the notch.
//    Composition mirrors Experimental/TextureStudio/Native/WindowsTextureStudio.cpp on the source system.

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#ifndef FRONTIER_BUILD_COMMIT
#define FRONTIER_BUILD_COMMIT "local"
#endif
#include "Engine/ContentInterchange/ProjectSpaceCodec.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/WindowsApplication.h"
#include "Engine/Lifecycle/StartupSequenceTrace.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/Editor/EditorWorkspace.h"
#include "UI/Editor/ProjectBrowserPanel.h"
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <imgui.h>
#include <objbase.h>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

//------------------------------------------------------------------------------------------------------------------------
//                                                         FILE DIALOGUES
//------------------------------------------------------------------------------------------------------------------------

static std::filesystem::path ChooseProjectPath(GLFWwindow *Window, bool Save)
{
    wchar_t       Filename[32768] = {};
    OPENFILENAMEW Selection{};
    Selection.lStructSize = sizeof(Selection);
    Selection.hwndOwner   = glfwGetWin32Window(Window);
    Selection.lpstrFilter = L"Frontier projects (*.projectspace)\0*.projectspace\0\0";
    Selection.lpstrFile   = Filename;
    Selection.nMaxFile    = 32768;
    Selection.lpstrDefExt = L"projectspace";
    Selection.Flags       = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (Save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const bool Accepted   = Save ? GetSaveFileNameW(&Selection) != FALSE : GetOpenFileNameW(&Selection) != FALSE;
    return Accepted ? std::filesystem::path(Filename) : std::filesystem::path{};
}

static std::vector<std::filesystem::path> ChooseImportFiles(GLFWwindow *Window)
{
    std::vector<wchar_t> Filenames(32768, L'\0');
    OPENFILENAMEW        Selection{};
    Selection.lStructSize = sizeof(Selection);
    Selection.hwndOwner   = glfwGetWin32Window(Window);
    Selection.lpstrFilter = L"Supported files (*.png;*.jpg;*.jpeg;*.webp;*.md)\0*.png;*.jpg;*.jpeg;*.webp;*.md\0\0";
    Selection.lpstrFile   = Filenames.data();
    Selection.nMaxFile    = DWORD(Filenames.size());
    Selection.Flags       = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (GetOpenFileNameW(&Selection) == FALSE) return {};
    std::vector<std::filesystem::path> Paths;
    const wchar_t                     *Cursor = Filenames.data();
    const std::wstring                 Head(Cursor);
    Cursor += Head.size() + 1;
    if (*Cursor == L'\0') Paths.emplace_back(Head);
    else
        while (*Cursor != L'\0')
        {
            Paths.emplace_back(std::filesystem::path(Head) / Cursor);
            Cursor += wcslen(Cursor) + 1;
        }
    return Paths;
}

static std::filesystem::path ChooseImportFolder(GLFWwindow *Window)
{
    wchar_t     Display[MAX_PATH] = {};
    BROWSEINFOW Selection{};
    Selection.hwndOwner      = glfwGetWin32Window(Window);
    Selection.pszDisplayName = Display;
    Selection.lpszTitle      = L"Select a folder to import supported files from";
    Selection.ulFlags        = BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
    if (const PIDLIST_ABSOLUTE Chosen = SHBrowseForFolderW(&Selection))
    {
        wchar_t    Folder[MAX_PATH] = {};
        const bool Resolved         = SHGetPathFromIDListW(Chosen, Folder) != FALSE;
        CoTaskMemFree(Chosen);
        if (Resolved) return std::filesystem::path(Folder);
    }
    return std::filesystem::path{};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  NEW WINDOW FOR A PROJECT
//------------------------------------------------------------------------------------------------------------------------

// 🚀 Opening a project opens a NEW Frontier window for it: a second process of this executable,
//    started with --project, so the Project Browser stays open behind it.
static void SpawnProjectWindow(const std::filesystem::path &ProjectPath)
{
    wchar_t Executable[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, Executable, MAX_PATH)) return;
    std::wstring        Command = L"\"" + std::wstring(Executable) + L"\" --project \"" + ProjectPath.wstring() + L"\"";
    STARTUPINFOW        Startup{};
    Startup.cb = sizeof(Startup);
    PROCESS_INFORMATION Process{};
    if (CreateProcessW(nullptr, Command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &Startup, &Process))
    {
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
        Frontier::DiagnosticJournal::Append("ProjectSelection", "New Frontier window spawned for project");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                              MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char **Arguments)
{
    // --project <path>: this process is a project window (one Log Stream panel + notch).
    std::filesystem::path ProjectArgument;
    for (int Index = 1; Index + 1 < ArgumentCount; ++Index)
        if (std::string(Arguments[Index]) == "--project") ProjectArgument = Arguments[Index + 1];
    const bool ProjectMode = !ProjectArgument.empty();

    Frontier::DiagnosticJournal::Initialize("Frontier");
    Frontier::DiagnosticJournal::Append("Startup", ProjectMode ? "Frontier project window (Log Stream)" : "Frontier Project Browser");

    Frontier::ActivationOwnership                AppearanceOwnership;
    Frontier::ControlCentrePanel                *Appearance = nullptr;
    std::unique_ptr<Frontier::ProjectBrowserPanel> Browser;
    std::unique_ptr<Frontier::EditorWorkspace>    Workspace;
    GLFWwindow                                  *Window = nullptr;
    std::filesystem::path                        Preferences;

    Frontier::ApplicationWindowCallbacks Callbacks;
    Callbacks.Title = ProjectMode ? "Frontier - Project" : "Frontier - Project Browser [" FRONTIER_BUILD_COMMIT "]";
    Callbacks.Initialize = [&](GLFWwindow *Current)
    {
        Window = Current;
        Frontier::IconPresentation::Attach("EngineContent/Icons");
        const auto *Local = _wgetenv(L"LOCALAPPDATA");
        Preferences = Local ? std::filesystem::path(Local) / "Frontier/Personal.projectspace" : std::filesystem::path{};

        // 🧾 Recorded startup sequence: every step is journalled; the project window also feeds its
        //    journal into the Log Stream so the first thing the panel shows is how the app started.
        Frontier::StartupSequenceTrace Startup;
        Startup.AddStep("Platform window (GLFW)", [] { return true; });
        Startup.AddStep("Vulkan device", [] { return true; });
        Startup.AddStep("Control Centre (notch)", [] { return true; });
        Startup.AddStep(ProjectMode ? "Log Stream panel" : "Project Browser panel", [] { return true; });

        AppearanceOwnership.reset(new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences,
                                                                    ProjectMode ? ProjectArgument : std::filesystem::path{}));
        Appearance = static_cast<Frontier::ControlCentrePanel *>(AppearanceOwnership.get());
        // 📝 Match TextureWorkspace's host: this project window has only its registered Log Stream panel + notch.
        if (ProjectMode) Appearance->ConfigureContentBrowserVisibility(false);
        if (!Appearance->Initialize() || !Appearance->Activate())
            throw std::runtime_error("Frontier appearance initialization failed: " + Appearance->QueryDiagnostic());
        (void)Startup.Run();

        if (ProjectMode)
        {
            std::string              ProjectName = "Project";
            Frontier::ProjectSpaceDocument Document;
            std::string              Diagnostic;
            if (Frontier::ProjectSpaceCodec::Decode(ProjectArgument, Document, Diagnostic))
                ProjectName = Document.Sections["project"]["name"].value_or(ProjectName);
            Workspace = std::make_unique<Frontier::EditorWorkspace>(*Appearance);
            Frontier::EditorProjectSummary Summary;
            Summary.Name     = ProjectName;
            Summary.Location = Frontier::DiagnosticJournal::EncodePath(ProjectArgument);
            Summary.Identity = Document.Sections["project"]["identity"].value_or(std::string{});
            if (const auto *Levels = Document.Sections["levels"].as_array()) Summary.Levels = Levels->size();
            if (const auto *Roots = Document.Sections["content_roots"].as_array()) Summary.Roots = Roots->size();
            if (const auto *Catalogue = Document.Sections["catalogue"].as_array()) Summary.Catalogue = Catalogue->size();
            Summary.StartupLevel = Document.Sections["engine"]["startup_level"].value_or(std::string{});
            Workspace->ConfigureProject(std::move(Summary));
            for (const Frontier::StartupStepEntry &Entry : Startup.QueryEntries())
                Workspace->QueryStream().Emit(Entry.Succeeded ? Frontier::LogLevel::Info : Frontier::LogLevel::Error,
                                              "Startup", 0,
                                              Entry.Name + (Entry.Succeeded ? " — ok" : " — FAILED"));
            Workspace->QueryStream().Emit(Frontier::LogLevel::Info, "EditorUI", 0,
                                          "Log Stream is the only panel in this window (by design)");
            glfwSetWindowTitle(Window, ("Frontier - " + ProjectName).c_str());
        }
        else
        {
            Browser = std::make_unique<Frontier::ProjectBrowserPanel>([&](bool Save) { return ChooseProjectPath(Window, Save); });
            Browser->PickImportFiles  = [&]() { return ChooseImportFiles(Window); };
            Browser->PickImportFolder = [&]() { return ChooseImportFolder(Window); };
        }
        Frontier::DiagnosticJournal::Append("Startup", ProjectMode ? "Project window initialized" : "Project Browser initialized");
    };
    Callbacks.PrepareInterface = [&]() { Appearance->PrepareInterface(); };
    Callbacks.RecoverWindows   = [&]() { if (Workspace) Workspace->QueryPanels().RecoverPlatformWindows(); };
    Callbacks.ConfigurePlatform = [&]()
    {
        if (Browser)
        {
            const auto Requested = Browser->ConsumeLaunchPath();
            if (!Requested.empty()) SpawnProjectWindow(Requested); // blank-project hand-off -> new window
        }
        if (Workspace) (void)Workspace->ConfigurePlatform();
    };
    Callbacks.ConstructInterface = [&](bool MainVisible)
    {
        (void)Appearance->Integrate(ImGui::GetIO().DeltaTime);
        if (Workspace) Workspace->ConstructInterface();
        else if (Browser)
        {
            Browser->ConfigurePalette(Frontier::ControlKit::Palette());
            Browser->ConstructInterface();
        }
        if (MainVisible) Appearance->ConstructInterface(); // the notch, in both windows
    };
    Callbacks.AfterRender = [&]() { if (Workspace) Workspace->IntegrateLayoutPersistence(ImGui::GetIO().DeltaTime); };
    Callbacks.Finalize = [&]()
    {
        Workspace.reset();
        Browser.reset();
        AppearanceOwnership.reset();
        Frontier::DiagnosticJournal::Append("Shutdown", "Frontier application resources reclaimed");
    };
    return Frontier::RunWindowsApplication(Callbacks);
}
