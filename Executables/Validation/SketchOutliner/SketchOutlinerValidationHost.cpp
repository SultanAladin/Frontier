/*==============================================================================================================================================
                                                        SKETCHOUTLINERVALIDATIONHOST.CPP
==============================================================================================================================================*/
// 🧩 Standalone Vulkan validation host for the parametric-sketch outliner. It stands up the shared Vulkan spine (PlatformWindow + VulkanHost +
//    presentation surface + VulkanImguiInterface + the Win32 ImGui relay) exactly as the ParametricSketcher app does, then — once the ImGui Vulkan
//    backend and its font texture exist — brings up the SvgIconRegistry against the same host and registers BOTH icon tiers (global g- + local
//    cad-). It builds one caller-owned SketchOutlinerState through InitializeSketchOutlinerSample, then each frame drives ConstructSketchOutlinerPanel
//    inside a single docked-full ImGui window, so a human can exercise selection, the animated twisties, inline rename, the eye toggle, drag
//    relocation (own-subtree guarded), and the search + classification-chip filters. It writes to its own Binaries\Validation\SketchOutlinerValidation.exe
//    and never touches SceneDirectory.exe. Bring-up and teardown are the reverse of each other, every Vulkan step gated on device-idle.

#include "Platform/Windowing/PlatformWindow.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/RenderExtension/Device/VulkanImguiInterface.h"

#include "EngineContext/Interface/WorkspaceHost/ImguiPlatformRelay.h"
#include "EngineContext/Interface/Theme/ThemeResolver.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"
#include "EngineContext/Interface/Icons/IconPackGlobal.h"
#include "EngineContext/Interface/Icons/IconPackCad.h"

#include "EngineContext/Interface/WorkspaceHost/SketchOutliner/SketchOutlinerPanel.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include <cstdint>
#include <cstdio>

