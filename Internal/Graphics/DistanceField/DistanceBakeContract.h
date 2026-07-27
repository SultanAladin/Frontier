/*==============================================================================================================================================
                                                            DISTANCEBAKECONTRACT.H
==============================================================================================================================================*/
// 🧩 The signed-distance bake CONTRACT the GPU dispatch shares with its callers: which algorithm fills the grid (DistanceBakeAlgorithm).
//    Both algorithms produce the same R16_SNORM SignedDistanceVolume; they differ only in how the per-voxel distance is resolved. The
//    CPU baker (the median-split tree + Eberly closest-point evaluation + the per-axis extent derivation) is unported this slice — only
//    the algorithm selector lands here, so EvaluateDistanceVolumeGpu and the CPU path select the fill strategy from one enum.

#pragma once
#ifndef FRONTIER_GRAPHICS_DISTANCEFIELD_DISTANCEBAKECONTRACT_H
#define FRONTIER_GRAPHICS_DISTANCEFIELD_DISTANCEBAKECONTRACT_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which bake algorithm fills the grid. Both produce the same R16_SNORM SignedDistanceVolume; they differ only in how the per-voxel
//    distance is resolved. ExactBoundingVolume queries the median-split triangle tree at every voxel (exact everywhere, O(voxels · log
//    tris)); FastSweeping seeds a narrow band around the triangles then propagates distance with directional sweeps / jump-flood (much
//    faster on dense grids, the band interior is swept-approximate rather than exact). CoarseJumpNarrowExact runs a whole-grid jump-flood
//    then refines EXACTLY only inside the narrow band (fast + band-exact — balanced).
enum class DistanceBakeAlgorithm
{
    ExactBoundingVolume   = 0,   // [-] - Eberly point-to-triangle over the AABB tree, per voxel (exact, whole grid — heaviest)
    FastSweeping          = 1,   // [-] - narrow-band seed + directional sweeps / jump-flood (fast, band-approximate)
    CoarseJumpNarrowExact = 2,   // [-] - whole-grid jump-flood, then EXACT refine only inside the narrow band (fast + band-exact — balanced)
};

} // namespace Frontier

#endif
