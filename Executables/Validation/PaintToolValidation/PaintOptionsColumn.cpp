/*==============================================================================================================================================
                                                        PAINTOPTIONSCOLUMN.CPP
==============================================================================================================================================*/
// 🧩 Implementation of slide 2's right column. See PaintOptionsColumn.h for why an edit is REPORTED rather than applied, and why the slider
//    reports on every frame of a drag.

#include "PaintOptionsColumn.h"

#include "PaintIconPack.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{

namespace
{

// 🔴 Every slider row in the prototype carries `step:1` — 38 rows, 38 `step:` occurrences, so none omits it and none disagrees.
//    Named here rather than written as a bare 1.0f inside the arithmetic so that a future fractional row has one place to become a
//    descriptor field. See the header's note on why it is not a field today.
constexpr float PaintSliderStep = 1.0f;

// .valuebox{ flex:0 0 76px } — the split value pill is a FIXED width, so the track absorbs every change in pane width. Reversing
// that (a proportional pill) makes the readings jitter in width as the card scales, which is what the fixed basis prevents.
constexpr float PaintValueBoxWidth = 76.0f;
// .slider-ctl{ gap:8px } between the pill and the track, and `.slider{ min-width:56px }`.
constexpr float PaintSliderControlGap = 8.0f;
constexpr float PaintSliderMinimumWidth = 56.0f;
// .valuebox .unitseg{ padding:0 7px; min-width:26px }
constexpr float PaintUnitSegmentMinimumWidth = 26.0f;
constexpr float PaintUnitSegmentPaddingX     = 7.0f;
// .valuebox .num{ padding:0 8px } — the reading is right-aligned against this inset.
constexpr float PaintValueNumberPaddingX = 8.0f;
// .seg-opt{ padding:0 10px }
constexpr float PaintSegmentPaddingX = 10.0f;
// .group{ gap:9px } between a group's title and its first row, and between rows inside one group.
constexpr float PaintGroupInnerGap = 9.0f;
// .sel select{ padding:7px 30px 7px 13px } — the 30 on the right reserves the chevron.
constexpr float PaintSelectPaddingLeft   = 13.0f;
constexpr float PaintSelectPaddingRight  = 30.0f;
constexpr float PaintSelectPaddingY      =  7.0f;
constexpr float PaintSelectChevronEdge   =  6.0f;
constexpr float PaintSelectChevronInset  = 13.0f;
// .opt-foot{ gap:7px; padding:0 11px }, .obtn{ padding:0 13px }
constexpr float PaintFootGap      =  7.0f;
constexpr float PaintFootPaddingX = 11.0f;
constexpr float PaintButtonPaddingX = 13.0f;
// .toast{ padding:9px 17px; bottom:24px } and `.toast.on{ translateY(-58px) }`
constexpr float PaintToastPaddingX = 17.0f;
constexpr float PaintToastPaddingY =  9.0f;
constexpr float PaintToastRise     = 58.0f;
constexpr float PaintToastBottom   = 24.0f;
constexpr float PaintToastSeconds  =  1.5f;   // the prototype's 1500 ms setTimeout
// .toast{ transition:opacity .18s } — the fade at both ends of the dwell.
constexpr float PaintToastFadeSeconds = 0.18f;
// .param-label .name svg{ width:13px; height:13px } and its 6 px gap to the caption.
constexpr float PaintParameterGlyphEdge = 13.0f;
constexpr float PaintParameterGlyphGap  =  6.0f;
// .toggle-row .name{ gap:7px } — the switch row's glyph gap differs from the slider label's 6, which is why both exist.
constexpr float PaintToggleGlyphGap = 7.0f;
// .nub{ transition:left .15s } — how long the nub takes to cross, end to end.
constexpr float PaintSwitchTravelSeconds = 0.15f;


[[nodiscard]] bool IsPointerInside(ImVec2 Pointer, ImVec2 Minimum, ImVec2 Maximum)
{
    return Pointer.x >= Minimum.x && Pointer.x <= Maximum.x && Pointer.y >= Minimum.y && Pointer.y <= Maximum.y;
}


[[nodiscard]] ImU32 ScaleAlpha(ImU32 Colour, float Scale)
{
    const float Alpha = (float)((Colour >> IM_COL32_A_SHIFT) & 0xFFu) * Scale;
    return (Colour & ~IM_COL32_A_MASK) | ((ImU32)(Alpha + 0.5f) << IM_COL32_A_SHIFT);
}


[[nodiscard]] float Saturate(float Value)
{
    return (Value < 0.0f) ? 0.0f : ((Value > 1.0f) ? 1.0f : Value);
}


// Mix two packed colours channel-wise. 📝 Every channel including alpha, because a CSS colour transition interpolates the whole
// value — blending only RGB would hold a transparent end colour fully opaque for the length of the travel.
[[nodiscard]] ImU32 BlendColour(ImU32 From, ImU32 To, float Ratio)
{
    const float Mix = Saturate(Ratio);

    ImU32 Blended = 0;
    for (int Shift = 0; Shift < 32; Shift += 8)
    {
        const float Start = (float)((From >> Shift) & 0xFFu);
        const float End   = (float)((To   >> Shift) & 0xFFu);
        Blended |= ((ImU32)(Start + ((End - Start) * Mix) + 0.5f) & 0xFFu) << Shift;
    }
    return Blended;
}


// Draw a parameter row's leading glyph, or nothing when the registry has not got it.
// 📝 The glyph takes Faint, matching `.param-label .name svg{ color:var(--faint) }` — dimmer than the caption beside it, which is
//    deliberate in the prototype: the mark is an aid to scanning, not a second label competing with the text.
void DrawParameterGlyph(ImDrawList* DrawList, SvgIconRegistry* Registry, const char* GlyphName,
                        ImVec2 Centre, ImU32 Tint, float Edge)
{
    if (Registry == nullptr || GlyphName == nullptr) { return; }

    const ImTextureID Texture = ResolveIconTexture(*Registry, ResolvePaintGlyphKey(GlyphName, false));
    if (Texture == 0) { return; }

    const float Half = Edge * 0.5f;
    DrawList->AddImage(Texture, ImVec2(Centre.x - Half, Centre.y - Half), ImVec2(Centre.x + Half, Centre.y + Half),
                       ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), Tint);
}


