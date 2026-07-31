/*==============================================================================================================================================
                                                     PAINTTOOLVALIDATIONHOST.CPP
==============================================================================================================================================*/
// 🧩 Standalone Vulkan validation host for the two-slide texture-paint card ported from Documentation/Prototypes/PaintToolMenu.html. It stands up
//    the shared Vulkan spine (PlatformWindow + VulkanHost + presentation surface + VulkanImguiInterface + the Win32 ImGui relay), then — once the
//    ImGui Vulkan backend and its font texture exist — brings up the SvgIconRegistry and this app's OWN paint pack plus its non-square strip store.
//    Each frame it drives ConstructPaintToolPanel inside one docked-full window, so the rail, the grid's two art modes, the carousel, all four
//    parameter widget kinds, the live-state reveals and both preview canvases can be exercised against the prototype side by side.
//
//    🔴 Two texture tiers, both gated on the ImGui Vulkan backend: the registry (square nib crops + parameter glyphs, registered up front) and the
//       strip store (the 5:1 landscape art, uploaded lazily). Both upload through ImGui_ImplVulkan_AddTexture, so both MUST come up after
//       ImGui_ImplVulkan_Init and go down before the backend shuts down. Bring-up and teardown are exact reverses, gated on device-idle.
//
//    📝 The prototype is a right-click context menu; here the card is persistent and centred, because a validation app has no scene to right-click
//       on. The open/close animation and both slides are still driven through the same state the menu drives.

#include "Platform/Windowing/PlatformWindow.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/RenderExtension/Device/VulkanImguiInterface.h"

#include "EngineContext/Interface/WorkspaceHost/ImguiPlatformRelay.h"
#include "EngineContext/Interface/Theme/ThemeResolver.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"
#include "EngineContext/Interface/Icons/IconPackGlobal.h"

