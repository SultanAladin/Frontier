/*==============================================================================================================================================
                                                           PAINTSURFACECHROME.CPP
==============================================================================================================================================*/
// 🧩 The drawing vocabulary itself: easing, the per-call-site fold / entry caches, text with tracking and clipping, the stroked glyph set, and
//    the controls (switch, segment row, scrub slider, drop face, swatch, ghost call, icon button).
//
// 🔴 Every glyph is transcribed from the prototype's inline SVG in its OWN viewBox and mapped through one projector. Redrawing them by eye at
//    ImGui scale is how the earlier attempts drifted: a 24-unit path and a 14-unit path look identical at 15 px until they sit side by side in
//    a header, and then the stroke weights disagree.
//
// 📝 Scrub is one shared latch keyed by the widget's id string, so exactly one value can be dragging at a time and the widget that owns the
//    latch keeps receiving the drag even when the cursor leaves its rect — which is what an `ew-resize` drag does in the source.

#include "PaintSurfaceChrome.h"

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
    constexpr int   CacheCapacity   = 512;    // [idx] - Distinct call sites the fold / span / age caches hold
    constexpr int   CacheKeyLength  = 64;     // [-]   - Call-site key buffer
    constexpr float EntrySeconds    = 0.26f;  // [s]   - rise keyframe duration

    struct CacheSlot
    {
        char  Key[CacheKeyLength];   // [-] - Call-site key, empty when free
        float Fold;                  // [-] - Eased fold travel 0..1
        float Span;                  // [px]- Body height remembered from the previous cycle
        float Age;                   // [s] - Entry-animation age
        int   Cycle;                 // [-] - Frame the slot was last touched on, for reclamation
    };

    CacheSlot Cache[CacheCapacity] = {};

    // -- The one scrub latch --------------------------------------------------------------------------------------------
    char  ScrubKey[CacheKeyLength] = {};
    float ScrubAnchorX             = 0.0f;   // [px] - Cursor x when the drag armed
    float ScrubAnchorValue         = 0.0f;   // [-]  - Value when the drag armed
    bool  ScrubMoved               = false;  // [-]  - The drag has actually moved

    int ResolveSlot(const char* Key)
    {
        if (Key == nullptr || Key[0] == '\0') return -1;
        int Free = -1;
        for (int Index = 0; Index < CacheCapacity; ++Index)
        {
            if (Cache[Index].Key[0] == '\0') { if (Free < 0) { Free = Index; } continue; }
            if (strncmp(Cache[Index].Key, Key, CacheKeyLength - 1) == 0) { return Index; }
        }
        if (Free < 0) { Free = 0; }   // 🔴 A full cache recycles slot 0 rather than dropping the request on the floor.
        CacheSlot& Slot = Cache[Free];
        memset(&Slot, 0, sizeof(CacheSlot));
        int Written = 0;
        while (Written < CacheKeyLength - 1 && Key[Written] != '\0') { Slot.Key[Written] = Key[Written]; ++Written; }
        Slot.Key[Written] = '\0';
        return Free;
    }

    float SolveCubicBezier(float Progress, float X1, float Y1, float X2, float Y2)
    {
        if (Progress <= 0.0f) { return 0.0f; }
        if (Progress >= 1.0f) { return 1.0f; }
        float Low = 0.0f, High = 1.0f, Guess = Progress;
        for (int Iteration = 0; Iteration < 20; ++Iteration)
        {
            const float O = 1.0f - Guess;
            const float X = 3.0f * O * O * Guess * X1 + 3.0f * O * Guess * Guess * X2 + Guess * Guess * Guess;
            if (X < Progress) { Low = Guess; } else { High = Guess; }
            Guess = (Low + High) * 0.5f;
        }
        const float O = 1.0f - Guess;
        return 3.0f * O * O * Guess * Y1 + 3.0f * O * Guess * Guess * Y2 + Guess * Guess * Guess;
    }

    // 📝 One projector for every transcribed path: viewBox units in, screen pixels out.
    struct GlyphProjection
    {
        ImVec2 Origin;    // [px] - Screen position of viewBox (0,0)
        float  Scale;     // [-]  - Screen pixels per viewBox unit

        ImVec2 operator()(float X, float Y) const { return ImVec2(Origin.x + X * Scale, Origin.y + Y * Scale); }
    };

    GlyphProjection ResolveProjection(ImVec2 Centre, float Edge, float ViewBox)
    {
        GlyphProjection Projection;
        Projection.Scale  = Edge / ViewBox;
        Projection.Origin = ImVec2(Centre.x - Edge * 0.5f, Centre.y - Edge * 0.5f);
        return Projection;
    }

    void StrokePolyline(ImDrawList* Draw, const GlyphProjection& To, const float* Points, int Count, ImU32 Tone, float Weight, bool Closed)
    {
        for (int Index = 0; Index < Count; ++Index) { Draw->PathLineTo(To(Points[Index * 2], Points[Index * 2 + 1])); }
        Draw->PathStroke(Tone, Closed ? ImDrawFlags_Closed : 0, Weight * To.Scale);
    }

    // 📝 A quadratic segment, sampled. The eye glyphs are the only quadratics in the set.
    void StrokeQuadratic(ImDrawList*            Draw,
                         const GlyphProjection& To,
                         ImVec2                 Start,
                         ImVec2                 Control,
                         ImVec2                 End,
                         ImU32                  Tone,
                         float                  Weight)
    {
        constexpr int Samples = 14;
        for (int Index = 0; Index <= Samples; ++Index)
        {
            const float T = (float)Index / (float)Samples;
            const float O = 1.0f - T;
            const float X = O * O * Start.x + 2.0f * O * T * Control.x + T * T * End.x;
            const float Y = O * O * Start.y + 2.0f * O * T * Control.y + T * T * End.y;
            Draw->PathLineTo(To(X, Y));
        }
        Draw->PathStroke(Tone, 0, Weight * To.Scale);
    }

    void StrokeArc(ImDrawList*            Draw,
                   const GlyphProjection& To,
                   ImVec2                 Centre,
                   float                  Radius,
                   float                  FromRadians,
                   float                  ToRadians,
                   ImU32                  Tone,
                   float                  Weight)
    {
        constexpr int Samples = 24;
        for (int Index = 0; Index <= Samples; ++Index)
        {
            const float T = FromRadians + (ToRadians - FromRadians) * ((float)Index / (float)Samples);
            Draw->PathLineTo(To(Centre.x + cosf(T) * Radius, Centre.y + sinf(T) * Radius));
        }
        Draw->PathStroke(Tone, 0, Weight * To.Scale);
    }

    void StrokeCircle(ImDrawList* Draw, const GlyphProjection& To, ImVec2 Centre, float Radius, ImU32 Tone, float Weight)
    {
        Draw->AddCircle(To(Centre.x, Centre.y), Radius * To.Scale, Tone, 0, Weight * To.Scale);
    }

    void FillCircle(ImDrawList* Draw, const GlyphProjection& To, ImVec2 Centre, float Radius, ImU32 Tone)
    {
        Draw->AddCircleFilled(To(Centre.x, Centre.y), Radius * To.Scale, Tone);
    }

    bool MatchScrubKey(const char* Key)
    {
        return ScrubKey[0] != '\0' && Key != nullptr && strncmp(ScrubKey, Key, CacheKeyLength - 1) == 0;
    }

    void ArmScrub(const char* Key, float AnchorX, float AnchorValue)
    {
        int Written = 0;
        while (Written < CacheKeyLength - 1 && Key[Written] != '\0') { ScrubKey[Written] = Key[Written]; ++Written; }
        ScrubKey[Written]  = '\0';
        ScrubAnchorX       = AnchorX;
        ScrubAnchorValue   = AnchorValue;
        ScrubMoved         = false;
    }

    void ReleaseScrub()
    {
        ScrubKey[0] = '\0';
        ScrubMoved  = false;
    }

    float ClampSpan(float Value, float Low, float High)
    {
        return Value < Low ? Low : (Value > High ? High : Value);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           EASING
//------------------------------------------------------------------------------------------------------------------------

float SolveEaseCurve(float Progress) { return SolveCubicBezier(Progress, 0.40f, 0.00f, 0.20f, 1.00f); }
float SolvePopCurve(float Progress)  { return SolveCubicBezier(Progress, 0.16f, 1.00f, 0.30f, 1.00f); }

void AdvanceTravel(float& Travel, bool Toward, float DeltaTime, float Seconds)
{
    const float Target = Toward ? 1.0f : 0.0f;
    const float Step   = Seconds > 0.0f ? DeltaTime / Seconds : 1.0f;
    Travel = Target > Travel ? ImMin(Target, Travel + Step) : ImMax(Target, Travel - Step);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        CACHED STATE
//------------------------------------------------------------------------------------------------------------------------

float ResolveFoldTravel(const char* Key, bool Open, float DeltaTime)
{
    const int Index = ResolveSlot(Key);
    if (Index < 0) { return Open ? 1.0f : 0.0f; }
    CacheSlot& Slot = Cache[Index];
    AdvanceTravel(Slot.Fold, Open, DeltaTime, Span::FoldSeconds);
    return SolveEaseCurve(Slot.Fold);
}

float ResolveCachedSpan(const char* Key)
{
    const int Index = ResolveSlot(Key);
    return Index < 0 ? 0.0f : Cache[Index].Span;
}

void RetainCachedSpan(const char* Key, float Span)
{
    const int Index = ResolveSlot(Key);
    if (Index >= 0) { Cache[Index].Span = Span; }
}

float ResolveEntryAge(const char* Key, float DeltaTime)
{
    const int Index = ResolveSlot(Key);
    if (Index < 0) { return 1.0f; }
    CacheSlot& Slot = Cache[Index];
    Slot.Age = ImMin(EntrySeconds, Slot.Age + DeltaTime);
    return Slot.Age / EntrySeconds;
}

void ResetEntryAge(const char* Key)
{
    const int Index = ResolveSlot(Key);
    if (Index >= 0) { Cache[Index].Age = 0.0f; }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            TEXT
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    struct FontFace
    {
        ImFont* Face;        // [-]  - Rasterised face
        float   PixelSize;   // [px] - Size it was rasterised at
    };

    FontFace FontTable[PaintFontMax] = {};
    int      FontTableCount          = 0;
}

void RegisterPaintFont(ImFont* Face, float PixelSize)
{
    if (Face == nullptr || FontTableCount >= PaintFontMax) return;
    FontTable[FontTableCount].Face      = Face;
    FontTable[FontTableCount].PixelSize = PixelSize;
    ++FontTableCount;
}

void ReclaimPaintFonts()
{
    // 🔴 The faces themselves belong to the atlas, which the host tears down. This only forgets the pointers, and it must run BEFORE the
    //    ImGui context dies or the next cycle would draw through freed faces.
    for (int Index = 0; Index < PaintFontMax; ++Index) { FontTable[Index] = FontFace{}; }
    FontTableCount = 0;
}

ImFont* ResolvePaintFont(float PixelSize)
{
    if (FontTableCount <= 0) { return ImGui::GetFont(); }
    int   Nearest  = 0;
    float Distance = FontTable[0].PixelSize - PixelSize;
    if (Distance < 0.0f) { Distance = -Distance; }
    for (int Index = 1; Index < FontTableCount; ++Index)
    {
        float Gap = FontTable[Index].PixelSize - PixelSize;
        if (Gap < 0.0f) { Gap = -Gap; }
        if (Gap < Distance) { Distance = Gap; Nearest = Index; }
    }
    return FontTable[Nearest].Face;
}

ImVec2 MeasureInk(float PixelSize, const char* Text)
{
    if (Text == nullptr || Text[0] == '\0') { return ImVec2(0.0f, PixelSize); }
    return ResolvePaintFont(PixelSize)->CalcTextSizeA(PixelSize, FLT_MAX, 0.0f, Text);
}

void RecordInk(ImDrawList* Draw, ImVec2 TopLeft, float PixelSize, ImU32 Tone, const char* Text)
{
    if (Text == nullptr || Text[0] == '\0') return;
    Draw->AddText(ResolvePaintFont(PixelSize), PixelSize, TopLeft, Tone, Text);
}

void RecordInkCentred(ImDrawList* Draw, ImVec2 Centre, float PixelSize, ImU32 Tone, const char* Text)
{
    if (Text == nullptr || Text[0] == '\0') return;
    const ImVec2 Extent = MeasureInk(PixelSize, Text);
    RecordInk(Draw, ImVec2(Centre.x - Extent.x * 0.5f, Centre.y - Extent.y * 0.5f), PixelSize, Tone, Text);
}

void RecordInkRight(ImDrawList* Draw, ImVec2 TopRight, float PixelSize, ImU32 Tone, const char* Text)
{
    if (Text == nullptr || Text[0] == '\0') return;
    const ImVec2 Extent = MeasureInk(PixelSize, Text);
    RecordInk(Draw, ImVec2(TopRight.x - Extent.x, TopRight.y), PixelSize, Tone, Text);
}

void RecordInkClipped(ImDrawList* Draw, ImVec2 TopLeft, float Width, float PixelSize, ImU32 Tone, const char* Text)
{
    if (Text == nullptr || Text[0] == '\0' || Width <= 0.0f) return;
    if (MeasureInk(PixelSize, Text).x <= Width)
    {
        RecordInk(Draw, TopLeft, PixelSize, Tone, Text);
        return;
    }

    // 📝 Trim to the widest prefix that leaves room for the ellipsis, matching CSS text-overflow rather than a hard clip.
    const float  Ellipsis = MeasureInk(PixelSize, "\xE2\x80\xA6").x;
    const float  Room     = Width - Ellipsis;
    char         Trimmed[192];
    int          Kept     = 0;
    const int    Length   = (int)strlen(Text);
    for (int Index = 0; Index < Length && Index < (int)sizeof(Trimmed) - 4; ++Index)
    {
        // 🔴 Never split a UTF-8 sequence: the labels carry middots and arrows, and a half-written glyph renders as a replacement box.
        if (((unsigned char)Text[Index] & 0xC0) == 0x80) { Trimmed[Index] = Text[Index]; continue; }
        Trimmed[Index] = '\0';
        if (MeasureInk(PixelSize, Trimmed).x > Room) { break; }
        Kept           = Index;
        Trimmed[Index] = Text[Index];
    }
    Trimmed[Kept] = '\0';
    strncat(Trimmed, "\xE2\x80\xA6", sizeof(Trimmed) - strlen(Trimmed) - 1);
    RecordInk(Draw, TopLeft, PixelSize, Tone, Trimmed);
}

float MeasureInkTracked(float PixelSize, float Tracking, const char* Text)
{
    if (Text == nullptr || Text[0] == '\0') { return 0.0f; }
    float Width = 0.0f;
    for (const char* Cursor = Text; *Cursor != '\0'; ++Cursor)
    {
        if (((unsigned char)*Cursor & 0xC0) == 0x80) continue;
        const char Glyph[2] = { *Cursor, '\0' };
        Width += MeasureInk(PixelSize, Glyph).x + Tracking;
    }
    return Width > 0.0f ? Width - Tracking : 0.0f;
}

void RecordInkTracked(ImDrawList* Draw, ImVec2 TopLeft, float PixelSize, float Tracking, ImU32 Tone, const char* Text)
{
    if (Text == nullptr || Text[0] == '\0') return;
    float Pen = TopLeft.x;
    for (const char* Cursor = Text; *Cursor != '\0'; ++Cursor)
    {
        const char Glyph[2] = { *Cursor, '\0' };
        RecordInk(Draw, ImVec2(Pen, TopLeft.y), PixelSize, Tone, Glyph);
        Pen += MeasureInk(PixelSize, Glyph).x + Tracking;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           GLYPHS
//------------------------------------------------------------------------------------------------------------------------

void RecordChevron(ImDrawList* Draw, ImVec2 Centre, float Width, ImU32 Tone, float Turns)
{
    // 📝 The 9x6 chevron path, rotated about its own centre. Weight 1.7 in a 9-unit box.
    const float Scale   = Width / 9.0f;
    const float Radians = Turns * 2.0f * 3.14159265358979f;
    const float Cos     = cosf(Radians);
    const float Sin     = sinf(Radians);
    const float Points[3][2] = { { 1.0f, 1.4f }, { 4.5f, 4.6f }, { 8.0f, 1.4f } };

    for (int Index = 0; Index < 3; ++Index)
    {
        const float X = (Points[Index][0] - 4.5f) * Scale;
        const float Y = (Points[Index][1] - 3.0f) * Scale;
        Draw->PathLineTo(ImVec2(Centre.x + X * Cos - Y * Sin, Centre.y + X * Sin + Y * Cos));
    }
    Draw->PathStroke(Tone, 0, 1.7f * Scale);
}

void RecordGlyph(ImDrawList* Draw, ImVec2 Centre, float Edge, PaintGlyph Glyph, ImU32 Tone)
{
    switch (Glyph)
    {
        case PaintGlyph::Chevron:
        {
            RecordChevron(Draw, Centre, Edge, Tone, 0.0f);
            break;
        }
        case PaintGlyph::Search:
        {
            // 14x14: circle r4.3 at (6,6) weight 1.4, then the shaft weight 1.5.
            const GlyphProjection To = ResolveProjection(Centre, Edge, 14.0f);
            StrokeCircle(Draw, To, ImVec2(6.0f, 6.0f), 4.3f, Tone, 1.4f);
            const float Shaft[] = { 9.2f, 9.2f, 12.4f, 12.4f };
            StrokePolyline(Draw, To, Shaft, 2, Tone, 1.5f, false);
            break;
        }
        case PaintGlyph::Cross:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 9.0f);
            const float Fall[]  = { 0.8f, 0.8f, 8.2f, 8.2f };
            const float Rise[]  = { 8.2f, 0.8f, 0.8f, 8.2f };
            StrokePolyline(Draw, To, Fall, 2, Tone, 1.6f, false);
            StrokePolyline(Draw, To, Rise, 2, Tone, 1.6f, false);
            break;
        }
        case PaintGlyph::Next:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            const float Path[] = { 9.0f, 5.0f, 16.0f, 12.0f, 9.0f, 19.0f };
            StrokePolyline(Draw, To, Path, 3, Tone, 2.0f, false);
            break;
        }
        case PaintGlyph::Previous:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            const float Path[] = { 15.0f, 5.0f, 8.0f, 12.0f, 15.0f, 19.0f };
            StrokePolyline(Draw, To, Path, 3, Tone, 2.0f, false);
            break;
        }
        case PaintGlyph::Plus:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            const float Upright[] = { 12.0f,  5.0f, 12.0f, 19.0f };
            const float Across[]  = {  5.0f, 12.0f, 19.0f, 12.0f };
            StrokePolyline(Draw, To, Upright, 2, Tone, 2.0f, false);
            StrokePolyline(Draw, To, Across,  2, Tone, 2.0f, false);
            break;
        }
        case PaintGlyph::Trash:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            const float Lip[]  = { 3.0f, 6.0f, 21.0f, 6.0f };
            const float Body[] = { 19.0f, 6.0f, 19.0f, 20.0f, 17.0f, 22.0f, 7.0f, 22.0f, 5.0f, 20.0f, 5.0f, 6.0f };
            const float Lid[]  = { 8.0f, 6.0f, 8.0f, 4.0f, 10.0f, 2.0f, 14.0f, 2.0f, 16.0f, 4.0f, 16.0f, 6.0f };
            StrokePolyline(Draw, To, Lip,  2, Tone, 1.7f, false);
            StrokePolyline(Draw, To, Body, 6, Tone, 1.7f, false);
            StrokePolyline(Draw, To, Lid,  6, Tone, 1.7f, false);
            break;
        }
        case PaintGlyph::Brush:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            const float Shaft[]  = { 9.0f, 12.0f, 18.0f, 3.0f, 20.4f, 2.4f, 22.0f, 4.0f, 21.4f, 6.4f, 13.0f, 15.0f };
            const float Ferrule[] = { 7.0f, 15.0f, 5.2f, 15.6f, 4.0f, 18.0f, 3.4f, 19.4f, 2.0f, 20.0f, 2.0f, 20.0f,
                                      3.4f, 21.4f, 5.4f, 22.0f, 7.0f, 22.0f, 10.0f, 20.6f, 11.0f, 18.0f, 10.4f, 16.0f, 7.0f, 15.0f };
            StrokePolyline(Draw, To, Shaft,   6, Tone, 1.7f, false);
            StrokePolyline(Draw, To, Ferrule, 13, Tone, 1.7f, false);
            break;
        }
        case PaintGlyph::Image:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            Draw->AddRect(To(3.0f, 3.0f), To(21.0f, 21.0f), Tone, 2.0f * To.Scale, 0, 1.7f * To.Scale);
            StrokeCircle(Draw, To, ImVec2(9.0f, 9.0f), 1.9f, Tone, 1.6f);
            const float Ridge[] = { 21.0f, 15.0f, 17.9f, 11.9f, 15.1f, 11.9f, 6.0f, 21.0f };
            StrokePolyline(Draw, To, Ridge, 4, Tone, 1.7f, false);
            break;
        }
        case PaintGlyph::Reload:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            StrokeArc(Draw, To, ImVec2(12.0f, 12.0f), 9.0f, -0.62f, 4.9f, Tone, 1.7f);
            const float Flag[] = { 21.0f, 3.0f, 21.0f, 9.0f, 15.0f, 9.0f };
            StrokePolyline(Draw, To, Flag, 3, Tone, 1.7f, false);
            break;
        }
        case PaintGlyph::Layers:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            const float Sheet[] = { 12.0f, 3.0f, 21.0f, 8.0f, 12.0f, 13.0f, 3.0f, 8.0f };
            const float Under[] = { 3.0f, 13.0f, 12.0f, 18.0f, 21.0f, 13.0f };
            StrokePolyline(Draw, To, Sheet, 4, Tone, 1.7f, true);
            StrokePolyline(Draw, To, Under, 3, Tone, 1.4f, false);
            break;
        }
        case PaintGlyph::Mask:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            StrokeCircle(Draw, To, ImVec2(12.0f, 12.0f), 9.0f, Tone, 1.7f);
            // 📝 The filled half is the source's `M12 3a9 9 0 0 0 0 18z` — the LEFT semicircle, swept anticlockwise.
            Draw->PathLineTo(To(12.0f, 3.0f));
            for (int Index = 0; Index <= 24; ++Index)
            {
                const float T = -1.5707963f - 3.14159265f * ((float)Index / 24.0f);
                Draw->PathLineTo(To(12.0f + cosf(T) * 9.0f, 12.0f + sinf(T) * 9.0f));
            }
            Draw->PathFillConvex(Tone);
            break;
        }
        case PaintGlyph::Sliders:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            const float UpperLeft[]  = {  4.0f,  7.0f, 13.0f,  7.0f };
            const float UpperRight[] = { 17.0f,  7.0f, 20.0f,  7.0f };
            const float LowerLeft[]  = {  4.0f, 17.0f,  7.0f, 17.0f };
            const float LowerRight[] = { 11.0f, 17.0f, 20.0f, 17.0f };
            StrokePolyline(Draw, To, UpperLeft,  2, Tone, 1.7f, false);
            StrokePolyline(Draw, To, UpperRight, 2, Tone, 1.7f, false);
            StrokePolyline(Draw, To, LowerLeft,  2, Tone, 1.7f, false);
            StrokePolyline(Draw, To, LowerRight, 2, Tone, 1.7f, false);
            StrokeCircle(Draw, To, ImVec2(15.0f,  7.0f), 2.1f, Tone, 1.6f);
            StrokeCircle(Draw, To, ImVec2( 9.0f, 17.0f), 2.1f, Tone, 1.6f);
            break;
        }
        case PaintGlyph::Clock:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            StrokeCircle(Draw, To, ImVec2(12.0f, 12.0f), 8.5f, Tone, 1.6f);
            const float Hands[] = { 12.0f, 7.0f, 12.0f, 12.0f, 15.4f, 14.1f };
            StrokePolyline(Draw, To, Hands, 3, Tone, 1.7f, false);
            break;
        }
        case PaintGlyph::Undo:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            const float Flag[] = { 9.0f, 7.0f, 5.0f, 7.0f, 5.0f, 3.0f };
            StrokePolyline(Draw, To, Flag, 3, Tone, 1.7f, false);
            StrokeArc(Draw, To, ImVec2(12.0f, 12.0f), 8.0f, 3.5f, 0.55f, Tone, 1.7f);
            break;
        }
        case PaintGlyph::Redo:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            const float Flag[] = { 15.0f, 7.0f, 19.0f, 7.0f, 19.0f, 3.0f };
            StrokePolyline(Draw, To, Flag, 3, Tone, 1.7f, false);
            StrokeArc(Draw, To, ImVec2(12.0f, 12.0f), 8.0f, -0.35f, 2.6f, Tone, 1.7f);
            break;
        }
        case PaintGlyph::EyeOn:
        case PaintGlyph::EyeOff:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 14.0f);
            StrokeQuadratic(Draw, To, ImVec2(1.0f, 7.0f), ImVec2(7.0f,  1.2f), ImVec2(13.0f, 7.0f), Tone, 1.4f);
            StrokeQuadratic(Draw, To, ImVec2(1.0f, 7.0f), ImVec2(7.0f, 12.8f), ImVec2(13.0f, 7.0f), Tone, 1.4f);
            if (Glyph == PaintGlyph::EyeOn) { FillCircle(Draw, To, ImVec2(7.0f, 7.0f), 2.3f, Tone); }
            else
            {
                const float Slash[] = { 1.4f, 10.4f, 12.6f, 3.6f };
                StrokePolyline(Draw, To, Slash, 2, Tone, 1.5f, false);
            }
            break;
        }
        case PaintGlyph::Rename:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            const float Nib[]  = { 4.0f, 20.0f, 8.0f, 20.0f, 20.0f, 8.0f, 16.0f, 4.0f, 4.0f, 16.0f };
            const float Neck[] = { 14.0f, 6.0f, 18.0f, 10.0f };
            StrokePolyline(Draw, To, Nib,  5, Tone, 1.7f, true);
            StrokePolyline(Draw, To, Neck, 2, Tone, 1.4f, false);
            break;
        }
        case PaintGlyph::Copy:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            const float Front[] = { 9.0f, 9.0f, 20.0f, 9.0f, 20.0f, 20.0f, 9.0f, 20.0f };
            const float Back[]  = { 15.0f, 5.0f, 4.0f, 5.0f, 4.0f, 16.0f };
            StrokePolyline(Draw, To, Front, 4, Tone, 1.7f, true);
            StrokePolyline(Draw, To, Back,  3, Tone, 1.4f, false);
            break;
        }
        case PaintGlyph::Target:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            StrokeCircle(Draw, To, ImVec2(12.0f, 12.0f), 7.5f, Tone, 1.6f);
            const float North[] = { 12.0f,  2.5f, 12.0f,  5.5f };
            const float South[] = { 12.0f, 18.5f, 12.0f, 21.5f };
            const float West[]  = {  2.5f, 12.0f,  5.5f, 12.0f };
            const float East[]  = { 18.5f, 12.0f, 21.5f, 12.0f };
            StrokePolyline(Draw, To, North, 2, Tone, 1.7f, false);
            StrokePolyline(Draw, To, South, 2, Tone, 1.7f, false);
            StrokePolyline(Draw, To, West,  2, Tone, 1.7f, false);
            StrokePolyline(Draw, To, East,  2, Tone, 1.7f, false);
            FillCircle(Draw, To, ImVec2(12.0f, 12.0f), 2.1f, Tone);
            break;
        }
        case PaintGlyph::Swap:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            const float UpperHead[] = {  7.0f,  4.0f,  3.0f,  8.0f,  7.0f, 12.0f };
            const float UpperShaft[] = { 3.0f,  8.0f, 16.0f,  8.0f };
            const float LowerHead[] = { 17.0f, 20.0f, 21.0f, 16.0f, 17.0f, 12.0f };
            const float LowerShaft[] = { 21.0f, 16.0f, 8.0f, 16.0f };
            StrokePolyline(Draw, To, UpperHead,  3, Tone, 1.7f, false);
            StrokePolyline(Draw, To, UpperShaft, 2, Tone, 1.7f, false);
            StrokePolyline(Draw, To, LowerHead,  3, Tone, 1.7f, false);
            StrokePolyline(Draw, To, LowerShaft, 2, Tone, 1.7f, false);
            break;
        }
        case PaintGlyph::Tick:
        {
            const GlyphProjection To = ResolveProjection(Centre, Edge, 24.0f);
            const float Path[] = { 5.0f, 12.5f, 10.0f, 17.5f, 19.0f, 6.5f };
            StrokePolyline(Draw, To, Path, 3, Tone, 2.0f, false);
            break;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SHARED PAINTING
//------------------------------------------------------------------------------------------------------------------------

ImU32 BlendTone(ImU32 From, ImU32 To, float Amount)
{
    const float T = ClampSpan(Amount, 0.0f, 1.0f);
    const int   R = (int)((float)((From >>  0) & 0xFF) + ((float)((To >>  0) & 0xFF) - (float)((From >>  0) & 0xFF)) * T);
    const int   G = (int)((float)((From >>  8) & 0xFF) + ((float)((To >>  8) & 0xFF) - (float)((From >>  8) & 0xFF)) * T);
    const int   B = (int)((float)((From >> 16) & 0xFF) + ((float)((To >> 16) & 0xFF) - (float)((From >> 16) & 0xFF)) * T);
    const int   A = (int)((float)((From >> 24) & 0xFF) + ((float)((To >> 24) & 0xFF) - (float)((From >> 24) & 0xFF)) * T);
    return IM_COL32(R, G, B, A);
}

ImU32 ApplyOpacity(ImU32 Tone, float Amount)
{
    const int Alpha = (int)((float)((Tone >> 24) & 0xFF) * ClampSpan(Amount, 0.0f, 1.0f));
    return (Tone & 0x00FFFFFF) | ((ImU32)Alpha << 24);
}

ImU32 ResolveGreyTone(float Level)
{
    const int Value = (int)(ClampSpan(Level, 0.0f, 1.0f) * 255.0f + 0.5f);
    return IM_COL32(Value, Value, Value, 255);
}

void RecordChequer(ImDrawList* Draw, ImVec2 TopLeft, ImVec2 BottomRight, float Cell, ImU32 Base, ImU32 Alternate, float Rounding)
{
    Draw->PushClipRect(TopLeft, BottomRight, true);
    Draw->AddRectFilled(TopLeft, BottomRight, Base, Rounding);
    const float Half = Cell * 0.5f;
    int Row = 0;
    for (float Y = TopLeft.y; Y < BottomRight.y; Y += Half, ++Row)
    {
        int Column = 0;
        for (float X = TopLeft.x; X < BottomRight.x; X += Half, ++Column)
        {
            if (((Row + Column) & 1) != 0) continue;
            Draw->AddRectFilled(ImVec2(X, Y),
                                ImVec2(ImMin(X + Half, BottomRight.x), ImMin(Y + Half, BottomRight.y)),
                                Alternate);
        }
    }
    Draw->PopClipRect();
}

void RecordDashedRect(ImDrawList* Draw, ImVec2 TopLeft, ImVec2 BottomRight, ImU32 Tone, float Rounding, float Dash, float Gap)
{
    (void)Rounding;
    const float Stride = Dash + Gap;
    // 📝 Four straight runs; the source's radius is small enough at this stroke weight that the corners read as square dashes anyway.
    for (float X = TopLeft.x; X < BottomRight.x; X += Stride)
    {
        const float End = ImMin(X + Dash, BottomRight.x);
        Draw->AddLine(ImVec2(X, TopLeft.y),     ImVec2(End, TopLeft.y),     Tone);
        Draw->AddLine(ImVec2(X, BottomRight.y), ImVec2(End, BottomRight.y), Tone);
    }
    for (float Y = TopLeft.y; Y < BottomRight.y; Y += Stride)
    {
        const float End = ImMin(Y + Dash, BottomRight.y);
        Draw->AddLine(ImVec2(TopLeft.x,     Y), ImVec2(TopLeft.x,     End), Tone);
        Draw->AddLine(ImVec2(BottomRight.x, Y), ImVec2(BottomRight.x, End), Tone);
    }
}

bool RecordHitZone(const char* Id, ImVec2 TopLeft, ImVec2 Size, bool* Hovered, bool* Held)
{
    if (Size.x <= 0.0f || Size.y <= 0.0f)
    {
        if (Hovered != nullptr) { *Hovered = false; }
        if (Held    != nullptr) { *Held    = false; }
        return false;
    }
    ImGui::SetCursorScreenPos(TopLeft);
    const bool Pressed = ImGui::InvisibleButton(Id, Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (Hovered != nullptr) { *Hovered = ImGui::IsItemHovered(); }
    if (Held    != nullptr) { *Held    = ImGui::IsItemActive(); }
    return Pressed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONTROLS
//------------------------------------------------------------------------------------------------------------------------

bool RecordSwitch(ImDrawList* Draw, ImVec2 TopLeft, bool On, const char* Id)
{
    constexpr float Width  = 34.0f;
    constexpr float Height = 19.0f;
    constexpr float Nub    = 15.0f;

    bool Hovered = false, Held = false;
    const bool Pressed = RecordHitZone(Id, TopLeft, ImVec2(Width, Height), &Hovered, &Held);

    const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + Height);
    Draw->AddRectFilled(TopLeft, BottomRight, On ? Tone::Accent : Tone::TileOn, Height * 0.5f);

    // 📝 The source stretches the nub to 19 px while held and slides its left edge back by 4 px when the switch is on.
    const float NubWidth = Held ? 19.0f : Nub;
    const float NubLeft  = On ? (TopLeft.x + Width - 2.0f - NubWidth) : (TopLeft.x + 2.0f);
    Draw->AddRectFilled(ImVec2(NubLeft, TopLeft.y + 2.0f),
                        ImVec2(NubLeft + NubWidth, TopLeft.y + 2.0f + Nub),
                        Tone::Knob, Nub * 0.5f);
    return Pressed;
}

int RecordSegmentRow(ImDrawList*        Draw,
                     ImVec2             TopLeft,
                     float              Width,
                     const char* const* Options,
                     int                OptionCount,
                     int                Current,
                     const char*        Id)
{
    constexpr float Height = 26.0f;
    if (OptionCount <= 0) { return -1; }

    const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + Height);
    Draw->AddRectFilled(TopLeft, BottomRight, Tone::Tile, Height * 0.5f);
    Draw->AddRect(TopLeft, BottomRight, Tone::Border, Height * 0.5f);

    int         Pressed = -1;
    const float Step    = Width / (float)OptionCount;
    for (int Index = 0; Index < OptionCount; ++Index)
    {
        const ImVec2 SegmentTL(TopLeft.x + Step * (float)Index, TopLeft.y);
        const ImVec2 SegmentBR(SegmentTL.x + Step, BottomRight.y);

        char SegmentId[CacheKeyLength];
        snprintf(SegmentId, CacheKeyLength, "%s#%d", Id, Index);

        bool Hovered = false;
        if (RecordHitZone(SegmentId, SegmentTL, ImVec2(Step, Height), &Hovered)) { Pressed = Index; }

        const bool On = (Index == Current);
        if (On)      { Draw->AddRectFilled(SegmentTL, SegmentBR, Tone::Accent, Height * 0.5f); }
        else if (Hovered) { Draw->AddRectFilled(SegmentTL, SegmentBR, Tone::TileHigh, Height * 0.5f); }

        const ImU32 Ink = On ? Tone::OnAccent : (Hovered ? Tone::Ink : Tone::Muted);
        RecordInkCentred(Draw, ImVec2((SegmentTL.x + SegmentBR.x) * 0.5f, (SegmentTL.y + SegmentBR.y) * 0.5f),
                         10.5f, Ink, Options[Index]);
    }
    return Pressed;
}

namespace
{
    // 📝 One shared scrub body: the pixel-per-unit speed matches the source's (max-min)/380.
    ScrubOutcome AdvanceScrub(const char* Id,
                              bool        Armed,
                              float&      Value,
                              float       Minimum,
                              float       Maximum,
                              float       Seed,
                              bool        SeedRequested)
    {
        ScrubOutcome Outcome = { false, false };
        const ImGuiIO& Io = ImGui::GetIO();

        if (Armed && !MatchScrubKey(Id))
        {
            ArmScrub(Id, Io.MousePos.x, Value);
            if (SeedRequested)
            {
                const float Next = ClampSpan(Seed, Minimum, Maximum);
                if (Next != Value) { Value = Next; Outcome.Changed = true; ScrubMoved = true; }
                ScrubAnchorValue = Value;
            }
        }

        if (!MatchScrubKey(Id)) { return Outcome; }

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const float Speed = (Maximum - Minimum) / 380.0f;
            const float Next  = ClampSpan(ScrubAnchorValue + (Io.MousePos.x - ScrubAnchorX) * Speed, Minimum, Maximum);
            if (Next != Value) { Value = Next; Outcome.Changed = true; ScrubMoved = true; }
            return Outcome;
        }

        // 📝 Release: exactly one commit, and only when the drag actually moved — the single point a revision is recorded.
        Outcome.Committed = ScrubMoved;
        ReleaseScrub();
        return Outcome;
    }
}

