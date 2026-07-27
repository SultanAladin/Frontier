/*==============================================================================================================================================
                                                                PATHENTRY.H
==============================================================================================================================================*/
// 🧩 File/folder entry: a labelled text field plus a browse button. The path buffer is caller-owned (fixed capacity). Stateless — one
//    definition serves every path field (import source, export target, texture path). The actual file dialogue is opened by the caller when
//    BrowseRequested comes back true, so this control stays platform-free.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_PATHENTRY_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_PATHENTRY_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct PathEntryDescriptor
{
    const char* Label;          // [-] - Row label
    char*       Buffer;         // [-] - Caller-owned mutable path buffer, edited in place
    int         BufferCapacity; // [-] - Size of Buffer including the null terminator
    bool        Enabled;        // [-] - false draws muted + ignores input
};


// 📝 What a PathEntry reports back this cycle. TextChanged fires on typed edits; BrowseRequested fires the cycle the button is pressed.
struct PathEntryResult
{
    bool TextChanged;       // [-] - The buffer was edited by typing
    bool BrowseRequested;   // [-] - The browse button was pressed (caller opens its own file dialogue)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record one path row (field + browse button). Returns both flags. No-op-safe (all false) if Buffer is null.
PathEntryResult ConstructPathEntry(const ThemeConfiguration& Theme, const PathEntryDescriptor& Descriptor);

}   // namespace Frontier

#endif
