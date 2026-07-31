/*==============================================================================================================================================
                                                          SPATIALCOMPASSCONTEXT.H
==============================================================================================================================================*/
// 🧩 The record-time payload the overlay consumes, plus the persistent state a viewport owns across cycles. The context borrows the hosting draw
//    list, the surface rectangle to place the widget within, the live camera to read + write, the theme for colours, and the frame delta for the
//    eased snap. The state holds the six projected face patches, the persistent selection, the drag/roll interaction bookkeeping, the eased
//    transition, and the two control-row triggers — everything the mockup kept in module-scope script variables. Header-only.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_SPATIALCOMPASSCONTEXT_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_SPATIALCOMPASSCONTEXT_H

#include "Configuration/CompassConfiguration.h"
#include "Topology/CompassPartition.h"
#include "Intersection/IntersectionResult.h"
#include "Projection/TransformAlignment.h"
#include "AuxiliaryTriggers/CameraResetTrigger.h"
#include "AuxiliaryTriggers/ProjectionModeToggle.h"

#include "../../Navigation/Camera/CameraConfiguration.h"
#include "../Theme/ThemeConfiguration.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything one record cycle needs and nothing it owns. DrawList is the hosting window's draw list (primitives appended,
//    no pipeline / barrier). SurfaceMin/Max is the viewport surface rect the widget places itself into (top-right corner).
//    Camera is the live orbit camera to read (project the cube) + write (a face pick / drag / snap moves it). Theme carries
//    the palette. DeltaSeconds advances the eased snap. SurfaceHovered gates input to when the pointer is over the surface.
struct SpatialCompassContext
{
    ImDrawList*                DrawList       = nullptr;  // [-]  - hosting window's draw list (borrowed)
    ImVec2                     SurfaceMin;                // [px] - viewport surface top-left (screen space)
    ImVec2                     SurfaceMax;                // [px] - viewport surface bottom-right (screen space)
    ViewportCamera*            Camera         = nullptr;  // [-]  - live orbit camera (read + written)
    const ThemeConfiguration*  Theme          = nullptr;  // [-]  - resolved palette (borrowed)
    float                      DeltaSeconds   = 0.0f;     // [s]  - frame delta for the eased snap
    bool                       SurfaceHovered = true;     // [-]  - pointer is over the surface (gates input)
};


// 📝 The persistent per-viewport state. Configuration is the ported CSS constants; Partition holds the six face patches
//    (built once, re-projected each cycle). SelectedPreset is the user's persistent face pick (the mockup's `.selected`),
//    Count when none. The drag bookkeeping mirrors the mockup's pointer capture (defer the orbit until travel exceeds the
//    threshold so a plain click still lands on a face). Transition drives the eased snap. Reset + Projection are the two
//    control-row triggers.
struct SpatialCompassState
{
    CompassConfiguration Configuration;                           // [-]  - ported CSS constants
    CompassPartition     Partition;                               // [-]  - the six face patches
    AlignmentPreset      SelectedPreset = AlignmentPreset::Count; // [-]  - persistent face pick (Count = none)

    // drag bookkeeping (mockup pointerdown/move/up with the >4px capture defer)
    bool   PointerHeld    = false;   // [-]  - a press is in progress
    bool   DragEngaged    = false;   // [-]  - travel passed the threshold -> orbiting
    ImVec2 PressPoint;               // [px] - where the press began
    float  PressYaw       = 0.0f;    // [rad]- camera Yaw at press
    float  PressPitch     = 0.0f;    // [rad]- camera Pitch at press

    OrientationTransition Transition;                             // [-]  - the eased snap
    CameraResetTrigger    ResetTrigger;                           // [-]  - Home button
    ProjectionModeToggle  ProjectionToggle;                       // [-]  - Perspective/Orthographic toggle
    bool                  Initialized = false;                    // [-]  - Partition built yet?

    // 📝 Last values the sync trace reported, so it fires on CHANGE rather than every frame (a 60fps line buries the
    //    transitions it exists to show). Diagnostic only — nothing in the projection or interaction path reads these.
    float TracedYaw       = 0.0f;    // [rad]- camera Yaw at the last emitted trace line
    float TracedPitch     = 0.0f;    // [rad]- camera Pitch at the last emitted trace line
    bool  TracedOrthographic = false; // [-] - lens at the last emitted trace line
    bool  TraceReported   = false;    // [-] - false until the first line is emitted (so the opening pose always prints)
};

}   // namespace Frontier

#endif
