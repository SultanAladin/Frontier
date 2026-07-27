/*==============================================================================================================================================
                                                       INSTRUMENTATIONVALIDATIONENTRY.CPP
==============================================================================================================================================*/
// 🧩 Standalone test bed for the modular Instrumentation overlay. Stands up a native Vulkan window + ImGui interface (the same spine the shared
//    workspace host uses), feeds a synthetic telemetry aggregate — a direct C++ port of LiveTelemetryScene.html's `T` model — through pull-
//    callbacks, and registers the five SegmentUI panels one-for-one with the mock: Render Report (metric grid + dot-matrix I/O), Live FPS (huge
//    readout + sparkline), Budget (threshold percent + caption), Frame Graph (dual-line series), and Memory Allocation (stacked bar + legend). One
//    EditorInstance bundles the whole per-run state (window, Vulkan host, ImGui interface, overlay extension, synthetic data); the loop records the
//    overlay every frame, proving an app builds the whole telemetry UI from the modular structs alone. The data-layer validation tally prints once.

#include "EngineContext/Interface/Instrumentation/InstrumentationExtension.h"
#include "EngineContext/Interface/Instrumentation/InstrumentationValidation.h"
#include "EngineContext/Interface/WorkspaceHost/ImguiPlatformRelay.h"

#include "Platform/Windowing/PlatformWindow.h"

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/RenderExtension/Device/VulkanImguiInterface.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include <cmath>
#include <cstdint>
#include <cstdio>


namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        SYNTHETIC TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

// 📝 The app's own numbers — a C++ port of the HTML's `T` model (frame time, GPU %, draws, rays, VRAM/RAM/disk, fps, and the
//    GPU/CPU frame-time series). The overlay never sees this type; it holds void* pointers to it and calls the retrievers
//    below once per card per paint. A real app swaps this aggregate for its live counters and the overlay is unchanged.
struct SyntheticTelemetry
{
    float ElapsedSeconds    = 0.0f;     // [s]    - advanced each frame; drives the synthetic waveforms
    float FrameMilliseconds = 16.6f;    // [ms]   - smoothed present time (metric grid + frame graph)
    float GpuPercent        = 62.0f;    // [%]    - GPU utilisation (metric grid)
    float DrawCalls         = 148.0f;   // [-]    - draw-call count (metric grid)
    float RaysMillions      = 4.3f;     // [M]    - rays per frame (metric grid)
    float VramMebibytes     = 1820.0f;  // [MiB]  - VRAM residency (matrix row + budget + alloc)
    float RamMebibytes      = 2560.0f;  // [MiB]  - RAM residency (matrix row)
    float DiskRate          = 12.0f;    // [MB/s] - disk throughput (matrix row)
    float FramesPerSecond   = 60.0f;    // [fps]  - live frame rate (fps readout)
    float GpuFrameMs        = 16.6f;    // [ms]   - GPU frame-time series (frame graph primary)
    float CpuFrameMs        = 15.0f;    // [ms]   - CPU frame-time series (frame graph secondary)
};

