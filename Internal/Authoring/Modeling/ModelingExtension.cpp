/*==============================================================================================================================================
                                                             MODELINGEXTENSION.CPP
==============================================================================================================================================*/
// 🧩 Modeling pillar lifecycle: initialize resets the revision stamp and activates the pillar; advance bumps the per-cycle revision stamp;
//    finalize deactivates. The modeling operations are stateless free functions and object data is per-object, so this file only sequences
//    the pillar's own activation around them — it holds no geometry and performs no allocation.

#include "Authoring/Modeling/ModelingExtension.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeModeling(ModelingExtension& Modeling) noexcept
{
    Modeling.Revision        = 0u;
    Modeling.ActivationState = true;
}

void AdvanceModeling(ModelingExtension& Modeling) noexcept
{
    if (!Modeling.ActivationState)
    {
        return;
    }

    // 📝 A single monotonic bump per cycle. Consumers compare this against a cached value to skip redundant rebuilds; wrapping at
    //    2^64 is not a concern within any real session lifetime.
    ++Modeling.Revision;
}

void FinalizeModeling(ModelingExtension& Modeling) noexcept
{
    Modeling.ActivationState = false;
}

} // namespace Frontier
