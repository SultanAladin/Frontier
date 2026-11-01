/*==========================================================================================================================================
                                             RANGE DISTANCE ATLAS DEFINITION
==========================================================================================================================================*/
// 🧩 Four-cascade camera-centered signed distance clipmap composer, cross-cascade smooth evaluator, & GPU float4 packer.

#include "Engine/Geometry/GeometryProcessing/RangeDistanceAtlas.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace Frontier
{
namespace
{

float EvaluateAnalyticSolidDistance(const AnalyticSolidDistanceSpec& Solid, const std::array<float, 3>& P) noexcept
{
    if (Solid.ShapeCategory == 2u)
    {
        return P[1] - Solid.CenterMeters[1];
    }
    if (Solid.ShapeCategory == 1u)
    {
        const float Dx = P[0] - Solid.CenterMeters[0];
        const float Dy = P[1] - Solid.CenterMeters[1];
        const float Dz = P[2] - Solid.CenterMeters[2];
        return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) - Solid.HalfExtentsMeters[0];
    }
    const float Qx     = std::abs(P[0] - Solid.CenterMeters[0]) - Solid.HalfExtentsMeters[0];
    const float Qy     = std::abs(P[1] - Solid.CenterMeters[1]) - Solid.HalfExtentsMeters[1];
    const float Qz     = std::abs(P[2] - Solid.CenterMeters[2]) - Solid.HalfExtentsMeters[2];
    const float OutX   = std::max(Qx, 0.0f);
    const float OutY   = std::max(Qy, 0.0f);
    const float OutZ   = std::max(Qz, 0.0f);
    const float Inside = std::min(std::max({Qx, Qy, Qz}), 0.0f);
    return std::sqrt(OutX * OutX + OutY * OutY + OutZ * OutZ) + Inside;
}

} // namespace

bool RangeDistanceAtlas::BuildClipmap(const RangeClipmapBuildSpec&               Spec,
                                      std::span<const PlacedDistanceVolumeSpec>  PlacedVolumes,
                                      std::span<const AnalyticSolidDistanceSpec> AnalyticSolids)
{
    ActiveRanges_          = std::clamp(Spec.ActiveRangeCount, 1u, MaximumRangeCount);
    const uint32_t AxisRes = std::clamp(Spec.AxisResolution, 12u, 64u);

    for (uint32_t RangeIdx = 0u; RangeIdx < MaximumRangeCount; ++RangeIdx)
    {
        Ranges_[RangeIdx] = VolumeDistanceField{};
        if (RangeIdx >= ActiveRanges_)
        {
            continue;
        }

        const float HalfExt    = std::max(1.0f, Spec.RangeHalfExtentsM[RangeIdx]);
        HalfExtents_[RangeIdx] = HalfExt;
        const float Step       = (2.0f * HalfExt) / static_cast<float>(AxisRes);

        // Snap cascade center to voxel step grid to prevent sub-voxel swimming as the camera moves.
        const std::array<float, 3> Center = {
            std::floor(Spec.AnchorWorldMeters[0] / Step + 0.5f) * Step,
            std::floor(Spec.AnchorWorldMeters[1] / Step + 0.5f) * Step,
            std::floor(Spec.AnchorWorldMeters[2] / Step + 0.5f) * Step,
        };
        SnappedCenters_[RangeIdx] = Center;

        VolumeDistanceField& Grid = Ranges_[RangeIdx];
        Grid.Dimensions           = {AxisRes, AxisRes, AxisRes};
        Grid.Minimum              = {Center[0] - HalfExt, Center[1] - HalfExt, Center[2] - HalfExt};
        Grid.Maximum              = {Center[0] + HalfExt, Center[1] + HalfExt, Center[2] + HalfExt};
        Grid.Step                 = {Step, Step, Step};
        Grid.Signed               = true;
        Grid.Distances.assign(static_cast<size_t>(AxisRes) * AxisRes * AxisRes, 1.0e5f);

        const uint32_t StrideY = AxisRes;
        const uint32_t StrideZ = AxisRes * AxisRes;

        for (uint32_t Z = 0u; Z < AxisRes; ++Z)
        {
            const float Wz = Grid.Minimum[2] + (static_cast<float>(Z) + 0.5f) * Step;
            for (uint32_t Y = 0u; Y < AxisRes; ++Y)
            {
                const float Wy = Grid.Minimum[1] + (static_cast<float>(Y) + 0.5f) * Step;
                for (uint32_t X = 0u; X < AxisRes; ++X)
                {
                    const float                Wx = Grid.Minimum[0] + (static_cast<float>(X) + 0.5f) * Step;
                    const std::array<float, 3> P  = {Wx, Wy, Wz};
                    float                      D  = 1.0e5f;

                    for (const AnalyticSolidDistanceSpec& Solid : AnalyticSolids)
                    {
                        D = std::min(D, EvaluateAnalyticSolidDistance(Solid, P));
                    }

                    for (const PlacedDistanceVolumeSpec& Vol : PlacedVolumes)
                    {
                        if (!Vol.SourceVolume || Vol.SourceVolume->Distances.empty())
                        {
                            continue;
                        }
                        const float                Scale  = std::max(1.0e-3f, Vol.UniformScale);
                        const std::array<float, 3> LocalP = {
                            (P[0] - Vol.TranslationMeters[0]) / Scale,
                            (P[1] - Vol.TranslationMeters[1]) / Scale,
                            (P[2] - Vol.TranslationMeters[2]) / Scale,
                        };
                        D = std::min(
                            D, SampleVolumeDistanceMeters(*Vol.SourceVolume, LocalP[0], LocalP[1], LocalP[2]) * Scale);
                    }

                    Grid.Distances[Z * StrideZ + Y * StrideY + X] = D;
                }
            }
        }
    }
    return ActiveRanges_ > 0u;
}

