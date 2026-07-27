/*============================================================================================================================================
                                                              POLYGONBEVEL.CPP
============================================================================================================================================*/
// 🧩 The vertex-centric bevel kernel body. BevelPolygonSelection extracts the base flat geometry, builds a private derived topology
//    (faces as corner runs, per-face normal + centroid, per-vertex incident corners, per-edge origin refs — the BevVert / EdgeHalf
//    analog, no half-edge built), places one boundary vertex per corner arc where the inward edge-offset lines meet, shrinks the
//    original faces to those boundary vertices, and bridges each beveled edge with a swept strip: one chamfer quad at Segments == 1,
//    Segments bands on a superellipse arc above. Offset points that fall within the weld threshold merge (auto-merge); every width is
//    clamped to the per-component safe limit so the bevel never self-crosses. The result rebuilds Target's PolygonCluster in place.

#include "PolygonBevel.h"

#include "PolygonCluster.h"
#include "VertexField.h"
#include "AdjacencyIndex.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr double GeometryEpsilon = 1.0e-9;

// 📝 A parametric line: a point on it and a unit direction. Two inward edge-offset lines meet at a boundary vertex.
struct ParametricLine
{
    Vector3d Anchor    = { 0.0, 0.0, 0.0 };   // [cm] - a point on the line
    Vector3d Direction = { 0.0, 0.0, 0.0 };   // [-]  - unit direction
};

// 📝 Derived adjacency over the flat input: faces as corner runs, per-face normal + centroid, per vertex its incident corners, and
//    per undirected edge the (face, local-corner) origin references (<= 2 for a manifold edge — one per adjacent face, recording the
//    corner whose OUTGOING edge is this edge so winding is recoverable). This is the BevVert / EdgeHalf analog; no half-edge built.
struct BevelTopology
{
    const std::vector<Vector3d>*          Positions = nullptr;
    std::vector<uint32_t>                 FaceBase  = {};      // [-]  - offset of face f's corners into Corners
    std::vector<uint32_t>                 FaceCount = {};      // [-]  - corner count of face f
    std::vector<uint32_t>                 Corners   = {};      // [-]  - flat per-corner vertex indices (copy of the input stream)
    std::vector<Vector3d>                 FaceNormal   = {};   // [-]  - outward unit normal per face
    std::vector<Vector3d>                 FaceCentroid = {};   // [cm] - centroid per face
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> VertexCorners = {};   // [-] - per vertex: (face, localCorner) refs
    std::unordered_map<uint64_t, std::vector<std::pair<uint32_t, uint32_t>>> EdgeOrigins = {};   // edge key -> origin (face, local)
};

// Undirected edge key: the two endpoint indices packed low-first so an edge and its reverse share a bucket. MUST match the live
// AdjacencyIndex::EncodeEdgeKey byte-for-byte — (Low << 32) | High — or the UI's selected-edge keys never find this kernel's buckets.
uint64_t EncodeEdge(uint32_t First, uint32_t Second)
{
    const uint32_t Low  = First < Second ? First : Second;
    const uint32_t High = First < Second ? Second : First;
    return ((uint64_t)Low << 32) | (uint64_t)High;
}

// The endpoint of an undirected edge key that is not Vertex. Low endpoint lives in the HIGH 32 bits (matches EncodeEdge above).
uint32_t ResolveEdgeOpposite(uint64_t Key, uint32_t Vertex)
{
    const uint32_t Low  = (uint32_t)(Key >> 32);
    const uint32_t High = (uint32_t)(Key & 0xFFFFFFFFu);
    return Low == Vertex ? High : Low;
}

// Newell's-method outward unit normal for a polygon loop (robust for any planar / near-planar face). Mirrors the inlined normal in
// AdjacencyIndex.cpp so a face beveled here shades exactly as the adjacency cache would derive it. Falls back to +Y when degenerate.
Vector3d EvaluateFaceNormal(const std::vector<Vector3d>& Positions, const std::vector<uint32_t>& Loop)
{
    double NormalX = 0.0, NormalY = 0.0, NormalZ = 0.0;
    const uint32_t Count = (uint32_t)Loop.size();
    for (uint32_t Corner = 0; Corner < Count; ++Corner)
    {
        const Vector3d& Current = Positions[Loop[Corner]];
        const Vector3d& Next    = Positions[Loop[(Corner + 1) % Count]];
        NormalX += (Current.YCoord - Next.YCoord) * (Current.ZCoord + Next.ZCoord);
        NormalY += (Current.ZCoord - Next.ZCoord) * (Current.XCoord + Next.XCoord);
        NormalZ += (Current.XCoord - Next.XCoord) * (Current.YCoord + Next.YCoord);
    }
    const double Length = std::sqrt(NormalX * NormalX + NormalY * NormalY + NormalZ * NormalZ);
    if (Length < GeometryEpsilon) return Vector3d{ 0.0, 1.0, 0.0 };
    return Vector3d{ NormalX / Length, NormalY / Length, NormalZ / Length };
}

// Closest-approach midpoint of two lines (the meeting point of two inward offset lines). Falls back to the anchor midpoint when the
// lines are near-parallel, so a straight or reflex corner still yields a finite point rather than shooting to infinity.
Vector3d EvaluateLineMeet(const ParametricLine& Alpha, const ParametricLine& Beta)
{
    const double A = DotProduct(Alpha.Direction, Alpha.Direction);
    const double B = DotProduct(Alpha.Direction, Beta.Direction);
    const double C = DotProduct(Beta.Direction, Beta.Direction);
    const Vector3d R = SubtractVector(Alpha.Anchor, Beta.Anchor);
    const double D = DotProduct(Alpha.Direction, R);
    const double E = DotProduct(Beta.Direction, R);
    const double Denominator = A * C - B * B;
    if (std::fabs(Denominator) < 1.0e-12)
        return ScaleVector(AddVector(Alpha.Anchor, Beta.Anchor), 0.5);
    const double T = (B * E - C * D) / Denominator;
    const double S = (A * E - B * D) / Denominator;
    const Vector3d PointAlpha = AddVector(Alpha.Anchor, ScaleVector(Alpha.Direction, T));
    const Vector3d PointBeta  = AddVector(Beta.Anchor,  ScaleVector(Beta.Direction, S));
    return ScaleVector(AddVector(PointAlpha, PointBeta), 0.5);
}

// The inward-offset line at vertex V of the edge V->Opposite within face Face, displaced perpendicular into the face plane by Offset.
// The line runs parallel to the edge; its anchor is V pushed Offset toward the face interior. With Offset 0 it is the original edge
// line through V (used for the non-beveled leg of a sole-offset corner).
ParametricLine ConstructOffsetLine(const BevelTopology& Topology, uint32_t Face, uint32_t Vertex, uint32_t Opposite, double Offset)
{
    const Vector3d& Origin   = (*Topology.Positions)[Vertex];
    const Vector3d& Terminus = (*Topology.Positions)[Opposite];
    const Vector3d EdgeDirection = NormalizeVector(SubtractVector(Terminus, Origin));
    Vector3d Inward = NormalizeVector(CrossProduct(Topology.FaceNormal[Face], EdgeDirection));
    if (DotProduct(Inward, SubtractVector(Topology.FaceCentroid[Face], Origin)) < 0.0)
        Inward = ScaleVector(Inward, -1.0);
    ParametricLine Line;
    Line.Anchor    = AddVector(Origin, ScaleVector(Inward, Offset));
    Line.Direction = EdgeDirection;
    return Line;
}

