/*==============================================================================================================================================
                                                            VIEWPORTBANDBOTTOM.H
==============================================================================================================================================*/
// 🧩 The 30 px chrome band below a viewport canvas (CadModellingInterface.html `.vp-footer`): a leading monospace navigation hint, an optional
//    trailing coordinate readout, and a caller-recorded control cluster pinned right. The band paints its darker fill, top hairline, and both
//    text runs; the caller records its own compact pills between Begin and End — whose menus open UPWARD so they overhang the canvas rather than
//    falling off the window. Stateless — one definition foots every viewport.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_BANDS_VIEWPORTBANDBOTTOM_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_BANDS_VIEWPORTBANDBOTTOM_H

#include "imgui.h"

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ViewportBandBottomDescriptor
{
    const char* Identifier;          // [-]  - Unique id for the band region
    const char* NavigationHintText;  // [-]  - Leading dim hint (`.vp-hint`); null omits it
    const char* CoordinateText;      // [-]  - Readout drawn just before the control cluster (`.vp-coord`); null omits it

    // 📝 Width the trailing control cluster needs, so the band right-aligns it on the SAME cycle it is recorded (the caller
    //    knows its own pills). 0 leaves the cursor after the hint instead of right-aligning.
    float       TrailingClusterSpan; // [px] - Total width of the controls the caller will record
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The band's fixed height in pixels (`--footer-h`, 30 px in the mockup). A viewport reserves this before sizing its canvas.
float ResolveViewportBandBottomHeight();

// 📝 Open the band: paint the fill + top hairline, emit the hint and coordinate runs, and leave the cursor in the trailing
//    cluster with the controls vertically centred. Pair with EndViewportBandBottom.
void BeginViewportBandBottom(const ThemeConfiguration& Theme, const ViewportBandBottomDescriptor& Descriptor);

// 📝 Close the band opened by BeginViewportBandBottom.
void EndViewportBandBottom(const ThemeConfiguration& Theme);

}   // namespace Frontier

#endif
