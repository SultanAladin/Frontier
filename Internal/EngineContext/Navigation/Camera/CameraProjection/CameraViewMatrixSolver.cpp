/*==============================================================================================================================================
                                                          CAMERAVIEWMATRIXSOLVER.CPP
==============================================================================================================================================*/
// 🧩 Orbit spec → orbit orientation. The eye is placed on the sphere around Target by composing a yaw rotation about world up (+Z) with a pitch
//    rotation about the yawed right axis, then walking Distance out from the target along the resulting forward. The view basis falls out of the same
//    rotation; the world→view matrix is a plain look-at through the shared algebra. Right-handed Z-up throughout (CoordinateSpace.h).

#include "CameraViewMatrixSolver.h"
#include "../../../MetricSpace/CoordinateSpace.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// The orbit orientation: yaw about world up (+Z), then pitch about the yawed right axis. Composed yaw · pitch so pitch stays
// about the eye's own right after the yaw turns it. At zero angles this is identity, so forward is the base -Y direction.
//
// The pitch angle is NEGATED into the +X rotation. Base forward is -Y; a right-handed turn about +X takes +Y→+Z, so a raw
// +Pitch would send forward toward -Z and place the eye BELOW the ground (Eye.z = Target.z + Distance·sin Pitch) — the
// inverted pose the default -0.6 pitch produced (eye at z ≈ -8.6, looking up through the floor). Negating restores the DCC
// convention this spec documents: NEGATIVE pitch lifts the eye ABOVE the ground looking down, positive drops it below.
[[nodiscard]] Quaternionf ResolveOrbitRotation(float Yaw, float Pitch) noexcept
{
    const Quaternionf YawRotation   = QuaternionFromAxisAngle(ReferenceUpAxis(), Yaw);
    const Quaternionf PitchRotation = QuaternionFromAxisAngle(ReferenceRightAxis(), -Pitch);
    return NormalizeQuaternion(MultiplyQuaternion(YawRotation, PitchRotation));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

Vector3f EvaluateObserverPosition(const ViewportCamera& Camera) noexcept
{
    // Base forward at zero angles is -Y (eye sits behind the target looking toward +Y). The orbit rotation swings it around
    // the sphere; the eye is Distance out from the target along the OPPOSITE of that forward (i.e. along +back).
    const Quaternionf Rotation    = ResolveOrbitRotation(Camera.Yaw, Camera.Pitch);
    const Vector3f    BaseForward = ScaleVector(ReferenceForwardAxis(), -1.0f); // -Y
    const Vector3f    Forward     = RotateVector(Rotation, BaseForward);
    return SubtractVector(Camera.Target, ScaleVector(Forward, Camera.Distance));
}

FocalOrientation SolveOrbitOrientation(const ViewportCamera& Camera) noexcept
{
    const Quaternionf Rotation    = ResolveOrbitRotation(Camera.Yaw, Camera.Pitch);
    const Vector3f    BaseForward = ScaleVector(ReferenceForwardAxis(), -1.0f); // -Y at zero angles

    FocalOrientation Orientation;
    Orientation.EyeForward  = NormalizeVector(RotateVector(Rotation, BaseForward));
    Orientation.EyePosition = SubtractVector(Camera.Target, ScaleVector(Orientation.EyeForward, Camera.Distance));

    // The view-right is forward × world-up (falls back to the yawed right axis at the poles, where that cross degenerates).
    Vector3f Right = CrossVector(Orientation.EyeForward, ReferenceUpAxis());
    if (VectorLength(Right) <= 1e-5f)
        Right = RotateVector(Rotation, ReferenceRightAxis());
    Orientation.EyeRight = NormalizeVector(Right);
    Orientation.EyeUp    = NormalizeVector(CrossVector(Orientation.EyeRight, Orientation.EyeForward));

    Orientation.ViewMatrix = ConstructLookAt(Orientation.EyePosition, Camera.Target, ReferenceUpAxis());
    return Orientation;
}

} // namespace Frontier
