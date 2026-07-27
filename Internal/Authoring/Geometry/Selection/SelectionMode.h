/*============================================================================================================================================
                                                             SELECTIONMODE.H
============================================================================================================================================*/
// ðŸ§© Which category of polygon component the active selection addresses â€” vertex, edge, or face. This is the one global modelling
//    mode that decides what a click picks and what the overlay highlights; the per-component SelectEnabled bits record the actual
//    selected set, this only names which pool those bits are read from. Object-level selection is a separate axis (the scene
//    record's object-select bit, driven from the outliner), not a value of this enum.
// ðŸ“ An unscoped enum so the three mode names read bare at call sites (FaceMode), matching the project's plain-data style; the
//    pool dispatch that consumes it lives in ComponentSelection.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_SELECTION_SELECTIONMODE_H
#define FRONTIER_AUTHORING_GEOMETRY_SELECTION_SELECTIONMODE_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ What a selection operates over. ObjectMode is the whole-object axis (no component pool â€” it drives the object-select bit and
//    the object silhouette); Vertex/Edge/Face address the matching component pool. The order is the toggle order shown in the
//    UI (Object first, then 1/2/3 vertex/edge/face in DCC convention).
enum SelectionMode
{
    ObjectMode = 0,   // [-] - selection addresses whole objects (no component pool)
    VertexMode = 1,   // [-] - selection addresses vertex components
    EdgeMode   = 2,   // [-] - selection addresses edge components
    FaceMode   = 3,   // [-] - selection addresses face components
    IslandMode = 4,   // [-] - a click grows the whole UV island (chart) around the hit face; the set lives in the Face pool
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Human-readable name of a mode ("Vertex" / "Edge" / "Face"), for the test window and logging. Never null.
const char* ResolveSelectionModeName(SelectionMode Mode);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_GEOMETRY_SELECTION_SELECTIONMODE_H
