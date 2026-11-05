#pragma once
/*==========================================================================================================================================
                                                            SCANSPARKLES.H
==========================================================================================================================================*/
// 🧩 Deterministic 130-particle holographic sparkle cloud (#8fd8f2) in [7.5, 4.2, 3.2] m around [0, 0, 0.82] m.
//    Matches Three.js <Sparkles count={130} scale={[7.5, 4.2, 3.2]} size={1.6} speed={0.28} color="#8fd8f2" />.

#include "Frontier/Engine/Foundation/DeterministicRandom.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Frontier
{

struct ScanSparkleSeed
{
    std::array<float, 3> BaseOffset = { 0.0f, 0.0f, 0.82f };  // vehicle-local metres
    float Phase = 0.0f;
    float Speed = 0.28f;
    float Scale = 1.0f;
};

struct EvaluatedSparkle
{
    std::array<float, 3> LocalPosition = { 0.0f, 0.0f, 0.82f };
    float RadiusMetres = 0.018f;
    float Brightness = 1.0f;
};

inline constexpr uint32_t DefaultScanSparkleCount = 130u;
inline constexpr std::array<float, 3> ScanSparkleLinearColour = { 0.279f, 0.687f, 0.888f }; // #8fd8f2 in linear RGB

[[nodiscard]] inline std::vector<ScanSparkleSeed> BuildScanSparkleCloud(uint32_t Count = DefaultScanSparkleCount,
                                                                        uint64_t Seed = 0x5343414E53504B4Cull)
{
    DeterministicRandom Generator(Seed, 0xda3e39cb94b95bdbull);
    std::vector<ScanSparkleSeed> Cloud;
    Cloud.reserve(Count);
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        const float OffsetX = float(Generator.NextUnit());
        const float OffsetY = float(Generator.NextUnit());
        const float OffsetZ = float(Generator.NextUnit());
        const float UnitPhase = float(Generator.NextUnit());
        const float UnitSpeed = float(Generator.NextUnit());
        const float UnitScale = float(Generator.NextUnit());
        ScanSparkleSeed Item;
        Item.BaseOffset = {
            (OffsetX - 0.5f) * 7.5f,
            (OffsetY - 0.5f) * 4.2f,
            0.82f + (OffsetZ - 0.5f) * 3.2f
        };
        Item.Phase = UnitPhase * 6.28318530718f;
        Item.Speed = 0.28f * (0.65f + 0.70f * UnitSpeed);
        Item.Scale = 0.55f + 0.90f * UnitScale;
        Cloud.push_back(Item);
    }
    return Cloud;
}

[[nodiscard]] inline EvaluatedSparkle EvaluateScanSparkle(const ScanSparkleSeed& Sparkle, float ElapsedSeconds) noexcept
{
    const float T = ElapsedSeconds * Sparkle.Speed + Sparkle.Phase;
    EvaluatedSparkle Out;
    Out.LocalPosition = {
        Sparkle.BaseOffset[0] + std::sin(T * 1.7f) * 0.08f,
        Sparkle.BaseOffset[1] + std::cos(T * 1.3f + 1.1f) * 0.08f,
        Sparkle.BaseOffset[2] + std::sin(T * 2.1f + 2.4f) * 0.07f
    };
    const float Twinkle = 0.45f + 0.55f * (0.5f + 0.5f * std::sin(T * 5.2f));
    Out.RadiusMetres = 0.016f * Sparkle.Scale;
    Out.Brightness = Twinkle;
    return Out;
}

} // namespace Frontier
