/*============================================================================================================================================
                                                            RIGIDBODYSCENEAUTHOR.H
============================================================================================================================================*/
// 🧩 Authors the rigid-body drop scene as two saved workspace documents so nothing about the scene is hardcoded in the validation host. One
//    document carries the crate geometry block (a unit box) plus every dynamic placed object — the stacked tower and the heavy wrecker above it.
//    The second carries the ground slab block plus its single static placed object. Two documents, not one, because the .wsdoc -> GPU bridge
//    (Graphics/Scene/WorkspaceDocumentDecoder) derives geometry block 0 only and skips objects referencing any other block; the crate box and the
//    ground slab are different shapes, so each needs its own document. This mirrors the heads + floor split the visibility scenes already use.
//
//    The documents are written once at startup if absent, then LOADED like any other scene: the host reads placements back out of the decoded
//    document to construct its rigid bodies, so the file is the single authority for the initial condition.

#pragma once
#ifndef FRONTIER_VALIDATION_RIGIDBODYDROP_RIGIDBODYSCENEAUTHOR_H
#define FRONTIER_VALIDATION_RIGIDBODYDROP_RIGIDBODYSCENEAUTHOR_H

#include <cstdint>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONTRACT
//------------------------------------------------------------------------------------------------------------------------

// 📝 The authored scene's fixed proportions. The host reads these to size its collision shapes, so the numbers live in exactly one place and the
//    box half-extents it hands the solver always agree with the box the encoder wrote. World is Z-up and metres.
struct RigidBodySceneProportions
{
    float CrateEdge      = 1.0f;     // [m]  - crate box edge length (authored as a unit cube, scaled by each object's placement)
    float GroundSpan     = 100.0f;   // [m]  - ground slab side length in X and Y
    float GroundDepth    = 1.0f;     // [m]  - ground slab thickness; its TOP face lies on z = 0
    float WreckerEdge    = 3.0f;     // [m]  - wrecker box edge length (a crate scaled up, so it reuses the same geometry block)
    float WreckerHeight  = 14.0f;    // [m]  - wrecker drop height, centre of mass above the ground plane
    uint32_t TowerColumns = 4u;      // [-]  - crates per side of the square tower footprint
    uint32_t TowerLevels  = 4u;      // [-]  - stacked levels
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Write the crate document (unit-box block + the tower's dynamic objects + the wrecker) to CratePath and the ground document (slab block + one
// static object) to GroundPath. Returns false if either file could not be written. Overwrites unconditionally so an edit to the proportions
// propagates on the next launch rather than silently reusing a stale document.
bool AuthorRigidBodyDropDocuments(const RigidBodySceneProportions& Proportions, const std::string& CratePath, const std::string& GroundPath);

} // namespace Frontier

#endif
