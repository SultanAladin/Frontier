/*==============================================================================================================================================
                                                            LAYERSTACKPANEL.CPP
==============================================================================================================================================*/
// 🧩 The Layers slide, drawn with the REAL Interface controls. Each region maps 1:1 onto the prototype:
//      .stack-add-wrap .sf-add -> InscribeAddAffordance   (full-width dashed button; joins its kind list into one card)
//      .search                 -> InscribeFilterField     (bordered box + magnifier, focus-lit)
//      .stack-row              -> InscribeStackRow        (hue tag + thumb + two-line text + opacity pill + eye + caret)
//      .stack-expand / .se-body-> InscribeInlineExpand     (Visible / Blend / Opacity / Paint / Tag + the mask editor)
//      .msk-toolbar            -> the White | Black | Invert bar
//      .pane-foot .sf-tally    -> the "6 / 12 · 1 hidden" strip
//    🔴 AN OPEN ROW AND ITS EXPAND ARE ONE CARD (LayerInspector.css:186). The row is the card's HEAD: it owns the top/left/right border and drops
//       its bottom edge, and the expand continues the same left/right and closes the bottom. Drawing each with a full box reads as two stacked
//       panels — the two-boxes fault the CSS calls out by name.
//    🔴 The opacity pill is a DRAG, not a click (cursor:ew-resize). It scrubs horizontally without opening anything.
//    📝 No icons: every mark is drawn from primitives (chevron, eye outline + pupil, magnifier, "+", "x") per the port instruction.

#include "LayerStackPanel.h"

#include "EngineContext/Interface/Components/PropertyPanelBase.h"

#include "EngineContext/Interface/Components/Controls/ValueSlider.h"

#include "EngineContext/Interface/Components/Controls/ColorEntry.h"

#include "EngineContext/Interface/Components/Controls/SelectionEntry.h"

#include "imgui.h"

#include <cfloat>
#include <cstdio>
#include <cstring>

using namespace Frontier;

