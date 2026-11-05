//==========================================================================================================================================
//                                                       LIGHTCLUSTERSPACE.CPP
//==========================================================================================================================================
//  The clustered-light grid: light records binned into a world-space grid, one 32-bit mask per cell.
//  Ported 1:1 from Experimental/VehicleHmiPrototype/Source/LightClusterSpace.js.
//==========================================================================================================================================

#include "LightClusterSpace.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier
{

LightClusterSpace::LightClusterSpace()
    : Storage(LightClusterBytes, 0u)
{
    const float Header[8] = {
        LightClusterBounds::Minimum[0], LightClusterBounds::Minimum[1], LightClusterBounds::Minimum[2], 0.0f,
        LightClusterBounds::Extent[0], LightClusterBounds::Extent[1], LightClusterBounds::Extent[2], 0.0f
    };
    std::memcpy(Storage.data(), Header, sizeof(Header));
}

const float* LightClusterSpace::Lights() const noexcept
{
    return reinterpret_cast<const float*>(Storage.data() + LightClusterHeaderFloats * 4u);
}

uint32_t LightClusterSpace::CellMask(uint32_t CellIndex) const noexcept
{
    if (CellIndex >= LightClusterCellCount)
        return 0xFFFFFFFFu;
    uint32_t Word = 0;
    std::memcpy(&Word, Storage.data() + (LightClusterHeaderFloats + LightClusterLightFloats) * 4u + CellIndex * 4u, 4u);
    return Word;
}

uint32_t LightClusterSpace::CellMask(uint32_t X, uint32_t Y, uint32_t Z) const noexcept
{
    const auto& Resolution = LightClusterBounds::Resolution;
    if (X >= Resolution[0] || Y >= Resolution[1] || Z >= Resolution[2])
        return 0xFFFFFFFFu;
    return CellMask(X + Resolution[0] * (Y + Resolution[1] * Z));
}

void LightClusterSpace::Update(std::span<const float> LightData)
{
    const size_t CopyFloats = std::min<size_t>(LightData.size(), LightClusterLightFloats);
    if (CopyFloats > 0u)
        std::memcpy(Storage.data() + LightClusterHeaderFloats * 4u, LightData.data(), CopyFloats * sizeof(float));
    if (CopyFloats < LightClusterLightFloats)
        std::memset(Storage.data() + LightClusterHeaderFloats * 4u + CopyFloats * sizeof(float), 0,
                    (LightClusterLightFloats - CopyFloats) * sizeof(float));

    const auto& Minimum = LightClusterBounds::Minimum;
    const auto& Extent = LightClusterBounds::Extent;
    const auto& Resolution = LightClusterBounds::Resolution;
    const std::array<float, 3> CellSize = { Extent[0] / float(Resolution[0]),
                                            Extent[1] / float(Resolution[1]),
                                            Extent[2] / float(Resolution[2]) };

    // 📝 The active lights: Params.w (float 15 of the record) is the active flag.
    struct Entry { uint32_t Index; std::array<float, 3> Centre; float Reach; };
    Entry Active[MaxSceneLights];
    uint32_t ActiveCount = 0u;
    for (uint32_t Index = 0; Index < MaxSceneLights; ++Index)
    {
        const size_t Offset = size_t(Index) * LightRecordFloats;
        if (Offset + 15u >= LightData.size())
            break;
        if (LightData[Offset + 15u] < 0.5f)
            continue;
        Active[ActiveCount++] = Entry{ Index,
                                       { LightData[Offset + 0u], LightData[Offset + 1u], LightData[Offset + 2u] },
                                       LightData[Offset + 7u] * ReachScale };
    }

    for (uint32_t Z = 0; Z < Resolution[2]; ++Z)
        for (uint32_t Y = 0; Y < Resolution[1]; ++Y)
            for (uint32_t X = 0; X < Resolution[0]; ++X)
            {
                const std::array<float, 3> Low = { Minimum[0] + float(X) * CellSize[0],
                                                   Minimum[1] + float(Y) * CellSize[1],
                                                   Minimum[2] + float(Z) * CellSize[2] };
                const std::array<float, 3> High = { Low[0] + CellSize[0], Low[1] + CellSize[1], Low[2] + CellSize[2] };

                uint32_t Mask = 0u;
                for (uint32_t Slot = 0; Slot < ActiveCount; ++Slot)
                {
                    float Distance2 = 0.0f;
                    for (uint32_t Axis = 0; Axis < 3u; ++Axis)
                    {
                        const float Nearest = std::min(std::max(Active[Slot].Centre[Axis], Low[Axis]), High[Axis]);
                        const float Delta = Active[Slot].Centre[Axis] - Nearest;
                        Distance2 += Delta * Delta;
                    }
                    if (Distance2 < Active[Slot].Reach * Active[Slot].Reach)
                        Mask |= 1u << Active[Slot].Index;
                }
                const uint32_t Cell = X + Resolution[0] * (Y + Resolution[1] * Z);
                std::memcpy(Storage.data() + (LightClusterHeaderFloats + LightClusterLightFloats) * 4u + Cell * 4u, &Mask, 4u);
            }
}

void LightClusterSpace::AssignPhotometry(std::span<const uint8_t> Atlas)
{
    if (Atlas.size() != PhotometryBytes)
        return;
    std::memcpy(Storage.data() + LightClusterPhotometryOffset, Atlas.data(), PhotometryBytes);
    float Flag = 1.0f;
    std::memcpy(Storage.data() + 3u * sizeof(float), &Flag, sizeof(Flag));   // Minimum.w
    Photometric = true;
}

void LightClusterSpace::AssignEmissive(std::span<const uint8_t> Triangles,
                                       std::span<const uint8_t> Clusters,
                                       std::span<const uint8_t> Cells,
                                       uint32_t ClusterCount)
{
    if (Clusters.size() <= LightClusterBytes - LightClusterEmissiveClustersOffset)
        std::memcpy(Storage.data() + LightClusterEmissiveClustersOffset, Clusters.data(), Clusters.size());
    if (Cells.size() <= LightClusterBytes - LightClusterEmissiveCellsOffset)
        std::memcpy(Storage.data() + LightClusterEmissiveCellsOffset, Cells.data(), Cells.size());
    if (!Triangles.empty() && Triangles.size() <= LightClusterEmissiveClustersOffset - LightClusterEmissiveTrianglesOffset)
        std::memcpy(Storage.data() + LightClusterEmissiveTrianglesOffset, Triangles.data(), Triangles.size());

    // 📝 Extent.w: the number of cluster instances in the table; 0 switches the structure off.
    const float Count = float(ClusterCount);
    std::memcpy(Storage.data() + 7u * sizeof(float), &Count, sizeof(Count));
}

uint32_t LightClusterSpace::QueryEmissiveClusterCount() const noexcept
{
    float Count = 0.0f;
    std::memcpy(&Count, Storage.data() + 7u * sizeof(float), sizeof(Count));
    return Count > 0.0f ? static_cast<uint32_t>(Count) : 0u;
}

void LightClusterSpace::Reset() noexcept
{
    const float Header[8] = {
        LightClusterBounds::Minimum[0], LightClusterBounds::Minimum[1], LightClusterBounds::Minimum[2], Photometric ? 1.0f : 0.0f,
        LightClusterBounds::Extent[0], LightClusterBounds::Extent[1], LightClusterBounds::Extent[2], 0.0f
    };
    std::memcpy(Storage.data(), Header, sizeof(Header));
    std::memset(Storage.data() + LightClusterHeaderFloats * 4u, 0, LightClusterDynamicBytes - LightClusterHeaderFloats * 4u);
}

uint32_t LightClusterSpace::QueryCandidates(std::array<float, 3> WorldPosition) const noexcept
{
    const auto& Minimum = LightClusterBounds::Minimum;
    const auto& Extent = LightClusterBounds::Extent;
    const auto& Resolution = LightClusterBounds::Resolution;

    const std::array<float, 3> Local = { (WorldPosition[0] - Minimum[0]) / Extent[0],
                                         (WorldPosition[1] - Minimum[1]) / Extent[1],
                                         (WorldPosition[2] - Minimum[2]) / Extent[2] };
    // 📝 Points outside the grid take every light: the mask is then all ones, so no light is skipped.
    for (uint32_t Axis = 0; Axis < 3u; ++Axis)
        if (!(Local[Axis] >= 0.0f) || !(Local[Axis] < 1.0f))
            return 0xFFFFFFFFu;

    std::array<uint32_t, 3> Cell = {};
    for (uint32_t Axis = 0; Axis < 3u; ++Axis)
        Cell[Axis] = std::min<uint32_t>(static_cast<uint32_t>(Local[Axis] * float(Resolution[Axis])), Resolution[Axis] - 1u);
    return CellMask(Cell[0], Cell[1], Cell[2]);
}

uint32_t LightClusterSpace::ResolveConservativeMask(std::array<float, 3> WorldPosition) const noexcept
{
    const auto& Minimum = LightClusterBounds::Minimum;
    const auto& Extent = LightClusterBounds::Extent;
    const auto& Resolution = LightClusterBounds::Resolution;
    const std::array<float, 3> CellSize = { Extent[0] / float(Resolution[0]),
                                            Extent[1] / float(Resolution[1]),
                                            Extent[2] / float(Resolution[2]) };

    std::array<uint32_t, 3> Cell = {};
    for (uint32_t Axis = 0; Axis < 3u; ++Axis)
    {
        const float Local = (WorldPosition[Axis] - Minimum[Axis]) / CellSize[Axis];
        Cell[Axis] = std::min<uint32_t>(static_cast<uint32_t>(std::floor(Local)), Resolution[Axis] - 1u);
    }
    const std::array<float, 3> Low = { Minimum[0] + float(Cell[0]) * CellSize[0],
                                       Minimum[1] + float(Cell[1]) * CellSize[1],
                                       Minimum[2] + float(Cell[2]) * CellSize[2] };
    const std::array<float, 3> High = { Low[0] + CellSize[0], Low[1] + CellSize[1], Low[2] + CellSize[2] };

    const float* Lights = this->Lights();
    uint32_t Mask = 0u;
    for (uint32_t Index = 0; Index < MaxSceneLights; ++Index)
    {
        const float* Record = Lights + size_t(Index) * LightRecordFloats;
        if (Record[15] < 0.5f)
            continue;
        float Distance2 = 0.0f;
        for (uint32_t Axis = 0; Axis < 3u; ++Axis)
        {
            const float Nearest = std::min(std::max(Record[Axis], Low[Axis]), High[Axis]);
            const float Delta = Record[Axis] - Nearest;
            Distance2 += Delta * Delta;
        }
        const float Reach = Record[7] * ReachScale;
        if (Distance2 < Reach * Reach)
            Mask |= 1u << Index;
    }
    return Mask;
}

} // namespace Frontier
