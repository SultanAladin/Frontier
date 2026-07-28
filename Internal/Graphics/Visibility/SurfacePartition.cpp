/*==============================================================================================================================================
                                                            SURFACEPARTITION.CPP
==============================================================================================================================================*/
// 🧩 The runtime single-level partitioner behind SurfacePartition.h. RuntimePartitionSurface walks a triangle-list index buffer in contiguous
//    runs of PartitionTriangleBudget triangles and, for each run, fits the two bounds the cull compute needs: a bounding sphere over the run's
//    referenced vertex positions (centroid centre + farthest-point radius — a loose but cheap fit, exact enough for frustum + HiZ rejection) and
//    a normal cone over the run's triangle face normals (average axis + widest half-angle). No simplification and no coarser levels — this is the
//    first-cut substitute for the offline CompileSurfacePartitionTree, so every partition lands on level 0 with zero geometric error and an
//    unbounded enclosing error. CollectPartitionCullRecords flattens the tree into the SSBO-order record array the P3 cull pass uploads.

#include "SurfacePartition.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// Fit a bounding sphere over one partition's referenced positions and write it into the cull record. Two-step: the centre is the centroid of the
// referenced positions, then the radius is the farthest position from that centre. Looser than Ritter/Welzl but a couple of arithmetic passes and
// always enclosing — the cull only needs a conservative sphere, and a slightly large one over-draws, never under-draws. FirstIndex / IndexSpan
// address the triangle run inside TriangleIndices; each index selects a Vector3f in Positions.
void FitPartitionSphere(const Vector3f*      Positions,
                        const uint32_t*      TriangleIndices,
                        uint32_t             FirstIndex,
                        uint32_t             IndexSpan,
                        PartitionCullRecord& Record)
{
    if (IndexSpan == 0)
        return;

    Vector3f Centroid = { 0.0f, 0.0f, 0.0f };
    for (uint32_t Step = 0; Step < IndexSpan; ++Step)
        Centroid = AddVector(Centroid, Positions[TriangleIndices[FirstIndex + Step]]);
    Centroid = ScaleVector(Centroid, 1.0f / static_cast<float>(IndexSpan));

    float RadiusSquared = 0.0f;
    for (uint32_t Step = 0; Step < IndexSpan; ++Step)
    {
        const Vector3f Offset   = SubtractVector(Positions[TriangleIndices[FirstIndex + Step]], Centroid);
        const float    Distance = DotVector(Offset, Offset);
        if (Distance > RadiusSquared)
            RadiusSquared = Distance;
    }

    Record.SphereX      = Centroid.XCoord;
    Record.SphereY      = Centroid.YCoord;
    Record.SphereZ      = Centroid.ZCoord;
    Record.SphereRadius = std::sqrt(RadiusSquared);
}

