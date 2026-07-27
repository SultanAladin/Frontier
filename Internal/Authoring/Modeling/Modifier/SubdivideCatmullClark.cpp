/*============================================================================================================================================
                                                        SUBDIVIDECATMULLCLARK.CPP
============================================================================================================================================*/
// 🧩 Catmull-Clark, one level, on flat indexed-face arrays. The pass is: (1) build local edge adjacency from the faces, (2)
//    compute one face point per face (centroid), (3) compute one edge point per edge — smooth interior rule, boundary midpoint on
//    an open edge, blended toward the held midpoint by the edge's sharpness, (4) reposition every original vertex — interior
//    valence rule, boundary crease rule on the border, blended toward the held position by the vertex's crease weight, (5) append
//    each n-gon as its n corner quads (original-vertex, edge-point, face-point, prev-edge-point), sharing edge and vertex points
//    across faces so the refined polygon stays watertight.
// 📝 Sharpness handling follows the semi-sharp-crease idea (Hoppe et al.): a per-edge weight in [0,1] linearly interpolates the
//    edge point between fully smooth and its held midpoint, and a per-vertex crease weight (derived from how many sharp edges
//    meet there) interpolates the vertex between its smooth valence position and the crease/held position. This gives continuous
//    control of edge softness — the "more than Blender" lever, since Blender's crease is a single integer per edge.

#include "SubdivideCatmullClark.h"

