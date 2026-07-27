/*==============================================================================================================================================
                                                          CAMERAVIEWMATRIXSOLVER.H
==============================================================================================================================================*/
// 🧩 Solves the render VIEW from the orbit spec. The camera stores Target / Distance / Yaw / Pitch, not an eye — this unit turns those into
//    the observer position, the world-space view basis (forward / right / up), and the world→view matrix. Orbit math lives here so navigation only
//    edits the angles and never touches matrices. Right-handed Z-up world (CoordinateSpace.h): at Yaw = Pitch = 0 the eye sits behind the target
//    along -Y looking toward +Y with +Z overhead. Everything is a pure function of the spec, recomputed each cycle — no cached eye to drift.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_CAMERAVIEWMATRIXSOLVER_H
#define FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_CAMERAVIEWMATRIXSOLVER_H

#include "../CameraConfiguration.h"
#include "../../../Math/LinearAlgebra_Float32.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The solved orbit orientation: the observer eye + its world-space basis + the world→view matrix. Navigation reads
//    EyeRight / EyeUp to pan in the view plane; the render pass reads ViewMatrix. All recomputed from the spec by SolveOrbitOrientation.
struct FocalOrientation
{
    Vector3f EyePosition;    // [m]  - World observer, derived from Target + Distance + angles
    Vector3f EyeForward;     // [-]  - Unit direction eye → target
    Vector3f EyeRight;       // [-]  - Unit view-right (world), the pan U axis
    Vector3f EyeUp;          // [-]  - Unit view-up (world), the pan V axis
    Matrix4f ViewMatrix;     // [-]  - World → view
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Evaluate the observer position on the orbit sphere from the spec's Target / Distance / Yaw / Pitch. Pure; no matrix built.
[[nodiscard]] Vector3f EvaluateObserverPosition(const ViewportCamera& Camera) noexcept;

// Solve the full orbit orientation (observer + basis + world→view matrix) from the spec. Recompute each cycle after edits.
[[nodiscard]] FocalOrientation SolveOrbitOrientation(const ViewportCamera& Camera) noexcept;

} // namespace Frontier

#endif
