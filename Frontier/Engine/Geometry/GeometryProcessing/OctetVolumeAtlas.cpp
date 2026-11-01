/*==========================================================================================================================================
                                             OCTET VOLUME ATLAS DEFINITION
==========================================================================================================================================*/
// 🧩 Two-level instanced 8-wide quantized compressed BVH builder, fast top-level refit, CPU reference tracer, & GPU packer.

#include "Engine/Geometry/GeometryProcessing/OctetVolumeAtlas.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace Frontier
{
namespace
{

constexpr uint32_t InteriorLinkBit     = 0x80000000u;
constexpr uint32_t MaxLeafSpanCount    = 63u;
constexpr uint32_t SahBinCount         = 16u;
constexpr uint32_t MaxBinaryLeafCount  = 3u;

uint32_t PackBytes4(uint8_t B0, uint8_t B1, uint8_t B2, uint8_t B3) noexcept
{
    return static_cast<uint32_t>(B0) | (static_cast<uint32_t>(B1) << 8u) |
           (static_cast<uint32_t>(B2) << 16u) | (static_cast<uint32_t>(B3) << 24u);
}

float BoxSurfaceArea(const std::array<float, 3>& Lo, const std::array<float, 3>& Hi) noexcept
{
    const float Dx = std::max(0.0f, Hi[0] - Lo[0]);
    const float Dy = std::max(0.0f, Hi[1] - Lo[1]);
    const float Dz = std::max(0.0f, Hi[2] - Lo[2]);
    return 2.0f * (Dx * Dy + Dy * Dz + Dz * Dx);
}

std::array<float, 3> SafeReciprocalDirection(const std::array<float, 3>& D) noexcept
{
    return {
        1.0f / (std::abs(D[0]) > 1.0e-8f ? D[0] : (D[0] >= 0.0f ? 1.0e-8f : -1.0e-8f)),
        1.0f / (std::abs(D[1]) > 1.0e-8f ? D[1] : (D[1] >= 0.0f ? 1.0e-8f : -1.0e-8f)),
        1.0f / (std::abs(D[2]) > 1.0e-8f ? D[2] : (D[2] >= 0.0f ? 1.0e-8f : -1.0e-8f)),
    };
}

bool IntersectBoxSlab(const std::array<float, 3>& Lo,
                      const std::array<float, 3>& Hi,
                      const std::array<float, 3>& Origin,
                      const std::array<float, 3>& InvDir,
                      float                       MinT,
                      float                       MaxT,
                      float&                      OutEntryT) noexcept
{
    float T0 = MinT;
    float T1 = MaxT;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const float A = (Lo[Axis] - Origin[Axis]) * InvDir[Axis];
        const float B = (Hi[Axis] - Origin[Axis]) * InvDir[Axis];
        T0            = std::max(T0, std::min(A, B));
        T1            = std::min(T1, std::max(A, B));
        if (T1 < T0)
        {
            return false;
        }
    }
    OutEntryT = T0;
    return true;
}

bool InvertAffineColumnMajor4x4(const std::array<float, 16>& M, std::array<float, 16>& OutInv) noexcept
{
    const float A00 = M[0], A10 = M[1], A20 = M[2];
    const float A01 = M[4], A11 = M[5], A21 = M[6];
    const float A02 = M[8], A12 = M[9], A22 = M[10];
    const float Tx  = M[12], Ty = M[13], Tz = M[14];

    const float C00 = A11 * A22 - A12 * A21;
    const float C01 = A12 * A20 - A10 * A22;
    const float C02 = A10 * A21 - A11 * A20;

    const float Det = A00 * C00 + A01 * C01 + A02 * C02;
    if (std::abs(Det) <= 1.0e-12f)
    {
        OutInv = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -Tx, -Ty, -Tz, 1.0f};
        return false;
    }
    const float InvDet = 1.0f / Det;

    OutInv[0] = C00 * InvDet;
    OutInv[4] = (A02 * A21 - A01 * A22) * InvDet;
    OutInv[8] = (A01 * A12 - A02 * A11) * InvDet;
    OutInv[3] = 0.0f;

    OutInv[1] = C01 * InvDet;
    OutInv[5] = (A00 * A22 - A02 * A20) * InvDet;
    OutInv[9] = (A02 * A10 - A00 * A12) * InvDet;
    OutInv[7] = 0.0f;

    OutInv[2]  = C02 * InvDet;
    OutInv[6]  = (A01 * A20 - A00 * A21) * InvDet;
    OutInv[10] = (A00 * A11 - A01 * A10) * InvDet;
    OutInv[11] = 0.0f;

    OutInv[12] = -(OutInv[0] * Tx + OutInv[4] * Ty + OutInv[8] * Tz);
    OutInv[13] = -(OutInv[1] * Tx + OutInv[5] * Ty + OutInv[9] * Tz);
    OutInv[14] = -(OutInv[2] * Tx + OutInv[6] * Ty + OutInv[10] * Tz);
    OutInv[15] = 1.0f;
    return true;
}

std::array<float, 3> TransformPointColumnMajor(const std::array<float, 16>& M, const std::array<float, 3>& P) noexcept
{
    return {
        M[0] * P[0] + M[4] * P[1] + M[8] * P[2] + M[12],
        M[1] * P[0] + M[5] * P[1] + M[9] * P[2] + M[13],
        M[2] * P[0] + M[6] * P[1] + M[10] * P[2] + M[14],
    };
}

std::array<float, 3> TransformDirectionColumnMajor(const std::array<float, 16>& M, const std::array<float, 3>& D) noexcept
{
    return {
        M[0] * D[0] + M[4] * D[1] + M[8] * D[2],
        M[1] * D[0] + M[5] * D[1] + M[9] * D[2],
        M[2] * D[0] + M[6] * D[1] + M[10] * D[2],
    };
}

std::array<float, 3> TransformObjectNormalToWorld(const std::array<float, 16>& WorldToObject,
                                                  const std::array<float, 3>&  ObjectNormal) noexcept
{
    // Inverse-transpose of ObjectToWorld equals transpose of WorldToObject upper 3x3: row_r = column_r(WorldToObject)
    const std::array<float, 3> Raw = {
        WorldToObject[0] * ObjectNormal[0] + WorldToObject[1] * ObjectNormal[1] + WorldToObject[2] * ObjectNormal[2],
        WorldToObject[4] * ObjectNormal[0] + WorldToObject[5] * ObjectNormal[1] + WorldToObject[6] * ObjectNormal[2],
        WorldToObject[8] * ObjectNormal[0] + WorldToObject[9] * ObjectNormal[1] + WorldToObject[10] * ObjectNormal[2],
    };
    const float Len = std::sqrt(Raw[0] * Raw[0] + Raw[1] * Raw[1] + Raw[2] * Raw[2]);
    if (Len <= 1.0e-8f)
    {
        return {0.0f, 1.0f, 0.0f};
    }
    return {Raw[0] / Len, Raw[1] / Len, Raw[2] / Len};
}

std::array<float, 3> NormalizeVector3(const std::array<float, 3>& V, const std::array<float, 3>& Fallback) noexcept
{
    const float Len = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
    if (Len <= 1.0e-8f)
    {
        return Fallback;
    }
    return {V[0] / Len, V[1] / Len, V[2] / Len};
}

bool IntersectRayTriangleMollerLocal(const PackedRayTriangle&    Tri,
                                     const std::array<float, 3>& Origin,
                                     const std::array<float, 3>& Dir,
                                     float                       MinT,
                                     float                       MaxT,
                                     float&                      OutT,
                                     float&                      OutU,
                                     float&                      OutV) noexcept
{
    const std::array<float, 3> Pvec = {
        Dir[1] * Tri.Edge2[2] - Dir[2] * Tri.Edge2[1],
        Dir[2] * Tri.Edge2[0] - Dir[0] * Tri.Edge2[2],
        Dir[0] * Tri.Edge2[1] - Dir[1] * Tri.Edge2[0],
    };
    const float Det = Tri.Edge1[0] * Pvec[0] + Tri.Edge1[1] * Pvec[1] + Tri.Edge1[2] * Pvec[2];
    if (std::abs(Det) <= 1.0e-9f)
    {
        return false;
    }
    const float                InvDet = 1.0f / Det;
    const std::array<float, 3> Tvec   = {
        Origin[0] - Tri.Vertex0[0],
        Origin[1] - Tri.Vertex0[1],
        Origin[2] - Tri.Vertex0[2],
    };
    const float U = (Tvec[0] * Pvec[0] + Tvec[1] * Pvec[1] + Tvec[2] * Pvec[2]) * InvDet;
    if (U < 0.0f || U > 1.0f)
    {
        return false;
    }
    const std::array<float, 3> Qvec = {
        Tvec[1] * Tri.Edge1[2] - Tvec[2] * Tri.Edge1[1],
        Tvec[2] * Tri.Edge1[0] - Tvec[0] * Tri.Edge1[2],
        Tvec[0] * Tri.Edge1[1] - Tvec[1] * Tri.Edge1[0],
    };
    const float V = (Dir[0] * Qvec[0] + Dir[1] * Qvec[1] + Dir[2] * Qvec[2]) * InvDet;
    if (V < 0.0f || (U + V) > 1.0f)
    {
        return false;
    }
    const float T = (Tri.Edge2[0] * Qvec[0] + Tri.Edge2[1] * Qvec[1] + Tri.Edge2[2] * Qvec[2]) * InvDet;
    if (T < MinT || T >= MaxT)
    {
        return false;
    }
    OutT = T;
    OutU = U;
    OutV = V;
    return true;
}

