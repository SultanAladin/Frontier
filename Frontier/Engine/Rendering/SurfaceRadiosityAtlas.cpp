/*==========================================================================================================================================
                                            SURFACE RADIOSITY ATLAS DEFINITION
==========================================================================================================================================*/
// 🧩 Six-axis surface patch capture, multi-bounce form-factor radiosity solver, spatial-normal interpolator, & GPU packer.

#include "Engine/Rendering/SurfaceRadiosityAtlas.h"

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

void SurfaceRadiosityAtlas::BuildPatchesFromInstances(std::span<const SurfaceCardBuildInstance> Instances)
{
    Patches_.clear();
    Patches_.reserve(std::min<size_t>(MaximumPatchCount, Instances.size() * 6u));

    static constexpr std::array<std::array<float, 3>, 6> AxisNormals = {{
        { 1.0f,  0.0f,  0.0f},
        {-1.0f,  0.0f,  0.0f},
        { 0.0f,  1.0f,  0.0f},
        { 0.0f, -1.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f},
        { 0.0f,  0.0f, -1.0f},
    }};

    for (const SurfaceCardBuildInstance& Inst : Instances)
    {
        if (Patches_.size() + 6u > MaximumPatchCount)
        {
            break;
        }
        const float Dx = std::max(0.05f, Inst.UpperWorldMeters[0] - Inst.LowerWorldMeters[0]);
        const float Dy = std::max(0.05f, Inst.UpperWorldMeters[1] - Inst.LowerWorldMeters[1]);
        const float Dz = std::max(0.05f, Inst.UpperWorldMeters[2] - Inst.LowerWorldMeters[2]);
        const std::array<float, 3> Mid = {
            0.5f * (Inst.LowerWorldMeters[0] + Inst.UpperWorldMeters[0]),
            0.5f * (Inst.LowerWorldMeters[1] + Inst.UpperWorldMeters[1]),
            0.5f * (Inst.LowerWorldMeters[2] + Inst.UpperWorldMeters[2]),
        };

        const std::array<float, 6> FaceAreas = {
            Dy * Dz, Dy * Dz, Dx * Dz, Dx * Dz, Dx * Dy, Dx * Dy,
        };
        const std::array<float, 3> ClampedRho = {
            std::clamp(Inst.DiffuseReflectance[0], 0.01f, 0.92f),
            std::clamp(Inst.DiffuseReflectance[1], 0.01f, 0.92f),
            std::clamp(Inst.DiffuseReflectance[2], 0.01f, 0.92f),
        };

        for (size_t Face = 0u; Face < 6u; ++Face)
        {
            if (FaceAreas[Face] < 0.02f)
            {
                continue;
            }
            // Skip downward face flush with or below ground plane
            if (Face == 3u && Inst.LowerWorldMeters[1] <= 0.08f)
            {
                continue;
            }
            SurfacePatchEntry Patch{};
            Patch.NormalUnit = AxisNormals[Face];
            Patch.CenterMeters = {
                Mid[0] + Patch.NormalUnit[0] * (0.5f * Dx),
                Mid[1] + Patch.NormalUnit[1] * (0.5f * Dy),
                Mid[2] + Patch.NormalUnit[2] * (0.5f * Dz),
            };
            Patch.AreaSquareMeters   = std::min(FaceAreas[Face], 64.0f);
            Patch.ExtentRadiusMeters = std::max(0.35f, std::sqrt(Patch.AreaSquareMeters));
            Patch.DiffuseReflectance = ClampedRho;
            Patch.SelfRadiance       = Inst.SelfRadiance;
            Patch.SolvedRadiosity    = Inst.SelfRadiance;
            Patches_.push_back(Patch);
        }
    }
}

