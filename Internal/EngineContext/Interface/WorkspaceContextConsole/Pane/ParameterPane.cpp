/*==============================================================================================================================================
                                                            PARAMETERPANE.CPP
==============================================================================================================================================*/
// 🧩 Draws the parameter widgets. Each is an ImDrawList composition over invisible hit-test buttons rather than an ImGui control, for the same reason
//    the tiles are: the prototype's slider is a split value pill beside a bordered track with an oversized knob, and its segmented strip wraps —
//    neither is expressible through SliderFloat or a row of Buttons without fighting the style stack. Ported from ToolCard's ToolParameterColumn; the
//    block is keyed by (Cluster, Action) rather than (Band, Tile), and paint's Dropdown kind is added.

#include "ParameterPane.h"

#include "../Rasterization/GlyphRasterization.h"

#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 🔴 The row dimensions the two workspaces DISAGREE on have moved to MetricsSpecification — pill height/width, track height, knob, segment height
    //    and gap, the switch, the dropdown box, and the three paddings/gaps that space the rows. What stays constexpr here is what both agree on, or
    //    what no workspace has ever restated. See the metrics header: this file used to write `(void)Metrics;` and hardcode modelling's set, which
    //    silently drew paint's rows a third too tall.
    constexpr float LabelGlyphEdge   = 13.0f;   // [px] - .param-label .name svg
    constexpr float LabelGlyphGap    =  6.0f;   // [px] - .param-label .name gap

    constexpr float ValueUnitMinWide = 32.0f;   // [px] - .valuebox .unitseg min-width
    constexpr float ValuePadding     =  9.0f;   // [px] - .valuebox .num padding
    constexpr float SliderGap        =  9.0f;   // [px] - .slider-ctl gap
    constexpr float TrackBorder      =  1.5f;   // [px] - .slider border

    constexpr float SegmentPadding   = 12.0f;   // [px] - .seg-opt padding
    constexpr float SegmentRounding  =  9.0f;   // [px] - .seg-opt radius

    constexpr float SwitchNubInset   =  3.0f;   // [px]

    constexpr float DropdownPadding  = 11.0f;   // [px]
    constexpr float DropdownRounding =  9.0f;   // [px]

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

    // 📝 The Dropdown's option list and count, reached uniformly whether the row inlined a Segmented strip or borrowed a Dropdown pointer.
    int DropdownOptionCount(const ParameterDescriptor& Parameter)
    {
        if (Parameter.DropdownLabels != nullptr)
        {
            return (Parameter.DropdownCount < ParameterDropdownOptionLimit) ? Parameter.DropdownCount
                                                                            : ParameterDropdownOptionLimit;
        }
        return 0;
    }

    // 📝 Resolve a by-label authored default to an index against the row's own option list. The default is carried BY LABEL because paint's two nib
    //    sets order their options differently, so index 0 does not name the same option in both. Returns InitialOption when no label is set.
    int ResolveInitialOption(const ParameterDescriptor& Parameter)
    {
        if (Parameter.InitialOptionLabel == nullptr)
        {
            return Parameter.InitialOption;
        }

        const char* const* Labels = nullptr;
        int                Count   = 0;
        if (Parameter.Category == ParameterCategory::Dropdown)
        {
            Labels = Parameter.DropdownLabels;
            Count  = DropdownOptionCount(Parameter);
        }
        else
        {
            Labels = Parameter.SegmentedLabels;
            Count  = TallyLabels(Parameter.SegmentedLabels, ParameterSegmentedOptionLimit);
        }
        for (int OptionIndex = 0; OptionIndex < Count; ++OptionIndex)
        {
            if (Labels[OptionIndex] != nullptr && std::strcmp(Labels[OptionIndex], Parameter.InitialOptionLabel) == 0)
            {
                return OptionIndex;
            }
        }
        return Parameter.InitialOption;
    }

    // 📝 A "#rrggbb" swatch string as a draw colour. Parsed by hand rather than through sscanf: the six digits are a fixed shape, and the scanf
    //    family's %x wants an `unsigned int` whose width the format string cannot enforce. An unparsable string falls back to a neutral grey.
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
                const char Digit  = Text[1 + ChannelIndex * 2 + DigitIndex];
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

    // 📝 A reading printed the way the prototype's number input shows it: trailing zeros dropped, so 2.50 reads "2.5" and 2.00 reads "2".
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

    // 📝 The caption strip above a widget: glyph, name. Shared by every kind except Switch, whose caption sits inline with its track.
    void InscribeParameterLabel(const SvgIconRegistry*     Icons,
                                const ParameterDescriptor& Parameter,
                                ImVec2                     Origin,
                                float                      ColumnWidth,
                                const PaletteSpecification& Palette)
    {
        ImDrawList* const Canvas     = ImGui::GetWindowDrawList();
        const float       LineHeight = ImGui::GetTextLineHeight();
        const float       MidY       = Origin.y + LineHeight * 0.5f;

        InscribeConsoleGlyph(Icons, Parameter.GlyphName, ImVec2(Origin.x, MidY - LabelGlyphEdge * 0.5f), LabelGlyphEdge, true);
        Canvas->AddText(ImVec2(Origin.x + LabelGlyphEdge + LabelGlyphGap, Origin.y), Palette.Muted,
                        (Parameter.Label != nullptr) ? Parameter.Label : "");
        (void)ColumnWidth;
    }

    // 📝 One wrapping strip of pills, shared by Segmented, AxisChoice's axis strip and SnapTargets' chips. Returns the height it consumed and reports
    //    a click through ClickedIndex — wrapping is why this cannot be a fixed-height row.
    struct StripOutcome
    {
        float Height;
        int   ClickedIndex;
    };

    StripOutcome InscribePillStrip(const SvgIconRegistry*      Icons,
                                   const char* const*          Labels,
                                   const bool*                 Armed,          // null for exclusive strips
                                   int                         LabelCount,
                                   int                         SelectedIndex,  // -1 for multi-select strips
                                   ImVec2                      Origin,
                                   float                       StripWidth,
                                   float                       PillHeight,
                                   float                       PillPadding,
                                   float                       PillGap,
                                   bool                        CarryGlyph,
                                   const PaletteSpecification& Palette)
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
                CursorY        += PillHeight + PillGap;
                Outcome.Height += PillHeight + PillGap;
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
                    // A multi-select chip reads as armed through an accent WASH and border rather than a solid fill: several can be on at once, and a
                    // row of solid accent pills would drown the pane.
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
                InscribeConsoleGlyph(Icons, "ParamSnap",
                                     ImVec2(TextX, (PillMin.y + PillMax.y) * 0.5f - ChipGlyphEdge * 0.5f),
                                     ChipGlyphEdge, !Chosen);
                TextX += ChipGlyphEdge + ChipGlyphGap;
            }
            Canvas->AddText(ImVec2(TextX, (PillMin.y + PillMax.y) * 0.5f - TextSize.y * 0.5f), TextInk, Text);

            CursorX = PillMax.x + PillGap;
        }

        return Outcome;
    }

    // 📝 The height one row consumes, caption included. Kept beside the draw so the two cannot drift; the wrapping kinds are measured by walking the
    //    same advance the draw uses.
    float MeasureRowHeight(const ParameterDescriptor& Parameter, float ColumnWidth, const MetricsSpecification& Metrics)
    {
        const float LineHeight  = ImGui::GetTextLineHeight();
        const float StripWidth  = ColumnWidth - Metrics.ParameterPadding * 2.0f;
        const float CaptionBand = LineHeight + Metrics.ParameterLabelGap;

        switch (Parameter.Category)
        {
            case ParameterCategory::Slider:
                // 📝 The TALLER of the pill and its track, because the two are centred on one mid-line and paint's disagree — a 32 px pill beside a
                //    22 px track. Taking the pill alone was right only as long as it was the taller of the two by construction.
                return CaptionBand + ((Metrics.ValuePillHeight > Metrics.SliderTrackHeight)
                                      ? Metrics.ValuePillHeight : Metrics.SliderTrackHeight);

            case ParameterCategory::Dropdown:
                return CaptionBand + Metrics.DropdownHeight;

            case ParameterCategory::Switch:
                // No caption band: the name sits inline with the switch.
                return (Metrics.SwitchHeight > LineHeight) ? Metrics.SwitchHeight : LineHeight;

            case ParameterCategory::Swatches:
                return CaptionBand + SwatchEdge;

            case ParameterCategory::Segmented:
            case ParameterCategory::AxisChoice:
            case ParameterCategory::SnapTargets:
            {
                const bool         Chips   = (Parameter.Category == ParameterCategory::SnapTargets);
                const char* const* Labels  = Chips ? Parameter.SnapTargetLabels : Parameter.SegmentedLabels;
                const int          Ceiling = Chips ? ParameterSnapTargetLimit : ParameterSegmentedOptionLimit;
                const int          LabelCount = TallyLabels(Labels, Ceiling);
                const float        PillHeight = Chips ? ChipHeight : Metrics.SegmentHeight;
                const float        Padding    = Chips ? ChipPadding : SegmentPadding;

                // An axis strip shares its row with the negate pill, so it wraps within the room that pill leaves.
                float Available = StripWidth;
                if (Parameter.Category == ParameterCategory::AxisChoice)
                {
                    Available -= ImGui::CalcTextSize("-").x + NegatePadding * 2.0f + Metrics.SegmentGap;
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
                        Consumed += PillHeight + Metrics.SegmentGap;
                    }
                    CursorX += PillWidth + Metrics.SegmentGap;
                }
                return CaptionBand + Consumed;
            }
        }
        return CaptionBand + Metrics.SegmentHeight;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void BindParameterBlock(ParameterBlock&            Block,
                        const ParameterDescriptor* Parameters,
                        int                        ParameterCount,
                        int                        Cluster,
                        int                        Action)
{
    // 🔴 Reseed ONLY on an identity change. Called every frame, so reseeding unconditionally would reset the reader's slider the instant they let go
    //    of it — the drag would appear to snap back to the authored default on every single frame.
    //    📝 ...but the row COUNT changing is also an identity change, for a workspace whose rows are resolved per frame rather than authored in a
    //       constexpr table. Paint reveals rows as the reader edits (a Custom grade reveals hardness), which keeps (Cluster, Action) fixed while the
    //       table underneath grows — without this the revealed row would draw against whatever reading the slot last held. A reveal that swapped a row
    //       without changing the count would still slip through; no workspace authors one, and a Key check per row would cost a string compare per row
    //       per frame to catch a case that does not exist.
    const int Incoming = (Parameters != nullptr && ParameterCount > 0) ? ParameterCount : 0;
    const int Settled  = (Incoming < ConsoleParameterBlockLimit) ? Incoming : ConsoleParameterBlockLimit;
    if (Block.Cluster == Cluster && Block.Action == Action && Block.RowCount == Settled)
    {
        return;
    }

    Block.Cluster         = Cluster;
    Block.Action          = Action;
    Block.RowCount        = 0;
    // A list left open across an identity change would float over an action's rows that no longer exist — the same reason paint's own column dismisses
    // its select on an instrument swap.
    Block.OpenDropdownRow = -1;

    if (Parameters == nullptr || ParameterCount <= 0)
    {
        return;
    }

    const int Bound = (ParameterCount < ConsoleParameterBlockLimit) ? ParameterCount : ConsoleParameterBlockLimit;
    for (int RowIndex = 0; RowIndex < Bound; ++RowIndex)
    {
        const ParameterDescriptor& Parameter = Parameters[RowIndex];
        ParameterState&            State     = Block.Rows[RowIndex];

        State              = ParameterState{};
        State.Reading      = Parameter.InitialReading;
        State.ChosenOption = ResolveInitialOption(Parameter);
        State.Activation   = Parameter.InitialActivation;
        State.Negated      = false;
        for (int TargetIndex = 0; TargetIndex < ParameterSnapTargetLimit; ++TargetIndex)
        {
            State.TargetArmed[TargetIndex] = Parameter.SnapTargetArmed[TargetIndex];
        }
    }
    Block.RowCount = Bound;
}


