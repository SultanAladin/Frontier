/*============================================================================================================================================
                                                              POLYGONINSET.H
============================================================================================================================================*/
// 🧩 The Blender-standard face-inset kernel for the authoring PolygonCluster: it clones the selected face set into a shrunk inner
//    cap, walls that cap back to the base along the region boundary (or every edge for the individual variant), removes the
//    original selected faces, and reports the appended cap faces / vertices as the follow-on selection. The inset is created at
//    ZERO offset (the clones sit coincident with their originals) so the topology exists before any motion; the caller then drives
//    TWO interactive scalars — Thickness (each boundary vertex slides inward along its in-plane corner bisector) and Depth (the
//    whole cap rides along the region / face normal) — reusing the viewport's modal-drag path exactly as extrude does. Two
//    variants: Region (one shared shrunk cap, rim on the outer boundary) and Individual (each face its own shrunk cap + full rim).
//    Manifold / boolean inset is deliberately out of scope here; this mirrors PolygonExtrude.{h,cpp} so no session math is duplicated.

#pragma once
#ifndef FRONTIER_AUTHORING_MODELING_INSET_POLYGONINSET_H
#define FRONTIER_AUTHORING_MODELING_INSET_POLYGONINSET_H

#include "LinearAlgebra_Float64.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Frontier
{

struct PolygonCluster;   // 📝 forward-declared — the kernel mutates its face stream + positions; the .cpp pulls the full header.
struct AdjacencyIndex;   // 📝 forward-declared — the kernel reads loops / normals / centres / edge incidence over the BASE topology only.

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The inset variant, taken from the toolbar's Individual toggle. `.Category` suffix per the naming skill (replaces `Kind`).
enum class InsetCategory
{
    RegionCategory,      // [-] - one shared shrunk cap over the selection, rim on the region's outer boundary only
    IndividualCategory   // [-] - each selected face its own shrunk cap + full rim ring, along that face's own normal
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One appended cap vertex and the two axes it slides along while the caller drives the interactive offset. InwardDirection
//    already carries the (1 / sin(halfAngle)) bisector-length correction, so it is NON-unit: sliding by Thickness translates each
//    incident boundary edge inward by exactly Thickness (Blender's default non-even offset). NormalDirection is the unit region /
//    face normal (the Depth axis). Interior region vertices carry a zero InwardDirection (they move with Depth only). OriginPosition
//    is the zero-offset position (identical to the base vertex the clone came from), so InsetOffsetAdvance re-derives the live
//    position idempotently as OriginPosition + InwardDirection * Thickness + NormalDirection * Depth.
struct InsetOffsetSample
{
    uint32_t NewVertex       = 0;                    // [-]  - appended cap vertex index into the cluster's VertexField
    Vector3d InwardDirection = { 0.0, 0.0, 0.0 };    // [-]  - in-plane bisector * (1 / sinHalf); zero for interior region vertices
    Vector3d NormalDirection = { 0.0, 0.0, 0.0 };    // [-]  - unit region / face normal (the Depth axis)
    Vector3d OriginPosition  = { 0.0, 0.0, 0.0 };    // [cm] - position at zero offset (the base vertex the clone came from)
};

// 📝 The whole outcome of a zero-offset inset: what topology was appended and how to drive the live offset. CapFaces / CapVertices
//    become the new selection (so the follow-on modal moves exactly the inset cap). Both variants drive the offset per-sample (an
//    inset never rides a single shared gizmo axis, unlike extrude's Region shortcut), so there is no SharedDirection member.
struct InsetOutcome
{
    bool                             Completed = false;              // [-] - false + no mutation on a degenerate request
    std::vector<InsetOffsetSample>   OffsetSamples;                 // [-] - one per appended cap vertex (drives the offset)
    std::unordered_set<uint32_t>     CapFaces;                       // [-] - appended shrunk-cap face ordinals -> new face selection
    std::unordered_set<uint32_t>     CapVertices;                    // [-] - appended cap vertices             -> new vertex selection
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Inset SelectedFaces of Target into a shrunk inner cap at ZERO offset, following the chosen variant. BaseAdjacency must describe
// Target's CURRENT topology (its loops / normals / centres / edge incidence are read before any append). On success the clones +
// cap faces + rim faces are appended, the original selected faces are removed, Outcome is filled, and true is returned. On a
// degenerate request (empty selection, a selected ordinal past the face count) Target is left untouched, Outcome.Completed is
// false, and false is returned. The caller must re-run ResolveAdjacencyIndex on the mutated Target afterwards — the passed-in
// BaseAdjacency is now stale.
bool InsetPolygonSelection(PolygonCluster&                     Target,
                           const AdjacencyIndex&               BaseAdjacency,
                           const std::unordered_set<uint32_t>& SelectedFaces,
                           InsetCategory                       Category,
                           InsetOutcome&                       Outcome);

// Drive the interactive offset: re-derive each sampled cap vertex as OriginPosition + InwardDirection * Thickness +
// NormalDirection * Depth from the zero-offset snapshot, so it is idempotent (a fresh Thickness / Depth each frame overwrites, never
// accumulates) and a cancel (both zero) rewinds exactly. A negative Thickness slides outward (outset) and a negative Depth sinks the
// cap, both with no special case. Returns false (no writes) if any sample's NewVertex is out of range for Target.
bool InsetOffsetAdvance(PolygonCluster&                       Target,
                        const std::vector<InsetOffsetSample>& Samples,
                        double                                Thickness,
                        double                                Depth);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_MODELING_INSET_POLYGONINSET_H
