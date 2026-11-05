/*==========================================================================================================================================
                                                         LIGHT CLUSTER SPACE
                                     ------------------------------------------------------------------
                  Light records binned into a world-space grid: each cell's 32-bit mask names
                                 the lights whose reach sphere touches the cell.
==========================================================================================================================================*/

// 🧩 The clustered-light grid: the portable (CPU) form of the demo's LightClusterSpace.js, 1:1.
//
// 📝 The grid covers the asphalt pad the lights stand on (CourseStructure's pad). Points outside it take
//    every light (LightClusterSpace.slang), so a light is never skipped; the cost is only the lost
//    culling there.
//
// 📝 Layout of LightClusterRecords (LightClusterSpace.slang): Minimum, Extent, Lights[32],
//    CellMasks[1024], the photometric Atlas, then the emissive region — one buffer (uniform in the
//    ray-tracing group, storage in the card relighting). Minimum.w is 1 once the atlas is uploaded and
//    Extent.w is the number of emissive cluster instances (0 switches the emissive structure off).
//
//    The host fills this record every frame; only the atlas is uploaded once. Nothing here touches a
//    device: the caller copies Bytes() into the uniform/storage binding of its own backend, which is
//    what makes the whole structure testable on a headless host.

#pragma once

#include "Photometry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{

// 📝 The lighting system's records: 32 lights, 16 floats (four vec4f) each.
inline constexpr uint32_t MaxSceneLights = 32;
inline constexpr uint32_t LightRecordFloats = 16;

// 📝 The emissive region (EmissiveLightSpace fills it): a per-set triangle table, a cluster-instance
//    table and the cell lists, appended after the photometric atlas.
struct EmissiveCapacity
{
    static constexpr uint32_t Triangles = 160;
    static constexpr uint32_t Clusters = 224;
    static constexpr uint32_t Slots = 8;
};

inline constexpr uint32_t EmissiveTriangleStride = 48;
inline constexpr uint32_t EmissiveClusterStride = 32;
inline constexpr uint32_t EmissiveCellStride = 16;

// 📝 The grid: the pad the lights stand on, 16 × 16 × 4 cells.
struct LightClusterBounds
{
    static constexpr std::array<float, 3> Minimum = { -60.0f, -60.0f, -2.0f };     // [m]
    static constexpr std::array<float, 3> Extent = { 216.0f, 120.0f, 30.0f };      // [m]
    static constexpr std::array<uint32_t, 3> Resolution = { 16u, 16u, 4u };        // cells along x, y, z
};

inline constexpr uint32_t LightClusterCellCount =
    LightClusterBounds::Resolution[0] * LightClusterBounds::Resolution[1] * LightClusterBounds::Resolution[2];   // 1024

// 📝 Byte offsets inside LightClusterRecords, in declaration order.
inline constexpr uint32_t LightClusterHeaderFloats = 8;                                     // Minimum and Extent, each a vec4f
inline constexpr uint32_t LightClusterLightFloats = MaxSceneLights * LightRecordFloats;      // 512
inline constexpr uint32_t LightClusterDynamicBytes =
    (LightClusterHeaderFloats + LightClusterLightFloats + LightClusterCellCount) * 4u;       // 6176, rewritten every frame
inline constexpr uint32_t LightClusterPhotometryOffset = LightClusterDynamicBytes;           // 6176: the atlas follows, uploaded once
inline constexpr uint32_t LightClusterEmissiveTrianglesOffset =
    LightClusterPhotometryOffset + PhotometryBytes;                                          // 29760
inline constexpr uint32_t LightClusterEmissiveClustersOffset =
    LightClusterEmissiveTrianglesOffset + EmissiveCapacity::Triangles * EmissiveTriangleStride;   // 37440
inline constexpr uint32_t LightClusterEmissiveCellsOffset =
    LightClusterEmissiveClustersOffset + EmissiveCapacity::Clusters * EmissiveClusterStride;      // 44608
inline constexpr uint32_t LightClusterBytes =
    LightClusterEmissiveCellsOffset + LightClusterCellCount * EmissiveCellStride;            // 60992, under the 64 KiB uniform binding limit
static_assert(LightClusterBytes == 60992, "LightClusterRecords must stay inside one 64 KiB uniform binding");

// 📝 One cell's mask: bit i is set when light i's reach sphere touches the cell.
class LightClusterSpace
{
  public:
    LightClusterSpace();

    // 📝 LightData: the packed light records (LightingSystem's buffer), LightRecordFloats floats per
    //    light. Each active light lands in every cell whose box lies within its reach; the test is
    //    conservative (the sphere-to-box distance), so no contributing light is dropped.
    void Update(std::span<const float> LightData);

    // 📝 Uploads the packed photometric atlas (Photometry.h) once and raises the flag the shaders test;
    //    until then every light uses its analytic beam shape.
    void AssignPhotometry(std::span<const uint8_t> Atlas);

    // 📝 Uploads the emissive structure: the triangle table when it changed, the cluster table and the
    //    cell lists every time, and ClusterCount (Extent.w), which is 0 to switch the structure off.
    void AssignEmissive(std::span<const uint8_t> Triangles,
                        std::span<const uint8_t> Clusters,
                        std::span<const uint8_t> Cells,
                        uint32_t ClusterCount);

    // 📝 Forgets everything dynamic (lights, masks, emissive structure) but keeps the atlas.
    void Reset() noexcept;

    [[nodiscard]] std::span<const uint8_t> Bytes() const noexcept { return Storage; }
    [[nodiscard]] std::span<uint8_t> Bytes() noexcept { return Storage; }

    [[nodiscard]] uint32_t CellMask(uint32_t X, uint32_t Y, uint32_t Z) const noexcept;
    [[nodiscard]] uint32_t CellMask(uint32_t CellIndex) const noexcept;

    // 📝 The mask a receiver at WorldPosition would read in LightClusterSpace.slang: the cell's mask,
    //    or every light when the point lies outside the grid (so no light is ever skipped).
    [[nodiscard]] uint32_t QueryCandidates(std::array<float, 3> WorldPosition) const noexcept;

    // 📝 The host-side cull proof: every light whose reach sphere touches Position's cell is listed.
    [[nodiscard]] uint32_t ResolveConservativeMask(std::array<float, 3> WorldPosition) const noexcept;

    [[nodiscard]] bool IsPhotometric() const noexcept { return Photometric; }
    [[nodiscard]] uint32_t QueryEmissiveClusterCount() const noexcept;
    [[nodiscard]] const float* Lights() const noexcept;   // the 512 light floats, for host checks

    // 📝 DIAGNOSTIC: scales every light's reach in the cull. 1 is exact. Below 1 it deliberately drops
    //    light, which is how the tests prove that their "grid on = grid off" comparison can fail.
    float ReachScale = 1.0f;

  private:
    std::vector<uint8_t> Storage;
    bool Photometric = false;
};

} // namespace Frontier
