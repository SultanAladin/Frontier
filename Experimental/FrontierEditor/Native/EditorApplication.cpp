/*==========================================================================================================================================
                                                           EDITORAPPLICATION.CPP
==========================================================================================================================================*/
// 🧩 Run the standalone editor workspace: production dockspace + notch hosting Viewport · Outliner · Inspector · Log Stream · Input.

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#ifndef FRONTIER_BUILD_COMMIT
#define FRONTIER_BUILD_COMMIT "local"
#endif

#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/WindowsApplication.h"
#include "Input/InputSubstrate.h"
#include "Native/GlfwInputPoll.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "Workspace/WorkspaceHost.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{

//----------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//----------------------------------------------------------------------------------------------------------------------

std::filesystem::path ExecutableDirectory(const char* ArgumentZero)
{
    std::error_code Error;
    if (ArgumentZero == nullptr || ArgumentZero[0] == '\0')
    {
        return std::filesystem::current_path(Error);
    }
    const std::filesystem::path Absolute = std::filesystem::absolute(ArgumentZero, Error);
    return Absolute.has_parent_path() ? Absolute.parent_path() : std::filesystem::current_path(Error);
}

// 📝 Bundled content resolves relative to the executable first (CLAUDE.md), then its parent, then the
//    working directory — launching from elsewhere must not hide the fonts or pick a partial collection.
std::filesystem::path ResolveContentRoot(const std::filesystem::path& FromExecutable)
{
    std::error_code             Error;
    const std::filesystem::path Working = std::filesystem::current_path(Error);
    const std::filesystem::path Candidates[] = {
        FromExecutable,
        FromExecutable / "..",
        FromExecutable / ".." / "..",
        FromExecutable / ".." / ".." / "..",
        FromExecutable / ".." / ".." / ".." / "..",
        Working,
    };
    for (const std::filesystem::path& Candidate : Candidates)
    {
        if (std::filesystem::is_directory(Candidate / "EngineContent/FontArchives", Error))
        {
            return std::filesystem::weakly_canonical(Candidate, Error);
        }
    }
    return FromExecutable;
}

void ReportSevereFailure(const char* Title, const std::string& Message) noexcept
{
    std::fprintf(stderr, "%s\n%s\n", Title, Message.c_str());
#if defined(_WIN32)
    MessageBoxA(nullptr, Message.c_str(), Title, MB_OK | MB_ICONERROR);
#endif
}

int RunEditorApplication(const char* ArgumentZero)
{
    const std::filesystem::path ExeDirectory = ExecutableDirectory(ArgumentZero);
    const std::filesystem::path ContentRoot  = ResolveContentRoot(ExeDirectory);
    Frontier::DiagnosticJournal::Append("Paths",
                                        "Content=" + Frontier::DiagnosticJournal::EncodePath(ContentRoot));

    GLFWwindow*                                     Window = nullptr;
    std::unique_ptr<Frontier::ControlCentrePanel>   Centre;
    std::unique_ptr<Frontier::WorkspaceHost>        Workspace;
    Frontier::InputSubstrate                        Substrate;
    Frontier::GlfwInputPoll                         Poll;

    Frontier::ApplicationWindowCallbacks Callbacks;
    Callbacks.Title             = "Frontier Editor [" FRONTIER_BUILD_COMMIT "]";
    Callbacks.MaterialRendering = false;

    Callbacks.Initialize = [&](GLFWwindow* CurrentWindow) {
        Window = CurrentWindow;
        Frontier::IconPresentation::Attach(Frontier::EncodeContentPath(ContentRoot / "EngineContent/Icons"));
        Centre = std::make_unique<Frontier::ControlCentrePanel>(
            ContentRoot / "EngineContent/FontArchives", ExeDirectory / "FrontierEditor.Personal.projectspace");
        Centre->ConfigureContentBrowserVisibility(false);
        if (!Centre->Initialize() || !Centre->Activate())
        {
            throw std::runtime_error(Centre->QueryDiagnostic());
        }
        Workspace = std::make_unique<Frontier::WorkspaceHost>(*Centre, Substrate,
                                                              ExeDirectory / "FrontierEditor.Layout.toml",
                                                              ExeDirectory / "FrontierEditor.Input.toml");
        Workspace->NotifyPointerCaptureChanged = [&](bool Captured) {
            glfwSetInputMode(Window, GLFW_CURSOR, Captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        };
    };

    Callbacks.ConfigurePlatform = [&]() {
        if (Workspace)
        {
            (void)Workspace->ConfigurePlatform();
        }
    };

    Callbacks.RecoverWindows = [&]() {
        if (Workspace)
        {
            Workspace->QueryPanels().RecoverPlatformWindows();
        }
    };

    Callbacks.PrepareInterface = [&]() { Centre->PrepareInterface(); };

    Callbacks.ConstructInterface = [&](bool MainVisible) {
        // 📝 Ingestion lives here, not in PrepareInterface: the host runs PrepareInterface before the
        //    ImGui adapters' NewFrame, so IO wheel and WantTextInput are only fresh from this point.
        Poll.Poll(Window, Substrate);
        const ImGuiIO& Io = ImGui::GetIO();
        if (Io.MouseWheel != 0.0f)
        {
            Substrate.InjectWheel(Io.MouseWheel);
        }
        Substrate.BeginCycle();
        const float Δτ = Io.DeltaTime;   // [s]
        (void)Centre->Integrate(Δτ);
        Workspace->Synchronize(Δτ);
        if (MainVisible)
        {
            const float Scale =
                std::clamp(Centre->QueryAppearance().QueryApplied().InterfaceScale / 100.0f, 0.6f, 2.0f);
            Workspace->ConstructInterface(Scale);
            Centre->ConstructInterface();
        }
    };

    Callbacks.Finalize = [&]() {
        if (Workspace)
        {
            Workspace->Finalize();
        }
        if (Window != nullptr)
        {
            glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        Workspace.reset();
        Centre.reset();
    };

    return Frontier::RunWindowsApplication(Callbacks);
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    (void)ArgumentCount;
    Frontier::DiagnosticJournal::Initialize("FrontierEditor");
    try
    {
        return RunEditorApplication(Arguments != nullptr && ArgumentCount > 0 ? Arguments[0] : nullptr);
    }
    catch (const std::exception& Failure)
    {
        Frontier::DiagnosticJournal::Append("SEVERE", Failure.what());
        ReportSevereFailure("Frontier Editor - Severe failure",
                            std::string("Startup failed: ") + Failure.what() + "\nLogs: " +
                                Frontier::DiagnosticJournal::EncodePath(Frontier::DiagnosticJournal::QueryFilename()));
        return 1;
    }
    catch (...)
    {
        Frontier::DiagnosticJournal::Append("SEVERE", "Unknown startup failure");
        ReportSevereFailure("Frontier Editor - Severe failure",
                            "Unknown startup failure. Collect the DiagnosticJournal log (see Frontier/Logs).");
        return 1;
    }
}
