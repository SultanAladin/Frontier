/*============================================================================================================================================
                                                           SUBDIVIDESIMPLE.CPP
============================================================================================================================================*/
// 🧩 One level of simple (linear) subdivision. New points are appended in a fixed order — original points, then one shared
//    midpoint per edge, then one centroid per face — and every n-gon is replaced by its n corner quads. Midpoints are keyed on
//    the unordered endpoint-index pair so adjacent faces reference the same midpoint and the refined polygon stays watertight. No
//    point moves: this is connectivity refinement only (Catmull-Clark, the smoothing scheme, also repositions points).

#include "SubdivideSimple.h"

#include <unordered_map>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Canonical key for an undirected edge: the two endpoint indices packed low-first into one 64-bit value, so an edge and
    //    its reverse hash to the same midpoint slot. Kept file-private (mirrors AdjacencyIndex's EncodeEdgeKey, no cross-include).
    uint64_t EncodeLocalEdgeKey(uint32_t FirstIndex, uint32_t SecondIndex)
    {
        const uint32_t LowerIndex  = FirstIndex < SecondIndex ? FirstIndex : SecondIndex;
        const uint32_t HigherIndex = FirstIndex < SecondIndex ? SecondIndex : FirstIndex;
        return ((uint64_t)HigherIndex << 32) | (uint64_t)LowerIndex;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void SubdivideSimpleLevel(const std::vector<Vector3d>& Positions,
                          const std::vector<uint32_t>& FaceVertexIndices,
                          const std::vector<uint32_t>& FaceVertexCounts,
                          std::vector<Vector3d>&       OutPositions,
                          std::vector<uint32_t>&       OutFaceVertexIndices,
                          std::vector<uint32_t>&       OutFaceVertexCounts)
{
    OutPositions.clear();
    OutFaceVertexIndices.clear();
    OutFaceVertexCounts.clear();

    // ① Original points carry over unchanged, keeping their indices stable for the new connectivity.
    OutPositions = Positions;

    // 📝 One midpoint per unique edge, minted on first sighting and reused by the second face that shares the edge.
    std::unordered_map<uint64_t, uint32_t> MidpointSlots;

    uint32_t CornerCursor = 0;
    for (uint32_t Face = 0; Face < (uint32_t)FaceVertexCounts.size(); ++Face)
    {
        const uint32_t CornerCount = FaceVertexCounts[Face];

        // A degenerate face (fewer than three corners) cannot be split into quads — pass its corners through verbatim.
        if (CornerCount < 3)
        {
            OutFaceVertexCounts.push_back(CornerCount);
            for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
                OutFaceVertexIndices.push_back(FaceVertexIndices[CornerCursor + Corner]);
            CornerCursor += CornerCount;
            continue;
        }

        // ② Resolve (mint or reuse) the midpoint of every boundary edge of this face.
        std::vector<uint32_t> CornerIndices(CornerCount);
        std::vector<uint32_t> EdgeMidpoints(CornerCount);
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
            CornerIndices[Corner] = FaceVertexIndices[CornerCursor + Corner];
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
        {
            const uint32_t OriginIndex   = CornerIndices[Corner];
            const uint32_t TerminusIndex = CornerIndices[(Corner + 1) % CornerCount];
            const uint64_t EdgeKey = EncodeLocalEdgeKey(OriginIndex, TerminusIndex);
            auto Existing = MidpointSlots.find(EdgeKey);
            if (Existing != MidpointSlots.end())
            {
                EdgeMidpoints[Corner] = Existing->second;
            }
            else
            {
                const Vector3d Midpoint = ScaleVector(AddVector(Positions[OriginIndex], Positions[TerminusIndex]), 0.5);
                const uint32_t MidpointSlot = (uint32_t)OutPositions.size();
                OutPositions.push_back(Midpoint);
                MidpointSlots.emplace(EdgeKey, MidpointSlot);
                EdgeMidpoints[Corner] = MidpointSlot;
            }
        }

        // ③ One centroid for this face (always unique — faces are never shared).
        Vector3d CentroidSum = { 0.0, 0.0, 0.0 };
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
            CentroidSum = AddVector(CentroidSum, Positions[CornerIndices[Corner]]);
        const uint32_t CentroidSlot = (uint32_t)OutPositions.size();
        OutPositions.push_back(ScaleVector(CentroidSum, 1.0 / (double)CornerCount));

        // ④ Append one quad per corner: corner -> outgoing-edge midpoint -> centroid -> incoming-edge midpoint.
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
        {
            const uint32_t PreviousCorner = (Corner + CornerCount - 1) % CornerCount;
            OutFaceVertexCounts.push_back(4);
            OutFaceVertexIndices.push_back(CornerIndices[Corner]);
            OutFaceVertexIndices.push_back(EdgeMidpoints[Corner]);
            OutFaceVertexIndices.push_back(CentroidSlot);
            OutFaceVertexIndices.push_back(EdgeMidpoints[PreviousCorner]);
        }

        CornerCursor += CornerCount;
    }
}

} // namespace Frontier