ScrubOutcome RecordScrubSlider(ImDrawList* Draw,
                               ImVec2      TopLeft,
                               float       Width,
                               const char* Id,
                               float&      Value,
                               float       Minimum,
                               float       Maximum,
                               const char* Unit,
                               int         Decimals)
{
    constexpr float Height    = 28.0f;
    constexpr float PillWidth = 78.0f;
    constexpr float Gap       = 10.0f;
    constexpr float TrackEdge = 20.0f;
    constexpr float KnobEdge  = 22.0f;

    const float Range = (Maximum - Minimum) != 0.0f ? (Maximum - Minimum) : 1.0f;

    // -- Value pill: black number segment + lighter unit segment ------------------------------------------------------
    const ImVec2 PillTL = TopLeft;
    const ImVec2 PillBR(PillTL.x + PillWidth, PillTL.y + Height);
    const float  SideLeft = PillBR.x - Span::SideSegment;

    Draw->AddRectFilled(PillTL, PillBR, Tone::Value, Height * 0.5f);
    Draw->PushClipRect(ImVec2(SideLeft, PillTL.y), PillBR, true);
    Draw->AddRectFilled(PillTL, PillBR, Tone::ValueSide, Height * 0.5f);
    Draw->PopClipRect();

    char Readout[32];
    snprintf(Readout, sizeof(Readout), Decimals > 0 ? "%.*f" : "%.0f", Decimals, Value);
    RecordInkCentred(Draw, ImVec2((PillTL.x + SideLeft) * 0.5f, PillTL.y + Height * 0.5f), 11.5f, Tone::Knob, Readout);
    if (Unit != nullptr)
    {
        RecordInkCentred(Draw, ImVec2((SideLeft + PillBR.x) * 0.5f, PillTL.y + Height * 0.5f), 10.0f, Tone::Muted, Unit);
    }

    char NumberId[CacheKeyLength];
    snprintf(NumberId, CacheKeyLength, "%s##n", Id);
    bool NumberHeld = false;
    RecordHitZone(NumberId, PillTL, ImVec2(SideLeft - PillTL.x, Height), nullptr, &NumberHeld);

    // -- Track ---------------------------------------------------------------------------------------------------------
    const float  TrackLeft  = PillBR.x + Gap;
    const float  TrackWidth = ImMax(KnobEdge, TopLeft.x + Width - TrackLeft);
    const ImVec2 TrackTL(TrackLeft, TopLeft.y + (Height - TrackEdge) * 0.5f);
    const ImVec2 TrackBR(TrackLeft + TrackWidth, TrackTL.y + TrackEdge);

    char TrackId[CacheKeyLength];
    snprintf(TrackId, CacheKeyLength, "%s##t", Id);
    bool TrackHovered = false, TrackHeld = false;
    RecordHitZone(TrackId, TrackTL, ImVec2(TrackWidth, TrackEdge), &TrackHovered, &TrackHeld);

    // 📝 Pressing the track seeds the value from the press position before the drag; pressing the number does not.
    const ImGuiIO& Io = ImGui::GetIO();
    const float SeedValue = Minimum + ClampSpan((Io.MousePos.x - TrackTL.x) / ImMax(1.0f, TrackWidth), 0.0f, 1.0f) * Range;
    const ScrubOutcome Outcome = AdvanceScrub(Id, NumberHeld || TrackHeld, Value, Minimum, Maximum, SeedValue, TrackHeld && !NumberHeld);

    const float Travel = ClampSpan((Value - Minimum) / Range, 0.0f, 1.0f);
    Draw->AddRectFilled(TrackTL, TrackBR, Tone::Track, TrackEdge * 0.5f);
    if (Travel > 0.0f)
    {
        Draw->AddRectFilled(TrackTL, ImVec2(TrackTL.x + TrackWidth * Travel, TrackBR.y),
                            TrackHovered ? Tone::FillHover : Tone::Fill, TrackEdge * 0.5f);
    }
    const ImVec2 Knob(TrackTL.x + TrackWidth * Travel, (TrackTL.y + TrackBR.y) * 0.5f);
    const float  KnobRadius = (TrackHeld ? KnobEdge * 1.14f : KnobEdge) * 0.5f;
    Draw->AddCircleFilled(Knob, KnobRadius, Tone::Knob);
    Draw->AddCircle(Knob, KnobRadius, IM_COL32(0, 0, 0, 102));
    return Outcome;
}