// Locate the local corner of Vertex within Face (the index where Corners equals Vertex). Returns false if absent.
bool ResolveLocalCorner(const BevelTopology& Topology, uint32_t Face, uint32_t Vertex, uint32_t& OutLocal)
{
    const uint32_t Base  = Topology.FaceBase[Face];
    const uint32_t Count = Topology.FaceCount[Face];
    for (uint32_t Local = 0; Local < Count; ++Local)
        if (Topology.Corners[Base + Local] == Vertex) { OutLocal = Local; return true; }
    return false;
}

// Construct the derived adjacency from the flat arrays. Returns false (empty topology) if a face has fewer than three corners or an
// index is out of range — the same precondition the adjacency constructor enforces.
bool ConstructBevelTopology(const std::vector<Vector3d>& Positions,
                            const std::vector<uint32_t>& FaceVertexIndices,
                            const std::vector<uint32_t>& FaceVertexCounts,
                            BevelTopology&               Topology)
{
    Topology.Positions = &Positions;
    Topology.VertexCorners.assign(Positions.size(), {});

    uint32_t Cursor = 0;
    for (uint32_t Face = 0; Face < (uint32_t)FaceVertexCounts.size(); ++Face)
    {
        const uint32_t Count = FaceVertexCounts[Face];
        if (Count < 3 || Cursor + Count > (uint32_t)FaceVertexIndices.size())
            return false;

        Topology.FaceBase.push_back((uint32_t)Topology.Corners.size());
        Topology.FaceCount.push_back(Count);

        std::vector<uint32_t> CornerIndices;
        Vector3d Centroid = { 0.0, 0.0, 0.0 };
        for (uint32_t Local = 0; Local < Count; ++Local)
        {
            const uint32_t Vertex = FaceVertexIndices[Cursor + Local];
            if (Vertex >= Positions.size()) return false;
            Topology.Corners.push_back(Vertex);
            CornerIndices.push_back(Vertex);
            Topology.VertexCorners[Vertex].push_back({ Face, Local });
            Centroid = AddVector(Centroid, Positions[Vertex]);
        }
        for (uint32_t Local = 0; Local < Count; ++Local)
        {
            const uint32_t Origin   = FaceVertexIndices[Cursor + Local];
            const uint32_t Terminus = FaceVertexIndices[Cursor + (Local + 1) % Count];
            Topology.EdgeOrigins[EncodeEdge(Origin, Terminus)].push_back({ Face, Local });
        }

        Topology.FaceCentroid.push_back(ScaleVector(Centroid, 1.0 / (double)Count));
        Topology.FaceNormal.push_back(EvaluateFaceNormal(Positions, CornerIndices));
        Cursor += Count;
    }
    return !Topology.FaceCount.empty();
}

// 📝 The mutable assembly the per-vertex routines fill. WorkingPositions begins as a copy of the input (vertex i == output i); a face
//    corner defaults to its original vertex and is overridden to a boundary vertex when its arc is processed. Faces are gathered as
//    index lists and resolved into the flat output (weld + drop degenerate + compact) once at the end.
struct BevelAssembly
{
    std::vector<Vector3d>              WorkingPositions = {};
    std::vector<std::vector<uint32_t>> Faces            = {};
    // [face][local] -> the output vertices replacing that corner, in winding order. Usually one (the corner moved to a boundary
    // vertex); a corner cut by a single-edge terminal bevel can expand to two (a quad corner -> pentagon).
    std::vector<std::vector<std::vector<uint32_t>>> CornerOut = {};
};

uint32_t AppendWorkingVertex(BevelAssembly& Assembly, const Vector3d& Position)
{
    Assembly.WorkingPositions.push_back(Position);
    return (uint32_t)Assembly.WorkingPositions.size() - 1;
}

// Append a face oriented so its Newell normal agrees with Reference (outward). Drops a face of fewer than three corners.
void AppendOrientedFace(BevelAssembly& Assembly, std::vector<uint32_t> Loop, const Vector3d& Reference)
{
    if (Loop.size() < 3) return;
    const Vector3d Normal = EvaluateFaceNormal(Assembly.WorkingPositions, Loop);
    if (DotProduct(Normal, Reference) < 0.0)
        std::reverse(Loop.begin(), Loop.end());
    Assembly.Faces.push_back(std::move(Loop));
}

// Order a ring of boundary vertices counter-clockwise about Centre in the plane perpendicular to Normal (for the vertex cap N-gon).
void OrderRingAngular(BevelAssembly& Assembly, const Vector3d& Centre, const Vector3d& Normal, std::vector<uint32_t>& Ring)
{
    Vector3d BasisU = std::fabs(Normal.XCoord) < 0.9 ? CrossProduct(Normal, Vector3d{ 1.0, 0.0, 0.0 })
                                                     : CrossProduct(Normal, Vector3d{ 0.0, 1.0, 0.0 });
    BasisU = NormalizeVector(BasisU);
    const Vector3d BasisW = NormalizeVector(CrossProduct(Normal, BasisU));
    std::sort(Ring.begin(), Ring.end(), [&](uint32_t Left, uint32_t Right)
    {
        const Vector3d DeltaLeft  = SubtractVector(Assembly.WorkingPositions[Left],  Centre);
        const Vector3d DeltaRight = SubtractVector(Assembly.WorkingPositions[Right], Centre);
        const double AngleLeft  = std::atan2(DotProduct(DeltaLeft,  BasisW), DotProduct(DeltaLeft,  BasisU));
        const double AngleRight = std::atan2(DotProduct(DeltaRight, BasisW), DotProduct(DeltaRight, BasisU));
        return AngleLeft < AngleRight;
    });
}

// Average of a vertex's incident face normals (its outward direction), used to orient the cap N-gon.
Vector3d EvaluateVertexNormal(const BevelTopology& Topology, uint32_t Vertex)
{
    Vector3d Normal = { 0.0, 0.0, 0.0 };
    for (const auto& Reference : Topology.VertexCorners[Vertex])
        Normal = AddVector(Normal, Topology.FaceNormal[Reference.first]);
    if (EvaluateVectorLength(Normal) < GeometryEpsilon)
        return Vector3d{ 0.0, 1.0, 0.0 };
    return NormalizeVector(Normal);
}

