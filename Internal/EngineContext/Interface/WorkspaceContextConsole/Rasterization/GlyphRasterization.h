/*==============================================================================================================================================
                                                          GLYPHRASTERIZATION.H
==============================================================================================================================================*/
// 🧩 The one place the console turns a glyph NAME into drawn pixels. Every strip (rail, action grid, probe, parameters) draws marks, and every one
//    would otherwise hand-build a registry key, pick a size, and decide what to do when the key misses. Centralised so a typo'd name is visibly a
//    typo rather than an invisible gap, and so the gated recolour rule lives in exactly one function. Ported from ToolGlyphInscription's
//    InscribeToolGlyph.
//
//    🔴 The stratum-badge function that sat beside InscribeToolGlyph did NOT come across: a badge names a modelling selection stratum, which is
//       gate-specific vocabulary the console no longer owns. A workspace that wants a badge in its header draws it itself and hands the console the
//       resulting texture through the pane header — the console rasterizes glyphs, not gate concepts.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_RASTERIZATION_GLYPHRASTERIZATION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_RASTERIZATION_GLYPHRASTERIZATION_H

#include "imgui.h"

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw one glyph at TopLeft, EdgePixels square. GatedCondition picks the faint variant — the prototype recolours a gated mark in CSS, which a
// rasterized texture cannot do after upload, so the two inks are two registered documents.
// 🔴 A missing key draws a visible placeholder rather than nothing: a glyph table and a cluster table drift apart silently otherwise, and an
//    unregistered name would leave a hole that reads as intentional whitespace at every size the console draws.
void InscribeConsoleGlyph(const SvgIconRegistry* Icons,
                          const char*            GlyphName,
                          ImVec2                 TopLeft,
                          float                  EdgePixels,
                          bool                   GatedCondition);

} // namespace Frontier

#endif