float RangeDistanceAtlas::SampleDistance(const std::array<float, 3>& WorldMeters) const noexcept
{
    if (ActiveRanges_ == 0u)
    {
        return 1.0e6f;
    }

    for (uint32_t RangeIdx = 0u; RangeIdx < ActiveRanges_; ++RangeIdx)
    {
        const std::array<float, 3>& C         = SnappedCenters_[RangeIdx];
        const float                 HalfExt   = HalfExtents_[RangeIdx];
        const float                 Chebyshev = std::max(
            {std::abs(WorldMeters[0] - C[0]), std::abs(WorldMeters[1] - C[1]), std::abs(WorldMeters[2] - C[2])});
        if (Chebyshev <= HalfExt * 0.96f || RangeIdx + 1u == ActiveRanges_)
        {
            const float FineD = SampleVolumeDistanceMeters(
                Ranges_[RangeIdx], WorldMeters[0], WorldMeters[1], WorldMeters[2]);
            const float InnerLimit = HalfExt * 0.76f;
            if (Chebyshev > InnerLimit && RangeIdx + 1u < ActiveRanges_)
            {
                const float BlendT  = std::clamp((Chebyshev - InnerLimit) / (HalfExt * 0.20f), 0.0f, 1.0f);
                const float CoarseD = SampleVolumeDistanceMeters(
                    Ranges_[RangeIdx + 1u], WorldMeters[0], WorldMeters[1], WorldMeters[2]);
                return FineD + (CoarseD - FineD) * BlendT;
            }
            return FineD;
        }
    }
    return SampleVolumeDistanceMeters(
        Ranges_[ActiveRanges_ - 1u], WorldMeters[0], WorldMeters[1], WorldMeters[2]);
}

