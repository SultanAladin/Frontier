/*==============================================================================================================================================
                                                              BOUNDARYTOPOLOGY.H
==============================================================================================================================================*/
// 🧩 The FULL B-REP boundary structure — the editable source of truth a solid is authored in, as opposed to the tessellated triangle stream a
//    viewport draws. A triangle stream stores only GEOMETRY (where points are); this stores TOPOLOGY as well (which vertex bounds which edge,
//    which edge bounds which face, which faces meet along which edge), which is exactly what a selection / corner-drag / face-push needs and
//    exactly what a triangle soup throws away. An extruded solid built here is selectable + movable by construction.
//
//    The layout is the RADIAL-EDGE structure (Weiler 1986), the model real kernels use, rather than a plain half-edge / DCEL:
//
//        FullBrepBody ─▶ SolidEnvelope ─▶ SolidFace ─▶ FaceLoop ─▶ CoEdge ─▶ SolidEdge ─▶ SolidVertex
//                       (a closed or      (bounded    (one outer   (one USE   (the shared  (the point;
//                        open boundary     surface)    ring + N     of an      curve)       the ONLY
//                        run; "shell"                  hole rings)  edge by                 place a
//                        is a banned word)                          one loop)               position lives)
//
//    🔴 WHY RADIAL AND NOT PLAIN HALF-EDGE. A half-edge pairs every edge with exactly TWO faces, so it cannot represent the states a real CAD
//       session passes through: an edge shared by three faces mid-boolean, a dangling wall with no second face yet, a wire edge with no face at
//       all, a void envelope inside a solid. Here a SolidEdge owns a RADIAL RING — a circular list of every CoEdge that uses it — of any length:
//         0 = a wire edge · 1 = an open boundary edge (a sheet's rim) · 2 = manifold · 3+ = non-manifold, legal and reported, never a crash.
//       An extrude in progress is legitimately non-manifold for one step, so this is not academic.
//
//    🔴 GEOMETRY LIVES ONLY ON THE VERTEX. A face carries a plane it re-derives, an edge carries no points at all. So EnforceVertexPosition is
//       the whole of "drag a corner": every edge and face touching that vertex follows because they never held a copy of it. This is the property
//       the current extrude lacks and the reason its output is inert.
//
//    Identity is a GENERATIONAL token (slot + generation), so a token held across a detach is detected as stale instead of silently resolving to
//    whatever was recycled into its slot. Detached slots are recycled through a per-arena vacancy list.
//
//    No dependency beyond the standard library — deliberately. This unit is the geometric kernel; the ImGui / store / Vulkan bridges live above it.

#pragma once
#ifndef FRONTIER_AUTHORING_PARAMETRICAUTHORING_BOUNDARY_BOUNDARYTOPOLOGY_H
#define FRONTIER_AUTHORING_PARAMETRICAUTHORING_BOUNDARY_BOUNDARYTOPOLOGY_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          IDENTITY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Phantom tags so a face token can never be passed where a vertex token is expected. Zero runtime cost — the tag is never instantiated.
namespace BoundaryTag
{
    struct Vertex   {};
    struct Edge     {};
    struct CoEdge   {};
    struct Loop     {};
    struct Face     {};
    struct Envelope {};
}

constexpr uint32_t BoundaryVacantSlot = 0xFFFFFFFFu;   // [-] - the "no record" slot sentinel

// 📝 A generational handle into one arena: Slot indexes the dense record run, Generation is bumped on every detach of that slot so a token kept
//    across a detach fails to resolve instead of aliasing the record recycled into its place.
template <typename TagType>
struct BoundaryToken
{
    uint32_t Slot       = BoundaryVacantSlot;   // [-] - dense index into the owning arena
    uint32_t Generation = 0u;                   // [-] - bumped on detach; a mismatch means the token is stale
};

