/*==============================================================================================================================================
                                                       TEXTUREPAINTLAYERSTACKHOST.CPP
==============================================================================================================================================*/
// 🧩 Standalone Vulkan validation host for the texture-paint layer stack rail. It stands up the shared Vulkan spine (PlatformWindow + VulkanHost +
//    presentation surface + VulkanImguiInterface + the Win32 ImGui relay), then — once the ImGui Vulkan backend and its font texture exist — brings
//    up the SvgIconRegistry against the same host and registers the global "g-" tier plus the new "paint-" tier the rail's category glyphs come
//    from. It seeds one caller-owned LayerStackPanelState through InitializeLayerStackSample and each frame drives ConstructLayerStackPanel inside a
//    single docked-full window, so a human can exercise add (4 categories), focus, inline rename, the visibility eye, the opacity scrub, the
//    blend-mode popup, reorder drag, delete and the 12-layer cap.
//
// 📝 This is a UI-ONLY port: nothing here allocates a paint atlas or composites anything. The rail records what the user asked for; the paint
//    extension performs it once wired.
//
//    🔴 The rail is drawn in a NARROW column against a dark slate, not stretched across the whole window. A rail stretched to 1200 px hides
//       exactly the defects this validation exists to find — clipped names, a collapsed meta line, hit zones that drift apart — because at that
//       width everything fits no matter how the geometry is computed.

#include "Platform/Windowing/PlatformWindow.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/RenderExtension/Device/VulkanImguiInterface.h"

#include "EngineContext/Interface/WorkspaceHost/ImguiPlatformRelay.h"
#include "EngineContext/Interface/Theme/ThemeResolver.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"
#include "EngineContext/Interface/Icons/IconPackGlobal.h"
#include "EngineContext/Interface/Icons/IconPackPaint.h"

#include "EngineContext/Interface/Workspaces/TexturePaint/LayerStackPanel.h"

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
    // 📝 The rail's authored width. The prototype's stack rail sits in a fixed-width inspector column, so the port is validated at the width it
    //    will actually ship at.
    constexpr float RailWidth = 340.0f;   // [px] - Width of the layer-stack column

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
    if (!InitializePlatformWindow(Window, "Frontier \xE2\x80\x94 Texture Paint Layer Stack", 1100, 900))
    {
        fprintf(stderr, "[layerstack-validation] window creation failed\n");
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
        fprintf(stderr, "[layerstack-validation] surface creation failed\n");
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

    // 📝 Clear to a near-black slate; the rail column draws over it.
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

    // 📝 The SAME resolved theme ControlsGallery draws from, mirrored into ImGui's style. The rail carries no palette of its own, so the styling
    //    the user asked to reuse follows from this one call.
    const ThemeConfiguration Theme = ResolveActiveTheme();
    EnforceThemeStyle(Theme);

    // -- Icon registry (needs the ImGui Vulkan backend live: it uploads through ImGui_ImplVulkan_AddTexture) --------------
    SvgIconRegistry Icons;
    if (!InitializeSvgIconRegistry(Icons, Host))
    {
        fprintf(stderr, "[layerstack-validation] SVG icon registry failed to start\n");
    }
    else
    {
        const bool GlobalOk = RegisterGlobalIconPack(Icons);
        const bool PaintOk  = RegisterPaintIconPack(Icons);
        if (!GlobalOk || !PaintOk)
        {
            fprintf(stderr, "[layerstack-validation] icon pack registration incomplete (global=%d paint=%d)\n",
                    (int)GlobalOk, (int)PaintOk);
        }
    }

    // -- The caller-owned rail state + its seeded opening document ---------------------------------------------------------
    LayerStackPanelState State;
    InitializeLayerStackSample(State);

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

        // 📝 One docked-full backdrop window; the rail occupies a fixed-width column on the left so it is exercised at its shipping width.
        const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(MainViewport->WorkPos);
        ImGui::SetNextWindowSize(MainViewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("Texture Paint Layer Stack", nullptr, HostFlags))
        {
            ImGui::BeginChild("##railcolumn", ImVec2(RailWidth, 0.0f), false);
            ConstructLayerStackPanel(Theme, State, &Icons);
            ImGui::EndChild();

            // 📝 A live readout of what the rail decided, beside it. Focus is the one piece of rail state a caller MUST read (it is where a stroke
            //    would land), and showing it here proves the panel's contract — including that it does NOT silently fall back to the top layer.
            ImGui::SameLine();
            ImGui::BeginChild("##focusreadout", ImVec2(0.0f, 0.0f), false);
            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            ImGui::Indent(16.0f);

            const PaintLayerEntry* Focused = ResolveFocusedLayer(State);
            if (Focused != nullptr)
            {
                ImGui::Text("Focus: %s", Focused->Label.c_str());
                ImGui::Text("Category: %s", ResolveLayerCategoryLabel(Focused->Category));
                ImGui::Text("Blend: %s", ResolveBlendModeTable()[Focused->BlendOrdinal]);
                ImGui::Text("Opacity: %d%%", (int)(Focused->Opacity * 100.0f + 0.5f));
                ImGui::Text("Concealed: %s", Focused->ConcealedState ? "yes" : "no");
            }
            else
            {
                ImGui::TextUnformatted("Focus: none \xE2\x80\x94 a stroke would be refused.");
            }

            ImGui::Unindent(16.0f);
            ImGui::EndChild();
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
