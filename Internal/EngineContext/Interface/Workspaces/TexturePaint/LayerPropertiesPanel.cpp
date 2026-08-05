/*==============================================================================================================================================
                                                         LAYERPROPERTIESPANEL.CPP
==============================================================================================================================================*/
// 🧩 The mask property column. Everything that CAN be a shared control IS one — SelectionEntry draws the White/Black, Off/On, Painted/Generated
//    chip rows; ValueSlider draws Strength and every generator parameter; PropertyPanelBase draws the card + its fold. Only three strips are
//    bespoke, because no shared control describes them: the mask preview swatch (a checkerboard under a resolved grey), the painted/imported
//    texture slot (thumb + two text lines + trailing round buttons), and the generator head pill with its GROUPED popup (the shared Dropdown
//    is flat, and this catalogue carries group headings + a per-row summary note).
//
// 🔴 The bespoke strips still take every tone + radius from the theme; nothing here hardcodes a colour. The only literals are geometry, and
//    they sit in one named block so the C++ and the source design can be diffed line by line.

#include "LayerPropertiesPanel.h"

#include "EngineContext/Interface/Components/PropertyPanelBase.h"

#include "EngineContext/Interface/Components/Controls/ControlLayout.h"

#include "EngineContext/Interface/Components/Controls/SelectionEntry.h"

