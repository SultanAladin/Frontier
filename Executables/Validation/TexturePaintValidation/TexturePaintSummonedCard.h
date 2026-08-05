/*==============================================================================================================================================
                                                     TEXTUREPAINTSUMMONEDCARD.H
==============================================================================================================================================*/
// 🧩 The paint instrument card summoned over the texture-paint field, and the pointer arbitration that decides when it may open. The card is
//    EMBEDDED, not re-implemented: it is PaintToolValidation's own ConstructPaintToolPanel, compiled in place from the sibling app folder and
//    driven here with a right-press instead of that app's always-open centred placement. This unit holds only the cross-frame summon state —
//    whether the card stands, and where its box was anchored when it opened.
//
//    🔴 Right-click is the opener, and it is unclaimed on this surface. The sketch-model viewport had to move its own console to Q because a
//       right-DRAG there orbits the camera; a texture-paint field has no orbit, no pan and no dolly, so the right press carries no navigation
//       meaning and is free to summon.
//
//    🔴 The anchor is LATCHED, not applied on the press frame. The card reads a press outside its own box as a dismissal, so re-anchoring on the
//       same frame the press arrives would open the card and immediately close it. The press records a request; the placement is applied at the
//       tail of the frame, after the card has reported.
//
//    📝 The card is anchored by its CENTRE, because that is what ConstructPaintToolPanel takes. The stored anchor is therefore the centre point,
//       and the clamp works in centre space against the resolved box extent — no separate corner is kept.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_TEXTUREPAINTVALIDATION_TEXTUREPAINTSUMMONEDCARD_H
#define FRONTIER_EXECUTABLES_VALIDATION_TEXTUREPAINTVALIDATION_TEXTUREPAINTSUMMONEDCARD_H

#include "PaintToolPanel.h"

#include "imgui.h"

namespace Frontier { struct SvgIconRegistry; struct PaintIconStore; struct ThemeConfiguration; }

namespace TexturePaintValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything the summoned card keeps between frames. The card's own cross-frame state (focus, carousel travel, live readings, the resolved
//    schema) all lives inside PaintToolPanelState, which the card owns the contract for — this struct adds only the summon: whether it stands,
//    the latched centre, and the field rectangle a press must land inside to count.
struct TexturePaintSummonedState
{
    // -- The embedded paint card, closed until a right-press asks for it --
    Frontier::PaintToolPanelState Card = {};   // [-]  - the whole instrument card: rail, grid, carousel, options, previews

    bool  CardSummoned          = false;   // [-]  - whether the card is drawn this frame
    float CardCentreX           = 0.0f;    // [px] - latched centre of the card box, screen space
    float CardCentreY           = 0.0f;    // [px] - latched centre of the card box, screen space
    bool  ReanchorRequested     = false;   // [-]  - a right-press landed; the placement is applied at the frame tail

    // 📝 The paint field the press must land inside. Seeded every frame by ConfineTexturePaintField, so a resized window re-confines the
    //    gesture without this unit knowing the host's layout.
    float FieldLeft             = 0.0f;    // [px] - field rectangle, screen space
    float FieldTop              = 0.0f;    // [px]
    float FieldWidth            = 0.0f;    // [px]
    float FieldHeight           = 0.0f;    // [px]
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Seed the summon state: the card's opening family and instrument are resolved so slide 2 is valid before it is ever shown, then the card is
// closed. Called once, before the frame loop.
void InitializeTexturePaintSummonedCard(TexturePaintSummonedState& State);

// Confine the summon gesture to the field rectangle the host drew this frame. A right-press outside it is ignored, so the host's own chrome
// never summons the card.
void ConfineTexturePaintField(TexturePaintSummonedState& State, ImVec2 FieldOrigin, ImVec2 FieldSpan);

// Service the right-press, advance the card's travel, and draw the card while it stands. Draws nothing while closed.
// 🔴 Must be called at WINDOW scope, with no BeginChild active: the card draws through the window draw list into a clip it pushes itself, and
//    its panes hit-test manually against that clip — an open child would confine both to the child's rectangle.
void ConstructTexturePaintSummonedCard(const Frontier::ThemeConfiguration& Theme,
                                       TexturePaintSummonedState&          State,
                                       Frontier::SvgIconRegistry*          Icons,
                                       Frontier::PaintIconStore*           StripStore);

} // namespace TexturePaintValidation

#endif
