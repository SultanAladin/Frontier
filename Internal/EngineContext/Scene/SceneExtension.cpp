/*==============================================================================================================================================
                                                                SCENEEXTENSION.CPP
==============================================================================================================================================*/
// 🧩 Scene pillar lifecycle: initialize reserves the directory and activates the pillar; advance bumps the per-cycle revision stamp; finalize
//    empties the directory and deactivates. All structural mutation flows through the SceneDirectory free functions — this file only sequences
//    the pillar's own lifecycle around them, holding no rendering or input concern.

#include "EngineContext/Scene/SceneExtension.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeScene(SceneExtension& Scene, uint32_t ReservedSlots)
{
    InitializeDirectory(Scene.Directory, ReservedSlots);
    Scene.Revision        = 0u;
    Scene.ActivationState = true;
}

void AdvanceScene(SceneExtension& Scene) noexcept
{
    if (!Scene.ActivationState)
    {
        return;
    }

    // 📝 A single monotonic bump per cycle. Consumers compare this against a cached value to skip redundant rebuilds; wrapping
    //    at 2^64 is not a concern within any real session lifetime.
    ++Scene.Revision;
}

void FinalizeScene(SceneExtension& Scene) noexcept
{
    FinalizeDirectory(Scene.Directory);
    Scene.ActivationState = false;
}

} // namespace Frontier
