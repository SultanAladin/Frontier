/*==============================================================================================================================================
                                                        SKETCHMODELVIEWPORTHOST.CPP
==============================================================================================================================================*/
// 🧩 Standalone Vulkan validation host for the sketch-model matcap viewport. It stands up the shared Vulkan spine (PlatformWindow + VulkanHost +
//    presentation surface + VulkanImguiInterface + the Win32 ImGui relay) exactly as the SketchOutliner validation does, resolves the shared theme,
//    and brings up the SvgIconRegistry once the ImGui Vulkan backend + font texture exist so the header/footer icons can rasterize. It builds one
//    caller-owned SketchModelViewportState (a perspective viewport with grid + axis + the SpatialCompass overlay) and each cycle records
//    ConstructSketchModelViewportPanel inside a single docked-full ImGui window so a human can orbit/pan/dolly and exercise the compass. Phase 1
//    runs the GPU analytic ground grid (GroundGridPass) into an OFFSCREEN colour surface sized to the framebuffer, submitted on a one-shot command
//    buffer + fence BEFORE the ImGui frame, then feeds that surface's ImGui texture id into the panel so ConstructViewportPanel blits the real GPU
//    grid instead of the ImDrawList placeholder. It writes its OWN exe to Binaries\Validation\SketchModelViewport.exe. Bring-up and teardown are the
//    reverse of each other, every Vulkan step gated on device-idle.

#include "Platform/Windowing/PlatformWindow.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/RenderExtension/Device/VulkanImguiInterface.h"
#include "Graphics/Grid/GroundGridPass.h"

#include "EngineContext/Interface/WorkspaceHost/ImguiPlatformRelay.h"
#include "EngineContext/Interface/Theme/ThemeResolver.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"
#include "EngineContext/Interface/Icons/IconPackGlobal.h"
#include "EngineContext/Interface/Icons/IconPackCad.h"

#include "SketchModelViewportPanel.h"
#include "SketchModelOffscreenSurface.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include <cstdint>
#include <cstdio>

