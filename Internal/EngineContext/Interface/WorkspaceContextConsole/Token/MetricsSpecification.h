/*==============================================================================================================================================
                                                         METRICSSPECIFICATION.H
==============================================================================================================================================*/
// 🧩 The console's fixed geometry — the box, the two columns, the pane bands, the rail/grid/parameter numbers, and the animation durations. Stated
//    once here because several numbers are load bearing in a way a re-derivation would silently break: CardWidth is the two column widths plus the
//    hairline, and the carousel track is twice that (a shrink-to-fit card sized itself to BOTH slides side by side and came out double width).
//
//    🔴 The two card workspaces DISAGREE on geometry and the disagreement is real, not reconcilable by one default: modelling's box is 523x396 with
//       a 326 px right column, paint's is 560x420 with a 363 px right column and several parameter-row numbers (the value pill taller than its track,
//       the stroke strip, the swatch well) modelling has no concept of. So the defaults below are ONE workspace's numbers and every field is
//       overridable: a workspace resolves this struct, then overwrites the fields it disagrees on before handing it to the console. The console reads
//       whatever it is given and derives nothing a workspace cannot restate.
//    📝 Durations are NOT scaled by UiScale — a bigger card does not take longer to slide. Only lengths scale.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_TOKEN_METRICSSPECIFICATION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_TOKEN_METRICSSPECIFICATION_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct MetricsSpecification
{
    // ── the box ──
    float CardWidth        = 523.0f;   // [px] - LeftColumnWidth + RightColumnWidth + 1 hairline
    float CardHeight       = 396.0f;   // [px] - fixed on both axes; the panes scroll inside it
    float LeftColumnWidth  = 196.0f;   // [px] - rail, and the probe/preview column that replaces it
    float RightColumnWidth = 326.0f;   // [px] - grid, and the options column that replaces it
    float CardRounding     =  18.0f;   // [px] - --r-menu

    // ── pane bands ──
    float HeaderHeight     =  53.0f;   // [px] - identical in both panes so the seam never steps
    float GridFootHeight   =  27.0f;   // [px]
    float OptionsFootHeight=  44.0f;   // [px]

    // ── rail ──
    float RailRowHeight    =  32.0f;   // [px]

    // ── grid ──
    float GridColumnCount  =   4.0f;   // [-]  - tiles per row
    float TileGap          =   6.0f;   // [px]
    float TileRounding     =   9.0f;   // [px]
    // 🔴 The tile's artwork square, and a field rather than the constant it used to be because the two workspaces disagree by nearly 2x: modelling's
    //    tile carries a 27 px glyph mark, paint's carries a 46 px round WELL holding the instrument's nib crop. Leaving this hardcoded drew paint's
    //    wells at modelling's size — a silently shrunken instrument, not a build error. The cell grows with it, so the grid stays proportionate.
    float TileArtEdge      =  27.0f;   // [px] - .tile svg / .well

    // ── parameter rows ──
    // 🔴 Fields rather than the constants they used to be, and for a sharper version of the reason the box is: the parameter pane took the metrics and
    //    then wrote `(void)Metrics;`, hardcoding modelling's set. The two workspaces disagree on every number below — modelling's value pill is 38 px
    //    over a 26 px track with a 21 px knob, paint's is 32 over 22 with 17 — so a paint row drawn through the pane came out a third too tall with its
    //    reading cropped, which is a wrong LAYOUT rather than a build error and would have read as the pane simply being ugly.
    float ParameterPadding  =  13.0f;   // [px] - .options-body padding
    float ParameterRowGap   =  13.0f;   // [px] - .options-body gap, between whole rows
    float ParameterLabelGap =   7.0f;   // [px] - .param gap, caption to widget
    float ValuePillHeight   =  38.0f;   // [px] - --row-h
    float ValuePillWidth    =  84.0f;   // [px] - .valuebox flex-basis
    float SliderTrackHeight =  26.0f;   // [px] - .slider
    float SliderKnobEdge    =  21.0f;   // [px] - .slider .knob
    float SegmentHeight     =  32.0f;   // [px] - .seg-opt
    float SegmentGap        =   6.0f;   // [px] - .segment gap
    float SwitchWidth       =  42.0f;   // [px] - .switch
    float SwitchHeight      =  24.0f;   // [px]
    float SwitchNubEdge     =  18.0f;   // [px] - .switch .nub
    float DropdownHeight    =  34.0f;   // [px] - the collapsed option box

    // ── animation (never scaled) ──
    float CarouselSeconds  =   0.34f;  // [s]  - cubic-bezier(.5,.05,.2,1)
    float OpenSeconds      =   0.14f;  // [s]  - cubic-bezier(.16,1,.3,1)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Resolve the console metrics, scaling every length by the theme's UiScale so one config knob resizes the whole card. Durations are left alone.
// A workspace that disagrees on a field overrides it AFTER this call, on the resolved (already-scaled) struct.
[[nodiscard]] MetricsSpecification ResolveConsoleMetrics(const ThemeConfiguration& Theme);

} // namespace Frontier

#endif
