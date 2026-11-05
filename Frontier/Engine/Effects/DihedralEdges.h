#pragma once
/*==========================================================================================================================================
                                                           DIHEDRALEDGES.H
==========================================================================================================================================*/
// 🧩 Hard-crease dihedral edge extraction over FacetArchiveData (14° body, 28° wheels/rims).
//    Matches Three.js EdgesGeometry(geometry, thresholdDegrees): boundary edges and shared edges whose
//    adjacent facet normals diverge by at least ThresholdDegrees are emitted; coplanar tessellation seams
//    are discarded cleanly.

#include "Frontier/Engine/Scene/FacetArchive.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Frontier
{

struct DihedralEdgeSegment
{
    std::array<float, 3> A = { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> B = { 0.0f, 0.0f, 0.0f };
};

struct DihedralEdgeSet
{
    float ThresholdDegrees = 14.0f;
    std::vector<DihedralEdgeSegment> Segments;

    [[nodiscard]] size_t Count() const noexcept { return Segments.size(); }
};

[[nodiscard]] inline DihedralEdgeSet ExtractDihedralEdges(const FacetArchiveData& Archive,
                                                          float ThresholdDegrees = 14.0f)
{
    DihedralEdgeSet Result;
    Result.ThresholdDegrees = ThresholdDegrees;
    if (!Archive.IsValid())
        return Result;

    constexpr float DegToRad = 3.14159265358979323846f / 180.0f;
    const float DotThreshold = std::cos(ThresholdDegrees * DegToRad);

    // Quantize vertex positions at 0.1 mm (1e4/m) so split UV/normal seams weld cleanly, matching EdgesGeometry.
    struct QuantizedVertex
    {
        int32_t X = 0, Y = 0, Z = 0;
        bool operator==(const QuantizedVertex& Other) const noexcept
        {
            return X == Other.X && Y == Other.Y && Z == Other.Z;
        }
    };
    struct QuantizedVertexHash
    {
        size_t operator()(const QuantizedVertex& V) const noexcept
        {
            size_t H = static_cast<size_t>(static_cast<uint32_t>(V.X)) * 73856093u;
            H ^= static_cast<size_t>(static_cast<uint32_t>(V.Y)) * 19349663u;
            H ^= static_cast<size_t>(static_cast<uint32_t>(V.Z)) * 83492791u;
            return H;
        }
    };

    std::unordered_map<QuantizedVertex, uint32_t, QuantizedVertexHash> WeldedIndexByPos;
    WeldedIndexByPos.reserve(Archive.VertexCount);
    std::vector<uint32_t> VertexToWelded(Archive.VertexCount, 0u);
    std::vector<std::array<float, 3>> WeldedPositions;
    WeldedPositions.reserve(Archive.VertexCount);

    for (uint32_t Vertex = 0; Vertex < Archive.VertexCount; ++Vertex)
    {
        const float Px = Archive.Positions[Vertex * 3u + 0u];
        const float Py = Archive.Positions[Vertex * 3u + 1u];
        const float Pz = Archive.Positions[Vertex * 3u + 2u];
        const QuantizedVertex Key{
            static_cast<int32_t>(std::lround(Px * 10000.0f)),
            static_cast<int32_t>(std::lround(Py * 10000.0f)),
            static_cast<int32_t>(std::lround(Pz * 10000.0f)),
        };
        auto [It, Inserted] = WeldedIndexByPos.emplace(Key, static_cast<uint32_t>(WeldedPositions.size()));
        if (Inserted)
            WeldedPositions.push_back({ Px, Py, Pz });
        VertexToWelded[Vertex] = It->second;
    }

    struct CandidateEdge
    {
        uint32_t V0 = 0;
        uint32_t V1 = 0;
        std::array<float, 3> Normal0 = { 0.0f, 0.0f, 1.0f };
        bool Matched = false;
        bool Emitted = false;
    };

    std::unordered_map<uint64_t, size_t> HalfEdgeLookup;
    HalfEdgeLookup.reserve(static_cast<size_t>(Archive.FacetCount) * 3u);
    std::vector<CandidateEdge> Candidates;
    Candidates.reserve(static_cast<size_t>(Archive.FacetCount) * 2u);

    auto PackKey = [](uint32_t A, uint32_t B) noexcept -> uint64_t {
        return (static_cast<uint64_t>(A) << 32u) | static_cast<uint64_t>(B);
    };

    for (uint32_t Facet = 0; Facet < Archive.FacetCount; ++Facet)
    {
        const uint32_t Raw0 = Archive.Facets[Facet * 3u + 0u];
        const uint32_t Raw1 = Archive.Facets[Facet * 3u + 1u];
        const uint32_t Raw2 = Archive.Facets[Facet * 3u + 2u];
        const uint32_t W[3] = { VertexToWelded[Raw0], VertexToWelded[Raw1], VertexToWelded[Raw2] };
        if (W[0] == W[1] || W[1] == W[2] || W[2] == W[0])
            continue;

        const auto& P0 = WeldedPositions[W[0]];
        const auto& P1 = WeldedPositions[W[1]];
        const auto& P2 = WeldedPositions[W[2]];
        const float E1x = P1[0] - P0[0], E1y = P1[1] - P0[1], E1z = P1[2] - P0[2];
        const float E2x = P2[0] - P0[0], E2y = P2[1] - P0[1], E2z = P2[2] - P0[2];
        const float Nx = E1y * E2z - E1z * E2y;
        const float Ny = E1z * E2x - E1x * E2z;
        const float Nz = E1x * E2y - E1y * E2x;
        const float Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
        if (Len < 1e-10f)
            continue;
        const std::array<float, 3> Normal = { Nx / Len, Ny / Len, Nz / Len };

        for (int Corner = 0; Corner < 3; ++Corner)
        {
            const uint32_t A = W[Corner];
            const uint32_t B = W[(Corner + 1) % 3];
            const uint64_t ReverseKey = PackKey(B, A);
            auto ReverseIt = HalfEdgeLookup.find(ReverseKey);
            if (ReverseIt != HalfEdgeLookup.end())
            {
                CandidateEdge& Existing = Candidates[ReverseIt->second];
                if (!Existing.Matched)
                {
                    Existing.Matched = true;
                    const float CosAngle = Existing.Normal0[0] * Normal[0]
                        + Existing.Normal0[1] * Normal[1]
                        + Existing.Normal0[2] * Normal[2];
                    if (CosAngle <= DotThreshold && !Existing.Emitted)
                    {
                        Existing.Emitted = true;
                        Result.Segments.push_back({ WeldedPositions[Existing.V0], WeldedPositions[Existing.V1] });
                    }
                    HalfEdgeLookup.erase(ReverseIt);
                    continue;
                }
            }

            const uint64_t ForwardKey = PackKey(A, B);
            if (HalfEdgeLookup.find(ForwardKey) == HalfEdgeLookup.end())
            {
                HalfEdgeLookup.emplace(ForwardKey, Candidates.size());
                Candidates.push_back(CandidateEdge{ A, B, Normal, false, false });
            }
        }
    }

    // Open boundary edges are always silhouette/feature creases.
    for (const CandidateEdge& Edge : Candidates)
    {
        if (!Edge.Matched && !Edge.Emitted)
            Result.Segments.push_back({ WeldedPositions[Edge.V0], WeldedPositions[Edge.V1] });
    }

    return Result;
}

} // namespace Frontier
