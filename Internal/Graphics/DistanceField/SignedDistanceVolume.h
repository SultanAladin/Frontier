/*==============================================================================================================================================
                                                            SIGNEDDISTANCEVOLUME.H
==============================================================================================================================================*/
// 🧩 The baked dense signed-distance volume data the GPU bake dispatch fills: Resolution.X*Y*Z int16 voxels (R16_SNORM encoding of
//    signedDistance / MaximumDistance) inside a padded world-space AABB. This is the GPU-path CONTRACT ONLY — the sparse narrow-band
//    container, the palette material volume, and the byte-exact .rsdf / .rsdfvdb cache serialization live in the CPU baker (unported this
//    slice). The GPU dispatch derives the padded AABB + per-axis extent, then reads its R16_SNORM voxels straight back into this struct.

#pragma once
#ifndef FRONTIER_GRAPHICS_DISTANCEFIELD_SIGNEDDISTANCEVOLUME_H
#define FRONTIER_GRAPHICS_DISTANCEFIELD_SIGNEDDISTANCEVOLUME_H

#include "LinearAlgebra_Float64.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The R16_SNORM decode divisor (32767 signed levels either side of the band): decode a voxel to world units with voxel / 32767 * MaximumDistance.
constexpr float SignedDistanceScale = 32767.0f;   // [-] - int16 SNORM range, decode voxel / 32767 * MaximumDistance

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Per-axis voxel extent of a distance grid. Replaces a single cube edge so a thin / oblong object bakes a grid tight to its AABB
//    (fixed cm-per-voxel density along every axis) instead of a wasteful cube around its longest side. A cube volume is simply
//    X == Y == Z. VoxelTotal is the linear voxel count; VoxelIndex is the row-major (Z outer, X inner) linear offset.
struct DistanceExtent
{
    uint32_t X = 0;   // [-] - voxels along local X
    uint32_t Y = 0;   // [-] - voxels along local Y
    uint32_t Z = 0;   // [-] - voxels along local Z

    bool operator==(const DistanceExtent& Other) const { return X == Other.X && Y == Other.Y && Z == Other.Z; }
    bool operator!=(const DistanceExtent& Other) const { return !(*this == Other); }
};

// The linear voxel count of a per-axis extent (X * Y * Z), in size_t so a large grid never overflows a uint32.
inline size_t VoxelTotal(const DistanceExtent& Extent)
{
    return static_cast<size_t>(Extent.X) * static_cast<size_t>(Extent.Y) * static_cast<size_t>(Extent.Z);
}

// The row-major linear offset of voxel (X,Y,Z) in a grid of the given extent (Z outer, Y middle, X inner — matches the CPU baker,
// the GPU dispatch, and the .rsdf payload order). The caller guarantees the coordinate lies inside the extent.
inline size_t VoxelIndex(const DistanceExtent& Extent, uint32_t X, uint32_t Y, uint32_t Z)
{
    return (static_cast<size_t>(Z) * Extent.Y + Y) * Extent.X + X;
}

// True when every axis holds at least one voxel (a usable grid). A zero on any axis means "unbaked / empty".
inline bool ExtentPopulated(const DistanceExtent& Extent)
{
    return Extent.X != 0 && Extent.Y != 0 && Extent.Z != 0;
}

// 📝 A fully-baked dense signed-distance volume. Voxels is Resolution.X*Y*Z entries of R16_SNORM; decode a voxel to world units
//    with voxel / 32767 * MaximumDistance. BoundaryMinimum / BoundaryMaximum are the padded world-space AABB the grid spans.
struct SignedDistanceVolume
{
    Vector3d             BoundaryMinimum = {};     // [cm] - padded world AABB lower corner
    Vector3d             BoundaryMaximum = {};     // [cm] - padded world AABB upper corner
    DistanceExtent       Resolution      = {};     // [-]  - per-axis voxel extent (grid is Resolution.X*Y*Z; cube == X==Y==Z)
    float                MaximumDistance = 0.0f;   // [cm] - R16_SNORM decode scale (distance at |voxel| == 32767)
    std::vector<int16_t> Voxels;                   // [-]  - VoxelTotal(Resolution) R16_SNORM signed-distance samples
};

} // namespace Frontier

#endif