// 📝 Resolve a single affected vertex: classify its incident edges, group its faces into arcs cut at beveled edges, place one boundary
//    vertex per arc, override CornerOut for every face in the arc, and (when the arc-ring has >= 3 boundary vertices) cap it with an
//    N-gon. selcount 1 is the terminal case (one beveled edge): the edge's two flanks split off while the rest of the fan keeps the
//    original vertex, so beveling one edge taper-caps cleanly at each end.
void ResolveAffectedVertex(const BevelTopology& Topology, uint32_t Vertex, double Amount, BevelAssembly& Assembly,
                           const std::unordered_set<uint64_t>& BeveledEdges)
{
    const auto& Incident = Topology.VertexCorners[Vertex];
    const uint32_t FanCount = (uint32_t)Incident.size();
    if (FanCount < 2) return;

    std::unordered_map<uint64_t, bool> EdgeBeveled;
    for (const auto& Reference : Incident)
    {
        const uint32_t Face  = Reference.first;
        const uint32_t Local = Reference.second;
        const uint32_t Count = Topology.FaceCount[Face];
        const uint32_t Base  = Topology.FaceBase[Face];
        const uint32_t Prev  = Topology.Corners[Base + (Local + Count - 1) % Count];
        const uint32_t Next  = Topology.Corners[Base + (Local + 1) % Count];
        const uint64_t KeyIn  = EncodeEdge(Vertex, Prev);
        const uint64_t KeyOut = EncodeEdge(Vertex, Next);
        if (!EdgeBeveled.count(KeyIn))  EdgeBeveled[KeyIn]  = false;
        if (!EdgeBeveled.count(KeyOut)) EdgeBeveled[KeyOut] = false;
        if (BeveledEdges.count(KeyIn))  EdgeBeveled[KeyIn]  = true;
        if (BeveledEdges.count(KeyOut)) EdgeBeveled[KeyOut] = true;
    }
    uint32_t SelectionCount = 0;
    for (const auto& Entry : EdgeBeveled) if (Entry.second) ++SelectionCount;
    if (SelectionCount == 0) return;

    std::unordered_map<uint32_t, uint32_t> FanIndexByFace;
    for (uint32_t Position = 0; Position < FanCount; ++Position)
        FanIndexByFace[Incident[Position].first] = Position;

    // ---- TERMINAL: exactly one beveled edge --------------------------------------------------------------------------
    if (SelectionCount == 1)
    {
        uint64_t BeveledKey = 0;
        for (const auto& Entry : EdgeBeveled) if (Entry.second) BeveledKey = Entry.first;
        const auto BeveledOrigins = Topology.EdgeOrigins.find(BeveledKey);
        if (BeveledOrigins == Topology.EdgeOrigins.end() || BeveledOrigins->second.size() != 2) return;
        const uint32_t Opposite = ResolveEdgeOpposite(BeveledKey, Vertex);

        auto BeveledFlankPoint = [&](uint32_t Face) -> uint32_t
        {
            uint32_t Local = 0;
            ResolveLocalCorner(Topology, Face, Vertex, Local);
            const uint32_t Count = Topology.FaceCount[Face];
            const uint32_t Base  = Topology.FaceBase[Face];
            const uint32_t Prev  = Topology.Corners[Base + (Local + Count - 1) % Count];
            const uint32_t Next  = Topology.Corners[Base + (Local + 1) % Count];
            const uint32_t OtherLeg = (Next == Opposite) ? Prev : Next;
            const Vector3d Meet = EvaluateLineMeet(ConstructOffsetLine(Topology, Face, Vertex, Opposite, Amount),
                                                   ConstructOffsetLine(Topology, Face, Vertex, OtherLeg, 0.0));
            return AppendWorkingVertex(Assembly, Meet);
        };
        const uint32_t FlankFace0 = BeveledOrigins->second[0].first;
        const uint32_t FlankFace1 = BeveledOrigins->second[1].first;
        const uint32_t BeveledPoint0 = BeveledFlankPoint(FlankFace0);
        const uint32_t BeveledPoint1 = BeveledFlankPoint(FlankFace1);

        std::unordered_map<uint64_t, uint32_t> LegToFlankPoint;
        auto MapFlankLeg = [&](uint32_t Face, uint32_t FlankPoint)
        {
            uint32_t Local = 0;
            ResolveLocalCorner(Topology, Face, Vertex, Local);
            const uint32_t Count = Topology.FaceCount[Face];
            const uint32_t Base  = Topology.FaceBase[Face];
            const uint32_t Prev  = Topology.Corners[Base + (Local + Count - 1) % Count];
            const uint32_t Next  = Topology.Corners[Base + (Local + 1) % Count];
            const uint32_t OtherLeg = (Next == Opposite) ? Prev : Next;
            LegToFlankPoint[EncodeEdge(Vertex, OtherLeg)] = FlankPoint;
        };
        MapFlankLeg(FlankFace0, BeveledPoint0);
        MapFlankLeg(FlankFace1, BeveledPoint1);

        std::unordered_map<uint64_t, uint32_t> FarSlide;
        auto EdgeBoundaryOnFace = [&](uint32_t Opp, uint32_t Face) -> uint32_t
        {
            if (Opp == Opposite) return Face == FlankFace0 ? BeveledPoint0 : BeveledPoint1;
            const uint64_t Key = EncodeEdge(Vertex, Opp);
            const auto Flank = LegToFlankPoint.find(Key);
            if (Flank != LegToFlankPoint.end()) return Flank->second;
            const auto Existing = FarSlide.find(Key);
            if (Existing != FarSlide.end()) return Existing->second;
            const Vector3d Direction = NormalizeVector(SubtractVector((*Topology.Positions)[Opp], (*Topology.Positions)[Vertex]));
            const uint32_t Out = AppendWorkingVertex(Assembly,
                AddVector((*Topology.Positions)[Vertex], ScaleVector(Direction, Amount)));
            FarSlide[Key] = Out;
            return Out;
        };

        std::vector<uint32_t> Ring;
        for (const auto& Reference : Incident)
        {
            const uint32_t Face  = Reference.first;
            const uint32_t Local = Reference.second;
            const uint32_t Count = Topology.FaceCount[Face];
            const uint32_t Base  = Topology.FaceBase[Face];
            const uint32_t Prev  = Topology.Corners[Base + (Local + Count - 1) % Count];
            const uint32_t Next  = Topology.Corners[Base + (Local + 1) % Count];
            const uint32_t IncomingPoint = EdgeBoundaryOnFace(Prev, Face);
            const uint32_t OutgoingPoint = EdgeBoundaryOnFace(Next, Face);
            if (IncomingPoint == OutgoingPoint)
                Assembly.CornerOut[Face][Local] = { IncomingPoint };
            else
                Assembly.CornerOut[Face][Local] = { IncomingPoint, OutgoingPoint };
            Ring.push_back(IncomingPoint);
            Ring.push_back(OutgoingPoint);
        }

        std::sort(Ring.begin(), Ring.end());
        Ring.erase(std::unique(Ring.begin(), Ring.end()), Ring.end());
        if (Ring.size() >= 3)
        {
            const Vector3d VertexNormal = EvaluateVertexNormal(Topology, Vertex);
            OrderRingAngular(Assembly, (*Topology.Positions)[Vertex], VertexNormal, Ring);
            AppendOrientedFace(Assembly, Ring, VertexNormal);
        }
        return;
    }

    // ---- GENERAL: two or more beveled edges ----------------------------------------------------------------------------
    // Union-find over the fan so faces separated only by non-beveled edges collapse into one arc; a beveled edge cuts the arc.
    std::vector<uint32_t> RepresentativeLink(FanCount);
    for (uint32_t Position = 0; Position < FanCount; ++Position) RepresentativeLink[Position] = Position;
    std::function<uint32_t(uint32_t)> Find = [&](uint32_t Item) -> uint32_t
    {
        while (RepresentativeLink[Item] != Item) { RepresentativeLink[Item] = RepresentativeLink[RepresentativeLink[Item]]; Item = RepresentativeLink[Item]; }
        return Item;
    };
    auto MergeSets = [&](uint32_t Left, uint32_t Right) { RepresentativeLink[Find(Left)] = Find(Right); };

    for (const auto& Entry : EdgeBeveled)
    {
        if (Entry.second) continue;
        const auto OriginsIterator = Topology.EdgeOrigins.find(Entry.first);
        if (OriginsIterator == Topology.EdgeOrigins.end() || OriginsIterator->second.size() != 2) continue;
        const uint32_t FaceA = OriginsIterator->second[0].first;
        const uint32_t FaceB = OriginsIterator->second[1].first;
        if (FanIndexByFace.count(FaceA) && FanIndexByFace.count(FaceB))
            MergeSets(FanIndexByFace[FaceA], FanIndexByFace[FaceB]);
    }

    std::unordered_map<uint32_t, std::vector<std::pair<uint64_t, uint32_t>>> ArcBoundary;
    for (const auto& Entry : EdgeBeveled)
    {
        if (!Entry.second) continue;
        const auto OriginsIterator = Topology.EdgeOrigins.find(Entry.first);
        if (OriginsIterator == Topology.EdgeOrigins.end() || OriginsIterator->second.size() != 2) continue;
        const uint32_t FaceA = OriginsIterator->second[0].first;
        const uint32_t FaceB = OriginsIterator->second[1].first;
        if (!FanIndexByFace.count(FaceA) || !FanIndexByFace.count(FaceB)) continue;
        ArcBoundary[Find(FanIndexByFace[FaceA])].push_back({ Entry.first, FaceA });
        ArcBoundary[Find(FanIndexByFace[FaceB])].push_back({ Entry.first, FaceB });
    }

    std::vector<uint32_t> Ring;
    for (const auto& ArcEntry : ArcBoundary)
    {
        const uint32_t Root = ArcEntry.first;
        const auto& Bounds = ArcEntry.second;
        Vector3d BoundaryPosition;
        if (Bounds.size() >= 2)
        {
            const ParametricLine LineAlpha = ConstructOffsetLine(Topology, Bounds[0].second, Vertex,
                                                                 ResolveEdgeOpposite(Bounds[0].first, Vertex), Amount);
            const ParametricLine LineBeta  = ConstructOffsetLine(Topology, Bounds[1].second, Vertex,
                                                                 ResolveEdgeOpposite(Bounds[1].first, Vertex), Amount);
            BoundaryPosition = EvaluateLineMeet(LineAlpha, LineBeta);
        }
        else if (!Bounds.empty())
        {
            const ParametricLine Line = ConstructOffsetLine(Topology, Bounds[0].second, Vertex,
                                                            ResolveEdgeOpposite(Bounds[0].first, Vertex), Amount);
            BoundaryPosition = Line.Anchor;
        }
        else
        {
            continue;
        }
        const uint32_t Out = AppendWorkingVertex(Assembly, BoundaryPosition);
        Ring.push_back(Out);
        for (uint32_t Position = 0; Position < FanCount; ++Position)
        {
            if (Find(Position) != Root) continue;
            const uint32_t Face = Incident[Position].first;
            uint32_t Local = 0;
            if (ResolveLocalCorner(Topology, Face, Vertex, Local))
                Assembly.CornerOut[Face][Local] = { Out };
        }
    }

    if (Ring.size() >= 3)
    {
        const Vector3d VertexNormal = EvaluateVertexNormal(Topology, Vertex);
        OrderRingAngular(Assembly, (*Topology.Positions)[Vertex], VertexNormal, Ring);
        AppendOrientedFace(Assembly, Ring, VertexNormal);
    }
}

