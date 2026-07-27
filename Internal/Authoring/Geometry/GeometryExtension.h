/*==============================================================================================================================================
                                                              GEOMETRYEXTENSION.H
==============================================================================================================================================*/
// 🧩 The Geometry library coordinator: brings the shared geometry data kernel up and down on the engine's initialize → loop → finalize
//    cadence, matching how every other library (Scene, Graphics, Interface) exposes one small lifecycle coordinator. Geometry is the neutral
//    shared foundation every authoring library reads — vertex fields, polygon clusters, half-edge adjacency, selection / picking resolution,
//    UV layout, interchange — so this coordinator owns no editing tools and no per-object data (each object carries its own PolygonCluster).
//    It exists so the engine can activate / advance / deactivate the kernel uniformly, ahead of the libraries that depend on it
//    (PolygonAuthoring, ParametricAuthoring, TextureAuthoring). Revision is a per-cycle change stamp consumers can compare against a cached
//    value. Held kernel-wide state (shared caches, pooled scratch) can be added to the struct later WITHOUT changing any call site.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_GEOMETRYEXTENSION_H
#define FRONTIER_AUTHORING_GEOMETRY_GEOMETRYEXTENSION_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The kernel's runtime state. Deliberately minimal today: an activation flag and a monotonic revision stamp. The geometry data lives
//    per-object and the operations on it are stateless free functions, so there is no global data to own here yet. When a shared cache /
//    pooled scratch buffer is introduced, add the field(s) below the stamp — the Initialize / Advance / Finalize surface stays put.
struct GeometryExtension
{
    uint64_t Revision        = 0u;      // [-] - increments once per AdvanceGeometry; a change-detection stamp for consumers
    bool     ActivationState = false;   // [-] - true between InitializeGeometry and FinalizeGeometry
    // 📝 (future) kernel-wide shared caches / pooled scratch slot in here — a held field added later needs no call-site change.
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bring the geometry kernel up: reset the revision stamp and mark the kernel active. Call once at engine start, before any dependent
// library (PolygonAuthoring / ParametricAuthoring / TextureAuthoring). No allocation today (nothing is held).
void InitializeGeometry(GeometryExtension& Geometry) noexcept;

// Advance the geometry kernel by one engine cycle: bump the revision stamp. Data edits arrive between cycles through the kernel's free
// functions; this hook is where deferred kernel-wide work would settle. Safe to call every frame; no allocation.
void AdvanceGeometry(GeometryExtension& Geometry) noexcept;

// Tear the geometry kernel down: mark the kernel inactive. Call once at engine stop, after every dependent library has finalized. Nothing
// is held today, so there is nothing to release; the symmetry with InitializeGeometry is deliberate so held state added later has a home.
void FinalizeGeometry(GeometryExtension& Geometry) noexcept;

} // namespace Frontier

#endif
