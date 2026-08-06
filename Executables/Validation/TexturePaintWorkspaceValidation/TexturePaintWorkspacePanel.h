/*==============================================================================================================================================
                                                         TEXTUREPAINTWORKSPACEPANEL.H
==============================================================================================================================================*/
// 🧩 The combined texture-paint workspace: a LEFT layer rail, a CENTRE paint field the instrument card is summoned over on right-click, and a
//    RIGHT property column. Each third is a reused unit — the rail is LayerStackValidation's own panel, the field + summon is
//    TexturePaintValidation's, and the column stacks the ChannelPropertyValidation channel cards with the shared LayerProperties mask card
//    beneath. This is the composition step the side-by-side validations were each built to prove alone: one window that holds the whole
//    painting surface against the SLATE three-column layout in Documentation/Prototypes/TexturePaint2.html.
//
//    🔴 FRAME ORDER IS LOAD-BEARING. The summoned card must be driven at WINDOW scope with no BeginChild open — it draws through the window
//       draw list into a clip it pushes itself, and its panes hit-test manually against that clip — so the field + card are drawn FIRST and
//       the two scroll children are opened AFTER. They never overlap the card: the placement clamp keeps the box inside the centre field, so
//       nothing the children draw can land on it.
//
//    📝 All state is caller-owned and combined HERE, not in Frontier: each reused panel owns the contract of its own namespace's state, and
//       this struct is the one object a host keeps for the window's life. Types live in the app-local namespace TexturePaintWorkspaceValidation.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_TEXTUREPAINTWORKSPACEVALIDATION_TEXTUREPAINTWORKSPACEPANEL_H
#define FRONTIER_EXECUTABLES_VALIDATION_TEXTUREPAINTWORKSPACEVALIDATION_TEXTUREPAINTWORKSPACEPANEL_H

#include "LayerStackPanel.h"
#include "ChannelPropertyPanel.h"
#include "TexturePaintSummonedCard.h"

#include "EngineContext/Interface/Workspaces/TexturePaint/LayerPropertiesPanel.h"
#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

namespace Frontier { struct SvgIconRegistry; struct PaintIconStore; }

namespace TexturePaintWorkspaceValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STATE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The whole workspace between frames. Each member is the state contract of the reused unit that owns it; this struct adds no domain of
//    its own — the layout constants live in the panel .cpp, and the summon pose rides inside Summoned exactly as TexturePaintValidation owns it.
struct TexturePaintWorkspaceState
{
    LayerStackValidation::LayerStackState            Layers;     // [-]  - the left rail: rows, focus, folds, filter, add list
    ChannelPropertyValidation::ChannelPropertyState  Channels;   // [-]  - the right column's channel chips + cards
    Frontier::LayerPropertiesState                   Mask;       // [-]  - the right column's mask card
    TexturePaintValidation::TexturePaintSummonedState Summoned;  // [-]  - the centre card summon + its cross-frame pose
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Seed the whole workspace to its opening pose: the layer rail and channel cards to the prototype's samples, the mask card to the shared
// panel's sample, and the summoned card closed but with its opening family resolved (TexturePaintValidation::InitializeTexturePaintSummonedCard
// does the open-then-close that makes slide 2 valid before it is ever shown). Call once, before the frame loop.
void InitializeTexturePaintWorkspaceSample(TexturePaintWorkspaceState& State);

// Draw the whole workspace for one frame inside a docked-full host window: the left rail + right column as scroll children, and the centre
// field with the right-click-summoned instrument card over it. Icons supplies the reference glyphs and the strip store the landscape well art;
// a null / empty registry still renders (the panels fall back to primitives), and a null strip store falls back to the nib crops.
// 🔴 Must be called at WINDOW scope inside the host's Begin/End — the function opens its own children but never nests the summon under one.
void ConstructTexturePaintWorkspacePanel(const Frontier::ThemeConfiguration& Theme,
                                         TexturePaintWorkspaceState&        State,
                                         Frontier::SvgIconRegistry*         Icons,
                                         Frontier::PaintIconStore*          StripStore);

}   // namespace TexturePaintWorkspaceValidation

#endif
