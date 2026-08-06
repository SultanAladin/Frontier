/*==============================================================================================================================================
                                                          PARAMETRICSKETCHCONSTRAINTSOLVER.H
==============================================================================================================================================*/
// 🧩 The parametricSketching sketch's parametric constraint solver: a capped-iteration Gauss-Seidel relaxation over a SHARED point pool,
//    ported from the p4.html prototype. A ParametricSketchShapeStore holds shapes whose Points are index-addressed with no identity;
//    this module builds a solve-time pool that welds coincident point handles into one moveable point, relaxes the free points so every
//    constraint + driving dimension holds, then writes the solved positions back into the shapes (re-deriving their analytic scalars via
//    ConstructParametricSketchShape). Coincident constraints hard-weld their two handles regardless of separation; Tangent is a real
//    distance rule (line↔circle = Radius, circle↔circle = R₁ ± R₂) with round centres pooled like ordinary points. Round dimensions
//    (Radius / Diameter) write the analytic scalar directly rather than moving points. Runs ONLY on an edit — never per frame. Lives
//    under the Authoring pillar (Authoring/Modeling/ParametricSketching) beside ParametricSketchShapeStore, the shape model it solves over.

#pragma once
#ifndef FRONTIER_AUTHORING_PARAMETRICAUTHORING_PARAMETRICSKETCHCONSTRAINTSOLVER_H
#define FRONTIER_AUTHORING_PARAMETRICAUTHORING_PARAMETRICSKETCHCONSTRAINTSOLVER_H

#include "ParametricSketchShapeStore.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 How a Solve run ended. Settled = the relaxation converged (largest per-iteration point move ≤ the residual tolerance) before the
//    iteration cap — or there was nothing to solve; Unsettled = the cap ran out with the residual still live, i.e. a conflicting
//    constraint / dimension pair the solver cannot satisfy. Reported instead of silently writing a half-solved sketch.
enum class ParametricSketchSolveCategory
{
    Settled   = 0,   // [-] - converged (residual ≤ tolerance) or nothing to solve
    Unsettled = 1    // [-] - the iteration cap ran out with the residual still above the tolerance
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The solve report a caller can surface ("sketch not fully solved") instead of guessing from geometry. IterationsUsed / FinalResidual
//    carry the convergence probe; FinalResidual is in world millimetres.
struct ParametricSketchSolveOutcome
{
    ParametricSketchSolveCategory Category       = ParametricSketchSolveCategory::Settled;  // [-] - settle vs cap exhaustion
    int                           IterationsUsed = 0;                                        // [-] - iterations actually run (0 = nothing to solve)
    float                         FinalResidual  = 0.0f;                                     // [mm] - largest single-point move on the final iteration
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Solve the sketch: relax the store's shapes so every constraint + driving dimension holds, then write the solved geometry back into the
// shapes (re-deriving analytic scalars + invalidating the flatten cache). A port of p4.html solve() that replaces the fixed 60 iterations
// with a convergence-capped run: iterations stop early once the largest single-point move settles under the residual tolerance, and the
// returned outcome reports settle vs cap exhaustion instead of silently leaving a half-solved sketch. Locked shapes are held rigid (all
// their points pinned). Records ONE edit-log entry so the History panel shows the re-solve. No-op (Settled, 0 iterations) when there is
// nothing to solve.
ParametricSketchSolveOutcome SolveParametricSketchSketch(ParametricSketchShapeStore& Store);

// Resolve the sketch's degree-of-freedom count — the p4.html updateSolveState heuristic, counted over WELDED GROUPS: DOF = Σ over pooled
// points (freePointCount × 2) − Dimensions.size() − (constraints excluding Coincident and Fixed). A free pooled point is one not pinned by
// a Fixed constraint (and not on a locked shape); a Coincident pair is ONE pooled point (its merge already removed the two raw DOFs), and
// a Fixed point is excluded by the pool count rather than subtracted again. <= 0 reads as "fully constrained" in the Properties panel; a
// positive count reads as "Under-constrained · N DOF". Pure read — never mutates the store.
int ResolveDegreesOfFreedom(const ParametricSketchShapeStore& Store);

// Resolve whether the point handle is pinned by a Fixed constraint in the store (or lies on a locked shape). The interaction path calls
// this to toggle the Fixed state + paint the anchor mark. False for an unset handle.
bool ResolvePointFixed(const ParametricSketchShapeStore& Store, ParametricSketchPointHandle Handle);

} // namespace Frontier

#endif
