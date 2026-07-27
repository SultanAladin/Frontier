/*==============================================================================================================================================
                                                          PARAMETRICSKETCHCONSTRAINTSOLVER.H
==============================================================================================================================================*/
// ðŸ§© The parametricSketching sketch's parametric constraint solver: a fixed-iteration Gauss-Seidel relaxation over a SHARED point pool, ported
//    from the p4.html prototype. A ParametricSketchShapeStore holds shapes whose Points are index-addressed with no identity; this module builds a
//    solve-time pool that welds coincident point handles into one moveable point, relaxes the free points so every constraint + driving
//    dimension holds, then writes the solved positions back into the shapes (re-deriving their analytic scalars via ConstructParametricSketchShape).
//    Round dimensions (Radius / Diameter) write the analytic scalar directly rather than moving points. Runs ONLY on an edit â€” never per
//    frame. Lives under the Authoring pillar (Authoring/Modeling/ParametricSketching) beside ParametricSketchShapeStore, the shape model it solves over.

#pragma once
#ifndef FRONTIER_AUTHORING_PARAMETRICAUTHORING_PARAMETRICSKETCHCONSTRAINTSOLVER_H
#define FRONTIER_AUTHORING_PARAMETRICAUTHORING_PARAMETRICSKETCHCONSTRAINTSOLVER_H

#include "ParametricSketchShapeStore.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Solve the sketch: relax the store's shapes so every constraint + driving dimension holds, then write the solved geometry back into the
// shapes (re-deriving analytic scalars + invalidating the flatten cache). A faithful port of p4.html solve() â€” 60 fixed relaxation
// iterations (no convergence check), constraints then dimensions per iteration, respecting the Fixed pins. Locked shapes are held rigid
// (all their points pinned). Records ONE edit-log entry so the History panel shows the re-solve. No-op when there is nothing to solve.
void SolveParametricSketchSketch(ParametricSketchShapeStore& Store);

// Resolve the sketch's degree-of-freedom count â€” the p4.html updateSolveState heuristic:
//     DOF = Î£ over shapes (freePointCount Ã— 2) âˆ’ Dimensions.size() âˆ’ Constraints.size()
// A free point is one not pinned by a Fixed constraint (and not on a locked shape). <= 0 reads as "fully constrained" in the Properties
// panel; a positive count reads as "Under-constrained Â· N DOF". Pure read â€” never mutates the store.
int ResolveDegreesOfFreedom(const ParametricSketchShapeStore& Store);

// Resolve whether the point handle is pinned by a Fixed constraint in the store (or lies on a locked shape). The interaction path calls
// this to toggle the Fixed state + paint the anchor mark. False for an unset handle.
bool ResolvePointFixed(const ParametricSketchShapeStore& Store, ParametricSketchPointHandle Handle);

} // namespace Frontier

#endif
