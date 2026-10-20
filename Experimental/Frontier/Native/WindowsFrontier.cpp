/*==========================================================================================================================================
                                                           WINDOWSFRONTIER.CPP
==========================================================================================================================================*/
// 🧩 Run the native Project Browser; opening a project activates the editor workspace for that project.

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#ifndef FRONTIER_BUILD_COMMIT
#define FRONTIER_BUILD_COMMIT "local"
#endif
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/WindowsApplication.h"
#include "Engine/Interface/IconPresentation.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/Editor/EditorWorkspace.h"
#include "UI/Editor/ProjectBrowserPanel.h"
#include <windows.h>
#include <commdlg.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <imgui.h>

//------------------------------------------------------------------------------------------------------------------------
//                                                     PROJECT BROWSER APPLICATION
//------------------------------------------------------------------------------------------------------------------------

std::filesystem::path ChooseProjectPath(GLFWwindow *Window, bool Save)
{
    wchar_t       Filename[32768] = {};
    OPENFILENAMEW Selection{};
    Selection.lStructSize  = sizeof(Selection);
    Selection.hwndOwner    = glfwGetWin32Window(Window);
    Selection.lpstrFilter  = L"Frontier projects (*.projectspace)\0*.projectspace\0\0";
    Selection.lpstrFile    = Filename;
    Selection.nMaxFile    = 32768;
    Selection.lpstrDefExt = L"projectspace";
    Selection.Flags       = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                            (Save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const bool Accepted = Save ? GetSaveFileNameW(&Selection) != FALSE : GetOpenFileNameW(&Selection) != FALSE;
    return Accepted ? std::filesystem::path(Filename) : std::filesystem::path{};
}

int main()
{
    Frontier::DiagnosticJournal::Initialize("Frontier");
    Frontier::DiagnosticJournal::Append("Startup", "Frontier Project Browser with editor workspace activation");
    Frontier::ActivationOwnership AppearanceOwnership;
    Frontier::ControlCentrePanel *Appearance = nullptr;
    std::unique_ptr<Frontier::ProjectBrowserPanel> Browser;
    std::unique_ptr<Frontier::EditorWorkspace> Workspace;
    GLFWwindow *Window = nullptr;
    std::filesystem::path Preferences;
    bool ReturnRequested = false;
    Frontier::ApplicationWindowCallbacks Callbacks;
    Callbacks.Title = "Frontier - Project Browser [" FRONTIER_BUILD_COMMIT "]";
    Callbacks.Initialize = [&](GLFWwindow *Current)
    {
        Window = Current;
        Frontier::IconPresentation::Attach("EngineContent/Icons");
        const auto *Local = _wgetenv(L"LOCALAPPDATA");
        Preferences = Local ? std::filesystem::path(Local) / "Frontier/Personal.projectspace" : std::filesystem::path{};
        AppearanceOwnership.reset(new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences));
        Appearance = static_cast<Frontier::ControlCentrePanel *>(AppearanceOwnership.get());
        if (!Appearance->Initialize() || !Appearance->Activate())
            throw std::runtime_error("Frontier appearance initialization failed: " + Appearance->QueryDiagnostic());
        Browser = std::make_unique<Frontier::ProjectBrowserPanel>([&](bool Save) { return ChooseProjectPath(Window, Save); });
        Frontier::DiagnosticJournal::Append("Startup", "Project Browser initialized");
    };
    const auto ActivateProject = [&](const std::filesystem::path &Requested)
    {
        Frontier::ProjectSpaceDocument Document;
        std::string Diagnostic;
        if (!Frontier::ProjectSpaceCodec::Decode(Requested, Document, Diagnostic))
            throw std::runtime_error("Project activation failed: " + Diagnostic);
        Workspace.reset();
        AppearanceOwnership.reset(new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences, Requested));
        Appearance = static_cast<Frontier::ControlCentrePanel *>(AppearanceOwnership.get());
        if (!Appearance->Initialize() || !Appearance->Activate())
            throw std::runtime_error("Project activation failed: " + Appearance->QueryDiagnostic());
        Workspace = std::make_unique<Frontier::EditorWorkspace>(*Appearance);
        Frontier::EditorProjectSummary Summary;
        Summary.Name = Document.Sections["project"]["name"].value_or(std::string("Project"));
        Summary.Location = Frontier::DiagnosticJournal::EncodePath(Requested);
        Summary.Identity = Document.Sections["project"]["identity"].value_or(std::string{});
        if (const auto *Levels = Document.Sections["levels"].as_array()) Summary.Levels = Levels->size();
        if (const auto *Roots = Document.Sections["content_roots"].as_array()) Summary.Roots = Roots->size();
        if (const auto *Catalogue = Document.Sections["catalogue"].as_array()) Summary.Catalogue = Catalogue->size();
        Summary.StartupLevel = Document.Sections["engine"]["startup_level"].value_or(std::string{});
        Workspace->ConfigureProject(std::move(Summary));
        Workspace->EvaluateReturnToBrowser = [&]() { ReturnRequested = true; };
        glfwSetWindowTitle(Window, ("Frontier - " + Workspace->QueryProject().Name).c_str());
        Frontier::DiagnosticJournal::Append("ProjectSelection", Frontier::DiagnosticJournal::EncodePath(Requested));
    };
    Callbacks.PrepareInterface = [&]() { Appearance->PrepareInterface(); };
    Callbacks.RecoverWindows = [&]() { if (Workspace) Workspace->QueryPanels().RecoverPlatformWindows(); };
    Callbacks.ConfigurePlatform = [&]()
    {
        const auto Requested = Browser->ConsumeLaunchPath();
        if (!Requested.empty()) ActivateProject(Requested);
        if (ReturnRequested && Workspace)
        {
            ReturnRequested = false;
            Workspace.reset();
            AppearanceOwnership.reset(new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences));
            Appearance = static_cast<Frontier::ControlCentrePanel *>(AppearanceOwnership.get());
            if (!Appearance->Initialize() || !Appearance->Activate())
                throw std::runtime_error("Project Browser reactivation failed: " + Appearance->QueryDiagnostic());
            Browser->ResetSelection();
            glfwSetWindowTitle(Window, Callbacks.Title.c_str());
            Frontier::DiagnosticJournal::Append("ProjectSelection", "Returned to the Project Browser");
        }
        if (Workspace) (void)Workspace->ConfigurePlatform();
    };
    Callbacks.ConstructInterface = [&](bool MainVisible)
    {
        if (Workspace)
        {
            (void)Appearance->Integrate(ImGui::GetIO().DeltaTime);
            Workspace->ConstructInterface();
            if (MainVisible) Appearance->ConstructInterface();
        }
        else if (MainVisible)
        {
            Browser->ConfigurePalette(Frontier::ControlKit::Palette());
            Browser->ConstructInterface();
        }
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