template <typename TagType> inline bool operator==(BoundaryToken<TagType> Left, BoundaryToken<TagType> Right)
{
    return Left.Slot == Right.Slot && Left.Generation == Right.Generation;
}
template <typename TagType> inline bool operator!=(BoundaryToken<TagType> Left, BoundaryToken<TagType> Right)
{
    return !(Left == Right);
}
template <typename TagType> inline bool TokenAssigned(BoundaryToken<TagType> Token)
{
    return Token.Slot != BoundaryVacantSlot;
}

using VertexToken   = BoundaryToken<BoundaryTag::Vertex>;
using EdgeToken     = BoundaryToken<BoundaryTag::Edge>;
using CoEdgeToken   = BoundaryToken<BoundaryTag::CoEdge>;
using LoopToken     = BoundaryToken<BoundaryTag::Loop>;
using FaceToken     = BoundaryToken<BoundaryTag::Face>;
using EnvelopeToken = BoundaryToken<BoundaryTag::Envelope>;

//------------------------------------------------------------------------------------------------------------------------
//                                                           ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

// 📝 A local double-precision position so the kernel carries no math-header coupling. Authored MILLIMETRES throughout, matching the sketch store;
//    the render bridge is the single place that scales to the viewport's unit.
struct BoundaryVector
{
    double XCoord = 0.0;   // [mm]
    double YCoord = 0.0;   // [mm]
    double ZCoord = 0.0;   // [mm]
};

inline BoundaryVector AddBoundaryVector(BoundaryVector A, BoundaryVector B)
{
    return BoundaryVector{ A.XCoord + B.XCoord, A.YCoord + B.YCoord, A.ZCoord + B.ZCoord };
}
inline BoundaryVector SubtractBoundaryVector(BoundaryVector A, BoundaryVector B)
{
    return BoundaryVector{ A.XCoord - B.XCoord, A.YCoord - B.YCoord, A.ZCoord - B.ZCoord };
}
inline BoundaryVector ScaleBoundaryVector(BoundaryVector A, double Factor)
{
    return BoundaryVector{ A.XCoord * Factor, A.YCoord * Factor, A.ZCoord * Factor };
}
inline double DotBoundaryVector(BoundaryVector A, BoundaryVector B)
{
    return A.XCoord * B.XCoord + A.YCoord * B.YCoord + A.ZCoord * B.ZCoord;
}
inline BoundaryVector CrossBoundaryVector(BoundaryVector A, BoundaryVector B)
{
    return BoundaryVector{ A.YCoord * B.ZCoord - A.ZCoord * B.YCoord,
                           A.ZCoord * B.XCoord - A.XCoord * B.ZCoord,
                           A.XCoord * B.YCoord - A.YCoord * B.XCoord };
}
double EvaluateBoundaryLength(BoundaryVector A);
BoundaryVector NormalizeBoundaryVector(BoundaryVector A, bool& OutResolved);

//------------------------------------------------------------------------------------------------------------------------
//                                                        ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which ring a loop is: the face's outer boundary, or a hole punched through it. A face carries exactly one Outer and any number of Inner.
enum class LoopCategory
{
    Outer = 0,   // [-] - the face's outer boundary ring
    Inner = 1    // [-] - a hole ring (wound opposite the outer ring when viewed along the face normal)
};

// 📝 The surface a face is carried on. Planar is fully solved here (a Newell plane); the analytic / freeform entries are declared so a NURBS face
//    can be attached later without a structural change — the topology above them is identical either way.
enum class FaceCategory
{
    Planar   = 0,   // [-] - a flat face; PlaneNormal + PlaneOffset are authoritative
    Ruled    = 1,   // [-] - a swept wall between two rings (planar per quad, tagged for the sweep's benefit)
    Analytic = 2,   // [-] - cylinder / cone / sphere / torus (surface parameters live above this unit)
    Freeform = 3    // [-] - NURBS patch (control net lives above this unit)
};

