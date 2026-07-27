/*==============================================================================================================================================
                                                          DEPTHEVALUATION.H
==============================================================================================================================================*/
// 🧩 Painter's-algorithm ordering for the six projected faces — the C++ stand-in for the CSS z-ordering that let the mockup's near faces draw over
//    the far ones through the translucent glass. Produces a back-to-front draw order (smallest DepthKey first) so the near faces layer on top.
//    Because the faces are drawn translucent (NO backface cull of the fill — the mockup keeps far faces visible THROUGH the glass), every face is
//    drawn; only the ordering matters. Header-only; the .cpp orders once per cycle before drawing.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_DEPTHEVALUATION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_DEPTHEVALUATION_H

#include "../Topology/CompassPartition.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Fill Order[0..FaceCount) with face indices sorted back-to-front (ascending DepthKey), so drawing them in order layers the
//    near faces last (on top). A tiny fixed-size selection sort — six elements, no allocation.
inline void OrderFacesBackToFront(const CompassPartition& Partition, int Order[CompassPartition::FaceCount])
{
    for (int Index = 0; Index < CompassPartition::FaceCount; ++Index)
    {
        Order[Index] = Index;
    }

    for (int Outer = 0; Outer < CompassPartition::FaceCount - 1; ++Outer)
    {
        int Lowest = Outer;
        for (int Inner = Outer + 1; Inner < CompassPartition::FaceCount; ++Inner)
        {
            if (Partition.Face[Order[Inner]].DepthKey < Partition.Face[Order[Lowest]].DepthKey)
            {
                Lowest = Inner;
            }
        }
        if (Lowest != Outer)
        {
            const int Swap = Order[Outer];
            Order[Outer]   = Order[Lowest];
            Order[Lowest]  = Swap;
        }
    }
}

}   // namespace Frontier

#endif
