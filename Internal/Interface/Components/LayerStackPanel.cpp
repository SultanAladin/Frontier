/*==============================================================================================================================================
                                                            LAYERSTACKPANEL.CPP
==============================================================================================================================================*/
// 🧩 3D Texture paint layer stack panel implementation: 1:1 port of LayerStackR2.html mockup using shared theme tokens + interface controls.

#include "LayerStackPanel.h"

#include "Cards/SectionHeader.h"

#include "Cards/ContentSection.h"

#include "Controls/ValueSlider.h"

#include "Controls/Dropdown.h"

#include "Controls/BooleanEntry.h"

#include "Controls/ColorEntry.h"

#include "imgui.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

static ImU32 ResolveCategoryTagColor(LayerTagCategory Category)
{
    switch (Category)
    {
    case LayerTagCategory::Material:  return IM_COL32(139, 92, 246, 255); // Violet
    case LayerTagCategory::Generator: return IM_COL32(16, 185, 129, 255); // Green
    case LayerTagCategory::Paint:     return IM_COL32(249, 115, 22, 255);  // Orange
    case LayerTagCategory::Fill:      return IM_COL32(59, 130, 246, 255);  // Blue
    default:                          return IM_COL32(139, 92, 246, 255);
    }
}

static const char* const g_BlendModeOptions[] = {
    "Normal", "Overlay", "Multiply", "Screen", "Add", "Subtract"
};
static const int g_BlendModeCount = 6;

static const char* const g_ResolutionOptions[] = {
    "512", "1K", "2K", "4K", "8K"
};
static const int g_ResolutionCount = 5;

static const char* const g_CategoryOptions[] = {
    "Material", "Generator", "Paint", "Fill"
};
static const int g_CategoryCount = 4;

