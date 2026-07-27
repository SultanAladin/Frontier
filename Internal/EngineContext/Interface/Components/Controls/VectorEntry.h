/*==============================================================================================================================================
                                                                VECTORENTRY.H
==============================================================================================================================================*/
// 🧩 XYZ coordinate entry: three labelled drag-fields sharing one row, for a position / extent / euler triple. The three floats are
//    caller-owned (contiguous). Stateless — one definition serves every vector field (transform panels, light position, etc.).

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_VECTORENTRY_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_VECTORENTRY_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct VectorEntryDescriptor
{
    const char* Label;         // [-] - Row label
    float*      Components;     // [-] - Caller-owned array of ComponentCount floats, edited in place
    int         ComponentCount;// [-] - 2, 3 or 4 (XY / XYZ / XYZW)
    float       Step;          // [-] - Drag sensitivity per pixel (<=0 -> 0.01)
    const char* Format;        // [-] - printf format (null -> "%.3f")
    bool        Enabled;       // [-] - false draws muted + ignores input
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record one multi-component drag-field row. Returns true on the cycle any component changed. No-op-safe if Components is null.
bool ConstructVectorEntry(const ThemeConfiguration& Theme, const VectorEntryDescriptor& Descriptor);

}   // namespace Frontier

#endif