using namespace Frontier;
using Frontier::SketchOutlinerUi::SketchOutlinerState;
using Frontier::SketchOutlinerUi::InitializeSketchOutlinerSample;
using Frontier::SketchOutlinerUi::ConstructSketchOutlinerPanel;
using Frontier::SketchOutlinerUi::ResolveSketchContentProfile;

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void ReportVkResult(VkResult Outcome)
    {
        if (Outcome != VK_SUCCESS && Outcome != VK_SUBOPTIMAL_KHR)
        {
            fprintf(stderr, "[vulkan] reported VkResult %d\n", (int)Outcome);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                              MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    (void)ArgumentCount;
    (void)ArgumentValues;

    // -- Window ---------------------------------------------------------------------------------------------------------
    PlatformWindow Window;
    if (!InitializePlatformWindow(Window, "Frontier \xE2\x80\x94 Sketch Outliner Validation", 1280, 900))
    {
        fprintf(stderr, "[outliner-validation] window creation failed\n");
        return 1;
    }

    // -- Vulkan host ----------------------------------------------------------------------------------------------------
    uint32_t     ExtensionCount     = 0;
    const char** RequiredExtensions = QueryRequiredInstanceExtensions(ExtensionCount);

    VulkanHost Host;
    if (!InitializeVulkanHost(Host, RequiredExtensions, ExtensionCount))
    {
        FinalizePlatformWindow(Window);
        return 1;
    }
    if (!ConstructPresentationSurface(Window, Host.Instance))
    {
        fprintf(stderr, "[outliner-validation] surface creation failed\n");
        FinalizeVulkanHost(Host);
        FinalizePlatformWindow(Window);
        return 1;
    }

    // -- ImGui interface (owns the swapchain + render pass the backend pipeline binds to) --------------------------------
    VulkanImguiInterface Interface;
    if (!InitializeVulkanImguiInterface(Interface, Host, Window))
    {
        vkDestroySurfaceKHR(Host.Instance, Window.PresentationSurface, Host.Allocator);
        FinalizeVulkanHost(Host);
        FinalizePlatformWindow(Window);
        return 1;
    }

    // 📝 Clear to a near-black slate; the outliner window draws over it.
    Interface.Window.ClearValue.color.float32[0] = 0.06f;
    Interface.Window.ClearValue.color.float32[1] = 0.07f;
    Interface.Window.ClearValue.color.float32[2] = 0.09f;
    Interface.Window.ClearValue.color.float32[3] = 1.00f;

    // -- ImGui context + backends ---------------------------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    Io.IniFilename = nullptr;

    AttachImguiPlatform(Window);

    ImGui_ImplVulkan_InitInfo InitInfo = {};
    InitInfo.ApiVersion                   = Host.ApiVersion;
    InitInfo.Instance                     = Host.Instance;
    InitInfo.PhysicalDevice               = Host.PhysicalDevice;
    InitInfo.Device                       = Host.Device;
    InitInfo.QueueFamily                  = Host.GraphicsQueueFamily;
    InitInfo.Queue                        = Host.GraphicsQueue;
    InitInfo.DescriptorPool               = Host.ImguiDescriptorPool;
    InitInfo.MinImageCount                = Interface.MinimumImageCount;
    InitInfo.ImageCount                   = Interface.Window.ImageCount;
    InitInfo.PipelineInfoMain.RenderPass  = Interface.Window.RenderPass;
    InitInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    InitInfo.Allocator                    = Host.Allocator;
    InitInfo.CheckVkResultFn              = &ReportVkResult;
    ImGui_ImplVulkan_Init(&InitInfo);

    // 📝 Resolve the shared theme once and mirror it into ImGui's style so the panel inherits the palette.
    const ThemeConfiguration Theme = ResolveActiveTheme();
    EnforceThemeStyle(Theme);

    // -- Icon registry (needs the ImGui Vulkan backend live: it uploads through ImGui_ImplVulkan_AddTexture) --------------
    SvgIconRegistry Icons;
    if (!InitializeSvgIconRegistry(Icons, Host))
    {
        fprintf(stderr, "[outliner-validation] SVG icon registry failed to start\n");
    }
    else
    {
        const bool GlobalOk = RegisterGlobalIconPack(Icons);
        const bool CadOk    = RegisterCadIconPack(Icons);
        if (!GlobalOk || !CadOk)
        {
            fprintf(stderr, "[outliner-validation] icon pack registration incomplete (global=%d cad=%d)\n",
                    (int)GlobalOk, (int)CadOk);
        }
    }

    // -- The caller-owned panel state + its default demonstration part ---------------------------------------------------
    SketchOutlinerState State;
    InitializeSketchOutlinerSample(State, ResolveSketchContentProfile());

    // -- Frame loop -----------------------------------------------------------------------------------------------------
    while (!QueryWindowCloseRequested(Window))
    {
        PollPlatformEvents(Window);

        if (!BeginImguiFrame(Interface, Host, Window))
        {
            continue;   // minimized / zero-extent / stale swapchain — the interface already flagged any rebuild
        }

        ImGui_ImplVulkan_NewFrame();
        AdvanceImguiPlatform();
        ImGui::NewFrame();

        // 📝 One docked-full window hosting the outliner panel. The panel owns its own rows, filter bar, and interactions.
        const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(MainViewport->WorkPos);
        ImGui::SetNextWindowSize(MainViewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("Sketch Outliner", nullptr, HostFlags))
        {
            ConstructSketchOutlinerPanel(Theme, State, &Icons, ResolveSketchContentProfile());
        }
        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::Render();
        SubmitAndPresentImguiFrame(Interface, Host, ImGui::GetDrawData());
    }

    // -- Teardown (reverse of bring-up, each Vulkan step gated on device-idle) -------------------------------------------
    vkDeviceWaitIdle(Host.Device);

    FinalizeSvgIconRegistry(Icons);

    ImGui_ImplVulkan_Shutdown();
    DetachImguiPlatform(Window);
    ImGui::DestroyContext();

    FinalizeVulkanImguiInterface(Interface, Host);
    vkDestroySurfaceKHR(Host.Instance, Window.PresentationSurface, Host.Allocator);
    FinalizeVulkanHost(Host);
    FinalizePlatformWindow(Window);
    return 0;
}
