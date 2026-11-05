#pragma once
/*==========================================================================================================================================
                                                         MATERIALTRANSITION.H
==========================================================================================================================================*/
// 🧩 1:1 3D radial material ripple transition and 8-swatch configurator paint library.
//    Matches interactive-vehicle-hmi-configurator's cubic-out 1.35 s, 6.4 m sphere expansion from the
//    3D raycast drop point on the vehicle body (or front nose fallback on click).

#include "Frontier/Engine/Scene/FacetArchive.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace Frontier
{

struct PaintFinishOption
{
    std::string_view Identifier;
    std::string_view DisplayName;
    uint32_t         HexSrgb = 0xc2c8d0u;
    std::array<float, 3> LinearAlbedo = { 0.539f, 0.578f, 0.631f };
    float            Roughness = 0.26f;
    float            Metalness = 0.82f;
    float            Clearcoat = 1.0f;
};

[[nodiscard]] constexpr float SrgbByteToLinear(uint8_t Byte) noexcept
{
    const float C = static_cast<float>(Byte) / 255.0f;
    if (C <= 0.04045f)
        return C / 12.92f;
    // Accurate piecewise rational/polynomial approximation usable in constexpr if needed; std::pow at runtime.
    const float Base = (C + 0.055f) / 1.055f;
    return Base * Base * (0.75f + 0.25f * Base);
}

inline constexpr std::array<PaintFinishOption, 8> ConfiguratorPaintFinishes = {{
    { "liquid-silver",   "Liquid Silver",   0xc2c8d0u, { 0.539f, 0.578f, 0.631f }, 0.24f, 0.86f, 1.0f },
    { "obsidian-black",  "Obsidian Black",  0x17191du, { 0.009f, 0.010f, 0.013f }, 0.20f, 0.78f, 1.0f },
    { "crimson-red",     "Crimson Red",     0x8a1a1du, { 0.254f, 0.010f, 0.013f }, 0.25f, 0.72f, 1.0f },
    { "atlantic-blue",   "Atlantic Blue",   0x1c3f73u, { 0.012f, 0.049f, 0.171f }, 0.25f, 0.76f, 1.0f },
    { "glacier-pearl",   "Glacier Pearl",   0xe7ebefu, { 0.799f, 0.831f, 0.863f }, 0.22f, 0.64f, 1.0f },
    { "desert-sand",     "Desert Sand",     0xb2946au, { 0.445f, 0.296f, 0.144f }, 0.58f, 0.12f, 0.18f },
    { "canyon-bronze",   "Canyon Bronze",   0x8c5a2eu, { 0.262f, 0.102f, 0.027f }, 0.28f, 0.84f, 1.0f },
    { "pine-matte",      "Pine Matte",      0x33442fu, { 0.033f, 0.058f, 0.028f }, 0.62f, 0.10f, 0.14f },
}};

struct MaterialTransition
{
    static constexpr float DurationSeconds = 1.35f;
    static constexpr float MaxRadiusMetres = 6.4f;
    static constexpr float FeatherWidthMetres = 0.55f;
    static constexpr std::array<float, 3> DefaultNoseOrigin = { 2.75f, 0.0f, 0.72f };

    uint32_t CurrentFinishIndex = 0;
    uint32_t PreviousFinishIndex = 0;
    std::array<float, 3> CurrentPaint = ConfiguratorPaintFinishes[0].LinearAlbedo;
    std::array<float, 3> PreviousPaint = ConfiguratorPaintFinishes[0].LinearAlbedo;
    std::array<float, 3> OriginBodyLocal = DefaultNoseOrigin;
    float ElapsedSeconds = DurationSeconds;
    bool  Active = false;

    void TriggerFinish(uint32_t FinishIndex, const std::array<float, 3>* DropBodyLocal = nullptr) noexcept
    {
        const uint32_t Clamped = FinishIndex % static_cast<uint32_t>(ConfiguratorPaintFinishes.size());
        if (Clamped == CurrentFinishIndex && !Active)
            return;

        PreviousFinishIndex = CurrentFinishIndex;
        PreviousPaint = Active ? BlendAtOrigin() : CurrentPaint;
        CurrentFinishIndex = Clamped;
        CurrentPaint = ConfiguratorPaintFinishes[Clamped].LinearAlbedo;
        OriginBodyLocal = DropBodyLocal ? *DropBodyLocal : DefaultNoseOrigin;
        ElapsedSeconds = 0.0f;
        Active = true;
    }

    void TriggerCustomPaint(const std::array<float, 3>& NextLinearAlbedo,
                            const std::array<float, 3>* DropBodyLocal = nullptr) noexcept
    {
        PreviousPaint = Active ? BlendAtOrigin() : CurrentPaint;
        CurrentPaint = NextLinearAlbedo;
        OriginBodyLocal = DropBodyLocal ? *DropBodyLocal : DefaultNoseOrigin;
        ElapsedSeconds = 0.0f;
        Active = true;
    }

    void Advance(float DeltaSeconds) noexcept
    {
        if (!Active)
            return;
        const float Step = std::clamp(DeltaSeconds, 0.0f, 0.10f);
        ElapsedSeconds += Step;
        if (ElapsedSeconds >= DurationSeconds)
        {
            ElapsedSeconds = DurationSeconds;
            PreviousPaint = CurrentPaint;
            PreviousFinishIndex = CurrentFinishIndex;
            Active = false;
        }
    }

    // 📝 Cubic-out sphere radius: uRipR = (1 - (1 - min(t / 1.35, 1))^3) * 6.4.
    [[nodiscard]] float ResolveRadiusMetres() const noexcept
    {
        if (!Active)
            return MaxRadiusMetres;
        const float Normalized = std::clamp(ElapsedSeconds / DurationSeconds, 0.0f, 1.0f);
        const float Inv = 1.0f - Normalized;
        return (1.0f - Inv * Inv * Inv) * MaxRadiusMetres;
    }

    // 📝 Smoothstep feather mask: ripM = (1 - smoothstep(uRipR - 0.55, uRipR, dist)) * uRipActive.
    [[nodiscard]] float ResolveMask(const std::array<float, 3>& BodyLocalPosition) const noexcept
    {
        if (!Active)
            return 0.0f;
        const float Dx = BodyLocalPosition[0] - OriginBodyLocal[0];
        const float Dy = BodyLocalPosition[1] - OriginBodyLocal[1];
        const float Dz = BodyLocalPosition[2] - OriginBodyLocal[2];
        const float Distance = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        const float Radius = ResolveRadiusMetres();
        const float Edge0 = Radius - FeatherWidthMetres;
        const float Edge1 = Radius;
        const float T = std::clamp((Distance - Edge0) / std::max(Edge1 - Edge0, 1e-5f), 0.0f, 1.0f);
        const float Smooth = T * T * (3.0f - 2.0f * T);
        return 1.0f - Smooth;
    }

    // 📝 Inside the expanding sphere = CurrentPaint; outside = PreviousPaint; ring glow along wavefront.
    [[nodiscard]] std::array<float, 3> ResolveAlbedo(const std::array<float, 3>& BodyLocalPosition,
                                                     float* OutWaveGlow = nullptr) const noexcept
    {
        if (!Active)
        {
            if (OutWaveGlow)
                *OutWaveGlow = 0.0f;
            return CurrentPaint;
        }
        const float Mask = ResolveMask(BodyLocalPosition);
        if (OutWaveGlow)
            *OutWaveGlow = 4.0f * Mask * (1.0f - Mask);
        return {
            PreviousPaint[0] + (CurrentPaint[0] - PreviousPaint[0]) * Mask,
            PreviousPaint[1] + (CurrentPaint[1] - PreviousPaint[1]) * Mask,
            PreviousPaint[2] + (CurrentPaint[2] - PreviousPaint[2]) * Mask,
        };
    }

private:
    [[nodiscard]] std::array<float, 3> BlendAtOrigin() const noexcept
    {
        return ResolveAlbedo(OriginBodyLocal, nullptr);
    }
};

struct BodyRaycastHit
{
    bool                  Hit = false;
    float                 Distance = 0.0f;
    uint32_t              FacetIndex = 0;
    uint32_t              MaterialFamily = 0;
    std::array<float, 3>  BodyPoint = { 0.0f, 0.0f, 0.0f };
};

// 📝 Exact Möller–Trumbore raycast in vehicle body-local coordinates (with optional Z rebase matching body Com).
[[nodiscard]] inline BodyRaycastHit PickVehicleBodyPoint(const FacetArchiveData& BodyArchive,
                                                         const std::array<float, 3>& RayOriginBodyLocal,
                                                         const std::array<float, 3>& RayDirectionBodyLocal,
                                                         float ZRebase = 0.11285f) noexcept
{
    BodyRaycastHit Result;
    if (!BodyArchive.IsValid())
        return Result;

    float BestT = 1e30f;
    for (uint32_t Facet = 0; Facet < BodyArchive.FacetCount; ++Facet)
    {
        const uint32_t I0 = BodyArchive.Facets[Facet * 3u + 0u];
        const uint32_t I1 = BodyArchive.Facets[Facet * 3u + 1u];
        const uint32_t I2 = BodyArchive.Facets[Facet * 3u + 2u];

        const std::array<float, 3> V0 = {
            BodyArchive.Positions[I0 * 3u + 0u],
            BodyArchive.Positions[I0 * 3u + 1u],
            BodyArchive.Positions[I0 * 3u + 2u] - ZRebase
        };
        const std::array<float, 3> V1 = {
            BodyArchive.Positions[I1 * 3u + 0u],
            BodyArchive.Positions[I1 * 3u + 1u],
            BodyArchive.Positions[I1 * 3u + 2u] - ZRebase
        };
        const std::array<float, 3> V2 = {
            BodyArchive.Positions[I2 * 3u + 0u],
            BodyArchive.Positions[I2 * 3u + 1u],
            BodyArchive.Positions[I2 * 3u + 2u] - ZRebase
        };

        const std::array<float, 3> E1 = { V1[0] - V0[0], V1[1] - V0[1], V1[2] - V0[2] };
        const std::array<float, 3> E2 = { V2[0] - V0[0], V2[1] - V0[1], V2[2] - V0[2] };
        const std::array<float, 3> P = {
            RayDirectionBodyLocal[1] * E2[2] - RayDirectionBodyLocal[2] * E2[1],
            RayDirectionBodyLocal[2] * E2[0] - RayDirectionBodyLocal[0] * E2[2],
            RayDirectionBodyLocal[0] * E2[1] - RayDirectionBodyLocal[1] * E2[0]
        };
        const float Det = E1[0] * P[0] + E1[1] * P[1] + E1[2] * P[2];
        if (std::fabs(Det) < 1e-8f)
            continue;
        const float InvDet = 1.0f / Det;
        const std::array<float, 3> Tvec = {
            RayOriginBodyLocal[0] - V0[0],
            RayOriginBodyLocal[1] - V0[1],
            RayOriginBodyLocal[2] - V0[2]
        };
        const float U = (Tvec[0] * P[0] + Tvec[1] * P[1] + Tvec[2] * P[2]) * InvDet;
        if (U < 0.0f || U > 1.0f)
            continue;
        const std::array<float, 3> Q = {
            Tvec[1] * E1[2] - Tvec[2] * E1[1],
            Tvec[2] * E1[0] - Tvec[0] * E1[2],
            Tvec[0] * E1[1] - Tvec[1] * E1[0]
        };
        const float V = (RayDirectionBodyLocal[0] * Q[0] + RayDirectionBodyLocal[1] * Q[1] + RayDirectionBodyLocal[2] * Q[2]) * InvDet;
        if (V < 0.0f || U + V > 1.0f)
            continue;
        const float HitT = (E2[0] * Q[0] + E2[1] * Q[1] + E2[2] * Q[2]) * InvDet;
        if (HitT > 1e-4f && HitT < BestT)
        {
            BestT = HitT;
            Result.Hit = true;
            Result.Distance = HitT;
            Result.FacetIndex = Facet;
            Result.MaterialFamily = Facet < BodyArchive.Families.size() ? BodyArchive.Families[Facet] : 0u;
            Result.BodyPoint = {
                RayOriginBodyLocal[0] + RayDirectionBodyLocal[0] * HitT,
                RayOriginBodyLocal[1] + RayDirectionBodyLocal[1] * HitT,
                RayOriginBodyLocal[2] + RayDirectionBodyLocal[2] * HitT
            };
        }
    }
    return Result;
}

} // namespace Frontier
