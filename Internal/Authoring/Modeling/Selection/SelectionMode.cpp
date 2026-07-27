/*============================================================================================================================================
                                                            SELECTIONMODE.CPP
============================================================================================================================================*/
// 🧩 The mode -> name lookup. Trivial today; it lives in its own unit so the enum and its presentation stay one component, and
//    so future per-mode metadata (pick radius defaults, overlay colour) has an obvious home next to it.

#include "SelectionMode.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const char* ResolveSelectionModeName(SelectionMode Mode)
{
    switch (Mode)
    {
        case ObjectMode: return "Object";
        case VertexMode: return "Vertex";
        case EdgeMode:   return "Edge";
        case FaceMode:   return "Face";
        case IslandMode: return "Island";
    }
    return "Object";
}

} // namespace Frontier
