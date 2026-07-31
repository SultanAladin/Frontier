/*==============================================================================================================================================
                                                            PAINTCARDSHELL.H
==============================================================================================================================================*/
// 🧩 The card's outer box and the two-slide carousel it scrolls: the pop-open animation, the clipped viewport, the translate between slides, and
//    the header/foot bands every pane shares. Carries NO pane content — the rail, grid, preview and options columns draw themselves into the
//    regions this hands back. Ported from Documentation/Prototypes/PaintToolMenu.html's `.tools` / `.tool-view` / `.tool-track` / `.pane-head`.
//
//    The prototype's structure, which the region names below mirror exactly:
//      .tool-view (clip)
//        └ .tool-track  (200% wide, translateX(-50%) to reach slide 2)
//            ├ slide 1: [ .rail 196px | .grid-pane 363px ]
//            └ slide 2: [ .preview-pane 196px | .options-pane 363px ]
//
//    🔴 The carousel is ONE translate of a double-width track, not two swapped panes, and the difference is visible: the prototype slides the
//       grid out as the options slide in, both fully drawn, sharing one continuous header band. Cross-fading two panes instead loses the
//       continuity that makes it read as one surface moving.

#pragma once
#ifndef FRONTIER_VALIDATION_PAINTTOOL_PAINTCARDSHELL_H
#define FRONTIER_VALIDATION_PAINTTOOL_PAINTCARDSHELL_H

#include "PaintCardSpecification.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which slide the carousel is resting on, or heading towards. The prototype toggles a `.to-options` class; the animation
//    between the two is carried by PaintCardShellState::SlidePhase rather than by a third enumerator, because a card
//    mid-translate is not in a third state — it is between two.
enum class PaintCardSlide
{
    Library = 0,   // slide 1 — the family rail beside the instrument grid
    Options,       // slide 2 — the standing preview beside the parameter rows
};


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The shell's animation state, owned by the caller so the card can be opened, closed and re-opened without the shell
//    holding global state. Both phases are normalised 0..1 and advanced by the shell from ImGui's delta time.
struct PaintCardShellState
{
    PaintCardSlide Slide      = PaintCardSlide::Library;   // [-] - which slide is current
    // 📝 0 at the Library edge, 1 at the Options edge. Eased, not linear — see the resolver.
    float          SlidePhase = 0.0f;                      // [-] - carousel progress
    // 📝 Drives the @keyframes pop on open: opacity plus a slight scale-up and downward settle.
    float          OpenPhase  = 0.0f;                      // [-] - 0 just-opened, 1 fully settled
    bool           IsOpen     = false;                     // [-] - false suppresses the card entirely
};


// 📝 One pane's drawable interior, handed back by the shell after it has drawn the pane's chrome. A pane draws its rows
//    inside Body and nothing outside it; the header and foot are the shell's business, so a pane cannot accidentally
//    disagree with its neighbour about band heights.
struct PaintPaneRegion
{
    ImVec2 BodyMinimum = ImVec2(0.0f, 0.0f);   // [px] - top-left of the scrolling interior, in screen space
    ImVec2 BodyMaximum = ImVec2(0.0f, 0.0f);   // [px] - bottom-right of the scrolling interior
    bool   IsVisible   = false;                // [-]  - false when fully translated off the clipped viewport
};


// 📝 Everything one pane header shows. Assembled by the caller because each of the four headers reads different sources —
//    the grid's icon is the selected instrument's nib crop, the rail's is the family band's glyph.
struct PaintPaneHeader
{
    const char* Title       = nullptr;   // [-] - the bold line
    const char* Subtitle    = nullptr;   // [-] - the faint second line; null draws a single-line header
    const char* TallyText   = nullptr;   // [-] - the pill on the right; null omits the pill
    bool        TallyAccent = false;     // [-] - .h-n.accent — the grid's tally is accent-inked, the rail's is muted
    // 📝 A back header instead of an icon+pill: the preview pane's header is the one clickable band on the card.
    bool        IsBackRow   = false;     // [-] - draws the chevron and hover fill, and reports its click
    ImTextureID IconTexture = 0;         // [-] - optional 24px glyph or 46px nib crop; 0 draws the empty black tile
    bool        IconIsNib   = false;     // [-] - .h-ic.nib — pale well fill + ring, and the art overflows the tile
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Advance the open and slide animations by one frame. Separate from drawing so a caller can step the card without drawing
// it, and so the eased phase is resolved once per frame rather than once per pane that reads it.
void AdvancePaintCardShell(PaintCardShellState& State, const PaintCardMetrics& Metrics, float DeltaSeconds);

// Request the carousel move to a slide. Idempotent — asking for the slide it is already on does nothing, so a held button
// cannot restart the translate every frame.
void RequestPaintCardSlide(PaintCardShellState& State, PaintCardSlide Slide);

// Draw the card box (fill, border, rounding, and the pop's scale/fade) at TopLeft and return the clipped viewport the
// track translates inside. Returns false when the card is closed, in which case nothing was drawn and no pane should be.
// 🔴 Must be called from a top-level ImGui window, NOT inside BeginChild: the shell pushes its own clip rectangle and draws
//    into the current window's draw list. ImGui::Begin cannot nest inside BeginChild, which is the constraint that shaped
//    this whole interface — the shell hands back rectangles and the panes draw with the draw list, rather than each pane
//    opening a child window of its own.
bool BeginPaintCardShell(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                         const PaintCardShellState& State, ImVec2 TopLeft);

// Close the clip the shell pushed. Must pair with a BeginPaintCardShell that returned true.
void EndPaintCardShell();

// Resolve one pane's rectangle for the current translate. Column selects the 196 px or 363 px column; Slide selects which
// half of the track it belongs to. The returned rectangle is already translated, so a pane never computes the carousel
// offset itself.
[[nodiscard]] PaintPaneRegion ResolvePaintPaneRegion(const PaintCardMetrics& Metrics, const PaintCardShellState& State,
                                                     ImVec2 TopLeft, PaintCardSlide Slide, bool IsLeftColumn,
                                                     float HeaderHeight, float FootHeight);

// Solve a CSS cubic-bezier at a normalised time. Shared rather than re-implemented per component: the card animates five
// different things on four named curves, and a second copy of this is a second chance to get one of them subtly wrong.
// 🔴 The return is NOT clamped to 0..1, and must not be. `cubic-bezier(.34,1.56,.64,1)` — the rail dot's growth — has
//    y1 = 1.56, so it peaks near 1.098 before settling. Clamping here would silently flatten every overshoot curve into an
//    ordinary ease-out, which is precisely the character those curves were chosen for.
[[nodiscard]] float SolvePaintCubicBezier(float Progress, float X1, float Y1, float X2, float Y2);

// Draw one pane's 53 px header band into a pane rectangle, and report whether a back row was clicked this frame.
// 📝 Height comes from the metrics rather than from a parameter: the prototype uses ONE rule for all four headers so the
//    band is continuous across the card, and letting a caller pass its own height is how that continuity gets broken.
bool ConstructPaintPaneHeader(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                              const PaintPaneHeader& Header, ImVec2 PaneMinimum, float PaneWidth);

} // namespace Frontier

#endif
