/*==============================================================================================================================================
                                                             FRUSTUMBOUNDARY.H
==============================================================================================================================================*/
// 🧩 Extracts the six view-frustum cull planes from a view-projection matrix (Gribb–Hartmann). A visibility / culling consumer intersects world
//    bounds against these to reject off-screen geometry before it is drawn. Independent of the orbit spec — it takes whatever view-projection the
//    caller assembled, so it serves perspective and orthographic cameras alike. Each plane is stored so its normal points INTO the frustum and a
//    point is inside when SignedDistanceToPlane >= 0.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_FRUSTUMBOUNDARY_H
#define FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_FRUSTUMBOUNDARY_H

#include "../../../Math/LinearAlgebra_Float32.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A plane in the form Normal·P + Offset = 0, normal pointing into the frustum (inside → SignedDistanceToPlane >= 0).
struct FrustumPlane
{
    Vector3f Normal;          // [-]  - Unit inward normal
    float    Offset = 0.0f;   // [m]  - Plane constant (d in n·p + d = 0)
};

// 📝 The six bounding planes of the view frustum, indexed by side. A world point is inside the frustum when it lies on the
//    inward side of all six.
struct FrustumBoundary
{
    FrustumPlane Left;     // [-] - +X-most clip plane
    FrustumPlane Right;    // [-] - -X-most clip plane
    FrustumPlane Bottom;   // [-] - +Y-most clip plane
    FrustumPlane Top;      // [-] - -Y-most clip plane
    FrustumPlane Near;     // [-] - Near clip plane
    FrustumPlane Far;      // [-] - Far clip plane
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Extract the six normalized inward-facing planes from a view-projection matrix (proj · view). Gribb–Hartmann rows.
[[nodiscard]] FrustumBoundary ExtractFrustumBoundary(const Matrix4f& ViewProjection) noexcept;

// Signed distance from a world point to a plane; >= 0 means the point is on the inward (inside) side.
[[nodiscard]] float SignedDistanceToPlane(const FrustumPlane& Plane, const Vector3f& WorldPoint) noexcept;

// True when a world-space sphere (centre + radius) lies at least partly inside all six planes — the coarse cull test.
[[nodiscard]] bool SphereInsideFrustum(const FrustumBoundary& Boundary, const Vector3f& Centre, float Radius) noexcept;

} // namespace Frontier

#endif
