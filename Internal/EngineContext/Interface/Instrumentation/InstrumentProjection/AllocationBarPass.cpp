/*==============================================================================================================================================
                                                            ALLOCATIONBARPASS.CPP
==============================================================================================================================================*/
// 🧩 The "Memory Allocation" body: a weight-proportioned stacked segment bar over a swatch legend (LiveTelemetryScene alloc card)

#include "AllocationBarPass.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

static const float BarHeight    = 34.0f;   // [px] - the stacked-bar band height (HTML .barwrap)
static const float SegmentGap   = 4.0f;    // [px] - gap between adjacent segments
static const float LegendGap    = 12.0f;   // [px] - gap between the bar and the legend grid
static const uint32_t LegendColumns = 3u;  // [-] - legend entries per row

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw the stacked bar then the legend grid. Segment widths track each segment's latest value as a weight; the legend shows
// name + rounded value under a colour swatch.
void ConstructAllocationBar(const InstrumentBodyContext& Context)
{
    ImDrawList* DrawList = Context.DrawList;
    const PanelStaticPayload& Payload = Context.Presentation->Payload;

    uint32_t Count = Payload.SegmentCount;
    if (Count == 0u)
    {
        return;
    }

    float OriginX   = Context.BodyMinimum.x;
    float OriginY   = Context.BodyMinimum.y;
    float BodyWidth = Context.BodyMaximum.x - Context.BodyMinimum.x;

    // 📝 Sum the segment weights (each segment's latest live value) to normalize the widths. A zero sum falls back to equal.
    float WeightSum = 0.0f;
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        float Weight = ResolveBodyRange(Context, Index).Latest;
        if (Weight < 0.0f) { Weight = 0.0f; }
        WeightSum += Weight;
    }
    bool EqualFallback = WeightSum <= 1e-4f;

    // 📝 Lay the segments left→right, each a rounded fill proportional to its weight, separated by SegmentGap.
    float TotalGap  = SegmentGap * static_cast<float>(Count - 1u);
    float TrackSpan = BodyWidth - TotalGap;
    float CursorX   = OriginX;
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        float Weight = EqualFallback ? 1.0f : ResolveBodyRange(Context, Index).Latest;
        if (Weight < 0.0f) { Weight = 0.0f; }
        float Fraction = EqualFallback ? (1.0f / static_cast<float>(Count)) : (Weight / WeightSum);
        float SegmentWidth = TrackSpan * Fraction;

        ImU32 Tint = ResolveInstrumentColour(Payload.Segments[Index].Tint);
        DrawList->AddRectFilled(ImVec2(CursorX, OriginY),
                                ImVec2(CursorX + SegmentWidth, OriginY + BarHeight),
                                Tint, 6.0f);
        CursorX += SegmentWidth + SegmentGap;
    }

    // 📝 The legend grid below: LegendColumns entries per row, each a colour dot + name (muted) + value (mono). The value is
    //    the segment's latest, rounded — the HTML shows integer MiB.
    ImFont* Font = ImGui::GetFont();
    float BaseSize = ImGui::GetFontSize();
    float LegendTop = OriginY + BarHeight + LegendGap;
    float ColumnWidth = BodyWidth / static_cast<float>(LegendColumns);
    float RowHeight = BaseSize * 2.4f;

    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        uint32_t Column = Index % LegendColumns;
        uint32_t Row    = Index / LegendColumns;
        float CellX = OriginX + Column * ColumnWidth;
        float CellY = LegendTop + Row * RowHeight;

        // 📝 Colour dot + name on the top line.
        DrawList->AddCircleFilled(ImVec2(CellX + 4.0f, CellY + BaseSize * 0.5f), 4.0f,
                                  ResolveInstrumentColour(Payload.Segments[Index].Tint));
        DrawList->AddText(Font, BaseSize * 0.85f, ImVec2(CellX + 13.0f, CellY),
                          IM_COL32(108, 108, 116, 255), Payload.Segments[Index].Label);

        // 📝 Value on the second line, brighter mono.
        char ValueText[24];
        std::snprintf(ValueText, sizeof(ValueText), "%d", static_cast<int>(ResolveBodyRange(Context, Index).Latest + 0.5f));
        DrawList->AddText(Font, BaseSize, ImVec2(CellX + 13.0f, CellY + BaseSize + 1.0f),
                          IM_COL32(220, 220, 226, 255), ValueText);
    }
}

}   // namespace Frontier