std::array<float, 3> RangeDistanceAtlas::SampleNormal(const std::array<float, 3>& WorldMeters) const noexcept
{
    const float Eps = 0.08f;
    const float Dxp = SampleDistance({WorldMeters[0] + Eps, WorldMeters[1] - Eps, WorldMeters[2] - Eps});
    const float Dxm = SampleDistance({WorldMeters[0] - Eps, WorldMeters[1] - Eps, WorldMeters[2] + Eps});
    const float Dyp = SampleDistance({WorldMeters[0] - Eps, WorldMeters[1] + Eps, WorldMeters[2] - Eps});
    const float Dzp = SampleDistance({WorldMeters[0] + Eps, WorldMeters[1] + Eps, WorldMeters[2] + Eps});

    const float Gx  = Dxp - Dxm - Dyp + Dzp;
    const float Gy  = -Dxp - Dxm + Dyp + Dzp;
    const float Gz  = -Dxp + Dxm - Dyp + Dzp;
    const float Len = std::sqrt(Gx * Gx + Gy * Gy + Gz * Gz);
    if (Len <= 1.0e-7f)
    {
        return {0.0f, 1.0f, 0.0f};
    }
    return {Gx / Len, Gy / Len, Gz / Len};
}

RangeSphereTraceHit RangeDistanceAtlas::TraceRay(const std::array<float, 3>& OriginMeters,
                                                 const std::array<float, 3>& DirectionUnit,
                                                 float                       MaxDistanceMeters,
                                                 uint32_t                    MaxSteps) const noexcept
{
    RangeSphereTraceHit Result{};
    if (ActiveRanges_ == 0u)
    {
        return Result;
    }

    float Travel = 0.14f;
    for (uint32_t Step = 0u; Step < MaxSteps; ++Step)
    {
        if (Travel >= MaxDistanceMeters)
        {
            break;
        }
        const std::array<float, 3> Pos = {
            OriginMeters[0] + DirectionUnit[0] * Travel,
            OriginMeters[1] + DirectionUnit[1] * Travel,
            OriginMeters[2] + DirectionUnit[2] * Travel,
        };
        const float Dist = SampleDistance(Pos);

        uint32_t ActiveR = ActiveRanges_ - 1u;
        for (uint32_t R = 0u; R < ActiveRanges_; ++R)
        {
            const auto& C = SnappedCenters_[R];
            if (std::max({std::abs(Pos[0] - C[0]), std::abs(Pos[1] - C[1]), std::abs(Pos[2] - C[2])}) <= HalfExtents_[R])
            {
                ActiveR = R;
                break;
            }
        }
        const float VoxelStep =
            (2.0f * HalfExtents_[ActiveR]) / static_cast<float>(std::max(1u, Ranges_[ActiveR].Dimensions[0]));
        if (Dist <= std::max(0.015f, VoxelStep * 0.20f))
        {
            Result.Hit            = true;
            Result.DistanceMeters = Travel;
            Result.RangeIndex     = ActiveR;
            Result.WorldPosition  = Pos;
            Result.GradientNormal = SampleNormal(Pos);
            return Result;
        }
        Travel += std::max(Dist * 0.88f, VoxelStep * 0.35f);
    }
    return Result;
}

