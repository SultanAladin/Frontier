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

// 📝 The orthographic half-height that frames EXACTLY what the perspective lens frames at the current orbit distance:
//    HalfHeight = Distance · tan(FieldOfView / 2). This is the identity that makes a lens toggle seamless — at the pivot plane
//    (the orbit Target) the two frustums subtend the same vertical world extent, so the scene neither jumps nor rescales when
//    the projection flips. It is also what keeps a wheel dolly meaningful in orthographic, where Distance alone changes nothing.
[[nodiscard]] float EvaluateOrthographicHalfHeight(const ViewportCamera& Camera) noexcept;

// 📝 Refit OrthographicHalfHeight from the current Distance + FieldOfView. A no-op unless the camera is orthographic, so a
//    navigation verb may call it unconditionally after editing Distance.
void ConformOrthographicExtent(ViewportCamera& Camera) noexcept;

// 📝 Set the lens and refit the ortho extent in ONE place, so the scene framing is preserved across the switch. Every projection
//    toggle in the UI (the spatial compass button, the Views menu row, a keybind) must route through this rather than assigning
//    Camera.Projection directly — that is what keeps the widgets and the rendered view from drifting into disagreement.
void AlignProjectionMode(ViewportCamera& Camera, ProjectionMode Mode) noexcept;

} // namespace Frontier

#endif
