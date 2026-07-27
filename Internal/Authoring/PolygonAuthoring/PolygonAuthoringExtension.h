/*==============================================================================================================================================
                                                          POLYGONAUTHORINGEXTENSION.H
==============================================================================================================================================*/
// 🧩 The PolygonAuthoring library coordinator: brings the polygon-editing toolset up and down on the engine's initialize → loop → finalize
//    cadence, matching how every other library (Geometry, Scene, Graphics) exposes one small lifecycle coordinator. The editing operations
//    themselves are stateless free functions (extrude, inset, bevel, deform, loop-cut, subdivide, topology-edit) that read + rewrite the
//    shared Geometry data (a PolygonCluster + its AdjacencyIndex), and each edited object carries its OWN data, so this coordinator owns no
//    per-object geometry — it exists so the engine can activate / advance / deactivate the library uniformly. Revision is a per-cycle change
//    stamp consumers can compare against a cached value. Held library-wide state (tool defaults, shared settings) can be added to the struct
//    later WITHOUT changing any call site.

#pragma once
#ifndef FRONTIER_AUTHORING_POLYGONAUTHORING_POLYGONAUTHORINGEXTENSION_H
#define FRONTIER_AUTHORING_POLYGONAUTHORING_POLYGONAUTHORINGEXTENSION_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The library's runtime state. Deliberately minimal today: an activation flag and a monotonic revision stamp. The editing tools are
//    stateless and object data is per-object, so there is no global geometry to own here yet. When library-wide tool defaults / shared
//    settings are introduced, add the field(s) below the stamp — the Initialize / Advance / Finalize surface stays put.
struct PolygonAuthoringExtension
{
    uint64_t Revision        = 0u;      // [-] - increments once per AdvancePolygonAuthoring; a change-detection stamp for consumers
    bool     ActivationState = false;   // [-] - true between InitializePolygonAuthoring and FinalizePolygonAuthoring
    // 📝 (future) library-wide tool defaults / shared settings slot in here — a held field added later needs no call-site change.
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bring the polygon-authoring library up: reset the revision stamp and mark the library active. Call once at engine start. No allocation
// today (nothing is held); it exists so the library activates uniformly with Geometry / Scene / Graphics.
void InitializePolygonAuthoring(PolygonAuthoringExtension& PolygonAuthoring) noexcept;

// Advance the polygon-authoring library by one engine cycle: bump the revision stamp. Object edits arrive between cycles through the editing
// operation free functions; this hook is where deferred library-wide work would settle. Safe to call every frame; no allocation.
void AdvancePolygonAuthoring(PolygonAuthoringExtension& PolygonAuthoring) noexcept;

// Tear the polygon-authoring library down: mark the library inactive. Call once at engine stop. Nothing is held today, so there is nothing
// to release; the symmetry with InitializePolygonAuthoring is deliberate so held state added later has an obvious finalize home.
void FinalizePolygonAuthoring(PolygonAuthoringExtension& PolygonAuthoring) noexcept;

} // namespace Frontier

#endif