int32_t ChooseQuantizationExponent(float Span) noexcept
{
    const float SafeSpan = std::max(Span, 1.0e-6f);
    int         RawExp   = 0;
    std::frexp(SafeSpan / 255.0f, &RawExp);
    // frexp returns m in [0.5, 1.0) s.t. x = m * 2^RawExp => 2^RawExp >= x (with exact power of two handled below)
    float Step = std::ldexp(1.0f, RawExp);
    if (Step * 255.0f < SafeSpan)
    {
        ++RawExp;
    }
    else
    {
        const float HalfStep = std::ldexp(1.0f, RawExp - 1);
        if (HalfStep * 255.0f >= SafeSpan)
        {
            --RawExp;
        }
    }
    return std::clamp(RawExp, -126, 127);
}

struct BinaryBuildEntry
{
    std::array<float, 3> Low{0.0f, 0.0f, 0.0f};
    std::array<float, 3> High{0.0f, 0.0f, 0.0f};
    uint32_t             LeftChild  = 0xFFFFFFFFu;
    uint32_t             RightChild = 0xFFFFFFFFu;
    uint32_t             FirstTri   = 0u;
    uint32_t             TriCount   = 0u;
};

struct RawTriBounds
{
    PackedRayTriangle    Tri{};
    std::array<float, 3> Low{};
    std::array<float, 3> High{};
    std::array<float, 3> Centroid{};
};

uint32_t BuildBinarySah(std::vector<BinaryBuildEntry>& BinaryCells,
                        std::vector<RawTriBounds>&     BuildTris,
                        uint32_t                       Begin,
                        uint32_t                       End)
{
    const uint32_t CellIndex = static_cast<uint32_t>(BinaryCells.size());
    BinaryCells.emplace_back();

    std::array<float, 3> Lo = {1.0e30f, 1.0e30f, 1.0e30f};
    std::array<float, 3> Hi = {-1.0e30f, -1.0e30f, -1.0e30f};
    std::array<float, 3> CLo = {1.0e30f, 1.0e30f, 1.0e30f};
    std::array<float, 3> CHi = {-1.0e30f, -1.0e30f, -1.0e30f};

    for (uint32_t Idx = Begin; Idx < End; ++Idx)
    {
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Lo[Axis]  = std::min(Lo[Axis], BuildTris[Idx].Low[Axis]);
            Hi[Axis]  = std::max(Hi[Axis], BuildTris[Idx].High[Axis]);
            CLo[Axis] = std::min(CLo[Axis], BuildTris[Idx].Centroid[Axis]);
            CHi[Axis] = std::max(CHi[Axis], BuildTris[Idx].Centroid[Axis]);
        }
    }

    const uint32_t Count = End - Begin;
    if (Count <= MaxBinaryLeafCount)
    {
        BinaryCells[CellIndex].Low      = Lo;
        BinaryCells[CellIndex].High     = Hi;
        BinaryCells[CellIndex].FirstTri = Begin;
        BinaryCells[CellIndex].TriCount = Count;
        return CellIndex;
    }

    int   BestAxis  = -1;
    int   BestSplit = -1;
    float BestCost  = static_cast<float>(Count);
    const float ParentArea = std::max(1.0e-8f, BoxSurfaceArea(Lo, Hi));

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const float Span = CHi[Axis] - CLo[Axis];
        if (Span <= 1.0e-6f)
        {
            continue;
        }

        struct SahBin
        {
            uint32_t             Count = 0u;
            std::array<float, 3> Lo{1.0e30f, 1.0e30f, 1.0e30f};
            std::array<float, 3> Hi{-1.0e30f, -1.0e30f, -1.0e30f};
        };
        std::array<SahBin, SahBinCount> Bins{};
        const float                     Scale = static_cast<float>(SahBinCount) / Span;

        for (uint32_t Idx = Begin; Idx < End; ++Idx)
        {
            const uint32_t BinIdx = std::min<uint32_t>(
                SahBinCount - 1u, static_cast<uint32_t>((BuildTris[Idx].Centroid[Axis] - CLo[Axis]) * Scale));
            ++Bins[BinIdx].Count;
            for (int K = 0; K < 3; ++K)
            {
                Bins[BinIdx].Lo[K] = std::min(Bins[BinIdx].Lo[K], BuildTris[Idx].Low[K]);
                Bins[BinIdx].Hi[K] = std::max(Bins[BinIdx].Hi[K], BuildTris[Idx].High[K]);
            }
        }

        std::array<float, SahBinCount - 1u>    LeftArea{}, RightArea{};
        std::array<uint32_t, SahBinCount - 1u> LeftCount{}, RightCount{};

        std::array<float, 3> CurLo{1.0e30f, 1.0e30f, 1.0e30f}, CurHi{-1.0e30f, -1.0e30f, -1.0e30f};
        uint32_t             CurN = 0u;
        for (uint32_t Split = 0u; Split < SahBinCount - 1u; ++Split)
        {
            CurN += Bins[Split].Count;
            if (Bins[Split].Count > 0u)
            {
                for (int K = 0; K < 3; ++K)
                {
                    CurLo[K] = std::min(CurLo[K], Bins[Split].Lo[K]);
                    CurHi[K] = std::max(CurHi[K], Bins[Split].Hi[K]);
                }
            }
            LeftCount[Split] = CurN;
            LeftArea[Split]  = CurN > 0u ? BoxSurfaceArea(CurLo, CurHi) : 0.0f;
        }

        CurLo = {1.0e30f, 1.0e30f, 1.0e30f};
        CurHi = {-1.0e30f, -1.0e30f, -1.0e30f};
        CurN  = 0u;
        for (int Split = static_cast<int>(SahBinCount) - 2; Split >= 0; --Split)
        {
            const uint32_t RBin = static_cast<uint32_t>(Split + 1);
            CurN += Bins[RBin].Count;
            if (Bins[RBin].Count > 0u)
            {
                for (int K = 0; K < 3; ++K)
                {
                    CurLo[K] = std::min(CurLo[K], Bins[RBin].Lo[K]);
                    CurHi[K] = std::max(CurHi[K], Bins[RBin].Hi[K]);
                }
            }
            RightCount[Split] = CurN;
            RightArea[Split]  = CurN > 0u ? BoxSurfaceArea(CurLo, CurHi) : 0.0f;
        }

        for (uint32_t Split = 0u; Split < SahBinCount - 1u; ++Split)
        {
            if (LeftCount[Split] == 0u || RightCount[Split] == 0u)
            {
                continue;
            }
            const float Cost = 0.5f + (LeftCount[Split] * LeftArea[Split] + RightCount[Split] * RightArea[Split]) / ParentArea;
            if (Cost < BestCost)
            {
                BestCost  = Cost;
                BestAxis  = Axis;
                BestSplit = static_cast<int>(Split);
            }
        }
    }

    uint32_t Mid = Begin + Count / 2u;
    if (BestAxis >= 0)
    {
        const float Span  = CHi[BestAxis] - CLo[BestAxis];
        const float Scale = static_cast<float>(SahBinCount) / Span;
        auto        Iter  = std::partition(BuildTris.begin() + Begin,
                                    BuildTris.begin() + End,
                                    [&](const RawTriBounds& Item)
                                    {
                                        const int Bin = std::min<int>(
                                            static_cast<int>(SahBinCount - 1u),
                                            static_cast<int>((Item.Centroid[BestAxis] - CLo[BestAxis]) * Scale));
                                        return Bin <= BestSplit;
                                    });
        Mid = static_cast<uint32_t>(std::distance(BuildTris.begin(), Iter));
        if (Mid == Begin || Mid == End)
        {
            Mid = Begin + Count / 2u;
        }
    }
    else if (Count > MaxLeafSpanCount)
    {
        // Split oversized coplanar clusters cleanly so every leaf span fits in 6 bits (<= 63 tris).
        Mid = Begin + Count / 2u;
    }
    else
    {
        BinaryCells[CellIndex].Low      = Lo;
        BinaryCells[CellIndex].High     = Hi;
        BinaryCells[CellIndex].FirstTri = Begin;
        BinaryCells[CellIndex].TriCount = Count;
        return CellIndex;
    }

    const uint32_t LeftChild       = BuildBinarySah(BinaryCells, BuildTris, Begin, Mid);
    const uint32_t RightChild      = BuildBinarySah(BinaryCells, BuildTris, Mid, End);
    BinaryCells[CellIndex].Low     = Lo;
    BinaryCells[CellIndex].High    = Hi;
    BinaryCells[CellIndex].LeftChild  = LeftChild;
    BinaryCells[CellIndex].RightChild = RightChild;
    return CellIndex;
}

