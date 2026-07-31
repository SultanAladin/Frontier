/*==============================================================================================================================================
                                                            OPERATIONCATALOGUE.H
==============================================================================================================================================*/
// 🧩 The declarative table of every polygon-mutation operation the editor offers, each entry pairing a label + glyph + keystroke with the
//    SelectionPredicate that decides when it may run. Distilled from the operation sets of Blender, Wings3D, Modo, Maya, 3ds Max, Silo, Hexagon,
//    Houdini and MeshLab, so the gate rules are the ones those applications actually enforce (BridgeSpan needs two regions; InsertEdgeLoop needs a
//    walkable quad ring; GridFillBoundary needs even boundary parity). Data only — no ImGui, no device. A menu, a toolbar, or a command palette all
//    read the SAME table, which is why the catalogue lives beside the predicates rather than inside the menu component.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_POLYGONMUTATION_OPERATIONCATALOGUE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_POLYGONMUTATION_OPERATIONCATALOGUE_H

#include "SelectionPredicate.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The functional band an operation belongs to, emitted as an uppercase caption above its run of rows. Ordered as the menu presents them:
//    the transforms a caller reaches for constantly sit at the top, the destructive band sits last behind a separator.
enum class OperationBand : uint8_t
{
    Transform = 0,
    ExtrudeBuild,
    CutSplit,
    MergeWeld,
    Subdivision,
    NormalsShading,
    Attributes,
    TopologyRepair,
    DuplicateSymmetry,
    SelectionConversion,
    Removal,

    BandCount
};

// 📝 Severity tint for a row. Neutral is the default; Emphasised marks the high-frequency operation of its band (the accent-filled glyph the
//    mockup calls `.hot`); Destructive marks the red-tinting removal rows (`.warn`).
enum class OperationSeverity : uint8_t
{
    Neutral = 0,
    Emphasised,
    Destructive
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One catalogue row. Glyph is a PolygonGlyph value carried as uint8_t so this header stays free of the glyph table (the catalogue is data
//    about topology; how a glyph is drawn is a rendering concern). BranchCaption non-null marks the row as opening an OperationBranch submenu
//    whose rows are the catalogue entries sharing that caption — so grouping and branching are both declared inline, never in a parallel array.
struct TopologyOperationDescriptor
{
    const char*        Label;              // [-] - Row text as the menu shows it
    const char*        KeyHint;            // [-] - Keystroke chip pinned right; null omits it
    uint8_t            Glyph;              // [-] - PolygonGlyph value for the leading icon
    OperationBand      Band;               // [-] - Functional band (emits the caption above its first row)
    OperationSeverity  Severity;           // [-] - Neutral / Emphasised / Destructive tint
    SelectionPredicate Predicate;          // [-] - When this operation may run
    const char*        BranchCaption;      // [-] - Non-null: this row opens a branch with this title
};


// 📝 One resolved row, produced by filtering the catalogue against a SelectionProfile. Carries the descriptor plus WHY it is in this state and
//    the chip text to draw — so the menu component renders without re-running any gate logic.
struct ResolvedOperationEntry
{
    const TopologyOperationDescriptor* Descriptor;      // [-] - Borrowed catalogue entry
    AvailabilityCondition              Condition;       // [-] - Available / CountShortfall / TopologyShortfall
    const char*                        ShortfallText;   // [-] - Reason chip for a gated row; null when available
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The whole catalogue and its length. Static storage, never mutated — the caller borrows it.
[[nodiscard]] const TopologyOperationDescriptor* ResolveOperationCatalogue(int& OperationCount);

// 📝 The uppercase caption for one band ("EXTRUDE / BUILD").
[[nodiscard]] const char* DescribeOperationBand(OperationBand Band);

// 📝 Filter the catalogue against one selection into caller-supplied storage, and report how many entries were written. Entries whose stratum
//    does not admit the selection are dropped entirely; the rest are written in catalogue order with their condition and chip text resolved.
//    RevealGatedEntries false additionally drops count/topology shortfalls, leaving only rows that can actually run.
//    Returns the count written, never exceeding EntryCapacity.
[[nodiscard]] int FilterOperationCatalogue(const SelectionProfile&  Profile,
                                           bool                     RevealGatedEntries,
                                           ResolvedOperationEntry*  Entries,
                                           int                      EntryCapacity);

// 📝 Tally how many catalogue entries are available versus gated for one selection, for the validation readout. Entries hidden by stratum
//    mismatch count toward neither.
void TallyOperationAvailability(const SelectionProfile& Profile, int& AvailableCount, int& GatedCount, int& HiddenCount);

}   // namespace Frontier

#endif
