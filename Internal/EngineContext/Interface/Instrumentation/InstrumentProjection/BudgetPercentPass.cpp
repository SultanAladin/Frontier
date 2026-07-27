/*==============================================================================================================================================
                                                            BUDGETPERCENTPASS.CPP
==============================================================================================================================================*/
// 🧩 The "Budget" body: a threshold-tinted percent over a muted wrapped caption (LiveTelemetryScene insight card)

#include "BudgetPercentPass.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw the percent + caption. The percent's tint follows the HTML thresholds (coral < 20, yellow < 40, else green).
void ConstructBudgetPercent(const InstrumentBodyContext& Context)
{
    ImDrawList* DrawList = Context.DrawList;
    const InstrumentTileDescriptor& Look = *Context.Presentation;
    const SignalEvaluationInterval& Range = ResolveBodyRange(Context, 0u);

    float OriginX = Context.BodyMinimum.x;
    float OriginY = Context.BodyMinimum.y;
    float BodyWidth = Context.BodyMaximum.x - Context.BodyMinimum.x;

    float Percent = Range.Latest;
    if (Percent < 0.0f) { Percent = 0.0f; }
    if (Percent > 100.0f) { Percent = 100.0f; }

    // 📝 Threshold tint matching the HTML: coral when scarce, yellow when tight, green when comfortable.
    ImU32 PercentColour = (Percent < 20.0f) ? IM_COL32(255, 90, 82, 255)
                        : (Percent < 40.0f) ? IM_COL32(255, 210, 63, 255)
                                            : IM_COL32(61, 220, 132, 255);

    ImFont* Font = ImGui::GetFont();
    float BaseSize = ImGui::GetFontSize();
    float BigSize  = BaseSize * 2.2f;

    // 📝 The big percent, integer part large + a small "%" following it.
    char PercentText[16];
    std::snprintf(PercentText, sizeof(PercentText), "%d", static_cast<int>(Percent + 0.5f));
    DrawList->AddText(Font, BigSize, ImVec2(OriginX, OriginY), PercentColour, PercentText);
    ImVec2 PercentExtent = Font->CalcTextSizeA(BigSize, FLT_MAX, 0.0f, PercentText);
    DrawList->AddText(Font, BaseSize * 1.05f,
                      ImVec2(OriginX + PercentExtent.x + 3.0f, OriginY + BigSize - BaseSize * 1.2f),
                      IM_COL32(108, 108, 116, 255), "%");

    // 📝 The caption sentence beneath, muted and wrapped to the body width.
    const char* Caption = Look.Payload.Caption;
    if (Caption[0] != '\0')
    {
        float CaptionY = OriginY + BigSize + 8.0f;
        DrawList->AddText(Font, BaseSize * 0.9f, ImVec2(OriginX, CaptionY),
                          IM_COL32(108, 108, 116, 255), Caption, nullptr, BodyWidth);
    }
}

}   // namespace Frontier
