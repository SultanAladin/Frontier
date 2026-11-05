#pragma once
/*==========================================================================================================================================
                                                            SCANSPARKLES.H
==========================================================================================================================================*/
// 🧩 Drei Sparkles attributes for the configurator scan, mapped to the engine's Z-up coordinates.
//    Seeded randomness makes a scan activation replayable; a new activation should use a new seed.

#include "Frontier/Engine/Foundation/DeterministicRandom.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Frontier
{

// Eight floats, matching SparkleSeedRecord in VehicleScan.slang and the prototype GPU record.
struct alignas(16) ScanSparkleSeed
{
    std::array<float, 3> BaseOffset = { 0.0f, 0.0f, 0.83f }; // body-relative metres
    float Size = 2.2f;
    std::array<float, 3> Noise = { 1.0f, 1.0f, 1.0f };
    float Speed = 0.35f;
};
static_assert(sizeof(ScanSparkleSeed) == 32);

inline constexpr uint32_t DefaultScanSparkleCount = 130u;
inline constexpr float DefaultScanSparkleOpacity = 0.55f;
inline constexpr std::array<float, 3> ScanSparkleLinearColour = { 0.274677f, 0.686685f, 0.887923f }; // #8fd8f2, linear sRGB

[[nodiscard]] inline std::vector<ScanSparkleSeed> BuildScanSparkleCloud(uint32_t Count = DefaultScanSparkleCount,
                                                                        uint64_t Seed = 0x5343414E53504B4Cull)
{
    DeterministicRandom Generator(Seed, 0xda3e39cb94b95bdbull);
    std::vector<ScanSparkleSeed> Cloud;
    Cloud.reserve(Count);
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        // Drei consumes X, Y-up, Z in that order; engine (x,y,z) = reference (x,-z,y).
        const double X = Generator.NextUnit();
        const double Height = Generator.NextUnit();
        const double Side = Generator.NextUnit();
        ScanSparkleSeed Item;
        Item.BaseOffset = { static_cast<float>((0.5 - X) * 7.5),
                            static_cast<float>(-(0.5 - Side) * 4.2),
                            static_cast<float>(0.83 + (0.5 - Height) * 3.2) };
        Cloud.push_back(Item);
    }
    return Cloud;
}

// BaseWorld is the transformed base position. Drei's noise offsets are applied after the model
// transform and use its original world X; applying them locally would rotate the drift with the car.
[[nodiscard]] inline std::array<float, 3> EvaluateScanSparkleWorld(const ScanSparkleSeed& Sparkle,
                                                                  const std::array<float, 3>& BaseWorld,
                                                                  float ElapsedSeconds) noexcept
{
    const float T = ElapsedSeconds * Sparkle.Speed;
    const float X = BaseWorld[0];
    return { BaseWorld[0] + std::cos(T + X * Sparkle.Noise[2] * 100.0f) * 0.2f,
             BaseWorld[1] - std::cos(T + X * Sparkle.Noise[1] * 100.0f) * 0.2f,
             BaseWorld[2] + std::sin(T + X * Sparkle.Noise[0] * 100.0f) * 0.2f };
}

[[nodiscard]] inline float ResolveScanSparklePixelDiameter(float Size, float PixelRatio, float ViewDepth) noexcept
{
    // WebGL has a one-pixel lower bound; its implementation-specific upper limit is unknown here.
    return std::max(1.0f, Size * 25.0f * PixelRatio / std::max(ViewDepth, 1.0e-6f));
}

[[nodiscard]] inline float ResolveScanSparkleAlpha(float CornerX, float CornerY, float Opacity) noexcept
{
    // Corner spans [-1,1]; gl_PointCoord spans [0,1].
    const float Distance = std::sqrt(CornerX * CornerX + CornerY * CornerY) * 0.5f;
    return std::clamp((0.05f / std::max(Distance, 1.0e-6f) - 0.1f) * Opacity, 0.0f, 1.0f);
}

} // namespace Frontier
