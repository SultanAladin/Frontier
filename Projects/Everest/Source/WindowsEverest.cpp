/*==========================================================================================================================================
                                                           WINDOWSEVEREST.CPP
==========================================================================================================================================*/
// 🧩 Standalone Windows x64 application entry point for Project Everest (launches directly into Everest GTX ReSTIR workspace
//    using staged <ProjectRoot>/Content/ archives with zero EngineContent/ dependency required at runtime).

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#ifndef FRONTIER_BUILD_COMMIT
#define FRONTIER_BUILD_COMMIT "local"
#endif

#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/VulkanPresentation.h"
#include "Engine/Interface/WindowsApplication.h"
#include "EverestWorkspace.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <windows.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{

std::filesystem::path QueryExecutableDirectory()
{
    wchar_t Buffer[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, Buffer, MAX_PATH))
    {
        return std::filesystem::path(Buffer).parent_path();
    }
    return std::filesystem::current_path();
}

std::filesystem::path ResolveEverestProjectRoot(const std::filesystem::path& ExeDir)
{
    std::error_code ErrorCode;
    const std::filesystem::path Candidates[] = {
        ExeDir,
        ExeDir / "Projects" / "Everest",
        std::filesystem::path("Projects/Everest"),
        std::filesystem::current_path(),
    };
    for (const auto& Candidate : Candidates)
    {
        if (std::filesystem::exists(Candidate / "Everest.projectspace", ErrorCode))
        {
            return Candidate;
        }
    }
    return ExeDir;
}

std::filesystem::path ResolveStagedOrEngineSubdir(const std::filesystem::path& ProjectRoot,
                                                  const std::filesystem::path& ExeDir,
                                                  const char*                  RelativeSubpath)
{
    std::error_code ErrorCode;
    const std::filesystem::path Candidates[] = {
        ProjectRoot / "Content" / RelativeSubpath,
        ExeDir / "Content" / RelativeSubpath,
        ExeDir / "EngineContent" / RelativeSubpath,
        std::filesystem::path("EngineContent") / RelativeSubpath,
    };
    for (const auto& Path : Candidates)
    {
        if (std::filesystem::exists(Path, ErrorCode))
        {
            return Path;
        }
    }
    return ProjectRoot / "Content" / RelativeSubpath;
}

} // namespace

int main()
{
    Frontier::DiagnosticJournal::Initialize("Everest");
    Frontier::DiagnosticJournal::Append("Startup", "Project Everest standalone GTX-First ReSTIR workspace starting");

    const std::filesystem::path            ExeDir      = QueryExecutableDirectory();
    const std::filesystem::path            ProjectRoot = ResolveEverestProjectRoot(ExeDir);
    const std::filesystem::path            ProjectFile = ProjectRoot / "Everest.projectspace";
    Frontier::ActivationOwnership          AppearanceOwnership;
    Frontier::ControlCentrePanel*          Appearance = nullptr;
    std::unique_ptr<Everest::EverestWorkspace> EverestSpace;
    std::map<VkImageView, VkDescriptorSet> ViewportDescriptors;

    const auto Describe = [&](VkImageView View) -> uint64_t
    {
        if (!View)
        {
            return 0u;
        }
        const auto Found = ViewportDescriptors.find(View);
        if (Found != ViewportDescriptors.end())
        {
            return reinterpret_cast<uint64_t>(Found->second);
        }
        const VkDescriptorSet Desc = ImGui_ImplVulkan_AddTexture(View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        ViewportDescriptors.emplace(View, Desc);
        return reinterpret_cast<uint64_t>(Desc);
    };

    const auto ReleaseDescriptor = [&](uint64_t Handle)
    {
        for (auto It = ViewportDescriptors.begin(); It != ViewportDescriptors.end(); ++It)
        {
            if (reinterpret_cast<uint64_t>(It->second) == Handle)
            {
                ImGui_ImplVulkan_RemoveTexture(It->second);
                ViewportDescriptors.erase(It);
                break;
            }
        }
    };

    Frontier::ApplicationWindowCallbacks Callbacks;
    Callbacks.Title = "Project Everest — GTX-First ReSTIR Proving Ground [" FRONTIER_BUILD_COMMIT "]";

    Callbacks.Initialize = [&](GLFWwindow*)
    {
        const auto IconsDir = ResolveStagedOrEngineSubdir(ProjectRoot, ExeDir, "Icons");
        const auto FontsDir = ResolveStagedOrEngineSubdir(ProjectRoot, ExeDir, "FontArchives");
        Frontier::IconPresentation::Attach(IconsDir);

        const auto*           Local       = _wgetenv(L"LOCALAPPDATA");
        std::filesystem::path Preferences = Local ? (std::filesystem::path(Local) / "Frontier/Personal.projectspace")
                                                  : std::filesystem::path{};

        AppearanceOwnership.reset(new Frontier::ControlCentrePanel(FontsDir, Preferences, ProjectFile));
        Appearance = static_cast<Frontier::ControlCentrePanel*>(AppearanceOwnership.get());
        if (!Appearance->Initialize() || !Appearance->Activate())
        {
            throw std::runtime_error("Everest ControlCentre initialization failed: " + Appearance->QueryDiagnostic());
        }

        EverestSpace = std::make_unique<Everest::EverestWorkspace>(*Appearance, ProjectRoot);
        Frontier::DiagnosticJournal::Append("Startup", "EverestWorkspace constructed from " + ProjectRoot.generic_string());
    };

    Callbacks.InitializeVulkan = [&](Frontier::VulkanPresentation& Display)
    {
        if (EverestSpace)
        {
            if (!EverestSpace->AttachVulkanPresentation(Display.QueryRasterAccess(), Describe, ReleaseDescriptor))
            {
                Frontier::DiagnosticJournal::Append("EverestVulkanWarning", EverestSpace->QueryDiagnostic());
            }
        }

        Display.ConfigureRendering(
            [&](VkCommandBuffer Commands, uint64_t Completed, uint64_t Upcoming)
            {
                if (EverestSpace)
                {
                    EverestSpace->ConstructGpuCommands(Commands, Completed, Upcoming);
                }
            },
            []() {});
    };

    Callbacks.PrepareInterface = [&]()
    {
        if (Appearance)
        {
            Appearance->PrepareInterface();
        }
    };

    Callbacks.RecoverWindows = [&]()
    {
        if (EverestSpace)
        {
            EverestSpace->QueryDockspace().RecoverPlatformWindows();
        }
    };

    Callbacks.ConfigurePlatform = [&]()
    {
        if (EverestSpace)
        {
            (void)EverestSpace->ConfigurePlatform();
        }
    };

    Callbacks.ConstructInterface = [&](bool MainVisible)
    {
        if (EverestSpace && Appearance)
        {
            (void)Appearance->Integrate(ImGui::GetIO().DeltaTime);
            EverestSpace->ConstructInterface(ImGui::GetIO().DeltaTime);
            if (MainVisible)
            {
                Appearance->ConstructInterface();
            }
        }
    };

    Callbacks.Finalize = [&]()
    {
        EverestSpace.reset();
        for (const auto& Entry : ViewportDescriptors)
        {
            if (Entry.second)
            {
                ImGui_ImplVulkan_RemoveTexture(Entry.second);
            }
        }
        ViewportDescriptors.clear();
        AppearanceOwnership.reset();
        Frontier::DiagnosticJournal::Append("Shutdown", "Project Everest standalone application exited cleanly");
    };

    return Frontier::RunWindowsApplication(Callbacks);
}
