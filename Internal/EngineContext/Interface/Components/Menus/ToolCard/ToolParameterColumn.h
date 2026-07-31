/*==============================================================================================================================================
                                                        TOOLPARAMETERCOLUMN.H
==============================================================================================================================================*/
// 🧩 The card's right column in its SECOND slide: the open tool's parameter rows, one widget per row across the six kinds the catalogue authors.
//    ⚠️ A descriptor is immutable authored data — it states a parameter's DEFAULT, never its current reading. The live values therefore live in a
//    separate state block the caller owns, so the same `constexpr` catalogue can drive several cards at once and reopening a tool can decide for
//    itself whether to resume where the reader left off or return to the authored defaults.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLPARAMETERCOLUMN_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLPARAMETERCOLUMN_H

#include "imgui.h"

#include "ToolCardSpecification.h"

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The widest parameter list any one tool authors, plus headroom. A tool exceeding this trips the assert in the state reset.
constexpr int ToolParameterStateLimit = 12;   // [idx] - rows one tool's state block can carry


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One row's live reading. A flat aggregate mirroring ToolParameterDescriptor's flatness — which member matters is decided by the
//    descriptor's Category, so this never has to say which kind it is.
struct ToolParameterState
{
    float Reading;                                // [-]  - Slider
    int   ChosenOption;                           // [idx]- Segmented / AxisChoice / Swatches
    bool  Activation;                             // [-]  - Toggle
    bool  Negated;                                // [-]  - AxisChoice: the negate pill
    bool  TargetArmed[ToolSnapTargetLimit];       // [-]  - SnapTargets
};


// 📝 The live readings for the open tool, plus which tool they belong to. The identity is carried so the column can tell "the reader
//    is still adjusting this tool" from "a different tool just opened and these readings are stale" — without it, opening a second
//    tool would inherit the first one's slider positions.
struct ToolParameterBlock
{
    int                Band;                                   // [idx]- band the readings belong to; -1 when unbound
    int                Tile;                                   // [idx]- tile within it; -1 for a single-shot band's own rows
    ToolParameterState Rows[ToolParameterStateLimit];           // [-]  - per-row readings
    int                RowCount;                               // [idx]- rows currently bound
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Seed a state block from a parameter table's authored defaults, and stamp it with the tool it belongs to. Idempotent for the same
// identity: calling it every frame is safe and does NOT stomp the reader's adjustments — it reseeds only when the identity changes.
void BindToolParameterBlock(ToolParameterBlock&            Block,
                            const ToolParameterDescriptor* Parameters,
                            int                            ParameterCount,
                            int                            Band,
                            int                            Tile);

// The height the rows occupy, for the scroll range. Measured without drawing so the shell can size its body before painting.
[[nodiscard]] float MeasureToolParameterHeight(const ToolParameterDescriptor* Parameters,
                                               int                            ParameterCount,
                                               const ToolCardMetrics&         Metrics);

// Draw the parameter rows and fold any interaction straight into Block. Returns true when a reading changed this frame, which is what
// a caller watches to know the pending operation needs re-previewing.
bool InscribeToolParameterColumn(const SvgIconRegistry*         Icons,
                                 const ToolParameterDescriptor* Parameters,
                                 int                            ParameterCount,
                                 ToolParameterBlock&            Block,
                                 ImVec2                         Origin,
                                 float                          ColumnWidth,
                                 const ToolCardPalette&         Palette,
                                 const ToolCardMetrics&         Metrics);

// Draw the stand-in a tool with NO parameters gets: a centred note naming the command instead of an empty pane. A void pane reads as
// a load failure, so a tool that genuinely takes no options says so.
void InscribeToolParameterVoidNote(const SvgIconRegistry* Icons,
                                   const char*            GlyphName,
                                   const char*            Label,
                                   ImVec2                 Origin,
                                   float                  ColumnWidth,
                                   float                  ColumnHeight,
                                   const ToolCardPalette& Palette);

} // namespace Frontier

#endif
