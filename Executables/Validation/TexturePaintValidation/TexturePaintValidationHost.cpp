/*==============================================================================================================================================
                                                     TEXTUREPAINTVALIDATIONHOST.CPP
==============================================================================================================================================*/
// 🧩 Standalone Vulkan validation host for the texture-paint surface. It stands up the shared Vulkan spine (PlatformWindow + VulkanHost +
//    presentation surface + VulkanImguiInterface + the Win32 ImGui relay), then — once the ImGui Vulkan backend and its font texture exist —
//    brings up the SvgIconRegistry plus the paint pack and the non-square strip store the instrument wells read. Each frame it draws one flat
//    paint field and drives the summoned instrument card over it, so the RIGHT-CLICK summon, the placement clamp, the dismiss and the whole
//    embedded card can be exercised as the workspace will actually present them.
//
//    🔴 The card is EMBEDDED, not re-implemented: every unit behind it (the card, its generated catalogue, its icon pack, its strip store) is
//       PaintToolValidation's own, compiled IN PLACE from that sibling folder by this app's Build.bat. That app's card lives outside
//       EngineContext.lib on purpose — the strip store needs a rectangular raster the shared SvgIconRegistry does not do — so in-place
//       compilation is the only way to embed it without perturbing the cards that already ship.
//
//    🔴 Two texture tiers, both gated on the ImGui Vulkan backend: the registry (square nib crops + parameter glyphs, registered up front) and
//       the strip store (the 5:1 landscape art, uploaded lazily). Both upload through ImGui_ImplVulkan_AddTexture, so both MUST come up after
//       ImGui_ImplVulkan_Init and go down before the backend shuts down. Bring-up and teardown are exact reverses, gated on device-idle.
//
//    📝 Unlike PaintToolValidation — where the card is persistent and centred because a card-only validation has nothing to right-click on —
//       here the card is closed at rest and summoned at the cursor. That is the difference this app exists to validate.

#include "Platform/Windowing/PlatformWindow.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/RenderExtension/Device/VulkanImguiInterface.h"

#include "EngineContext/Interface/WorkspaceHost/ImguiPlatformRelay.h"
#include "EngineContext/Interface/Theme/ThemeResolver.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"
#include "EngineContext/Interface/Icons/IconPackGlobal.h"

#include "PaintCatalogue.h"
#include "PaintIconPack.h"
#include "PaintIconStore.h"

#include "TexturePaintSummonedCard.h"

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


    //-------------------------------------------------- THE PAINT FIELD --------------------------------------------------

    // Draw the flat field the card is summoned over, and the one hint line that says how. Validation scaffolding, not a ported surface — a plain
    // filled rectangle on purpose, so nothing about its appearance can be mistaken for part of the card under review.
    // 📝 Returns nothing: the field fills the host window, so the caller already knows its rectangle and confines the gesture to it directly.
    void InscribePaintField(ImVec2 FieldOrigin, ImVec2 FieldSpan, bool CardSummoned)
    {
        ImDrawList* Canvas = ImGui::GetWindowDrawList();

        const ImVec2 FieldMaximum(FieldOrigin.x + FieldSpan.x, FieldOrigin.y + FieldSpan.y);
        Canvas->AddRectFilled(FieldOrigin, FieldMaximum, IM_COL32(18, 19, 23, 255));

        // 📝 A faint centre crosshair, so the field reads as a surface with a location rather than a flat void — it is the only way to see that
        //    the summoned card is placed at the CURSOR and not at some fixed point.
        const ImVec2 FieldCentre(FieldOrigin.x + FieldSpan.x * 0.5f, FieldOrigin.y + FieldSpan.y * 0.5f);
        const float  ArmLength = 9.0f;                                     // [px] - half-span of each crosshair arm
        Canvas->AddLine(ImVec2(FieldCentre.x - ArmLength, FieldCentre.y), ImVec2(FieldCentre.x + ArmLength, FieldCentre.y), IM_COL32(255, 255, 255, 28));
        Canvas->AddLine(ImVec2(FieldCentre.x, FieldCentre.y - ArmLength), ImVec2(FieldCentre.x, FieldCentre.y + ArmLength), IM_COL32(255, 255, 255, 28));

        if (!CardSummoned)
        {
            const char* Hint = "right-click the field to summon the instrument card";
            const ImVec2 HintSpan = ImGui::CalcTextSize(Hint);
            Canvas->AddText(ImVec2(FieldCentre.x - HintSpan.x * 0.5f, FieldCentre.y + 24.0f), IM_COL32(255, 255, 255, 64), Hint);
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
    if (!InitializePlatformWindow(Window, "Frontier \xE2\x80\x94 Texture Paint Validation", 1280, 900))
    {
        fprintf(stderr, "[texturepaint-validation] window creation failed\n");
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
        fprintf(stderr, "[texturepaint-validation] surface creation failed\n");
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

    // 📝 Clear to the prototype's own desk colour, so the card sits on the backdrop it was designed against.
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

    // 📝 The theme is resolved once; the card resolves its own palette and metrics from it every frame, so the field and the card scale together.
    const ThemeConfiguration Theme = ResolveActiveTheme();
    EnforceThemeStyle(Theme);

    // -- Texture tiers (both need the ImGui Vulkan backend live) ----------------------------------------------------------
    SvgIconRegistry Icons;
    if (!InitializeSvgIconRegistry(Icons, Host))
    {
        fprintf(stderr, "[texturepaint-validation] SVG icon registry failed to start\n");
    }
    else
    {
        const bool GlobalOk = RegisterGlobalIconPack(Icons);
        const bool PaintOk  = RegisterPaintIconPack(Icons);
        if (!GlobalOk || !PaintOk)
        {
            fprintf(stderr, "[texturepaint-validation] icon pack registration incomplete (global=%d paint=%d)\n",
                    (int)GlobalOk, (int)PaintOk);
        }
    }

    PaintIconStore StripStore;
    if (!InitializePaintIconStore(StripStore, Host, PaintStripShortEdge))
    {
        fprintf(stderr, "[texturepaint-validation] strip store failed to start; wells fall back to the nib crop\n");
    }

    // -- The caller-owned summon state, seeded closed --------------------------------------------------------------------
    TexturePaintValidation::TexturePaintSummonedState Summoned;
    TexturePaintValidation::InitializeTexturePaintSummonedCard(Summoned);

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
        if (ImGui::Begin("Texture Paint", nullptr, HostFlags))
        {
            const ImVec2 FieldOrigin = ImGui::GetWindowPos();
            const ImVec2 FieldSpan   = ImGui::GetWindowSize();

            InscribePaintField(FieldOrigin, FieldSpan, Summoned.CardSummoned);

            // 🔴 The card is driven at WINDOW scope, with NO child open: it draws through the window draw list into a clip it pushes itself, and
            //    its panes hit-test manually against that clip — an active BeginChild would confine both to the child's rectangle.
            TexturePaintValidation::ConfineTexturePaintField(Summoned, FieldOrigin, FieldSpan);
            TexturePaintValidation::ConstructTexturePaintSummonedCard(Theme, Summoned, &Icons, &StripStore);
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
