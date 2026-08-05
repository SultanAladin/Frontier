/*==============================================================================================================================================
                                                        SKETCHMODELVIEWPORTPANEL.H
==============================================================================================================================================*/
// 🧩 App-local composition for the SketchModelViewport validation host. Owns one persistent ViewportPanelState (perspective, grid + axis +
//    spatial compass enabled) across cycles PLUS the Phase-1 GPU-grid seam: a GroundGridPass and an offscreen colour surface the pass draws
//    into. The viewport, the compass, and the grid all read ONE camera — Viewport.Camera (a render-canonical Frontier::ViewportCamera driven
//    by the shared Navigation verbs) — so there is no bridge and no convention drift. The host records the grid into the surface before the
//    ImGui frame, then feeds the surface's ImGui texture id into ViewportPanelState.RenderedTexture so the shared ConstructViewportPanel blits
//    the real GPU grid instead of the ImDrawList placeholder. The SpatialCompass overlay still draws on top of the same camera.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELVIEWPORTPANEL_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELVIEWPORTPANEL_H

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"
#include "EngineContext/Interface/WorkspaceHost/Viewport/ViewportPanel.h"
#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include "Graphics/Grid/GroundGridPass.h"

#include "SketchModelOffscreenSurface.h"
#include "SketchModelSummonedSurfaces.h"
#include "SketchModelViewportChrome.h"

#include <cstdint>
#include <vector>

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Caller-owned state for the validation panel. Wraps the shared ViewportPanelState plus the GPU-grid resources. Viewport.Camera is the
//    single source of truth — navigation, the compass, and the grid math all read it, so nothing can drift.
struct SketchModelViewportState
{
    Frontier::ViewportPanelState Viewport;      // [-]  - Shared perspective viewport (the one camera + grid + axis + compass)

    Frontier::GroundGridPass     Grid;          // [-]  - GPU analytic ground grid (line + dot + axis layers)
    SketchModelOffscreenSurface  Surface;       // [-]  - Offscreen colour target the grid draws into, sampled by ImGui

    SketchModelChromeState       Chrome;        // [-]  - Band chrome selections (grid layers, scene unit, view caption)

    // 📝 The two surfaces summoned over the canvas: the directory + inspector card (Tab) and the action console (right-click). Both are embedded
    //    whole; this panel only reports the canvas rect the console's summon is armed over and records them after the viewport column.
    SketchModelSummonedState     Summoned;      // [-]  - Directory card + action console state (both closed at rest)

    // 📝 The canvas rect the bands left for the scene, reported back by ConstructSketchModelViewportPanel so the host can size
    //    the offscreen surface to the CANVAS rather than the whole framebuffer (the bands occupy 52 + 30 px of it).
    uint32_t CanvasWidth  = 0u;                 // [px] - Canvas width  the grid should render at
    uint32_t CanvasHeight = 0u;                 // [px] - Canvas height the grid should render at

    // 📝 This frame's extruded prism / thin-wall tessellation, rebuilt by PublishSketchModelSolidScene and published through the store bridge for
    //    the GPU matcap pass. Kept in the state (rather than a local) so the vectors retain their capacity frame to frame instead of reallocating.
    std::vector<Frontier::ParametricSketchShapeBody> SolidBodies;   // [-] - tessellated solid bodies, empty until something is extruded
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Initialize the panel state once: a perspective viewport with grid, axis, and the spatial compass all enabled, plus both summoned surfaces in
//    their opening pose (an EMPTY directory + EMPTY history, and a closed action console). The GroundGridPass +
//    offscreen surface are brought up separately by the host (they need the VulkanHost).
void InitializeSketchModelViewportSample(SketchModelViewportState& State);

// 📝 Refresh the camera aspect ratio from the surface extent so the grid frames exactly what the panel navigates. Call each frame before
//    AssembleSketchModelGridConstants.
void ConformSketchModelAspect(SketchModelViewportState& State, uint32_t SurfaceWidth, uint32_t SurfaceHeight);

// 📝 Fill the grid push constants from the viewport camera + the Settings layer toggles (replicates RenderExtension's AssembleGridConstants).
Frontier::GroundGridConstants AssembleSketchModelGridConstants(const SketchModelViewportState& State);

// 🔴 Publish this frame's EXTRUDE payload for the GPU matcap pass: the scene view (camera + canvas rect) and the tessellated prism / thin-wall
//    bodies, both through the store's file-scope bridges. Called from inside the canvas child, where the rect and the one camera are both known.
//
//    The published ViewProjection carries a ×0.01 UNIT FOLD. ParametricSketchSolidSequence uploads every position × 0.1 (authored mm → cm, what the
//    CadMain sketch camera expects), but this viewport's camera is in METRES — so the matrix converts that cm back to m. Without the fold the solid
//    renders 100× oversized and fills the frustum. The compensation lives here, in the caller's matrix, so the shared sequence stays correct for
//    CadMain and the editor. Non-const State: tessellating warms the store's flatten caches and refills State.SolidBodies.
void PublishSketchModelSolidScene(SketchModelViewportState& State, ImVec2 CanvasOrigin, ImVec2 CanvasSize);

// 📝 Composite the matcap solid image the host rendered into the canvas at exactly CanvasOrigin/CanvasSize (the target is canvas-sized, so a 1:1
//    blit with no aspect correction; its transparent clear lets the grid show through). A no-op when nothing was rendered — the first frame, or
//    whenever no shape is extruded. Call right after ConstructViewportPanel's grid blit and before the CPU shape overlay strokes the outlines.
void CompositeSketchModelSolidImage(ImVec2 CanvasOrigin, ImVec2 CanvasSize);

// 📝 Record the full viewport column for this cycle: the 52 px band (title cluster + Views/Settings pills), the canvas hosting the
//    rendered grid + spatial compass, the 30 px footer band (navigation hint + coordinate readout + Units pill), and finally the two
//    summoned surfaces over the canvas — the directory card on Tab and the action console on right-click. Writes the
//    canvas extent back into State so the host can size the offscreen surface to it. Returns the shared viewport result.
Frontier::ViewportPanelResult ConstructSketchModelViewportPanel(const Frontier::ThemeConfiguration& Theme,
                                                                const Frontier::SvgIconRegistry&    Icons,
                                                                SketchModelViewportState&            State);

}   // namespace SketchModelViewportValidation

#endif
