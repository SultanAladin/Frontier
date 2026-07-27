/*==============================================================================================================================================
                                                              ALIGNMENTPRESET.H
==============================================================================================================================================*/
// 🧩 The seven named orientations a CAD spatial compass snaps to — the six axis-aligned faces plus the isometric home. Each preset is one entry
//    in the orientation table (Elevation / Azimuth degrees) that both the projected cube geometry and the camera snap read. A 1:1 port of the
//    HTML mockup's VIEWS table (ViewOrientationCube.html): front{0,0} back{0,180} right{0,-90} left{0,90} top{-90,0} bottom{90,0} iso{-30,-45}.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_ALIGNMENTPRESET_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_ALIGNMENTPRESET_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A standard viewing angle. The six axis faces are selectable + framed; Isometric is the Home preset. Count is a sentinel.
enum class AlignmentPreset
{
    Front,        // [-] - Looks toward +Y   (Elevation 0,   Azimuth 0)
    Back,         // [-] - Looks toward -Y   (Elevation 0,   Azimuth 180)
    Right,        // [-] - Looks toward -X   (Elevation 0,   Azimuth -90)
    Left,         // [-] - Looks toward +X   (Elevation 0,   Azimuth 90)
    Top,          // [-] - Looks straight down (Elevation -90, Azimuth 0)
    Bottom,       // [-] - Looks straight up   (Elevation 90,  Azimuth 0)
    Isometric,    // [-] - The Home 3/4 view   (Elevation -30, Azimuth -45)
    Count         // [-] - Sentinel: number of presets
};

// 📝 Which zone of the compass a cursor evaluation resolved to. The port exposes the six faces only (edges/corners were removed
//    from the mockup); None means the cursor missed every clickable patch this cycle.
enum class CompassZone
{
    None,         // [-] - Cursor is over no clickable patch
    Face          // [-] - Cursor is over one of the six named faces
};

}   // namespace Frontier

#endif
