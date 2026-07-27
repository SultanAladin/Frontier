/*============================================================================================================================================
                                                             ADJACENCYINDEX.CPP
============================================================================================================================================*/
// 🧩 The buildTopology port: derive the adjacency cache from a cluster's face stream + positions in one aggregate pass. Per
//    face it records the loop, the fan triangulation (incident-face-tagged), the Newell normal + centroid, per-vertex incidence +
//    one-ring, and the loop-edge -> face map keyed by packed uint64. Faces stay original polygons; only the N loop edges are
//    keyed (no fan diagonals). A malformed cluster leaves Result empty and returns false — no partial cache is published.

#include "AdjacencyIndex.h"

#include "PolygonCluster.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

uint64_t EncodeEdgeKey(uint32_t FirstVertex, uint32_t SecondVertex)
{
    const uint32_t Low  = FirstVertex < SecondVertex ? FirstVertex : SecondVertex;
    const uint32_t High = FirstVertex < SecondVertex ? SecondVertex : FirstVertex;
    return ((uint64_t)Low << 32) | (uint64_t)High;
}

bool ResolveAdjacencyIndex(const PolygonCluster& Source, AdjacencyIndex& Result)
{
    Result = AdjacencyIndex{};

    const uint32_t VertexCount = (uint32_t)Source.Attributes.Position.size();
    const uint32_t FaceCount   = (uint32_t)Source.FaceVertexCounts.size();

    // --- Validate the face stream up front (same aggregate no-partial contract as the display derivation). ------------
    {
        uint32_t CornerCursor = 0;
        for (uint32_t CornerCount : Source.FaceVertexCounts)
        {
            if (CornerCount < 3) return false;                                                 // degenerate face
            if (CornerCursor + CornerCount > Source.FaceVertexIndices.size()) return false;    // truncated stream
            for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
                if (Source.FaceVertexIndices[CornerCursor + Corner] >= VertexCount) return false;   // index out of range
            CornerCursor += CornerCount;
        }
        if (CornerCursor != Source.FaceVertexIndices.size()) return false;                     // trailing uncovered indices
    }

    AdjacencyIndex Built = {};
    Built.FaceCount = FaceCount;
    Built.FaceVertexLoops.reserve(FaceCount);
    Built.FaceTriangleFan.reserve(FaceCount);
    Built.FaceNormal.reserve(FaceCount);
    Built.FaceCenter.reserve(FaceCount);
    Built.FaceEdgeKeys.reserve(FaceCount);
    Built.VertexFaces.assign(VertexCount, {});
    Built.VertexAdjacency.assign(VertexCount, {});

    const std::vector<Vector3d>& Position = Source.Attributes.Position;

    uint32_t CornerCursor = 0;
    for (uint32_t FaceOrdinal = 0; FaceOrdinal < FaceCount; ++FaceOrdinal)
    {
        const uint32_t CornerCount = Source.FaceVertexCounts[FaceOrdinal];

        // The loop of corner vertex indices, winding order.
        std::vector<uint32_t> Loop;
        Loop.reserve(CornerCount);
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
            Loop.push_back(Source.FaceVertexIndices[CornerCursor + Corner]);

        // 📝 Newell's method -> a robust normal for any planar / near-planar polygon; centroid = average of the loop verts.
        double NormalX = 0.0, NormalY = 0.0, NormalZ = 0.0;
        double CenterX = 0.0, CenterY = 0.0, CenterZ = 0.0;
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
        {
            const Vector3d& Current = Position[Loop[Corner]];
            const Vector3d& Next    = Position[Loop[(Corner + 1) % CornerCount]];
            NormalX += (Current.YCoord - Next.YCoord) * (Current.ZCoord + Next.ZCoord);
            NormalY += (Current.ZCoord - Next.ZCoord) * (Current.XCoord + Next.XCoord);
            NormalZ += (Current.XCoord - Next.XCoord) * (Current.YCoord + Next.YCoord);
            CenterX += Current.XCoord;
            CenterY += Current.YCoord;
            CenterZ += Current.ZCoord;
        }
        const double NormalLength = std::sqrt(NormalX * NormalX + NormalY * NormalY + NormalZ * NormalZ);
        const double InverseLength = NormalLength > 0.0 ? 1.0 / NormalLength : 1.0;
        Built.FaceNormal.push_back(Vector3d{ NormalX * InverseLength, NormalY * InverseLength, NormalZ * InverseLength });
        Built.FaceCenter.push_back(Vector3d{ CenterX / CornerCount, CenterY / CornerCount, CenterZ / CornerCount });

        // 📝 Fan triangulation (v0, vi, vi+1) for raster / pick; incident = this face. The flat TriangleStream mirrors
        //    the per-face fan so a triangle hit resolves back to its incident polygon.
        std::vector<AdjacencyTriangle> FaceFan;
        FaceFan.reserve(CornerCount >= 2 ? CornerCount - 2 : 0);
        for (uint32_t Corner = 1; Corner + 1 < CornerCount; ++Corner)
        {
            AdjacencyTriangle Triangle = {};
            Triangle.VertexIndex[0] = Loop[0];
            Triangle.VertexIndex[1] = Loop[Corner];
            Triangle.VertexIndex[2] = Loop[Corner + 1];
            Triangle.IncidentFace   = FaceOrdinal;
            FaceFan.push_back(Triangle);
            Built.TriangleStream.push_back(Triangle);
        }
        Built.FaceTriangleFan.push_back(std::move(FaceFan));

        // 📝 Per-vertex incidence + real (loop-edge) one-ring adjacency + the loop-edge -> face map. One-ring pairs are
        //    inserted both ways but kept unique; VertexFaces may list a face once per corner (matching the prototype).
        std::vector<uint64_t> FaceEdges;
        FaceEdges.reserve(CornerCount);
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
        {
            const uint32_t LoopStart = Loop[Corner];
            const uint32_t LoopEnd   = Loop[(Corner + 1) % CornerCount];

            Built.VertexFaces[LoopStart].push_back(FaceOrdinal);

            // one-ring: add LoopEnd to LoopStart's adjacency and LoopStart to LoopEnd's, uniquely
            {
                std::vector<uint32_t>& StartRing = Built.VertexAdjacency[LoopStart];
                bool StartHasEnd = false;
                for (uint32_t Adjacent : StartRing) if (Adjacent == LoopEnd) { StartHasEnd = true; break; }
                if (!StartHasEnd) StartRing.push_back(LoopEnd);

                std::vector<uint32_t>& EndRing = Built.VertexAdjacency[LoopEnd];
                bool EndHasStart = false;
                for (uint32_t Adjacent : EndRing) if (Adjacent == LoopStart) { EndHasStart = true; break; }
                if (!EndHasStart) EndRing.push_back(LoopStart);
            }

            const uint64_t Key = EncodeEdgeKey(LoopStart, LoopEnd);
            FaceEdges.push_back(Key);

            auto FacesEntry = Built.EdgeFaces.find(Key);
            if (FacesEntry == Built.EdgeFaces.end())
            {
                Built.EdgeFaces.emplace(Key, std::vector<uint32_t>{ FaceOrdinal });
                Built.EdgeVertices.emplace(Key, EdgeEndpoints{ LoopStart < LoopEnd ? LoopStart : LoopEnd,
                                                               LoopStart < LoopEnd ? LoopEnd : LoopStart });
                Built.EdgeKeys.push_back(Key);
            }
            else
            {
                FacesEntry->second.push_back(FaceOrdinal);
            }
        }
        Built.FaceEdgeKeys.push_back(std::move(FaceEdges));
        Built.FaceVertexLoops.push_back(std::move(Loop));

        CornerCursor += CornerCount;
    }

    Result = std::move(Built);
    return true;
}

std::vector<uint32_t> EvaluateFaceAdjacency(const AdjacencyIndex& Index, uint32_t FaceOrdinal)
{
    std::vector<uint32_t> Adjacent;
    if (FaceOrdinal >= Index.FaceEdgeKeys.size()) return Adjacent;

    for (uint64_t Key : Index.FaceEdgeKeys[FaceOrdinal])
    {
        auto FacesEntry = Index.EdgeFaces.find(Key);
        if (FacesEntry == Index.EdgeFaces.end()) continue;
        for (uint32_t IncidentFace : FacesEntry->second)
        {
            if (IncidentFace == FaceOrdinal) continue;
            bool Present = false;
            for (uint32_t Existing : Adjacent) if (Existing == IncidentFace) { Present = true; break; }
            if (!Present) Adjacent.push_back(IncidentFace);
        }
    }
    return Adjacent;
}

} // namespace Frontier
