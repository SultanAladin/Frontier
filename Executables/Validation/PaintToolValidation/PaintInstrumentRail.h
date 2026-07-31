/*==============================================================================================================================================
                                                         PAINTINSTRUMENTRAIL.H
==============================================================================================================================================*/
// 🧩 The card's left column on slide 1: ten family rows, each a colour dot, a caption and a tally, with an accent marker on the active one.
//    Ported from Documentation/Prototypes/PaintToolMenu.html's `.rail` / `.rail-item` / `RenderRail()`.
//
//    📝 A family carries a COLOUR DOT rather than a glyph, which the prototype states its reasoning for: the instruments themselves are the
//       artwork on this card, and a second set of line icons beside them would compete with the wells. The dot is also the family's identity
//       across both slides — the same colour reappears in the rail header's band mark and behind the chosen instrument.
//
//    🔴 Draws with the window draw list into a rectangle, NOT into a child window. Same constraint as the shell: ImGui::Begin cannot nest
//       inside BeginChild, and the rail lives inside the shell's clip. Hit-testing is therefore manual and must respect that clip, or an
//       off-card rail row takes clicks while translated out of sight.

#pragma once
#ifndef FRONTIER_VALIDATION_PAINTTOOL_PAINTINSTRUMENTRAIL_H
#define FRONTIER_VALIDATION_PAINTTOOL_PAINTINSTRUMENTRAIL_H

#include "PaintCardShell.h"
#include "PaintCardSpecification.h"
#include "PaintCatalogue.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One phase per family row, so a row that is no longer active can animate its dot back DOWN rather than snapping.
//    Sized to the catalogue's family count and asserted against it at the definition site, so adding a family cannot
//    silently overflow the state.
constexpr int PaintRailRowLimit = 12;


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The rail's animation state, owned by the caller alongside the shell's. Only the dot scale animates; the row's
//    background and text colour are 120 ms CSS transitions the prototype applies to a hover, which ImGui's per-frame
//    hover test reproduces directly without a stored phase.
struct PaintRailState
{
    // 📝 0 = resting dot, 1 = fully grown. Per row rather than one shared phase for the active row, because the
    //    OUTGOING row has to shrink at the same time the incoming one grows — one phase would make the old dot snap.
    float DotPhase[PaintRailRowLimit] = {};   // [-] - per-family dot growth
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Advance every family row's dot growth toward its target for one frame. Separate from drawing for the same reason the
// shell's advance is: the phase is stepped once per frame, not once per reader.
void AdvancePaintRail(PaintRailState& State, const PaintCardMetrics& Metrics, int ActiveFamilyIndex, float DeltaSeconds);

// Draw the ten family rows into a pane region and report which row was clicked, or -1 for none.
// 📝 Returns the clicked index rather than mutating a selection, so the caller owns what a click MEANS — the prototype's
//    handler also re-renders the grid and resets its fade, and burying that inside the rail would hide it.
[[nodiscard]] int ConstructPaintInstrumentRail(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                               const PaintRailState& State, const PaintPaneRegion& Region,
                                               const PaintFamilyDescriptor* Families, int FamilyCount,
                                               int ActiveFamilyIndex);

// Draw the rail pane's header: the band mark tinted by the active family, the fixed title, the family count and the
// catalogue's total instrument tally.
// 🔴 The band mark is an INLINE svg in the prototype, not a pack glyph — a black rounded square, a filled circle at the
//    family's colour, and a wider ring of the same colour at 35% opacity. It is therefore drawn here with primitives
//    rather than routed through PaintPaneHeader::IconTexture, which can only carry a raster.
// 📝 The active family reaches this band ONLY as `ActiveDotColour`. Its title is the literal "Instruments" and its
//    subtitle is the family COUNT, neither of which changes as the rail moves — so the parameter is the colour rather
//    than the descriptor, which would otherwise imply the caption and tally are read from it too.
void ConstructPaintRailHeader(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                              const char* ActiveDotColour, int FamilyCount, int InstrumentTally,
                              ImVec2 PaneMinimum, float PaneWidth);

} // namespace Frontier

#endif
