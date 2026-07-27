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

// A sensible default perspective orbit camera: target at the world origin, eye pulled back and tilted slightly down over the
// Z-up ground plane. The starting pose the RenderExtension boots into.
[[nodiscard]] inline ViewportCamera ResolveDefaultPerspectiveCamera() noexcept
{
    ViewportCamera Camera;
    Camera.Target                = Vector3f{ 0.0f, 0.0f, 0.0f };
    Camera.Distance              = 6.0f;
    Camera.Yaw                   = 0.0f;
    Camera.Pitch                 = -0.35f;
    Camera.Projection            = ProjectionMode::Perspective;
    Camera.FieldOfView           = 1.0471976f;
    return Camera;
}

// The same orbit pose in an orthographic lens for CAD / ortho views. HalfHeight is derived from the default distance so the
// framing matches the perspective default at boot; the caller re-fits it when framing a specific extent.
[[nodiscard]] inline ViewportCamera ResolveDefaultOrthographicCamera() noexcept
{
    ViewportCamera Camera         = ResolveDefaultPerspectiveCamera();
    Camera.Projection             = ProjectionMode::Orthographic;
    Camera.OrthographicHalfHeight = Camera.Distance * 0.5f;
    return Camera;
}

} // namespace Frontier

#endif
