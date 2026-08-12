/*==============================================================================================================================================
                                                            PAINTPANELAYOUT.CPP
==============================================================================================================================================*/
// 🧩 The composite surfaces: pane head / foot, face tabs, the fold card, field and metadata rows, chips, slots, the compact log, and the deferred
//    menu overlay with its published anchors.
//
// 🔴 Anchors are published on the cycle a face DRAWS, and read on the next. That one-cycle lag is deliberate: a menu opened by a press cannot
//    know its own screen position until the face beneath it has been laid out, and the alternative — laying the pane out twice per cycle — would
//    double every hit-zone id and break ImGui's active-item tracking.

#include "PaintPaneLayout.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <cstring>
#include <cmath>

namespace PaintLayerSequenceValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL STATE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int   AnchorCapacity  = 64;      // [idx] - Distinct dropdown call sites
    constexpr float MenuUnfoldSpan  = 0.17f;   // [s]   - unfold keyframe
    constexpr float MenuLineHeight  = 26.0f;   // [px]  - .opt
    constexpr float MenuGroupHeight = 20.0f;   // [px]  - .menu-group
    constexpr float MenuPad         = 5.0f;    // [px]  - .menu padding
    constexpr float MenuGap         = 5.0f;    // [px]  - Face-to-menu gap
    constexpr float MenuSpanMax     = 210.0f;  // [px]  - .menu max-height
    constexpr float AddMenuSpanMax  = 288.0f;  // [px]  - .add-menu max-height
    constexpr float AddMenuWidth    = 214.0f;  // [px]  - .add-menu min-width
    constexpr float AddLineHeight   = 25.0f;   // [px]  - .afm
    constexpr float KindLineHeight  = 29.0f;   // [px]  - .kind

    struct MenuAnchor
    {
        char              Key[LayoutKeySpan];   // [-]  - Call-site key
        PaintMenuCategory Category;             // [-]  - Shape
        ImVec2            FaceTopLeft;          // [px] - Face position
        ImVec2            FaceSize;             // [px] - Face extent
        ImVec2            ClipTopLeft;          // [px] - Owning pane's clip
        ImVec2            ClipBottomRight;      // [px] - Owning pane's clip
        int               Current;              // [idx]- Selected value
    };

    MenuAnchor AnchorTable[AnchorCapacity] = {};
    int        AnchorTableCount            = 0;
    float      MenuAge                     = 0.0f;   // [s] - Age of the currently-open menu
    char       MenuAgeKey[LayoutKeySpan]   = {};

    void CopyKey(char* Target, const char* Source)
    {
        int Written = 0;
        if (Source != nullptr)
        {
            while (Written < LayoutKeySpan - 1 && Source[Written] != '\0') { Target[Written] = Source[Written]; ++Written; }
        }
        Target[Written] = '\0';
    }

    MenuAnchor* ResolveAnchor(const char* Key)
    {
        if (Key == nullptr || Key[0] == '\0') { return nullptr; }
        for (int Index = 0; Index < AnchorTableCount; ++Index)
        {
            if (strncmp(AnchorTable[Index].Key, Key, LayoutKeySpan - 1) == 0) { return &AnchorTable[Index]; }
        }
        return nullptr;
    }

    float ClampSpan(float Value, float Low, float High)
    {
        return Value < Low ? Low : (Value > High ? High : Value);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PANE CHROME
//------------------------------------------------------------------------------------------------------------------------

bool RecordPaneHead(ImDrawList* Draw, ImVec2 TopLeft, float Width, const PaneHeadEntry& Entry, const char* Id)
{
    const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + Span::HeadHeight);
    const bool   Stepping = (Id != nullptr) && (Entry.Forward || Entry.Backward);

    bool Hovered = false;
    bool Pressed = false;
    if (Stepping) { Pressed = RecordHitZone(Id, TopLeft, ImVec2(Width, Span::HeadHeight), &Hovered); }

    Draw->AddRectFilled(TopLeft, BottomRight, (Stepping && Hovered) ? Tone::HeadHover : Tone::Head);
    Draw->AddLine(ImVec2(TopLeft.x, BottomRight.y), BottomRight, Tone::Border);

    float Pen = TopLeft.x + 10.0f;
    const float Middle = TopLeft.y + Span::HeadHeight * 0.5f;

    if (Entry.Backward)
    {
        // 📝 A stepping head slides its chevron 2 px toward the travel on hover.
        const float Nudge = Hovered ? -2.0f : 0.0f;
        if (Hovered) { Draw->AddRectFilled(ImVec2(Pen, Middle - 12.0f), ImVec2(Pen + 24.0f, Middle + 12.0f), Tone::TileHigh, 7.0f); }
        RecordGlyph(Draw, ImVec2(Pen + 12.0f + Nudge, Middle), 13.0f, PaintGlyph::Previous, Hovered ? Tone::Ink : Tone::Muted);
        Pen += 24.0f + 9.0f;
    }
    else if (Entry.IconPresent)
    {
        const ImVec2 BoxTL(Pen, Middle - 13.0f);
        const ImVec2 BoxBR(Pen + 26.0f, Middle + 13.0f);
        Draw->AddRectFilled(BoxTL, BoxBR, Tone::Desk, 7.0f);
        Draw->AddRect(BoxTL, BoxBR, (Stepping && Hovered) ? Tone::HairStrong : Tone::Border, 7.0f);
        RecordGlyph(Draw, ImVec2(Pen + 13.0f, Middle), 15.0f, Entry.Icon, (Stepping && Hovered) ? Tone::Ink : Tone::Muted);
        Pen += 26.0f + 9.0f;
    }

    // -- Trailing columns are measured first so the title knows how much room it has -------------------------------------
    float Tail = BottomRight.x - 10.0f;
    if (Entry.Forward)
    {
        const float Nudge = Hovered ? 2.0f : 0.0f;
        if (Hovered) { Draw->AddRectFilled(ImVec2(Tail - 24.0f, Middle - 12.0f), ImVec2(Tail, Middle + 12.0f), Tone::TileHigh, 7.0f); }
        RecordGlyph(Draw, ImVec2(Tail - 12.0f + Nudge, Middle), 13.0f, PaintGlyph::Next, Hovered ? Tone::Ink : Tone::Muted);
        Tail -= 24.0f + 9.0f;
    }
    if (Entry.Chord != nullptr)
    {
        const float ChordWidth = MeasureInkTracked(9.0f, 0.4f, Entry.Chord) + 12.0f;
        Draw->AddRectFilled(ImVec2(Tail - ChordWidth, Middle - 8.0f), ImVec2(Tail, Middle + 8.0f), Tone::PanelDeep, 5.0f);
        Draw->AddRect(ImVec2(Tail - ChordWidth, Middle - 8.0f), ImVec2(Tail, Middle + 8.0f), Tone::Border, 5.0f);
        RecordInkTracked(Draw, ImVec2(Tail - ChordWidth + 6.0f, Middle - 5.0f), 9.0f, 0.4f, Tone::Faint, Entry.Chord);
        Tail -= ChordWidth + 9.0f;
    }
    if (Entry.Count != nullptr)
    {
        const float CountWidth = MeasureInk(9.5f, Entry.Count).x + 16.0f;
        Draw->AddRectFilled(ImVec2(Tail - CountWidth, Middle - 9.0f), ImVec2(Tail, Middle + 9.0f), Tone::PanelDeep, 9.0f);
        Draw->AddRect(ImVec2(Tail - CountWidth, Middle - 9.0f), ImVec2(Tail, Middle + 9.0f), Tone::Border, 9.0f);
        RecordInkCentred(Draw, ImVec2(Tail - CountWidth * 0.5f, Middle), 9.5f, Tone::Muted, Entry.Count);
        Tail -= CountWidth + 9.0f;
    }

    const float TitleRoom = Tail - Pen;
    if (Entry.Detail != nullptr && Entry.Detail[0] != '\0')
    {
        RecordInkClipped(Draw, ImVec2(Pen, TopLeft.y + 7.0f),  TitleRoom, 12.5f, Tone::Ink, Entry.Label);
        RecordInkClipped(Draw, ImVec2(Pen, TopLeft.y + 25.0f), TitleRoom,  9.5f, Tone::Faint, Entry.Detail);
    }
    else
    {
        RecordInkClipped(Draw, ImVec2(Pen, Middle - 7.0f), TitleRoom, 12.5f, Tone::Ink, Entry.Label);
    }
    return Pressed;
}

