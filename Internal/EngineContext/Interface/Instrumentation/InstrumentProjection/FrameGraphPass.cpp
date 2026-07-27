/*==============================================================================================================================================
                                                             FRAMEGRAPHPASS.CPP
==============================================================================================================================================*/
// 🧩 The "Frame Graph" body: dual-line series over a dashed grid with date labels + legend (LiveTelemetryScene graph card)

#include "FrameGraphPass.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

static const uint32_t GridRows   = 4u;     // [-] - dashed horizontal grid lines (HTML draws 5 edges → 4 rows)
static const float    DashLength = 5.0f;   // [px] - dash + gap length of the grid strokes
static const float    AxisBand   = 16.0f;  // [px] - reserved band at the graph bottom for date labels
static const float    LegendBand = 16.0f;  // [px] - reserved band below the axis for the legend

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 A dashed horizontal line, approximated as a run of short segments — the HTML uses setLineDash([5,5]); ImGui's draw list
    //    has no native dash, so we step across in DashLength strides drawing every other span.
    void ConstructDashedRow(ImDrawList* DrawList, float LeftX, float RightX, float Y, ImU32 Colour)
    {
        float X = LeftX;
        while (X < RightX)
        {
            float SegmentEnd = X + DashLength;
            if (SegmentEnd > RightX) { SegmentEnd = RightX; }
            DrawList->AddLine(ImVec2(X, Y), ImVec2(SegmentEnd, Y), Colour, 1.0f);
            X += DashLength * 2.0f;
        }
    }

    // 📝 Trace one series across the plot on the shared vertical scale, at the given colour + width. Skips a too-short window.
    void ConstructTrace(ImDrawList*                     DrawList,
                        const SignalEvaluationInterval& Range,
                        float                           LeftX,
                        float                           PlotWidth,
                        float                           PlotTop,
                        float                           PlotHeight,
                        float                           SharedMinimum,
                        float                           SharedExtent,
                        ImU32                           Colour,
                        float                           Width)
    {
        if (Range.Count < 2u)
        {
            return;
        }
        for (uint32_t Index = 0u; Index + 1u < Range.Count; ++Index)
        {
            float X0 = LeftX + (static_cast<float>(Index)      / (Range.Count - 1u)) * PlotWidth;
            float X1 = LeftX + (static_cast<float>(Index + 1u) / (Range.Count - 1u)) * PlotWidth;
            float Y0 = PlotTop + PlotHeight - ((Range.Samples[Index]     - SharedMinimum) / SharedExtent) * PlotHeight;
            float Y1 = PlotTop + PlotHeight - ((Range.Samples[Index + 1u] - SharedMinimum) / SharedExtent) * PlotHeight;
            DrawList->AddLine(ImVec2(X0, Y0), ImVec2(X1, Y1), Colour, Width);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw the whole graph body top-to-bottom: header + value, dashed grid, date labels, both traces (grey behind, blue front),
// head dot on the primary, and the legend.
void ConstructFrameGraph(const InstrumentBodyContext& Context)
{
    ImDrawList* DrawList = Context.DrawList;
    const InstrumentTileDescriptor& Look = *Context.Presentation;
    const PanelStaticPayload& Payload = Look.Payload;
    const SignalEvaluationInterval& Primary   = ResolveBodyRange(Context, 0u);
    const SignalEvaluationInterval& Secondary = ResolveBodyRange(Context, 1u);

    ImFont* Font = ImGui::GetFont();
    float BaseSize = ImGui::GetFontSize();

    float OriginX   = Context.BodyMinimum.x;
    float OriginY   = Context.BodyMinimum.y;
    float BodyWidth = Context.BodyMaximum.x - Context.BodyMinimum.x;

    // 📝 Header line (muted) + the big current value (primary latest, presentation decimals + unit).
    if (Payload.Header[0] != '\0')
    {
        DrawList->AddText(Font, BaseSize * 0.82f, ImVec2(OriginX, OriginY), IM_COL32(108, 108, 116, 255), Payload.Header);
    }
    char ValueText[32];
    std::snprintf(ValueText, sizeof(ValueText), "%.*f%s", Look.DecimalPlaces, Primary.Latest, Look.UnitSuffix);
    float ValueY = OriginY + BaseSize + 2.0f;
    DrawList->AddText(Font, BaseSize * 1.35f, ImVec2(OriginX, ValueY), IM_COL32(244, 244, 246, 255), ValueText);

    // 📝 The plot rectangle sits below the header/value and above the axis + legend bands.
    float PlotTop    = ValueY + BaseSize * 1.6f;
    float PlotBottom = Context.BodyMaximum.y - AxisBand - LegendBand;
    float PlotHeight = PlotBottom - PlotTop;
    float LeftX      = OriginX;
    float RightX     = OriginX + BodyWidth;
    if (PlotHeight < 10.0f)
    {
        return;   // 📝 Card too short to plot; header/value already drawn.
    }

    // 📝 Dashed horizontal grid, faint white — GridRows spans across the plot height.
    ImU32 GridColour = IM_COL32(255, 255, 255, 18);
    for (uint32_t Row = 0u; Row <= GridRows; ++Row)
    {
        float Y = PlotTop + (static_cast<float>(Row) / GridRows) * PlotHeight;
        ConstructDashedRow(DrawList, LeftX, RightX, Y, GridColour);
    }

    // 📝 X-axis date labels, evenly spaced, centred under their tick, muted mono.
    if (Payload.AxisCount > 0u)
    {
        float LabelY = PlotBottom + 4.0f;
        for (uint32_t Label = 0u; Label < Payload.AxisCount; ++Label)
        {
            float Fraction = (Payload.AxisCount > 1u)
                           ? static_cast<float>(Label) / (Payload.AxisCount - 1u)
                           : 0.5f;
            float TickX = LeftX + Fraction * BodyWidth;
            const char* Text = Payload.AxisLabels[Label];
            ImVec2 Extent = Font->CalcTextSizeA(BaseSize * 0.78f, FLT_MAX, 0.0f, Text);
            DrawList->AddText(Font, BaseSize * 0.78f, ImVec2(TickX - Extent.x * 0.5f, LabelY),
                              IM_COL32(255, 255, 255, 82), Text);
        }
    }

    // 📝 Shared vertical scale across BOTH series, padded 25% like the HTML so the traces never touch the edges.
    float SharedMinimum = Primary.Latest;
    float SharedMaximum = Primary.Latest;
    bool AnySample = false;
    const SignalEvaluationInterval* Series[2] = { &Primary, &Secondary };
    for (uint32_t Which = 0u; Which < 2u; ++Which)
    {
        const SignalEvaluationInterval& Range = *Series[Which];
        if (Range.Count > 0u)
        {
            if (!AnySample) { SharedMinimum = Range.Minimum; SharedMaximum = Range.Maximum; AnySample = true; }
            if (Range.Minimum < SharedMinimum) { SharedMinimum = Range.Minimum; }
            if (Range.Maximum > SharedMaximum) { SharedMaximum = Range.Maximum; }
        }
    }
    float SharedPad = (SharedMaximum - SharedMinimum) * 0.25f;
    if (SharedPad < 1e-3f) { SharedPad = 1.0f; }
    SharedMinimum -= SharedPad;
    SharedMaximum += SharedPad;
    float SharedExtent = SharedMaximum - SharedMinimum;
    if (SharedExtent < 1e-3f) { SharedExtent = 1.0f; }

    // 📝 Secondary (grey) behind, primary (accent) in front — the HTML draws CPU then GPU.
    ConstructTrace(DrawList, Secondary, LeftX, BodyWidth, PlotTop, PlotHeight, SharedMinimum, SharedExtent,
                   ResolveInstrumentColour(Look.AccentSecondary), 1.6f);
    ConstructTrace(DrawList, Primary, LeftX, BodyWidth, PlotTop, PlotHeight, SharedMinimum, SharedExtent,
                   ResolveInstrumentColour(Look.AccentPrimary), 2.2f);

    // 📝 Head dot on the primary trace's newest sample.
    if (Primary.Count >= 1u)
    {
        float HeadX = LeftX + BodyWidth;
        float HeadY = PlotTop + PlotHeight - ((Primary.Latest - SharedMinimum) / SharedExtent) * PlotHeight;
        DrawList->AddCircleFilled(ImVec2(HeadX, HeadY), 3.2f, ResolveInstrumentColour(Look.AccentPrimary));
    }

    // 📝 Legend along the bottom: a short colour rule + name for each series.
    float LegendY = Context.BodyMaximum.y - LegendBand + 2.0f;
    float LegendX = OriginX;
    const char* PrimaryName   = (Payload.PrimaryLegend[0]   != '\0') ? Payload.PrimaryLegend   : "Primary";
    const char* SecondaryName = (Payload.SecondaryLegend[0] != '\0') ? Payload.SecondaryLegend : "Secondary";

    DrawList->AddLine(ImVec2(LegendX, LegendY + BaseSize * 0.5f), ImVec2(LegendX + 14.0f, LegendY + BaseSize * 0.5f),
                      ResolveInstrumentColour(Look.AccentPrimary), 2.0f);
    DrawList->AddText(Font, BaseSize * 0.8f, ImVec2(LegendX + 19.0f, LegendY), IM_COL32(108, 108, 116, 255), PrimaryName);
    ImVec2 PrimaryExtent = Font->CalcTextSizeA(BaseSize * 0.8f, FLT_MAX, 0.0f, PrimaryName);

    float SecondX = LegendX + 19.0f + PrimaryExtent.x + 18.0f;
    DrawList->AddLine(ImVec2(SecondX, LegendY + BaseSize * 0.5f), ImVec2(SecondX + 14.0f, LegendY + BaseSize * 0.5f),
                      ResolveInstrumentColour(Look.AccentSecondary), 2.0f);
    DrawList->AddText(Font, BaseSize * 0.8f, ImVec2(SecondX + 19.0f, LegendY), IM_COL32(108, 108, 116, 255), SecondaryName);
}

}   // namespace Frontier
