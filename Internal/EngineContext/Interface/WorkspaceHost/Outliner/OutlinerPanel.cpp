/*==============================================================================================================================================
                                                              OUTLINERPANEL.CPP
==============================================================================================================================================*/
// 🧩 Records the shared outliner: header, then one indented row per model entry with an optional arrow, tint chip, title and eye toggle. Every
//    hit-region reports back by record token, so the workspace resolves the interaction against its own Scene assembly — the outliner itself
//    never mutates scene state. Parameterized entirely by OutlinerConfiguration, so the same body serves every workspace's tree.

#include "OutlinerPanel.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

OutlinerPanelResult ConstructOutlinerPanel(const ThemeConfiguration& Theme, const OutlinerConfiguration& Configuration,
                                           const OutlinerModel& Model)
{
    OutlinerPanelResult Result = {};
    Result.Interaction = OutlinerInteraction::None;
    Result.Record      = ResolveNullRecordToken();

    const float RowHeight   = Configuration.RowHeight   > 0.0f ? Configuration.RowHeight   : Theme.Metrics.RowHeight;
    const float IndentWidth = Configuration.IndentWidth > 0.0f ? Configuration.IndentWidth : Theme.Metrics.IndentWidth;
    const float ArrowWidth  = RowHeight;      // 📝 Square arrow hit-box on the left
    const float EyeWidth    = RowHeight;      // 📝 Square eye hit-box on the right
    const float ChipWidth   = RowHeight * 0.4f;

    if (Configuration.HeaderCaption != nullptr)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme.Palette.TextMuted);
        ImGui::TextUnformatted(Configuration.HeaderCaption);
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    for (int Index = 0; Index < Model.RowCount; ++Index)
    {
        const OutlinerRow& Row = Model.Rows[Index];
        ImGui::PushID(static_cast<int>(Row.Record.Index) * 131 + static_cast<int>(Row.Record.Generation) + 1);

        const ImVec2 RowMin  = ImGui::GetCursorScreenPos();
        const float  RowFull = ImGui::GetContentRegionAvail().x;
        const ImVec2 RowMax(RowMin.x + RowFull, RowMin.y + RowHeight);

        // 📝 Selection highlight spans the whole row behind everything else.
        if (Row.Selected)
        {
            DrawList->AddRectFilled(RowMin, RowMax, Theme.Palette.AccentSubtle);
        }

        const float Indent  = Row.Depth * IndentWidth;
        float       CursorX = RowMin.x + Indent;

        // -- Collapse arrow ------------------------------------------------------------------------------------------
        if (Row.ChildCount > 0)
        {
            const ImVec2 ArrowMin(CursorX, RowMin.y);
            const ImVec2 ArrowMax(CursorX + ArrowWidth, RowMin.y + RowHeight);
            const ImVec2 Mid((ArrowMin.x + ArrowMax.x) * 0.5f, (ArrowMin.y + ArrowMax.y) * 0.5f);
            const float  R = RowHeight * 0.22f;

            // 📝 Right-pointing (collapsed) or down-pointing (expanded) triangle.
            if (Row.Expanded)
            {
                DrawList->AddTriangleFilled(ImVec2(Mid.x - R, Mid.y - R * 0.6f), ImVec2(Mid.x + R, Mid.y - R * 0.6f),
                                            ImVec2(Mid.x, Mid.y + R * 0.7f), Theme.Palette.TextMuted);
            }
            else
            {
                DrawList->AddTriangleFilled(ImVec2(Mid.x - R * 0.6f, Mid.y - R), ImVec2(Mid.x - R * 0.6f, Mid.y + R),
                                            ImVec2(Mid.x + R * 0.7f, Mid.y), Theme.Palette.TextMuted);
            }

            ImGui::SetCursorScreenPos(ArrowMin);
            if (ImGui::InvisibleButton("##arrow", ImVec2(ArrowWidth, RowHeight)))
            {
                Result.Interaction = OutlinerInteraction::ToggledExpand;
                Result.Record      = Row.Record;
            }
        }
        CursorX += ArrowWidth;

        // -- Tint chip -----------------------------------------------------------------------------------------------
        if (Configuration.TintChips && Row.TintColor != 0)
        {
            const ImVec2 ChipMin(CursorX, RowMin.y + (RowHeight - ChipWidth) * 0.5f);
            DrawList->AddRectFilled(ChipMin, ImVec2(ChipMin.x + ChipWidth, ChipMin.y + ChipWidth), Row.TintColor, 2.0f);
            CursorX += ChipWidth + Theme.Metrics.ControlSpacing;
        }

        // -- Title (also the select hit-region) ----------------------------------------------------------------------
        const float TitleWidth = (RowMax.x - EyeWidth) - CursorX;
        ImGui::SetCursorScreenPos(ImVec2(CursorX, RowMin.y));
        if (ImGui::InvisibleButton("##select", ImVec2(TitleWidth > 1.0f ? TitleWidth : 1.0f, RowHeight)))
        {
            Result.Interaction    = OutlinerInteraction::Selected;
            Result.Record         = Row.Record;
            Result.AdditiveSelect = ImGui::GetIO().KeyCtrl;
        }
        DrawList->AddText(ImVec2(CursorX + 2.0f, RowMin.y + (RowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                          Row.Selected ? Theme.Palette.TextOnAccent : Theme.Palette.TextPrimary,
                          Row.Title ? Row.Title : "");

        // -- Visibility eye ------------------------------------------------------------------------------------------
        if (Configuration.VisibilityColumn)
        {
            const ImVec2 EyeMin(RowMax.x - EyeWidth, RowMin.y);
            ImGui::SetCursorScreenPos(EyeMin);
            if (ImGui::InvisibleButton("##eye", ImVec2(EyeWidth, RowHeight)))
            {
                Result.Interaction = OutlinerInteraction::ToggledVisible;
                Result.Record      = Row.Record;
            }
            const ImVec2 Mid(EyeMin.x + EyeWidth * 0.5f, EyeMin.y + RowHeight * 0.5f);
            const ImU32  EyeColor = Row.Visible ? Theme.Palette.TextPrimary : Theme.Palette.TextMuted;
            DrawList->AddCircle(Mid, RowHeight * 0.18f, EyeColor, 12, Row.Visible ? 1.5f : 1.0f);
            if (!Row.Visible)
            {
                DrawList->AddLine(ImVec2(Mid.x - RowHeight * 0.22f, Mid.y + RowHeight * 0.22f),
                                  ImVec2(Mid.x + RowHeight * 0.22f, Mid.y - RowHeight * 0.22f), EyeColor, 1.0f);
            }
        }

        // 📝 Advance the cursor past the row for the next entry.
        ImGui::SetCursorScreenPos(ImVec2(RowMin.x, RowMin.y + RowHeight));
        ImGui::Dummy(ImVec2(RowFull, 0.0f));

        ImGui::PopID();
    }

    return Result;
}

}   // namespace Frontier
