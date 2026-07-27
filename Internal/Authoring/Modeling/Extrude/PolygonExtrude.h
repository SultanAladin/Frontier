/*============================================================================================================================================
                                                              POLYGONEXTRUDE.H
============================================================================================================================================*/
// 🧩 The Blender-standard face-extrude kernel for the authoring PolygonCluster: it clones a selected face set into a new cap,
//    walls the cap to the base along the region's boundary (or every edge for the individual variant), removes the original
//    selected faces, and reports the appended cap faces / vertices as the follow-on selection. The extrude is created at ZERO
//    offset (the clones sit coincident with their originals) so the topology exists before any motion; the caller then slides
//    the caps interactively along the per-vertex directions this kernel returns, reusing the viewport's modal-drag path. Three
//    variants: Region (one shared average-normal axis), Along Normals (per-vertex averaged normal, shrink/grow safe), and
//    Individual (each face its own ring along its own normal). Manifold / boolean extrude is deliberately out of scope here.

#pragma once
#ifndef FRONTIER_AUTHORING_MODELING_EXTRUDE_POLYGONEXTRUDE_H
#define FRONTIER_AUTHORING_MODELING_EXTRUDE_POLYGONEXTRUDE_H

#include "LinearAlgebra_Float64.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Frontier
{

struct PolygonCluster;   // 📝 forward-declared — the kernel mutates its face stream + positions; the .cpp pulls the full header.
struct AdjacencyIndex;   // 📝 forward-declared — the kernel reads loops / normals / edge incidence over the BASE topology only.

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The extrude variant, taken from the toolbar's Type dropdown (Manifold is booleans — out of scope, not represented here).
//    `.Category` suffix per the naming skill (replaces the banned `Kind`).
enum class ExtrudeCategory
{
    RegionCategory,        // [-] - one region cap, boundary walls, one shared average-normal direction
    AlongNormalsCategory,  // [-] - one region cap, boundary walls, per-vertex averaged-normal direction (shrink/grow safe)
    IndividualCategory     // [-] - each selected face its own cap + full wall ring, along that face's own normal
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One appended cap vertex and the unit axis it slides along while the caller drives the interactive offset. OriginPosition
//    is the zero-offset position (identical to the base vertex the clone was taken from), so ExtrudeOffsetAdvance re-derives the
//    live position idempotently as OriginPosition + Direction * Distance — the same origin-snapshot rule the drag gizmo uses.
struct ExtrudeOffsetSample
{
    uint32_t NewVertex      = 0;                     // [-]  - appended cap vertex index into the cluster's VertexField
    Vector3d Direction      = { 0.0, 0.0, 0.0 };     // [-]  - unit slide direction (region-average / per-vertex / per-face)
    Vector3d OriginPosition = { 0.0, 0.0, 0.0 };     // [cm] - position at zero offset (the base vertex the clone came from)
};

// 📝 The whole outcome of a zero-offset extrude: what topology was appended and how to drive the live offset. CapFaces /
//    CapVertices become the new selection (so the follow-on modal moves exactly the extruded caps). SharedDirection is the one
//    unit axis for the Region variant (zero for the per-vertex variants, which carry direction per-sample instead).
struct ExtrudeOutcome
{
    bool                             Completed = false;              // [-] - false + no mutation on a degenerate request
    std::vector<ExtrudeOffsetSample> OffsetSamples;                 // [-] - one per appended cap vertex (drives the offset)
    std::unordered_set<uint32_t>     CapFaces;                       // [-] - appended cap face ordinals -> new face selection
    std::unordered_set<uint32_t>     CapVertices;                    // [-] - appended cap vertices    -> new vertex selection
    Vector3d                         SharedDirection = { 0.0, 0.0, 0.0 };   // [-] - Region only (unit); zero otherwise
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Extrude SelectedFaces of Target into a new cap at ZERO offset, following the chosen variant. BaseAdjacency must describe
// Target's CURRENT topology (its loops / normals / edge incidence are read before any append). On success the clones + cap
// faces + wall faces are appended, the original selected faces are removed, Outcome is filled, and true is returned. On a
// degenerate request (empty selection, a selected ordinal past the face count) Target is left untouched, Outcome.Completed is
// false, and false is returned. The caller must re-run ResolveAdjacencyIndex on the mutated Target afterwards — the passed-in
// BaseAdjacency is now stale.
bool ExtrudePolygonSelection(PolygonCluster&                     Target,
                             const AdjacencyIndex&               BaseAdjacency,
                             const std::unordered_set<uint32_t>& SelectedFaces,
                             ExtrudeCategory                     Category,
                             ExtrudeOutcome&                     Outcome);

// Drive the interactive offset for the per-vertex variants (Along Normals / Individual), where a single shared world delta
// cannot represent divergent directions. Re-derives each sampled cap vertex as OriginPosition + Direction * Distance (signed),
// so it is idempotent from the zero-offset snapshot and a negative Distance slides inward with no special case. Returns false
// (no writes) if any sample's NewVertex is out of range for Target. Region can use this too, but the shared-axis Region path
// may instead ride the viewport's existing ApplyGizmoDeltaToSelection with SharedDirection.
bool ExtrudeOffsetAdvance(PolygonCluster&                         Target,
                          const std::vector<ExtrudeOffsetSample>& Samples,
                          double                                  Distance);

// Reverse the corner winding of the named faces in place (Blender's Flip toggle on the extrude): a face's outward normal is the
// Newell normal of its loop, so reversing the loop flips the normal. Applied to the appended cap faces after an extrude so the
// whole shell reads inside-out under the matcap. Face ordinals out of range are skipped. Leaves the corner UVs paired with their
// corners (they are reversed alongside the indices), so the cap keeps its mapping. No-op on an empty set.
void ReverseAppendedFaceWinding(PolygonCluster& Target, const std::unordered_set<uint32_t>& Faces);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_MODELING_EXTRUDE_POLYGONEXTRUDE_H