// 📝 Bridge one beveled edge with a swept strip between its four boundary points. At Segments == 1 this is the single flat chamfer
//    quad OriginA -> TerminusA -> OriginB -> TerminusB. Above, the strip is subdivided into Segments bands ACROSS the beveled edge.
//    CRITICAL: the two adjacent faces wind the shared edge in OPPOSITE directions, so OriginA/TerminusA (face A) pair with
//    TerminusB/OriginB (face B) — NOT OriginB/TerminusB. The two rounding chords are therefore the edge's cross-sections:
//    ChordU = OriginA..TerminusB (the two offsets of endpoint U) and ChordV = TerminusA..OriginB (the two offsets of endpoint V).
//    Lofting between the diagonals instead (OriginA..OriginB) crosses the quad into a bowtie — the diamond artefact. Each chord's
//    interior loops bulge toward that endpoint's ORIGINAL vertex (ApexU / ApexV) so a higher-roundness profile rounds out toward
//    where the sharp edge used to be, per endpoint, with no pinch toward the edge centre.
void AppendBeveledEdgeStrip(BevelAssembly&      Assembly,
                            uint32_t            OriginA,
                            uint32_t            TerminusA,
                            uint32_t            OriginB,
                            uint32_t            TerminusB,
                            const Vector3d&     ApexU,
                            const Vector3d&     ApexV,
                            const Vector3d&     Reference,
                            uint32_t            Segments,
                            const BevelProfile& Profile)
{
    if (Segments <= 1)
    {
        AppendOrientedFace(Assembly, { OriginA, TerminusA, OriginB, TerminusB }, Reference);
        return;
    }

    // The flat chamfer positions of the four corners; interior loops trace the profile arc across each cross-section chord.
    const Vector3d OriginAPos   = Assembly.WorkingPositions[OriginA];
    const Vector3d TerminusAPos = Assembly.WorkingPositions[TerminusA];
    const Vector3d OriginBPos   = Assembly.WorkingPositions[OriginB];
    const Vector3d TerminusBPos = Assembly.WorkingPositions[TerminusB];

    // 📝 One loop of two points per profile step S in 0..Segments. S = 0 reuses the OriginA / TerminusA boundary vertices (face A
    //    edge), S = Segments reuses TerminusB / OriginB (face B edge, opposite winding) so the strip stays welded to the shrunk
    //    faces at both ends. ChordU spans endpoint U's two offsets; ChordV spans endpoint V's two offsets.
    std::vector<uint32_t> ChordU(Segments + 1);   // [-] - endpoint U: OriginA -> TerminusB, one vertex per step
    std::vector<uint32_t> ChordV(Segments + 1);   // [-] - endpoint V: TerminusA -> OriginB
    ChordU[0] = OriginA;        ChordV[0] = TerminusA;
    ChordU[Segments] = TerminusB; ChordV[Segments] = OriginB;

    // 📝 The rounded profile is the quadratic Bézier of the corner: B(t) = (1-t)^2*Start + 2(1-t)t*Control + t^2*End, where Start / End
    //    are the two edge-offset points of one endpoint and Control sits between the chord midpoint and that endpoint's original vertex,
    //    pulled by Roundness. Roundness 0 -> Control at the chord midpoint -> a straight chamfer line (no bulge). Roundness ~0.5 ->
    //    Control ~halfway to the apex -> a near-circular quarter arc. Roundness 1 -> Control at the apex -> a square shoulder reaching
    //    toward where the sharp corner was. C1-continuous with the shrunk faces at both ends, so no diamond kink and no overshoot.
    const double Roundness = ClampScalar(Profile.Roundness, 0.0, 1.0);
    auto SampleCornerArc = [&](const Vector3d& Start, const Vector3d& End, const Vector3d& Apex, double T) -> Vector3d
    {
        const Vector3d Midpoint = ScaleVector(AddVector(Start, End), 0.5);
        const Vector3d Control  = AddVector(Midpoint, ScaleVector(SubtractVector(Apex, Midpoint), Roundness));
        const double   OneMinus = 1.0 - T;
        Vector3d Point = ScaleVector(Start,   OneMinus * OneMinus);
        Point          = AddVector(Point, ScaleVector(Control, 2.0 * OneMinus * T));
        Point          = AddVector(Point, ScaleVector(End,     T * T));
        return Point;
    };

    for (uint32_t Step = 1; Step < Segments; ++Step)
    {
        const double T = (double)Step / (double)Segments;
        ChordU[Step] = AppendWorkingVertex(Assembly, SampleCornerArc(OriginAPos,   TerminusBPos, ApexU, T));
        ChordV[Step] = AppendWorkingVertex(Assembly, SampleCornerArc(TerminusAPos, OriginBPos,   ApexV, T));
    }

    // Emit one quad band per step spanning the two cross-section chords. Step 0 == the original flat chamfer ring.
    for (uint32_t Step = 0; Step < Segments; ++Step)
        AppendOrientedFace(Assembly,
                           { ChordU[Step], ChordV[Step], ChordV[Step + 1], ChordU[Step + 1] },
                           Reference);
}

