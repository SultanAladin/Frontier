//==========================================================================================================================================
//                                                            ParametricSketchTransform.h
//==========================================================================================================================================
// ðŸ§© 2D construction transforms for the parametricSketching workspace â€” Offset (and, in later phases, Mirror / Array). Offset inflates / deflates
//    each selected CLOSED shape by a signed world-mm distance (Clipper2's polygon offsetter over the flattened outline) and stores the
//    result as one analytic Profile per surviving outer loop, its contained holes carried, the original kept. Mirrors the ParametricSketchBoolean
//    orchestration idiom: the Properties "Transform" card invokes these free functions on the current SelectionSet.

#ifndef FRONTIER_AUTHORING_PARAMETRICAUTHORING_OPERATIONS_TRANSFORM_PARAMETRICSKETCHTRANSFORM_H
#define FRONTIER_AUTHORING_PARAMETRICAUTHORING_OPERATIONS_TRANSFORM_PARAMETRICSKETCHTRANSFORM_H

#include <vector>
#include "imgui.h"
#include "ParametricSketchShapeStore.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CATEGORIES
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ What AppendOffsetResult resolved to â€” the Properties panel maps each onto a chip / notice. Mirrors BooleanOutcome.
// How an offset CORNER is built - the Clipper2 JoinType the offsetter uses at each convex vertex. Round is the CAD default (a true arc), Miter
//    extends the two edges to their sharp intersection (clamped), Bevel (Square) cuts the corner off flat. The operator box's "Corners" dropdown
//    selects one; the value maps 1:1 onto Clipper2Lib::JoinType inside SolveLoopOffset.
enum class SketchOffsetCornerStyle
{
    Round = 0,   // [-] - a true arc at each convex corner (the CAD default)
    Miter = 1,   // [-] - the two edges extended to their sharp intersection (clamped by Clipper2's miter limit)
    Bevel = 2    // [-] - the corner cut off flat (Clipper2 Square)
};

enum class OffsetOutcome
{
    Committed         = 0,   // [-] - one or more offset Profiles were appended; originals kept
    NeedsClosedShapes = 1,   // [-] - the selection held no offsettable closed shape; nothing changed
    EmptyResult       = 2,   // [-] - the offset collapsed every loop (a large negative distance ate the shape); nothing changed
    NothingSelected   = 3     // [-] - the selection set was empty; nothing changed
};

// ðŸ“ What AppendMirrorResult resolved to â€” the Properties panel maps each onto a notice. Mirrors OffsetOutcome.
enum class MirrorOutcome
{
    Committed       = 0,   // [-] - one or more mirror copies were appended; originals kept
    NoDatum         = 1,   // [-] - no selected shape had its datum enabled (nothing to reflect across); nothing changed
    EmptyResult     = 2,   // [-] - every reflected shape failed to re-solve; nothing changed
    NothingSelected = 3    // [-] - the selection set was empty; nothing changed
};

