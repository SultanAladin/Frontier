/*==============================================================================================================================================
                                                    SKETCHMODELGROUNDPROJECTION.H
==============================================================================================================================================*/
// 🧩 The shared camera ↔ ground projection seam for every canvas overlay that has to place authored CAD geometry (workplanes, sketch primitives)
//    onto the viewport. SketchModelViewport.exe wires no GPU sketch bridge, so a plane / shape is drawn as an ImGui DrawList overlay projected by
//    the ONE viewport camera. The forward map (world mm → canvas pixel) and the inverse map (cursor pixel → ground world mm at Z = 0) both live
//    here so the workplane overlay and the primitive-draw unit share ONE chain and can never drift apart. The chain mirrors
//    AssembleSketchModelGridConstants exactly (SolveOrbitOrientation + EvaluateProjectionFrame + MultiplyMatrix), so a projected point sits
//    precisely where the analytic ground grid says its world point is.
//
//    🔴 The CAD model is authored in MILLIMETRES; this viewport renders in METRES. Every world coordinate is scaled mm→m at projection, and a
//       ground hit read back is scaled m→mm, so a caller never juggles units — it speaks authored mm on both sides.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELGROUNDPROJECTION_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELGROUNDPROJECTION_H

#include "imgui.h"

#include "EngineContext/Math/LinearAlgebra_Float32.h"

namespace SketchModelViewportValidation
{

// The panel state carrying the one viewport camera; defined in SketchModelViewportPanel.h (forward-declared to keep this header includable
// without a cycle — the projection functions only take it by const reference).
struct SketchModelViewportState;

// 🔴 The authored-mm → render-metres scale. The CAD model is 200 mm sheets / 10 mm grid; the viewport camera lives at ~6-18 m. Projecting mm
//    raw puts a default sheet 400 m across, outside the frustum, and nothing draws. Every world coordinate is multiplied by this before projecting.
constexpr float MillimetresToMetres = 0.001f;

// 📝 One projected world point: the canvas pixel it lands on + whether it is IN FRONT of the eye (clip w > 0). A behind-eye endpoint makes its
//    whole primitive undrawable — a naive divide by a negative w mirrors the point to the wrong side of the screen — so the flag gates drawing.
struct ProjectedPoint
{
    ImVec2 Pixel;
    bool   InFront;
};

// Project a world point (authored mm) through a prebuilt view-projection to a canvas pixel. Scales mm→m internally; InFront is false for a
// behind-eye point (the caller drops the primitive rather than smearing it). Column-major Column[c][r], with the projection's Vulkan Y-down
// negation already folded into the matrix, so NDC-y grows downward like the screen.
ProjectedPoint ProjectWorldPoint(const Frontier::Matrix4f& ViewProjection, ImVec2 CanvasOrigin, ImVec2 CanvasSize,
                                 float WorldX, float WorldY, float WorldZ);

// Build the view-projection from State.Viewport.Camera and project a ground point (authored mm, Z = 0) to a canvas pixel. The convenience form
// for a caller that projects a handful of points; a caller projecting many should build the matrix once and call ProjectWorldPoint directly.
ProjectedPoint ProjectGroundMillimetres(const SketchModelViewportState& State, ImVec2 CanvasOrigin, ImVec2 CanvasSize, float MmX, float MmY);

// Build the view-projection from State.Viewport.Camera. Exposed so a caller stroking many points builds it once (the per-point ProjectWorldPoint
// takes the matrix), rather than paying SolveOrbitOrientation + EvaluateProjectionFrame + MultiplyMatrix per point through ProjectGroundMillimetres.
Frontier::Matrix4f AssembleGroundViewProjection(const SketchModelViewportState& State);

// 📝 Cast the cursor onto the ground plane (world Z = 0), returning the hit in authored MILLIMETRES. False when the ray is parallel to the ground
//    or points away from it (a near-horizon graze), so the caller holds its last valid point rather than snapping to infinity.
bool CastCursorToGroundMillimetres(const SketchModelViewportState& State, ImVec2 CanvasOrigin, ImVec2 CanvasSize, ImVec2 Pixel,
                                   float& OutMmX, float& OutMmY);

}   // namespace SketchModelViewportValidation

#endif
