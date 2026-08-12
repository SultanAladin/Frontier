/*==============================================================================================================================================
                                                            PAINTSURFACECHROME.H
==============================================================================================================================================*/
// 🧩 The drawing vocabulary the paint layer card is built from: the prototype's token block, its two easing curves, its stroked glyph set and
//    the handful of controls every pane reuses (switch, segmented row, scrub slider, dropdown face, swatch, fold card, chip, slot, ghost call).
//
// 🔴 The tones below are the prototype's OWN `:root` block, carried locally on purpose. The card is a 1:1 port of a surface that declares its
//    own palette — including the two red tones (`--danger` and the brighter destructive-text red) that ColorPaletteDescriptor has no field for.
//    Reading the engine theme here would silently retune every value away from the source. The engine ThemeConfiguration still travels through
//    the panel so the host stays uniform with its siblings; it simply is not what these surfaces paint with.
//
// 📝 Folds animate against a CACHED body height rather than a measured one: a body is drawn once, its consumed height is remembered under its
//    call-site key, and the next cycle animates the clip against that. This is what the source gets from `grid-template-rows: 1fr -> 0fr` — no
//    height measuring, no jump on first paint — and it costs one cycle of lag the eye never sees.

#pragma once
#ifndef FRONTIER_PAINTLAYERSEQUENCEVALIDATION_PAINTSURFACECHROME_H
#define FRONTIER_PAINTLAYERSEQUENCEVALIDATION_PAINTSURFACECHROME_H

#include "imgui.h"

namespace PaintLayerSequenceValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            TONES
//------------------------------------------------------------------------------------------------------------------------

