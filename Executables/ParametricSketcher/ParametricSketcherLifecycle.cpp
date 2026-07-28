/*==============================================================================================================================================
                                                        PARAMETRICSKETCHERLIFECYCLE.CPP
==============================================================================================================================================*/
// 🧩 Parametric Sketcher entry point. Stands up one Vulkan window + ImGui interface (the shared spine every Vulkan app uses), clears to solid
//    black, and drives the SHARED WorkspacePanelDock (from EngineContext.lib) each frame. That panel owns the ENTIRE dock model itself (its own
//    host window, partition tree, trapezoid tab strip, (+) dropdown) — so main() does NOT enable ImGui docking and does NOT wrap it in a Begin;
//    it resolves the shared theme once, mirrors it into ImGui's style, and calls ConstructWorkspacePanelDock once per frame. Nothing else is drawn.

#include "Platform/Windowing/PlatformWindow.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/RenderExtension/Device/VulkanImguiInterface.h"

#include "EngineContext/Interface/WorkspaceHost/WorkspacePanelDock.h"
#include "EngineContext/Interface/WorkspaceHost/ImguiPlatformRelay.h"
#include "EngineContext/Interface/Theme/ThemeResolver.h"
#include "EngineContext/Interface/Icons/SvgIconRegistry.h"
#include "EngineContext/Interface/Icons/IconPackGlobal.h"
#include "EngineContext/Interface/Icons/IconPackCad.h"

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
            fprintf(stderr, "[vulkan] reported VkResult %d\n", (int)Outcome);
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
    if (!InitializePlatformWindow(Window, "Frontier \xE2\x80\x94 Parametric Sketcher", 1600, 900))
    {
        fprintf(stderr, "[sketcher] window creation failed\n");
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
        fprintf(stderr, "[sketcher] surface creation failed\n");
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

    // 📝 Clear to solid black each frame (--bg #000), matching the WorkspaceDock host; the dock chrome draws over it.
    Interface.Window.ClearValue.color.float32[0] = 0.00f;
    Interface.Window.ClearValue.color.float32[1] = 0.00f;
    Interface.Window.ClearValue.color.float32[2] = 0.00f;
    Interface.Window.ClearValue.color.float32[3] = 1.00f;

    // -- ImGui context + backends ---------------------------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    Io.IniFilename = nullptr;   // 📝 the dock owns its own layout state; no imgui.ini beside the exe

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

    // 📝 Resolve the shared theme once and mirror it into ImGui's style so the dock's nested widgets inherit the palette.
    const ThemeConfiguration Theme = ResolveActiveTheme();
    EnforceThemeStyle(Theme);

    // 📝 The parametric-sketch icon registry — brought up AFTER ImGui_ImplVulkan_Init (it uploads glyph textures through the live Vulkan backend).
    //    Both tiers register: the global g- pack + the cad- pack the outliner rows resolve. Threaded into the dock so any tab's Sketch Outliner box
    //    draws real SVG glyphs; a failed bring-up leaves the pointer usable (the outliner falls back to procedural strokes).
    SvgIconRegistry Icons;
    const bool IconsReady = InitializeSvgIconRegistry(Icons, Host);
    if (!IconsReady)
    {
        fprintf(stderr, "[sketcher] SVG icon registry failed to start — outliner rows fall back to procedural glyphs\n");
    }
    else
    {
        const bool GlobalOk = RegisterGlobalIconPack(Icons);
        const bool CadOk    = RegisterCadIconPack(Icons);
        if (!GlobalOk || !CadOk)
            fprintf(stderr, "[sketcher] icon pack registration incomplete (global=%d cad=%d)\n", (int)GlobalOk, (int)CadOk);
    }
    const SvgIconRegistry* IconRegistry = IconsReady ? &Icons : nullptr;

    // -- The shared interior dock (owns its own host window + partition tree + trapezoid strip) --------------------------
    WorkspacePanelDock Dock;
    InitializeWorkspacePanelDock(Dock);

    // 📝 A standalone editor opens only its own type; the (+) mints "Sketch 1", "Sketch 2", … and the boot tab reads "Sketch 1".
    const WorkspaceDocumentType SketchCatalogue[] = { { "Sketch", "Sketch", WorkspaceCategory::Draughting } };
    ConfigureWorkspaceCatalogue(Dock, SketchCatalogue, 1, 0);

    // -- Frame loop -----------------------------------------------------------------------------------------------------
    while (!QueryWindowCloseRequested(Window))
    {
        PollPlatformEvents(Window);

        if (!BeginImguiFrame(Interface, Host, Window))
            continue;   // minimized / zero-extent / stale swapchain — the interface already flagged any rebuild

        ImGui_ImplVulkan_NewFrame();
        AdvanceImguiPlatform();
        ImGui::NewFrame();

        // 📝 The whole UI: the shared dock, driven once per frame. It owns the host window + DockSpace + strip itself. The icon registry lets any
        //    tab's Sketch Outliner box resolve real CAD SVG glyphs.
        ConstructWorkspacePanelDock(Theme, Dock, IconRegistry);

        ImGui::Render();
        SubmitAndPresentImguiFrame(Interface, Host, ImGui::GetDrawData());
    }

    // -- Teardown (reverse of bring-up, each Vulkan step gated on device-idle) -------------------------------------------
    vkDeviceWaitIdle(Host.Device);

    if (IconsReady)
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
