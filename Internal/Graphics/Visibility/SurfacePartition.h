/*==============================================================================================================================================
                                                             SURFACEPARTITION.H
==============================================================================================================================================*/
// 🧩 The geometry unit the GPU-driven visibility renderer culls and rasterizes: a MicroSurfacePartition — a contiguous run of ~64–128 triangles
//    drawn from one PolygonSurface, carrying the three bounds the cull compute needs (a bounding sphere, a normal cone, and a screen-space error
//    bound). A MicroSurfacePartition is the CPU-side authoring record; its GPU twin is PartitionCullRecord, a std140-packed struct the cull pass
//    reads from an SSBO. Partitions belong to a SurfacePartitionLevel, and one or more levels compose a SurfacePartitionTree per source surface.
//    This first cut builds a SINGLE level at runtime (RuntimePartitionSurface — group contiguous triangles, fit the bounds), no offline LOD tree
//    and no error-driven cut yet; the tree / level containers and the enclosing-error field are present so the later CompileSurfacePartitionTree
//    (the offline edge-collapse LOD builder) and the P4 software raster populate them without a schema change. POD + free functions, no device
//    objects — the SSBO upload of PartitionCullRecord lives with the cull pass, not here.

#pragma once
#ifndef FRONTIER_GRAPHICS_VISIBILITY_SURFACEPARTITION_H
#define FRONTIER_GRAPHICS_VISIBILITY_SURFACEPARTITION_H

#include "EngineContext/Math/LinearAlgebra_Float32.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The triangle budget one MicroSurfacePartition spans. 128 is the Nanite/Vulkanite unit — small enough that its bounding sphere and normal
//    cone stay tight (so frustum + backface cone cull is selective), large enough that the per-partition indirect-draw overhead stays amortized.
//    The runtime partitioner fills each partition to this count, the last partition of a surface taking the remainder.
constexpr uint32_t PartitionTriangleBudget = 128;

// 📝 A partition whose normal cone spans a half-angle at or beyond this is treated as non-coneable — its ConeCosine is stored as -1 so the cull
//    pass never backface-rejects it (a partition wrapping more than a hemisphere cannot be uniformly back-facing). π/2 in cosine terms is 0; a
//    small negative floor leaves headroom for the fit's numerical slack.
constexpr float PartitionConeRejectFloor = -0.10f;

// 📝 The enclosing-error sentinel a top-level partition carries when no coarser level encloses it. A very large value means "never culled by the
//    monotonic error cut" — the partition is always eligible, which is exactly the single-level behaviour of this first cut.
constexpr float PartitionUnboundedError = 3.4e38f;


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The GPU-facing per-partition cull record — the struct the P3 early / late cull compute reads from an SSBO, one per resident partition. Laid
//    out std140-friendly: two vec4s so the whole record is 32 bytes with no straddle. SphereXYZ + SphereRadius pack the bounding sphere in world
//    space; ConeAxisXYZ + ConeCosine pack the normal cone (axis unit-length, cosine of the half-angle, or -1 when non-coneable). The compute pass
//    tests this against the frustum, the view vector (backface cone), and last frame's HierarchicalDepthPyramid.
struct PartitionCullRecord
{
    float SphereX      = 0.0f;   // [m] - bounding-sphere centre X (world)
    float SphereY      = 0.0f;   // [m] - bounding-sphere centre Y (world)
    float SphereZ      = 0.0f;   // [m] - bounding-sphere centre Z (world)
    float SphereRadius = 0.0f;   // [m] - bounding-sphere radius (world)

    float ConeAxisX    = 0.0f;   // [-] - normal-cone axis X (unit)
    float ConeAxisY    = 0.0f;   // [-] - normal-cone axis Y (unit)
    float ConeAxisZ    = 0.0f;   // [-] - normal-cone axis Z (unit)
    float ConeCosine   = -1.0f;  // [-] - cosine of the cone half-angle, or -1 when non-coneable (never backface-rejected)
};

