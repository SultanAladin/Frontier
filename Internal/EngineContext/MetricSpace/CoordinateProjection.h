/*==============================================================================================================================================
                                                            COORDINATEPROJECTION.H
==============================================================================================================================================*/
// 🧩 A placed local frame within the world: a position (metres) and an orientation (unit quaternion) that together map points and directions
//    between an item's own space and the shared right-handed Z-up world (CoordinateSpace.h). WorldToLocal / LocalToWorld carry positions (the
//    translation applies); the Direction variants carry directions (rotation only, no translation). The three axis accessors read the item's own
//    right / up / forward by rotating the reference axes. Struct + free functions over our LinearAlgebra types — no glm, header-only, POD.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_METRICSPACE_COORDINATEPROJECTION_H
#define FRONTIER_ENGINECONTEXT_METRICSPACE_COORDINATEPROJECTION_H

#include "../Math/LinearAlgebra_Float32.h"
#include "CoordinateSpace.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// A local frame placed in the world. Position is in metres; Orientation is a unit quaternion (identity = axis-aligned with
// the world reference frame).
struct CoordinateProjection
{
    Vector3f    Position;                                   // [m] - Frame origin in world space
    Quaternionf Orientation = Quaternionf{ 0, 0, 0, 1 };   // [-] - World rotation of the frame (unit)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    POINT / DIRECTION TRANSFORMS
//------------------------------------------------------------------------------------------------------------------------

// World point → this frame's local space (undo translation, then undo rotation via the conjugate).
[[nodiscard]] inline Vector3f WorldToLocal(const CoordinateProjection& Frame, const Vector3f& WorldPoint) noexcept
{
    const Quaternionf Inverse{ -Frame.Orientation.XCoord, -Frame.Orientation.YCoord, -Frame.Orientation.ZCoord, Frame.Orientation.WCoord };
    return RotateVector(Inverse, SubtractVector(WorldPoint, Frame.Position));
}

// Local point → world space (rotate into the world, then add the frame origin).
[[nodiscard]] inline Vector3f LocalToWorld(const CoordinateProjection& Frame, const Vector3f& LocalPoint) noexcept
{
    return AddVector(RotateVector(Frame.Orientation, LocalPoint), Frame.Position);
}

// World direction → local direction (rotation only; translation does not apply to directions).
[[nodiscard]] inline Vector3f WorldDirectionToLocal(const CoordinateProjection& Frame, const Vector3f& WorldDirection) noexcept
{
    const Quaternionf Inverse{ -Frame.Orientation.XCoord, -Frame.Orientation.YCoord, -Frame.Orientation.ZCoord, Frame.Orientation.WCoord };
    return RotateVector(Inverse, WorldDirection);
}

// Local direction → world direction (rotation only).
[[nodiscard]] inline Vector3f LocalDirectionToWorld(const CoordinateProjection& Frame, const Vector3f& LocalDirection) noexcept
{
    return RotateVector(Frame.Orientation, LocalDirection);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAME AXES
//------------------------------------------------------------------------------------------------------------------------

// The frame's own right / up / forward, its orientation applied to the world reference axes (CoordinateSpace.h).
[[nodiscard]] inline Vector3f RightAxis(const CoordinateProjection& Frame) noexcept
{
    return RotateVector(Frame.Orientation, ReferenceRightAxis());
}

[[nodiscard]] inline Vector3f UpAxis(const CoordinateProjection& Frame) noexcept
{
    return RotateVector(Frame.Orientation, ReferenceUpAxis());
}

[[nodiscard]] inline Vector3f ForwardAxis(const CoordinateProjection& Frame) noexcept
{
    return RotateVector(Frame.Orientation, ReferenceForwardAxis());
}

} // namespace Frontier

#endif
