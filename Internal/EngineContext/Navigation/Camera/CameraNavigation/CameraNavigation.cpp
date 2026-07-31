/*==============================================================================================================================================
                                                            CAMERANAVIGATION.CPP
==============================================================================================================================================*/
// 🧩 The four navigation verbs. Orbit + dolly are direct edits to the angle / distance fields with their constraint reapplied. Pan + fly need the
//    current view basis to know which way "right" / "up" / "forward" point in the world, so they ask CameraViewMatrixSolver for it — the one shared
//    helper the flattened unit is built around. All four mutate the spec only; the eye and matrices stay derived, never stored.

#include "CameraNavigation.h"
#include "../CameraProjection/CameraViewMatrixSolver.h"
#include "../CameraProjection/ProjectionEvaluator.h"
#include "../CameraConstraint/PitchBoundary.h"
#include "../CameraConstraint/DistanceBoundary.h"
#include "../../../MetricSpace/CoordinateSpace.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void OrbitViewportCamera(ViewportCamera& Camera, float YawDelta, float PitchDelta) noexcept
{
    Camera.Yaw  += YawDelta;
    Camera.Pitch = ConstrainPitch(Camera.Pitch + PitchDelta);
}

void PanViewportCamera(ViewportCamera& Camera, float RightDelta, float UpDelta) noexcept
{
    // The view basis tells us which world directions "right" and "up" are for the current orbit pose; the target slides
    // along them so the whole framing translates without rotating.
    const FocalOrientation Orientation = SolveOrbitOrientation(Camera);
    Camera.Target = AddVector(Camera.Target, ScaleVector(Orientation.EyeRight, RightDelta));
    Camera.Target = AddVector(Camera.Target, ScaleVector(Orientation.EyeUp,    UpDelta));
}

void DollyViewportCamera(ViewportCamera& Camera, float DistanceDelta) noexcept
{
    Camera.Distance = ConstrainDistance(Camera.Distance + DistanceDelta);

    // 🐞 Distance alone is INVISIBLE to a parallel projection — the ortho matrix reads OrthographicHalfHeight and nothing else,
    //    so before this refit a wheel dolly in orthographic moved the eye along the view axis and changed the picture not at all
    //    (the lens has no foreshortening to reveal it). Refitting the extent from the new Distance is what makes zoom mean the
    //    same thing in both lenses, and it holds the two framings in agreement so a later toggle still does not jump.
    ConformOrthographicExtent(Camera);
}

void FlyViewportCamera(ViewportCamera& Camera, float ForwardDelta, float RightDelta, float UpDelta) noexcept
{
    // Fly walks the pivot through the world: forward / right follow the view, up follows world +Z so ascent stays vertical
    // regardless of pitch (the editor-fly convention). The eye follows because it is derived from Target.
    const FocalOrientation Orientation = SolveOrbitOrientation(Camera);
    Camera.Target = AddVector(Camera.Target, ScaleVector(Orientation.EyeForward, ForwardDelta));
    Camera.Target = AddVector(Camera.Target, ScaleVector(Orientation.EyeRight,   RightDelta));
    Camera.Target = AddVector(Camera.Target, ScaleVector(ReferenceUpAxis(), UpDelta));
}

} // namespace Frontier