void RecordPaneFoot(ImDrawList* Draw,
                    ImVec2      TopLeft,
                    float       Width,
                    ImU32       Hue,
                    bool        HuePresent,
                    const char* Leading,
                    const char* Middle,
                    const char* Trailing)
{
    const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + Span::FootHeight);
    Draw->AddRectFilled(TopLeft, BottomRight, Tone::Head);
    Draw->AddLine(TopLeft, ImVec2(BottomRight.x, TopLeft.y), Tone::Border);

    const float Centre = TopLeft.y + Span::FootHeight * 0.5f;
    float       Pen    = TopLeft.x + 11.0f;

    if (HuePresent)
    {
        Draw->AddRectFilled(ImVec2(Pen, Centre - 3.5f), ImVec2(Pen + 7.0f, Centre + 3.5f), Hue, 2.0f);
        Pen += 7.0f + 7.0f;
    }
    if (Leading != nullptr)
    {
        RecordInk(Draw, ImVec2(Pen, Centre - 5.0f), 10.0f, Tone::Muted, Leading);
        Pen += MeasureInk(10.0f, Leading).x + 7.0f;
    }
    if (Middle != nullptr)
    {
        RecordInk(Draw, ImVec2(Pen, Centre - 5.0f), 10.0f, Tone::Ink, Middle);
    }
    if (Trailing != nullptr)
    {
        RecordInkRight(Draw, ImVec2(BottomRight.x - 11.0f, Centre - 5.0f), 10.0f, Tone::Muted, Trailing);
    }
}

int RecordFaceTabs(ImDrawList* Draw, ImVec2 TopLeft, float Width, InspectorFace Face, float Travel, const char* Id)
{
    const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + Span::FaceTabHeight);
    Draw->AddRectFilled(TopLeft, BottomRight, Tone::PanelDeep);
    Draw->AddLine(ImVec2(TopLeft.x, BottomRight.y), BottomRight, Tone::Border);

    static const char* Names[2] = { "Layer", "Mask" };
    const float        Half     = Width * 0.5f;
    int                Pressed  = -1;

    for (int Index = 0; Index < 2; ++Index)
    {
        const ImVec2 SegmentTL(TopLeft.x + Half * (float)Index, TopLeft.y);

        char SegmentId[LayoutKeySpan];
        snprintf(SegmentId, LayoutKeySpan, "%s#%d", Id, Index);

        bool Hovered = false;
        if (RecordHitZone(SegmentId, SegmentTL, ImVec2(Half, Span::FaceTabHeight), &Hovered)) { Pressed = Index; }

        const bool  On  = ((int)Face == Index);
        const ImU32 Ink = (On || Hovered) ? Tone::Ink : Tone::Muted;

        // 📝 The dot is currentColor at 42% opacity, scaling to 1.0 and 1.25x on the live segment.
        const float  Text     = MeasureInk(11.0f, Names[Index]).x;
        const float  Total    = 6.0f + 6.0f + Text;
        const float  Left     = SegmentTL.x + (Half - Total) * 0.5f;
        const float  Centre   = TopLeft.y + Span::FaceTabHeight * 0.5f;
        const float  DotEdge  = On ? 3.75f : 3.0f;
        Draw->AddCircleFilled(ImVec2(Left + 3.0f, Centre), DotEdge, On ? Ink : ApplyOpacity(Ink, 0.42f));
        RecordInk(Draw, ImVec2(Left + 12.0f, Centre - 5.5f), 11.0f, Ink, Names[Index]);
    }

    // 📝 The 2 px thumb rides the eased travel rather than the face, so it slides with the panes it labels.
    const float ThumbLeft = TopLeft.x + Half * Travel;
    Draw->AddRectFilled(ImVec2(ThumbLeft, BottomRight.y - 2.0f), ImVec2(ThumbLeft + Half, BottomRight.y), Tone::Accent, 2.0f);
    return Pressed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          FOLD CARD
//------------------------------------------------------------------------------------------------------------------------

