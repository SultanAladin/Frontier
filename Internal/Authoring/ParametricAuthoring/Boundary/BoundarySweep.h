/*==============================================================================================================================================
                                                              BOUNDARYSWEEP.H
==============================================================================================================================================*/
// 🧩 The DEDICATED extrude for the parametric sketcher — a profile swept into a real B-rep prism (or a thin wall, for an open profile). It shares
//    NOTHING with the polygon kernel's PolygonExtrude: that one clones faces inside a PolygonCluster's flat face stream for subdivision-surface
//    modelling, which is a different data model with different invariants (it has no coedge, no radial ring, no envelope, and its "faces" are index
//    spans, not bounded surfaces). Trying to serve both from one kernel is what produced the inert extrude in the first place.
//
//    🔴 THE POINT OF THIS FILE. The existing sketch extrude writes a scalar depth onto a shape and lets the tessellator emit a triangle stream for
//       the matcap. That stream is a rendering artifact with no topology, so the result is a solid you can look at and nothing else. Here the sweep
//       CONSTRUCTS the boundary — every cap corner, every wall quad, every shared edge — so the outcome hands back real tokens the viewport can pick,
//       highlight, and drag. Editability is not bolted on afterwards; it is what the operation produces.
//
//    Winding contract, and why the caller must honour it: the outer ring arrives counter-clockwise seen from +Direction, hole rings clockwise (the
//    same convention EvaluateFilledPolygon already enforces on the sketch store). Given that, the wall quad [A0, B0, B1, A1] has an outward normal
//    for BOTH the outer ring and the hole rings with no runtime sign test — for a hole, "outward" correctly means "into the cavity".
//
//        end cap (+Direction, ring as authored)          ┌───────────┐
//                                                        │           │   wall quad per ring span:
//        wall band (one quad per ring span)              │  A1───B1  │      [A0, B0, B1, A1]
//                                                        │  │     │  │
//        start cap (-Direction, ring reversed)           └──A0───B0──┘

#pragma once
#ifndef FRONTIER_AUTHORING_PARAMETRICAUTHORING_BOUNDARY_BOUNDARYSWEEP_H
#define FRONTIER_AUTHORING_PARAMETRICAUTHORING_BOUNDARY_BOUNDARYSWEEP_H

#include "BoundaryTopology.h"

#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 What ExtrudeProfileIntoBrep resolved to. The caller maps each onto a notice, exactly as the sketch store's boolean / offset outcomes do.
enum class SweepOutcomeCategory
{
    Completed         = 0,   // [-] - the envelope was built and the audit found no structural fault
    DegenerateProfile = 1,   // [-] - the outer ring had too few distinct points to sweep
    ZeroDistance      = 2,   // [-] - the requested distance rounds to nothing
    DegenerateAxis    = 3,   // [-] - the direction vector could not be normalized
    OpenProfileCapped = 4,   // [-] - caps were requested on an open profile (an open run bounds no face); the sweep still built, uncapped
    ValidationFault   = 5    // [-] - the boundary failed its structural audit and was ROLLED BACK (see the rollback contract below)
};

// 📝 Provenance stamped on every face / vertex the sweep creates, so the viewport can filter a pick ("only the end cap", "walls only") and so a later
//    edit can find the ring it belongs to without re-deriving anything geometrically.
enum class SweepGroupCategory : uint32_t
{
    StartCap   = 1u,   // [-] - the face at the profile's own elevation (normal against the sweep direction)
    EndCap     = 2u,   // [-] - the face at the swept elevation (normal along the sweep direction)
    OuterWall  = 3u,   // [-] - a wall quad raised from the outer ring
    InnerWall  = 4u    // [-] - a wall quad raised from a hole ring (its normal points into the cavity)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The 2D region being swept, already flattened to world millimetres on its sketch plane (curves tessellated by the caller — a sweep cannot carry
//    an analytic arc through to a boundary without a curved-surface representation, so the flatten happens before this point, once).
struct SweepProfile
{
    std::vector<BoundaryVector>              OuterRing;              // [mm] - counter-clockwise seen from +Direction, no repeated closing point
    std::vector<std::vector<BoundaryVector>> InnerRings;             // [mm] - hole rings, clockwise seen from +Direction
    bool                                     ClosedEnabled = true;   // [-]  - false = an open run; the sweep yields a thin wall with no caps
};

// 📝 How the profile is swept. Distance is signed (a negative distance sweeps against Direction, which is how a downward drag is expressed without a
//    separate flag). SymmetricEnabled straddles the profile's own plane, which no drag direction can convey and so must be stated here.
struct SweepSpecification
{
    BoundaryVector Direction{ 0.0, 0.0, 1.0 };   // [-]   - the sweep axis (normalized internally)
    double         Distance         = 0.0;        // [mm]  - signed sweep height
    bool           SymmetricEnabled = false;      // [-]   - straddle the profile plane (half the height either side)
    double         DraftRadians     = 0.0;        // [rad] - taper: the end ring is offset outward by Distance * tan(Draft) along its own miter normal
    bool           StartCapEnabled  = true;       // [-]   - close the start face (ignored for an open profile)
    bool           EndCapEnabled    = true;       // [-]   - close the end face (ignored for an open profile)
    uint32_t       GroupTagBase     = 0u;         // [-]   - added to every SweepGroupCategory stamp, so several sweeps into one body stay distinguishable
};

// 📝 ONE ring raised twice. BasePositions is the profile-plane source the ring was lifted FROM, stored verbatim and never re-derived.
//
//    🔴 THIS FIELD IS THE FIX FOR THE SYMMETRIC-DRAG DRIFT. Recovering the base by subtracting the CURRENT start offset from the CURRENT start ring is
//       circular: on a symmetric sweep the offset that placed the ring came from the OLD distance while the offset being subtracted comes from the NEW
//       one, so the recovered "base" moves by half the delta and the whole body creeps away under the pointer, one drag frame at a time. Keeping the
//       source positions makes every re-drag idempotent from the same reference, which is what the old comment claimed and the old code did not do.
struct SweepRingRecord
{
    std::vector<BoundaryVector> BasePositions;      // [mm] - the profile-plane ring the lift started from; authoritative, never recomputed
    std::vector<VertexToken>    StartRing;          // [-]  - corners at the start plane, index-for-index with BasePositions
    std::vector<VertexToken>    EndRing;            // [-]  - corners at the end plane, index-for-index with BasePositions
    bool                        HoleRing = false;   // [-]  - false for the outer ring, true for each punched cavity
};

// 📝 Everything the sweep produced, in tokens the viewport can pick and drag directly. The vertex runs are the corner handles: a drag on one of
//    EndRingVertices is exactly the "move the extruded corner" gesture that the depth-scalar approach cannot express at all.
struct SweepOutcome
{
    SweepOutcomeCategory Category = SweepOutcomeCategory::DegenerateProfile;

