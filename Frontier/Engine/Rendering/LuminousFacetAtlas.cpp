/*==========================================================================================================================================
                                             LUMINOUS FACET ATLAS DEFINITION
==========================================================================================================================================*/
// 🧩 Flux-weighted self-radiant triangle CDF builder, O(log N) binary-search sampler, & solid-angle PDF evaluator.

#include "Engine/Rendering/LuminousFacetAtlas.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{

constexpr float PiConstant = 3.14159265358979323846f;

float LuminanceRgb(const std::array<float, 3>& C) noexcept
{
    return 0.2126f * C[0] + 0.7152f * C[1] + 0.0722f * C[2];
}

} // namespace

void LuminousFacetAtlas::BuildFromWorldTriangles(std::span<const PackedRayTriangle> WorldTriangles,
                                                 const SurfaceScatteringAtlas&      ScatteringAtlas)
{
    Entries_.clear();
    TotalPowerWatts_ = 0.0f;

    for (const PackedRayTriangle& Tri : WorldTriangles)
    {
        if (Entries_.size() >= MaximumLuminousFacets)
        {
            break;
        }
        const CompiledSurfaceOptics& Optics = ScatteringAtlas.Slot(Tri.ClassificationId);
        const float                  Luma   = LuminanceRgb(Optics.SelfRadiance);
        if (Luma <= 1.0e-3f)
        {
            continue;
        }

        const float Cx = Tri.Edge1[1] * Tri.Edge2[2] - Tri.Edge1[2] * Tri.Edge2[1];
        const float Cy = Tri.Edge1[2] * Tri.Edge2[0] - Tri.Edge1[0] * Tri.Edge2[2];
        const float Cz = Tri.Edge1[0] * Tri.Edge2[1] - Tri.Edge1[1] * Tri.Edge2[0];
        const float Area = 0.5f * std::sqrt(Cx * Cx + Cy * Cy + Cz * Cz);
        if (Area <= 1.0e-6f)
        {
            continue;
        }

        LuminousFacetEntry Entry{};
        Entry.Vertex0Meters     = Tri.Vertex0;
        Entry.Edge1Meters       = Tri.Edge1;
        Entry.Edge2Meters       = Tri.Edge2;
        Entry.AreaSquareMeters  = Area;
        Entry.SelfRadiance      = Optics.SelfRadiance;
        Entry.RadiantPowerWatts = PiConstant * Area * Luma;
        TotalPowerWatts_ += Entry.RadiantPowerWatts;
        Entries_.push_back(Entry);
    }

    if (Entries_.empty() || TotalPowerWatts_ <= 1.0e-8f)
    {
        Entries_.clear();
        TotalPowerWatts_ = 0.0f;
        return;
    }

    double Running = 0.0;
    for (size_t Idx = 0u; Idx < Entries_.size(); ++Idx)
    {
        const float Mass = Entries_[Idx].RadiantPowerWatts / TotalPowerWatts_;
        Running += static_cast<double>(Mass);
        Entries_[Idx].ProbabilityMass       = Mass;
        Entries_[Idx].CumulativeProbability = (Idx + 1u == Entries_.size()) ? 1.0f : static_cast<float>(std::min(1.0, Running));
    }
}

