/*==============================================================================================================================================
                                                          COLORPALETTEDESCRIPTOR.H
==============================================================================================================================================*/
// 🧩 Named colour set. Kept flat + aligned so a palette reads at a glance and a second palette (light mode, high-contrast) is one more
//    instance of the SAME struct — never a parallel hardcoded set. Owned here so a palette can be authored without pulling in metrics.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_THEME_COLORPALETTEDESCRIPTOR_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_THEME_COLORPALETTEDESCRIPTOR_H

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ColorPaletteDescriptor
{
    ImU32 DeskBackground;       // [-] - Outer desk / dead space behind panels
    ImU32 PanelBackground;      // [-] - Panel body fill
    ImU32 PanelHeader;          // [-] - Section header strip fill
    ImU32 PanelBorder;          // [-] - Panel + separator lines
    ImU32 ControlBackground;    // [-] - Slider / entry track fill
    ImU32 ControlHovered;       // [-] - Control fill under cursor
    ImU32 ControlActive;        // [-] - Control fill while dragging / pressed
    ImU32 AccentPrimary;        // [-] - Selection + active-tab + slider grab
    ImU32 AccentSubtle;         // [-] - Hover tint of the accent
    ImU32 SelectionMarker;      // [-] - Blue selection accent (dropdown radio + hover bar)
    ImU32 TextPrimary;          // [-] - Default label text
    ImU32 TextMuted;            // [-] - Secondary / disabled text
    ImU32 TextOnAccent;         // [-] - Text drawn over an accent fill

    // 📝 Pill / value-box tones — the split "[ grey axis | black number | grey unit ]" look every numeric row shares.
    ImU32 ValueNumberSegment;   // [-] - Fully-black centre segment holding the editable number
    ImU32 ValueSideSegment;     // [-] - Lighter-grey side segments (axis letter / unit suffix)
    ImU32 ValueOutline;         // [-] - White hairline outline around a pill
    ImU32 ValueText;            // [-] - Bright numeric readout text
    ImU32 SliderTrack;          // [-] - Pitch-black slider track fill
    ImU32 SliderFill;           // [-] - Lighter-grey travelled portion of a slider
    ImU32 SliderKnob;           // [-] - White circular slider knob / segmented-pill fill
    ImU32 KnobText;             // [-] - Text drawn over a white knob / selected pill
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Resolve the built-in dark palette. A light / high-contrast palette is a sibling Resolve* returning the SAME struct.
ColorPaletteDescriptor ResolveDarkPalette();

}   // namespace Frontier

#endif