// ðŸ“ What AppendArrayResult resolved to â€” the Properties panel maps each onto a notice. Mirrors MirrorOutcome.
enum class ArrayOutcome
{
    Committed       = 0,   // [-] - one or more array copies were emitted; original kept
    NotEnabled      = 1,   // [-] - no selected shape had its array enabled; nothing changed
    EmptyResult     = 2,   // [-] - the parameters emitted no copy (count < 2 / no host vertices); nothing changed
    NothingSelected = 3    // [-] - the selection set was empty; nothing changed
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        FREE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ Offset ONE closed world-mm loop by a signed distance (positive = outward / inflate, negative = inward / deflate) via Clipper2's
//    polygon offsetter. Returns the resulting loop set (outer loops CCW, hole loops CW) â€” a single loop usually, but a concave inward
//    offset can split into several, and an outward offset of a self-near shape can merge. Empty when the offset collapses the region.
//    JoinType is Round so an offset corner reads as a true arc (the CAD default); the caller flattens the analytic shape first.
std::vector<std::vector<ImVec2>> SolveLoopOffset(const std::vector<ImVec2>& Loop, float DistanceMm,
                                                 SketchOffsetCornerStyle CornerStyle = SketchOffsetCornerStyle::Round);

// 📝 Offset ONE open polyline by a signed distance into a SINGLE parallel open curve on one side (the Blender "offset open curve" result, not
//    Clipper2's both-sided ribbon). Positive shifts along the LEFT segment normal, negative shifts right. Interior joints ride the averaged
//    adjacent normal lengthened to hold a constant perpendicular distance (a miter), clamped so a sharp corner bevels instead of spiking. Returns
//    the offset polyline (same point count as the source), or empty when the source has < 2 points or the distance is ~zero.
std::vector<ImVec2> SolveOpenCurveOffset(const std::vector<ImVec2>& Polyline, float DistanceMm);

// ðŸ“ The orchestrator the Properties "Offset" button calls: for every CLOSED shape in the SelectionSet, flatten its outline (holes
//    carried), offset it by DistanceMm, and append one Profile per surviving outer loop (its contained holes carried, tint + folder
//    inherited from the source). The originals are KEPT (offset is additive construction, not a consuming boolean). Re-selects the new
//    Profiles. Sets Store.Notice on a failure path; never mutates the store on a non-Committed outcome. A shape with no closed region is
//    skipped (an open Line / curve cannot be area-offset this pass); the outcome is NeedsClosedShapes only when NONE offsetted.
OffsetOutcome AppendOffsetResult(ParametricSketchShapeStore& Store, float DistanceMm,
                                 SketchOffsetCornerStyle CornerStyle = SketchOffsetCornerStyle::Round);

// ðŸ“ The orchestrator the Properties "Mirror across datum" button calls: reflect every selected shape that carries an enabled datum across
//    that datum's active axis (DatumAnchor + DatumRotation orient the axes; DatumAxis picks them â€” Horizontal / Vertical, or Cross â†’ both,
//    a 4-up with the original). Each reflected copy re-solves from its reflected defining points (the DuplicateParametricSketchShape idiom), carrying
//    tint / folder / elevation / fill and its (reflected) hole loops; closed loops reverse winding to keep the outer-CCW / hole-CW convention
//    a reflection would otherwise flip. Originals are KEPT (mirror is additive construction). Re-selects the new copies. Sets Store.Notice on a
//    failure path; never mutates the store on a non-Committed outcome. Corner fillets are not carried onto the copy this pass (re-addable via B).
MirrorOutcome AppendMirrorResult(ParametricSketchShapeStore& Store);

// ðŸ“ Refresh every DRIVEN mirror child (a shape with MirrorSource != 0) from its live source: reflect the source across the source datum's
//    axis (picked by the child's MirrorAxisIndex) and overwrite the child's geometry in place, preserving its identity (Identifier / Title /
//    MirrorSource / MirrorAxisIndex / TintIndex / Displayed). A child whose source vanished, whose source datum is disabled, or whose axis the
//    source no longer emits is ERASED (the mirror relationship is gone). Called by AppendParametricSketchEditDetailed before every history snapshot so a
//    source edit or a datum move propagates to the copies. Never logs / snapshots itself (the caller does); safe to call when nothing is mirrored.
void ReflowMirrorChildren(ParametricSketchShapeStore& Store);

// ðŸ“ The orchestrator the Properties "Build / Refresh array" button calls: for every selected shape that carries an enabled array (and is not
//    itself a driven child), stamp its current mode / parameters, erase its existing array children (refresh-not-duplicate), and let the reflow
//    build the run fresh. Linear steps each copy by ArrayLinearStep rotated through the datum angle; Radial sweeps ArrayRadialSweep about the
//    datum anchor; Instance-on-points drops one copy per defining vertex of ArrayHostShape. The original is KEPT (array is additive). Keeps the
//    SOURCE selected so its datum stays shown. Sets Store.Notice on a failure path; never mutates the store on a non-Committed outcome.
ArrayOutcome AppendArrayResult(ParametricSketchShapeStore& Store);

// ðŸ“ Refresh every DRIVEN array copy (a shape with ArraySource != 0) from its live source: re-stamp the source at this copy's slot (linear
//    translate / radial rotate about the datum anchor / instance-on-points host vertex) and overwrite the copy's geometry in place, preserving
//    its identity (Identifier / Title / ArraySource / ArrayMode / ArrayInstance / TintIndex / Displayed). A copy whose source vanished, whose
//    source array is disabled, whose ordinal now exceeds the instance count, or whose instance-on-points host is gone is ERASED. Called by
//    AppendParametricSketchEditDetailed (after ReflowMirrorChildren) before every history snapshot so a source edit or a parameter change propagates.
void ReflowArrayChildren(ParametricSketchShapeStore& Store);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_PARAMETRICAUTHORING_OPERATIONS_TRANSFORM_PARAMETRICSKETCHTRANSFORM_H
