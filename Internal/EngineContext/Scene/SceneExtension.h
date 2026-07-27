/*==============================================================================================================================================
                                                                SCENEEXTENSION.H
==============================================================================================================================================*/
// 🧩 The Scene pillar coordinator: owns the shared world tree (one SceneDirectory) that every editor reads and mutates, and drives its lifecycle
//    on the engine's initialize → loop → finalize cadence. AdvanceScene is the per-cycle update hook (deferred teardown sweeps, revision bump);
//    it holds no rendering or input concern — those pillars query the directory it owns. One scene per running engine (a genuinely singular thing).

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_SCENE_SCENEEXTENSION_H
#define FRONTIER_ENGINECONTEXT_SCENE_SCENEEXTENSION_H

#include <cstdint>

#include "EngineContext/Scene/SceneDirectory.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The pillar's runtime state. Owns the directory by value; a monotonically-advancing revision stamps each cycle so consumers
//    (outliner, viewport) can cheaply detect "did the tree change since I last read it?" without diffing the whole store.
struct SceneExtension
{
    SceneDirectory Directory;      // [-] - the shared world tree owned by this pillar
    uint64_t       Revision = 0u;  // [-] - increments once per AdvanceScene; a change-detection stamp for consumers
    bool           ActivationState = false;   // [-] - true between Initialize and Finalize
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bring the scene up: reserve directory backing for ReservedSlots and mark the pillar active. Call once at engine start.
void InitializeScene(SceneExtension& Scene, uint32_t ReservedSlots);

// Advance the scene by one engine cycle: bump the revision stamp. Structural edits arrive between cycles through the directory
// free functions; this hook is where deferred work (teardown sweeps) would settle. Safe to call every frame; no allocation.
void AdvanceScene(SceneExtension& Scene) noexcept;

// Tear the scene down: empty the directory (retaining backing capacity) and mark the pillar inactive. Call once at engine stop.
void FinalizeScene(SceneExtension& Scene) noexcept;

} // namespace Frontier

#endif