// The height of one row, widget included but the group title excluded.
[[nodiscard]] float ResolveRowHeight(const PaintCardMetrics& Metrics, const PaintControlDescriptor& Control)
{
    const float LabelHeight = ImGui::GetTextLineHeight();

    switch (Control.Category)
    {
        case PaintControlCategory::Slider:
            // .param{ gap:6px } between the label and the slider-ctl, whose height is the TALLER of its two children.
            // 🔴 The pill is 32 px (`--row-h`) and the track is 22, so the row is 32 — measured, and the first version of this used a
            //    fudged multiple of the rail's row height that produced 22 and cropped every reading.
            return LabelHeight + Metrics.ControlGap + std::max(Metrics.ValueBoxHeight, Metrics.SliderHeight);

        case PaintControlCategory::Segmented:
            // 📝 One ROW of pills. `.segment{ flex-wrap:wrap }` can wrap to two, and this does not account for it — the measured
            //    peak is 3 options at ~11 px text inside a 337 px body, which cannot wrap. Recorded rather than silently assumed.
            return LabelHeight + Metrics.ControlGap + Metrics.SegmentHeight;

        case PaintControlCategory::Select:
            return LabelHeight + Metrics.ControlGap + (PaintSelectPaddingY * 2.0f) + ImGui::GetTextLineHeight();

        case PaintControlCategory::Switch:
            // .toggle-row is a single line — the label and the switch share it, so there is no ControlGap.
            return std::max(Metrics.SwitchHeight, LabelHeight);
    }
    return LabelHeight;
}


// 📝 Rounded to 4 decimals through a string-free route, mirroring `Number(V.toFixed(4))`. The prototype rounds because its step
//    arithmetic accumulates float error over a drag; the same is true here.
[[nodiscard]] float RoundToFourDecimals(float Value)
{
    return std::floor((Value * 10000.0f) + 0.5f) / 10000.0f;
}

} // namespace


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void AdvancePaintOptions(PaintOptionsState& State, const PaintControlValue* Values, int ValueCount, float DeltaSeconds)
{
    if (State.ToastSeconds > 0.0f)
    {
        State.ToastSeconds -= DeltaSeconds;
        if (State.ToastSeconds < 0.0f) { State.ToastSeconds = 0.0f; }
    }

    // ── the switch nubs, each chasing its own committed state ──
    if (Values == nullptr || ValueCount <= 0) { return; }

    const int Limit = std::min(ValueCount, PaintSchemaValueLimit);
    // 🔴 A RATE, not an accumulated timer: `transition:left .15s` restarts from wherever the nub currently is, so flipping a switch
    //    mid-travel must continue from the partial position rather than snapping to an end and re-running the full 150 ms. Stepping
    //    the phase toward the target gives exactly that, and it also means a switch that never moves costs one comparison.
    const float PhaseStep = (PaintSwitchTravelSeconds > 0.0f) ? (DeltaSeconds / PaintSwitchTravelSeconds) : 1.0f;

    for (int Index = 0; Index < Limit; ++Index)
    {
        const float Target  = Values[Index].Activated ? 1.0f : 0.0f;
        float&      Phase   = State.SwitchPhase[Index];

        if (Phase < Target)      { Phase = std::min(Target, Phase + PhaseStep); }
        else if (Phase > Target) { Phase = std::max(Target, Phase - PhaseStep); }
    }
}


void SettlePaintSwitches(PaintOptionsState& State, const PaintControlValue* Values, int ValueCount)
{
    const int Limit = std::min(ValueCount, PaintSchemaValueLimit);
    for (int Index = 0; Index < Limit; ++Index)
    {
        State.SwitchPhase[Index] = (Values != nullptr && Values[Index].Activated) ? 1.0f : 0.0f;
    }

    // 📝 The slots past the new value count are zeroed too, so a longer instrument's leftover travel cannot surface on a shorter
    //    one's row later — the array outlives any one schema.
    for (int Index = Limit; Index < PaintSchemaValueLimit; ++Index)
    {
        State.SwitchPhase[Index] = 0.0f;
    }
}


void FlashPaintToast(PaintOptionsState& State, const char* Message)
{
    if (Message == nullptr) { return; }

    std::snprintf(State.ToastMessage, sizeof(State.ToastMessage), "%s", Message);
    // 🔴 Assigned, not accumulated. The prototype clears the pending timeout before setting a new one, so a second flash inside the
    //    dwell shows its text for a FULL 1500 ms rather than inheriting the remainder — a toast that expired early would cut a
    //    message off mid-read.
    State.ToastSeconds = PaintToastSeconds;
}


float QuantisePaintReading(const PaintControlDescriptor& Control, float Value)
{
    // 🔴 Clamp FIRST. Snapping first can land outside a boundary that is not a whole number of steps from the floor, and the value
    //    would then stick there — see the header note.
    float Clamped = std::max(Control.MinimumBoundary, std::min(Control.MaximumBoundary, Value));

    if (PaintSliderStep > 0.0f)
    {
        Clamped = std::floor((Clamped / PaintSliderStep) + 0.5f) * PaintSliderStep;
        // 📝 Re-clamped after the snap. With a step of 1 and integer boundaries this cannot fire, but it is the guard that makes the
        //    clamp-then-snap order actually safe rather than only safe for today's schema.
        Clamped = std::max(Control.MinimumBoundary, std::min(Control.MaximumBoundary, Clamped));
    }

    return RoundToFourDecimals(Clamped);
}