// 📝 The six allocation segments, mirroring the HTML's allocDefs. Values wander with VRAM so the stacked bar animates.
static const uint32_t AllocationSegmentCount = 6u;
struct AllocationSegment
{
    float Value = 0.0f;   // [MiB] - current segment size (its stacked-bar weight)
};
struct AllocationModel
{
    AllocationSegment Segments[AllocationSegmentCount];
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PULL RETRIEVERS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One retriever per signal — each reads one live figure from the opaque context and returns it. Cheap and non-blocking.
static float RetrieveFrameMilliseconds(void* Context){ return static_cast<SyntheticTelemetry*>(Context)->FrameMilliseconds; }
static float RetrieveGpuPercent(void* Context)       { return static_cast<SyntheticTelemetry*>(Context)->GpuPercent; }
static float RetrieveDrawCalls(void* Context)        { return static_cast<SyntheticTelemetry*>(Context)->DrawCalls; }
static float RetrieveRaysMillions(void* Context)     { return static_cast<SyntheticTelemetry*>(Context)->RaysMillions; }
static float RetrieveVramMebibytes(void* Context)    { return static_cast<SyntheticTelemetry*>(Context)->VramMebibytes; }
static float RetrieveRamMebibytes(void* Context)     { return static_cast<SyntheticTelemetry*>(Context)->RamMebibytes; }
static float RetrieveDiskRate(void* Context)         { return static_cast<SyntheticTelemetry*>(Context)->DiskRate; }
static float RetrieveFramesPerSecond(void* Context)  { return static_cast<SyntheticTelemetry*>(Context)->FramesPerSecond; }
static float RetrieveGpuFrameMs(void* Context)       { return static_cast<SyntheticTelemetry*>(Context)->GpuFrameMs; }
static float RetrieveCpuFrameMs(void* Context)       { return static_cast<SyntheticTelemetry*>(Context)->CpuFrameMs; }

// 📝 Budget: VRAM headroom of a 6 GiB tier, as a percent (matches the HTML's head = (1 - vram/6144)*100).
static const float VramCapacity = 6144.0f;
static float RetrieveVramHeadroom(void* Context)
{
    float Vram = static_cast<SyntheticTelemetry*>(Context)->VramMebibytes;
    float Head = (1.0f - Vram / VramCapacity) * 100.0f;
    if (Head < 0.0f) { Head = 0.0f; }
    return Head;
}

// 📝 Allocation segment retrievers — one per segment, reading the shared AllocationModel by index.
static AllocationModel GlobalAllocation;
static float RetrieveSegment0(void*){ return GlobalAllocation.Segments[0].Value; }
static float RetrieveSegment1(void*){ return GlobalAllocation.Segments[1].Value; }
static float RetrieveSegment2(void*){ return GlobalAllocation.Segments[2].Value; }
static float RetrieveSegment3(void*){ return GlobalAllocation.Segments[3].Value; }
static float RetrieveSegment4(void*){ return GlobalAllocation.Segments[4].Value; }
static float RetrieveSegment5(void*){ return GlobalAllocation.Segments[5].Value; }

// 📝 Advance the synthetic figures one frame. Pure math on ElapsedSeconds so the waveforms are deterministic and the app
//    needs no wall-clock — a real host writes its measured values here instead.
static void AdvanceSyntheticTelemetry(SyntheticTelemetry& Telemetry, float DeltaSeconds)
{
    Telemetry.ElapsedSeconds += DeltaSeconds;
    float Phase = Telemetry.ElapsedSeconds;

    Telemetry.FrameMilliseconds = 16.6f + 3.2f * std::sin(Phase * 1.1f) + 0.8f * std::sin(Phase * 5.7f);
    Telemetry.FramesPerSecond   = 1000.0f / Telemetry.FrameMilliseconds;
    Telemetry.GpuPercent        = 62.0f + 22.0f * std::sin(Phase * 0.6f);
    Telemetry.DrawCalls         = 148.0f + 12.0f * std::sin(Phase * 0.9f);
    Telemetry.RaysMillions      = 4.3f + 0.8f * std::sin(Phase * 1.4f);

    Telemetry.VramMebibytes     = 3200.0f + 1600.0f * std::sin(Phase * 0.35f);
    Telemetry.RamMebibytes      = 4200.0f + 1400.0f * std::sin(Phase * 0.27f);
    Telemetry.DiskRate          = 60.0f + 50.0f * std::sin(Phase * 0.8f);

    Telemetry.GpuFrameMs        = Telemetry.FrameMilliseconds;
    Telemetry.CpuFrameMs        = Telemetry.FrameMilliseconds * 0.9f + 1.2f * std::sin(Phase * 2.3f);

    // 📝 Allocation segments — the HTML pins Surface Cache to ~55% of VRAM and holds the rest steady with a little wander.
    GlobalAllocation.Segments[0].Value = Telemetry.VramMebibytes * 0.55f;                 // Surface Cache
    GlobalAllocation.Segments[1].Value = 204.0f + 20.0f * std::sin(Phase * 0.5f);         // Geometry
    GlobalAllocation.Segments[2].Value = 96.0f  + 12.0f * std::sin(Phase * 0.7f);         // SDF Field
    GlobalAllocation.Segments[3].Value = 120.0f + 16.0f * std::sin(Phase * 0.9f);         // Render Targets
    GlobalAllocation.Segments[4].Value = 210.0f + 24.0f * std::sin(Phase * 0.6f);         // Textures
    GlobalAllocation.Segments[5].Value = 134.0f + 14.0f * std::sin(Phase * 1.1f);         // Misc
}


//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One running instance of the standalone test bed — every piece of per-run state bundled so bring-up, the frame loop, and
//    teardown pass a single reference. Window + Host + Interface are the shared present spine (native window, Vulkan host,
//    crash-safe ImGui swapchain); Overlay is the modular telemetry extension; Telemetry is the app's synthetic data the pull-
//    callbacks read. Held by value in main and finalized in reverse. Not a coordinator noun — it names the one live run.
struct EditorInstance
{
    PlatformWindow           Window;      // [-] - native decorated window + WSI surface
    VulkanHost               Host;        // [-] - instance / device / graphics queue / ImGui descriptor pool
    VulkanImguiInterface     Interface;   // [-] - per-window swapchain + crash-safe ImGui frame lifecycle
    InstrumentationExtension Overlay;     // [-] - the modular telemetry-card overlay (record store + scratch)
    SyntheticTelemetry       Telemetry;   // [-] - the app's own numbers the pull-callbacks surface
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void ReportVkResult(VkResult Outcome)
    {
        if (Outcome != VK_SUCCESS && Outcome != VK_SUBOPTIMAL_KHR)
            fprintf(stderr, "[vulkan] backend reported VkResult %d\n", (int)Outcome);
    }

