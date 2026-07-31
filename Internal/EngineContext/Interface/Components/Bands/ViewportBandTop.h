/*==============================================================================================================================================
                                                              VIEWPORTBANDTOP.H
==============================================================================================================================================*/
// 🧩 The 52 px chrome band above a viewport canvas (CadModellingInterface.html `.vp-topbar`): a leading title cluster (glyph + name + faint
//    subtitle) and a trailing tool cluster, separated by elastic space. The band paints its fill, bottom hairline, and the title cluster; the
//    caller records its own pills into the trailing cluster between Begin and End, so the band never hardcodes which controls a workspace shows.
//    Stateless — the modeling, sketch, UV, and paint viewports all band identically.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_BANDS_VIEWPORTBANDTOP_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_BANDS_VIEWPORTBANDTOP_H

#include "imgui.h"

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The band's leading cluster. TitleText is the primary name; SubtitleText is the faint run beneath it (`.vp-sub`);
//    IconTexture draws the glyph ahead of both (`.vp-ic`) and is omitted when zero.
struct ViewportBandTopDescriptor
{
    const char* Identifier;          // [-]  - Unique id for the band region
    const char* TitleText;           // [-]  - Primary name (`.vp-name`); null omits the cluster text
    const char* SubtitleText;        // [-]  - Faint secondary run (`.vp-sub`); null omits it
    ImTextureID IconTexture;         // [-]  - Leading glyph (`.vp-ic`); 0 omits it

    // 📝 Width the trailing cluster needs, so the band can right-align it (`justify-content:space-between`) on the SAME cycle it
    //    is recorded — the caller knows its own pills, and passing the span keeps the band stateless and free of a frame's lag.
    //    0 leaves the cursor immediately after the title cluster instead of right-aligning.
    float       TrailingClusterSpan; // [px] - Total width of the controls the caller will record
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The band's fixed height in pixels (`height:52px`). A viewport reserves this before sizing its canvas.
float ResolveViewportBandTopHeight();

// 📝 Open the band: paint the fill + hairline, emit the title cluster, and leave the cursor in the trailing cluster with the
//    pills vertically centred. Records the caller's controls in between. Pair with EndViewportBandTop.
void BeginViewportBandTop(const ThemeConfiguration& Theme, const ViewportBandTopDescriptor& Descriptor);

// 📝 Screen position where the LEADING cluster continues — just past the glyph and title text, vertically centred for a control.
//    A caller that wants a pill in the leading cluster (the mockup puts the Views pill there, replacing a static label) parks the
//    cursor here instead of guessing an offset. Valid only between Begin and End.
ImVec2 ResolveViewportBandTopLeadingCursor();

// 📝 Close the band opened by BeginViewportBandTop and advance the cursor past it to where the canvas begins.
void EndViewportBandTop(const ThemeConfiguration& Theme);

}   // namespace Frontier

#endif