uint32_t CollapseBinaryToOctet(const std::vector<BinaryBuildEntry>& BinaryCells,
                               uint32_t                             BinaryRoot,
                               std::vector<OctetInteriorBlock>&     OutBlocks)
{
    const uint32_t BlockIndex = static_cast<uint32_t>(OutBlocks.size());
    OutBlocks.emplace_back();

    const BinaryBuildEntry& Root = BinaryCells[BinaryRoot];

    std::array<uint32_t, 8> CandidateNodes{};
    uint32_t                ActiveCount = 0u;
    if (Root.TriCount > 0u)
    {
        CandidateNodes[ActiveCount++] = BinaryRoot;
    }
    else
    {
        CandidateNodes[ActiveCount++] = Root.LeftChild;
        CandidateNodes[ActiveCount++] = Root.RightChild;

        // Greedily expand the largest-surface-area interior child until 8 children are filled or all are leaves.
        while (ActiveCount < 8u)
        {
            int   BestSlot = -1;
            float BestArea = -1.0f;
            for (uint32_t Slot = 0u; Slot < ActiveCount; ++Slot)
            {
                const BinaryBuildEntry& Cand = BinaryCells[CandidateNodes[Slot]];
                if (Cand.TriCount == 0u)
                {
                    const float Area = BoxSurfaceArea(Cand.Low, Cand.High);
                    if (Area > BestArea)
                    {
                        BestArea = Area;
                        BestSlot = static_cast<int>(Slot);
                    }
                }
            }
            if (BestSlot < 0)
            {
                break;
            }
            const BinaryBuildEntry& Expanded = BinaryCells[CandidateNodes[static_cast<uint32_t>(BestSlot)]];
            CandidateNodes[static_cast<uint32_t>(BestSlot)] = Expanded.LeftChild;
            CandidateNodes[ActiveCount++]                   = Expanded.RightChild;
        }
    }

    OctetInteriorBlock Block{};
    Block.OriginMeters = Root.Low;

    const int32_t ExpX = ChooseQuantizationExponent(Root.High[0] - Root.Low[0]);
    const int32_t ExpY = ChooseQuantizationExponent(Root.High[1] - Root.Low[1]);
    const int32_t ExpZ = ChooseQuantizationExponent(Root.High[2] - Root.Low[2]);

    const float InvStepX = 1.0f / std::ldexp(1.0f, ExpX);
    const float InvStepY = 1.0f / std::ldexp(1.0f, ExpY);
    const float InvStepZ = 1.0f / std::ldexp(1.0f, ExpZ);

    const uint8_t ActiveMask = static_cast<uint8_t>((1u << ActiveCount) - 1u);
    Block.PackedExponentsMask = PackBytes4(static_cast<uint8_t>(ExpX + 128),
                                           static_cast<uint8_t>(ExpY + 128),
                                           static_cast<uint8_t>(ExpZ + 128),
                                           ActiveMask);

    for (uint32_t Slot = 0u; Slot < ActiveCount; ++Slot)
    {
        const BinaryBuildEntry& Child = BinaryCells[CandidateNodes[Slot]];
        Block.QuantizedLowX[Slot] =
            static_cast<uint8_t>(std::clamp(std::floor((Child.Low[0] - Root.Low[0]) * InvStepX), 0.0f, 255.0f));
        Block.QuantizedHighX[Slot] =
            static_cast<uint8_t>(std::clamp(std::ceil((Child.High[0] - Root.Low[0]) * InvStepX), 0.0f, 255.0f));
        Block.QuantizedLowY[Slot] =
            static_cast<uint8_t>(std::clamp(std::floor((Child.Low[1] - Root.Low[1]) * InvStepY), 0.0f, 255.0f));
        Block.QuantizedHighY[Slot] =
            static_cast<uint8_t>(std::clamp(std::ceil((Child.High[1] - Root.Low[1]) * InvStepY), 0.0f, 255.0f));
        Block.QuantizedLowZ[Slot] =
            static_cast<uint8_t>(std::clamp(std::floor((Child.Low[2] - Root.Low[2]) * InvStepZ), 0.0f, 255.0f));
        Block.QuantizedHighZ[Slot] =
            static_cast<uint8_t>(std::clamp(std::ceil((Child.High[2] - Root.Low[2]) * InvStepZ), 0.0f, 255.0f));

        if (Child.TriCount > 0u)
        {
            const uint32_t ClampedCount = std::min<uint32_t>(Child.TriCount, 127u);
            Block.SlotLinks[Slot]       = (ClampedCount << 24u) | (Child.FirstTri & 0x00FFFFFFu);
        }
        else
        {
            const uint32_t ChildBlock = CollapseBinaryToOctet(BinaryCells, CandidateNodes[Slot], OutBlocks);
            Block.SlotLinks[Slot]     = InteriorLinkBit | (ChildBlock & 0x7FFFFFFFu);
        }
    }

    OutBlocks[BlockIndex] = Block;
    return BlockIndex;
}

