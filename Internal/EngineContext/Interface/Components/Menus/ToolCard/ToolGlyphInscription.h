/*==============================================================================================================================================
                                                        TOOLGLYPHINSCRIPTION.H
==============================================================================================================================================*/
// 🧩 The one place a tool card turns a glyph NAME into drawn pixels. Every column (rail, grid, probe, parameters) draws marks, and every one of them
//    would otherwise hand-build a registry key, pick a size, and decide what to do when the key misses. Centralised so a typo'd name is visibly a
//    typo rather than an invisible gap, and so the gated recolour rule lives in exactly one function.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLGLYPHINSCRIPTION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLGLYPHINSCRIPTION_H

#include "imgui.h"

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw one tool glyph at TopLeft, EdgePixels square. GatedCondition picks the faint variant — the prototype recolours a gated
// mark in CSS, which a rasterized texture cannot do after upload, so the two inks are two registered documents.
// 🔴 A missing key draws a visible placeholder rather than nothing. A glyph table and a band table drift apart silently
//    otherwise: an unregistered name would leave a hole that reads as intentional whitespace at every size the card draws.
void InscribeToolGlyph(const SvgIconRegistry* Icons,
                       const char*            GlyphName,
                       ImVec2                 TopLeft,
                       float                  EdgePixels,
                       bool                   GatedCondition);

// Draw one stratum badge at TopLeft. A badge carries its own fixed palette over a black rounded backing, so unlike a tool glyph
// it has no gated variant — a badge names the current selection and is never unavailable.
void InscribeStratumBadge(const SvgIconRegistry* Icons,
                          const char*            StratumName,
                          ImVec2                 TopLeft,
                          float                  EdgePixels);

} // namespace Frontier

#endif
