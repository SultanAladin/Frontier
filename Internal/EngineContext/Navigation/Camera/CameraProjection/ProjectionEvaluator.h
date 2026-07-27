/*==============================================================================================================================================
                                                          PROJECTIONEVALUATOR.H
==============================================================================================================================================*/
// 🧩 Derives the render PROJECTION frame from the orbit spec's lens fields. Perspective reads FieldOfView; Orthographic reads OrthographicHalfHeight;
//    both read AspectRatio + the clip range. Kept separate from the view frame so a consumer that only needs the projection (a picking ray, an
//    ortho gizmo) takes just this. Pure function of the spec — recompute when the lens or aspect changes. ConformCameraAspect refreshes AspectRatio
//    from a framebuffer extent so the caller need not divide by hand.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_PROJECTIONEVALUATOR_H
#define FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_PROJECTIONEVALUATOR_H

#include "../CameraConfiguration.h"
#include "../../../Math/LinearAlgebra_Float32.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Derive the view→clip projection matrix from the spec's lens fields (perspective FOV or orthographic half-height) + aspect
// + clip range. Vulkan clip convention (depth 0..1) via the shared algebra.
[[nodiscard]] Matrix4f EvaluateProjectionFrame(const ViewportCamera& Camera) noexcept;

// Refresh AspectRatio in place from a framebuffer extent (guards a zero height). Call on resize before evaluating.
void ConformCameraAspect(ViewportCamera& Camera, uint32_t FramebufferWidth, uint32_t FramebufferHeight) noexcept;

} // namespace Frontier

#endif
