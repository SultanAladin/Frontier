/*==========================================================================================================================================
                                                        EMISSIVE LIGHT SPACE
                                     ------------------------------------------------------------------
                  The second clustered-light structure: EMISSIVE SURFACES as lights. Emissive
                  triangles are grouped into small spatial clusters, every cluster instance
                    (a cluster on a placed occupant) is binned into the same 16 × 16 × 4
                     world grid the point, beam and flood lights use, and the ray tracer's
                                next-event estimation samples them there.
==========================================================================================================================================*/

// 🧩 Emissive surfaces as lights: the portable (CPU) form of the demo's EmissiveLightSpace.js, 1:1.
//
// 📝 Why. Until now an emitter (a head or tail lamp, a street lamp's lens, the tunnel strips, a traffic
//    head) lit the world only when a path happened to HIT it: a cosine or BSDF ray landing on a few
//    square centimetres of lens, then down-weighted by a heuristic (0.15 to 1) that had no light pdf to
//    balance against, so it lost energy as well as being noisy. Sampling the emitters directly, with
//    exact area pdfs, makes their light low-variance and lets the partition below put the energy back.
//
// 📝 The structure (all of it lives inside LightClusterRecords, so every consumer reads one binding):
//      EmissiveTriangles   the emissive triangles of each facet set, local space: A, E1, E2 and the
//                          material family. A set's triangles are ordered so that every cluster is a
//                          contiguous run of ≤ 8 (recursive median split of the centroids). The table is
//                          per SET, shared by every occupant of it (sixty pole heads, one table entry).
//      EmissiveClusters    one record per cluster INSTANCE: world centre and radius, occupant, first
//                          triangle, count, and the cluster's power (area × luminance, world).
//      EmissiveCells       per grid cell, up to 8 cluster ids (u16, empty = 0x7FFF): the cell's light
//                          technique draws from exactly this list, the strongest by power / distance²
//                          at the cell. A cell with more candidates keeps the eight strongest; the rest
//                          are simply not part of that cell's light technique.
//
// 📝 Unbiased by construction, with no approximation. For an emitter point the BSDF technique keeps its
//    old weight w_b(ω) (the renderer's heuristic) and the light technique takes 1 − w_b(ω) — any
//    partition of unity is a valid combination — but ONLY where the light technique can actually draw
//    that point: the point's triangle is in a cluster listed in the cell of the vertex it is seen from
//    (the shader tests exactly that with the BVH triangle index stored here). Every other emitter point,
//    listed nowhere, outside the grid, or with the feature off, takes weight 1 from the BSDF technique,
//    as it always did. The 'reach' below only decides which clusters are candidates for a cell's eight
//    slots; it is not a cutoff on correctness.

#pragma once

#include "LightClusterSpace.h"

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

inline constexpr float EmissiveFloor = 0.5f;             // [-] luminance below which a family is not a light
inline constexpr double EmissiveReachThreshold = 5.0e-4; // [-] irradiance at which a cluster stops being listed
inline constexpr uint32_t EmissiveClusterSize = 8u;      // [-] triangles per cluster at most
inline constexpr uint16_t EmissiveEmptySlot = 0x7FFFu;   // [-] an empty cell slot

// 📝 Occupant roles the structure reads and mirrors (FacetStructure's OccupantRole).
namespace OccupantRole
{
    inline constexpr uint32_t Raster = 1u;
    inline constexpr uint32_t Distance = 2u;
    inline constexpr uint32_t Cards = 4u;
    inline constexpr uint32_t Dynamic = 8u;
    inline constexpr uint32_t Fallback = 16u;
    inline constexpr uint32_t EmissiveCluster = 64u;
}

[[nodiscard]] inline double EmissiveLuminance(std::array<float, 3> Emission) noexcept
{
    return 0.2126 * double(Emission[0]) + 0.7152 * double(Emission[1]) + 0.0722 * double(Emission[2]);
}

