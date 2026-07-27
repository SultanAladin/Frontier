/*==============================================================================================================================================
                                                            THEMECONFIGURATION.H
==============================================================================================================================================*/
// 🧩 The active palette + metrics every UI component reads. ONE resolved ThemeConfiguration is passed by const-reference into every
//    Construct* call — no component hardcodes a colour or a spacing. This is the single source of visual truth (the "shared, not duplicated"
//    rule). The palette lives in ColorPaletteDescriptor; this file adds the layout metrics and the combined struct.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_THEME_THEMECONFIGURATION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_THEME_THEMECONFIGURATION_H

#include "ColorPaletteDescriptor.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Layout metrics in device-independent pixels. All spacing/rounding flows from here so a density change is one edit.
struct ThemeMetrics
{
    float PanelPadding;         // [px]  - Inner margin of a panel body
    float ControlSpacing;       // [px]  - Vertical gap between stacked controls
    float ControlHeight;        // [px]  - Standard single-line control height
    float RowHeight;            // [px]  - Outliner / list row height
    float IndentWidth;          // [px]  - Per-depth indent in the outliner
    float CornerRounding;       // [px]  - Panel + control corner radius
    float BorderThickness;      // [px]  - Panel + separator line thickness
    float LabelColumnRatio;     // [0-1] - Fraction of a control row given to its label

    // 📝 Pill-style row metrics (ControlsPreview.html). All scale with UiScale so one config knob resizes every control.
    float UiScale;              // [-]  - Global multiplier applied to the pill metrics below (config: UiScale)
    float PillRowHeight;        // [px] - Height of a numeric value pill / slider / dropdown row (config: RowHeight, pre-scale)
    float PillRounding;         // [px] - Corner radius of a value pill (999 => fully rounded)
    float SideSegmentWidth;     // [px] - Width of a grey axis/unit segment inside a value pill
    float NumericFontScale;     // [-]  - Font enlargement for the numeric readout inside a pill
    float SegmentFontScale;     // [-]  - Font scale for axis/unit segment glyphs
};


// 📝 The resolved theme handed to every component. Resolved once per cycle (ResolveActiveTheme), passed by const-ref everywhere.
struct ThemeConfiguration
{
    ColorPaletteDescriptor Palette;    // [-] - Active colours
    ThemeMetrics           Metrics;    // [-] - Active spacing / sizing
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Resolve the built-in metrics. A denser / touch profile is a sibling Resolve* returning the SAME struct.
ThemeMetrics ResolveDefaultMetrics();

}   // namespace Frontier

#endif
