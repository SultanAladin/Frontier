/*==========================================================================================================================================
                                                           WINDOWSFRONTIER.CPP
==========================================================================================================================================*/
// 🧩 Run only the native Project Browser; project selection never activates another workspace in this test application.

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/WindowsApplication.h"
#include "Engine/Interface/IconPresentation.h"
#include "UI/Editor/ControlCentrePanel.h"
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
    Frontier::DiagnosticJournal::Append("Startup", "Frontier Project Browser ONLY; editor activation disabled");
    Frontier::ActivationOwnership AppearanceOwnership;
    Frontier::ControlCentrePanel *Appearance = nullptr;
    std::unique_ptr<Frontier::ProjectBrowserPanel> Browser;
    GLFWwindow *Window = nullptr;
    std::string SelectedProject;
    Frontier::ApplicationWindowCallbacks Callbacks;
    Callbacks.Title = "Frontier - Project Browser";
    Callbacks.Initialize = [&](GLFWwindow *Current)
    {
        Window = Current;
        Frontier::IconPresentation::Attach("EngineContent/Icons");
        const auto *Local = _wgetenv(L"LOCALAPPDATA");
        const auto Preferences = Local ? std::filesystem::path(Local) / "Frontier/Personal.projectspace" : std::filesystem::path{};
        AppearanceOwnership.reset(new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences));
        Appearance = static_cast<Frontier::ControlCentrePanel *>(AppearanceOwnership.get());
        if (!Appearance->Initialize() || !Appearance->Activate())
            throw std::runtime_error("Frontier appearance initialization failed: " + Appearance->QueryDiagnostic());
        Browser = std::make_unique<Frontier::ProjectBrowserPanel>([&](bool Save) { return ChooseProjectPath(Window, Save); });
        Frontier::DiagnosticJournal::Append("Startup", "Project Browser initialized");
    };
    Callbacks.PrepareInterface = [&]() { Appearance->PrepareInterface(); };
    Callbacks.ConfigurePlatform = [&]()
    {
        const auto Requested = Browser->ConsumeLaunchPath();
        if (!Requested.empty())
        {
            SelectedProject = Frontier::DiagnosticJournal::EncodePath(Requested);
            Frontier::DiagnosticJournal::Append("ProjectSelection", SelectedProject + "; remaining in Project Browser by build scope");
        }
    };
    Callbacks.ConstructInterface = [&](bool MainVisible)
    {
        if (!MainVisible) return;
        Browser->ConfigurePalette(Frontier::ControlKit::Palette());
        Browser->ConstructInterface();
        if (!SelectedProject.empty())
        {
            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(.98f);
            if (ImGui::Begin("Project selected", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
            {
                ImGui::TextUnformatted("Project validated. This test build contains the Project Browser only.");
                ImGui::TextWrapped("%s", SelectedProject.c_str());
                if (ImGui::Button("Dismiss")) SelectedProject.clear();
            }
            ImGui::End();
        }
    };
    Callbacks.Finalize = [&]()
    {
        Browser.reset();
        AppearanceOwnership.reset();
        Frontier::DiagnosticJournal::Append("Shutdown", "Frontier application resources reclaimed");
    };
    return Frontier::RunWindowsApplication(Callbacks);
}
