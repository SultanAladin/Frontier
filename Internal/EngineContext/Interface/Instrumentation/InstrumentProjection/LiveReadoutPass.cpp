/*==============================================================================================================================================
                                                             LIVEREADOUTPASS.CPP
==============================================================================================================================================*/
// 🧩 The "Live FPS" body: huge number + trend arrow + min·max·avg sub-line + gradient-filled sparkline (LiveTelemetryScene fps card)

#include "LiveReadoutPass.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw the readout: big value, arrow, sub-line, sparkline. Mirrors the HTML fps card layout top-to-bottom inside the body.
void ConstructLiveReadout(const InstrumentBodyContext& Context)
{
    ImDrawList* DrawList = Context.DrawList;
    const InstrumentTileDescriptor& Look = *Context.Presentation;
    const SignalEvaluationInterval& Range = ResolveBodyRange(Context, 0u);

    float BodyWidth  = Context.BodyMaximum.x - Context.BodyMinimum.x;
    float OriginX    = Context.BodyMinimum.x;
    float OriginY    = Context.BodyMinimum.y;

    // 📝 The window average, for the trend arrow (latest vs mean). Guard the empty window.
    float Average = Range.Latest;
    if (Range.Count > 0u)
    {
        float Sum = 0.0f;
        for (uint32_t Index = 0u; Index < Range.Count; ++Index)
        {
            Sum += Range.Samples[Index];
        }
        Average = Sum / static_cast<float>(Range.Count);
    }
    bool Rising = Range.Latest >= Average;

    // 📝 The big number — the latest value, formatted to the presentation's decimals + unit. Drawn ~2.4× the base font by
    //    scaling the font-size argument of AddText (draw-list text takes a size), so it reads as the panel's headline figure.
    char BigText[32];
    std::snprintf(BigText, sizeof(BigText), "%.*f", Look.DecimalPlaces, Range.Latest);
    ImFont* Font = ImGui::GetFont();
    float BaseSize = ImGui::GetFontSize();
    float HugeSize = BaseSize * 2.7f;
    DrawList->AddText(Font, HugeSize, ImVec2(OriginX, OriginY), IM_COL32(244, 244, 246, 255), BigText);

    // 📝 The trend arrow after the number: up-right / down-right, green when rising else coral (HTML fpsArrow behaviour).
    ImVec2 BigExtent = Font->CalcTextSizeA(HugeSize, FLT_MAX, 0.0f, BigText);
    ImU32 ArrowColour = Rising ? IM_COL32(61, 220, 132, 255) : IM_COL32(255, 90, 82, 255);
    const char* Arrow = Rising ? "\xE2\x86\x97" : "\xE2\x86\x98";   // ↗ / ↘
    DrawList->AddText(Font, BaseSize * 1.2f, ImVec2(OriginX + BigExtent.x + 4.0f, OriginY + 2.0f), ArrowColour, Arrow);

    // 📝 The unit suffix under-hangs the big number's baseline in the HTML; here we tag it small right after the arrow line.
    if (Look.UnitSuffix[0] != '\0')
    {
        DrawList->AddText(Font, BaseSize * 0.85f,
                          ImVec2(OriginX + BigExtent.x + 4.0f, OriginY + HugeSize - BaseSize),
                          IM_COL32(108, 108, 116, 255), Look.UnitSuffix);
    }

    // 📝 The "min · max · avg" sub-line, muted mono, below the number.
    float MinValue = (Range.Count > 0u) ? Range.Minimum : 0.0f;
    float MaxValue = (Range.Count > 0u) ? Range.Maximum : 0.0f;
    char SubText[96];
    std::snprintf(SubText, sizeof(SubText), "%.*f min  %.*f max  %.*f avg",
                  Look.DecimalPlaces, MinValue, Look.DecimalPlaces, MaxValue, Look.DecimalPlaces, Average);
    float SubY = OriginY + HugeSize + 6.0f;
    DrawList->AddText(Font, BaseSize * 0.85f, ImVec2(OriginX, SubY), IM_COL32(108, 108, 116, 255), SubText);

    // 📝 The sparkline, filling the remaining body height below the sub-line. Polyline of the window over the body width,
    //    a translucent under-fill down to the baseline, and a bright head dot — the SegmentUI sparkline look.
    float PlotTop    = SubY + BaseSize + 6.0f;
    float PlotBottom = Context.BodyMaximum.y;
    if (Range.Count >= 2u && PlotBottom - PlotTop > 6.0f)
    {
        float PlotHeight = PlotBottom - PlotTop;
        float Extent = RangeExtent(Range);
        ImU32 LineColour = ResolveInstrumentColour(Look.AccentPrimary);

        // 📝 Under-fill first (behind the line): a filled polygon from the trace down to the baseline, faded accent.
        ImU32 FillColour = ResolveInstrumentColourAlpha(Look.AccentPrimary, 0.18f);
        for (uint32_t Index = 0u; Index + 1u < Range.Count; ++Index)
        {
            float X0 = OriginX + (static_cast<float>(Index)        / (Range.Count - 1u)) * BodyWidth;
            float X1 = OriginX + (static_cast<float>(Index + 1u)   / (Range.Count - 1u)) * BodyWidth;
            float Y0 = PlotBottom - ((Range.Samples[Index]     - Range.Minimum) / Extent) * (PlotHeight - 3.0f) - 2.0f;
            float Y1 = PlotBottom - ((Range.Samples[Index + 1u] - Range.Minimum) / Extent) * (PlotHeight - 3.0f) - 2.0f;
            DrawList->AddQuadFilled(ImVec2(X0, Y0), ImVec2(X1, Y1), ImVec2(X1, PlotBottom), ImVec2(X0, PlotBottom), FillColour);
        }

        // 📝 The trace line on top.
        for (uint32_t Index = 0u; Index + 1u < Range.Count; ++Index)
        {
            float X0 = OriginX + (static_cast<float>(Index)      / (Range.Count - 1u)) * BodyWidth;
            float X1 = OriginX + (static_cast<float>(Index + 1u) / (Range.Count - 1u)) * BodyWidth;
            float Y0 = PlotBottom - ((Range.Samples[Index]     - Range.Minimum) / Extent) * (PlotHeight - 3.0f) - 2.0f;
            float Y1 = PlotBottom - ((Range.Samples[Index + 1u] - Range.Minimum) / Extent) * (PlotHeight - 3.0f) - 2.0f;
            DrawList->AddLine(ImVec2(X0, Y0), ImVec2(X1, Y1), LineColour, 2.0f);
        }

        // 📝 Head dot at the newest sample.
        float HeadX = OriginX + BodyWidth;
        float HeadY = PlotBottom - ((Range.Latest - Range.Minimum) / Extent) * (PlotHeight - 3.0f) - 2.0f;
        DrawList->AddCircleFilled(ImVec2(HeadX, HeadY), 3.0f, LineColour);
    }
}

}   // namespace Frontier
