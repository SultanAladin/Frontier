/*==============================================================================================================================================
                                                            CAROUSELOFFSET.H
==============================================================================================================================================*/
// 🧩 The translate that turns one Travel value into the two slides' screen positions. Extracted from the console shell because both the first
//    slide (rail + action grid) and the second (probe + options) read the SAME offset — the track is one double-width surface sliding under a
//    clip, not two panes swapped — and a second copy of the arithmetic is a second chance for the two halves to disagree about where the seam is.
//
//    🔴 The track is 2 * CardWidth wide and slides by -Travel * CardWidth: at Travel 0 the first slide fills the clip, at Travel 1 the second
//       does, and in between BOTH are partly visible, which is the slide the reader sees. A pane that computed its own left edge from a boolean
//       "which slide is current" would pop rather than slide. Every pane asks here instead.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PANE_CAROUSELOFFSET_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PANE_CAROUSELOFFSET_H

#include "imgui.h"

#include "../Token/MetricsSpecification.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which slide a pane belongs to. The rail and action grid are FirstSlide; the probe and options are SecondSlide. The offset resolver reads this
//    to place the pane's left edge, so a pane names its slide once and never touches Travel itself.
enum class ConsoleSlide
{
    FirstSlide,    // rail + action grid
    SecondSlide,   // probe + options
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The horizontal screen offset a pane on Slide draws at, given the surface's left edge and the eased Travel. The two slides differ by exactly one
// CardWidth, so a caller adds this to its own within-slide column x and never re-derives the seam.
[[nodiscard]] float ResolveCarouselOffset(ConsoleSlide                Slide,
                                          float                       Travel,
                                          const MetricsSpecification& Metrics);

} // namespace Frontier

#endif