// Fit a normal cone over one partition's triangle face normals and write it into the cull record. The axis is the normalized sum of the (area-
// weighted) face normals; the half-angle is the widest deviation of any face normal from that axis, stored as its cosine. A degenerate run (no
// coherent axis) or one spanning more than PartitionConeRejectFloor is marked non-coneable (ConeCosine -1) so the cull never backface-rejects it.
void FitPartitionCone(const Vector3f*      Positions,
                      const uint32_t*      TriangleIndices,
                      uint32_t             FirstIndex,
                      uint32_t             IndexSpan,
                      PartitionCullRecord& Record)
{
    Record.ConeAxisX  = 0.0f;
    Record.ConeAxisY  = 0.0f;
    Record.ConeAxisZ  = 0.0f;
    Record.ConeCosine = -1.0f;

    const uint32_t TriangleCount = IndexSpan / 3;
    if (TriangleCount == 0)
        return;

    // Accumulate the (area-weighted, via the un-normalized cross product) face normals into a mean axis.
    Vector3f AxisSum = { 0.0f, 0.0f, 0.0f };
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        const uint32_t Base = FirstIndex + Triangle * 3;
        const Vector3f Edge0  = SubtractVector(Positions[TriangleIndices[Base + 1]], Positions[TriangleIndices[Base]]);
        const Vector3f Edge1  = SubtractVector(Positions[TriangleIndices[Base + 2]], Positions[TriangleIndices[Base]]);
        AxisSum = AddVector(AxisSum, CrossVector(Edge0, Edge1));
    }

    const Vector3f Axis = NormalizeVector(AxisSum);
    if (VectorLength(Axis) <= 1e-6f)
        return;   // no coherent axis — leave non-coneable

    // The cone half-angle is the widest face-normal deviation from the axis; keep the minimum cosine (widest angle).
    float MinimumCosine = 1.0f;
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        const uint32_t Base = FirstIndex + Triangle * 3;
        const Vector3f Edge0  = SubtractVector(Positions[TriangleIndices[Base + 1]], Positions[TriangleIndices[Base]]);
        const Vector3f Edge1  = SubtractVector(Positions[TriangleIndices[Base + 2]], Positions[TriangleIndices[Base]]);
        const Vector3f Normal = NormalizeVector(CrossVector(Edge0, Edge1));
        const float    Cosine = DotVector(Normal, Axis);
        if (Cosine < MinimumCosine)
            MinimumCosine = Cosine;
    }

    if (MinimumCosine <= PartitionConeRejectFloor)
        return;   // spans more than the reject floor — leave non-coneable

    Record.ConeAxisX  = Axis.XCoord;
    Record.ConeAxisY  = Axis.YCoord;
    Record.ConeAxisZ  = Axis.ZCoord;
    Record.ConeCosine = MinimumCosine;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SurfacePartitionTree RuntimePartitionSurface(const Vector3f* Positions,
                                             uint32_t        PositionCount,
                                             const uint32_t* TriangleIndices,
                                             uint32_t        IndexCount)
{
    SurfacePartitionTree Tree;
    Tree.SourceIndexCount = IndexCount;

    // A malformed source (null, empty, or an index count not a whole number of triangles) yields an empty tree — the cull pass then records
    // nothing rather than reading past the buffer.
    if (Positions == nullptr || TriangleIndices == nullptr || PositionCount == 0 || IndexCount < 3)
        return Tree;

    const uint32_t TriangleCount   = IndexCount / 3;
    const uint32_t IndicesPerBlock = PartitionTriangleBudget * 3;

    SurfacePartitionLevel Level;
    Level.LevelOrdinal = 0;

    // Walk the index buffer in contiguous blocks of PartitionTriangleBudget triangles; the final block takes the remainder.
    for (uint32_t FirstIndex = 0; FirstIndex < TriangleCount * 3; FirstIndex += IndicesPerBlock)
    {
        const uint32_t Remaining = (TriangleCount * 3) - FirstIndex;
        const uint32_t IndexSpan = (Remaining < IndicesPerBlock) ? Remaining : IndicesPerBlock;

        MicroSurfacePartition Partition;
        Partition.IndexOffset    = FirstIndex;
        Partition.IndexCount     = IndexSpan;
        Partition.LevelOrdinal   = 0;
        Partition.GeometricError = 0.0f;
        Partition.EnclosingError = PartitionUnboundedError;

        FitPartitionSphere(Positions, TriangleIndices, FirstIndex, IndexSpan, Partition.CullRecord);
        FitPartitionCone(Positions, TriangleIndices, FirstIndex, IndexSpan, Partition.CullRecord);

        Level.Partitions.push_back(Partition);
    }

    Tree.Levels.push_back(std::move(Level));
    return Tree;
}

std::vector<PartitionCullRecord> CollectPartitionCullRecords(const SurfacePartitionTree& Tree)
{
    uint32_t Total = 0;
    for (const SurfacePartitionLevel& Level : Tree.Levels)
        Total += static_cast<uint32_t>(Level.Partitions.size());

    std::vector<PartitionCullRecord> Records;
    Records.reserve(Total);
    for (const SurfacePartitionLevel& Level : Tree.Levels)
        for (const MicroSurfacePartition& Partition : Level.Partitions)
            Records.push_back(Partition.CullRecord);

    return Records;
}

} // namespace Frontier