std::array<float, 3> ComputeTriangleLocalTangent(const PackedRayTriangle& Tri, const std::array<float, 3>& LocalNormal) noexcept
{
    const float Du1 = Tri.Uv1[0] - Tri.Uv0U;
    const float Dv1 = Tri.Uv1[1] - Tri.Uv0V;
    const float Du2 = Tri.Uv2[0] - Tri.Uv0U;
    const float Dv2 = Tri.Uv2[1] - Tri.Uv0V;
    const float Det = Du1 * Dv2 - Du2 * Dv1;
    if (std::abs(Det) > 1.0e-7f)
    {
        const float                InvDet = 1.0f / Det;
        const std::array<float, 3> RawT   = {
            (Dv2 * Tri.Edge1[0] - Dv1 * Tri.Edge2[0]) * InvDet,
            (Dv2 * Tri.Edge1[1] - Dv1 * Tri.Edge2[1]) * InvDet,
            (Dv2 * Tri.Edge1[2] - Dv1 * Tri.Edge2[2]) * InvDet,
        };
        const float NdotT = LocalNormal[0] * RawT[0] + LocalNormal[1] * RawT[1] + LocalNormal[2] * RawT[2];
        const std::array<float, 3> Ortho = {
            RawT[0] - LocalNormal[0] * NdotT,
            RawT[1] - LocalNormal[1] * NdotT,
            RawT[2] - LocalNormal[2] * NdotT,
        };
        return NormalizeVector3(Ortho, {1.0f, 0.0f, 0.0f});
    }
    return NormalizeVector3(Tri.Edge1, {1.0f, 0.0f, 0.0f});
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           ARCHIVE REGISTRATION & TLAS BUILD
//------------------------------------------------------------------------------------------------------------------------

void OctetVolumeAtlas::Clear() noexcept
{
    Archives_.clear();
    Instances_.clear();
    OrderedInstanceIndices_.clear();
    TopCells_.clear();
    TotalOctetBlocks_ = 0u;
    TotalTriangles_   = 0u;
}

uint32_t OctetVolumeAtlas::RegisterBottomArchive(const FacetField& Field, const BoundingVolumeBuildSpec& Spec)
{
    const uint32_t     ArchiveIndex = static_cast<uint32_t>(Archives_.size());
    BottomArchiveEntry Entry{};

    const size_t              TriCount = Field.Corners.size() / 3u;
    std::vector<RawTriBounds> BuildTris;
    BuildTris.reserve(TriCount);

    const bool HasUvs = (Field.CornerUVs.size() >= Field.Corners.size());
    for (size_t TriIdx = 0u; TriIdx < TriCount; ++TriIdx)
    {
        const size_t        Base = TriIdx * 3u;
        const FacetPoint3D& P0   = Field.Corners[Base + 0u];
        const FacetPoint3D& P1   = Field.Corners[Base + 1u];
        const FacetPoint3D& P2   = Field.Corners[Base + 2u];

        const float X0 = static_cast<float>(P0.X), Y0 = static_cast<float>(P0.Y), Z0 = static_cast<float>(P0.Z);
        const float X1 = static_cast<float>(P1.X), Y1 = static_cast<float>(P1.Y), Z1 = static_cast<float>(P1.Z);
        const float X2 = static_cast<float>(P2.X), Y2 = static_cast<float>(P2.Y), Z2 = static_cast<float>(P2.Z);

        RawTriBounds Item{};
        Item.Tri.Vertex0 = {X0, Y0, Z0};
        Item.Tri.Edge1   = {X1 - X0, Y1 - Y0, Z1 - Z0};
        Item.Tri.Edge2   = {X2 - X0, Y2 - Y0, Z2 - Z0};

        const float Nx = Item.Tri.Edge1[1] * Item.Tri.Edge2[2] - Item.Tri.Edge1[2] * Item.Tri.Edge2[1];
        const float Ny = Item.Tri.Edge1[2] * Item.Tri.Edge2[0] - Item.Tri.Edge1[0] * Item.Tri.Edge2[2];
        const float Nz = Item.Tri.Edge1[0] * Item.Tri.Edge2[1] - Item.Tri.Edge1[1] * Item.Tri.Edge2[0];
        if ((Nx * Nx + Ny * Ny + Nz * Nz) <= 1.0e-12f)
        {
            continue;
        }

        if (HasUvs)
        {
            Item.Tri.Uv0U = static_cast<float>(Field.CornerUVs[Base + 0u][0]);
            Item.Tri.Uv0V = static_cast<float>(Field.CornerUVs[Base + 0u][1]);
            Item.Tri.Uv1  = {static_cast<float>(Field.CornerUVs[Base + 1u][0]), static_cast<float>(Field.CornerUVs[Base + 1u][1])};
            Item.Tri.Uv2  = {static_cast<float>(Field.CornerUVs[Base + 2u][0]), static_cast<float>(Field.CornerUVs[Base + 2u][1])};
        }
        else
        {
            Item.Tri.Uv0U = X0 * 0.25f;
            Item.Tri.Uv0V = Z0 * 0.25f;
            Item.Tri.Uv1  = {X1 * 0.25f, Z1 * 0.25f};
            Item.Tri.Uv2  = {X2 * 0.25f, Z2 * 0.25f};
        }

        const uint32_t ClassId =
            (TriIdx < Field.Classification.size()) ? Field.Classification[TriIdx] : Spec.DefaultClassification;
        Item.Tri.ClassificationId  = ClassId;
        Item.Tri.TransmissiveGlass = Spec.ClassifyGlass && Spec.ClassifyGlass(ClassId);
        if (Item.Tri.TransmissiveGlass && Spec.GlassOptics)
        {
            Spec.GlassOptics(ClassId, Item.Tri.RefractiveIndex, Item.Tri.AbsorptionPerM);
        }

        Item.Low      = {std::min({X0, X1, X2}) - 1.0e-4f,
                         std::min({Y0, Y1, Y2}) - 1.0e-4f,
                         std::min({Z0, Z1, Z2}) - 1.0e-4f};
        Item.High     = {std::max({X0, X1, X2}) + 1.0e-4f,
                         std::max({Y0, Y1, Y2}) + 1.0e-4f,
                         std::max({Z0, Z1, Z2}) + 1.0e-4f};
        Item.Centroid = {(X0 + X1 + X2) / 3.0f, (Y0 + Y1 + Y2) / 3.0f, (Z0 + Z1 + Z2) / 3.0f};
        BuildTris.push_back(Item);
    }

    if (!BuildTris.empty())
    {
        std::vector<BinaryBuildEntry> BinaryCells;
        BinaryCells.reserve(BuildTris.size() * 2u);
        const uint32_t BinaryRoot = BuildBinarySah(BinaryCells, BuildTris, 0u, static_cast<uint32_t>(BuildTris.size()));
        Entry.LocalLow            = BinaryCells[BinaryRoot].Low;
        Entry.LocalHigh           = BinaryCells[BinaryRoot].High;

        Entry.Triangles.resize(BuildTris.size());
        for (size_t Idx = 0u; Idx < BuildTris.size(); ++Idx)
        {
            Entry.Triangles[Idx] = BuildTris[Idx].Tri;
        }
        Entry.Blocks.reserve(std::max<size_t>(1u, BinaryCells.size() / 3u));
        CollapseBinaryToOctet(BinaryCells, BinaryRoot, Entry.Blocks);
    }

    Entry.GlobalBlockOffset    = TotalOctetBlocks_;
    Entry.GlobalTriangleOffset = TotalTriangles_;
    TotalOctetBlocks_ += static_cast<uint32_t>(Entry.Blocks.size());
    TotalTriangles_ += static_cast<uint32_t>(Entry.Triangles.size());
    Archives_.push_back(std::move(Entry));
    return ArchiveIndex;
}

bool OctetVolumeAtlas::BuildInstanceScene(std::span<const InstancePlacementSpec> Placements)
{
    return RefitTopLevelInstances(Placements);
}

bool OctetVolumeAtlas::RefitTopLevelInstances(std::span<const InstancePlacementSpec> UpdatedPlacements)
{
    Instances_.clear();
    OrderedInstanceIndices_.clear();
    TopCells_.clear();

    Instances_.reserve(UpdatedPlacements.size());
    for (const InstancePlacementSpec& Spec : UpdatedPlacements)
    {
        if (Spec.BottomArchiveIndex >= Archives_.size())
        {
            continue;
        }
        const BottomArchiveEntry& Archive = Archives_[Spec.BottomArchiveIndex];
        if (Archive.Blocks.empty() || Archive.Triangles.empty())
        {
            continue;
        }

        ResolvedInstanceEntry Resolved{};
        Resolved.Spec           = Spec;
        Resolved.BlockOffset    = Archive.GlobalBlockOffset;
        Resolved.TriangleOffset = Archive.GlobalTriangleOffset;
        InvertAffineColumnMajor4x4(Spec.ObjectToWorldColumn, Resolved.WorldToObjectColumn);

        std::array<float, 3> WLo = {1.0e30f, 1.0e30f, 1.0e30f};
        std::array<float, 3> WHi = {-1.0e30f, -1.0e30f, -1.0e30f};
        for (int Corner = 0; Corner < 8; ++Corner)
        {
            const std::array<float, 3> LocalCorner = {
                (Corner & 1) ? Archive.LocalHigh[0] : Archive.LocalLow[0],
                (Corner & 2) ? Archive.LocalHigh[1] : Archive.LocalLow[1],
                (Corner & 4) ? Archive.LocalHigh[2] : Archive.LocalLow[2],
            };
            const std::array<float, 3> WorldCorner = TransformPointColumnMajor(Spec.ObjectToWorldColumn, LocalCorner);
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                WLo[Axis] = std::min(WLo[Axis], WorldCorner[Axis] - 1.0e-4f);
                WHi[Axis] = std::max(WHi[Axis], WorldCorner[Axis] + 1.0e-4f);
            }
        }
        Resolved.WorldLow  = WLo;
        Resolved.WorldHigh = WHi;
        Instances_.push_back(Resolved);
    }

    if (Instances_.empty())
    {
        return false;
    }

    OrderedInstanceIndices_.resize(Instances_.size());
    std::iota(OrderedInstanceIndices_.begin(), OrderedInstanceIndices_.end(), 0u);
    TopCells_.reserve(Instances_.size() * 2u);
    BuildTopRecursive(OrderedInstanceIndices_, 0u, static_cast<uint32_t>(OrderedInstanceIndices_.size()));
    return true;
}

uint32_t OctetVolumeAtlas::BuildTopRecursive(std::vector<uint32_t>& OrderedIndices, uint32_t Begin, uint32_t End)
{
    const uint32_t CellIndex = static_cast<uint32_t>(TopCells_.size());
    TopCells_.emplace_back();

    std::array<float, 3> Lo{1.0e30f, 1.0e30f, 1.0e30f}, Hi{-1.0e30f, -1.0e30f, -1.0e30f};
    std::array<float, 3> CLo{1.0e30f, 1.0e30f, 1.0e30f}, CHi{-1.0e30f, -1.0e30f, -1.0e30f};

    for (uint32_t Pos = Begin; Pos < End; ++Pos)
    {
        const ResolvedInstanceEntry& Inst = Instances_[OrderedIndices[Pos]];
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Lo[Axis]            = std::min(Lo[Axis], Inst.WorldLow[Axis]);
            Hi[Axis]            = std::max(Hi[Axis], Inst.WorldHigh[Axis]);
            const float CenterA = 0.5f * (Inst.WorldLow[Axis] + Inst.WorldHigh[Axis]);
            CLo[Axis]           = std::min(CLo[Axis], CenterA);
            CHi[Axis]           = std::max(CHi[Axis], CenterA);
        }
    }

    const uint32_t Count = End - Begin;
    if (Count <= 2u)
    {
        TopCells_[CellIndex].LowerBounds  = Lo;
        TopCells_[CellIndex].UpperBounds  = Hi;
        TopCells_[CellIndex].LeftOrFirst  = Begin;
        TopCells_[CellIndex].RightOrCount = Count;
        return CellIndex;
    }

    int   SplitAxis = 0;
    float MaxSpan   = CHi[0] - CLo[0];
    for (int Axis = 1; Axis < 3; ++Axis)
    {
        const float Span = CHi[Axis] - CLo[Axis];
        if (Span > MaxSpan)
        {
            MaxSpan   = Span;
            SplitAxis = Axis;
        }
    }

    const uint32_t Mid = Begin + Count / 2u;
    std::nth_element(OrderedIndices.begin() + Begin,
                     OrderedIndices.begin() + Mid,
                     OrderedIndices.begin() + End,
                     [&](uint32_t A, uint32_t B)
                     {
                         const float Ca = 0.5f * (Instances_[A].WorldLow[SplitAxis] + Instances_[A].WorldHigh[SplitAxis]);
                         const float Cb = 0.5f * (Instances_[B].WorldLow[SplitAxis] + Instances_[B].WorldHigh[SplitAxis]);
                         return Ca < Cb;
                     });

    const uint32_t LeftChild          = BuildTopRecursive(OrderedIndices, Begin, Mid);
    const uint32_t RightChild         = BuildTopRecursive(OrderedIndices, Mid, End);
    TopCells_[CellIndex].LowerBounds  = Lo;
    TopCells_[CellIndex].UpperBounds  = Hi;
    TopCells_[CellIndex].LeftOrFirst  = LeftChild;
    TopCells_[CellIndex].RightOrCount = RightChild | InteriorLinkBit;
    return CellIndex;
}

std::array<float, 3> OctetVolumeAtlas::LowerWorldBounds() const noexcept
{
    return TopCells_.empty() ? std::array<float, 3>{0.0f, 0.0f, 0.0f} : TopCells_[0].LowerBounds;
}