#include "PaintCardSpecification.h"
#include "PaintCatalogue.h"
#include "PaintIconPack.h"
#include "PaintIconStore.h"
#include "PaintToolPanel.h"

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
    // 📝 The short-axis raster height every landscape strip is uploaded at. 64 px against the authored 300x60 box gives a 320 px
    //    long axis, which is above the 225 px the stand actually draws after its 1.5x grow — so the strip is minified, never
    //    magnified, at the one site that shows it.
    constexpr uint32_t PaintStripShortEdge = 64u;


    void ReportVkResult(VkResult Outcome)
    {
        if (Outcome != VK_SUCCESS && Outcome != VK_SUBOPTIMAL_KHR)
        {
            fprintf(stderr, "[vulkan] reported VkResult %d\n", (int)Outcome);
        }
    }


    //---------------------------------------------------- AUTHORING COLUMN ----------------------------------------------------

    // Draw the host's own controls. Validation scaffolding, not a ported surface — plain ImGui widgets on purpose, so nothing
    // about its appearance can be mistaken for part of the card under review.
    void InscribeAuthoringColumn(PaintToolPanelState& State)
    {
        ImGui::TextUnformatted("CARD");
        ImGui::Separator();
        ImGui::Spacing();

        if (State.Shell.IsOpen)
        {
            if (ImGui::Button("close", ImVec2(-1.0f, 0.0f))) { ClosePaintToolPanel(State); }
        }
        else
        {
            if (ImGui::Button("open", ImVec2(-1.0f, 0.0f))) { OpenPaintToolPanel(State, State.FamilyIndex); }
        }

        ImGui::TextUnformatted((State.Shell.Slide == PaintCardSlide::Options) ? "slide  options" : "slide  library");
        ImGui::Text("travel %.2f", State.Shell.SlidePhase);
        ImGui::Text("pop    %.2f", State.Shell.OpenPhase);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 🔴 The art switch. NOT in the prototype, which only ever calls NibArt at the well: it is the one control this host adds,
        //    so the 48x48 crop can be compared against the whole authored instrument on screen. Two RASTERS, not two samplings —
        //    see the note on PaintWellArtMode.
        ImGui::TextUnformatted("WELL ART");
        int ArtChoice = (State.ArtMode == PaintWellArtMode::FullStrip) ? 1 : 0;
        if (ImGui::RadioButton("nib crop", &ArtChoice, 0)) { State.ArtMode = PaintWellArtMode::NibCrop; }
        if (ImGui::RadioButton("full strip", &ArtChoice, 1)) { State.ArtMode = PaintWellArtMode::FullStrip; }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("SELECTION");

        int FamilyCount = 0;
        const PaintFamilyDescriptor* Families = ResolvePaintFamilies(FamilyCount);
        if (State.FamilyIndex >= 0 && State.FamilyIndex < FamilyCount)
        {
            ImGui::Text("family %s", Families[State.FamilyIndex].Caption);
        }

        int InstrumentCount = 0;
        const PaintInstrumentDescriptor* Instruments = ResolvePaintInstruments(InstrumentCount);
        if (State.InstrumentIndex >= 0 && State.InstrumentIndex < InstrumentCount)
        {
            ImGui::TextWrapped("%s", Instruments[State.InstrumentIndex].Label);
        }
        else
        {
            ImGui::TextDisabled("no instrument");
        }

        ImGui::Spacing();
        ImGui::Text("groups %d", State.Schema.GroupCount);
        ImGui::Text("seeded %d", State.ValueCount);
        ImGui::Text("swatch %d", State.Preview.SwatchIndex);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("the card is centred; scroll a pane with the wheel");
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
    if (!InitializePlatformWindow(Window, "Frontier \xE2\x80\x94 Paint Tool Card Validation", 1280, 900))
    {
        fprintf(stderr, "[paintcard-validation] window creation failed\n");
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
        fprintf(stderr, "[paintcard-validation] surface creation failed\n");
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

    // 📝 The theme is resolved once; the card resolves its own palette and metrics from it, so the authoring column and the card
    //    under review scale together.
    const ThemeConfiguration Theme    = ResolveActiveTheme();
    EnforceThemeStyle(Theme);
    const PaintCardPalette   Palette  = ResolvePaintCardPalette(Theme);
    const PaintCardMetrics   Metrics  = ResolvePaintCardMetrics(Theme);

    // -- Texture tiers (both need the ImGui Vulkan backend live) ----------------------------------------------------------
    SvgIconRegistry Icons;
    if (!InitializeSvgIconRegistry(Icons, Host))
    {
        fprintf(stderr, "[paintcard-validation] SVG icon registry failed to start\n");
    }
    else
    {
        const bool GlobalOk = RegisterGlobalIconPack(Icons);
        const bool PaintOk  = RegisterPaintIconPack(Icons);
        if (!GlobalOk || !PaintOk)
        {
            fprintf(stderr, "[paintcard-validation] icon pack registration incomplete (global=%d paint=%d)\n",
                    (int)GlobalOk, (int)PaintOk);
        }
    }

    PaintIconStore StripStore;
    if (!InitializePaintIconStore(StripStore, Host, PaintStripShortEdge))
    {
        fprintf(stderr, "[paintcard-validation] strip store failed to start; wells fall back to the nib crop\n");
    }

    // -- The caller-owned card state, opened on the prototype's first band --------------------------------------------------
    PaintToolPanelState State;
    OpenPaintToolPanel(State, 0);

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
        if (ImGui::Begin("Paint Tool Card", nullptr, HostFlags))
        {
            const float Scale          = (Theme.Metrics.UiScale > 0.0f) ? Theme.Metrics.UiScale : 1.0f;
            const float AuthoringWidth = 210.0f * Scale;

            ImGui::BeginChild("PaintAuthoring", ImVec2(AuthoringWidth, 0.0f), ImGuiChildFlags_Borders);
            InscribeAuthoringColumn(State);
            ImGui::EndChild();

            // 🔴 The card is built at WINDOW scope, after the authoring child closes. It draws with the window draw list into a
            //    clip it pushes itself, and the panes hit-test manually against that clip — an active BeginChild would confine
            //    both to the child's own rectangle.
            const ImVec2 FieldMinimum(ImGui::GetWindowPos().x + AuthoringWidth, ImGui::GetWindowPos().y);
            const ImVec2 FieldSize(ImGui::GetWindowSize().x - AuthoringWidth, ImGui::GetWindowSize().y);
            const ImVec2 CardCentre(FieldMinimum.x + FieldSize.x * 0.5f, FieldMinimum.y + FieldSize.y * 0.5f);

            ConstructPaintToolPanel(State, Palette, Metrics, &Icons, &StripStore, CardCentre);
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
