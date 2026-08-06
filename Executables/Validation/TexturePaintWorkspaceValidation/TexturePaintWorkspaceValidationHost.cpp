/*==============================================================================================================================================
                                                    TEXTUREPAINTWORKSPACEVALIDATIONHOST.CPP
==============================================================================================================================================*/
// 🧩 Standalone Vulkan validation host for the combined texture-paint workspace: the layer rail, the centre paint field with its
//    right-click-summoned instrument card, and the property column of channel + mask cards. It stands up the shared Vulkan spine
//    (PlatformWindow + VulkanHost + presentation surface + VulkanImguiInterface + the Win32 ImGui relay), then — once the ImGui Vulkan
//    backend and its font texture exist — brings up the SvgIconRegistry plus the paint pack and the non-square strip store the instrument
//    wells read. Each frame it drives the whole workspace in one docked-full window.
//
//    🔴 Two texture tiers, both gated on the ImGui Vulkan backend: the registry (square nib crops + parameter glyphs, registered up front) and
//       the strip store (the 5:1 landscape art, uploaded lazily). Both upload through ImGui_ImplVulkan_AddTexture, so both MUST come up after
//       ImGui_ImplVulkan_Init and go down before the backend shuts down. Bring-up and teardown are exact reverses, gated on device-idle.
//
//    📝 Every third of the workspace is EMBEDDED, not re-implemented: the rail + channel panels are compiled in place from their sibling
//       validation folders, and the summoned card pulls in PaintToolValidation's whole card as TexturePaintValidation already does. This app's
//       Build.bat rebuilds the shared pillar libs (which ship the theme + the shared mask card) and links the four siblings' units in.

#include "Platform/Windowing/PlatformWindow.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/RenderExtension/Device/VulkanImguiInterface.h"

#include "EngineContext/Interface/WorkspaceHost/ImguiPlatformRelay.h"
#include "EngineContext/Interface/Theme/ThemeResolver.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"
#include "EngineContext/Interface/Icons/IconPackGlobal.h"

#include "PaintIconPack.h"
#include "PaintIconStore.h"

#include "TexturePaintWorkspacePanel.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include <cstdint>
#include <cstdio>

using namespace Frontier;

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The short-axis raster height every landscape strip is uploaded at. Matched to PaintToolValidation's own constant: 64 px against the
    //    authored 300x60 box gives a 320 px long axis, above the 225 px the well actually draws after its 1.5x grow — so the strip is
    //    minified, never magnified, at the one site that shows it.
    constexpr uint32_t PaintStripShortEdge = 64u;

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
    // 📝 Wider than the single-surface hosts: the three columns need the room, and 1400 px keeps the summoned card's 560 px box clear of
    //    the rail and column without folding.
    PlatformWindow Window;
    if (!InitializePlatformWindow(Window, "Frontier \xE2\x80\x94 Texture Paint Workspace Validation", 1400, 900))
    {
        fprintf(stderr, "[texturepaint-workspace] window creation failed\n");
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
        fprintf(stderr, "[texturepaint-workspace] surface creation failed\n");
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

    // 📝 Clear to the prototype's own desk colour, so the rail, field and column sit on the backdrop they were designed against.
    Interface.Window.ClearValue.color.float32[0] = 0.055f;
    Interface.Window.ClearValue.color.float32[1] = 0.059f;
    Interface.Window.ClearValue.color.float32[2] = 0.067f;
    Interface.Window.ClearValue.color.float32[3] = 1.000f;

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

    // 📝 The theme is resolved once; every embedded panel resolves its own palette and metrics from it each frame, so the workspace scales
    //    as one surface.
    const ThemeConfiguration Theme = ResolveActiveTheme();
    EnforceThemeStyle(Theme);

    // -- Texture tiers (both need the ImGui Vulkan backend live) ----------------------------------------------------------
    SvgIconRegistry Icons;
    if (!InitializeSvgIconRegistry(Icons, Host))
    {
        fprintf(stderr, "[texturepaint-workspace] SVG icon registry failed to start\n");
    }
    else
    {
        const bool GlobalOk = RegisterGlobalIconPack(Icons);
        const bool PaintOk  = RegisterPaintIconPack(Icons);
        if (!GlobalOk || !PaintOk)
        {
            fprintf(stderr, "[texturepaint-workspace] icon pack registration incomplete (global=%d paint=%d)\n",
                    (int)GlobalOk, (int)PaintOk);
        }
    }

    PaintIconStore StripStore;
    if (!InitializePaintIconStore(StripStore, Host, PaintStripShortEdge))
    {
        fprintf(stderr, "[texturepaint-workspace] strip store failed to start; wells fall back to the nib crop\n");
    }

    // -- The caller-owned workspace state, seeded to the combined opening pose -------------------------------------------
    TexturePaintWorkspaceValidation::TexturePaintWorkspaceState State;
    TexturePaintWorkspaceValidation::InitializeTexturePaintWorkspaceSample(State);

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

        const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(MainViewport->WorkPos);
        ImGui::SetNextWindowSize(MainViewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("Texture Paint Workspace", nullptr, HostFlags))
        {
            TexturePaintWorkspaceValidation::ConstructTexturePaintWorkspacePanel(Theme, State, &Icons, &StripStore);
        }
        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::Render();
        SubmitAndPresentImguiFrame(Interface, Host, ImGui::GetDrawData());
    }

    // -- Teardown (reverse of bring-up, each Vulkan step gated on device-idle) -------------------------------------------
    vkDeviceWaitIdle(Host.Device);

    FinalizePaintIconStore(StripStore);
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
