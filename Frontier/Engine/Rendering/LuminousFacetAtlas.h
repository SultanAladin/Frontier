/*==========================================================================================================================================
                                                 LUMINOUS FACET ATLAS
                                     ------------------------------------------------------------------
                 Flux-weighted luminous triangle CDF (O(log N) binary search) & solid-angle PDF importance sampler
==========================================================================================================================================*/
// 🧩 Builds a power-weighted (Area * Luminance(SelfRadiance)) cumulative distribution table over all self-radiant
//    world triangles so ReSTIR DI draws unbiased area-light proposals from studio softboxes, headlamps, & glowing solids.

#pragma once

#include "Engine/Geometry/GeometryProcessing/BoundingVolumeAtlas.h"
#include "Engine/Rendering/SurfaceScatteringAtlas.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{

struct LuminousFacetEntry
{
    std::array<float, 3> Vertex0Meters{0.0f, 0.0f, 0.0f};      // [m]
    float                AreaSquareMeters = 0.0f;              // [m^2]
    std::array<float, 3> Edge1Meters{0.0f, 0.0f, 0.0f};        // [m]
    float                CumulativeProbability = 1.0f;         // [-] - monotonic CDF in (0, 1]
    std::array<float, 3> Edge2Meters{0.0f, 0.0f, 0.0f};        // [m]
    float                ProbabilityMass = 0.0f;               // [-] - discrete selection probability P_i
    std::array<float, 3> SelfRadiance{0.0f, 0.0f, 0.0f};       // [W/(m^2*sr)]
    float                RadiantPowerWatts = 0.0f;             // [W]
};

struct LuminousFacetSample
{
    bool                 Valid             = false;
    uint32_t             FacetIndex        = 0u;
    std::array<float, 3> WorldPointMeters  = {0.0f, 0.0f, 0.0f};    // [m]
    std::array<float, 3> GeometricNormal   = {0.0f, 1.0f, 0.0f};    // [-]
    std::array<float, 3> SelfRadiance      = {0.0f, 0.0f, 0.0f};    // [W/(m^2*sr)]
    float                SolidAnglePdf     = 0.0f;                  // [1/sr]
    float                SelectionProbMass = 0.0f;                  // [-]
};

class LuminousFacetAtlas
{
  public:
    static constexpr uint32_t MaximumLuminousFacets = 2048u;

    LuminousFacetAtlas() = default;

    void BuildFromWorldTriangles(std::span<const PackedRayTriangle> WorldTriangles,
                                 const SurfaceScatteringAtlas&      ScatteringAtlas);

    [[nodiscard]] bool     Empty() const noexcept { return Entries_.empty(); }
    [[nodiscard]] uint32_t FacetCount() const noexcept { return static_cast<uint32_t>(Entries_.size()); }
    [[nodiscard]] float    TotalRadiantPowerWatts() const noexcept { return TotalPowerWatts_; }
    [[nodiscard]] std::span<const LuminousFacetEntry> Entries() const noexcept { return Entries_; }

    // CPU reference O(log N) binary-search sampler & solid-angle PDF evaluator (mirrors LuminousFacetSampling.slang)
    [[nodiscard]] LuminousFacetSample SampleFromReceiver(const std::array<float, 3>& ReceiverMeters,
                                                         float                       UniformCdf,
                                                         float                       UniformU,
                                                         float                       UniformV) const noexcept;

    // Packs 4 * float4 per luminous facet for GPU StructuredBuffer<float4>
    void PackForGpu(std::vector<std::array<float, 4>>& OutPackedFloat4Table) const;

  private:
    std::vector<LuminousFacetEntry> Entries_{};
    float                           TotalPowerWatts_ = 0.0f;
};

} // namespace Frontier
