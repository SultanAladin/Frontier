/*============================================================================================================================================
                                                            FACETRIANGULATION.CPP
============================================================================================================================================*/
// 🧩 The shared face-to-triangle policy. A triangle/quad fans from corner 0. An N-gon (five+ corners) is folded onto its
//    best-fit plane: a Newell normal averages the face's own cross products (stable for non-planar or concave faces), an
//    in-plane orthonormal basis (U, V) is raised from it, every corner is projected to a 2D (U·P, V·P) coordinate, and Earcut
//    triangulates the resulting simple polygon. Earcut's returned indices are already local corner indices, so they pass
//    straight back to the caller. If the face is degenerate (zero-area Newell normal) the fan run is returned as a last resort.

#include "FaceTriangulation.h"

#include <array>
#include "earcut.hpp"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Fan run (0, i, i+1) over a face of CornerCount corners — the fast path for triangles and convex quads, and the
    //    degenerate-normal fallback for anything Earcut cannot fold to a plane.
    std::vector<uint32_t> FanCorners(uint32_t CornerCount)
    {
        std::vector<uint32_t> Triangles;
        if (CornerCount < 3)
            return Triangles;
        Triangles.reserve((size_t)(CornerCount - 2) * 3);
        for (uint32_t Corner = 1; Corner + 1 < CornerCount; ++Corner)
        {
            Triangles.push_back(0);
            Triangles.push_back(Corner);
            Triangles.push_back(Corner + 1);
        }
        return Triangles;
    }

    // 📝 Newell's method: sum each edge's (Yi-Yj)(Zi+Zj), … cross contribution over the whole loop. Robust for slightly
    //    non-planar or concave faces where a single cross product would flip. The magnitude is twice the projected area.
    Vector3d EvaluateNewellNormal(const std::vector<Vector3d>& CornerPositions)
    {
        Vector3d Normal = { 0.0, 0.0, 0.0 };
        const size_t Count = CornerPositions.size();
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const Vector3d& Current = CornerPositions[Index];
            const Vector3d& Next    = CornerPositions[(Index + 1) % Count];
            Normal.XCoord += (Current.YCoord - Next.YCoord) * (Current.ZCoord + Next.ZCoord);
            Normal.YCoord += (Current.ZCoord - Next.ZCoord) * (Current.XCoord + Next.XCoord);
            Normal.ZCoord += (Current.XCoord - Next.XCoord) * (Current.YCoord + Next.YCoord);
        }
        return Normal;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::vector<uint32_t> TriangulateFaceCorners(const std::vector<Vector3d>& CornerPositions)
{
    const uint32_t CornerCount = (uint32_t)CornerPositions.size();
    if (CornerCount < 3)
        return {};
    if (CornerCount <= 4)
        return FanCorners(CornerCount);   // triangle / quad: a fan is exact and cheaper than a plane fold

    // Raise an in-plane orthonormal basis from the Newell normal. A zero-length normal means the face has no area to
    //    project onto (fully collinear corners) — fall back to the fan so the caller always receives a usable run.
    const Vector3d Newell = EvaluateNewellNormal(CornerPositions);
    if (EvaluateVectorLengthSquared(Newell) < 1e-18)
        return FanCorners(CornerCount);
    const Vector3d PlaneNormal = NormalizeVector(Newell);

    // Choose the world axis least aligned with the normal as the seed, so the cross product is well-conditioned.
    Vector3d Seed = { 1.0, 0.0, 0.0 };
    const double AbsX = PlaneNormal.XCoord < 0.0 ? -PlaneNormal.XCoord : PlaneNormal.XCoord;
    const double AbsY = PlaneNormal.YCoord < 0.0 ? -PlaneNormal.YCoord : PlaneNormal.YCoord;
    const double AbsZ = PlaneNormal.ZCoord < 0.0 ? -PlaneNormal.ZCoord : PlaneNormal.ZCoord;
    if (AbsX <= AbsY && AbsX <= AbsZ) Seed = { 1.0, 0.0, 0.0 };
    else if (AbsY <= AbsZ)            Seed = { 0.0, 1.0, 0.0 };
    else                              Seed = { 0.0, 0.0, 1.0 };

    const Vector3d AxisU = NormalizeVector(CrossProduct(PlaneNormal, Seed));
    const Vector3d AxisV = CrossProduct(PlaneNormal, AxisU);   // already unit-length: normal ⟂ U, both unit

    // Project every corner onto (U, V). Earcut consumes one ring (the outer boundary); the face has no holes of its own.
    using EarPoint = std::array<double, 2>;
    std::vector<std::vector<EarPoint>> Rings(1);
    Rings[0].reserve(CornerCount);
    for (const Vector3d& Position : CornerPositions)
        Rings[0].push_back({ DotProduct(Position, AxisU), DotProduct(Position, AxisV) });

    const std::vector<uint32_t> Indices = mapbox::earcut<uint32_t>(Rings);
    if (Indices.size() < 3)
        return FanCorners(CornerCount);   // Earcut declined (e.g. self-intersecting projection) — fan rather than drop the face
    return Indices;
}

} // namespace Frontier