#include "EngineContext/Interface/Components/Controls/ValueSlider.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // -- Geometry ---------------------------------------------------------------------------------------------------------
    // 📝 One block, transcribed from the source design so a side-by-side diff is mechanical. Tones come from the theme.
    constexpr float SwatchEdge        = 34.0f;   // [px] - Mask preview swatch, outer square
    constexpr float SwatchRound       =  7.0f;   // [px] - Swatch corner radius
    constexpr float CheckerCell       =  4.0f;   // [px] - Transparency checkerboard cell under the preview grey
    constexpr float SlotThumbEdge     = 38.0f;   // [px] - Texture-slot thumbnail square
    constexpr float SlotThumbRound    =  6.0f;   // [px] - Thumbnail corner radius
    constexpr float SlotRound         =  9.0f;   // [px] - Texture-slot outer strip radius
    constexpr float SlotPad           =  6.0f;   // [px] - Texture-slot inner padding
    constexpr float SlotGap           =  9.0f;   // [px] - Gap between thumb, text block and buttons
    constexpr float SlotButtonEdge    = 24.0f;   // [px] - Trailing round button square
    constexpr float SlotButtonRound   =  6.0f;   // [px] - Trailing round button radius
    constexpr float SlotButtonGap     =  4.0f;   // [px] - Gap between trailing buttons
    constexpr float ImportStripHeight = 44.0f;   // [px] - Dashed "Import base mask" placeholder height
    constexpr float DashLength        =  5.0f;   // [px] - Dashed-border dash
    constexpr float DashGap           =  4.0f;   // [px] - Dashed-border gap
    constexpr float RuleInset         =  4.0f;   // [px] - Horizontal inset of the section divider rule
    constexpr float RuleMargin        = 10.0f;   // [px] - Vertical space around the section divider rule
    constexpr float CaptionGapTop     =  8.0f;   // [px] - Space above a small ALL-CAPS caption
    constexpr float CaptionGapBottom  =  3.0f;   // [px] - Space below a small ALL-CAPS caption
    constexpr float CaptionScale      = 0.82f;   // [-]  - Font scale of a small ALL-CAPS caption
    constexpr float MetaScale         = 0.86f;   // [-]  - Font scale of a secondary meta line
    constexpr float PopupItemPad      =  8.0f;   // [px] - Generator popup, item text inset
    constexpr float PopupItemPadHover = 11.0f;   // [px] - Generator popup, item text inset while hovered (the nudge-right tell)
    constexpr float PopupWindowPad    =  6.0f;   // [px] - Generator popup window padding
    constexpr float PopupItemGap      =  2.0f;   // [px] - Generator popup, gap between rows
    constexpr float PopupRound        =  6.0f;   // [px] - Generator popup, item hover-fill radius
    constexpr float TickWeight        =  2.0f;   // [px] - Stroke weight of the chosen-generator tick

    // -- Vocabulary -------------------------------------------------------------------------------------------------------
    const char* const GroupTable[MaskGeneratorGroupCapacity] = { "Mask", "Wear", "Procedural" };

    const MaskGeneratorDefinition GeneratorTable[MaskGeneratorCapacity] =
    {
        { "Curvature",         "Mask",       "Convex edge wear",
          { { "Balance",   0.0f, 1.0f, 0.50f }, { "Contrast", 0.0f, 1.0f, 0.70f }, { "Radius",   0.0f, 1.0f, 0.25f } }, 3 },
        { "Ambient Occlusion", "Mask",       "Cavity dirt",
          { { "Spread",    0.0f, 1.0f, 0.40f }, { "Contrast", 0.0f, 1.0f, 0.60f }, { nullptr,    0.0f, 0.0f, 0.00f } }, 2 },
        { "Metal Edge Wear",   "Wear",       "Curvature + grunge",
          { { "Intensity", 0.0f, 1.0f, 0.60f }, { "Softness", 0.0f, 1.0f, 0.30f }, { "Grain",    0.0f, 1.0f, 0.45f } }, 3 },
        { "Dirt",              "Wear",       "Occlusion-driven",
          { { "Amount",    0.0f, 1.0f, 0.50f }, { "Scale",    0.0f, 1.0f, 0.30f }, { nullptr,    0.0f, 0.0f, 0.00f } }, 2 },
        { "Perlin Noise",      "Procedural", "Fractal value noise",
          { { "Scale",     0.0f, 1.0f, 0.40f }, { "Octaves",  0.0f, 1.0f, 0.50f }, { "Contrast", 0.0f, 1.0f, 0.50f } }, 3 }
    };

    const char* const BaseToneOptions[2] = { "White", "Black" };
    const char* const InvertOptions[2]   = { "Off",   "On"    };
    const char* const SourceOptions[2]   = { "Painted", "Generated" };


    // -- Tone helpers -----------------------------------------------------------------------------------------------------

    // 📝 Blend two packed colours by Amount (0 => First, 1 => Second). Used for the muted/danger button tints.
    ImU32 MixColour(ImU32 First, ImU32 Second, float Amount)
    {
        const ImVec4 A = ImGui::ColorConvertU32ToFloat4(First);
        const ImVec4 B = ImGui::ColorConvertU32ToFloat4(Second);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(A.x + (B.x - A.x) * Amount,
                                                     A.y + (B.y - A.y) * Amount,
                                                     A.z + (B.z - A.z) * Amount,
                                                     A.w + (B.w - A.w) * Amount));
    }

    // 📝 A flat grey from a 0-1 luminance, fully opaque — what the preview swatch and the checkerboard are painted with.
    ImU32 GreyTone(float Luminance)
    {
        const float Clamped = Luminance < 0.0f ? 0.0f : (Luminance > 1.0f ? 1.0f : Luminance);
        const int   Level   = static_cast<int>(Clamped * 255.0f + 0.5f);
        return IM_COL32(Level, Level, Level, 255);
    }


    // -- Small text primitives --------------------------------------------------------------------------------------------

    // 📝 Draw text at a scaled font size and report its height so the caller can stack lines.
    float DrawScaledText(ImVec2 Where, const char* Text, ImU32 Tint, float Scale)
    {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * Scale);
        const float Height = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddText(Where, Tint, Text);
        ImGui::PopFont();
        return Height;
    }

    // 📝 Draw text clipped to MaxWidth so a long filename cannot spill past its column. The clip rect is a NAMED local — ImDrawList keeps
    //    only a pointer to it for the duration of the call, so a temporary's address would dangle.
    float DrawClippedText(ImVec2 Where, const char* Text, ImU32 Tint, float Scale, float MaxWidth)
    {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * Scale);
        const float  Height = ImGui::GetTextLineHeight();
        const float  Span   = MaxWidth > 0.0f ? MaxWidth : 0.0f;
        const ImVec4 Clip(Where.x, Where.y, Where.x + Span, Where.y + Height * 2.0f);
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), Where, Tint, Text, nullptr, 0.0f, &Clip);
        ImGui::PopFont();
        return Height;
    }

    // 📝 One ALL-CAPS section caption ("IMPORTED BASE"), advancing the layout cursor past it.
    void RecordCaption(const ThemeConfiguration& Theme, const char* Text)
    {
        ImGui::Dummy(ImVec2(0.0f, CaptionGapTop));
        const ImVec2 Where = ImGui::GetCursorScreenPos();
        const float  Height = DrawScaledText(Where, Text, Theme.Palette.TextMuted, CaptionScale);
        ImGui::SetCursorScreenPos(ImVec2(Where.x, Where.y + Height + CaptionGapBottom));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    // 📝 One full-width hairline rule separating the tone rows from the source rows.
    void RecordRule(const ThemeConfiguration& Theme)
    {
        ImGui::Dummy(ImVec2(0.0f, RuleMargin));
        const ImVec2 Where = ImGui::GetCursorScreenPos();
        const float  Width = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(Where.x + RuleInset, Where.y),
                                            ImVec2(Where.x + Width - RuleInset, Where.y),
                                            Theme.Palette.PanelBorder, 1.0f);
        ImGui::Dummy(ImVec2(0.0f, RuleMargin));
    }


    // -- Bespoke strips ---------------------------------------------------------------------------------------------------

    // 📝 The transparency checkerboard every mask thumbnail sits on, so a low-strength mask reads as partly transparent.
    void DrawCheckerboard(ImDrawList* Draw, ImVec2 Min, ImVec2 Max, const ThemeConfiguration& Theme, float Rounding)
    {
        Draw->AddRectFilled(Min, Max, Theme.Palette.ValueNumberSegment, Rounding);
        Draw->PushClipRect(Min, Max, true);
        int Row = 0;
        for (float Y = Min.y; Y < Max.y; Y += CheckerCell, ++Row)
        {
            int Column = 0;
            for (float X = Min.x; X < Max.x; X += CheckerCell, ++Column)
            {
                if (((Row + Column) & 1) == 0)
                {
                    continue;
                }
                const ImVec2 CellMax(X + CheckerCell < Max.x ? X + CheckerCell : Max.x,
                                     Y + CheckerCell < Max.y ? Y + CheckerCell : Max.y);
                Draw->AddRectFilled(ImVec2(X, Y), CellMax, Theme.Palette.ControlActive, 0.0f);
            }
        }
        Draw->PopClipRect();
    }

    // 📝 A trailing round button inside a slot strip. Returns true on click.
    // 🔴 The source design tints destructive buttons red on hover; this palette has NO danger tone and inventing one would be the first
    //    hardcoded colour in the file. A destructive button therefore reads by its glyph, and hover brightens the ink like any other button.
    //    Add a Danger field to ColorPaletteDescriptor if the distinction is ever wanted — do not spot-fix it here.
    bool RecordSlotButton(const ThemeConfiguration& Theme, const char* Identifier, ImVec2 Where, char Mark, bool Enabled)
    {
        ImDrawList*  Draw = ImGui::GetWindowDrawList();
        const ImVec2 Max(Where.x + SlotButtonEdge, Where.y + SlotButtonEdge);

        ImGui::SetCursorScreenPos(Where);
        ImGui::PushID(Identifier);
        const bool Pressed = ImGui::InvisibleButton("##slotbutton", ImVec2(SlotButtonEdge, SlotButtonEdge)) && Enabled;
        const bool Hovered = ImGui::IsItemHovered() && Enabled;
        ImGui::PopID();

        const ImU32 Fill = Hovered ? Theme.Palette.ControlActive : Theme.Palette.PanelBackground;
        const ImU32 Edge = Hovered ? Theme.Palette.ValueOutline  : Theme.Palette.PanelBorder;
        const ImU32 Ink  = !Enabled ? MixColour(Theme.Palette.PanelBackground, Theme.Palette.TextMuted, 0.45f)
                                    : (Hovered ? Theme.Palette.TextPrimary : Theme.Palette.TextMuted);
        Draw->AddRectFilled(Where, Max, Fill, SlotButtonRound);
        Draw->AddRect(Where, Max, Edge, SlotButtonRound, ImDrawFlags_None, 1.0f);

        const char   Glyph[2] = { Mark, '\0' };
        const ImVec2 Size     = ImGui::CalcTextSize(Glyph);
        Draw->AddText(ImVec2(Where.x + (SlotButtonEdge - Size.x) * 0.5f, Where.y + (SlotButtonEdge - Size.y) * 0.5f), Ink, Glyph);

        return Pressed;
    }

    // 📝 One slot strip: rounded fill, a thumbnail, a title + meta line, and up to two trailing buttons. Returns the strip's height so
    //    the caller can advance the cursor. The button outcomes are reported through the two out flags.
    float RecordSlotStrip(const ThemeConfiguration& Theme, const char* Identifier,
                          bool ThumbLit, float ThumbTone, const char* Title, const char* Meta,
                          const char* LeadMark, const char* TrailMark, bool TrailEnabled,
                          bool& LeadPressed, bool& TrailPressed)
    {
        LeadPressed  = false;
        TrailPressed = false;

        ImDrawList*  Draw   = ImGui::GetWindowDrawList();
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float  Width  = ImGui::GetContentRegionAvail().x;
        const float  Height = SlotThumbEdge + SlotPad * 2.0f;
        const ImVec2 Max(Origin.x + Width, Origin.y + Height);

        Draw->AddRectFilled(Origin, Max, Theme.Palette.ControlBackground, SlotRound);
        Draw->AddRect(Origin, Max, Theme.Palette.PanelBorder, SlotRound, ImDrawFlags_None, 1.0f);

        // -- Thumbnail ----------------------------------------------------------------------------------------------------
        const ImVec2 ThumbMin(Origin.x + SlotPad, Origin.y + SlotPad);
        const ImVec2 ThumbMax(ThumbMin.x + SlotThumbEdge, ThumbMin.y + SlotThumbEdge);
        if (ThumbLit)
        {
            Draw->AddRectFilled(ThumbMin, ThumbMax, GreyTone(ThumbTone), SlotThumbRound);
        }
        else
        {
            DrawCheckerboard(Draw, ThumbMin, ThumbMax, Theme, SlotThumbRound);
        }
        Draw->AddRect(ThumbMin, ThumbMax, Theme.Palette.PanelBorder, SlotThumbRound, ImDrawFlags_None, 1.0f);

        // -- Trailing buttons (laid out right-to-left so the text column can be sized against them) -----------------------
        const int   ButtonCount = (LeadMark != nullptr ? 1 : 0) + (TrailMark != nullptr ? 1 : 0);
        const float ButtonBlock = ButtonCount > 0 ? ButtonCount * SlotButtonEdge + (ButtonCount - 1) * SlotButtonGap : 0.0f;
        const float ButtonY     = Origin.y + (Height - SlotButtonEdge) * 0.5f;
        float       ButtonX     = Max.x - SlotPad - ButtonBlock;

        ImGui::PushID(Identifier);
        if (LeadMark != nullptr)
        {
            LeadPressed = RecordSlotButton(Theme, "##lead", ImVec2(ButtonX, ButtonY), LeadMark[0], true);
            ButtonX += SlotButtonEdge + SlotButtonGap;
        }
        if (TrailMark != nullptr)
        {
            TrailPressed = RecordSlotButton(Theme, "##trail", ImVec2(ButtonX, ButtonY), TrailMark[0], TrailEnabled);
        }
        ImGui::PopID();

        // -- Text column --------------------------------------------------------------------------------------------------
        const float TextLeft  = ThumbMax.x + SlotGap;
        const float TextWidth = (Max.x - SlotPad - ButtonBlock - SlotGap) - TextLeft;
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase);
        const float TitleH = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * MetaScale);
        const float MetaH = ImGui::GetTextLineHeight();
        ImGui::PopFont();

        const float BlockTop = Origin.y + (Height - (TitleH + MetaH + 2.0f)) * 0.5f;
        DrawClippedText(ImVec2(TextLeft, BlockTop), Title, Theme.Palette.TextPrimary, 1.0f, TextWidth);
        DrawClippedText(ImVec2(TextLeft, BlockTop + TitleH + 2.0f), Meta, Theme.Palette.TextMuted, MetaScale, TextWidth);

        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Max.y));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        return Height;
    }

    // 📝 The dashed "Import base mask" placeholder shown when no base texture is bound. Returns true on click.
    bool RecordImportStrip(const ThemeConfiguration& Theme, const char* Caption)
    {
        ImDrawList*  Draw   = ImGui::GetWindowDrawList();
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float  Width  = ImGui::GetContentRegionAvail().x;
        const ImVec2 Max(Origin.x + Width, Origin.y + ImportStripHeight);

        ImGui::SetCursorScreenPos(Origin);
        const bool Pressed = ImGui::InvisibleButton("##importbase", ImVec2(Width, ImportStripHeight));
        const bool Hovered = ImGui::IsItemHovered();

        if (Hovered)
        {
            Draw->AddRectFilled(Origin, Max, Theme.Palette.ControlHovered, SlotRound);
        }

        // 📝 A dashed border, walked segment by segment (ImDrawList has no dash pattern).
        const ImU32 Edge = Hovered ? Theme.Palette.AccentPrimary : Theme.Palette.PanelBorder;
        const float Step = DashLength + DashGap;
        for (float X = Origin.x; X < Max.x; X += Step)
        {
            const float End = X + DashLength < Max.x ? X + DashLength : Max.x;
            Draw->AddLine(ImVec2(X, Origin.y), ImVec2(End, Origin.y), Edge, 1.0f);
            Draw->AddLine(ImVec2(X, Max.y),    ImVec2(End, Max.y),    Edge, 1.0f);
        }
        for (float Y = Origin.y; Y < Max.y; Y += Step)
        {
            const float End = Y + DashLength < Max.y ? Y + DashLength : Max.y;
            Draw->AddLine(ImVec2(Origin.x, Y), ImVec2(Origin.x, End), Edge, 1.0f);
            Draw->AddLine(ImVec2(Max.x,    Y), ImVec2(Max.x,    End), Edge, 1.0f);
        }

        const ImVec2 Size = ImGui::CalcTextSize(Caption);
        Draw->AddText(ImVec2(Origin.x + (Width - Size.x) * 0.5f, Origin.y + (ImportStripHeight - Size.y) * 0.5f),
                      Hovered ? Theme.Palette.TextPrimary : Theme.Palette.TextMuted, Caption);

        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Max.y));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        return Pressed;
    }

    // 📝 The mask preview swatch + its two caption lines: a checkerboard under the resolved grey, so strength reads as transparency.
    void RecordPreviewStrip(const ThemeConfiguration& Theme, const LayerPropertiesState& State)
    {
        ImGui::Dummy(ImVec2(0.0f, CaptionGapTop));

        ImDrawList*  Draw   = ImGui::GetWindowDrawList();
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const ImVec2 SwatchMin = Origin;
        const ImVec2 SwatchMax(SwatchMin.x + SwatchEdge, SwatchMin.y + SwatchEdge);

        DrawCheckerboard(Draw, SwatchMin, SwatchMax, Theme, SwatchRound);

        // 📝 Strength is the swatch's ALPHA over the checkerboard; base tone (flipped by invert) is its grey.
        const float Tone     = ResolveMaskPreviewTone(State);
        const float Strength = State.Strength < 0.0f ? 0.0f : (State.Strength > 1.0f ? 1.0f : State.Strength);
        const int   Level    = static_cast<int>(Tone * 255.0f + 0.5f);
        const int   Alpha    = static_cast<int>(Strength * 255.0f + 0.5f);
        Draw->AddRectFilled(SwatchMin, SwatchMax, IM_COL32(Level, Level, Level, Alpha), SwatchRound);
        Draw->AddRect(SwatchMin, SwatchMax, Theme.Palette.PanelBorder, SwatchRound, ImDrawFlags_None, 1.0f);

        const float TextLeft = SwatchMax.x + SlotGap;
        const float TitleH   = DrawScaledText(ImVec2(TextLeft, SwatchMin.y + 4.0f), "Mask preview", Theme.Palette.TextMuted, 1.0f);
        DrawScaledText(ImVec2(TextLeft, SwatchMin.y + 4.0f + TitleH + 2.0f), "Material atlas \xC2\xB7 A", Theme.Palette.TextMuted, MetaScale);

        ImGui::SetCursorScreenPos(ImVec2(Origin.x, SwatchMax.y));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    // 📝 The generator head pill + its GROUPED popup. Mirrors Dropdown.cpp's chrome (same head pill, same hover fill + nudge), but adds the
    //    group headings and the per-row summary note this catalogue carries. Returns true the cycle the binding changed.
    bool RecordGeneratorChoice(const ThemeConfiguration& Theme, LayerPropertiesState& State)
    {
        const MaskGeneratorDefinition* Chosen = ResolveMaskGenerator(State);

        ImGui::PushID("##generator-choice");
        const float  FieldWidth = BeginControlRow(Theme, "Generator");
        const float  Height     = ResolvePillHeight(Theme);
        const ImVec2 Origin     = ImGui::GetCursorScreenPos();

        const ValuePillLayout Head = DrawValuePill(Theme, Origin, ImVec2(FieldWidth, Height), nullptr, ">", true);
        {
            const char*  Text     = Chosen != nullptr ? Chosen->Label : "Choose generator";
            const ImVec2 TextSize = ImGui::CalcTextSize(Text);
            ImGui::GetWindowDrawList()->AddText(ImVec2(Head.NumberMin.x + 14.0f, Head.NumberMin.y + (Height - TextSize.y) * 0.5f),
                                                Chosen != nullptr ? Theme.Palette.ValueText : Theme.Palette.TextMuted, Text);
        }

        ImGui::SetCursorScreenPos(Origin);
        if (ImGui::InvisibleButton("##head", ImVec2(FieldWidth, Height)))
        {
            ImGui::OpenPopup("##genlist");
        }

        // -- Popup ---------------------------------------------------------------------------------------------------------
        int GeneratorCount = 0;
        const MaskGeneratorDefinition* Table = ResolveMaskGeneratorTable(GeneratorCount);
        int GroupCount = 0;
        const char* const* Groups = ResolveMaskGeneratorGroupTable(GroupCount);

        bool Changed = false;
        const float ItemH   = Height * 0.86f;
        const float HeadH   = ItemH * 0.72f;

        ImGui::SetNextWindowPos(ImVec2(Origin.x, Origin.y + Height + 6.0f));
        ImGui::SetNextWindowSize(ImVec2(FieldWidth, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme.Palette.PanelHeader);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PopupWindowPad, PopupWindowPad));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0.0f, PopupItemGap));

        if (ImGui::BeginPopup("##genlist"))
        {
            ImDrawList* Draw   = ImGui::GetWindowDrawList();
            const float AvailW = ImGui::GetContentRegionAvail().x;

            for (int GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
            {
                // -- Group heading -------------------------------------------------------------------------------------
                const ImVec2 HeadWhere = ImGui::GetCursorScreenPos();
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * CaptionScale);
                const ImVec2 HeadSize = ImGui::CalcTextSize(Groups[GroupIndex]);
                Draw->AddText(ImVec2(HeadWhere.x + PopupItemPad, HeadWhere.y + (HeadH - HeadSize.y) * 0.5f),
                              Theme.Palette.TextMuted, Groups[GroupIndex]);
                ImGui::PopFont();
                ImGui::SetCursorScreenPos(ImVec2(HeadWhere.x, HeadWhere.y + HeadH));
                ImGui::Dummy(ImVec2(0.0f, 0.0f));

                // -- Rows of that group --------------------------------------------------------------------------------
                for (int Index = 0; Index < GeneratorCount; ++Index)
                {
                    if (std::strcmp(Table[Index].Group, Groups[GroupIndex]) != 0)
                    {
                        continue;
                    }

                    const bool   Selected = (Index == State.GeneratorOrdinal);
                    const ImVec2 ItemMin  = ImGui::GetCursorScreenPos();
                    const ImVec2 ItemMax(ItemMin.x + AvailW, ItemMin.y + ItemH);

                    ImGui::PushID(Index);
                    const bool Clicked = ImGui::InvisibleButton("##genitem", ImVec2(AvailW, ItemH));
                    const bool Hovered = ImGui::IsItemHovered();
                    ImGui::PopID();

                    if (Hovered)
                    {
                        Draw->AddRectFilled(ItemMin, ItemMax, Theme.Palette.ControlActive, PopupRound);
                    }

                    const ImVec2 LabelSize = ImGui::CalcTextSize(Table[Index].Label);
                    const float  Inset     = Hovered ? PopupItemPadHover : PopupItemPad;
                    Draw->AddText(ImVec2(ItemMin.x + Inset, ItemMin.y + (ItemH - LabelSize.y) * 0.5f),
                                  Theme.Palette.TextPrimary, Table[Index].Label);

                    if (Selected)
                    {
                        // 📝 A tick stroked in the accent marks the bound generator (the source design's check mark).
                        const float  Mid = (ItemMin.y + ItemMax.y) * 0.5f;
                        const ImVec2 Tip(ItemMax.x - PopupItemPad - 9.0f, Mid + 4.0f);
                        Draw->AddLine(ImVec2(Tip.x - 4.0f, Mid), Tip, Theme.Palette.AccentPrimary, TickWeight);
                        Draw->AddLine(Tip, ImVec2(Tip.x + 7.0f, Mid - 6.0f), Theme.Palette.AccentPrimary, TickWeight);
                    }
                    else
                    {
                        // 📝 The unbound rows carry their summary note instead, right-aligned + muted.
                        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * CaptionScale);
                        const ImVec2 NoteSize = ImGui::CalcTextSize(Table[Index].Summary);
                        Draw->AddText(ImVec2(ItemMax.x - PopupItemPad - NoteSize.x, ItemMin.y + (ItemH - NoteSize.y) * 0.5f),
                                      Theme.Palette.TextMuted, Table[Index].Summary);
                        ImGui::PopFont();
                    }

                    if (Clicked)
                    {
                        AlignMaskGenerator(State, Index);
                        Changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (GroupIndex + 1 < GroupCount)
                {
                    const ImVec2 RuleWhere = ImGui::GetCursorScreenPos();
                    Draw->AddLine(ImVec2(RuleWhere.x + 3.0f, RuleWhere.y + 3.0f),
                                  ImVec2(RuleWhere.x + AvailW - 3.0f, RuleWhere.y + 3.0f),
                                  Theme.Palette.PanelBorder, 1.0f);
                    ImGui::Dummy(ImVec2(0.0f, 6.0f));
                }
            }
            ImGui::EndPopup();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + Height));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        EndControlRow(Theme);
        ImGui::PopID();

        return Changed;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const MaskGeneratorDefinition* ResolveMaskGeneratorTable(int& Count)
{
    Count = MaskGeneratorCapacity;
    return GeneratorTable;
}