namespace LayerStackValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float RowHeight       = 44.0f;   // [px] - .stack-row height
    constexpr float RowGap          =  2.0f;   // [px] - .stack-body gap
    constexpr float TagWidth        =  4.0f;   // [px] - .sr-tag
    constexpr float TagHeight       = 28.0f;   // [px] - .sr-tag
    constexpr float ThumbSize       = 30.0f;   // [px] - .sr-thumb
    constexpr float OpacityPillWide = 41.0f;   // [px] - .sr-opacity min-width + padding
    constexpr float EyeSize         = 20.0f;   // [px] - .sr-visibility
    constexpr float CaretSize       = 18.0f;   // [px] - .sr-caret
    constexpr float SpineWidth      =  3.0f;   // [px] - .stack-row.active::before
    constexpr float AddHeight       = 30.0f;   // [px] - .sf-add
    constexpr float FilterHeight    = 30.0f;   // [px] - .search
    constexpr float MaskToolHeight  = 24.0f;   // [px] - .msk-tool

    // 📝 Fade an ImU32 without unpacking it by hand — used for muted rows and disabled marks.
    ImU32 ApplyOpacity(ImU32 Tone, float Opacity)
    {
        ImVec4 Unpacked = ImGui::ColorConvertU32ToFloat4(Tone);
        Unpacked.w *= Opacity;
        return ImGui::ColorConvertFloat4ToU32(Unpacked);
    }

    ImU32 PackTint(const float Channels[3], float Opacity = 1.0f)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(Channels[0], Channels[1], Channels[2], Opacity));
    }

    // Inscribe a small chevron. Points UP when the expand is open (.stack-row.expanded rotates 180°), DOWN when folded.
    void InscribeChevron(ImDrawList* Draw, ImVec2 Centre, float Extent, ImU32 Tone, bool Open)
    {
        const float Half = Extent * 0.5f;
        const float Rise = Open ? -Half * 0.6f : Half * 0.6f;
        Draw->AddLine(ImVec2(Centre.x - Half, Centre.y - Rise * 0.85f), ImVec2(Centre.x, Centre.y + Rise), Tone, 1.6f);
        Draw->AddLine(ImVec2(Centre.x + Half, Centre.y - Rise * 0.85f), ImVec2(Centre.x, Centre.y + Rise), Tone, 1.6f);
    }

    void InscribeCrossMark(ImDrawList* Draw, ImVec2 Centre, float Extent, ImU32 Tone)
    {
        const float Half = Extent * 0.5f;
        Draw->AddLine(ImVec2(Centre.x - Half, Centre.y - Half), ImVec2(Centre.x + Half, Centre.y + Half), Tone, 1.5f);
        Draw->AddLine(ImVec2(Centre.x + Half, Centre.y - Half), ImVec2(Centre.x - Half, Centre.y + Half), Tone, 1.5f);
    }

    void InscribePlusMark(ImDrawList* Draw, ImVec2 Centre, float Extent, ImU32 Tone)
    {
        const float Half = Extent * 0.5f;
        Draw->AddLine(ImVec2(Centre.x, Centre.y - Half), ImVec2(Centre.x, Centre.y + Half), Tone, 1.5f);
        Draw->AddLine(ImVec2(Centre.x - Half, Centre.y), ImVec2(Centre.x + Half, Centre.y), Tone, 1.5f);
    }

    // The eye: an open almond with a pupil, or the same outline with a slash through it when hidden.
    void InscribeEyeMark(ImDrawList* Draw, ImVec2 Centre, float Extent, ImU32 Tone, bool Open)
    {
        const float Wide = Extent * 0.5f;
        const float Tall = Extent * 0.30f;

        // Two arcs meeting at the corners — drawn as quadratic beziers so the almond reads cleanly at 14px.
        Draw->PathClear();
        Draw->PathLineTo(ImVec2(Centre.x - Wide, Centre.y));
        Draw->PathBezierQuadraticCurveTo(ImVec2(Centre.x, Centre.y - Tall * 2.0f), ImVec2(Centre.x + Wide, Centre.y));
        Draw->PathStroke(Tone, 0, 1.4f);

        Draw->PathClear();
        Draw->PathLineTo(ImVec2(Centre.x - Wide, Centre.y));
        Draw->PathBezierQuadraticCurveTo(ImVec2(Centre.x, Centre.y + Tall * 2.0f), ImVec2(Centre.x + Wide, Centre.y));
        Draw->PathStroke(Tone, 0, 1.4f);

        if (Open) Draw->AddCircleFilled(Centre, Extent * 0.17f, Tone, 10);
        else      Draw->AddLine(ImVec2(Centre.x - Wide, Centre.y + Tall * 1.4f),
                                ImVec2(Centre.x + Wide, Centre.y - Tall * 1.4f), Tone, 1.5f);
    }

    // The filter box's magnifier: a ring plus a handle.
    void InscribeMagnifierMark(ImDrawList* Draw, ImVec2 Centre, float Extent, ImU32 Tone)
    {
        const float Radius = Extent * 0.32f;
        const ImVec2 Ring(Centre.x - Extent * 0.06f, Centre.y - Extent * 0.06f);
        Draw->AddCircle(Ring, Radius, Tone, 12, 1.4f);
        Draw->AddLine(ImVec2(Ring.x + Radius * 0.72f, Ring.y + Radius * 0.72f),
                      ImVec2(Centre.x + Extent * 0.42f, Centre.y + Extent * 0.42f), Tone, 1.5f);
    }

    // A dimmed uppercase caption — the .se-k key column and the mask section label.
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
    // ── "+ Add Layer", above the filter. While its list is up the button becomes the TOP HALF OF ONE CARD ─────────────
    void InscribeAddAffordance(const ThemeConfiguration& Theme, LayerStackState& State)
    {
        const ColorPaletteDescriptor& Palette = Theme.Palette;
        ImDrawList* Draw = ImGui::GetWindowDrawList();

        const bool   AtCapacity = State.LayerTotal >= LayerCapacity;
        const float  Width      = ImGui::GetContentRegionAvail().x;
        const ImVec2 TopLeft    = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton("##add-layer", ImVec2(Width, AddHeight));
        const bool Hovered = !AtCapacity && ImGui::IsItemHovered();
        const bool Pressed = !AtCapacity && ImGui::IsItemClicked();

        const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + AddHeight);

        // 🔴 While the list is open the button squares off its BOTTOM corners and drops its bottom border, so the
        //    seam with the list below vanishes and the pair reads as one card rather than two floating panels.
        const float    Rounding = 9.0f;
        ImDrawFlags    Corners  = ImDrawFlags_None;
        if (State.AddListOpen) Corners = ImDrawFlags_RoundCornersTop;

        ImU32 Fill   = Hovered ? Palette.ControlHovered : Palette.PanelHeader;
        ImU32 Border = Hovered ? Palette.ControlActive  : Palette.PanelBorder;
        ImU32 Text   = Hovered ? Palette.TextPrimary    : Palette.TextMuted;
        if (AtCapacity)
        {
            Fill   = Palette.PanelHeader;
            Border = Palette.PanelBorder;
            Text   = ApplyOpacity(Palette.TextMuted, 0.35f);
        }

        Draw->AddRectFilled(TopLeft, BottomRight, Fill, Rounding, Corners);
        Draw->AddRect(TopLeft, BottomRight, Border, Rounding, Corners, Theme.Metrics.BorderThickness);

        char Caption[64];
        if (AtCapacity) snprintf(Caption, sizeof(Caption), "Layer cap of %d reached", LayerCapacity);
        else            snprintf(Caption, sizeof(Caption), "Add Layer");

        const ImVec2 Extent = ImGui::CalcTextSize(Caption);
        const float  MarkRoom = AtCapacity ? 0.0f : 18.0f;
        const float  TextX  = TopLeft.x + (Width - Extent.x + MarkRoom) * 0.5f;
        const float  MidY   = TopLeft.y + AddHeight * 0.5f;

        if (!AtCapacity) InscribePlusMark(Draw, ImVec2(TextX - 11.0f, MidY), 9.0f, Text);
        Draw->AddText(ImVec2(TextX, MidY - Extent.y * 0.5f), Text, Caption);

        if (Pressed) ImGui::OpenPopup("##add-layer-list");

        // ---- the kind list, flush beneath the button ---------------------------------------------------------------
        ImGui::SetNextWindowPos(ImVec2(TopLeft.x, BottomRight.y));
        ImGui::SetNextWindowSize(ImVec2(Width, 0.0f));
        if (ImGui::BeginPopup("##add-layer-list"))
        {
            State.AddListOpen = true;
            const LayerKindEntry* Kinds = ResolveLayerKindTable();

            for (int Ordinal = 0; Ordinal < LayerKindCount; ++Ordinal)
            {
                const LayerKindEntry& Kind = Kinds[Ordinal];

                // The kind swatch, then the label, then the summary in a dimmer tone.
                const ImVec2 RowStart = ImGui::GetCursorScreenPos();
                // 📝 An empty-labelled Selectable supplies the hit region and hover fill; the swatch and the label
                //    are inscribed over it below, so the row can carry a tinted mark the widget cannot draw itself.
                ImGui::PushID(Ordinal);
                if (ImGui::Selectable("##kind", false, 0, ImVec2(0.0f, 20.0f)))
                {
                    InsertLayerOfKind(State, (LayerKind)Ordinal);
                    ImGui::CloseCurrentPopup();
                }
                ImDrawList* ListDraw = ImGui::GetWindowDrawList();
                const ImVec2 SwatchCentre(RowStart.x + 7.0f, RowStart.y + 10.0f);
                ListDraw->AddRectFilled(ImVec2(SwatchCentre.x - 4.5f, SwatchCentre.y - 4.5f),
                                        ImVec2(SwatchCentre.x + 4.5f, SwatchCentre.y + 4.5f), Kind.Tint, 2.0f);
                ListDraw->AddText(ImVec2(RowStart.x + 19.0f, RowStart.y + 3.0f), Palette.TextPrimary, Kind.Label);

                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Kind.Summary);
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        else
        {
            State.AddListOpen = false;
        }
    }

    // ── the filter box ────────────────────────────────────────────────────────────────────────────────────────────────
    void InscribeFilterField(const ThemeConfiguration& Theme, LayerStackState& State)
    {
        const ColorPaletteDescriptor& Palette = Theme.Palette;
        ImDrawList* Draw = ImGui::GetWindowDrawList();

        const float  Width   = ImGui::GetContentRegionAvail().x;
        const ImVec2 TopLeft = ImGui::GetCursorScreenPos();

        // 🔴 The box is drawn by hand but the input is recorded FIRST (its Active state decides the border tone), so a
        //    straight sequence would fill the box on top of the glyphs and the field would read as empty. Split the
        //    draw list: the box goes into channel 0, the input's own commands into channel 1, and the merge below puts
        //    channel 1 in front. Setting the channel to whatever it already was — the earlier form — reordered nothing.
        const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + FilterHeight);

        Draw->ChannelsSplit(2);
        Draw->ChannelsSetCurrent(1);

        ImGui::PushStyleColor(ImGuiCol_FrameBg,     IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ApplyOpacity(Palette.TextMuted, 0.55f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));

        ImGui::SetCursorScreenPos(ImVec2(TopLeft.x + 28.0f, TopLeft.y + (FilterHeight - ImGui::GetFontSize()) * 0.5f));
        ImGui::SetNextItemWidth(Width - 36.0f);
        ImGui::InputTextWithHint("##layer-filter", "Filter\xE2\x80\xA6", State.FilterTerm, sizeof(State.FilterTerm));
        const bool Active = ImGui::IsItemActive();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        Draw->ChannelsSetCurrent(0);
        Draw->AddRectFilled(TopLeft, BottomRight, Palette.PanelHeader, 9.0f);
        Draw->AddRect(TopLeft, BottomRight, Active ? Palette.ControlActive : Palette.PanelBorder, 9.0f, 0,
                      Theme.Metrics.BorderThickness);
        InscribeMagnifierMark(Draw, ImVec2(TopLeft.x + 15.0f, TopLeft.y + FilterHeight * 0.5f), 14.0f,
                              ApplyOpacity(Palette.TextMuted, 0.8f));
        Draw->ChannelsMerge();

        ImGui::SetCursorScreenPos(ImVec2(TopLeft.x, BottomRight.y));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    }

    // ── one stack row: tag + thumb + two-line text + opacity pill + eye + caret ────────────────────────────────────────
    // Returns via the out-parameters so the caller can act after the loop rather than mutating mid-iteration.
    void InscribeStackRow(const ThemeConfiguration& Theme, LayerStackState& State, int Ordinal,
                          bool& FocusRequested, bool& FoldToggled, bool& VisibilityToggled)
    {
        const ColorPaletteDescriptor& Palette = Theme.Palette;
        LayerRecord& Record = State.Layers[Ordinal];
        ImDrawList*  Draw   = ImGui::GetWindowDrawList();

        const bool Focused = (State.FocusOrdinal == Ordinal);
        const bool Open    = Record.Expanded;
        const float Muted  = Record.Shown ? 1.0f : 0.4f;

        const float  Width   = ImGui::GetContentRegionAvail().x;
        const ImVec2 TopLeft = ImGui::GetCursorScreenPos();

        ImGui::PushID(Ordinal);
        ImGui::InvisibleButton("##row", ImVec2(Width, RowHeight));
        const bool RowHovered = ImGui::IsItemHovered();
        const bool RowPressed = ImGui::IsItemClicked();

        const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + RowHeight);

        // ---- the row surface ---------------------------------------------------------------------------------------
        // 🔴 An OPEN row is the HEAD of one card: it keeps the body's fill and border but DROPS its bottom edge, so
        //    the expand below continues the same box. An open head also stops recolouring on hover — the expand is a
        //    sibling no selector here can repaint, so a hover lift would light the head alone and split the card.
        ImU32 Fill   = IM_COL32(0, 0, 0, 0);
        ImU32 Border = IM_COL32(0, 0, 0, 0);
        if (Focused)      { Fill = Palette.ControlActive;  Border = Palette.PanelBorder; }
        else if (Open)    { Fill = Palette.PanelHeader;    Border = Palette.PanelBorder; }
        else if (RowHovered) Fill = Palette.ControlHovered;

        if ((Fill & IM_COL32_A_MASK) != 0)
        {
            Draw->AddRectFilled(TopLeft, Open ? ImVec2(BottomRight.x, BottomRight.y + 1.0f) : BottomRight, Fill, 0.0f);
        }
        if ((Border & IM_COL32_A_MASK) != 0)
        {
            if (Open)
            {
                // top / left / right only — the expand closes the bottom
                Draw->AddLine(TopLeft, ImVec2(BottomRight.x, TopLeft.y), Border, Theme.Metrics.BorderThickness);
                Draw->AddLine(TopLeft, ImVec2(TopLeft.x, BottomRight.y), Border, Theme.Metrics.BorderThickness);
                Draw->AddLine(ImVec2(BottomRight.x, TopLeft.y), BottomRight, Border, Theme.Metrics.BorderThickness);
            }
            else
            {
                Draw->AddRect(TopLeft, BottomRight, Border, 0.0f, 0, Theme.Metrics.BorderThickness);
            }
        }

        // The focus spine, hard against the rail's left edge.
        if (Focused)
        {
            Draw->AddRectFilled(ImVec2(TopLeft.x - 6.0f, TopLeft.y),
                                ImVec2(TopLeft.x - 6.0f + SpineWidth, BottomRight.y), Palette.AccentPrimary);
        }

        // ---- the hue tag (the layer's OWN identity colour) ---------------------------------------------------------
        float PenX = TopLeft.x + 5.0f;
        const float MidY = TopLeft.y + RowHeight * 0.5f;
        Draw->AddRectFilled(ImVec2(PenX, MidY - TagHeight * 0.5f), ImVec2(PenX + TagWidth, MidY + TagHeight * 0.5f),
                            PackTint(Record.Tag, Muted), TagWidth * 0.5f);
        PenX += TagWidth + 7.0f;

        // ---- the thumb: a black tile carrying the KIND tint as a filled glyph --------------------------------------
        const LayerKindEntry& Kind = ResolveLayerKindTable()[(int)Record.Kind];
        const ImVec2 ThumbTopLeft(PenX, MidY - ThumbSize * 0.5f);
        const ImVec2 ThumbBottomRight(PenX + ThumbSize, MidY + ThumbSize * 0.5f);
        Draw->AddRectFilled(ThumbTopLeft, ThumbBottomRight, IM_COL32(0, 0, 0, 0xff), 8.0f);
        Draw->AddRect(ThumbTopLeft, ThumbBottomRight, Palette.PanelBorder, 8.0f, 0, Theme.Metrics.BorderThickness);
        // A paint layer shows its authored ink; the flooded kinds show a solid kind-tinted mark.
        const ImVec2 GlyphCentre((ThumbTopLeft.x + ThumbBottomRight.x) * 0.5f, MidY);
        if (Record.PaintsBaseColour)
        {
            Draw->AddRectFilled(ImVec2(GlyphCentre.x - 7.5f, GlyphCentre.y - 7.5f),
                                ImVec2(GlyphCentre.x + 7.5f, GlyphCentre.y + 7.5f), PackTint(Record.Paint, Muted), 3.0f);
        }
        Draw->AddCircleFilled(ImVec2(ThumbBottomRight.x - 6.0f, ThumbTopLeft.y + 6.0f), 3.0f,
                              ApplyOpacity(Kind.Tint, Muted), 10);
        PenX += ThumbSize + 7.0f;

        // ---- the right-hand marks, measured from the far edge so the text column gets what is left -----------------
        float TailX = BottomRight.x - 7.0f;

        const ImVec2 CaretCentre(TailX - CaretSize * 0.5f, MidY);
        TailX -= CaretSize + 4.0f;

        const ImVec2 EyeCentre(TailX - EyeSize * 0.5f, MidY);
        TailX -= EyeSize + 5.0f;

        const ImVec2 PillTopLeft(TailX - OpacityPillWide, MidY - 9.0f);
        const ImVec2 PillBottomRight(TailX, MidY + 9.0f);
        TailX -= OpacityPillWide + 7.0f;

        // ---- the two-line text column ------------------------------------------------------------------------------
        const float TextRoom = TailX - PenX;
        if (TextRoom > 20.0f)
        {
            Draw->PushClipRect(ImVec2(PenX, TopLeft.y), ImVec2(PenX + TextRoom, BottomRight.y), true);

            char Meta[96];
            snprintf(Meta, sizeof(Meta), "%s \xC2\xB7 %s \xC2\xB7 %d ch",
                     Kind.Label, ResolveBlendModeTable()[Record.BlendIndex], Record.ChannelTotal);

            const float NameHeight = ImGui::GetFontSize();
            Draw->AddText(ImVec2(PenX, MidY - NameHeight - 1.0f), ApplyOpacity(Palette.TextPrimary, Muted), Record.Name);
            Draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.82f, ImVec2(PenX, MidY + 2.0f),
                          ApplyOpacity(Palette.TextMuted, Muted), Meta);

            Draw->PopClipRect();
        }

        // ---- the opacity pill: a horizontal SCRUB, not a click ----------------------------------------------------
        ImGui::SetCursorScreenPos(PillTopLeft);
        ImGui::InvisibleButton("##opacity", ImVec2(OpacityPillWide, 18.0f));
        const bool PillHovered = ImGui::IsItemHovered();
        if (PillHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            Record.Opacity += ImGui::GetIO().MouseDelta.x * 0.6f;
            if (Record.Opacity <   0.0f) Record.Opacity =   0.0f;
            if (Record.Opacity > 100.0f) Record.Opacity = 100.0f;
        }
        const bool PillEngaged = ImGui::IsItemActive();

        Draw->AddRectFilled(PillTopLeft, PillBottomRight, IM_COL32(0, 0, 0, 0xff), 9.0f);
        Draw->AddRect(PillTopLeft, PillBottomRight,
                      (PillHovered || PillEngaged) ? Palette.ControlActive : Palette.PanelBorder, 9.0f, 0,
                      Theme.Metrics.BorderThickness);
        char Reading[16];
        snprintf(Reading, sizeof(Reading), "%d%%", (int)(Record.Opacity + 0.5f));
        const ImVec2 ReadingExtent = ImGui::CalcTextSize(Reading);
        Draw->AddText(ImVec2((PillTopLeft.x + PillBottomRight.x - ReadingExtent.x) * 0.5f,
                             MidY - ReadingExtent.y * 0.5f),
                      (PillHovered || PillEngaged) ? Palette.TextPrimary : ApplyOpacity(Palette.TextMuted, Muted),
                      Reading);

        // ---- the eye. Hidden rows keep it lit; shown rows reveal it on hover ---------------------------------------
        ImGui::SetCursorScreenPos(ImVec2(EyeCentre.x - EyeSize * 0.5f, EyeCentre.y - EyeSize * 0.5f));
        ImGui::InvisibleButton("##visibility", ImVec2(EyeSize, EyeSize));
        const bool EyeHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) VisibilityToggled = true;

        if (RowHovered || EyeHovered || !Record.Shown)
        {
            if (EyeHovered)
            {
                Draw->AddRectFilled(ImVec2(EyeCentre.x - EyeSize * 0.5f, EyeCentre.y - EyeSize * 0.5f),
                                    ImVec2(EyeCentre.x + EyeSize * 0.5f, EyeCentre.y + EyeSize * 0.5f),
                                    Palette.ControlHovered, 5.0f);
            }
            InscribeEyeMark(Draw, EyeCentre, 14.0f,
                            EyeHovered ? Palette.TextPrimary : ApplyOpacity(Palette.TextMuted, 0.85f), Record.Shown);
        }

        // ---- the caret. Toggles the fold WITHOUT touching focus ----------------------------------------------------
        ImGui::SetCursorScreenPos(ImVec2(CaretCentre.x - CaretSize * 0.5f, CaretCentre.y - CaretSize * 0.5f));
        ImGui::InvisibleButton("##caret", ImVec2(CaretSize, CaretSize));
        const bool CaretHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) FoldToggled = true;

        if (CaretHovered)
        {
            Draw->AddRectFilled(ImVec2(CaretCentre.x - CaretSize * 0.5f, CaretCentre.y - CaretSize * 0.5f),
                                ImVec2(CaretCentre.x + CaretSize * 0.5f, CaretCentre.y + CaretSize * 0.5f),
                                Palette.ControlHovered, 5.0f);
        }
        InscribeChevron(Draw, CaretCentre, 9.0f,
                        CaretHovered ? Palette.TextPrimary
                                     : (Open ? Palette.TextPrimary : ApplyOpacity(Palette.TextMuted, 0.85f)), Open);

        // 🔴 A row press only counts when it did not land on one of the three marks above — those are consumed by
        //    their own InvisibleButtons, but ImGui reports the row's click regardless of who else was hovered.
        if (RowPressed && !PillHovered && !EyeHovered && !CaretHovered) FocusRequested = true;

        ImGui::PopID();

        ImGui::SetCursorScreenPos(ImVec2(TopLeft.x, BottomRight.y));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    }

    // ── the mask editor, inside the expand ────────────────────────────────────────────────────────────────────────────
    void InscribeMaskRegion(const ThemeConfiguration& Theme, LayerRecord& Record)
    {
        const ColorPaletteDescriptor& Palette = Theme.Palette;
        ImDrawList* Draw = ImGui::GetWindowDrawList();

        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        InscribeSectionCaption(Theme, "MASK");

        // ---- empty: one "Add mask" affordance ---------------------------------------------------------------------
        if (!Record.MaskEnabled)
        {
            const float  Width   = ImGui::GetContentRegionAvail().x;
            const ImVec2 TopLeft = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##add-mask", ImVec2(Width, 26.0f));
            const bool Hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) Record.MaskEnabled = true;

            const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + 26.0f);
            Draw->AddRectFilled(TopLeft, BottomRight, Hovered ? Palette.ControlHovered : Palette.PanelHeader, 7.0f);
            Draw->AddRect(TopLeft, BottomRight, Hovered ? Palette.ControlActive : Palette.PanelBorder, 7.0f, 0,
                          Theme.Metrics.BorderThickness);

            const ImVec2 Extent = ImGui::CalcTextSize("Add mask");
            const float  TextX  = TopLeft.x + (Width - Extent.x + 16.0f) * 0.5f;
            const float  MidY   = TopLeft.y + 13.0f;
            InscribePlusMark(Draw, ImVec2(TextX - 10.0f, MidY), 8.0f,
                             Hovered ? Palette.TextPrimary : Palette.TextMuted);
            Draw->AddText(ImVec2(TextX, MidY - Extent.y * 0.5f),
                          Hovered ? Palette.TextPrimary : Palette.TextMuted, "Add mask");
            return;
        }

        // ---- the head: the mask's own shade as a tile, its meta line, and a delete mark ----------------------------
        {
            const float  Width   = ImGui::GetContentRegionAvail().x;
            const ImVec2 TopLeft = ImGui::GetCursorScreenPos();
            const float  Height  = 34.0f;

            // 🔴 The tile shows the RESOLVED shade: the fill, then the invert applied on top. Reading the fill alone
            //    draws an inverted black mask as black, which is exactly the state the invert flag is there to show.
            const float Base  = (Record.MaskFillMode == MaskFill::Black) ? 0.0f : 1.0f;
            const float Light = Record.MaskInverted ? (1.0f - Base) : Base;

            const ImVec2 TileTopLeft(TopLeft.x, TopLeft.y + 2.0f);
            const ImVec2 TileBottomRight(TopLeft.x + 30.0f, TopLeft.y + 32.0f);
            Draw->AddRectFilled(TileTopLeft, TileBottomRight,
                                ImGui::ColorConvertFloat4ToU32(ImVec4(Light, Light, Light, 1.0f)), 6.0f);
            Draw->AddRect(TileTopLeft, TileBottomRight, Palette.PanelBorder, 6.0f, 0, Theme.Metrics.BorderThickness);

            char Meta[96];
            snprintf(Meta, sizeof(Meta), "%s fill%s \xC2\xB7 %d comp",
                     (Record.MaskFillMode == MaskFill::Black) ? "Black" : "White",
                     Record.MaskInverted ? " \xC2\xB7 inverted" : "", Record.ComponentTotal);

            Draw->AddText(ImVec2(TopLeft.x + 38.0f, TopLeft.y + 4.0f), Palette.TextPrimary, "Layer mask");
            Draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.82f, ImVec2(TopLeft.x + 38.0f, TopLeft.y + 19.0f),
                          Palette.TextMuted, Meta);

            // the delete mark, hard right
            const ImVec2 CrossCentre(TopLeft.x + Width - 10.0f, TopLeft.y + Height * 0.5f);
            ImGui::SetCursorScreenPos(ImVec2(CrossCentre.x - 9.0f, CrossCentre.y - 9.0f));
            ImGui::InvisibleButton("##drop-mask", ImVec2(18.0f, 18.0f));
            const bool CrossHovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) Record.MaskEnabled = false;
            InscribeCrossMark(Draw, CrossCentre, 9.0f,
                              CrossHovered ? IM_COL32(0xe0, 0x5a, 0x5a, 0xff) : Palette.TextMuted);

            ImGui::SetCursorScreenPos(ImVec2(TopLeft.x, TopLeft.y + Height));
            ImGui::Dummy(ImVec2(Width, 0.0f));
        }

        // ---- the White | Black | Invert toolbar -------------------------------------------------------------------
        {
            const float  Width   = ImGui::GetContentRegionAvail().x;
            const ImVec2 TopLeft = ImGui::GetCursorScreenPos();
            const float  Each    = (Width - 8.0f) / 3.0f;

            struct ToolEntry { const char* Label; bool On; };
            const ToolEntry Tools[3] =
            {
                { "White",  Record.MaskFillMode != MaskFill::Black },
                { "Black",  Record.MaskFillMode == MaskFill::Black },
                { "Invert", Record.MaskInverted }
            };

            for (int Ordinal = 0; Ordinal < 3; ++Ordinal)
            {
                const ImVec2 ToolTopLeft(TopLeft.x + (Each + 4.0f) * Ordinal, TopLeft.y);
                const ImVec2 ToolBottomRight(ToolTopLeft.x + Each, ToolTopLeft.y + MaskToolHeight);

                ImGui::SetCursorScreenPos(ToolTopLeft);
                ImGui::PushID(Ordinal);
                ImGui::InvisibleButton("##tool", ImVec2(Each, MaskToolHeight));
                const bool Hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked())
                {
                    if (Ordinal == 0)      Record.MaskFillMode = MaskFill::White;
                    else if (Ordinal == 1) Record.MaskFillMode = MaskFill::Black;
                    else                   Record.MaskInverted = !Record.MaskInverted;
                }
                ImGui::PopID();

                const bool  On   = Tools[Ordinal].On;
                const ImU32 Fill = On ? Palette.AccentPrimary : (Hovered ? Palette.ControlHovered : Palette.PanelHeader);
                const ImU32 Text = On ? Palette.TextOnAccent
                                      : (Hovered ? Palette.TextPrimary : Palette.TextMuted);

                Draw->AddRectFilled(ToolTopLeft, ToolBottomRight, Fill, 6.0f);
                if (!On) Draw->AddRect(ToolTopLeft, ToolBottomRight, Palette.PanelBorder, 6.0f, 0,
                                       Theme.Metrics.BorderThickness);

                const ImVec2 Extent = ImGui::CalcTextSize(Tools[Ordinal].Label);
                Draw->AddText(ImVec2((ToolTopLeft.x + ToolBottomRight.x - Extent.x) * 0.5f,
                                     (ToolTopLeft.y + ToolBottomRight.y - Extent.y) * 0.5f),
                              Text, Tools[Ordinal].Label);
            }

            ImGui::SetCursorScreenPos(ImVec2(TopLeft.x, TopLeft.y + MaskToolHeight));
            ImGui::Dummy(ImVec2(Width, 0.0f));
        }

        // ---- mask strength -----------------------------------------------------------------------------------------
        {
            ValueSliderDescriptor Strength = {};
            Strength.Label   = "Strength";
            Strength.Value   = &Record.MaskStrength;
            Strength.Minimum = 0.0f;
            Strength.Maximum = 100.0f;
            Strength.Format  = "%.0f";
            Strength.Unit    = "%";
            Strength.Enabled = true;
            ConstructValueSlider(Theme, Strength);
        }

        // ---- the component stack -----------------------------------------------------------------------------------
        for (int Ordinal = 0; Ordinal < Record.ComponentTotal; ++Ordinal)
        {
            MaskComponent& Component = Record.Components[Ordinal];
            ImGui::PushID(1000 + Ordinal);

            const float  Width   = ImGui::GetContentRegionAvail().x;
            const ImVec2 TopLeft = ImGui::GetCursorScreenPos();
            const float  Height  = 22.0f;

            ImGui::InvisibleButton("##component", ImVec2(Width, Height));
            const bool Hovered = ImGui::IsItemHovered();

            const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + Height);
            if (Hovered) Draw->AddRectFilled(TopLeft, BottomRight, Palette.ControlHovered, 5.0f);

            // the enable dot
            const ImVec2 DotCentre(TopLeft.x + 9.0f, TopLeft.y + Height * 0.5f);
            Draw->AddCircleFilled(DotCentre, 3.5f,
                                  Component.Enabled ? Palette.AccentPrimary : ApplyOpacity(Palette.TextMuted, 0.4f), 10);

            char Reading[16];
            snprintf(Reading, sizeof(Reading), "%d%%", (int)(Component.Weight * 100.0f + 0.5f));
            const ImVec2 ReadingExtent = ImGui::CalcTextSize(Reading);

            Draw->AddText(ImVec2(TopLeft.x + 19.0f, TopLeft.y + (Height - ImGui::GetFontSize()) * 0.5f),
                          Component.Enabled ? Palette.TextPrimary : ApplyOpacity(Palette.TextMuted, 0.6f),
                          Component.Label);
            Draw->AddText(ImVec2(BottomRight.x - ReadingExtent.x - 6.0f, TopLeft.y + (Height - ReadingExtent.y) * 0.5f),
                          Palette.TextMuted, Reading);

            // clicking the row toggles the component
            if (ImGui::IsItemClicked()) Component.Enabled = !Component.Enabled;

            ImGui::SetCursorScreenPos(ImVec2(TopLeft.x, BottomRight.y));
            ImGui::Dummy(ImVec2(Width, 0.0f));
            ImGui::PopID();
        }
    }

    // ── the inline expand: the lower half of the open row's card ───────────────────────────────────────────────────────
    void InscribeInlineExpand(const ThemeConfiguration& Theme, LayerStackState& State, int Ordinal)
    {
        const ColorPaletteDescriptor& Palette = Theme.Palette;
        LayerRecord& Record = State.Layers[Ordinal];
        ImDrawList*  Draw   = ImGui::GetWindowDrawList();

        const bool   Focused = (State.FocusOrdinal == Ordinal);
        const float  Width   = ImGui::GetContentRegionAvail().x;
        const ImVec2 TopLeft = ImGui::GetCursorScreenPos();

        ImGui::PushID(Ordinal);

        // The body's own surface is stamped first so every control lands on top of it. Its height is not known
        // until the rows are recorded, so the fill is emitted into a split channel and closed out below.
        Draw->ChannelsSplit(2);
        Draw->ChannelsSetCurrent(1);

        ImGui::Dummy(ImVec2(Width, 5.0f));
        ImGui::Indent(8.0f);
        ImGui::PushItemWidth(Width - 16.0f);

        // ---- Visible ----------------------------------------------------------------------------------------------
        {
            int Selected = Record.Shown ? 0 : 1;
            const char* const Options[2] = { "Shown", "Hidden" };
            SelectionEntryDescriptor Visible = {};
            Visible.Label         = "Visible";
            Visible.SelectedIndex = &Selected;
            Visible.Options       = Options;
            Visible.OptionCount   = 2;
            Visible.Enabled       = true;
            if (ConstructSelectionEntry(Theme, Visible)) Record.Shown = (Selected == 0);
        }

        // ---- Blend ------------------------------------------------------------------------------------------------
        {
            SelectionEntryDescriptor Blend = {};
            Blend.Label         = "Blend";
            Blend.SelectedIndex = &Record.BlendIndex;
            Blend.Options       = ResolveBlendModeTable();
            Blend.OptionCount   = BlendModeCount;
            Blend.Enabled       = true;
            ConstructSelectionEntry(Theme, Blend);
        }

        // ---- Opacity ----------------------------------------------------------------------------------------------
        {
            ValueSliderDescriptor Opacity = {};
            Opacity.Label   = "Opacity";
            Opacity.Value   = &Record.Opacity;
            Opacity.Minimum = 0.0f;
            Opacity.Maximum = 100.0f;
            Opacity.Format  = "%.0f";
            Opacity.Unit    = "%";
            Opacity.Enabled = true;
            ConstructValueSlider(Theme, Opacity);
        }

        // ---- Paint — only where the layer actually writes baseColour ----------------------------------------------
        // 🔴 Labelled "Paint", not "Colour": the Tag row below is also a colour, and these two do different things —
        //    this one changes the pixels the layer deposits, Tag only changes its marker in the rail.
        if (Record.PaintsBaseColour)
        {
            ColorEntryDescriptor Paint = {};
            Paint.Label        = "Paint";
            Paint.Channels     = Record.Paint;
            Paint.IncludeAlpha = false;
            Paint.Enabled      = true;
            ConstructColorEntry(Theme, Paint);
        }

        // ---- Tag — offered on EVERY layer, unlike Paint above -----------------------------------------------------
        {
            ColorEntryDescriptor Tag = {};
            Tag.Label        = "Tag";
            Tag.Channels     = Record.Tag;
            Tag.IncludeAlpha = false;
            Tag.Enabled      = true;
            ConstructColorEntry(Theme, Tag);
        }

        // ---- the mask editor ---------------------------------------------------------------------------------------
        InscribeMaskRegion(Theme, Record);

        ImGui::PopItemWidth();
        ImGui::Unindent(8.0f);
        ImGui::Dummy(ImVec2(Width, 7.0f));

        // ---- close out the card's lower half ----------------------------------------------------------------------
        const float BottomY = ImGui::GetCursorScreenPos().y;
        Draw->ChannelsSetCurrent(0);

        const ImVec2 BottomRight(TopLeft.x + Width, BottomY);
        Draw->AddRectFilled(TopLeft, BottomRight, Focused ? Palette.ControlActive : Palette.PanelHeader, 0.0f);
        // left / right / bottom only — the row above owns the top edge
        Draw->AddLine(TopLeft, ImVec2(TopLeft.x, BottomRight.y), Palette.PanelBorder, Theme.Metrics.BorderThickness);
        Draw->AddLine(ImVec2(BottomRight.x, TopLeft.y), BottomRight, Palette.PanelBorder, Theme.Metrics.BorderThickness);
        Draw->AddLine(ImVec2(TopLeft.x, BottomRight.y), BottomRight, Palette.PanelBorder, Theme.Metrics.BorderThickness);

        // the lower half of the focus spine, continuing the row's
        if (Focused)
        {
            Draw->AddRectFilled(ImVec2(TopLeft.x - 6.0f, TopLeft.y),
                                ImVec2(TopLeft.x - 6.0f + SpineWidth, BottomRight.y), Palette.AccentPrimary);
        }

        Draw->ChannelsMerge();
        ImGui::PopID();
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructLayerStackPanel(const ThemeConfiguration& Theme, LayerStackState& State)
{
    const ColorPaletteDescriptor& Palette = Theme.Palette;
    ImDrawList* Draw = ImGui::GetWindowDrawList();

    // ---- the pane head: "Layers" over the surface name, with the tally ---------------------------------------------
    {
        const float  Width   = ImGui::GetContentRegionAvail().x;
        const ImVec2 TopLeft = ImGui::GetCursorScreenPos();
        const float  Height  = 44.0f;

        Draw->AddRectFilled(TopLeft, ImVec2(TopLeft.x + Width, TopLeft.y + Height), Palette.PanelHeader);
        Draw->AddLine(ImVec2(TopLeft.x, TopLeft.y + Height), ImVec2(TopLeft.x + Width, TopLeft.y + Height),
                      Palette.PanelBorder, Theme.Metrics.BorderThickness);

        Draw->AddText(ImVec2(TopLeft.x + 12.0f, TopLeft.y + 8.0f), Palette.TextPrimary, "Layers");
        Draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.85f, ImVec2(TopLeft.x + 12.0f, TopLeft.y + 25.0f),
                      Palette.TextMuted, State.SurfaceName);

        char Tally[16];
        snprintf(Tally, sizeof(Tally), "%d", State.LayerTotal);
        const ImVec2 TallyExtent = ImGui::CalcTextSize(Tally);
        Draw->AddText(ImVec2(TopLeft.x + Width - TallyExtent.x - 14.0f, TopLeft.y + (Height - TallyExtent.y) * 0.5f),
                      Palette.TextMuted, Tally);

        ImGui::SetCursorScreenPos(ImVec2(TopLeft.x, TopLeft.y + Height));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    }

    // ---- "+ Add Layer", then the filter --------------------------------------------------------------------------
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::Indent(7.0f);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 7.0f);

    InscribeAddAffordance(Theme, State);
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    InscribeFilterField(Theme, State);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    // ---- the stack rows ------------------------------------------------------------------------------------------
    // 🔴 Every mutation is DEFERRED past the loop: a focus change, a fold or a removal that landed mid-iteration
    //    would shift the records the remaining rows are drawn from, so a row would be skipped or drawn twice.
    int  FocusRequest      = -1;
    int  FoldRequest       = -1;
    int  VisibilityRequest = -1;

    for (int Ordinal = 0; Ordinal < State.LayerTotal; ++Ordinal)
    {
        if (!QueryLayerPassesFilter(State.Layers[Ordinal], State.FilterTerm)) continue;

        bool FocusRequested = false, FoldToggled = false, VisibilityToggled = false;
        InscribeStackRow(Theme, State, Ordinal, FocusRequested, FoldToggled, VisibilityToggled);

        if (FocusRequested)    FocusRequest      = Ordinal;
        if (FoldToggled)       FoldRequest       = Ordinal;
        if (VisibilityToggled) VisibilityRequest = Ordinal;

        if (State.Layers[Ordinal].Expanded) InscribeInlineExpand(Theme, State, Ordinal);

        ImGui::Dummy(ImVec2(0.0f, RowGap));
    }

    ImGui::PopItemWidth();
    ImGui::Unindent(7.0f);

    // ---- apply the deferred verbs --------------------------------------------------------------------------------
    // 📝 Focusing a row also OPENS its expand, so the settings are there to edit right away (LayerInspector.js:820).
    if (FocusRequest >= 0)
    {
        State.FocusOrdinal                   = FocusRequest;
        State.Layers[FocusRequest].Expanded  = true;
    }
    // 🔴 The caret toggles the fold WITHOUT touching focus, so a row can be opened to read its settings without
    //    stealing the paint target from the layer currently being stroked.
    if (FoldRequest >= 0)
    {
        State.Layers[FoldRequest].Expanded = !State.Layers[FoldRequest].Expanded;
    }
    if (VisibilityRequest >= 0)
    {
        State.Layers[VisibilityRequest].Shown = !State.Layers[VisibilityRequest].Shown;
    }

    // ---- the foot tally ------------------------------------------------------------------------------------------
    {
        const int    Hidden  = CountHiddenLayers(State);
        const float  Width   = ImGui::GetContentRegionAvail().x;
        const ImVec2 TopLeft = ImGui::GetCursorScreenPos();
        const float  Height  = 26.0f;

        Draw->AddLine(TopLeft, ImVec2(TopLeft.x + Width, TopLeft.y), Palette.PanelBorder,
                      Theme.Metrics.BorderThickness);

        char Reading[64];
        if (Hidden > 0) snprintf(Reading, sizeof(Reading), "%d / %d   \xC2\xB7   %d hidden",
                                 State.LayerTotal, LayerCapacity, Hidden);
        else            snprintf(Reading, sizeof(Reading), "%d / %d", State.LayerTotal, LayerCapacity);

        Draw->AddText(ImVec2(TopLeft.x + 12.0f, TopLeft.y + (Height - ImGui::GetFontSize()) * 0.5f),
                      Palette.TextMuted, Reading);

        ImGui::SetCursorScreenPos(ImVec2(TopLeft.x, TopLeft.y + Height));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    }
}

}   // namespace LayerStackValidation