// 📝 The CPU authoring record for one MicroSurfacePartition: where its triangles sit in the source index buffer, the bounds mirrored into the GPU
//    record, and the two LOD fields the offline tree will drive. IndexOffset / IndexCount address a contiguous triangle run in the owning
//    PolygonSurface's index buffer (IndexCount is a multiple of three). GeometricError is this partition's own simplification error (0 at the
//    finest level); EnclosingError is the error of the coarser partition that encloses it (PartitionUnboundedError at the top). LevelOrdinal is
//    the tree tier this partition belongs to (0 = finest). This first cut fills one level: GeometricError 0, EnclosingError unbounded, level 0.
struct MicroSurfacePartition
{
    uint32_t IndexOffset    = 0;                         // [-] - first index of this partition's triangle run in the source index buffer
    uint32_t IndexCount     = 0;                         // [-] - index count of the run (a multiple of three)
    uint32_t LevelOrdinal   = 0;                         // [-] - tree tier this partition sits on (0 = finest)

    float    GeometricError = 0.0f;                      // [m] - this partition's own screen-space simplification error (0 at the finest level)
    float    EnclosingError = PartitionUnboundedError;   // [m] - error of the enclosing coarser partition (unbounded at the top level)

    PartitionCullRecord CullRecord = {};                 // [-] - the bounds the cull compute reads (mirrored into the GPU SSBO)
};

// 📝 One tier of a SurfacePartitionTree: the partitions produced at a single level of detail. The finest level (LevelOrdinal 0) is the source
//    triangles grouped into partitions; each coarser level halves the triangle count (offline edge-collapse, deferred). This first cut holds
//    exactly one level.
struct SurfacePartitionLevel
{
    uint32_t                           LevelOrdinal = 0;   // [-] - 0 = finest; increases toward the coarsest tier
    std::vector<MicroSurfacePartition> Partitions;         // [-] - the partitions at this tier
};

// 📝 The level-of-detail tree for one source PolygonSurface: the finest level plus every coarser level the offline builder produced. LOD
//    selection is error-driven per partition (a partition draws when its GeometricError is below the screen threshold and its EnclosingError is
//    above it — the monotonic cut). Built offline by CompileSurfacePartitionTree; this first cut carries a single level built at runtime by
//    RuntimePartitionSurface, so any error-driven cut trivially selects every partition.
struct SurfacePartitionTree
{
    uint32_t                            SourceIndexCount = 0;   // [-] - index count of the source surface this tree was built from
    std::vector<SurfacePartitionLevel>  Levels;                 // [-] - finest first; one entry in the single-level first cut
};


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build a single-level SurfacePartitionTree from a source surface at runtime: walk the triangle-list index buffer in contiguous runs of
// PartitionTriangleBudget triangles, and for each run fit a bounding sphere (over its referenced positions) and a normal cone (over its triangle
// face normals), writing one MicroSurfacePartition per run into level 0. Positions are the source vertex positions in world space; TriangleIndices
// is the triangle-list index buffer (three indices per triangle); IndexCount is its length. Returns a tree with one level; every partition carries
// GeometricError 0 and EnclosingError PartitionUnboundedError, so the later error cut selects all of them. This is the first-cut substitute for the
// offline CompileSurfacePartitionTree — no simplification, no coarser levels.
SurfacePartitionTree RuntimePartitionSurface(const Vector3f* Positions,
                                             uint32_t        PositionCount,
                                             const uint32_t* TriangleIndices,
                                             uint32_t        IndexCount);

// Flatten every partition of a tree into a contiguous PartitionCullRecord array in level-then-partition order — the exact layout the P3 cull SSBO
// expects. The returned index of each record equals the partition's global ordinal, which the raster packs into the visibility id. A convenience
// over walking Levels by hand; allocates one vector sized to the total partition count.
std::vector<PartitionCullRecord> CollectPartitionCullRecords(const SurfacePartitionTree& Tree);

} // namespace Frontier

#endif
