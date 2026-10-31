/*==========================================================================================================================================
                                                 SCENEDIRECTORYPANELPRESENTATION.CPP
==========================================================================================================================================*/
// 🧩 General-purpose Editor Outliner: dynamic scene title, count cards, search, collapsible groups, add/delete, and *Editor launch.

#include "Engine/Interface/SceneDirectoryPanel.h"

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DropdownControls.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/SceneDirectory.h"
#include "SceneDirectoryPanelRegistration.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL UTILITIES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

ImU32 FallbackClassificationTint(RecordClassification Classification) noexcept
{
    switch (Classification)
    {
        case RecordClassification::Environment:  return ParseHexSpectrum("#e8c27a", 0xFF7AC2E8u);
        case RecordClassification::Geometry:     return ParseHexSpectrum("#8fb3a4", 0xFFA4B38Fu);
        case RecordClassification::Illumination: return ParseHexSpectrum("#9fb4d6", 0xFFD6B49Fu);
        case RecordClassification::Camera:       return ParseHexSpectrum("#9fb4d6", 0xFFD6B49Fu);
        case RecordClassification::Physics:      return ParseHexSpectrum("#e8833a", 0xFF3A83E8u);
        case RecordClassification::Interface:    return ParseHexSpectrum("#a8bbeb", 0xFFEBBBA8u);
        case RecordClassification::Content:      return ParseHexSpectrum("#a8bbeb", 0xFFEBBBA8u);
        default:                                 break;
    }
    return ParseHexSpectrum("#9fb4d6", 0xFFD6B49Fu);
}

std::string ResolveGroupLabel(const SceneDirectoryMetadata& Metadata, RecordClassification Classification)
{
    if (!Metadata.GroupName.empty())
    {
        return Metadata.GroupName;
    }
    return std::string(RecordClassificationName(Classification));
}

