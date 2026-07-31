/*==============================================================================================================================================
                                                        TOOLCARDSPECIFICATION.H
==============================================================================================================================================*/
// 🧩 The data vocabulary every tool card is authored in, and the resolved palette + metrics it draws with. This header carries NO drawing code and
//    no workspace knowledge: the modelling, drafting and texture-paint cards each supply their own band/tool/parameter tables against these types,
//    and the components in this folder render whatever they are handed. Transcribed from Documentation/Prototypes/ModellingToolMenu.html — the
//    metrics are that file's CSS numbers, not approximations of them.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLCARDSPECIFICATION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLCARDSPECIFICATION_H

#include "imgui.h"

#include "../../../Theme/ThemeConfiguration.h"

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Fixed table ceilings. A parameter is a plain aggregate rather than a vector so a whole catalogue can be a `constexpr`
//    table in a translation unit with no allocation and no initialization order to reason about; these are the measured maxima
//    of the prototype's own tables, not guesses. A table that exceeds one trips the static assert at its definition site.
constexpr int ToolSegmentedOptionLimit = 6;    // [idx] - widest segmented control (prototype peak 5)
constexpr int ToolSnapTargetLimit      = 6;    // [idx] - snap chip strip (prototype peak 3 armed of 4 offered)
constexpr int ToolSwatchLimit          = 8;    // [idx] - swatch strip (prototype peak 5)
constexpr int ToolProbeAxisLimit       = 3;    // [idx] - a vector readout is XYZ
constexpr int ToolProbeRowLimit        = 6;    // [idx] - rows in one probe section (prototype peak 4)
constexpr int ToolProbeSectionLimit    = 5;    // [idx] - sections in one probe readout (prototype peak 4)
constexpr int ToolProbeAggregateLimit  = 6;    // [idx] - aggregate rows under a multi-selection (prototype peak 5)


//------------------------------------------------------------------------------------------------------------------------
//                                                             TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which widget a parameter row draws. The six the prototype authors, no more: a seventh kind is a change to both the table
//    and the parameter column, and naming it here keeps those two in step.
enum class ToolParameterCategory
{
    Slider,        // numeric pill + track
    Segmented,     // exclusive option strip
    Toggle,        // on/off switch
    Swatches,      // colour chip strip
    AxisChoice,    // X/Y/Z strip plus a negate pill
    SnapTargets,   // multi-select chip strip
};


// 📝 A tool's availability against the current selection. The three-state model the prototype resolves and the shipped gate
//    mirrors: Absent is dropped from the grid entirely, Gated draws greyed with its shortfall replacing the label under the
//    pointer, Live draws normally. Absent and Gated are NOT interchangeable — one says "not for this selection", the other says
//    "for this selection, but not yet".
enum class ToolAvailability
{
    Absent,
    Gated,
    Live,
};


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One parameter row. A single aggregate covers all six kinds rather than a union or a hierarchy: the table is authored by hand
//    in a header, and a designer editing a row should not have to know which arm of a variant it lives in. Unused members simply
//    stay zero — Boundary/Unit mean nothing to a Toggle, OptionLabels mean nothing to a Slider.
struct ToolParameterDescriptor
{
    ToolParameterCategory Category;                              // [-]  - which widget draws
    const char*           GlyphName;                             // [-]  - leading mark, prototype identifier
    const char*           Label;                                 // [-]  - row caption

    float                 MinimumBoundary;                       // [-]  - Slider: travel floor
    float                 MaximumBoundary;                       // [-]  - Slider: travel ceiling
    float                 InitialReading;                        // [-]  - Slider: authored default
    const char*           Unit;                                  // [-]  - Slider: unit segment text

    const char*           OptionLabels[ToolSegmentedOptionLimit]; // [-]  - Segmented: options, null-terminated
    int                   InitialOption;                         // [idx]- Segmented / Swatches / AxisChoice: chosen index

    bool                  InitialActivation;                     // [-]  - Toggle: authored default

    const char*           SwatchTints[ToolSwatchLimit];          // [-]  - Swatches: "#rrggbb" chips, null-terminated

    const char*           SnapTargetLabels[ToolSnapTargetLimit]; // [-]  - SnapTargets: chips offered, null-terminated
    bool                  SnapTargetArmed[ToolSnapTargetLimit];  // [-]  - SnapTargets: which start on
};


