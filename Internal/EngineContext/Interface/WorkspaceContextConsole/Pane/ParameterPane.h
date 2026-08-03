/*==============================================================================================================================================
                                                            PARAMETERPANE.H
==============================================================================================================================================*/
// 🧩 The console's right column in its SECOND slide: the open action's parameter rows, one widget per row across the kinds the catalogue authors.
//    ⚠️ A descriptor is immutable authored data — it states a parameter's DEFAULT, never its current reading. The live values therefore live in a
//    separate ParameterBlock the caller owns, so the same constexpr catalogue can drive several consoles at once and reopening an action can decide
//    for itself whether to resume where the reader left off or return to the authored defaults. Ported from ToolParameterColumn; the block is keyed
//    by (Cluster, Action) instead of (Band, Tile), and carries paint's Dropdown reading.
//
//    🔴 The reveal-condition problem paint has does NOT live here: a parameter row that appears and disappears as the reader edits (paint's "Custom
//       grade reveals hardness") is resolved by the WORKSPACE before it hands its parameter table down — the console draws whatever rows it is
//       given. Keeping the reveal in the workspace is why the pane stays gate-agnostic like the rest of the console.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PANE_PARAMETERPANE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PANE_PARAMETERPANE_H

#include "imgui.h"

#include "../Descriptor/ParameterDescriptor.h"
#include "../Token/PaletteSpecification.h"
#include "../Token/MetricsSpecification.h"

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The widest parameter list any one action authors, plus headroom. An action exceeding this trips the assert in the block reset. Paint's peak
//    of 10 visible rows sets the floor; 12 keeps headroom.
constexpr int ConsoleParameterBlockLimit = 12;   // [idx] - rows one action's block can carry


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One row's live reading. A flat aggregate mirroring ParameterDescriptor's flatness — which member matters is decided by the descriptor's
//    Category, so this never has to say which kind it is. ChosenOption serves Segmented / Dropdown / AxisChoice / Swatches alike.
struct ParameterState
{
    float Reading;                                     // [-]  - Slider
    int   ChosenOption;                                // [idx]- Segmented / Dropdown / AxisChoice / Swatches
    bool  Activation;                                  // [-]  - Switch
    bool  Negated;                                     // [-]  - AxisChoice: the negate pill
    bool  TargetArmed[ParameterSnapTargetLimit];       // [-]  - SnapTargets
};


// 📝 The live readings for the open action, plus which action they belong to. The identity is carried so the pane can tell "the reader is still
//    adjusting this action" from "a different action just opened and these readings are stale" — without it, opening a second action would inherit
//    the first's slider positions.
struct ParameterBlock
{
    int            Cluster;                                  // [idx]- cluster the readings belong to; -1 when unbound
    int            Action;                                   // [idx]- action within it; -1 for a single-shot cluster's own rows
    ParameterState Rows[ConsoleParameterBlockLimit];         // [-]  - per-row readings
    int            RowCount;                                 // [idx]- rows currently bound
    // 🔴 Which Dropdown row has its list open, and it lives OUT here with the readings rather than inside the pane because an open list outlives the
    //    frame that opened it and must be closable by the identity change — a list left open across an action swap would float over rows that no
    //    longer exist. -1 for none, which is also what a reseed restores.
    int            OpenDropdownRow;                          // [idx]- row whose option list is open, or -1
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Seed a block from a parameter table's authored defaults and stamp it with the action it belongs to. Idempotent for the same identity: calling it
// every frame is safe and does NOT stomp the reader's adjustments — it reseeds only when the identity changes.
void BindParameterBlock(ParameterBlock&            Block,
                        const ParameterDescriptor* Parameters,
                        int                        ParameterCount,
                        int                        Cluster,
                        int                        Action);

// The height the rows occupy, for the scroll range. Measured without drawing so the pane can size its body before painting.
[[nodiscard]] float MeasureParameterPaneHeight(const ParameterDescriptor*  Parameters,
                                               int                         ParameterCount,
                                               const MetricsSpecification& Metrics);

// Draw the parameter rows and fold any interaction straight into Block. Returns true when a reading changed this frame, which a caller watches to
// know the pending action needs re-previewing.
// 📝 ListCeilingY is the absolute screen y an open dropdown's list may not run past — the options body's own bottom edge. Passed rather than derived
//    from Origin, which is scroll-shifted: adding a height to it would slide the list's floor up the card as the reader scrolls.
bool InscribeParameterPane(const SvgIconRegistry*      Icons,
                           const ParameterDescriptor*  Parameters,
                           int                         ParameterCount,
                           ParameterBlock&             Block,
                           ImVec2                      Origin,
                           float                       ColumnWidth,
                           float                       ListCeilingY,
                           const PaletteSpecification& Palette,
                           const MetricsSpecification& Metrics);

// Draw the stand-in an action with NO parameters gets: a centred note naming the command instead of an empty pane. A void pane reads as a load
// failure, so an action that genuinely takes no options says so.
void InscribeParameterVoidNote(const SvgIconRegistry*      Icons,
                               const char*                 GlyphName,
                               const char*                 Label,
                               ImVec2                      Origin,
                               float                       ColumnWidth,
                               float                       ColumnHeight,
                               const PaletteSpecification& Palette);

} // namespace Frontier

#endif