const char* const* ResolveMaskGeneratorGroupTable(int& Count)
{
    Count = MaskGeneratorGroupCapacity;
    return GroupTable;
}


const MaskGeneratorDefinition* ResolveMaskGenerator(const LayerPropertiesState& State)
{
    if (State.GeneratorOrdinal < 0 || State.GeneratorOrdinal >= MaskGeneratorCapacity)
    {
        return nullptr;
    }
    return &GeneratorTable[State.GeneratorOrdinal];
}


void AlignMaskGenerator(LayerPropertiesState& State, int Ordinal)
{
    if (Ordinal < 0 || Ordinal >= MaskGeneratorCapacity)
    {
        State.GeneratorOrdinal = -1;
        return;
    }
    State.GeneratorOrdinal = Ordinal;
    RestoreMaskParameters(State);
}


void RestoreMaskParameters(LayerPropertiesState& State)
{
    const MaskGeneratorDefinition* Generator = ResolveMaskGenerator(State);
    if (Generator == nullptr)
    {
        return;
    }
    for (int Index = 0; Index < MaskParameterCapacity; ++Index)
    {
        State.ParameterValues[Index] = Index < Generator->ParameterCount ? Generator->Parameters[Index].Default : 0.0f;
    }
}


float ResolveMaskPreviewTone(const LayerPropertiesState& State)
{
    const float Base = (State.BaseToneOrdinal == static_cast<int>(MaskBaseTone::Black)) ? 0.0f : 1.0f;
    return (State.InvertOrdinal != 0) ? (1.0f - Base) : Base;
}


