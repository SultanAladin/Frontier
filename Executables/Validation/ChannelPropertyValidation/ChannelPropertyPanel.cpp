/*==============================================================================================================================================
                                                           CHANNELPROPERTYPANEL.CPP
==============================================================================================================================================*/
// 🧩 The channel slide, drawn with the REAL Interface controls. Each region maps 1:1 onto the prototype:
//      .chips-region  -> InscribeChipRegion    (swatch pills + "x" removal + the "+" popup, grouped four ways)
//      .chan-panel    -> BeginPropertyCard     (one collapsible card per live channel)
//      .segrow        -> ConstructSelectionEntry ("[ Value | Texture | Generator ]")
//      .slider / .colorbar -> ConstructValueSlider / ConstructColorEntry
//      paint + import slots -> InscribePaintSlot / InscribeImportSlot
//    🔴 Texture mode ALWAYS shows the paint slot; the import is a separate optional base beneath it. Erasing strokes and deleting the import are
//       independent actions and neither leaves Texture mode (LayerKinds.js:68 — the atlas IS the storage).
//    📝 No icons: every mark is drawn from primitives (chevron lines, "x" strokes, a filled swatch circle) per the port instruction.

#include "ChannelPropertyPanel.h"

#include "EngineContext/Interface/Components/PropertyPanelBase.h"

#include "EngineContext/Interface/Components/Controls/ValueSlider.h"

#include "EngineContext/Interface/Components/Controls/ColorEntry.h"

#include "EngineContext/Interface/Components/Controls/SelectionEntry.h"

#include "imgui.h"

#include <cfloat>
#include <cstdio>
#include <cstring>

using namespace Frontier;

