/*==============================================================================================================================================
                                                           GEOMETRYTREEBUILD.CPP
==============================================================================================================================================*/
// 🧩 The CPU surface-area-heuristic tree build, ported 1:1 from three-mesh-bvh. Each section below names the reference file it came from so the two
//    can be diffed when the dependency moves.
//
//    🔴 THE PRIMITIVE BOUNDS ARRAY IS CENTRE / HALF-EXTENT, NOT MINIMUM / MAXIMUM. Six floats per primitive laid out
//       [CentreX, HalfX, CentreY, HalfY, CentreZ, HalfZ] — the axes INTERLEAVED, centre before half-extent. Every index expression in the reference
//       depends on it: `Base + 2 * Axis` is that axis's centre and `Base + 2 * Axis + 1` its half-extent, which is why the split code reads a
//       centre with a stride-2 offset rather than the stride-3 a min/max layout would need. Reading this array as min/max is the single easiest way
//       to corrupt this port, and it does not fail loudly: half-extents are small positive numbers, so a min/max misreading yields a plausible
//       slightly-wrong box everywhere and a tree that is merely poor rather than broken.

#include "Graphics/Acceleration/GeometryTreeBuild.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Frontier
{

namespace
{

constexpr float PositiveInfinity = std::numeric_limits<float>::infinity();
constexpr float NegativeInfinity = -std::numeric_limits<float>::infinity();

// A box as the reference stores it: six floats, minimum XYZ then maximum XYZ. Distinct from the centre/half-extent primitive layout above.
struct BoundsSix
{
    float Value[6] = { PositiveInfinity, PositiveInfinity, PositiveInfinity, NegativeInfinity, NegativeInfinity, NegativeInfinity };
};

// The intermediate pointer-linked node the reference builds before packing. Discarded once PopulateBuffer has flattened it.
struct BuildNode
{
    BoundsSix Bounds;
    uint32_t  Offset    = 0;                      // [-] - leaf only: first primitive in the permutation
    uint32_t  Count     = 0;                      // [-] - leaf only: primitive count; 0 on an interior node
    uint32_t  SplitAxis = 0;                      // [-] - interior only
    bool      LeafCondition = false;
    uint32_t  Left  = 0xFFFFFFFFu;                // [-] - index into the build-node pool
    uint32_t  Right = 0xFFFFFFFFu;
};

// ─── ARRAYBOXUTILITIES.JS ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────

void MakeEmptyBounds(BoundsSix& Target)
{
    Target.Value[0] = Target.Value[1] = Target.Value[2] = PositiveInfinity;
    Target.Value[3] = Target.Value[4] = Target.Value[5] = NegativeInfinity;
}

// 🔴 The reference seeds SplitDistance to -Infinity and returns -1 only when every edge is shorter than that, which cannot happen for a real box —
//    so for any populated box this returns a valid axis, INCLUDING when the box is degenerate (all edges zero, axis 0 wins). The -1 path exists for
//    an inverted/empty box, where every edge is negative and... still beats -Infinity. Keeping the exact seed matters: raising it to 0.0f would
//    start returning -1 for flat-but-real geometry (a ground plane on one axis) and silently turn those nodes into oversized leaves.
int32_t GetLongestEdgeIndex(const BoundsSix& Bounds)
{
    int32_t SplitDimensionIndex = -1;
    float   SplitDistance       = NegativeInfinity;

    for (int32_t Index = 0; Index < 3; ++Index)
    {
        const float Distance = Bounds.Value[Index + 3] - Bounds.Value[Index];
        if (Distance > SplitDistance)
        {
            SplitDistance       = Distance;
            SplitDimensionIndex = Index;
        }
    }

    return SplitDimensionIndex;
}

void CopyBounds(const BoundsSix& Source, BoundsSix& Target)
{
    for (int32_t Index = 0; Index < 6; ++Index)
        Target.Value[Index] = Source.Value[Index];
}

void UnionBounds(const BoundsSix& A, const BoundsSix& B, BoundsSix& Target)
{
    for (int32_t Dimension = 0; Dimension < 3; ++Dimension)
    {
        const int32_t Upper = Dimension + 3;

        const float MinimumA = A.Value[Dimension];
        const float MinimumB = B.Value[Dimension];
        Target.Value[Dimension] = (MinimumA < MinimumB) ? MinimumA : MinimumB;

        const float MaximumA = A.Value[Upper];
        const float MaximumB = B.Value[Upper];
        Target.Value[Upper] = (MaximumA > MaximumB) ? MaximumA : MaximumB;
    }
}

// Expand Bounds by the primitive whose six-float record starts at StartIndex in the centre/half-extent array.
void ExpandByPrimitiveBounds(size_t StartIndex, const std::vector<float>& PrimitiveBounds, BoundsSix& Bounds)
{
    for (int32_t Dimension = 0; Dimension < 3; ++Dimension)
    {
        const float Centre = PrimitiveBounds[StartIndex + 2 * Dimension];
        const float Half   = PrimitiveBounds[StartIndex + 2 * Dimension + 1];

        const float Minimum = Centre - Half;
        const float Maximum = Centre + Half;

        if (Minimum < Bounds.Value[Dimension])     Bounds.Value[Dimension]     = Minimum;
        if (Maximum > Bounds.Value[Dimension + 3]) Bounds.Value[Dimension + 3] = Maximum;
    }
}

// 📝 Surface area of the box, doubled — the reference's `2 * (d0*d1 + d1*d2 + d2*d0)`. The factor of 2 cancels in the SAH ratio, but it is kept so
//    the cost numbers match the reference's exactly.
float ComputeSurfaceArea(const BoundsSix& Bounds)
{
    const float Extent0 = Bounds.Value[3] - Bounds.Value[0];
    const float Extent1 = Bounds.Value[4] - Bounds.Value[1];
    const float Extent2 = Bounds.Value[5] - Bounds.Value[2];

    return 2.0f * (Extent0 * Extent1 + Extent1 * Extent2 + Extent2 * Extent0);
}

// ─── COMPUTEBOUNDSUTILS.JS ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// 📝 The primitive box union and the CENTROID box are computed in one sweep, exactly as the reference does, because they read the same memory.
//    The two are genuinely different boxes and both are needed: the primitive box bounds the node's geometry, the centroid box bounds only the
//    primitives' centres and is what the split candidates are drawn from.
void GetBounds(const std::vector<float>& PrimitiveBounds,
               uint32_t                  Offset,
               uint32_t                  Count,
               BoundsSix&                Target,
               BoundsSix&                CentroidTarget)
{
    float MinimumX = PositiveInfinity, MinimumY = PositiveInfinity, MinimumZ = PositiveInfinity;
    float MaximumX = NegativeInfinity, MaximumY = NegativeInfinity, MaximumZ = NegativeInfinity;

    float CentroidMinimumX = PositiveInfinity, CentroidMinimumY = PositiveInfinity, CentroidMinimumZ = PositiveInfinity;
    float CentroidMaximumX = NegativeInfinity, CentroidMaximumY = NegativeInfinity, CentroidMaximumZ = NegativeInfinity;

    for (size_t Index = static_cast<size_t>(Offset) * 6, End = static_cast<size_t>(Offset + Count) * 6; Index < End; Index += 6)
    {
        const float CentreX = PrimitiveBounds[Index + 0];
        const float HalfX   = PrimitiveBounds[Index + 1];
        const float LowerX  = CentreX - HalfX;
        const float UpperX  = CentreX + HalfX;
        if (LowerX  < MinimumX)         MinimumX         = LowerX;
        if (UpperX  > MaximumX)         MaximumX         = UpperX;
        if (CentreX < CentroidMinimumX) CentroidMinimumX = CentreX;
        if (CentreX > CentroidMaximumX) CentroidMaximumX = CentreX;

        const float CentreY = PrimitiveBounds[Index + 2];
        const float HalfY   = PrimitiveBounds[Index + 3];
        const float LowerY  = CentreY - HalfY;
        const float UpperY  = CentreY + HalfY;
        if (LowerY  < MinimumY)         MinimumY         = LowerY;
        if (UpperY  > MaximumY)         MaximumY         = UpperY;
        if (CentreY < CentroidMinimumY) CentroidMinimumY = CentreY;
        if (CentreY > CentroidMaximumY) CentroidMaximumY = CentreY;

        const float CentreZ = PrimitiveBounds[Index + 4];
        const float HalfZ   = PrimitiveBounds[Index + 5];
        const float LowerZ  = CentreZ - HalfZ;
        const float UpperZ  = CentreZ + HalfZ;
        if (LowerZ  < MinimumZ)         MinimumZ         = LowerZ;
        if (UpperZ  > MaximumZ)         MaximumZ         = UpperZ;
        if (CentreZ < CentroidMinimumZ) CentroidMinimumZ = CentreZ;
        if (CentreZ > CentroidMaximumZ) CentroidMaximumZ = CentreZ;
    }

    Target.Value[0] = MinimumX;  Target.Value[1] = MinimumY;  Target.Value[2] = MinimumZ;
    Target.Value[3] = MaximumX;  Target.Value[4] = MaximumY;  Target.Value[5] = MaximumZ;

    CentroidTarget.Value[0] = CentroidMinimumX;  CentroidTarget.Value[1] = CentroidMinimumY;  CentroidTarget.Value[2] = CentroidMinimumZ;
    CentroidTarget.Value[3] = CentroidMaximumX;  CentroidTarget.Value[4] = CentroidMaximumY;  CentroidTarget.Value[5] = CentroidMaximumZ;
}

// ─── SPLITUTILS.JS ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

struct SplitBin
{
    uint32_t  Count = 0;
    BoundsSix Bounds;
    BoundsSix RightCacheBounds;
    BoundsSix LeftCacheBounds;
    float     Candidate = 0.0f;
};

struct SplitChoice
{
    int32_t Axis     = -1;    // [-] - -1 means "do not split"; the caller turns the node into a leaf
    float   Position = 0.0f;
};

// The mean centroid along Axis, for the AVERAGE strategy.
float GetAverage(const std::vector<float>& PrimitiveBounds, uint32_t Offset, uint32_t Count, int32_t Axis)
{
    float Average = 0.0f;
    for (uint32_t Index = Offset, End = Offset + Count; Index < End; ++Index)
        Average += PrimitiveBounds[static_cast<size_t>(Index) * 6 + static_cast<size_t>(Axis) * 2];

    return Average / static_cast<float>(Count);
}

// 🔴 THE SAH SEARCH IS SEEDED WITH THE COST OF *NOT* SPLITTING, AND THAT IS WHAT MAKES IT A HEURISTIC RATHER THAN A BISECTION. BestCost starts at
//    PrimitiveIntersectCost * Count — the modelled cost of leaving this node as a leaf — so a candidate is only accepted when it beats leaving the
//    node alone. When no candidate does, Axis stays -1 and the caller makes a leaf even though the node still holds more than MaxLeafSize
//    primitives. Dropping that seed (starting at +Infinity) would force a split at every node and produce a deeper, worse tree that still looks
//    perfectly well-formed.
SplitChoice GetOptimalSplit(const BoundsSix&          NodeBounds,
                            const BoundsSix&          CentroidBounds,
                            const std::vector<float>& PrimitiveBounds,
                            uint32_t                  Offset,
                            uint32_t                  Count,
                            GeometryTreeStrategy      Strategy,
                            std::vector<SplitBin>&    BinScratch)
{
    SplitChoice Choice;

    if (Strategy == GeometryTreeStrategy::Center)
    {
        Choice.Axis = GetLongestEdgeIndex(CentroidBounds);
        if (Choice.Axis != -1)
            Choice.Position = (CentroidBounds.Value[Choice.Axis] + CentroidBounds.Value[Choice.Axis + 3]) / 2.0f;

        return Choice;
    }

    if (Strategy == GeometryTreeStrategy::Average)
    {
        Choice.Axis = GetLongestEdgeIndex(NodeBounds);
        if (Choice.Axis != -1)
            Choice.Position = GetAverage(PrimitiveBounds, Offset, Count, Choice.Axis);

        return Choice;
    }

    const float RootSurfaceArea = ComputeSurfaceArea(NodeBounds);
    float       BestCost        = GeometryTreePrimitiveIntersectCost * static_cast<float>(Count);

    const size_t RangeStart = static_cast<size_t>(Offset) * 6;
    const size_t RangeEnd   = static_cast<size_t>(Offset + Count) * 6;

    for (int32_t Axis = 0; Axis < 3; ++Axis)
    {
        const float AxisLeft   = CentroidBounds.Value[Axis];
        const float AxisRight  = CentroidBounds.Value[Axis + 3];
        const float AxisLength = AxisRight - AxisLeft;
        const float BinWidth   = AxisLength / static_cast<float>(GeometryTreeBinCount);

        // ─── exhaustive path : fewer primitives than a quarter of the bins ──────────────────────────────────────────────────────────────────
        // 📝 With very few primitives, every primitive position is tried as a split candidate rather than bucketing them. The reference notes this
        //    is simply faster at that size; it is also exact, which is why the two paths can disagree slightly on the same input.
        if (Count < GeometryTreeExactSplitLimit)
        {
            BinScratch.assign(Count, SplitBin{});

            uint32_t BinIndex = 0;
            for (size_t Cursor = RangeStart; Cursor < RangeEnd; Cursor += 6, ++BinIndex)
            {
                SplitBin& Bin = BinScratch[BinIndex];
                Bin.Candidate = PrimitiveBounds[Cursor + 2 * Axis];
                Bin.Count     = 0;

                MakeEmptyBounds(Bin.RightCacheBounds);
                MakeEmptyBounds(Bin.LeftCacheBounds);
                MakeEmptyBounds(Bin.Bounds);

                ExpandByPrimitiveBounds(Cursor, PrimitiveBounds, Bin.Bounds);
            }

            std::sort(BinScratch.begin(), BinScratch.end(),
                      [](const SplitBin& A, const SplitBin& B) { return A.Candidate < B.Candidate; });

            // Collapse duplicate candidates — two primitives sharing a centre give the same split twice.
            uint32_t SplitCount = Count;
            for (uint32_t Index = 0; Index < SplitCount; ++Index)
            {
                const float Candidate = BinScratch[Index].Candidate;
                while (Index + 1 < SplitCount && BinScratch[Index + 1].Candidate == Candidate)
                {
                    BinScratch.erase(BinScratch.begin() + static_cast<ptrdiff_t>(Index) + 1);
                    --SplitCount;
                }
            }

            // Sort each primitive to one side of every surviving candidate.
            for (size_t Cursor = RangeStart; Cursor < RangeEnd; Cursor += 6)
            {
                const float Centre = PrimitiveBounds[Cursor + 2 * Axis];
                for (uint32_t Index = 0; Index < SplitCount; ++Index)
                {
                    SplitBin& Bin = BinScratch[Index];
                    if (Centre >= Bin.Candidate)
                    {
                        ExpandByPrimitiveBounds(Cursor, PrimitiveBounds, Bin.RightCacheBounds);
                    }
                    else
                    {
                        ExpandByPrimitiveBounds(Cursor, PrimitiveBounds, Bin.LeftCacheBounds);
                        ++Bin.Count;
                    }
                }
            }

            for (uint32_t Index = 0; Index < SplitCount; ++Index)
            {
                const SplitBin& Bin        = BinScratch[Index];
                const uint32_t  LeftCount  = Bin.Count;
                const uint32_t  RightCount = Count - Bin.Count;

                float LeftProbability  = 0.0f;
                float RightProbability = 0.0f;
                if (LeftCount  != 0) LeftProbability  = ComputeSurfaceArea(Bin.LeftCacheBounds)  / RootSurfaceArea;
                if (RightCount != 0) RightProbability = ComputeSurfaceArea(Bin.RightCacheBounds) / RootSurfaceArea;

                const float Cost = GeometryTreeTraversalCost + GeometryTreePrimitiveIntersectCost *
                                   (LeftProbability * static_cast<float>(LeftCount) + RightProbability * static_cast<float>(RightCount));

                if (Cost < BestCost)
                {
                    Choice.Axis     = Axis;
                    BestCost        = Cost;
                    Choice.Position = Bin.Candidate;
                }
            }

            continue;
        }

        // ─── binned path : the ordinary case ────────────────────────────────────────────────────────────────────────────────────────────────
        BinScratch.assign(GeometryTreeBinCount, SplitBin{});
        for (uint32_t Index = 0; Index < GeometryTreeBinCount; ++Index)
        {
            SplitBin& Bin = BinScratch[Index];
            Bin.Count     = 0;
            Bin.Candidate = AxisLeft + BinWidth + static_cast<float>(Index) * BinWidth;
            MakeEmptyBounds(Bin.Bounds);
        }

        // 🔴 THE BIN INDEX IS A TRUNCATION AND THE CLAMP IS LOAD-BEARING. The reference writes `~~(relativeCenter / binWidth)`, a truncate-toward-
        //    zero cast, then clamps to the last bin. The clamp catches the primitive sitting exactly on the centroid box's upper bound, which maps
        //    to BIN_COUNT rather than BIN_COUNT-1 — that primitive always exists, because the box was measured from these very centres. Without the
        //    clamp it is a one-past-the-end write on every single node.
        for (size_t Cursor = RangeStart; Cursor < RangeEnd; Cursor += 6)
        {
            const float TriangleCentre = PrimitiveBounds[Cursor + 2 * Axis];
            const float RelativeCentre = TriangleCentre - AxisLeft;

            int32_t Index = static_cast<int32_t>(RelativeCentre / BinWidth);
            if (Index >= static_cast<int32_t>(GeometryTreeBinCount)) Index = static_cast<int32_t>(GeometryTreeBinCount) - 1;
            if (Index < 0)                                           Index = 0;

            SplitBin& Bin = BinScratch[static_cast<size_t>(Index)];
            ++Bin.Count;

            ExpandByPrimitiveBounds(Cursor, PrimitiveBounds, Bin.Bounds);
        }

        // Suffix-union the bins right to left, so each candidate's right side is one lookup rather than a rescan.
        CopyBounds(BinScratch[GeometryTreeBinCount - 1].Bounds, BinScratch[GeometryTreeBinCount - 1].RightCacheBounds);
        for (int32_t Index = static_cast<int32_t>(GeometryTreeBinCount) - 2; Index >= 0; --Index)
        {
            const SplitBin& NextBin = BinScratch[static_cast<size_t>(Index) + 1];
            UnionBounds(BinScratch[static_cast<size_t>(Index)].Bounds, NextBin.RightCacheBounds,
                        BinScratch[static_cast<size_t>(Index)].RightCacheBounds);
        }

        BoundsSix LeftBounds;
        uint32_t  LeftCount = 0;
        for (uint32_t Index = 0; Index < GeometryTreeBinCount - 1; ++Index)
        {
            const SplitBin& Bin      = BinScratch[Index];
            const uint32_t  BinCount = Bin.Count;

            const SplitBin& NextBin    = BinScratch[Index + 1];
            const BoundsSix& RightBounds = NextBin.RightCacheBounds;

            // 📝 An empty bin must not fold its identity box into the running left union — that is why the expansion is guarded on BinCount and
            //    the very first non-empty bin COPIES rather than unions.
            if (BinCount != 0)
            {
                if (LeftCount == 0) CopyBounds(Bin.Bounds, LeftBounds);
                else                UnionBounds(Bin.Bounds, LeftBounds, LeftBounds);
            }

            LeftCount += BinCount;

            float LeftProbability  = 0.0f;
            float RightProbability = 0.0f;
            if (LeftCount != 0) LeftProbability = ComputeSurfaceArea(LeftBounds) / RootSurfaceArea;

            const uint32_t RightCount = Count - LeftCount;
            if (RightCount != 0) RightProbability = ComputeSurfaceArea(RightBounds) / RootSurfaceArea;

            const float Cost = GeometryTreeTraversalCost + GeometryTreePrimitiveIntersectCost *
                               (LeftProbability * static_cast<float>(LeftCount) + RightProbability * static_cast<float>(RightCount));

            if (Cost < BestCost)
            {
                Choice.Axis     = Axis;
                BestCost        = Cost;
                Choice.Position = Bin.Candidate;
            }
        }
    }

    return Choice;
}

// ─── SORTUTILS.JS ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// Hoare partition over the primitive permutation AND its bounds records together, returning the index of the first element on the right side.
//
// 🔴 A PRIMITIVE WHOSE CENTRE LIES EXACTLY ON THE SPLIT PLANE GOES RIGHT, AND THE SAH BINNING ABOVE ASSUMES THE SAME RULE. The two comparisons
//    encode it: the left scan advances while `< Position`, the right scan retreats while `>= Position`. Flipping either to the other side splits
//    the tie differently from the way the cost was evaluated, so the partition produces child counts the SAH did not price — usually harmless,
//    occasionally an empty child that collapses the node back to a leaf for no reason.
uint32_t Partition(std::vector<uint32_t>& PrimitiveOrder,
                   std::vector<float>&    PrimitiveBounds,
                   uint32_t               Offset,
                   uint32_t               Count,
                   const SplitChoice&     Split)
{
    int64_t     Left       = static_cast<int64_t>(Offset);
    int64_t     Right      = static_cast<int64_t>(Offset) + static_cast<int64_t>(Count) - 1;
    const float Position   = Split.Position;
    const size_t AxisOffset = static_cast<size_t>(Split.Axis) * 2;

    while (true)
    {
        while (Left <= Right && PrimitiveBounds[static_cast<size_t>(Left) * 6 + AxisOffset] < Position)
            ++Left;

        while (Left <= Right && PrimitiveBounds[static_cast<size_t>(Right) * 6 + AxisOffset] >= Position)
            --Right;

        if (Left < Right)
        {
            std::swap(PrimitiveOrder[static_cast<size_t>(Left)], PrimitiveOrder[static_cast<size_t>(Right)]);

            for (int32_t Index = 0; Index < 6; ++Index)
                std::swap(PrimitiveBounds[static_cast<size_t>(Left) * 6 + Index],
                          PrimitiveBounds[static_cast<size_t>(Right) * 6 + Index]);

            ++Left;
            --Right;
        }
        else
        {
            return static_cast<uint32_t>(Left);
        }
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ClearGeometryTree(GeometryTree& Tree)
{
    Tree.NodeWords.clear();
    Tree.NodeWords.shrink_to_fit();
    Tree.PrimitiveOrder.clear();
    Tree.PrimitiveOrder.shrink_to_fit();
    Tree.ParentTable.clear();
    Tree.ParentTable.shrink_to_fit();

    Tree.NodeCount      = 0;
    Tree.LeafCount      = 0;
    Tree.MaximumDepth   = 0;
    Tree.ReadyCondition = false;
}

bool BuildGeometryTree(const float*               Positions,
                       uint32_t                   PositionCount,
                       const uint32_t*            Indices,
                       uint32_t                   IndexCount,
                       const GeometryTreeOptions& Options,
                       GeometryTree&              Result)
{
    ClearGeometryTree(Result);

    if (Positions == nullptr || Indices == nullptr) return false;
    if (PositionCount == 0 || IndexCount == 0)      return false;
    if (IndexCount % 3 != 0)                        return false;

    const uint32_t TriangleCount = IndexCount / 3;

    // ─── primitive bounds : centre / half-extent, six floats each ───────────────────────────────────────────────────────────────────────────
    std::vector<float> PrimitiveBounds(static_cast<size_t>(TriangleCount) * 6, 0.0f);
    Result.PrimitiveOrder.resize(TriangleCount);

    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        Result.PrimitiveOrder[Triangle] = Triangle;

        const uint32_t Corner0 = Indices[Triangle * 3 + 0];
        const uint32_t Corner1 = Indices[Triangle * 3 + 1];
        const uint32_t Corner2 = Indices[Triangle * 3 + 2];

        if (Corner0 >= PositionCount || Corner1 >= PositionCount || Corner2 >= PositionCount)
        {
            ClearGeometryTree(Result);
            return false;
        }

        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            const float A = Positions[static_cast<size_t>(Corner0) * 3 + Axis];
            const float B = Positions[static_cast<size_t>(Corner1) * 3 + Axis];
            const float C = Positions[static_cast<size_t>(Corner2) * 3 + Axis];

            const float Minimum = std::fmin(A, std::fmin(B, C));
            const float Maximum = std::fmax(A, std::fmax(B, C));

            // 📝 Centre and HALF-extent, matching the reference's layout. The centre is the box midpoint, NOT the triangle's centroid — those
            //    differ, and the split code wants the box midpoint.
            PrimitiveBounds[static_cast<size_t>(Triangle) * 6 + Axis * 2 + 0] = (Minimum + Maximum) * 0.5f;
            PrimitiveBounds[static_cast<size_t>(Triangle) * 6 + Axis * 2 + 1] = (Maximum - Minimum) * 0.5f;
        }
    }

    // ─── build : recursive split into a pointer-linked pool ─────────────────────────────────────────────────────────────────────────────────
    std::vector<BuildNode> Pool;
    Pool.reserve(static_cast<size_t>(TriangleCount) * 2);

    std::vector<SplitBin> BinScratch;
    BinScratch.reserve(GeometryTreeBinCount);

    const uint32_t MaxLeafSize = (Options.MaxLeafSize == 0) ? 1u : Options.MaxLeafSize;
    const uint32_t MaxDepth    = (Options.MaxDepth    == 0) ? 1u : Options.MaxDepth;

    uint32_t DeepestLevel = 0;

    // An explicit stack rather than recursion: MaxDepth is 40 by default but a caller may raise it, and a 40-deep C++ recursion over a builder
    // holding a bin array per level is a real stack cost. The traversal order matches the reference's left-then-right descent.
    struct PendingSplit
    {
        uint32_t  NodeIndex;
        uint32_t  Offset;
        uint32_t  Count;
        BoundsSix CentroidBounds;
        uint32_t  Depth;
    };

    Pool.push_back(BuildNode{});
    {
        BoundsSix RootNodeBounds, RootCentroidBounds;
        GetBounds(PrimitiveBounds, 0, TriangleCount, RootNodeBounds, RootCentroidBounds);
        Pool[0].Bounds = RootNodeBounds;

        std::vector<PendingSplit> Stack;
        Stack.push_back(PendingSplit{ 0, 0, TriangleCount, RootCentroidBounds, 0 });

        while (!Stack.empty())
        {
            const PendingSplit Pending = Stack.back();
            Stack.pop_back();

            if (Pending.Depth > DeepestLevel) DeepestLevel = Pending.Depth;

            const auto MakeLeaf = [&Pool](uint32_t NodeIndex, uint32_t Offset, uint32_t Count)
            {
                Pool[NodeIndex].LeafCondition = true;
                Pool[NodeIndex].Offset        = Offset;
                Pool[NodeIndex].Count         = Count;
            };

            if (Pending.Count <= MaxLeafSize || Pending.Depth >= MaxDepth)
            {
                MakeLeaf(Pending.NodeIndex, Pending.Offset, Pending.Count);
                continue;
            }

            // 🔴 THE NODE BOX IS COPIED OUT OF THE POOL, NOT REFERENCED INTO IT. Pool grows by push_back while this loop runs, and a vector that
            //    reallocates invalidates every reference into it. Holding `Pool[i].Bounds` across the child push_backs below is a use-after-free
            //    that only fires once the reserve is exceeded — so it survives every small mesh and corrupts the heap on a large one, which is
            //    exactly how it presented: clean at 12 triangles, 0xC0000374 at 1000.
            const BoundsSix NodeBounds = Pool[Pending.NodeIndex].Bounds;

            const SplitChoice Split = GetOptimalSplit(NodeBounds,
                                                      Pending.CentroidBounds,
                                                      PrimitiveBounds,
                                                      Pending.Offset,
                                                      Pending.Count,
                                                      Options.Strategy,
                                                      BinScratch);

            if (Split.Axis == -1)
            {
                MakeLeaf(Pending.NodeIndex, Pending.Offset, Pending.Count);
                continue;
            }

            const uint32_t SplitOffset = Partition(Result.PrimitiveOrder, PrimitiveBounds, Pending.Offset, Pending.Count, Split);

            // 📝 A partition that put everything on one side means the split separated nothing; the reference makes a leaf rather than recursing
            //    on an identical range, which would not terminate.
            if (SplitOffset == Pending.Offset || SplitOffset == Pending.Offset + Pending.Count)
            {
                MakeLeaf(Pending.NodeIndex, Pending.Offset, Pending.Count);
                continue;
            }

            Pool[Pending.NodeIndex].SplitAxis = static_cast<uint32_t>(Split.Axis);

            const uint32_t LeftStart = Pending.Offset;
            const uint32_t LeftCount = SplitOffset - Pending.Offset;
            const uint32_t RightStart = SplitOffset;
            const uint32_t RightCount = Pending.Count - LeftCount;

            // Bounds are computed into locals FIRST and only then assigned into the pool, so no reference into Pool is alive across its growth.
            BoundsSix LeftNodeBounds,  LeftCentroidBounds;
            BoundsSix RightNodeBounds, RightCentroidBounds;
            GetBounds(PrimitiveBounds, LeftStart,  LeftCount,  LeftNodeBounds,  LeftCentroidBounds);
            GetBounds(PrimitiveBounds, RightStart, RightCount, RightNodeBounds, RightCentroidBounds);

            const uint32_t LeftIndex = static_cast<uint32_t>(Pool.size());
            Pool.push_back(BuildNode{});
            const uint32_t RightIndex = static_cast<uint32_t>(Pool.size());
            Pool.push_back(BuildNode{});

            Pool[LeftIndex].Bounds  = LeftNodeBounds;
            Pool[RightIndex].Bounds = RightNodeBounds;

            Pool[Pending.NodeIndex].Left  = LeftIndex;
            Pool[Pending.NodeIndex].Right = RightIndex;

            // Pushed right-then-left so the left child is popped first, matching the reference's depth-first order and therefore its node numbering.
            Stack.push_back(PendingSplit{ RightIndex, RightStart, RightCount, RightCentroidBounds, Pending.Depth + 1 });
            Stack.push_back(PendingSplit{ LeftIndex,  LeftStart,  LeftCount,  LeftCentroidBounds,  Pending.Depth + 1 });
        }
    }

    // ─── pack : flatten the pool into the 32-byte node form ─────────────────────────────────────────────────────────────────────────────────
    // 📝 The packed layout, from buildUtils.js:
    //      words 0..5 : the six bounds floats
    //      word 6     : leaf -> the primitive offset;  interior -> the RELATIVE index from this node to its right child
    //      word 7     : leaf -> count in the low 16 bits and 0xFFFF in the high;  interior -> the split axis
    //    The left child is always the very next node, which is why only the right child needs storing.
    Result.NodeWords.assign(Pool.size() * GeometryTreeWordsPerNode, 0u);
    if (Options.DynamicCondition)
        Result.ParentTable.assign(Pool.size(), GeometryTreeNoParent);

    uint32_t LeafTally = 0;

    // 📝 Subtree sizes for every pool node, computed ONCE bottom-up. The emission below needs each node's left-subtree size to place its right
    //    child, and asking for it per node during emission would rewalk that subtree every time — quadratic on a real mesh (a 15k-triangle Suzanne
    //    has ~3k interior nodes and would rewalk millions of nodes). Children always occupy higher pool indices than their parent, because the
    //    build pushes them after it, so a single descending sweep resolves every size in one pass.
    std::vector<uint32_t> SubtreeSize(Pool.size(), 1u);
    for (size_t Index = Pool.size(); Index-- > 0; )
    {
        if (!Pool[Index].LeafCondition)
            SubtreeSize[Index] = 1u + SubtreeSize[Pool[Index].Left] + SubtreeSize[Pool[Index].Right];
    }

    // Emit depth-first, left subtree fully before the right, assigning final indices as we go — the reference's _populateBuffer, iterative here for
    // the same stack reason as the build.
    {
        std::vector<uint32_t> WriteStack;      // build-pool indices awaiting emission
        std::vector<uint32_t> SlotStack;       // the final node slot each one was promised
        std::vector<uint32_t> ParentStack;     // the final slot of each one's parent

        WriteStack.push_back(0);
        SlotStack.push_back(0);
        ParentStack.push_back(GeometryTreeNoParent);

        while (!WriteStack.empty())
        {
            const uint32_t PoolIndex = WriteStack.back();  WriteStack.pop_back();
            const uint32_t Slot      = SlotStack.back();   SlotStack.pop_back();
            const uint32_t Parent    = ParentStack.back(); ParentStack.pop_back();

            const BuildNode& Node = Pool[PoolIndex];
            const size_t     Base = static_cast<size_t>(Slot) * GeometryTreeWordsPerNode;

            for (int32_t Index = 0; Index < 6; ++Index)
            {
                float        Value = Node.Bounds.Value[Index];
                uint32_t     Word  = 0;
                static_assert(sizeof(Word) == sizeof(Value), "float and uint32_t must be the same width to punt bits between them");
                std::memcpy(&Word, &Value, sizeof(Word));
                Result.NodeWords[Base + static_cast<size_t>(Index)] = Word;
            }

            if (!Result.ParentTable.empty())
                Result.ParentTable[Slot] = Parent;

            if (Node.LeafCondition)
            {
                Result.NodeWords[Base + 6] = Node.Offset;
                Result.NodeWords[Base + 7] = (GeometryTreeLeafFlag << 16) | (Node.Count & 0xFFFFu);
                ++LeafTally;
                continue;
            }

            // 🔴 Both child slots are derived from THIS node's slot alone — never from a running cursor. A depth-first emission lays a subtree down
            //    contiguously starting at its own slot, so the left child always sits immediately after its parent and the right child clears the
            //    whole left subtree. A shared mutable cursor double-counts: the parent would advance it across its entire subtree and each child
            //    would then advance it again across theirs, so slots collide and run past the end of NodeWords.
            const uint32_t LeftSlot  = Slot + 1;
            const uint32_t RightSlot = LeftSlot + SubtreeSize[Node.Left];

            Result.NodeWords[Base + 6] = RightSlot - Slot;   // relative, as the reference stores it
            Result.NodeWords[Base + 7] = Node.SplitAxis;

            WriteStack.push_back(Node.Right);
            SlotStack.push_back(RightSlot);
            ParentStack.push_back(Slot);

            WriteStack.push_back(Node.Left);
            SlotStack.push_back(LeftSlot);
            ParentStack.push_back(Slot);
        }
    }

    Result.NodeCount      = static_cast<uint32_t>(Pool.size());
    Result.LeafCount      = LeafTally;
    Result.MaximumDepth   = DeepestLevel;
    Result.ReadyCondition = true;

    return true;
}

} // namespace Frontier
