/*==============================================================================================================================================
                                                    WORKSPACECONTEXTCONSOLEPANE.CPP
==============================================================================================================================================*/
// 🧩 Composes the console. The carousel is the substance: the prototype translates a 200%-wide track by -50%, which in a draw list becomes both
//    slides drawn at a shared horizontal offset and clipped to the surface. Everything else is fixed chrome — two pinned headers on one continuous
//    band, two footers, and the eased scroll every body shares. Ported from ToolCard's ToolCardShell with the gate lifted out: where the card held a
//    StratumBit/StratumName and asked ResolveTileAvailability itself, this holds neither and routes every standing question through Descriptor.Gate.
//    The two hand-written bezier solves the card carried are gone; both curves are read from Motion/Easing/EvaluateEasing.

#include "WorkspaceContextConsolePane.h"

#include "RailStrip.h"
#include "ActionStrip.h"
#include "CarouselOffset.h"
#include "ProbeStrip.h"
#include "../Descriptor/ActionDescriptor.h"
#include "../Rasterization/PaneHeaderRasterization.h"
#include "../../../Motion/Easing/EasingProfile.h"

#include <cmath>
#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float FooterPadding     = 11.0f;   // [px] - .opt-foot padding
    constexpr float FooterButtonHigh  = 28.0f;   // [px] - .obtn height
    constexpr float FooterButtonPadX  = 14.0f;   // [px] - .obtn padding
    constexpr float FooterButtonRound =  8.0f;   // [px] - .obtn radius
    constexpr float FooterGap         =  7.0f;   // [px] - .opt-foot gap
    constexpr float GridFootPadding   = 13.0f;   // [px] - .grid-foot padding

    constexpr float ScrollEaseRate    = 18.0f;   // [1/s]- scroll ease-out; higher is snappier
    constexpr float RailWheelStep     = 80.0f;   // [px] - rail travel per wheel notch
    constexpr float BodyWheelStep     = 46.0f;   // [px] - grid/options travel per notch, about one tile row

    constexpr ImU32 CommitInk         = IM_COL32(0x0B, 0x0B, 0x0D, 0xFF);   // .obtn.go colour

    // 📝 The prototype's two easing curves, now read from the shared solver rather than hand-inlined. The carousel slide is cubic-bezier(.5,.05,.2,1)
    //    and the open-pop is cubic-bezier(.16,1,.3,1); both are the same four numbers the CSS carries, evaluated through EvaluateEasing.
    constexpr EasingProfile CarouselEase{ 0.5f,  0.05f, 0.2f, 1.0f };
    constexpr EasingProfile OpenPopEase { 0.16f, 1.0f,  0.3f, 1.0f };

    // 📝 Wheel scrolling with an ease-out, shared by all three scrolling bodies. ImGui's own scroll snaps; the prototype's panes have momentum, and a
    //    console whose bodies scroll differently from each other reads as several widgets rather than one surface.
    void ApplyEasedScroll(float WheelStep)
    {
        ImGuiStorage* const Storage   = ImGui::GetStateStorage();
        const ImGuiID       TargetKey = ImGui::GetID("##scroll-target");

        float Target = Storage->GetFloat(TargetKey, ImGui::GetScrollY());
        if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
        {
            Target -= ImGui::GetIO().MouseWheel * WheelStep;
        }
        const float Ceiling = ImGui::GetScrollMaxY();
        Target = (Target < 0.0f) ? 0.0f : ((Target > Ceiling) ? Ceiling : Target);
        Storage->SetFloat(TargetKey, Target);

        const float Current = ImGui::GetScrollY();
        const float Eased   = Current + (Target - Current)
                            * (1.0f - std::exp(-ScrollEaseRate * ImGui::GetIO().DeltaTime));
        if (std::fabs(Target - Current) > 0.25f)
        {
            ImGui::SetScrollY(Eased);
        }
    }

    // 📝 True while a body is still travelling — the condition hover-to-swap is muted under, so a rail row does not fire simply because it slid beneath
    //    a still pointer.
    bool BodyIsScrolling()
    {
        const float Target = ImGui::GetStateStorage()->GetFloat(ImGui::GetID("##scroll-target"), ImGui::GetScrollY());
        return std::fabs(Target - ImGui::GetScrollY()) > 0.5f;
    }

    // 📝 One footer button. Returns whether it was pressed. Ported unchanged from the card; the only edit is ToolCardPalette → PaletteSpecification.
    bool InscribeFooterButton(ImDrawList* Canvas, const char* Text, ImVec2 Minimum, bool Emphasised,
                              const PaletteSpecification& Palette)
    {
        const ImVec2 TextSize = ImGui::CalcTextSize(Text);
        const float  Width    = TextSize.x + FooterButtonPadX * 2.0f;
        const ImVec2 Maximum(Minimum.x + Width, Minimum.y + FooterButtonHigh);

        ImGui::SetCursorScreenPos(Minimum);
        ImGui::PushID(Text);
        const bool Pressed = ImGui::InvisibleButton("##obtn", ImVec2(Width, FooterButtonHigh));
        const bool Hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        ImU32 Ground  = Emphasised ? Palette.Accent : Palette.TileFill;
        ImU32 TextInk = Emphasised ? CommitInk : Palette.Muted;
        if (Hovered)
        {
            Ground  = Emphasised ? IM_COL32(0x6F, 0x9B, 0xFF, 0xFF) : Palette.TileHoverFill;
            TextInk = Emphasised ? CommitInk : Palette.Ink;
        }

        Canvas->AddRectFilled(Minimum, Maximum, Ground, FooterButtonRound);
        Canvas->AddText(ImVec2(Minimum.x + FooterButtonPadX, (Minimum.y + Maximum.y) * 0.5f - TextSize.y * 0.5f),
                        TextInk, Text);
        return Pressed;
    }


    // 📝 The footer's left-hand note, drawn from Origin rightward. When AccentFigures is set, the run is split at digit boundaries so the numbers
    //    carry the accent ink and the words around them stay faint — the prototype's `.spend b{ color:var(--accent) }`, reproduced by walking runs
    //    because one AddText cannot change colour mid-string. Faint throughout otherwise. Returns nothing; the caller has already clipped.
    void InscribeFooterNote(ImDrawList* Canvas, ImVec2 Origin, const char* Note, bool AccentFigures,
                            const PaletteSpecification& Palette)
    {
        if (Note == nullptr || Note[0] == '\0')
            return;

        if (!AccentFigures)
        {
            Canvas->AddText(Origin, Palette.Faint, Note);
            return;
        }

        auto IsDigit = [](char C) { return C >= '0' && C <= '9'; };

        float PenX = Origin.x;
        const char* RunStart = Note;
        bool RunIsDigit = IsDigit(Note[0]);
        for (const char* Cursor = Note; ; ++Cursor)
        {
            const bool AtEnd = (*Cursor == '\0');
            // A run ends where the digit/non-digit character type flips, or at the terminator; flush the run that just closed.
            if (AtEnd || IsDigit(*Cursor) != RunIsDigit)
            {
                if (Cursor != RunStart)
                {
                    Canvas->AddText(ImVec2(PenX, Origin.y), RunIsDigit ? Palette.Accent : Palette.Faint,
                                    RunStart, Cursor);
                    PenX += ImGui::CalcTextSize(RunStart, Cursor).x;
                }
                if (AtEnd)
                    break;
                RunStart   = Cursor;
                RunIsDigit = IsDigit(*Cursor);
            }
        }
    }

    // 📝 The parameter table the open focus points at, and the label/glyph that names it. A single-shot cluster carries its own rows on its lone
    //    action, so "which action is open" resolves to a cluster's own action OR a chosen action within it — resolved in one place because both the
    //    options header and the options body need the same answer. Ported from ToolCard's ResolveOpenTool, keyed by (Cluster, Action).
    struct OpenAction
    {
        const ParameterDescriptor* Parameters     = nullptr;
        int                        ParameterCount = 0;
        const char*                Label          = nullptr;
        const char*                GlyphName      = nullptr;
        bool                       Bound          = false;
    };

    OpenAction ResolveOpenAction(const WorkspaceContextConsoleDescriptor& Descriptor, const ConsoleFocus& Focus)
    {
        OpenAction Open = {};
        if (Descriptor.Clusters == nullptr || Focus.OpenCluster < 0 || Focus.OpenCluster >= Descriptor.ClusterCount)
        {
            return Open;
        }

        const ClusterDescriptor& Cluster = Descriptor.Clusters[Focus.OpenCluster];

        // A single-shot cluster (no actions) opens its own lone parameter rows, named by the cluster itself — the prototype gives even a
        // no-parameter command its confirm footer, so a single-shot row still opens the options slide rather than firing outright.
        if (Cluster.Actions == nullptr || Cluster.ActionCount <= 0)
        {
            Open.Label     = Cluster.Caption;
            Open.GlyphName = Cluster.GlyphName;
            Open.Bound     = true;
            return Open;
        }

        if (Focus.OpenAction >= 0 && Focus.OpenAction < Cluster.ActionCount)
        {
            const ActionDescriptor& Action = Cluster.Actions[Focus.OpenAction];
            Open.Parameters     = Action.Parameters;
            Open.ParameterCount = Action.ParameterCount;
            Open.Label          = Action.Label;
            Open.GlyphName      = Action.GlyphName;
            Open.Bound          = true;
        }
        return Open;
    }

    // 📝 The open cluster, but only when it drives a grid (has actions). A single-shot cluster returns null here: its grid slide shows nothing, it
    //    opens straight to its options. Shared by the grid header and the grid body.
    const ClusterDescriptor* ResolveGridCluster(const WorkspaceContextConsoleDescriptor& Descriptor, int OpenCluster)
    {
        if (Descriptor.Clusters != nullptr && OpenCluster >= 0 && OpenCluster < Descriptor.ClusterCount
            && Descriptor.Clusters[OpenCluster].Actions != nullptr)
        {
            return &Descriptor.Clusters[OpenCluster];
        }
        return nullptr;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void AdvanceConsoleCarousel(ConsoleCarousel& Carousel, float DeltaSeconds, const MetricsSpecification& Metrics)
{
    Carousel.OpenAge += DeltaSeconds;

    const float Target = Carousel.ShowingOptions ? 1.0f : 0.0f;
    if (Carousel.Travel == Target)
    {
        return;
    }

    // 📝 Advanced in LINEAR progress and shaped by the bezier at read time. Easing the stored value itself would compose the curve with itself every
    //    frame, so a slide interrupted mid-travel would accelerate differently from one started at rest.
    const float Step = (Metrics.CarouselSeconds > 0.0f) ? (DeltaSeconds / Metrics.CarouselSeconds) : 1.0f;
    if (Target > Carousel.Travel)
    {
        Carousel.Travel += Step;
        if (Carousel.Travel > 1.0f) { Carousel.Travel = 1.0f; }
    }
    else
    {
        Carousel.Travel -= Step;
        if (Carousel.Travel < 0.0f) { Carousel.Travel = 0.0f; }
    }
}


ConsoleResult ConstructWorkspaceContextConsole(const SvgIconRegistry*                    Icons,
                                               const WorkspaceContextConsoleDescriptor& Descriptor,
                                               ImVec2                                   AnchorPosition,
                                               ConsoleFocus&                            Focus,
                                               ConsoleCarousel&                         Carousel,
                                               ParameterBlock&                          Parameters,
                                               const ThemeConfiguration&                Theme)
{
    ConsoleResult Result   = {};
    Result.ActivatedCluster = -1;
    Result.ActivatedAction  = -1;

    const PaletteSpecification  Palette = ResolveConsolePalette(Theme);
    // The theme-resolved geometry, unless the workspace supplied its own. Taken by value either way so every read below is one name.
    const MetricsSpecification  Metrics = (Descriptor.Metrics != nullptr) ? *Descriptor.Metrics : ResolveConsoleMetrics(Theme);

    AdvanceConsoleCarousel(Carousel, ImGui::GetIO().DeltaTime, Metrics);

    // The open-pop: the surface scales up from .97 and settles, over OpenSeconds. Applied as an alpha and a scale about the surface's own centre.
    const float PopProgress = (Metrics.OpenSeconds > 0.0f)
                            ? EvaluateEasing(OpenPopEase, Carousel.OpenAge / Metrics.OpenSeconds)
                            : 1.0f;
    const float PopScale    = 0.97f + 0.03f * PopProgress;
    const float PopLift     = -4.0f * (1.0f - PopProgress);

    const float CardWidth  = Metrics.CardWidth * PopScale;
    const float CardHeight = Metrics.CardHeight * PopScale;

    ImGui::SetNextWindowPos(ImVec2(AnchorPosition.x + (Metrics.CardWidth - CardWidth) * 0.5f,
                                   AnchorPosition.y + (Metrics.CardHeight - CardHeight) * 0.5f + PopLift));
    ImGui::SetNextWindowSize(ImVec2(CardWidth, CardHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Metrics.CardRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Palette.CardFill);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::SetNextWindowBgAlpha(PopProgress);

    const ImGuiWindowFlags CardFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                                     | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
                                     | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                                     | ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("##workspace-context-console", nullptr, CardFlags))
    {
        ImDrawList* const Canvas = ImGui::GetWindowDrawList();
        const ImVec2      Origin = ImGui::GetWindowPos();

        const float LeftWidth  = Metrics.LeftColumnWidth * PopScale;
        const float RightWidth = CardWidth - LeftWidth - 1.0f;
        const float HeaderHigh = Metrics.HeaderHeight * PopScale;
        const float HeaderBase = Origin.y + HeaderHigh;

        // 🔴 The slide offset. The prototype's track is 200% wide and translates by -50%; here that is one horizontal offset applied to BOTH slides,
        //    which sit a CardWidth apart and are clipped to the surface. Both are drawn while mid-travel — culling the outgoing slide makes the
        //    console appear to blank and repopulate instead of to slide. ResolveCarouselOffset states the seam so neither slide re-derives it.
        const float Shaped     = EvaluateEasing(CarouselEase, Carousel.Travel);
        const float FirstShift  = ResolveCarouselOffset(ConsoleSlide::FirstSlide,  Shaped, Metrics) * PopScale;
        const float SecondShift = ResolveCarouselOffset(ConsoleSlide::SecondSlide, Shaped, Metrics) * PopScale;

        Canvas->PushClipRect(Origin, ImVec2(Origin.x + CardWidth, Origin.y + CardHeight), true);

        //================================================== SLIDE ONE — RAIL + GRID ==================================================
        {
            const float SlideX = Origin.x + FirstShift;

            const ConsoleRailPlan Plan = ResolveConsoleRailPlan(Descriptor.Clusters, Descriptor.ClusterCount,
                                                                Descriptor.Gate, Metrics);

            // ---- rail header: name over the filtered count, tally pill; no stratum badge (the gate is the workspace's now) ----
            {
                char RailPill[24] = {};
                std::snprintf(RailPill, sizeof(RailPill), "%d", Plan.RowCount);

                ConsolePaneHeader Head = {};
                Head.Title     = "Actions";
                Head.TallyText = RailPill;
                InscribeConsolePaneHeader(Palette, Metrics, Head, ImVec2(SlideX, Origin.y), LeftWidth);
            }

            // ---- rail body ----
            const float RailBodyHigh = CardHeight - HeaderHigh;
            ImGui::SetCursorScreenPos(ImVec2(SlideX, HeaderBase));
            ImGui::BeginChild("##rail-body", ImVec2(LeftWidth, RailBodyHigh), false,
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                              | ImGuiWindowFlags_NoScrollWithMouse);
            {
                ApplyEasedScroll(RailWheelStep);
                const bool Scrolling = BodyIsScrolling();

                const ConsoleRailOutcome Outcome =
                    InscribeRailStrip(Icons, Descriptor.Clusters, Plan, Focus.OpenCluster,
                                      ImVec2(SlideX, HeaderBase - ImGui::GetScrollY()), LeftWidth,
                                      Palette, Metrics, Scrolling);

                if (Outcome.ActivatedCluster >= 0)
                {
                    if (Outcome.SingleShotFired)
                    {
                        // A single-shot cluster opens the options slide on its own rows rather than firing outright — the prototype gives even a
                        // no-parameter command its confirm footer, so a destructive click is never one click.
                        Focus.OpenCluster       = Outcome.ActivatedCluster;
                        Focus.OpenAction        = -1;
                        Carousel.ShowingOptions = true;
                    }
                    else
                    {
                        Focus.OpenCluster = Outcome.ActivatedCluster;
                    }
                }

                ImGui::SetCursorScreenPos(ImVec2(SlideX, HeaderBase));
                ImGui::Dummy(ImVec2(1.0f, Plan.ContentHeight));
            }
            ImGui::EndChild();

            // ---- grid header: cluster glyph on its black tile, caption over the live/gated split, tally pill ----
            const float GridX = SlideX + LeftWidth + 1.0f;
            Canvas->AddLine(ImVec2(SlideX + LeftWidth, Origin.y), ImVec2(SlideX + LeftWidth, Origin.y + CardHeight),
                            Palette.Hairline, 1.0f);

            const ClusterDescriptor* const GridCluster = ResolveGridCluster(Descriptor, Focus.OpenCluster);

            if (GridCluster != nullptr)
            {
                // The rail plan already resolved this cluster's tallies; reuse them rather than re-walking the gate for the header.
                int Applicable = 0;
                int Live       = 0;
                for (int RowIndex = 0; RowIndex < Plan.RowCount; ++RowIndex)
                {
                    if (Plan.Rows[RowIndex].ClusterIndex == Focus.OpenCluster)
                    {
                        Applicable = Plan.Rows[RowIndex].ApplicableTally;
                        Live       = Plan.Rows[RowIndex].LiveTally;
                        break;
                    }
                }

                char GridPill[24] = {};
                std::snprintf(GridPill, sizeof(GridPill), "%d actions", Applicable);

                char GridSubtitle[72] = {};
                if (Applicable - Live > 0)
                {
                    std::snprintf(GridSubtitle, sizeof(GridSubtitle), "%d of %d available  -  %d gated",
                                  Live, Applicable, Applicable - Live);
                }
                else
                {
                    std::snprintf(GridSubtitle, sizeof(GridSubtitle), "%d of %d available", Live, Applicable);
                }

                ConsolePaneHeader Head = {};
                Head.Title     = GridCluster->Caption;
                Head.Subtitle  = GridSubtitle;
                Head.TallyText = GridPill;
                InscribeConsolePaneHeader(Palette, Metrics, Head, ImVec2(GridX, Origin.y), RightWidth);
            }
            else
            {
                ConsolePaneHeader Head = {};
                Head.Title = "";
                InscribeConsolePaneHeader(Palette, Metrics, Head, ImVec2(GridX, Origin.y), RightWidth);
            }

            // ---- grid body + pinned tally footer ----
            const float GridFootHigh = Metrics.GridFootHeight * PopScale;
            const float GridFootTop  = Origin.y + CardHeight - GridFootHigh;

            Canvas->AddRectFilled(ImVec2(GridX, HeaderBase), ImVec2(GridX + RightWidth, GridFootTop), Palette.PaneFill);

            ImGui::SetCursorScreenPos(ImVec2(GridX, HeaderBase));
            ImGui::BeginChild("##grid-body", ImVec2(RightWidth, GridFootTop - HeaderBase), false,
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                              | ImGuiWindowFlags_NoScrollWithMouse);
            {
                ApplyEasedScroll(BodyWheelStep);

                // 🔴 A new cluster starts at the top. Carried-over scroll would open the grid part-way down a set the reader has not seen the start
                //    of — and because the clusters differ in length, the same offset means a different thing in each.
                ImGuiStorage* const State      = ImGui::GetStateStorage();
                const ImGuiID       ClusterKey = ImGui::GetID("##shown-cluster");
                if (State->GetInt(ClusterKey, -1) != Focus.OpenCluster)
                {
                    State->SetInt(ClusterKey, Focus.OpenCluster);
                    ImGui::SetScrollY(0.0f);
                    State->SetFloat(ImGui::GetID("##scroll-target"), 0.0f);
                }

                if (GridCluster != nullptr)
                {
                    const ConsoleActionOutcome Outcome =
                        InscribeActionStrip(Icons, *GridCluster, Descriptor.Gate, Descriptor.Surfaces,
                                            ImVec2(GridX, HeaderBase - ImGui::GetScrollY()), RightWidth, PopScale,
                                            Palette, Metrics);

                    if (Outcome.ActivatedAction >= 0)
                    {
                        Focus.OpenAction        = Outcome.ActivatedAction;
                        Carousel.ShowingOptions = true;
                    }

                    ImGui::SetCursorScreenPos(ImVec2(GridX, HeaderBase));
                    ImGui::Dummy(ImVec2(1.0f, MeasureActionStripHeight(*GridCluster, Descriptor.Gate,
                                                                       RightWidth, Metrics)));
                }
            }
            ImGui::EndChild();

            // The tally strip is pinned BELOW the scroll, not inside it: a tally that scrolls out of view is a tally nobody reads.
            Canvas->AddRectFilled(ImVec2(GridX, GridFootTop), ImVec2(GridX + RightWidth, Origin.y + CardHeight),
                                  Palette.PaneFill, Metrics.CardRounding, ImDrawFlags_RoundCornersBottomRight);
            Canvas->AddLine(ImVec2(GridX, GridFootTop), ImVec2(GridX + RightWidth, GridFootTop), Palette.Hairline, 1.0f);
            {
                const char* const FooterText = (GridCluster != nullptr) ? GridCluster->Caption : "";
                Canvas->AddText(ImVec2(GridX + GridFootPadding,
                                       GridFootTop + GridFootHigh * 0.5f - ImGui::GetTextLineHeight() * 0.5f),
                                Palette.Faint, FooterText);
            }
        }

        //=============================================== SLIDE TWO — READOUT + OPTIONS ===============================================
        {
            const float SlideX      = Origin.x + SecondShift;
            const OpenAction Open    = ResolveOpenAction(Descriptor, Focus);

            BindParameterBlock(Parameters, Open.Parameters, Open.ParameterCount, Focus.OpenCluster, Focus.OpenAction);

            // ---- readout header: the back row, INSIDE the shared band so the header line stays continuous across both slides ----
            {
                // The probe names what it is measuring ("Face", "12 faces"); with no probe there is nothing being measured, so the band falls back to
                // the generic word rather than asserting a subject the workspace never supplied.
                ConsolePaneHeader Head = {};
                Head.Title     = (Descriptor.Probe != nullptr && Descriptor.Probe->Identity != nullptr)
                               ? Descriptor.Probe->Identity : "Measurements";
                Head.IsBackRow = true;
                if (InscribeConsolePaneHeader(Palette, Metrics, Head, ImVec2(SlideX, Origin.y), LeftWidth))
                {
                    Carousel.ShowingOptions = false;
                }
            }

            // ---- readout body ----
            // 📝 The ground is laid FIRST and unconditionally, then the readout is drawn over it. A workspace that supplies no probe (construction's
            //    gate measures nothing) leaves the column as that bare ground rather than an empty readout frame — which is why the null test guards
            //    only the strip, not the fill.
            Canvas->AddRectFilled(ImVec2(SlideX, HeaderBase), ImVec2(SlideX + LeftWidth, Origin.y + CardHeight),
                                  Palette.CardFill, Metrics.CardRounding, ImDrawFlags_RoundCornersBottomLeft);

            // 🔴 A bound column painter REPLACES the probe strip rather than drawing over it: the two are alternative answers to "what is this column
            //    for", and paint's stroke preview needs the whole height. Checked first so a workspace binding both never double-paints.
            if (Descriptor.Surfaces.PaintColumn != nullptr)
            {
                SurfaceRegion ColumnRegion = {};
                ColumnRegion.Minimum  = ImVec2(SlideX, HeaderBase);
                ColumnRegion.Maximum  = ImVec2(SlideX + LeftWidth, Origin.y + CardHeight);
                ColumnRegion.PopScale = PopScale;
                Descriptor.Surfaces.PaintColumn(ColumnRegion, Palette, Descriptor.Surfaces.Context);
            }
            else if (Descriptor.Probe != nullptr)
            {
                // The readout scrolls in its own child, like the action grid: a long section list must not push the options column's footer around.
                ImGui::SetCursorScreenPos(ImVec2(SlideX, HeaderBase));
                ImGui::BeginChild("##console-readout-body", ImVec2(LeftWidth, Origin.y + CardHeight - HeaderBase), false,
                                  ImGuiWindowFlags_NoScrollbar);
                InscribeProbeStrip(Icons, Descriptor.Probe, ImVec2(SlideX, HeaderBase), LeftWidth, Palette, Metrics);

                // Reserve the measured height so the child's scroll range matches what the strip actually painted.
                ImGui::SetCursorScreenPos(ImVec2(SlideX, HeaderBase));
                ImGui::Dummy(ImVec2(1.0f, MeasureProbeStripHeight(Descriptor.Probe, LeftWidth, Metrics)));
                ImGui::EndChild();
            }

            // ---- options header: the open action's glyph, name over the cluster it belongs to ----
            const float OptionsX = SlideX + LeftWidth + 1.0f;
            Canvas->AddLine(ImVec2(SlideX + LeftWidth, Origin.y), ImVec2(SlideX + LeftWidth, Origin.y + CardHeight),
                            Palette.Hairline, 1.0f);

            if (Open.Bound)
            {
                char OptionsPill[24] = {};
                std::snprintf(OptionsPill, sizeof(OptionsPill), "%d", Open.ParameterCount);

                const char* const ClusterCaption =
                    (Focus.OpenCluster >= 0 && Focus.OpenCluster < Descriptor.ClusterCount)
                        ? Descriptor.Clusters[Focus.OpenCluster].Caption : "";

                ConsolePaneHeader Head = {};
                Head.Title       = Open.Label;
                Head.Subtitle    = ClusterCaption;
                Head.TallyText   = OptionsPill;
                Head.TallyAccent = true;
                InscribeConsolePaneHeader(Palette, Metrics, Head, ImVec2(OptionsX, Origin.y), RightWidth);
            }
            else
            {
                ConsolePaneHeader Head = {};
                Head.Title = "";
                InscribeConsolePaneHeader(Palette, Metrics, Head, ImVec2(OptionsX, Origin.y), RightWidth);
            }

            // ---- options body + Apply/Cancel footer ----
            const float OptionsFootHigh = Metrics.OptionsFootHeight * PopScale;
            const float OptionsFootTop  = Origin.y + CardHeight - OptionsFootHigh;

            Canvas->AddRectFilled(ImVec2(OptionsX, HeaderBase), ImVec2(OptionsX + RightWidth, OptionsFootTop),
                                  Palette.PaneFill);

            ImGui::SetCursorScreenPos(ImVec2(OptionsX, HeaderBase));
            ImGui::BeginChild("##options-body", ImVec2(RightWidth, OptionsFootTop - HeaderBase), false,
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                              | ImGuiWindowFlags_NoScrollWithMouse);
            {
                ApplyEasedScroll(BodyWheelStep);

                // A newly opened action starts at the top of its own options, for the same reason a new cluster does.
                ImGuiStorage* const State     = ImGui::GetStateStorage();
                const ImGuiID       ActionKey = ImGui::GetID("##shown-action");
                const int           ActionTag = Focus.OpenCluster * 1024 + Focus.OpenAction;
                if (State->GetInt(ActionKey, -99999) != ActionTag)
                {
                    State->SetInt(ActionKey, ActionTag);
                    ImGui::SetScrollY(0.0f);
                    State->SetFloat(ImGui::GetID("##scroll-target"), 0.0f);
                }

                if (Open.Bound && Open.ParameterCount > 0)
                {
                    if (InscribeParameterPane(Icons, Open.Parameters, Open.ParameterCount, Parameters,
                                              ImVec2(OptionsX, HeaderBase - ImGui::GetScrollY()), RightWidth,
                                              OptionsFootTop, Palette, Metrics))
                    {
                        Result.ReadingChanged = true;
                    }

                    ImGui::SetCursorScreenPos(ImVec2(OptionsX, HeaderBase));
                    ImGui::Dummy(ImVec2(1.0f, MeasureParameterPaneHeight(Open.Parameters, Open.ParameterCount, Metrics)));
                }
                else if (Open.Bound)
                {
                    // An action that genuinely takes no options SAYS so. An empty pane reads as a load failure.
                    InscribeParameterVoidNote(Icons, Open.GlyphName, Open.Label,
                                              ImVec2(OptionsX, HeaderBase), RightWidth,
                                              OptionsFootTop - HeaderBase, Palette);
                }
            }
            ImGui::EndChild();

            Canvas->AddRectFilled(ImVec2(OptionsX, OptionsFootTop),
                                  ImVec2(OptionsX + RightWidth, Origin.y + CardHeight),
                                  Palette.PaneFill, Metrics.CardRounding, ImDrawFlags_RoundCornersBottomRight);
            Canvas->AddLine(ImVec2(OptionsX, OptionsFootTop), ImVec2(OptionsX + RightWidth, OptionsFootTop),
                            Palette.Hairline, 1.0f);
            {
                const float FootMidY = OptionsFootTop + OptionsFootHigh * 0.5f;

                // The workspace names both buttons and the note beside them; a workspace that says nothing gets Cancel/Apply over the open label.
                const char* const CommitCaption = Descriptor.Footer.CommitCaption ? Descriptor.Footer.CommitCaption : "Apply";
                const char* const RevertCaption = Descriptor.Footer.RevertCaption ? Descriptor.Footer.RevertCaption : "Cancel";
                const char* const NoteText      = Descriptor.Footer.NoteText ? Descriptor.Footer.NoteText
                                                                             : (Open.Bound ? Open.Label : "");

                // Laid out from the right so the two buttons keep their positions whatever the note beside them says.
                const float CommitWidth = ImGui::CalcTextSize(CommitCaption).x + FooterButtonPadX * 2.0f;
                const float RevertWidth = ImGui::CalcTextSize(RevertCaption).x + FooterButtonPadX * 2.0f;
                const float CommitX     = OptionsX + RightWidth - FooterPadding - CommitWidth;
                const float RevertX     = CommitX - FooterGap - RevertWidth;

                Canvas->PushClipRect(ImVec2(OptionsX + FooterPadding, OptionsFootTop),
                                     ImVec2(RevertX - FooterGap, Origin.y + CardHeight), true);
                InscribeFooterNote(Canvas, ImVec2(OptionsX + FooterPadding, FootMidY - ImGui::GetTextLineHeight() * 0.5f),
                                   NoteText, Descriptor.Footer.AccentFigures, Palette);
                Canvas->PopClipRect();

                if (InscribeFooterButton(Canvas, RevertCaption, ImVec2(RevertX, FootMidY - FooterButtonHigh * 0.5f),
                                         false, Palette))
                {
                    Result.RevertRequested  = true;
                    Carousel.ShowingOptions = false;
                }
                if (InscribeFooterButton(Canvas, CommitCaption, ImVec2(CommitX, FootMidY - FooterButtonHigh * 0.5f),
                                         true, Palette))
                {
                    Result.ActivatedCluster = Focus.OpenCluster;
                    Result.ActivatedAction  = Focus.OpenAction;
                    Result.CommitRequested  = true;
                    Carousel.ShowingOptions = false;
                }
            }
        }

        Canvas->PopClipRect();

        // The surface border, drawn last so it crosses both slides rather than being painted over by either.
        Canvas->AddRect(Origin, ImVec2(Origin.x + CardWidth, Origin.y + CardHeight),
                        Palette.HairlineStrong, Metrics.CardRounding, 0, 1.0f);
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    return Result;
}

} // namespace Frontier
