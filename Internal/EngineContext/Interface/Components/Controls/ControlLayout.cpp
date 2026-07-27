/*==============================================================================================================================================
                                                              CONTROLLAYOUT.CPP
==============================================================================================================================================*/
// 🧩 The one implementation of the label/field split AND the shared pill / slider-track painters. All controls route through here, so changing
//    the row rhythm or the pill look re-styles every control at once. The pill painters draw with the window draw list; the editable number
//    field is a native ImGui drag-widget positioned inside the pill's black segment so real text editing keeps working.

#include "ControlLayout.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 A pill with PillRounding >= half its height reads as a full capsule; clamp so ImDrawList never over-rounds a short pill.
    float ResolveRounding(const ThemeConfiguration& Theme, float Height)
    {
        const float Half = Height * 0.5f;
        return Theme.Metrics.PillRounding >= Half ? Half : Theme.Metrics.PillRounding;
    }

    // 📝 Temporarily rescale the current font by a factor (ImGui 1.92 way: keep the font, set an unscaled base size). Pairs with PopFont.
    void PushScaledFont(float Factor)
    {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * Factor);
    }

    // 📝 Draw one grey side-segment (axis letter or unit suffix) with its glyph centred + muted. Corners rounded only on the outer edge so
    //    the segment butts flush against the central black number.
    void DrawSideSegment(const ThemeConfiguration& Theme, ImVec2 Min, ImVec2 Max, const char* Text, float Rounding, ImDrawFlags OuterCorners)
    {
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        Draw->AddRectFilled(Min, Max, Theme.Palette.ValueSideSegment, Rounding, OuterCorners);

        if (Text && Text[0] != '\0')
        {
            PushScaledFont(Theme.Metrics.SegmentFontScale);

            const ImVec2 TextSize = ImGui::CalcTextSize(Text);
            const ImVec2 Where(Min.x + (Max.x - Min.x - TextSize.x) * 0.5f,
                               Min.y + (Max.y - Min.y - TextSize.y) * 0.5f);
            Draw->AddText(Where, Theme.Palette.TextMuted, Text);

            ImGui::PopFont();
        }
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float BeginControlRow(const ThemeConfiguration& Theme, const char* Label)
{
    const float RowWidth   = ImGui::GetContentRegionAvail().x;
    const float LabelWidth = RowWidth * Theme.Metrics.LabelColumnRatio;
    const float FieldWidth = RowWidth - LabelWidth;

    // 📝 Label drawn muted, vertically centred against the (taller) pill on the same line.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Label);
    ImGui::SameLine(LabelWidth);

    // 📝 Hand the field the exact remaining width so trailing widgets line up regardless of label length.
    ImGui::SetNextItemWidth(FieldWidth > 1.0f ? FieldWidth : 1.0f);
    return FieldWidth;
}


void EndControlRow(const ThemeConfiguration& Theme)
{
    (void)Theme;   // 📝 Spacing is carried by ImGuiStyle.ItemSpacing (mirrored from the theme in EnforceThemeStyle); nothing extra to add.
}


float ResolvePillHeight(const ThemeConfiguration& Theme)
{
    const float Themed = Theme.Metrics.PillRowHeight;
    const float Floor  = ImGui::GetFrameHeight();
    return Themed > Floor ? Themed : Floor;
}


ValuePillLayout DrawValuePill(const ThemeConfiguration& Theme, ImVec2 TopLeft, ImVec2 Size,
                              const char* AxisText, const char* UnitText, bool Enabled)
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();

    ValuePillLayout Layout = {};
    Layout.Height  = Size.y;
    Layout.PillMin = TopLeft;
    Layout.PillMax = ImVec2(TopLeft.x + Size.x, TopLeft.y + Size.y);

    const float Rounding = ResolveRounding(Theme, Size.y);
    const float SegW     = Theme.Metrics.SideSegmentWidth;
    const bool  HasAxis  = AxisText && AxisText[0] != '\0';
    const bool  HasUnit  = UnitText && UnitText[0] != '\0';

    // 📝 The black number segment fills what the grey caps leave behind.
    Layout.NumberMin = ImVec2(Layout.PillMin.x + (HasAxis ? SegW : 0.0f), Layout.PillMin.y);
    Layout.NumberMax = ImVec2(Layout.PillMax.x - (HasUnit ? SegW : 0.0f), Layout.PillMax.y);

    // 📝 Base fill: the whole capsule painted black first, then grey caps overlaid so the joins are seamless.
    Draw->AddRectFilled(Layout.PillMin, Layout.PillMax, Theme.Palette.ValueNumberSegment, Rounding);

    if (HasAxis)
    {
        DrawSideSegment(Theme, Layout.PillMin, ImVec2(Layout.NumberMin.x, Layout.PillMax.y),
                        AxisText, Rounding, ImDrawFlags_RoundCornersLeft);
    }
    if (HasUnit)
    {
        DrawSideSegment(Theme, ImVec2(Layout.NumberMax.x, Layout.PillMin.y), Layout.PillMax,
                        UnitText, Rounding, ImDrawFlags_RoundCornersRight);
    }

    // 📝 No outline: the pill reads as a flat filled capsule (the caller asked for no border on the [ number | unit ] pill).

    return Layout;
}


