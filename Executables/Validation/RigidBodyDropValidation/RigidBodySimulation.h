/*============================================================================================================================================
                                                            RIGIDBODYSIMULATION.H
============================================================================================================================================*/
// 🧩 The Jolt seam: turns the decoded workspace documents into a live rigid-body world, advances it, and writes each body's resting transform
//    back into the GPU instance records the visibility raster uploads. Owns the whole Jolt lifetime (allocator, factory, type registry, job pool,
//    temp allocator, world) so the host never touches a JPH type.
//
//    🔴 Writeback bypasses LocalPlacement. A placement carries EULER degrees, and a tumbling crate round-tripped through Euler gimbals — the box
//       visibly snaps as the solved quaternion crosses a singularity. So the authored placement is read ONCE as the initial condition, and from
//       then on each body's quaternion + translation compose Model / NormalBasis / InverseModel directly. The document is the initial condition;
//       it is never the running representation.
//
//    Units: world is Z-up and metres. Gravity therefore points down -Z, NOT -Y.

#pragma once
#ifndef FRONTIER_VALIDATION_RIGIDBODYDROP_RIGIDBODYSIMULATION_H
#define FRONTIER_VALIDATION_RIGIDBODYDROP_RIGIDBODYSIMULATION_H

#include "Authoring/Geometry/Interchange/WorkspaceDocumentEncoder.h"
#include "Graphics/Scene/SuzanneScene.h"
#include "RigidBodySceneAuthor.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONTRACT
//------------------------------------------------------------------------------------------------------------------------

// 📝 The knobs the control window drives. Read at the top of every advance, so a change lands on the next step with no reconstruction.
struct RigidBodyTuning
{
    float    GravityStrength = 9.81f;   // [m/s²] - downward acceleration along -Z (0 floats the scene, negative lifts it)
    float    StepSeconds     = 1.0f / 60.0f;   // [s]  - fixed solver interval; the advance clamps its own substep count to this
    int32_t  Substeps        = 1;       // [-]    - collision substeps per step; more is stiffer contact at linear cost
    float    Restitution     = 0.15f;   // [-]    - bounce, applied to every dynamic body when reseeded
    float    Friction        = 0.55f;   // [-]    - contact friction, applied to every dynamic body when reseeded
    bool     Advancing       = true;    // [-]    - false holds the world still; the host still uploads so the view stays live
};

// 📝 What the control window reports back. Recomputed each advance from the live bodies — no cached counters to drift out of step.
struct RigidBodyReadout
{
    uint32_t BodyCount        = 0u;    // [-]     - dynamic bodies in the world (the ground is static and not counted)
    uint32_t AwakeCount       = 0u;    // [-]     - bodies still active; reaching 0 is the scene having settled
    float    KineticEnergy    = 0.0f;  // [J]     - summed ½mv² + rotational, the settle curve the window plots
    float    HighestCrate     = 0.0f;  // [m]     - tallest dynamic centre of mass; falls as the tower collapses
    float    ElapsedSeconds   = 0.0f;  // [s]     - simulated time since the last reseed
    uint32_t AdvanceOrdinal   = 0u;    // [-]     - steps taken since the last reseed
};

// 📝 One simulated body paired with the instance record it drives. SceneSlot indexes the host's instance vector; the ordinal is stable for the
//    world's lifetime because bodies are constructed in document order and never removed.
struct RigidBodyLink
{
    uint32_t SceneSlot     = 0u;   // [idx]  - slot in the host's SuzanneSceneInstance vector this body writes
    uint32_t BodyOrdinal   = 0u;   // [idx]  - slot in the internal body-id table
    float    HalfExtent[3] = { 0.5f, 0.5f, 0.5f };   // [m]  - collision box half-extents, from the authored scale
    // BodyInterface exposes no mass accessor, so the density-derived mass is kept here at construction — the energy readout needs it every frame
    // and re-deriving it from the shape's MassProperties per frame would allocate a Ref for nothing.
    float    Mass          = 1.0f;   // [kg] - rigid mass, from box volume x density
    float    SeedLocation[3] = { 0.0f, 0.0f, 0.0f };   // [m] - authored spawn translation, kept so Reseed restores it without re-decoding
    float    SeedYaw         = 0.0f;                  // [°] - authored spawn turn about +Z (the only authored rotation in this scene)
};

// 📝 The whole simulation. Opaque by design: the Jolt objects live behind a pointer so no consumer of this header pulls in Jolt.
struct RigidBodySimulation
{
    void*                      Internals    = nullptr;   // [-] - owned Jolt world + interfaces; opaque so the host stays Jolt-free
    std::vector<RigidBodyLink> Links        = {};        // [-] - body <-> instance pairing, document order
    RigidBodyReadout           Readout      = {};        // [-] - refreshed by every advance
    float                      SettleResidue = 0.0f;     // [s] - leftover real time not yet consumed by a fixed step
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Stand up the Jolt world and construct one body per placed object. CrateDocument's objects become dynamic boxes (the last object is the wrecker,
// sized by its own authored scale); GroundDocument's single object becomes the static ground plane. CrateSlotBase / GroundSlotBase are where each
// document's instances start in the host's merged instance vector, so writeback addresses the right records. Returns false if Jolt could not be
// initialized. Safe to call once per process; call FinalizeRigidBodySimulation before exit.
bool InitializeRigidBodySimulation(RigidBodySimulation& Simulation, const RigidBodySceneProportions& Proportions,
                                  const WorkspaceDocument& CrateDocument, uint32_t CrateSlotBase,
                                  const WorkspaceDocument& GroundDocument, uint32_t GroundSlotBase,
                                  const RigidBodyTuning& Tuning);

// Return every dynamic body to its authored placement, zero its velocities, reapply the tuning's friction / restitution, and clear the readout.
// This is the control window's Reset. The spawn transform it restores was captured from the decoded document at Initialize (RigidBodyLink's
// SeedLocation / SeedYaw), so the authored file remains the only source of the initial condition — Reset cannot drift away from the file.
void ReseedRigidBodySimulation(RigidBodySimulation& Simulation, const RigidBodyTuning& Tuning);

// Advance the world by RealSeconds of wall time, consuming it in fixed Tuning.StepSeconds intervals (leftover carried in SettleResidue so the
// simulation is frame-rate independent). A paused tuning advances nothing but still refreshes the readout. Applies gravity and substep changes
// before stepping. Caps the steps consumed in one call so a hitch cannot spiral into an unbounded catch-up.
void AdvanceRigidBodySimulation(RigidBodySimulation& Simulation, const RigidBodyTuning& Tuning, float RealSeconds);

// Compose each linked body's solved transform into Instances: Model from quaternion + translation + authored half-extent scale, NormalBasis from
// the rotation alone, InverseModel analytically. Leaves every other instance field (tint, material, partition, ordinal) untouched, so the record
// the decoder produced keeps its identity. Call before UploadVisibilityScene each frame.
void TransferRigidBodyTransforms(const RigidBodySimulation& Simulation, std::vector<SuzanneSceneInstance>& Instances);

// Release the Jolt world and every registered type. Idempotent.
void FinalizeRigidBodySimulation(RigidBodySimulation& Simulation);

} // namespace Frontier

#endif
