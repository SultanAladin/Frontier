/*==============================================================================================================================================
                                                          COMPASSCONFIGURATION.H
==============================================================================================================================================*/
// 🧩 The layout + visual constants of the spatial compass overlay, ported 1:1 from the HTML mockup's CSS token set and rig geometry. Margins and
//    the widget footprint place it top-right of a viewport surface (the mockup's `#gizmo{top:26px;right:26px;width:190px}`); the cube half-extent,
//    the rig push-back, and the two perspective focal lengths reproduce `.cube{96px}`, `translateZ(-60px)`, and `perspective:640px / 4000px`. The
//    transition duration matches the CSS `.42s` eased snap. Header-only data so the projection + intersection + trigger units share one source.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_COMPASSCONFIGURATION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_COMPASSCONFIGURATION_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Every geometric + timing constant the overlay needs, one field per mockup CSS token. Defaults reproduce the mockup at
//    1.0 scale; the caller scales UiScale-wise by multiplying WidgetPixels / CubeHalfExtent before record time if desired.
struct CompassConfiguration
{
    // widget placement (mockup #gizmo)
    float MarginRight   = 26.0f;    // [px] - Gap from the surface right edge
    float MarginTop     = 26.0f;    // [px] - Gap from the surface top edge
    float WidgetPixels  = 170.0f;   // [px] - The square cube stage side (.stage 170px)

    // cube geometry (mockup .cube / .face)
    float CubeHalfExtent = 48.0f;   // [px] - Half a face edge (.cube 96px -> half 48px)
    float RigPushBack    = 60.0f;   // [px] - translateZ(-60px) on the rig before rotation

    // projection focal lengths (mockup perspective)
    float FocalPerspective  = 640.0f;   // [px] - perspective:640px  (persp)
    float FocalOrthographic = 4000.0f;  // [px] - perspective:4000px (near-parallel ortho)

    // control row (mockup .ctlRow: Home + projection toggle share one pill)
    float ControlRowGap    = 12.0f;   // [px] - Gap between the cube stage and the control row
    float ControlButton    = 36.0f;   // [px] - .ctlBtn diameter
    float ControlPadding   = 3.0f;    // [px] - .ctlRow inner padding
    float ControlSeparator = 1.0f;    // [px] - .ctlSep width
    float ControlRounding   = 22.0f;  // [px] - .ctlRow border-radius (rounded pill)

    // interaction
    float DragThreshold  = 4.0f;    // [px] - Pointer travel before a press becomes an orbit drag (mockup >4)
    float OrbitSpeed     = 0.6f;    // [deg/px] - Drag sensitivity (mockup dx*0.6)
    float SnapTolerance  = 26.0f;   // [deg] - Free-orbit release snaps to a named view within this (mockup bd<26)

    // eased transition (mockup .rig transition .42s)
    float TransitionDuration = 0.42f;  // [s] - Snap ease duration
};

}   // namespace Frontier

#endif