using namespace Frontier;
using SketchModelViewportValidation::SketchModelViewportState;
using SketchModelViewportValidation::SketchModelOffscreenSurface;
using SketchModelViewportValidation::InitializeSketchModelViewportSample;
using SketchModelViewportValidation::ConformSketchModelAspect;
using SketchModelViewportValidation::AssembleSketchModelGridConstants;
using SketchModelViewportValidation::InitializeSketchModelOffscreenSurface;
using SketchModelViewportValidation::QuerySketchModelOffscreenRebuildRequired;
using SketchModelViewportValidation::ResolveSketchModelOffscreenTexture;
using SketchModelViewportValidation::RecordSketchModelOffscreenSurface;
using SketchModelViewportValidation::FinalizeSketchModelOffscreenSurface;
using SketchModelViewportValidation::ConstructSketchModelViewportPanel;

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

    // 📝 The context RecordSketchModelOffscreenSurface hands its record callback: the built grid pass + the frame's push constants.
    struct GridRecordContext
    {
        const GroundGridPass*      Pass;        // [-] - The built analytic-grid pipeline
        const GroundGridConstants* Constants;   // [-] - This frame's push data (derived from the bridged camera)
    };

    // 📝 Draw the analytic grid into the already-open dynamic-rendering scope the offscreen surface opened for us.
    void RecordGridDraw(VkCommandBuffer CommandBuffer, VkExtent2D Extent, void* Context)
    {
        const GridRecordContext* Payload = static_cast<const GridRecordContext*>(Context);
        RecordGroundGridPass(*Payload->Pass, CommandBuffer, Extent, *Payload->Constants);
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
    if (!InitializePlatformWindow(Window, "Frontier \xE2\x80\x94 Sketch Model Viewport", 1280, 900))
    {
        fprintf(stderr, "[sketchmodel-viewport] window creation failed\n");
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
        fprintf(stderr, "[sketchmodel-viewport] surface creation failed\n");
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

    // 📝 Clear to a near-black slate; the viewport window draws over it.
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
        fprintf(stderr, "[sketchmodel-viewport] SVG icon registry failed to start\n");
    }
    else
    {
        const bool GlobalOk = RegisterGlobalIconPack(Icons);
        const bool CadOk    = RegisterCadIconPack(Icons);
        if (!GlobalOk || !CadOk)
        {
            fprintf(stderr, "[sketchmodel-viewport] icon pack registration incomplete (global=%d cad=%d)\n",
                    (int)GlobalOk, (int)CadOk);
        }
    }

    // -- The caller-owned panel state (perspective viewport + grid + axis + compass) -------------------------------------
    SketchModelViewportState State;
    InitializeSketchModelViewportSample(State);

    // -- GPU analytic ground grid (loads AnalyticGroundPlane.{vert,frag}.spv from the staged "Shaders" dir) ---------------
    //    The offscreen surface uses B8G8R8A8_UNORM (its struct default), so the grid pipeline is built for that same format.
    if (!InitializeGroundGridPass(State.Grid, Host, State.Surface.Format, "Shaders"))
    {
        fprintf(stderr, "[sketchmodel-viewport] ground grid pass unavailable — viewport shows the placeholder grid\n");
    }

    // -- One-shot command pool + fence the offscreen grid records through, BEFORE each ImGui frame -----------------------
    VkCommandPool GridCommandPool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo GridPoolInformation = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    GridPoolInformation.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    GridPoolInformation.queueFamilyIndex = Host.GraphicsQueueFamily;
    vkCreateCommandPool(Host.Device, &GridPoolInformation, Host.Allocator, &GridCommandPool);

    VkCommandBuffer GridCommandBuffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo GridCommandInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    GridCommandInformation.commandPool        = GridCommandPool;
    GridCommandInformation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    GridCommandInformation.commandBufferCount = 1;
    vkAllocateCommandBuffers(Host.Device, &GridCommandInformation, &GridCommandBuffer);

    VkFence GridFence = VK_NULL_HANDLE;
    VkFenceCreateInfo GridFenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    vkCreateFence(Host.Device, &GridFenceInformation, Host.Allocator, &GridFence);

    // -- Frame loop -----------------------------------------------------------------------------------------------------
    while (!QueryWindowCloseRequested(Window))
    {
        PollPlatformEvents(Window);

        if (!BeginImguiFrame(Interface, Host, Window))
        {
            continue;   // minimized / zero-extent / stale swapchain — the interface already flagged any rebuild
        }

        // -- Draw the GPU grid into the offscreen surface, sized to the CANVAS the bands left over ------------------------
        //    📝 The panel reports its canvas extent each cycle (framebuffer minus the 52 px + 30 px bands). On the very first
        //       cycle no panel has run yet, so fall back to the framebuffer extent; it is corrected from cycle two onward.
        uint32_t CanvasWidth  = State.CanvasWidth;
        uint32_t CanvasHeight = State.CanvasHeight;
        if (CanvasWidth == 0u || CanvasHeight == 0u)
        {
            QueryFramebufferExtent(Window, CanvasWidth, CanvasHeight);
        }
        if (State.Grid.ReadyCondition && CanvasWidth > 0 && CanvasHeight > 0)
        {
            if (QuerySketchModelOffscreenRebuildRequired(State.Surface, CanvasWidth, CanvasHeight))
            {
                vkDeviceWaitIdle(Host.Device);   // the ImGui backend may still hold the old descriptor set this frame
                InitializeSketchModelOffscreenSurface(State.Surface, Host, CanvasWidth, CanvasHeight, State.Surface.Format);
            }

            ConformSketchModelAspect(State, CanvasWidth, CanvasHeight);
            const GroundGridConstants GridConstants = AssembleSketchModelGridConstants(State);
            GridRecordContext RecordPayload = { &State.Grid, &GridConstants };

            // 📝 Record + submit the grid on its own one-shot buffer and WAIT the fence, so the surface is in SHADER_READ_ONLY
            //    before ImGui samples it this same frame. A stall, but correct + simple for a validation host.
            vkResetCommandBuffer(GridCommandBuffer, 0);
            VkCommandBufferBeginInfo GridBegin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            GridBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(GridCommandBuffer, &GridBegin);

            const float GridClear[4] = { 0.06f, 0.07f, 0.09f, 1.0f };
            RecordSketchModelOffscreenSurface(State.Surface, Host, GridCommandBuffer, GridClear, &RecordGridDraw, &RecordPayload);

            vkEndCommandBuffer(GridCommandBuffer);

            VkSubmitInfo GridSubmit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            GridSubmit.commandBufferCount = 1;
            GridSubmit.pCommandBuffers    = &GridCommandBuffer;
            vkResetFences(Host.Device, 1, &GridFence);
            if (vkQueueSubmit(Host.GraphicsQueue, 1, &GridSubmit, GridFence) == VK_SUCCESS)
            {
                vkWaitForFences(Host.Device, 1, &GridFence, VK_TRUE, UINT64_MAX);
                State.Viewport.RenderedTexture = ResolveSketchModelOffscreenTexture(State.Surface);
            }
        }

        ImGui_ImplVulkan_NewFrame();
        AdvanceImguiPlatform();
        ImGui::NewFrame();

        // 📝 One docked-full window hosting the viewport panel. The panel owns its own camera, grid, and compass overlay.
        const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(MainViewport->WorkPos);
        ImGui::SetNextWindowSize(MainViewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("Sketch Model Viewport", nullptr, HostFlags))
        {
            ConstructSketchModelViewportPanel(Theme, Icons, State);
        }
        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::Render();
        SubmitAndPresentImguiFrame(Interface, Host, ImGui::GetDrawData());
    }

    // -- Teardown (reverse of bring-up, each Vulkan step gated on device-idle) -------------------------------------------
    vkDeviceWaitIdle(Host.Device);

    FinalizeSketchModelOffscreenSurface(State.Surface, Host);
    FinalizeGroundGridPass(State.Grid, Host);
    if (GridFence       != VK_NULL_HANDLE) vkDestroyFence(Host.Device, GridFence, Host.Allocator);
    if (GridCommandPool != VK_NULL_HANDLE) vkDestroyCommandPool(Host.Device, GridCommandPool, Host.Allocator);

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
