/*==============================================================================================================================================
                                                CONSTRUCTIONCATALOGUEVALIDATIONHOST.CPP
==============================================================================================================================================*/
// 🧩 Standalone Vulkan validation host for the two-slide construction catalogue ported from Documentation/Prototypes/ConstructionCatalogueMenu.html.
//    It stands up the shared Vulkan spine (PlatformWindow + VulkanHost + presentation surface + VulkanImguiInterface + the Win32 ImGui relay), then
//    — once the ImGui Vulkan backend and its font texture exist — brings up the SvgIconRegistry and registers the global pack, the shared ToolMenu
//    pack (chrome + shared parameter glyphs), and this target's placeholder Construction glyph pack (one reference mark under every op/param/badge
//    key). Each frame it drives ConstructConstructionCataloguePanel inside one docked-full window, so a human can drive the document-state control
//    surface, watch the gate re-evaluate all 128 operations, and step the carousel against the prototype side by side.
//    🔴 The icon registry uploads through ImGui_ImplVulkan_AddTexture, so it MUST come up after ImGui_ImplVulkan_Init and go down before the backend
//       shuts down. Bring-up and teardown are exact reverses, every Vulkan step gated on device-idle.

#include "Platform/Windowing/PlatformWindow.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/RenderExtension/Device/VulkanImguiInterface.h"

#include "EngineContext/Interface/WorkspaceHost/ImguiPlatformRelay.h"
#include "EngineContext/Interface/Theme/ThemeResolver.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"
#include "EngineContext/Interface/Icons/IconPackGlobal.h"
#include "EngineContext/Interface/Icons/IconPackToolMenu.h"

#include "ConstructionCataloguePanel.h"
#include "ConstructionGlyphs.h"

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
    if (!InitializePlatformWindow(Window, "Frontier \xE2\x80\x94 Construction Catalogue Validation", 1280, 900))
    {
        fprintf(stderr, "[construction-validation] window creation failed\n");
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
        fprintf(stderr, "[construction-validation] surface creation failed\n");
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

    // 📝 Clear to a near-black slate, so the card sits on the backdrop the prototype was designed against.
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

    // 📝 Resolve the shared theme once and mirror it into ImGui's style. The card resolves its OWN palette and metrics from this
    //    same configuration, so the scaffolding column and the card under review scale together.
    const ThemeConfiguration Theme = ResolveActiveTheme();
    EnforceThemeStyle(Theme);

    // -- Icon registry (needs the ImGui Vulkan backend live: it uploads through ImGui_ImplVulkan_AddTexture) --------------
    //    🔴 Three packs: the global chrome glyphs, the shared ToolMenu pack (parameter glyphs the reused options column resolves),
    //       and this target's placeholder Construction pack — one reference mark under every op/param/badge key while real art is
    //       deferred. The Construction pack rides the SAME "tool-<name>" key namespace the reused leaves resolve through.
    SvgIconRegistry Icons;
    if (!InitializeSvgIconRegistry(Icons, Host))
    {
        fprintf(stderr, "[construction-validation] SVG icon registry failed to start\n");
    }
    else
    {
        const bool GlobalOk       = RegisterGlobalIconPack(Icons);
        const bool ToolMenuOk     = RegisterToolMenuIconPack(Icons);
        const bool ConstructionOk = ConstructionCatalogueValidation::RegisterConstructionGlyphPack(Icons);
        if (!GlobalOk || !ToolMenuOk || !ConstructionOk)
        {
            fprintf(stderr, "[construction-validation] icon pack registration incomplete (global=%d toolmenu=%d construction=%d)\n",
                    (int)GlobalOk, (int)ToolMenuOk, (int)ConstructionOk);
        }
    }

    // -- The caller-owned card state, seeded to the prototype's opening pose ----------------------------------------------
    ConstructionCatalogueValidation::ConstructionCatalogueState State;
    ConstructionCatalogueValidation::InitializeConstructionCatalogueSample(State);

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

        // 📝 One docked-full window hosting the panel: the scaffolding column on the left, the live card on the right.
        const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(MainViewport->WorkPos);
        ImGui::SetNextWindowSize(MainViewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("Construction Catalogue", nullptr, HostFlags))
        {
            ConstructionCatalogueValidation::ConstructConstructionCataloguePanel(Theme, State, &Icons);
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