void InitializeLayerPropertiesSample(LayerPropertiesState& State)
{
    State.MaskExpanded    = true;
    State.BaseToneOrdinal = static_cast<int>(MaskBaseTone::White);
    State.InvertOrdinal   = 0;
    State.Strength        = 1.0f;
    State.SourceOrdinal   = static_cast<int>(MaskSourceOrigin::Painted);
    State.StrokeCount     = 148;
    State.TexturePresent  = false;
    State.TextureLabel[0] = '\0';

    // 📝 Open on Metal Edge Wear so the generator branch has something to show the moment the source chip is flipped.
    AlignMaskGenerator(State, 2);
}


void ConstructLayerPropertiesPanel(const ThemeConfiguration& Theme, LayerPropertiesState& State)
{
    BeginPropertyPanel(Theme, "##layer-properties");

    if (BeginPropertyCard(Theme, "Mask", &State.MaskExpanded))
    {
        // -- Mask tone ---------------------------------------------------------------------------------------------------
        SelectionEntryDescriptor BaseTone = {};
        BaseTone.Label = "Base Mask"; BaseTone.SelectedIndex = &State.BaseToneOrdinal;
        BaseTone.Options = BaseToneOptions; BaseTone.OptionCount = 2; BaseTone.Enabled = true;
        ConstructSelectionEntry(Theme, BaseTone);

        SelectionEntryDescriptor Invert = {};
        Invert.Label = "Invert"; Invert.SelectedIndex = &State.InvertOrdinal;
        Invert.Options = InvertOptions; Invert.OptionCount = 2; Invert.Enabled = true;
        ConstructSelectionEntry(Theme, Invert);

        ValueSliderDescriptor Strength = {};
        Strength.Label = "Strength"; Strength.Value = &State.Strength;
        Strength.Minimum = 0.0f; Strength.Maximum = 1.0f; Strength.Format = "%.2f"; Strength.Unit = "-"; Strength.Enabled = true;
        ConstructValueSlider(Theme, Strength);

        RecordRule(Theme);

        // -- Source selection --------------------------------------------------------------------------------------------
        SelectionEntryDescriptor Source = {};
        Source.Label = "Source"; Source.SelectedIndex = &State.SourceOrdinal;
        Source.Options = SourceOptions; Source.OptionCount = 2; Source.Enabled = true;
        ConstructSelectionEntry(Theme, Source);

        if (State.SourceOrdinal == static_cast<int>(MaskSourceOrigin::Painted))
        {
            // -- Painted atlas -------------------------------------------------------------------------------------------
            const bool Painted = State.StrokeCount > 0;
            char       PaintMeta[96];
            if (Painted)
            {
                std::snprintf(PaintMeta, sizeof(PaintMeta), "%d strokes \xC2\xB7 2048 \xC3\x97 2048 atlas", State.StrokeCount);
            }
            else
            {
                std::snprintf(PaintMeta, sizeof(PaintMeta), "Atlas allocates on first stroke");
            }

            bool AddStroke  = false;
            bool ClearPaint = false;
            RecordSlotStrip(Theme, "##paintslot",
                            Painted, 0.91f,
                            Painted ? "Painted mask" : "No mask strokes yet", PaintMeta,
                            Painted ? nullptr : "+", "x", Painted,
                            AddStroke, ClearPaint);
            if (AddStroke)
            {
                State.StrokeCount = 1;
            }
            if (ClearPaint)
            {
                State.StrokeCount = 0;
            }

            // -- Imported base -------------------------------------------------------------------------------------------
            RecordCaption(Theme, State.TexturePresent ? "IMPORTED BASE" : "IMPORTED BASE \xE2\x80\x94 OPTIONAL");

            if (State.TexturePresent)
            {
                bool Reimport = false;
                bool Drop     = false;
                RecordSlotStrip(Theme, "##baseslot",
                                false, 0.0f,
                                State.TextureLabel, "2048 \xC3\x97 2048 \xC2\xB7 Linear 8",
                                "r", "x", true,
                                Reimport, Drop);
                if (Reimport)
                {
                    std::snprintf(State.TextureLabel, sizeof(State.TextureLabel), "mask_import_02_replaced.png");
                }
                if (Drop)
                {
                    State.TexturePresent  = false;
                    State.TextureLabel[0] = '\0';
                }
            }
            else if (RecordImportStrip(Theme, "Import base mask"))
            {
                State.TexturePresent = true;
                std::snprintf(State.TextureLabel, sizeof(State.TextureLabel), "mask_import_01.png");
            }
        }
        else
        {
            // -- Generator -----------------------------------------------------------------------------------------------
            RecordGeneratorChoice(Theme, State);

            const MaskGeneratorDefinition* Generator = ResolveMaskGenerator(State);
            if (Generator != nullptr)
            {
                RecordCaption(Theme, Generator->Summary);

                for (int Index = 0; Index < Generator->ParameterCount; ++Index)
                {
                    const MaskParameterDefinition& Parameter = Generator->Parameters[Index];
                    ValueSliderDescriptor Row = {};
                    Row.Label = Parameter.Label; Row.Value = &State.ParameterValues[Index];
                    Row.Minimum = Parameter.Minimum; Row.Maximum = Parameter.Maximum;
                    Row.Format = "%.2f"; Row.Unit = "-"; Row.Enabled = true;
                    ConstructValueSlider(Theme, Row);
                }
            }
        }

        // -- Preview ------------------------------------------------------------------------------------------------------
        RecordPreviewStrip(Theme, State);

        EndPropertyCard(Theme);
    }

    EndPropertyPanel(Theme);
}

}   // namespace Frontier