static bool QueryMatchesFilter(const char* Text, const char* Filter)
{
    if (!Filter || Filter[0] == '\0')
    {
        return true;
    }

    std::string TextLower = Text;
    std::string FilterLower = Filter;
    std::transform(TextLower.begin(), TextLower.end(), TextLower.begin(), [](unsigned char Char) { return (char)std::tolower(Char); });
    std::transform(FilterLower.begin(), FilterLower.end(), FilterLower.begin(), [](unsigned char Char) { return (char)std::tolower(Char); });

    return TextLower.find(FilterLower) != std::string::npos;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeLayerStackModel(LayerStackModel& Model)
{
    Model.LayerRecords.clear();
    Model.ActiveLayerIndex = 0;
    Model.SearchFilter[0] = '\0';

    // Layer 0: "Detail" (Open in prototype)
    {
        LayerRecord DetailLayer = {};
        DetailLayer.LayerTitle = "Detail";
        DetailLayer.CategoryTag = LayerTagCategory::Paint;
        DetailLayer.LayerTypeTitle = "Paint";
        DetailLayer.BlendModeIndex = 1; // Overlay
        DetailLayer.ChannelCount = 4;
        DetailLayer.VisibilityEnabled = true;
        DetailLayer.OpacityFraction = 1.0f;
        DetailLayer.ResolutionIndex = 2; // 2K
        DetailLayer.CardExpanded = true;
        DetailLayer.PropertiesExpanded = true;
        DetailLayer.ColourExpanded = true;
        DetailLayer.MaskExpanded = true;

        DetailLayer.LayerMask.MaskEnabled = true;
        DetailLayer.LayerMask.MaskName = "Layer Mask";
        DetailLayer.LayerMask.MaskFillModeIndex = 1; // White
        DetailLayer.LayerMask.MaskOpacityFraction = 0.8f;
        DetailLayer.LayerMask.MaskComponents.push_back({ "AO Cavity", "Generator" });

        Model.LayerRecords.push_back(DetailLayer);
    }

    // Layer 1: "Base" (Closed in prototype)
    {
        LayerRecord BaseLayer = {};
        BaseLayer.LayerTitle = "Base";
        BaseLayer.CategoryTag = LayerTagCategory::Material;
        BaseLayer.LayerTypeTitle = "Material";
        BaseLayer.BlendModeIndex = 0; // Normal
        BaseLayer.ChannelCount = 4;
        BaseLayer.VisibilityEnabled = true;
        BaseLayer.OpacityFraction = 1.0f;
        BaseLayer.ResolutionIndex = 2; // 2K
        BaseLayer.CardExpanded = false;
        BaseLayer.PropertiesExpanded = false;
        BaseLayer.ColourExpanded = false;
        BaseLayer.MaskExpanded = false;

        BaseLayer.LayerMask.MaskEnabled = false;
        BaseLayer.LayerMask.MaskName = "Layer Mask";
        BaseLayer.LayerMask.MaskFillModeIndex = 0; // Black
        BaseLayer.LayerMask.MaskOpacityFraction = 1.0f;

        Model.LayerRecords.push_back(BaseLayer);
    }
}

void ConstructLayerStackPanel(const ThemeConfiguration& Theme, LayerStackModel& Model)
{
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 8.0f));

    // ════════════════════════════════════════════════════════════════════════════════
    // 1. PANEL HEADER STRIP ("Layers")
    // ════════════════════════════════════════════════════════════════════════════════
    {
        float HeaderHeight = 34.0f;
        float AvailWidth = ImGui::GetContentRegionAvail().x;
        ImVec2 HeaderMin = ImGui::GetCursorScreenPos();
        ImVec2 HeaderMax = ImVec2(HeaderMin.x + AvailWidth, HeaderMin.y + HeaderHeight);

        DrawList->AddRectFilled(HeaderMin, HeaderMax, Theme.Palette.PanelHeader, 6.0f);
        DrawList->AddRect(HeaderMin, HeaderMax, Theme.Palette.PanelBorder, 6.0f);

        ImVec2 TextPos = ImVec2(HeaderMin.x + 10.0f, HeaderMin.y + 8.0f);
        DrawList->AddText(TextPos, IM_COL32(204, 204, 204, 255), "Layers");

        ImGui::Dummy(ImVec2(AvailWidth, HeaderHeight));
    }

    // ════════════════════════════════════════════════════════════════════════════════
    // 2. SEARCH FILTER WELL
    // ════════════════════════════════════════════════════════════════════════════════
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputTextWithHint("##layer-filter", "Filter layers by name...", Model.SearchFilter, sizeof(Model.SearchFilter));

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    // ════════════════════════════════════════════════════════════════════════════════
    // 3. ADD-LAYER PILL BAR
    // ════════════════════════════════════════════════════════════════════════════════
    {
        float AddBarHeight = 34.0f;
        float AvailWidth = ImGui::GetContentRegionAvail().x;
        ImVec2 AddMin = ImGui::GetCursorScreenPos();
        ImVec2 AddMax = ImVec2(AddMin.x + AvailWidth, AddMin.y + AddBarHeight);

        bool Hovered = ImGui::IsMouseHoveringRect(AddMin, AddMax);
        ImU32 PillBg = Hovered ? IM_COL32(30, 30, 30, 255) : IM_COL32(20, 20, 20, 255);
        ImU32 PillBd = Hovered ? IM_COL32(80, 80, 80, 255) : IM_COL32(35, 35, 35, 255);

        DrawList->AddRectFilled(AddMin, AddMax, PillBg, 999.0f);
        DrawList->AddRect(AddMin, AddMax, PillBd, 999.0f);

        const char* AddLabel = "+ Add Layer";
        ImVec2 TextSize = ImGui::CalcTextSize(AddLabel);
        ImVec2 TextPos = ImVec2(AddMin.x + (AvailWidth - TextSize.x) * 0.5f, AddMin.y + (AddBarHeight - TextSize.y) * 0.5f);
        DrawList->AddText(TextPos, IM_COL32(235, 235, 235, 255), AddLabel);

        if (ImGui::InvisibleButton("##add-layer-btn", ImVec2(AvailWidth, AddBarHeight)))
        {
            LayerRecord NewRecord = {};
            NewRecord.LayerTitle = "New Layer " + std::to_string(Model.LayerRecords.size() + 1);
            NewRecord.CategoryTag = LayerTagCategory::Paint;
            NewRecord.LayerTypeTitle = "Paint";
            NewRecord.BlendModeIndex = 0;
            NewRecord.ChannelCount = 4;
            NewRecord.VisibilityEnabled = true;
            NewRecord.OpacityFraction = 1.0f;
            NewRecord.ResolutionIndex = 2;
            NewRecord.CardExpanded = true;
            NewRecord.PropertiesExpanded = true;
            NewRecord.ColourExpanded = true;
            NewRecord.MaskExpanded = false;

            Model.LayerRecords.insert(Model.LayerRecords.begin(), NewRecord);
            Model.ActiveLayerIndex = 0;
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    // ════════════════════════════════════════════════════════════════════════════════
    // 4. LAYER LIST (SINGLE FUSED CARDS)
    // ════════════════════════════════════════════════════════════════════════════════
    int IndexToDelete = -1;

    for (size_t LayerIdx = 0; LayerIdx < Model.LayerRecords.size(); ++LayerIdx)
    {
        LayerRecord& Layer = Model.LayerRecords[LayerIdx];

        if (!QueryMatchesFilter(Layer.LayerTitle.c_str(), Model.SearchFilter))
        {
            continue;
        }

        ImGui::PushID((int)LayerIdx);

        float PanelWidth = ImGui::GetContentRegionAvail().x;
        ImVec2 CardStartPos = ImGui::GetCursorScreenPos();
        float HeaderRowHeight = 52.0f;

        ImU32 CardBg = Layer.CardExpanded ? Theme.Palette.PanelBackground : IM_COL32(0, 0, 0, 0);
        ImU32 CardBd = Layer.CardExpanded ? Theme.Palette.AccentPrimary : IM_COL32(28, 28, 28, 255);

        ImGui::BeginGroup();

        // --- Row Header Area ------------------------------------------------------------------------------------------
        ImVec2 RowMin = ImGui::GetCursorScreenPos();
        ImVec2 RowMax = ImVec2(RowMin.x + PanelWidth, RowMin.y + HeaderRowHeight);

        // Reserve height in ImGui layout cleanly
        ImGui::InvisibleButton("##row-header-area", ImVec2(PanelWidth, HeaderRowHeight));
        bool RowHovered = ImGui::IsItemHovered();
        bool RowClicked = ImGui::IsItemClicked();

        if (RowHovered && !Layer.CardExpanded)
        {
            DrawList->AddRectFilled(RowMin, RowMax, IM_COL32(26, 26, 26, 255), 10.0f);
        }

        // 4.1 Twisty Chevron
        float CursorX = RowMin.x + 8.0f;
        float CursorY = RowMin.y + (HeaderRowHeight - 16.0f) * 0.5f;
        ImVec2 ChevronMin = ImVec2(CursorX, CursorY);
        ImVec2 ChevronMax = ImVec2(CursorX + 16.0f, CursorY + 16.0f);

        ImU32 ChevronColor = Layer.CardExpanded ? Theme.Palette.TextPrimary : Theme.Palette.TextMuted;
        if (Layer.CardExpanded)
        {
            DrawList->AddTriangleFilled(
                ImVec2(ChevronMin.x + 3.0f, ChevronMin.y + 5.0f),
                ImVec2(ChevronMin.x + 13.0f, ChevronMin.y + 5.0f),
                ImVec2(ChevronMin.x + 8.0f, ChevronMin.y + 11.0f),
                ChevronColor
            );
        }
        else
        {
            DrawList->AddTriangleFilled(
                ImVec2(ChevronMin.x + 5.0f, ChevronMin.y + 3.0f),
                ImVec2(ChevronMin.x + 11.0f, ChevronMin.y + 8.0f),
                ImVec2(ChevronMin.x + 5.0f, ChevronMin.y + 13.0f),
                ChevronColor
            );
        }

        if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(ChevronMin, ChevronMax))
        {
            Layer.CardExpanded = !Layer.CardExpanded;
        }

        // 4.2 Tag Color Bar
        CursorX += 20.0f;
        ImVec2 TagMin = ImVec2(CursorX, RowMin.y + 9.0f);
        ImVec2 TagMax = ImVec2(CursorX + 4.0f, RowMin.y + HeaderRowHeight - 9.0f);
        DrawList->AddRectFilled(TagMin, TagMax, ResolveCategoryTagColor(Layer.CategoryTag), 999.0f);

        // 4.3 Thumbnail Swatch
        CursorX += 10.0f;
        ImVec2 ThumbMin = ImVec2(CursorX, RowMin.y + (HeaderRowHeight - 36.0f) * 0.5f);
        ImVec2 ThumbMax = ImVec2(ThumbMin.x + 36.0f, ThumbMin.y + 36.0f);
        DrawList->AddRectFilled(ThumbMin, ThumbMax, IM_COL32(14, 14, 14, 255), 8.0f);
        DrawList->AddRect(ThumbMin, ThumbMax, IM_COL32(36, 36, 36, 255), 8.0f);
        DrawList->AddRectFilled(ImVec2(ThumbMin.x + 4.0f, ThumbMin.y + 4.0f), ImVec2(ThumbMax.x - 4.0f, ThumbMax.y - 4.0f), ResolveCategoryTagColor(Layer.CategoryTag), 5.0f);

        // 4.4 Row Text (Title + Meta)
        CursorX += 44.0f;
        ImVec2 TitlePos = ImVec2(CursorX, RowMin.y + 10.0f);
        DrawList->AddText(TitlePos, IM_COL32(237, 237, 237, 255), Layer.LayerTitle.c_str());

        std::string MetaText = Layer.LayerTypeTitle + " · " + g_BlendModeOptions[Layer.BlendModeIndex] + " · " + std::to_string(Layer.ChannelCount) + " ch";
        ImVec2 MetaPos = ImVec2(CursorX, RowMin.y + 28.0f);
        DrawList->AddText(ImGui::GetFont(), 10.0f, MetaPos, IM_COL32(138, 138, 138, 255), MetaText.c_str());

        // 4.5 Opacity Pill Button
        float RightControlsWidth = 110.0f;
        float OpacityPillX = RowMin.x + PanelWidth - RightControlsWidth;
        ImVec2 OpacityPillMin = ImVec2(OpacityPillX, RowMin.y + 14.0f);
        ImVec2 OpacityPillMax = ImVec2(OpacityPillX + 46.0f, RowMin.y + 38.0f);

        DrawList->AddRectFilled(OpacityPillMin, OpacityPillMax, IM_COL32(10, 10, 10, 255), 999.0f);
        DrawList->AddRect(OpacityPillMin, OpacityPillMax, IM_COL32(35, 35, 35, 255), 999.0f);

        char OpacityBuffer[16];
        snprintf(OpacityBuffer, sizeof(OpacityBuffer), "%.0f%%", Layer.OpacityFraction * 100.0f);
        ImVec2 OpacityTextSize = ImGui::CalcTextSize(OpacityBuffer);
        ImVec2 OpacityTextPos = ImVec2(OpacityPillMin.x + (46.0f - OpacityTextSize.x) * 0.5f, OpacityPillMin.y + (24.0f - OpacityTextSize.y) * 0.5f);
        DrawList->AddText(OpacityTextPos, IM_COL32(180, 180, 180, 255), OpacityBuffer);

        if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(OpacityPillMin, OpacityPillMax))
        {
            Layer.OpacityFraction -= 0.25f;
            if (Layer.OpacityFraction < 0.0f) Layer.OpacityFraction = 1.0f;
        }

        // 4.6 Action Icons (Eye Visibility + Trash Delete)
        float IconX = OpacityPillX + 54.0f;
        ImVec2 EyeMin = ImVec2(IconX, RowMin.y + 16.0f);
        ImVec2 EyeMax = ImVec2(IconX + 20.0f, RowMin.y + 36.0f);

        ImU32 EyeColor = Layer.VisibilityEnabled ? IM_COL32(235, 235, 235, 255) : IM_COL32(90, 90, 90, 255);
        DrawList->AddCircle(ImVec2(EyeMin.x + 10.0f, EyeMin.y + 10.0f), 5.0f, EyeColor, 12, 1.5f);
        if (!Layer.VisibilityEnabled)
        {
            DrawList->AddLine(ImVec2(EyeMin.x + 3.0f, EyeMin.y + 3.0f), ImVec2(EyeMin.x + 17.0f, EyeMin.y + 17.0f), EyeColor, 1.5f);
        }

        if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(EyeMin, EyeMax))
        {
            Layer.VisibilityEnabled = !Layer.VisibilityEnabled;
        }

        IconX += 24.0f;
        ImVec2 TrashMin = ImVec2(IconX, RowMin.y + 16.0f);
        ImVec2 TrashMax = ImVec2(IconX + 20.0f, RowMin.y + 36.0f);
        DrawList->AddRect(ImVec2(TrashMin.x + 4.0f, TrashMin.y + 6.0f), ImVec2(TrashMin.x + 16.0f, TrashMin.y + 18.0f), IM_COL32(140, 140, 140, 255), 2.0f);
        DrawList->AddLine(ImVec2(TrashMin.x + 2.0f, TrashMin.y + 6.0f), ImVec2(TrashMin.x + 18.0f, TrashMin.y + 6.0f), IM_COL32(140, 140, 140, 255), 1.5f);

        if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(TrashMin, TrashMax))
        {
            IndexToDelete = (int)LayerIdx;
        }
        else if (RowClicked && !ImGui::IsMouseHoveringRect(ChevronMin, ChevronMax) && !ImGui::IsMouseHoveringRect(OpacityPillMin, OpacityPillMax) && !ImGui::IsMouseHoveringRect(EyeMin, EyeMax) && !ImGui::IsMouseHoveringRect(TrashMin, TrashMax))
        {
            Model.ActiveLayerIndex = (int)LayerIdx;
        }

        // --- Expanded Fused Body --------------------------------------------------------------------------------------
        if (Layer.CardExpanded)
        {
            ImGui::Indent(4.0f);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));

            // ── Section 1: PROPERTIES ──
            {
                SectionHeaderDescriptor PropHeader = {};
                PropHeader.Title = "PROPERTIES";
                PropHeader.Expanded = &Layer.PropertiesExpanded;
                PropHeader.FoldFraction = Layer.PropertiesExpanded ? 1.0f : 0.0f;

                if (ConstructSectionHeader(Theme, PropHeader))
                {
                    Layer.PropertiesExpanded = !Layer.PropertiesExpanded;
                }

                if (Layer.PropertiesExpanded)
                {
                    ContentSectionDescriptor SectionDesc = {};
                    SectionDesc.Identifier = "##sec-properties";
                    SectionDesc.FixedHeight = 0.0f;

                    BeginContentSection(Theme, SectionDesc);

                    BooleanEntryDescriptor VisToggle = {};
                    VisToggle.Label = "Visible";
                    VisToggle.Value = &Layer.VisibilityEnabled;
                    VisToggle.Enabled = true;
                    ConstructBooleanEntry(Theme, VisToggle);

                    DropdownDescriptor BlendDrop = {};
                    BlendDrop.Label = "Blend";
                    BlendDrop.SelectedIndex = &Layer.BlendModeIndex;
                    BlendDrop.Options = g_BlendModeOptions;
                    BlendDrop.OptionCount = g_BlendModeCount;
                    BlendDrop.Enabled = true;
                    ConstructDropdown(Theme, BlendDrop);

                    ValueSliderDescriptor OpacSlider = {};
                    OpacSlider.Label = "Opacity";
                    OpacSlider.Value = &Layer.OpacityFraction;
                    OpacSlider.Minimum = 0.0f;
                    OpacSlider.Maximum = 1.0f;
                    OpacSlider.Format = "%.2f";
                    OpacSlider.Unit = "%";
                    OpacSlider.Enabled = true;
                    ConstructValueSlider(Theme, OpacSlider);

                    DropdownDescriptor ResDrop = {};
                    ResDrop.Label = "Resolution";
                    ResDrop.SelectedIndex = &Layer.ResolutionIndex;
                    ResDrop.Options = g_ResolutionOptions;
                    ResDrop.OptionCount = g_ResolutionCount;
                    ResDrop.Enabled = true;
                    ConstructDropdown(Theme, ResDrop);

                    EndContentSection(Theme);
                }
            }

            // ── Section 2: COLOUR ──
            {
                SectionHeaderDescriptor ColourHeader = {};
                ColourHeader.Title = "COLOUR";
                ColourHeader.Expanded = &Layer.ColourExpanded;
                ColourHeader.FoldFraction = Layer.ColourExpanded ? 1.0f : 0.0f;

                if (ConstructSectionHeader(Theme, ColourHeader))
                {
                    Layer.ColourExpanded = !Layer.ColourExpanded;
                }

                if (Layer.ColourExpanded)
                {
                    ContentSectionDescriptor SectionDesc = {};
                    SectionDesc.Identifier = "##sec-colour";
                    SectionDesc.FixedHeight = 0.0f;

                    BeginContentSection(Theme, SectionDesc);

                    int CategoryIdx = (int)Layer.CategoryTag;
                    DropdownDescriptor CatDrop = {};
                    CatDrop.Label = "Tag";
                    CatDrop.SelectedIndex = &CategoryIdx;
                    CatDrop.Options = g_CategoryOptions;
                    CatDrop.OptionCount = g_CategoryCount;
                    CatDrop.Enabled = true;
                    if (ConstructDropdown(Theme, CatDrop))
                    {
                        Layer.CategoryTag = (LayerTagCategory)CategoryIdx;
                    }

                    EndContentSection(Theme);
                }
            }

            // ── Section 3: MASK ──
            {
                SectionHeaderDescriptor MaskHeader = {};
                MaskHeader.Title = "MASK";
                MaskHeader.Expanded = &Layer.MaskExpanded;
                MaskHeader.FoldFraction = Layer.MaskExpanded ? 1.0f : 0.0f;

                if (ConstructSectionHeader(Theme, MaskHeader))
                {
                    Layer.MaskExpanded = !Layer.MaskExpanded;
                }

                if (Layer.MaskExpanded)
                {
                    ContentSectionDescriptor SectionDesc = {};
                    SectionDesc.Identifier = "##sec-mask";
                    SectionDesc.FixedHeight = 0.0f;

                    BeginContentSection(Theme, SectionDesc);

                    if (!Layer.LayerMask.MaskEnabled)
                    {
                        if (ImGui::Button("+ Add Mask", ImVec2(ImGui::GetContentRegionAvail().x, 28.0f)))
                        {
                            Layer.LayerMask.MaskEnabled = true;
                        }
                    }
                    else
                    {
                        ImVec2 MskPos = ImGui::GetCursorScreenPos();
                        DrawList->AddRectFilled(MskPos, ImVec2(MskPos.x + 44.0f, MskPos.y + 44.0f), IM_COL32(40, 40, 40, 255), 8.0f);
                        DrawList->AddRect(MskPos, ImVec2(MskPos.x + 44.0f, MskPos.y + 44.0f), IM_COL32(60, 60, 60, 255), 8.0f);

                        DrawList->AddText(ImVec2(MskPos.x + 52.0f, MskPos.y + 4.0f), IM_COL32(230, 230, 230, 255), Layer.LayerMask.MaskName.c_str());
                        std::string MaskMetaStr = (Layer.LayerMask.MaskFillModeIndex == 0 ? "Black fill" : "White fill") + std::string(" · ") + std::to_string(Layer.LayerMask.MaskComponents.size()) + " comp";
                        DrawList->AddText(ImVec2(MskPos.x + 52.0f, MskPos.y + 24.0f), IM_COL32(130, 130, 130, 255), MaskMetaStr.c_str());

                        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 48.0f));

                        float ModeBtnWidth = (ImGui::GetContentRegionAvail().x - 10.0f) / 3.0f;
                        const char* Modes[3] = { "Black", "White", "Invert" };

                        for (int m = 0; m < 3; ++m)
                        {
                            if (m > 0) ImGui::SameLine();
                            bool IsActive = (Layer.LayerMask.MaskFillModeIndex == m);
                            if (IsActive)
                            {
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.23f, 0.51f, 0.96f, 1.0f));
                            }
                            else
                            {
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                            }

                            if (ImGui::Button(Modes[m], ImVec2(ModeBtnWidth, 26.0f)))
                            {
                                Layer.LayerMask.MaskFillModeIndex = m;
                            }
                            ImGui::PopStyleColor();
                        }

                        ValueSliderDescriptor MaskOpacSlider = {};
                        MaskOpacSlider.Label = "Mask opacity";
                        MaskOpacSlider.Value = &Layer.LayerMask.MaskOpacityFraction;
                        MaskOpacSlider.Minimum = 0.0f;
                        MaskOpacSlider.Maximum = 1.0f;
                        MaskOpacSlider.Format = "%.2f";
                        MaskOpacSlider.Unit = "%";
                        MaskOpacSlider.Enabled = true;
                        ConstructValueSlider(Theme, MaskOpacSlider);

                        for (size_t c = 0; c < Layer.LayerMask.MaskComponents.size(); ++c)
                        {
                            ImGui::BulletText("%s (%s)", Layer.LayerMask.MaskComponents[c].ComponentName.c_str(), Layer.LayerMask.MaskComponents[c].ComponentType.c_str());
                        }

                        if (ImGui::Button("+ Add component", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f)))
                        {
                            Layer.LayerMask.MaskComponents.push_back({ "Cavity Generator", "Generator" });
                        }

                        if (ImGui::Button("Remove mask", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f)))
                        {
                            Layer.LayerMask.MaskEnabled = false;
                        }
                    }

                    EndContentSection(Theme);
                }
            }

            ImGui::Unindent(4.0f);
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
        }

        ImGui::EndGroup();

        if (Layer.CardExpanded)
        {
            ImVec2 CardEndPos = ImGui::GetItemRectMax();
            DrawList->AddRect(CardStartPos, CardEndPos, CardBd, 10.0f, 0, 1.5f);
        }

        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }

    if (IndexToDelete >= 0 && IndexToDelete < (int)Model.LayerRecords.size())
    {
        Model.LayerRecords.erase(Model.LayerRecords.begin() + IndexToDelete);
        if (Model.ActiveLayerIndex >= (int)Model.LayerRecords.size())
        {
            Model.ActiveLayerIndex = (int)Model.LayerRecords.size() - 1;
        }
    }

    // ════════════════════════════════════════════════════════════════════════════════
    // 5. STATUS BAR
    // ════════════════════════════════════════════════════════════════════════════════
    {
        float StatusHeight = 24.0f;
        float AvailWidth = ImGui::GetContentRegionAvail().x;
        ImVec2 StatusMin = ImGui::GetCursorScreenPos();
        ImVec2 StatusMax = ImVec2(StatusMin.x + AvailWidth, StatusMin.y + StatusHeight);

        DrawList->AddRectFilled(StatusMin, StatusMax, Theme.Palette.PanelHeader, 4.0f);
        DrawList->AddRect(StatusMin, StatusMax, Theme.Palette.PanelBorder, 4.0f);

        std::string StatusStr = std::to_string(Model.LayerRecords.size()) + " layers";
        ImVec2 StatusTextPos = ImVec2(StatusMin.x + 10.0f, StatusMin.y + 4.0f);
        DrawList->AddText(StatusTextPos, IM_COL32(138, 138, 138, 255), StatusStr.c_str());

        ImGui::Dummy(ImVec2(AvailWidth, StatusHeight));
    }

    ImGui::PopStyleVar(2);
}
