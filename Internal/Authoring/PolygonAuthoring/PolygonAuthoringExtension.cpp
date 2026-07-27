/*==============================================================================================================================================
                                                         POLYGONAUTHORINGEXTENSION.CPP
==============================================================================================================================================*/
// 🧩 PolygonAuthoring library lifecycle: initialize resets the revision stamp and activates the library; advance bumps the per-cycle
//    revision stamp; finalize deactivates. The editing operations are stateless free functions and object data is per-object, so this file
//    only sequences the library's own activation around them — it holds no geometry and performs no allocation.

#include "Authoring/PolygonAuthoring/PolygonAuthoringExtension.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializePolygonAuthoring(PolygonAuthoringExtension& PolygonAuthoring) noexcept
{
    PolygonAuthoring.Revision        = 0u;
    PolygonAuthoring.ActivationState = true;
}

void AdvancePolygonAuthoring(PolygonAuthoringExtension& PolygonAuthoring) noexcept
{
    if (!PolygonAuthoring.ActivationState)
    {
        return;
    }

    // 📝 A single monotonic bump per cycle. Consumers compare this against a cached value to skip redundant rebuilds; wrapping at
    //    2^64 is not a concern within any real session lifetime.
    ++PolygonAuthoring.Revision;
}

void FinalizePolygonAuthoring(PolygonAuthoringExtension& PolygonAuthoring) noexcept
{
    PolygonAuthoring.ActivationState = false;
}

} // namespace Frontier
