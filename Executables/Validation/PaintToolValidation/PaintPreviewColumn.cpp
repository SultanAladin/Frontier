/*==============================================================================================================================================
                                                        PAINTPREVIEWCOLUMN.CPP
==============================================================================================================================================*/
// 🧩 The stroke ribbon, the dab, the ink chips, the standing instrument, and the spec rows.

#include "PaintPreviewColumn.h"
#include "PaintIconPack.h"
#include "PaintIconStore.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
    // .pv-stroke / .pv-dot-wrap / .pv-stand — all three share the 9 px radius; .pv-row is 6.
    constexpr float PreviewRounding = 9.0f;
    constexpr float SpecRowRounding = 6.0f;

    // .pv-foot{ gap:9px } · .pv-swatches{ margin-bottom:6px } · .pv-stand{ margin-top:2px } · .pv-spec{ gap:1px }
    constexpr float PreviewFootGap   = 9.0f;
    constexpr float SwatchStripBelow = 6.0f;
    constexpr float StandMarginTop   = 2.0f;
    constexpr float SpecRowGap       = 1.0f;

    // .pv-row{ padding:4px 6px } · .pv-sw{ border:2px } and its :hover scale
    constexpr float SpecRowPaddingX = 6.0f;
    constexpr float SpecRowPaddingY = 4.0f;
    constexpr float SwatchBorder    = 2.0f;
    constexpr float SwatchHoverGrow = 1.12f;

    // 🔴 The canvases are 2x their CSS box — `<canvas class="pv-stroke" width="360" height="92">` inside a 46 px-tall element, and
    //    `<canvas class="pv-dot" width="92" height="92">` inside a 46 px one. Every radius and jitter in PaintPreview() is therefore
    //    in BACKING-STORE pixels and gets halved on the way to the screen. Porting the arithmetic without this factor would double
    //    every stroke width, which at Size 80 fills the strip solid.
    constexpr float CanvasScale = 2.0f;

    // The prototype's own stamp count and the literals of its path. Named rather than inlined because the sine's 1.7 and the 0.24
    // amplitude are what make it read as one confident stroke instead of a wave.
    constexpr int   RibbonStampCount   = 220;
    constexpr float RibbonInsetX       = 14.0f;   // `X = 14 + T * (RW - 28)` — 14 each side, in canvas pixels
    constexpr float RibbonSineCycles   = 1.7f;
    constexpr float RibbonAmplitude    = 0.24f;
    constexpr float RibbonGrainChance  = 0.55f;
    constexpr float RibbonScatterSpan  = 1.6f;
    constexpr int   DabGrainBiteCount  = 240;
    constexpr float DabBiteEdge        = 1.6f;
    constexpr float DabRadiusFactor    = 0.42f;
    constexpr float DabSizeReference   = 80.0f;   // `Size / 80` — the schema's slider maximum
    constexpr float SoftnessFloor      = 0.02f;

    float Saturate(float Value) { return (Value < 0.0f) ? 0.0f : ((Value > 1.0f) ? 1.0f : Value); }

    bool IsPointerInside(ImVec2 Pointer, ImVec2 Minimum, ImVec2 Maximum)
    {
        return (Pointer.x >= Minimum.x && Pointer.x < Maximum.x && Pointer.y >= Minimum.y && Pointer.y < Maximum.y);
    }

    ImU32 WithAlpha(ImU32 Colour, float Alpha)
    {
        const ImU32 Channels = Colour & ~IM_COL32_A_MASK;
        return Channels | ((ImU32)(Saturate(Alpha) * 255.0f + 0.5f) << IM_COL32_A_SHIFT);
    }

    // 🔴 A seeded generator, NOT rand(). The prototype paints onto a retained canvas once per parameter change; ImGui re-runs this
    //    every frame, so a live random source would make the grain crawl and the stroke boil under a still cursor. Seeding on
    //    (instrument, swatch, parameters) gives the prototype's visual — a fixed speckle that reshuffles only when something changes.
    //    📝 PCG-style rather than a Mersenne twister: this is called ~700 times per frame for pixels, and it must be cheap and
    //       stateless-by-value so each surface can restart from its own seed independently.
    struct StampRandom
    {
        unsigned int State;

        explicit StampRandom(unsigned int Seed) : State(Seed | 1u) {}

        unsigned int NextBits()
        {
            State ^= State << 13;
            State ^= State >> 17;
            State ^= State << 5;
            return State;
        }

        // [0,1)
        float NextUnit() { return (float)(NextBits() >> 8) / 16777216.0f; }
    };

    // 📝 Folds the parameters into the seed so a slider drag reshuffles the speckle, matching the prototype's repaint-on-commit.
    //    Quantised to the schema's own step before hashing, or float noise would reshuffle on values that display identically.
    unsigned int ResolveStampSeed(int InstrumentIndex, int SwatchIndex, const PaintStrokeParameters& Parameters)
    {
        unsigned int Seed = 2166136261u;
        const auto Fold = [&Seed](unsigned int Value)
        {
            Seed ^= Value;
            Seed *= 16777619u;
        };

        Fold((unsigned int)(InstrumentIndex + 1));
        Fold((unsigned int)(SwatchIndex + 1));
        Fold((unsigned int)(Parameters.SizePixels * 100.0f));
        Fold((unsigned int)(Parameters.Opacity    * 1000.0f));
        Fold((unsigned int)(Parameters.Flow       * 1000.0f));
        Fold((unsigned int)(Parameters.Grain      * 1000.0f));
        Fold((unsigned int)(Parameters.Scatter    * 1000.0f));
        Fold((unsigned int)(Parameters.Softness   * 1000.0f));
        return Seed;
    }

    // Draw one pale preview surface: the paper fill under a strong hairline, both at the 9 px radius.
    void ConstructPaperSurface(const PaintCardPalette& Palette, ImVec2 Minimum, ImVec2 Maximum)
    {
        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        DrawList->AddRectFilled(Minimum, Maximum, Palette.PaperFill, PreviewRounding);
        DrawList->AddRect(Minimum, Maximum, Palette.HairlineStrong, PreviewRounding, 0, 1.0f);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ImU32 ResolvePaintPreviewInk(int InstrumentIndex, const PaintPreviewState& State, ImU32 Fallback)
{
    int InstrumentCount = 0;
    const PaintInstrumentDescriptor* const Instruments = ResolvePaintInstruments(InstrumentCount);
    if (Instruments == nullptr || InstrumentIndex < 0 || InstrumentIndex >= InstrumentCount) { return Fallback; }

    int FamilyCount = 0;
    const PaintFamilyDescriptor* const Families = ResolvePaintFamilies(FamilyCount);
    if (Families == nullptr) { return Fallback; }

    // The instrument names its family by key; the swatch row hangs off the family, not the instrument.
    const char* const FamilyKey = Instruments[InstrumentIndex].FamilyKey;
    for (int FamilyIndex = 0; FamilyIndex < FamilyCount; ++FamilyIndex)
    {
        const PaintFamilyDescriptor& Family = Families[FamilyIndex];
        if (Family.Key == nullptr || FamilyKey == nullptr) { continue; }

        bool Matches = true;
        for (int Character = 0; ; ++Character)
        {
            if (Family.Key[Character] != FamilyKey[Character]) { Matches = false; break; }
            if (Family.Key[Character] == '\0') { break; }
        }
        if (!Matches) { continue; }

        if (Family.Swatches == nullptr || Family.SwatchCount <= 0) { return Fallback; }

        // 📝 Clamped rather than asserted: the swatch index survives an instrument change in the prototype too, and a family with
        //    fewer inks than the last one would otherwise read past its row. The prototype's `|| "#101014"` guard is the same idea.
        const int Index = (State.SwatchIndex < 0) ? 0
                        : ((State.SwatchIndex >= Family.SwatchCount) ? (Family.SwatchCount - 1) : State.SwatchIndex);
        return ResolveAuthoredColour(Family.Swatches[Index], Fallback);
    }

    return Fallback;
}


void ConstructPaintStrokeRibbon(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                const PaintStrokeParameters& Parameters, ImU32 Ink,
                                int InstrumentIndex, int SwatchIndex,
                                ImVec2 StripMinimum, float StripWidth)
{
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    const ImVec2 StripMaximum(StripMinimum.x + StripWidth, StripMinimum.y + Metrics.StrokeStripHeight);
    ConstructPaperSurface(Palette, StripMinimum, StripMaximum);

    // 🔴 Clipped to the paper. Scatter jitters stamps by up to 1.6x the stroke width, which at Size 80 reaches well past the strip,
    //    and an unclipped stamp would land on the card's dark chrome as a stray blob.
    DrawList->PushClipRect(StripMinimum, StripMaximum, true);

    // The canvas is 2x, so the formula's own units are canvas pixels and everything converts on the way out.
    const float CanvasWidth  = StripWidth * CanvasScale;
    const float CanvasHeight = Metrics.StrokeStripHeight * CanvasScale;

    // `Width = max(1, Size * (RH / 46) * 0.9)`, where RH is the canvas height — 92 for a 46 px box, so the ratio is the 2x itself.
    const float StrokeWidth = std::max(1.0f, Parameters.SizePixels * (CanvasHeight / Metrics.StrokeStripHeight) * 0.9f);

    StampRandom Random(ResolveStampSeed(InstrumentIndex, SwatchIndex, Parameters));

    for (int Stamp = 0; Stamp < RibbonStampCount; ++Stamp)
    {
        const float T = (float)Stamp / (float)(RibbonStampCount - 1);

        // The path, in canvas pixels: a single eased S across the strip, 14 px inset at each end.
        const float CanvasX = RibbonInsetX + (T * (CanvasWidth - (RibbonInsetX * 2.0f)));
        const float CanvasY = (CanvasHeight * 0.5f)
                            + (std::sin(T * 3.14159265f * RibbonSineCycles) * (CanvasHeight * RibbonAmplitude));

        // Pressure envelope: thin at both ends, full through the middle.
        const float Envelope = std::sin(Saturate(T) * 3.14159265f);
        float       Alpha    = Parameters.Opacity * Parameters.Flow * (0.35f + (0.65f * Envelope));

        // 🔴 The generator is advanced ONLY when grain is on, because the prototype short-circuits on `Grain > 0` before ever
        //    calling random(). That ordering is load bearing: it decides which values the scatter draws below then see, so
        //    consuming a value unconditionally would give a different speckle at every grain setting including zero.
        if (Parameters.Grain > 0.0f && Random.NextUnit() < (Parameters.Grain * RibbonGrainChance))
        {
            Alpha *= 0.25f + (Random.NextUnit() * 0.5f);
        }

        float JitterX = 0.0f;
        float JitterY = 0.0f;
        if (Parameters.Scatter > 0.0f)
        {
            JitterX = (Random.NextUnit() - 0.5f) * Parameters.Scatter * StrokeWidth * RibbonScatterSpan;
            JitterY = (Random.NextUnit() - 0.5f) * Parameters.Scatter * StrokeWidth * RibbonScatterSpan;
        }

        // `max(.4, (Width / 2) * (0.55 + 0.45 * Env))` — the radius tapers with the same envelope as the alpha.
        const float CanvasRadius = std::max(0.4f, (StrokeWidth * 0.5f) * (0.55f + (0.45f * Envelope)));

        const ImVec2 Centre(StripMinimum.x + ((CanvasX + JitterX) / CanvasScale),
                            StripMinimum.y + ((CanvasY + JitterY) / CanvasScale));

        // 📝 12 segments, not the well's 32: these are 1-3 px radii at 220 per frame, and the silhouette is built by overlap
        //    rather than by any one circle's smoothness.
        DrawList->AddCircleFilled(Centre, CanvasRadius / CanvasScale, WithAlpha(Ink, Alpha), 12);
    }

    DrawList->PopClipRect();
}


void ConstructPaintDab(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                       const PaintStrokeParameters& Parameters, ImU32 Ink,
                       int InstrumentIndex, int SwatchIndex, ImVec2 WellMinimum)
{
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    const float  WellEdge = Metrics.StrokeStripHeight;   // .pv-dot-wrap is 46x46, the same as the strip's height
    const ImVec2 WellMaximum(WellMinimum.x + WellEdge, WellMinimum.y + WellEdge);
    ConstructPaperSurface(Palette, WellMinimum, WellMaximum);

    DrawList->PushClipRect(WellMinimum, WellMaximum, true);

    const float  CanvasEdge = WellEdge * CanvasScale;
    const ImVec2 Centre((WellMinimum.x + WellMaximum.x) * 0.5f, (WellMinimum.y + WellMaximum.y) * 0.5f);

    // `max(3, min(DW,DH) * 0.42 * (0.35 + 0.65 * (Size / 80)))`, in canvas pixels.
    const float CanvasRadius = std::max(3.0f, CanvasEdge * DabRadiusFactor
                                           * (0.35f + (0.65f * Saturate(Parameters.SizePixels / DabSizeReference))));
    const float Radius = CanvasRadius / CanvasScale;
    const float Alpha  = Saturate(Parameters.Opacity * Parameters.Flow);

    if (Parameters.Softness > SoftnessFloor)
    {
        // 🔴 A radial gradient from opaque ink at `Radius * (1 - Softness)` to fully transparent at `Radius`. ImDrawList has no
        //    gradient brush, so it is built as concentric rings — the inner core solid, then a ramp outward.
        //    📝 Rings rather than AddCircleFilled with a fading colour: a single circle cannot carry a per-vertex alpha ramp, and
        //       the falloff is the whole difference between a hard marker dot and a soft airbrush puff.
        const float CoreRadius = Radius * (1.0f - Saturate(Parameters.Softness));
        DrawList->AddCircleFilled(Centre, CoreRadius, WithAlpha(Ink, Alpha), 32);

        constexpr int RingCount = 18;
        for (int Ring = 0; Ring < RingCount; ++Ring)
        {
            const float Inner = CoreRadius + ((Radius - CoreRadius) * ((float)Ring / (float)RingCount));
            const float Outer = CoreRadius + ((Radius - CoreRadius) * ((float)(Ring + 1) / (float)RingCount));
            // Linear ramp to zero, matching the gradient's two stops.
            const float RingAlpha = Alpha * (1.0f - ((float)(Ring + 1) / (float)RingCount));
            DrawList->AddCircle(Centre, (Inner + Outer) * 0.5f, WithAlpha(Ink, RingAlpha), 32, (Outer - Inner) + 0.5f);
        }
    }
    else
    {
        DrawList->AddCircleFilled(Centre, Radius, WithAlpha(Ink, Alpha), 32);
    }

    // 🔴 `clearRect` bites HOLES in the dab — it erases to transparent, revealing the paper beneath. Reproduced by painting the
    //    paper colour back over, which is equivalent here because the dab sits directly on an opaque paper fill and nothing else
    //    is composited beneath it. On a translucent surface this would differ, and that is why it is stated rather than assumed.
    if (Parameters.Grain > 0.0f)
    {
        StampRandom Random(ResolveStampSeed(InstrumentIndex, SwatchIndex, Parameters));
        const int   Bites = (int)((Parameters.Grain * (float)DabGrainBiteCount) + 0.5f);
        const float Edge  = DabBiteEdge / CanvasScale;

        for (int Bite = 0; Bite < Bites; ++Bite)
        {
            // Uniform over the disc: `sqrt(random()) * Radius` — without the square root the specks crowd the centre.
            const float Angle    = Random.NextUnit() * 6.28318531f;
            const float Distance = std::sqrt(Random.NextUnit()) * Radius;
            const ImVec2 SpeckMinimum(Centre.x + (std::cos(Angle) * Distance), Centre.y + (std::sin(Angle) * Distance));
            DrawList->AddRectFilled(SpeckMinimum, ImVec2(SpeckMinimum.x + Edge, SpeckMinimum.y + Edge), Palette.PaperFill);
        }
    }

    DrawList->PopClipRect();
}


int ConstructPaintSwatchStrip(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                              int InstrumentIndex, int SelectedSwatchIndex,
                              ImVec2 StripMinimum, float StripWidth)
{
    int InstrumentCount = 0;
    const PaintInstrumentDescriptor* const Instruments = ResolvePaintInstruments(InstrumentCount);
    if (Instruments == nullptr || InstrumentIndex < 0 || InstrumentIndex >= InstrumentCount) { return -1; }

    int FamilyCount = 0;
    const PaintFamilyDescriptor* const Families = ResolvePaintFamilies(FamilyCount);
    if (Families == nullptr) { return -1; }

    // Resolve the family's swatch row by key, the same walk ResolvePaintPreviewInk does.
    const char* const  FamilyKey = Instruments[InstrumentIndex].FamilyKey;
    const char* const* Swatches  = nullptr;
    int                SwatchCount = 0;
    for (int FamilyIndex = 0; FamilyIndex < FamilyCount; ++FamilyIndex)
    {
        const PaintFamilyDescriptor& Family = Families[FamilyIndex];
        if (Family.Key == nullptr || FamilyKey == nullptr) { continue; }

        bool Matches = true;
        for (int Character = 0; ; ++Character)
        {
            if (Family.Key[Character] != FamilyKey[Character]) { Matches = false; break; }
            if (Family.Key[Character] == '\0') { break; }
        }
        if (Matches) { Swatches = Family.Swatches; SwatchCount = Family.SwatchCount; break; }
    }
    if (Swatches == nullptr || SwatchCount <= 0) { return -1; }

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Pointer = ImGui::GetIO().MousePos;
    const ImVec2 ClipMin = DrawList->GetClipRectMin();
    const ImVec2 ClipMax = DrawList->GetClipRectMax();
    const bool   PointerInClip = IsPointerInside(Pointer, ClipMin, ClipMax);

    int ClickedIndex = -1;
    const float Radius = Metrics.SwatchDiameter * 0.5f;

    for (int Index = 0; Index < SwatchCount; ++Index)
    {
        // flex-wrap is authored but never triggers: five 17 px chips with 5 px gaps span 105 px inside a ~170 px meta column.
        const float  ChipLeft = StripMinimum.x + ((Metrics.SwatchDiameter + Metrics.SwatchGap) * (float)Index);
        const ImVec2 ChipMinimum(ChipLeft, StripMinimum.y);
        const ImVec2 ChipMaximum(ChipLeft + Metrics.SwatchDiameter, StripMinimum.y + Metrics.SwatchDiameter);
        if (ChipMaximum.x > StripMinimum.x + StripWidth) { break; }

        const bool IsChosen  = (Index == SelectedSwatchIndex);
        const bool IsHovered = PointerInClip && IsPointerInside(Pointer, ChipMinimum, ChipMaximum);
        if (IsHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { ClickedIndex = Index; }

        // .pv-sw:hover{ transform:scale(1.12) } — about the chip's own centre, so the strip does not reflow.
        const float  Grow   = IsHovered ? SwatchHoverGrow : 1.0f;
        const ImVec2 Centre((ChipMinimum.x + ChipMaximum.x) * 0.5f, (ChipMinimum.y + ChipMaximum.y) * 0.5f);
        const float  Drawn  = Radius * Grow;

        const ImU32 Chip = ResolveAuthoredColour(Swatches[Index], Palette.Muted);
        DrawList->AddCircleFilled(Centre, Drawn, Chip, 20);

        // border:2px solid rgba(255,255,255,.12), or #fff when chosen. Drawn INSIDE the chip's radius so a selected chip does not
        // grow — the CSS border is part of the 17 px box, not added to it.
        const ImU32 Border = IsChosen ? IM_COL32(0xFF, 0xFF, 0xFF, 0xFF) : IM_COL32(0xFF, 0xFF, 0xFF, 0x1F);
        DrawList->AddCircle(Centre, Drawn - (SwatchBorder * 0.5f), Border, 20, SwatchBorder);
    }

    return ClickedIndex;
}


void ConstructPaintInstrumentStand(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                   SvgIconRegistry* Registry, PaintIconStore* StripStore,
                                   int InstrumentIndex, ImVec2 StandMinimum, float StandWidth)
{
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    const ImVec2 StandMaximum(StandMinimum.x + StandWidth, StandMinimum.y + Metrics.StandHeight);

    // .pv-stand{ background:var(--menu-2); border:1px solid var(--hair) } — this one is DARK, unlike the two paper surfaces above it.
    DrawList->AddRectFilled(StandMinimum, StandMaximum, Palette.PaneFill, PreviewRounding);
    DrawList->AddRect(StandMinimum, StandMaximum, Palette.Hairline, PreviewRounding, 0, 1.0f);

    DrawList->PushClipRect(StandMinimum, StandMaximum, true);

    const ImVec2 Centre((StandMinimum.x + StandMaximum.x) * 0.5f, (StandMinimum.y + StandMaximum.y) * 0.5f);

    ImTextureID Texture   = 0;
    bool        IsRotated = false;
    float       ArtWidth  = Metrics.StandArtWidth;
    float       ArtHeight = Metrics.StandArtHeight;

    if (StripStore != nullptr)
    {
        const PaintStripTexture* const Strip = ResolvePaintStrip(*StripStore, InstrumentIndex);
        if (Strip != nullptr && Strip->PixelHeight > 0u)
        {
            Texture   = Strip->StripTextureId;
            IsRotated = true;
            // `width:150px; height:30px` then `scale(1.5)` — the CSS box, grown about its centre.
            ArtWidth  = Metrics.StandArtWidth  * Metrics.StandArtGrow;
            ArtHeight = Metrics.StandArtHeight * Metrics.StandArtGrow;
        }
    }

    // 📝 The nib crop as the fallback, unrotated: a cropped tip standing on end would read as a bug rather than as a loading state.
    if (Texture == 0 && Registry != nullptr)
    {
        Texture   = ResolveIconTexture(*Registry, ResolvePaintNibKey(InstrumentIndex));
        IsRotated = false;
        ArtWidth  = Metrics.NibArtEdge;
        ArtHeight = Metrics.NibArtEdge;
    }

    if (Texture != 0)
    {
        if (IsRotated)
        {
            // 🔴 `transform:rotate(-90deg)` cannot go through AddImage, which only takes an axis-aligned rectangle. The quad is
            //    built by hand with AddImageQuad, and the corner ORDER is what carries the rotation: -90 degrees maps the art's
            //    top-left to the screen's bottom-left, so the instrument's tip (authored at the RIGHT of the landscape box) ends
            //    up at the TOP. Getting the order wrong stands the instrument on its tip, which still looks deliberate.
            //    📝 After the rotate, the art's WIDTH runs vertically — so the on-screen extent is (ArtHeight, ArtWidth).
            const float HalfWide = ArtHeight * 0.5f;   // horizontal extent after the rotate
            const float HalfTall = ArtWidth  * 0.5f;   // vertical extent after the rotate

            const ImVec2 ScreenTopLeft(Centre.x - HalfWide, Centre.y - HalfTall);
            const ImVec2 ScreenTopRight(Centre.x + HalfWide, Centre.y - HalfTall);
            const ImVec2 ScreenBottomRight(Centre.x + HalfWide, Centre.y + HalfTall);
            const ImVec2 ScreenBottomLeft(Centre.x - HalfWide, Centre.y + HalfTall);

            // UVs walk the source so that source-RIGHT (the tip) lands at screen-TOP.
            DrawList->AddImageQuad(Texture,
                                   ScreenTopLeft, ScreenTopRight, ScreenBottomRight, ScreenBottomLeft,
                                   ImVec2(1.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec2(0.0f, 1.0f), ImVec2(0.0f, 0.0f));
        }
        else
        {
            const ImVec2 ArtMinimum(Centre.x - (ArtWidth * 0.5f), Centre.y - (ArtHeight * 0.5f));
            DrawList->AddImage(Texture, ArtMinimum, ImVec2(ArtMinimum.x + ArtWidth, ArtMinimum.y + ArtHeight));
        }
    }

    DrawList->PopClipRect();

    // ⚠️ `filter:drop-shadow(0 3px 5px rgba(0,0,0,.55))` is NOT reproduced. A real drop shadow needs the art's alpha blurred, which
    //    means an offscreen pass; ImDrawList cannot do it and faking it with a dark rounded rectangle behind the quad would put a
    //    shadow under the art's BOUNDING BOX rather than under the instrument's silhouette, which reads worse than none.
}


int ConstructPaintPreviewColumn(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                const PaintPaneRegion& Region, const PaintPreviewState& State,
                                const PaintStrokeParameters& Parameters,
                                SvgIconRegistry* Registry, PaintIconStore* StripStore,
                                int InstrumentIndex, int VisibleGroupCount, int ParameterCount)
{
    // 📝 Same off-card bail as the rail and the grid: a pane translated away must neither draw nor hit-test.
    if (!Region.IsVisible) { return -1; }

    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    // .preview-pane{ background:var(--menu) } — the column's own ground, matching the rail it replaces.
    DrawList->AddRectFilled(Region.BodyMinimum, Region.BodyMaximum, Palette.CardFill);

    DrawList->PushClipRect(Region.BodyMinimum, Region.BodyMaximum, true);

    // .preview-body{ padding:12px 13px; gap:9px } — shares the options body's padding.
    const float PaddingX  = Metrics.OptionsPaddingX;
    const float PaddingY  = Metrics.OptionsPaddingY;
    const float BodyWidth = (Region.BodyMaximum.x - Region.BodyMinimum.x) - (PaddingX * 2.0f);
    const float OriginX   = Region.BodyMinimum.x + PaddingX;
    float       CursorY   = Region.BodyMinimum.y + PaddingY;

    const ImU32 Ink = ResolvePaintPreviewInk(InstrumentIndex, State, Palette.PaperInk);

    // ── the stroke ribbon ──
    ConstructPaintStrokeRibbon(Palette, Metrics, Parameters, Ink, InstrumentIndex, State.SwatchIndex,
                               ImVec2(OriginX, CursorY), BodyWidth);
    CursorY += Metrics.StrokeStripHeight + PreviewFootGap;

    // ── the foot: dab well, then the swatch strip and hint beside it ──
    const float WellEdge = Metrics.StrokeStripHeight;
    ConstructPaintDab(Palette, Metrics, Parameters, Ink, InstrumentIndex, State.SwatchIndex, ImVec2(OriginX, CursorY));

    const float MetaLeft  = OriginX + WellEdge + PreviewFootGap;
    const float MetaWidth = (OriginX + BodyWidth) - MetaLeft;

    // 📝 The meta block is vertically CENTRED against the 46 px well (`.pv-foot{ align-items:center }`), so its own height is
    //    resolved first — chips plus their margin plus the hint line — rather than being laid out from the well's top.
    const float HintHeight = ImGui::GetTextLineHeight();
    const float MetaHeight = Metrics.SwatchDiameter + SwatchStripBelow + HintHeight;
    const float MetaTop    = CursorY + ((WellEdge - MetaHeight) * 0.5f);

    const int ClickedSwatch = ConstructPaintSwatchStrip(Palette, Metrics, InstrumentIndex, State.SwatchIndex,
                                                       ImVec2(MetaLeft, MetaTop), MetaWidth);

    DrawList->AddText(ImVec2(MetaLeft, MetaTop + Metrics.SwatchDiameter + SwatchStripBelow),
                      Palette.Faint, "Stroke & dab preview");

    CursorY += WellEdge + StandMarginTop;

    // ── the standing instrument ──
    ConstructPaintInstrumentStand(Palette, Metrics, Registry, StripStore, InstrumentIndex,
                                  ImVec2(OriginX, CursorY), BodyWidth);
    CursorY += Metrics.StandHeight + PreviewFootGap;

    // ── the spec rows ──
    // 🔴 Three rows, and the two counts are LIVE: `Groups` is the count of controls the schema's `when:` predicates currently admit
    //    and `Parameters` is the size of the live parameter set, so both move as the options column is used. Hard-coding them would
    //    make the block look right and stop tracking, which is the failure mode that survives a screenshot.
    int InstrumentCount = 0;
    const PaintInstrumentDescriptor* const Instruments = ResolvePaintInstruments(InstrumentCount);

    const char* FamilyCaption = "";
    if (Instruments != nullptr && InstrumentIndex >= 0 && InstrumentIndex < InstrumentCount)
    {
        int FamilyCount = 0;
        const PaintFamilyDescriptor* const Families = ResolvePaintFamilies(FamilyCount);
        const char* const FamilyKey = Instruments[InstrumentIndex].FamilyKey;
        for (int FamilyIndex = 0; Families != nullptr && FamilyIndex < FamilyCount; ++FamilyIndex)
        {
            const PaintFamilyDescriptor& Family = Families[FamilyIndex];
            if (Family.Key == nullptr || FamilyKey == nullptr) { continue; }

            bool Matches = true;
            for (int Character = 0; ; ++Character)
            {
                if (Family.Key[Character] != FamilyKey[Character]) { Matches = false; break; }
                if (Family.Key[Character] == '\0') { break; }
            }
            if (Matches) { FamilyCaption = Family.Caption; break; }
        }
    }

    char GroupText[16];
    char ParameterText[16];
    std::snprintf(GroupText, sizeof(GroupText), "%d", VisibleGroupCount);
    std::snprintf(ParameterText, sizeof(ParameterText), "%d", ParameterCount);

    struct SpecRow { const char* Key; const char* Value; };
    const SpecRow Rows[] = { { "Family", FamilyCaption }, { "Groups", GroupText }, { "Parameters", ParameterText } };

    const ImVec2 Pointer = ImGui::GetIO().MousePos;
    const ImVec2 ClipMin = DrawList->GetClipRectMin();
    const ImVec2 ClipMax = DrawList->GetClipRectMax();
    const bool   PointerInClip = IsPointerInside(Pointer, ClipMin, ClipMax);

    const float RowHeight = ImGui::GetTextLineHeight() + (SpecRowPaddingY * 2.0f);
    for (const SpecRow& Row : Rows)
    {
        const ImVec2 RowMinimum(OriginX, CursorY);
        const ImVec2 RowMaximum(OriginX + BodyWidth, CursorY + RowHeight);
        CursorY += RowHeight + SpecRowGap;

        // .pv-row:hover{ background:var(--tile) }
        if (PointerInClip && IsPointerInside(Pointer, RowMinimum, RowMaximum))
        {
            DrawList->AddRectFilled(RowMinimum, RowMaximum, Palette.TileFill, SpecRowRounding);
        }

        const float TextY = RowMinimum.y + SpecRowPaddingY;

        // The value is right-aligned and reserved first, so a long family caption clips against it rather than running under it.
        const float ValueWidth = ImGui::CalcTextSize(Row.Value).x;
        const float ValueLeft  = RowMaximum.x - SpecRowPaddingX - ValueWidth;
        DrawList->AddText(ImVec2(ValueLeft, TextY), Palette.Ink, Row.Value);

        // text-overflow:ellipsis on the key — clipped here, as elsewhere on this card.
        DrawList->PushClipRect(ImVec2(RowMinimum.x + SpecRowPaddingX, RowMinimum.y), ImVec2(ValueLeft - 8.0f, RowMaximum.y), true);
        DrawList->AddText(ImVec2(RowMinimum.x + SpecRowPaddingX, TextY), Palette.Muted, Row.Key);
        DrawList->PopClipRect();
    }

    DrawList->PopClipRect();
    return ClickedSwatch;
}

} // namespace Frontier