RadiositySolveMetrics SurfaceRadiosityAtlas::SolveCpuMultiBounce(std::span<const SurfaceCardLuminaireInput> Luminaires,
                                                                 const std::array<float, 3>&                SkyZenithRadiance,
                                                                 float                                      SkyWeight,
                                                                 float                                      IndirectGain,
                                                                 uint32_t                                   BounceCount,
                                                                 const RangeDistanceAtlas*                  OptionalClipmap)
{
    RadiositySolveMetrics Metrics{};
    Metrics.PatchCount = static_cast<uint32_t>(Patches_.size());
    if (Patches_.empty())
    {
        return Metrics;
    }

    const size_t                         N = Patches_.size();
    std::vector<std::array<float, 3>>    DirectExitance(N, {0.0f, 0.0f, 0.0f});
    std::vector<std::array<float, 3>>    CurrentB(N, {0.0f, 0.0f, 0.0f});
    std::vector<std::array<float, 3>>    NextB(N, {0.0f, 0.0f, 0.0f});

    // Step 1: Direct luminaire + sky hemisphere injection -> R_0 = L_e + (rho / pi) * E_direct
    for (size_t I = 0u; I < N; ++I)
    {
        const SurfacePatchEntry&   P   = Patches_[I];
        std::array<float, 3>       Irr = {0.0f, 0.0f, 0.0f};
        const float                SkyUp = 0.5f * P.NormalUnit[1] + 0.5f;
        for (int C = 0; C < 3; ++C)
        {
            Irr[C] += SkyZenithRadiance[C] * SkyWeight * PiConstant * (0.25f + 0.75f * SkyUp);
        }

        for (const SurfaceCardLuminaireInput& L : Luminaires)
        {
            if (L.Category == 0u)
            {
                const float NdotL = std::max(0.0f,
                                             P.NormalUnit[0] * L.PositionOrDir[0] + P.NormalUnit[1] * L.PositionOrDir[1] +
                                                 P.NormalUnit[2] * L.PositionOrDir[2]);
                if (NdotL > 0.0f)
                {
                    float Vis = 1.0f;
                    if (OptionalClipmap && !OptionalClipmap->Empty())
                    {
                        const std::array<float, 3> Start = {
                            P.CenterMeters[0] + P.NormalUnit[0] * 0.25f,
                            P.CenterMeters[1] + P.NormalUnit[1] * 0.25f,
                            P.CenterMeters[2] + P.NormalUnit[2] * 0.25f,
                        };
                        Vis = OptionalClipmap->TraceRay(Start, L.PositionOrDir, 45.0f, 24u).Hit ? 0.08f : 1.0f;
                    }
                    for (int C = 0; C < 3; ++C)
                    {
                        Irr[C] += L.RadianceOrFlux[C] * NdotL * Vis;
                    }
                }
            }
            else
            {
                const float Dx    = L.PositionOrDir[0] - P.CenterMeters[0];
                const float Dy    = L.PositionOrDir[1] - P.CenterMeters[1];
                const float Dz    = L.PositionOrDir[2] - P.CenterMeters[2];
                const float Dist2 = Dx * Dx + Dy * Dy + Dz * Dz + 0.25f;
                const float Dist  = std::sqrt(Dist2);
                const float NdotL = std::max(0.0f, (P.NormalUnit[0] * Dx + P.NormalUnit[1] * Dy + P.NormalUnit[2] * Dz) / Dist);
                if (NdotL > 0.0f && Dist < L.RangeOrArea)
                {
                    const float Atten = NdotL / Dist2;
                    for (int C = 0; C < 3; ++C)
                    {
                        Irr[C] += L.RadianceOrFlux[C] * Atten;
                    }
                }
            }
        }

        for (int C = 0; C < 3; ++C)
        {
            DirectExitance[I][C] = P.SelfRadiance[C] + (P.DiffuseReflectance[C] / PiConstant) * Irr[C];
            CurrentB[I][C]       = DirectExitance[I][C];
        }
    }

    // Step 2: Iterative multi-bounce Jacobi form-factor solver B^(k+1) = R_0 + Gain * rho * sum_j F_ij * B^(k)_j
    const uint32_t ClampedBounces = std::min(BounceCount, 8u);
    const float    ClampedGain    = std::clamp(IndirectGain, 0.0f, 1.8f);

    for (uint32_t Bounce = 0u; Bounce < ClampedBounces; ++Bounce)
    {
        float MaxDelta = 0.0f;
        for (size_t I = 0u; I < N; ++I)
        {
            const SurfacePatchEntry& Pi      = Patches_[I];
            std::array<float, 3>     Gather  = {0.0f, 0.0f, 0.0f};
            for (size_t J = 0u; J < N; ++J)
            {
                if (I == J)
                {
                    continue;
                }
                const SurfacePatchEntry& Pj = Patches_[J];
                const float              Dx = Pj.CenterMeters[0] - Pi.CenterMeters[0];
                const float              Dy = Pj.CenterMeters[1] - Pi.CenterMeters[1];
                const float              Dz = Pj.CenterMeters[2] - Pi.CenterMeters[2];
                const float              R2 = Dx * Dx + Dy * Dy + Dz * Dz;
                if (R2 < 0.04f || R2 > 2500.0f)
                {
                    continue;
                }
                const float R     = std::sqrt(R2);
                const float InvR  = 1.0f / R;
                const float CosI  = (Pi.NormalUnit[0] * Dx + Pi.NormalUnit[1] * Dy + Pi.NormalUnit[2] * Dz) * InvR;
                const float CosJ  = -(Pj.NormalUnit[0] * Dx + Pj.NormalUnit[1] * Dy + Pj.NormalUnit[2] * Dz) * InvR;
                if (CosI <= 0.0f || CosJ <= 0.0f)
                {
                    continue;
                }
                // Bounded disk-to-differential-area form factor F_ij <= 0.35 prevents close-patch singularity
                const float FormFactor = std::min(0.35f, (CosI * CosJ * Pj.AreaSquareMeters) / (PiConstant * R2 + Pj.AreaSquareMeters));
                for (int C = 0; C < 3; ++C)
                {
                    Gather[C] += FormFactor * CurrentB[J][C];
                }
            }

            for (int C = 0; C < 3; ++C)
            {
                NextB[I][C] = DirectExitance[I][C] + ClampedGain * Pi.DiffuseReflectance[C] * Gather[C];
                MaxDelta    = std::max(MaxDelta, std::abs(NextB[I][C] - CurrentB[I][C]));
            }
        }
        CurrentB = NextB;
        if (Bounce == 0u)
        {
            Metrics.InitialResidual = MaxDelta;
        }
        Metrics.FinalResidual   = MaxDelta;
        Metrics.ExecutedBounces = Bounce + 1u;
    }

    float LumaSum = 0.0f;
    for (size_t I = 0u; I < N; ++I)
    {
        Patches_[I].SolvedRadiosity = CurrentB[I];
        LumaSum += LuminanceRgb(CurrentB[I]);
    }
    Metrics.MeanRadiosityLuma = LumaSum / static_cast<float>(N);
    return Metrics;
}

