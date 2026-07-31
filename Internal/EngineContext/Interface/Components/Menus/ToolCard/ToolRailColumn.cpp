/*==============================================================================================================================================
                                                        TOOLRAILCOLUMN.CPP
==============================================================================================================================================*/
// 🧩 Resolves and draws the band rail. The resolve half is the interesting one: filtering rows out moves the rules that divide them, so the plan is
//    built in two passes — survivors first, then rules placed against the survivors rather than against the authored table.

#include "ToolRailColumn.h"
#include "ToolGlyphInscription.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float RailBodyPadding  =  7.0f;   // [px] - .rail-body padding
    constexpr float RailRowRounding  =  9.0f;   // [px] - .rail-item radius
    constexpr float RailGlyphEdge    = 18.0f;   // [px] - .r-ic
    constexpr float RailGlyphGutter  = 31.0f;   // [px] - row left edge to caption: 6 padding + 18 glyph + 7 gap
    constexpr float RailMarkerWidth  =  3.0f;   // [px] - .rail-item.active::before width
    constexpr float RailMarkerHeight = 15.0f;   // [px] - that bar's length
    constexpr float RuleHeight       =  9.0f;   // [px] - vertical space .rail-sep occupies
    constexpr float TrailingInset    =  7.0f;   // [px] - right edge to the trailing figure
    constexpr float CaptionGap       =  9.0f;   // [px] - caption clip to the trailing figure
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ToolRailPlan ResolveToolRailPlan(const ToolBandDescriptor* Bands,
                                 int                       BandCount,
                                 unsigned int              StratumBit,
                                 const ToolCardMetrics&    Metrics)
{
    ToolRailPlan Plan = {};
    if (Bands == nullptr || BandCount <= 0)
    {
        return Plan;
    }

    // PASS ONE — which bands earn a row.
    // 🔴 A band with no applicable tile is OMITTED, not dimmed. This is the one place the hide-vs-grey rule inverts relative to the
    //    tiles, and the empty selection is why: with nothing picked, thirteen modelling families have zero applicable tools, and
    //    dimming all thirteen would bury the four creation bands that DO apply under a wall of dead rows. A tile greys because its
    //    shortfall is actionable ("needs 2+ faces"); a family that cannot apply at this stratum carries no such information.
    //    A single-shot row carries no tiles, so it is judged on its own mask instead.
    for (int BandIndex = 0; BandIndex < BandCount && Plan.RowCount < ToolRailRowLimit; ++BandIndex)
    {
        const ToolBandDescriptor& Band       = Bands[BandIndex];
        const bool                SingleShot = (Band.Tiles == nullptr);

        int Applicable = 0;
        int Live       = 0;
        if (SingleShot)
        {
            Applicable = ((Band.StrataMask & StratumBit) != 0u) ? 1 : 0;
            Live       = Applicable;
        }
        else
        {
            Applicable = TallyApplicableTiles(Band, StratumBit);
            Live       = TallyLiveTiles(Band, StratumBit);
        }

        if (Applicable == 0)
        {
            continue;
        }

        ToolRailRow& Row     = Plan.Rows[Plan.RowCount++];
        Row.BandIndex        = BandIndex;
        Row.LiveTally        = Live;
        Row.ApplicableTally  = Applicable;
        Row.SingleShot       = SingleShot;
        Row.RuleAbove        = false;
    }

    // PASS TWO — where the rules land.
    // 📝 A rule is authored above a band, but the surviving neighbour may not be that band. So the rule migrates: it belongs above
    //    the first survivor at or after each authored rule position. Walking the survivors and asking "was a rule authored anywhere
    //    in the gap since the previous survivor?" resolves both failure modes at once — a rule whose own band was dropped still
    //    divides the groups, and a rule with no survivor after it simply never gets placed. The FIRST row never takes a rule: a
    //    leading rule divides the list from nothing above it.
    for (int RowIndex = 1; RowIndex < Plan.RowCount; ++RowIndex)
    {
        const int GapFirst = Plan.Rows[RowIndex - 1].BandIndex + 1;   // first band after the previous survivor
        const int GapLast  = Plan.Rows[RowIndex].BandIndex;           // this survivor, inclusive

        bool RuleInGap = false;
        for (int BandIndex = GapFirst; BandIndex <= GapLast; ++BandIndex)
        {
            if (Bands[BandIndex].SeparatorAbove)
            {
                RuleInGap = true;
                break;
            }
        }
        Plan.Rows[RowIndex].RuleAbove = RuleInGap;
    }

    // The scroll range the body needs, rules included.
    Plan.ContentHeight = RailBodyPadding * 2.0f;
    for (int RowIndex = 0; RowIndex < Plan.RowCount; ++RowIndex)
    {
        Plan.ContentHeight += Metrics.RailRowHeight;
        if (Plan.Rows[RowIndex].RuleAbove)
        {
            Plan.ContentHeight += RuleHeight;
        }
    }

    return Plan;
}


