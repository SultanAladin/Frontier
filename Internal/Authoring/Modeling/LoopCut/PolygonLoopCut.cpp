/*============================================================================================================================================
                                                              POLYGONLOOPCUT.CPP
============================================================================================================================================*/
// 🧩 Loop cut. An edge->faces map drives a bidirectional walk of the quad ring containing the seed edge: from a ring edge, cross
//    the adjacent quad to its opposite edge (the rails being the quad's two side edges), hop across that edge to the next face,
//    and repeat — stopping when the ring closes, reaches a boundary, or meets a non-quad. Each ring edge mints CutCount evenly
//    spaced loop vertices; a crossed quad rewrites into CutCount+1 quad strips along its two ring edges; a non-crossed face
//    touching a ring edge has every cut point spliced into its corner list so the cut leaves no T-junction. Edge orientation is
//    propagated from the seed (A's rail stays A's rail) so every loop vertex's slide direction points to the same rail and the
//    whole loop slides coherently. Live-native port of the retired Polygon/Operations/LoopCutOperation kernel, writing back into
//    the PolygonCluster (positions + face stream + per-corner UVs) instead of standalone OUT arrays.

#include "PolygonLoopCut.h"

#include "PolygonCluster.h"
#include "AdjacencyIndex.h"   // 📝 EncodeEdgeKey — the live packed-edge-key encoder (min << 32 | max), shared with the picker.
#include "VertexField.h"      // 📝 AccumulateVertexPosition / EvaluateVertexCount / RefreshVertex* — append a loop vertex.