// 📝 One tool tile. StrataMask is a bitmask over the workspace's own selection strata so a tool declares every selection it
//    serves in one field. Shortfall is the precondition prose shown when the tool applies but cannot run; null means it always
//    can, which is what separates a Gated tile from a Live one.
struct ToolTileDescriptor
{
    const char*                    Label;           // [-]  - tile caption
    const char*                    Keystroke;       // [-]  - accelerator in the tile corner; null for none
    const char*                    Shortfall;       // [-]  - why it cannot run; null when always available
    const char*                    GlyphName;       // [-]  - tile artwork, prototype identifier
    unsigned int                   StrataMask;      // [-]  - selections this tool serves
    const ToolParameterDescriptor* Parameters;      // [-]  - options-pane rows (borrowed)
    int                            ParameterCount;  // [idx]- number of rows
};


// 📝 One rail row. A band with Tiles drives the grid; a band with none is a SINGLE-SHOT row that runs on click (Delete,
//    Dissolve) — a destructive command with no page to open, which is why it sits below the rail's separator. SeparatorAbove
//    draws the rule that divides the groups; the rail resolves stranded and doubled rules away at build time.
struct ToolBandDescriptor
{
    const char*                    Caption;         // [-]  - rail label, also the grid header title
    const char*                    Keystroke;       // [-]  - accelerator, single-shot rows only
    const char*                    GlyphName;       // [-]  - rail artwork, also the grid header badge
    const ToolTileDescriptor*      Tiles;           // [-]  - tiles this band opens (borrowed); null makes it single-shot
    int                            TileCount;       // [idx]- number of tiles
    unsigned int                   StrataMask;      // [-]  - single-shot rows only: selections they serve
    const ToolParameterDescriptor* Parameters;      // [-]  - single-shot rows only: their own options rows
    int                            ParameterCount;  // [idx]- number of those rows
    bool                           SeparatorAbove;  // [-]  - draw the dividing rule above this row
};


// 📝 One row of a probe readout. Four row shapes share the struct, distinguished by which members are set: a scalar
//    (Key/Reading/Unit), a vector (VectorLabel + AxisLabels/AxisReadings), a condition (ConditionLabel + ConditionMet), and a
//    bounded scalar that colours its reading when it leaves [LimitFloor, LimitCeiling].
struct ToolProbeRowDescriptor
{
    const char* Key;                                  // [-] - scalar row caption
    const char* Reading;                              // [-] - scalar row value, pre-formatted
    const char* Unit;                                 // [-] - scalar row unit

    const char* VectorLabel;                          // [-] - vector row caption; non-null selects the vector shape
    const char* AxisLabels[ToolProbeAxisLimit];       // [-] - "X" "Y" "Z"
    const char* AxisReadings[ToolProbeAxisLimit];     // [-] - per-axis values, pre-formatted
    const char* VectorUnit;                           // [-] - unit shared by the axes

    const char* ConditionLabel;                       // [-] - condition row caption; non-null selects the condition shape
    bool        ConditionMet;                         // [-] - drives the dot colour

    bool        LimitPresent;                         // [-] - true when the bounds below apply
    float       LimitFloor;                           // [-] - reading below this colours as a breach
    float       LimitCeiling;                         // [-] - reading above this colours as a breach
};


// 📝 One titled group of probe rows.
struct ToolProbeSectionDescriptor
{
    const char*                   Title;                        // [-]  - uppercase section rule
    ToolProbeRowDescriptor        Rows[ToolProbeRowLimit];       // [-]  - rows in order
    int                           RowCount;                     // [idx]- how many are set
};


// 📝 The whole readout for one selection stratum. Sections describe a single picked component; Aggregate is what a multi-pick
//    reports instead, because a sum over twelve faces is a different statement from one face's own measurements.
struct ToolProbeReadoutDescriptor
{
    const char*                   Identity;                              // [-]  - what is being measured
    ToolProbeSectionDescriptor    Sections[ToolProbeSectionLimit];        // [-]  - single-selection readout
    int                           SectionCount;                          // [idx]- how many are set
    ToolProbeRowDescriptor        Aggregate[ToolProbeAggregateLimit];     // [-]  - multi-selection readout
    int                           AggregateCount;                        // [idx]- how many are set
};


