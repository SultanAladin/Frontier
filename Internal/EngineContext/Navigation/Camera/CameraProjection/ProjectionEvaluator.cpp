/*==============================================================================================================================================
                                                          PROJECTIONEVALUATOR.CPP
==============================================================================================================================================*/
// 🧩 Lens fields → projection matrix. A thin selector over the shared algebra's ConstructPerspective / ConstructOrthographic, plus the aspect
//    refresh. No state of its own; the spec is the source of truth.

#include "ProjectionEvaluator.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

Matrix4f EvaluateProjectionFrame(const ViewportCamera& Camera) noexcept
{
    if (Camera.Projection == ProjectionMode::Orthographic)
        return ConstructOrthographic(Camera.OrthographicHalfHeight, Camera.AspectRatio, Camera.NearPlane, Camera.FarPlane);
    return ConstructPerspective(Camera.FieldOfView, Camera.AspectRatio, Camera.NearPlane, Camera.FarPlane);
}

void ConformCameraAspect(ViewportCamera& Camera, uint32_t FramebufferWidth, uint32_t FramebufferHeight) noexcept
{
    const uint32_t SafeHeight = FramebufferHeight == 0 ? 1 : FramebufferHeight;
    Camera.AspectRatio = static_cast<float>(FramebufferWidth) / static_cast<float>(SafeHeight);
}

float EvaluateOrthographicHalfHeight(const ViewportCamera& Camera) noexcept
{
    // Clamp the half-angle well inside a right angle: tan blows up at π/2, and a degenerate FOV would hand the projection an
    // infinite extent that collapses the whole matrix.
    constexpr float HalfAngleCeiling = 1.5533431f;                          // [rad] - ~89°, keeps tan finite
    float HalfAngle = Camera.FieldOfView * 0.5f;
    if (HalfAngle > HalfAngleCeiling) { HalfAngle = HalfAngleCeiling; }
    if (HalfAngle < 1e-4f)            { HalfAngle = 1e-4f; }

    const float HalfHeight = Camera.Distance * std::tan(HalfAngle);
    return HalfHeight < 1e-4f ? 1e-4f : HalfHeight;                         // never zero — an empty frustum draws nothing
}

void ConformOrthographicExtent(ViewportCamera& Camera) noexcept
{
    if (Camera.Projection != ProjectionMode::Orthographic) return;
    Camera.OrthographicHalfHeight = EvaluateOrthographicHalfHeight(Camera);
}

void AlignProjectionMode(ViewportCamera& Camera, ProjectionMode Mode) noexcept
{
    Camera.Projection = Mode;
    ConformOrthographicExtent(Camera);
}

} // namespace Frontier
