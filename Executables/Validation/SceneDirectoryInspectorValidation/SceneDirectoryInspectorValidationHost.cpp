/*==============================================================================================================================================
                                                SCENEDIRECTORYINSPECTORVALIDATIONHOST.CPP
==============================================================================================================================================*/
// 🧩 Standalone Vulkan validation host for the summoned two-slide scene-directory inspector ported from
//    Documentation/Prototypes/SceneDirectoryInspector.html. It stands up the shared Vulkan spine (PlatformWindow + VulkanHost + presentation
//    surface + VulkanImguiInterface + the Win32 ImGui relay), then — once the ImGui Vulkan backend and its font texture exist — brings up the
//    SvgIconRegistry and registers the global pack plus this target's placeholder Inspector glyph pack (one reference mark under every
//    classification + chrome key). Each frame it drives ConstructSceneDirectoryInspectorPanel inside one docked-full window over a bare
//    viewport, so a human can Tab / right-click to summon the card, walk the directory ⇄ inspect carousel and the inner Properties ⇄ History
//    carousel, and drive selection / rename / drag / visibility / revisions against the prototype side by side.
//    🔴 The icon registry uploads through ImGui_ImplVulkan_AddTexture, so it MUST come up after ImGui_ImplVulkan_Init and go down before the
//       backend shuts down. Bring-up and teardown are exact reverses, every Vulkan step gated on device-idle.

#include "Platform/Windowing/PlatformWindow.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/RenderExtension/Device/VulkanImguiInterface.h"

#include "EngineContext/Interface/WorkspaceHost/ImguiPlatformRelay.h"
#include "EngineContext/Interface/Theme/ThemeResolver.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"
#include "EngineContext/Interface/Icons/IconPackGlobal.h"

#include "SceneDirectoryInspectorPanel.h"
#include "InspectorGlyphs.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include <cstdint>
#include <cstdio>

using namespace Frontier;

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
    if (!InitializePlatformWindow(Window, "Frontier \xE2\x80\x94 Scene Directory Inspector Validation", 1280, 900))
    {
        fprintf(stderr, "[inspector-validation] window creation failed\n");
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
        fprintf(stderr, "[inspector-validation] surface creation failed\n");
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

    // 📝 Clear to a near-black slate, so the summoned card sits on the backdrop the prototype was designed against.
    Interface.Window.ClearValue.color.float32[0] = 0.06f;
    Interface.Window.ClearValue.color.float32[1] = 0.07f;
    Interface.Window.ClearValue.color.float32[2] = 0.09f;
    Interface.Window.ClearValue.color.float32[3] = 1.00f;

    // -- ImGui context + backends ---------------------------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    // 🔴 NO keyboard nav: this panel owns Tab as its summon / carousel key (the prototype's keydown map), and ImGui's
    //    NavEnableKeyboard would consume Tab for widget focus-cycling before the panel ever sees it. The panel's own
    //    InputText / combo widgets work without nav; nav only adds arrow / Tab focus movement we do not want here.
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

    // 📝 Resolve the shared theme once and mirror it into ImGui's style. The card resolves its OWN palette and metrics from this
    //    same configuration, so the summoned surface scales with the host.
    const ThemeConfiguration Theme = ResolveActiveTheme();
    EnforceThemeStyle(Theme);

    // -- Icon registry (needs the ImGui Vulkan backend live: it uploads through ImGui_ImplVulkan_AddTexture) --------------
    //    🔴 Two packs: the global chrome glyphs, and this target's placeholder Inspector pack — one reference mark under every
    //       classification key ("sdi-class-<key>") and every chrome key ("sdi-ui-<key>") while real art is deferred.
    SvgIconRegistry Icons;
    if (!InitializeSvgIconRegistry(Icons, Host))
    {
        fprintf(stderr, "[inspector-validation] SVG icon registry failed to start\n");
    }
    else
    {
        const bool GlobalOk    = RegisterGlobalIconPack(Icons);
        const bool InspectorOk = SceneDirectoryInspectorValidation::RegisterInspectorGlyphPack(Icons);
        if (!GlobalOk || !InspectorOk)
        {
            fprintf(stderr, "[inspector-validation] icon pack registration incomplete (global=%d inspector=%d)\n",
                    (int)GlobalOk, (int)InspectorOk);
        }
    }

    // -- The caller-owned inspector state, seeded to the prototype's opening pose ------------------------------------------
    SceneDirectoryInspectorValidation::InspectorPanelState State;
    SceneDirectoryInspectorValidation::InitializeInspectorSample(State);

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

        // 📝 One docked-full window over a bare viewport. The panel owns its own summon (Tab / right-click), both carousels, and
        //    all interaction; it draws the card on the foreground draw list, so the host window just supplies the input surface.
        const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(MainViewport->WorkPos);
        ImGui::SetNextWindowSize(MainViewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("Scene Directory Inspector", nullptr, HostFlags))
        {
            SceneDirectoryInspectorValidation::ConstructSceneDirectoryInspectorPanel(Theme, State, &Icons);
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
