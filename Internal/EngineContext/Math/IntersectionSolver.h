/*==============================================================================================================================================
                                                            INTERSECTIONSOLVER.H
==============================================================================================================================================*/
// 🧩 Ray casting primitives shared by picking. A Ray is an origin + a (not necessarily unit) direction in some space;
//    ResolveRayTriangleIntersection is the Möller–Trumbore test that reports the parametric distance along the ray and the
//    barycentric coordinates of the hit on the triangle. Double precision, reusing the LinearAlgebra_Float64 cross/dot — the authoring
//    math stays double, the float boundary is only the display mirror. This is the CPU half of the hybrid picker (§1.5): the
//    single-click element path casts one ray per object and keeps the nearest hit.
// 📝 Barycentric convention: for triangle (A, B, C) the hit point P = (1 - U - V)·A + U·B + V·C, so U weights vertex B, V
//    weights vertex C, and (1 - U - V) weights vertex A. Component classification reads these three weights to snap to the
//    nearest corner (vertex), opposite edge, or the face itself.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_MATH_INTERSECTIONSOLVER_H
#define FRONTIER_ENGINECONTEXT_MATH_INTERSECTIONSOLVER_H

#include "LinearAlgebra_Float64.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A parametric ray P(t) = Origin + t·Direction, t >= 0 ahead of the origin. Direction need not be unit length; the returned
//    t is in Direction-length units, so callers that want a world distance normalize Direction first (the picker does).
struct Ray
{
    Vector3d Origin    = { 0.0, 0.0, 0.0 };   // [cm] - ray start point
    Vector3d Direction = { 0.0, 0.0, 1.0 };   // [-]  - ray direction (caller chooses unit or not)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Möller–Trumbore ray/triangle test. On a front- or back-facing hit ahead of the origin, writes the parametric distance
// HitDistance (t, in Direction-length units) and the barycentric weights HitU (vertex B) / HitV (vertex C), then returns true.
// Returns false (outputs untouched) when the ray is parallel to the triangle, the hit is outside the triangle, or behind the
// origin (t < 0). Both windings are accepted — picking must hit a face from either side. Tolerance is fixed at 1e-9.
[[nodiscard]] bool ResolveRayTriangleIntersection(const Ray&      CastRay,
                                                  const Vector3d& VertexA,
                                                  const Vector3d& VertexB,
                                                  const Vector3d& VertexC,
                                                  double&         HitDistance,
                                                  double&         HitU,
                                                  double&         HitV) noexcept;

} // namespace Frontier

#endif