ScrubOutcome RecordScrubPill(ImDrawList* Draw,
                             ImVec2      TopLeft,
                             ImVec2      Size,
                             const char* Id,
                             float&      Value,
                             float       Minimum,
                             float       Maximum,
                             const char* Suffix)
{
    bool Hovered = false, Held = false;
    RecordHitZone(Id, TopLeft, Size, &Hovered, &Held);
    const ScrubOutcome Outcome = AdvanceScrub(Id, Held, Value, Minimum, Maximum, Value, false);

    const ImVec2 BottomRight(TopLeft.x + Size.x, TopLeft.y + Size.y);
    Draw->AddRectFilled(TopLeft, BottomRight, Tone::Value, Size.y * 0.5f);

    char Readout[32];
    snprintf(Readout, sizeof(Readout), "%.0f%s", Value, Suffix != nullptr ? Suffix : "");
    RecordInkCentred(Draw, ImVec2((TopLeft.x + BottomRight.x) * 0.5f, (TopLeft.y + BottomRight.y) * 0.5f),
                     10.5f, (Hovered || Held) ? Tone::Ink : Tone::Muted, Readout);
    return Outcome;
}

bool RecordDropFace(ImDrawList* Draw, ImVec2 TopLeft, float Width, const char* Label, bool Open, const char* Id)
{
    constexpr float Height = 28.0f;
    const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + Height);
    const float  CapLeft = BottomRight.x - Span::SideSegment;

    bool Hovered = false;
    const bool Pressed = RecordHitZone(Id, TopLeft, ImVec2(Width, Height), &Hovered);

    Draw->AddRectFilled(TopLeft, BottomRight, Tone::Value, Height * 0.5f);
    Draw->PushClipRect(ImVec2(CapLeft, TopLeft.y), BottomRight, true);
    Draw->AddRectFilled(TopLeft, BottomRight, Tone::ValueSide, Height * 0.5f);
    Draw->PopClipRect();

    RecordInkClipped(Draw, ImVec2(TopLeft.x + 12.0f, TopLeft.y + (Height - 11.5f) * 0.5f - 1.0f),
                     CapLeft - TopLeft.x - 16.0f, 11.5f, Tone::Ink, Label);
    RecordChevron(Draw, ImVec2((CapLeft + BottomRight.x) * 0.5f, TopLeft.y + Height * 0.5f),
                  9.0f, Hovered ? Tone::Ink : Tone::Muted, Open ? 0.5f : 0.0f);
    return Pressed;
}