#include <algorithm>
#include <unordered_map>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr uint32_t InvalidCorner = 0xFFFFFFFFu;   // [-] - "vertex not present in this face" sentinel

    // 📝 A ring edge oriented by its rails: VertexA stays on the seed-A side around the whole ring, VertexB on the seed-B side,
    //    so the slide direction (B - A) is coherent for every loop vertex.
    struct OrientedEdge
    {
        uint32_t VertexA = 0;
        uint32_t VertexB = 0;
    };

    // 📝 Position of Vertex within a face's corner list, or InvalidCorner if absent.
    uint32_t FindCorner(const std::vector<uint32_t>& Corners, uint32_t Vertex)
    {
        for (uint32_t Index = 0; Index < (uint32_t)Corners.size(); ++Index)
            if (Corners[Index] == Vertex) return Index;
        return InvalidCorner;
    }

    // 📝 Cross a quad: given a ring edge oriented (A,B) that is one edge of the quad, resolve the opposite edge oriented so A's
    //    rail-partner is the new A. The rail-partner of a corner is its quad adjacent that is NOT the edge's other endpoint.
    //    Returns false if (A,B) is not an edge of this quad.
    bool ResolveOppositeQuadEdge(const std::vector<uint32_t>& Quad, const OrientedEdge& In, OrientedEdge& Out)
    {
        if (Quad.size() != 4) return false;
        const uint32_t PositionA = FindCorner(Quad, In.VertexA);
        const uint32_t PositionB = FindCorner(Quad, In.VertexB);
        if (PositionA == InvalidCorner || PositionB == InvalidCorner) return false;

        const uint32_t PrevA = Quad[(PositionA + 3) % 4];
        const uint32_t NextA = Quad[(PositionA + 1) % 4];
        const uint32_t PrevB = Quad[(PositionB + 3) % 4];
        const uint32_t NextB = Quad[(PositionB + 1) % 4];
        // A,B must be adjacent in the quad for the edge to be real.
        if (PrevA != In.VertexB && NextA != In.VertexB) return false;

        Out.VertexA = (PrevA == In.VertexB) ? NextA : PrevA;   // A's adjacent that isn't B
        Out.VertexB = (PrevB == In.VertexA) ? NextB : PrevB;   // B's adjacent that isn't A
        return true;
    }

    // 📝 The shared ring walk behind both ConstructEdgeLoopCut and ResolveLoopCutPreview. Unpacks the flat faces to per-face
    //    corner lists + an edge->faces map (①), then walks the quad ring both ways from the seed edge (②): from a ring edge,
    //    cross the adjacent quad to its opposite edge, hop across that edge to the next face, repeat — stopping at a closed ring,
    //    a boundary, or a non-quad. Outputs the corner lists + edge->faces map, the deduped rail-oriented ring edges, and the
    //    crossed-quad mask. Returns false (nothing populated meaningfully) when the seed pair is not an edge of the polygon or
    //    borders no quad — the no-op condition both callers report.
    bool WalkLoopCutRing(const std::vector<uint32_t>&                          FaceVertexIndices,
                         const std::vector<uint32_t>&                          FaceVertexCounts,
                         uint32_t                                              SeedVertexA,
                         uint32_t                                              SeedVertexB,
                         std::vector<std::vector<uint32_t>>&                   OutFaceCorners,
                         std::unordered_map<uint64_t, std::vector<uint32_t>>&  OutEdgeFaces,
                         std::unordered_map<uint64_t, OrientedEdge>&           OutRingEdges,
                         std::vector<bool>&                                    OutCrossedFaces)
    {
        const uint32_t FaceCount = (uint32_t)FaceVertexCounts.size();

        // ① Unpack faces to per-face corner lists + an edge->faces adjacency map.
        OutFaceCorners.assign(FaceCount, {});
        OutEdgeFaces.clear();
        {
            uint32_t CornerCursor = 0;
            for (uint32_t Face = 0; Face < FaceCount; ++Face)
            {
                const uint32_t CornerCount = FaceVertexCounts[Face];
                OutFaceCorners[Face].resize(CornerCount);
                for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
                    OutFaceCorners[Face][Corner] = FaceVertexIndices[CornerCursor + Corner];
                for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
                {
                    const uint32_t Origin   = OutFaceCorners[Face][Corner];
                    const uint32_t Terminus = OutFaceCorners[Face][(Corner + 1) % CornerCount];
                    OutEdgeFaces[EncodeEdgeKey(Origin, Terminus)].push_back(Face);
                }
                CornerCursor += CornerCount;
            }
        }

        const uint64_t SeedKey = EncodeEdgeKey(SeedVertexA, SeedVertexB);
        OutRingEdges.clear();
        OutCrossedFaces.assign(FaceCount, false);
        if (OutEdgeFaces.find(SeedKey) == OutEdgeFaces.end())
            return false;   // seed pair is not an edge of the polygon → no-op

        // ② Walk the ring in both directions from the seed edge. OutRingEdges maps each ring edge key to its rail orientation;
        //    OutCrossedFaces marks the quads that split.
        OutRingEdges.emplace(SeedKey, OrientedEdge{ SeedVertexA, SeedVertexB });
        for (uint32_t StartFace : OutEdgeFaces[SeedKey])
        {
            OrientedEdge CurrentEdge = { SeedVertexA, SeedVertexB };
            uint32_t     CurrentFace = StartFace;
            while (true)
            {
                if (CurrentFace >= FaceCount || OutFaceCorners[CurrentFace].size() != 4) break;   // non-quad terminus
                if (OutCrossedFaces[CurrentFace]) break;                                            // ring closed
                OutCrossedFaces[CurrentFace] = true;

                OrientedEdge OppositeEdge;
                if (!ResolveOppositeQuadEdge(OutFaceCorners[CurrentFace], CurrentEdge, OppositeEdge)) break;
                const uint64_t OppositeKey = EncodeEdgeKey(OppositeEdge.VertexA, OppositeEdge.VertexB);
                if (OutRingEdges.find(OppositeKey) == OutRingEdges.end())
                    OutRingEdges.emplace(OppositeKey, OppositeEdge);

                // Hop across the opposite edge to the next face (the one that isn't the face we just crossed).
                const std::vector<uint32_t>& Across = OutEdgeFaces[OppositeKey];
                uint32_t NextFace = InvalidCorner;
                for (uint32_t Candidate : Across)
                    if (Candidate != CurrentFace) { NextFace = Candidate; break; }
                if (NextFace == InvalidCorner) break;   // boundary edge: ring ends here

                CurrentEdge = OppositeEdge;
                CurrentFace = NextFace;
            }
        }

        uint32_t CrossedTotal = 0;
        for (bool Crossed : OutCrossedFaces) if (Crossed) ++CrossedTotal;
        return CrossedTotal != 0;   // seed touched no quad → nothing to cut
    }

    // 📝 The running corner-slice offset of each face (prefix sum of FaceVertexCounts), so a face ordinal maps to its first
    //    corner in the flat FaceVertexIndices / FaceCornerTexture arrays — the same mapping Extrude uses to read corner UVs.
    std::vector<uint32_t> ResolveFaceCornerOffsets(const std::vector<uint32_t>& FaceVertexCounts)
    {
        std::vector<uint32_t> Offsets;
        Offsets.reserve(FaceVertexCounts.size());
        uint32_t Cursor = 0;
        for (uint32_t Count : FaceVertexCounts)
        {
            Offsets.push_back(Cursor);
            Cursor += Count;
        }
        return Offsets;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructEdgeLoopCut(PolygonCluster& Target,
                          uint32_t        SeedVertexA,
                          uint32_t        SeedVertexB,
                          uint32_t        CutCount,
                          LoopCutOutcome& Outcome)
{
    Outcome = LoopCutOutcome{};

    if (CutCount < 1) CutCount = 1;
    const uint32_t FaceCount = (uint32_t)Target.FaceVertexCounts.size();
    const double   Divisions = (double)(CutCount + 1);

    // ①+② Build the corner lists + edge->faces map and walk the quad ring from the seed (shared with the preview resolver).
    std::vector<std::vector<uint32_t>> FaceCorners;
    std::unordered_map<uint64_t, std::vector<uint32_t>> EdgeFaces;
    std::unordered_map<uint64_t, OrientedEdge> RingEdges;
    std::vector<bool> CrossedFaces;
    if (!WalkLoopCutRing(Target.FaceVertexIndices, Target.FaceVertexCounts, SeedVertexA, SeedVertexB,
                         FaceCorners, EdgeFaces, RingEdges, CrossedFaces))
        return false;   // seed not an edge, or it touched no quad → nothing to cut

    const bool CarriesTexture = !Target.FaceCornerTexture.empty();
    const std::vector<uint32_t> CornerOffsets = ResolveFaceCornerOffsets(Target.FaceVertexCounts);

    // 📝 The corner UV of vertex Vertex within face Face (empty-UV cluster → zero). Reads the flat FaceCornerTexture through the
    //    face's slice offset, exactly as Extrude does. A cut point spliced into a face interpolates between the two endpoint
    //    corner UVs of the ring edge IN THAT FACE, so a seam vertex keeps a distinct UV per side.
    auto ResolveCornerTexture = [&](uint32_t Face, uint32_t Vertex) -> Vector2d
    {
        if (!CarriesTexture) return Vector2d{ 0.0, 0.0 };
        const uint32_t Offset      = CornerOffsets[Face];
        const uint32_t CornerCount = Target.FaceVertexCounts[Face];
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
            if (Target.FaceVertexIndices[Offset + Corner] == Vertex)
                return Target.FaceCornerTexture[Offset + Corner];
        return Vector2d{ 0.0, 0.0 };
    };

    // ③ Mint CutCount evenly-spaced loop vertices per ring edge (param (i+1)/(CutCount+1) from VertexA toward VertexB) and
    //    record each as a slide sample. Direction = (B - A)/(CutCount+1), so Slide ∈ [-1,1] shifts the whole bundle by one
    //    division; Slide 0 is the even rest position the vertices are minted at.
    std::unordered_map<uint64_t, std::vector<uint32_t>> EdgeCutSlots;   // ring edge key → cut slots ordered VertexA → VertexB
    for (const auto& Entry : RingEdges)
    {
        const OrientedEdge& Edge = Entry.second;
        const Vector3d PositionA = Target.Attributes.Position[Edge.VertexA];
        const Vector3d PositionB = Target.Attributes.Position[Edge.VertexB];
        const Vector3d Span      = SubtractVector(PositionB, PositionA);
        const Vector3d Direction = ScaleVector(Span, 1.0 / Divisions);
        std::vector<uint32_t>& Slots = EdgeCutSlots[Entry.first];
        Slots.resize(CutCount);
        for (uint32_t Cut = 0; Cut < CutCount; ++Cut)
        {
            const double  Parameter = (double)(Cut + 1) / Divisions;
            const Vector3d Base      = AddVector(PositionA, ScaleVector(Span, Parameter));
            const uint32_t Slot     = AccumulateVertexPosition(Target.Attributes, Base);
            // Attribute-complete the clone at rest so it shades / paints like the edge it split (Extrude's CloneCapVertex rule).
            if (!Target.Attributes.Normal.empty())
                RefreshVertexNormal(Target.Attributes, Slot, Target.Attributes.Normal[Edge.VertexA]);
            if (!Target.Attributes.TextureCoordinate.empty())
                RefreshVertexTexture(Target.Attributes, Slot,
                                     AddVector(ScaleVector(Target.Attributes.TextureCoordinate[Edge.VertexA], 1.0 - Parameter),
                                               ScaleVector(Target.Attributes.TextureCoordinate[Edge.VertexB], Parameter)));
            if (!Target.Attributes.Color.empty())
                RefreshVertexColor(Target.Attributes, Slot, Target.Attributes.Color[Edge.VertexA]);
            Slots[Cut] = Slot;
            Outcome.OffsetSamples.push_back(LoopCutOffsetSample{ Slot, Direction, Base });
            Outcome.LoopVertices.insert(Slot);
        }
    }

    // 📝 The cut slots of a ring edge ordered from a given start vertex: stored VertexA → VertexB, reversed when the caller walks
    //    the edge from VertexB, so a quad / face always reads them in its own traversal direction.
    auto OrientedCutSlots = [&](uint64_t Key, uint32_t StartVertex, std::vector<uint32_t>& Out)
    {
        const std::vector<uint32_t>& Slots = EdgeCutSlots[Key];
        Out = Slots;
        if (RingEdges[Key].VertexA != StartVertex)
            std::reverse(Out.begin(), Out.end());
    };

    // The consecutive loop edges the inserted cuts form, so the follow-on selection + overlay track exactly the new loop.
    auto RecordLoopEdge = [&](uint32_t First, uint32_t Second)
    {
        Outcome.LoopEdges.insert(EncodeEdgeKey(First, Second));
    };

    // ④ Rewrite the face stream into fresh arrays. A crossed quad becomes CutCount+1 quad strips between its two opposite ring
    //    edges; any other face splices every cut point of each ring edge it carries into its corner list (verbatim when none).
    std::vector<uint32_t> RewriteIndices;
    std::vector<uint32_t> RewriteCounts;
    std::vector<Vector2d> RewriteTexture;

    auto AppendCorner = [&](uint32_t Face, uint32_t Vertex, const Vector2d& OverrideTexture, bool UseOverride)
    {
        RewriteIndices.push_back(Vertex);
        if (CarriesTexture)
            RewriteTexture.push_back(UseOverride ? OverrideTexture : ResolveCornerTexture(Face, Vertex));
    };

    for (uint32_t Face = 0; Face < FaceCount; ++Face)
    {
        const std::vector<uint32_t>& Corners = FaceCorners[Face];

        if (CrossedFaces[Face])
        {
            // Find the first edge of the quad that carries cut points; its opposite edge carries the matching set.
            uint32_t SplitPosition = InvalidCorner;
            for (uint32_t Corner = 0; Corner < 4; ++Corner)
            {
                const uint64_t Key = EncodeEdgeKey(Corners[Corner], Corners[(Corner + 1) % 4]);
                if (EdgeCutSlots.find(Key) != EdgeCutSlots.end()) { SplitPosition = Corner; break; }
            }
            if (SplitPosition != InvalidCorner)
            {
                // Near edge C0→C1, far edge C3→C2 (the rails are C0-C3 and C1-C2). Cut points of each edge ordered from the rail
                //    they share with strip 0 (C0 / C3), so NearCuts[i] and FarCuts[i] are the two ends of one loop edge.
                const uint32_t C0 = Corners[SplitPosition];
                const uint32_t C1 = Corners[(SplitPosition + 1) % 4];
                const uint32_t C2 = Corners[(SplitPosition + 2) % 4];
                const uint32_t C3 = Corners[(SplitPosition + 3) % 4];
                std::vector<uint32_t> NearCuts, FarCuts;
                OrientedCutSlots(EncodeEdgeKey(C0, C1), C0, NearCuts);
                OrientedCutSlots(EncodeEdgeKey(C2, C3), C3, FarCuts);

                // Per-face corner UVs of the four quad corners, so each strip's cut corners interpolate within THIS face.
                const Vector2d T0 = ResolveCornerTexture(Face, C0);
                const Vector2d T1 = ResolveCornerTexture(Face, C1);
                const Vector2d T2 = ResolveCornerTexture(Face, C2);
                const Vector2d T3 = ResolveCornerTexture(Face, C3);

                uint32_t PrevNear = C0, PrevFar = C3;
                Vector2d PrevNearTexture = T0, PrevFarTexture = T3;
                for (uint32_t Cut = 0; Cut < CutCount; ++Cut)
                {
                    const double  Parameter = (double)(Cut + 1) / Divisions;
                    const Vector2d NearTexture = AddVector(ScaleVector(T0, 1.0 - Parameter), ScaleVector(T1, Parameter));
                    const Vector2d FarTexture  = AddVector(ScaleVector(T3, 1.0 - Parameter), ScaleVector(T2, Parameter));

                    RewriteCounts.push_back(4);
                    AppendCorner(Face, PrevNear,     PrevNearTexture, true);
                    AppendCorner(Face, NearCuts[Cut], NearTexture,    true);
                    AppendCorner(Face, FarCuts[Cut],  FarTexture,     true);
                    AppendCorner(Face, PrevFar,      PrevFarTexture,  true);

                    RecordLoopEdge(NearCuts[Cut], FarCuts[Cut]);   // the loop edge this strip boundary forms
                    PrevNear = NearCuts[Cut]; PrevNearTexture = NearTexture;
                    PrevFar  = FarCuts[Cut];  PrevFarTexture  = FarTexture;
                }
                // Final strip up to the far rail (C1, C2).
                RewriteCounts.push_back(4);
                AppendCorner(Face, PrevNear, PrevNearTexture, true);
                AppendCorner(Face, C1,       T1,              true);
                AppendCorner(Face, C2,       T2,              true);
                AppendCorner(Face, PrevFar,  PrevFarTexture,  true);
                continue;
            }
            // Fallback (a crossed quad with no resolvable cut edge shouldn't occur): emit verbatim below.
        }

        // Non-crossed (or fallback): rebuild corners, splicing in every cut point on any ring edge (in walk order).
        const uint32_t CornerCount = (uint32_t)Corners.size();
        std::vector<uint32_t> Rebuilt;
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
        {
            Rebuilt.push_back(Corners[Corner]);
            const uint64_t Key = EncodeEdgeKey(Corners[Corner], Corners[(Corner + 1) % CornerCount]);
            if (EdgeCutSlots.find(Key) != EdgeCutSlots.end())
            {
                std::vector<uint32_t> Cuts;
                OrientedCutSlots(Key, Corners[Corner], Cuts);
                for (uint32_t Slot : Cuts) Rebuilt.push_back(Slot);
            }
        }
        RewriteCounts.push_back((uint32_t)Rebuilt.size());
        for (uint32_t Vertex : Rebuilt)
            AppendCorner(Face, Vertex, Vector2d{ 0.0, 0.0 }, false);
    }

    Target.FaceVertexIndices = RewriteIndices;
    Target.FaceVertexCounts  = RewriteCounts;
    if (CarriesTexture) Target.FaceCornerTexture = RewriteTexture;

    Outcome.Completed = true;
    return true;
}

bool ResolveLoopCutPreview(const PolygonCluster&        Target,
                           uint32_t                     SeedVertexA,
                           uint32_t                     SeedVertexB,
                           uint32_t                     CutCount,
                           std::vector<LoopCutSegment>& OutSegments)
{
    OutSegments.clear();
    if (CutCount < 1) CutCount = 1;
    const double Divisions = (double)(CutCount + 1);

    std::vector<std::vector<uint32_t>> FaceCorners;
    std::unordered_map<uint64_t, std::vector<uint32_t>> EdgeFaces;
    std::unordered_map<uint64_t, OrientedEdge> RingEdges;
    std::vector<bool> CrossedFaces;
    if (!WalkLoopCutRing(Target.FaceVertexIndices, Target.FaceVertexCounts, SeedVertexA, SeedVertexB,
                         FaceCorners, EdgeFaces, RingEdges, CrossedFaces))
        return false;   // seed not an edge, or it touched no quad → no preview line

    // CutCount segments per crossed quad: for each cut level, the line between the matching points on the two opposite ring
    //    edges (the same points ConstructEdgeLoopCut would mint), so the preview traces every loop the confirm will insert.
    for (uint32_t Face = 0; Face < (uint32_t)CrossedFaces.size(); ++Face)
    {
        if (!CrossedFaces[Face] || FaceCorners[Face].size() != 4) continue;
        const std::vector<uint32_t>& Corners = FaceCorners[Face];
        uint32_t SplitPosition = InvalidCorner;
        for (uint32_t Corner = 0; Corner < 4; ++Corner)
        {
            const uint64_t Key = EncodeEdgeKey(Corners[Corner], Corners[(Corner + 1) % 4]);
            if (RingEdges.find(Key) != RingEdges.end()) { SplitPosition = Corner; break; }
        }
        if (SplitPosition == InvalidCorner) continue;   // a crossed quad with no resolvable ring edge shouldn't occur

        // Near edge C0→C1, far edge C3→C2; cut level i sits at param (i+1)/(CutCount+1) along each, from the shared rail side.
        const Vector3d C0 = Target.Attributes.Position[Corners[SplitPosition]];
        const Vector3d C1 = Target.Attributes.Position[Corners[(SplitPosition + 1) % 4]];
        const Vector3d C2 = Target.Attributes.Position[Corners[(SplitPosition + 2) % 4]];
        const Vector3d C3 = Target.Attributes.Position[Corners[(SplitPosition + 3) % 4]];
        for (uint32_t Cut = 0; Cut < CutCount; ++Cut)
        {
            const double Parameter = (double)(Cut + 1) / Divisions;
            LoopCutSegment Segment;
            Segment.MidpointA = AddVector(C0, ScaleVector(SubtractVector(C1, C0), Parameter));
            Segment.MidpointB = AddVector(C3, ScaleVector(SubtractVector(C2, C3), Parameter));
            OutSegments.push_back(Segment);
        }
    }
    return !OutSegments.empty();
}

bool LoopCutOffsetAdvance(PolygonCluster&                         Target,
                          const std::vector<LoopCutOffsetSample>& Samples,
                          double                                  Slide)
{
    const uint32_t VertexCount = EvaluateVertexCount(Target.Attributes);
    for (const LoopCutOffsetSample& Sample : Samples)
        if (Sample.NewVertex >= VertexCount) return false;

    for (const LoopCutOffsetSample& Sample : Samples)
        Target.Attributes.Position[Sample.NewVertex] = AddVector(Sample.OriginPosition, ScaleVector(Sample.Direction, Slide));
    return true;
}

} // namespace Frontier