bool EditPillNumber(const ThemeConfiguration& Theme, const ValuePillLayout& Layout, const char* Identifier,
                    float* Value, float Speed, float Minimum, float Maximum, const char* Format)
{
    if (Value == nullptr)
    {
        return false;
    }

    // 📝 Park the native drag-widget exactly over the black number segment. Transparent frame => only the typed number shows, but the widget
    //    still owns real editing (drag-scrub, double-click-select, text cursor, keyboard).
    const ImVec2 SegMin  = Layout.NumberMin;
    const ImVec2 SegSize(Layout.NumberMax.x - Layout.NumberMin.x, Layout.NumberMax.y - Layout.NumberMin.y);

    ImGui::SetCursorScreenPos(SegMin);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,           Theme.Palette.ValueText);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, (SegSize.y - ImGui::GetFontSize()) * 0.5f));

    // 📝 Enlarge just the numeric glyphs to the themed numeric scale (tabular, prominent) — like the HTML readout.
    PushScaledFont(Theme.Metrics.NumericFontScale);

    ImGui::PushID(Identifier);
    ImGui::SetNextItemWidth(SegSize.x > 1.0f ? SegSize.x : 1.0f);
    const bool Changed = ImGui::DragFloat("##pillnum", Value, Speed, Minimum, Maximum,
                                          Format ? Format : "%.3f", ImGuiSliderFlags_None);
    ImGui::PopID();

    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);

    return Changed;
}


ImVec2 DrawSliderTrack(const ThemeConfiguration& Theme, ImVec2 TopLeft, ImVec2 Size, float Fraction, bool Enabled)
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();

    const ImVec2 Min = TopLeft;
    const ImVec2 Max = ImVec2(TopLeft.x + Size.x, TopLeft.y + Size.y);
    const float  Rounding = Size.y * 0.5f;

    // 📝 Pitch-black track + white hairline.
    Draw->AddRectFilled(Min, Max, Theme.Palette.SliderTrack, Rounding);

    // 📝 Grey travelled fill up to Fraction (skipped when Fraction < 0 for an unbounded drag field).
    const float Clamped = Fraction < 0.0f ? -1.0f : (Fraction > 1.0f ? 1.0f : Fraction);
    if (Clamped >= 0.0f)
    {
        const float FillRight = Min.x + Size.x * Clamped;
        if (FillRight > Min.x + 1.0f)
        {
            Draw->AddRectFilled(Min, ImVec2(FillRight, Max.y), Theme.Palette.SliderFill, Rounding);
        }
    }

    // 📝 No track outline: the UVeditor .slider is a borderless capsule (matches the no-outline pill request).

    // 📝 White circular knob centred at Fraction (or centred when the fill is hidden).
    const float KnobFrac = Clamped >= 0.0f ? Clamped : 0.5f;
    const ImVec2 Knob(Min.x + Size.x * KnobFrac, (Min.y + Max.y) * 0.5f);
    const float  KnobRadius = Size.y * 0.5f + 1.0f;
    Draw->AddCircleFilled(Knob, KnobRadius, Theme.Palette.SliderKnob, 24);
    Draw->AddCircle(Knob, KnobRadius, ImGui::GetColorU32(IM_COL32(0, 0, 0, 90)), 24, 1.0f);

    return Knob;
}

}   // namespace Frontier
