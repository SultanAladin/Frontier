/*==============================================================================================================================================
                                                              PROPERTYPANELBASE.CPP
==============================================================================================================================================*/
// 🧩 The properties-column scaffolding: a scrolling child hosting collapsible cards. Composes SectionHeader + ContentSection so every
//    workspace's property panel shares one card rhythm + one scroll region. Workspaces supply only the controls inside each card. The scroll
//    region is SMOOTHED: the mouse wheel feeds a per-panel target position and the actual scroll eases toward it each frame, so the page
//    eases (a subtle scroll-lag micro-animation) instead of snapping row-by-row.

#include "PropertyPanelBase.h"

#include "imgui.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Frame-rate-independent exponential ease: close ~Rate of the remaining gap per second, stable at any frame time.
    float EaseToward(float Current, float Target, float Rate, float DeltaTime)
    {
        if (DeltaTime <= 0.0f)
        {
            return Target;
        }
        return Current + (Target - Current) * (1.0f - std::exp(-Rate * DeltaTime));
    }

    // -- Card fold animation ---------------------------------------------------------------------------------------------
    // 📝 Each card eases its body open / closed instead of popping. The persisted collapse INTENT is the caller-owned Expanded bool; the
    //    eased 0..1 reveal fraction (Fold) and the cached natural body height (BodyH) live in per-card ImGui storage keyed by the card id.
    //    Every frame the card is drawn at HeaderH + BodyH * Fold and the body is CLIPPED to that animated height. This is the retired
    //    property panel's proven measure-then-clip pattern, adapted to immediate-mode controls.
    //
    //    🔴 Two bugs the old attempts hit, and the guards here:
    //       • Clipping — the body is recorded inside an explicit PushClipRect to [top .. top + BodyH*Fold] and the layout cursor is
    //         advanced by exactly that reveal height, so no control ever floats past the rounded card or bleeds into the next one.
    //       • Oscillation — the fold NEVER feeds back into the header hit-rect (that height is fixed in SectionHeader), and the intent is
    //         flipped only on a click EDGE (ConstructSectionHeader returns clicked-this-frame). A moving hit-rect + hover-driven toggle
    //         was what made earlier versions flicker open↔closed; both are removed.
    constexpr float FoldRate     = 0.28f;   // [-]  - per-frame ease constant for the body reveal (matches the retired SectionFold)
    constexpr float FoldSnap     = 0.01f;   // [-]  - snap the eased fold exactly once within this of the target
    constexpr float MeasureTallH = 8192.0f; // [px] - a generous clip ceiling used the first frame a card is measured (before BodyH is known)

    struct CardFoldState
    {
        float Fold  = 0.0f;    // [-]  - eased 0..1 reveal fraction
        float BodyH = 0.0f;    // [px] - cached natural (unfolded) body height
        bool  Seeded = false;  // [-]  - the fold was initialised to match the intent once (so a card shown already-open does not animate in)
    };

    // Resolve (lazily create) the fold state for a card id, stored in the window's ImGui storage so it survives across frames without a
    //    caller-side struct. Keyed by the card title id the caller already pushes, mirroring how the retired panel keyed folds by section.
    CardFoldState* ResolveCardFold(ImGuiID CardId)
    {
        ImGuiStorage* Store = ImGui::GetStateStorage();
        CardFoldState* Fold = static_cast<CardFoldState*>(Store->GetVoidPtr(CardId));
        if (Fold == nullptr)
        {
            Fold = IM_NEW(CardFoldState)();
            Store->SetVoidPtr(CardId, Fold);
        }
        return Fold;
    }

    // -- Card background (UVeditor .prop-section look: a rounded --panel-2 rectangle wrapping the header + body, with a gap between cards) --
    // 📝 The card fill is painted on a background draw channel BEHIND the header + eased body: the split opens at card start, everything
    //    records on the foreground, then the rounded fill paints once the animated extent is known. FinishPendingCard is called from
    //    EndPropertyCard, from the NEXT BeginPropertyCard, and from EndPropertyPanel so every card is closed exactly once.
    bool   CardOpen      = false;
    float  CardTopY      = 0.0f;
    float  CardLeftX     = 0.0f;
    float  CardRightX    = 0.0f;
    float  CardRounding  = 14.0f;   // matches props.css .prop-section border-radius:14px
    float  CardGap       = 6.0f;    // matches props.css .prop-section margin:6px (gap between rounded cards)
    ImU32  CardFill      = 0;

    // Animation carry from BeginPropertyCard → EndPropertyCard (the pair never nests, so a single slot suffices, like CardOpen).
    CardFoldState* PendingFold    = nullptr;   // [-]  - the open card's fold state (null when the card is fully collapsed / not measuring)
    float          PendingBodyTop = 0.0f;      // [px] - screen-Y where the open card's body begins (below the header)
    float          PendingRevealH = 0.0f;      // [px] - the animated reveal height the body is clipped + advanced to this frame
    bool           PendingClipped = false;     // [-]  - a clip rect + group were opened for the body and must be closed in EndPropertyCard

    void FinishPendingCard()
    {
        if (!CardOpen)
        {
            return;
        }
        CardOpen = false;

        ImDrawList* Draw = ImGui::GetWindowDrawList();
        const float BottomY = ImGui::GetCursorScreenPos().y;

        Draw->ChannelsSetCurrent(0);
        Draw->AddRectFilled(ImVec2(CardLeftX, CardTopY), ImVec2(CardRightX, BottomY), CardFill, CardRounding);
        Draw->ChannelsMerge();
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void BeginPropertyPanel(const ThemeConfiguration& Theme, const char* Identifier)
{
    // 📝 The panel body is the UVeditor --bg (black), NOT the card fill — so the gaps between the rounded --panel-2 cards actually SHOW as
    //    black separators. (ImGuiCol_ChildBg is --panel-2 globally, which would otherwise make card + body the same colour = one merged block.)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme.Palette.DeskBackground);

    // 📝 A vertically-scrolling child so a long stack of cards scrolls independently of the viewport. NoScrollWithMouse: we capture the wheel
    //    ourselves below so we can ease toward a target instead of ImGui snapping ScrollY directly.
    ImGui::BeginChild(Identifier ? Identifier : "##properties", ImVec2(0.0f, 0.0f), false,
                      ImGuiWindowFlags_NoScrollWithMouse);

    // -- Smooth scroll ---------------------------------------------------------------------------------------------------
    ImGuiStorage* Store     = ImGui::GetStateStorage();
    const ImGuiID TargetKey = ImGui::GetID("##scrolltarget");
    const ImGuiID SeedKey   = ImGui::GetID("##scrollseed");
    const float   MaxY      = ImGui::GetScrollMaxY();

    // Seed the target from the live scroll on first sight, and re-seed if something else moved the scroll (keyboard nav, resize clamp).
    float Target = Store->GetFloat(TargetKey, ImGui::GetScrollY());
    if (Store->GetInt(SeedKey, 0) == 0)
    {
        Target = ImGui::GetScrollY();
        Store->SetInt(SeedKey, 1);
    }

    // Feed the wheel into the target while the panel (or a child of it) is hovered. One "notch" moves ~3 text lines.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
    {
        const float Wheel = ImGui::GetIO().MouseWheel;
        if (Wheel != 0.0f)
        {
            Target -= Wheel * ImGui::GetTextLineHeightWithSpacing() * 3.0f;
        }
    }

    Target = Target < 0.0f ? 0.0f : (Target > MaxY ? MaxY : Target);

    // Ease the real scroll toward the target; snap the last pixel so it settles crisply.
    float NewScroll = EaseToward(ImGui::GetScrollY(), Target, 18.0f, ImGui::GetIO().DeltaTime);
    if (std::fabs(NewScroll - Target) < 0.5f)
    {
        NewScroll = Target;
    }
    ImGui::SetScrollY(NewScroll);
    Store->SetFloat(TargetKey, Target);
}


void EndPropertyPanel(const ThemeConfiguration& Theme)
{
    (void)Theme;
    // 📝 Paint the last card's background (a collapsed final card would otherwise leave the split unmerged).
    FinishPendingCard();
    // 📝 A trailing gap so the last card isn't flush against the panel's bottom edge.
    ImGui::Dummy(ImVec2(0.0f, CardGap));
    ImGui::EndChild();
    ImGui::PopStyleColor();   // ChildBg
}


bool BeginPropertyCard(const ThemeConfiguration& Theme, const char* Title, bool* Expanded)
{
    // 📝 Close the previous card's background first (a collapsed card never reaches EndPropertyCard).
    FinishPendingCard();

    // 📝 A gap above every card (UVeditor .prop-section margin) so cards read as separate rounded panels.
    ImGui::Dummy(ImVec2(0.0f, CardGap));

    // 📝 Open the background/foreground channel split and remember this card's top-left + full width for the rounded fill.
    ImDrawList*  Draw   = ImGui::GetWindowDrawList();
    const ImVec2 Cursor = ImGui::GetCursorScreenPos();
    CardOpen     = true;
    CardTopY     = Cursor.y;
    CardLeftX    = Cursor.x;
    CardRightX   = Cursor.x + ImGui::GetContentRegionAvail().x;
    CardRounding = 14.0f;   // UVeditor .prop-section border-radius:14px (independent of the small control CornerRounding)
    CardFill     = Theme.Palette.PanelBackground;   // UVeditor --panel-2 (#0e0e0e) rounded card fill
    Draw->ChannelsSplit(2);
    Draw->ChannelsSetCurrent(1);

    // -- Fold state ------------------------------------------------------------------------------------------------------
    // 📝 Resolve this card's persistent fold, then ease it toward the caller's intent. The FIRST time a card is seen its fold snaps to
    //    the intent (no animate-in for a card that starts open); after that it eases so every open / close eases.
    const ImGuiID  CardId = ImGui::GetID(Title ? Title : "##card");
    CardFoldState* Fold   = ResolveCardFold(CardId);
    const bool     Intent = (Expanded == nullptr) || (*Expanded);   // null Expanded => a non-collapsible, always-open card
    const float    Target = Intent ? 1.0f : 0.0f;
    if (!Fold->Seeded)
    {
        Fold->Fold   = Target;
        Fold->Seeded = true;
    }
    else
    {
        Fold->Fold += (Target - Fold->Fold) * FoldRate;
        if (std::fabs(Target - Fold->Fold) < FoldSnap)
        {
            Fold->Fold = Target;
        }
    }

    // -- Header (fixed hit-rect; flips the intent on a click EDGE — the oscillation guard) --------------------------------
    SectionHeaderDescriptor Header = {};
    Header.Title        = Title;
    Header.Expanded     = Expanded;
    Header.FoldFraction = Fold->Fold;
    if (ConstructSectionHeader(Theme, Header) && Expanded != nullptr)
    {
        *Expanded = !*Expanded;
    }

    // -- Body reveal decision --------------------------------------------------------------------------------------------
    // 📝 The natural body height must be measured at least once before it can be eased. Until BodyH is known we still record the body but
    //    clip it to a tall ceiling so ImGui lays it out at full size and EndPropertyCard can read that natural height. Once measured, the
    //    body is clipped + advanced to BodyH * Fold. A fully-collapsed, already-measured card records nothing at all (perf + no stray hits).
    PendingFold    = Fold;
    PendingBodyTop = ImGui::GetCursorScreenPos().y;
    PendingClipped = false;

    const bool Measured = (Fold->BodyH > 0.0f);
    if (Measured && Fold->Fold <= 0.0001f)
    {
        // Fully closed and its height already known → skip the body entirely. The card is just its header this frame. (A card that has
        //    NOT been measured yet always falls through below, even when closed, so its natural height is captured once — that is what
        //    lets the very first open animate from 0 instead of popping to full.)
        PendingRevealH = 0.0f;
        return false;
    }

    PendingRevealH = Measured ? Fold->BodyH * Fold->Fold : 0.0f;
    // 📝 Measured → clip to the eased reveal. Not yet measured → clip to a tall ceiling so ImGui lays the body out at full size and the
    //    group extent is real; the reveal still advances by BodyH*Fold in EndPropertyCard, so an unmeasured closed card contributes 0
    //    visible height while its natural size is captured for next frame.
    const float ClipBottom = PendingBodyTop + (Measured ? PendingRevealH : MeasureTallH);
    Draw->PushClipRect(ImVec2(CardLeftX, PendingBodyTop), ImVec2(CardRightX, ClipBottom), true);
    ImGui::BeginGroup();   // 📝 So EndPropertyCard can read the body's natural extent via GetItemRectSize.
    PendingClipped = true;

    ContentSectionDescriptor Body = {};
    Body.Identifier  = Title;
    Body.FixedHeight = 0.0f;   // 📝 Grows to its controls; the outer panel provides the scroll.
    BeginContentSection(Theme, Body);
    return true;
}


void EndPropertyCard(const ThemeConfiguration& Theme)
{
    EndContentSection(Theme);

    if (PendingClipped)
    {
        ImGui::EndGroup();
        // 📝 Cache the natural (unfolded) body height from the group's measured extent — this is the "measure once, then ease" capture.
        //    Kept refreshed every open frame so a control that changes size updates the target height without a stale cache.
        const float NaturalH = ImGui::GetItemRectSize().y;
        if (NaturalH > 0.0f && PendingFold != nullptr)
        {
            PendingFold->BodyH = NaturalH;
        }

        ImGui::GetWindowDrawList()->PopClipRect();

        // 📝 Force the layout cursor to the ANIMATED reveal height, not the natural group height. The first (measuring) frame reveals the
        //    full body so the panel does not jump; every eased frame after clamps to BodyH * Fold so cards below follow the ease and no
        //    content floats past the rounded card.
        const bool  Measured = (PendingFold != nullptr && PendingFold->BodyH > 0.0f && PendingFold->Fold < 0.9999f);
        const float RevealH  = Measured ? PendingFold->BodyH * PendingFold->Fold : NaturalH;
        ImGui::SetCursorScreenPos(ImVec2(CardLeftX, PendingBodyTop + RevealH));
    }

    PendingFold    = nullptr;
    PendingClipped = false;

    FinishPendingCard();
}

}   // namespace Frontier
