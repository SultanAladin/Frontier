/*==============================================================================================================================================
                                                              SECTIONHEADER.H
==============================================================================================================================================*/
// 🧩 Title + separator for a UI block, with an optional collapse arrow. The expansion flag is caller-owned so collapse state survives across
//    cycles. Stateless control — one definition heads every card in every panel.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CARDS_SECTIONHEADER_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CARDS_SECTIONHEADER_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SectionHeaderDescriptor
{
    const char* Title;          // [-] - Header caption
    bool*       Expanded;       // [-] - Caller-owned collapse INTENT (null -> always-open, no chevron). The header only reads it to
                                //       decide whether to draw a chevron; the header never writes it (the caller flips it on a click).
    float       FoldFraction;   // [-] - Eased 0..1 reveal fraction the chevron rotates with (0 = collapsed/right, 1 = open/down)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record a section header strip with a fixed-height clickable hit-rect. Returns true only on the frame the strip is CLICKED (one edge
//    per press) so the caller flips its persisted intent once — the header itself is stateless. The chevron rotates with FoldFraction.
bool ConstructSectionHeader(const ThemeConfiguration& Theme, const SectionHeaderDescriptor& Descriptor);

}   // namespace Frontier

#endif