float MeasureWidth(ImFont* Font, float Size, const char* Text) noexcept
{
    if (Font == nullptr || Text == nullptr || Text[0] == '\0')
    {
        return 0.0f;
    }
    return Font->CalcTextSizeA(Size, 1.0e9f, 0.0f, Text).x;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANEL REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

DockPanelSpecification MakeSceneDirectoryPanelSpecification(SceneDirectoryPanel& Panel)
{
    DockPanelSpecification Specification;
    Specification.Key              = "Outliner";
    Specification.Caption          = "Outliner";
    Specification.ConstructContent = [&Panel]() { Panel.ConstructInterface(); };
    Specification.Open             = true;
    return Specification;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    OUTLINER PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

void SceneDirectoryPanel::ConstructInterface()
{
    SceneDirectory&                        Directory = SceneDirectory::Instance();
    InspectorRuntime&                      Runtime   = InspectorRuntime::Instance();
    const InspectorPalette                 Palette   = InspectorPalette::Resolve();
    const EditorTypefaces                  Faces     = EditorTypefaces::Resolve();
    static std::array<char, 128>           SearchBuffer{};
    static std::string                     SynchronizedSearch;
    static std::unordered_set<std::string> FoldedGroups;

    const ImVec2 WindowPos  = ImGui::GetWindowPos();
    const ImVec2 CursorTop  = ImGui::GetCursorScreenPos();
    const ImVec2 WindowSize = ImGui::GetWindowSize();
    const float  PanelTop   = CursorTop.y;
    const float  PanelBot   = WindowPos.y + WindowSize.y;
    const float  PadX       = 14.0f;
    const float  LeftX      = WindowPos.x + PadX;
    const float  RightX     = WindowPos.x + WindowSize.x - PadX;
    const float  InnerW     = std::max(140.0f, RightX - LeftX);
    ImDrawList*  DrawList   = ImGui::GetWindowDrawList();

    DrawList->AddRectFilled(ImVec2(WindowPos.x, PanelTop), ImVec2(WindowPos.x + WindowSize.x, PanelBot), Palette.ColumnBg);
    DrawList->AddLine(ImVec2(WindowPos.x + WindowSize.x - 1.0f, PanelTop),
                      ImVec2(WindowPos.x + WindowSize.x - 1.0f, PanelBot),
                      Palette.EdgeBorder, 1.0f);

    float CursorY = PanelTop + 16.0f;

    // -- Brand header ("frontier." + Layers glyph) --------------------------------------------------------------------
    ConstructEditorGlyph(DrawList, ImVec2(LeftX + 12.0f, CursorY + 11.0f), 8.5f, Palette.TextSecondary, "Layers", 1.5f);
    DrawList->AddText(Faces.Medium, 21.0f, ImVec2(LeftX + 28.0f, CursorY), Palette.TextPrimary, "frontier");
    const float BrandWordW = MeasureWidth(Faces.Medium, 21.0f, "frontier");
    DrawList->AddText(Faces.Medium, 21.0f, ImVec2(LeftX + 28.0f + BrandWordW, CursorY), Palette.TextMuted, ".");
    const char* EngineTag = "EDITOR";
    DrawList->AddText(Faces.Medium, 9.0f,
                      ImVec2(RightX - MeasureWidth(Faces.Medium, 9.0f, EngineTag) - 4.0f, CursorY + 7.5f),
                      Palette.TextFaint, EngineTag);
    CursorY += 36.0f;

    // -- Active scene title (queried dynamically from SceneDirectory — never hardcoded) -------------------------------
    const std::string SceneTitle    = Directory.QueryActiveSceneTitle();
    const std::string SceneSubtitle = Directory.QueryActiveSceneSubtitle();

    DrawList->AddText(Faces.SemiBold, 9.0f, ImVec2(LeftX + 6.0f, CursorY), Palette.TextFaint, "WORKSPACE");
    DrawList->AddCircleFilled(ImVec2(RightX - 8.0f, CursorY + 4.5f), 5.0f, ModulateSpectrumAlpha(Palette.LiveGreen, 0.25f), 12);
    DrawList->AddCircleFilled(ImVec2(RightX - 8.0f, CursorY + 4.5f), 2.5f, Palette.LiveGreen, 10);
    CursorY += 16.0f;

    DrawList->AddText(Faces.Medium, 15.5f, ImVec2(LeftX + 6.0f, CursorY), Palette.TextPrimary, SceneTitle.c_str());
    const float SceneNameW = MeasureWidth(Faces.Medium, 15.5f, SceneTitle.c_str());
    DrawList->AddText(Faces.Regular, 15.5f, ImVec2(LeftX + 6.0f + SceneNameW, CursorY), Palette.TextFaint, ".levelspace");
    CursorY += 28.0f;

    // -- Enabled / Hidden count cards ---------------------------------------------------------------------------------
    uint32_t EnabledRecords = 0;
    uint32_t HiddenRecords  = 0;
    for (const SceneDirectoryRow& Row : QueryRows())
    {
        if (Row.Visible) ++EnabledRecords;
        else             ++HiddenRecords;
    }
    {
        const float CardGap = 9.0f;
        const float CardW   = (InnerW - CardGap) * 0.5f;
        const float CardH   = 60.0f;
        for (int CardIdx = 0; CardIdx < 2; ++CardIdx)
        {
            const bool  EnabledCard = (CardIdx == 0);
            const float CardX       = LeftX + static_cast<float>(CardIdx) * (CardW + CardGap);
            DrawList->AddRectFilled(ImVec2(CardX, CursorY), ImVec2(CardX + CardW, CursorY + CardH), Palette.CountCardBg, 14.0f);
            DrawList->AddRect(ImVec2(CardX, CursorY), ImVec2(CardX + CardW, CursorY + CardH), Palette.CountCardBorder, 14.0f);

            const ImVec2 BadgeCentre(CardX + 21.0f, CursorY + 21.0f);
            DrawList->AddCircleFilled(BadgeCentre, 11.0f, EnabledCard ? Palette.LiveGreen : Palette.CountCardBorder, 18);
            ConstructEditorGlyph(DrawList, BadgeCentre, 5.2f,
                                 EnabledCard ? ParseHexSpectrum("#0b2414", 0xFF14240Bu) : Palette.TextMuted,
                                 EnabledCard ? "Check" : "EyeOff", 1.5f);

            const char* Caption = EnabledCard ? "Enabled" : "Hidden";
            DrawList->AddText(Faces.Regular, 10.0f, ImVec2(CardX + 12.0f, CursorY + CardH - 18.0f), Palette.TextMuted, Caption);

            char CountText[24] = {};
            std::snprintf(CountText, sizeof(CountText), "%u", EnabledCard ? EnabledRecords : HiddenRecords);
            const float NumW = MeasureWidth(Faces.Light, 26.0f, CountText);
            DrawList->AddText(Faces.Light, 26.0f, ImVec2(CardX + CardW - 12.0f - NumW, CursorY + 10.0f),
                              Palette.TextPrimary, CountText);
        }
        CursorY += CardH + 14.0f;
    }

    // -- Scene heading + '+' dynamic Add Record popup -----------------------------------------------------------------
    {
        DrawList->AddText(Faces.Medium, 12.0f, ImVec2(LeftX + 6.0f, CursorY + 3.0f), Palette.TextPrimary, "Scene");
        char Badge[24] = {};
        std::snprintf(Badge, sizeof(Badge), "%u", QueryTotal());
        const float BadgeW = MeasureWidth(Faces.Regular, 10.0f, Badge) + 10.0f;
        const float BadgeX = LeftX + 6.0f + MeasureWidth(Faces.Medium, 12.0f, "Scene") + 7.0f;
        DrawList->AddRect(ImVec2(BadgeX, CursorY + 2.0f), ImVec2(BadgeX + BadgeW, CursorY + 19.0f), Palette.CountCardBorder, 5.0f);
        DrawList->AddText(Faces.Regular, 10.0f, ImVec2(BadgeX + 5.0f, CursorY + 4.5f), Palette.TextMuted, Badge);

        ImGui::SetCursorScreenPos(ImVec2(RightX - 24.0f, CursorY));
        if (ImGui::InvisibleButton("##AddSceneRecordBtn", ImVec2(22.0f, 22.0f)))
        {
            ImGui::OpenPopup("##OutlinerAddRecordPopup");
        }
        const bool AddHovered = ImGui::IsItemHovered();
        DrawList->AddRectFilled(ImVec2(RightX - 24.0f, CursorY), ImVec2(RightX - 2.0f, CursorY + 22.0f),
                                AddHovered ? Palette.RowHoverBg : Palette.SearchBg, 6.0f);
        DrawList->AddRect(ImVec2(RightX - 24.0f, CursorY), ImVec2(RightX - 2.0f, CursorY + 22.0f), Palette.SearchBorder, 6.0f);
        DrawList->AddLine(ImVec2(RightX - 18.0f, CursorY + 11.0f), ImVec2(RightX - 8.0f, CursorY + 11.0f), Palette.TextSecondary, 1.4f);
        DrawList->AddLine(ImVec2(RightX - 13.0f, CursorY + 6.0f), ImVec2(RightX - 13.0f, CursorY + 16.0f), Palette.TextSecondary, 1.4f);

        ImGui::SetNextWindowPos(ImVec2(RightX - 210.0f, CursorY + 24.0f), ImGuiCond_Appearing);
        if (DropdownControlSurface::BeginMenu("##OutlinerAddRecordPopup"))
        {
            if (!ConstructibleTypes.empty())
            {
                for (const auto& [TypeId, DisplayLabel] : ConstructibleTypes)
                {
                    if (DropdownControlSurface::Option(DisplayLabel.c_str()))
                    {
                        RequestConstructRecord(TypeId);
                    }
                }
            }
            else
            {
                for (uint32_t Index = 0; Index < static_cast<uint32_t>(RecordClassification::Count); ++Index)
                {
                    const auto Classification = static_cast<RecordClassification>(Index);
                    char OptionLabel[64] = {};
                    std::snprintf(OptionLabel, sizeof(OptionLabel), "Add %s", RecordClassificationName(Classification));
                    if (DropdownControlSurface::Option(OptionLabel))
                    {
                        RequestSpawn(Classification);
                    }
                }
            }
            DropdownControlSurface::EndMenu();
        }
        CursorY += 28.0f;
    }

    // -- Search field -------------------------------------------------------------------------------------------------
    {
        const float SearchH = 34.0f;
        DrawList->AddRectFilled(ImVec2(LeftX + 2.0f, CursorY), ImVec2(RightX - 2.0f, CursorY + SearchH), Palette.SearchBg, 8.0f);
        DrawList->AddRect(ImVec2(LeftX + 2.0f, CursorY), ImVec2(RightX - 2.0f, CursorY + SearchH), Palette.SearchBorder, 8.0f);
        ConstructEditorGlyph(DrawList, ImVec2(LeftX + 16.0f, CursorY + SearchH * 0.5f), 6.0f, Palette.TextFaint, "Search", 1.35f);

        if (QueryTextFilter() != SynchronizedSearch)
        {
            std::snprintf(SearchBuffer.data(), SearchBuffer.size(), "%s", QueryTextFilter().c_str());
            SynchronizedSearch = QueryTextFilter();
        }

        ImGui::SetCursorScreenPos(ImVec2(LeftX + 30.0f, CursorY + 9.0f));
        ImGui::PushFont(Faces.Regular, 11.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Palette.TextPrimary));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(Palette.TextFaint));
        ImGui::SetNextItemWidth(std::max(40.0f, InnerW - 40.0f));
        if (ImGui::InputTextWithHint("##OutlinerSearchField", "Find an object\xE2\x80\xA6",
                                     SearchBuffer.data(), SearchBuffer.size()))
        {
            ConfigureTextFilter(SearchBuffer.data());
            SynchronizedSearch = QueryTextFilter();
        }
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        CursorY += SearchH + 14.0f;
    }

    // -- Grouped scrollable tree --------------------------------------------------------------------------------------
    const float FooterHeight = 56.0f;
    const float TreeHeight   = std::max(60.0f, (PanelBot - FooterHeight) - CursorY);

    ImGui::SetCursorScreenPos(ImVec2(LeftX, CursorY));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##SceneDirectoryGroupedTree", ImVec2(InnerW, TreeHeight), false))
    {
        ImDrawList* NestedDraw  = ImGui::GetWindowDrawList();
        const auto& VisibleRows = QueryVisible();
        bool        NeedsSync   = false;

        std::vector<std::string> OrderedGroups;
        for (const SceneDirectoryRow& Row : VisibleRows)
        {
            const SceneDirectoryMetadata Meta  = Directory.QueryMetadata(Row.Token);
            const std::string            Group = ResolveGroupLabel(Meta, Row.Classification);
            if (std::find(OrderedGroups.begin(), OrderedGroups.end(), Group) == OrderedGroups.end())
            {
                OrderedGroups.push_back(Group);
            }
        }

        for (const std::string& GroupName : OrderedGroups)
        {
            uint32_t GroupMemberCount = 0;
            for (const SceneDirectoryRow& Row : VisibleRows)
            {
                if (ResolveGroupLabel(Directory.QueryMetadata(Row.Token), Row.Classification) == GroupName)
                {
                    ++GroupMemberCount;
                }
            }

            const bool   GroupFolded = (FoldedGroups.find(GroupName) != FoldedGroups.end()) && QueryTextFilter().empty();
            const ImVec2 GroupOrigin = ImGui::GetCursorScreenPos();
            ImGui::PushID(GroupName.c_str());
            if (ImGui::InvisibleButton("##GroupFoldBtn", ImVec2(InnerW, 22.0f)))
            {
                if (FoldedGroups.find(GroupName) != FoldedGroups.end()) FoldedGroups.erase(GroupName);
                else                                                     FoldedGroups.insert(GroupName);
            }
            ConstructEditorGlyph(NestedDraw, ImVec2(GroupOrigin.x + 10.0f, GroupOrigin.y + 10.0f), 4.5f,
                                 Palette.TextMuted, GroupFolded ? "ChevronRight" : "ChevronDown", 1.35f);
            NestedDraw->AddText(Faces.Medium, 10.5f, ImVec2(GroupOrigin.x + 22.0f, GroupOrigin.y + 4.5f),
                                Palette.TextMuted, GroupName.c_str());

            char GroupCountText[16] = {};
            std::snprintf(GroupCountText, sizeof(GroupCountText), "%u", GroupMemberCount);
            NestedDraw->AddText(Faces.Regular, 9.5f,
                                ImVec2(GroupOrigin.x + InnerW - 8.0f - MeasureWidth(Faces.Regular, 9.5f, GroupCountText),
                                       GroupOrigin.y + 5.0f),
                                Palette.TextFaint, GroupCountText);
            ImGui::PopID();

            if (!GroupFolded)
            {
                for (const SceneDirectoryRow& Row : VisibleRows)
                {
                    const SceneDirectoryMetadata Meta = Directory.QueryMetadata(Row.Token);
                    if (ResolveGroupLabel(Meta, Row.Classification) != GroupName)
                    {
                        continue;
                    }

                    const ImVec2 RowMin      = ImGui::GetCursorScreenPos();
                    const float  RowH        = 38.0f;
                    const ImVec2 RowMax(RowMin.x + InnerW - 4.0f, RowMin.y + RowH);
                    const bool   SelectedRow = (QuerySelected() == Row.Token);
                    const ImU32  RecordTint  = ParseHexSpectrum(Meta.AccentHex, FallbackClassificationTint(Row.Classification));

                    ImGui::PushID(static_cast<int>(Row.Token.Value));

                    const bool  HasEditor  = Runtime.QueryHasEditor(Row.Token);
                    const bool  Removable  = Meta.RemovableInEditor;
                    const float EyeWidth   = 26.0f;
                    const float TrashW     = (Removable && SelectedRow) ? 22.0f : 0.0f;
                    const float EditorBtnW = HasEditor ? 22.0f : 0.0f;
                    const float MainHitW   = std::max(40.0f, (InnerW - 4.0f) - EyeWidth - TrashW - EditorBtnW);

                    ImGui::SetCursorScreenPos(RowMin);
                    const bool RowClicked = ImGui::InvisibleButton("##SelectRow", ImVec2(MainHitW, RowH));
                    const bool RowDouble  = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
                    const bool RowHovered = ImGui::IsItemHovered();

                    if (SelectedRow)
                    {
                        NestedDraw->AddRectFilled(RowMin, RowMax, Palette.RowSelectedBottom, 8.0f);
                        NestedDraw->AddRect(RowMin, RowMax, Palette.RowSelectedBorder, 8.0f);
                        NestedDraw->AddRectFilled(ImVec2(RowMin.x, RowMin.y + 11.0f),
                                                  ImVec2(RowMin.x + 2.5f, RowMin.y + 27.0f),
                                                  RecordTint, 1.5f);
                    }
                    else if (RowHovered)
                    {
                        NestedDraw->AddRectFilled(RowMin, RowMax, Palette.RowHoverBg, 8.0f);
                    }

                    const float AlphaMul = Row.Visible ? 1.0f : 0.45f;
                    float       ItemX    = RowMin.x + 14.0f + static_cast<float>(Row.DepthTier) * 20.0f;

                    if (Row.ChildCount > 0)
                    {
                        ImGui::SetCursorScreenPos(ImVec2(ItemX - 10.0f, RowMin.y + 10.0f));
                        if (ImGui::InvisibleButton("##SubtreeFold", ImVec2(16.0f, 18.0f)))
                        {
                            ConfigureCollapsed(Row.Token, !Row.Collapsed);
                        }
                        ConstructEditorGlyph(NestedDraw, ImVec2(ItemX - 2.0f, RowMin.y + RowH * 0.5f), 4.0f,
                                             Palette.TextFaint, Row.Collapsed ? "ChevronRight" : "ChevronDown", 1.3f);
                        ItemX += 8.0f;
                    }

                    ConstructEditorGlyph(NestedDraw, ImVec2(ItemX + 6.0f, RowMin.y + RowH * 0.5f), 6.8f,
                                         ModulateSpectrumAlpha(RecordTint, 0.88f * AlphaMul),
                                         Meta.GlyphIdentifier.empty() ? "Box" : Meta.GlyphIdentifier, 1.4f);
                    ItemX += 22.0f;

                    const ImU32 NameInk = ModulateSpectrumAlpha(SelectedRow ? Palette.TextPrimary : Palette.TextSecondary, AlphaMul);
                    NestedDraw->AddText(SelectedRow ? Faces.Medium : Faces.Regular, 12.0f,
                                        ImVec2(ItemX, RowMin.y + (RowH - 12.0f) * 0.5f), NameInk, Row.Name.c_str());

                    float RightCursor = RowMax.x - EyeWidth;

                    // Eye visibility toggle.
                    ImGui::SetCursorScreenPos(ImVec2(RightCursor, RowMin.y + 5.0f));
                    if (ImGui::InvisibleButton("##EyeToggle", ImVec2(EyeWidth, RowH - 10.0f)))
                    {
                        (void)Directory.ConfigureVisible(Row.Token, !Row.Visible);
                        NeedsSync = true;
                    }
                    const bool EyeHovered = ImGui::IsItemHovered();
                    if (RowHovered || SelectedRow || !Row.Visible || EyeHovered)
                    {
                        ConstructEditorGlyph(NestedDraw, ImVec2(RightCursor + EyeWidth * 0.5f, RowMin.y + RowH * 0.5f),
                                             5.8f, EyeHovered ? Palette.TextPrimary : Palette.TextFaint,
                                             Row.Visible ? "Eye" : "EyeOff", 1.3f);
                    }

                    // Dynamic Delete button on selected removable row.
                    if (TrashW > 0.0f)
                    {
                        RightCursor -= TrashW;
                        ImGui::SetCursorScreenPos(ImVec2(RightCursor, RowMin.y + 6.0f));
                        if (ImGui::InvisibleButton("##RowDeleteBtn", ImVec2(TrashW, RowH - 12.0f)))
                        {
                            RequestDeleteRecord(Row.Token);
                        }
                        const bool TrashHover = ImGui::IsItemHovered();
                        ConstructEditorGlyph(NestedDraw, ImVec2(RightCursor + TrashW * 0.5f, RowMin.y + RowH * 0.5f),
                                             5.2f, TrashHover ? Palette.DangerRed : Palette.TextFaint, "Trash", 1.25f);
                    }

                    // Optional *Editor launch button [↗].
                    if (HasEditor)
                    {
                        RightCursor -= EditorBtnW;
                        ImGui::SetCursorScreenPos(ImVec2(RightCursor, RowMin.y + 6.0f));
                        if (ImGui::InvisibleButton("##RowEditorBtn", ImVec2(EditorBtnW, RowH - 12.0f)) || RowDouble)
                        {
                            Select(Row.Token);
                            (void)Runtime.RequestEditorOpen(Row.Token);
                            NeedsSync = true;
                        }
                        const bool EditorHover = ImGui::IsItemHovered();
                        ConstructEditorGlyph(NestedDraw, ImVec2(RightCursor + EditorBtnW * 0.5f, RowMin.y + RowH * 0.5f),
                                             5.0f, EditorHover ? Palette.ThemeAccent : Palette.TextFaint,
                                             "EditorLaunch", 1.25f);
                    }

                    // Operational dot + metadata readout.
                    ImU32 DotColour = Palette.ToggleOffBg;
                    if (Meta.OperationalCondition == RecordOperationalCondition::Live) DotColour = Palette.LiveGreen;
                    if (Meta.OperationalCondition == RecordOperationalCondition::Warm) DotColour = Palette.WarmAmber;
                    RightCursor -= 10.0f;
                    NestedDraw->AddCircleFilled(ImVec2(RightCursor + 3.5f, RowMin.y + RowH * 0.5f), 3.4f,
                                                ModulateSpectrumAlpha(DotColour, AlphaMul), 12);

                    if (!Meta.LiveMetadata.empty())
                    {
                        const float MetaW = MeasureWidth(Faces.Regular, 9.5f, Meta.LiveMetadata.c_str());
                        if (RightCursor - 6.0f - MetaW > ItemX + 64.0f)
                        {
                            NestedDraw->AddText(Faces.Regular, 9.5f,
                                                ImVec2(RightCursor - 6.0f - MetaW, RowMin.y + (RowH - 9.5f) * 0.5f),
                                                Palette.TextFaint, Meta.LiveMetadata.c_str());
                        }
                    }

                    if (RowClicked)
                    {
                        Select(Row.Token);
                        NeedsSync = true;
                    }

                    ImGui::PopID();
                    ImGui::SetCursorScreenPos(ImVec2(RowMin.x, RowMin.y + RowH + 2.0f));
                    ImGui::Dummy(ImVec2(InnerW - 4.0f, 0.0f));
                }
            }
            ImGui::Dummy(ImVec2(InnerW - 4.0f, 8.0f));
        }

        if (NeedsSync)
        {
            Synchronize(Directory);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // -- Scene footer (dynamic title + subtitle) ----------------------------------------------------------------------
    const float FootTop = PanelBot - FooterHeight;
    DrawList->AddLine(ImVec2(LeftX, FootTop), ImVec2(RightX, FootTop), Palette.EdgeBorder, 1.0f);
    const ImVec2 WorldBoxMin(LeftX + 6.0f, FootTop + 11.0f);
    DrawList->AddRectFilled(WorldBoxMin, ImVec2(WorldBoxMin.x + 34.0f, WorldBoxMin.y + 34.0f), Palette.CountCardBg, 8.0f);
    ConstructEditorGlyph(DrawList, ImVec2(WorldBoxMin.x + 17.0f, WorldBoxMin.y + 17.0f), 7.5f,
                         Palette.TextMuted, "Globe", 1.35f);
    DrawList->AddText(Faces.Medium, 11.0f, ImVec2(WorldBoxMin.x + 45.0f, FootTop + 12.0f),
                      Palette.TextSecondary, SceneTitle.c_str());
    DrawList->AddText(Faces.Regular, 10.0f, ImVec2(WorldBoxMin.x + 45.0f, FootTop + 28.0f),
                      Palette.TextFaint, SceneSubtitle.c_str());

    ImGui::SetCursorScreenPos(ImVec2(LeftX, PanelBot - 2.0f));
    ImGui::Dummy(ImVec2(InnerW, 1.0f));
}

} // namespace Frontier

#else // !(FRONTIER_EDITOR && FRONTIER_WITH_INTERFACE_SDK)

namespace Frontier
{

void SceneDirectoryPanel::ConstructInterface()
{
}

} // namespace Frontier

#endif // FRONTIER_EDITOR && FRONTIER_WITH_INTERFACE_SDK