float ResolvePaintOptionsContentHeight(const PaintCardMetrics& Metrics,
                                       const PaintVisibleControl* Visible, int VisibleCount)
{
    if (Visible == nullptr || VisibleCount <= 0) { return 0.0f; }

    float Height = Metrics.OptionsPaddingY * 2.0f;

    for (int Index = 0; Index < VisibleCount; ++Index)
    {
        const PaintVisibleControl& Row = Visible[Index];
        if (Row.Control == nullptr) { continue; }

        if (Row.StartsGroup)
        {
            // .options-body{ gap:14px } between whole groups, but not above the first.
            if (Index > 0) { Height += Metrics.OptionsRowGap; }
            // .group-title then .group{ gap:9px } beneath it.
            Height += ImGui::GetTextLineHeight() + PaintGroupInnerGap;
        }
        else
        {
            Height += PaintGroupInnerGap;
        }

        Height += ResolveRowHeight(Metrics, *Row.Control);
    }

    return Height;
}


PaintOptionsOutcome ConstructPaintOptionsColumn(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                                const PaintPaneRegion& Region, PaintOptionsState& State,
                                                SvgIconRegistry* Registry,
                                                const PaintVisibleControl* Visible, int VisibleCount,
                                                const PaintControlValue* Values, int ValueCount,
                                                float ScrollOffset)
{
    PaintOptionsOutcome Outcome;

    // 📝 Same guard as the rail: a pane translated off the card draws nothing AND tests nothing, so the hidden options column
    //    cannot swallow clicks meant for the grid sitting on top of it mid-carousel.
    if (!Region.IsVisible || Visible == nullptr || VisibleCount <= 0 || Values == nullptr) { return Outcome; }

    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    // .options-pane{ background:var(--menu-2) }
    DrawList->AddRectFilled(Region.BodyMinimum, Region.BodyMaximum, Palette.PaneFill);

    // 🔴 The body clip, which the pane owes the same way the grid and preview panes do. `overflow-y:auto` on the prototype's
    //    options div crops its scrolled content to the div; here the rows are drawn with the WINDOW draw list at an offset, so
    //    without this a scrolled row paints straight over the pane header above and the Reset/Select foot below — the header and
    //    foot are drawn by their own functions OUTSIDE this call and cannot overdraw what already landed on top of them.
    //    📝 Pushed after the background fill and popped before the deferred dropdown: the fill IS the pane, and the dropdown is a
    //       native popup in the prototype and so is deliberately allowed to escape the pane's bounds.
    DrawList->PushClipRect(Region.BodyMinimum, Region.BodyMaximum, true);

    const ImVec2 Pointer = ImGui::GetIO().MousePos;
    const bool   PointerInClip = IsPointerInside(Pointer, DrawList->GetClipRectMin(), DrawList->GetClipRectMax());
    const bool   Pressed  = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    // 🔴 Held, not clicked, for the slider. The prototype commits from `pointermove` under a pointer capture, so the knob keeps
    //    following the cursor after it leaves the 22 px track — which at this size it does constantly.
    const bool   Held     = ImGui::IsMouseDown(ImGuiMouseButton_Left);

    const float BodyLeft  = Region.BodyMinimum.x + Metrics.OptionsPaddingX;
    const float BodyRight = Region.BodyMaximum.x - Metrics.OptionsPaddingX;
    const float BodyWidth = BodyRight - BodyLeft;
    float       CursorY   = Region.BodyMinimum.y + Metrics.OptionsPaddingY - ScrollOffset;

    // 🔴 The open dropdown is drawn LAST, after every row, so its list floats over the rows beneath instead of being overdrawn by
    //    them. Its geometry is captured during the walk and replayed below — ImDrawList has no z within one list, so deferring the
    //    draw is the only way to get the stacking the prototype gets from `<select>` being a native popup.
    bool         DeferredMenuValid = false;
    ImVec2       DeferredMenuAnchor(0.0f, 0.0f);
    float        DeferredMenuWidth = 0.0f;
    const PaintControlDescriptor* DeferredMenuControl = nullptr;
    int          DeferredMenuValueIndex = -1;
    int          DeferredMenuChosen     = 0;

    for (int Index = 0; Index < VisibleCount; ++Index)
    {
        const PaintVisibleControl& Row = Visible[Index];
        if (Row.Control == nullptr) { continue; }
        if (Row.ValueIndex < 0 || Row.ValueIndex >= ValueCount) { continue; }

        const PaintControlDescriptor& Control = *Row.Control;
        const PaintControlValue&      Value   = Values[Row.ValueIndex];

        // ── the group title ──
        if (Row.StartsGroup)
        {
            if (Index > 0) { CursorY += Metrics.OptionsRowGap; }
            if (Row.GroupTitle != nullptr)
            {
                // .group-title{ font-size:9px; letter-spacing:1.2px; text-transform:uppercase; color:var(--faint) }
                // ⚠️ The UPPERCASING is applied here, but neither the 9 px size nor the 1.2 px letter-spacing is: ImGui draws from one
                //    font atlas at one size, and per-run tracking needs a glyph-by-glyph pass. So the title reads at body size and
                //    normal tracking — the most visible unported detail in this column, and cosmetic rather than structural.
                char Upper[64];
                std::snprintf(Upper, sizeof(Upper), "%s", Row.GroupTitle);
                for (char* Scan = Upper; *Scan != '\0'; ++Scan)
                {
                    if (*Scan >= 'a' && *Scan <= 'z') { *Scan = (char)(*Scan - 'a' + 'A'); }
                }
                DrawList->AddText(ImVec2(BodyLeft, CursorY), Palette.Faint, Upper);
            }
            CursorY += ImGui::GetTextLineHeight() + PaintGroupInnerGap;
        }
        else
        {
            CursorY += PaintGroupInnerGap;
        }

        const float RowHeight = ResolveRowHeight(Metrics, Control);

        // 📝 Rows scrolled fully out of the body are skipped for DRAWING but their cursor still advances, so the ones below stay put.
        //    The hit-test goes with the draw: an off-body row must not take a click.
        const bool RowOnScreen = (CursorY + RowHeight) >= Region.BodyMinimum.y && CursorY <= Region.BodyMaximum.y;
        if (!RowOnScreen) { CursorY += RowHeight; continue; }

        // ── the label line, shared by three of the four kinds ──
        const bool  IsSwitchRow = (Control.Category == PaintControlCategory::Switch);
        float       WidgetTop   = CursorY;

        if (!IsSwitchRow)
        {
            const float LabelMidY = CursorY + (ImGui::GetTextLineHeight() * 0.5f);
            DrawParameterGlyph(DrawList, Registry, Control.GlyphName,
                               ImVec2(BodyLeft + (PaintParameterGlyphEdge * 0.5f), LabelMidY),
                               Palette.Faint, PaintParameterGlyphEdge);

            const float CaptionLeft = BodyLeft + PaintParameterGlyphEdge + PaintParameterGlyphGap;
            if (Control.Label != nullptr)
            {
                DrawList->AddText(ImVec2(CaptionLeft, CursorY), Palette.Muted, Control.Label);
            }
            WidgetTop = CursorY + ImGui::GetTextLineHeight() + Metrics.ControlGap;
        }

        switch (Control.Category)
        {
            // ══════════════════════════════ SLIDER ══════════════════════════════
            case PaintControlCategory::Slider:
            {
                const float Span = Control.MaximumBoundary - Control.MinimumBoundary;
                // 📝 A zero span would divide by zero on the ratio. No schema row has one, but the fill is a visual that would go NaN
                //    and vanish rather than fail loudly, so it is guarded.
                const float Ratio = (Span > 0.0f)
                                  ? std::max(0.0f, std::min(1.0f, (Value.Reading - Control.MinimumBoundary) / Span))
                                  : 0.0f;

                // ── the split value pill: `.num` then `.unitseg` ──
                // 🔴 `.slider-ctl{ align-items:center }`, and its two children are DIFFERENT heights — the pill 32, the track 22. Both
                //    are centred on the control's own mid-line, so neither sits at WidgetTop. Anchoring both to the top instead leaves
                //    the track riding 5 px high against the pill, which reads as a misaligned row rather than as a wrong number.
                const float  ControlMidY = WidgetTop + (std::max(Metrics.ValueBoxHeight, Metrics.SliderHeight) * 0.5f);
                const ImVec2 PillMinimum(BodyLeft, ControlMidY - (Metrics.ValueBoxHeight * 0.5f));
                const ImVec2 PillMaximum(BodyLeft + PaintValueBoxWidth, ControlMidY + (Metrics.ValueBoxHeight * 0.5f));
                const float  PillRounding = (PillMaximum.y - PillMinimum.y) * 0.5f;   // border-radius:999px

                char Reading[24];
                // 📝 %g, so an integral reading prints "8" rather than "8.0000" — the prototype's `Field.value = Read()` prints a
                //    JS number, which has no trailing zeros.
                std::snprintf(Reading, sizeof(Reading), "%g", (double)Value.Reading);

                float UnitWidth = 0.0f;
                if (Control.Unit != nullptr && Control.Unit[0] != '\0')
                {
                    UnitWidth = std::max(PaintUnitSegmentMinimumWidth,
                                         ImGui::CalcTextSize(Control.Unit).x + (PaintUnitSegmentPaddingX * 2.0f));
                }

                const float NumberRight = PillMaximum.x - UnitWidth;

                // The whole pill is one rounded box with `overflow:hidden`; the two segments are its halves, so the number takes the
                // left corners and the unit the right.
                DrawList->AddRectFilled(PillMinimum, ImVec2(NumberRight, PillMaximum.y), Palette.ValueBlack, PillRounding,
                                        (UnitWidth > 0.0f) ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersAll);
                if (UnitWidth > 0.0f)
                {
                    DrawList->AddRectFilled(ImVec2(NumberRight, PillMinimum.y), PillMaximum, Palette.ValueUnitFill,
                                            PillRounding, ImDrawFlags_RoundCornersRight);
                    const ImVec2 UnitSize = ImGui::CalcTextSize(Control.Unit);
                    DrawList->AddText(ImVec2(NumberRight + ((UnitWidth - UnitSize.x) * 0.5f),
                                             ControlMidY - (UnitSize.y * 0.5f)),
                                      Palette.Muted, Control.Unit);
                }

                // text-align:right against the 8 px inset.
                const ImVec2 ReadingSize = ImGui::CalcTextSize(Reading);
                DrawList->AddText(ImVec2(NumberRight - PaintValueNumberPaddingX - ReadingSize.x,
                                         ControlMidY - (ReadingSize.y * 0.5f)),
                                  Palette.ValueInk, Reading);

                // ── the track ──
                const float  TrackLeft  = PillMaximum.x + PaintSliderControlGap;
                const float  TrackWidth = std::max(PaintSliderMinimumWidth, BodyRight - TrackLeft);
                const ImVec2 TrackMinimum(TrackLeft, ControlMidY - (Metrics.SliderHeight * 0.5f));
                const ImVec2 TrackMaximum(TrackLeft + TrackWidth, ControlMidY + (Metrics.SliderHeight * 0.5f));
                const float  TrackRounding = Metrics.SliderHeight * 0.5f;

                // .slider{ background:#000; border:1.5px solid var(--outline) }
                DrawList->AddRectFilled(TrackMinimum, TrackMaximum, IM_COL32_BLACK, TrackRounding);

                // 🔴 The fill is CLIPPED to the track rather than rounded to the ratio's own width. `.fill` is a square-cornered
                //    div inside an `overflow:hidden` pill, so at a low ratio its right edge is a straight cut, not a curve —
                //    rounding it would round an edge the prototype leaves flat and make small values read as a pill.
                //    🔴 The LEFT cap must still follow the pill, though, and a PushClipRect cannot give it: the clip is
                //       axis-aligned, so it squared off the track's rounded left end and the outline below then stroked the curve
                //       on top of the square — the fill reading as a box with a border laid over it. Rounded on the left only, so
                //       the flat right cut above survives and the left cap matches the pill exactly.
                //       📝 Once the fill is wider than the corner radius the flags are enough on their own; below that width
                //          ImGui shrinks the radius to fit, which is the same thing `overflow:hidden` does to a tiny fill.
                if (Ratio > 0.0f)
                {
                    DrawList->AddRectFilled(TrackMinimum, ImVec2(TrackLeft + (TrackWidth * Ratio), TrackMaximum.y),
                                            Palette.TrackTravelled, TrackRounding, ImDrawFlags_RoundCornersLeft);
                }
                DrawList->AddRect(TrackMinimum, TrackMaximum, Palette.SliderOutline, TrackRounding, 0,
                                  Metrics.SliderOutlineWide);

                const bool TrackHovered = PointerInClip && IsPointerInside(Pointer, TrackMinimum, TrackMaximum);

                // 🔴 The drag is latched on the ROW, not on "some slider is held": without the latch, pressing on one track and
                //    dragging over another would start editing the second parameter mid-gesture.
                if (TrackHovered && Pressed) { State.OpenSelectValueIndex = -1; }

                const bool ThisRowDragging = TrackHovered && Held;
                if (ThisRowDragging && TrackWidth > 0.0f)
                {
                    const float Raw = Control.MinimumBoundary + (((Pointer.x - TrackLeft) / TrackWidth) * Span);
                    Outcome.Request      = PaintOptionsRequest::SetReading;
                    Outcome.ValueIndex   = Row.ValueIndex;
                    Outcome.Reading      = QuantisePaintReading(Control, Raw);
                }

                // .knob{ width:17px; transform:translate(-50%,-50%) } — centred on the ratio, so it overhangs both ends by half its
                // width, exactly as the prototype's does. `:active` grows it 1.08 about its own centre.
                const float KnobScale  = ThisRowDragging ? 1.08f : 1.0f;
                const float KnobRadius = (Metrics.SliderKnobEdge * 0.5f) * KnobScale;
                const ImVec2 KnobCentre(TrackLeft + (TrackWidth * Ratio), (TrackMinimum.y + TrackMaximum.y) * 0.5f);
                DrawList->AddCircleFilled(KnobCentre, KnobRadius, Palette.KnobFill, 20);
                break;
            }

            // ═════════════════════════════ SEGMENTED ════════════════════════════
            case PaintControlCategory::Segmented:
            {
                float PillLeft = BodyLeft;
                for (int Option = 0; Option < Control.OptionCount && Option < PaintSegmentedOptionLimit; ++Option)
                {
                    const char* Label = (Control.OptionLabels != nullptr) ? Control.OptionLabels[Option] : nullptr;
                    if (Label == nullptr) { continue; }

                    const float PillWidth = ImGui::CalcTextSize(Label).x + (PaintSegmentPaddingX * 2.0f);
                    const ImVec2 PillMinimum(PillLeft, WidgetTop);
                    const ImVec2 PillMaximum(PillLeft + PillWidth, WidgetTop + Metrics.SegmentHeight);
                    PillLeft += PillWidth + Metrics.SegmentGap;

                    const bool IsChosen  = (Option == Value.ChosenOption);
                    const bool IsHovered = PointerInClip && IsPointerInside(Pointer, PillMinimum, PillMaximum);

                    if (IsHovered && Pressed && !IsChosen)
                    {
                        // 🔴 Reported, NOT applied — committing this can add or remove a row below (grade -> Custom hardness), and
                        //    this walk is already mid-frame. See the header.
                        Outcome.Request      = PaintOptionsRequest::ChooseOption;
                        Outcome.ValueIndex   = Row.ValueIndex;
                        Outcome.ChosenOption = Option;
                        State.OpenSelectValueIndex = -1;
                    }

                    // .seg-opt.sel{ background:var(--knob); color:#1b1b1e } — the chosen pill inverts, so its ink is KnobInk.
                    const ImU32 PillFill = IsChosen ? Palette.KnobFill : (IsHovered ? Palette.TileHoverFill : Palette.TileFill);
                    const ImU32 PillInk  = IsChosen ? Palette.KnobInk  : (IsHovered ? Palette.Ink : Palette.Muted);
                    DrawList->AddRectFilled(PillMinimum, PillMaximum, PillFill, Metrics.SegmentRounding);

                    const ImVec2 LabelSize = ImGui::CalcTextSize(Label);
                    DrawList->AddText(ImVec2(PillMinimum.x + PaintSegmentPaddingX,
                                             PillMinimum.y + ((Metrics.SegmentHeight - LabelSize.y) * 0.5f)),
                                      PillInk, Label);
                }
                break;
            }

            // ══════════════════════════════ SELECT ══════════════════════════════
            case PaintControlCategory::Select:
            {
                const float  FieldHeight = (PaintSelectPaddingY * 2.0f) + ImGui::GetTextLineHeight();
                const ImVec2 FieldMinimum(BodyLeft, WidgetTop);
                const ImVec2 FieldMaximum(BodyRight, WidgetTop + FieldHeight);
                const float  FieldRounding = FieldHeight * 0.5f;   // border-radius:999px

                const bool IsOpen    = (State.OpenSelectValueIndex == Row.ValueIndex);
                const bool IsHovered = PointerInClip && IsPointerInside(Pointer, FieldMinimum, FieldMaximum);

                // .sel select{ background:#000; border:1px solid var(--hair-strong) }, and `:focus{ border-color:var(--accent) }`
                // — an open menu is the focused state.
                DrawList->AddRectFilled(FieldMinimum, FieldMaximum, IM_COL32_BLACK, FieldRounding);
                DrawList->AddRect(FieldMinimum, FieldMaximum, IsOpen ? Palette.Accent : Palette.HairlineStrong,
                                  FieldRounding, 0, 1.0f);

                const char* Chosen = nullptr;
                if (Control.OptionLabels != nullptr && Value.ChosenOption >= 0 && Value.ChosenOption < Control.OptionCount)
                {
                    Chosen = Control.OptionLabels[Value.ChosenOption];
                }
                if (Chosen != nullptr)
                {
                    DrawList->PushClipRect(ImVec2(FieldMinimum.x + PaintSelectPaddingLeft, FieldMinimum.y),
                                           ImVec2(FieldMaximum.x - PaintSelectPaddingRight, FieldMaximum.y), true);
                    DrawList->AddText(ImVec2(FieldMinimum.x + PaintSelectPaddingLeft, FieldMinimum.y + PaintSelectPaddingY),
                                      Palette.Ink, Chosen);
                    DrawList->PopClipRect();
                }

                // .sel::after — a 6x6 box with only its right and bottom borders, rotated 45deg: a chevron pointing down.
                // 📝 Built as two lines rather than as a rotated square, because only two of the four borders are set and drawing
                //    the box would add the two the prototype leaves off.
                const float  ChevronMidX = FieldMaximum.x - PaintSelectChevronInset;
                // transform:translateY(-70%) — not -50%, so the mark sits slightly high in the field.
                const float  ChevronMidY = ((FieldMinimum.y + FieldMaximum.y) * 0.5f) - (PaintSelectChevronEdge * 0.20f);
                const float  ChevronArm  = PaintSelectChevronEdge * 0.7071f;   // the 45deg projection of a 6 px side
                DrawList->AddLine(ImVec2(ChevronMidX - ChevronArm, ChevronMidY - (ChevronArm * 0.5f)),
                                  ImVec2(ChevronMidX, ChevronMidY + (ChevronArm * 0.5f)), Palette.Muted, 1.5f);
                DrawList->AddLine(ImVec2(ChevronMidX, ChevronMidY + (ChevronArm * 0.5f)),
                                  ImVec2(ChevronMidX + ChevronArm, ChevronMidY - (ChevronArm * 0.5f)), Palette.Muted, 1.5f);

                if (IsHovered && Pressed)
                {
                    // A click on the field toggles its own menu and closes any other.
                    State.OpenSelectValueIndex = IsOpen ? -1 : Row.ValueIndex;
                }
                else if (IsOpen)
                {
                    DeferredMenuValid      = true;
                    DeferredMenuAnchor     = ImVec2(FieldMinimum.x, FieldMaximum.y);
                    DeferredMenuWidth      = FieldMaximum.x - FieldMinimum.x;
                    DeferredMenuControl    = &Control;
                    DeferredMenuValueIndex = Row.ValueIndex;
                    DeferredMenuChosen     = Value.ChosenOption;
                }
                break;
            }

            // ══════════════════════════════ SWITCH ══════════════════════════════
            case PaintControlCategory::Switch:
            {
                // .toggle-row — label left, switch right, both on one line.
                const float RowMidY = WidgetTop + (RowHeight * 0.5f);

                DrawParameterGlyph(DrawList, Registry, Control.GlyphName,
                                   ImVec2(BodyLeft + (PaintParameterGlyphEdge * 0.5f), RowMidY),
                                   Palette.Faint, PaintParameterGlyphEdge);

                if (Control.Label != nullptr)
                {
                    // .toggle-row .name{ color:var(--ink) } — BRIGHTER than a slider's label, which takes --muted. The switch row
                    //    has no separate reading to draw the eye, so its caption carries the emphasis instead.
                    const ImVec2 LabelSize = ImGui::CalcTextSize(Control.Label);
                    DrawList->AddText(ImVec2(BodyLeft + PaintParameterGlyphEdge + PaintToggleGlyphGap,
                                             RowMidY - (LabelSize.y * 0.5f)), Palette.Ink, Control.Label);
                }

                const ImVec2 TrackMinimum(BodyRight - Metrics.SwitchWidth, RowMidY - (Metrics.SwitchHeight * 0.5f));
                const ImVec2 TrackMaximum(BodyRight, RowMidY + (Metrics.SwitchHeight * 0.5f));
                const bool   IsHovered = PointerInClip && IsPointerInside(Pointer, TrackMinimum, TrackMaximum);

                if (IsHovered && Pressed)
                {
                    Outcome.Request    = PaintOptionsRequest::ToggleActivation;
                    Outcome.ValueIndex = Row.ValueIndex;
                    Outcome.Activated  = !Value.Activated;
                    State.OpenSelectValueIndex = -1;
                }

                // 📝 The travel this frame, eased. The phase itself is advanced in AdvancePaintOptions so a skipped draw still
                //    settles it; here it is only read, which keeps this walk free of the frame-order hazard the header describes.
                const float SwitchPhase = (Row.ValueIndex < PaintSchemaValueLimit)
                                        ? Saturate(State.SwitchPhase[Row.ValueIndex])
                                        : (Value.Activated ? 1.0f : 0.0f);
                // `transition` with no timing function is `ease`, whose midpoint is steeper than linear — smoothstep is the closest
                // one-line match, and a linear nub reads mechanical against the prototype's.
                const float SwitchEased = SwitchPhase * SwitchPhase * (3.0f - (2.0f * SwitchPhase));

                // .switch.on{ background:var(--accent) } — the background transitions WITH the nub, so it cross-fades rather than
                // snapping to accent while the nub is still mid-travel.
                DrawList->AddRectFilled(TrackMinimum, TrackMaximum,
                                        BlendColour(Palette.TrackFill, Palette.Accent, SwitchEased),
                                        Metrics.SwitchHeight * 0.5f);

                // .nub{ top:3px; left:3px } off, `left:19px` on — so the travel is 16 px, not (38 - 16 - 3).
                const float NubInset = 3.0f;
                const float NubLeft  = TrackMinimum.x + NubInset + (16.0f * SwitchEased);
                const ImVec2 NubCentre(NubLeft + (Metrics.SwitchNubEdge * 0.5f), RowMidY);
                DrawList->AddCircleFilled(NubCentre, Metrics.SwitchNubEdge * 0.5f, Palette.KnobFill, 16);
                break;
            }
        }

        CursorY += RowHeight;
    }

    // 🔴 The body clip ends with the row walk. Everything above scrolls and must be cropped to the pane; the dropdown below is the
    //    prototype's native `<select>` popup, which is not part of the scrolling flow and is already clamped to the pane's own
    //    bottom edge by its own geometry.
    DrawList->PopClipRect();

    // ── the deferred dropdown list, drawn over every row ──
    if (DeferredMenuValid && DeferredMenuControl != nullptr)
    {
        const PaintControlDescriptor& Control = *DeferredMenuControl;
        const float EntryHeight = (PaintSelectPaddingY * 2.0f) + ImGui::GetTextLineHeight();
        const int   EntryCount  = std::min(Control.OptionCount, PaintSelectOptionLimit);

        // 🔴 The list is clamped to the pane's own bottom, not allowed to run past it. The 21-entry graphite grade scale at ~26 px
        //    an entry is ~546 px of list inside a 420 px card, so the longest dropdown in the schema does NOT fit — it is the case
        //    that matters, not an edge one. Clamped and scroll-free here, which means the tail is unreachable; recorded rather
        //    than hidden, because a silently truncated grade scale reads as a shorter scale.
        const float ListTop    = DeferredMenuAnchor.y + 2.0f;
        const float ListBottom = std::min(ListTop + (EntryHeight * (float)EntryCount), Region.BodyMaximum.y - 2.0f);

        const ImVec2 ListMinimum(DeferredMenuAnchor.x, ListTop);
        const ImVec2 ListMaximum(DeferredMenuAnchor.x + DeferredMenuWidth, ListBottom);

        DrawList->AddRectFilled(ListMinimum, ListMaximum, Palette.CardFill, Metrics.ButtonRounding);
        DrawList->AddRect(ListMinimum, ListMaximum, Palette.HairlineStrong, Metrics.ButtonRounding, 0, 1.0f);

        DrawList->PushClipRect(ListMinimum, ListMaximum, true);
        for (int Option = 0; Option < EntryCount; ++Option)
        {
            const char* Label = (Control.OptionLabels != nullptr) ? Control.OptionLabels[Option] : nullptr;
            if (Label == nullptr) { continue; }

            const ImVec2 EntryMinimum(ListMinimum.x, ListTop + (EntryHeight * (float)Option));
            const ImVec2 EntryMaximum(ListMaximum.x, EntryMinimum.y + EntryHeight);
            if (EntryMinimum.y >= ListMaximum.y) { break; }

            const bool IsChosen  = (Option == DeferredMenuChosen);
            const bool IsHovered = PointerInClip && IsPointerInside(Pointer, EntryMinimum, EntryMaximum);

            if (IsHovered && Pressed)
            {
                Outcome.Request      = PaintOptionsRequest::ChooseOption;
                Outcome.ValueIndex   = DeferredMenuValueIndex;
                Outcome.ChosenOption = Option;
                State.OpenSelectValueIndex = -1;
            }

            if (IsHovered) { DrawList->AddRectFilled(EntryMinimum, EntryMaximum, Palette.TileHoverFill); }
            DrawList->AddText(ImVec2(EntryMinimum.x + PaintSelectPaddingLeft, EntryMinimum.y + PaintSelectPaddingY),
                              IsChosen ? Palette.Accent : Palette.Ink, Label);
        }
        DrawList->PopClipRect();
    }

    return Outcome;
}


