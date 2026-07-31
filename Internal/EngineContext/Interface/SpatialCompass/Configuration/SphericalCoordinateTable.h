/*==============================================================================================================================================
                                                          SPHERICALCOORDINATETABLE.H
==============================================================================================================================================*/
// 🧩 The pre-computed orientation table: one (Elevation, Azimuth) pair per AlignmentPreset, plus the derived (Yaw, Pitch) camera pose each preset
//    frames. A verbatim port of the HTML mockup's `VIEWS` object. Elevation/Azimuth are the cube RIG angles (rotateX / rotateY in degrees); the
//    live orbit camera is the INVERSE of the rig, so the framed camera pose is Yaw = -Azimuth, Pitch = -Elevation (see the mapping note below).
//    Header-only data — the projection, intersection, and snap units all read it so the seven views are defined exactly once.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_SPHERICALCOORDINATETABLE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_SPHERICALCOORDINATETABLE_H

#include "AlignmentPreset.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One row of the orientation table. Elevation/Azimuth are the RIG angles (degrees) the mockup rotates the cube to; Label is
//    the uppercase face caption. Selectable is false for Isometric (Home is triggered by the control row, not a face pick).
struct AlignmentEntry
{
    AlignmentPreset Preset;         // [-]   - Which named view
    float           ElevationDeg;   // [deg] - Rig rotateX (mockup `ax`)
    float           AzimuthDeg;     // [deg] - Rig rotateY (mockup `ay`)
    const char*     Label;          // [-]   - Uppercase caption ("FRONT" ...)
    bool            Selectable;     // [-]   - True for the six faces, false for Isometric
};


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The orientation table, in AlignmentPreset order. Values copied 1:1 from ViewOrientationCube.html `VIEWS`.
inline const AlignmentEntry& ResolveAlignmentEntry(AlignmentPreset Preset)
{
    static const AlignmentEntry Table[static_cast<int>(AlignmentPreset::Count)] =
    {
        { AlignmentPreset::Front,       0.0f,    0.0f, "FRONT",  true  },
        { AlignmentPreset::Back,        0.0f,  180.0f, "BACK",   true  },
        { AlignmentPreset::Right,       0.0f,  -90.0f, "RIGHT",  true  },
        { AlignmentPreset::Left,        0.0f,   90.0f, "LEFT",   true  },
        { AlignmentPreset::Top,       -90.0f,    0.0f, "TOP",    true  },
        { AlignmentPreset::Bottom,     90.0f,    0.0f, "BOTTOM", true  },
        { AlignmentPreset::Isometric, -30.0f,  -45.0f, "ISO",    false },
    };
    return Table[static_cast<int>(Preset)];
}

// 📝 The RIG angles the cube itself should display for the live camera — the EXACT inverse of ResolvePresetCameraPose below, so a
//    preset snap and the cube's own display agree on every axis. Azimuth = -Yaw (the yaw axis inverts: turning the camera right
//    spins the cube left). Elevation = +Pitch does NOT invert, because this orbit camera already reads negative Pitch as an
//    ELEVATED eye (CameraViewMatrixSolver negates Pitch into its +X rotation) — the rig's -90 "looking down from above" and the
//    camera's -90 pitch are the SAME sign, not opposites.
//
// 🐞 This previously read Elevation = -Pitch, which contradicted ResolvePresetCameraPose's Pitch = +Elevation: the two were
//    inverses of each other, so every non-zero elevation displayed FLIPPED. Clicking TOP snapped the camera overhead correctly
//    but drew the cube as if seen from below (BOTTOM toward the viewer), and a drag-orbit release then snapped to the mirrored
//    preset. Only Front/Back/Right/Left (elevation 0) were unaffected, which is why the break read as "the cube is out of sync".
inline void ResolveDisplayAngles(float CameraYawRadians,
                                 float CameraPitchRadians,
                                 float& ElevationDegrees,
                                 float& AzimuthDegrees)
{
    const float RadToDeg = 57.2957795f;
    ElevationDegrees     =  CameraPitchRadians * RadToDeg;
    AzimuthDegrees       = -CameraYawRadians   * RadToDeg;
}

// 📝 The framed orbit pose for a preset: resolve the camera Yaw/Pitch (radians) that frames the preset head-on. Yaw = -Azimuth
//    (the azimuth axis round-trips through the display mapping). Pitch = +Elevation: this orbit camera reads positive Pitch as
//    an ELEVATED eye (looking down), so the Top row (Elevation -90 rig) must snap the eye ABOVE -> Pitch -90 puts the eye over
//    the scene looking down and lands the TOP face toward the viewer. e.g. Top -> Pitch -90; Bottom -> Pitch +90; Front -> 0.
inline void ResolvePresetCameraPose(AlignmentPreset Preset, float& YawRadians, float& PitchRadians)
{
    const AlignmentEntry& Entry = ResolveAlignmentEntry(Preset);
    const float DegToRad = 0.01745329252f;
    YawRadians   = -Entry.AzimuthDeg   * DegToRad;
    PitchRadians =  Entry.ElevationDeg * DegToRad;
}

}   // namespace Frontier

#endif
