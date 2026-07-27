//==========================================================================================================================================
//                                                              DraughtLoft.h
//==========================================================================================================================================
// 🧩 Lofting for the draughting workspace — interpolate a 3D display surface / solid through 2+ selected 2D section profiles. Each section
//    supplies its own Elevation (Z plane); the sections are correlated point-to-point (anti-twist), interpolated across (ruled or smooth),
//    and tessellated to a positions + normals + indices stream stored as a DraughtLoftBody on the store. Closed sections cap into a solid;
//    open sections form a double-sided sheet. This is a TESSELLATED display surface (not an exact B-rep), matching the CAD analytic-ops
//    approach. Mirrors the DraughtTransform / DraughtBoolean orchestration idiom: the Properties "Loft" card invokes AppendLoftResult on the
//    current SelectionSet, and AppendDraughtEditDetailed calls ReflowLoftBodies so a source-profile edit re-solves every driven body.

#ifndef FRONTIER_AUTHORING_MODELING_DRAUGHTING_DRAUGHTLOFT_H
#define FRONTIER_AUTHORING_MODELING_DRAUGHTING_DRAUGHTLOFT_H

#include "DraughtShapeStore.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CATEGORIES
//------------------------------------------------------------------------------------------------------------------------

// 📝 What AppendLoftResult resolved to — the Properties panel maps each onto a chip / notice. Mirrors OffsetOutcome / BooleanOutcome.
enum class LoftOutcome
{
    Committed          = 0,   // [-] - a lofted body was appended; the source sections are kept
    TooFewProfiles     = 1,   // [-] - fewer than two usable sections were resolved (and no apex); nothing changed
    NeedsUniformClosure = 2,  // [-] - the sections mixed open + closed (cannot loft across them coherently); nothing changed
    DegenerateSections = 3,   // [-] - a section flattened to too few distinct points to interpolate; nothing changed
    EmptyResult        = 4,   // [-] - the interpolation produced no triangles; nothing changed
    NothingSelected    = 5    // [-] - the selection set was empty; nothing changed
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        FREE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The orchestrator the Properties "Loft" button calls: resolve the ordered section profiles (Spec.SectionProfiles, or the current
//    SelectionSet / Selected when Spec leaves them empty), flatten each section at its Elevation, correlate section points to defuse the
//    twist, interpolate the surface across the sections (ruled or smooth per Spec.Transition), tessellate it (capping closed sections into
//    a solid), and append ONE DraughtLoftBody to Store.LoftBodies (tint / folder inherited from the base section). The source sections are
//    KEPT (loft is additive construction, not consuming). Selects the new body. Sets Store.Notice on a failure path; never mutates the
//    store on a non-Committed outcome. The core tier honours SectionProfiles + Transition + Correlation::Automatic + ClosedLoopEnabled;
//    GuideCurves / Centerline / non-Normal end conditions / Connectors are the P4 advanced tier (accepted but not yet interpolated).
LoftOutcome AppendLoftResult(DraughtShapeStore& Store, const LoftSpecification& Spec);

// 📝 Refresh every lofted body from its live source sections: re-flatten each section in Body.Recipe.SectionProfiles, re-correlate,
//    re-interpolate, and overwrite the body's Positions / Normals / Indices in place (bumping TessellationRevision so the GPU preview
//    re-uploads), preserving its identity (Identifier / Title / TintIndex / Displayed / Recipe). A body whose sections dropped below two
//    survivors is ERASED (the loft relationship is gone). Called by AppendDraughtEditDetailed (after the mirror / array reflow) before every
//    history snapshot so a source edit propagates to the body. Never logs / snapshots itself; safe to call when nothing is lofted.
void ReflowLoftBodies(DraughtShapeStore& Store);

// 📝 Resolve a lofted body by id (null when absent) — the outliner / Properties / GPU preview borrow the selected body through this.
DraughtLoftBody* ResolveLoftBody(DraughtShapeStore& Store, uint32_t Identifier);

// 📝 Detach the lofted body Identifier from the store: erase it, clear it from the selection, and record an edit-log entry. No-op when
//    absent. A body carries no constraints / dimensions to prune, so this mirrors DetachDatum. (`Delete` / `Remove` → Detach.)
void DetachLoftBody(DraughtShapeStore& Store, uint32_t Identifier);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_MODELING_DRAUGHTING_DRAUGHTLOFT_H
