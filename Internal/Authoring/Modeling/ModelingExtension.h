/*==============================================================================================================================================
                                                              MODELINGEXTENSION.H
==============================================================================================================================================*/
// 🧩 The Modeling pillar coordinator: brings the Authoring/Modeling area up and down on the engine's initialize → loop → finalize cadence,
//    matching how every other pillar (Scene, Graphics, Instrumentation) exposes one small lifecycle coordinator. The modeling operations
//    themselves are stateless free functions (extrude, inset, subdivide, boolean, loft, …) and each edited object carries its OWN data
//    (its PolygonCluster, its ModifierStack, its DraughtShapeStore), so this coordinator owns no per-object geometry — it exists so the
//    engine can activate / advance / deactivate the pillar uniformly. Revision is a per-cycle change stamp consumers can compare against a
//    cached value. Held pillar-wide state (metadata, shared settings) can be added to the struct later WITHOUT changing any call site.

#pragma once
#ifndef FRONTIER_AUTHORING_MODELING_MODELINGEXTENSION_H
#define FRONTIER_AUTHORING_MODELING_MODELINGEXTENSION_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The pillar's runtime state. Deliberately minimal today: an activation flag and a monotonic revision stamp, mirroring SceneExtension.
//    The modeling tools are stateless and object data is per-object, so there is no global geometry to own here yet. When pillar-wide
//    metadata / shared settings are introduced, add the field(s) below the stamp — the Initialize / Advance / Finalize surface stays put.
struct ModelingExtension
{
    uint64_t Revision        = 0u;      // [-] - increments once per AdvanceModeling; a change-detection stamp for consumers
    bool     ActivationState = false;   // [-] - true between InitializeModeling and FinalizeModeling
    // 📝 (future) pillar-wide metadata / shared modeling settings slot in here — a held field added later needs no call-site change.
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bring the modeling pillar up: reset the revision stamp and mark the pillar active. Call once at engine start. No allocation today
// (nothing is held); it exists so the pillar activates uniformly with Scene / Graphics / Instrumentation.
void InitializeModeling(ModelingExtension& Modeling) noexcept;

// Advance the modeling pillar by one engine cycle: bump the revision stamp. Object edits arrive between cycles through the modeling
// operation free functions; this hook is where deferred pillar-wide work would settle. Safe to call every frame; no allocation.
void AdvanceModeling(ModelingExtension& Modeling) noexcept;

// Tear the modeling pillar down: mark the pillar inactive. Call once at engine stop. Nothing is held today, so there is nothing to
// release; the symmetry with InitializeModeling is deliberate so held state added later has an obvious finalize home.
void FinalizeModeling(ModelingExtension& Modeling) noexcept;

} // namespace Frontier

#endif