    // 📝 Copy a short label into an inline buffer, truncating to Capacity-1 and always terminating.
    void AssignLabel(char* Destination, uint32_t Capacity, const char* Source)
    {
        uint32_t Index = 0u;
        while (Source != nullptr && Source[Index] != '\0' && Index + 1u < Capacity)
        {
            Destination[Index] = Source[Index];
            ++Index;
        }
        Destination[Index] = '\0';
    }

    InstrumentColour MakeColour(uint8_t Red, uint8_t Green, uint8_t Blue)
    {
        InstrumentColour Colour;
        Colour.Red = Red; Colour.Green = Green; Colour.Blue = Blue; Colour.Alpha = 255u;
        return Colour;
    }

    // 📝 Fill one metric cell (dot tint + label + unit + decimals) of the render report.
    void AssignMetricCell(PanelMetricCell& Cell, InstrumentColour Dot, const char* Label, const char* Unit, uint8_t Decimals)
    {
        Cell.DotTint = Dot;
        AssignLabel(Cell.Label, PanelLabelCapacity, Label);
        AssignLabel(Cell.UnitSuffix, 8u, Unit);
        Cell.DecimalPlaces = Decimals;
    }

    // 📝 Fill one dot-matrix row (lit tint + label + capacity + unit) of the render report.
    void AssignMatrixRow(PanelMatrixRow& Row, InstrumentColour Lit, const char* Label, float Capacity, const char* Unit)
    {
        Row.LitTint = Lit;
        AssignLabel(Row.Label, PanelLabelCapacity, Label);
        Row.Capacity = Capacity;
        AssignLabel(Row.UnitSuffix, 8u, Unit);
    }

    // 📝 Fill one allocation segment (tint + name).
    void AssignSegment(PanelSegment& Segment, InstrumentColour Tint, const char* Label)
    {
        Segment.Tint = Tint;
        AssignLabel(Segment.Label, PanelLabelCapacity, Label);
    }