// 📝 The colours the card draws with. Most are the shared theme's; the rest are card-specific tokens the theme has no field for
//    (the grid pane's one-notch-darker fill, the tile and tile-hover fills, the rail's selected fill, the amber a gated tile
//    states its shortfall in). Resolved once per cycle from a ThemeConfiguration so a palette change still flows from one place.
struct ToolCardPalette
{
    ImU32 CardFill;            // [-] - --menu        card + rail background
    ImU32 GridFill;            // [-] - --menu-2      grid + options pane, one notch darker
    ImU32 RailSelectedFill;    // [-] - --rail-sel    active rail row
    ImU32 TileFill;            // [-] - --tile        tile rest
    ImU32 TileHoverFill;       // [-] - --tile-hi     tile under pointer
    ImU32 GatedTileFill;       // [-] - #16161a       tile whose precondition is short
    ImU32 Hairline;            // [-] - --hair        pane rules
    ImU32 HairlineStrong;      // [-] - --hair-strong card border
    ImU32 Accent;              // [-] - --accent      active marker, live tallies
    ImU32 Ink;                 // [-] - --ink         primary text + live glyph
    ImU32 Muted;               // [-] - --muted       secondary text
    ImU32 Faint;               // [-] - --faint       tertiary text + gated glyph
    ImU32 ShortfallInk;        // [-] - #c9a227       the amber a gated tile explains itself in
    ImU32 ValueBlack;          // [-] - --value-black numeric pill centre
    ImU32 ValueUnitFill;       // [-] - --value-unit  unit segment
    ImU32 TrackFill;           // [-] - --track-bg    switch track, off
    ImU32 TrackTravelled;      // [-] - --track-fill  slider travelled portion
    ImU32 KnobFill;            // [-] - --knob        slider knob, selected segment
    ImU32 KnobInk;             // [-] - text over a knob-filled segment
    ImU32 BreachInk;           // [-] - #e0603f       a probe reading outside its limits
};


// 📝 The card's fixed geometry, straight from the prototype's CSS. Stated once here because several of these numbers are load
//    bearing in a way a re-derivation would silently break: the card width is the two column widths plus the hairline, and the
//    carousel track is twice that. A shrink-to-fit card sized itself to BOTH slides side by side and came out double width.
struct ToolCardMetrics
{
    float CardWidth        = 523.0f;   // [px] - 196 rail + 326 grid + 1 hairline
    float CardHeight       = 396.0f;   // [px] - fixed on both axes; the panes scroll inside it
    float LeftColumnWidth  = 196.0f;   // [px] - rail, and the probe column that replaces it
    float RightColumnWidth = 326.0f;   // [px] - grid, and the options column that replaces it
    float HeaderHeight     =  53.0f;   // [px] - identical in both panes so the seam never steps
    float RailRowHeight    =  32.0f;   // [px]
    float GridFootHeight   =  27.0f;   // [px]
    float OptionsFootHeight=  44.0f;   // [px]
    float CardRounding     =  18.0f;   // [px] - --r-menu
    float TileRounding     =   9.0f;   // [px]
    float GridColumnCount  =   4.0f;   // [-]  - tiles per row
    float TileGap          =   6.0f;   // [px]
    float CarouselSeconds  =   0.34f;  // [s]  - cubic-bezier(.5,.05,.2,1)
    float OpenSeconds      =   0.14f;  // [s]  - cubic-bezier(.16,1,.3,1)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Resolve the card palette from the shared theme. Card-specific tokens the theme has no field for are stated here as the
// prototype's own literals, so this function is the single place they exist.
[[nodiscard]] ToolCardPalette ResolveToolCardPalette(const ThemeConfiguration& Theme);

// Resolve the card metrics, scaled by the theme's UiScale so one config knob resizes the whole card.
[[nodiscard]] ToolCardMetrics ResolveToolCardMetrics(const ThemeConfiguration& Theme);

// Whether a tool applies to a stratum at all, and if so whether its precondition is met. StratumBit is the single bit of the
// active stratum. 🔴 A mask that names EVERY selection still does not reach an empty one: a wildcard tool offering to transform
// a selection that does not exist is the bug this signature exists to make impossible, so an empty stratum must be named.
[[nodiscard]] ToolAvailability ResolveTileAvailability(const ToolTileDescriptor& Tile, unsigned int StratumBit);

// How many of a band's tiles are not Absent for this stratum — the figure the rail row states and the filter tests against zero.
[[nodiscard]] int TallyApplicableTiles(const ToolBandDescriptor& Band, unsigned int StratumBit);

// How many of a band's tiles are Live (applicable AND their precondition met).
[[nodiscard]] int TallyLiveTiles(const ToolBandDescriptor& Band, unsigned int StratumBit);

} // namespace Frontier

#endif