namespace ChannelPropertyValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float ChipHeight       = 23.0f;   // [px] - .chan-chip
    constexpr float ChipGap          =  6.0f;   // [px] - .chips gap
    constexpr float ChipPaddingLeft  = 10.0f;   // [px] - .chan-chip padding-left
    constexpr float ChipPaddingRight =  5.0f;   // [px] - .chan-chip padding-right
    constexpr float ChipRemoveSize   = 17.0f;   // [px] - .chip .cx
    constexpr float SwatchDiameter   =  9.0f;   // [px] - .chip .cswatch
    constexpr float AddButtonSize    = 27.0f;   // [px] - .add-btn
    constexpr float SlotHeight       = 50.0f;   // [px] - .slot
    constexpr float SlotThumbSize    = 38.0f;   // [px] - .slot-thumb

    const char* const SourceOptions[3] = { "Value", "Texture", "Generator" };

    // 📝 Fade an ImU32 without unpacking it by hand — used for muted marks and disabled buttons.
    ImU32 ApplyOpacity(ImU32 Tone, float Opacity)
    {
        ImVec4 Unpacked = ImGui::ColorConvertU32ToFloat4(Tone);
        Unpacked.w *= Opacity;
        return ImGui::ColorConvertFloat4ToU32(Unpacked);
    }

    // Inscribe a small chevron. Downward when Open, rightward when folded — the .ch-tw mark.
    void InscribeChevron(ImDrawList* Draw, ImVec2 Centre, float Extent, ImU32 Tone, bool Open)
    {
        const float Half = Extent * 0.5f;
        if (Open)
        {
            Draw->AddLine(ImVec2(Centre.x - Half, Centre.y - Half * 0.5f), ImVec2(Centre.x, Centre.y + Half * 0.6f), Tone, 1.6f);
            Draw->AddLine(ImVec2(Centre.x + Half, Centre.y - Half * 0.5f), ImVec2(Centre.x, Centre.y + Half * 0.6f), Tone, 1.6f);
        }
        else
        {
            Draw->AddLine(ImVec2(Centre.x - Half * 0.5f, Centre.y - Half), ImVec2(Centre.x + Half * 0.6f, Centre.y), Tone, 1.6f);
            Draw->AddLine(ImVec2(Centre.x - Half * 0.5f, Centre.y + Half), ImVec2(Centre.x + Half * 0.6f, Centre.y), Tone, 1.6f);
        }
    }

    // Inscribe the two crossing strokes of an "x" — the chip's removal mark and the delete buttons.
    void InscribeCrossMark(ImDrawList* Draw, ImVec2 Centre, float Extent, ImU32 Tone)
    {
        const float Half = Extent * 0.5f;
        Draw->AddLine(ImVec2(Centre.x - Half, Centre.y - Half), ImVec2(Centre.x + Half, Centre.y + Half), Tone, 1.5f);
        Draw->AddLine(ImVec2(Centre.x + Half, Centre.y - Half), ImVec2(Centre.x - Half, Centre.y + Half), Tone, 1.5f);
    }

    // Inscribe a "+" — the add button's mark.
    void InscribePlusMark(ImDrawList* Draw, ImVec2 Centre, float Extent, ImU32 Tone)
    {
        const float Half = Extent * 0.5f;
        Draw->AddLine(ImVec2(Centre.x, Centre.y - Half), ImVec2(Centre.x, Centre.y + Half), Tone, 1.5f);
        Draw->AddLine(ImVec2(Centre.x - Half, Centre.y), ImVec2(Centre.x + Half, Centre.y), Tone, 1.5f);
    }

    // A compact bordered button carrying a text mark. Returns true when pressed. Enabled=false draws muted + swallows input.
    bool InscribeCompactButton(const ThemeConfiguration& Theme, const char* Identifier, const char* Caption,
                               float Width, bool Enabled, bool Dangerous)
    {
        const ColorPaletteDescriptor& Palette = Theme.Palette;
        ImDrawList* Draw = ImGui::GetWindowDrawList();

        const float  Height  = 24.0f;
        const ImVec2 TopLeft = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton(Identifier, ImVec2(Width, Height));
        const bool Hovered = Enabled && ImGui::IsItemHovered();
        const bool Pressed = Enabled && ImGui::IsItemClicked();

        ImU32 Fill   = Hovered ? Palette.ControlHovered : Palette.PanelHeader;
        ImU32 Border = Hovered ? Palette.ControlActive  : Palette.PanelBorder;
        ImU32 Text   = Hovered ? Palette.TextPrimary    : Palette.TextMuted;

        if (Dangerous && Hovered)
        {
            Fill   = IM_COL32(0x38, 0x18, 0x18, 0xff);
            Border = IM_COL32(0x8a, 0x39, 0x39, 0xff);
            Text   = IM_COL32(0xe0, 0x5a, 0x5a, 0xff);
        }
        if (!Enabled)
        {
            Fill   = Palette.PanelHeader;
            Border = Palette.PanelBorder;
            Text   = ApplyOpacity(Palette.TextMuted, 0.35f);
        }

        const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + Height);
        Draw->AddRectFilled(TopLeft, BottomRight, Fill, 6.0f);
        Draw->AddRect(TopLeft, BottomRight, Border, 6.0f, 0, Theme.Metrics.BorderThickness);

        const ImVec2 Extent = ImGui::CalcTextSize(Caption);
        Draw->AddText(ImVec2(TopLeft.x + (Width - Extent.x) * 0.5f, TopLeft.y + (Height - Extent.y) * 0.5f), Text, Caption);

        return Pressed;
    }

    // A dimmed uppercase caption — the .slot-label / .afm-head strip.
    void InscribeSectionCaption(const ThemeConfiguration& Theme, const char* Caption)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ApplyOpacity(Theme.Palette.TextMuted, 0.75f));
        ImGui::TextUnformatted(Caption);
        ImGui::PopStyleColor();
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // ── the "+" popup: fourteen channels under four group headings ────────────────────────────────────────────────────
    void InscribeAddMenu(const ThemeConfiguration& Theme, ChannelPropertyState& State)
    {
        const ChannelSlot* Slots = ResolveChannelSlotTable();

        if (!ImGui::BeginPopup("##channel-add-menu"))
        {
            State.AddMenuOpen = false;
            return;
        }

        State.AddMenuOpen = true;

        for (int Group = 0; Group < ChannelGroupCount; ++Group)
        {
            const ChannelGroup Section = (ChannelGroup)Group;
            if (Group > 0) ImGui::Separator();
            InscribeSectionCaption(Theme, DescribeChannelGroup(Section));

            for (int Ordinal = 0; Ordinal < ChannelSlotCount; ++Ordinal)
            {
                const ChannelSlot& Slot = Slots[Ordinal];
                if (Slot.Group != Section) continue;

                ChannelAuthoring& Authoring = State.Channels[Ordinal];

                // the swatch sits inline ahead of the label, matching .afm-item .swatch
                const ImVec2 SwatchOrigin = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(14.0f, ImGui::GetTextLineHeight()));
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(SwatchOrigin.x + 5.5f, SwatchOrigin.y + ImGui::GetTextLineHeight() * 0.5f),
                    5.5f, Slot.Hue, 12);
                ImGui::SameLine(0.0f, 4.0f);

                char Row[96];
                snprintf(Row, sizeof(Row), "%s##add-%d", Slot.Label, Ordinal);

                // 🔴 Base Colour is pinned on — it is listed with a tick but cannot be toggled off.
                const bool Selectable = Slot.Removable;
                if (ImGui::MenuItem(Row, nullptr, Authoring.Enabled, Selectable))
                {
                    Authoring.Enabled = !Authoring.Enabled;
                    if (Authoring.Enabled) Authoring.Expanded = true;
                }
            }
        }

        ImGui::EndPopup();
    }

    // ── the chip region: one pill per live channel, then the "+" button ───────────────────────────────────────────────
    void InscribeChipRegion(const ThemeConfiguration& Theme, ChannelPropertyState& State)
    {
        const ColorPaletteDescriptor& Palette = Theme.Palette;
        const ChannelSlot*            Slots   = ResolveChannelSlotTable();
        ImDrawList*                   Draw    = ImGui::GetWindowDrawList();

        const int   Live       = CountEnabledChannels(State);
        const float RegionWide = ImGui::GetContentRegionAvail().x;

        char Heading[64];
        snprintf(Heading, sizeof(Heading), "CHANNELS   %d / %d", Live, ChannelSlotCount);
        InscribeSectionCaption(Theme, Heading);
        ImGui::Spacing();

        // Wrap the pills by hand: ImGui has no flex-wrap, so track the pen and break on overflow.
        float PenX      = ImGui::GetCursorPosX();
        float RowStartX = PenX;
        int   Removal   = -1;

        for (int Ordinal = 0; Ordinal < ChannelSlotCount; ++Ordinal)
        {
            if (!State.Channels[Ordinal].Enabled) continue;

            const ChannelSlot& Slot      = Slots[Ordinal];
            const ImVec2       TextSize  = ImGui::CalcTextSize(Slot.Label);
            const float        ChipWidth = ChipPaddingLeft + SwatchDiameter + 5.0f + TextSize.x + 5.0f
                                         + ChipRemoveSize + ChipPaddingRight;

            if (PenX > RowStartX && (PenX - RowStartX) + ChipWidth > RegionWide)
            {
                ImGui::NewLine();
                PenX = RowStartX;
            }

            ImGui::SetCursorPosX(PenX);
            const ImVec2 TopLeft = ImGui::GetCursorScreenPos();

            char ChipIdentifier[32];
            snprintf(ChipIdentifier, sizeof(ChipIdentifier), "##chip-%d", Ordinal);
            ImGui::InvisibleButton(ChipIdentifier, ImVec2(ChipWidth, ChipHeight));
            const bool ChipHovered = ImGui::IsItemHovered();

            const ImVec2 BottomRight(TopLeft.x + ChipWidth, TopLeft.y + ChipHeight);
            const float  Rounding = ChipHeight * 0.5f;
            Draw->AddRectFilled(TopLeft, BottomRight, Palette.ControlBackground, Rounding);
            Draw->AddRect(TopLeft, BottomRight,
                          ChipHovered ? Palette.ControlActive : Palette.PanelBorder,
                          Rounding, 0, Theme.Metrics.BorderThickness);

            // swatch · label · removal mark
            Draw->AddCircleFilled(ImVec2(TopLeft.x + ChipPaddingLeft + SwatchDiameter * 0.5f, TopLeft.y + ChipHeight * 0.5f),
                                  SwatchDiameter * 0.5f, Slot.Hue, 12);
            Draw->AddText(ImVec2(TopLeft.x + ChipPaddingLeft + SwatchDiameter + 5.0f, TopLeft.y + (ChipHeight - TextSize.y) * 0.5f),
                          Palette.TextPrimary, Slot.Label);

            const ImVec2 RemoveCentre(BottomRight.x - ChipPaddingRight - ChipRemoveSize * 0.5f, TopLeft.y + ChipHeight * 0.5f);

            if (Slot.Removable)
            {
                const ImVec2 Cursor       = ImGui::GetMousePos();
                const float  Reach        = ChipRemoveSize * 0.5f;
                const bool   OverRemove   = ChipHovered
                                          && Cursor.x >= RemoveCentre.x - Reach && Cursor.x <= RemoveCentre.x + Reach
                                          && Cursor.y >= RemoveCentre.y - Reach && Cursor.y <= RemoveCentre.y + Reach;

                Draw->AddCircleFilled(RemoveCentre, Reach,
                                      OverRemove ? IM_COL32(0xe0, 0x5a, 0x5a, 0xff) : Palette.ControlActive, 14);
                InscribeCrossMark(Draw, RemoveCentre, 7.0f,
                                  OverRemove ? IM_COL32(0xff, 0xff, 0xff, 0xff) : Palette.TextMuted);

                if (OverRemove && ImGui::IsItemClicked()) Removal = Ordinal;
            }
            else
            {
                // pinned: the mark is drawn faded and inert
                Draw->AddCircleFilled(RemoveCentre, ChipRemoveSize * 0.5f, ApplyOpacity(Palette.ControlActive, 0.4f), 14);
                InscribeCrossMark(Draw, RemoveCentre, 7.0f, ApplyOpacity(Palette.TextMuted, 0.3f));
            }

            PenX += ChipWidth + ChipGap;
            ImGui::SameLine(0.0f, 0.0f);
        }

        // ── the "+" button, wrapping with the pills ──
        if (PenX > RowStartX && (PenX - RowStartX) + AddButtonSize > RegionWide)
        {
            ImGui::NewLine();
            PenX = RowStartX;
        }
        ImGui::SetCursorPosX(PenX);

        const ImVec2 AddTopLeft = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##channel-add", ImVec2(AddButtonSize, AddButtonSize));
        const bool AddHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) ImGui::OpenPopup("##channel-add-menu");

        const ImVec2 AddCentre(AddTopLeft.x + AddButtonSize * 0.5f, AddTopLeft.y + AddButtonSize * 0.5f);
        Draw->AddCircle(AddCentre, AddButtonSize * 0.5f,
                        AddHovered ? Palette.AccentPrimary : Palette.PanelBorder, 20, Theme.Metrics.BorderThickness);
        if (AddHovered) Draw->AddCircleFilled(AddCentre, AddButtonSize * 0.5f - 1.0f, Palette.AccentSubtle, 20);
        InscribePlusMark(Draw, AddCentre, 11.0f, AddHovered ? Palette.TextPrimary : Palette.TextMuted);

        InscribeAddMenu(Theme, State);

        // 🔴 Deferred so the chip loop is never mutated mid-iteration.
        if (Removal >= 0) State.Channels[Removal].Enabled = false;

        ImGui::Spacing();
    }

    // ── the always-present paint slot ─────────────────────────────────────────────────────────────────────────────────
    void InscribePaintSlot(const ThemeConfiguration& Theme, int Ordinal, ChannelAuthoring& Authoring, ImU32 Hue)
    {
        const ColorPaletteDescriptor& Palette = Theme.Palette;
        ImDrawList*                   Draw    = ImGui::GetWindowDrawList();

        const bool   Painted = Authoring.StrokeCount > 0;
        const float  Wide    = ImGui::GetContentRegionAvail().x;
        const ImVec2 TopLeft = ImGui::GetCursorScreenPos();

        Draw->AddRectFilled(TopLeft, ImVec2(TopLeft.x + Wide, TopLeft.y + SlotHeight), Palette.ControlBackground, 9.0f);
        Draw->AddRect(TopLeft, ImVec2(TopLeft.x + Wide, TopLeft.y + SlotHeight), Palette.PanelBorder, 9.0f, 0,
                      Theme.Metrics.BorderThickness);

        // the thumb carries the channel hue once strokes exist, so paint reads as present at a glance
        const ImVec2 ThumbTopLeft(TopLeft.x + 6.0f, TopLeft.y + (SlotHeight - SlotThumbSize) * 0.5f);
        Draw->AddRectFilled(ThumbTopLeft, ImVec2(ThumbTopLeft.x + SlotThumbSize, ThumbTopLeft.y + SlotThumbSize),
                            Painted ? Hue : Palette.DeskBackground, 6.0f);
        Draw->AddRect(ThumbTopLeft, ImVec2(ThumbTopLeft.x + SlotThumbSize, ThumbTopLeft.y + SlotThumbSize),
                      Palette.PanelBorder, 6.0f, 0, Theme.Metrics.BorderThickness);

        char Headline[64];
        char Detail[96];
        if (Painted)
        {
            snprintf(Headline, sizeof(Headline), "%s", "Painted strokes");
            snprintf(Detail, sizeof(Detail), "%d strokes \xC2\xB7 2048 \xC3\x97 2048 atlas", Authoring.StrokeCount);
        }
        else
        {
            snprintf(Headline, sizeof(Headline), "%s", "No strokes yet");
            snprintf(Detail, sizeof(Detail), "%s", "Atlas allocates on the first stroke");
        }

        const float TextX = ThumbTopLeft.x + SlotThumbSize + 9.0f;
        Draw->AddText(ImVec2(TextX, TopLeft.y + 11.0f), Palette.TextPrimary, Headline);
        Draw->AddText(ImVec2(TextX, TopLeft.y + 28.0f), ApplyOpacity(Palette.TextMuted, 0.85f), Detail);

        // the erase button rides the slot's right edge
        ImGui::SetCursorScreenPos(ImVec2(TopLeft.x + Wide - 66.0f, TopLeft.y + (SlotHeight - 24.0f) * 0.5f));
        char EraseIdentifier[48];
        snprintf(EraseIdentifier, sizeof(EraseIdentifier), "##erase-paint-%d", Ordinal);
        if (InscribeCompactButton(Theme, EraseIdentifier, "Erase", 60.0f, Painted, true))
        {
            Authoring.StrokeCount = 0;
        }

        ImGui::SetCursorScreenPos(ImVec2(TopLeft.x, TopLeft.y + SlotHeight + 6.0f));
    }

    // ── the optional imported base beneath the paint ──────────────────────────────────────────────────────────────────
    void InscribeImportSlot(const ThemeConfiguration& Theme, int Ordinal, ChannelAuthoring& Authoring)
    {
        const ColorPaletteDescriptor& Palette = Theme.Palette;
        ImDrawList*                   Draw    = ImGui::GetWindowDrawList();

        const float  Wide    = ImGui::GetContentRegionAvail().x;
        const ImVec2 TopLeft = ImGui::GetCursorScreenPos();

        if (!Authoring.ImportedBase)
        {
            // the dashed "Import base texture" strip — ImGui has no dashed stroke, so a hairline rect stands in
            const float Height = 44.0f;
            ImGui::InvisibleButton("##import-strip", ImVec2(Wide, Height));
            const bool Hovered = ImGui::IsItemHovered();

            Draw->AddRect(TopLeft, ImVec2(TopLeft.x + Wide, TopLeft.y + Height),
                          Hovered ? Palette.AccentPrimary : Palette.PanelBorder, 9.0f, 0, Theme.Metrics.BorderThickness);

            const char*  Caption = "Import base texture";
            const ImVec2 Extent  = ImGui::CalcTextSize(Caption);
            Draw->AddText(ImVec2(TopLeft.x + (Wide - Extent.x) * 0.5f, TopLeft.y + (Height - Extent.y) * 0.5f),
                          Hovered ? Palette.TextPrimary : Palette.TextMuted, Caption);

            if (ImGui::IsItemClicked())
            {
                Authoring.ImportedBase = true;
                snprintf(Authoring.ImportName, sizeof(Authoring.ImportName), "channel_%d_import.png", Ordinal);
                Authoring.ImportWidth  = 2048;
                Authoring.ImportHeight = 2048;
                Authoring.ImportFormat = "Linear 8";
            }
            return;
        }

        Draw->AddRectFilled(TopLeft, ImVec2(TopLeft.x + Wide, TopLeft.y + SlotHeight), Palette.ControlBackground, 9.0f);
        Draw->AddRect(TopLeft, ImVec2(TopLeft.x + Wide, TopLeft.y + SlotHeight), Palette.PanelBorder, 9.0f, 0,
                      Theme.Metrics.BorderThickness);

        const ImVec2 ThumbTopLeft(TopLeft.x + 6.0f, TopLeft.y + (SlotHeight - SlotThumbSize) * 0.5f);
        Draw->AddRectFilled(ThumbTopLeft, ImVec2(ThumbTopLeft.x + SlotThumbSize, ThumbTopLeft.y + SlotThumbSize),
                            Palette.DeskBackground, 6.0f);
        Draw->AddRect(ThumbTopLeft, ImVec2(ThumbTopLeft.x + SlotThumbSize, ThumbTopLeft.y + SlotThumbSize),
                      Palette.PanelBorder, 6.0f, 0, Theme.Metrics.BorderThickness);

        char Detail[96];
        snprintf(Detail, sizeof(Detail), "%d \xC3\x97 %d \xC2\xB7 %s",
                 Authoring.ImportWidth, Authoring.ImportHeight, Authoring.ImportFormat);

        const float TextX = ThumbTopLeft.x + SlotThumbSize + 9.0f;
        Draw->AddText(ImVec2(TextX, TopLeft.y + 11.0f), Palette.TextPrimary, Authoring.ImportName);
        Draw->AddText(ImVec2(TextX, TopLeft.y + 28.0f), ApplyOpacity(Palette.TextMuted, 0.85f), Detail);

        // replace + delete, both riding the right edge
        ImGui::SetCursorScreenPos(ImVec2(TopLeft.x + Wide - 140.0f, TopLeft.y + (SlotHeight - 24.0f) * 0.5f));
        char ReplaceIdentifier[48];
        snprintf(ReplaceIdentifier, sizeof(ReplaceIdentifier), "##replace-import-%d", Ordinal);
        if (InscribeCompactButton(Theme, ReplaceIdentifier, "Replace", 68.0f, true, false))
        {
            snprintf(Authoring.ImportName, sizeof(Authoring.ImportName), "channel_%d_replaced.png", Ordinal);
            Authoring.ImportFormat = "Linear 16";
        }

        ImGui::SameLine(0.0f, 4.0f);
        char DeleteIdentifier[48];
        snprintf(DeleteIdentifier, sizeof(DeleteIdentifier), "##delete-import-%d", Ordinal);
        if (InscribeCompactButton(Theme, DeleteIdentifier, "Delete", 60.0f, true, true))
        {
            // 🔴 Only the underlay goes. The strokes and the mode both survive.
            Authoring.ImportedBase = false;
            Authoring.ImportName[0] = '\0';
        }

        ImGui::SetCursorScreenPos(ImVec2(TopLeft.x, TopLeft.y + SlotHeight + 6.0f));
    }

    // ── the generator picker + its live parameter rows ────────────────────────────────────────────────────────────────
    void InscribeGeneratorRegion(const ThemeConfiguration& Theme, int Ordinal, ChannelAuthoring& Authoring)
    {
        const GeneratorEntry* Catalogue = ResolveGeneratorCatalogue();

        // the picker: a dropdown carrying every catalogue entry, grouped by section
        char PickerIdentifier[48];
        snprintf(PickerIdentifier, sizeof(PickerIdentifier), "##generator-pick-%d", Ordinal);

        const char* Current = (Authoring.GeneratorIndex >= 0)
                            ? Catalogue[Authoring.GeneratorIndex].Label
                            : "Choose generator";

        ImGui::TextUnformatted("Generator");
        ImGui::SameLine(88.0f * Theme.Metrics.UiScale);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo(PickerIdentifier, Current))
        {
            for (int Entry = 0; Entry < GeneratorCount; ++Entry)
            {
                if (Catalogue[Entry].Section)
                {
                    if (Entry > 0) ImGui::Separator();
                    InscribeSectionCaption(Theme, Catalogue[Entry].Section);
                }

                const bool Chosen = (Authoring.GeneratorIndex == Entry);
                if (ImGui::Selectable(Catalogue[Entry].Label, Chosen)) AssignGenerator(Authoring, Entry);

                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Catalogue[Entry].Note);
            }
            ImGui::EndCombo();
        }

        if (Authoring.GeneratorIndex < 0) return;

        const GeneratorEntry& Definition = Catalogue[Authoring.GeneratorIndex];

        ImGui::Separator();
        InscribeSectionCaption(Theme, Definition.Note);

        // reset + remove
        char ResetIdentifier[48];
        char DropIdentifier[48];
        snprintf(ResetIdentifier, sizeof(ResetIdentifier), "##generator-reset-%d", Ordinal);
        snprintf(DropIdentifier, sizeof(DropIdentifier), "##generator-drop-%d", Ordinal);

        if (InscribeCompactButton(Theme, ResetIdentifier, "Reset", 60.0f, true, false)) ResetGeneratorParameters(Authoring);
        ImGui::SameLine(0.0f, 4.0f);
        if (InscribeCompactButton(Theme, DropIdentifier, "Remove", 66.0f, true, true))
        {
            Authoring.GeneratorIndex = -1;
            return;
        }

        ImGui::Spacing();

        for (int Parameter = 0; Parameter < Definition.ParameterCount; ++Parameter)
        {
            const GeneratorParameter& Spec = Definition.Parameters[Parameter];

            ValueSliderDescriptor Row = {};
            Row.Label   = Spec.Label;
            Row.Value   = &Authoring.GeneratorParameters[Parameter];
            Row.Minimum = Spec.Minimum;
            Row.Maximum = Spec.Maximum;
            Row.Format  = "%.2f";
            Row.Unit    = "-";
            Row.Enabled = true;
            ConstructValueSlider(Theme, Row);
        }
    }

    // ── one channel card's body ───────────────────────────────────────────────────────────────────────────────────────
    void InscribeChannelBody(const ThemeConfiguration& Theme, int Ordinal, const ChannelSlot& Slot,
                             ChannelAuthoring& Authoring)
    {
        // 🔴 Normal has no storage and nothing to author — the note IS the whole body.
        if (Slot.Editor == ChannelEditor::Derived)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ApplyOpacity(Theme.Palette.TextMuted, 0.85f));
            ImGui::TextWrapped("Derived from the painted height. No value to author.");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            InscribeSectionCaption(Theme, Slot.Placement);
            return;
        }

        // the source segment always leads the body
        int SourceIndex = (int)Authoring.Source;

        SelectionEntryDescriptor Segment = {};
        Segment.Label         = "Source";
        Segment.SelectedIndex = &SourceIndex;
        Segment.Options       = SourceOptions;
        Segment.OptionCount   = 3;
        Segment.Enabled       = true;
        if (ConstructSelectionEntry(Theme, Segment)) Authoring.Source = (SourceMode)SourceIndex;

        switch (Authoring.Source)
        {
            case SourceMode::Texture:
            {
                // paint is ALWAYS present; the import is a separate optional base under it
                InscribePaintSlot(Theme, Ordinal, Authoring, Slot.Hue);
                InscribeSectionCaption(Theme, Authoring.ImportedBase ? "IMPORTED BASE" : "IMPORTED BASE - OPTIONAL");
                InscribeImportSlot(Theme, Ordinal, Authoring);
                break;
            }

            case SourceMode::Generator:
            {
                InscribeGeneratorRegion(Theme, Ordinal, Authoring);
                break;
            }

            case SourceMode::Value:
            {
                if (Slot.Editor == ChannelEditor::Colour)
                {
                    ColorEntryDescriptor Swatch = {};
                    Swatch.Label        = "Colour";
                    Swatch.Channels     = Authoring.Tint;
                    Swatch.IncludeAlpha = false;
                    Swatch.Enabled      = true;
                    ConstructColorEntry(Theme, Swatch);
                }
                else
                {
                    ValueSliderDescriptor Amount = {};
                    Amount.Label   = "Amount";
                    Amount.Value   = &Authoring.Amount;
                    Amount.Minimum = Slot.Minimum;
                    Amount.Maximum = Slot.Maximum;
                    Amount.Format  = Slot.Format;
                    Amount.Unit    = Slot.Unit;
                    Amount.Enabled = true;
                    ConstructValueSlider(Theme, Amount);
                }
                break;
            }
        }

        ImGui::Spacing();
        InscribeSectionCaption(Theme, Slot.Placement);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructChannelPropertyPanel(const ThemeConfiguration& Theme, ChannelPropertyState& State)
{
    const ColorPaletteDescriptor& Palette = Theme.Palette;
    const ChannelSlot*            Slots   = ResolveChannelSlotTable();

    // -- Slide head: layer name over "Classification · Blend" ----------------------------------------------------------
    ImGui::Dummy(ImVec2(0.0f, Theme.Metrics.PanelPadding));
    ImGui::Indent(Theme.Metrics.PanelPadding);
    ImGui::TextUnformatted(State.LayerName);

    char SubLine[96];
    snprintf(SubLine, sizeof(SubLine), "%s \xC2\xB7 %s", State.Classification, State.Blend);
    ImGui::PushStyleColor(ImGuiCol_Text, ApplyOpacity(Palette.TextMuted, 0.9f));
    ImGui::TextUnformatted(SubLine);
    ImGui::PopStyleColor();
    ImGui::Unindent(Theme.Metrics.PanelPadding);
    ImGui::Spacing();
    ImGui::Separator();

    // -- Body: the chip region, then one card per live channel ---------------------------------------------------------
    BeginPropertyPanel(Theme, "##channel-property-panel");

    if (BeginPropertyCard(Theme, "Channels", &State.ChipsExpanded))
    {
        InscribeChipRegion(Theme, State);
        EndPropertyCard(Theme);
    }

    for (int Ordinal = 0; Ordinal < ChannelSlotCount; ++Ordinal)
    {
        ChannelAuthoring& Authoring = State.Channels[Ordinal];
        if (!Authoring.Enabled) continue;

        const ChannelSlot& Slot = Slots[Ordinal];

        // 📝 The card title carries the folded summary the prototype puts in .ch-src.
        char Title[96];
        snprintf(Title, sizeof(Title), "%s   \xC2\xB7   %s", Slot.Label,
                 (Slot.Editor == ChannelEditor::Derived) ? "Derived" : DescribeSourceMode(Authoring.Source));

        ImGui::PushID(Ordinal);
        if (BeginPropertyCard(Theme, Title, &Authoring.Expanded))
        {
            InscribeChannelBody(Theme, Ordinal, Slot, Authoring);
            EndPropertyCard(Theme);
        }
        ImGui::PopID();
    }

    EndPropertyPanel(Theme);

    // -- Slide foot: the two counts ------------------------------------------------------------------------------------
    ImGui::Separator();
    ImGui::Indent(Theme.Metrics.PanelPadding);
    char Foot[96];
    snprintf(Foot, sizeof(Foot), "%d channels          %d atlases", CountEnabledChannels(State), AtlasTotal);
    ImGui::PushStyleColor(ImGuiCol_Text, ApplyOpacity(Palette.TextMuted, 0.8f));
    ImGui::TextUnformatted(Foot);
    ImGui::PopStyleColor();
    ImGui::Unindent(Theme.Metrics.PanelPadding);
}

}   // namespace ChannelPropertyValidation