PaintOptionsRequest ConstructPaintOptionsFoot(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                              int LiveParameterCount, int GroupCount,
                                              ImVec2 FootMinimum, float PaneWidth)
{
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    const ImVec2 FootMaximum(FootMinimum.x + PaneWidth, FootMinimum.y + Metrics.OptionsFootHeight);

    // .opt-foot{ border-top:1px solid var(--hair) }
    DrawList->AddLine(FootMinimum, ImVec2(FootMaximum.x, FootMinimum.y), Palette.Hairline, 1.0f);

    const ImVec2 Pointer = ImGui::GetIO().MousePos;
    const bool   PointerInClip = IsPointerInside(Pointer, DrawList->GetClipRectMin(), DrawList->GetClipRectMax());
    const bool   Pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const float  MidY    = (FootMinimum.y + FootMaximum.y) * 0.5f;

    PaintOptionsRequest Request = PaintOptionsRequest::None;

    // ── the two buttons, laid out from the RIGHT so the spend text takes what is left ──
    // 📝 Right-to-left because `.spend{ flex:1 }` and both buttons are `flex:0 0 auto`: the buttons claim their intrinsic widths and
    //    the text absorbs the remainder. Laying out left-to-right would need the text width first, which is the wrong dependency.
    struct FootButton { const char* Label; bool IsPrimary; PaintOptionsRequest Request; };
    const FootButton Buttons[] =
    {
        { "Select", true,  PaintOptionsRequest::ApplyInstrument },   // .obtn.go   — rightmost
        { "Reset",  false, PaintOptionsRequest::ResetToDefaults },   // .obtn.ghost
    };

    float ButtonRight = FootMaximum.x - PaintFootPaddingX;
    for (const FootButton& Button : Buttons)
    {
        const ImVec2 LabelSize  = ImGui::CalcTextSize(Button.Label);
        const float  ButtonWide = LabelSize.x + (PaintButtonPaddingX * 2.0f);
        const ImVec2 ButtonMinimum(ButtonRight - ButtonWide, MidY - (Metrics.ButtonHeight * 0.5f));
        const ImVec2 ButtonMaximum(ButtonRight, MidY + (Metrics.ButtonHeight * 0.5f));
        ButtonRight -= ButtonWide + PaintFootGap;

        const bool IsHovered = PointerInClip && IsPointerInside(Pointer, ButtonMinimum, ButtonMaximum);
        if (IsHovered && Pressed) { Request = Button.Request; }

        // .obtn.go{ background:var(--accent); color:#0b0b0d } with `:hover{ background:#6f9bff }`, a LIGHTER accent — so the primary
        // button brightens on hover while the ghost fills in. Two different hover languages, kept distinct.
        ImU32 ButtonFill = 0;
        ImU32 ButtonInk  = 0;
        if (Button.IsPrimary)
        {
            ButtonFill = IsHovered ? IM_COL32(0x6F, 0x9B, 0xFF, 0xFF) : Palette.Accent;
            ButtonInk  = IM_COL32(0x0B, 0x0B, 0x0D, 0xFF);
        }
        else
        {
            ButtonFill = IsHovered ? Palette.TileHoverFill : Palette.TileFill;
            ButtonInk  = IsHovered ? Palette.Ink : Palette.Muted;
        }

        DrawList->AddRectFilled(ButtonMinimum, ButtonMaximum, ButtonFill, Metrics.ButtonRounding);
        DrawList->AddText(ImVec2(ButtonMinimum.x + PaintButtonPaddingX, MidY - (LabelSize.y * 0.5f)), ButtonInk, Button.Label);
    }

    // ── the live tally ──
    // 🔴 Both numbers are LIVE and both come from the caller's collapsed list: the parameter count is the rows currently admitted by
    //    the `when:` predicates and the group count is AFTER empty groups are dropped. Hard-coding either would look right on the
    //    instruments that happen to match and stop tracking on the 39 that hide a row.
    // 📝 The prototype accents the two numbers and dims the words around them (`.spend b{ color:var(--accent) }`). Reproduced by
    //    drawing the run in pieces, since one AddText cannot change colour mid-string.
    char Number[16];
    float TextLeft = FootMinimum.x + PaintFootPaddingX;
    const float TextTop = MidY - (ImGui::GetTextLineHeight() * 0.5f);
    const float TextLimit = ButtonRight;

    std::snprintf(Number, sizeof(Number), "%d", LiveParameterCount);
    DrawList->PushClipRect(ImVec2(TextLeft, FootMinimum.y), ImVec2(std::max(TextLeft, TextLimit), FootMaximum.y), true);
    DrawList->AddText(ImVec2(TextLeft, TextTop), Palette.Accent, Number);
    TextLeft += ImGui::CalcTextSize(Number).x;
    DrawList->AddText(ImVec2(TextLeft, TextTop), Palette.Faint, " live parameters \xc2\xb7 ");
    TextLeft += ImGui::CalcTextSize(" live parameters \xc2\xb7 ").x;
    std::snprintf(Number, sizeof(Number), "%d", GroupCount);
    DrawList->AddText(ImVec2(TextLeft, TextTop), Palette.Accent, Number);
    TextLeft += ImGui::CalcTextSize(Number).x;
    DrawList->AddText(ImVec2(TextLeft, TextTop), Palette.Faint, " groups");
    DrawList->PopClipRect();

    return Request;
}