bool RecordSwatchRow(ImDrawList* Draw, ImVec2 TopLeft, ImU32 Colour, const char* Hex, const char* Id)
{
    constexpr float SwatchWidth  = 32.0f;
    constexpr float SwatchHeight = 20.0f;

    bool Hovered = false;
    const bool Pressed = RecordHitZone(Id, TopLeft, ImVec2(SwatchWidth + 8.0f + 60.0f, SwatchHeight), &Hovered);

    const float Grow = Hovered ? 1.07f : 1.0f;
    const ImVec2 Centre(TopLeft.x + SwatchWidth * 0.5f, TopLeft.y + SwatchHeight * 0.5f);
    const ImVec2 SwatchTL(Centre.x - SwatchWidth * 0.5f * Grow, Centre.y - SwatchHeight * 0.5f * Grow);
    const ImVec2 SwatchBR(Centre.x + SwatchWidth * 0.5f * Grow, Centre.y + SwatchHeight * 0.5f * Grow);
    Draw->AddRectFilled(SwatchTL, SwatchBR, Colour, 6.0f);
    Draw->AddRect(SwatchTL, SwatchBR, Tone::Border, 6.0f);

    RecordInk(Draw, ImVec2(TopLeft.x + SwatchWidth + 8.0f, TopLeft.y + 4.0f), 11.0f, Tone::Muted, Hex);
    return Pressed;
}

