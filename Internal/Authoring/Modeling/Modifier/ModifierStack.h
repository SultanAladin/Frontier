/*============================================================================================================================================
                                                             MODIFIERSTACK.H
============================================================================================================================================*/
// 🧩 A non-destructive ordered list of polygon modifiers owned per object. The base PolygonCluster is never mutated; the stack is
//    evaluated in order into a DERIVED PolygonCluster that feeds the display polygon + GPU buffers, so any entry can be toggled
//    off, deleted, reordered, or re-tuned and the result re-evaluates from the untouched base cage. Each ModifierEntry is one
//    operation plus its settings and a stable identifier (so the UI can address a row across reorders). Subdivision is wired to
//    real geometry today — SimpleScheme (linear split, SubdivideSimpleLevel) and CatmullClarkScheme (smooth quad limit surface,
//    SubdivideCatmullClarkLevel, boundary + semi-sharp-crease aware). Loop / Doo-Sabin and Bevel / Chamfer are registered,
//    settings-editable, re-orderable STUBS that pass the polygon through unchanged until their kernel ops land.
// 📝 Flat-array port of the retired topology-typed stack: the live model is a flat PolygonCluster (Positions + FaceVertexIndices +
//    FaceVertexCounts), so evaluation runs the flat subdivision kernels directly — no half-edge extract / rebuild. Only the
//    positions and the face arrays flow through the kernels; the derived cluster's normals are re-derived at display time and its
//    UVs are display-only (the base cage keeps the authoritative texture coordinates the seams and picking read).

#pragma once
#ifndef FRONTIER_AUTHORING_MODELING_MODIFIER_MODIFIERSTACK_H
#define FRONTIER_AUTHORING_MODELING_MODIFIER_MODIFIERSTACK_H

#include "PolygonCluster.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 The subdivision schemes the UI offers. SimpleScheme and CatmullClarkScheme refine real geometry today; Loop and Doo-Sabin
//    are stubs (the stack passes the polygon through) until their limit-surface math lands. The enum order is the dropdown order.
enum SubdivisionScheme
{
    CatmullClarkScheme = 0,   // [-] - smooth quad subdivision (boundary + semi-sharp-crease aware)
    SimpleScheme       = 1,   // [-] - linear split, no smoothing
    LoopScheme         = 2,   // [-] - smooth triangle subdivision (stub)
    DooSabinScheme     = 3,   // [-] - dual / sqrt(3) subdivision (stub)
};

// 📝 Which operation an entry performs. Subdivision honours its Scheme; Bevel / Chamfer are stubs carrying their settings.
enum ModifierCategory
{
    SubdivisionModifier = 0,   // [-] - subdivide by Scheme at Level
    BevelModifier       = 1,   // [-] - round edges by Amount over Segments (stub)
    ChamferModifier     = 2,   // [-] - flat-cut edges by Distance (stub)
};

// 📝 Direction to move an entry within the stack (one slot per step) — the reorder buttons the Properties UI exposes.
enum ReorderDirection
{
    ReorderUpward   = 0,   // [-] - toward the front of the stack (evaluated earlier)
    ReorderDownward = 1,   // [-] - toward the back of the stack (evaluated later)
};

// 📝 One modifier in the stack: its category + the settings for every category (only the active category's fields are read, so
//    one flat struct avoids a tagged union for a handful of scalars). EvaluationEnabled is the per-entry viewport toggle (an off
//    entry is skipped during evaluation but kept in the stack). Identifier is stable across reorder/insert so the UI can track a
//    row. BoundarySmoothEnabled / CreaseEnabled are the Catmull-Clark robustness options (relax open borders; honour seams as
//    hard creases) — the "more than Blender" levers exposed per entry.
struct ModifierEntry
{
    ModifierCategory  Category              = SubdivisionModifier;   // [-]  - which operation this entry performs
    bool              EvaluationEnabled     = true;                  // [-]  - evaluated in the viewport when true
    uint64_t          Identifier            = 0;                     // [-]  - stable id for UI addressing / reorder
    SubdivisionScheme Scheme                = CatmullClarkScheme;    // [-]  - subdivision scheme (SubdivisionModifier)
    int               Level                 = 1;                     // [-]  - viewport subdivision iterations (drives live tessellation)
    int               RenderLevel           = 2;                     // [-]  - render subdivision iterations (authored + persisted; consumed by the offline render path when it lands)
    bool              BoundarySmoothEnabled = true;                  // [-]  - relax open borders along the boundary curve (CC)
    bool              CreaseEnabled         = true;                  // [-]  - honour marked seams as hard creases (CC)
    float             Amount                = 0.1f;                  // [cm] - bevel rounding amount (BevelModifier)
    int               Segments              = 1;                     // [-]  - bevel segment count (BevelModifier)
    float             Distance              = 0.1f;                  // [cm] - chamfer cut distance (ChamferModifier)
};

// 📝 The ordered stack for one object. Entries evaluate front-to-back. NextIdentifier mints stable ids so a deleted entry's id
//    is never reissued within a session (avoids the UI confusing a new row with the one it replaced).
struct ModifierStack
{
    std::vector<ModifierEntry> Entries        = {};   // [-] - modifiers in evaluation order
    uint64_t                   NextIdentifier = 1;    // [-] - next stable id to mint (0 = unassigned)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Append a modifier of Category to the stack with default settings and a freshly minted stable Identifier; returns that id so the
// caller can immediately address the new entry.
uint64_t AppendModifier(ModifierStack& Stack, ModifierCategory Category);

// Remove the entry with Identifier from the stack. No-op if no entry matches. Stable ids are never reissued, so a later append
// cannot collide with the removed row.
void RemoveModifier(ModifierStack& Stack, uint64_t Identifier);

// Resolve a writable pointer to the entry with Identifier, or nullptr if none matches. The pointer is valid only until the next
// structural change to Stack.Entries (append / remove / reorder).
ModifierEntry* RetrieveModifier(ModifierStack& Stack, uint64_t Identifier);

// Move the entry with Identifier one slot in Direction, swapping it with its adjacent entry. No-op if no entry matches or the
// entry is already at the relevant end. Reordering changes evaluation order (a subdivide before a bevel differs from after).
void ReorderModifier(ModifierStack& Stack, uint64_t Identifier, ReorderDirection Direction);

// Evaluate the stack: copy Base, apply every ENABLED entry in order, and write the derived polygon to OutEvaluated. An empty /
// all-disabled stack yields a faithful copy of the base (display == cage). SeamEdges (keyed by AdjacencyIndex::EncodeEdgeKey)
// seed hard creases for a Catmull-Clark entry whose CreaseEnabled is set. Only Simple / Catmull-Clark subdivision changes
// geometry today; every other category / scheme passes the working polygon through unchanged. OutEvaluated is fully reset first;
// on an internal failure it falls back to the last good working polygon so the object never disappears.
void EvaluateModifierStack(const PolygonCluster&               Base,
                           const ModifierStack&                Stack,
                           const std::unordered_set<uint64_t>& SeamEdges,
                           PolygonCluster&                     OutEvaluated);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_MODELING_MODIFIER_MODIFIERSTACK_H
