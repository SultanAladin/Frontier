/*==========================================================================================================================================
                                                 RANGE DISTANCE ATLAS
                                     ------------------------------------------------------------------
                 Four-range camera-centered signed distance clipmap composed from .sdfspace volumes & analytic solids
==========================================================================================================================================*/
// 🧩 Multi-resolution 4-cascade signed distance clipmap (Range 0..3) with voxel-snapped centers, cross-range trilinear
//    transition blending, sphere tracing, soft cone visibility, and contiguous float4 GPU storage packing.

#pragma once

#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                      ANALYTIC & ARCHIVE CLIPMAP PRIMITIVE SPECS
//------------------------------------------------------------------------------------------------------------------------

struct PlacedDistanceVolumeSpec
{
    const VolumeDistanceField* SourceVolume        = nullptr;                   // [-] - baked .sdfspace asset
    std::array<float, 3>       TranslationMeters   = {0.0f, 0.0f, 0.0f};        // [m] - world placement offset
    float                      UniformScale        = 1.0f;                      // [-] - uniform metric scale
};

struct AnalyticSolidDistanceSpec
{
    uint32_t             ShapeCategory     = 0u;                                // [-] - 0=Box, 1=Sphere, 2=HorizontalPlane
    std::array<float, 3> CenterMeters      = {0.0f, 0.0f, 0.0f};                // [m] - world center (or plane Y in [1])
    std::array<float, 3> HalfExtentsMeters = {1.0f, 1.0f, 1.0f};                // [m] - box half-extents or [0]=sphere radius
};

struct RangeClipmapBuildSpec
{
    std::array<float, 3> AnchorWorldMeters  = {0.0f, 1.2f, 0.0f};               // [m] - camera / focus anchor
    std::array<float, 4> RangeHalfExtentsM  = {8.0f, 24.0f, 72.0f, 200.0f};     // [m] - concentric cascade half-extents
    uint32_t             ActiveRangeCount   = 4u;                               // [1..4]
    uint32_t             AxisResolution     = 32u;                              // [voxels] - per-axis resolution per cascade
};

struct RangeSphereTraceHit
{
    bool                 Hit            = false;
    float                DistanceMeters = 0.0f;                                 // [m]
    uint32_t             RangeIndex     = 0u;                                   // [0..3]
    std::array<float, 3> WorldPosition  = {0.0f, 0.0f, 0.0f};                   // [m]
    std::array<float, 3> GradientNormal = {0.0f, 1.0f, 0.0f};                   // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 RANGE DISTANCE ATLAS
//------------------------------------------------------------------------------------------------------------------------

class RangeDistanceAtlas
{
  public:
    static constexpr uint32_t MaximumRangeCount = 4u;

    RangeDistanceAtlas() = default;

    bool BuildClipmap(const RangeClipmapBuildSpec&                 Spec,
                      std::span<const PlacedDistanceVolumeSpec>    PlacedVolumes,
                      std::span<const AnalyticSolidDistanceSpec>   AnalyticSolids);

    [[nodiscard]] bool     Empty() const noexcept { return ActiveRanges_ == 0u; }
    [[nodiscard]] uint32_t ActiveRangeCount() const noexcept { return ActiveRanges_; }
    [[nodiscard]] const VolumeDistanceField& RangeVolume(uint32_t RangeIndex) const noexcept { return Ranges_[RangeIndex]; }

    // Smoothly blended 4-range clipmap distance & gradient normal on CPU (mirrors RangeDistanceSampling.slang)
    [[nodiscard]] float                SampleDistance(const std::array<float, 3>& WorldMeters) const noexcept;
    [[nodiscard]] std::array<float, 3> SampleNormal(const std::array<float, 3>& WorldMeters) const noexcept;
    [[nodiscard]] RangeSphereTraceHit  TraceRay(const std::array<float, 3>& OriginMeters,
                                                const std::array<float, 3>& DirectionUnit,
                                                float                       MaxDistanceMeters,
                                                uint32_t                    MaxSteps = 48u) const noexcept;

    // Packs 16 float4 header slots (4 per range) + all 4 voxel grids into one contiguous GPU float4 table
    void PackForGpu(std::vector<std::array<float, 4>>& OutPackedFloat4Table) const;

  private:
    std::array<VolumeDistanceField, MaximumRangeCount> Ranges_{};
    std::array<float, MaximumRangeCount>               HalfExtents_{8.0f, 24.0f, 72.0f, 200.0f};
    std::array<std::array<float, 3>, MaximumRangeCount> SnappedCenters_{};
    uint32_t                                           ActiveRanges_ = 0u;
};

} // namespace Frontier