std::array<float, 3> OctetVolumeAtlas::UpperWorldBounds() const noexcept
{
    return TopCells_.empty() ? std::array<float, 3>{0.0f, 0.0f, 0.0f} : TopCells_[0].UpperBounds;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           CPU REFERENCE OCTET RAY QUERIES
//------------------------------------------------------------------------------------------------------------------------

OctetInstanceHit OctetVolumeAtlas::SearchClosestOpaque(const std::array<float, 3>& OriginMeters,
                                                       const std::array<float, 3>& DirectionUnit,
                                                       float                       MinDistanceMeters,
                                                       float                       MaxDistanceMeters) const noexcept
{
    OctetInstanceHit Result{};
    Result.DistanceMeters = MaxDistanceMeters;
    if (Empty())
    {
        return Result;
    }

    const std::array<float, 3> WorldInvDir = SafeReciprocalDirection(DirectionUnit);
    std::array<uint32_t, 48>   TopStack{};
    uint32_t                   TopPtr    = 0u;
    float                      RootEntry = 0.0f;
    if (IntersectBoxSlab(TopCells_[0].LowerBounds,
                         TopCells_[0].UpperBounds,
                         OriginMeters,
                         WorldInvDir,
                         MinDistanceMeters,
                         Result.DistanceMeters,
                         RootEntry))
    {
        TopStack[TopPtr++] = 0u;
    }

    float BestU = 0.0f;
    float BestV = 0.0f;

    while (TopPtr > 0u)
    {
        ++Result.VisitedBlockCount;
        const EnclosureCell& TopCell = TopCells_[TopStack[--TopPtr]];
        if ((TopCell.RightOrCount & InteriorLinkBit) != 0u)
        {
            const uint32_t LeftIdx  = TopCell.LeftOrFirst;
            const uint32_t RightIdx = TopCell.RightOrCount & ~InteriorLinkBit;
            float          EntryL = 0.0f, EntryR = 0.0f;
            const bool     HitL = IntersectBoxSlab(TopCells_[LeftIdx].LowerBounds,
                                               TopCells_[LeftIdx].UpperBounds,
                                               OriginMeters,
                                               WorldInvDir,
                                               MinDistanceMeters,
                                               Result.DistanceMeters,
                                               EntryL);
            const bool     HitR = IntersectBoxSlab(TopCells_[RightIdx].LowerBounds,
                                               TopCells_[RightIdx].UpperBounds,
                                               OriginMeters,
                                               WorldInvDir,
                                               MinDistanceMeters,
                                               Result.DistanceMeters,
                                               EntryR);
            if (HitL && HitR && TopPtr + 2u <= TopStack.size())
            {
                if (EntryL < EntryR)
                {
                    TopStack[TopPtr++] = RightIdx;
                    TopStack[TopPtr++] = LeftIdx;
                }
                else
                {
                    TopStack[TopPtr++] = LeftIdx;
                    TopStack[TopPtr++] = RightIdx;
                }
            }
            else if (HitL && TopPtr + 1u <= TopStack.size())
            {
                TopStack[TopPtr++] = LeftIdx;
            }
            else if (HitR && TopPtr + 1u <= TopStack.size())
            {
                TopStack[TopPtr++] = RightIdx;
            }
            continue;
        }

        for (uint32_t LeafOffset = 0u; LeafOffset < TopCell.RightOrCount; ++LeafOffset)
        {
            const uint32_t               InstIndex = OrderedInstanceIndices_[TopCell.LeftOrFirst + LeafOffset];
            const ResolvedInstanceEntry& Inst      = Instances_[InstIndex];
            float                        InstEntry = 0.0f;
            if (!IntersectBoxSlab(
                    Inst.WorldLow, Inst.WorldHigh, OriginMeters, WorldInvDir, MinDistanceMeters, Result.DistanceMeters, InstEntry))
            {
                continue;
            }

            const BottomArchiveEntry& Archive = Archives_[Inst.Spec.BottomArchiveIndex];
            const std::array<float, 3> ObjOrigin = TransformPointColumnMajor(Inst.WorldToObjectColumn, OriginMeters);
            const std::array<float, 3> ObjDir    = TransformDirectionColumnMajor(Inst.WorldToObjectColumn, DirectionUnit);
            const std::array<float, 3> ObjInvDir = SafeReciprocalDirection(ObjDir);

            std::array<uint32_t, 48> BlasStack{};
            uint32_t                 BlasPtr = 0u;
            BlasStack[BlasPtr++]             = 0u;

            while (BlasPtr > 0u)
            {
                ++Result.VisitedBlockCount;
                const OctetInteriorBlock& Block = Archive.Blocks[BlasStack[--BlasPtr]];
                const int32_t ExpX = static_cast<int32_t>(Block.PackedExponentsMask & 0xFFu) - 128;
                const int32_t ExpY = static_cast<int32_t>((Block.PackedExponentsMask >> 8u) & 0xFFu) - 128;
                const int32_t ExpZ = static_cast<int32_t>((Block.PackedExponentsMask >> 16u) & 0xFFu) - 128;
                const uint8_t Mask = static_cast<uint8_t>((Block.PackedExponentsMask >> 24u) & 0xFFu);

                const float StepX = std::ldexp(1.0f, ExpX);
                const float StepY = std::ldexp(1.0f, ExpY);
                const float StepZ = std::ldexp(1.0f, ExpZ);

                struct ChildHitCandidate
                {
                    uint32_t Link;
                    float    EntryT;
                };
                std::array<ChildHitCandidate, 8> InternalHits{};
                uint32_t                         InternalCount = 0u;

                for (uint32_t Slot = 0u; Slot < 8u; ++Slot)
                {
                    if ((Mask & (1u << Slot)) == 0u)
                    {
                        continue;
                    }
                    const std::array<float, 3> ChildLo = {
                        Block.OriginMeters[0] + static_cast<float>(Block.QuantizedLowX[Slot]) * StepX,
                        Block.OriginMeters[1] + static_cast<float>(Block.QuantizedLowY[Slot]) * StepY,
                        Block.OriginMeters[2] + static_cast<float>(Block.QuantizedLowZ[Slot]) * StepZ,
                    };
                    const std::array<float, 3> ChildHi = {
                        Block.OriginMeters[0] + static_cast<float>(Block.QuantizedHighX[Slot]) * StepX,
                        Block.OriginMeters[1] + static_cast<float>(Block.QuantizedHighY[Slot]) * StepY,
                        Block.OriginMeters[2] + static_cast<float>(Block.QuantizedHighZ[Slot]) * StepZ,
                    };
                    float ChildEntry = 0.0f;
                    if (!IntersectBoxSlab(ChildLo, ChildHi, ObjOrigin, ObjInvDir, MinDistanceMeters, Result.DistanceMeters, ChildEntry))
                    {
                        continue;
                    }

                    const uint32_t Link = Block.SlotLinks[Slot];
                    if ((Link & InteriorLinkBit) != 0u)
                    {
                        InternalHits[InternalCount++] = {Link & ~InteriorLinkBit, ChildEntry};
                    }
                    else
                    {
                        const uint32_t SpanCount = (Link >> 24u) & 0x7Fu;
                        const uint32_t SpanStart = Link & 0x00FFFFFFu;
                        for (uint32_t TriOff = 0u; TriOff < SpanCount; ++TriOff)
                        {
                            const PackedRayTriangle& Tri = Archive.Triangles[SpanStart + TriOff];
                            if (Tri.TransmissiveGlass)
                            {
                                continue;
                            }
                            float HitT = 0.0f, HitU = 0.0f, HitV = 0.0f;
                            if (IntersectRayTriangleMollerLocal(
                                    Tri, ObjOrigin, ObjDir, MinDistanceMeters, Result.DistanceMeters, HitT, HitU, HitV))
                            {
                                Result.Hit            = true;
                                Result.DistanceMeters = HitT;
                                Result.InstanceIndex  = InstIndex;
                                Result.PrimitiveIndex = SpanStart + TriOff;
                                BestU                 = HitU;
                                BestV                 = HitV;
                            }
                        }
                    }
                }

                // Push farther children first so nearest child pops first from stack.
                for (uint32_t I = 1u; I < InternalCount; ++I)
                {
                    ChildHitCandidate Key = InternalHits[I];
                    int               J   = static_cast<int>(I) - 1;
                    while (J >= 0 && InternalHits[static_cast<uint32_t>(J)].EntryT < Key.EntryT)
                    {
                        InternalHits[static_cast<uint32_t>(J + 1)] = InternalHits[static_cast<uint32_t>(J)];
                        --J;
                    }
                    InternalHits[static_cast<uint32_t>(J + 1)] = Key;
                }
                for (uint32_t I = 0u; I < InternalCount && BlasPtr < BlasStack.size(); ++I)
                {
                    BlasStack[BlasPtr++] = InternalHits[I].Link;
                }
            }
        }
    }

    if (Result.Hit)
    {
        const ResolvedInstanceEntry& Inst = Instances_[Result.InstanceIndex];
        const PackedRayTriangle&     Tri  = Archives_[Inst.Spec.BottomArchiveIndex].Triangles[Result.PrimitiveIndex];
        Result.ClassificationId           = (Inst.Spec.ClassificationOverride != 0xFFFFFFFFu)
                                                ? Inst.Spec.ClassificationOverride
                                                : Tri.ClassificationId;
        Result.WorldPosition              = {
            OriginMeters[0] + DirectionUnit[0] * Result.DistanceMeters,
            OriginMeters[1] + DirectionUnit[1] * Result.DistanceMeters,
            OriginMeters[2] + DirectionUnit[2] * Result.DistanceMeters,
        };
        const std::array<float, 3> ObjCross = {
            Tri.Edge1[1] * Tri.Edge2[2] - Tri.Edge1[2] * Tri.Edge2[1],
            Tri.Edge1[2] * Tri.Edge2[0] - Tri.Edge1[0] * Tri.Edge2[2],
            Tri.Edge1[0] * Tri.Edge2[1] - Tri.Edge1[1] * Tri.Edge2[0],
        };
        const std::array<float, 3> ObjNormal = NormalizeVector3(ObjCross, {0.0f, 1.0f, 0.0f});
        Result.GeometricNormal               = TransformObjectNormalToWorld(Inst.WorldToObjectColumn, ObjNormal);
        const std::array<float, 3> ObjTan    = ComputeTriangleLocalTangent(Tri, ObjNormal);
        Result.WorldTangent = NormalizeVector3(TransformDirectionColumnMajor(Inst.Spec.ObjectToWorldColumn, ObjTan), {1.0f, 0.0f, 0.0f});

        const float W0        = 1.0f - BestU - BestV;
        Result.TextureCoord[0] = W0 * Tri.Uv0U + BestU * Tri.Uv1[0] + BestV * Tri.Uv2[0];
        Result.TextureCoord[1] = W0 * Tri.Uv0V + BestU * Tri.Uv1[1] + BestV * Tri.Uv2[1];
    }
    return Result;
}

GlassRefractionHit OctetVolumeAtlas::SearchGlassRefractionPeel(const std::array<float, 3>& OriginMeters,
                                                               const std::array<float, 3>& DirectionUnit,
                                                               float                       MaxDistanceMeters) const noexcept
{
    GlassRefractionHit GlassOut{};
    if (Empty())
    {
        return GlassOut;
    }

    const std::array<float, 3> WorldInvDir = SafeReciprocalDirection(DirectionUnit);
    float                      FirstT      = MaxDistanceMeters;
    float                      SecondT     = 0.0f;
    uint32_t                   HitInst     = 0xFFFFFFFFu;
    uint32_t                   HitPrim     = 0xFFFFFFFFu;

    std::array<uint32_t, 48> TopStack{};
    uint32_t                 TopPtr    = 0u;
    float                    RootEntry = 0.0f;
    if (IntersectBoxSlab(TopCells_[0].LowerBounds, TopCells_[0].UpperBounds, OriginMeters, WorldInvDir, 1.0e-3f, MaxDistanceMeters, RootEntry))
    {
        TopStack[TopPtr++] = 0u;
    }

    while (TopPtr > 0u)
    {
        const EnclosureCell& TopCell = TopCells_[TopStack[--TopPtr]];
        if ((TopCell.RightOrCount & InteriorLinkBit) != 0u)
        {
            const uint32_t L = TopCell.LeftOrFirst;
            const uint32_t R = TopCell.RightOrCount & ~InteriorLinkBit;
            float          EL = 0.0f, ER = 0.0f;
            if (IntersectBoxSlab(TopCells_[L].LowerBounds, TopCells_[L].UpperBounds, OriginMeters, WorldInvDir, 1.0e-3f, MaxDistanceMeters, EL) &&
                TopPtr < TopStack.size())
            {
                TopStack[TopPtr++] = L;
            }
            if (IntersectBoxSlab(TopCells_[R].LowerBounds, TopCells_[R].UpperBounds, OriginMeters, WorldInvDir, 1.0e-3f, MaxDistanceMeters, ER) &&
                TopPtr < TopStack.size())
            {
                TopStack[TopPtr++] = R;
            }
            continue;
        }

        for (uint32_t LeafOffset = 0u; LeafOffset < TopCell.RightOrCount; ++LeafOffset)
        {
            const uint32_t               InstIndex = OrderedInstanceIndices_[TopCell.LeftOrFirst + LeafOffset];
            const ResolvedInstanceEntry& Inst      = Instances_[InstIndex];
            float                        InstEntry = 0.0f;
            if (!IntersectBoxSlab(Inst.WorldLow, Inst.WorldHigh, OriginMeters, WorldInvDir, 1.0e-3f, MaxDistanceMeters, InstEntry))
            {
                continue;
            }
            const BottomArchiveEntry&  Archive   = Archives_[Inst.Spec.BottomArchiveIndex];
            const std::array<float, 3> ObjOrigin = TransformPointColumnMajor(Inst.WorldToObjectColumn, OriginMeters);
            const std::array<float, 3> ObjDir    = TransformDirectionColumnMajor(Inst.WorldToObjectColumn, DirectionUnit);
            const std::array<float, 3> ObjInvDir = SafeReciprocalDirection(ObjDir);

            std::array<uint32_t, 48> BlasStack{};
            uint32_t                 BlasPtr = 0u;
            BlasStack[BlasPtr++]             = 0u;
            while (BlasPtr > 0u)
            {
                const OctetInteriorBlock& Block = Archive.Blocks[BlasStack[--BlasPtr]];
                const int32_t ExpX = static_cast<int32_t>(Block.PackedExponentsMask & 0xFFu) - 128;
                const int32_t ExpY = static_cast<int32_t>((Block.PackedExponentsMask >> 8u) & 0xFFu) - 128;
                const int32_t ExpZ = static_cast<int32_t>((Block.PackedExponentsMask >> 16u) & 0xFFu) - 128;
                const uint8_t Mask = static_cast<uint8_t>((Block.PackedExponentsMask >> 24u) & 0xFFu);
                const float   StepX = std::ldexp(1.0f, ExpX);
                const float   StepY = std::ldexp(1.0f, ExpY);
                const float   StepZ = std::ldexp(1.0f, ExpZ);

                for (uint32_t Slot = 0u; Slot < 8u; ++Slot)
                {
                    if ((Mask & (1u << Slot)) == 0u)
                    {
                        continue;
                    }
                    const std::array<float, 3> ChildLo = {
                        Block.OriginMeters[0] + static_cast<float>(Block.QuantizedLowX[Slot]) * StepX,
                        Block.OriginMeters[1] + static_cast<float>(Block.QuantizedLowY[Slot]) * StepY,
                        Block.OriginMeters[2] + static_cast<float>(Block.QuantizedLowZ[Slot]) * StepZ,
                    };
                    const std::array<float, 3> ChildHi = {
                        Block.OriginMeters[0] + static_cast<float>(Block.QuantizedHighX[Slot]) * StepX,
                        Block.OriginMeters[1] + static_cast<float>(Block.QuantizedHighY[Slot]) * StepY,
                        Block.OriginMeters[2] + static_cast<float>(Block.QuantizedHighZ[Slot]) * StepZ,
                    };
                    float ChildEntry = 0.0f;
                    if (!IntersectBoxSlab(ChildLo, ChildHi, ObjOrigin, ObjInvDir, 1.0e-3f, MaxDistanceMeters, ChildEntry))
                    {
                        continue;
                    }
                    const uint32_t Link = Block.SlotLinks[Slot];
                    if ((Link & InteriorLinkBit) != 0u)
                    {
                        if (BlasPtr < BlasStack.size())
                        {
                            BlasStack[BlasPtr++] = Link & ~InteriorLinkBit;
                        }
                    }
                    else
                    {
                        const uint32_t SpanCount = (Link >> 24u) & 0x7Fu;
                        const uint32_t SpanStart = Link & 0x00FFFFFFu;
                        for (uint32_t TriOff = 0u; TriOff < SpanCount; ++TriOff)
                        {
                            const PackedRayTriangle& Tri = Archive.Triangles[SpanStart + TriOff];
                            if (!Tri.TransmissiveGlass)
                            {
                                continue;
                            }
                            float T = 0.0f, U = 0.0f, V = 0.0f;
                            if (IntersectRayTriangleMollerLocal(Tri, ObjOrigin, ObjDir, 1.0e-3f, MaxDistanceMeters, T, U, V))
                            {
                                if (T < FirstT)
                                {
                                    if (HitPrim != 0xFFFFFFFFu && (T + 0.002f) < FirstT)
                                    {
                                        SecondT = FirstT;
                                    }
                                    FirstT  = T;
                                    HitInst = InstIndex;
                                    HitPrim = SpanStart + TriOff;
                                }
                                else if (T > FirstT + 0.002f && (SecondT == 0.0f || T < SecondT))
                                {
                                    SecondT = T;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (HitPrim != 0xFFFFFFFFu)
    {
        const ResolvedInstanceEntry& Inst = Instances_[HitInst];
        const PackedRayTriangle&     Tri  = Archives_[Inst.Spec.BottomArchiveIndex].Triangles[HitPrim];
        GlassOut.HitGlass                 = true;
        GlassOut.ClassificationId         = (Inst.Spec.ClassificationOverride != 0xFFFFFFFFu)
                                                ? Inst.Spec.ClassificationOverride
                                                : Tri.ClassificationId;
        GlassOut.FrontHitMeters           = FirstT;
        GlassOut.ThicknessMeters          = (SecondT > FirstT) ? std::min(0.18f, SecondT - FirstT) : 0.014f;
        GlassOut.RefractiveIndex          = Tri.RefractiveIndex;
        GlassOut.AbsorptionPerMeter       = Tri.AbsorptionPerM;

        const std::array<float, 3> ObjCross = {
            Tri.Edge1[1] * Tri.Edge2[2] - Tri.Edge1[2] * Tri.Edge2[1],
            Tri.Edge1[2] * Tri.Edge2[0] - Tri.Edge1[0] * Tri.Edge2[2],
            Tri.Edge1[0] * Tri.Edge2[1] - Tri.Edge1[1] * Tri.Edge2[0],
        };
        std::array<float, 3> WorldN = TransformObjectNormalToWorld(Inst.WorldToObjectColumn, NormalizeVector3(ObjCross, {0.0f, 1.0f, 0.0f}));
        if ((WorldN[0] * DirectionUnit[0] + WorldN[1] * DirectionUnit[1] + WorldN[2] * DirectionUnit[2]) > 0.0f)
        {
            WorldN = {-WorldN[0], -WorldN[1], -WorldN[2]};
        }
        GlassOut.FrontNormal = WorldN;
    }
    return GlassOut;
}

std::array<float, 3> OctetVolumeAtlas::EvaluateShadowTransmittance(const std::array<float, 3>& OriginMeters,
                                                                   const std::array<float, 3>& DirectionUnit,
                                                                   float                       MaxDistanceMeters) const noexcept
{
    if (Empty())
    {
        return {1.0f, 1.0f, 1.0f};
    }

    const std::array<float, 3> WorldInvDir = SafeReciprocalDirection(DirectionUnit);
    std::array<float, 3>       Trans{1.0f, 1.0f, 1.0f};
    std::array<uint32_t, 48>   TopStack{};
    uint32_t                   TopPtr    = 0u;
    float                      RootEntry = 0.0f;
    if (IntersectBoxSlab(TopCells_[0].LowerBounds, TopCells_[0].UpperBounds, OriginMeters, WorldInvDir, 1.5e-3f, MaxDistanceMeters, RootEntry))
    {
        TopStack[TopPtr++] = 0u;
    }

    while (TopPtr > 0u)
    {
        const EnclosureCell& TopCell = TopCells_[TopStack[--TopPtr]];
        if ((TopCell.RightOrCount & InteriorLinkBit) != 0u)
        {
            const uint32_t L = TopCell.LeftOrFirst;
            const uint32_t R = TopCell.RightOrCount & ~InteriorLinkBit;
            float          EL = 0.0f, ER = 0.0f;
            if (IntersectBoxSlab(TopCells_[L].LowerBounds, TopCells_[L].UpperBounds, OriginMeters, WorldInvDir, 1.5e-3f, MaxDistanceMeters, EL) &&
                TopPtr < TopStack.size())
            {
                TopStack[TopPtr++] = L;
            }
            if (IntersectBoxSlab(TopCells_[R].LowerBounds, TopCells_[R].UpperBounds, OriginMeters, WorldInvDir, 1.5e-3f, MaxDistanceMeters, ER) &&
                TopPtr < TopStack.size())
            {
                TopStack[TopPtr++] = R;
            }
            continue;
        }

        for (uint32_t LeafOffset = 0u; LeafOffset < TopCell.RightOrCount; ++LeafOffset)
        {
            const uint32_t               InstIndex = OrderedInstanceIndices_[TopCell.LeftOrFirst + LeafOffset];
            const ResolvedInstanceEntry& Inst      = Instances_[InstIndex];
            float                        InstEntry = 0.0f;
            if (!IntersectBoxSlab(Inst.WorldLow, Inst.WorldHigh, OriginMeters, WorldInvDir, 1.5e-3f, MaxDistanceMeters, InstEntry))
            {
                continue;
            }
            const BottomArchiveEntry&  Archive   = Archives_[Inst.Spec.BottomArchiveIndex];
            const std::array<float, 3> ObjOrigin = TransformPointColumnMajor(Inst.WorldToObjectColumn, OriginMeters);
            const std::array<float, 3> ObjDir    = TransformDirectionColumnMajor(Inst.WorldToObjectColumn, DirectionUnit);
            const std::array<float, 3> ObjInvDir = SafeReciprocalDirection(ObjDir);

            std::array<uint32_t, 48> BlasStack{};
            uint32_t                 BlasPtr = 0u;
            BlasStack[BlasPtr++]             = 0u;
            while (BlasPtr > 0u)
            {
                const OctetInteriorBlock& Block = Archive.Blocks[BlasStack[--BlasPtr]];
                const int32_t ExpX = static_cast<int32_t>(Block.PackedExponentsMask & 0xFFu) - 128;
                const int32_t ExpY = static_cast<int32_t>((Block.PackedExponentsMask >> 8u) & 0xFFu) - 128;
                const int32_t ExpZ = static_cast<int32_t>((Block.PackedExponentsMask >> 16u) & 0xFFu) - 128;
                const uint8_t Mask = static_cast<uint8_t>((Block.PackedExponentsMask >> 24u) & 0xFFu);
                const float   StepX = std::ldexp(1.0f, ExpX);
                const float   StepY = std::ldexp(1.0f, ExpY);
                const float   StepZ = std::ldexp(1.0f, ExpZ);

                for (uint32_t Slot = 0u; Slot < 8u; ++Slot)
                {
                    if ((Mask & (1u << Slot)) == 0u)
                    {
                        continue;
                    }
                    const std::array<float, 3> ChildLo = {
                        Block.OriginMeters[0] + static_cast<float>(Block.QuantizedLowX[Slot]) * StepX,
                        Block.OriginMeters[1] + static_cast<float>(Block.QuantizedLowY[Slot]) * StepY,
                        Block.OriginMeters[2] + static_cast<float>(Block.QuantizedLowZ[Slot]) * StepZ,
                    };
                    const std::array<float, 3> ChildHi = {
                        Block.OriginMeters[0] + static_cast<float>(Block.QuantizedHighX[Slot]) * StepX,
                        Block.OriginMeters[1] + static_cast<float>(Block.QuantizedHighY[Slot]) * StepY,
                        Block.OriginMeters[2] + static_cast<float>(Block.QuantizedHighZ[Slot]) * StepZ,
                    };
                    float ChildEntry = 0.0f;
                    if (!IntersectBoxSlab(ChildLo, ChildHi, ObjOrigin, ObjInvDir, 1.5e-3f, MaxDistanceMeters, ChildEntry))
                    {
                        continue;
                    }
                    const uint32_t Link = Block.SlotLinks[Slot];
                    if ((Link & InteriorLinkBit) != 0u)
                    {
                        if (BlasPtr < BlasStack.size())
                        {
                            BlasStack[BlasPtr++] = Link & ~InteriorLinkBit;
                        }
                    }
                    else
                    {
                        const uint32_t SpanCount = (Link >> 24u) & 0x7Fu;
                        const uint32_t SpanStart = Link & 0x00FFFFFFu;
                        for (uint32_t TriOff = 0u; TriOff < SpanCount; ++TriOff)
                        {
                            const PackedRayTriangle& Tri = Archive.Triangles[SpanStart + TriOff];
                            float                    T = 0.0f, U = 0.0f, V = 0.0f;
                            if (IntersectRayTriangleMollerLocal(Tri, ObjOrigin, ObjDir, 1.5e-3f, MaxDistanceMeters, T, U, V))
                            {
                                if (!Tri.TransmissiveGlass)
                                {
                                    return {0.0f, 0.0f, 0.0f};
                                }
                                const float F0 = std::pow((Tri.RefractiveIndex - 1.0f) / (Tri.RefractiveIndex + 1.0f), 2.0f);
                                const float Tr = (1.0f - F0);
                                Trans[0] *= Tr * std::exp(-Tri.AbsorptionPerM[0] * 0.015f);
                                Trans[1] *= Tr * std::exp(-Tri.AbsorptionPerM[1] * 0.015f);
                                Trans[2] *= Tr * std::exp(-Tri.AbsorptionPerM[2] * 0.015f);
                            }
                        }
                    }
                }
            }
        }
    }
    return Trans;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      GPU FLOAT4 TABLE PACKING & WORLD EXTRACTION
//------------------------------------------------------------------------------------------------------------------------

void OctetVolumeAtlas::PackForGpu(std::vector<std::array<float, 4>>& OutTopCellTable,
                                  std::vector<std::array<float, 4>>& OutInstanceTable,
                                  std::vector<std::array<float, 4>>& OutOctetBlockTable,
                                  std::vector<std::array<float, 4>>& OutTriangleTable) const
{
    OutTopCellTable.clear();
    OutInstanceTable.clear();
    OutOctetBlockTable.clear();
    OutTriangleTable.clear();

    if (Empty())
    {
        OutTopCellTable.resize(2u, {0.0f, 0.0f, 0.0f, 0.0f});
        OutInstanceTable.resize(9u, {0.0f, 0.0f, 0.0f, 0.0f});
        OutOctetBlockTable.resize(6u, {0.0f, 0.0f, 0.0f, 0.0f});
        OutTriangleTable.resize(5u, {0.0f, 0.0f, 0.0f, 0.0f});
        return;
    }

    OutTopCellTable.reserve(TopCells_.size() * 2u);
    for (const EnclosureCell& C : TopCells_)
    {
        OutTopCellTable.push_back({C.LowerBounds[0], C.LowerBounds[1], C.LowerBounds[2], std::bit_cast<float>(C.LeftOrFirst)});
        OutTopCellTable.push_back({C.UpperBounds[0], C.UpperBounds[1], C.UpperBounds[2], std::bit_cast<float>(C.RightOrCount)});
    }

    // Reorder Instances in OrderedInstanceIndices_ order so TLAS leaves address contiguous slices [LeftOrFirst .. +Count)
    OutInstanceTable.reserve(OrderedInstanceIndices_.size() * 9u);
    for (uint32_t InstIdx : OrderedInstanceIndices_)
    {
        const ResolvedInstanceEntry& Inst = Instances_[InstIdx];
        const auto&                  Inv  = Inst.WorldToObjectColumn;
        const auto&                  Fwd  = Inst.Spec.ObjectToWorldColumn;
        const uint32_t               OverrideFlag = Inst.Spec.OverrideOpticsEnabled ? 1u : 0u;

        // 0..3: WorldToObject columns 0..3
        OutInstanceTable.push_back({Inv[0], Inv[1], Inv[2], 0.0f});
        OutInstanceTable.push_back({Inv[4], Inv[5], Inv[6], 0.0f});
        OutInstanceTable.push_back({Inv[8], Inv[9], Inv[10], 0.0f});
        OutInstanceTable.push_back({Inv[12], Inv[13], Inv[14], 1.0f});
        // 4..5: World AABB Lo & Hi
        OutInstanceTable.push_back({Inst.WorldLow[0], Inst.WorldLow[1], Inst.WorldLow[2], Fwd[12]});
        OutInstanceTable.push_back({Inst.WorldHigh[0], Inst.WorldHigh[1], Inst.WorldHigh[2], Fwd[13]});
        // 6: ObjectToWorld tangent basis column0.xyz + Fwd[14]
        OutInstanceTable.push_back({Fwd[0], Fwd[1], Fwd[2], Fwd[14]});
        // 7: Offsets & override identifiers
        OutInstanceTable.push_back({std::bit_cast<float>(Inst.BlockOffset),
                                    std::bit_cast<float>(Inst.TriangleOffset),
                                    std::bit_cast<float>(Inst.Spec.ClassificationOverride),
                                    std::bit_cast<float>(OverrideFlag)});
        // 8: Per-instance albedo tint & self-radiance boost
        OutInstanceTable.push_back({Inst.Spec.AlbedoMultiplier[0],
                                    Inst.Spec.AlbedoMultiplier[1],
                                    Inst.Spec.AlbedoMultiplier[2],
                                    Inst.Spec.SelfRadianceScale});
    }

    OutOctetBlockTable.reserve(static_cast<size_t>(TotalOctetBlocks_) * 6u);
    OutTriangleTable.reserve(static_cast<size_t>(TotalTriangles_) * 5u);

    for (const BottomArchiveEntry& Archive : Archives_)
    {
        for (const OctetInteriorBlock& B : Archive.Blocks)
        {
            OutOctetBlockTable.push_back({B.OriginMeters[0],
                                          B.OriginMeters[1],
                                          B.OriginMeters[2],
                                          std::bit_cast<float>(B.PackedExponentsMask)});
            OutOctetBlockTable.push_back({std::bit_cast<float>(B.SlotLinks[0]),
                                          std::bit_cast<float>(B.SlotLinks[1]),
                                          std::bit_cast<float>(B.SlotLinks[2]),
                                          std::bit_cast<float>(B.SlotLinks[3])});
            OutOctetBlockTable.push_back({std::bit_cast<float>(B.SlotLinks[4]),
                                          std::bit_cast<float>(B.SlotLinks[5]),
                                          std::bit_cast<float>(B.SlotLinks[6]),
                                          std::bit_cast<float>(B.SlotLinks[7])});
            OutOctetBlockTable.push_back({std::bit_cast<float>(PackBytes4(B.QuantizedLowX[0], B.QuantizedLowX[1], B.QuantizedLowX[2], B.QuantizedLowX[3])),
                                          std::bit_cast<float>(PackBytes4(B.QuantizedLowX[4], B.QuantizedLowX[5], B.QuantizedLowX[6], B.QuantizedLowX[7])),
                                          std::bit_cast<float>(PackBytes4(B.QuantizedHighX[0], B.QuantizedHighX[1], B.QuantizedHighX[2], B.QuantizedHighX[3])),
                                          std::bit_cast<float>(PackBytes4(B.QuantizedHighX[4], B.QuantizedHighX[5], B.QuantizedHighX[6], B.QuantizedHighX[7]))});
            OutOctetBlockTable.push_back({std::bit_cast<float>(PackBytes4(B.QuantizedLowY[0], B.QuantizedLowY[1], B.QuantizedLowY[2], B.QuantizedLowY[3])),
                                          std::bit_cast<float>(PackBytes4(B.QuantizedLowY[4], B.QuantizedLowY[5], B.QuantizedLowY[6], B.QuantizedLowY[7])),
                                          std::bit_cast<float>(PackBytes4(B.QuantizedHighY[0], B.QuantizedHighY[1], B.QuantizedHighY[2], B.QuantizedHighY[3])),
                                          std::bit_cast<float>(PackBytes4(B.QuantizedHighY[4], B.QuantizedHighY[5], B.QuantizedHighY[6], B.QuantizedHighY[7]))});
            OutOctetBlockTable.push_back({std::bit_cast<float>(PackBytes4(B.QuantizedLowZ[0], B.QuantizedLowZ[1], B.QuantizedLowZ[2], B.QuantizedLowZ[3])),
                                          std::bit_cast<float>(PackBytes4(B.QuantizedLowZ[4], B.QuantizedLowZ[5], B.QuantizedLowZ[6], B.QuantizedLowZ[7])),
                                          std::bit_cast<float>(PackBytes4(B.QuantizedHighZ[0], B.QuantizedHighZ[1], B.QuantizedHighZ[2], B.QuantizedHighZ[3])),
                                          std::bit_cast<float>(PackBytes4(B.QuantizedHighZ[4], B.QuantizedHighZ[5], B.QuantizedHighZ[6], B.QuantizedHighZ[7]))});
        }

        for (const PackedRayTriangle& T : Archive.Triangles)
        {
            const uint32_t PackedClassAndGlass = (T.ClassificationId & 0x7FFFFFFFu) | (T.TransmissiveGlass ? 0x80000000u : 0u);
            OutTriangleTable.push_back({T.Vertex0[0], T.Vertex0[1], T.Vertex0[2], T.Uv0U});
            OutTriangleTable.push_back({T.Edge1[0], T.Edge1[1], T.Edge1[2], T.Uv0V});
            OutTriangleTable.push_back({T.Edge2[0], T.Edge2[1], T.Edge2[2], std::bit_cast<float>(PackedClassAndGlass)});
            OutTriangleTable.push_back({T.Uv1[0], T.Uv1[1], T.Uv2[0], T.Uv2[1]});
            OutTriangleTable.push_back({T.AbsorptionPerM[0], T.AbsorptionPerM[1], T.AbsorptionPerM[2], T.RefractiveIndex});
        }
    }
}

void OctetVolumeAtlas::ExtractWorldTriangles(std::vector<PackedRayTriangle>& OutWorldTriangles) const
{
    OutWorldTriangles.clear();
    OutWorldTriangles.reserve(TotalTriangles_);
    for (const ResolvedInstanceEntry& Inst : Instances_)
    {
        const BottomArchiveEntry& Archive = Archives_[Inst.Spec.BottomArchiveIndex];
        const auto&               Fwd     = Inst.Spec.ObjectToWorldColumn;
        for (const PackedRayTriangle& LocalT : Archive.Triangles)
        {
            PackedRayTriangle WorldT  = LocalT;
            const auto        P0Local = LocalT.Vertex0;
            const std::array<float, 3> P1Local = {
                P0Local[0] + LocalT.Edge1[0],
                P0Local[1] + LocalT.Edge1[1],
                P0Local[2] + LocalT.Edge1[2],
            };
            const std::array<float, 3> P2Local = {
                P0Local[0] + LocalT.Edge2[0],
                P0Local[1] + LocalT.Edge2[1],
                P0Local[2] + LocalT.Edge2[2],
            };
            const auto W0 = TransformPointColumnMajor(Fwd, P0Local);
            const auto W1 = TransformPointColumnMajor(Fwd, P1Local);
            const auto W2 = TransformPointColumnMajor(Fwd, P2Local);

            WorldT.Vertex0 = W0;
            WorldT.Edge1   = {W1[0] - W0[0], W1[1] - W0[1], W1[2] - W0[2]};
            WorldT.Edge2   = {W2[0] - W0[0], W2[1] - W0[1], W2[2] - W0[2]};
            if (Inst.Spec.ClassificationOverride != 0xFFFFFFFFu)
            {
                WorldT.ClassificationId = Inst.Spec.ClassificationOverride;
            }
            OutWorldTriangles.push_back(WorldT);
        }
    }
}

} // namespace Frontier