void OpenPaintCard(PaintFlow&       Flow,
                   const char*      Key,
                   const char*      Title,
                   ImU32            DotHue,
                   bool             DotPresent,
                   const char*      Tag,
                   bool             TagLive,
                   bool             Folded,
                   PaintCardExtent& Extent,
                   PaintFlow&       Body)
{
    ImDrawList* Draw = Flow.Draw;
    const float DeltaTime = ImGui::GetIO().DeltaTime;

    CopyKey(Extent.Key, Key);
    Extent.Origin = ImVec2(Flow.Left, Flow.Pen);
    Extent.Width  = Flow.Width;
    Extent.Travel = ResolveFoldTravel(Key, !Folded, DeltaTime);

    const ImVec2 HeadTL = Extent.Origin;
    const ImVec2 HeadBR(HeadTL.x + Flow.Width, HeadTL.y + CardHeadHeight);

    char HeadId[LayoutKeySpan];
    snprintf(HeadId, LayoutKeySpan, "##%s-head", Key);
    bool Hovered = false;
    Extent.HeadPressed = RecordHitZone(HeadId, HeadTL, ImVec2(Flow.Width, CardHeadHeight), &Hovered);

    Draw->AddRectFilled(HeadTL, HeadBR, Hovered ? Tone::Tile : Tone::Head, Span::CardRound, ImDrawFlags_RoundCornersTop);
    // 🔴 A folded card's head loses its bottom rule (border-bottom-color:transparent) — draw it only while the body is showing.
    if (Extent.Travel > 0.01f) { Draw->AddLine(ImVec2(HeadTL.x, HeadBR.y), HeadBR, Tone::Border); }

    float Pen = HeadTL.x + 9.0f;
    const float Middle = HeadTL.y + CardHeadHeight * 0.5f;

    // 📝 The twist rotates -90 degrees folded, which is `Travel` running 0 -> 1 over a quarter turn.
    RecordChevron(Draw, ImVec2(Pen + 5.0f, Middle), 10.0f, Hovered ? Tone::Ink : Tone::Faint, -0.25f * (1.0f - Extent.Travel));
    Pen += 10.0f + 8.0f;

    if (DotPresent)
    {
        Draw->AddCircleFilled(ImVec2(Pen + 3.0f, Middle), 3.0f, DotHue);
        Pen += 6.0f + 8.0f;
    }

    float Tail = HeadBR.x - 9.0f;
    if (Tag != nullptr && Tag[0] != '\0')
    {
        const float TagWidth = MeasureInkTracked(9.0f, 0.5f, Tag) + 14.0f;
        Draw->AddRectFilled(ImVec2(Tail - TagWidth, Middle - 8.0f), ImVec2(Tail, Middle + 8.0f), Tone::PanelDeep, 8.0f);
        RecordInkTracked(Draw, ImVec2(Tail - TagWidth + 7.0f, Middle - 4.5f), 9.0f, 0.5f,
                         TagLive ? Tone::Marker : Tone::Faint, Tag);
        Tail -= TagWidth + 8.0f;
    }
    RecordInkClipped(Draw, ImVec2(Pen, Middle - 5.5f), Tail - Pen, 11.0f, Tone::Ink, Title);

    // -- Open the clipped body -------------------------------------------------------------------------------------------
    const float Remembered = ResolveCachedSpan(Key);
    Extent.VisibleSpan = Remembered * Extent.Travel;
    Extent.BodyTop     = HeadBR.y;

    Body       = Flow;
    Body.Left  = Flow.Left + 9.0f;
    Body.Width = Flow.Width - 18.0f;
    Body.Pen   = HeadBR.y + 8.0f;

    // 🔴 Clip to the ANIMATED span, never to the remembered one: the body must keep drawing (so its height stays current) while being
    //    revealed only as far as the fold has travelled.
    Draw->PushClipRect(ImVec2(Extent.Origin.x, HeadBR.y),
                       ImVec2(Extent.Origin.x + Flow.Width, HeadBR.y + Extent.VisibleSpan),
                       true);
}

