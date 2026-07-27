/*============================================================================================================================================
                                                           COMPONENTSELECTION.CPP
============================================================================================================================================*/
// 🧩 The component-selection operations — the verbatim engine port of selection.js's selClear / selectFace|Vert|Edge /
//    computeLoopEdges (loopContinueAtVert + walkLoopDir + walkStraightestDir) / connectedVertsFrom + selLinked / selAll, plus
//    the loop preview. Every walk reads the AdjacencyIndex (the TOPO shape); the straightness fallback additionally reads the
//    cluster positions for the edge-direction heuristic. No cluster mutation, no half-edge — the non-edit selection path.

#include "ComponentSelection.h"

#include "AdjacencyIndex.h"
#include "PolygonCluster.h"

#include <cmath>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The loop continuation at vertex CurrentVertex arriving along CurrentEdge (the port of loopContinueAtVert): the edge
    //    incident to the vertex that shares NO face with the current edge — the one straight across a clean valence-4 quad
    //    crossing. Writes the continuation edge key + its far vertex and returns true; returns false when the vertex is not a
    //    clean valence-4 crossing (pole / boundary / tri fan) or the continuation is ambiguous, so the caller falls back.
    bool ResolveLoopContinuation(const AdjacencyIndex& Index,
                                 uint32_t              CurrentVertex,
                                 uint64_t              CurrentEdge,
                                 uint64_t&             OutEdge,
                                 uint32_t&             OutFarVertex)
    {
        if (CurrentVertex >= Index.VertexAdjacency.size()) return false;
        const std::vector<uint32_t>& AdjacentVertices = Index.VertexAdjacency[CurrentVertex];
        if (AdjacentVertices.size() != 4) return false;   // valence-4 only for the topology rule

        auto CurrentFacesLookup = Index.EdgeFaces.find(CurrentEdge);
        if (CurrentFacesLookup == Index.EdgeFaces.end()) return false;
        const std::vector<uint32_t>& CurrentFaces = CurrentFacesLookup->second;

        bool     Found        = false;
        uint64_t FoundEdge    = 0;
        uint32_t FoundFar     = 0;
        for (uint32_t Adjacent : AdjacentVertices)
        {
            const uint64_t Key = EncodeEdgeKey(CurrentVertex, Adjacent);
            if (Key == CurrentEdge) continue;

            // the continuation shares no face with the current edge
            auto KeyFacesLookup = Index.EdgeFaces.find(Key);
            if (KeyFacesLookup == Index.EdgeFaces.end()) continue;
            bool SharesFace = false;
            for (uint32_t KeyFace : KeyFacesLookup->second)
                for (uint32_t CurrentFace : CurrentFaces)
                    if (KeyFace == CurrentFace) { SharesFace = true; break; }
            if (SharesFace) continue;

            if (Found) return false;   // ambiguous -> bail to the straightness fallback
            Found     = true;
            FoundEdge = Key;
            FoundFar  = Adjacent;
        }
        if (!Found) return false;
        OutEdge      = FoundEdge;
        OutFarVertex = FoundFar;
        return true;
    }

    // 📝 One direction of the vertex-centric loop walk (the port of walkLoopDir): advance across valence-4 crossings from
    //    StartVertex, adding each continuation edge, until a pole / boundary or the loop closes. Returns true if it made any
    //    topology-rule progress, so the caller knows whether a straightness fallback is still needed for this direction.
    bool WalkLoopDirection(const AdjacencyIndex&          Index,
                           std::unordered_set<uint64_t>&  Loop,
                           uint64_t                       SeedEdge,
                           uint32_t                       StartVertex)
    {
        uint64_t CurrentEdge   = SeedEdge;
        uint32_t CurrentVertex = StartVertex;
        bool     Advanced      = false;
        for (int Guard = 0; Guard < 4000; ++Guard)
        {
            uint64_t StepEdge = 0;
            uint32_t StepFar  = 0;
            if (!ResolveLoopContinuation(Index, CurrentVertex, CurrentEdge, StepEdge, StepFar)) break;
            if (Loop.count(StepEdge)) break;   // closed the loop
            Loop.insert(StepEdge);
            Advanced      = true;
            CurrentEdge   = StepEdge;
            CurrentVertex = StepFar;
        }
        return Advanced;
    }

    // 📝 The unit edge direction from Origin to Target (the port of edgeDir), reading the cluster positions. A degenerate
    //    (zero-length) edge falls back to a unit length of 1 so the dot-product score stays finite.
    Vector3d EvaluateEdgeDirection(const PolygonCluster& Cluster, uint32_t OriginVertex, uint32_t TargetVertex)
    {
        const Vector3d& Origin = Cluster.Attributes.Position[OriginVertex];
        const Vector3d& Target = Cluster.Attributes.Position[TargetVertex];
        Vector3d Delta  = SubtractVector(Target, Origin);
        double   Length = EvaluateVectorLength(Delta);
        if (Length < 1e-12) Length = 1.0;
        return ScaleVector(Delta, 1.0 / Length);
    }

    // 📝 The straightness fallback (the port of walkStraightestDir): from an endpoint of the seed, continue to the adjacent
    //    vertex whose direction best matches the incoming direction, until no continuation scores above 0.3 (a corner) or the loop
    //    closes. Used only where the vertex-centric rule cannot follow (poles / boundaries / non-quad topology).
    void WalkStraightestDirection(const AdjacencyIndex&          Index,
                                  const PolygonCluster&          Cluster,
                                  std::unordered_set<uint64_t>&  Loop,
                                  uint64_t                       SeedEdge,
                                  uint32_t                       StartVertex)
    {
        uint64_t CurrentEdge   = SeedEdge;
        uint32_t CurrentVertex = StartVertex;
        for (int Guard = 0; Guard < 4000; ++Guard)
        {
            auto EndpointsLookup = Index.EdgeVertices.find(CurrentEdge);
            if (EndpointsLookup == Index.EdgeVertices.end()) break;
            const uint32_t EndpointA = EndpointsLookup->second.LowerVertex;
            const uint32_t EndpointB = EndpointsLookup->second.HigherVertex;
            const uint32_t PreviousVertex = (CurrentVertex == EndpointA) ? EndpointB : EndpointA;
            const Vector3d PreviousDirection = EvaluateEdgeDirection(Cluster, PreviousVertex, CurrentVertex);

            if (CurrentVertex >= Index.VertexAdjacency.size()) break;
            uint64_t BestEdge  = 0;
            uint32_t BestFar   = 0;
            double   BestScore = -2.0;
            bool     HaveBest  = false;
            for (uint32_t Adjacent : Index.VertexAdjacency[CurrentVertex])
            {
                const uint64_t Key = EncodeEdgeKey(CurrentVertex, Adjacent);
                if (Key == CurrentEdge) continue;
                const Vector3d Direction = EvaluateEdgeDirection(Cluster, CurrentVertex, Adjacent);
                const double   Score = DotProduct(PreviousDirection, Direction);   // straightest continuation
                if (Score > BestScore) { BestScore = Score; BestEdge = Key; BestFar = Adjacent; HaveBest = true; }
            }
            if (!HaveBest || Loop.count(BestEdge) || BestScore < 0.3) break;
            Loop.insert(BestEdge);
            CurrentEdge   = BestEdge;
            CurrentVertex = BestFar;
        }
    }

    // 📝 The edge-loop Set grown from a seed edge (the port of computeLoopEdges): walk the vertex-centric rule from both
    //    endpoints, falling back to the straightness heuristic per direction where the rule cannot follow.
    std::unordered_set<uint64_t> ComputeEdgeLoop(const AdjacencyIndex& Index, const PolygonCluster& Cluster, uint64_t SeedEdge)
    {
        std::unordered_set<uint64_t> Loop;
        Loop.insert(SeedEdge);
        auto EndpointsLookup = Index.EdgeVertices.find(SeedEdge);
        if (EndpointsLookup == Index.EdgeVertices.end()) return Loop;
        const uint32_t VertexA = EndpointsLookup->second.LowerVertex;
        const uint32_t VertexB = EndpointsLookup->second.HigherVertex;
        const bool OkA = WalkLoopDirection(Index, Loop, SeedEdge, VertexA);
        const bool OkB = WalkLoopDirection(Index, Loop, SeedEdge, VertexB);
        if (!OkA) WalkStraightestDirection(Index, Cluster, Loop, SeedEdge, VertexA);
        if (!OkB) WalkStraightestDirection(Index, Cluster, Loop, SeedEdge, VertexB);
        return Loop;
    }

    // 📝 The connected vertex component reachable from a seed set by walking VertexAdjacency (the port of connectedVertsFrom):
    //    a flood over shared vertices, so it ignores seams (pure topological connectivity).
    std::unordered_set<uint32_t> FloodConnectedVertices(const AdjacencyIndex& Index, const std::unordered_set<uint32_t>& Seeds)
    {
        std::unordered_set<uint32_t> Component(Seeds.begin(), Seeds.end());
        std::vector<uint32_t> Stack(Seeds.begin(), Seeds.end());
        while (!Stack.empty())
        {
            const uint32_t Vertex = Stack.back();
            Stack.pop_back();
            if (Vertex >= Index.VertexAdjacency.size()) continue;
            for (uint32_t Adjacent : Index.VertexAdjacency[Vertex])
                if (Component.insert(Adjacent).second) Stack.push_back(Adjacent);
        }
        return Component;
    }

    // 📝 The loop edge of quad face Face opposite EdgeKey (the port of oppositeFaceEdge): the one loop edge that shares no
    //    vertex with EdgeKey — the band's exit edge across the quad. Returns 0 (no opposite) when the face is not a quad, when
    //    EdgeKey / a candidate is missing from the map, or when the disjoint edge is not unique (ambiguous → caller bails).
    uint64_t ResolveOppositeFaceEdge(const AdjacencyIndex& Index, uint32_t Face, uint64_t EdgeKey)
    {
        if (Face >= Index.FaceEdgeKeys.size()) return 0;
        const std::vector<uint64_t>& Loop = Index.FaceEdgeKeys[Face];
        if (Loop.size() != 4) return 0;   // quad-only for the topological loop rule

        auto SeedEndpoints = Index.EdgeVertices.find(EdgeKey);
        if (SeedEndpoints == Index.EdgeVertices.end()) return 0;
        const uint32_t SeedLower  = SeedEndpoints->second.LowerVertex;
        const uint32_t SeedHigher = SeedEndpoints->second.HigherVertex;

        uint64_t Found      = 0;
        bool     HaveFound  = false;
        for (uint64_t Key : Loop)
        {
            if (Key == EdgeKey) continue;
            auto Endpoints = Index.EdgeVertices.find(Key);
            if (Endpoints == Index.EdgeVertices.end()) continue;
            const uint32_t Lower  = Endpoints->second.LowerVertex;
            const uint32_t Higher = Endpoints->second.HigherVertex;
            // disjoint from EdgeKey → the edge straight across the quad
            if (Lower != SeedLower && Lower != SeedHigher && Higher != SeedLower && Higher != SeedHigher)
            {
                if (HaveFound) return 0;   // ambiguous -> bail to the geometric fallback
                HaveFound = true;
                Found     = Key;
            }
        }
        return HaveFound ? Found : 0;
    }

    // 📝 The face adjacent across EdgeKey other than Current (the port of edgeFaces.find(nf => nf !== curFace)): the interior
    //    face sharing EdgeKey. Returns InvalidFaceOrdinal when EdgeKey is a boundary (only Current incident) or unmapped.
    constexpr uint32_t InvalidFaceOrdinal = 0xFFFFFFFF;
    uint32_t ResolveFaceAcrossEdge(const AdjacencyIndex& Index, uint64_t EdgeKey, uint32_t Current)
    {
        auto Faces = Index.EdgeFaces.find(EdgeKey);
        if (Faces == Index.EdgeFaces.end()) return InvalidFaceOrdinal;
        for (uint32_t Face : Faces->second)
            if (Face != Current) return Face;
        return InvalidFaceOrdinal;
    }

    // 📝 One arm of the topological face-loop walk (the port of walkFaceLoopDir): step to the face across ExitEdge, then
    //    continue out that quad's opposite edge, adding each face reached, until a boundary or a non-quad breaks the chain. Reports
    //    the terminal (face, exit edge) the clean quad chain stalled on via OutTerminalFace / OutTerminalEdge — the LAST quad
    //    reached and the edge it was about to cross when the opposite-edge rule failed (a tri / pole ahead). The caller RESUMES the
    //    geometric fallback from THERE (not from the seed, where Ring.count would break instantly on this arm's own captured faces)
    //    so a loop that runs into a pole mid-chain keeps going on THIS arm — the direction-symmetry fix. Returns true if it advanced;
    //    StalledAtPole reports the chain ended at a tri / pole (a fallback is warranted) versus a clean boundary / closed loop (done).
    bool WalkFaceLoopDirection(const AdjacencyIndex&          Index,
                               std::unordered_set<uint32_t>&  Ring,
                               uint32_t                       Seed,
                               uint64_t                       ExitEdge,
                               uint32_t&                      OutTerminalFace,
                               uint64_t&                      OutTerminalEdge,
                               bool&                          OutStalledAtPole)
    {
        uint32_t CurrentFace = Seed;
        uint64_t EntryEdge   = ExitEdge;
        bool     Advanced    = false;
        OutTerminalFace      = Seed;
        OutTerminalEdge      = ExitEdge;
        OutStalledAtPole     = false;
        for (int Guard = 0; Guard < 4000; ++Guard)
        {
            const uint32_t AcrossFace = ResolveFaceAcrossEdge(Index, EntryEdge, CurrentFace);
            if (AcrossFace == InvalidFaceOrdinal || Ring.count(AcrossFace)) break;   // boundary or closed — no fallback
            const uint64_t NextEdge = ResolveOppositeFaceEdge(Index, AcrossFace, EntryEdge);
            if (NextEdge == 0)   // tri / pole ahead — the clean quad chain ends here; leave the resume point on this arm's last quad
            {
                OutStalledAtPole = true;
                break;
            }
            Ring.insert(AcrossFace);
            Advanced         = true;
            EntryEdge        = NextEdge;
            CurrentFace      = AcrossFace;
            OutTerminalFace  = CurrentFace;   // last clean quad captured on this arm
            OutTerminalEdge  = EntryEdge;     // the edge it will exit next (into the pole)
        }
        return Advanced;
    }

    // 📝 The unit midpoint direction of an edge (the port of edgeMidDir): its two endpoints' direction, reading cluster
    //    positions. A missing / degenerate edge yields a zero vector, which scores neutrally in the anti-parallel test.
    Vector3d EvaluateEdgeMidDirection(const AdjacencyIndex& Index, const PolygonCluster& Cluster, uint64_t EdgeKey)
    {
        auto Endpoints = Index.EdgeVertices.find(EdgeKey);
        if (Endpoints == Index.EdgeVertices.end()) return Vector3d{ 0.0, 0.0, 0.0 };
        return EvaluateEdgeDirection(Cluster, Endpoints->second.LowerVertex, Endpoints->second.HigherVertex);
    }

    // 📝 The geometric face-ring fallback for one direction (the port of walkFaceRingDirGeom): step to the face across
    //    EntryEdge, then exit its MOST ANTI-PARALLEL loop edge, until a boundary or the ring closes. Used only where the quad
    //    rule cannot follow (tris / poles). Facing-independent (topology + object-space direction, never screen space).
    void WalkFaceRingGeometric(const AdjacencyIndex&          Index,
                               const PolygonCluster&          Cluster,
                               std::unordered_set<uint32_t>&  Ring,
                               uint32_t                       Seed,
                               uint64_t                       EntryEdgeSeed)
    {
        uint32_t CurrentFace = Seed;
        uint64_t EntryEdge   = EntryEdgeSeed;
        for (int Guard = 0; Guard < 4000; ++Guard)
        {
            const uint32_t AcrossFace = ResolveFaceAcrossEdge(Index, EntryEdge, CurrentFace);
            if (AcrossFace == InvalidFaceOrdinal || Ring.count(AcrossFace)) break;
            Ring.insert(AcrossFace);
            if (AcrossFace >= Index.FaceEdgeKeys.size()) break;
            const Vector3d EntryDirection = EvaluateEdgeMidDirection(Index, Cluster, EntryEdge);
            uint64_t BestEdge  = 0;
            double   BestScore = 2.0;   // seek the smallest (most anti-parallel) dot
            bool     HaveBest  = false;
            for (uint64_t Key : Index.FaceEdgeKeys[AcrossFace])
            {
                if (Key == EntryEdge) continue;
                const Vector3d Direction = EvaluateEdgeMidDirection(Index, Cluster, Key);
                const double   Score = DotProduct(EntryDirection, Direction);
                if (Score < BestScore) { BestScore = Score; BestEdge = Key; HaveBest = true; }
            }
            if (!HaveBest) break;
            EntryEdge   = BestEdge;
            CurrentFace = AcrossFace;
        }
    }

    // 📝 The per-face corner base offsets into FaceVertexIndices / FaceCornerTexture (the running sum of FaceVertexCounts):
    //    face F's corners occupy [FaceCornerBase[F], FaceCornerBase[F] + FaceVertexCounts[F]). Computed once so the seam test
    //    can locate a vertex's corner slot in a face without re-summing the whole prefix each lookup.
    std::vector<uint32_t> ResolveFaceCornerBases(const PolygonCluster& Cluster)
    {
        std::vector<uint32_t> Bases;
        Bases.reserve(Cluster.FaceVertexCounts.size());
        uint32_t Running = 0;
        for (uint32_t Count : Cluster.FaceVertexCounts)
        {
            Bases.push_back(Running);
            Running += Count;
        }
        return Bases;
    }

    // 📝 The per-corner UV of vertex Vertex within face Face, read from FaceCornerTexture at the corner slot where the face's
    //    loop lists Vertex (the port of the seam-aware UV lookup). Returns true with the UV when found; false when the vertex is
    //    not a corner of the face or the corner-UV array is short. A seam vertex holds a DIFFERENT UV per incident face, which
    //    is exactly what the seam test below compares.
    bool ResolveCornerTexture(const PolygonCluster&         Cluster,
                              const std::vector<uint32_t>&  CornerBases,
                              uint32_t                      Face,
                              uint32_t                      Vertex,
                              Vector2d&                     OutTexture)
    {
        if (Face >= Cluster.FaceVertexCounts.size() || Face >= CornerBases.size()) return false;
        const uint32_t Base  = CornerBases[Face];
        const uint32_t Count = Cluster.FaceVertexCounts[Face];
        for (uint32_t Corner = 0; Corner < Count; ++Corner)
        {
            const uint32_t Slot = Base + Corner;
            if (Slot >= Cluster.FaceVertexIndices.size() || Slot >= Cluster.FaceCornerTexture.size()) return false;
            if (Cluster.FaceVertexIndices[Slot] == Vertex)
            {
                OutTexture = Cluster.FaceCornerTexture[Slot];
                return true;
            }
        }
        return false;
    }

    // 📝 Whether the interior edge EdgeKey (shared by faces FaceA / FaceB) is a UV SEAM: true when either endpoint's corner UV
    //    differs across the two faces beyond epsilon. A missing corner UV on either side is treated as a seam (the faces cannot
    //    be proven to share the chart). Boundary edges are handled by the caller (they are hard island edges), so this only
    //    runs for the two-incident-face case.
    bool ResolveEdgeIsSeam(const PolygonCluster&         Cluster,
                           const std::vector<uint32_t>&  CornerBases,
                           const AdjacencyIndex&         Index,
                           uint64_t                      EdgeKey,
                           uint32_t                      FaceA,
                           uint32_t                      FaceB)
    {
        auto Endpoints = Index.EdgeVertices.find(EdgeKey);
        if (Endpoints == Index.EdgeVertices.end()) return true;
        const uint32_t Lower  = Endpoints->second.LowerVertex;
        const uint32_t Higher = Endpoints->second.HigherVertex;

        constexpr double SeamEpsilon = 1e-6;
        for (uint32_t Endpoint : { Lower, Higher })
        {
            Vector2d TextureA;
            Vector2d TextureB;
            if (!ResolveCornerTexture(Cluster, CornerBases, FaceA, Endpoint, TextureA)) return true;
            if (!ResolveCornerTexture(Cluster, CornerBases, FaceB, Endpoint, TextureB)) return true;
            if (std::fabs(TextureA.XCoord - TextureB.XCoord) > SeamEpsilon ||
                std::fabs(TextureA.YCoord - TextureB.YCoord) > SeamEpsilon)
                return true;
        }
        return false;
    }

    // 📝 One arm of a face band: run the topological quad-loop walk out SeedEdge, then — if it stalled at a tri / pole rather than a
    //    clean boundary / closed loop — RESUME the geometric heuristic from the last clean quad so the loop continues past the pole.
    //    (The port ran the geometric fallback only when the topological walk never advanced, so an arm that reached one quad then a
    //    pole was truncated — the cause of a face showing its loop in only one direction. Resuming per arm restores both arms.)
    void WalkFaceBandArm(const AdjacencyIndex&          Index,
                         const PolygonCluster&          Cluster,
                         std::unordered_set<uint32_t>&  Ring,
                         uint32_t                       Seed,
                         uint64_t                       SeedEdge)
    {
        uint32_t TerminalFace  = Seed;
        uint64_t TerminalEdge  = SeedEdge;
        bool     StalledAtPole = false;
        const bool Advanced = WalkFaceLoopDirection(Index, Ring, Seed, SeedEdge,
                                                    TerminalFace, TerminalEdge, StalledAtPole);
        // Resume geometrically when the clean chain hit a pole (mid-chain OR on the first step, where Advanced is false).
        if (StalledAtPole || !Advanced)
            WalkFaceRingGeometric(Index, Cluster, Ring, TerminalFace, TerminalEdge);
    }

    // 📝 The face band grown from a seed face (the port of computeFaceBand): a quad seed has two loop axes (its opposite-edge
    //    pairs FaceEdgeKeys[0]/[2] and [1]/[3]); each traces one continuous band, so pick the LONGER (more loop-like) with a
    //    deterministic tiebreak — never a camera-dependent choice, so the ghost matches the commit. A non-quad seed has no
    //    topological axis: fall back to the geometric walk out every loop edge. Each arm resumes its geometric fallback from where
    //    its topological chain stalled (WalkFaceBandArm), so a loop that runs into a pole keeps going on BOTH arms of the axis.
    std::unordered_set<uint32_t> ComputeFaceBand(const AdjacencyIndex& Index, const PolygonCluster& Cluster, uint32_t Seed)
    {
        if (Seed >= Index.FaceEdgeKeys.size()) return std::unordered_set<uint32_t>{ Seed };
        const std::vector<uint64_t>& Loop = Index.FaceEdgeKeys[Seed];

        if (Loop.size() == 4)
        {
            std::unordered_set<uint32_t> BandFirst{ Seed };
            WalkFaceBandArm(Index, Cluster, BandFirst, Seed, Loop[0]);
            WalkFaceBandArm(Index, Cluster, BandFirst, Seed, Loop[2]);

            std::unordered_set<uint32_t> BandSecond{ Seed };
            WalkFaceBandArm(Index, Cluster, BandSecond, Seed, Loop[1]);
            WalkFaceBandArm(Index, Cluster, BandSecond, Seed, Loop[3]);

            return BandFirst.size() >= BandSecond.size() ? BandFirst : BandSecond;
        }

        // non-quad seed: no topological loop axis — walk the geometric heuristic out every loop edge
        std::unordered_set<uint32_t> Ring{ Seed };
        for (uint64_t Key : Loop) WalkFaceRingGeometric(Index, Cluster, Ring, Seed, Key);
        return Ring;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ResetSelectionState(SelectionState& State)
{
    State.Faces.clear();
    State.Vertices.clear();
    State.Edges.clear();
    State.LastPick = SelectionAnchor{};
}

void ResolveClickSelection(SelectionState& State,
                           SelectionMode   Mode,
                           uint32_t        Component,
                           uint64_t        EdgeComponent,
                           bool            Additive)
{
    if (!Additive)
    {
        State.Faces.clear();
        State.Vertices.clear();
        State.Edges.clear();
    }

    if (Mode == VertexMode)
    {
        if (Additive && State.Vertices.count(Component)) State.Vertices.erase(Component);
        else                                             State.Vertices.insert(Component);
        State.LastPick = SelectionAnchor{ true, VertexMode, Component, 0 };
    }
    else if (Mode == EdgeMode)
    {
        if (Additive && State.Edges.count(EdgeComponent)) State.Edges.erase(EdgeComponent);
        else                                              State.Edges.insert(EdgeComponent);
        State.LastPick = SelectionAnchor{ true, EdgeMode, 0, EdgeComponent };
    }
    else   // FaceMode / ObjectMode both operate on faces here
    {
        if (Additive && State.Faces.count(Component)) State.Faces.erase(Component);
        else                                          State.Faces.insert(Component);
        State.LastPick = SelectionAnchor{ true, FaceMode, Component, 0 };
    }
}

void ResolveRegionSelectionMerge(SelectionState&                      State,
                                 SelectionMode                        Mode,
                                 const std::unordered_set<uint32_t>&  HitVertices,
                                 const std::unordered_set<uint64_t>&  HitEdges,
                                 const std::unordered_set<uint32_t>&  HitFaces,
                                 bool                                 Additive,
                                 bool                                 Subtractive)
{
    // 📝 Region set semantics mirror ResolveClickSelection: plain replaces (clear then add), Shift adds, Ctrl removes. Only
    //    the active mode's set is touched, and LastPick is left alone — a swept region has no single anchor to seed a loop from.
    if (Mode == ObjectMode) return;

    if (Mode == VertexMode)
    {
        if (!Additive && !Subtractive) State.Vertices.clear();
        if (Subtractive) for (uint32_t Vertex : HitVertices) State.Vertices.erase(Vertex);
        else             State.Vertices.insert(HitVertices.begin(), HitVertices.end());
    }
    else if (Mode == EdgeMode)
    {
        if (!Additive && !Subtractive) State.Edges.clear();
        if (Subtractive) for (uint64_t Edge : HitEdges) State.Edges.erase(Edge);
        else             State.Edges.insert(HitEdges.begin(), HitEdges.end());
    }
    else   // FaceMode / IslandMode both operate on the face set
    {
        if (!Additive && !Subtractive) State.Faces.clear();
        if (Subtractive) for (uint32_t Face : HitFaces) State.Faces.erase(Face);
        else             State.Faces.insert(HitFaces.begin(), HitFaces.end());
    }
}

void ResolveEdgeLoopSelection(SelectionState&       State,
                              const AdjacencyIndex&  Index,
                              const PolygonCluster&  Cluster,
                              uint64_t               SeedEdge,
                              bool                   Additive)
{
    // 📝 Grow the loop, then MERGE it into the edge set when Additive (Shift held) so a second Shift+loop-click keeps the
    //    first loop; a plain loop-click replaces (the other-mode sets always clear — a loop is an edge-mode gesture).
    const std::unordered_set<uint64_t> Loop = ComputeEdgeLoop(Index, Cluster, SeedEdge);
    if (!Additive) State.Edges.clear();
    State.Edges.insert(Loop.begin(), Loop.end());
    State.Faces.clear();
    State.Vertices.clear();
    State.LastPick = SelectionAnchor{ true, EdgeMode, 0, SeedEdge };
}

void ResolveFaceLoopSelection(SelectionState&       State,
                              const AdjacencyIndex&  Index,
                              const PolygonCluster&  Cluster,
                              uint32_t               SeedFace,
                              bool                   Additive)
{
    // 📝 Grow the band, then MERGE it into the face set when Additive (Shift held) so a second Shift+loop-click keeps the
    //    first band; a plain loop-click replaces (the other-mode sets always clear — a ring is a face-mode gesture).
    const std::unordered_set<uint32_t> Band = ComputeFaceBand(Index, Cluster, SeedFace);
    if (!Additive) State.Faces.clear();
    State.Faces.insert(Band.begin(), Band.end());
    State.Edges.clear();
    State.Vertices.clear();
    State.LastPick = SelectionAnchor{ true, FaceMode, SeedFace, 0 };
}

std::vector<uint32_t> ResolveIslandFaces(const AdjacencyIndex&  Index,
                                         const PolygonCluster&  Cluster,
                                         uint32_t               SeedFace)
{
    // 📝 Flood from SeedFace across interior, non-seam loop edges. A boundary edge (one incident face) is a hard island edge;
    //    a seam edge stops the flood so the island is exactly one UV chart. When the cluster has no per-corner UVs the seam
    //    test cannot run, so every interior edge is crossable and the result degrades to a topological connected component.
    const bool SeamTestEnabled = !Cluster.FaceCornerTexture.empty();
    const std::vector<uint32_t> CornerBases = SeamTestEnabled ? ResolveFaceCornerBases(Cluster) : std::vector<uint32_t>{};

    std::unordered_set<uint32_t> Island;
    Island.insert(SeedFace);
    std::vector<uint32_t> Stack{ SeedFace };
    while (!Stack.empty())
    {
        const uint32_t Face = Stack.back();
        Stack.pop_back();
        if (Face >= Index.FaceEdgeKeys.size()) continue;
        for (uint64_t EdgeKey : Index.FaceEdgeKeys[Face])
        {
            const uint32_t Across = ResolveFaceAcrossEdge(Index, EdgeKey, Face);
            if (Across == InvalidFaceOrdinal) continue;                                        // boundary — hard island edge
            if (Island.count(Across)) continue;
            if (SeamTestEnabled && ResolveEdgeIsSeam(Cluster, CornerBases, Index, EdgeKey, Face, Across)) continue;
            Island.insert(Across);
            Stack.push_back(Across);
        }
    }
    return std::vector<uint32_t>(Island.begin(), Island.end());
}

void ResolveIslandSelection(SelectionState&        State,
                            const AdjacencyIndex&   Index,
                            const PolygonCluster&   Cluster,
                            uint32_t                SeedFace,
                            bool                    Additive)
{
    // 📝 Grow the island, then MERGE it into the face set when Additive (Shift held) so a second Shift+click keeps the first
    //    island; a plain click replaces (the other-mode sets always clear — an island is a face-pool gesture).
    const std::vector<uint32_t> Island = ResolveIslandFaces(Index, Cluster, SeedFace);
    if (!Additive) State.Faces.clear();
    State.Faces.insert(Island.begin(), Island.end());
    State.Edges.clear();
    State.Vertices.clear();
    State.LastPick = SelectionAnchor{ true, FaceMode, SeedFace, 0 };
}

uint32_t ResolveLinkedSelection(SelectionState& State, const AdjacencyIndex& Index, SelectionMode Mode)
{
    // seeds = the vertices of the current selection (the port of seedVertsFromSelection)
    std::unordered_set<uint32_t> Seeds;
    if (Mode == VertexMode)
    {
        for (uint32_t Vertex : State.Vertices) Seeds.insert(Vertex);
    }
    else if (Mode == EdgeMode)
    {
        for (uint64_t Key : State.Edges)
        {
            auto Endpoints = Index.EdgeVertices.find(Key);
            if (Endpoints == Index.EdgeVertices.end()) continue;
            Seeds.insert(Endpoints->second.LowerVertex);
            Seeds.insert(Endpoints->second.HigherVertex);
        }
    }
    else
    {
        for (uint32_t Face : State.Faces)
            if (Face < Index.FaceVertexLoops.size())
                for (uint32_t Vertex : Index.FaceVertexLoops[Face]) Seeds.insert(Vertex);
    }
    if (Seeds.empty()) return 0;

    const std::unordered_set<uint32_t> Component = FloodConnectedVertices(Index, Seeds);

    if (Mode == VertexMode)
    {
        for (uint32_t Vertex : Component) State.Vertices.insert(Vertex);
    }
    else if (Mode == EdgeMode)
    {
        for (uint64_t Key : Index.EdgeKeys)
        {
            auto Endpoints = Index.EdgeVertices.find(Key);
            if (Endpoints == Index.EdgeVertices.end()) continue;
            if (Component.count(Endpoints->second.LowerVertex) && Component.count(Endpoints->second.HigherVertex))
                State.Edges.insert(Key);
        }
    }
    else
    {
        for (uint32_t Face = 0; Face < Index.FaceCount; ++Face)
        {
            bool AllInside = true;
            for (uint32_t Vertex : Index.FaceVertexLoops[Face])
                if (!Component.count(Vertex)) { AllInside = false; break; }
            if (AllInside) State.Faces.insert(Face);
        }
    }
    return (uint32_t)Component.size();
}

void ResolveSelectAll(SelectionState& State, const AdjacencyIndex& Index, SelectionMode Mode)
{
    if (Mode == VertexMode)
    {
        for (uint32_t Vertex = 0; Vertex < Index.VertexAdjacency.size(); ++Vertex) State.Vertices.insert(Vertex);
    }
    else if (Mode == EdgeMode)
    {
        for (uint64_t Key : Index.EdgeKeys) State.Edges.insert(Key);
    }
    else
    {
        for (uint32_t Face = 0; Face < Index.FaceCount; ++Face) State.Faces.insert(Face);
    }
}

void ResolveLoopPreview(SelectionPreview&      Preview,
                        const AdjacencyIndex&   Index,
                        const PolygonCluster&   Cluster,
                        SelectionMode           Mode,
                        bool                    HoverValid,
                        uint64_t                HoverEdge,
                        uint32_t                HoverFace)
{
    Preview = SelectionPreview{};
    if (!HoverValid) return;

    if (Mode == EdgeMode)
    {
        Preview.Edges    = ComputeEdgeLoop(Index, Cluster, HoverEdge);
        Preview.SeedEdge = HoverEdge;
        Preview.Enabled  = !Preview.Edges.empty();
    }
    else if (Mode == FaceMode)
    {
        Preview.Faces   = ComputeFaceBand(Index, Cluster, HoverFace);
        Preview.Enabled = !Preview.Faces.empty();
    }
}

void ResolveGrowSelection(SelectionState& State, const AdjacencyIndex& Index, SelectionMode Mode)
{
    // 📝 The port of selGrow — add the one-ring adjacency of every currently selected component (Blender Ctrl+NumpadPlus). Snapshot
    //    the additions first so a growth does not feed on itself within one pass (a single ring, not a flood). Vertex: one-ring
    //    adjacency. Edge: every loop edge of every face touching either endpoint. Face / Island / Object: edge-adjacent faces.
    if (Mode == VertexMode)
    {
        std::vector<uint32_t> Additions;
        for (uint32_t Vertex : State.Vertices)
            if (Vertex < Index.VertexAdjacency.size())
                for (uint32_t Adjacent : Index.VertexAdjacency[Vertex]) Additions.push_back(Adjacent);
        for (uint32_t Vertex : Additions) State.Vertices.insert(Vertex);
    }
    else if (Mode == EdgeMode)
    {
        std::vector<uint64_t> Additions;
        for (uint64_t Key : State.Edges)
        {
            auto Endpoints = Index.EdgeVertices.find(Key);
            if (Endpoints == Index.EdgeVertices.end()) continue;
            for (uint32_t Vertex : { Endpoints->second.LowerVertex, Endpoints->second.HigherVertex })
            {
                if (Vertex >= Index.VertexFaces.size()) continue;
                for (uint32_t Face : Index.VertexFaces[Vertex])
                    if (Face < Index.FaceEdgeKeys.size())
                        for (uint64_t EdgeKey : Index.FaceEdgeKeys[Face]) Additions.push_back(EdgeKey);
            }
        }
        for (uint64_t Key : Additions) State.Edges.insert(Key);
    }
    else   // Face / Island / Object — face-set gestures
    {
        std::vector<uint32_t> Additions;
        for (uint32_t Face : State.Faces)
            for (uint32_t Adjacent : EvaluateFaceAdjacency(Index, Face)) Additions.push_back(Adjacent);
        for (uint32_t Face : Additions) State.Faces.insert(Face);
    }
}

void ResolveShrinkSelection(SelectionState& State, const AdjacencyIndex& Index, SelectionMode Mode)
{
    // 📝 The port of selShrink — peel off every boundary component (one that touches an UNselected adjacent), Blender
    //    Ctrl+NumpadMinus. Collect the boundary components against the CURRENT set first, then remove them together so the erosion is
    //    one ring deep, not a cascade. Mirrors the grow adjacency exactly so grow-then-shrink round-trips a solid interior.
    if (Mode == VertexMode)
    {
        std::vector<uint32_t> Removals;
        for (uint32_t Vertex : State.Vertices)
        {
            if (Vertex >= Index.VertexAdjacency.size()) continue;
            for (uint32_t Adjacent : Index.VertexAdjacency[Vertex])
                if (!State.Vertices.count(Adjacent)) { Removals.push_back(Vertex); break; }
        }
        for (uint32_t Vertex : Removals) State.Vertices.erase(Vertex);
    }
    else if (Mode == EdgeMode)
    {
        std::vector<uint64_t> Removals;
        for (uint64_t Key : State.Edges)
        {
            auto Endpoints = Index.EdgeVertices.find(Key);
            if (Endpoints == Index.EdgeVertices.end()) continue;
            bool Boundary = false;
            for (uint32_t Vertex : { Endpoints->second.LowerVertex, Endpoints->second.HigherVertex })
            {
                if (Vertex >= Index.VertexFaces.size()) continue;
                for (uint32_t Face : Index.VertexFaces[Vertex])
                {
                    if (Face >= Index.FaceEdgeKeys.size()) continue;
                    for (uint64_t EdgeKey : Index.FaceEdgeKeys[Face])
                        if (EdgeKey != Key && !State.Edges.count(EdgeKey)) { Boundary = true; break; }
                    if (Boundary) break;
                }
                if (Boundary) break;
            }
            if (Boundary) Removals.push_back(Key);
        }
        for (uint64_t Key : Removals) State.Edges.erase(Key);
    }
    else   // Face / Island / Object — face-set gestures
    {
        std::vector<uint32_t> Removals;
        for (uint32_t Face : State.Faces)
            for (uint32_t Adjacent : EvaluateFaceAdjacency(Index, Face))
                if (!State.Faces.count(Adjacent)) { Removals.push_back(Face); break; }
        for (uint32_t Face : Removals) State.Faces.erase(Face);
    }
}

} // namespace Frontier