float MeasureParameterPaneHeight(const ParameterDescriptor*  Parameters,
                                 int                         ParameterCount,
                                 const MetricsSpecification& Metrics)
{
    if (Parameters == nullptr || ParameterCount <= 0)
    {
        return Metrics.ParameterPadding * 2.0f;
    }

    float Consumed = Metrics.ParameterPadding * 2.0f;
    for (int RowIndex = 0; RowIndex < ParameterCount; ++RowIndex)
    {
        Consumed += MeasureRowHeight(Parameters[RowIndex], Metrics.RightColumnWidth, Metrics);
        if (RowIndex + 1 < ParameterCount)
        {
            Consumed += Metrics.ParameterRowGap;
        }
    }
    return Consumed;
}


bool InscribeParameterPane(const SvgIconRegistry*      Icons,
                           const ParameterDescriptor*  Parameters,
                           int                         ParameterCount,
                           ParameterBlock&             Block,
                           ImVec2                      Origin,
                           float                       ColumnWidth,
                           float                       ListCeilingY,
                           const PaletteSpecification& Palette,
                           const MetricsSpecification& Metrics)
{
    if (Parameters == nullptr || ParameterCount <= 0)
    {
        return false;
    }

    ImDrawList* const Canvas     = ImGui::GetWindowDrawList();
    const float       LineHeight = ImGui::GetTextLineHeight();
    const float       StripWidth = ColumnWidth - Metrics.ParameterPadding * 2.0f;

    bool        Changed = false;
    float       CursorY = Origin.y + Metrics.ParameterPadding;
    const float RowLeft = Origin.x + Metrics.ParameterPadding;

    const int Bound = (ParameterCount < Block.RowCount) ? ParameterCount : Block.RowCount;

    // 🔴 The open dropdown's list is drawn LAST, after every row, so it floats over the rows beneath instead of being overdrawn by them: an ImDrawList
    //    has no z within one list, so deferring the draw is the only way to get the stacking a native popup gets for free. Its geometry is captured
    //    during the walk and replayed below.
    int                DeferredListRow    = -1;
    ImVec2             DeferredListAnchor(0.0f, 0.0f);
    float              DeferredListWidth  = 0.0f;
    int                DeferredListCount  = 0;
    const char* const* DeferredListLabels = nullptr;
    int                DeferredListChosen = 0;

    for (int RowIndex = 0; RowIndex < Bound; ++RowIndex)
    {
        const ParameterDescriptor& Parameter = Parameters[RowIndex];
        ParameterState&            State     = Block.Rows[RowIndex];

        ImGui::PushID(RowIndex);

        // A Switch carries its own inline caption; every other kind takes the caption strip above its widget.
        float WidgetY = CursorY;
        if (Parameter.Category != ParameterCategory::Switch)
        {
            InscribeParameterLabel(Icons, Parameter, ImVec2(RowLeft, CursorY), ColumnWidth, Palette);
            WidgetY = CursorY + LineHeight + Metrics.ParameterLabelGap;
        }

        switch (Parameter.Category)
        {
            //---------------------------------------------------------- SLIDER ----------------------------------------------------------
            case ParameterCategory::Slider:
            {
                // The split value pill: a right-aligned number over black, then a unit segment. Drawn as two rects inside one pill so the rounded
                // ends belong to the pill rather than to either half.
                // 🔴 The pill and the track are CENTRED on the row's own mid-line rather than both anchored to its top, because they are different
                //    heights: paint's pill is 32 px against a 22 px track, and anchoring both to the top leaves the track riding 5 px high — which
                //    reads as a broken row rather than as a wrong number. Modelling's two are 38 and 26, so the same rule was already needed there
                //    and was only invisible because the track happened to be drawn about the pill's mid.
                const float  ControlHeight = (Metrics.ValuePillHeight > Metrics.SliderTrackHeight)
                                           ? Metrics.ValuePillHeight : Metrics.SliderTrackHeight;
                const float  ControlMidY   = WidgetY + ControlHeight * 0.5f;

                const ImVec2 BoxMin(RowLeft, ControlMidY - Metrics.ValuePillHeight * 0.5f);
                const ImVec2 BoxMax(BoxMin.x + Metrics.ValuePillWidth, ControlMidY + Metrics.ValuePillHeight * 0.5f);
                const float  Radius = Metrics.ValuePillHeight * 0.5f;

                char Printed[32] = {};
                FormatReading(Printed, sizeof(Printed), State.Reading);

                const bool CarryUnit = (Parameter.Unit != nullptr && Parameter.Unit[0] != '\0');
                float      UnitWidth = 0.0f;
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
                    Canvas->AddRectFilled(UnitMin, BoxMax, Palette.ValueUnitFill, Radius, ImDrawFlags_RoundCornersRight);
                    const ImVec2 UnitSize = ImGui::CalcTextSize(Parameter.Unit);
                    Canvas->AddText(ImVec2((UnitMin.x + BoxMax.x) * 0.5f - UnitSize.x * 0.5f,
                                           (BoxMin.y + BoxMax.y) * 0.5f - UnitSize.y * 0.5f),
                                    Palette.Muted, Parameter.Unit);
                }

                const ImVec2 PrintedSize = ImGui::CalcTextSize(Printed);
                Canvas->AddText(ImVec2(BoxMax.x - UnitWidth - ValuePadding - PrintedSize.x,
                                       (BoxMin.y + BoxMax.y) * 0.5f - PrintedSize.y * 0.5f),
                                Palette.Ink, Printed);

                // The track. Its hit area is the full row height rather than the 26px visual, so a drag that strays vertically off the bar keeps its
                // grab — a slider that drops the value the moment the pointer leaves its 26 pixels feels broken.
                const float  TrackLeft  = BoxMax.x + SliderGap;
                const float  TrackRight = Origin.x + Metrics.ParameterPadding + StripWidth;
                const float  TrackMidY  = ControlMidY;
                const ImVec2 TrackMin(TrackLeft, TrackMidY - Metrics.SliderTrackHeight * 0.5f);
                const ImVec2 TrackMax(TrackRight, TrackMidY + Metrics.SliderTrackHeight * 0.5f);

                ImGui::SetCursorScreenPos(ImVec2(TrackLeft, ControlMidY - ControlHeight * 0.5f));
                ImGui::InvisibleButton("##track", ImVec2(TrackRight - TrackLeft, ControlHeight));
                const bool Grabbed = ImGui::IsItemActive();

                const float Span   = Parameter.MaximumBoundary - Parameter.MinimumBoundary;
                const float Usable = (TrackMax.x - TrackMin.x) - Metrics.SliderKnobEdge;
                if (Grabbed && Usable > 0.0f && Span > 0.0f)
                {
                    const float PointerX  = ImGui::GetIO().MousePos.x - (TrackMin.x + Metrics.SliderKnobEdge * 0.5f);
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

                Canvas->AddRectFilled(TrackMin, TrackMax, SliderGround, Metrics.SliderTrackHeight * 0.5f);
                // 📝 The knob's centre travels between the two inset ends, so the fill is measured to that centre rather than to a naive fraction of
                //    the full width — otherwise the fill runs past the knob at the top of the range.
                const float KnobCentreX = TrackMin.x + Metrics.SliderKnobEdge * 0.5f + Travelled * Usable;
                if (KnobCentreX > TrackMin.x)
                {
                    Canvas->AddRectFilled(TrackMin, ImVec2(KnobCentreX, TrackMax.y), Palette.TrackTravelled,
                                          Metrics.SliderTrackHeight * 0.5f);
                }
                Canvas->AddRect(TrackMin, TrackMax, TrackOutline, Metrics.SliderTrackHeight * 0.5f, 0, TrackBorder);
                Canvas->AddCircleFilled(ImVec2(KnobCentreX, TrackMidY), Metrics.SliderKnobEdge * 0.5f, Palette.KnobFill);
                break;
            }

            //--------------------------------------------------------- SEGMENTED --------------------------------------------------------
            case ParameterCategory::Segmented:
            {
                const int OptionCount = TallyLabels(Parameter.SegmentedLabels, ParameterSegmentedOptionLimit);
                const StripOutcome Strip = InscribePillStrip(Icons, Parameter.SegmentedLabels, nullptr, OptionCount,
                                                             State.ChosenOption, ImVec2(RowLeft, WidgetY),
                                                             StripWidth, Metrics.SegmentHeight, SegmentPadding,
                                                             Metrics.SegmentGap, false, Palette);
                if (Strip.ClickedIndex >= 0 && Strip.ClickedIndex != State.ChosenOption)
                {
                    State.ChosenOption = Strip.ClickedIndex;
                    Changed            = true;
                }
                break;
            }

            //--------------------------------------------------------- DROPDOWN ---------------------------------------------------------
            case ParameterCategory::Dropdown:
            {
                // 📝 The collapsed box. Its LIST is drawn after the row walk, not here — see the deferred block below on why the two halves are split.
                const int OptionCount = DropdownOptionCount(Parameter);
                const bool ListOpen   = (Block.OpenDropdownRow == RowIndex);

                const ImVec2 BoxMin(RowLeft, WidgetY);
                const ImVec2 BoxMax(RowLeft + StripWidth, WidgetY + Metrics.DropdownHeight);

                ImGui::SetCursorScreenPos(BoxMin);
                const bool Pressed = ImGui::InvisibleButton("##dropdown", ImVec2(StripWidth, Metrics.DropdownHeight));
                const bool Hovered = ImGui::IsItemHovered();
                if (Pressed && OptionCount > 0)
                {
                    // A click on the box toggles its own list and closes any other's.
                    Block.OpenDropdownRow = ListOpen ? -1 : RowIndex;
                }
                else if (ListOpen && OptionCount > 0)
                {
                    DeferredListRow    = RowIndex;
                    DeferredListAnchor = ImVec2(BoxMin.x, BoxMax.y);
                    DeferredListWidth  = StripWidth;
                    DeferredListCount  = OptionCount;
                    DeferredListLabels = Parameter.DropdownLabels;
                    DeferredListChosen = State.ChosenOption;
                }

                Canvas->AddRectFilled(BoxMin, BoxMax, Hovered ? Palette.TileHoverFill : Palette.TileFill, DropdownRounding);
                Canvas->AddRect(BoxMin, BoxMax, ListOpen ? Palette.Accent : Palette.Hairline, DropdownRounding, 0, 1.0f);

                const char* Current = "";
                if (Parameter.DropdownLabels != nullptr && State.ChosenOption >= 0 && State.ChosenOption < OptionCount)
                {
                    Current = (Parameter.DropdownLabels[State.ChosenOption] != nullptr)
                            ? Parameter.DropdownLabels[State.ChosenOption] : "";
                }
                Canvas->AddText(ImVec2(BoxMin.x + DropdownPadding, (BoxMin.y + BoxMax.y) * 0.5f - LineHeight * 0.5f),
                                Palette.Ink, Current);
                // A downward caret at the right edge, marking the box as a list rather than a text field.
                const float CaretMidX = BoxMax.x - DropdownPadding - 4.0f;
                const float CaretMidY = (BoxMin.y + BoxMax.y) * 0.5f;
                Canvas->AddLine(ImVec2(CaretMidX - 4.0f, CaretMidY - 2.0f), ImVec2(CaretMidX, CaretMidY + 3.0f), Palette.Muted, 1.5f);
                Canvas->AddLine(ImVec2(CaretMidX + 4.0f, CaretMidY - 2.0f), ImVec2(CaretMidX, CaretMidY + 3.0f), Palette.Muted, 1.5f);
                break;
            }

            //---------------------------------------------------------- SWITCH ----------------------------------------------------------
            case ParameterCategory::Switch:
            {
                const float RowHeight = (Metrics.SwitchHeight > LineHeight) ? Metrics.SwitchHeight : LineHeight;
                const float MidY      = WidgetY + RowHeight * 0.5f;

                // The whole row is the hit target, not just the switch — a 42x24 switch is a small thing to ask a reader to hit when the label beside
                // it means the same.
                ImGui::SetCursorScreenPos(ImVec2(RowLeft, WidgetY));
                const bool Pressed = ImGui::InvisibleButton("##switch", ImVec2(StripWidth, RowHeight));
                if (Pressed)
                {
                    State.Activation = !State.Activation;
                    Changed          = true;
                }

                InscribeConsoleGlyph(Icons, Parameter.GlyphName, ImVec2(RowLeft, MidY - LabelGlyphEdge * 0.5f), LabelGlyphEdge, true);
                Canvas->AddText(ImVec2(RowLeft + LabelGlyphEdge + LabelGlyphGap + 1.0f, MidY - LineHeight * 0.5f),
                                Palette.Ink, (Parameter.Label != nullptr) ? Parameter.Label : "");

                const ImVec2 TrackMin(RowLeft + StripWidth - Metrics.SwitchWidth, MidY - Metrics.SwitchHeight * 0.5f);
                const ImVec2 TrackMax(TrackMin.x + Metrics.SwitchWidth, TrackMin.y + Metrics.SwitchHeight);
                Canvas->AddRectFilled(TrackMin, TrackMax, State.Activation ? Palette.Accent : Palette.TrackFill,
                                      Metrics.SwitchHeight * 0.5f);

                const float NubX = State.Activation
                                 ? (TrackMax.x - SwitchNubInset - Metrics.SwitchNubEdge * 0.5f)
                                 : (TrackMin.x + SwitchNubInset + Metrics.SwitchNubEdge * 0.5f);
                Canvas->AddCircleFilled(ImVec2(NubX, MidY), Metrics.SwitchNubEdge * 0.5f, Palette.KnobFill);
                break;
            }

            //--------------------------------------------------------- SWATCHES ---------------------------------------------------------
            case ParameterCategory::Swatches:
            {
                const int TintCount = TallyLabels(Parameter.SwatchTints, ParameterSwatchLimit);
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

                    // The chip is inset by its border so a selected and an unselected chip present the same colour area — insetting only the selected
                    // one makes the swatches appear to resize as the reader picks between them.
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
            case ParameterCategory::AxisChoice:
            {
                const int   OptionCount = TallyLabels(Parameter.SegmentedLabels, ParameterSegmentedOptionLimit);
                const float NegateWidth = ImGui::CalcTextSize("-").x + NegatePadding * 2.0f;

                const StripOutcome Strip = InscribePillStrip(Icons, Parameter.SegmentedLabels, nullptr, OptionCount,
                                                             State.ChosenOption, ImVec2(RowLeft, WidgetY),
                                                             StripWidth - NegateWidth - Metrics.SegmentGap,
                                                             Metrics.SegmentHeight, SegmentPadding,
                                                             Metrics.SegmentGap, false, Palette);
                if (Strip.ClickedIndex >= 0 && Strip.ClickedIndex != State.ChosenOption)
                {
                    State.ChosenOption = Strip.ClickedIndex;
                    Changed            = true;
                }

                // 🔴 The negate pill is VOID — dimmed and inert — unless a real axis is chosen. "Free" has no direction to negate, and a pill that
                //    toggles while meaning nothing would let the reader arm a state the tool then silently ignores. The prototype decides this by the
                //    option's own text, which is what makes "Free" the sentinel rather than index 0.
                const char* const Chosen = (State.ChosenOption >= 0 && State.ChosenOption < OptionCount)
                                         ? Parameter.SegmentedLabels[State.ChosenOption] : nullptr;
                const bool        Void   = (Chosen == nullptr) || (std::strcmp(Chosen, "Free") == 0);

                const ImVec2 PillMin(RowLeft + StripWidth - NegateWidth, WidgetY);
                const ImVec2 PillMax(PillMin.x + NegateWidth, PillMin.y + Metrics.SegmentHeight);

                ImGui::SetCursorScreenPos(PillMin);
                const bool Pressed = ImGui::InvisibleButton("##negate", ImVec2(NegateWidth, Metrics.SegmentHeight));
                const bool Hovered = ImGui::IsItemHovered();
                if (Pressed && !Void)
                {
                    State.Negated = !State.Negated;
                    Changed       = true;
                }
                if (Void && State.Negated)
                {
                    // Falling back to a free axis clears the negation rather than remembering it: a hidden armed flag that reasserts itself when the
                    // reader picks an axis again is a state nothing on screen accounts for.
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
            case ParameterCategory::SnapTargets:
            {
                const int TargetCount = TallyLabels(Parameter.SnapTargetLabels, ParameterSnapTargetLimit);
                const StripOutcome Strip = InscribePillStrip(Icons, Parameter.SnapTargetLabels, State.TargetArmed,
                                                             TargetCount, -1, ImVec2(RowLeft, WidgetY),
                                                             StripWidth, ChipHeight, ChipPadding,
                                                             Metrics.SegmentGap, true, Palette);
                if (Strip.ClickedIndex >= 0)
                {
                    State.TargetArmed[Strip.ClickedIndex] = !State.TargetArmed[Strip.ClickedIndex];
                    Changed                               = true;
                }
                break;
            }
        }

        ImGui::PopID();

        CursorY += MeasureRowHeight(Parameter, ColumnWidth, Metrics);
        if (RowIndex + 1 < Bound)
        {
            CursorY += Metrics.ParameterRowGap;
        }
    }

    //---------------------------------------------- THE DEFERRED DROPDOWN LIST ----------------------------------------------
    // 🔴 Drawn after every row so it floats over them, and hit-tested here for the same reason: an InvisibleButton placed during the walk would sit
    //    UNDER the rows below it in ImGui's own item order, so an entry overlapping a slider would hand the click to the slider.
    if (DeferredListRow >= 0 && DeferredListLabels != nullptr)
    {
        const float EntryHeight = Metrics.DropdownHeight;
        // 📝 Clamped to the column's own bottom rather than allowed to run past it. The longest list a workspace authors (paint's 21-entry graphite
        //    grade scale) is taller than the card, so the tail is unreachable — recorded rather than hidden, because a silently truncated scale reads
        //    as a shorter scale.
        //    🔴 The ceiling is an ABSOLUTE screen y, not Origin plus a height: Origin is scroll-shifted (the pane is drawn at HeaderBase minus the
        //       scroll offset), so a height added to it would slide the list's own floor up the card as the reader scrolls.
        const float ListTop    = DeferredListAnchor.y + 2.0f;
        const float Wanted     = ListTop + EntryHeight * static_cast<float>(DeferredListCount);
        const float Ceiling    = ListCeilingY - 2.0f;
        const float ListBottom = (Wanted < Ceiling) ? Wanted : Ceiling;

        const ImVec2 ListMin(DeferredListAnchor.x, ListTop);
        const ImVec2 ListMax(DeferredListAnchor.x + DeferredListWidth, ListBottom);

        Canvas->AddRectFilled(ListMin, ListMax, Palette.CardFill, DropdownRounding);
        Canvas->AddRect(ListMin, ListMax, Palette.HairlineStrong, DropdownRounding, 0, 1.0f);

        Canvas->PushClipRect(ListMin, ListMax, true);
        ImGui::PushID(DeferredListRow);
        for (int OptionIndex = 0; OptionIndex < DeferredListCount; ++OptionIndex)
        {
            const char* const Text = DeferredListLabels[OptionIndex];
            if (Text == nullptr) { continue; }

            const ImVec2 EntryMin(ListMin.x, ListTop + EntryHeight * static_cast<float>(OptionIndex));
            const ImVec2 EntryMax(ListMax.x, EntryMin.y + EntryHeight);
            if (EntryMin.y >= ListMax.y) { break; }

            ImGui::SetCursorScreenPos(EntryMin);
            ImGui::PushID(OptionIndex);
            const bool Pressed = ImGui::InvisibleButton("##entry", ImVec2(DeferredListWidth, EntryHeight));
            const bool Hovered = ImGui::IsItemHovered();
            ImGui::PopID();

            if (Pressed)
            {
                Block.Rows[DeferredListRow].ChosenOption = OptionIndex;
                Block.OpenDropdownRow                    = -1;
                Changed                                  = true;
            }

            if (Hovered) { Canvas->AddRectFilled(EntryMin, EntryMax, Palette.TileHoverFill); }
            Canvas->AddText(ImVec2(EntryMin.x + DropdownPadding, (EntryMin.y + EntryMax.y) * 0.5f - LineHeight * 0.5f),
                            (OptionIndex == DeferredListChosen) ? Palette.Accent : Palette.Ink, Text);
        }
        ImGui::PopID();
        Canvas->PopClipRect();
    }

    return Changed;
}


void InscribeParameterVoidNote(const SvgIconRegistry*      Icons,
                               const char*                 GlyphName,
                               const char*                 Label,
                               ImVec2                      Origin,
                               float                       ColumnWidth,
                               float                       ColumnHeight,
                               const PaletteSpecification& Palette)
{
    ImDrawList* const Canvas     = ImGui::GetWindowDrawList();
    const float       LineHeight = ImGui::GetTextLineHeight();
    const float       CentreX    = Origin.x + ColumnWidth * 0.5f;

    const char* const Caption = (Label != nullptr) ? Label : "This command";
    const char* const Note    = "runs immediately and takes no options";

    const float BlockHeight = VoidNoteGlyph + VoidNoteGap + LineHeight * 2.0f + VoidNoteGap;
    float       CursorY     = Origin.y + (ColumnHeight - BlockHeight) * 0.5f;

    InscribeConsoleGlyph(Icons, GlyphName, ImVec2(CentreX - VoidNoteGlyph * 0.5f, CursorY), VoidNoteGlyph, false);
    CursorY += VoidNoteGlyph + VoidNoteGap * 2.0f;

    const ImVec2 CaptionSize = ImGui::CalcTextSize(Caption);
    Canvas->AddText(ImVec2(CentreX - CaptionSize.x * 0.5f, CursorY), Palette.Muted, Caption);
    CursorY += LineHeight + VoidNoteGap * 0.5f;

    const ImVec2 NoteSize = ImGui::CalcTextSize(Note);
    Canvas->AddText(ImVec2(CentreX - NoteSize.x * 0.5f, CursorY), Palette.Faint, Note);
}

} // namespace Frontier
