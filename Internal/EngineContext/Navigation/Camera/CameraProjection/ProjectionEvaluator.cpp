/*==============================================================================================================================================
                                                          PROJECTIONEVALUATOR.CPP
==============================================================================================================================================*/
// 🧩 Lens fields → projection matrix. A thin selector over the shared algebra's ConstructPerspective / ConstructOrthographic, plus the aspect
//    refresh. No state of its own; the spec is the source of truth.

#include "ProjectionEvaluator.h"

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

} // namespace Frontier