bool RecordGhostCall(ImDrawList* Draw, ImVec2 TopLeft, float Width, PaintGlyph Glyph, const char* Label, const char* Id)
{
    constexpr float Height = 30.0f;
    const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + Height);

    bool Hovered = false;
    const bool Pressed = RecordHitZone(Id, TopLeft, ImVec2(Width, Height), &Hovered);

    if (Hovered) { Draw->AddRectFilled(TopLeft, BottomRight, Tone::MarkerSoft, 8.0f); }
    RecordDashedRect(Draw, TopLeft, BottomRight, Hovered ? Tone::Marker : Tone::Border, 8.0f, 5.0f, 4.0f);

    const ImU32  Ink   = Hovered ? Tone::Ink : Tone::Muted;
    const float  Text  = MeasureInk(11.0f, Label).x;
    const float  Total = 13.0f + 7.0f + Text;
    const float  Left  = TopLeft.x + (Width - Total) * 0.5f;
    RecordGlyph(Draw, ImVec2(Left + 6.5f, TopLeft.y + Height * 0.5f), 13.0f, Glyph, Ink);
    RecordInk(Draw, ImVec2(Left + 20.0f, TopLeft.y + (Height - 11.0f) * 0.5f - 1.0f), 11.0f, Ink, Label);
    return Pressed;
}

