/*==============================================================================================================================================
                                                             BOUNDARYTOPOLOGY.CPP
==============================================================================================================================================*/
// 🧩 The radial-edge B-rep body. Attach / stitch / traverse / edit / audit, all over the six generational arenas declared in the header. The one
//    subtlety worth stating up front is EDGE STITCHING: AttachFace never assumes it is building a fresh edge. It keys (minVertexSlot, maxVertexSlot)
//    into Body.EdgeLookup and, on a hit, threads a new coedge into that edge's radial ring instead. That single behaviour is what turns a pile of
//    independently-built quads into a connected solid — and is why the swept walls below actually share their corners with the caps.

#include "BoundaryTopology.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       FILE-LOCAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr uint32_t RingStepBudget = 1u << 20;   // [-] - a corrupt cycle degrades into a false rather than a hang

    // 📝 The stitch key for a vertex pair — order-independent so A->B and B->A resolve to the one shared edge.
    uint64_t ComposeEdgeKey(uint32_t SlotA, uint32_t SlotB)
    {
        const uint32_t Low  = SlotA < SlotB ? SlotA : SlotB;
        const uint32_t High = SlotA < SlotB ? SlotB : SlotA;
        return (static_cast<uint64_t>(Low) << 32) | static_cast<uint64_t>(High);
    }

    // 📝 The weld cell a position falls in, quantized by the tolerance. Cells are probed with their 26 neighbours on lookup, so a position sitting
    //    a hair over a cell boundary from its twin still welds — the classic failure of a naive single-cell hash.
    int64_t QuantizeAxis(double Value, double Tolerance)
    {
        return static_cast<int64_t>(std::floor(Value / Tolerance));
    }

    uint64_t ComposeCellKey(int64_t X, int64_t Y, int64_t Z)
    {
        // 📝 A 64-bit mix of three cell ordinates. Collisions are harmless: a bucket is a candidate list the caller distance-tests anyway.
        uint64_t Hash = 1469598103934665603ull;
        auto Fold = [&Hash](int64_t Value)
        {
            uint64_t Bits = static_cast<uint64_t>(Value);
            for (int Byte = 0; Byte < 8; ++Byte)
            {
                Hash ^= (Bits & 0xFFull);
                Hash *= 1099511628211ull;
                Bits >>= 8;
            }
        };
        Fold(X); Fold(Y); Fold(Z);
        return Hash;
    }

    void AppendFinding(BoundaryValidationOutcome& Outcome, bool Fault, const char* Format, ...)
    {
        char Line[192];
        va_list Arguments;
        va_start(Arguments, Format);
        std::vsnprintf(Line, sizeof(Line), Format, Arguments);
        va_end(Arguments);
        Outcome.Findings.emplace_back(Line);
        if (Fault) Outcome.SoundStatus = false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

double EvaluateBoundaryLength(BoundaryVector A)
{
    return std::sqrt(A.XCoord * A.XCoord + A.YCoord * A.YCoord + A.ZCoord * A.ZCoord);
}

BoundaryVector NormalizeBoundaryVector(BoundaryVector A, bool& OutResolved)
{
    const double Length = EvaluateBoundaryLength(A);
    if (Length < 1.0e-12)
    {
        OutResolved = false;
        return BoundaryVector{ 0.0, 0.0, 0.0 };
    }
    OutResolved = true;
    return BoundaryVector{ A.XCoord / Length, A.YCoord / Length, A.ZCoord / Length };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIFECYCLE + BUILDER
//------------------------------------------------------------------------------------------------------------------------

void InitializeBrepBody(FullBrepBody& Body, const char* Title, double WeldToleranceMm)
{
    FinalizeBrepBody(Body);
    Body.Title         = (Title != nullptr) ? Title : "";
    Body.WeldTolerance = (WeldToleranceMm > 0.0) ? WeldToleranceMm : 1.0e-4;
    Body.Revision      = 1u;
}

void FinalizeBrepBody(FullBrepBody& Body)
{
    Body.Vertices  = BoundaryArena<SolidVertex,   BoundaryTag::Vertex>{};
    Body.Edges     = BoundaryArena<SolidEdge,     BoundaryTag::Edge>{};
    Body.CoEdges   = BoundaryArena<CoEdge,        BoundaryTag::CoEdge>{};
    Body.Loops     = BoundaryArena<FaceLoop,      BoundaryTag::Loop>{};
    Body.Faces     = BoundaryArena<SolidFace,     BoundaryTag::Face>{};
    Body.Envelopes = BoundaryArena<SolidEnvelope, BoundaryTag::Envelope>{};

    Body.FirstEnvelope = EnvelopeToken{};
    Body.EnvelopeCount = 0u;
    Body.EdgeLookup.clear();
    Body.WeldGrid.clear();
    ++Body.Revision;
}

namespace
{
    // 📝 Bucket a vertex into the weld grid so a later coincident position finds it.
    void EnrollWeldCell(FullBrepBody& Body, VertexToken Vertex, BoundaryVector Position)
    {
        const uint64_t Key = ComposeCellKey(QuantizeAxis(Position.XCoord, Body.WeldTolerance),
                                            QuantizeAxis(Position.YCoord, Body.WeldTolerance),
                                            QuantizeAxis(Position.ZCoord, Body.WeldTolerance));
        Body.WeldGrid[Key].push_back(Vertex);
    }

    // 📝 Drop a vertex from its weld bucket (a move re-buckets, a detach removes).
    void WithdrawWeldCell(FullBrepBody& Body, VertexToken Vertex, BoundaryVector Position)
    {
        const uint64_t Key = ComposeCellKey(QuantizeAxis(Position.XCoord, Body.WeldTolerance),
                                            QuantizeAxis(Position.YCoord, Body.WeldTolerance),
                                            QuantizeAxis(Position.ZCoord, Body.WeldTolerance));
        auto Bucket = Body.WeldGrid.find(Key);
        if (Bucket == Body.WeldGrid.end()) return;
        std::vector<VertexToken>& Run = Bucket->second;
        for (size_t Index = 0; Index < Run.size(); ++Index)
            if (Run[Index] == Vertex)
            {
                Run.erase(Run.begin() + static_cast<long>(Index));
                break;
            }
        if (Run.empty()) Body.WeldGrid.erase(Bucket);
    }
}

VertexToken AttachVertex(FullBrepBody& Body, BoundaryVector Position, uint32_t GroupTag)
{
    const VertexToken Token  = ArenaAttach(Body.Vertices);
    SolidVertex* const Record = ArenaResolve(Body.Vertices, Token);
    Record->Position = Position;
    Record->GroupTag = GroupTag;
    EnrollWeldCell(Body, Token, Position);
    ++Body.Revision;
    return Token;
}

VertexToken ResolveOrAttachVertex(FullBrepBody& Body, BoundaryVector Position, uint32_t GroupTag)
{
    const double ToleranceSquared = Body.WeldTolerance * Body.WeldTolerance;
    const int64_t CellX = QuantizeAxis(Position.XCoord, Body.WeldTolerance);
    const int64_t CellY = QuantizeAxis(Position.YCoord, Body.WeldTolerance);
    const int64_t CellZ = QuantizeAxis(Position.ZCoord, Body.WeldTolerance);

    // 🔴 Probe the 27-cell neighbourhood, not just the owning cell: a pair of positions a nanometre apart can straddle a cell boundary and would
    //    otherwise fail to weld, producing two coincident corners that drag apart later — the classic "my solid split along an invisible seam" bug.
    for (int64_t OffsetX = -1; OffsetX <= 1; ++OffsetX)
    for (int64_t OffsetY = -1; OffsetY <= 1; ++OffsetY)
    for (int64_t OffsetZ = -1; OffsetZ <= 1; ++OffsetZ)
    {
        const uint64_t Key = ComposeCellKey(CellX + OffsetX, CellY + OffsetY, CellZ + OffsetZ);
        auto Bucket = Body.WeldGrid.find(Key);
        if (Bucket == Body.WeldGrid.end()) continue;
        for (VertexToken Candidate : Bucket->second)
        {
            const SolidVertex* const Record = ArenaResolve(Body.Vertices, Candidate);
            if (Record == nullptr) continue;
            const BoundaryVector Gap = SubtractBoundaryVector(Record->Position, Position);
            if (DotBoundaryVector(Gap, Gap) <= ToleranceSquared)
                return Candidate;
        }
    }
    return AttachVertex(Body, Position, GroupTag);
}

EnvelopeToken AttachEnvelope(FullBrepBody& Body, EnvelopeCategory Category)
{
    const EnvelopeToken Token   = ArenaAttach(Body.Envelopes);
    SolidEnvelope* const Record = ArenaResolve(Body.Envelopes, Token);
    Record->Category     = Category;
    Record->NextEnvelope = Body.FirstEnvelope;
    Body.FirstEnvelope   = Token;
    ++Body.EnvelopeCount;
    ++Body.Revision;
    return Token;
}

namespace
{
    // 📝 Resolve the SolidEdge joining two vertices, creating it when absent. OutSenseReversed reports whether the caller's From -> To direction runs
    //    against the edge's own VertexA -> VertexB — the fact a coedge records so two faces can traverse one shared edge in opposite senses.
    EdgeToken ResolveOrAttachEdge(FullBrepBody& Body, VertexToken From, VertexToken To, bool& OutSenseReversed, uint32_t GroupTag)
    {
        const uint64_t Key = ComposeEdgeKey(From.Slot, To.Slot);
        auto Existing = Body.EdgeLookup.find(Key);
        if (Existing != Body.EdgeLookup.end())
        {
            const SolidEdge* const Record = ArenaResolve(Body.Edges, Existing->second);
            if (Record != nullptr)
            {
                OutSenseReversed = (Record->VertexA != From);
                return Existing->second;
            }
            Body.EdgeLookup.erase(Existing);   // a stale map entry from a detached edge — fall through and rebuild
        }

        const EdgeToken Token   = ArenaAttach(Body.Edges);
        SolidEdge* const Record = ArenaResolve(Body.Edges, Token);
        Record->VertexA  = From;
        Record->VertexB  = To;
        Record->GroupTag = GroupTag;
        Body.EdgeLookup.emplace(Key, Token);

        if (SolidVertex* const Origin   = ArenaResolve(Body.Vertices, From)) Origin->IncidentEdges.push_back(Token);
        if (SolidVertex* const Terminus = ArenaResolve(Body.Vertices, To))   Terminus->IncidentEdges.push_back(Token);

        OutSenseReversed = false;
        return Token;
    }

    // 📝 Thread a coedge into its edge's radial ring (a circular singly-linked list). A first use points at itself; every later use is spliced in
    //    behind the head, which keeps the operation O(1) and the ring's order irrelevant (it is a set, not a sequence).
    void EnrollRadial(FullBrepBody& Body, EdgeToken Edge, CoEdgeToken Use)
    {
        SolidEdge* const EdgeRecord = ArenaResolve(Body.Edges, Edge);
        CoEdge*    const UseRecord  = ArenaResolve(Body.CoEdges, Use);
        if (EdgeRecord == nullptr || UseRecord == nullptr) return;

        if (!TokenAssigned(EdgeRecord->RadialFirst))
        {
            EdgeRecord->RadialFirst = Use;
            UseRecord->NextRadial   = Use;
        }
        else
        {
            CoEdge* const HeadRecord = ArenaResolve(Body.CoEdges, EdgeRecord->RadialFirst);
            if (HeadRecord == nullptr) return;
            UseRecord->NextRadial  = HeadRecord->NextRadial;
            HeadRecord->NextRadial = Use;
        }
        ++EdgeRecord->RadialCount;
    }

    // 📝 Unthread a coedge from its edge's radial ring, walking to its predecessor. Rings are short (2 in the manifold case), so the walk is free.
    void WithdrawRadial(FullBrepBody& Body, EdgeToken Edge, CoEdgeToken Use)
    {
        SolidEdge* const EdgeRecord = ArenaResolve(Body.Edges, Edge);
        if (EdgeRecord == nullptr || !TokenAssigned(EdgeRecord->RadialFirst)) return;

        if (EdgeRecord->RadialCount == 1u && EdgeRecord->RadialFirst == Use)
        {
            EdgeRecord->RadialFirst = CoEdgeToken{};
            EdgeRecord->RadialCount = 0u;
            return;
        }

        CoEdgeToken Walk = EdgeRecord->RadialFirst;
        for (uint32_t Step = 0; Step < EdgeRecord->RadialCount + 1u; ++Step)
        {
            CoEdge* const WalkRecord = ArenaResolve(Body.CoEdges, Walk);
            if (WalkRecord == nullptr) return;
            if (WalkRecord->NextRadial == Use)
            {
                CoEdge* const TargetRecord = ArenaResolve(Body.CoEdges, Use);
                WalkRecord->NextRadial = (TargetRecord != nullptr) ? TargetRecord->NextRadial : Walk;
                if (EdgeRecord->RadialFirst == Use) EdgeRecord->RadialFirst = Walk;
                if (EdgeRecord->RadialCount > 0u) --EdgeRecord->RadialCount;
                return;
            }
            Walk = WalkRecord->NextRadial;
        }
    }

    // 📝 Build a closed coedge ring for Loop over Ring's vertices, stitching every span onto a shared edge. Returns false (with nothing attached)
    //    on a degenerate ring — a repeated vertex, a stale token, or fewer than three corners.
    bool AttachCoEdgeRing(FullBrepBody& Body, LoopToken Loop, const std::vector<VertexToken>& Ring, uint32_t GroupTag)
    {
        const size_t Count = Ring.size();
        if (Count < 3) return false;
        for (size_t Index = 0; Index < Count; ++Index)
        {
            if (ArenaResolve(Body.Vertices, Ring[Index]) == nullptr) return false;
            for (size_t Other = Index + 1; Other < Count; ++Other)
                if (Ring[Index] == Ring[Other]) return false;   // a ring may not revisit a corner
        }

        std::vector<CoEdgeToken> Uses;
        Uses.reserve(Count);
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const VertexToken From = Ring[Index];
            const VertexToken To   = Ring[(Index + 1) % Count];

            bool            SenseReversed = false;
            const EdgeToken Edge = ResolveOrAttachEdge(Body, From, To, SenseReversed, GroupTag);

            const CoEdgeToken Use  = ArenaAttach(Body.CoEdges);
            CoEdge* const UseRecord = ArenaResolve(Body.CoEdges, Use);
            UseRecord->Edge          = Edge;
            UseRecord->OriginVertex  = From;
            UseRecord->OwningLoop    = Loop;
            UseRecord->SenseReversed = SenseReversed;
            EnrollRadial(Body, Edge, Use);
            Uses.push_back(Use);
        }

        for (size_t Index = 0; Index < Count; ++Index)
        {
            CoEdge* const Record = ArenaResolve(Body.CoEdges, Uses[Index]);
            Record->NextInLoop     = Uses[(Index + 1) % Count];
            Record->PreviousInLoop = Uses[(Index + Count - 1) % Count];
        }

        FaceLoop* const LoopRecord = ArenaResolve(Body.Loops, Loop);
        LoopRecord->FirstCoEdge = Uses.front();
        LoopRecord->CoEdgeCount = static_cast<uint32_t>(Count);
        return true;
    }
}

FaceToken AttachFace(FullBrepBody&                   Body,
                     EnvelopeToken                   Envelope,
                     const std::vector<VertexToken>& OuterRing,
                     FaceCategory                    Category,
                     uint32_t                        GroupTag)
{
    SolidEnvelope* const EnvelopeRecord = ArenaResolve(Body.Envelopes, Envelope);
    if (EnvelopeRecord == nullptr || OuterRing.size() < 3) return FaceToken{};

    const FaceToken Face      = ArenaAttach(Body.Faces);
    const LoopToken Loop      = ArenaAttach(Body.Loops);

    FaceLoop* const LoopRecord = ArenaResolve(Body.Loops, Loop);
    LoopRecord->OwningFace = Face;
    LoopRecord->Category   = LoopCategory::Outer;

    if (!AttachCoEdgeRing(Body, Loop, OuterRing, GroupTag))
    {
        ArenaDetach(Body.Loops, Loop);
        ArenaDetach(Body.Faces, Face);
        return FaceToken{};
    }

    SolidFace* const FaceRecord = ArenaResolve(Body.Faces, Face);
    FaceRecord->FirstLoop      = Loop;
    FaceRecord->LoopCount      = 1u;
    FaceRecord->OwningEnvelope = Envelope;
    FaceRecord->Category       = Category;
    FaceRecord->GroupTag       = GroupTag;

    // Re-resolve the envelope: the arena may have reallocated while the loop / coedges were attached.
    SolidEnvelope* const LiveEnvelope = ArenaResolve(Body.Envelopes, Envelope);
    FaceRecord->NextFaceInEnvelope = LiveEnvelope->FirstFace;
    LiveEnvelope->FirstFace        = Face;
    ++LiveEnvelope->FaceCount;

    // 🔴 A face whose plane will not solve is a ZERO-NORMAL face: every downstream consumer (the tessellator's drop-axis choice, the ray hit test,
    //    the boolean's orientation classification) reads that normal and produces nonsense from it. Reject the whole attach and unwind, rather than
    //    letting a face with an undefined normal into the body where it will be diagnosed three subsystems away.
    if (!EnforceFacePlane(Body, Face))
    {
        DetachFace(Body, Face);
        return FaceToken{};
    }
    ++Body.Revision;
    return Face;
}

LoopToken AttachInnerLoop(FullBrepBody& Body, FaceToken Face, const std::vector<VertexToken>& HoleRing)
{
    SolidFace* const FaceRecord = ArenaResolve(Body.Faces, Face);
    if (FaceRecord == nullptr || HoleRing.size() < 3) return LoopToken{};

    const uint32_t  GroupTag = FaceRecord->GroupTag;
    const LoopToken Loop     = ArenaAttach(Body.Loops);

    FaceLoop* const LoopRecord = ArenaResolve(Body.Loops, Loop);
    LoopRecord->OwningFace = Face;
    LoopRecord->Category   = LoopCategory::Inner;

    if (!AttachCoEdgeRing(Body, Loop, HoleRing, GroupTag))
    {
        ArenaDetach(Body.Loops, Loop);
        return LoopToken{};
    }

    // Append at the tail so the outer ring stays first in the run (the tessellator + plane solver both assume that).
    SolidFace* const LiveFace = ArenaResolve(Body.Faces, Face);
    LoopToken Walk = LiveFace->FirstLoop;
    for (uint32_t Step = 0; Step < RingStepBudget; ++Step)
    {
        FaceLoop* const WalkRecord = ArenaResolve(Body.Loops, Walk);
        if (WalkRecord == nullptr) break;
        if (!TokenAssigned(WalkRecord->NextLoopOnFace))
        {
            WalkRecord->NextLoopOnFace = Loop;
            break;
        }
        Walk = WalkRecord->NextLoopOnFace;
    }
    ++LiveFace->LoopCount;
    ++Body.Revision;
    return Loop;
}

bool EnforceFacePlane(FullBrepBody& Body, FaceToken Face)
{
    SolidFace* const FaceRecord = ArenaResolve(Body.Faces, Face);
    if (FaceRecord == nullptr) return false;

    std::vector<BoundaryVector> Ring;
    if (!ResolveLoopPositions(Body, FaceRecord->FirstLoop, Ring) || Ring.size() < 3) return false;

    // 🔴 Newell's method, not a three-point cross product: the leading triple of a ring is routinely collinear (a filleted corner run, a subdivided
    //    wall), and a cross product there yields a zero normal and an inside-out face. Newell integrates the whole ring, so it is stable and also
    //    returns the best-fit plane for a ring that is not exactly planar.
    BoundaryVector Normal{ 0.0, 0.0, 0.0 };
    BoundaryVector Centroid{ 0.0, 0.0, 0.0 };
    const size_t Count = Ring.size();
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const BoundaryVector Current = Ring[Index];
        const BoundaryVector Next    = Ring[(Index + 1) % Count];
        Normal.XCoord += (Current.YCoord - Next.YCoord) * (Current.ZCoord + Next.ZCoord);
        Normal.YCoord += (Current.ZCoord - Next.ZCoord) * (Current.XCoord + Next.XCoord);
        Normal.ZCoord += (Current.XCoord - Next.XCoord) * (Current.YCoord + Next.YCoord);
        Centroid = AddBoundaryVector(Centroid, Current);
    }
    Centroid = ScaleBoundaryVector(Centroid, 1.0 / static_cast<double>(Count));

    bool Resolved = false;
    const BoundaryVector Unit = NormalizeBoundaryVector(Normal, Resolved);
    if (!Resolved) return false;

    SolidFace* const LiveFace = ArenaResolve(Body.Faces, Face);
    LiveFace->PlaneNormal = Unit;
    LiveFace->PlaneOffset = DotBoundaryVector(Unit, Centroid);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ADJACENCY RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

bool ResolveLoopRing(const FullBrepBody& Body, LoopToken Loop, std::vector<CoEdgeToken>& OutRing)
{
    OutRing.clear();
    const FaceLoop* const LoopRecord = ArenaResolve(Body.Loops, Loop);
    if (LoopRecord == nullptr || !TokenAssigned(LoopRecord->FirstCoEdge)) return false;

    const CoEdgeToken Head = LoopRecord->FirstCoEdge;
    CoEdgeToken       Walk = Head;
    OutRing.reserve(LoopRecord->CoEdgeCount);
    for (uint32_t Step = 0; Step < RingStepBudget; ++Step)
    {
        const CoEdge* const WalkRecord = ArenaResolve(Body.CoEdges, Walk);
        if (WalkRecord == nullptr) { OutRing.clear(); return false; }
        OutRing.push_back(Walk);
        Walk = WalkRecord->NextInLoop;
        if (Walk == Head) return true;
        if (!TokenAssigned(Walk)) { OutRing.clear(); return false; }
    }
    OutRing.clear();
    return false;
}

bool ResolveLoopVertices(const FullBrepBody& Body, LoopToken Loop, std::vector<VertexToken>& OutVertices)
{
    OutVertices.clear();
    std::vector<CoEdgeToken> Ring;
    if (!ResolveLoopRing(Body, Loop, Ring)) return false;
    OutVertices.reserve(Ring.size());
    for (CoEdgeToken Use : Ring)
    {
        const CoEdge* const Record = ArenaResolve(Body.CoEdges, Use);
        if (Record == nullptr) { OutVertices.clear(); return false; }
        OutVertices.push_back(Record->OriginVertex);
    }
    return true;
}

bool ResolveLoopPositions(const FullBrepBody& Body, LoopToken Loop, std::vector<BoundaryVector>& OutPositions)
{
    OutPositions.clear();
    std::vector<VertexToken> Ring;
    if (!ResolveLoopVertices(Body, Loop, Ring)) return false;
    OutPositions.reserve(Ring.size());
    for (VertexToken Vertex : Ring)
    {
        const SolidVertex* const Record = ArenaResolve(Body.Vertices, Vertex);
        if (Record == nullptr) { OutPositions.clear(); return false; }
        OutPositions.push_back(Record->Position);
    }
    return true;
}

bool ResolveFaceLoops(const FullBrepBody& Body, FaceToken Face, std::vector<LoopToken>& OutLoops)
{
    OutLoops.clear();
    const SolidFace* const FaceRecord = ArenaResolve(Body.Faces, Face);
    if (FaceRecord == nullptr) return false;

    LoopToken Walk = FaceRecord->FirstLoop;
    for (uint32_t Step = 0; Step < RingStepBudget && TokenAssigned(Walk); ++Step)
    {
        const FaceLoop* const Record = ArenaResolve(Body.Loops, Walk);
        if (Record == nullptr) break;
        OutLoops.push_back(Walk);
        Walk = Record->NextLoopOnFace;
    }
    return !OutLoops.empty();
}

bool ResolveEnvelopeFaces(const FullBrepBody& Body, EnvelopeToken Envelope, std::vector<FaceToken>& OutFaces)
{
    OutFaces.clear();
    const SolidEnvelope* const Record = ArenaResolve(Body.Envelopes, Envelope);
    if (Record == nullptr) return false;

    FaceToken Walk = Record->FirstFace;
    for (uint32_t Step = 0; Step < RingStepBudget && TokenAssigned(Walk); ++Step)
    {
        const SolidFace* const FaceRecord = ArenaResolve(Body.Faces, Walk);
        if (FaceRecord == nullptr) break;
        OutFaces.push_back(Walk);
        Walk = FaceRecord->NextFaceInEnvelope;
    }
    return true;
}

bool ResolveRadialRing(const FullBrepBody& Body, EdgeToken Edge, std::vector<CoEdgeToken>& OutRing)
{
    OutRing.clear();
    const SolidEdge* const EdgeRecord = ArenaResolve(Body.Edges, Edge);
    if (EdgeRecord == nullptr) return false;
    if (!TokenAssigned(EdgeRecord->RadialFirst)) return true;   // a wire edge — legally empty

    const CoEdgeToken Head = EdgeRecord->RadialFirst;
    CoEdgeToken       Walk = Head;
    for (uint32_t Step = 0; Step < RingStepBudget; ++Step)
    {
        const CoEdge* const Record = ArenaResolve(Body.CoEdges, Walk);
        if (Record == nullptr) { OutRing.clear(); return false; }
        OutRing.push_back(Walk);
        Walk = Record->NextRadial;
        if (Walk == Head) return true;
    }
    OutRing.clear();
    return false;
}

void ResolveBodyEnvelopes(const FullBrepBody& Body, std::vector<EnvelopeToken>& OutEnvelopes)
{
    OutEnvelopes.clear();
    EnvelopeToken Walk = Body.FirstEnvelope;
    for (uint32_t Step = 0; Step < RingStepBudget && TokenAssigned(Walk); ++Step)
    {
        const SolidEnvelope* const Record = ArenaResolve(Body.Envelopes, Walk);
        if (Record == nullptr) break;
        OutEnvelopes.push_back(Walk);
        Walk = Record->NextEnvelope;
    }
}

void ResolveLiveVertices(const FullBrepBody& Body, std::vector<VertexToken>& OutVertices)
{
    OutVertices.clear();
    OutVertices.reserve(Body.Vertices.LiveCount);
    for (uint32_t Slot = 0; Slot < static_cast<uint32_t>(Body.Vertices.Records.size()); ++Slot)
        if (Body.Vertices.LiveFlags[Slot] != 0u)
            OutVertices.push_back(ArenaTokenAt(Body.Vertices, Slot));
}

void ResolveLiveEdges(const FullBrepBody& Body, std::vector<EdgeToken>& OutEdges)
{
    OutEdges.clear();
    OutEdges.reserve(Body.Edges.LiveCount);
    for (uint32_t Slot = 0; Slot < static_cast<uint32_t>(Body.Edges.Records.size()); ++Slot)
        if (Body.Edges.LiveFlags[Slot] != 0u)
            OutEdges.push_back(ArenaTokenAt(Body.Edges, Slot));
}

void ResolveLiveFaces(const FullBrepBody& Body, std::vector<FaceToken>& OutFaces)
{
    OutFaces.clear();
    OutFaces.reserve(Body.Faces.LiveCount);
    for (uint32_t Slot = 0; Slot < static_cast<uint32_t>(Body.Faces.Records.size()); ++Slot)
        if (Body.Faces.LiveFlags[Slot] != 0u)
            OutFaces.push_back(ArenaTokenAt(Body.Faces, Slot));
}

bool ResolveCoEdgeEndpoints(const FullBrepBody& Body, CoEdgeToken Use, VertexToken& OutOrigin, VertexToken& OutTerminus)
{
    const CoEdge* const UseRecord = ArenaResolve(Body.CoEdges, Use);
    if (UseRecord == nullptr) return false;
    const SolidEdge* const EdgeRecord = ArenaResolve(Body.Edges, UseRecord->Edge);
    if (EdgeRecord == nullptr) return false;
    OutOrigin   = UseRecord->OriginVertex;
    OutTerminus = UseRecord->SenseReversed ? EdgeRecord->VertexA : EdgeRecord->VertexB;
    return true;
}

FaceToken ResolveOppositeFace(const FullBrepBody& Body, CoEdgeToken Use)
{
    const CoEdge* const UseRecord = ArenaResolve(Body.CoEdges, Use);
    if (UseRecord == nullptr) return FaceToken{};

    std::vector<CoEdgeToken> Radial;
    if (!ResolveRadialRing(Body, UseRecord->Edge, Radial) || Radial.size() != 2u) return FaceToken{};

    const CoEdgeToken Other = (Radial[0] == Use) ? Radial[1] : Radial[0];
    const CoEdge* const OtherRecord = ArenaResolve(Body.CoEdges, Other);
    if (OtherRecord == nullptr) return FaceToken{};
    const FaceLoop* const LoopRecord = ArenaResolve(Body.Loops, OtherRecord->OwningLoop);
    return (LoopRecord != nullptr) ? LoopRecord->OwningFace : FaceToken{};
}

BoundaryVector ResolveVertexPosition(const FullBrepBody& Body, VertexToken Vertex)
{
    const SolidVertex* const Record = ArenaResolve(Body.Vertices, Vertex);
    return (Record != nullptr) ? Record->Position : BoundaryVector{};
}

bool ResolveVertexFaces(const FullBrepBody& Body, VertexToken Vertex, std::vector<FaceToken>& OutFaces)
{
    OutFaces.clear();
    const SolidVertex* const VertexRecord = ArenaResolve(Body.Vertices, Vertex);
    if (VertexRecord == nullptr) return false;

    for (EdgeToken Edge : VertexRecord->IncidentEdges)
    {
        std::vector<CoEdgeToken> Radial;
        if (!ResolveRadialRing(Body, Edge, Radial)) continue;
        for (CoEdgeToken Use : Radial)
        {
            const CoEdge* const UseRecord = ArenaResolve(Body.CoEdges, Use);
            if (UseRecord == nullptr) continue;
            const FaceLoop* const LoopRecord = ArenaResolve(Body.Loops, UseRecord->OwningLoop);
            if (LoopRecord == nullptr) continue;
            const FaceToken Face = LoopRecord->OwningFace;
            if (std::find(OutFaces.begin(), OutFaces.end(), Face) == OutFaces.end())
                OutFaces.push_back(Face);
        }
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          EDITING
//------------------------------------------------------------------------------------------------------------------------

bool EnforceVertexPosition(FullBrepBody& Body, VertexToken Vertex, BoundaryVector Position)
{
    SolidVertex* const Record = ArenaResolve(Body.Vertices, Vertex);
    if (Record == nullptr) return false;

    const BoundaryVector Previous = Record->Position;
    WithdrawWeldCell(Body, Vertex, Previous);
    ArenaResolve(Body.Vertices, Vertex)->Position = Position;
    EnrollWeldCell(Body, Vertex, Position);

    // Every face touching the corner is re-planed: the topology never changed, only the one position it all hangs off.
    std::vector<FaceToken> Touched;
    ResolveVertexFaces(Body, Vertex, Touched);
    for (FaceToken Face : Touched) EnforceFacePlane(Body, Face);

    ++Body.Revision;
    return true;
}

bool TranslateFace(FullBrepBody& Body, FaceToken Face, BoundaryVector Offset)
{
    std::vector<LoopToken> Loops;
    if (!ResolveFaceLoops(Body, Face, Loops)) return false;

    std::vector<VertexToken> Moving;
    for (LoopToken Loop : Loops)
    {
        std::vector<VertexToken> Ring;
        ResolveLoopVertices(Body, Loop, Ring);
        for (VertexToken Vertex : Ring)
            if (std::find(Moving.begin(), Moving.end(), Vertex) == Moving.end())
                Moving.push_back(Vertex);
    }

    for (VertexToken Vertex : Moving)
        EnforceVertexPosition(Body, Vertex, AddBoundaryVector(ResolveVertexPosition(Body, Vertex), Offset));
    return !Moving.empty();
}

bool TranslateEnvelope(FullBrepBody& Body, EnvelopeToken Envelope, BoundaryVector Offset)
{
    std::vector<FaceToken> Faces;
    if (!ResolveEnvelopeFaces(Body, Envelope, Faces)) return false;

    std::vector<VertexToken> Moving;
    for (FaceToken Face : Faces)
    {
        std::vector<LoopToken> Loops;
        ResolveFaceLoops(Body, Face, Loops);
        for (LoopToken Loop : Loops)
        {
            std::vector<VertexToken> Ring;
            ResolveLoopVertices(Body, Loop, Ring);
            for (VertexToken Vertex : Ring)
                if (std::find(Moving.begin(), Moving.end(), Vertex) == Moving.end())
                    Moving.push_back(Vertex);
        }
    }

    for (VertexToken Vertex : Moving)
        EnforceVertexPosition(Body, Vertex, AddBoundaryVector(ResolveVertexPosition(Body, Vertex), Offset));
    return !Moving.empty();
}

VertexToken SplitEdge(FullBrepBody& Body, EdgeToken Edge, BoundaryVector Position)
{
    SolidEdge* const EdgeRecord = ArenaResolve(Body.Edges, Edge);
    if (EdgeRecord == nullptr) return VertexToken{};

    const VertexToken VertexA = EdgeRecord->VertexA;
    const VertexToken VertexB = EdgeRecord->VertexB;
    const uint32_t    GroupTag = EdgeRecord->GroupTag;

    std::vector<CoEdgeToken> Radial;
    if (!ResolveRadialRing(Body, Edge, Radial)) return VertexToken{};

    // The new corner, plus the trailing edge the split creates. The leading half REUSES the original edge token, so any handle a caller is holding
    // onto stays live and still names the same physical span (just shortened) — a split that invalidated both halves would break every selection.
    const VertexToken Middle = AttachVertex(Body, Position, GroupTag);
    const EdgeToken   Trailing = ArenaAttach(Body.Edges);

    {
        SolidEdge* const TrailingRecord = ArenaResolve(Body.Edges, Trailing);
        TrailingRecord->VertexA  = Middle;
        TrailingRecord->VertexB  = VertexB;
        TrailingRecord->GroupTag = GroupTag;
    }
    {
        SolidEdge* const LeadingRecord = ArenaResolve(Body.Edges, Edge);
        LeadingRecord->VertexB     = Middle;
        LeadingRecord->RadialFirst = CoEdgeToken{};
        LeadingRecord->RadialCount = 0u;
    }

    Body.EdgeLookup.erase(ComposeEdgeKey(VertexA.Slot, VertexB.Slot));
    Body.EdgeLookup[ComposeEdgeKey(VertexA.Slot, Middle.Slot)]  = Edge;
    Body.EdgeLookup[ComposeEdgeKey(Middle.Slot, VertexB.Slot)]  = Trailing;

    if (SolidVertex* const TerminusRecord = ArenaResolve(Body.Vertices, VertexB))
    {
        auto IncidenceSlot = std::find(TerminusRecord->IncidentEdges.begin(), TerminusRecord->IncidentEdges.end(), Edge);
        if (IncidenceSlot != TerminusRecord->IncidentEdges.end()) *IncidenceSlot = Trailing;
    }
    if (SolidVertex* const MiddleRecord = ArenaResolve(Body.Vertices, Middle))
    {
        MiddleRecord->IncidentEdges.push_back(Edge);
        MiddleRecord->IncidentEdges.push_back(Trailing);
    }

    // 🔴 EVERY use of the old edge is split, not just one: a shared edge is used by two (or more) faces, and updating one side only would leave the
    //    neighbour walking a span that no longer exists — the solid tears along that edge on the next traversal.
    for (CoEdgeToken Use : Radial)
    {
        CoEdge* const UseRecord = ArenaResolve(Body.CoEdges, Use);
        if (UseRecord == nullptr) continue;
        const bool      Reversed  = UseRecord->SenseReversed;
        const LoopToken OwningLoop = UseRecord->OwningLoop;

        const CoEdgeToken Inserted = ArenaAttach(Body.CoEdges);
        CoEdge* const LeadingUse  = ArenaResolve(Body.CoEdges, Use);
        CoEdge* const InsertedUse = ArenaResolve(Body.CoEdges, Inserted);

        InsertedUse->OwningLoop    = OwningLoop;
        InsertedUse->SenseReversed = Reversed;

        if (!Reversed)
        {
            // The use runs A -> B, so it becomes A -> M followed by M -> B.
            LeadingUse->Edge          = Edge;
            LeadingUse->OriginVertex  = VertexA;
            InsertedUse->Edge         = Trailing;
            InsertedUse->OriginVertex = Middle;
        }
        else
        {
            // The use runs B -> A, so it becomes B -> M followed by M -> A.
            LeadingUse->Edge          = Trailing;
            LeadingUse->OriginVertex  = VertexB;
            InsertedUse->Edge         = Edge;
            InsertedUse->OriginVertex = Middle;
        }

        // Splice the new use directly after the existing one in the loop ring.
        const CoEdgeToken Following = LeadingUse->NextInLoop;
        LeadingUse->NextInLoop      = Inserted;
        InsertedUse->PreviousInLoop = Use;
        InsertedUse->NextInLoop     = Following;
        if (CoEdge* const FollowingRecord = ArenaResolve(Body.CoEdges, Following))
            FollowingRecord->PreviousInLoop = Inserted;

        if (FaceLoop* const LoopRecord = ArenaResolve(Body.Loops, OwningLoop))
            ++LoopRecord->CoEdgeCount;

        EnrollRadial(Body, ArenaResolve(Body.CoEdges, Use)->Edge,      Use);
        EnrollRadial(Body, ArenaResolve(Body.CoEdges, Inserted)->Edge, Inserted);
    }

    ++Body.Revision;
    return Middle;
}

bool DetachFace(FullBrepBody& Body, FaceToken Face)
{
    const SolidFace* const Probe = ArenaResolve(Body.Faces, Face);
    if (Probe == nullptr) return false;
    const EnvelopeToken Envelope = Probe->OwningEnvelope;

    std::vector<LoopToken> Loops;
    ResolveFaceLoops(Body, Face, Loops);

    std::vector<EdgeToken>   TouchedEdges;
    std::vector<VertexToken> TouchedVertices;

    for (LoopToken Loop : Loops)
    {
        std::vector<CoEdgeToken> Ring;
        ResolveLoopRing(Body, Loop, Ring);
        for (CoEdgeToken Use : Ring)
        {
            CoEdge* const UseRecord = ArenaResolve(Body.CoEdges, Use);
            if (UseRecord == nullptr) continue;
            const EdgeToken   Edge   = UseRecord->Edge;
            const VertexToken Origin = UseRecord->OriginVertex;
            if (std::find(TouchedEdges.begin(), TouchedEdges.end(), Edge) == TouchedEdges.end())
                TouchedEdges.push_back(Edge);
            if (std::find(TouchedVertices.begin(), TouchedVertices.end(), Origin) == TouchedVertices.end())
                TouchedVertices.push_back(Origin);
            WithdrawRadial(Body, Edge, Use);
            ArenaDetach(Body.CoEdges, Use);
        }
        ArenaDetach(Body.Loops, Loop);
    }

    // An edge whose radial ring emptied is now a wire with no purpose — detach it and unstitch its lookup + incidence records.
    for (EdgeToken Edge : TouchedEdges)
    {
        const SolidEdge* const EdgeRecord = ArenaResolve(Body.Edges, Edge);
        if (EdgeRecord == nullptr || EdgeRecord->RadialCount > 0u) continue;

        const VertexToken VertexA = EdgeRecord->VertexA;
        const VertexToken VertexB = EdgeRecord->VertexB;
        Body.EdgeLookup.erase(ComposeEdgeKey(VertexA.Slot, VertexB.Slot));

        for (VertexToken Endpoint : { VertexA, VertexB })
            if (SolidVertex* const Record = ArenaResolve(Body.Vertices, Endpoint))
            {
                auto Found = std::find(Record->IncidentEdges.begin(), Record->IncidentEdges.end(), Edge);
                if (Found != Record->IncidentEdges.end()) Record->IncidentEdges.erase(Found);
            }
        ArenaDetach(Body.Edges, Edge);
    }

    // A corner with nothing left to bound is detached too, so a body never accumulates orphan points.
    for (VertexToken Vertex : TouchedVertices)
    {
        const SolidVertex* const Record = ArenaResolve(Body.Vertices, Vertex);
        if (Record == nullptr || !Record->IncidentEdges.empty()) continue;
        WithdrawWeldCell(Body, Vertex, Record->Position);
        ArenaDetach(Body.Vertices, Vertex);
    }

    // Unlink from the envelope's face run.
    if (SolidEnvelope* const EnvelopeRecord = ArenaResolve(Body.Envelopes, Envelope))
    {
        if (EnvelopeRecord->FirstFace == Face)
        {
            const SolidFace* const FaceRecord = ArenaResolve(Body.Faces, Face);
            EnvelopeRecord->FirstFace = (FaceRecord != nullptr) ? FaceRecord->NextFaceInEnvelope : FaceToken{};
        }
        else
        {
            FaceToken Walk = EnvelopeRecord->FirstFace;
            for (uint32_t Step = 0; Step < RingStepBudget && TokenAssigned(Walk); ++Step)
            {
                SolidFace* const WalkRecord = ArenaResolve(Body.Faces, Walk);
                if (WalkRecord == nullptr) break;
                if (WalkRecord->NextFaceInEnvelope == Face)
                {
                    const SolidFace* const FaceRecord = ArenaResolve(Body.Faces, Face);
                    WalkRecord->NextFaceInEnvelope = (FaceRecord != nullptr) ? FaceRecord->NextFaceInEnvelope : FaceToken{};
                    break;
                }
                Walk = WalkRecord->NextFaceInEnvelope;
            }
        }
        if (EnvelopeRecord->FaceCount > 0u) --EnvelopeRecord->FaceCount;
    }

    ArenaDetach(Body.Faces, Face);
    ++Body.Revision;
    return true;
}

bool DetachEnvelope(FullBrepBody& Body, EnvelopeToken Envelope)
{
    std::vector<FaceToken> Faces;
    if (!ResolveEnvelopeFaces(Body, Envelope, Faces)) return false;
    for (FaceToken Face : Faces) DetachFace(Body, Face);

    if (Body.FirstEnvelope == Envelope)
    {
        const SolidEnvelope* const Record = ArenaResolve(Body.Envelopes, Envelope);
        Body.FirstEnvelope = (Record != nullptr) ? Record->NextEnvelope : EnvelopeToken{};
    }
    else
    {
        EnvelopeToken Walk = Body.FirstEnvelope;
        for (uint32_t Step = 0; Step < RingStepBudget && TokenAssigned(Walk); ++Step)
        {
            SolidEnvelope* const WalkRecord = ArenaResolve(Body.Envelopes, Walk);
            if (WalkRecord == nullptr) break;
            if (WalkRecord->NextEnvelope == Envelope)
            {
                const SolidEnvelope* const Record = ArenaResolve(Body.Envelopes, Envelope);
                WalkRecord->NextEnvelope = (Record != nullptr) ? Record->NextEnvelope : EnvelopeToken{};
                break;
            }
            Walk = WalkRecord->NextEnvelope;
        }
    }

    if (ArenaDetach(Body.Envelopes, Envelope) && Body.EnvelopeCount > 0u) --Body.EnvelopeCount;
    ++Body.Revision;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          PICKING
//------------------------------------------------------------------------------------------------------------------------

VertexToken ResolveNearestVertex(const FullBrepBody& Body, BoundaryVector Probe, double MaxDistanceMm, double& OutDistance)
{
    VertexToken Nearest;
    double      NearestSquared = MaxDistanceMm * MaxDistanceMm;
    OutDistance = 0.0;

    for (uint32_t Slot = 0; Slot < static_cast<uint32_t>(Body.Vertices.Records.size()); ++Slot)
    {
        if (Body.Vertices.LiveFlags[Slot] == 0u) continue;
        const BoundaryVector Gap = SubtractBoundaryVector(Body.Vertices.Records[Slot].Position, Probe);
        const double Squared = DotBoundaryVector(Gap, Gap);
        if (Squared <= NearestSquared)
        {
            NearestSquared = Squared;
            Nearest        = ArenaTokenAt(Body.Vertices, Slot);
            OutDistance    = std::sqrt(Squared);
        }
    }
    return Nearest;
}

EdgeToken ResolveNearestEdge(const FullBrepBody& Body, BoundaryVector Probe, double MaxDistanceMm,
                             BoundaryVector& OutFoot, double& OutParameter, double& OutDistance)
{
    EdgeToken Nearest;
    double    NearestSquared = MaxDistanceMm * MaxDistanceMm;
    OutFoot      = BoundaryVector{};
    OutParameter = 0.0;
    OutDistance  = 0.0;

    for (uint32_t Slot = 0; Slot < static_cast<uint32_t>(Body.Edges.Records.size()); ++Slot)
    {
        if (Body.Edges.LiveFlags[Slot] == 0u) continue;
        const SolidEdge& Record = Body.Edges.Records[Slot];
        const BoundaryVector From = ResolveVertexPosition(Body, Record.VertexA);
        const BoundaryVector To   = ResolveVertexPosition(Body, Record.VertexB);
        const BoundaryVector Span = SubtractBoundaryVector(To, From);
        const double SpanSquared  = DotBoundaryVector(Span, Span);

        double Parameter = 0.0;
        if (SpanSquared > 1.0e-18)
            Parameter = DotBoundaryVector(SubtractBoundaryVector(Probe, From), Span) / SpanSquared;
        Parameter = (Parameter < 0.0) ? 0.0 : ((Parameter > 1.0) ? 1.0 : Parameter);

        const BoundaryVector Foot = AddBoundaryVector(From, ScaleBoundaryVector(Span, Parameter));
        const BoundaryVector Gap  = SubtractBoundaryVector(Probe, Foot);
        const double Squared = DotBoundaryVector(Gap, Gap);
        if (Squared <= NearestSquared)
        {
            NearestSquared = Squared;
            Nearest        = ArenaTokenAt(Body.Edges, Slot);
            OutFoot        = Foot;
            OutParameter   = Parameter;
            OutDistance    = std::sqrt(Squared);
        }
    }
    return Nearest;
}

namespace
{
    // 📝 Project a planar face's ring onto its two dominant axes and run an even-odd crossing test. Dropping the axis the normal is largest in is the
    //    standard way to avoid a degenerate projection (an edge-on face collapsing to a line).
    bool PointInsideProjectedRing(const std::vector<BoundaryVector>& Ring, BoundaryVector Probe, int DropAxis)
    {
        auto AxisU = [DropAxis](BoundaryVector Value) -> double
        {
            return (DropAxis == 0) ? Value.YCoord : Value.XCoord;
        };
        auto AxisV = [DropAxis](BoundaryVector Value) -> double
        {
            return (DropAxis == 2) ? Value.YCoord : Value.ZCoord;
        };

        const double ProbeU = AxisU(Probe);
        const double ProbeV = AxisV(Probe);
        bool Inside = false;
        const size_t Count = Ring.size();
        for (size_t Index = 0, Previous = Count - 1; Index < Count; Previous = Index++)
        {
            const double CurrentU = AxisU(Ring[Index]),    CurrentV = AxisV(Ring[Index]);
            const double PreviousU = AxisU(Ring[Previous]), PreviousV = AxisV(Ring[Previous]);
            if (((CurrentV > ProbeV) != (PreviousV > ProbeV)) &&
                (ProbeU < (PreviousU - CurrentU) * (ProbeV - CurrentV) / (PreviousV - CurrentV) + CurrentU))
                Inside = !Inside;
        }
        return Inside;
    }
}

FaceToken ResolveRayFaceHit(const FullBrepBody& Body, BoundaryVector Origin, BoundaryVector Direction,
                            BoundaryVector& OutPoint, double& OutDistance)
{
    FaceToken Nearest;
    double    NearestParameter = 1.0e30;
    OutPoint    = BoundaryVector{};
    OutDistance = 0.0;

    std::vector<FaceToken> Faces;
    ResolveLiveFaces(Body, Faces);
    for (FaceToken Face : Faces)
    {
        const SolidFace* const Record = ArenaResolve(Body.Faces, Face);
        if (Record == nullptr) continue;
        if (Record->Category != FaceCategory::Planar && Record->Category != FaceCategory::Ruled) continue;

        const double Denominator = DotBoundaryVector(Record->PlaneNormal, Direction);
        if (std::fabs(Denominator) < 1.0e-12) continue;   // the ray runs parallel to the plane

        const double Parameter = (Record->PlaneOffset - DotBoundaryVector(Record->PlaneNormal, Origin)) / Denominator;
        if (Parameter <= 1.0e-6 || Parameter >= NearestParameter) continue;

        const BoundaryVector Hit = AddBoundaryVector(Origin, ScaleBoundaryVector(Direction, Parameter));

        const double AbsoluteX = std::fabs(Record->PlaneNormal.XCoord);
        const double AbsoluteY = std::fabs(Record->PlaneNormal.YCoord);
        const double AbsoluteZ = std::fabs(Record->PlaneNormal.ZCoord);
        const int DropAxis = (AbsoluteX >= AbsoluteY && AbsoluteX >= AbsoluteZ) ? 0 : ((AbsoluteY >= AbsoluteZ) ? 1 : 2);

        std::vector<LoopToken> Loops;
        if (!ResolveFaceLoops(Body, Face, Loops)) continue;

        std::vector<BoundaryVector> Ring;
        if (!ResolveLoopPositions(Body, Loops.front(), Ring) || Ring.size() < 3) continue;
        if (!PointInsideProjectedRing(Ring, Hit, DropAxis)) continue;

        // A hit inside a hole ring passes THROUGH the face, so the ray must continue to whatever lies behind it.
        bool InsideHole = false;
        for (size_t Index = 1; Index < Loops.size() && !InsideHole; ++Index)
        {
            std::vector<BoundaryVector> HoleRing;
            if (ResolveLoopPositions(Body, Loops[Index], HoleRing) && HoleRing.size() >= 3)
                InsideHole = PointInsideProjectedRing(HoleRing, Hit, DropAxis);
        }
        if (InsideHole) continue;

        NearestParameter = Parameter;
        Nearest          = Face;
        OutPoint         = Hit;
        OutDistance      = Parameter;
    }
    return Nearest;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        VALIDATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The record sets ONE audit covers. ValidateBrepBody fills these from the live runs; ValidateBrepEnvelope fills them by walking one envelope,
    //    so the same checks answer "is this body sound" and "is the envelope I just built sound" without a second copy of the logic.
    struct AuditScope
    {
        std::vector<CoEdgeToken>   CoEdges;
        std::vector<LoopToken>     Loops;
        std::vector<EdgeToken>     Edges;
        uint32_t                   VertexCount   = 0u;
        uint32_t                   FaceCount     = 0u;
        uint32_t                   EnvelopeCount = 0u;
    };

    // 📝 Every record the audit below inspects, gathered so the checks read one scope rather than the arenas directly.
    void AuditScopeFromEnvelope(const FullBrepBody& Body, EnvelopeToken Envelope, AuditScope& OutScope)
    {
        std::vector<FaceToken> Faces;
        if (!ResolveEnvelopeFaces(Body, Envelope, Faces)) return;
        OutScope.FaceCount     = static_cast<uint32_t>(Faces.size());
        OutScope.EnvelopeCount = 1u;

        std::vector<VertexToken> Corners;
        for (FaceToken Face : Faces)
        {
            std::vector<LoopToken> Loops;
            ResolveFaceLoops(Body, Face, Loops);
            for (LoopToken Loop : Loops)
            {
                OutScope.Loops.push_back(Loop);
                std::vector<CoEdgeToken> Ring;
                ResolveLoopRing(Body, Loop, Ring);
                for (CoEdgeToken Use : Ring)
                {
                    OutScope.CoEdges.push_back(Use);
                    const CoEdge* const UseRecord = ArenaResolve(Body.CoEdges, Use);
                    if (UseRecord == nullptr) continue;
                    if (std::find(OutScope.Edges.begin(), OutScope.Edges.end(), UseRecord->Edge) == OutScope.Edges.end())
                        OutScope.Edges.push_back(UseRecord->Edge);
                    if (std::find(Corners.begin(), Corners.end(), UseRecord->OriginVertex) == Corners.end())
                        Corners.push_back(UseRecord->OriginVertex);
                }
            }
        }
        OutScope.VertexCount = static_cast<uint32_t>(Corners.size());
    }

    BoundaryValidationOutcome AuditScopedRecords(const FullBrepBody& Body, const AuditScope& Scope);
}

BoundaryValidationOutcome ValidateBrepBody(const FullBrepBody& Body)
{
    AuditScope Scope;
    Scope.VertexCount   = Body.Vertices.LiveCount;
    Scope.FaceCount     = Body.Faces.LiveCount;
    Scope.EnvelopeCount = Body.Envelopes.LiveCount;
    for (uint32_t Slot = 0; Slot < static_cast<uint32_t>(Body.CoEdges.Records.size()); ++Slot)
        if (Body.CoEdges.LiveFlags[Slot] != 0u) Scope.CoEdges.push_back(ArenaTokenAt(Body.CoEdges, Slot));
    for (uint32_t Slot = 0; Slot < static_cast<uint32_t>(Body.Loops.Records.size()); ++Slot)
        if (Body.Loops.LiveFlags[Slot] != 0u) Scope.Loops.push_back(ArenaTokenAt(Body.Loops, Slot));
    for (uint32_t Slot = 0; Slot < static_cast<uint32_t>(Body.Edges.Records.size()); ++Slot)
        if (Body.Edges.LiveFlags[Slot] != 0u) Scope.Edges.push_back(ArenaTokenAt(Body.Edges, Slot));
    return AuditScopedRecords(Body, Scope);
}

BoundaryValidationOutcome ValidateBrepEnvelope(const FullBrepBody& Body, EnvelopeToken Envelope)
{
    AuditScope Scope;
    AuditScopeFromEnvelope(Body, Envelope, Scope);
    return AuditScopedRecords(Body, Scope);
}

namespace
{

BoundaryValidationOutcome AuditScopedRecords(const FullBrepBody& Body, const AuditScope& Scope)
{
    BoundaryValidationOutcome Outcome;
    Outcome.VertexCount   = Scope.VertexCount;
    Outcome.EdgeCount     = static_cast<uint32_t>(Scope.Edges.size());
    Outcome.FaceCount     = Scope.FaceCount;
    Outcome.LoopCount     = static_cast<uint32_t>(Scope.Loops.size());
    Outcome.CoEdgeCount   = static_cast<uint32_t>(Scope.CoEdges.size());
    Outcome.EnvelopeCount = Scope.EnvelopeCount;

    // ── Coedge integrity: mutual loop links, a live edge, and an origin that agrees with the recorded sense. ─────────────
    for (CoEdgeToken Use : Scope.CoEdges)
    {
        const uint32_t Slot = Use.Slot;
        const CoEdge* const Probe = ArenaResolve(Body.CoEdges, Use);
        if (Probe == nullptr)
        {
            AppendFinding(Outcome, true, "coedge %u: detached mid-audit", Slot);
            continue;
        }
        const CoEdge& Record = *Probe;

        const CoEdge* const Following = ArenaResolve(Body.CoEdges, Record.NextInLoop);
        const CoEdge* const Preceding = ArenaResolve(Body.CoEdges, Record.PreviousInLoop);
        if (Following == nullptr || Preceding == nullptr)
        {
            AppendFinding(Outcome, true, "coedge %u: loop link resolves to nothing", Slot);
            continue;
        }
        if (!(Following->PreviousInLoop == Use) || !(Preceding->NextInLoop == Use))
            AppendFinding(Outcome, true, "coedge %u: loop links are not mutual", Slot);

        const SolidEdge* const EdgeRecord = ArenaResolve(Body.Edges, Record.Edge);
        if (EdgeRecord == nullptr)
        {
            AppendFinding(Outcome, true, "coedge %u: references a detached edge", Slot);
            continue;
        }
        const VertexToken ExpectedOrigin = Record.SenseReversed ? EdgeRecord->VertexB : EdgeRecord->VertexA;
        if (!(Record.OriginVertex == ExpectedOrigin))
            AppendFinding(Outcome, true, "coedge %u: origin disagrees with its edge sense", Slot);

        if (ArenaResolve(Body.Loops, Record.OwningLoop) == nullptr)
            AppendFinding(Outcome, true, "coedge %u: owning loop is detached", Slot);
    }

    // ── Loop integrity: the ring closes, its length matches, and every use points back at it. ────────────────────────────
    for (LoopToken Loop : Scope.Loops)
    {
        const uint32_t Slot = Loop.Slot;
        const FaceLoop* const LoopProbe = ArenaResolve(Body.Loops, Loop);
        if (LoopProbe == nullptr)
        {
            AppendFinding(Outcome, true, "loop %u: detached mid-audit", Slot);
            continue;
        }
        const FaceLoop& Record = *LoopProbe;
        if (Record.Category == LoopCategory::Inner) ++Outcome.InnerLoopCount;

        std::vector<CoEdgeToken> Ring;
        if (!ResolveLoopRing(Body, Loop, Ring))
        {
            AppendFinding(Outcome, true, "loop %u: ring does not close", Slot);
            continue;
        }
        if (Ring.size() != Record.CoEdgeCount)
            AppendFinding(Outcome, true, "loop %u: ring length %zu disagrees with the recorded %u",
                          Slot, Ring.size(), Record.CoEdgeCount);
        if (Ring.size() < 3)
            AppendFinding(Outcome, true, "loop %u: only %zu uses (a ring needs three)", Slot, Ring.size());
        for (CoEdgeToken Use : Ring)
        {
            const CoEdge* const UseRecord = ArenaResolve(Body.CoEdges, Use);
            if (UseRecord != nullptr && !(UseRecord->OwningLoop == Loop))
                AppendFinding(Outcome, true, "loop %u: a use claims a different owning loop", Slot);
        }
        if (ArenaResolve(Body.Faces, Record.OwningFace) == nullptr)
            AppendFinding(Outcome, true, "loop %u: owning face is detached", Slot);
    }

    // ── Edge integrity: the radial ring closes, its length matches, and every use names this edge. Classification only. ──
    for (EdgeToken Edge : Scope.Edges)
    {
        const uint32_t Slot = Edge.Slot;
        const SolidEdge* const EdgeProbe = ArenaResolve(Body.Edges, Edge);
        if (EdgeProbe == nullptr)
        {
            AppendFinding(Outcome, true, "edge %u: detached mid-audit", Slot);
            continue;
        }
        const SolidEdge& Record = *EdgeProbe;

        std::vector<CoEdgeToken> Radial;
        if (!ResolveRadialRing(Body, Edge, Radial))
        {
            AppendFinding(Outcome, true, "edge %u: radial ring does not close", Slot);
            continue;
        }
        if (Radial.size() != Record.RadialCount)
            AppendFinding(Outcome, true, "edge %u: radial length %zu disagrees with the recorded %u",
                          Slot, Radial.size(), Record.RadialCount);
        for (CoEdgeToken Use : Radial)
        {
            const CoEdge* const UseRecord = ArenaResolve(Body.CoEdges, Use);
            if (UseRecord != nullptr && !(UseRecord->Edge == Edge))
                AppendFinding(Outcome, true, "edge %u: a radial use names a different edge", Slot);
        }

        // 🔴 Classification, NOT a fault. A rim is a sheet; a 3+ ring is a legal non-manifold intermediate. Reporting these as errors is the classic
        //    validator mistake that makes a correct mid-operation body look broken.
        if      (Record.RadialCount == 0u) ++Outcome.WireEdgeCount;
        else if (Record.RadialCount == 1u) ++Outcome.RimEdgeCount;
        else if (Record.RadialCount >  2u) ++Outcome.NonManifoldEdgeCount;

        const BoundaryVector Span = SubtractBoundaryVector(ResolveVertexPosition(Body, Record.VertexB),
                                                           ResolveVertexPosition(Body, Record.VertexA));
        if (EvaluateBoundaryLength(Span) < Body.WeldTolerance) ++Outcome.DegenerateEdgeCount;
    }

    if (Outcome.WireEdgeCount > 0u)
        AppendFinding(Outcome, false, "%u wire edge(s) (used by no face)", Outcome.WireEdgeCount);
    if (Outcome.RimEdgeCount > 0u)
        AppendFinding(Outcome, false, "%u rim edge(s) — an open boundary run, not a closed solid", Outcome.RimEdgeCount);
    if (Outcome.NonManifoldEdgeCount > 0u)
        AppendFinding(Outcome, false, "%u non-manifold edge(s) (3+ faces meet along them)", Outcome.NonManifoldEdgeCount);
    if (Outcome.DegenerateEdgeCount > 0u)
        AppendFinding(Outcome, false, "%u edge(s) shorter than the weld tolerance", Outcome.DegenerateEdgeCount);

    Outcome.ClosedStatus = (Outcome.RimEdgeCount == 0u && Outcome.WireEdgeCount == 0u &&
                            Outcome.NonManifoldEdgeCount == 0u && Outcome.EdgeCount > 0u);

    // ── The generalized Euler-Poincare relation: V - E + F - R = 2(S - G). Solve for the genus; a non-integral result means a topology fault the
    //    per-record checks above did not localise, which is worth flagging even though we cannot name the offending record. ──────────────────────
    if (Outcome.EnvelopeCount > 0u)
    {
        const double Left = static_cast<double>(Outcome.VertexCount) - static_cast<double>(Outcome.EdgeCount)
                          + static_cast<double>(Outcome.FaceCount)   - static_cast<double>(Outcome.InnerLoopCount);
        Outcome.GenusEstimate = static_cast<double>(Outcome.EnvelopeCount) - (Left * 0.5);
        const double Rounded  = std::floor(Outcome.GenusEstimate + 0.5);
        Outcome.EulerStatus   = std::fabs(Outcome.GenusEstimate - Rounded) < 1.0e-6;
        if (!Outcome.EulerStatus && Outcome.ClosedStatus)
            AppendFinding(Outcome, true, "Euler-Poincare solved a non-integral genus (%.3f) — the boundary is inconsistent",
                          Outcome.GenusEstimate);
    }

    return Outcome;
}

} // anonymous namespace

bool ResolveBrepBounds(const FullBrepBody& Body, BoundaryVector& OutMinimum, BoundaryVector& OutMaximum)
{
    bool Seeded = false;
    for (uint32_t Slot = 0; Slot < static_cast<uint32_t>(Body.Vertices.Records.size()); ++Slot)
    {
        if (Body.Vertices.LiveFlags[Slot] == 0u) continue;
        const BoundaryVector Position = Body.Vertices.Records[Slot].Position;
        if (!Seeded)
        {
            OutMinimum = Position;
            OutMaximum = Position;
            Seeded     = true;
            continue;
        }
        OutMinimum.XCoord = (std::min)(OutMinimum.XCoord, Position.XCoord);
        OutMinimum.YCoord = (std::min)(OutMinimum.YCoord, Position.YCoord);
        OutMinimum.ZCoord = (std::min)(OutMinimum.ZCoord, Position.ZCoord);
        OutMaximum.XCoord = (std::max)(OutMaximum.XCoord, Position.XCoord);
        OutMaximum.YCoord = (std::max)(OutMaximum.YCoord, Position.YCoord);
        OutMaximum.ZCoord = (std::max)(OutMaximum.ZCoord, Position.ZCoord);
    }
    return Seeded;
}

} // namespace Frontier