void RangeDistanceAtlas::PackForGpu(std::vector<std::array<float, 4>>& OutPackedFloat4Table) const
{
    OutPackedFloat4Table.clear();
    OutPackedFloat4Table.resize(MaximumRangeCount * 4u, {0.0f, 0.0f, 0.0f, 0.0f});

    for (uint32_t RangeIdx = 0u; RangeIdx < MaximumRangeCount; ++RangeIdx)
    {
        if (RangeIdx >= ActiveRanges_ || Ranges_[RangeIdx].Distances.empty())
        {
            continue;
        }
        const VolumeDistanceField& Grid     = Ranges_[RangeIdx];
        const uint32_t             QuadBase = static_cast<uint32_t>(OutPackedFloat4Table.size());

        OutPackedFloat4Table[RangeIdx * 4u + 0u] = {
            Grid.Minimum[0], Grid.Minimum[1], Grid.Minimum[2], static_cast<float>(Grid.Dimensions[0])};
        OutPackedFloat4Table[RangeIdx * 4u + 1u] = {
            Grid.Maximum[0], Grid.Maximum[1], Grid.Maximum[2], static_cast<float>(Grid.Dimensions[1])};
        OutPackedFloat4Table[RangeIdx * 4u + 2u] = {
            Grid.Step[0], Grid.Step[1], Grid.Step[2], static_cast<float>(Grid.Dimensions[2])};
        OutPackedFloat4Table[RangeIdx * 4u + 3u] = {
            HalfExtents_[RangeIdx], HalfExtents_[RangeIdx] * 0.20f, std::bit_cast<float>(QuadBase), 1.0f};

        const size_t VoxelCount = Grid.Distances.size();
        const size_t QuadCount  = (VoxelCount + 3u) / 4u;
        OutPackedFloat4Table.resize(static_cast<size_t>(QuadBase) + QuadCount, {1.0e5f, 1.0e5f, 1.0e5f, 1.0e5f});
        for (size_t VoxelIdx = 0u; VoxelIdx < VoxelCount; ++VoxelIdx)
        {
            OutPackedFloat4Table[static_cast<size_t>(QuadBase) + (VoxelIdx >> 2u)][VoxelIdx & 3u] = Grid.Distances[VoxelIdx];
        }
    }
}

void RangeDistanceAtlas::PackVolume3DForGpu(uint32_t&           OutWidth,
                                            uint32_t&           OutHeight,
                                            uint32_t&           OutDepth,
                                            std::vector<float>& OutScalarDistances) const
{
    if (ActiveRanges_ == 0u || Ranges_[0].Distances.empty())
    {
        OutWidth  = 4u;
        OutHeight = 4u;
        OutDepth  = 4u;
        OutScalarDistances.assign(64u, 100.0f);
        return;
    }

    const uint32_t NX           = std::max(1u, Ranges_[0].Dimensions[0]);
    const uint32_t NY           = std::max(1u, Ranges_[0].Dimensions[1]);
    const uint32_t NZ           = std::max(1u, Ranges_[0].Dimensions[2]);
    const uint32_t PaddedSlices = NZ + 2u;
    OutWidth                    = NX;
    OutHeight                   = NY;
    OutDepth                    = MaximumRangeCount * PaddedSlices;

    const size_t SliceFloats = static_cast<size_t>(NX) * NY;
    OutScalarDistances.assign(SliceFloats * OutDepth, 1.0e4f);

    for (uint32_t RangeIdx = 0u; RangeIdx < MaximumRangeCount; ++RangeIdx)
    {
        if (RangeIdx >= ActiveRanges_)
        {
            continue;
        }
        const VolumeDistanceField& Grid = Ranges_[RangeIdx];
        if (Grid.Dimensions[0] != NX || Grid.Dimensions[1] != NY || Grid.Dimensions[2] != NZ ||
            Grid.Distances.size() < SliceFloats * NZ)
        {
            continue;
        }

        const size_t BaseZ = static_cast<size_t>(RangeIdx) * PaddedSlices;
        // Guard slice 0 duplicates z = 0
        std::copy_n(Grid.Distances.data(), SliceFloats, OutScalarDistances.data() + BaseZ * SliceFloats);
        // Active slices [1 .. NZ]
        std::copy_n(Grid.Distances.data(), SliceFloats * NZ, OutScalarDistances.data() + (BaseZ + 1u) * SliceFloats);
        // Guard slice NZ + 1 duplicates z = NZ - 1
        std::copy_n(Grid.Distances.data() + (NZ - 1u) * SliceFloats,
                    SliceFloats,
                    OutScalarDistances.data() + (BaseZ + NZ + 1u) * SliceFloats);
    }
}

} // namespace Frontier
