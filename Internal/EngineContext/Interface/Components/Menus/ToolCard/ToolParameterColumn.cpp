/*==============================================================================================================================================
                                                        TOOLPARAMETERCOLUMN.CPP
==============================================================================================================================================*/
// 🧩 Draws the six parameter widgets. Each is an ImDrawList composition over invisible hit-test buttons rather than an ImGui control, for the same
//    reason the tiles are: the prototype's slider is a split value pill beside a bordered track with an oversized knob, and its segmented strip
//    wraps — neither is expressible through SliderFloat or a row of Buttons without fighting the style stack harder than drawing them outright.

#include "ToolParameterColumn.h"
#include "ToolGlyphInscription.h"

#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float BodyPadding      = 13.0f;   // [px] - .options-body padding
    constexpr float RowGap           = 13.0f;   // [px] - .options-body gap
    constexpr float LabelGap         =  7.0f;   // [px] - .param gap, caption to widget
    constexpr float LabelGlyphEdge   = 13.0f;   // [px] - .param-label .name svg
    constexpr float LabelGlyphGap    =  6.0f;   // [px] - .param-label .name gap

    constexpr float ValueBoxHeight   = 38.0f;   // [px] - --row-h
    constexpr float ValueBoxWidth    = 84.0f;   // [px] - .valuebox flex-basis
    constexpr float ValueUnitMinWide = 32.0f;   // [px] - .valuebox .unitseg min-width
    constexpr float ValuePadding     =  9.0f;   // [px] - .valuebox .num padding
    constexpr float SliderGap        =  9.0f;   // [px] - .slider-ctl gap
    constexpr float TrackHeight      = 26.0f;   // [px] - .slider height
    constexpr float TrackBorder      =  1.5f;   // [px] - .slider border
    constexpr float KnobEdge         = 21.0f;   // [px] - .slider .knob

    constexpr float SegmentHeight    = 32.0f;   // [px] - .seg-opt height
    constexpr float SegmentGap       =  6.0f;   // [px] - .segment gap
    constexpr float SegmentPadding   = 12.0f;   // [px] - .seg-opt padding
    constexpr float SegmentRounding  =  9.0f;   // [px] - .seg-opt radius

    constexpr float SwitchWidth      = 42.0f;   // [px] - .switch
    constexpr float SwitchHeight     = 24.0f;   // [px]
    constexpr float SwitchNubEdge    = 18.0f;   // [px] - .switch .nub
    constexpr float SwitchNubInset   =  3.0f;   // [px]

    constexpr float NegatePadding    = 10.0f;   // [px] - .neg-pill padding
    constexpr float ChipHeight       = 30.0f;   // [px] - .chip height
    constexpr float ChipPadding      = 10.0f;   // [px] - .chip padding
    constexpr float ChipGlyphEdge    = 12.0f;   // [px] - .chip svg
    constexpr float ChipGlyphGap     =  6.0f;   // [px] - .chip gap
    constexpr float SwatchEdge       = 22.0f;   // [px] - .swatch
    constexpr float SwatchGap        =  6.0f;   // [px] - .swatches gap
    constexpr float SwatchRounding   =  7.0f;   // [px] - .swatch radius
    constexpr float SwatchBorder     =  2.0f;   // [px] - .swatch border

    constexpr float VoidNoteGlyph    = 26.0f;   // [px] - .shot-note svg
    constexpr float VoidNoteGap      =  7.0f;   // [px] - .shot-note gap

    constexpr ImU32 SegmentSelectedInk = IM_COL32(0x1B, 0x1B, 0x1E, 0xFF);   // .seg-opt.sel colour
    constexpr ImU32 NegateSelectedInk  = IM_COL32(0x0B, 0x0B, 0x0D, 0xFF);   // .neg-pill.on colour
    constexpr ImU32 TrackOutline       = IM_COL32(0xFF, 0xFF, 0xFF, 0x38);   // --outline rgba(255,255,255,.22)
    constexpr ImU32 SliderGround       = IM_COL32(0x00, 0x00, 0x00, 0xFF);   // .slider background
    constexpr ImU32 SwatchSelectedEdge = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);   // .swatch.on border

    // 📝 How many entries a null-terminated authored array actually carries.
    int TallyLabels(const char* const* Labels, int Ceiling)
    {
        int Count = 0;
        while (Count < Ceiling && Labels[Count] != nullptr)
        {
            ++Count;
        }
        return Count;
    }

    // 📝 A "#rrggbb" swatch string as a draw colour. Parsed by hand rather than through sscanf: the six digits are a fixed shape, and
    //    the scanf family's %x wants an `unsigned int` whose width the format string cannot enforce. An unparsable string falls back
    //    to a neutral grey rather than drawing garbage — a malformed tint should look wrong on inspection, not read as a black chip.
    ImU32 ResolveSwatchTint(const char* Text)
    {
        constexpr ImU32 FallbackTint = IM_COL32(0x80, 0x80, 0x88, 0xFF);

        if (Text == nullptr || Text[0] != '#' || std::strlen(Text) < 7)
        {
            return FallbackTint;
        }

        int Channels[3] = {};
        for (int ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
        {
            int Value = 0;
            for (int DigitIndex = 0; DigitIndex < 2; ++DigitIndex)
            {
                const char Digit = Text[1 + ChannelIndex * 2 + DigitIndex];
                int        Nibble = 0;
                if      (Digit >= '0' && Digit <= '9') { Nibble = Digit - '0'; }
                else if (Digit >= 'a' && Digit <= 'f') { Nibble = Digit - 'a' + 10; }
                else if (Digit >= 'A' && Digit <= 'F') { Nibble = Digit - 'A' + 10; }
                else                                   { return FallbackTint; }
                Value = Value * 16 + Nibble;
            }
            Channels[ChannelIndex] = Value;
        }
        return IM_COL32(Channels[0], Channels[1], Channels[2], 0xFF);
    }

    // 📝 A reading printed the way the prototype's number input shows it: trailing zeros dropped, so 2.50 reads "2.5" and 2.00 reads
    //    "2". A fixed precision would print "12.00" for a subdivision count.
    void FormatReading(char* Buffer, int Capacity, float Reading)
    {
        std::snprintf(Buffer, static_cast<size_t>(Capacity), "%.2f", Reading);

        char* const Point = std::strchr(Buffer, '.');
        if (Point == nullptr)
        {
            return;
        }
        char* Tail = Buffer + std::strlen(Buffer) - 1;
        while (Tail > Point && *Tail == '0')
        {
            *Tail-- = '\0';
        }
        if (Tail == Point)
        {
            *Point = '\0';
        }
    }

    // 📝 The caption strip above a widget: glyph, name, and the row's own trailing note. Shared by every kind except Toggle, whose
    //    caption sits inline with its switch.
    void InscribeParameterLabel(const SvgIconRegistry* Icons,
                                const ToolParameterDescriptor& Parameter,
                                ImVec2                 Origin,
                                float                  ColumnWidth,
                                const ToolCardPalette& Palette)
    {
        ImDrawList* const Canvas    = ImGui::GetWindowDrawList();
        const float       LineHeight = ImGui::GetTextLineHeight();
        const float       MidY       = Origin.y + LineHeight * 0.5f;

        InscribeToolGlyph(Icons, Parameter.GlyphName,
                          ImVec2(Origin.x, MidY - LabelGlyphEdge * 0.5f), LabelGlyphEdge, true);
        Canvas->AddText(ImVec2(Origin.x + LabelGlyphEdge + LabelGlyphGap, Origin.y), Palette.Muted,
                        (Parameter.Label != nullptr) ? Parameter.Label : "");
        (void)ColumnWidth;
    }

    // 📝 One wrapping strip of pills, shared by Segmented, AxisChoice's axis strip and SnapTargets' chips. Returns the height it
    //    consumed and reports a click through ClickedIndex — wrapping is why this cannot be a fixed-height row.
    struct StripOutcome
    {
        float Height;
        int   ClickedIndex;
    };

    StripOutcome InscribePillStrip(const SvgIconRegistry* Icons,
                                   const char* const*     Labels,
                                   const bool*            Armed,          // null for exclusive strips
                                   int                    LabelCount,
                                   int                    SelectedIndex,  // -1 for multi-select strips
                                   ImVec2                 Origin,
                                   float                  StripWidth,
                                   float                  PillHeight,
                                   float                  PillPadding,
                                   bool                   CarryGlyph,
                                   const ToolCardPalette& Palette)
    {
        ImDrawList* const Canvas  = ImGui::GetWindowDrawList();
        StripOutcome      Outcome = { PillHeight, -1 };

        float CursorX = Origin.x;
        float CursorY = Origin.y;

        for (int LabelIndex = 0; LabelIndex < LabelCount; ++LabelIndex)
        {
            const char* const Text     = Labels[LabelIndex];
            const ImVec2      TextSize = ImGui::CalcTextSize(Text);

            float PillWidth = TextSize.x + PillPadding * 2.0f;
            if (CarryGlyph)
            {
                PillWidth += ChipGlyphEdge + ChipGlyphGap;
            }

            // Wrap when the pill would overrun the strip, exactly as the prototype's `flex-wrap` does.
            if (CursorX > Origin.x && CursorX + PillWidth > Origin.x + StripWidth)
            {
                CursorX         = Origin.x;
                CursorY        += PillHeight + SegmentGap;
                Outcome.Height += PillHeight + SegmentGap;
            }

            const ImVec2 PillMin(CursorX, CursorY);
            const ImVec2 PillMax(CursorX + PillWidth, CursorY + PillHeight);

            ImGui::SetCursorScreenPos(PillMin);
            ImGui::PushID(LabelIndex);
            const bool Pressed = ImGui::InvisibleButton("##pill", ImVec2(PillWidth, PillHeight));
            const bool Hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            if (Pressed)
            {
                Outcome.ClickedIndex = LabelIndex;
            }

            const bool Chosen = (Armed != nullptr) ? Armed[LabelIndex] : (LabelIndex == SelectedIndex);

            ImU32 Ground  = Hovered ? Palette.TileHoverFill : Palette.TileFill;
            ImU32 TextInk = Hovered ? Palette.Ink : Palette.Muted;
            if (Chosen)
            {
                if (Armed != nullptr)
                {
                    // A multi-select chip reads as armed through an accent WASH and border rather than a solid fill: several can be
                    // on at once, and a row of solid accent pills would drown the pane.
                    Ground  = IM_COL32(0x5B, 0x8C, 0xFF, 0x24);
                    TextInk = Palette.Ink;
                }
                else
                {
                    Ground  = Palette.KnobFill;
                    TextInk = SegmentSelectedInk;
                }
            }

            Canvas->AddRectFilled(PillMin, PillMax, Ground, SegmentRounding);
            if (Chosen && Armed != nullptr)
            {
                Canvas->AddRect(PillMin, PillMax, Palette.Accent, SegmentRounding, 0, 1.0f);
            }

            float TextX = PillMin.x + PillPadding;
            if (CarryGlyph)
            {
                InscribeToolGlyph(Icons, "ParamSnap",
                                  ImVec2(TextX, (PillMin.y + PillMax.y) * 0.5f - ChipGlyphEdge * 0.5f),
                                  ChipGlyphEdge, !Chosen);
                TextX += ChipGlyphEdge + ChipGlyphGap;
            }
            Canvas->AddText(ImVec2(TextX, (PillMin.y + PillMax.y) * 0.5f - TextSize.y * 0.5f), TextInk, Text);

            CursorX = PillMax.x + SegmentGap;
        }

        return Outcome;
    }

    // 📝 The height one row consumes, caption included. Kept beside the draw so the two cannot drift; the wrapping kinds are measured
    //    by walking the same advance the draw uses.
    float MeasureRowHeight(const ToolParameterDescriptor& Parameter, float ColumnWidth)
    {
        const float LineHeight  = ImGui::GetTextLineHeight();
        const float StripWidth  = ColumnWidth - BodyPadding * 2.0f;
        const float CaptionBand = LineHeight + LabelGap;

        switch (Parameter.Category)
        {
            case ToolParameterCategory::Slider:
                return CaptionBand + ValueBoxHeight;

            case ToolParameterCategory::Toggle:
                // No caption band: the name sits inline with the switch.
                return (SwitchHeight > LineHeight) ? SwitchHeight : LineHeight;

            case ToolParameterCategory::Swatches:
                return CaptionBand + SwatchEdge;

            case ToolParameterCategory::Segmented:
            case ToolParameterCategory::AxisChoice:
            case ToolParameterCategory::SnapTargets:
            {
                const bool  Chips      = (Parameter.Category == ToolParameterCategory::SnapTargets);
                const char* const* Labels = Chips ? Parameter.SnapTargetLabels : Parameter.OptionLabels;
                const int   Ceiling    = Chips ? ToolSnapTargetLimit : ToolSegmentedOptionLimit;
                const int   LabelCount = TallyLabels(Labels, Ceiling);
                const float PillHeight = Chips ? ChipHeight : SegmentHeight;
                const float Padding    = Chips ? ChipPadding : SegmentPadding;

                // An axis strip shares its row with the negate pill, so it wraps within the room that pill leaves.
                float Available = StripWidth;
                if (Parameter.Category == ToolParameterCategory::AxisChoice)
                {
                    Available -= ImGui::CalcTextSize("-").x + NegatePadding * 2.0f + SegmentGap;
                }

                float Consumed = PillHeight;
                float CursorX  = 0.0f;
                for (int LabelIndex = 0; LabelIndex < LabelCount; ++LabelIndex)
                {
                    float PillWidth = ImGui::CalcTextSize(Labels[LabelIndex]).x + Padding * 2.0f;
                    if (Chips)
                    {
                        PillWidth += ChipGlyphEdge + ChipGlyphGap;
                    }
                    if (CursorX > 0.0f && CursorX + PillWidth > Available)
                    {
                        CursorX   = 0.0f;
                        Consumed += PillHeight + SegmentGap;
                    }
                    CursorX += PillWidth + SegmentGap;
                }
                return CaptionBand + Consumed;
            }
        }
        return CaptionBand + SegmentHeight;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void BindToolParameterBlock(ToolParameterBlock&            Block,
                            const ToolParameterDescriptor* Parameters,
                            int                            ParameterCount,
                            int                            Band,
                            int                            Tile)
{
    // 🔴 Reseed ONLY on an identity change. Called every frame, so reseeding unconditionally would reset the reader's slider the
    //    instant they let go of it — the drag would appear to snap back to the authored default on every single frame.
    if (Block.Band == Band && Block.Tile == Tile)
    {
        return;
    }

    Block.Band     = Band;
    Block.Tile     = Tile;
    Block.RowCount = 0;

    if (Parameters == nullptr || ParameterCount <= 0)
    {
        return;
    }

    const int Bound = (ParameterCount < ToolParameterStateLimit) ? ParameterCount : ToolParameterStateLimit;
    for (int RowIndex = 0; RowIndex < Bound; ++RowIndex)
    {
        const ToolParameterDescriptor& Parameter = Parameters[RowIndex];
        ToolParameterState&            State     = Block.Rows[RowIndex];

        State       = ToolParameterState{};
        State.Reading      = Parameter.InitialReading;
        State.ChosenOption = Parameter.InitialOption;
        State.Activation   = Parameter.InitialActivation;
        State.Negated      = false;
        for (int TargetIndex = 0; TargetIndex < ToolSnapTargetLimit; ++TargetIndex)
        {
            State.TargetArmed[TargetIndex] = Parameter.SnapTargetArmed[TargetIndex];
        }
    }
    Block.RowCount = Bound;
}


float MeasureToolParameterHeight(const ToolParameterDescriptor* Parameters,
                                int                            ParameterCount,
                                const ToolCardMetrics&         Metrics)
{
    if (Parameters == nullptr || ParameterCount <= 0)
    {
        return BodyPadding * 2.0f;
    }

    float Consumed = BodyPadding * 2.0f;
    for (int RowIndex = 0; RowIndex < ParameterCount; ++RowIndex)
    {
        Consumed += MeasureRowHeight(Parameters[RowIndex], Metrics.RightColumnWidth);
        if (RowIndex + 1 < ParameterCount)
        {
            Consumed += RowGap;
        }
    }
    return Consumed;
}


bool InscribeToolParameterColumn(const SvgIconRegistry*         Icons,
                                 const ToolParameterDescriptor* Parameters,
                                 int                            ParameterCount,
                                 ToolParameterBlock&            Block,
                                 ImVec2                         Origin,
                                 float                          ColumnWidth,
                                 const ToolCardPalette&         Palette,
                                 const ToolCardMetrics&         Metrics)
{
    (void)Metrics;

    if (Parameters == nullptr || ParameterCount <= 0)
    {
        return false;
    }

    ImDrawList* const Canvas     = ImGui::GetWindowDrawList();
    const float       LineHeight = ImGui::GetTextLineHeight();
    const float       StripWidth = ColumnWidth - BodyPadding * 2.0f;

    bool  Changed = false;
    float CursorY = Origin.y + BodyPadding;
    const float RowLeft = Origin.x + BodyPadding;

    const int Bound = (ParameterCount < Block.RowCount) ? ParameterCount : Block.RowCount;

    for (int RowIndex = 0; RowIndex < Bound; ++RowIndex)
    {
        const ToolParameterDescriptor& Parameter = Parameters[RowIndex];
        ToolParameterState&            State     = Block.Rows[RowIndex];

        ImGui::PushID(RowIndex);

        // A Toggle carries its own inline caption; every other kind takes the caption strip above its widget.
        float WidgetY = CursorY;
        if (Parameter.Category != ToolParameterCategory::Toggle)
        {
            InscribeParameterLabel(Icons, Parameter, ImVec2(RowLeft, CursorY), ColumnWidth, Palette);
            WidgetY = CursorY + LineHeight + LabelGap;
        }

        switch (Parameter.Category)
        {
            //---------------------------------------------------------- SLIDER ----------------------------------------------------------
            case ToolParameterCategory::Slider:
            {
                // The split value pill: a right-aligned number over black, then a unit segment. Drawn as two rects inside one pill so
                // the rounded ends belong to the pill rather than to either half.
                const ImVec2 BoxMin(RowLeft, WidgetY);
                const ImVec2 BoxMax(BoxMin.x + ValueBoxWidth, BoxMin.y + ValueBoxHeight);
                const float  Radius = ValueBoxHeight * 0.5f;

                char Printed[32] = {};
                FormatReading(Printed, sizeof(Printed), State.Reading);

                const bool  CarryUnit = (Parameter.Unit != nullptr && Parameter.Unit[0] != '\0');
                float       UnitWidth = 0.0f;
                if (CarryUnit)
                {
                    UnitWidth = ImGui::CalcTextSize(Parameter.Unit).x + ValuePadding * 2.0f;
                    if (UnitWidth < ValueUnitMinWide)
                    {
                        UnitWidth = ValueUnitMinWide;
                    }
                }

                Canvas->AddRectFilled(BoxMin, BoxMax, Palette.ValueBlack, Radius);
                if (CarryUnit)
                {
                    const ImVec2 UnitMin(BoxMax.x - UnitWidth, BoxMin.y);
                    Canvas->AddRectFilled(UnitMin, BoxMax, Palette.ValueUnitFill, Radius,
                                          ImDrawFlags_RoundCornersRight);
                    const ImVec2 UnitSize = ImGui::CalcTextSize(Parameter.Unit);
                    Canvas->AddText(ImVec2((UnitMin.x + BoxMax.x) * 0.5f - UnitSize.x * 0.5f,
                                           (BoxMin.y + BoxMax.y) * 0.5f - UnitSize.y * 0.5f),
                                    Palette.Muted, Parameter.Unit);
                }

                const ImVec2 PrintedSize = ImGui::CalcTextSize(Printed);
                Canvas->AddText(ImVec2(BoxMax.x - UnitWidth - ValuePadding - PrintedSize.x,
                                       (BoxMin.y + BoxMax.y) * 0.5f - PrintedSize.y * 0.5f),
                                Palette.Ink, Printed);

                // The track. Its hit area is the full row height rather than the 26px visual, so a drag that strays vertically off
                // the bar keeps its grab — a slider that drops the value the moment the pointer leaves its 26 pixels feels broken.
                const float  TrackLeft  = BoxMax.x + SliderGap;
                const float  TrackRight = Origin.x + BodyPadding + StripWidth;
                const float  TrackMidY  = (BoxMin.y + BoxMax.y) * 0.5f;
                const ImVec2 TrackMin(TrackLeft, TrackMidY - TrackHeight * 0.5f);
                const ImVec2 TrackMax(TrackRight, TrackMidY + TrackHeight * 0.5f);

                ImGui::SetCursorScreenPos(ImVec2(TrackLeft, BoxMin.y));
                ImGui::InvisibleButton("##track", ImVec2(TrackRight - TrackLeft, ValueBoxHeight));
                const bool Grabbed = ImGui::IsItemActive();

                const float Span  = Parameter.MaximumBoundary - Parameter.MinimumBoundary;
                const float Usable = (TrackMax.x - TrackMin.x) - KnobEdge;
                if (Grabbed && Usable > 0.0f && Span > 0.0f)
                {
                    const float PointerX  = ImGui::GetIO().MousePos.x - (TrackMin.x + KnobEdge * 0.5f);
                    float       Fraction  = PointerX / Usable;
                    Fraction              = (Fraction < 0.0f) ? 0.0f : ((Fraction > 1.0f) ? 1.0f : Fraction);
                    const float Candidate = Parameter.MinimumBoundary + Fraction * Span;
                    if (Candidate != State.Reading)
                    {
                        State.Reading = Candidate;
                        Changed       = true;
                    }
                }

                float Travelled = 0.0f;
                if (Span > 0.0f)
                {
                    Travelled = (State.Reading - Parameter.MinimumBoundary) / Span;
                    Travelled = (Travelled < 0.0f) ? 0.0f : ((Travelled > 1.0f) ? 1.0f : Travelled);
                }

                Canvas->AddRectFilled(TrackMin, TrackMax, SliderGround, TrackHeight * 0.5f);
                // 📝 The knob's centre travels between the two inset ends, so the fill is measured to that centre rather than to a
                //    naive fraction of the full width — otherwise the fill runs past the knob at the top of the range.
                const float KnobCentreX = TrackMin.x + KnobEdge * 0.5f + Travelled * Usable;
                if (KnobCentreX > TrackMin.x)
                {
                    Canvas->AddRectFilled(TrackMin, ImVec2(KnobCentreX, TrackMax.y),
                                          Palette.TrackTravelled, TrackHeight * 0.5f);
                }
                Canvas->AddRect(TrackMin, TrackMax, TrackOutline, TrackHeight * 0.5f, 0, TrackBorder);
                Canvas->AddCircleFilled(ImVec2(KnobCentreX, TrackMidY), KnobEdge * 0.5f, Palette.KnobFill);
                break;
            }

            //--------------------------------------------------------- SEGMENTED --------------------------------------------------------
            case ToolParameterCategory::Segmented:
            {
                const int OptionCount = TallyLabels(Parameter.OptionLabels, ToolSegmentedOptionLimit);
                const StripOutcome Strip = InscribePillStrip(Icons, Parameter.OptionLabels, nullptr, OptionCount,
                                                             State.ChosenOption, ImVec2(RowLeft, WidgetY),
                                                             StripWidth, SegmentHeight, SegmentPadding, false, Palette);
                if (Strip.ClickedIndex >= 0 && Strip.ClickedIndex != State.ChosenOption)
                {
                    State.ChosenOption = Strip.ClickedIndex;
                    Changed            = true;
                }
                break;
            }

            //---------------------------------------------------------- TOGGLE ----------------------------------------------------------
            case ToolParameterCategory::Toggle:
            {
                const float RowHeight = (SwitchHeight > LineHeight) ? SwitchHeight : LineHeight;
                const float MidY      = WidgetY + RowHeight * 0.5f;

                // The whole row is the hit target, not just the switch — a 42×24 switch is a small thing to ask a reader to hit when
                // the label beside it means the same.
                ImGui::SetCursorScreenPos(ImVec2(RowLeft, WidgetY));
                const bool Pressed = ImGui::InvisibleButton("##toggle", ImVec2(StripWidth, RowHeight));
                if (Pressed)
                {
                    State.Activation = !State.Activation;
                    Changed          = true;
                }

                InscribeToolGlyph(Icons, Parameter.GlyphName,
                                  ImVec2(RowLeft, MidY - LabelGlyphEdge * 0.5f), LabelGlyphEdge, true);
                Canvas->AddText(ImVec2(RowLeft + LabelGlyphEdge + LabelGlyphGap + 1.0f, MidY - LineHeight * 0.5f),
                                Palette.Ink, (Parameter.Label != nullptr) ? Parameter.Label : "");

                const ImVec2 TrackMin(RowLeft + StripWidth - SwitchWidth, MidY - SwitchHeight * 0.5f);
                const ImVec2 TrackMax(TrackMin.x + SwitchWidth, TrackMin.y + SwitchHeight);
                Canvas->AddRectFilled(TrackMin, TrackMax, State.Activation ? Palette.Accent : Palette.TrackFill,
                                      SwitchHeight * 0.5f);

                const float NubX = State.Activation
                                 ? (TrackMax.x - SwitchNubInset - SwitchNubEdge * 0.5f)
                                 : (TrackMin.x + SwitchNubInset + SwitchNubEdge * 0.5f);
                Canvas->AddCircleFilled(ImVec2(NubX, MidY), SwitchNubEdge * 0.5f, Palette.KnobFill);
                break;
            }

            //--------------------------------------------------------- SWATCHES ---------------------------------------------------------
            case ToolParameterCategory::Swatches:
            {
                const int TintCount = TallyLabels(Parameter.SwatchTints, ToolSwatchLimit);
                for (int TintIndex = 0; TintIndex < TintCount; ++TintIndex)
                {
                    const ImVec2 ChipMin(RowLeft + static_cast<float>(TintIndex) * (SwatchEdge + SwatchGap), WidgetY);
                    const ImVec2 ChipMax(ChipMin.x + SwatchEdge, ChipMin.y + SwatchEdge);

                    ImGui::SetCursorScreenPos(ChipMin);
                    ImGui::PushID(TintIndex);
                    const bool Pressed = ImGui::InvisibleButton("##swatch", ImVec2(SwatchEdge, SwatchEdge));
                    ImGui::PopID();
                    if (Pressed && TintIndex != State.ChosenOption)
                    {
                        State.ChosenOption = TintIndex;
                        Changed            = true;
                    }

                    // The chip is inset by its border so a selected and an unselected chip present the same colour area — insetting
                    // only the selected one makes the swatches appear to resize as the reader picks between them.
                    Canvas->AddRectFilled(ImVec2(ChipMin.x + SwatchBorder, ChipMin.y + SwatchBorder),
                                          ImVec2(ChipMax.x - SwatchBorder, ChipMax.y - SwatchBorder),
                                          ResolveSwatchTint(Parameter.SwatchTints[TintIndex]), SwatchRounding);
                    if (TintIndex == State.ChosenOption)
                    {
                        Canvas->AddRect(ChipMin, ChipMax, SwatchSelectedEdge, SwatchRounding, 0, SwatchBorder);
                    }
                }
                break;
            }

            //-------------------------------------------------------- AXISCHOICE --------------------------------------------------------
            case ToolParameterCategory::AxisChoice:
            {
                const int   OptionCount = TallyLabels(Parameter.OptionLabels, ToolSegmentedOptionLimit);
                const float NegateWidth = ImGui::CalcTextSize("-").x + NegatePadding * 2.0f;

                const StripOutcome Strip = InscribePillStrip(Icons, Parameter.OptionLabels, nullptr, OptionCount,
                                                             State.ChosenOption, ImVec2(RowLeft, WidgetY),
                                                             StripWidth - NegateWidth - SegmentGap,
                                                             SegmentHeight, SegmentPadding, false, Palette);
                if (Strip.ClickedIndex >= 0 && Strip.ClickedIndex != State.ChosenOption)
                {
                    State.ChosenOption = Strip.ClickedIndex;
                    Changed            = true;
                }

                // 🔴 The negate pill is VOID — dimmed and inert — unless a real axis is chosen. "Free" has no direction to negate, and
                //    a pill that toggles while meaning nothing would let the reader arm a state the tool then silently ignores.
                //    The prototype decides this by the option's own text, which is what makes "Free" the sentinel rather than index 0.
                const char* const Chosen = (State.ChosenOption >= 0 && State.ChosenOption < OptionCount)
                                         ? Parameter.OptionLabels[State.ChosenOption] : nullptr;
                const bool        Void   = (Chosen == nullptr) || (std::strcmp(Chosen, "Free") == 0);

                const ImVec2 PillMin(RowLeft + StripWidth - NegateWidth, WidgetY);
                const ImVec2 PillMax(PillMin.x + NegateWidth, PillMin.y + SegmentHeight);

                ImGui::SetCursorScreenPos(PillMin);
                const bool Pressed = ImGui::InvisibleButton("##negate", ImVec2(NegateWidth, SegmentHeight));
                const bool Hovered = ImGui::IsItemHovered();
                if (Pressed && !Void)
                {
                    State.Negated = !State.Negated;
                    Changed       = true;
                }
                if (Void && State.Negated)
                {
                    // Falling back to a free axis clears the negation rather than remembering it: a hidden armed flag that reasserts
                    // itself when the reader picks an axis again is a state nothing on screen accounts for.
                    State.Negated = false;
                    Changed       = true;
                }

                const bool  Armed      = State.Negated && !Void;
                const ImU32 PillGround = Armed ? Palette.Accent : Palette.TileFill;
                ImU32       PillInk    = Armed ? NegateSelectedInk : Palette.Faint;
                if (!Armed && Hovered && !Void)
                {
                    PillInk = Palette.Ink;
                }

                Canvas->AddRectFilled(PillMin, PillMax, Void ? Palette.GatedTileFill : PillGround, SegmentRounding);
                const ImVec2 NegateSize = ImGui::CalcTextSize("-");
                Canvas->AddText(ImVec2((PillMin.x + PillMax.x) * 0.5f - NegateSize.x * 0.5f,
                                       (PillMin.y + PillMax.y) * 0.5f - NegateSize.y * 0.5f),
                                Void ? Palette.Faint : PillInk, "-");
                break;
            }

            //------------------------------------------------------- SNAPTARGETS -------------------------------------------------------
            case ToolParameterCategory::SnapTargets:
            {
                const int TargetCount = TallyLabels(Parameter.SnapTargetLabels, ToolSnapTargetLimit);
                const StripOutcome Strip = InscribePillStrip(Icons, Parameter.SnapTargetLabels, State.TargetArmed,
                                                             TargetCount, -1, ImVec2(RowLeft, WidgetY),
                                                             StripWidth, ChipHeight, ChipPadding, true, Palette);
                if (Strip.ClickedIndex >= 0)
                {
                    State.TargetArmed[Strip.ClickedIndex] = !State.TargetArmed[Strip.ClickedIndex];
                    Changed                               = true;
                }
                break;
            }
        }

        ImGui::PopID();

        CursorY += MeasureRowHeight(Parameter, ColumnWidth);
        if (RowIndex + 1 < Bound)
        {
            CursorY += RowGap;
        }
    }

    return Changed;
}


void InscribeToolParameterVoidNote(const SvgIconRegistry* Icons,
                                   const char*            GlyphName,
                                   const char*            Label,
                                   ImVec2                 Origin,
                                   float                  ColumnWidth,
                                   float                  ColumnHeight,
                                   const ToolCardPalette& Palette)
{
    ImDrawList* const Canvas     = ImGui::GetWindowDrawList();
    const float       LineHeight = ImGui::GetTextLineHeight();
    const float       CentreX    = Origin.x + ColumnWidth * 0.5f;

    const char* const Caption = (Label != nullptr) ? Label : "This command";
    const char* const Note    = "runs immediately and takes no options";

    const float BlockHeight = VoidNoteGlyph + VoidNoteGap + LineHeight * 2.0f + VoidNoteGap;
    float       CursorY     = Origin.y + (ColumnHeight - BlockHeight) * 0.5f;

    InscribeToolGlyph(Icons, GlyphName, ImVec2(CentreX - VoidNoteGlyph * 0.5f, CursorY), VoidNoteGlyph, false);
    CursorY += VoidNoteGlyph + VoidNoteGap * 2.0f;

    const ImVec2 CaptionSize = ImGui::CalcTextSize(Caption);
    Canvas->AddText(ImVec2(CentreX - CaptionSize.x * 0.5f, CursorY), Palette.Muted, Caption);
    CursorY += LineHeight + VoidNoteGap * 0.5f;

    const ImVec2 NoteSize = ImGui::CalcTextSize(Note);
    Canvas->AddText(ImVec2(CentreX - NoteSize.x * 0.5f, CursorY), Palette.Faint, Note);
}

} // namespace Frontier