    EnvelopeToken            Envelope;            // [-] - the envelope the sweep attached (Outer for a solid, Sheet for a thin wall)
    FaceToken                StartCapFace;        // [-] - unassigned when uncapped
    FaceToken                EndCapFace;          // [-] - unassigned when uncapped
    std::vector<FaceToken>   WallFaces;           // [-] - one per ring span, outer ring first then each hole ring

    // 🔴 EVERY ring, outer first then each hole. EnforceSweepDistance walks all of them: moving only the outer ring leaves the cavity walls at the
    //    build-time height while the outer wall follows the pointer, and the hole tears away from both caps.
    std::vector<SweepRingRecord> Rings;

    std::vector<VertexToken> StartRingVertices;   // [-] - a copy of Rings[0].StartRing, for a caller that only wants the outer handles
    std::vector<VertexToken> EndRingVertices;     // [-] - a copy of Rings[0].EndRing, matching StartRingVertices index for index

    // 🔴 The SIGN-NORMALISED sweep as actually built, carried so a re-drag replays it instead of trusting the caller to hand back a byte-identical
    //    specification (nothing recorded or verified that, and a mismatched draft angle or straddle flag silently rebuilt the body wrong). Direction
    //    is the unit axis, Distance is always POSITIVE: a negative authored distance is folded into the axis here, so no downstream step ever sees a
    //    backwards sweep — which is what turned both cap normals inward and reversed every wall quad into a fully inside-out prism on a downward drag.
    SweepSpecification ResolvedSweep;

    // 📝 Whether the profile that built this was closed. Recorded rather than inferred because a re-drag needs it for the miter solve (an open run's
    //    endpoints must not wrap) and there is no reliable way to recover it from the built topology afterwards.
    bool ClosedProfile = true;

    BoundaryValidationOutcome Audit;              // [-] - the structural audit, scoped to THIS envelope (not the whole body)
    std::string               Notice;             // [-] - one reader line on a non-Completed outcome ("" otherwise)
};

// Whether the sweep actually built geometry. Completed is the clean case; OpenProfileCapped built a valid uncapped sheet and merely reports that the
// requested caps were impossible, so it is a SUCCESS a caller should render — treating it as failure would reject every open-profile thin wall.
inline bool QuerySweepConstructed(SweepOutcomeCategory Category)
{
    return Category == SweepOutcomeCategory::Completed || Category == SweepOutcomeCategory::OpenProfileCapped;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Sweep Profile into Body along Specification, ATTACHING a new envelope (the body is not cleared, so several sweeps may share one body). A closed
// profile yields a capped prism: a start cap wound against the sweep direction, an end cap wound with it, and a wall band per ring — all sharing
// their corner vertices, so the result is watertight by construction rather than by a later stitching pass. An open profile yields a Sheet envelope
// of wall quads only, since an open run bounds no face to cap.
//
// 🔴 ROLLBACK CONTRACT — the one statement, no second version elsewhere. On ANY outcome for which QuerySweepConstructed is false, the envelope is
//    detached and the body is left exactly as it was found, so a failed sweep never leaves debris for the next operation to trip over. That INCLUDES
//    ValidationFault: a structurally unsound envelope left in the body would fail every subsequent audit and blame whichever operation ran next.
//    The audit findings survive in Outcome.Audit, so a caller can still report WHY without the wreckage staying attached.
bool ExtrudeProfileIntoBrep(FullBrepBody&             Body,
                            const SweepProfile&       Profile,
                            const SweepSpecification& Specification,
                            SweepOutcome&             Outcome);

// Re-sweep an EXISTING outcome to a new distance without rebuilding topology: every ring (outer AND each hole) is re-placed from its stored
// BasePositions at the new offsets and re-drafted, then the touched faces re-plane. This is what an interactive height drag calls each frame —
// O(ring) writes instead of a full teardown and rebuild, and crucially it PRESERVES every token, so a selection made mid-drag survives the drag.
//
// The sweep AXIS, the draft angle and the symmetric straddle all come from Outcome (resolved at build time); only the distance is passed. A negative
// DistanceMm re-normalises exactly as the build did, so dragging back through zero flips the sweep instead of inverting the solid. False on an
// outcome that never constructed, or on stale tokens.
bool EnforceSweepDistance(FullBrepBody& Body, const SweepOutcome& Outcome, double DistanceMm);

// The reader label for a sweep outcome, for a notice line.
const char* ResolveSweepOutcomeLabel(SweepOutcomeCategory Category);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_PARAMETRICAUTHORING_BOUNDARY_BOUNDARYSWEEP_H
