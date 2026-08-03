/*==============================================================================================================================================
                                                          PARAMETERDESCRIPTOR.H
==============================================================================================================================================*/
// 🧩 One parameter row in the console's options pane, and the widget vocabulary the rows are authored in. A single aggregate covers every widget
//    kind rather than a union or a hierarchy: the table is authored by hand in a workspace header, and an author editing a row should not have to
//    know which arm of a variant it lives in. Unused members stay zero — Boundary/Unit mean nothing to a Switch, OptionLabels mean nothing to a
//    Slider. Ported from ToolCard's ToolParameterDescriptor, widened by the one kind paint needs (Dropdown) so all three workspaces share it.
//
//    🔴 The widget set is the UNION of the three workspaces' vocabularies, not any one workspace's: modelling authors Slider/Segmented/Switch/
//       Swatches/AxisChoice/SnapTargets, paint authors Slider/Segmented/Dropdown/Switch. Dropdown is paint's alone (no card had one); Swatches/
//       AxisChoice/SnapTargets are modelling's alone. A workspace simply never authors a kind it does not use, so the union costs nothing but the
//       enumerator names.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_DESCRIPTOR_PARAMETERDESCRIPTOR_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_DESCRIPTOR_PARAMETERDESCRIPTOR_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Fixed table ceilings, the measured maxima across all three workspaces' own tables rather than guesses. A table that exceeds
//    one trips the static assert at its definition site. Segmented/Swatch/Snap come from modelling's ToolCard peaks; Dropdown is
//    paint's 21-entry graphite grade scale, rounded up.
constexpr int ParameterSegmentedOptionLimit = 6;    // [idx] - widest segmented strip (peak 5)
constexpr int ParameterDropdownOptionLimit   = 24;   // [idx] - longest dropdown (paint's graphite scale, peak 21)
constexpr int ParameterSwatchLimit           = 8;    // [idx] - swatch chip strip (peak 5)
constexpr int ParameterSnapTargetLimit       = 6;    // [idx] - snap chip strip (peak 3 armed of 4 offered)


//------------------------------------------------------------------------------------------------------------------------
//                                                             TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which widget a parameter row draws. The union of all three workspaces' authored kinds; see the header note on why. A new kind is a
//    change to both this enum and the parameter pane, and naming it here keeps the two in step.
enum class ParameterCategory
{
    Slider,        // numeric pill + track
    Segmented,     // exclusive option strip
    Dropdown,      // paint's collapsed option list; the one kind with no card precedent
    Switch,        // on/off track + nub
    Swatches,      // colour chip strip (modelling)
    AxisChoice,    // X/Y/Z strip plus a negate pill (modelling)
    SnapTargets,   // multi-select chip strip (modelling)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ParameterDescriptor
{
    ParameterCategory Category;                                     // [-]  - which widget draws
    const char*       Key;                                          // [-]  - the parameter's identity (paint's `k`); carried, not inferred from Label
    const char*       GlyphName;                                    // [-]  - leading mark, prototype identifier
    const char*       Label;                                        // [-]  - row caption

    float             MinimumBoundary;                             // [-]  - Slider: travel floor
    float             MaximumBoundary;                             // [-]  - Slider: travel ceiling
    float             InitialReading;                              // [-]  - Slider: authored default
    const char*       Unit;                                        // [-]  - Slider: unit segment text

    // 📝 Segmented options inline (fixed small ceiling); Dropdown options borrowed by pointer (the 21-entry scale would bloat every row if inlined).
    const char*       SegmentedLabels[ParameterSegmentedOptionLimit]; // [-]  - Segmented: options, null-terminated
    const char* const* DropdownLabels;                            // [-]  - Dropdown: options (borrowed); null for non-dropdowns
    int               DropdownCount;                              // [idx]- Dropdown: how many options
    // 🔴 The authored default is carried BY LABEL, not index: paint's two marker nib sets order their options differently, so index 0 does not
    //    name the same option in both. Resolving by label makes that mix-up impossible. Segmented rows may set InitialOption instead when order is stable.
    const char*       InitialOptionLabel;                          // [-]  - Segmented / Dropdown: authored default, by label
    int               InitialOption;                               // [idx]- Segmented / Swatches / AxisChoice: chosen index, when order is fixed

    bool              InitialActivation;                           // [-]  - Switch: authored default

    const char*       SwatchTints[ParameterSwatchLimit];           // [-]  - Swatches: "#rrggbb" chips, null-terminated
    const char*       SnapTargetLabels[ParameterSnapTargetLimit];  // [-]  - SnapTargets: chips offered, null-terminated
    bool              SnapTargetArmed[ParameterSnapTargetLimit];   // [-]  - SnapTargets: which start on
};

} // namespace Frontier

#endif
