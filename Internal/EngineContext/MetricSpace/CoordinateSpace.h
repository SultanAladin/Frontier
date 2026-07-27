/*==============================================================================================================================================
                                                              COORDINATESPACE.H
==============================================================================================================================================*/
// 🧩 The single source of truth for the world reference frame: right-handed, Z-up, distances in METRES (the engine standard, see DistanceMetric.h).
//    Everything that has to agree on "which way is up" — the orbit camera basis, the analytic ground grid, the origin axes — reads its axes from
//    here instead of hardcoding {0,1,0}. Up is +Z, the ground plane the grid lives on is the XY plane (z = 0), and forward-into-the-scene is +Y.
//    Vulkan's own clip-space handedness is dealt with at the projection matrix, not here; this header only fixes the WORLD convention. Header-only,
//    POD, no link unit — same as LinearAlgebra. Lives under EngineContext/MetricSpace so the metric foundation (frame + unit standard) is owned in one place.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_METRICSPACE_COORDINATESPACE_H
#define FRONTIER_ENGINECONTEXT_METRICSPACE_COORDINATESPACE_H

#include "../Math/LinearAlgebra_Float32.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    REFERENCE AXES (RIGHT-HANDED, Z-UP)
//------------------------------------------------------------------------------------------------------------------------

// The world "up" — +Z. Height climbs along this axis; the ground plane is everything at z = 0.
[[nodiscard]] inline Vector3f ReferenceUpAxis() noexcept
{
    return Vector3f{ 0.0f, 0.0f, 1.0f };
}

// The world "right" — +X. First of the two horizontal (ground-plane) axes.
[[nodiscard]] inline Vector3f ReferenceRightAxis() noexcept
{
    return Vector3f{ 1.0f, 0.0f, 0.0f };
}

// The world "forward-into-the-scene" — +Y. Second of the two horizontal axes; with Right (+X) and Up (+Z) it forms a
// right-handed basis (Right × Forward = Up).
[[nodiscard]] inline Vector3f ReferenceForwardAxis() noexcept
{
    return Vector3f{ 0.0f, 1.0f, 0.0f };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GROUND-PLANE HELPERS
//------------------------------------------------------------------------------------------------------------------------

// The height of a world point above the ground plane (its component along Up), in metres. Grid ray-plane tests intersect
// where this is 0.
[[nodiscard]] inline float ResolveGroundPlaneHeight(const Vector3f& WorldPosition) noexcept
{
    return WorldPosition.ZCoord;
}

// The two horizontal coordinates of a world point, packed for the grid's cell math (X = Right axis, Y = Forward axis).
struct GroundPlaneCoordinate
{
    float U = 0.0f;   // [m] - Along Right (+X)
    float V = 0.0f;   // [m] - Along Forward (+Y)
};

[[nodiscard]] inline GroundPlaneCoordinate ResolveGroundPlaneCoordinate(const Vector3f& WorldPosition) noexcept
{
    return GroundPlaneCoordinate{ WorldPosition.XCoord, WorldPosition.YCoord };
}

} // namespace Frontier

#endif
