/*==============================================================================================================================================
                                                            CAMERANAVIGATION.H
==============================================================================================================================================*/
// 🧩 The four ways the orbit camera is driven from pointer / key deltas, each one a small edit to the spec. Orbit turns Yaw / Pitch (drag);
//    Pan slides Target in the view plane (shift-drag); Dolly changes Distance (scroll); Fly walks Target along the view basis (WASD). Every verb
//    reapplies its constraint (ConstrainPitch after orbit, ConstrainDistance after dolly) so the caller never has to. They all mutate the same
//    ViewportCamera and derive the view basis internally through ViewFrameEvaluation, so navigation never touches a matrix. One cohesive unit:
//    the four operations share the derived-basis helper and none is independently swappable, so they live together rather than one file each.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_CAMERANAVIGATION_H
#define FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_CAMERANAVIGATION_H

#include "../CameraConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Orbit: turn the eye around the target. YawDelta / PitchDelta are angle deltas in radians (already scaled by sensitivity by
// the caller). Pitch is clamped away from the poles (ConstrainPitch). Positive YawDelta turns the view right; positive
// PitchDelta raises it.
void OrbitViewportCamera(ViewportCamera& Camera, float YawDelta, float PitchDelta) noexcept;

// Pan: slide the target (and with it the eye) within the current view plane. RightDelta / UpDelta are world-cm distances
// along the view-right / view-up axes. Distance-scaled panning is the caller's choice — it passes larger deltas when farther out.
void PanViewportCamera(ViewportCamera& Camera, float RightDelta, float UpDelta) noexcept;

// Dolly: move the eye toward / away from the target by changing Distance. DistanceDelta is a world-cm change (negative pulls
// in, positive pushes out). Clamped to the positive orbit range (ConstrainDistance).
void DollyViewportCamera(ViewportCamera& Camera, float DistanceDelta) noexcept;

// Fly: walk the target through the world along the view basis (the WASD complement to orbit). ForwardDelta / RightDelta /
// UpDelta are world-cm distances; Forward / Right follow the view, Up follows world up (+Z). The eye rides along because it
// is derived from the target.
void FlyViewportCamera(ViewportCamera& Camera, float ForwardDelta, float RightDelta, float UpDelta) noexcept;

} // namespace Frontier

#endif