// Resolve the working assembly into the flat output: weld coincident (or near, when WeldRadius > 0) points, drop degenerate faces,
// and compact to referenced vertices. Returns false if nothing survived. WeldRadius is the auto-merge distance: the weld grid uses
// max(fine-coincident-epsilon, WeldRadius) so near points collapse, not just exact ones.
bool FinalizeAssembly(BevelAssembly&         Assembly,
                      double                 WeldRadius,
                      std::vector<Vector3d>& OutPositions,
                      std::vector<uint32_t>& OutFaceVertexIndices,
                      std::vector<uint32_t>& OutFaceVertexCounts)
{
    OutPositions.clear();
    OutFaceVertexIndices.clear();
    OutFaceVertexCounts.clear();
    if (Assembly.WorkingPositions.empty()) return false;

    Vector3d Minimum = Assembly.WorkingPositions[0];
    Vector3d Maximum = Assembly.WorkingPositions[0];
    for (const Vector3d& Point : Assembly.WorkingPositions)
    {
        Minimum.XCoord = std::min(Minimum.XCoord, Point.XCoord); Maximum.XCoord = std::max(Maximum.XCoord, Point.XCoord);
        Minimum.YCoord = std::min(Minimum.YCoord, Point.YCoord); Maximum.YCoord = std::max(Maximum.YCoord, Point.YCoord);
        Minimum.ZCoord = std::min(Minimum.ZCoord, Point.ZCoord); Maximum.ZCoord = std::max(Maximum.ZCoord, Point.ZCoord);
    }
    const double Diagonal = EvaluateVectorLength(SubtractVector(Maximum, Minimum));
    const double CoincidentEpsilon = 1.0e-6 * Diagonal + 1.0e-9;
    const double WeldEpsilon = std::max(CoincidentEpsilon, WeldRadius);

    // 📝 The weld key is the LOSSLESS quantized integer triple, not a hash of it: hashing three coords into one 64-bit value risks a
    //    collision that welds two genuinely distinct vertices. std::map over the exact triple cannot collide.
    std::map<std::tuple<int64_t, int64_t, int64_t>, uint32_t> WeldMap;
    std::vector<uint32_t> WeldRemap(Assembly.WorkingPositions.size());
    std::vector<Vector3d> WeldedPositions;
    auto EncodeQuantized = [&](const Vector3d& Point) -> std::tuple<int64_t, int64_t, int64_t>
    {
        return { (int64_t)std::llround(Point.XCoord / WeldEpsilon),
                 (int64_t)std::llround(Point.YCoord / WeldEpsilon),
                 (int64_t)std::llround(Point.ZCoord / WeldEpsilon) };
    };
    for (uint32_t Index = 0; Index < (uint32_t)Assembly.WorkingPositions.size(); ++Index)
    {
        const auto Key = EncodeQuantized(Assembly.WorkingPositions[Index]);
        const auto Existing = WeldMap.find(Key);
        if (Existing != WeldMap.end())
        {
            WeldRemap[Index] = Existing->second;
        }
        else
        {
            const uint32_t NewIndex = (uint32_t)WeldedPositions.size();
            WeldMap[Key] = NewIndex;
            WeldRemap[Index] = NewIndex;
            WeldedPositions.push_back(Assembly.WorkingPositions[Index]);
        }
    }

    std::vector<std::vector<uint32_t>> CleanFaces;
    for (const auto& Face : Assembly.Faces)
    {
        std::vector<uint32_t> Mapped;
        for (uint32_t Index : Face)
        {
            const uint32_t Welded = WeldRemap[Index];
            if (Mapped.empty() || Mapped.back() != Welded) Mapped.push_back(Welded);
        }
        if (Mapped.size() >= 3 && Mapped.front() == Mapped.back()) Mapped.pop_back();
        if (Mapped.size() < 3) continue;
        std::vector<uint32_t> Sorted = Mapped;
        std::sort(Sorted.begin(), Sorted.end());
        if (std::adjacent_find(Sorted.begin(), Sorted.end()) != Sorted.end()) continue;
        CleanFaces.push_back(std::move(Mapped));
    }
    if (CleanFaces.empty()) return false;

    std::vector<uint32_t> CompactRemap(WeldedPositions.size(), 0xFFFFFFFFu);
    for (const auto& Face : CleanFaces)
        for (uint32_t Index : Face)
            if (CompactRemap[Index] == 0xFFFFFFFFu)
            {
                CompactRemap[Index] = (uint32_t)OutPositions.size();
                OutPositions.push_back(WeldedPositions[Index]);
            }
    for (const auto& Face : CleanFaces)
    {
        OutFaceVertexCounts.push_back((uint32_t)Face.size());
        for (uint32_t Index : Face) OutFaceVertexIndices.push_back(CompactRemap[Index]);
    }
    return true;
}

// Perpendicular distance from Point to the infinite line through Alpha and Beta.
double EvaluatePerpendicularDistance(const Vector3d& Point, const Vector3d& Alpha, const Vector3d& Beta)
{
    const Vector3d Direction = SubtractVector(Beta, Alpha);
    const double Length = EvaluateVectorLength(Direction);
    if (Length < GeometryEpsilon) return EvaluateVectorLength(SubtractVector(Point, Alpha));
    return EvaluateVectorLength(CrossProduct(SubtractVector(Point, Alpha), Direction)) / Length;
}

