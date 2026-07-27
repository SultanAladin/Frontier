/*==============================================================================================================================================
                                                          SURFACEPATCHDEFINITION.H
==============================================================================================================================================*/
// 🧩 One clickable/renderable quad of the cube — a single face. Holds the four cube-space corners (before rotation), the preset it frames, and,
//    once projected, the four screen points + a depth key for painter ordering and the polygon-inside test. A 1:1 port of the mockup's six
//    `.f-*` faces: each face is a 96px quad pushed out ±48px along one axis. Header-only POD; the intersection + projection units fill and read it.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_SURFACEPATCHDEFINITION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_SURFACEPATCHDEFINITION_H

#include "../Configuration/AlignmentPreset.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A cube-space point (before the rig rotation), in the mockup's pixel units where the cube half-edge is 48.
struct CubeSpacePoint
{
    float X = 0.0f;   // [px] - Cube-local X (right +)
    float Y = 0.0f;   // [px] - Cube-local Y (up +)
    float Z = 0.0f;   // [px] - Cube-local Z (toward viewer at identity +)
};


// 📝 One face patch. Corners are the untransformed cube-space quad; the preset is the view a click frames. OutwardNormal is the
//    face's cube-space outward direction, used for backface rejection (a face pointing away from the viewer is not clickable).
struct SurfacePatchDefinition
{
    AlignmentPreset Preset;          // [-] - View this face frames when clicked
    CubeSpacePoint  Corner[4];       // [px] - The four cube-space quad corners (CCW when seen from outside)
    CubeSpacePoint  OutwardNormal;   // [-] - Cube-space outward face direction

    // Filled per cycle by the projection unit:
    ImVec2          ScreenCorner[4]; // [px] - Projected screen positions of the four corners
    float           DepthKey;        // [-] - Mean projected depth (larger = nearer the viewer)
    bool            FacingViewer;    // [-] - True when the outward normal points toward the camera (front face)
};

}   // namespace Frontier

#endif
