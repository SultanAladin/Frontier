/*==============================================================================================================================================
                                                          WORKSPACELIFECYCLEHOST.CPP
==============================================================================================================================================*/
// 🧩 The shared application spine. Bring-up order: window → Vulkan host → surface → ImGui interface (creates the render pass) → ImGui context +
//    Win32/Vulkan backends → theme → dock state + workspaces. The frame loop polls events, services the resize backstop, begins a crash-safe
//    frame, records the themed host window (tab strip over the workspace dock), and submits/presents. Teardown mirrors bring-up in reverse,
//    each Vulkan step gated on device-idle. Every rule that keeps resize from crashing lives in the VulkanImguiInterface calls this loop makes.

#include "WorkspaceLifecycleHost.h"

#include "WorkspaceTabStrip.h"
#include "ImguiPlatformRelay.h"

#include "../Theme/ThemeResolver.h"

#include "Platform/Windowing/PlatformWindow.h"

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include "Graphics/RenderExtension/Device/VulkanImguiInterface.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void ReportVkResult(VkResult Outcome)
    {
        if (Outcome != VK_SUCCESS && Outcome != VK_SUBOPTIMAL_KHR)
            fprintf(stderr, "[vulkan] backend reported VkResult %d\n", (int)Outcome);
    }

    // Record the single full-viewport host window: the workspace tab row over the four-region dock. Mirrors the layout
    // the D3D scaffolds use for their host window, but the content is the shared tab strip + dock.
    void RecordWorkspaceDeck(const ThemeConfiguration& Theme, WorkspaceDockState& Dock)
    {
        const ImGuiViewport* Viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(Viewport->WorkPos);
        ImGui::SetNextWindowSize(Viewport->WorkSize);

        const ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(Theme.Palette.DeskBackground));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("##FrontierWorkspaceHost", nullptr, HostFlags))
        {
            ConstructWorkspaceTabStrip(Theme, Dock);
            ConstructWorkspaceDock(Theme, Dock);
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

int ExecuteWorkspaceApplication(const WorkspaceApplicationSpec& Spec)
{
    // -- Window ---------------------------------------------------------------------------------------------------------
    PlatformWindow Window;
    if (!InitializePlatformWindow(Window, Spec.WindowTitle, Spec.InitialWidth, Spec.InitialHeight))
    {
        fprintf(stderr, "[host] window creation failed\n");
        return 1;
    }

    // -- Vulkan host ----------------------------------------------------------------------------------------------------
    uint32_t ExtensionCount = 0;
    const char** RequiredExtensions = QueryRequiredInstanceExtensions(ExtensionCount);

    VulkanHost Host;
    if (!InitializeVulkanHost(Host, RequiredExtensions, ExtensionCount))
    {
        FinalizePlatformWindow(Window);
        return 1;
    }
    if (!ConstructPresentationSurface(Window, Host.Instance))
    {
        fprintf(stderr, "[host] surface creation failed\n");
        FinalizeVulkanHost(Host);
        FinalizePlatformWindow(Window);
        return 1;
    }

    // -- ImGui interface (creates the swapchain + render pass the backend pipeline binds to) -----------------------------
    VulkanImguiInterface Interface;
    if (!InitializeVulkanImguiInterface(Interface, Host, Window))
    {
        vkDestroySurfaceKHR(Host.Instance, Window.PresentationSurface, Host.Allocator);
        FinalizeVulkanHost(Host);
        FinalizePlatformWindow(Window);
        return 1;
    }

    // -- ImGui context + backends ---------------------------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    Io.IniFilename = nullptr;   // don't litter an imgui.ini next to the exe

    AttachImguiPlatform(Window);

    ImGui_ImplVulkan_InitInfo InitInfo = {};
    InitInfo.ApiVersion                 = Host.ApiVersion;
    InitInfo.Instance                   = Host.Instance;
    InitInfo.PhysicalDevice             = Host.PhysicalDevice;
    InitInfo.Device                     = Host.Device;
    InitInfo.QueueFamily                = Host.GraphicsQueueFamily;
    InitInfo.Queue                      = Host.GraphicsQueue;
    InitInfo.DescriptorPool             = Host.ImguiDescriptorPool;
    InitInfo.MinImageCount              = Interface.MinimumImageCount;
    InitInfo.ImageCount                 = Interface.Window.ImageCount;
    InitInfo.PipelineInfoMain.RenderPass = Interface.Window.RenderPass;
    InitInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    InitInfo.Allocator                  = Host.Allocator;
    InitInfo.CheckVkResultFn            = &ReportVkResult;
    ImGui_ImplVulkan_Init(&InitInfo);

    // -- Theme + workspace dock -----------------------------------------------------------------------------------------
    const ThemeConfiguration Theme = ResolveActiveTheme();
    EnforceThemeStyle(Theme);

    WorkspaceDockState Dock;
    InitializeWorkspaceDockState(Dock);
    for (uint32_t Index = 0; Index < Spec.WorkspaceCount; ++Index)
        RegisterWorkspace(Dock, Spec.Workspaces[Index]);
    if (Spec.DefaultWorkspaceIndex < Spec.WorkspaceCount)
        RequestWorkspaceActivation(Dock, (int)Spec.DefaultWorkspaceIndex);
    if (Spec.DocumentCatalogue != nullptr && Spec.DocumentCatalogueCount > 0)
        RegisterWorkspaceCatalogue(Dock, Spec.DocumentCatalogue, (int)Spec.DocumentCatalogueCount, (int)Spec.DefaultDocumentTypeIndex);

    // -- Frame loop -----------------------------------------------------------------------------------------------------
    while (!QueryWindowCloseRequested(Window))
    {
        PollPlatformEvents(Window);

        if (!BeginImguiFrame(Interface, Host, Window))
            continue;   // minimized / zero-extent / stale swapchain — the interface already flagged any rebuild

        ImGui_ImplVulkan_NewFrame();
        AdvanceImguiPlatform();
        ImGui::NewFrame();

        RecordWorkspaceDeck(Theme, Dock);

        ImGui::Render();
        SubmitAndPresentImguiFrame(Interface, Host, ImGui::GetDrawData());
    }

    // -- Teardown (reverse of bring-up, each Vulkan step gated on device-idle) ------------------------------------------
    vkDeviceWaitIdle(Host.Device);
    ImGui_ImplVulkan_Shutdown();
    DetachImguiPlatform(Window);
    ImGui::DestroyContext();

    FinalizeVulkanImguiInterface(Interface, Host);
    vkDestroySurfaceKHR(Host.Instance, Window.PresentationSurface, Host.Allocator);
    FinalizeVulkanHost(Host);
    FinalizePlatformWindow(Window);
    return 0;
}

}   // namespace Frontier