// 📝 Inputs. The host adapts its own scene to these four flat tables once per rebuild; nothing here
//    knows about facet archives, materials or transforms.
struct EmissiveTriangleSource
{
    std::array<float, 3> A = { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> B = { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> C = { 0.0f, 0.0f, 0.0f };
    uint32_t Family = 0u;
    uint32_t BvhSlot = 0xFFFFFFFFu;   // the triangle's index in the BVH triangle buffer
};

struct EmissiveSetSource
{
    std::vector<EmissiveTriangleSource> Facets;
};

struct EmissiveOccupantSource
{
    std::array<float, 16> Transform = { 1.0f, 0.0f, 0.0f, 0.0f,
                                        0.0f, 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, 1.0f };   // column-major, Transform[12..14] = translation
    uint32_t FacetSet = 0u;
    uint32_t Roles = OccupantRole::Raster;
    bool Inactive = false;
    int32_t MaterialOverride = -1;
};

struct EmissiveLightStatistics
{
    uint32_t Sets = 0;
    uint32_t Triangles = 0;
    uint32_t Instances = 0;
    uint32_t TruncatedCells = 0;
    uint32_t EmptyCells = 0;
    uint32_t ExcludedSets = 0;
};

// 📝 Splits a list of triangle indices into runs of at most Limit by recursive median split of the
//    centroids along the longest axis; the order of the runs is the table order. Exported so the
//    validation can prove the ≤ 8 contiguity invariant directly.
[[nodiscard]] std::vector<std::vector<uint32_t>> PartitionEmissiveTriangles(
    std::span<const std::array<float, 3>> Centroids,
    uint32_t Limit = EmissiveClusterSize);

class EmissiveLightSpace
{
  public:
    EmissiveLightSpace();

    // 📝 Per facet set: its emissive triangles (a family whose emission luminance reaches the floor),
    //    grouped into clusters and appended to the table. Sets that no longer fit the table's capacity
    //    are left out and their occupants stay unrepresented, which the shader treats as "not an NEE
    //    light". Materials is indexed by family; a family beyond its end has no emission.
    //    Returns the number of sets represented.
    uint32_t Construct(std::span<const EmissiveSetSource> Sets, std::span<const std::array<float, 3>> Materials);

    // 📝 Refreshes the instances and the cell lists from the occupants; returns whether anything the
    //    GPU holds changed. EmissionScale multiplies every emission (the host's slider), so it scales
    //    the powers the lists are ranked by (ranking only: the shader reads the live material
    //    emission). Materials is indexed by family, as in Construct.
    bool Update(std::span<EmissiveOccupantSource> Occupants,
                std::span<const std::array<float, 3>> Materials,
                float EmissionScale = 1.0f);

    // 📝 Whether the roles flag should be set on an occupant (it is represented in the table), for the
    //    host to mirror into the occupant records.
    [[nodiscard]] bool Represents(uint32_t OccupantIndex) const noexcept;

    [[nodiscard]] std::span<const uint8_t> TriangleBytes() const noexcept { return Triangles; }
    [[nodiscard]] std::span<const uint8_t> ClusterBytes() const noexcept { return Clusters; }
    [[nodiscard]] std::span<const uint8_t> CellBytes() const noexcept
    {
        return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(Cells.data()), Cells.size() * sizeof(uint32_t));
    }

    [[nodiscard]] uint32_t Count() const noexcept { return ClusterCount; }
    [[nodiscard]] uint32_t TriangleCount() const noexcept { return TrianglesWritten; }
    [[nodiscard]] const EmissiveLightStatistics& Statistics() const noexcept { return Stats; }
    [[nodiscard]] bool MembershipChanged() const noexcept { return MembershipDirty; }

    // 📝 The eight cluster ids of one cell, strongest first (EmissiveEmptySlot when the slot is empty).
    [[nodiscard]] std::array<uint16_t, EmissiveCapacity::Slots> CellSlots(uint32_t Cell) const noexcept;

  private:
    struct TriangleEntry
    {
        std::array<float, 3> A = {};
        std::array<float, 3> E1 = {};
        std::array<float, 3> E2 = {};
        double Area = 0.0;
        uint32_t Family = 0u;
        uint32_t Bvh = 0xFFFFFFFFu;
        std::array<float, 3> Centroid = {};
        uint32_t Index = 0u;
    };

    struct ClusterEntry
    {
        uint32_t First = 0u;
        uint32_t TriangleCount = 0u;
        std::array<float, 3> Centre = {};
        float Radius = 0.0f;
        std::vector<uint32_t> Triangles;   // indices into the set's triangle list
    };

    struct InstanceEntry
    {
        uint32_t Occupant = 0u;
        uint32_t Set = 0u;
        uint32_t Cluster = 0u;
        bool Dynamic = false;
    };

    struct WorldCluster
    {
        std::array<float, 3> Centre = {};
        float Radius = 0.0f;
        double Power = 0.0;
    };

    struct CellLists
    {
        std::vector<uint16_t> Slot;
        std::vector<double> Importance;
        std::vector<int32_t> Count;
        std::vector<uint32_t> Total;
    };

    void Accumulate(uint32_t Slot, const WorldCluster& World, float EmissionScale, CellLists& Target, bool Lazy);
    void WriteCell(std::vector<uint32_t>& Words, uint32_t Cell, const CellLists& Lists) const;
    void BinIntoGrid(const std::vector<WorldCluster>& World, float EmissionScale, bool StaticDirty);
    [[nodiscard]] WorldCluster InstanceOf(const ClusterEntry& Cluster,
                                          uint32_t Set,
                                          const std::array<float, 16>& Transform,
                                          std::span<const std::array<float, 3>> Materials) const;

    std::vector<uint8_t> Triangles;
    std::vector<uint8_t> Clusters;
    std::vector<uint32_t> Cells;
    std::vector<uint32_t> StaticCells;

    // 📝 Per set: its clusters, and the triangles they name (set-local indices into TriangleSources).
    std::vector<std::vector<ClusterEntry>> SetClusters;
    std::vector<std::vector<TriangleEntry>> SetTriangles;

    std::vector<InstanceEntry> Instances;
    std::vector<uint32_t> Included;   // occupant indices represented in the table
    std::vector<WorldCluster> World;

    CellLists Static;
    CellLists Work;
    std::vector<int32_t> Touched;
    uint32_t TouchedCount = 0u;
    bool StaticBuilt = false;

    uint32_t ClusterCount = 0u;
    uint32_t TrianglesWritten = 0u;
    EmissiveLightStatistics Stats{};
    EmissiveLightStatistics StaticStats{};
    std::string Signature;
    std::vector<std::array<float, 16>> DynamicMatrices;
    std::vector<bool> DynamicMatrixValid;
    float LastScale = std::numeric_limits<float>::quiet_NaN();
    bool Constructed = false;
    bool MembershipDirty = false;
    bool Overfull = false;
};

} // namespace Frontier