// Reduce the supplied beveligible undirected edge keys to those that are manifold (exactly two adjacent faces) in Topology.
std::unordered_set<uint64_t> ResolveManifoldEdges(const BevelTopology& Topology, const std::unordered_set<uint64_t>& Keys)
{
    std::unordered_set<uint64_t> Beveled;
    for (uint64_t Key : Keys)
    {
        const auto Origins = Topology.EdgeOrigins.find(Key);
        if (Origins != Topology.EdgeOrigins.end() && Origins->second.size() == 2)
            Beveled.insert(Key);
    }
    return Beveled;
}

// The region's silhouette-loop edges: each manifold undirected edge whose adjacent faces include exactly one selected face.
std::unordered_set<uint64_t> ResolveRegionBoundaryEdges(const BevelTopology&                Topology,
                                                        const std::unordered_set<uint32_t>& SelectedFaces)
{
    std::unordered_set<uint64_t> Boundary;
    for (const auto& Entry : Topology.EdgeOrigins)
    {
        if (Entry.second.size() != 2) continue;
        uint32_t SelectedAdjacent = 0;
        for (const auto& Reference : Entry.second)
            if (SelectedFaces.count(Reference.first) != 0) ++SelectedAdjacent;
        if (SelectedAdjacent == 1) Boundary.insert(Entry.first);
    }
    return Boundary;
}

