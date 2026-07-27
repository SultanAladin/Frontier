/*============================================================================================================================================
                                                            POLYGONDESCRIPTOR.CPP
============================================================================================================================================*/
// 🧩 Derives a polygon's axis-aligned bounding extent from its authoring positions. The bounds sweep is a single aggregate pass
//    over the whole position array, never a per-element entry point (anti-fragility §3.1).

#include "PolygonDescriptor.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

PolygonBounds EvaluatePolygonBounds(const VertexField& Field)
{
    PolygonBounds Bounds = {};
    if (Field.Position.empty()) return Bounds;

    Vector3d MinimumCorner = Field.Position[0];
    Vector3d MaximumCorner = Field.Position[0];
    for (const Vector3d& Position : Field.Position)
    {
        if (Position.XCoord < MinimumCorner.XCoord) MinimumCorner.XCoord = Position.XCoord;
        if (Position.YCoord < MinimumCorner.YCoord) MinimumCorner.YCoord = Position.YCoord;
        if (Position.ZCoord < MinimumCorner.ZCoord) MinimumCorner.ZCoord = Position.ZCoord;
        if (Position.XCoord > MaximumCorner.XCoord) MaximumCorner.XCoord = Position.XCoord;
        if (Position.YCoord > MaximumCorner.YCoord) MaximumCorner.YCoord = Position.YCoord;
        if (Position.ZCoord > MaximumCorner.ZCoord) MaximumCorner.ZCoord = Position.ZCoord;
    }

    Bounds.MinimumCorner = MinimumCorner;
    Bounds.MaximumCorner = MaximumCorner;
    return Bounds;
}

} // namespace Frontier
