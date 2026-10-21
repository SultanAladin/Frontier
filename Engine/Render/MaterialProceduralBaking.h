/*============================================================================================================================================
                                                          MATERIALPROCEDURALBAKING.H
============================================================================================================================================*/
// 🧩 CPU port of the Alloy procedural kernels (alloy-procedural-v7): seeded value noise and the tapered scratch
// field bake into shared height/coverage maps, so every channel samples one consistent procedural definition.

#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{
// 📝 Bit-faithful intent, float-precision reality: these follow the GLSL line for line; GPU/CPU rounding may
// differ in the last ulp, so parity checks use tolerance, never exact equality.
[[nodiscard]] float ProceduralHash31(float X, float Y, float Z);
[[nodiscard]] float ProceduralNoise3(float X, float Y, float Z);
[[nodiscard]] float ProceduralStoneFbm(float X, float Y, float Z);

struct ScratchFieldParameters
{
    float Density = 0, Length = 0, Width = 0, Depth = 0, Spread = 0, Bend = 0, Scale = 1, AngleRadians = 0;
    uint32_t Seed = 0;
};

struct ScratchFieldSample
{
    float Coverage = 0, Height = 0, Rim = 0;
};

// 📝 The fragment shader reads screen-space derivatives for anti-aliasing; the baker substitutes the known
// texel step in scaled UV (Scale / Extent), which is what fwidth converges to on a flat preview card.
[[nodiscard]] ScratchFieldSample EvaluateScratchField(float U, float V, const ScratchFieldParameters &Parameters, float PixelUv);

struct ScratchBakeMaps
{
    std::vector<float> Height, Coverage;
    uint32_t Extent = 0;
};

// 📝 Row-major maps, V running down the rows like image data; Height holds signed groove depth in world units,
// Coverage the 0..1 cut mask shared by the roughness and occlusion channels.
[[nodiscard]] ScratchBakeMaps BakeScratchMaps(const ScratchFieldParameters &Parameters, uint32_t Extent);

// 📝 Tangent-space normal from relief (groove floor near -1): normalize(-dh/du, -dh/dv, 1) with clamped
// borders, matching the shipped detail-texture convention; the scratch bake is not periodic.
[[nodiscard]] std::vector<uint8_t> ConvertHeightToNormalRgba(const std::vector<float> &Relief, uint32_t Extent, float Strength);
// 📝 ORM multipliers over the authored scalars: occlusion darkens the cuts, green divides by the shader's +0.3
// ceiling so the authored scalar must equal min(1, BaseRoughness + .3), blue preserves full metalness.
[[nodiscard]] std::vector<uint8_t> ConvertScratchOrmRgba(const std::vector<float> &Coverage, uint32_t Extent, float BaseRoughness);

// 📝 Op registry: each procedural op declares the channels it can bake and the slider ranges its Alloy-keyed
// params expose. Params keep their Alloy uniform spellings so recipe controls resolve without translation.
struct ProceduralOpParam
{
    const char *Key = "";
    const char *Label = "";
    float Minimum = 0, Maximum = 1, Default = 0;
    bool Logarithmic = false;
    const char *Unit = "";
    float Step = .01f;
};
[[nodiscard]] std::vector<std::string> QueryProceduralOps();
[[nodiscard]] std::vector<std::string> QueryProceduralOpChannels(std::string_view Op);
[[nodiscard]] std::vector<ProceduralOpParam> QueryProceduralOpParams(std::string_view Op);
// 📝 Canonical pool spelling for a recipe target key; direction aliases collapse onto one stored angle.
[[nodiscard]] std::string ResolveProceduralParamKey(std::string_view Op, std::string_view Key);
// 📝 Maximum roughness lift an op can bake above its base; the material's roughness scalar carries
// base+lift as the ORM ceiling so green never clips. Ops without an ORM channel report zero.
[[nodiscard]] float QueryProceduralOpCeilingLift(std::string_view Op);
// 📝 Channel dispatcher: bake one channel's RGBA from a channel's op + params; the ORM base comes from the
// pool's roughness, and relief normalization plus strength match the offline pilot bake.
[[nodiscard]] std::vector<uint8_t> BakeProceduralChannelRgba(std::string_view Op, std::string_view Channel,
    const std::map<std::string, double> &Params, uint32_t Extent);
// 📝 Deterministic PNG encoding shared by the offline baker, the save path and the regeneration tests.
[[nodiscard]] std::vector<uint8_t> EncodeProceduralPng(const std::vector<uint8_t> &Rgba, uint32_t Extent);
}
