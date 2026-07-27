/*==============================================================================================================================================
                                                               PARAMETRICSKETCHFILLET.H
==============================================================================================================================================*/
// ðŸ§© The parametricSketching workspace's analytic corner Fillet / Chamfer engine â€” the exact-geometry heart of the Plasticity `B` tool. Given ONE
//    vertex of a closed loop (Rectangle / Polygon / Profile) or an interior vertex of an open Polyline, it solves the corner triple
//    (P[i-1], P[i], P[i+1]) into either a true tangent ARC (fillet) or a straight setback edge (chamfer), rebuilding the host shape's
//    Points so the result stays one analytic shape the view flattens + picks like any other. The arc is exact (Centre / Radius / start +
//    sweep angle) â€” the display polyline is a background tessellation, never a dotted overlay. A safe-limit clamp keeps the setback within
//    both leg lengths (Plasticity's max-radius behaviour). Pure CPU + analytic; the modal (ParametricSketchFilletModal) drives the live drag over
//    this, and the store's ConstructParametricSketchShape re-solves the rebuilt Points so the round-family scalars stay in lockstep.

#pragma once
#ifndef FRONTIER_AUTHORING_PARAMETRICAUTHORING_OPERATIONS_FILLET_PARAMETRICSKETCHFILLET_H
#define FRONTIER_AUTHORING_PARAMETRICAUTHORING_OPERATIONS_FILLET_PARAMETRICSKETCHFILLET_H

#include "imgui.h"
#include "ParametricSketchShapeStore.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ Which corner edit the `B` tool is committing. Fillet rounds the corner into a tangent arc (drag-positive); Chamfer cuts it with a
//    straight setback edge (drag-negative). One tool, two outcomes chosen by the drag sign â€” the Plasticity idiom. Named .Category per the skills.
enum class ParametricSketchCornerCategory
{
    Fillet  = 0,   // [-] - round the corner: a true tangent arc of the requested radius
    Chamfer = 1     // [-] - cut the corner: a straight edge set back the requested distance along each leg
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ The resolved geometry of a corner the tool is about to round / cut, computed once from the corner triple + the requested magnitude.
//    Resolved is false for a degenerate corner (collinear legs, a zero leg, or a non-positive magnitude) â€” the caller then leaves the shape
//    untouched. TangentA / TangentB are the setback points on the two legs (t=0 / t=1 of a fillet arc, or the two chamfer-edge ends); for a
//    fillet Centre / Radius / StartAngle / SweepAngle carry the exact arc, matching ParametricSketchShape's own arc scalars.
struct ParametricSketchCornerSolution
{
    bool   Resolved   = false;          // [-]   - a valid corner edit was solved (false = leave the shape as-is)
    ImVec2 TangentA   = ImVec2(0, 0);   // [mm]  - setback point on the incoming leg (arc start / chamfer end A)
    ImVec2 TangentB   = ImVec2(0, 0);   // [mm]  - setback point on the outgoing leg (arc end / chamfer end B)
    ImVec2 Centre     = ImVec2(0, 0);   // [mm]  - fillet arc centre (unused for a chamfer)
    float  Radius     = 0.0f;           // [mm]  - fillet arc radius (unused for a chamfer)
    float  StartAngle = 0.0f;           // [rad] - fillet arc start angle at TangentA (unused for a chamfer)
    float  SweepAngle = 0.0f;           // [rad] - fillet arc signed sweep TangentA -> TangentB (unused for a chamfer)
    float  SafeLimit  = 0.0f;           // [mm]  - the largest radius / distance the two leg lengths allow (the drag clamps to this)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         FREE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Resolve the corner edit for vertex Index of Shape at the requested Magnitude (mm) and Category, WITHOUT mutating the shape. The corner is
// the triple (P[Index-1], P[Index], P[Index+1]); a closed shape wraps the neighbour lookup, an open Polyline rejects its two endpoints (no
// second leg). Fillet reads Magnitude as the arc radius, Chamfer as the equal setback distance along each leg; both clamp to SafeLimit =
// min(leg lengths) * tan(half-angle). Returns Resolved = false for a straight / degenerate corner or a non-positive Magnitude.
ParametricSketchCornerSolution SolveCornerEdit(const ParametricSketchShape&    Shape,
                                      int                    Index,
                                      float                  Magnitude,
                                      ParametricSketchCornerCategory  Category);

// Resolve the largest radius / distance vertex Index of Shape can carry (the drag's clamp ceiling), or 0 for a degenerate / non-corner
// vertex. A convenience over SolveCornerEdit for the modal to seed its arm-time safe limit without solving the full arc.
float ResolveCornerSafeLimit(const ParametricSketchShape& Shape, int Index);

// Fillet vertex Index of the shape Identifier in the store: solve the tangent arc at Magnitude, rebuild the host loop (replace the corner
// point with the two tangent points, splicing the arc span for a closed Profile / open Polyline), re-solve via ConstructParametricSketchShape,
// record a History edit ("Filleted <Title>"), and re-select. No-op returning false for a degenerate corner / absent shape. The arc stays
// exact â€” the view flattens it at the curve-resolution budget for display only.
bool FilletShapeCorner(ParametricSketchShapeStore& Store, uint32_t Identifier, int Index, float Magnitude);

// Chamfer vertex Index of the shape Identifier in the store: solve the equal setback at Magnitude, replace the corner point with the two
// tangent points (a straight edge between them), re-solve via ConstructParametricSketchShape, record a History edit ("Chamfered <Title>"), and
// re-select. No-op returning false for a degenerate corner / absent shape.
bool ChamferShapeCorner(ParametricSketchShapeStore& Store, uint32_t Identifier, int Index, float Magnitude);

// Evaluate the four interior parametric points of a fillet arc solution (t = 0.25, 0.5, 0.75, and the mathematical centre) into Out, for
// the snapping engine to register alongside the arc's endpoints (t = 0 / 1) and centre. Out is cleared then filled; a chamfer / unresolved
// solution yields an empty Out. The points are exact (evaluated on the analytic arc), not sampled off the display polyline.
void EvaluateArcParametricSnaps(const ParametricSketchCornerSolution& Solution, std::vector<ImVec2>& Out);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_PARAMETRICAUTHORING_OPERATIONS_FILLET_PARAMETRICSKETCHFILLET_H
