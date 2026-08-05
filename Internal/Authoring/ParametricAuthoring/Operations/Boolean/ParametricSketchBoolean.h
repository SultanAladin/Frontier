//==========================================================================================================================================
//                                                            ParametricSketchBoolean.h
//==========================================================================================================================================
// ðŸ§© 2D boolean regions for the parametricSketching workspace â€” Union / Subtract / Intersect on flattened closed loops (Clipper2 sweep-line clip
//    over the whole selection at once), and the orchestrator the Properties panel invokes. Results are stored as one analytic Profile
//    shape per surviving outer loop, with contained inner loops carried as CW holes so the fill renders a punched interior.

#ifndef FRONTIER_AUTHORING_PARAMETRICAUTHORING_OPERATIONS_BOOLEAN_PARAMETRICSKETCHBOOLEAN_H
#define FRONTIER_AUTHORING_PARAMETRICAUTHORING_OPERATIONS_BOOLEAN_PARAMETRICSKETCHBOOLEAN_H

#include <vector>
#include "imgui.h"
#include "ParametricSketchShapeStore.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CATEGORIES
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ The three region operations the workspace offers. Subtract is directed A-B (base minus the tools).
enum class BooleanCategory
{
    Union     = 0,   // [-] - the combined region of every operand
    Subtract  = 1,   // [-] - the base with every tool carved out (holes when a tool sits fully inside)
    Intersect = 2     // [-] - only the region shared by all operands
};

// ðŸ“ What AppendBooleanResult resolved to â€” the Properties panel maps each onto a chip / notice.
enum class BooleanOutcome
{
    Committed         = 0,   // [-] - a new Profile (or several) was appended; operands hidden
    NeedsClosedShapes = 1,   // [-] - an operand was open or un-flattenable; nothing changed
    EmptyResult       = 2,   // [-] - the operation produced no region (e.g. disjoint intersect); nothing changed
    TooFewOperands    = 3     // [-] - fewer than two shapes were selected
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        FREE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ One-pair clip (base = Subject, single tool = Clip), a convenience over SolveRegionBoolean. Loops are closed world-mm loops with no
//    repeated closing point. Returns the loop set (outer loops CCW, hole loops CW) from the Clipper2 PolyTree. Empty when no region survives.
std::vector<std::vector<ImVec2>> SolveLoopBoolean(const std::vector<ImVec2>& Subject,
                                                  const std::vector<ImVec2>& Clip,
                                                  BooleanCategory            Op);

// ðŸ“ Clip N operand loops in one Clipper2 Execute â€” operand 0 is the subject, every later operand a clip. Union / Subtract / Intersect
//    resolve the whole selection at once (holes + disjoint regions carried natively). Returns the outer(CCW)/hole(CW) loop set.
std::vector<std::vector<ImVec2>> SolveRegionBoolean(const std::vector<std::vector<ImVec2>>& OperandLoops,
                                                    BooleanCategory                         Op);

// ðŸ“ Clip N operand REGIONS in one Clipper2 Execute, each region a loop set { outer, hole0, hole1, â€¦ } (a Profile already carrying holes
//    hands its HoleLoops in here). Operand 0 is the subject region, every later operand a clip region â€” every loop (outer + holes) of an
//    operand is fed to Clipper2 so an existing void survives a further boolean (the NonZero rule reads a CW hole inside its CCW outer as a
//    punched interior). This is the multi-hole path: chaining subtracts accumulates holes instead of dropping the earlier ones. Returns
//    the outer(CCW)/hole(CW) loop set from the PolyTree.
std::vector<std::vector<ImVec2>> SolveRegionBooleanWithHoles(const std::vector<std::vector<std::vector<ImVec2>>>& OperandRegions,
                                                             BooleanCategory                                      Op);

// ðŸ“ Classify raw loops into outer (CCW) / hole (CW) by containment depth (even â‡’ outer, odd â‡’ hole) and force the winding; drops
//    zero-area slivers in place. Clipper2 output is already nested, so this is only needed when a caller hands in unclassified loops.
void NormalizeBooleanLoops(std::vector<std::vector<ImVec2>>& Loops);

// ðŸ“ The orchestrator the Properties buttons call: validates the SelectionSet is â‰¥2 closed shapes, flattens each, solves, and appends
//    one Profile per outer loop (with its contained holes). Hides the operands (recoverable via the outliner) and reselects the result.
//    Sets Store.Notice on a failure path. Never mutates the store on a non-Committed outcome.
BooleanOutcome AppendBooleanResult(ParametricSketchShapeStore& Store, BooleanCategory Op);

// The same orchestrator with the operand disposal made explicit - the Boolean popup's "Keep operands" toggle. KeepOperands = false is the
//    historical behaviour above (operands hidden, recoverable via the outliner); true leaves every operand DISPLAYED so the result overlays
//    its sources. The result is appended, logged and reselected identically either way - the only difference is the operands' Displayed flag.
BooleanOutcome AppendBooleanResult(ParametricSketchShapeStore& Store, BooleanCategory Op, bool KeepOperands);

// Solve the boolean WITHOUT touching the store - the popup's live preview. Reads OperandIdentifiers in order (front = the Subtract base),
//    flattens each operand exactly as the commit does (its outer loop + any holes it already carries), and returns the surviving loop set:
//    outer loops CCW, hole loops CW, so the consumer can stroke outers and voids distinctly. Empty when fewer than two operands resolve, an
//    operand is open / un-flattenable, or no region survives. Never sets Store.Notice - a preview must stay silent.
std::vector<std::vector<ImVec2>> ResolveBooleanPreviewLoops(ParametricSketchShapeStore&   Store,
                                                            const std::vector<uint32_t>& OperandIdentifiers,
                                                            BooleanCategory              Op);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_PARAMETRICAUTHORING_OPERATIONS_BOOLEAN_PARAMETRICSKETCHBOOLEAN_H
