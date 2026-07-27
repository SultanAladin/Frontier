/*==============================================================================================================================================
                                                          COORDINATEPROJECTION.H
==============================================================================================================================================*/
// 🧩 The CPU 3D→2D projection that draws the cube — a verbatim port of the mockup's CSS rig transform. A cube-space point is rotated the SAME way
//    the CSS rig rotates (rotateY(Azimuth) then rotateX(Elevation)), pushed back along Z (translateZ(-RigPushBack)), then divided by the CSS
//    perspective focal length to yield a screen offset from the widget centre. This is exactly `translateZ(-60px) rotateX(ax) rotateY(ay)` with a
//    640px / 4000px perspective — no matrices, no GPU pass. Header-only; the .cpp projects the six faces through it each cycle.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_COORDINATEPROJECTION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_COORDINATEPROJECTION_H

#include "../Topology/SurfacePatchDefinition.h"

#include "imgui.h"

#include <math.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The per-cycle projection inputs: the live rig angles (derived from the camera), the widget centre in screen pixels, the
//    rig push-back, and the perspective focal length. One of these is built each cycle and threaded through every projection.
struct ProjectionContext
{
    float  ElevationDeg;   // [deg] - Rig rotateX (mockup `ax`)
    float  AzimuthDeg;     // [deg] - Rig rotateY (mockup `ay`)
    ImVec2 WidgetCentre;   // [px]  - Screen position of the cube centre
    float  RigPushBack;    // [px]  - translateZ(-RigPushBack) before perspective divide
    float  FocalLength;    // [px]  - CSS perspective focal length (640 persp / 4000 ortho)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Rotate a cube-space point exactly as the CSS rig does. CSS composes outer→inner, so a point sees rotateY (azimuth) first,
//    then rotateX (elevation) — a direct port of the mockup's `rotatePoint(p, elevDeg, azimDeg)`. Returns the rotated point.
inline CubeSpacePoint RotateCubePoint(const CubeSpacePoint& Point, float ElevationDeg, float AzimuthDeg)
{
    const float DegToRad = 0.01745329252f;
    const float Elev = ElevationDeg * DegToRad;
    const float Azim = AzimuthDeg   * DegToRad;

    // rotateY (azimuth) — spins around the vertical axis
    const float X1 =  Point.X * cosf(Azim) + Point.Z * sinf(Azim);
    const float Z1 = -Point.X * sinf(Azim) + Point.Z * cosf(Azim);
    const float Y1 =  Point.Y;

    // rotateX (elevation) — tilts around the horizontal axis
    const float Y2 = Y1 * cosf(Elev) - Z1 * sinf(Elev);
    const float Z2 = Y1 * sinf(Elev) + Z1 * cosf(Elev);
    const float X2 = X1;

    return CubeSpacePoint{ X2, Y2, Z2 };
}

// 📝 Project a rotated cube-space point to a screen position through the CSS perspective divide. The rig pushes the point back
//    by RigPushBack along -Z, then CSS divides screen offset by (FocalLength / (FocalLength - ZTowardViewer)); screen Y is
//    inverted (down is +). Returns the screen point; ProjectedDepth (larger = nearer) rides out for painter ordering.
inline ImVec2 ProjectCubePoint(const CubeSpacePoint& Rotated, const ProjectionContext& Context, float& ProjectedDepth)
{
    // translateZ(-RigPushBack): the whole rig sits RigPushBack behind the perspective origin. Larger Z is toward the viewer.
    const float ZTowardViewer = Rotated.Z - Context.RigPushBack;
    ProjectedDepth            = ZTowardViewer;

    // CSS perspective divide: an element at depth z is scaled by focal / (focal - z_toward_viewer).
    float Denominator = Context.FocalLength - ZTowardViewer;
    if (Denominator < 1.0f) { Denominator = 1.0f; }         // guard against the point crossing the eye
    const float Scale = Context.FocalLength / Denominator;

    return ImVec2(Context.WidgetCentre.x + Rotated.X * Scale,
                  Context.WidgetCentre.y - Rotated.Y * Scale);
}

// 📝 Project a whole face patch: rotate + project its four corners, fill ScreenCorner[], set DepthKey to the mean depth, and
//    resolve FacingViewer from the rotated outward normal (points toward the viewer -> front-facing -> clickable).
inline void ProjectSurfacePatch(SurfacePatchDefinition& Patch, const ProjectionContext& Context)
{
    float DepthSum = 0.0f;
    for (int Corner = 0; Corner < 4; ++Corner)
    {
        const CubeSpacePoint Rotated = RotateCubePoint(Patch.Corner[Corner], Context.ElevationDeg, Context.AzimuthDeg);
        float Depth = 0.0f;
        Patch.ScreenCorner[Corner] = ProjectCubePoint(Rotated, Context, Depth);
        DepthSum += Depth;
    }
    Patch.DepthKey = DepthSum * 0.25f;

    const CubeSpacePoint RotatedNormal = RotateCubePoint(Patch.OutwardNormal, Context.ElevationDeg, Context.AzimuthDeg);
    Patch.FacingViewer = RotatedNormal.Z > 0.0f;   // +Z after rotation points toward the viewer
}

}   // namespace Frontier

#endif
