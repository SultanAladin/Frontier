/*==============================================================================================================================================
                                                          INSTRUMENTENCLOSUREPASS.CPP
==============================================================================================================================================*/
// 🧩 The card host: a solid OLED-dark ImGui window with title + close, unrolling every bound ring and constructing one body pass

#include "InstrumentEnclosurePass.h"
#include "InstrumentBodyContext.h"

#include "RenderReportPass.h"
#include "LiveReadoutPass.h"
#include "BudgetPercentPass.h"
#include "FrameGraphPass.h"
#include "AllocationBarPass.h"

#include "imgui.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

static const float TitleBarHeight = 26.0f;   // [px] - reserved band at the card top for the drag bar (grip + title + close)
static const float BodyInset      = 15.0f;   // [px] - padding between the card frame and the body rectangle (HTML 13px 15px)

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Route to exactly one body pass on the record's classification — one case per SegmentUI panel. NumericReadout is gone;
    //    LiveReadout is the default fallback (every single signal renders as a big number + sparkline).
    void ConstructBody(InstrumentRecordClassification Category, const InstrumentBodyContext& Context)
    {
        switch (Category)
        {
            case InstrumentRecordClassification::RenderReport:  ConstructRenderReport(Context);  break;
            case InstrumentRecordClassification::BudgetPercent: ConstructBudgetPercent(Context); break;
            case InstrumentRecordClassification::FrameGraph:    ConstructFrameGraph(Context);    break;
            case InstrumentRecordClassification::AllocationBar: ConstructAllocationBar(Context); break;
            case InstrumentRecordClassification::LiveReadout:
            default:                                            ConstructLiveReadout(Context);   break;
        }
    }

    // 📝 The HTML seeds each panel at a specific width/height (perf 270, fps 200, budget 200, graph 340, alloc full-width). We
    //    seed the ImGui window to the same first-use size so the first frame already looks like the mock; the user drags/resizes
    //    from there and ImGui persists it. Returned as a first-use hint only.
    ImVec2 ResolveSeedSize(InstrumentRecordClassification Category)
    {
        switch (Category)
        {
            case InstrumentRecordClassification::RenderReport:  return ImVec2(270.0f, 360.0f);
            case InstrumentRecordClassification::LiveReadout:   return ImVec2(200.0f, 170.0f);
            case InstrumentRecordClassification::BudgetPercent: return ImVec2(200.0f, 150.0f);
            case InstrumentRecordClassification::FrameGraph:    return ImVec2(340.0f, 250.0f);
            case InstrumentRecordClassification::AllocationBar: return ImVec2(560.0f, 128.0f);
            default:                                            return ImVec2(240.0f, 160.0f);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Open the card window, paint its drag bar (grip + title + close 'x'), resolve the body rectangle inset inside the content
// region, unroll every bound ring into a slice of the caller's scratch, and construct the body pass. The window itself carries
// drag/resize/dock — ImGui persists its rect by the label id, which we key to the slot so cards stay distinct and stable.
InstrumentEnclosureOutcome ConstructInstrumentEnclosure(InstrumentRecordEntry& Record,
                                                        uint32_t               SlotIndex,
                                                        float*                 ScratchSpan,
                                                        uint32_t               ScratchCapacity)
{
    InstrumentEnclosureOutcome Outcome;
    const InstrumentTileDescriptor& Look = Record.Presentation;

    // 📝 Solid, flat card — push the OLED fill/border and the HTML's 16px rounding, no transparency, no blur.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ResolveInstrumentColour(Look.EnclosureFill));
    ImGui::PushStyleColor(ImGuiCol_Border,   ResolveInstrumentColour(Look.EnclosureBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    // 📝 A stable per-slot window id: the visible title plus a hidden ##slot suffix so two same-titled cards never collide.
    char WindowLabel[InstrumentTitleCapacity + 16];
    std::snprintf(WindowLabel, sizeof(WindowLabel), "%s##instrument-%u", Record.Title, SlotIndex);

    ImGui::SetNextWindowSize(ResolveSeedSize(Record.Category), ImGuiCond_FirstUseEver);
    // 📝 We draw our own drag bar (for the OLED look + close 'x'), so the ImGui title bar is off.
    ImGui::Begin(WindowLabel, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 CardMinimum = ImGui::GetWindowPos();
    ImVec2 CardSize    = ImGui::GetWindowSize();
    ImVec2 CardMaximum(CardMinimum.x + CardSize.x, CardMinimum.y + CardSize.y);
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    // 📝 Drag-bar grip: three dim dots at the far left, echoing the HTML's grip affordance.
    ImU32 GripColour = ResolveInstrumentColourAlpha(Look.TitleTint, 0.5f);
    float GripX = CardMinimum.x + BodyInset;
    float GripY = CardMinimum.y + 13.0f;
    for (uint32_t Dot = 0u; Dot < 3u; ++Dot)
    {
        DrawList->AddCircleFilled(ImVec2(GripX + Dot * 5.0f, GripY), 1.6f, GripColour);
    }

    // 📝 Title text, uppercase-tracked in the HTML; here we draw the record title as-is at the tint, after the grip.
    ImVec2 TitlePosition(GripX + 20.0f, CardMinimum.y + 7.0f);
    DrawList->AddText(TitlePosition, ResolveInstrumentColour(Look.TitleTint), Record.Title);

    // 📝 Close affordance: a small 'x' hit-box at the top-right. A click reports close intent up to the extension.
    float CloseExtent = 12.0f;
    ImVec2 CloseMinimum(CardMaximum.x - CloseExtent - 10.0f, CardMinimum.y + 7.0f);
    ImVec2 CloseMaximum(CloseMinimum.x + CloseExtent, CloseMinimum.y + CloseExtent);
    bool CloseHovered = ImGui::IsMouseHoveringRect(CloseMinimum, CloseMaximum);
    if (CloseHovered)
    {
        // 📝 Coral hover chip behind the cross, matching the HTML's .x:hover background.
        DrawList->AddRectFilled(ImVec2(CloseMinimum.x - 4.0f, CloseMinimum.y - 3.0f),
                                ImVec2(CloseMaximum.x + 4.0f, CloseMaximum.y + 3.0f),
                                IM_COL32(255, 90, 82, 42), 6.0f);
    }
    ImU32 CrossColour = CloseHovered ? IM_COL32(255, 120, 112, 255) : ResolveInstrumentColour(Look.TitleTint);
    DrawList->AddLine(CloseMinimum, CloseMaximum, CrossColour, 1.4f);
    DrawList->AddLine(ImVec2(CloseMaximum.x, CloseMinimum.y), ImVec2(CloseMinimum.x, CloseMaximum.y), CrossColour, 1.4f);
    if (CloseHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        Outcome.CloseRequested = true;
    }

    // 📝 Body rectangle = card interior below the drag bar, inset by BodyInset on the other three sides.
    InstrumentBodyContext Body;
    Body.DrawList     = DrawList;
    Body.BodyMinimum  = ImVec2(CardMinimum.x + BodyInset, CardMinimum.y + TitleBarHeight);
    Body.BodyMaximum  = ImVec2(CardMaximum.x - BodyInset, CardMaximum.y - BodyInset);
    Body.Presentation = &Look;

    // 📝 Unroll each bound ring into its own equal slice of the scratch, in registration order, so a body pass reads any slot
    //    it needs as a plain forward array. SliceWidth caps a single ring's unroll; the render report's seven rings each get
    //    an eighth of the buffer — ample for a dot-matrix (40 cols) or a graph line (60 points).
    uint32_t SliceCount = (Record.SignalCount > 0u) ? Record.SignalCount : 1u;
    uint32_t SliceWidth = ScratchCapacity / InstrumentSignalCapacity;
    Body.RangeCount = Record.SignalCount;
    for (uint32_t Signal = 0u; Signal < Record.SignalCount; ++Signal)
    {
        Body.Ranges[Signal] = RetrieveEvaluationRange(Record.History[Signal],
                                                      ScratchSpan + Signal * SliceWidth,
                                                      SliceWidth);
    }
    (void)SliceCount;

    ConstructBody(Record.Category, Body);

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    return Outcome;
}

}   // namespace Frontier