// 📝 What an envelope bounds. Outer is a solid's exterior; Void is an interior cavity (its faces point INTO the cavity); Sheet is an open run with
//    a rim — the thin-wall extrude of an open profile is exactly a Sheet, which is why it must be representable rather than an error.
enum class EnvelopeCategory
{
    Outer = 0,   // [-] - the exterior boundary of a solid region
    Void  = 1,   // [-] - an enclosed internal cavity
    Sheet = 2    // [-] - an open boundary run (has rim edges with a radial count of 1)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          RECORDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A corner point. The ONLY holder of geometry in the whole structure — which is what makes a corner drag a one-field write with every incident
//    edge and face following for free. IncidentEdges is kept explicitly rather than walked as a disk cycle: a non-manifold vertex has no single
//    well-defined cycle, and an honest small vector is cheaper to keep correct than a cycle that silently truncates.
struct SolidVertex
{
    BoundaryVector         Position;         // [mm] - the authored corner position
    std::vector<EdgeToken> IncidentEdges;    // [-]  - every edge touching this vertex (unordered; non-manifold safe)
    uint32_t               GroupTag = 0u;    // [-]  - caller-defined provenance mark (the sweep stamps start-ring / end-ring here)
};

// 📝 The shared curve between two vertices. Carries NO points — its geometry is entirely its two endpoints (a straight edge) or a curve resolved
//    above this unit. RadialFirst heads a circular singly-linked ring of every CoEdge that uses this edge; RadialCount is that ring's length and
//    is the manifold classifier (0 wire · 1 rim · 2 manifold · 3+ non-manifold).
struct SolidEdge
{
    VertexToken VertexA;                 // [-] - the edge's first endpoint (its forward sense runs A -> B)
    VertexToken VertexB;                 // [-] - the edge's second endpoint
    CoEdgeToken RadialFirst;             // [-] - head of the radial ring of uses (unassigned = a wire edge)
    uint32_t    RadialCount = 0u;        // [-] - length of the radial ring
    uint32_t    GroupTag    = 0u;        // [-] - caller-defined provenance mark
};

// 📝 ONE USE of an edge by ONE loop — the half-edge, extended for the radial world. NextInLoop / PreviousInLoop walk the loop's boundary ring;
//    NextRadial walks every OTHER use of the same edge (across faces). SenseReversed records whether this use runs against the edge's own A -> B
//    direction, which is how two adjacent faces can traverse one shared edge in opposite directions without duplicating it.
struct CoEdge
{
    EdgeToken   Edge;                        // [-] - the edge this is a use of
    VertexToken OriginVertex;                // [-] - the vertex this use STARTS at (following the loop direction)
    CoEdgeToken NextInLoop;                  // [-] - next use around the owning loop
    CoEdgeToken PreviousInLoop;              // [-] - previous use around the owning loop
    CoEdgeToken NextRadial;                  // [-] - next use of the SAME edge (circular; itself when the edge has one use)
    LoopToken   OwningLoop;                  // [-] - the loop this use belongs to
    bool        SenseReversed = false;       // [-] - true when this use runs Edge.VertexB -> Edge.VertexA
};

// 📝 One closed ring of coedges bounding part of a face. A face's loops form a singly-linked run headed by SolidFace::FirstLoop, outer ring first.
struct FaceLoop
{
    CoEdgeToken  FirstCoEdge;                        // [-] - any use in the ring (the ring is circular, so any entry works)
    FaceToken    OwningFace;                         // [-] - the face this ring bounds
    LoopToken    NextLoopOnFace;                     // [-] - next ring on the same face (unassigned = last)
    LoopCategory Category   = LoopCategory::Outer;   // [-] - outer boundary or hole ring
    uint32_t     CoEdgeCount = 0u;                   // [-] - ring length, kept for validation + cheap reserves
};

// 📝 A bounded piece of surface. PlaneNormal / PlaneOffset are re-derived by EnforceFacePlane from the outer ring (Newell's method, which is
//    correct for a non-planar-ish ring too — it returns the best-fit plane rather than exploding on the first three collinear points).
struct SolidFace
{
    LoopToken      FirstLoop;                           // [-] - head of the loop run (outer ring first)
    EnvelopeToken  OwningEnvelope;                      // [-] - the envelope this face belongs to
    FaceToken      NextFaceInEnvelope;                  // [-] - next face in the envelope run (unassigned = last)
    BoundaryVector PlaneNormal;                         // [-]  - unit outward normal (Planar / Ruled faces)
    double         PlaneOffset  = 0.0;                  // [mm] - dot(PlaneNormal, any point on the face)
    FaceCategory   Category     = FaceCategory::Planar; // [-]  - which surface carries the face
    uint32_t       LoopCount    = 0u;                   // [-]  - ring count (1 = no holes)
    uint32_t       GroupTag     = 0u;                   // [-]  - caller-defined provenance (the sweep tags caps vs walls here)
};

// 📝 A connected boundary run. A watertight solid is one Outer envelope plus one Void envelope per internal cavity; an open sheet is one Sheet
//    envelope whose rim edges have a radial count of 1. ("Shell" is the usual word and is banned by the naming skill.)
struct SolidEnvelope
{
    FaceToken        FirstFace;                                // [-] - head of the face run
    EnvelopeToken    NextEnvelope;                             // [-] - next envelope on the body (unassigned = last)
    EnvelopeCategory Category  = EnvelopeCategory::Outer;      // [-] - exterior / cavity / open run
    uint32_t         FaceCount = 0u;                           // [-] - faces in the run
};

//------------------------------------------------------------------------------------------------------------------------
//                                                           ARENAS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A dense record run with generational recycling. Detaching a slot bumps its generation and pushes it onto the vacancy list, so a stale token
//    is detected on resolve rather than aliasing a recycled record. Records are never shifted, so a token's Slot is stable for its whole life.
template <typename RecordType, typename TagType>
struct BoundaryArena
{
    std::vector<RecordType> Records;                 // [-] - the dense run (never compacted; holes are recycled)
    std::vector<uint32_t>   Generations;             // [-] - parallel generation counter per slot
    std::vector<uint8_t>    LiveFlags;               // [-] - parallel liveness flag per slot
    std::vector<uint32_t>   Vacancies;               // [-] - detached slots awaiting reuse
    uint32_t                LiveCount = 0u;          // [-] - live record count
};

template <typename RecordType, typename TagType>
BoundaryToken<TagType> ArenaAttach(BoundaryArena<RecordType, TagType>& Arena)
{
    BoundaryToken<TagType> Token;
    if (!Arena.Vacancies.empty())
    {
        Token.Slot = Arena.Vacancies.back();
        Arena.Vacancies.pop_back();
        Arena.Records[Token.Slot]   = RecordType{};
        Arena.LiveFlags[Token.Slot] = 1u;
    }
    else
    {
        Token.Slot = static_cast<uint32_t>(Arena.Records.size());
        Arena.Records.push_back(RecordType{});
        Arena.Generations.push_back(0u);
        Arena.LiveFlags.push_back(1u);
    }
    Token.Generation = Arena.Generations[Token.Slot];
    ++Arena.LiveCount;
    return Token;
}

template <typename RecordType, typename TagType>
bool ArenaDetach(BoundaryArena<RecordType, TagType>& Arena, BoundaryToken<TagType> Token)
{
    if (!TokenAssigned(Token) || Token.Slot >= Arena.Records.size()) return false;
    if (Arena.LiveFlags[Token.Slot] == 0u || Arena.Generations[Token.Slot] != Token.Generation) return false;
    Arena.LiveFlags[Token.Slot] = 0u;
    ++Arena.Generations[Token.Slot];   // every surviving copy of this token is now stale
    Arena.Vacancies.push_back(Token.Slot);
    Arena.Records[Token.Slot] = RecordType{};
    --Arena.LiveCount;
    return true;
}

template <typename RecordType, typename TagType>
RecordType* ArenaResolve(BoundaryArena<RecordType, TagType>& Arena, BoundaryToken<TagType> Token)
{
    if (!TokenAssigned(Token) || Token.Slot >= Arena.Records.size()) return nullptr;
    if (Arena.LiveFlags[Token.Slot] == 0u || Arena.Generations[Token.Slot] != Token.Generation) return nullptr;
    return &Arena.Records[Token.Slot];
}

template <typename RecordType, typename TagType>
const RecordType* ArenaResolve(const BoundaryArena<RecordType, TagType>& Arena, BoundaryToken<TagType> Token)
{
    if (!TokenAssigned(Token) || Token.Slot >= Arena.Records.size()) return nullptr;
    if (Arena.LiveFlags[Token.Slot] == 0u || Arena.Generations[Token.Slot] != Token.Generation) return nullptr;
    return &Arena.Records[Token.Slot];
}

template <typename RecordType, typename TagType>
BoundaryToken<TagType> ArenaTokenAt(const BoundaryArena<RecordType, TagType>& Arena, uint32_t Slot)
{
    BoundaryToken<TagType> Token;
    if (Slot < Arena.Records.size() && Arena.LiveFlags[Slot] != 0u)
    {
        Token.Slot       = Slot;
        Token.Generation = Arena.Generations[Slot];
    }
    return Token;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE BODY
//------------------------------------------------------------------------------------------------------------------------

// 📝 One authored solid / sheet: six arenas plus the two acceleration maps the builder needs. EdgeLookup keys an edge by its two vertex slots so a
//    face attached later STITCHES onto an edge an earlier face already created (this is what makes the walls of an extrude actually share edges
//    with the caps rather than sit as loose coincident quads). WeldGrid buckets vertices by a quantized cell so a coincident position resolves to
//    the existing vertex instead of spawning a duplicate.
//
//    🔴 Revision bumps on EVERY structural or positional change. A consumer (the tessellator, the GPU upload) caches against it and re-derives only
//       when it moves — the same contract the sketch store's body revision uses, and for the same reason: an adaptive re-tessellation hashed on its
//       own output jitters every frame and provokes a re-upload storm.
struct FullBrepBody
{
    BoundaryArena<SolidVertex,   BoundaryTag::Vertex>   Vertices;
    BoundaryArena<SolidEdge,     BoundaryTag::Edge>     Edges;
    BoundaryArena<CoEdge,        BoundaryTag::CoEdge>   CoEdges;
    BoundaryArena<FaceLoop,      BoundaryTag::Loop>     Loops;
    BoundaryArena<SolidFace,     BoundaryTag::Face>     Faces;
    BoundaryArena<SolidEnvelope, BoundaryTag::Envelope> Envelopes;

    EnvelopeToken FirstEnvelope;                                  // [-] - head of the envelope run
    uint32_t      EnvelopeCount = 0u;                             // [-] - envelopes on the body

    std::unordered_map<uint64_t, EdgeToken>                 EdgeLookup;   // [-] - (minSlot,maxSlot) -> edge, for stitching
    std::unordered_map<uint64_t, std::vector<VertexToken>>  WeldGrid;     // [-] - quantized cell -> vertices, for welding
    double                                                  WeldTolerance = 1.0e-4;   // [mm] - coincidence radius

    std::string Title;                                            // [-] - display title ("Extrude 1")
    uint32_t    Revision = 0u;                                    // [-] - bumped on every change; consumers cache against it
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIFECYCLE + BUILDER
//------------------------------------------------------------------------------------------------------------------------

// Reset Body to an empty solid carrying Title and Tolerance (pass <= 0 to keep the default 1e-4 mm weld radius). Safe to call on a used body.
void InitializeBrepBody(FullBrepBody& Body, const char* Title, double WeldToleranceMm);

// Release every arena + map, returning Body to the state InitializeBrepBody would leave (minus the title). Safe on an already-empty body.
void FinalizeBrepBody(FullBrepBody& Body);

// Attach a fresh vertex at Position UNCONDITIONALLY (no weld test) and return its token. Use ResolveOrAttachVertex unless you specifically want a
// duplicate at a coincident position (a deliberate seam).
VertexToken AttachVertex(FullBrepBody& Body, BoundaryVector Position, uint32_t GroupTag);

// Resolve the existing vertex within the body's weld tolerance of Position, attaching a fresh one only when none is close. This is what makes a
// swept ring share its corners with the cap that reuses them, so a corner drag moves the wall and the cap together.
VertexToken ResolveOrAttachVertex(FullBrepBody& Body, BoundaryVector Position, uint32_t GroupTag);

// Attach an empty envelope and link it into the body's run. Faces are then attached into it.
EnvelopeToken AttachEnvelope(FullBrepBody& Body, EnvelopeCategory Category);

// Attach a face bounded by OuterRing (>= 3 distinct vertex tokens, wound counter-clockwise as seen from OUTSIDE the solid) into Envelope. Creates
// the loop + one coedge per ring edge, STITCHES each onto an existing SolidEdge when one already joins that vertex pair (extending its radial ring)
// or creates the edge otherwise, then derives the face plane. Returns an unassigned token on a degenerate ring (< 3 vertices, a repeated vertex, a
// stale token, a stale envelope, OR a ring whose plane will not solve — a zero-normal face is unwound rather than admitted) leaving Body untouched.
FaceToken AttachFace(FullBrepBody&                   Body,
                     EnvelopeToken                   Envelope,
                     const std::vector<VertexToken>& OuterRing,
                     FaceCategory                    Category,
                     uint32_t                        GroupTag);

// Attach a hole ring to an existing face: same stitching as AttachFace, wound OPPOSITE the outer ring so the punched region reads as a void under
// the winding rule. Returns an unassigned token (Body untouched) on a degenerate ring or a stale face.
LoopToken AttachInnerLoop(FullBrepBody& Body, FaceToken Face, const std::vector<VertexToken>& HoleRing);

// Re-derive Face's plane from its outer ring by Newell's method (robust to collinear leading triples and to a slightly non-planar ring, unlike a
// three-point cross product). Called automatically by AttachFace and by every verb that moves a vertex; exposed for a caller that edits positions
// directly. Returns false on a stale face or a degenerate ring.
bool EnforceFacePlane(FullBrepBody& Body, FaceToken Face);

//------------------------------------------------------------------------------------------------------------------------
//                                                   ADJACENCY RESOLUTION
//------------------------------------------------------------------------------------------------------------------------
// 📝 Every resolver clears its output first and returns false on a stale token, so a caller can chain them without guarding each step. The ring
//    walks are guarded against a corrupt cycle by a step budget, so a structural bug degrades into a false rather than a hang.

bool ResolveLoopRing      (const FullBrepBody& Body, LoopToken     Loop,     std::vector<CoEdgeToken>& OutRing);
bool ResolveLoopVertices  (const FullBrepBody& Body, LoopToken     Loop,     std::vector<VertexToken>& OutVertices);
bool ResolveLoopPositions (const FullBrepBody& Body, LoopToken     Loop,     std::vector<BoundaryVector>& OutPositions);
bool ResolveFaceLoops     (const FullBrepBody& Body, FaceToken     Face,     std::vector<LoopToken>&   OutLoops);
bool ResolveEnvelopeFaces (const FullBrepBody& Body, EnvelopeToken Envelope, std::vector<FaceToken>&   OutFaces);
bool ResolveRadialRing    (const FullBrepBody& Body, EdgeToken     Edge,     std::vector<CoEdgeToken>& OutRing);
void ResolveBodyEnvelopes (const FullBrepBody& Body,                          std::vector<EnvelopeToken>& OutEnvelopes);

// Every LIVE token of one arena, in slot order — the walk a tessellator / picker iterates. Cheap linear scans over the dense runs.
void ResolveLiveVertices(const FullBrepBody& Body, std::vector<VertexToken>& OutVertices);
void ResolveLiveEdges   (const FullBrepBody& Body, std::vector<EdgeToken>&   OutEdges);
void ResolveLiveFaces   (const FullBrepBody& Body, std::vector<FaceToken>&   OutFaces);

// The two endpoints of a coedge FOLLOWING ITS LOOP DIRECTION (which is the edge's own A -> B only when SenseReversed is false). False on a stale token.
bool ResolveCoEdgeEndpoints(const FullBrepBody& Body, CoEdgeToken CoEdge, VertexToken& OutOrigin, VertexToken& OutTerminus);

// The face on the far side of Edge from CoEdge — the manifold neighbour. Unassigned when the edge is a rim (radial 1) or non-manifold (3+), which
// the caller must treat as "there is no single neighbour" rather than as an error.
FaceToken ResolveOppositeFace(const FullBrepBody& Body, CoEdgeToken CoEdge);

// A vertex's position (origin on a stale token — check with TokenAssigned first when that distinction matters).
BoundaryVector ResolveVertexPosition(const FullBrepBody& Body, VertexToken Vertex);

// Every face touching Vertex, deduplicated — the set a corner drag must re-plane. False on a stale token.
bool ResolveVertexFaces(const FullBrepBody& Body, VertexToken Vertex, std::vector<FaceToken>& OutFaces);

//------------------------------------------------------------------------------------------------------------------------
//                                                          EDITING
//------------------------------------------------------------------------------------------------------------------------
// 📝 These are the verbs the viewport's selection + drag call. Each keeps the topology intact and bumps Revision so the derived streams re-build.

// Move a corner to Position: writes the one field, re-buckets the weld grid, re-planes every touching face, bumps Revision. This IS "drag a corner
// point" — every incident edge and face follows because none of them stored a copy of it. False on a stale token.
bool EnforceVertexPosition(FullBrepBody& Body, VertexToken Vertex, BoundaryVector Position);

// Translate every vertex of Face by Offset (a face push-pull along an arbitrary vector; pass the face normal scaled by a distance for the usual
// push). Re-planes the face and its neighbours. False on a stale token.
bool TranslateFace(FullBrepBody& Body, FaceToken Face, BoundaryVector Offset);

// Translate a whole envelope's vertices by Offset — moving the swept body as a unit without disturbing its topology.
bool TranslateEnvelope(FullBrepBody& Body, EnvelopeToken Envelope, BoundaryVector Offset);

// Insert a vertex at Position into the middle of Edge, splitting it into two edges and splitting EVERY coedge in its radial ring (so all adjacent
// faces gain the new corner coherently — a split that only updated one side would tear the solid). Returns the new vertex, or an unassigned token on
// a stale edge. This is the primitive behind "add a corner point to an extruded wall".
VertexToken SplitEdge(FullBrepBody& Body, EdgeToken Edge, BoundaryVector Position);

// Detach a face and everything it alone owned: its loops + coedges, then any edge whose radial ring emptied, then any vertex left with no incident
// edge. Unlinks the face from its envelope run. Detaching a face from a closed envelope leaves a legal OPEN envelope (rim edges of radial 1), which
// is exactly what a "delete this wall" edit should produce. False on a stale token.
bool DetachFace(FullBrepBody& Body, FaceToken Face);

// Detach an entire envelope and every face beneath it.
bool DetachEnvelope(FullBrepBody& Body, EnvelopeToken Envelope);

//------------------------------------------------------------------------------------------------------------------------
//                                                          PICKING
//------------------------------------------------------------------------------------------------------------------------
// 📝 World-space queries the viewport's pick path calls after unprojecting the cursor. Linear scans over the live runs — correct and adequate at
//    sketch-body scale; a caller that grows past that can bucket the same walks through EngineContext/SpatialAcceleration without touching this API.

// Nearest live vertex to Probe within MaxDistance mm. Returns an unassigned token when none is close; OutDistance carries the hit distance.
VertexToken ResolveNearestVertex(const FullBrepBody& Body, BoundaryVector Probe, double MaxDistanceMm, double& OutDistance);

// Nearest live edge to Probe within MaxDistance mm (clamped point-to-segment). OutFoot receives the closest point ON the edge, OutParameter its
// normalized position along VertexA -> VertexB — the value a "split here" gesture feeds straight into SplitEdge.
EdgeToken ResolveNearestEdge(const FullBrepBody& Body, BoundaryVector Probe, double MaxDistanceMm,
                             BoundaryVector& OutFoot, double& OutParameter, double& OutDistance);

// Cast a ray at the body's PLANAR / RULED faces and return the nearest front hit. OutDistance is the ray parameter, OutPoint the world hit. Holes
// are honoured (a hit inside an inner ring is rejected), so a ray through a punched face passes to whatever is behind it. Unassigned token on a miss.
FaceToken ResolveRayFaceHit(const FullBrepBody& Body, BoundaryVector Origin, BoundaryVector Direction,
                            BoundaryVector& OutPoint, double& OutDistance);

//------------------------------------------------------------------------------------------------------------------------
//                                                        VALIDATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The audit a builder runs after an operation. SoundStatus is false only for a STRUCTURAL fault (a broken cycle, a mismatched back-link, a stale
//    reference) — the classifications below are reported as findings but are legal states, not faults: a rim edge is a sheet, a non-manifold edge is
//    a mid-boolean intermediate. Confusing "not a closed manifold solid" with "corrupt" is the classic B-rep validator mistake.
struct BoundaryValidationOutcome
{
    bool     SoundStatus = true;      // [-] - no structural fault found

    uint32_t VertexCount    = 0u;     // [-] - live V
    uint32_t EdgeCount      = 0u;     // [-] - live E
    uint32_t FaceCount      = 0u;     // [-] - live F
    uint32_t LoopCount      = 0u;     // [-] - live loops (outer + inner)
    uint32_t InnerLoopCount = 0u;     // [-] - live hole rings (the R term of the generalized Euler-Poincare relation)
    uint32_t EnvelopeCount  = 0u;     // [-] - live envelopes (the S term)
    uint32_t CoEdgeCount    = 0u;     // [-] - live coedges

    uint32_t WireEdgeCount        = 0u;   // [-] - edges with a radial ring of 0 (no face uses them)
    uint32_t RimEdgeCount         = 0u;   // [-] - edges used once (an open boundary — a sheet's rim)
    uint32_t NonManifoldEdgeCount = 0u;   // [-] - edges used 3+ times (legal; reported so a caller can decide)
    uint32_t DegenerateEdgeCount  = 0u;   // [-] - edges shorter than the weld tolerance

    bool   ClosedStatus   = false;    // [-] - every edge is used exactly twice (a watertight boundary)
    double GenusEstimate  = 0.0;      // [-] - solved from V - E + F - R = 2(S - G); non-integral flags a topology fault
    bool   EulerStatus    = true;     // [-] - the genus solved to a whole number

    std::vector<std::string> Findings;   // [-] - one reader line per fault / classification
};

// Audit Body end to end: cycle closure + back-link agreement in every loop and radial ring, coedge endpoint agreement against its edge and sense,
// loop / face / envelope ownership agreement, degenerate edges, edge-use classification, and the generalized Euler-Poincare relation. Pure read.
BoundaryValidationOutcome ValidateBrepBody(const FullBrepBody& Body);

// The SAME audit restricted to the records ONE envelope reaches. This is what an operation reports against: a body may legitimately already carry an
// older sheet or a mid-edit non-manifold envelope, and auditing the whole body would blame this operation for a fault it did not cause. Counts are
// scoped too (V / E / F / R / S), so the Euler-Poincare relation is solved over the envelope alone. Pure read.
BoundaryValidationOutcome ValidateBrepEnvelope(const FullBrepBody& Body, EnvelopeToken Envelope);

// The axis-aligned bounds of every live vertex. False (bounds untouched) on an empty body.
bool ResolveBrepBounds(const FullBrepBody& Body, BoundaryVector& OutMinimum, BoundaryVector& OutMaximum);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_PARAMETRICAUTHORING_BOUNDARY_BOUNDARYTOPOLOGY_H