void SealPaintCard(PaintFlow& Flow, PaintCardExtent& Extent, const PaintFlow& Body)
{
    Flow.Draw->PopClipRect();

    // 📝 The body's own consumed height, plus the 8 px lead-in and the 9 px tail of .card-body.
    const float Consumed = (Body.Pen - Extent.BodyTop) + 9.0f;
    RetainCachedSpan(Extent.Key, Consumed);

    const float Total = CardHeadHeight + Extent.VisibleSpan;
    const ImVec2 CardTL = Extent.Origin;
    const ImVec2 CardBR(CardTL.x + Extent.Width, CardTL.y + Total);
    Flow.Draw->AddRect(CardTL, CardBR, Tone::Border, Span::CardRound);

    Flow.Pen = CardBR.y + 6.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            ROWS
//------------------------------------------------------------------------------------------------------------------------

FieldPlacement RecordFieldRow(PaintFlow& Flow, const char* Label, float ControlHeight)
{
    const float Height = ControlHeight > FieldRowHeight ? ControlHeight : FieldRowHeight;
    RecordInkClipped(Flow.Draw,
                     ImVec2(Flow.Left, Flow.Pen + (Height - 11.0f) * 0.5f - 1.0f),
                     FieldLabelWidth, 11.0f, Tone::Muted, Label);

    FieldPlacement Placement;
    Placement.Origin = ImVec2(Flow.Left + FieldLabelWidth + FieldLabelGap, Flow.Pen + (Height - ControlHeight) * 0.5f);
    Placement.Width  = Flow.Width - FieldLabelWidth - FieldLabelGap;
    Flow.Pen        += Height + FieldRowGap;
    return Placement;
}

FieldPlacement RecordWideRow(PaintFlow& Flow, float ControlHeight)
{
    FieldPlacement Placement;
    Placement.Origin = ImVec2(Flow.Left, Flow.Pen);
    Placement.Width  = Flow.Width;
    Flow.Pen        += ControlHeight + FieldRowGap;
    return Placement;
}

void RecordMetaRow(PaintFlow& Flow, const char* Key, const char* Value, bool Dim, bool Hairline)
{
    const float Centre = Flow.Pen + MetaRowHeight * 0.5f;
    RecordInk(Flow.Draw, ImVec2(Flow.Left, Centre - 5.5f), 11.0f, Tone::Muted, Key);

    // 📝 The value column is capped at 62% of the row, exactly as .mrow .v is.
    const float Room = Flow.Width * 0.62f;
    if (MeasureInk(11.0f, Value).x <= Room)
    {
        RecordInkRight(Flow.Draw, ImVec2(Flow.Left + Flow.Width, Centre - 5.5f), 11.0f, Dim ? Tone::Faint : Tone::Ink, Value);
    }
    else
    {
        RecordInkClipped(Flow.Draw, ImVec2(Flow.Left + Flow.Width - Room, Centre - 5.5f), Room, 11.0f,
                         Dim ? Tone::Faint : Tone::Ink, Value);
    }

    Flow.Pen += MetaRowHeight;
    if (Hairline) { Flow.Draw->AddLine(ImVec2(Flow.Left, Flow.Pen), ImVec2(Flow.Left + Flow.Width, Flow.Pen), Tone::Hair); }
}

void RecordMetaChipRun(PaintFlow& Flow, const MetaChipEntry* Entries, int EntryCount)
{
    float Pen = Flow.Left;
    float Row = Flow.Pen;

    for (int Index = 0; Index < EntryCount; ++Index)
    {
        const MetaChipEntry& Entry = Entries[Index];
        float Width = 16.0f + MeasureInk(9.5f, Entry.Caption).x;
        if (Entry.DotPresent) { Width += 5.0f + 5.0f; }
        if (Entry.Strong != nullptr) { Width += 4.0f + MeasureInk(9.5f, Entry.Strong).x; }

        if (Pen > Flow.Left && Pen + Width > Flow.Left + Flow.Width)
        {
            Pen  = Flow.Left;
            Row += MetaChipHeight + 4.0f;
        }

        const ImVec2 ChipTL(Pen, Row);
        const ImVec2 ChipBR(Pen + Width, Row + MetaChipHeight);
        Flow.Draw->AddRectFilled(ChipTL, ChipBR, Tone::PanelDeep, MetaChipHeight * 0.5f);
        Flow.Draw->AddRect(ChipTL, ChipBR, Tone::Border, MetaChipHeight * 0.5f);

        float Ink = Pen + 8.0f;
        if (Entry.DotPresent)
        {
            Flow.Draw->AddCircleFilled(ImVec2(Ink + 2.5f, Row + MetaChipHeight * 0.5f), 2.5f, Entry.Dot);
            Ink += 5.0f + 5.0f;
        }
        RecordInk(Flow.Draw, ImVec2(Ink, Row + 5.0f), 9.5f, Tone::Muted, Entry.Caption);
        if (Entry.Strong != nullptr)
        {
            Ink += MeasureInk(9.5f, Entry.Caption).x + 4.0f;
            RecordInk(Flow.Draw, ImVec2(Ink, Row + 5.0f), 9.5f, Tone::Ink, Entry.Strong);
        }
        Pen += Width + 4.0f;
    }

    Flow.Pen = Row + MetaChipHeight + 7.0f;
}

void RecordSlotCaption(PaintFlow& Flow, const char* Label)
{
    RecordInkTracked(Flow.Draw, ImVec2(Flow.Left + 1.0f, Flow.Pen + 9.0f), 9.0f, 0.7f, Tone::Faint, Label);
    Flow.Pen += 9.0f + 10.0f;
}

void RecordAbsentNote(PaintFlow& Flow, const char* Text)
{
    RecordInk(Flow.Draw, ImVec2(Flow.Left + 4.0f, Flow.Pen + 4.0f), 10.0f, Tone::Faint, Text);
    Flow.Pen += 10.0f + 6.0f;
}

void RecordEmptyPane(ImDrawList* Draw, ImVec2 TopLeft, ImVec2 Size, const char* Leading, const char* Strong, const char* Trailing)
{
    const float  Total  = MeasureInk(11.0f, Leading).x
                        + (Strong   != nullptr ? MeasureInk(11.0f, Strong).x   : 0.0f)
                        + (Trailing != nullptr ? MeasureInk(11.0f, Trailing).x : 0.0f);
    const ImVec2 Centre(TopLeft.x + Size.x * 0.5f, TopLeft.y + 22.0f + 11.0f);
    float        Pen    = Centre.x - Total * 0.5f;

    RecordInk(Draw, ImVec2(Pen, Centre.y - 5.5f), 11.0f, Tone::Faint, Leading);
    Pen += MeasureInk(11.0f, Leading).x;
    if (Strong != nullptr)
    {
        RecordInk(Draw, ImVec2(Pen, Centre.y - 5.5f), 11.0f, Tone::Ink, Strong);
        Pen += MeasureInk(11.0f, Strong).x;
    }
    if (Trailing != nullptr) { RecordInk(Draw, ImVec2(Pen, Centre.y - 5.5f), 11.0f, Tone::Faint, Trailing); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            SLOTS
//------------------------------------------------------------------------------------------------------------------------

int RecordSlot(PaintFlow& Flow, const SlotEntry& Entry, const char* Id)
{
    const ImVec2 SlotTL(Flow.Left, Flow.Pen + 5.0f);
    const ImVec2 SlotBR(Flow.Left + Flow.Width, SlotTL.y + SlotHeight);

    char BodyId[LayoutKeySpan];
    snprintf(BodyId, LayoutKeySpan, "##%s-slot", Id);
    bool Hovered = false;
    RecordHitZone(BodyId, SlotTL, ImVec2(Flow.Width, SlotHeight), &Hovered);

    Flow.Draw->AddRectFilled(SlotTL, SlotBR, Tone::Tile, 9.0f);
    Flow.Draw->AddRect(SlotTL, SlotBR, Hovered ? Tone::HairStrong : Tone::Border, 9.0f);

    // -- 38 px tile: chequer beneath, flat tone or glyph over ------------------------------------------------------------
    const ImVec2 TileTL(SlotTL.x + 6.0f, SlotTL.y + 6.0f);
    const ImVec2 TileBR(TileTL.x + SlotThumbEdge, TileTL.y + SlotThumbEdge);
    if (Entry.ThumbGlyph)
    {
        RecordChequer(Flow.Draw, TileTL, TileBR, 8.0f, IM_COL32(10, 10, 10, 255), IM_COL32(25, 25, 25, 255), 6.0f);
        RecordGlyph(Flow.Draw, ImVec2((TileTL.x + TileBR.x) * 0.5f, (TileTL.y + TileBR.y) * 0.5f), 15.0f, Entry.Thumb, Tone::Faint);
    }
    else
    {
        Flow.Draw->AddRectFilled(TileTL, TileBR, Entry.ThumbTone, 6.0f);
    }
    Flow.Draw->AddRect(TileTL, TileBR, Tone::Border, 6.0f);

    // -- Trailing buttons ------------------------------------------------------------------------------------------------
    int   Fired = -1;
    float Tail  = SlotBR.x - 6.0f;
    for (int Index = Entry.ActionCount - 1; Index >= 0; --Index)
    {
        char ActionId[LayoutKeySpan];
        snprintf(ActionId, LayoutKeySpan, "##%s-act%d", Id, Index);
        const ImVec2 ButtonTL(Tail - 24.0f, SlotTL.y + (SlotHeight - 24.0f) * 0.5f);
        if (RecordIconButton(Flow.Draw, ButtonTL, Entry.Actions[Index], ActionId,
                             Entry.ActionDangerous[Index], Entry.ActionAvailable[Index]))
        {
            Fired = Index;
        }
        Tail -= 24.0f + 4.0f;
    }

    const float TextRoom = Tail - (TileBR.x + 9.0f);
    RecordInkClipped(Flow.Draw, ImVec2(TileBR.x + 9.0f, SlotTL.y + 12.0f), TextRoom, 11.0f, Tone::Ink,   Entry.Name);
    RecordInkClipped(Flow.Draw, ImVec2(TileBR.x + 9.0f, SlotTL.y + 27.0f), TextRoom,  9.5f, Tone::Faint, Entry.Detail);

    Flow.Pen = SlotBR.y;
    return Fired;
}

void RecordPreviewStrip(PaintFlow& Flow, ImU32 Tone_, bool TonePresent, const char* Caption, const char* Placement)
{
    Flow.Pen += 8.0f;
    Flow.Draw->AddLine(ImVec2(Flow.Left, Flow.Pen), ImVec2(Flow.Left + Flow.Width, Flow.Pen), Tone::Hair);
    Flow.Pen += 7.0f;

    const ImVec2 TileTL(Flow.Left, Flow.Pen);
    const ImVec2 TileBR(TileTL.x + 34.0f, TileTL.y + 34.0f);
    if (TonePresent) { Flow.Draw->AddRectFilled(TileTL, TileBR, Tone_, 7.0f); }
    else             { RecordChequer(Flow.Draw, TileTL, TileBR, 8.0f, IM_COL32(10, 10, 10, 255), IM_COL32(25, 25, 25, 255), 7.0f); }
    Flow.Draw->AddRect(TileTL, TileBR, Tone::Border, 7.0f);

    RecordInkClipped(Flow.Draw, ImVec2(TileBR.x + 9.0f, TileTL.y + 6.0f),  Flow.Width - 43.0f, 10.5f, Tone::Muted, Caption);
    RecordInkClipped(Flow.Draw, ImVec2(TileBR.x + 9.0f, TileTL.y + 20.0f), Flow.Width - 43.0f,  9.5f, Tone::Faint, Placement);

    Flow.Pen = TileBR.y;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         CHIP RUNS
//------------------------------------------------------------------------------------------------------------------------

ChannelChipOutcome RecordChannelChipRun(PaintFlow&             Flow,
                                        const PaintLayerEntry& Layer,
                                        bool                   AddPresent,
                                        bool                   AddOpen,
                                        const char*            Id)
{
    ChannelChipOutcome Outcome = { -1, false, ImVec2(0.0f, 0.0f), 25.0f };

    const ChannelDefinition* Table = ResolveChannelTable();
    float Pen = Flow.Left;
    float Row = Flow.Pen;

    for (int Index = 0; Index < ChannelCount; ++Index)
    {
        if (!Layer.Channels[Index].Enabled) continue;
        const ChannelDefinition& Definition = Table[Index];

        // 📝 A pinned chip has no cross and closes at 10 px; a dismissible one carries a 15 px button and closes at 5 px.
        const float Text  = MeasureInk(11.0f, Definition.Label).x;
        const float Width = 9.0f + 8.0f + 6.0f + Text + (Definition.Removable ? (6.0f + 15.0f + 5.0f) : 10.0f);

        if (Pen > Flow.Left && Pen + Width > Flow.Left + Flow.Width)
        {
            Pen  = Flow.Left;
            Row += ChipHeight + 5.0f;
        }

        const ImVec2 ChipTL(Pen, Row);
        const ImVec2 ChipBR(Pen + Width, Row + ChipHeight);

        char ChipId[LayoutKeySpan];
        snprintf(ChipId, LayoutKeySpan, "##%s-chip%d", Id, Index);
        bool ChipHovered = false;
        RecordHitZone(ChipId, ChipTL, ImVec2(Width, ChipHeight), &ChipHovered);

        Flow.Draw->AddRectFilled(ChipTL, ChipBR, ChipHovered ? Tone::TileHigh : Tone::Tile, ChipHeight * 0.5f);
        Flow.Draw->AddRect(ChipTL, ChipBR, ChipHovered ? Tone::HairStrong : Tone::Border, ChipHeight * 0.5f);

        const float Centre = Row + ChipHeight * 0.5f;
        Flow.Draw->AddCircleFilled(ImVec2(Pen + 9.0f + 4.0f, Centre), 4.0f, Definition.Hue);
        RecordInk(Flow.Draw, ImVec2(Pen + 9.0f + 8.0f + 6.0f, Centre - 5.5f), 11.0f, Tone::Ink, Definition.Label);

        if (Definition.Removable)
        {
            char CrossId[LayoutKeySpan];
            snprintf(CrossId, LayoutKeySpan, "##%s-cross%d", Id, Index);
            const ImVec2 CrossCentre(ChipBR.x - 5.0f - 7.5f, Centre);
            bool CrossHovered = false;
            if (RecordHitZone(CrossId, ImVec2(CrossCentre.x - 7.5f, CrossCentre.y - 7.5f), ImVec2(15.0f, 15.0f), &CrossHovered))
            {
                Outcome.Dismissed = Index;
            }
            Flow.Draw->AddCircleFilled(CrossCentre, CrossHovered ? 8.4f : 7.5f, CrossHovered ? Tone::Danger : Tone::TileOn);
            RecordGlyph(Flow.Draw, CrossCentre, 9.0f, PaintGlyph::Cross, CrossHovered ? Tone::Knob : Tone::Muted);
        }
        Pen += Width + 5.0f;
    }

    if (AddPresent)
    {
        constexpr float AddEdge = 25.0f;
        if (Pen > Flow.Left && Pen + AddEdge > Flow.Left + Flow.Width)
        {
            Pen  = Flow.Left;
            Row += ChipHeight + 5.0f;
        }
        const ImVec2 AddTL(Pen, Row + (ChipHeight - AddEdge) * 0.5f);

        char AddId[LayoutKeySpan];
        snprintf(AddId, LayoutKeySpan, "##%s-add", Id);
        bool AddHovered = false;
        Outcome.AddPressed = RecordHitZone(AddId, AddTL, ImVec2(AddEdge, AddEdge), &AddHovered);
        Outcome.AddOrigin  = AddTL;
        Outcome.AddEdge    = AddEdge;

        const ImVec2 AddCentre(AddTL.x + AddEdge * 0.5f, AddTL.y + AddEdge * 0.5f);
        if (AddHovered) { Flow.Draw->AddCircleFilled(AddCentre, AddEdge * 0.5f, IM_COL32(255, 255, 255, 13)); }
        // 📝 A dashed ring; the source rotates the glyph 90 degrees on hover.
        for (int Step = 0; Step < 16; Step += 2)
        {
            const float From = (float)Step / 16.0f * 6.2831853f;
            const float To   = (float)(Step + 1) / 16.0f * 6.2831853f;
            Flow.Draw->PathArcTo(AddCentre, AddEdge * 0.5f, From, To, 4);
            Flow.Draw->PathStroke(AddHovered ? Tone::Accent : Tone::Border, 0, 1.0f);
        }
        const float Turns = AddOpen ? 0.125f : (AddHovered ? 0.25f : 0.0f);
        const float Reach = 6.5f;
        const float Cos   = cosf(Turns * 6.2831853f);
        const float Sin   = sinf(Turns * 6.2831853f);
        const ImU32 Ink   = AddHovered ? Tone::Ink : Tone::Muted;
        Flow.Draw->AddLine(ImVec2(AddCentre.x - Reach * Cos, AddCentre.y - Reach * Sin),
                           ImVec2(AddCentre.x + Reach * Cos, AddCentre.y + Reach * Sin), Ink, 1.5f);
        Flow.Draw->AddLine(ImVec2(AddCentre.x + Reach * Sin, AddCentre.y - Reach * Cos),
                           ImVec2(AddCentre.x - Reach * Sin, AddCentre.y + Reach * Cos), Ink, 1.5f);
        Pen += AddEdge + 5.0f;
    }

    Flow.Pen = Row + ChipHeight + 2.0f;
    return Outcome;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        COMPACT LOG
//------------------------------------------------------------------------------------------------------------------------

int RecordCompactLog(PaintFlow&             Flow,
                     const PaintLayerEntry& Layer,
                     HistoryScope           Scope,
                     int                    ScopeChannel,
                     int                    Limit,
                     const char*            Id)
{
    // 📝 The source takes the LAST `Limit` matching revisions and reverses them, so the newest sits at the top.
    int Matching[HistoryCapacity];
    int MatchCount = 0;
    for (int Index = 0; Index < Layer.HistoryCount; ++Index)
    {
        const HistoryEntry& Entry = Layer.History[Index];
        if (Entry.Scope != Scope) continue;
        if (Scope == HistoryScope::ChannelSlot && Entry.ScopeChannel != ScopeChannel) continue;
        Matching[MatchCount++] = Index;
    }

    if (MatchCount == 0)
    {
        RecordAbsentNote(Flow, "No recorded changes for this slot.");
        return -1;
    }

    const HistoryCategoryDefinition* Categories = ResolveHistoryCategoryTable();
    const int First = (MatchCount > Limit) ? (MatchCount - Limit) : 0;
    int       Fired = -1;

    Flow.Pen += 2.0f;
    for (int Walk = MatchCount - 1; Walk >= First; --Walk)
    {
        const int           Ordinal = Matching[Walk];
        const HistoryEntry& Entry   = Layer.History[Ordinal];

        char RowId[LayoutKeySpan];
        snprintf(RowId, LayoutKeySpan, "##%s-log%d", Id, Ordinal);
        bool Hovered = false;
        if (RecordHitZone(RowId, ImVec2(Flow.Left, Flow.Pen), ImVec2(Flow.Width, LogRowHeight), &Hovered)) { Fired = Ordinal; }

        if (Hovered)
        {
            Flow.Draw->AddRectFilled(ImVec2(Flow.Left, Flow.Pen), ImVec2(Flow.Left + Flow.Width, Flow.Pen + LogRowHeight),
                                     Tone::RowHover, 5.0f);
        }

        const float Centre = Flow.Pen + LogRowHeight * 0.5f;
        Flow.Draw->AddCircleFilled(ImVec2(Flow.Left + 4.0f + 2.5f, Centre), 2.5f, Categories[(int)Entry.Category].Hue);

        char Stamp[8];
        snprintf(Stamp, sizeof(Stamp), "%02d:%02d", Entry.Hour, Entry.Minute);
        const float StampWidth = MeasureInk(9.5f, Stamp).x;

        RecordInkClipped(Flow.Draw, ImVec2(Flow.Left + 16.0f, Centre - 5.0f),
                         Flow.Width - 16.0f - StampWidth - 11.0f, 10.5f, Hovered ? Tone::Ink : Tone::Muted, Entry.Title);
        RecordInkRight(Flow.Draw, ImVec2(Flow.Left + Flow.Width - 4.0f, Centre - 5.0f), 9.5f, Tone::Faint, Stamp);

        Flow.Pen += LogRowHeight + 1.0f;
    }
    return Fired;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       MENU OVERLAY
//------------------------------------------------------------------------------------------------------------------------

void PublishMenuAnchor(const char*       Key,
                       PaintMenuCategory Category,
                       ImVec2            FaceTopLeft,
                       ImVec2            FaceSize,
                       ImVec2            ClipTopLeft,
                       ImVec2            ClipBottomRight,
                       int               Current)
{
    MenuAnchor* Anchor = ResolveAnchor(Key);
    if (Anchor == nullptr)
    {
        if (AnchorTableCount >= AnchorCapacity) { return; }
        Anchor = &AnchorTable[AnchorTableCount++];
        CopyKey(Anchor->Key, Key);
    }
    Anchor->Category        = Category;
    Anchor->FaceTopLeft     = FaceTopLeft;
    Anchor->FaceSize        = FaceSize;
    Anchor->ClipTopLeft     = ClipTopLeft;
    Anchor->ClipBottomRight = ClipBottomRight;
    Anchor->Current         = Current;
}

namespace
{
    void AppendLine(PaintMenuPlan& Plan,
                    const char*    Label,
                    const char*    Note,
                    int            Value,
                    ImU32          Swatch,
                    bool           SwatchPresent,
                    bool           Selected,
                    bool           Available,
                    bool           Heading,
                    float          Height)
    {
        if (Plan.LineCount >= (int)(sizeof(Plan.Lines) / sizeof(Plan.Lines[0]))) return;
        PaintMenuLine& Line = Plan.Lines[Plan.LineCount++];
        Line.Origin        = ImVec2(0.0f, Height);   // 📝 Height rides in y until the pass below stacks the rows.
        Line.Size          = ImVec2(0.0f, Height);
        Line.Label         = Label;
        Line.Note          = Note;
        Line.Value         = Value;
        Line.Swatch        = Swatch;
        Line.SwatchPresent = SwatchPresent;
        Line.Selected      = Selected;
        Line.Available     = Available;
        Line.Heading       = Heading;
    }
}

PaintMenuPlan AssemblePaintMenu(const char* OpenKey, const PaintLayerEntry* Layer, float DeltaTime)
{
    PaintMenuPlan Plan = {};
    Plan.Live = false;

    if (OpenKey == nullptr || OpenKey[0] == '\0')
    {
        MenuAge       = 0.0f;
        MenuAgeKey[0] = '\0';
        return Plan;
    }

    const MenuAnchor* Anchor = ResolveAnchor(OpenKey);
    if (Anchor == nullptr) { return Plan; }   // 📝 Opened this cycle; its face has not published a position yet.

    if (strncmp(MenuAgeKey, OpenKey, LayoutKeySpan - 1) != 0)
    {
        CopyKey(MenuAgeKey, OpenKey);
        MenuAge = 0.0f;
    }
    MenuAge = MenuAge + DeltaTime < MenuUnfoldSpan ? MenuAge + DeltaTime : MenuUnfoldSpan;

    Plan.Live     = true;
    Plan.Category = Anchor->Category;
    Plan.Age      = SolvePopCurve(MenuAge / MenuUnfoldSpan);
    CopyKey(Plan.Key, OpenKey);

    // -- Rows ------------------------------------------------------------------------------------------------------------
    switch (Anchor->Category)
    {
        case PaintMenuCategory::Options:
        {
            for (int Index = 0; Index < BlendCount; ++Index)
            {
                AppendLine(Plan, ResolveBlendName(Index), nullptr, Index, 0, false,
                           Index == Anchor->Current, true, false, MenuLineHeight);
            }
            break;
        }
        case PaintMenuCategory::Generators:
        {
            const GeneratorDefinition* Table = ResolveGeneratorTable();
            for (int Group = 0; Group < GeneratorGroupCount; ++Group)
            {
                const char* Heading = ResolveGeneratorSectionName(Group);
                AppendLine(Plan, Heading, nullptr, -1, 0, false, false, false, true, MenuGroupHeight);
                for (int Index = 0; Index < GeneratorCount; ++Index)
                {
                    if (strcmp(Table[Index].Section, Heading) != 0) continue;
                    AppendLine(Plan, Table[Index].Label, Table[Index].Note, Index, 0, false,
                               Index == Anchor->Current, true, false, MenuLineHeight);
                }
            }
            break;
        }
        case PaintMenuCategory::LayerKinds:
        {
            const LayerCategoryDefinition* Table = ResolveLayerCategoryTable();
            for (int Index = 0; Index < LayerCategoryCount; ++Index)
            {
                AppendLine(Plan, Table[Index].Label, Table[Index].Summary, Index, Table[Index].Tint, true,
                           false, true, false, KindLineHeight);
            }
            break;
        }
        case PaintMenuCategory::ChannelSet:
        {
            const ChannelDefinition* Table = ResolveChannelTable();
            for (int Group = 0; Group < ChannelGroupCount; ++Group)
            {
                AppendLine(Plan, ResolveChannelSectionName((ChannelSection)Group), nullptr, -1, 0, false,
                           false, false, true, MenuGroupHeight);
                for (int Index = 0; Index < ChannelCount; ++Index)
                {
                    if ((int)Table[Index].Section != Group) continue;
                    const bool On = (Layer != nullptr) && Layer->Channels[Index].Enabled;
                    // 🔴 A pinned channel is listed but inert (.afm.locked has pointer-events:none) — it must never be re-added or removed.
                    AppendLine(Plan, Table[Index].Label, nullptr, Index, Table[Index].Hue, true,
                               On, Table[Index].Removable, false, AddLineHeight);
                }
            }
            break;
        }
    }

    // -- Extent ----------------------------------------------------------------------------------------------------------
    const bool  Attached = (Anchor->Category == PaintMenuCategory::LayerKinds);
    const bool  Floating = (Anchor->Category == PaintMenuCategory::ChannelSet);
    const float Pad      = Attached ? 0.0f : MenuPad;

    float Content = 0.0f;
    for (int Index = 0; Index < Plan.LineCount; ++Index) { Content += Plan.Lines[Index].Size.y + (Attached ? 0.0f : 1.0f); }
    if (!Attached && Plan.LineCount > 0) { Content -= 1.0f; }

    float Width = Anchor->FaceSize.x;
    if (Floating) { Width = AddMenuWidth; }
    if (Attached) { Width = Anchor->FaceSize.x; }

    const float SpanMax = Floating ? AddMenuSpanMax : MenuSpanMax;
    const float Height  = ClampSpan(Content + Pad * 2.0f, 0.0f, SpanMax);

    // 📝 The add-menu opens 31 px below its button; a dropdown 5 px below its face; the kind list is flush against it.
    const float Below = Attached ? Anchor->FaceSize.y : (Floating ? 31.0f : Anchor->FaceSize.y + MenuGap);
    Plan.Origin = ImVec2(Anchor->FaceTopLeft.x, Anchor->FaceTopLeft.y + Below);
    Plan.Size   = ImVec2(Width, Height);

    // 🔴 Flip above the face when the menu would leave the pane it belongs to; the source's overflow does this by scrolling, but a floating
    //    overlay has no scroll to give and would simply be cut off.
    if (Plan.Origin.y + Height > Anchor->ClipBottomRight.y - 4.0f)
    {
        const float Above = Anchor->FaceTopLeft.y - Height - (Attached ? 0.0f : MenuGap);
        if (Above >= Anchor->ClipTopLeft.y + 4.0f) { Plan.Origin.y = Above; }
    }
    if (Plan.Origin.x + Width > Anchor->ClipBottomRight.x - 4.0f)
    {
        Plan.Origin.x = Anchor->ClipBottomRight.x - 4.0f - Width;
    }

    Plan.ClipTopLeft     = Anchor->ClipTopLeft;
    Plan.ClipBottomRight = Anchor->ClipBottomRight;

    // -- Stack the rows --------------------------------------------------------------------------------------------------
    float Pen = Plan.Origin.y + Pad;
    for (int Index = 0; Index < Plan.LineCount; ++Index)
    {
        PaintMenuLine& Line = Plan.Lines[Index];
        const float    Tall = Line.Size.y;
        Line.Origin = ImVec2(Plan.Origin.x + Pad, Pen);
        Line.Size   = ImVec2(Width - Pad * 2.0f, Tall);
        Pen += Tall + (Attached ? 0.0f : 1.0f);
    }
    return Plan;
}

int ProbePaintMenu(const PaintMenuPlan& Plan)
{
    if (!Plan.Live) { return -1; }

    // 📝 One blocking zone under the whole menu keeps a click inside it from reaching the pane beneath, even between rows.
    char BlockId[LayoutKeySpan];
    snprintf(BlockId, LayoutKeySpan, "##menu-%s-block", Plan.Key);
    RecordHitZone(BlockId, Plan.Origin, Plan.Size);

    int Chosen = -1;
    for (int Index = 0; Index < Plan.LineCount; ++Index)
    {
        const PaintMenuLine& Line = Plan.Lines[Index];
        if (Line.Heading || !Line.Available) continue;

        char LineId[LayoutKeySpan];
        snprintf(LineId, LayoutKeySpan, "##menu-%s-%d", Plan.Key, Index);
        if (RecordHitZone(LineId, Line.Origin, Line.Size)) { Chosen = Line.Value; }
    }
    return Chosen;
}

void RecordPaintMenu(ImDrawList* Draw, const PaintMenuPlan& Plan)
{
    if (!Plan.Live) { return; }

    // 📝 The unfold keyframe: scaled from the top edge, clipped open, fading in.
    const float  Reveal = Plan.Age;
    const float  Scale  = 0.86f + 0.14f * Reveal;
    const ImVec2 MenuTL(Plan.Origin.x, Plan.Origin.y - 4.0f * (1.0f - Reveal));
    const ImVec2 MenuBR(MenuTL.x + Plan.Size.x, MenuTL.y + Plan.Size.y * Scale);

    Draw->PushClipRect(ImVec2(MenuTL.x - 2.0f, MenuTL.y),
                       ImVec2(MenuBR.x + 2.0f, MenuTL.y + Plan.Size.y * Reveal + 2.0f), false);

    const bool  Attached = (Plan.Category == PaintMenuCategory::LayerKinds);
    const float Round    = Attached ? 9.0f : (Plan.Category == PaintMenuCategory::ChannelSet ? 9.0f : 9.0f);

    Draw->AddRectFilled(MenuTL, MenuBR, ApplyOpacity(Tone::MenuFill, Reveal), Round,
                        Attached ? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersAll);
    Draw->AddRect(MenuTL, MenuBR, ApplyOpacity(Attached ? Tone::Border : Tone::HairStrong, Reveal), Round,
                  Attached ? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersAll);

    const ImGuiIO& Io = ImGui::GetIO();
    for (int Index = 0; Index < Plan.LineCount; ++Index)
    {
        const PaintMenuLine& Line = Plan.Lines[Index];
        const ImVec2 LineTL(Line.Origin.x, MenuTL.y + (Line.Origin.y - Plan.Origin.y) * Scale);
        const ImVec2 LineBR(LineTL.x + Line.Size.x, LineTL.y + Line.Size.y);
        const float  Centre = (LineTL.y + LineBR.y) * 0.5f;

        if (Line.Heading)
        {
            RecordInkTracked(Draw, ImVec2(LineTL.x + 8.0f, LineBR.y - 12.0f), 9.0f, 0.8f,
                             ApplyOpacity(Tone::Faint, Reveal), Line.Label);
            continue;
        }

        const bool Hovered = Line.Available
                          && Io.MousePos.x >= LineTL.x && Io.MousePos.x <= LineBR.x
                          && Io.MousePos.y >= LineTL.y && Io.MousePos.y <= LineBR.y;

        // 📝 Hovering indents the row — 5 px on an option, 4 px on a kind / channel entry.
        const float Indent = Hovered ? (Plan.Category == PaintMenuCategory::Options ? 5.0f : 4.0f) : 0.0f;
        const float Ghost  = Line.Available ? 1.0f : 0.45f;

        if (Hovered)
        {
            const ImU32 Wash = (Plan.Category == PaintMenuCategory::ChannelSet) ? Tone::RowHover : Tone::TileOn;
            Draw->AddRectFilled(LineTL, LineBR, ApplyOpacity(Wash, Reveal), 6.0f);
            if (Plan.Category == PaintMenuCategory::Options || Plan.Category == PaintMenuCategory::Generators)
            {
                // 📝 The 3 px marker bar the source grows on ::before.
                Draw->AddRectFilled(ImVec2(LineTL.x, LineTL.y + 4.0f), ImVec2(LineTL.x + 3.0f, LineBR.y - 4.0f),
                                    ApplyOpacity(Tone::Marker, Reveal), 3.0f, ImDrawFlags_RoundCornersRight);
            }
        }

        float Pen  = LineTL.x + 12.0f + Indent;
        float Tail = LineBR.x - 12.0f;

        if (Line.SwatchPresent)
        {
            if (Plan.Category == PaintMenuCategory::LayerKinds)
            {
                Draw->AddRectFilled(ImVec2(Pen - 2.0f, Centre - 4.5f), ImVec2(Pen + 7.0f, Centre + 4.5f),
                                    ApplyOpacity(Line.Swatch, Reveal * Ghost), 2.0f);
            }
            else
            {
                Draw->AddCircleFilled(ImVec2(Pen + 2.5f, Centre), 4.5f, ApplyOpacity(Line.Swatch, Reveal * Ghost));
            }
            Pen += 9.0f + 9.0f;
        }

        if (Plan.Category == PaintMenuCategory::Options || Plan.Category == PaintMenuCategory::Generators)
        {
            // -- Radio -------------------------------------------------------------------------------------------------
            const ImVec2 Radio(Tail - 6.0f, Centre);
            Draw->AddCircle(Radio, 6.0f, ApplyOpacity(Line.Selected ? Tone::Marker : Tone::RadioEdge, Reveal), 0, 1.5f);
            if (Line.Selected) { Draw->AddCircleFilled(Radio, 3.0f, ApplyOpacity(Tone::Marker, Reveal)); }
            Tail -= 12.0f + 8.0f;
        }
        else if (Plan.Category == PaintMenuCategory::ChannelSet)
        {
            if (Line.Selected)
            {
                RecordGlyph(Draw, ImVec2(Tail - 6.0f, Centre), 12.0f, PaintGlyph::Tick, ApplyOpacity(Tone::Ok, Reveal));
            }
            Tail -= 12.0f + 8.0f;
        }

        if (Line.Note != nullptr)
        {
            const float NoteWidth = MeasureInk(9.5f, Line.Note).x;
            if (Plan.Category == PaintMenuCategory::LayerKinds || Plan.Category == PaintMenuCategory::Generators)
            {
                RecordInk(Draw, ImVec2(Tail - NoteWidth, Centre - 5.0f), 9.5f,
                          ApplyOpacity(Tone::Faint, Reveal * Ghost), Line.Note);
                Tail -= NoteWidth + 8.0f;
            }
        }

        RecordInkClipped(Draw, ImVec2(Pen, Centre - 6.0f), Tail - Pen, 11.5f,
                         ApplyOpacity(Hovered ? Tone::Ink : Tone::Ink, Reveal * Ghost), Line.Label);
    }
    Draw->PopClipRect();
}

bool WithinPaintMenu(const PaintMenuPlan& Plan)
{
    if (!Plan.Live) { return false; }
    const ImVec2& Cursor = ImGui::GetIO().MousePos;
    return Cursor.x >= Plan.Origin.x && Cursor.x <= Plan.Origin.x + Plan.Size.x
        && Cursor.y >= Plan.Origin.y && Cursor.y <= Plan.Origin.y + Plan.Size.y;
}

}   // namespace PaintLayerSequenceValidation
