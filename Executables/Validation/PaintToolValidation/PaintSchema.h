/*==============================================================================================================================================
                                                             PAINTSCHEMA.H
==============================================================================================================================================*/
// 🧩 Which parameter rows an instrument shows, and what they start at. Ported from Documentation/Prototypes/PaintToolMenu.html's SchemaFor() +
//    VisibleControls() + SeedParams(), whose behaviour was pinned by evaluating that file's own functions over all 102 instruments
//    (_ClaudeScratch/tmp/ProbePaintSchema.js) rather than by reading them.
//
//    🔴 HAND-WRITTEN, not generated, and that is the one file here where that is the right call. The prototype's schema is not a table: it is a
//       function with conditional rows, two title swaps (Marker/Highlighter, Spray Can/Airbrush), a spliced-in extra control for highlighters, and
//       per-instrument defaults that vary by flag. Emitting the 17 distinct outcomes as 17 tables would state the RESULTS while losing the rule
//       that produced them, so the next person could not tell which parts are load bearing.
//
//    Measured shape, which the table ceilings in PaintCardSpecification.h come from:
//      102 instruments -> 17 distinct panes | at most 2 groups | at most 10 visible rows | at most 7 controls in one group
//
//    Three behaviours that are easy to get wrong and are each asserted in the check:
//      * A hidden row is still SEEDED. VisibleControls filters for drawing; SeedParams ignores `when` entirely, so a value survives being
//        hidden and reappears unchanged. 39 of the 102 instruments have at least one.
//      * An empty group DISAPPEARS. When every conditional row in a family group is hidden, the group is dropped rather than drawn with a title and
//        no body — Chalk Stick and Wax Crayon show ONLY a Stroke group.
//      * The eraser does NOT use the shared Stroke group. It authors its own, with a different size default (14) and no opacity/flow/smoothing.

#pragma once
#ifndef FRONTIER_VALIDATION_PAINTTOOL_PAINTSCHEMA_H
#define FRONTIER_VALIDATION_PAINTTOOL_PAINTSCHEMA_H

#include "PaintCardSpecification.h"

namespace Frontier
{

struct PaintInstrumentDescriptor;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The live value of one control. A single aggregate for all four kinds, tagged by the control's own Category rather than by a
//    discriminant of its own — the value and the control that owns it are always resolved together, so a second tag could disagree.
struct PaintControlValue
{
    float       Reading       = 0.0f;      // [-] - Slider: current value
    int         ChosenOption  = 0;         // [idx]- Segmented / Select: current index into OptionLabels
    bool        Activated     = false;     // [-] - Switch: current state
};


// 📝 One titled group of rows, resolved for a specific instrument. BOTH the title and the rows are by value, and both for the
//    same reason: they are computed per instrument, not looked up. The title swaps (Marker vs Highlighter, Spray Can vs
//    Airbrush) and the rows carry that instrument's own authored defaults, so there is no static table either could point at.
struct PaintControlGroup
{
    const char*            Title = nullptr;                    // [-]  - group caption, possibly computed
    PaintControlDescriptor Controls[PaintGroupControlLimit];    // [-]  - rows in order, owned
    int                    ControlCount = 0;                    // [idx]- how many
};


// 📝 An instrument's whole resolved schema: every group and every row it DECLARES, hidden ones included. Visibility is applied
//    separately by ResolveVisiblePaintControls, because a hidden row must still be seeded and must keep its value while hidden.
//
//    🔴 The rows are OWNED here rather than borrowed from static tables, which the prototype forces: SchemaFor bakes the
//       instrument into what it returns — the pen's nib default is `Tool.nib||"Medium"`, the marker's option list switches on
//       Tool.highlighter, the spray's spread default switches on Tool.airbrush. A static table could only hold the
//       un-personalised shape, so every seed site would have to re-apply the overrides and the two would drift.
//       📝 Consequence for callers: PaintVisibleControl points INTO this object, so the schema must outlive the visible list.
struct PaintSchema
{
    PaintControlGroup Groups[PaintGroupLimit];   // [-]  - family group then Stroke, in that order
    int               GroupCount = 0;            // [idx]- how many groups are set
};


// 📝 One row as the options column actually draws it: the descriptor, which group titled it, and where its value lives. Flattened
//    deliberately — the column walks a single list and emits a title only when it changes, which is how the prototype's DOM reads.
struct PaintVisibleControl
{
    const PaintControlDescriptor* Control   = nullptr;   // [-]  - the row (borrowed)
    const char*                   GroupTitle = nullptr;  // [-]  - the owning group's caption
    bool                          StartsGroup = false;   // [-]  - true on the first row of each group
    int                           ValueIndex = 0;        // [idx]- index into the seeded value array
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Resolve every group and row an instrument declares, hidden rows included. Mirrors the prototype's SchemaFor(): the family group
// first (its title computed where the prototype computes it), then the Stroke group.
[[nodiscard]] PaintSchema ResolvePaintSchema(const PaintInstrumentDescriptor& Instrument);

// Seed every declared row's value, INCLUDING rows currently hidden. Mirrors SeedParams(), which ignores `when` — so a value
// survives being hidden and reappears unchanged when its condition comes back. Writes ValueCapacity entries at most and returns how
// many it wrote; the caller sizes the array at PaintSchemaValueLimit.
int SeedPaintValues(const PaintInstrumentDescriptor& Instrument, const PaintSchema& Schema,
                    PaintControlValue* Values, int ValueCapacity);

// Collapse a schema to the rows that are actually drawn, in draw order. Mirrors VisibleControls(): a row whose predicate is false is
// ABSENT rather than greyed, and a group left with no rows is dropped entirely rather than drawn as a title with no body.
// 🔴 Values must be the array SeedPaintValues filled for this same schema — the live-state predicates read it, so passing a stale or
//    differently-ordered array silently resolves the wrong rows.
int ResolveVisiblePaintControls(const PaintInstrumentDescriptor& Instrument, const PaintSchema& Schema,
                                const PaintControlValue* Values, int ValueCount,
                                PaintVisibleControl* Visible, int VisibleCapacity);

// The total number of rows any instrument can DECLARE — the size the value array must be. Larger than PaintVisibleControlLimit
// because hidden rows are declared and seeded too.
constexpr int PaintSchemaValueLimit = PaintGroupLimit * PaintGroupControlLimit;

// Resolve an option label to its index in a control's list, or 0 when the label is absent. Central because the authored default is
// carried by label and every seed site needs the same conversion.
[[nodiscard]] int ResolvePaintOptionIndex(const PaintControlDescriptor& Control, const char* OptionLabel);

} // namespace Frontier

#endif