std::array<float, 3> SurfaceRadiosityAtlas::SampleRadiosity(const std::array<float, 3>& WorldMeters,
                                                            const std::array<float, 3>& NormalUnit) const noexcept
{
    if (Patches_.empty())
    {
        return {0.0f, 0.0f, 0.0f};
    }

    std::array<float, 3> Accum  = {0.0f, 0.0f, 0.0f};
    float                Weight = 0.0f;

    for (const SurfacePatchEntry& P : Patches_)
    {
        const float Align = std::max(0.0f,
                                     P.NormalUnit[0] * NormalUnit[0] + P.NormalUnit[1] * NormalUnit[1] +
                                         P.NormalUnit[2] * NormalUnit[2]);
        if (Align <= 0.05f)
        {
            continue;
        }
        const float Dx    = WorldMeters[0] - P.CenterMeters[0];
        const float Dy    = WorldMeters[1] - P.CenterMeters[1];
        const float Dz    = WorldMeters[2] - P.CenterMeters[2];
        const float Dist2 = Dx * Dx + Dy * Dy + Dz * Dz;
        const float Sigma = std::max(0.75f, P.ExtentRadiusMeters * 1.8f);
        const float W     = (Align * Align) / (1.0f + Dist2 / (Sigma * Sigma));
        for (int C = 0; C < 3; ++C)
        {
            Accum[C] += W * P.SolvedRadiosity[C];
        }
        Weight += W;
    }

    if (Weight <= 1.0e-6f)
    {
        return {0.0f, 0.0f, 0.0f};
    }
    return {Accum[0] / Weight, Accum[1] / Weight, Accum[2] / Weight};
}

void SurfaceRadiosityAtlas::PackForGpu(std::vector<std::array<float, 4>>& OutPatchGeometryFloat4,
                                       std::vector<std::array<float, 4>>& OutPatchRadiosityFloat4) const
{
    OutPatchGeometryFloat4.clear();
    OutPatchRadiosityFloat4.clear();

    if (Patches_.empty())
    {
        OutPatchGeometryFloat4.resize(4u, {0.0f, 0.0f, 0.0f, 0.0f});
        OutPatchRadiosityFloat4.resize(1u, {0.0f, 0.0f, 0.0f, 0.0f});
        return;
    }

    OutPatchGeometryFloat4.reserve(Patches_.size() * 4u);
    OutPatchRadiosityFloat4.reserve(Patches_.size());

    for (const SurfacePatchEntry& P : Patches_)
    {
        OutPatchGeometryFloat4.push_back({P.CenterMeters[0], P.CenterMeters[1], P.CenterMeters[2], P.AreaSquareMeters});
        OutPatchGeometryFloat4.push_back({P.NormalUnit[0], P.NormalUnit[1], P.NormalUnit[2], P.ExtentRadiusMeters});
        OutPatchGeometryFloat4.push_back({P.DiffuseReflectance[0], P.DiffuseReflectance[1], P.DiffuseReflectance[2], 1.0f});
        OutPatchGeometryFloat4.push_back({P.SelfRadiance[0], P.SelfRadiance[1], P.SelfRadiance[2], 0.0f});
        OutPatchRadiosityFloat4.push_back({P.SolvedRadiosity[0], P.SolvedRadiosity[1], P.SolvedRadiosity[2], 1.0f});
    }
}

} // namespace Frontier
