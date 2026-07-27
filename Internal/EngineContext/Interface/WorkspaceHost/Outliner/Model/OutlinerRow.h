/*==============================================================================================================================================
                                                              OUTLINERROW.H
==============================================================================================================================================*/
// 🧩 One outliner row, bound to a Scene RecordToken — NEVER a positional draw slot. The generation-stamped token is what a click resolves
//    back to, so re-ordering / deleting rows can never select the wrong record entry (this is the whole point of the payload-clean Scene rule).
//    The row is a flat view record: the outliner model builds a list of these from the Scene assembly each cycle; the outliner never owns
//    scene data.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_OUTLINER_OUTLINERROW_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_OUTLINER_OUTLINERROW_H

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Stable, generation-stamped record token. Mirrors Scene's RecordToken so the outliner and Scene speak the same identity (the outliner
//    holds only the value, never a pointer into the assembly). Index 0 / Generation 0 is the null token.
struct OutlinerRecordToken
{
    unsigned Index;        // [-] - Slot into the Scene assembly's record store
    unsigned Generation;   // [-] - Reuse stamp; distinguishes a recycled slot from the original record entry
};


// 📝 One presented row. Depth drives indentation; ChildCount + Expanded drive the collapse arrow. TintColor is the record entry's display chip.
struct OutlinerRow
{
    OutlinerRecordToken Record;        // [-] - The record entry this row selects (never a slot ordinal)
    const char*         Title;         // [-] - Display name
    int                 Depth;         // [-] - Indent level in the tree
    int                 ChildCount;    // [-] - Number of direct nested entries (0 -> no arrow)
    ImU32               TintColor;     // [-] - Colour chip (0 -> no chip)
    bool                Expanded;      // [-] - Collapse state of this row's subtree
    bool                Selected;      // [-] - Row is in the current selection
    bool                Visible;       // [-] - Record visibility (drives the eye toggle)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 True when two record tokens refer to the same live record entry (index AND generation match).
bool OutlinerRecordTokensEqual(const OutlinerRecordToken& A, const OutlinerRecordToken& B);

// 📝 The null record token (Index 0, Generation 0) — "no record entry".
OutlinerRecordToken ResolveNullRecordToken();

}   // namespace Frontier

#endif