void ConstructPaintToast(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                         const PaintOptionsState& State, float CardCentreX, float CardBottomY)
{
    if (State.ToastSeconds <= 0.0f || State.ToastMessage[0] == '\0') { return; }

    // 🔴 The FOREGROUND list, not the window list. `.toast` is `position:fixed; z-index:120`, so it floats over the card rather than
    //    inside a pane — drawing it into the options column's list would clip it to that column and cut it in half.
    ImDrawList* DrawList = ImGui::GetForegroundDrawList();

    // 📝 The rise and the fade are one 0.18 s transition on both `opacity` and `transform`, so the toast slides UP as it appears and
    //    back down as it goes. Reproduced by driving both from the same phase rather than fading in place.
    const float Elapsed   = PaintToastSeconds - State.ToastSeconds;
    const float RiseIn    = std::min(1.0f, Elapsed / PaintToastFadeSeconds);
    const float FadeOut   = std::min(1.0f, State.ToastSeconds / PaintToastFadeSeconds);
    const float Presence  = std::min(RiseIn, FadeOut);

    const ImVec2 TextSize = ImGui::CalcTextSize(State.ToastMessage);
    const float  BoxWide  = TextSize.x + (PaintToastPaddingX * 2.0f);
    const float  BoxTall  = TextSize.y + (PaintToastPaddingY * 2.0f);

    // translateY goes from +12 to -58, measured from `bottom:24px`.
    const float RestingY  = CardBottomY - PaintToastBottom;
    const float OffsetY   = 12.0f + ((-PaintToastRise - 12.0f) * Presence);
    const float BoxTop    = RestingY + OffsetY - BoxTall;

    const ImVec2 BoxMinimum(CardCentreX - (BoxWide * 0.5f), BoxTop);
    const ImVec2 BoxMaximum(BoxMinimum.x + BoxWide, BoxTop + BoxTall);
    const float  BoxRounding = BoxTall * 0.5f;   // border-radius:999px

    DrawList->AddRectFilled(BoxMinimum, BoxMaximum, ScaleAlpha(Palette.CardFill, Presence), BoxRounding);
    DrawList->AddRect(BoxMinimum, BoxMaximum, ScaleAlpha(Palette.HairlineStrong, Presence), BoxRounding, 0, 1.0f);
    DrawList->AddText(ImVec2(BoxMinimum.x + PaintToastPaddingX, BoxMinimum.y + PaintToastPaddingY),
                      ScaleAlpha(Palette.Ink, Presence), State.ToastMessage);
}

} // namespace Frontier