LuminousFacetSample LuminousFacetAtlas::SampleFromReceiver(const std::array<float, 3>& ReceiverMeters,
                                                           float                       UniformCdf,
                                                           float                       UniformU,
                                                           float                       UniformV) const noexcept
{
    LuminousFacetSample Sample{};
    if (Entries_.empty())
    {
        return Sample;
    }

    const float Xi = std::clamp(UniformCdf, 0.0f, 0.999999f);
    uint32_t    Lo = 0u;
    uint32_t    Hi = static_cast<uint32_t>(Entries_.size()) - 1u;
    while (Lo < Hi)
    {
        const uint32_t Mid = Lo + ((Hi - Lo) >> 1u);
        if (Xi <= Entries_[Mid].CumulativeProbability)
        {
            Hi = Mid;
        }
        else
        {
            Lo = Mid + 1u;
        }
    }

    const LuminousFacetEntry& E   = Entries_[Lo];
    const float               Su  = std::sqrt(std::clamp(UniformU, 0.0f, 1.0f));
    const float               B1  = 1.0f - Su;
    const float               B2  = std::clamp(UniformV, 0.0f, 1.0f) * Su;

    Sample.Valid      = true;
    Sample.FacetIndex = Lo;
    Sample.WorldPointMeters = {
        E.Vertex0Meters[0] + B1 * E.Edge1Meters[0] + B2 * E.Edge2Meters[0],
        E.Vertex0Meters[1] + B1 * E.Edge1Meters[1] + B2 * E.Edge2Meters[1],
        E.Vertex0Meters[2] + B1 * E.Edge1Meters[2] + B2 * E.Edge2Meters[2],
    };

    const float Cx  = E.Edge1Meters[1] * E.Edge2Meters[2] - E.Edge1Meters[2] * E.Edge2Meters[1];
    const float Cy  = E.Edge1Meters[2] * E.Edge2Meters[0] - E.Edge1Meters[0] * E.Edge2Meters[2];
    const float Cz  = E.Edge1Meters[0] * E.Edge2Meters[1] - E.Edge1Meters[1] * E.Edge2Meters[0];
    const float Len = std::sqrt(Cx * Cx + Cy * Cy + Cz * Cz);
    Sample.GeometricNormal = (Len > 1.0e-8f) ? std::array<float, 3>{Cx / Len, Cy / Len, Cz / Len}
                                             : std::array<float, 3>{0.0f, 1.0f, 0.0f};
    Sample.SelfRadiance      = E.SelfRadiance;
    Sample.SelectionProbMass = E.ProbabilityMass;

    const float Dx      = Sample.WorldPointMeters[0] - ReceiverMeters[0];
    const float Dy      = Sample.WorldPointMeters[1] - ReceiverMeters[1];
    const float Dz      = Sample.WorldPointMeters[2] - ReceiverMeters[2];
    const float Dist2   = Dx * Dx + Dy * Dy + Dz * Dz;
    const float Dist    = std::sqrt(std::max(1.0e-6f, Dist2));
    const float AbsCosL = std::max(1.0e-4f,
                                   std::abs((Sample.GeometricNormal[0] * Dx + Sample.GeometricNormal[1] * Dy +
                                             Sample.GeometricNormal[2] * Dz) /
                                            Dist));
    Sample.SolidAnglePdf = (E.ProbabilityMass / std::max(1.0e-6f, E.AreaSquareMeters)) * (Dist2 / AbsCosL);
    return Sample;
}

void LuminousFacetAtlas::PackForGpu(std::vector<std::array<float, 4>>& OutPackedFloat4Table) const
{
    OutPackedFloat4Table.clear();
    if (Entries_.empty())
    {
        OutPackedFloat4Table.resize(4u, {0.0f, 0.0f, 0.0f, 0.0f});
        return;
    }

    OutPackedFloat4Table.reserve(Entries_.size() * 4u);
    for (const LuminousFacetEntry& E : Entries_)
    {
        OutPackedFloat4Table.push_back({E.Vertex0Meters[0], E.Vertex0Meters[1], E.Vertex0Meters[2], E.AreaSquareMeters});
        OutPackedFloat4Table.push_back({E.Edge1Meters[0], E.Edge1Meters[1], E.Edge1Meters[2], E.CumulativeProbability});
        OutPackedFloat4Table.push_back({E.Edge2Meters[0], E.Edge2Meters[1], E.Edge2Meters[2], E.ProbabilityMass});
        OutPackedFloat4Table.push_back({E.SelfRadiance[0], E.SelfRadiance[1], E.SelfRadiance[2], E.RadiantPowerWatts});
    }
}

} // namespace Frontier