ToolRailOutcome InscribeToolRailColumn(const SvgIconRegistry*    Icons,
                                       const ToolBandDescriptor* Bands,
                                       const ToolRailPlan&       Plan,
                                       int                       OpenBand,
                                       ImVec2                    Origin,
                                       float                     ColumnWidth,
                                       const ToolCardPalette&    Palette,
                                       const ToolCardMetrics&    Metrics,
                                       bool                      ScrollSuppressesHover)
{
    ToolRailOutcome Outcome = {};
    Outcome.HoveredBand     = -1;
    Outcome.ActivatedBand   = -1;

    if (Bands == nullptr || Plan.RowCount == 0)
    {
        return Outcome;
    }

    ImDrawList* Canvas     = ImGui::GetWindowDrawList();
    const float ColumnRight = Origin.x + ColumnWidth;
    float       CursorY     = Origin.y + RailBodyPadding;

    for (int RowIndex = 0; RowIndex < Plan.RowCount; ++RowIndex)
    {
        const ToolRailRow&        Row  = Plan.Rows[RowIndex];
        const ToolBandDescriptor& Band = Bands[Row.BandIndex];

        // The rule dividing the creation bands from the modelling families, and those from the single-shot commands below. A
        // mis-click into Delete costs a deliberate crossing of it rather than a slip.
        if (Row.RuleAbove)
        {
            const float RuleY = CursorY + RuleHeight * 0.5f;
            Canvas->AddLine(ImVec2(Origin.x + RailBodyPadding * 2.0f, RuleY),
                            ImVec2(ColumnRight - RailBodyPadding * 2.0f, RuleY), Palette.Hairline, 1.0f);
            CursorY += RuleHeight;
        }

        const bool   Active = (!Row.SingleShot && Row.BandIndex == OpenBand);
        const ImVec2 RowMin(Origin.x + RailBodyPadding, CursorY);
        const ImVec2 RowMax(ColumnRight - RailBodyPadding, CursorY + Metrics.RailRowHeight);

        ImGui::SetCursorScreenPos(RowMin);
        ImGui::PushID(Row.BandIndex);
        const bool Pressed = ImGui::InvisibleButton("##band", ImVec2(RowMax.x - RowMin.x, RowMax.y - RowMin.y));
        const bool Hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        if (Hovered)
        {
            Outcome.HoveredBand = Row.BandIndex;
            // ⚠️ Hover-to-swap is muted while the rail scrolls, but a CLICK always acts. Under the wheel the pointer holds still
            //    while rows travel beneath it, so every row that passes would swap the grid and one throw would thrash through
            //    five bands.
            if (!ScrollSuppressesHover && !Row.SingleShot)
            {
                Outcome.ActivatedBand   = Row.BandIndex;
                Outcome.SingleShotFired = false;
            }
        }
        if (Pressed)
        {
            Outcome.ActivatedBand   = Row.BandIndex;
            Outcome.SingleShotFired = Row.SingleShot;
        }

        if (Active || (Hovered && Row.SingleShot))
        {
            Canvas->AddRectFilled(RowMin, RowMax, Palette.RailSelectedFill, RailRowRounding);
        }
        if (Active)
        {
            // The accent bar marking the open band sits flush to the rail's left edge rather than inside the row fill, so the eye
            // reads it as an index marker on the column rather than as part of the row.
            const float MarkerMidY = (RowMin.y + RowMax.y) * 0.5f;
            Canvas->AddRectFilled(ImVec2(Origin.x, MarkerMidY - RailMarkerHeight * 0.5f),
                                  ImVec2(Origin.x + RailMarkerWidth, MarkerMidY + RailMarkerHeight * 0.5f),
                                  Palette.Accent, RailMarkerWidth);
        }

        const float RowMidY     = (RowMin.y + RowMax.y) * 0.5f;
        const ImU32 CaptionTint = Active ? Palette.Ink : Palette.Muted;

        InscribeToolGlyph(Icons, Band.GlyphName,
                          ImVec2(RowMin.x + 6.0f, RowMidY - RailGlyphEdge * 0.5f), RailGlyphEdge, false);

        // 📝 The trailing figure is the band's LIVE count, not its total: a reader picking a family wants to know what will work on
        //    this selection, and the total is already implied by the tiles they are about to see. A single-shot row shows its
        //    accelerator instead — it has no tiles to count.
        char TrailingText[16] = {};
        if (Row.SingleShot)
        {
            std::snprintf(TrailingText, sizeof(TrailingText), "%s", (Band.Keystroke != nullptr) ? Band.Keystroke : "");
        }
        else
        {
            std::snprintf(TrailingText, sizeof(TrailingText), "%d", Row.LiveTally);
        }

        float TrailingWidth = 0.0f;
        if (TrailingText[0] != '\0')
        {
            const ImVec2 TrailingSize = ImGui::CalcTextSize(TrailingText);
            TrailingWidth             = TrailingSize.x;
            Canvas->AddText(ImVec2(RowMax.x - TrailingInset - TrailingSize.x, RowMidY - TrailingSize.y * 0.5f),
                            Palette.Faint, TrailingText);
        }

        // The caption is clipped to the room the trailing figure leaves, so a long band name can never overrun the count beside it.
        const float CaptionX = RowMin.x + RailGlyphGutter;
        Canvas->PushClipRect(ImVec2(CaptionX, RowMin.y),
                             ImVec2(RowMax.x - TrailingWidth - CaptionGap, RowMax.y), true);
        Canvas->AddText(ImVec2(CaptionX, RowMidY - ImGui::GetTextLineHeight() * 0.5f), CaptionTint, Band.Caption);
        Canvas->PopClipRect();

        CursorY = RowMax.y;
    }

    return Outcome;
}

} // namespace Frontier