bool RecordIconButton(ImDrawList* Draw,
                      ImVec2      TopLeft,
                      PaintGlyph  Glyph,
                      const char* Id,
                      bool        Destructive,
                      bool        Available)
{
    constexpr float Edge = 24.0f;
    const ImVec2 BottomRight(TopLeft.x + Edge, TopLeft.y + Edge);

    bool Hovered = false;
    bool Pressed = RecordHitZone(Id, TopLeft, ImVec2(Edge, Edge), &Hovered);
    if (!Available) { Pressed = false; Hovered = false; }

    const ImU32 Fill   = Hovered ? (Destructive ? IM_COL32(224, 90, 90, 41) : Tone::TileHigh) : Tone::Head;
    const ImU32 Edging = Hovered ? (Destructive ? IM_COL32(224, 90, 90, 102) : Tone::HairStrong) : Tone::Border;
    ImU32       Ink    = Hovered ? (Destructive ? Tone::Danger : Tone::Ink) : Tone::Muted;
    if (!Available) { Ink = ApplyOpacity(Tone::Muted, 0.28f); }

    Draw->AddRectFilled(TopLeft, BottomRight, Available ? Fill : ApplyOpacity(Tone::Head, 0.28f), 6.0f);
    Draw->AddRect(TopLeft, BottomRight, Available ? Edging : ApplyOpacity(Tone::Border, 0.28f), 6.0f);
    RecordGlyph(Draw, ImVec2((TopLeft.x + BottomRight.x) * 0.5f, (TopLeft.y + BottomRight.y) * 0.5f), 13.0f, Glyph, Ink);
    return Pressed;
}

float RecordSectionCaption(ImDrawList* Draw, ImVec2 TopLeft, float Width, const char* Label)
{
    constexpr float PadTop    = 9.0f;
    constexpr float PadBottom = 5.0f;
    constexpr float Size      = 9.0f;

    RecordInkTracked(Draw, ImVec2(TopLeft.x + 2.0f, TopLeft.y + PadTop), Size, 1.1f, Tone::Faint, Label);
    const float RuleLeft = TopLeft.x + 2.0f + MeasureInkTracked(Size, 1.1f, Label) + 8.0f;
    const float RuleY    = TopLeft.y + PadTop + Size * 0.5f;
    if (RuleLeft < TopLeft.x + Width) { Draw->AddLine(ImVec2(RuleLeft, RuleY), ImVec2(TopLeft.x + Width, RuleY), Tone::Hair); }
    return PadTop + Size + PadBottom;
}

}   // namespace PaintLayerSequenceValidation