    // 📝 Build + register the five panels, one-for-one with LiveTelemetryScene.html. Each configures its static payload and
    //    binds its pull sources in the order its body pass expects (slot 0 = primary).
    void RegisterAllPanels(InstrumentationExtension& Overlay, SyntheticTelemetry& Telemetry)
    {
        // --- HTML palette ---
        InstrumentColour Green  = MakeColour(61, 220, 132);
        InstrumentColour Blue   = MakeColour(90, 108, 245);
        InstrumentColour Yellow = MakeColour(255, 210, 63);
        InstrumentColour Coral  = MakeColour(255, 90, 82);
        InstrumentColour Violet = MakeColour(123, 92, 255);
        InstrumentColour Orange = MakeColour(255, 157, 63);
        InstrumentColour Grey   = MakeColour(138, 138, 146);

        // --- (1) Render Report: 4 metric cells + 3 dot-matrix rows ---------------------------------------------------
        {
            InstrumentRegistration Report;
            Report.Title    = "Render Report";
            Report.Category = InstrumentRecordClassification::RenderReport;

            PanelStaticPayload& Payload = Report.Presentation.Payload;
            Payload.MetricCount = 4u;
            AssignMetricCell(Payload.MetricCells[0], Green,  "FRAME", "ms", 1u);
            AssignMetricCell(Payload.MetricCells[1], Blue,   "GPU",   "%",  0u);
            AssignMetricCell(Payload.MetricCells[2], Yellow, "DRAWS", "",   0u);
            AssignMetricCell(Payload.MetricCells[3], Coral,  "RAYS",  "M",  1u);

            Payload.MatrixCount = 3u;
            AssignMatrixRow(Payload.MatrixRows[0], Green,  "VRAM",     6144.0f, "MiB");
            AssignMatrixRow(Payload.MatrixRows[1], Blue,   "RAM",      8192.0f, "MiB");
            AssignMatrixRow(Payload.MatrixRows[2], Yellow, "DISK I/O", 120.0f,  "MB/s");

            // 📝 Sources in slot order: 4 metrics, then 3 matrix rows.
            Report.SignalCount = 7u;
            Report.Sources[0].Retrieve = RetrieveFrameMilliseconds; Report.Sources[0].Context = &Telemetry;
            Report.Sources[1].Retrieve = RetrieveGpuPercent;        Report.Sources[1].Context = &Telemetry;
            Report.Sources[2].Retrieve = RetrieveDrawCalls;         Report.Sources[2].Context = &Telemetry;
            Report.Sources[3].Retrieve = RetrieveRaysMillions;      Report.Sources[3].Context = &Telemetry;
            Report.Sources[4].Retrieve = RetrieveVramMebibytes;     Report.Sources[4].Context = &Telemetry;
            Report.Sources[5].Retrieve = RetrieveRamMebibytes;      Report.Sources[5].Context = &Telemetry;
            Report.Sources[6].Retrieve = RetrieveDiskRate;          Report.Sources[6].Context = &Telemetry;

            RegisterInstrumentCard(Overlay, Report);
        }

        // --- (2) Live FPS: huge readout + sparkline ------------------------------------------------------------------
        {
            InstrumentRegistration Fps;
            Fps.Title    = "Live FPS";
            Fps.Category = InstrumentRecordClassification::LiveReadout;
            Fps.Presentation.AccentPrimary = MakeColour(91, 255, 157);   // HTML --green-bright
            AssignLabel(Fps.Presentation.UnitSuffix, 8u, "fps");
            Fps.Presentation.DecimalPlaces = 0u;
            Fps.SignalCount = 1u;
            Fps.Sources[0].Retrieve = RetrieveFramesPerSecond; Fps.Sources[0].Context = &Telemetry;
            RegisterInstrumentCard(Overlay, Fps);
        }

        // --- (3) Budget: threshold percent + caption -----------------------------------------------------------------
        {
            InstrumentRegistration Budget;
            Budget.Title    = "Budget";
            Budget.Category = InstrumentRecordClassification::BudgetPercent;
            Budget.Presentation.AccentPrimary = Violet;
            AssignLabel(Budget.Presentation.Payload.Caption, PanelCaptionCapacity,
                        "VRAM headroom of the 6 GB tier remains for streaming.");
            Budget.SignalCount = 1u;
            Budget.Sources[0].Retrieve = RetrieveVramHeadroom; Budget.Sources[0].Context = &Telemetry;
            RegisterInstrumentCard(Overlay, Budget);
        }

        // --- (4) Frame Graph: dual-line series + date labels + legend ------------------------------------------------
        {
            InstrumentRegistration Graph;
            Graph.Title    = "Frame Graph";
            Graph.Category = InstrumentRecordClassification::FrameGraph;
            Graph.Presentation.AccentPrimary   = MakeColour(107, 123, 255); // HTML graph blue #6b7bff
            Graph.Presentation.AccentSecondary = Grey;
            AssignLabel(Graph.Presentation.UnitSuffix, 8u, "ms");
            Graph.Presentation.DecimalPlaces = 1u;

            PanelStaticPayload& Payload = Graph.Presentation.Payload;
            AssignLabel(Payload.Header, PanelLabelCapacity + 24u, "Frame Time  8s window");
            AssignLabel(Payload.PrimaryLegend, PanelLabelCapacity, "GPU frame");
            AssignLabel(Payload.SecondaryLegend, PanelLabelCapacity, "CPU frame");
            const char* Days[7] = { "6 Jul", "7 Jul", "8 Jul", "9 Jul", "10 Jul", "11 Jul", "12 Jul" };
            Payload.AxisCount = 7u;
            for (uint32_t Day = 0u; Day < 7u; ++Day)
            {
                AssignLabel(Payload.AxisLabels[Day], PanelLabelCapacity, Days[Day]);
            }

            Graph.SignalCount = 2u;
            Graph.Sources[0].Retrieve = RetrieveGpuFrameMs; Graph.Sources[0].Context = &Telemetry;
            Graph.Sources[1].Retrieve = RetrieveCpuFrameMs; Graph.Sources[1].Context = &Telemetry;
            RegisterInstrumentCard(Overlay, Graph);
        }

        // --- (5) Memory Allocation: stacked bar + legend -------------------------------------------------------------
        {
            InstrumentRegistration Alloc;
            Alloc.Title    = "Memory Allocation";
            Alloc.Category = InstrumentRecordClassification::AllocationBar;

            PanelStaticPayload& Payload = Alloc.Presentation.Payload;
            Payload.SegmentCount = AllocationSegmentCount;
            AssignSegment(Payload.Segments[0], Green,  "Surface Cache");
            AssignSegment(Payload.Segments[1], Blue,   "Geometry");
            AssignSegment(Payload.Segments[2], Violet, "SDF Field");
            AssignSegment(Payload.Segments[3], Coral,  "Render Targets");
            AssignSegment(Payload.Segments[4], Orange, "Textures");
            AssignSegment(Payload.Segments[5], Yellow, "Misc");

            SignalRetriever SegmentRetrievers[AllocationSegmentCount] =
            {
                RetrieveSegment0, RetrieveSegment1, RetrieveSegment2, RetrieveSegment3, RetrieveSegment4, RetrieveSegment5
            };
            Alloc.SignalCount = AllocationSegmentCount;
            for (uint32_t Index = 0u; Index < AllocationSegmentCount; ++Index)
            {
                Alloc.Sources[Index].Retrieve = SegmentRetrievers[Index];
                Alloc.Sources[Index].Context  = nullptr;   // 📝 segments read the file-scope GlobalAllocation
            }
            RegisterInstrumentCard(Overlay, Alloc);
        }
    }
}

}   // namespace Frontier


