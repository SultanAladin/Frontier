/*==============================================================================================================================================
                                                    TEXTUREPAINTSUMMONEDCARD.CPP
==============================================================================================================================================*/
// 🧩 The right-press summon, the on-screen placement clamp, and the per-frame drive of the embedded paint card. Ported gesture behaviour from
//    SketchModelSummonedSurfaces.cpp's console summon, with right-click restored as the opener (that viewport had to yield right-drag to the
//    camera orbit; a texture-paint field has no orbit, so the press is free here).

#include "TexturePaintSummonedCard.h"

#include "PaintCardSpecification.h"
#include "PaintCatalogue.h"
#include "PaintIconStore.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"
#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

namespace TexturePaintValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float FieldEdgeMargin = 12.0f;   // [px] - the gap the card box keeps off every field edge

    // 📝 Whether a point is inside the field rectangle the host confined this frame. A zero-extent field (before the first
    //    ConfineTexturePaintField, or a minimized window) contains nothing, so no press can summon through it.
    bool WithinPaintField(const TexturePaintSummonedState& State, ImVec2 Point)
    {
        if (State.FieldWidth <= 0.0f || State.FieldHeight <= 0.0f) { return false; }
        return Point.x >= State.FieldLeft && Point.x <= State.FieldLeft + State.FieldWidth &&
               Point.y >= State.FieldTop  && Point.y <= State.FieldTop  + State.FieldHeight;
    }


    // 📝 Place the card fully on screen from the pointer. It opens down-right of the press like every other summoned surface in the tree, folding
    //    back when an edge would cross the field; the returned point is the box CENTRE, because that is what ConstructPaintToolPanel takes.
    // ⚠️ Resolved from the card's own metrics rather than a second copy of its 560x420 numbers, so a re-scaled theme moves the clamp with the box.
    ImVec2 ResolveCardPlacement(const Frontier::ThemeConfiguration& Theme, ImVec2 Pointer,
                                const TexturePaintSummonedState& State)
    {
        const Frontier::PaintCardMetrics Metrics = Frontier::ResolvePaintCardMetrics(Theme);

        const float FieldRight  = State.FieldLeft + State.FieldWidth;
        const float FieldBottom = State.FieldTop  + State.FieldHeight;

        float Left = Pointer.x + 10.0f;
        float Top  = Pointer.y + 10.0f;

        if (Left + Metrics.CardWidth > FieldRight - FieldEdgeMargin)
        {
            const float Folded = Pointer.x - Metrics.CardWidth - 10.0f;
            Left = (Folded > State.FieldLeft + FieldEdgeMargin) ? Folded : State.FieldLeft + FieldEdgeMargin;
        }
        if (Top + Metrics.CardHeight > FieldBottom - FieldEdgeMargin)
        {
            const float Folded = FieldBottom - FieldEdgeMargin - Metrics.CardHeight;
            Top = (Folded > State.FieldTop + FieldEdgeMargin) ? Folded : State.FieldTop + FieldEdgeMargin;
        }

        return ImVec2(Left + Metrics.CardWidth * 0.5f, Top + Metrics.CardHeight * 0.5f);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeTexturePaintSummonedCard(TexturePaintSummonedState& State)
{
    // 🔴 The card is OPENED to seed it — OpenPaintToolPanel is the only entry that resolves the family's first instrument, its schema and its
    //    value set, so slide 2 is valid before the carousel can ever reach it. Then it is closed again, leaving the seeded state standing. A
    //    bare ClosePaintToolPanel on a default-constructed state would walk a schema with zero groups the first time a press opened it.
    Frontier::OpenPaintToolPanel(State.Card, 0);
    Frontier::ClosePaintToolPanel(State.Card);

    State.CardSummoned      = false;
    State.CardCentreX       = 0.0f;
    State.CardCentreY       = 0.0f;
    State.ReanchorRequested = false;
}


void ConfineTexturePaintField(TexturePaintSummonedState& State, ImVec2 FieldOrigin, ImVec2 FieldSpan)
{
    State.FieldLeft   = FieldOrigin.x;
    State.FieldTop    = FieldOrigin.y;
    State.FieldWidth  = FieldSpan.x;
    State.FieldHeight = FieldSpan.y;
}


void ConstructTexturePaintSummonedCard(const Frontier::ThemeConfiguration& Theme,
                                       TexturePaintSummonedState&          State,
                                       Frontier::SvgIconRegistry*          Icons,
                                       Frontier::PaintIconStore*           StripStore)
{
    const ImGuiIO& Io = ImGui::GetIO();

    // -- The right press. Latched, never applied here: see the header note on the open-and-immediately-dismiss race. --
    //    🔴 A press while the card already stands is left ALONE rather than treated as a re-anchor. The card reads an outside press as its own
    //       dismissal, and it is the authority on whether the press landed on one of its panes — moving the box out from under a press it was
    //       about to answer would relocate the card on a click meant for a tile.
    if (!State.CardSummoned && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && WithinPaintField(State, Io.MousePos))
    {
        State.ReanchorRequested = true;
    }

    // -- Draw the card. Called UNCONDITIONALLY: it self-gates on CardOpen and still advances its carousel slide-back and its toast on a closed
    //    frame, so skipping the call while closed would freeze a fading toast and a half-travelled slide. --
    const Frontier::PaintCardPalette Palette = Frontier::ResolvePaintCardPalette(Theme);
    const Frontier::PaintCardMetrics Metrics = Frontier::ResolvePaintCardMetrics(Theme);

    // 🔴 OUTSIDE-PRESS DISMISSAL — the card's embedded console never raises a DismissRequested (it is built with a default-zero result), so
    //    without this the card would stand forever once a right-press opened it. A press on one of the card's own panes is left for the card to
    //    answer; a press anywhere outside the box closes it, the same contract the Tab-summoned surfaces use (SceneDirectoryInspector's
    //    `left-click outside the card closes`). Both buttons dismiss, matching the right-press opener. The press that OPENED the card can never
    //    reach here — it was consumed by the latch the frame the card was still closed.
    if (State.CardSummoned)
    {
        const ImVec2 BoxLeft(State.CardCentreX - Metrics.CardWidth * 0.5f, State.CardCentreY - Metrics.CardHeight * 0.5f);
        const bool OverBox = Io.MousePos.x >= BoxLeft.x && Io.MousePos.x <= BoxLeft.x + Metrics.CardWidth &&
                             Io.MousePos.y >= BoxLeft.y && Io.MousePos.y <= BoxLeft.y + Metrics.CardHeight;
        if (!OverBox && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        {
            Frontier::ClosePaintToolPanel(State.Card);
        }
    }

    Frontier::ConstructPaintToolPanel(State.Card, Theme, Palette, Metrics, Icons, StripStore,
                                      ImVec2(State.CardCentreX, State.CardCentreY));

    // 🔴 The summon flag TRACKS the card rather than deciding for it: the dismissal above (or the card's own Select footer) already spent
    //    CardOpen this frame, so the flag is re-read from the card's report, not driven by a choice made here.
    State.CardSummoned = State.Card.CardOpen;

    // -- The frame TAIL: apply a latched press now that the card has reported and spent this frame's dismissal. --
    if (State.ReanchorRequested)
    {
        State.ReanchorRequested = false;

        const ImVec2 Centre = ResolveCardPlacement(Theme, Io.MousePos, State);
        State.CardCentreX   = Centre.x;
        State.CardCentreY   = Centre.y;

        // 🔴 Re-OPENED, not merely re-flagged: OpenPaintToolPanel replays the carousel travel and the open-pop from zero, which is the
        //    animation the prototype plays on every summon. Setting CardOpen alone would pop the card in fully-grown and mid-slide.
        Frontier::OpenPaintToolPanel(State.Card, State.Card.FamilyIndex);
        State.CardSummoned = true;
    }
}

} // namespace TexturePaintValidation
