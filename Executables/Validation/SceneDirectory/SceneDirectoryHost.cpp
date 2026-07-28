/*==============================================================================================================================================
                                                            SCENEDIRECTORYHOST.CPP
==============================================================================================================================================*/
// 🧩 Standalone Vulkan validation host for the SceneDirectoryPanel (Outliner). It stands up the shared Vulkan spine (PlatformWindow + VulkanHost +
//    presentation surface + VulkanImguiInterface + the Win32 ImGui relay) exactly as the SketchOutliner validation does, then — once the ImGui
//    Vulkan backend and its font texture exist — brings up the SvgIconRegistry against the same host and registers the global (g-) + scene (scene-)
//    icon tiers so every row draws its real multi-colour SVG glyph. It builds one caller-owned SceneDirectoryState through
//    InitializeSceneDirectorySample, then each frame drives ConstructSceneDirectoryPanel inside one full-viewport window so a human can exercise
//    selection, twisties, inline rename, the eye toggle, drag relocation, and the search + chip filters. Bring-up and teardown are the reverse of
//    each other, every Vulkan step gated on device-idle. It writes its own Binaries\Validation\SceneDirectory.exe.
//
//    NOTE: this host was ported from a Win32 + D3D11 backend to native Vulkan so it could reuse the Vulkan-only SvgIconRegistry — the same registry
//    the CAD outliner uses — instead of re-drawing procedural line art. That is why the real SVG icons now render as brightly as IconGallery.html.

#include "Platform/Windowing/PlatformWindow.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/RenderExtension/Device/VulkanImguiInterface.h"

#include "EngineContext/Interface/WorkspaceHost/ImguiPlatformRelay.h"
#include "EngineContext/Interface/Theme/ThemeResolver.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"
#include "EngineContext/Interface/Icons/IconPackGlobal.h"
#include "EngineContext/Interface/Icons/IconPackScene.h"

#include "SceneDirectoryPanel.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include <cstdint>
#include <cstdio>

using namespace Frontier;
using SceneDirectoryValidation::SceneDirectoryState;
using SceneDirectoryValidation::InitializeSceneDirectorySample;
using SceneDirectoryValidation::ConstructSceneDirectoryPanel;

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

    // 📝 Unpack a straight-alpha ImU32 into a 4-float RGBA clear colour (0-1). Used to clear with the theme's desk background.
    void UnpackClearColor(ImU32 Packed, float Out[4])
    {
        Out[0] = ((Packed >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
        Out[1] = ((Packed >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
        Out[2] = ((Packed >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
        Out[3] = 1.0f;
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
    if (!InitializePlatformWindow(Window, "Frontier \xE2\x80\x94 Scene Directory", 520, 1000))
    {
        fprintf(stderr, "[scene-directory] window creation failed\n");
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
        fprintf(stderr, "[scene-directory] surface creation failed\n");
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

    // -- ImGui context + backends ---------------------------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    Io.IniFilename = nullptr;   // don't litter an imgui.ini next to the exe — this is a throwaway validation

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

    // 📝 Resolve the shared theme once and mirror it into ImGui's style so nested raw widgets inherit the palette.
    const ThemeConfiguration Theme = ResolveActiveTheme();
    EnforceThemeStyle(Theme);

    // 📝 Clear to the theme desk background; the outliner window draws over it.
    UnpackClearColor(Theme.Palette.DeskBackground, Interface.Window.ClearValue.color.float32);

    // -- Icon registry (needs the ImGui Vulkan backend live: it uploads through ImGui_ImplVulkan_AddTexture) --------------
    SvgIconRegistry Icons;
    bool IconsReady = InitializeSvgIconRegistry(Icons, Host);
    if (!IconsReady)
    {
        fprintf(stderr, "[scene-directory] SVG icon registry failed to start (falling back to procedural glyphs)\n");
    }
    else
    {
        const bool GlobalOk = RegisterGlobalIconPack(Icons);
        const bool SceneOk  = RegisterSceneIconPack(Icons);
        if (!GlobalOk || !SceneOk)
        {
            fprintf(stderr, "[scene-directory] icon pack registration incomplete (global=%d scene=%d)\n",
                    (int)GlobalOk, (int)SceneOk);
        }
    }
    const SvgIconRegistry* IconRegistry = IconsReady ? &Icons : nullptr;

    // -- The caller-owned panel state + its default demonstration tree ---------------------------------------------------
    SceneDirectoryState State;
    InitializeSceneDirectorySample(State);

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

        // 📝 One full-viewport window hosting the outliner so it reads like a real docked panel.
        const ImGuiViewport* Viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(Viewport->WorkPos);
        ImGui::SetNextWindowSize(Viewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme.Palette.DeskBackground);
        const ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("Scene Directory", nullptr, HostFlags))
        {
            ConstructSceneDirectoryPanel(Theme, State, IconRegistry);
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::Render();
        SubmitAndPresentImguiFrame(Interface, Host, ImGui::GetDrawData());
    }

    // -- Teardown (reverse of bring-up, each Vulkan step gated on device-idle) -------------------------------------------
    vkDeviceWaitIdle(Host.Device);

    if (IconsReady) { FinalizeSvgIconRegistry(Icons); }

    ImGui_ImplVulkan_Shutdown();
    DetachImguiPlatform(Window);
    ImGui::DestroyContext();

    FinalizeVulkanImguiInterface(Interface, Host);
    vkDestroySurfaceKHR(Host.Instance, Window.PresentationSurface, Host.Allocator);
    FinalizeVulkanHost(Host);
    FinalizePlatformWindow(Window);
    return 0;
}
