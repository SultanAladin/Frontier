/*==============================================================================================================================================
                                                           INTERSECTIONSOLVER.CPP
==============================================================================================================================================*/
// 🧩 The Möller–Trumbore ray/triangle intersection. The canonical algorithm: solve P(t) = (1-u-v)A + uB + vC for (t, u, v) by
//    Cramer's rule on the edge vectors, using the scalar triple product as the shared determinant. Both windings accepted
//    (the determinant sign is not gated), so a face is pickable from either side; only the parallel case (|det| ~ 0) and
//    out-of-triangle / behind-origin solutions are rejected.

#include "IntersectionSolver.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ResolveRayTriangleIntersection(const Ray&      CastRay,
                                    const Vector3d& VertexA,
                                    const Vector3d& VertexB,
                                    const Vector3d& VertexC,
                                    double&         HitDistance,
                                    double&         HitU,
                                    double&         HitV) noexcept
{
    constexpr double Tolerance = 1e-9;

    const Vector3d EdgeAB = SubtractVector(VertexB, VertexA);
    const Vector3d EdgeAC = SubtractVector(VertexC, VertexA);

    // 📝 Determinant = (Direction × EdgeAC) · EdgeAB. Near zero => ray parallel to the triangle plane, no single solution.
    const Vector3d PerpendicularVector = CrossProduct(CastRay.Direction, EdgeAC);
    const double   Determinant         = DotProduct(EdgeAB, PerpendicularVector);
    if (Determinant > -Tolerance && Determinant < Tolerance)
        return false;

    const double InverseDeterminant = 1.0 / Determinant;

    // 📝 Barycentric U weights VertexB: U = ((Origin - A) · (Direction × EdgeAC)) / det, valid only in [0, 1].
    const Vector3d OriginToA = SubtractVector(CastRay.Origin, VertexA);
    const double   BarycentricU = DotProduct(OriginToA, PerpendicularVector) * InverseDeterminant;
    if (BarycentricU < 0.0 || BarycentricU > 1.0)
        return false;

    // 📝 Barycentric V weights VertexC: V = (Direction · ((Origin - A) × EdgeAB)) / det, with U + V <= 1 inside the triangle.
    const Vector3d CrossOriginEdge = CrossProduct(OriginToA, EdgeAB);
    const double   BarycentricV    = DotProduct(CastRay.Direction, CrossOriginEdge) * InverseDeterminant;
    if (BarycentricV < 0.0 || BarycentricU + BarycentricV > 1.0)
        return false;

    // 📝 Parametric distance along the ray; reject hits behind the origin.
    const double Distance = DotProduct(EdgeAC, CrossOriginEdge) * InverseDeterminant;
    if (Distance < Tolerance)
        return false;

    HitDistance = Distance;
    HitU        = BarycentricU;
    HitV        = BarycentricV;
    return true;
}

} // namespace Frontier
