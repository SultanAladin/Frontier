/*==============================================================================================================================================
                                                               SPATIALEXTENT.H
==============================================================================================================================================*/
// 🧩 An axis-aligned metric region of world space, sampled on a regular voxel lattice: where it starts (RegionOrigin, metres), how far it reaches
//    on each axis (RegionSpan, metres), and how many voxels divide it per axis (ResolutionVoxels). This is the domain a distance/colour field is
//    baked over — the origin + span fix it in the world reference frame (CoordinateSpace.h), the resolution fixes the sampling. Struct-only, POD,
//    metres throughout (DistanceMetric.h), no glm — the unsigned triple is defined here so the header stands on our own types alone.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_METRICSPACE_SPATIALEXTENT_H
#define FRONTIER_ENGINECONTEXT_METRICSPACE_SPATIALEXTENT_H

#include "../Math/LinearAlgebra_Float32.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// A per-axis unsigned voxel count. Kept local (LinearAlgebra carries only float vectors) so a lattice size never borrows a
// signed float vector.
struct VoxelResolution
{
    uint32_t X = 0;   // [voxels] - Along Right (+X)
    uint32_t Y = 0;   // [voxels] - Along Forward (+Y)
    uint32_t Z = 0;   // [voxels] - Along Up (+Z)
};

// An axis-aligned metric region on a regular voxel lattice.
struct SpatialExtent
{
    Vector3f        RegionOrigin;       // [m]      - World-space corner the region starts at
    Vector3f        RegionSpan;         // [m]      - Extent along each axis from the origin
    VoxelResolution ResolutionVoxels;   // [voxels] - Lattice divisions per axis
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    REGION HELPERS
//------------------------------------------------------------------------------------------------------------------------

// The size of one voxel along each axis, in metres (span / resolution). A zero resolution on an axis yields 0 there.
[[nodiscard]] inline Vector3f ResolveVoxelSize(const SpatialExtent& Extent) noexcept
{
    return Vector3f{
        Extent.ResolutionVoxels.X ? Extent.RegionSpan.XCoord / static_cast<float>(Extent.ResolutionVoxels.X) : 0.0f,
        Extent.ResolutionVoxels.Y ? Extent.RegionSpan.YCoord / static_cast<float>(Extent.ResolutionVoxels.Y) : 0.0f,
        Extent.ResolutionVoxels.Z ? Extent.RegionSpan.ZCoord / static_cast<float>(Extent.ResolutionVoxels.Z) : 0.0f };
}

// The far corner of the region (origin + span), in metres.
[[nodiscard]] inline Vector3f ResolveRegionFarCorner(const SpatialExtent& Extent) noexcept
{
    return AddVector(Extent.RegionOrigin, Extent.RegionSpan);
}

} // namespace Frontier

#endif
