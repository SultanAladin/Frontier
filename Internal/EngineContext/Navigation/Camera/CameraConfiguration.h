/*==============================================================================================================================================
                                                            CAMERACONFIGURATION.H
==============================================================================================================================================*/
// 🧩 The viewport camera as a spherical-orbit specification — the DCC/CAD standard (Blender / Maya / Fusion). Rather than a free world pose, the
//    camera is a Target point the eye orbits, a Distance out from it, and Yaw / Pitch angles that place the eye on the surrounding sphere; the eye
//    position and view basis are DERIVED from those each frame (ViewFrameEvaluation), never stored as the source of truth. This keeps orbit, pan,
//    and dolly as three clean edits to Target / angles / Distance. The lens (perspective or orthographic) and clip range ride alongside so the
//    projection frame derives from the same struct. Header-only spec; the navigation, projection, constraint, and transition units all read it.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_CAMERACONFIGURATION_H
#define FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_CAMERACONFIGURATION_H

#include "../../Math/LinearAlgebra_Float32.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             ENUMS
//------------------------------------------------------------------------------------------------------------------------

// Which projection the camera derives. Perspective is the default; Orthographic serves precise CAD / ortho views.
enum class ProjectionMode
{
    Perspective,    // [-] - FieldOfView drives a perspective frustum (DEFAULT)
    Orthographic    // [-] - OrthographicHalfHeight drives a parallel projection
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The spherical-orbit camera. Target is the world point the eye looks at and orbits; Distance is how far the eye sits out
//    from it; Yaw turns the eye around the world up (+Z) and Pitch raises / lowers it. At Yaw = Pitch = 0 the eye sits behind
//    the target along -Y looking toward +Y with +Z overhead (see CoordinateSpace.h). The lens fields + clip range let the
//    projection frame derive from the same spec. Nothing here is a matrix — ViewFrameEvaluation / ProjectionEvaluation
//    synthesize those on demand so the spec stays the single source of truth.
struct ViewportCamera
{
    Vector3f       Target;                                      // [m]   - World point the eye orbits + looks at
    float          Distance              = 6.0f;               // [m]   - Eye distance out from Target
    float          Yaw                   = 0.0f;               // [rad] - Orbit angle about world up (+Z)
    float          Pitch                 = -0.35f;             // [rad] - Orbit elevation (negative looks slightly down)

    ProjectionMode Projection            = ProjectionMode::Perspective; // [-] - Lens model
    float          FieldOfView           = 1.0471976f;         // [rad] - Vertical FOV (~60°) for Perspective
    float          OrthographicHalfHeight = 5.0f;              // [m]   - Half vertical world extent for Orthographic
    float          AspectRatio           = 1.7777778f;         // [-]   - Width / height (16:9 default)
    float          NearPlane             = 0.01f;              // [m]   - Near clip
    float          FarPlane              = 1000.0f;            // [m]   - Far clip
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// A sensible default perspective orbit camera: the eye starts ABOVE the Z-up ground grid, pulled well back and pitched down so the
// whole radial scene (heads on a ring ~6.5 m out, lifted ~1.7 m) is framed from a raised three-quarter vantage. Target sits a little
// above the floor at the ring centre; Distance clears the ring and Pitch tilts the eye down onto the scene. The pose the RenderExtension boots into.
[[nodiscard]] inline ViewportCamera ResolveDefaultPerspectiveCamera() noexcept
{
    ViewportCamera Camera;
    Camera.Target                = Vector3f{ 0.0f, 0.0f, 1.5f };
    Camera.Distance              = 18.0f;
    Camera.Yaw                   = 0.0f;
    Camera.Pitch                 = -0.6f;
    Camera.Projection            = ProjectionMode::Perspective;
    Camera.FieldOfView           = 1.0471976f;
    return Camera;
}

// The same orbit pose in an orthographic lens for CAD / ortho views. HalfHeight is derived from the default distance through the
// SAME identity ProjectionEvaluator uses — Distance · tan(FOV/2) — so this boots framing exactly what the perspective default
// frames. (A plain Distance · 0.5 was used here once; it disagrees with the 60° FOV, whose tan(30°) is 0.577, so the two defaults
// framed ~15% differently and a lens toggle visibly rescaled the scene.) Kept inline rather than calling ProjectionEvaluator so
// this spec header stays dependency-free; ConformOrthographicExtent is the runtime path and computes the identical value.
[[nodiscard]] inline ViewportCamera ResolveDefaultOrthographicCamera() noexcept
{
    ViewportCamera Camera         = ResolveDefaultPerspectiveCamera();
    Camera.Projection             = ProjectionMode::Orthographic;
    Camera.OrthographicHalfHeight = Camera.Distance * std::tan(Camera.FieldOfView * 0.5f);
    return Camera;
}

} // namespace Frontier

#endif