// 📝 One entry per `--token` of the prototype's :root block, same order.
namespace Tone
{
    constexpr ImU32 Desk         = IM_COL32(  0,   0,   0, 255);   // --desk
    constexpr ImU32 Panel        = IM_COL32( 14,  14,  14, 255);   // --panel
    constexpr ImU32 PanelDeep    = IM_COL32( 11,  11,  11, 255);   // --panel-2
    constexpr ImU32 Head         = IM_COL32( 18,  18,  20, 255);   // --head
    constexpr ImU32 HeadHover    = IM_COL32( 23,  23,  26, 255);   // .pane-head.step:hover
    constexpr ImU32 Border       = IM_COL32( 28,  28,  28, 255);   // --border
    constexpr ImU32 BorderHover  = IM_COL32( 35,  35,  35, 255);   // .card:hover
    constexpr ImU32 Hair         = IM_COL32( 22,  22,  22, 255);   // --hair
    constexpr ImU32 HairStrong   = IM_COL32( 42,  42,  48, 255);   // --hair-strong
    constexpr ImU32 Tile         = IM_COL32( 20,  20,  20, 255);   // --tile
    constexpr ImU32 TileHigh     = IM_COL32( 26,  26,  26, 255);   // --tile-hi
    constexpr ImU32 TileOn       = IM_COL32( 31,  31,  31, 255);   // --tile-on
    constexpr ImU32 Value        = IM_COL32(  0,   0,   0, 255);   // --value
    constexpr ImU32 ValueSide    = IM_COL32( 20,  20,  20, 255);   // --value-side
    constexpr ImU32 Ink          = IM_COL32(237, 237, 237, 255);   // --ink
    constexpr ImU32 Muted        = IM_COL32(138, 138, 138, 255);   // --muted
    constexpr ImU32 Faint        = IM_COL32(106, 106, 106, 255);   // --faint
    constexpr ImU32 Accent       = IM_COL32(232, 232, 232, 255);   // --accent
    constexpr ImU32 OnAccent     = IM_COL32( 16,  16,  16, 255);   // --on-accent
    constexpr ImU32 Marker       = IM_COL32( 74, 144, 226, 255);   // --marker
    constexpr ImU32 MarkerSoft   = IM_COL32( 74, 144, 226,  36);   // --marker-soft
    constexpr ImU32 MarkerPanel  = IM_COL32( 13,  17,  23, 255);   // .sub.on background
    constexpr ImU32 Danger       = IM_COL32(224,  90,  90, 255);   // --danger
    constexpr ImU32 DangerLoud   = IM_COL32(255, 107, 107, 255);   // destructive text red
    constexpr ImU32 DangerWash   = IM_COL32(255, 107, 107,  31);   // .ctx-item.danger:hover
    constexpr ImU32 DangerEdge   = IM_COL32(255, 107, 107,  82);   // .act-danger border
    constexpr ImU32 DangerInk    = IM_COL32( 18,   4,   4, 255);   // .act-danger:hover text
    constexpr ImU32 Ok           = IM_COL32( 79, 209, 139, 255);   // --ok
    constexpr ImU32 Track        = IM_COL32( 20,  20,  20, 255);   // --track
    constexpr ImU32 Fill         = IM_COL32( 90,  90,  90, 255);   // --fill
    constexpr ImU32 FillHover    = IM_COL32(107, 107, 107, 255);   // .track:hover .f
    constexpr ImU32 Knob         = IM_COL32(255, 255, 255, 255);   // --knob
    constexpr ImU32 RowHover     = IM_COL32(255, 255, 255,  11);   // --row-hover
    constexpr ImU32 MenuFill     = IM_COL32( 20,  20,  22, 255);   // .menu / .ctx background
    constexpr ImU32 MenuSep      = IM_COL32( 34,  34,  34, 255);   // .ctx-sep
    constexpr ImU32 RadioEdge    = IM_COL32( 43,  43,  43, 255);   // .opt .radio border
    constexpr ImU32 SpineOff     = IM_COL32( 27,  27,  27, 255);   // hidden layer's bubble + spine
    constexpr ImU32 SpineOffInk  = IM_COL32( 92,  92,  92, 255);   // hidden layer's bubble text
    constexpr ImU32 EyeOff       = IM_COL32( 69,  69,  71, 255);   // .lrow.muted .eye
    constexpr ImU32 RailIdle     = IM_COL32( 36,  36,  36, 255);   // .hx-line
    constexpr ImU32 NodeEdge     = IM_COL32( 38,  38,  38, 255);   // .hx-node ring
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           METRICS
//------------------------------------------------------------------------------------------------------------------------

namespace Span
{
    constexpr float CardWidth      = 860.0f;   // [px] - --card-w
    constexpr float CardHeight     = 740.0f;   // [px] - --card-h
    constexpr float RailWidth      = 420.0f;   // [px] - --rail-w
    constexpr float HeadHeight     =  46.0f;   // [px] - --head-h
    constexpr float FootHeight     =  26.0f;   // [px] - --foot-h
    constexpr float PillHeight     =  28.0f;   // [px] - --pill-h
    constexpr float SideSegment    =  30.0f;   // [px] - --side-seg
    constexpr float CardRound      =  11.0f;   // [px] - --r-card
    constexpr float ShellRound     =  14.0f;   // [px] - .Card border-radius
    constexpr float FaceTabHeight  =  30.0f;   // [px] - .face-tabs
    constexpr float BodyPad        =   8.0f;   // [px] - .pane-body padding
    constexpr float SlideSeconds   = 0.38f;    // [s]  - .Track transition
    constexpr float SwipeSeconds   = 0.34f;    // [s]  - .swipe-track transition
    constexpr float FoldSeconds    = 0.26f;    // [s]  - .card-fold transition
    constexpr float ThumbSeconds   = 0.30f;    // [s]  - .ft-thumb transition
    constexpr float PopSeconds     = 0.17f;    // [s]  - unfold keyframe
}

// 📝 The two authored curves. Ease is --ease (.4,0,.2,1); Pop is --pop (.16,1,.3,1).
float SolveEaseCurve(float Progress);
float SolvePopCurve(float Progress);

// 📝 Step one 0..1 travel toward its target over `Seconds`.
void AdvanceTravel(float& Travel, bool Toward, float DeltaTime, float Seconds);

//------------------------------------------------------------------------------------------------------------------------
//                                                        CACHED STATE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Per-call-site fold travel, eased. Feeding it the body's cached height is what makes the collapse read as the source's grid-row animation.
float ResolveFoldTravel(const char* Key, bool Open, float DeltaTime);

// 📝 Body height remembered from the previous cycle under one call-site key. Zero until the body has been drawn once.
float ResolveCachedSpan(const char* Key);
void  RetainCachedSpan(const char* Key, float Span);

// 📝 Entry-animation age for one call-site key, advanced each cycle and reset by ResetEntryAge.
float ResolveEntryAge(const char* Key, float DeltaTime);
void  ResetEntryAge(const char* Key);

//------------------------------------------------------------------------------------------------------------------------
//                                                            TEXT
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The source draws at NINE distinct pixel sizes (9 · 9.5 · 10 · 10.5 · 11 · 11.5 · 12 · 12.5 · 13). Rasterising one face and scaling it
//    across that spread is what makes a port read as approximate — a 9 px caption drawn from a 13 px atlas is visibly soft beside its 12.5 px
//    title. The host rasterises a face per whole size and registers each here; every draw then picks the NEAREST registered size, so the
//    residual scale is at most half a pixel.
constexpr int PaintFontMax = 8;   // [idx] - Registered faces
void    RegisterPaintFont(ImFont* Face, float PixelSize);
void    ReclaimPaintFonts();
ImFont* ResolvePaintFont(float PixelSize);

ImVec2 MeasureInk(float PixelSize, const char* Text);
void   RecordInk(ImDrawList* Draw, ImVec2 TopLeft, float PixelSize, ImU32 Tone, const char* Text);
void   RecordInkCentred(ImDrawList* Draw, ImVec2 Centre, float PixelSize, ImU32 Tone, const char* Text);
void   RecordInkRight(ImDrawList* Draw, ImVec2 TopRight, float PixelSize, ImU32 Tone, const char* Text);

// 📝 Draws at most `Width` worth of text, trimming to "..." when it overruns — the port of CSS text-overflow: ellipsis.
void RecordInkClipped(ImDrawList* Draw, ImVec2 TopLeft, float Width, float PixelSize, ImU32 Tone, const char* Text);

// 📝 Letter-spaced small caps, drawn glyph by glyph. The source leans on letter-spacing for every uppercase label.
void RecordInkTracked(ImDrawList* Draw, ImVec2 TopLeft, float PixelSize, float Tracking, ImU32 Tone, const char* Text);
float MeasureInkTracked(float PixelSize, float Tracking, const char* Text);

//------------------------------------------------------------------------------------------------------------------------
//                                                           GLYPHS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The prototype's inline SVG set, transcribed as stroked paths. Each draws inside a square of `Edge` centred on `Centre`.
enum class PaintGlyph
{
    Chevron = 0,
    Search,
    Cross,
    Next,
    Previous,
    Plus,
    Trash,
    Brush,
    Image,
    Reload,
    Layers,
    Mask,
    Sliders,
    Clock,
    Undo,
    Redo,
    EyeOn,
    EyeOff,
    Rename,
    Copy,
    Target,
    Swap,
    Tick
};

void RecordGlyph(ImDrawList* Draw, ImVec2 Centre, float Edge, PaintGlyph Glyph, ImU32 Tone);

// 📝 The chevron rotates in three places — folded cards (-90 degrees), open carets (180 degrees) and dropdown caps (180 degrees).
void RecordChevron(ImDrawList* Draw, ImVec2 Centre, float Width, ImU32 Tone, float Turns);

//------------------------------------------------------------------------------------------------------------------------
//                                                       SHARED PAINTING
//------------------------------------------------------------------------------------------------------------------------

ImU32 BlendTone(ImU32 From, ImU32 To, float Amount);
ImU32 ApplyOpacity(ImU32 Tone, float Amount);
ImU32 ResolveGreyTone(float Level);

// 📝 The transparency chequer both the slot thumbnails (8 px) and the hero art (14 px) sit on.
void RecordChequer(ImDrawList* Draw, ImVec2 TopLeft, ImVec2 BottomRight, float Cell, ImU32 Base, ImU32 Alternate, float Rounding);

// 📝 A dashed hairline, used by the ghost calls' borders.
void RecordDashedRect(ImDrawList* Draw, ImVec2 TopLeft, ImVec2 BottomRight, ImU32 Tone, float Rounding, float Dash, float Gap);

// 📝 One invisible hit zone at an absolute rect. Returns true on release inside, and reports hover / hold separately.
bool RecordHitZone(const char* Id, ImVec2 TopLeft, ImVec2 Size, bool* Hovered = nullptr, bool* Held = nullptr);

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONTROLS
//------------------------------------------------------------------------------------------------------------------------

// 📝 34x19 pill with a 15 px nub. Returns true when pressed.
bool RecordSwitch(ImDrawList* Draw, ImVec2 TopLeft, bool On, const char* Id);

// 📝 26 px segmented row. Returns the pressed segment, or -1.
int RecordSegmentRow(ImDrawList*        Draw,
                     ImVec2             TopLeft,
                     float              Width,
                     const char* const* Options,
                     int                OptionCount,
                     int                Current,
                     const char*        Id);

// 📝 What one scrub cycle did. `Committed` fires once, on release after movement — the single point a revision is recorded.
struct ScrubOutcome
{
    bool Changed;      // [-] - The value moved during this cycle
    bool Committed;    // [-] - The drag ended having moved
};

// 📝 The source's slider: a 78 px value pill (number + unit segment) beside a 20 px track with a 22 px knob. Both the number and the track
//    scrub; pressing the track additionally seeds the value from the press position.
ScrubOutcome RecordScrubSlider(ImDrawList* Draw,
                               ImVec2      TopLeft,
                               float       Width,
                               const char* Id,
                               float&      Value,
                               float       Minimum,
                               float       Maximum,
                               const char* Unit,
                               int         Decimals);

// 📝 A bare scrub zone with no chrome of its own — the stack row's 41x19 opacity pill.
ScrubOutcome RecordScrubPill(ImDrawList* Draw,
                             ImVec2      TopLeft,
                             ImVec2      Size,
                             const char* Id,
                             float&      Value,
                             float       Minimum,
                             float       Maximum,
                             const char* Suffix);

// 📝 The closed face of a dropdown: label plus a 30 px cap carrying the chevron. Returns true when pressed. The menu itself is deferred.
bool RecordDropFace(ImDrawList* Draw, ImVec2 TopLeft, float Width, const char* Label, bool Open, const char* Id);

// 📝 A 32x20 colour swatch beside its hex text. Returns true when pressed.
bool RecordSwatchRow(ImDrawList* Draw, ImVec2 TopLeft, ImU32 Colour, const char* Hex, const char* Id);

// 📝 A dashed full-width call to action. Returns true when pressed.
bool RecordGhostCall(ImDrawList* Draw, ImVec2 TopLeft, float Width, PaintGlyph Glyph, const char* Label, const char* Id);

// 📝 A 24x24 bordered icon button. `Destructive` swaps the hover wash to the danger tone; `Available` false greys it and refuses the press.
bool RecordIconButton(ImDrawList* Draw,
                      ImVec2      TopLeft,
                      PaintGlyph  Glyph,
                      const char* Id,
                      bool        Destructive,
                      bool        Available);

// 📝 The 9 px uppercase rule caption that opens a sub-section. Returns the height it consumed.
float RecordSectionCaption(ImDrawList* Draw, ImVec2 TopLeft, float Width, const char* Label);

}   // namespace PaintLayerSequenceValidation

#endif
