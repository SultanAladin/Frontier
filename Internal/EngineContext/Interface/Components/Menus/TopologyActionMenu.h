/*==============================================================================================================================================
                                                          TOPOLOGYACTIONMENU.H
==============================================================================================================================================*/
// 🧩 The floating context menu that offers the operations valid for the current selection. Follows Documentation/Prototypes/TopologyActionMenu.html
//    exactly: a rounded card whose header pairs the stratum's badge tile with its name, an "N of M operations" subtitle and a "N selected" pill, over
//    uppercase band captions and rows of [ glyph tile | label | chip ]. A row whose gate fails greys out and shows the SHORTFALL as a chip in the
//    keystroke slot — "needs 2 loops", "requires a quad ring" — so the caller learns the missing precondition instead of a bare "unavailable"; a row
//    whose stratum does not apply is absent entirely. Severity is carried by the glyph TILE (accent fill on hover for emphasised, a danger wash
//    across the whole row for destructive), which is what separates a menu of 94 operations into something scannable.
//    Stateless chrome: it renders a resolved entry array it does not own, and reports which row was activated. The caller owns the selection, the
//    catalogue, and the open/closed decision — so this same component serves the viewport menu, a docked palette, or a command list.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOPOLOGYACTIONMENU_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOPOLOGYACTIONMENU_H

#include "imgui.h"

#include "../../Theme/ThemeConfiguration.h"
#include "PolygonMutation/OperationCatalogue.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which corner of the anchor point the card grows from. Resolved by the caller (or by ResolveMenuPlacement) rather than left
//    to ImGui: ImGui clamps a window to the viewport, which is not the same as flipping it — a card anchored near the bottom of a
//    panel must open UPWARD off the click, not slide up until it covers the click.
enum class MenuGrowth
{
    DownRight = 0,      // [-] - Card below and right of the anchor (the default)
    DownLeft,           // [-] - Below, right edge on the anchor
    UpRight,            // [-] - Above, left edge on the anchor
    UpLeft              // [-] - Above, right edge on the anchor
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One open branch. A row whose descriptor carries a BranchCaption opens a child card of these; the caller supplies the payload
//    because branch contents are domain data (axis names, material names) rather than topology, and the menu should not know them.
struct MenuBranchDescriptor
{
    const char*        Caption;         // [-] - Branch title, matched against a row's BranchCaption
    const char* const* Options;         // [-] - Option labels (borrowed)
    int                OptionCount;     // [-] - Number of options
    int                MarkedIndex;     // [idx] - Option currently in effect; -1 when none
};


// 📝 Everything one menu draws. Entries is the FILTERED array — the menu never gates anything itself, it renders what
//    FilterOperationCatalogue resolved, which is what keeps the availability rule in exactly one place.
struct TopologyActionMenuDescriptor
{
    const char*                   Identifier;        // [-]  - Unique id (scopes the ImGui window)
    TopologyStratum               ActiveStratum;     // [-]  - Drives the header badge artwork and title
    int                           SelectedCount;     // [idx]- Shown in the header's right-hand pill
    int                           CatalogueCount;    // [idx]- Total catalogue size, for the "N of M operations" subtitle; 0 hides it
    const ResolvedOperationEntry* Entries;           // [-]  - Resolved rows (borrowed, not owned)
    int                           EntryCount;        // [-]  - Number of rows
    const MenuBranchDescriptor*   Branches;          // [-]  - Branch payloads (borrowed); null disables branching
    int                           BranchCount;       // [-]  - Number of branch payloads
    ImVec2                        AnchorPosition;    // [px] - Screen point the card grows from
    MenuGrowth                    Growth;            // [-]  - Which way it grows
    float                         CardWidth;         // [px] - Card width; 0 takes the component default
    float                         HeightLimit;       // [px] - Body scrolls past this total height; 0 leaves the card unbounded
};


// 📝 What the menu reports back. ActivatedIndex indexes the ENTRY array (not the catalogue), so a caller maps it back through
//    Entries[i].Descriptor. BranchOptionIndex is set only when the activation came from inside a branch.
struct TopologyActionMenuResult
{
    int  ActivatedIndex;        // [idx] - Entry activated this cycle; -1 when none
    int  BranchOptionIndex;     // [idx] - Option chosen within a branch; -1 when the row itself was clicked
    int  HoveredIndex;          // [idx] - Entry under the pointer; -1 when none
    bool DismissRequested;      // [-]   - Pointer went down outside the card, or Escape was pressed
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record one context menu as a floating card. Draws nothing and reports no activation when EntryCount is 0 — an empty menu is
//    a caller bug (nothing is selected), not something to render as an empty box. Greyed rows are drawn but never activate.
TopologyActionMenuResult ConstructTopologyActionMenu(const ThemeConfiguration& Theme, const TopologyActionMenuDescriptor& Descriptor);

// 📝 The card's height in pixels for a given entry array, so a caller can decide which way to grow before recording it. Counts
//    band captions and separators, because those are what make a 40-row menu taller than 40 × RowHeight.
[[nodiscard]] float ResolveTopologyActionMenuHeight(const ThemeConfiguration&     Theme,
                                                   const ResolvedOperationEntry* Entries,
                                                   int                           EntryCount);

// 📝 Pick the growth direction that keeps a card of the given height inside the confinement band. Prefers DownRight and flips
//    only on the axis that would overflow — so a menu near a corner flips both, and one mid-panel flips neither.
[[nodiscard]] MenuGrowth ResolveMenuPlacement(ImVec2 AnchorPosition, ImVec2 CardSize, ImVec2 BandTopLeft, ImVec2 BandBottomRight);

}   // namespace Frontier

#endif
