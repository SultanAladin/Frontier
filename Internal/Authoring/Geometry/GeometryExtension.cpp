/*==============================================================================================================================================
                                                             GEOMETRYEXTENSION.CPP
==============================================================================================================================================*/
// 🧩 Geometry kernel lifecycle: initialize resets the revision stamp and activates the kernel; advance bumps the per-cycle revision stamp;
//    finalize deactivates. The geometry data is per-object and the operations on it are stateless free functions, so this file only sequences
//    the kernel's own activation around them — it holds no geometry and performs no allocation.

#include "Authoring/Geometry/GeometryExtension.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeGeometry(GeometryExtension& Geometry) noexcept
{
    Geometry.Revision        = 0u;
    Geometry.ActivationState = true;
}

void AdvanceGeometry(GeometryExtension& Geometry) noexcept
{
    if (!Geometry.ActivationState)
    {
        return;
    }

    // 📝 A single monotonic bump per cycle. Consumers compare this against a cached value to skip redundant rebuilds; wrapping at
    //    2^64 is not a concern within any real session lifetime.
    ++Geometry.Revision;
}

void FinalizeGeometry(GeometryExtension& Geometry) noexcept
{
    Geometry.ActivationState = false;
}

} // namespace Frontier