// 📝 The shared bevel worker: given the resolved set of beveligible edges over Topology, place per-vertex boundary vertices, shrink
//    the original faces, sweep the profile strip per beveled edge, and finalize (weld + compact). Fills the flat output arrays.
bool BevelResolvedEdges(BevelTopology&                      Topology,
                        const std::vector<Vector3d>&        Positions,
                        const std::unordered_set<uint64_t>& Beveled,
                        double                              Amount,
                        uint32_t                            Segments,
                        const BevelProfile&                 Profile,
                        double                              WeldRadius,
                        std::vector<Vector3d>&              OutPositions,
                        std::vector<uint32_t>&              OutFaceVertexIndices,
                        std::vector<uint32_t>&              OutFaceVertexCounts)
{
    if (Beveled.empty()) return false;

    std::unordered_set<uint32_t> Affected;
    for (uint64_t Key : Beveled)
    {
        Affected.insert((uint32_t)(Key & 0xFFFFFFFFu));
        Affected.insert((uint32_t)(Key >> 32));
    }

    BevelAssembly Assembly;
    Assembly.WorkingPositions = Positions;
    Assembly.CornerOut.resize(Topology.FaceCount.size());
    for (uint32_t Face = 0; Face < (uint32_t)Topology.FaceCount.size(); ++Face)
    {
        const uint32_t Count = Topology.FaceCount[Face];
        const uint32_t Base  = Topology.FaceBase[Face];
        Assembly.CornerOut[Face].resize(Count);
        for (uint32_t Local = 0; Local < Count; ++Local)
            Assembly.CornerOut[Face][Local] = { Topology.Corners[Base + Local] };
    }

    for (uint32_t Vertex : Affected)
        ResolveAffectedVertex(Topology, Vertex, Amount, Assembly, Beveled);

    // Shrunk original faces — each face's corners moved to their boundary vertices, oriented to the original normal.
    for (uint32_t Face = 0; Face < (uint32_t)Topology.FaceCount.size(); ++Face)
    {
        std::vector<uint32_t> Loop;
        for (const auto& CornerPoints : Assembly.CornerOut[Face])
            for (uint32_t Point : CornerPoints) Loop.push_back(Point);
        AppendOrientedFace(Assembly, Loop, Topology.FaceNormal[Face]);
    }

    // Profile strip per beveled edge, spanning its boundary vertices on both adjacent faces.
    for (uint64_t Key : Beveled)
    {
        const auto Origins = Topology.EdgeOrigins.find(Key);
        if (Origins == Topology.EdgeOrigins.end() || Origins->second.size() != 2) continue;
        const uint32_t FaceA = Origins->second[0].first;
        const uint32_t LocalA = Origins->second[0].second;
        const uint32_t FaceB = Origins->second[1].first;
        const uint32_t LocalB = Origins->second[1].second;
        const uint32_t CountA = Topology.FaceCount[FaceA];
        const uint32_t CountB = Topology.FaceCount[FaceB];
        const uint32_t OriginA   = Assembly.CornerOut[FaceA][LocalA].back();
        const uint32_t TerminusA = Assembly.CornerOut[FaceA][(LocalA + 1) % CountA].front();
        const uint32_t OriginB   = Assembly.CornerOut[FaceB][LocalB].back();
        const uint32_t TerminusB = Assembly.CornerOut[FaceB][(LocalB + 1) % CountB].front();
        const Vector3d Reference = NormalizeVector(AddVector(Topology.FaceNormal[FaceA], Topology.FaceNormal[FaceB]));
        // 📝 The two rounding chords bulge toward their OWN endpoint's original vertex, not the edge centre. ChordU spans face A's
        //    corner LocalA (endpoint U) offset to face B's opposite offset; ChordV spans face A's corner LocalA+1 (endpoint V). The
        //    apexes are those two original vertex positions, so each cross-section arc reaches toward the sharp corner it replaced.
        const uint32_t VertexU = Topology.Corners[Topology.FaceBase[FaceA] + LocalA];
        const uint32_t VertexV = Topology.Corners[Topology.FaceBase[FaceA] + (LocalA + 1) % CountA];
        AppendBeveledEdgeStrip(Assembly, OriginA, TerminusA, OriginB, TerminusB,
                               Positions[VertexU], Positions[VertexV], Reference, Segments, Profile);
    }

    return FinalizeAssembly(Assembly, WeldRadius, OutPositions, OutFaceVertexIndices, OutFaceVertexCounts);
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Superellipse profile height fraction at T in [0,1]. T = 0 / 1 are the two edge-offset points (bulge 0); midway returns the peak
//    bulge toward the corner apex. Roundness maps to the exponent: 0 = flat chamfer (straight line, bulge 0 everywhere so segments
//    lie on the cut), 0.5 = circular quarter-arc, 1 = square shoulder. When CustomPoints is present the polyline overrides this. Public
//    (declared in the header) so the internal sweep AND the toolbar preview swatch draw from one curve.
double EvaluateBevelProfile(double T, const BevelProfile& Profile)
{
    T = ClampScalar(T, 0.0, 1.0);
    if (!Profile.CustomPoints.empty())
    {
        // Piecewise-linear over the control points (X assumed monotonically 0..1, Y the bulge fraction).
        const std::vector<Vector2d>& Points = Profile.CustomPoints;
        if (Points.size() == 1) return ClampScalar(Points[0].YCoord, 0.0, 1.0);
        for (size_t Index = 0; Index + 1 < Points.size(); ++Index)
        {
            const Vector2d& Left  = Points[Index];
            const Vector2d& Right = Points[Index + 1];
            if (T >= Left.XCoord && T <= Right.XCoord)
            {
                const double Span = Right.XCoord - Left.XCoord;
                const double Fraction = Span > GeometryEpsilon ? (T - Left.XCoord) / Span : 0.0;
                return ClampScalar(Left.YCoord + (Right.YCoord - Left.YCoord) * Fraction, 0.0, 1.0);
            }
        }
        return ClampScalar(Points.back().YCoord, 0.0, 1.0);
    }

    const double Roundness = ClampScalar(Profile.Roundness, 0.0, 1.0);
    // A quarter superellipse |x|^p + |y|^p = 1 mapped so p rises with Roundness: p = 1 is the straight chamfer line (bulge 0),
    //    p = 2 the circular arc, larger p the square shoulder. Sample the arc height above the chord at parameter T.
    //    Exponent: Roundness 0 -> 1 (flat), 0.5 -> 2 (round), 1 -> ~6 (square-ish shoulder).
    const double Exponent = 1.0 + Roundness * 10.0;
    const double Centered = 2.0 * T - 1.0;                       // [-] - T in [0,1] -> [-1,1]
    const double ArcHeight = std::pow(std::max(0.0, 1.0 - std::pow(std::fabs(Centered), Exponent)), 1.0 / Exponent);
    return ClampScalar(ArcHeight, 0.0, 1.0);
}

double EvaluateBevelClampLimit(const PolygonCluster&               Source,
                               const AdjacencyIndex&               /*BaseAdjacency*/,
                               const std::unordered_set<uint32_t>& SelectedFaces,
                               const std::unordered_set<uint64_t>& SelectedEdges,
                               const std::unordered_set<uint32_t>& SelectedVertices,
                               BevelCategory                       Category)
{
    const std::vector<Vector3d>& Positions = Source.Attributes.Position;
    BevelTopology Topology;
    if (!ConstructBevelTopology(Positions, Source.FaceVertexIndices, Source.FaceVertexCounts, Topology))
        return 0.0;

    // VERTEX mode — bound by half the shortest incident edge of each selected vertex.
    if (Category == BevelCategory::VertexCategory)
    {
        if (SelectedVertices.empty()) return 0.0;
        double Limit = 1.0e30;
        for (uint32_t Vertex : SelectedVertices)
        {
            if (Vertex >= Topology.VertexCorners.size()) continue;
            for (const auto& Reference : Topology.VertexCorners[Vertex])
            {
                const uint32_t Face  = Reference.first;
                const uint32_t Local = Reference.second;
                const uint32_t Count = Topology.FaceCount[Face];
                const uint32_t Base  = Topology.FaceBase[Face];
                const uint32_t Prev  = Topology.Corners[Base + (Local + Count - 1) % Count];
                const uint32_t Next  = Topology.Corners[Base + (Local + 1) % Count];
                Limit = std::min(Limit, EvaluateVectorLength(SubtractVector(Positions[Next], Positions[Vertex])));
                Limit = std::min(Limit, EvaluateVectorLength(SubtractVector(Positions[Prev], Positions[Vertex])));
            }
        }
        return Limit >= 1.0e30 ? 0.0 : 0.49 * Limit;
    }

    // FACE mode — the region silhouette loop, bounded by the edge rule on those boundary edges (plus each face inradius as a floor).
    std::unordered_set<uint64_t> Beveled;
    if (Category == BevelCategory::FaceCategory)
    {
        if (SelectedFaces.empty()) return 0.0;
        Beveled = ResolveRegionBoundaryEdges(Topology, SelectedFaces);
        // Inradius floor over the selected faces so a large face never overshoots its own centroid.
        double InradiusLimit = 1.0e30;
        for (uint32_t Face : SelectedFaces)
        {
            if (Face >= Topology.FaceCount.size()) continue;
            const uint32_t Count = Topology.FaceCount[Face];
            const uint32_t Base  = Topology.FaceBase[Face];
            for (uint32_t Local = 0; Local < Count; ++Local)
            {
                const Vector3d& Alpha = Positions[Topology.Corners[Base + Local]];
                const Vector3d& Beta  = Positions[Topology.Corners[Base + (Local + 1) % Count]];
                InradiusLimit = std::min(InradiusLimit, EvaluatePerpendicularDistance(Topology.FaceCentroid[Face], Alpha, Beta));
            }
        }
        if (Beveled.empty()) return InradiusLimit >= 1.0e30 ? 0.0 : 0.9 * InradiusLimit;
    }
    else   // EDGE mode
    {
        Beveled = ResolveManifoldEdges(Topology, SelectedEdges);
    }
    if (Beveled.empty()) return 0.0;

    // EDGE / FACE — bound by the perpendicular distance from each beveled edge's line to each adjacent face's centroid.
    double Limit = 1.0e30;
    for (uint64_t Key : Beveled)
    {
        const uint32_t Origin   = (uint32_t)(Key & 0xFFFFFFFFu);
        const uint32_t Terminus = (uint32_t)(Key >> 32);
        const auto Origins = Topology.EdgeOrigins.find(Key);
        if (Origins == Topology.EdgeOrigins.end()) continue;
        for (const auto& Reference : Origins->second)
        {
            const uint32_t Face = Reference.first;
            Limit = std::min(Limit, EvaluatePerpendicularDistance(Topology.FaceCentroid[Face], Positions[Origin], Positions[Terminus]));
        }
    }
    return Limit >= 1.0e30 ? 0.0 : 0.9 * Limit;
}

bool BevelPolygonSelection(PolygonCluster&                     Target,
                           const AdjacencyIndex&               BaseAdjacency,
                           const std::unordered_set<uint32_t>& SelectedFaces,
                           const std::unordered_set<uint64_t>& SelectedEdges,
                           const std::unordered_set<uint32_t>& SelectedVertices,
                           BevelCategory                       Category,
                           double                              Width,
                           uint32_t                            Segments,
                           const BevelProfile&                 Profile,
                           bool                                AutoMerge,
                           double                              WeldThreshold,
                           BevelOutcome&                       Outcome)
{
    Outcome = BevelOutcome{};

    // Clamp the width to the safe limit so a stale modal drag can never overshoot into self-intersection.
    const double ClampLimit = EvaluateBevelClampLimit(Target, BaseAdjacency, SelectedFaces, SelectedEdges, SelectedVertices, Category);
    Outcome.ClampLimit = ClampLimit;
    if (ClampLimit <= GeometryEpsilon) return false;
    const double Amount = std::min(Width, ClampLimit);
    if (Amount <= 1.0e-6) return false;

    const uint32_t SweepSegments = Segments < 1 ? 1 : Segments;
    const double WeldRadius = AutoMerge ? std::max(0.0, WeldThreshold) : 0.0;

    // Snapshot the base flat geometry; the kernel rebuilds Target from this (never mutates in place until the finalize succeeds).
    const std::vector<Vector3d> Positions = Target.Attributes.Position;

    BevelTopology Topology;
    if (!ConstructBevelTopology(Positions, Target.FaceVertexIndices, Target.FaceVertexCounts, Topology))
        return false;

    // Resolve the beveligible edges for the active mode.
    std::unordered_set<uint64_t> Beveled;
    if (Category == BevelCategory::VertexCategory)
    {
        // Vertex bevel handled below via the pulled-point path (no per-edge strip); early-resolve is not needed here.
    }
    else if (Category == BevelCategory::FaceCategory)
    {
        if (SelectedFaces.empty()) return false;
        Beveled = ResolveRegionBoundaryEdges(Topology, SelectedFaces);
    }
    else
    {
        Beveled = ResolveManifoldEdges(Topology, SelectedEdges);
    }

    std::vector<Vector3d> OutPositions;
    std::vector<uint32_t> OutFaceVertexIndices;
    std::vector<uint32_t> OutFaceVertexCounts;

    if (Category == BevelCategory::VertexCategory)
    {
        if (SelectedVertices.empty()) return false;

        BevelAssembly Assembly;
        Assembly.WorkingPositions = Positions;

        // One pulled point per (selected vertex, incident edge): the vertex slid Amount along that edge, shared across faces sharing
        //    the edge so adjacent faces meet on the same point.
        std::unordered_map<uint64_t, uint32_t> PulledPoint;
        auto ResolvePulled = [&](uint32_t Vertex, uint32_t Opposite) -> uint32_t
        {
            const uint64_t Key = EncodeEdge(Vertex, Opposite);
            const auto Existing = PulledPoint.find(Key);
            if (Existing != PulledPoint.end()) return Existing->second;
            const Vector3d Direction = NormalizeVector(SubtractVector(Positions[Opposite], Positions[Vertex]));
            const uint32_t Out = AppendWorkingVertex(Assembly, AddVector(Positions[Vertex], ScaleVector(Direction, Amount)));
            PulledPoint[Key] = Out;
            return Out;
        };

        for (uint32_t Face = 0; Face < (uint32_t)Topology.FaceCount.size(); ++Face)
        {
            const uint32_t Count = Topology.FaceCount[Face];
            const uint32_t Base  = Topology.FaceBase[Face];
            std::vector<uint32_t> Loop;
            for (uint32_t Local = 0; Local < Count; ++Local)
            {
                const uint32_t Vertex = Topology.Corners[Base + Local];
                if (!SelectedVertices.count(Vertex)) { Loop.push_back(Vertex); continue; }
                const uint32_t Prev = Topology.Corners[Base + (Local + Count - 1) % Count];
                const uint32_t Next = Topology.Corners[Base + (Local + 1) % Count];
                Loop.push_back(ResolvePulled(Vertex, Prev));
                Loop.push_back(ResolvePulled(Vertex, Next));
            }
            AppendOrientedFace(Assembly, Loop, Topology.FaceNormal[Face]);
        }

        // Cap each selected vertex with one N-gon over its pulled points (a dome cap; Segments-domes are the fast follow).
        for (uint32_t Vertex : SelectedVertices)
        {
            if (Vertex >= Topology.VertexCorners.size() || Topology.VertexCorners[Vertex].size() < 3) continue;
            std::unordered_set<uint32_t> Seen;
            std::vector<uint32_t> Ring;
            for (const auto& Reference : Topology.VertexCorners[Vertex])
            {
                const uint32_t Face  = Reference.first;
                const uint32_t Local = Reference.second;
                const uint32_t Count = Topology.FaceCount[Face];
                const uint32_t Base  = Topology.FaceBase[Face];
                for (uint32_t Opposite : { Topology.Corners[Base + (Local + Count - 1) % Count],
                                           Topology.Corners[Base + (Local + 1) % Count] })
                {
                    const uint64_t Key = EncodeEdge(Vertex, Opposite);
                    const auto Pulled = PulledPoint.find(Key);
                    if (Pulled != PulledPoint.end() && !Seen.count(Pulled->second))
                    { Seen.insert(Pulled->second); Ring.push_back(Pulled->second); }
                }
            }
            if (Ring.size() >= 3)
            {
                const Vector3d VertexNormal = EvaluateVertexNormal(Topology, Vertex);
                OrderRingAngular(Assembly, Positions[Vertex], VertexNormal, Ring);
                AppendOrientedFace(Assembly, Ring, VertexNormal);
            }
        }

        if (!FinalizeAssembly(Assembly, WeldRadius, OutPositions, OutFaceVertexIndices, OutFaceVertexCounts))
            return false;
    }
    else
    {
        if (!BevelResolvedEdges(Topology, Positions, Beveled, Amount, SweepSegments, Profile, WeldRadius,
                                OutPositions, OutFaceVertexIndices, OutFaceVertexCounts))
            return false;
    }

    // Commit the rebuilt geometry into Target. The topology changed wholesale, so the old per-corner UVs / colours cannot carry
    //    through (they are dropped, as Loop Cut does). Normals, however, MUST be re-derived here: an empty modifier stack streams
    //    the cage's Normal array VERBATIM to the GPU (no smooth-normal pass runs unless a modifier refined the geometry), so a
    //    cleared array would light the whole object black. Rebuild a smooth per-vertex normal from the new faces (Newell + accumulate).
    Target.Attributes.Position = OutPositions;
    Target.Attributes.TextureCoordinate.clear();
    Target.Attributes.Color.clear();
    Target.FaceVertexIndices = OutFaceVertexIndices;
    Target.FaceVertexCounts  = OutFaceVertexCounts;
    Target.FaceCornerTexture.clear();

    // Smooth per-vertex normals over the rebuilt topology (area-weighted Newell accumulation, then normalize; +Y on a degenerate).
    std::vector<Vector3d> SmoothNormals(OutPositions.size(), Vector3d{ 0.0, 0.0, 0.0 });
    {
        uint32_t CornerCursor = 0;
        for (uint32_t CornerCount : OutFaceVertexCounts)
        {
            if (CornerCount >= 3)
            {
                Vector3d FaceNormal{ 0.0, 0.0, 0.0 };
                for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
                {
                    const Vector3d& Current = OutPositions[OutFaceVertexIndices[CornerCursor + Corner]];
                    const Vector3d& Next    = OutPositions[OutFaceVertexIndices[CornerCursor + (Corner + 1) % CornerCount]];
                    FaceNormal.XCoord += (Current.YCoord - Next.YCoord) * (Current.ZCoord + Next.ZCoord);
                    FaceNormal.YCoord += (Current.ZCoord - Next.ZCoord) * (Current.XCoord + Next.XCoord);
                    FaceNormal.ZCoord += (Current.XCoord - Next.XCoord) * (Current.YCoord + Next.YCoord);
                }
                for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
                {
                    const uint32_t VertexSlot = OutFaceVertexIndices[CornerCursor + Corner];
                    SmoothNormals[VertexSlot] = AddVector(SmoothNormals[VertexSlot], FaceNormal);
                }
            }
            CornerCursor += CornerCount;
        }
        for (Vector3d& Normal : SmoothNormals)
        {
            const double LengthSquared = DotProduct(Normal, Normal);
            Normal = LengthSquared > 1.0e-24 ? NormalizeVector(Normal) : Vector3d{ 0.0, 1.0, 0.0 };
        }
    }
    Target.Attributes.Normal = std::move(SmoothNormals);

    // The follow-on selection is the vertices the bevel introduced, not the whole object. Face selection is left EMPTY: reporting
    //    every rebuilt face (as an earlier revision did) highlighted the entire specimen. The modal clears the stale edge keys and
    //    tracks the region through these vertices instead. (A precise per-bevel face set is a later refinement.)
    Outcome.ResultFaces.clear();
    for (uint32_t Vertex = 0; Vertex < (uint32_t)OutPositions.size(); ++Vertex)
        Outcome.ResultVertices.insert(Vertex);

    Outcome.Completed = true;
    return true;
}

} // namespace Frontier
