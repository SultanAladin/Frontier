/*==============================================================================================================================================
                                                        TOOLCARDSHELL.CPP
==============================================================================================================================================*/
// 🧩 Composes the card. The carousel is the substance here: the prototype translates a 200%-wide track by -50%, which in a draw list becomes both
//    slides drawn at a shared horizontal offset and clipped to the card. Everything else is the fixed chrome — two pinned headers on one continuous
//    band, two footers, and the eased scroll both bodies share.

#include "ToolCardShell.h"
#include "ToolGlyphInscription.h"
#include "ToolProbeColumn.h"
#include "ToolTileGrid.h"

#include <cmath>
#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float PanePadding      = 13.0f;   // [px] - .pane-head padding
    constexpr float HeaderGap        = 10.0f;   // [px] - .pane-head gap
    constexpr float HeaderTileEdge   = 24.0f;   // [px] - .pane-head .h-ic
    constexpr float HeaderTileRound  =  6.0f;   // [px] - .h-ic radius
    constexpr float HeaderGlyphInset = 17.0f;   // [px] - .grid-pane .h-ic svg, inset from the badge's full bleed
    constexpr float HeaderSubtitleGap =  3.0f;  // [px] - .h-txt .fn margin-top
    constexpr float PillPaddingX     =  9.0f;   // [px] - .h-n padding
    constexpr float PillPaddingY     =  4.0f;   // [px]
    constexpr float BackArrowEdge    = 16.0f;   // [px] - .pane-head .b-arrow
    constexpr float FooterPadding    = 11.0f;   // [px] - .opt-foot padding
    constexpr float FooterButtonHigh = 28.0f;   // [px] - .obtn height
    constexpr float FooterButtonPadX = 14.0f;   // [px] - .obtn padding
    constexpr float FooterButtonRound =  8.0f;  // [px] - .obtn radius
    constexpr float FooterGap        =  7.0f;   // [px] - .opt-foot gap
    constexpr float GridFootPadding  = 13.0f;   // [px] - .grid-foot padding

    constexpr float ScrollEaseRate   = 18.0f;   // [1/s]- scroll ease-out; higher is snappier
    constexpr float RailWheelStep    = 80.0f;   // [px] - rail travel per wheel notch
    constexpr float BodyWheelStep    = 46.0f;   // [px] - grid/options travel per notch, about one tile row

    constexpr ImU32 HeaderBackHover  = IM_COL32(0x29, 0x29, 0x30, 0xFF);   // .pane-head.back:hover
    constexpr ImU32 CommitInk        = IM_COL32(0x0B, 0x0B, 0x0D, 0xFF);   // .obtn.go colour

    // 📝 The prototype's two easing curves, evaluated directly. A cubic-bezier's Y at a given X needs the parameter solved for first,
    //    which Newton converges on in a handful of steps at this precision — cheaper and shorter than a sampled table, and it keeps the
    //    curve stated as the same four numbers the CSS carries.
    float SolveCubicBezier(float NormalisedTime, float FirstX, float FirstY, float SecondX, float SecondY)
    {
        if (NormalisedTime <= 0.0f) { return 0.0f; }
        if (NormalisedTime >= 1.0f) { return 1.0f; }

        const auto CurveAt = [](float Parameter, float First, float Second)
        {
            const float Inverse = 1.0f - Parameter;
            return 3.0f * Inverse * Inverse * Parameter * First
                 + 3.0f * Inverse * Parameter * Parameter * Second
                 + Parameter * Parameter * Parameter;
        };
        const auto SlopeAt = [](float Parameter, float First, float Second)
        {
            const float Inverse = 1.0f - Parameter;
            return 3.0f * Inverse * Inverse * First
                 + 6.0f * Inverse * Parameter * (Second - First)
                 + 3.0f * Parameter * Parameter * (1.0f - Second);
        };

        float Parameter = NormalisedTime;
        for (int Iteration = 0; Iteration < 6; ++Iteration)
        {
            const float Error = CurveAt(Parameter, FirstX, SecondX) - NormalisedTime;
            if (std::fabs(Error) < 1.0e-4f)
            {
                break;
            }
            const float Slope = SlopeAt(Parameter, FirstX, SecondX);
            if (std::fabs(Slope) < 1.0e-6f)
            {
                break;
            }
            Parameter -= Error / Slope;
            Parameter  = (Parameter < 0.0f) ? 0.0f : ((Parameter > 1.0f) ? 1.0f : Parameter);
        }
        return CurveAt(Parameter, FirstY, SecondY);
    }

    // 📝 Wheel scrolling with an ease-out, shared by all three scrolling bodies. ImGui's own scroll snaps; the prototype's panes have
    //    momentum, and a card whose two panes scroll differently from each other reads as two widgets.
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

    // 📝 True while a body is still travelling — the condition hover-to-swap is muted under.
    bool BodyIsScrolling()
    {
        const float Target = ImGui::GetStateStorage()->GetFloat(ImGui::GetID("##scroll-target"), ImGui::GetScrollY());
        return std::fabs(Target - ImGui::GetScrollY()) > 0.5f;
    }

    // 📝 The rounded count pill that closes a pane header, right-aligned in the strip it is given. Returns its left edge so the caption
    //    beside it can be clipped to the room left over.
    float InscribeHeaderPill(ImDrawList* Canvas, const char* Text, float RightEdge, float MidY,
                             ImU32 Ground, ImU32 TextInk)
    {
        const ImVec2 TextSize = ImGui::CalcTextSize(Text);
        const ImVec2 PillMax(RightEdge, MidY + TextSize.y * 0.5f + PillPaddingY);
        const ImVec2 PillMin(PillMax.x - TextSize.x - PillPaddingX * 2.0f, MidY - TextSize.y * 0.5f - PillPaddingY);
        Canvas->AddRectFilled(PillMin, PillMax, Ground, (PillMax.y - PillMin.y) * 0.5f);
        Canvas->AddText(ImVec2(PillMin.x + PillPaddingX, PillMin.y + PillPaddingY), TextInk, Text);
        return PillMin.x;
    }

    // 📝 Title over subtitle, the shape both headers share, clipped to the room the pill leaves.
    void InscribeHeaderText(ImDrawList* Canvas, const char* Title, const char* Subtitle,
                            float LeftEdge, float RightLimit, float MidY, const ToolCardPalette& Palette)
    {
        const float LineHeight = ImGui::GetTextLineHeight();
        const float BlockHigh  = LineHeight * 2.0f + HeaderSubtitleGap;

        Canvas->PushClipRect(ImVec2(LeftEdge, MidY - BlockHigh), ImVec2(RightLimit, MidY + BlockHigh), true);
        Canvas->AddText(ImVec2(LeftEdge, MidY - BlockHigh * 0.5f), Palette.Ink, (Title != nullptr) ? Title : "");
        if (Subtitle != nullptr)
        {
            Canvas->AddText(ImVec2(LeftEdge, MidY - BlockHigh * 0.5f + LineHeight + HeaderSubtitleGap),
                            Palette.Faint, Subtitle);
        }
        Canvas->PopClipRect();
    }

    // 📝 A header's ground plus its bottom hairline. Both panes call this with the SAME height, so the two headers land on one
    //    continuous band — a 31px header beside a 52px one reads as two stacked cards rather than one menu with two panes.
    void InscribeHeaderGround(ImDrawList* Canvas, ImVec2 Minimum, ImVec2 Maximum, float Rounding,
                              ImDrawFlags Corners, ImU32 Ground, ImU32 Hairline)
    {
        Canvas->AddRectFilled(Minimum, Maximum, Ground, Rounding, Corners);
        Canvas->AddLine(ImVec2(Minimum.x, Maximum.y), ImVec2(Maximum.x, Maximum.y), Hairline, 1.0f);
    }

    // 📝 One footer button. Returns whether it was pressed.
    bool InscribeFooterButton(ImDrawList* Canvas, const char* Text, ImVec2 Minimum, bool Emphasised,
                              const ToolCardPalette& Palette)
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

    // 📝 The back chevron the options slide's left header carries, drawn as two strokes rather than registered as a glyph — it is
    //    chrome belonging to the shell, not catalogue artwork, and registering it would put a shell concern in a workspace's icon pack.
    void InscribeBackChevron(ImDrawList* Canvas, ImVec2 Centre, float Edge, ImU32 Tint)
    {
        const float Reach = Edge * 0.28f;
        Canvas->AddLine(ImVec2(Centre.x + Reach * 0.6f, Centre.y - Reach),
                        ImVec2(Centre.x - Reach * 0.5f, Centre.y), Tint, 1.6f);
        Canvas->AddLine(ImVec2(Centre.x - Reach * 0.5f, Centre.y),
                        ImVec2(Centre.x + Reach * 0.6f, Centre.y + Reach), Tint, 1.6f);
    }

    // 📝 The parameter table the open selection points at, and the label/glyph that names it. A single-shot band carries its own rows,
    //    so "which tool is open" resolves to a band OR a tile — resolved in one place because four call sites need the same answer.
    struct OpenTool
    {
        const ToolParameterDescriptor* Parameters;
        int                            ParameterCount;
        const char*                    Label;
        const char*                    GlyphName;
        bool                           Bound;
    };

    OpenTool ResolveOpenTool(const ToolCardDescriptor& Descriptor, const ToolCardSelection& Selection)
    {
        OpenTool Open = {};
        if (Descriptor.Bands == nullptr || Selection.OpenBand < 0 || Selection.OpenBand >= Descriptor.BandCount)
        {
            return Open;
        }

        const ToolBandDescriptor& Band = Descriptor.Bands[Selection.OpenBand];
        if (Band.Tiles == nullptr)
        {
            Open.Parameters     = Band.Parameters;
            Open.ParameterCount = Band.ParameterCount;
            Open.Label          = Band.Caption;
            Open.GlyphName      = Band.GlyphName;
            Open.Bound          = true;
            return Open;
        }

        if (Selection.OpenTile >= 0 && Selection.OpenTile < Band.TileCount)
        {
            const ToolTileDescriptor& Tile = Band.Tiles[Selection.OpenTile];
            Open.Parameters     = Tile.Parameters;
            Open.ParameterCount = Tile.ParameterCount;
            Open.Label          = Tile.Label;
            Open.GlyphName      = Tile.GlyphName;
            Open.Bound          = true;
        }
        return Open;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void AdvanceToolCardCarousel(ToolCardCarousel& Carousel, float DeltaSeconds, const ToolCardMetrics& Metrics)
{
    Carousel.OpenAge += DeltaSeconds;

    const float Target = Carousel.ShowingOptions ? 1.0f : 0.0f;
    if (Carousel.Travel == Target)
    {
        return;
    }

    // 📝 Advanced in LINEAR progress and shaped by the bezier at read time. Easing the stored value itself would compose the curve with
    //    itself on every frame, so a slide interrupted mid-travel would accelerate differently from one started at rest.
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


ToolCardResult ConstructToolCard(const SvgIconRegistry*    Icons,
                                 const ToolCardDescriptor& Descriptor,
                                 ToolCardSelection&        Selection,
                                 ToolCardCarousel&         Carousel,
                                 ToolParameterBlock&       Parameters,
                                 const ThemeConfiguration& Theme)
{
    ToolCardResult Result = {};
    Result.ActivatedBand  = -1;
    Result.ActivatedTile  = -1;

    const ToolCardPalette Palette = ResolveToolCardPalette(Theme);
    const ToolCardMetrics Metrics = ResolveToolCardMetrics(Theme);

    AdvanceToolCardCarousel(Carousel, ImGui::GetIO().DeltaTime, Metrics);

    // The open-pop: the card scales up from .97 and settles, over .14s. Applied as an alpha and a scale about the card's own centre.
    const float PopProgress = (Metrics.OpenSeconds > 0.0f)
                            ? SolveCubicBezier(Carousel.OpenAge / Metrics.OpenSeconds, 0.16f, 1.0f, 0.3f, 1.0f)
                            : 1.0f;
    const float PopScale    = 0.97f + 0.03f * PopProgress;
    const float PopLift     = -4.0f * (1.0f - PopProgress);

    const float CardWidth  = Metrics.CardWidth * PopScale;
    const float CardHeight = Metrics.CardHeight * PopScale;

    ImGui::SetNextWindowPos(ImVec2(Descriptor.AnchorPosition.x + (Metrics.CardWidth - CardWidth) * 0.5f,
                                   Descriptor.AnchorPosition.y + (Metrics.CardHeight - CardHeight) * 0.5f + PopLift));
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

    if (ImGui::Begin(Descriptor.Identifier, nullptr, CardFlags))
    {
        ImDrawList* const Canvas = ImGui::GetWindowDrawList();
        const ImVec2      Origin = ImGui::GetWindowPos();

        const float LeftWidth  = Metrics.LeftColumnWidth * PopScale;
        const float RightWidth = CardWidth - LeftWidth - 1.0f;
        const float HeaderHigh = Metrics.HeaderHeight * PopScale;
        const float HeaderBase = Origin.y + HeaderHigh;
        const float HeaderMidY = Origin.y + HeaderHigh * 0.5f;

        // 🔴 The slide offset. The prototype's track is 200% wide and translates by -50%; here that is one horizontal offset applied to
        //    BOTH slides, which are drawn at ±CardWidth of each other and clipped to the card. Both are drawn while mid-travel — culling
        //    the outgoing slide makes the card appear to blank and repopulate instead of to slide.
        const float Shaped     = SolveCubicBezier(Carousel.Travel, 0.5f, 0.05f, 0.2f, 1.0f);
        const float SlideShift = -Shaped * CardWidth;

        Canvas->PushClipRect(Origin, ImVec2(Origin.x + CardWidth, Origin.y + CardHeight), true);

        //================================================== SLIDE ONE — RAIL + GRID ==================================================
        {
            const float SlideX = Origin.x + SlideShift;

            const ToolRailPlan Plan = ResolveToolRailPlan(Descriptor.Bands, Descriptor.BandCount,
                                                          Selection.StratumBit, Metrics);

            // ---- rail header: stratum badge, name over the count, tally pill ----
            InscribeHeaderGround(Canvas, ImVec2(SlideX, Origin.y), ImVec2(SlideX + LeftWidth, HeaderBase),
                                 Metrics.CardRounding, ImDrawFlags_RoundCornersTopLeft,
                                 Palette.RailSelectedFill, Palette.Hairline);
            {
                const ImVec2 TileMin(SlideX + PanePadding, HeaderMidY - HeaderTileEdge * 0.5f);
                Canvas->AddRectFilled(TileMin, ImVec2(TileMin.x + HeaderTileEdge, TileMin.y + HeaderTileEdge),
                                      IM_COL32(0, 0, 0, 255), HeaderTileRound);
                InscribeStratumBadge(Icons, Selection.StratumName, TileMin, HeaderTileEdge);

                char RailPill[24] = {};
                std::snprintf(RailPill, sizeof(RailPill), "%d", Plan.RowCount);
                const float PillLeft = InscribeHeaderPill(Canvas, RailPill, SlideX + LeftWidth - PanePadding,
                                                          HeaderMidY, Palette.GridFill, Palette.Muted);

                char RailSubtitle[48] = {};
                std::snprintf(RailSubtitle, sizeof(RailSubtitle), "%d selected", Selection.SelectedCount);
                InscribeHeaderText(Canvas, (Selection.StratumName != nullptr) ? Selection.StratumName : "Selection",
                                   RailSubtitle, TileMin.x + HeaderTileEdge + HeaderGap, PillLeft - 4.0f,
                                   HeaderMidY, Palette);
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

                const ToolRailOutcome Outcome =
                    InscribeToolRailColumn(Icons, Descriptor.Bands, Plan, Selection.OpenBand,
                                           ImVec2(SlideX, HeaderBase - ImGui::GetScrollY()), LeftWidth,
                                           Palette, Metrics, Scrolling);

                if (Outcome.ActivatedBand >= 0)
                {
                    if (Outcome.SingleShotFired)
                    {
                        // A single-shot row opens the options slide on its own rows rather than firing outright — the prototype gives
                        // even a no-parameter command its confirm footer, so a destructive click is never one click.
                        Selection.OpenBand      = Outcome.ActivatedBand;
                        Selection.OpenTile      = -1;
                        Carousel.ShowingOptions = true;
                    }
                    else
                    {
                        Selection.OpenBand = Outcome.ActivatedBand;
                    }
                }

                ImGui::SetCursorScreenPos(ImVec2(SlideX, HeaderBase));
                ImGui::Dummy(ImVec2(1.0f, Plan.ContentHeight));
            }
            ImGui::EndChild();

            // ---- grid header: band glyph on its black tile, caption over the live/gated split, tally pill ----
            const float GridX = SlideX + LeftWidth + 1.0f;
            InscribeHeaderGround(Canvas, ImVec2(GridX, Origin.y), ImVec2(GridX + RightWidth, HeaderBase),
                                 Metrics.CardRounding, ImDrawFlags_RoundCornersTopRight,
                                 Palette.RailSelectedFill, Palette.Hairline);
            Canvas->AddLine(ImVec2(SlideX + LeftWidth, Origin.y), ImVec2(SlideX + LeftWidth, Origin.y + CardHeight),
                            Palette.Hairline, 1.0f);

            const ToolBandDescriptor* OpenBandPtr = nullptr;
            if (Descriptor.Bands != nullptr && Selection.OpenBand >= 0 && Selection.OpenBand < Descriptor.BandCount
                && Descriptor.Bands[Selection.OpenBand].Tiles != nullptr)
            {
                OpenBandPtr = &Descriptor.Bands[Selection.OpenBand];
            }

            if (OpenBandPtr != nullptr)
            {
                const ImVec2 TileMin(GridX + PanePadding, HeaderMidY - HeaderTileEdge * 0.5f);
                Canvas->AddRectFilled(TileMin, ImVec2(TileMin.x + HeaderTileEdge, TileMin.y + HeaderTileEdge),
                                      IM_COL32(0, 0, 0, 255), HeaderTileRound);
                InscribeToolGlyph(Icons, OpenBandPtr->GlyphName,
                                  ImVec2(TileMin.x + (HeaderTileEdge - HeaderGlyphInset) * 0.5f,
                                         TileMin.y + (HeaderTileEdge - HeaderGlyphInset) * 0.5f),
                                  HeaderGlyphInset, false);

                const int Applicable = TallyApplicableTiles(*OpenBandPtr, Selection.StratumBit);
                const int Live       = TallyLiveTiles(*OpenBandPtr, Selection.StratumBit);

                char GridPill[24] = {};
                std::snprintf(GridPill, sizeof(GridPill), "%d tools", Applicable);
                const float PillLeft = InscribeHeaderPill(Canvas, GridPill, GridX + RightWidth - PanePadding,
                                                          HeaderMidY, Palette.GridFill, Palette.Muted);

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
                InscribeHeaderText(Canvas, OpenBandPtr->Caption, GridSubtitle,
                                   TileMin.x + HeaderTileEdge + HeaderGap, PillLeft - 4.0f, HeaderMidY, Palette);
            }

            // ---- grid body + pinned tally footer ----
            const float GridFootHigh = Metrics.GridFootHeight * PopScale;
            const float GridFootTop  = Origin.y + CardHeight - GridFootHigh;

            Canvas->AddRectFilled(ImVec2(GridX, HeaderBase), ImVec2(GridX + RightWidth, GridFootTop), Palette.GridFill);

            ImGui::SetCursorScreenPos(ImVec2(GridX, HeaderBase));
            ImGui::BeginChild("##grid-body", ImVec2(RightWidth, GridFootTop - HeaderBase), false,
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                              | ImGuiWindowFlags_NoScrollWithMouse);
            {
                ApplyEasedScroll(BodyWheelStep);

                // 🔴 A new band starts at the top. Carried-over scroll would open the grid part-way down a set the reader has not seen
                //    the start of — and because the bands differ in length, the same offset means a different thing in each.
                ImGuiStorage* const State   = ImGui::GetStateStorage();
                const ImGuiID       BandKey = ImGui::GetID("##shown-band");
                if (State->GetInt(BandKey, -1) != Selection.OpenBand)
                {
                    State->SetInt(BandKey, Selection.OpenBand);
                    ImGui::SetScrollY(0.0f);
                    State->SetFloat(ImGui::GetID("##scroll-target"), 0.0f);
                }

                if (OpenBandPtr != nullptr)
                {
                    const ToolTileOutcome Outcome =
                        InscribeToolTileGrid(Icons, *OpenBandPtr, Selection.StratumBit,
                                             ImVec2(GridX, HeaderBase - ImGui::GetScrollY()), RightWidth,
                                             Palette, Metrics);

                    if (Outcome.ActivatedTile >= 0)
                    {
                        Selection.OpenTile      = Outcome.ActivatedTile;
                        Carousel.ShowingOptions = true;
                    }

                    ImGui::SetCursorScreenPos(ImVec2(GridX, HeaderBase));
                    ImGui::Dummy(ImVec2(1.0f, MeasureToolTileGridHeight(*OpenBandPtr, Selection.StratumBit,
                                                                        RightWidth, Metrics)));
                }
            }
            ImGui::EndChild();

            // The tally strip is pinned BELOW the scroll, not inside it: a tally that scrolls out of view is a tally nobody reads.
            Canvas->AddRectFilled(ImVec2(GridX, GridFootTop), ImVec2(GridX + RightWidth, Origin.y + CardHeight),
                                  Palette.GridFill, Metrics.CardRounding, ImDrawFlags_RoundCornersBottomRight);
            Canvas->AddLine(ImVec2(GridX, GridFootTop), ImVec2(GridX + RightWidth, GridFootTop), Palette.Hairline, 1.0f);
            {
                char FooterText[80] = {};
                std::snprintf(FooterText, sizeof(FooterText), "on %d %s%s", Selection.SelectedCount,
                              (Selection.StratumName != nullptr) ? Selection.StratumName : "item",
                              (Selection.SelectedCount == 1) ? "" : "s");
                Canvas->AddText(ImVec2(GridX + GridFootPadding,
                                       GridFootTop + GridFootHigh * 0.5f - ImGui::GetTextLineHeight() * 0.5f),
                                Palette.Faint, FooterText);
            }
        }

        //=============================================== SLIDE TWO — PROBE + OPTIONS ===============================================
        {
            const float SlideX  = Origin.x + SlideShift + CardWidth;
            const OpenTool Open = ResolveOpenTool(Descriptor, Selection);

            BindToolParameterBlock(Parameters, Open.Parameters, Open.ParameterCount,
                                   Selection.OpenBand, Selection.OpenTile);

            // ---- probe header: the back row, INSIDE the 53px band so the header line stays continuous across both slides ----
            ImGui::SetCursorScreenPos(ImVec2(SlideX, Origin.y));
            const bool BackPressed = ImGui::InvisibleButton("##back", ImVec2(LeftWidth, HeaderHigh));
            const bool BackHovered = ImGui::IsItemHovered();
            if (BackPressed)
            {
                Carousel.ShowingOptions = false;
            }

            InscribeHeaderGround(Canvas, ImVec2(SlideX, Origin.y), ImVec2(SlideX + LeftWidth, HeaderBase),
                                 Metrics.CardRounding, ImDrawFlags_RoundCornersTopLeft,
                                 BackHovered ? HeaderBackHover : Palette.RailSelectedFill, Palette.Hairline);
            InscribeBackChevron(Canvas, ImVec2(SlideX + PanePadding + BackArrowEdge * 0.5f, HeaderMidY),
                                BackArrowEdge, BackHovered ? Palette.Ink : Palette.Muted);
            {
                char ProbeSubtitle[48] = {};
                std::snprintf(ProbeSubtitle, sizeof(ProbeSubtitle), "%d selected", Selection.SelectedCount);
                InscribeHeaderText(Canvas,
                                   (Descriptor.Probe != nullptr && Descriptor.Probe->Identity != nullptr)
                                       ? Descriptor.Probe->Identity : "Measurements",
                                   ProbeSubtitle,
                                   SlideX + PanePadding + BackArrowEdge + HeaderGap,
                                   SlideX + LeftWidth - PanePadding, HeaderMidY, Palette);
            }

            // ---- probe body ----
            Canvas->AddRectFilled(ImVec2(SlideX, HeaderBase), ImVec2(SlideX + LeftWidth, Origin.y + CardHeight),
                                  Palette.CardFill, Metrics.CardRounding, ImDrawFlags_RoundCornersBottomLeft);

            ImGui::SetCursorScreenPos(ImVec2(SlideX, HeaderBase));
            ImGui::BeginChild("##probe-body", ImVec2(LeftWidth, CardHeight - HeaderHigh), false,
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                              | ImGuiWindowFlags_NoScrollWithMouse);
            {
                ApplyEasedScroll(BodyWheelStep);
                if (Descriptor.Probe != nullptr)
                {
                    InscribeToolProbeColumn(Icons, *Descriptor.Probe, Selection.SelectedCount,
                                            ImVec2(SlideX, HeaderBase - ImGui::GetScrollY()), LeftWidth, Palette);

                    ImGui::SetCursorScreenPos(ImVec2(SlideX, HeaderBase));
                    ImGui::Dummy(ImVec2(1.0f, MeasureToolProbeHeight(*Descriptor.Probe, Selection.SelectedCount,
                                                                     LeftWidth)));
                }
            }
            ImGui::EndChild();

            // ---- options header: the open tool's glyph, name over its band ----
            const float OptionsX = SlideX + LeftWidth + 1.0f;
            InscribeHeaderGround(Canvas, ImVec2(OptionsX, Origin.y), ImVec2(OptionsX + RightWidth, HeaderBase),
                                 Metrics.CardRounding, ImDrawFlags_RoundCornersTopRight,
                                 Palette.RailSelectedFill, Palette.Hairline);
            Canvas->AddLine(ImVec2(SlideX + LeftWidth, Origin.y), ImVec2(SlideX + LeftWidth, Origin.y + CardHeight),
                            Palette.Hairline, 1.0f);

            if (Open.Bound)
            {
                const ImVec2 TileMin(OptionsX + PanePadding, HeaderMidY - HeaderTileEdge * 0.5f);
                Canvas->AddRectFilled(TileMin, ImVec2(TileMin.x + HeaderTileEdge, TileMin.y + HeaderTileEdge),
                                      IM_COL32(0, 0, 0, 255), HeaderTileRound);
                InscribeToolGlyph(Icons, Open.GlyphName,
                                  ImVec2(TileMin.x + (HeaderTileEdge - HeaderGlyphInset) * 0.5f,
                                         TileMin.y + (HeaderTileEdge - HeaderGlyphInset) * 0.5f),
                                  HeaderGlyphInset, false);

                char OptionsPill[24] = {};
                std::snprintf(OptionsPill, sizeof(OptionsPill), "%d", Open.ParameterCount);
                const float PillLeft = InscribeHeaderPill(Canvas, OptionsPill, OptionsX + RightWidth - PanePadding,
                                                          HeaderMidY, Palette.GridFill, Palette.Accent);

                const char* BandCaption = (Selection.OpenBand >= 0 && Selection.OpenBand < Descriptor.BandCount)
                                        ? Descriptor.Bands[Selection.OpenBand].Caption : "";
                InscribeHeaderText(Canvas, Open.Label, BandCaption,
                                   TileMin.x + HeaderTileEdge + HeaderGap, PillLeft - 4.0f, HeaderMidY, Palette);
            }

            // ---- options body + Apply/Cancel footer ----
            const float OptionsFootHigh = Metrics.OptionsFootHeight * PopScale;
            const float OptionsFootTop  = Origin.y + CardHeight - OptionsFootHigh;

            Canvas->AddRectFilled(ImVec2(OptionsX, HeaderBase), ImVec2(OptionsX + RightWidth, OptionsFootTop),
                                  Palette.GridFill);

            ImGui::SetCursorScreenPos(ImVec2(OptionsX, HeaderBase));
            ImGui::BeginChild("##options-body", ImVec2(RightWidth, OptionsFootTop - HeaderBase), false,
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                              | ImGuiWindowFlags_NoScrollWithMouse);
            {
                ApplyEasedScroll(BodyWheelStep);

                // A newly opened tool starts at the top of its own options, for the same reason a new band does.
                ImGuiStorage* const State   = ImGui::GetStateStorage();
                const ImGuiID       ToolKey = ImGui::GetID("##shown-tool");
                const int           ToolTag = Selection.OpenBand * 1024 + Selection.OpenTile;
                if (State->GetInt(ToolKey, -99999) != ToolTag)
                {
                    State->SetInt(ToolKey, ToolTag);
                    ImGui::SetScrollY(0.0f);
                    State->SetFloat(ImGui::GetID("##scroll-target"), 0.0f);
                }

                if (Open.Bound && Open.ParameterCount > 0)
                {
                    InscribeToolParameterColumn(Icons, Open.Parameters, Open.ParameterCount, Parameters,
                                                ImVec2(OptionsX, HeaderBase - ImGui::GetScrollY()), RightWidth,
                                                Palette, Metrics);

                    ImGui::SetCursorScreenPos(ImVec2(OptionsX, HeaderBase));
                    ImGui::Dummy(ImVec2(1.0f, MeasureToolParameterHeight(Open.Parameters, Open.ParameterCount, Metrics)));
                }
                else if (Open.Bound)
                {
                    // A tool that genuinely takes no options SAYS so. An empty pane reads as a load failure.
                    InscribeToolParameterVoidNote(Icons, Open.GlyphName, Open.Label,
                                                  ImVec2(OptionsX, HeaderBase), RightWidth,
                                                  OptionsFootTop - HeaderBase, Palette);
                }
            }
            ImGui::EndChild();

            Canvas->AddRectFilled(ImVec2(OptionsX, OptionsFootTop),
                                  ImVec2(OptionsX + RightWidth, Origin.y + CardHeight),
                                  Palette.GridFill, Metrics.CardRounding, ImDrawFlags_RoundCornersBottomRight);
            Canvas->AddLine(ImVec2(OptionsX, OptionsFootTop), ImVec2(OptionsX + RightWidth, OptionsFootTop),
                            Palette.Hairline, 1.0f);
            {
                const float FootMidY = OptionsFootTop + OptionsFootHigh * 0.5f;

                // Laid out from the right so the two buttons keep their positions whatever the note beside them says.
                const float CommitWidth = ImGui::CalcTextSize("Apply").x + FooterButtonPadX * 2.0f;
                const float CancelWidth = ImGui::CalcTextSize("Cancel").x + FooterButtonPadX * 2.0f;
                const float CommitX     = OptionsX + RightWidth - FooterPadding - CommitWidth;
                const float CancelX     = CommitX - FooterGap - CancelWidth;

                char SpendText[80] = {};
                std::snprintf(SpendText, sizeof(SpendText), "on %d %s%s", Selection.SelectedCount,
                              (Selection.StratumName != nullptr) ? Selection.StratumName : "item",
                              (Selection.SelectedCount == 1) ? "" : "s");
                Canvas->PushClipRect(ImVec2(OptionsX + FooterPadding, OptionsFootTop),
                                     ImVec2(CancelX - FooterGap, Origin.y + CardHeight), true);
                Canvas->AddText(ImVec2(OptionsX + FooterPadding, FootMidY - ImGui::GetTextLineHeight() * 0.5f),
                                Palette.Faint, SpendText);
                Canvas->PopClipRect();

                if (InscribeFooterButton(Canvas, "Cancel", ImVec2(CancelX, FootMidY - FooterButtonHigh * 0.5f),
                                         false, Palette))
                {
                    Carousel.ShowingOptions = false;
                }
                if (InscribeFooterButton(Canvas, "Apply", ImVec2(CommitX, FootMidY - FooterButtonHigh * 0.5f),
                                         true, Palette))
                {
                    Result.ActivatedBand   = Selection.OpenBand;
                    Result.ActivatedTile   = Selection.OpenTile;
                    Result.CommitRequested = true;
                    Carousel.ShowingOptions = false;
                }
            }
        }

        Canvas->PopClipRect();

        // The card border, drawn last so it crosses both slides rather than being painted over by either.
        Canvas->AddRect(Origin, ImVec2(Origin.x + CardWidth, Origin.y + CardHeight),
                        Palette.HairlineStrong, Metrics.CardRounding, 0, 1.0f);
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    return Result;
}

} // namespace Frontier