#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Canonical undirected edge key — lower vertex index high, higher low — matching AdjacencyIndex::EncodeEdgeKey so the
    //    EdgeSharpness map the caller passes (keyed by that function) lines up with the keys minted here.
    uint64_t EncodeLocalEdgeKey(uint32_t FirstIndex, uint32_t SecondIndex)
    {
        const uint32_t LowerIndex  = FirstIndex < SecondIndex ? FirstIndex : SecondIndex;
        const uint32_t HigherIndex = FirstIndex < SecondIndex ? SecondIndex : FirstIndex;
        return ((uint64_t)HigherIndex << 32) | (uint64_t)LowerIndex;
    }

    // 📝 Per-edge working record built once from the faces: its two endpoints, the incident faces (1 = boundary, 2 = interior),
    //    its minted edge-point slot in the output, and its sharpness. Diagnostics keep it small and cache-friendly.
    struct EdgeRecord
    {
        uint32_t OriginVertex   = 0;             // [-]   - lower endpoint vertex index
        uint32_t TerminusVertex = 0;             // [-]   - higher endpoint vertex index
        uint32_t FirstFace      = 0xFFFFFFFFu;   // [-]   - one incident face ordinal
        uint32_t SecondFace     = 0xFFFFFFFFu;   // [-]   - other incident face ordinal (invalid = boundary edge)
        uint32_t EdgePointSlot  = 0xFFFFFFFFu;   // [-]   - output vertex slot of this edge's edge point
        float    Sharpness      = 0.0f;          // [0-1] - 0 fully smooth .. 1 fully hard (held midpoint)
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void SubdivideCatmullClarkLevel(const std::vector<Vector3d>&               Positions,
                                const std::vector<uint32_t>&               FaceVertexIndices,
                                const std::vector<uint32_t>&               FaceVertexCounts,
                                const std::unordered_map<uint64_t, float>& EdgeSharpness,
                                bool                                       BoundarySmoothEnabled,
                                std::vector<Vector3d>&                     OutPositions,
                                std::vector<uint32_t>&                     OutFaceVertexIndices,
                                std::vector<uint32_t>&                     OutFaceVertexCounts)
{
    OutPositions.clear();
    OutFaceVertexIndices.clear();
    OutFaceVertexCounts.clear();

    const uint32_t FaceCount   = (uint32_t)FaceVertexCounts.size();
    const uint32_t VertexCount = (uint32_t)Positions.size();

    // ① Locate each face's slice of the corner stream and its centroid (the face point). A degenerate face (< 3 corners) has no
    //    face point and is carried through untouched in step ⑤.
    std::vector<uint32_t> FaceCornerStart(FaceCount, 0);
    std::vector<Vector3d> FacePoint(FaceCount, Vector3d{ 0.0, 0.0, 0.0 });
    {
        uint32_t CornerCursor = 0;
        for (uint32_t Face = 0; Face < FaceCount; ++Face)
        {
            FaceCornerStart[Face] = CornerCursor;
            const uint32_t CornerCount = FaceVertexCounts[Face];
            if (CornerCount >= 3)
            {
                Vector3d CentroidSum = { 0.0, 0.0, 0.0 };
                for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
                    CentroidSum = AddVector(CentroidSum, Positions[FaceVertexIndices[CornerCursor + Corner]]);
                FacePoint[Face] = ScaleVector(CentroidSum, 1.0 / (double)CornerCount);
            }
            CornerCursor += CornerCount;
        }
    }

    // ② Build edge adjacency from the faces: one EdgeRecord per unique undirected edge, tagged with its incident faces and the
    //    caller's sharpness. Only faces with >= 3 corners contribute edges (a degenerate face is not part of the surface).
    std::unordered_map<uint64_t, uint32_t> EdgeSlotByKey;
    std::vector<EdgeRecord>                EdgeRecords;
    for (uint32_t Face = 0; Face < FaceCount; ++Face)
    {
        const uint32_t CornerCount = FaceVertexCounts[Face];
        if (CornerCount < 3) continue;
        const uint32_t CornerStart = FaceCornerStart[Face];
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
        {
            const uint32_t OriginIndex   = FaceVertexIndices[CornerStart + Corner];
            const uint32_t TerminusIndex = FaceVertexIndices[CornerStart + (Corner + 1) % CornerCount];
            const uint64_t EdgeKey = EncodeLocalEdgeKey(OriginIndex, TerminusIndex);
            auto Existing = EdgeSlotByKey.find(EdgeKey);
            if (Existing == EdgeSlotByKey.end())
            {
                EdgeRecord Record;
                Record.OriginVertex   = OriginIndex < TerminusIndex ? OriginIndex : TerminusIndex;
                Record.TerminusVertex = OriginIndex < TerminusIndex ? TerminusIndex : OriginIndex;
                Record.FirstFace      = Face;
                auto SharpnessEntry = EdgeSharpness.find(EdgeKey);
                if (SharpnessEntry != EdgeSharpness.end())
                    Record.Sharpness = SharpnessEntry->second < 0.0f ? 0.0f : (SharpnessEntry->second > 1.0f ? 1.0f : SharpnessEntry->second);
                EdgeSlotByKey.emplace(EdgeKey, (uint32_t)EdgeRecords.size());
                EdgeRecords.push_back(Record);
            }
            else if (EdgeRecords[Existing->second].SecondFace == 0xFFFFFFFFu)
            {
                EdgeRecords[Existing->second].SecondFace = Face;
            }
        }
    }

    // 📝 Per-vertex accumulators for the interior valence rule and for the boundary / crease rules. F = sum of incident face
    //    points, R = sum of incident edge midpoints, both averaged by their counts below. Boundary state remembers the two
    //    boundary-edge adjacents so the (Prev + 6P + Next)/8 crease rule can run. SharpEdgeCount decides the crease category.
    std::vector<Vector3d> IncidentFacePointSum(VertexCount, Vector3d{ 0.0, 0.0, 0.0 });
    std::vector<uint32_t> IncidentFaceCount(VertexCount, 0);
    std::vector<Vector3d> IncidentEdgeMidpointSum(VertexCount, Vector3d{ 0.0, 0.0, 0.0 });
    std::vector<uint32_t> IncidentEdgeCount(VertexCount, 0);
    std::vector<uint32_t> BoundaryAdjacentFirst(VertexCount, 0xFFFFFFFFu);
    std::vector<uint32_t> BoundaryAdjacentSecond(VertexCount, 0xFFFFFFFFu);
    std::vector<uint32_t> SharpEdgeCount(VertexCount, 0);
    std::vector<uint32_t> SharpAdjacentFirst(VertexCount, 0xFFFFFFFFu);
    std::vector<uint32_t> SharpAdjacentSecond(VertexCount, 0xFFFFFFFFu);

    // ③ Mint one edge point per edge (appended after the original vertices, which occupy slots 0..VertexCount-1) and fold each
    //    edge's contribution into its two endpoints' accumulators. The edge point blends the smooth rule toward the held midpoint
    //    by max(boundary, sharpness): a boundary or fully-sharp edge keeps its midpoint; a smooth interior edge averages the two
    //    endpoints with the two adjacent face points.
    OutPositions = Positions;   // original slots first; repositioned in step ④
    for (EdgeRecord& Record : EdgeRecords)
    {
        const Vector3d EndpointMidpoint = ScaleVector(AddVector(Positions[Record.OriginVertex], Positions[Record.TerminusVertex]), 0.5);
        const bool     BoundaryEdge     = (Record.SecondFace == 0xFFFFFFFFu);

        Vector3d SmoothEdgePoint = EndpointMidpoint;
        if (!BoundaryEdge)
        {
            // (V0 + V1 + F0 + F1) / 4 — the standard interior edge point.
            Vector3d EdgePointSum = AddVector(Positions[Record.OriginVertex], Positions[Record.TerminusVertex]);
            EdgePointSum = AddVector(EdgePointSum, FacePoint[Record.FirstFace]);
            EdgePointSum = AddVector(EdgePointSum, FacePoint[Record.SecondFace]);
            SmoothEdgePoint = ScaleVector(EdgePointSum, 0.25);
        }

        // A boundary edge is fully hard by construction; otherwise the sharpness sets the hardness. Blend smooth -> held midpoint.
        const double Hardness = BoundaryEdge ? 1.0 : (double)Record.Sharpness;
        const Vector3d EdgePoint = AddVector(ScaleVector(SmoothEdgePoint, 1.0 - Hardness),
                                             ScaleVector(EndpointMidpoint, Hardness));

        Record.EdgePointSlot = (uint32_t)OutPositions.size();
        OutPositions.push_back(EdgePoint);

        // Fold into endpoint accumulators (used only by the interior vertex rule; F/R average the incident face/edge data).
        IncidentEdgeMidpointSum[Record.OriginVertex]   = AddVector(IncidentEdgeMidpointSum[Record.OriginVertex], EndpointMidpoint);
        IncidentEdgeMidpointSum[Record.TerminusVertex] = AddVector(IncidentEdgeMidpointSum[Record.TerminusVertex], EndpointMidpoint);
        ++IncidentEdgeCount[Record.OriginVertex];
        ++IncidentEdgeCount[Record.TerminusVertex];

        // Boundary bookkeeping — a boundary vertex has exactly two boundary edges; remember the opposite endpoint of each.
        if (BoundaryEdge)
        {
            uint32_t* FirstSlotOrigin  = &BoundaryAdjacentFirst[Record.OriginVertex];
            uint32_t* SecondSlotOrigin = &BoundaryAdjacentSecond[Record.OriginVertex];
            if (*FirstSlotOrigin == 0xFFFFFFFFu) *FirstSlotOrigin = Record.TerminusVertex; else *SecondSlotOrigin = Record.TerminusVertex;
            uint32_t* FirstSlotTerm  = &BoundaryAdjacentFirst[Record.TerminusVertex];
            uint32_t* SecondSlotTerm = &BoundaryAdjacentSecond[Record.TerminusVertex];
            if (*FirstSlotTerm == 0xFFFFFFFFu) *FirstSlotTerm = Record.OriginVertex; else *SecondSlotTerm = Record.OriginVertex;
        }

        // Sharp-edge bookkeeping for the crease-vertex rule (a semi-sharp edge counts as sharp for categorizing the vertex).
        if (Record.Sharpness > 0.0f || BoundaryEdge)
        {
            ++SharpEdgeCount[Record.OriginVertex];
            ++SharpEdgeCount[Record.TerminusVertex];
            uint32_t* FirstSlotOrigin  = &SharpAdjacentFirst[Record.OriginVertex];
            uint32_t* SecondSlotOrigin = &SharpAdjacentSecond[Record.OriginVertex];
            if (*FirstSlotOrigin == 0xFFFFFFFFu) *FirstSlotOrigin = Record.TerminusVertex; else if (*SecondSlotOrigin == 0xFFFFFFFFu) *SecondSlotOrigin = Record.TerminusVertex;
            uint32_t* FirstSlotTerm  = &SharpAdjacentFirst[Record.TerminusVertex];
            uint32_t* SecondSlotTerm = &SharpAdjacentSecond[Record.TerminusVertex];
            if (*FirstSlotTerm == 0xFFFFFFFFu) *FirstSlotTerm = Record.OriginVertex; else if (*SecondSlotTerm == 0xFFFFFFFFu) *SecondSlotTerm = Record.OriginVertex;
        }
    }

    // Fold each face point into its corner vertices' accumulators (the F term of the interior rule).
    for (uint32_t Face = 0; Face < FaceCount; ++Face)
    {
        const uint32_t CornerCount = FaceVertexCounts[Face];
        if (CornerCount < 3) continue;
        const uint32_t CornerStart = FaceCornerStart[Face];
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
        {
            const uint32_t VertexSlot = FaceVertexIndices[CornerStart + Corner];
            IncidentFacePointSum[VertexSlot] = AddVector(IncidentFacePointSum[VertexSlot], FacePoint[Face]);
            ++IncidentFaceCount[VertexSlot];
        }
    }

    // ④ Reposition every original vertex in its stable slot. Three categories: a corner / high-crease vertex (>= 3 sharp edges,
    //    or an isolated vertex) is held; a crease / boundary vertex (exactly 2 sharp or boundary edges) uses the (Prev+6P+Next)/8
    //    rule along its two sharp adjacents; an interior smooth vertex uses (F + 2R + (n-3)P)/n. A partially-sharp vertex blends
    //    the smooth and crease results by its average incident sharpness so semi-sharp creases ease in continuously.
    for (uint32_t Vertex = 0; Vertex < VertexCount; ++Vertex)
    {
        const Vector3d OldPosition = Positions[Vertex];
        const uint32_t Valence    = IncidentEdgeCount[Vertex];

        // An isolated vertex (no incident edges) has nothing to relax toward — hold it exactly.
        if (Valence == 0)
        {
            OutPositions[Vertex] = OldPosition;
            continue;
        }

        // Smooth interior update: (F + 2R + (n-3)P) / n, with F = average incident face point, R = average incident edge midpoint.
        const Vector3d AverageFacePoint   = ScaleVector(IncidentFacePointSum[Vertex],   IncidentFaceCount[Vertex] > 0 ? 1.0 / (double)IncidentFaceCount[Vertex] : 0.0);
        const Vector3d AverageEdgeMidpoint = ScaleVector(IncidentEdgeMidpointSum[Vertex], 1.0 / (double)Valence);
        Vector3d SmoothPosition = AddVector(AverageFacePoint, ScaleVector(AverageEdgeMidpoint, 2.0));
        SmoothPosition = AddVector(SmoothPosition, ScaleVector(OldPosition, (double)Valence - 3.0));
        SmoothPosition = ScaleVector(SmoothPosition, 1.0 / (double)Valence);

        // Crease / boundary update: (Prev + 6P + Next) / 8 along the two sharp adjacents. A boundary vertex uses its two boundary
        // adjacents; an interior crease vertex uses its two sharp adjacents. Held to the old position if the pair is incomplete.
        const bool     OnBoundary    = (BoundaryAdjacentFirst[Vertex] != 0xFFFFFFFFu);
        const uint32_t CreaseFirst   = OnBoundary ? BoundaryAdjacentFirst[Vertex]  : SharpAdjacentFirst[Vertex];
        const uint32_t CreaseSecond  = OnBoundary ? BoundaryAdjacentSecond[Vertex] : SharpAdjacentSecond[Vertex];
        Vector3d CreasePosition = OldPosition;
        if (CreaseFirst != 0xFFFFFFFFu && CreaseSecond != 0xFFFFFFFFu)
        {
            CreasePosition = ScaleVector(OldPosition, 6.0);
            CreasePosition = AddVector(CreasePosition, Positions[CreaseFirst]);
            CreasePosition = AddVector(CreasePosition, Positions[CreaseSecond]);
            CreasePosition = ScaleVector(CreasePosition, 1.0 / 8.0);
        }

        // 📝 Category resolves the vertex weighting. >= 3 sharp/boundary edges pins a corner (held). Exactly 2 makes a crease
        //    vertex — its result eases from the smooth position toward the crease-rule position by the average incident
        //    sharpness, so a semi-sharp crease interpolates continuously (a fully-sharp crease is the full (Prev+6P+Next)/8).
        //    A boundary vertex is treated as a crease of sharpness 1 unless BoundarySmoothEnabled relaxes an interior-smooth run
        //    of the boundary. Fewer than 2 sharp edges is an ordinary interior vertex — the smooth rule alone.
        const uint32_t CreaseDegree = SharpEdgeCount[Vertex];
        if (CreaseDegree >= 3)
        {
            OutPositions[Vertex] = OldPosition;   // corner — pinned
        }
        else if (OnBoundary)
        {
            // Open-boundary vertex: relax along the boundary curve when smoothing is enabled, else hold the border hard.
            OutPositions[Vertex] = BoundarySmoothEnabled ? CreasePosition : OldPosition;
        }
        else if (CreaseDegree >= 2)
        {
            // Interior crease — blend smooth -> crease by the mean sharpness of this vertex's incident sharp edges.
            double SharpnessAccumulator = 0.0;
            uint32_t SharpnessSamples   = 0;
            if (SharpAdjacentFirst[Vertex] != 0xFFFFFFFFu)
            {
                auto Entry = EdgeSharpness.find(EncodeLocalEdgeKey(Vertex, SharpAdjacentFirst[Vertex]));
                SharpnessAccumulator += (Entry != EdgeSharpness.end()) ? (double)Entry->second : 1.0;
                ++SharpnessSamples;
            }
            if (SharpAdjacentSecond[Vertex] != 0xFFFFFFFFu)
            {
                auto Entry = EdgeSharpness.find(EncodeLocalEdgeKey(Vertex, SharpAdjacentSecond[Vertex]));
                SharpnessAccumulator += (Entry != EdgeSharpness.end()) ? (double)Entry->second : 1.0;
                ++SharpnessSamples;
            }
            const double MeanSharpness = SharpnessSamples > 0 ? SharpnessAccumulator / (double)SharpnessSamples : 1.0;
            OutPositions[Vertex] = AddVector(ScaleVector(SmoothPosition, 1.0 - MeanSharpness),
                                             ScaleVector(CreasePosition, MeanSharpness));
        }
        else
        {
            OutPositions[Vertex] = SmoothPosition;   // ordinary interior vertex
        }
    }

    // ⑤ Append each n-gon as its n corner quads. A quad walks: corner original vertex -> the edge point of the outgoing edge ->
    //    the face point -> the edge point of the incoming edge. Edge points are shared by key so adjacent faces reference the
    //    same one; the face point is unique per face and appended here. A degenerate face (< 3 corners) is copied through unsplit.
    for (uint32_t Face = 0; Face < FaceCount; ++Face)
    {
        const uint32_t CornerCount = FaceVertexCounts[Face];
        const uint32_t CornerStart = FaceCornerStart[Face];

        if (CornerCount < 3)
        {
            OutFaceVertexCounts.push_back(CornerCount);
            for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
                OutFaceVertexIndices.push_back(FaceVertexIndices[CornerStart + Corner]);
            continue;
        }

        // The face point occupies a fresh slot (faces are never shared, so no dedup needed).
        const uint32_t FacePointSlot = (uint32_t)OutPositions.size();
        OutPositions.push_back(FacePoint[Face]);

        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
        {
            const uint32_t CurrentVertex  = FaceVertexIndices[CornerStart + Corner];
            const uint32_t NextVertex     = FaceVertexIndices[CornerStart + (Corner + 1) % CornerCount];
            const uint32_t PreviousVertex = FaceVertexIndices[CornerStart + (Corner + CornerCount - 1) % CornerCount];

            const uint32_t OutgoingEdgeSlot = EdgeRecords[EdgeSlotByKey.at(EncodeLocalEdgeKey(CurrentVertex, NextVertex))].EdgePointSlot;
            const uint32_t IncomingEdgeSlot = EdgeRecords[EdgeSlotByKey.at(EncodeLocalEdgeKey(PreviousVertex, CurrentVertex))].EdgePointSlot;

            OutFaceVertexCounts.push_back(4);
            OutFaceVertexIndices.push_back(CurrentVertex);
            OutFaceVertexIndices.push_back(OutgoingEdgeSlot);
            OutFaceVertexIndices.push_back(FacePointSlot);
            OutFaceVertexIndices.push_back(IncomingEdgeSlot);
        }
    }
}

} // namespace Frontier
