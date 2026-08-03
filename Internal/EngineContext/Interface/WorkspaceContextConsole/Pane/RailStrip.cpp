/*==============================================================================================================================================
                                                              RAILSTRIP.CPP
==============================================================================================================================================*/
// 🧩 Resolves and draws the cluster rail. The resolve half is the interesting one: filtering rows out moves the rules that divide them, so the plan
//    is built in two passes — survivors first, then rules placed against the survivors rather than against the authored table. Ported from
//    ToolRailColumn, with the one substantive change the header calls out: standing comes from the VerdictBinding, never from a stratum mask read here.

#include "RailStrip.h"

#include "../Descriptor/ActionDescriptor.h"
#include "../Rasterization/GlyphRasterization.h"

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

    // 📝 The two counts a cluster's row reports, resolved through the gate rather than a mask. Applicable = actions that are not Omitted (the row
    //    survives if this is non-zero); Live = actions the gate reports runnable now (the trailing figure). A single-shot cluster carries one action,
    //    so the same loop judges it — SingleShot is decided by ActionCount, not by a separate mask.
    struct ClusterTally
    {
        int Applicable;
        int Live;
    };

    ClusterTally TallyCluster(const ClusterDescriptor& Cluster, const VerdictBinding& Gate)
    {
        ClusterTally Tally = {};
        for (int ActionIndex = 0; ActionIndex < Cluster.ActionCount; ++ActionIndex)
        {
            const ActionVerdict Verdict = ResolveActionVerdict(Gate, Cluster.Actions[ActionIndex]);
            if (Verdict.Standing == ActionStanding::Omitted)
            {
                continue;
            }
            ++Tally.Applicable;
            if (Verdict.Standing == ActionStanding::Live)
            {
                ++Tally.Live;
            }
        }
        return Tally;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ConsoleRailPlan ResolveConsoleRailPlan(const ClusterDescriptor*    Clusters,
                                       int                         ClusterCount,
                                       const VerdictBinding&       Gate,
                                       const MetricsSpecification& Metrics)
{
    ConsoleRailPlan Plan = {};
    if (Clusters == nullptr || ClusterCount <= 0)
    {
        return Plan;
    }

    // PASS ONE — which clusters earn a row.
    // 🔴 A cluster with no applicable action is OMITTED, not dimmed. This is the one place the hide-vs-grey rule inverts relative to the actions,
    //    and the empty selection is why: with nothing picked, thirteen modelling families have zero applicable tools, and dimming all thirteen
    //    would bury the four creation clusters that DO apply under a wall of dead rows. An action greys because its shortfall is actionable; a
    //    family that cannot apply in this context carries no such information.
    for (int ClusterIndex = 0; ClusterIndex < ClusterCount && Plan.RowCount < ConsoleRailRowLimit; ++ClusterIndex)
    {
        const ClusterDescriptor& Cluster    = Clusters[ClusterIndex];
        const bool               SingleShot = (Cluster.Actions == nullptr) || (Cluster.ActionCount <= 1);

        const ClusterTally Tally = TallyCluster(Cluster, Gate);
        if (Tally.Applicable == 0)
        {
            continue;
        }

        ConsoleRailRow& Row = Plan.Rows[Plan.RowCount++];
        Row.ClusterIndex    = ClusterIndex;
        Row.LiveTally       = Tally.Live;
        Row.ApplicableTally = Tally.Applicable;
        Row.SingleShot      = SingleShot;
        Row.RuleAbove       = false;
    }

    // PASS TWO — where the rules land.
    // 📝 A rule is authored above a cluster, but the surviving neighbour may not be that cluster. So the rule migrates: it belongs above the first
    //    survivor at or after each authored rule position. Walking the survivors and asking "was a rule authored anywhere in the gap since the
    //    previous survivor?" resolves both failure modes at once — a rule whose own cluster was dropped still divides the groups, and a rule with no
    //    survivor after it simply never gets placed. The FIRST row never takes a rule: a leading rule divides the list from nothing above it.
    for (int RowIndex = 1; RowIndex < Plan.RowCount; ++RowIndex)
    {
        const int GapFirst = Plan.Rows[RowIndex - 1].ClusterIndex + 1;   // first cluster after the previous survivor
        const int GapLast  = Plan.Rows[RowIndex].ClusterIndex;           // this survivor, inclusive

        bool RuleInGap = false;
        for (int ClusterIndex = GapFirst; ClusterIndex <= GapLast; ++ClusterIndex)
        {
            if (Clusters[ClusterIndex].SeparatorAbove)
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


ConsoleRailOutcome InscribeRailStrip(const SvgIconRegistry*      Icons,
                                     const ClusterDescriptor*    Clusters,
                                     const ConsoleRailPlan&      Plan,
                                     int                         OpenCluster,
                                     ImVec2                      Origin,
                                     float                       ColumnWidth,
                                     const PaletteSpecification& Palette,
                                     const MetricsSpecification& Metrics,
                                     bool                        ScrollSuppressesHover)
{
    ConsoleRailOutcome Outcome = {};
    Outcome.HoveredCluster     = -1;
    Outcome.ActivatedCluster   = -1;

    if (Clusters == nullptr || Plan.RowCount == 0)
    {
        return Outcome;
    }

    ImDrawList* const Canvas      = ImGui::GetWindowDrawList();
    const float       ColumnRight = Origin.x + ColumnWidth;
    float             CursorY     = Origin.y + RailBodyPadding;

    for (int RowIndex = 0; RowIndex < Plan.RowCount; ++RowIndex)
    {
        const ConsoleRailRow&    Row     = Plan.Rows[RowIndex];
        const ClusterDescriptor& Cluster = Clusters[Row.ClusterIndex];

        // The rule dividing the creation clusters from the modelling families, and those from the single-shot commands below. A mis-click into a
        // destructive command costs a deliberate crossing of it rather than a slip.
        if (Row.RuleAbove)
        {
            const float RuleY = CursorY + RuleHeight * 0.5f;
            Canvas->AddLine(ImVec2(Origin.x + RailBodyPadding * 2.0f, RuleY),
                            ImVec2(ColumnRight - RailBodyPadding * 2.0f, RuleY), Palette.Hairline, 1.0f);
            CursorY += RuleHeight;
        }

        const bool   Active = (!Row.SingleShot && Row.ClusterIndex == OpenCluster);
        const ImVec2 RowMin(Origin.x + RailBodyPadding, CursorY);
        const ImVec2 RowMax(ColumnRight - RailBodyPadding, CursorY + Metrics.RailRowHeight);

        ImGui::SetCursorScreenPos(RowMin);
        ImGui::PushID(Row.ClusterIndex);
        const bool Pressed = ImGui::InvisibleButton("##cluster", ImVec2(RowMax.x - RowMin.x, RowMax.y - RowMin.y));
        const bool Hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        if (Hovered)
        {
            Outcome.HoveredCluster = Row.ClusterIndex;
            // ⚠️ Hover-to-swap is muted while the rail scrolls, but a CLICK always acts. Under the wheel the pointer holds still while rows travel
            //    beneath it, so every row that passes would swap the grid and one throw would thrash through five clusters.
            if (!ScrollSuppressesHover && !Row.SingleShot)
            {
                Outcome.ActivatedCluster = Row.ClusterIndex;
                Outcome.SingleShotFired  = false;
            }
        }
        if (Pressed)
        {
            Outcome.ActivatedCluster = Row.ClusterIndex;
            Outcome.SingleShotFired  = Row.SingleShot;
        }

        if (Active || (Hovered && Row.SingleShot))
        {
            Canvas->AddRectFilled(RowMin, RowMax, Palette.RailSelectedFill, RailRowRounding);
        }
        if (Active)
        {
            // The accent bar marking the open cluster sits flush to the rail's left edge rather than inside the row fill, so the eye reads it as an
            // index marker on the column rather than as part of the row.
            const float MarkerMidY = (RowMin.y + RowMax.y) * 0.5f;
            Canvas->AddRectFilled(ImVec2(Origin.x, MarkerMidY - RailMarkerHeight * 0.5f),
                                  ImVec2(Origin.x + RailMarkerWidth, MarkerMidY + RailMarkerHeight * 0.5f),
                                  Palette.Accent, RailMarkerWidth);
        }

        const float RowMidY     = (RowMin.y + RowMax.y) * 0.5f;
        const ImU32 CaptionTint = Active ? Palette.Ink : Palette.Muted;

        InscribeConsoleGlyph(Icons, Cluster.GlyphName,
                             ImVec2(RowMin.x + 6.0f, RowMidY - RailGlyphEdge * 0.5f), RailGlyphEdge, false);

        // 📝 The trailing figure is the cluster's LIVE count, not its total: a reader picking a family wants to know what will work in this context,
        //    and the total is already implied by the actions they are about to see. A single-shot row shows its accelerator instead — it has no
        //    actions to count.
        char TrailingText[16] = {};
        if (Row.SingleShot)
        {
            std::snprintf(TrailingText, sizeof(TrailingText), "%s", (Cluster.Keystroke != nullptr) ? Cluster.Keystroke : "");
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

        // The caption is clipped to the room the trailing figure leaves, so a long cluster name can never overrun the count beside it.
        const float CaptionX = RowMin.x + RailGlyphGutter;
        Canvas->PushClipRect(ImVec2(CaptionX, RowMin.y),
                             ImVec2(RowMax.x - TrailingWidth - CaptionGap, RowMax.y), true);
        Canvas->AddText(ImVec2(CaptionX, RowMidY - ImGui::GetTextLineHeight() * 0.5f), CaptionTint, Cluster.Caption);
        Canvas->PopClipRect();

        CursorY = RowMax.y;
    }

    return Outcome;
}

} // namespace Frontier