//------------------------------------------------------------------------------------------------------------------------
//                                                              MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    using namespace Frontier;
    (void)ArgumentCount;
    (void)ArgumentValues;

    // -- Data-layer validation (headless, no ImGui) — printed once so a CI run sees the tally ---------------------------
    InstrumentationValidationReport Report = RunInstrumentationValidation();
    printf("[instrumentation] validation %u/%u checks passed\n", Report.PassedCount, Report.CheckedCount);

    EditorInstance Editor;

    // -- Window ---------------------------------------------------------------------------------------------------------
    if (!InitializePlatformWindow(Editor.Window, "Frontier \xE2\x80\x94 Instrumentation Validation", 1600, 900))
    {
        fprintf(stderr, "[instrumentation] window creation failed\n");
        return 1;
    }

    // -- Vulkan host ----------------------------------------------------------------------------------------------------
    uint32_t ExtensionCount = 0;
    const char** RequiredExtensions = QueryRequiredInstanceExtensions(ExtensionCount);

    if (!InitializeVulkanHost(Editor.Host, RequiredExtensions, ExtensionCount))
    {
        FinalizePlatformWindow(Editor.Window);
        return 1;
    }
    if (!ConstructPresentationSurface(Editor.Window, Editor.Host.Instance))
    {
        fprintf(stderr, "[instrumentation] surface creation failed\n");
        FinalizeVulkanHost(Editor.Host);
        FinalizePlatformWindow(Editor.Window);
        return 1;
    }

    // -- ImGui interface (creates the swapchain + render pass the backend pipeline binds to) -----------------------------
    if (!InitializeVulkanImguiInterface(Editor.Interface, Editor.Host, Editor.Window))
    {
        vkDestroySurfaceKHR(Editor.Host.Instance, Editor.Window.PresentationSurface, Editor.Host.Allocator);
        FinalizeVulkanHost(Editor.Host);
        FinalizePlatformWindow(Editor.Window);
        return 1;
    }

    // -- ImGui context + backends ---------------------------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    Io.IniFilename = nullptr;

    AttachImguiPlatform(Editor.Window);

    ImGui_ImplVulkan_InitInfo InitInfo = {};
    InitInfo.ApiVersion                   = Editor.Host.ApiVersion;
    InitInfo.Instance                     = Editor.Host.Instance;
    InitInfo.PhysicalDevice               = Editor.Host.PhysicalDevice;
    InitInfo.Device                       = Editor.Host.Device;
    InitInfo.QueueFamily                  = Editor.Host.GraphicsQueueFamily;
    InitInfo.Queue                        = Editor.Host.GraphicsQueue;
    InitInfo.DescriptorPool               = Editor.Host.ImguiDescriptorPool;
    InitInfo.MinImageCount                = Editor.Interface.MinimumImageCount;
    InitInfo.ImageCount                   = Editor.Interface.Window.ImageCount;
    InitInfo.PipelineInfoMain.RenderPass  = Editor.Interface.Window.RenderPass;
    InitInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    InitInfo.Allocator                    = Editor.Host.Allocator;
    InitInfo.CheckVkResultFn              = &ReportVkResult;
    ImGui_ImplVulkan_Init(&InitInfo);

    // -- The overlay + its data -----------------------------------------------------------------------------------------
    InitializeInstrumentationExtension(Editor.Overlay);
    RegisterAllPanels(Editor.Overlay, Editor.Telemetry);

    // -- Frame loop -----------------------------------------------------------------------------------------------------
    while (!QueryWindowCloseRequested(Editor.Window))
    {
        PollPlatformEvents(Editor.Window);

        if (!BeginImguiFrame(Editor.Interface, Editor.Host, Editor.Window))
            continue;   // minimized / zero-extent / stale swapchain — the interface already flagged any rebuild

        // 📝 Advance the synthetic data by a fixed step (deterministic; the loop carries no clock). A real host writes its
        //    measured figures instead — the pull-callbacks then surface them with no overlay change.
        AdvanceSyntheticTelemetry(Editor.Telemetry, 1.0f / 60.0f);

        ImGui_ImplVulkan_NewFrame();
        AdvanceImguiPlatform();
        ImGui::NewFrame();

        // 📝 The whole overlay: pull every card's signal, host each card, run the pill tray. One call, no allocation.
        RefreshInstrumentationExtension(Editor.Overlay, Io.DisplaySize.y);

        ImGui::Render();
        SubmitAndPresentImguiFrame(Editor.Interface, Editor.Host, ImGui::GetDrawData());
    }

    // -- Teardown (reverse of bring-up, each Vulkan step gated on device-idle) ------------------------------------------
    vkDeviceWaitIdle(Editor.Host.Device);
    FinalizeInstrumentationExtension(Editor.Overlay);

    ImGui_ImplVulkan_Shutdown();
    DetachImguiPlatform(Editor.Window);
    ImGui::DestroyContext();

    FinalizeVulkanImguiInterface(Editor.Interface, Editor.Host);
    vkDestroySurfaceKHR(Editor.Host.Instance, Editor.Window.PresentationSurface, Editor.Host.Allocator);
    FinalizeVulkanHost(Editor.Host);
    FinalizePlatformWindow(Editor.Window);
    return 0;
}
