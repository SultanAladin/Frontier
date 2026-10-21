/*============================================================================================================================================
                                                          MATERIALPROCEDURALBAKING.CPP
============================================================================================================================================*/
// 🧩 Exact CPU evaluation of the Alloy noise and scratch kernels; shared maps keep every baked channel consistent.

#include "Engine/Render/MaterialProceduralBaking.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Frontier
{
namespace
{
float ProceduralSmoothstep(float Edge0, float Edge1, float X)
{
    const float Span = Edge1 - Edge0;
    const float T = Span != 0 ? std::clamp((X - Edge0) / Span, 0.f, 1.f) : (X < Edge0 ? 0.f : 1.f);
    return T * T * (3 - 2 * T);
}

float ProceduralMix(float A, float B, float T) { return A + (B - A) * T; }
}

float ProceduralHash31(float X, float Y, float Z)
{
    // hash31: p=fract(p*.1031); p+=dot(p,p.yzx+33.33); fract((p.x+p.y)*p.z).
    X = X * .1031f; Y = Y * .1031f; Z = Z * .1031f;
    X -= std::floor(X); Y -= std::floor(Y); Z -= std::floor(Z);
    const float Dot = X * (Y + 33.33f) + Y * (Z + 33.33f) + Z * (X + 33.33f);
    X += Dot; Y += Dot; Z += Dot;
    const float Hash = (X + Y) * Z;
    return Hash - std::floor(Hash);
}

float ProceduralNoise3(float X, float Y, float Z)
{
    const float Ix = std::floor(X), Iy = std::floor(Y), Iz = std::floor(Z);
    float Fx = X - Ix, Fy = Y - Iy, Fz = Z - Iz;
    Fx = Fx * Fx * (3 - 2 * Fx); Fy = Fy * Fy * (3 - 2 * Fy); Fz = Fz * Fz * (3 - 2 * Fz);
    const float X00 = ProceduralMix(ProceduralHash31(Ix, Iy, Iz), ProceduralHash31(Ix + 1, Iy, Iz), Fx);
    const float X10 = ProceduralMix(ProceduralHash31(Ix, Iy + 1, Iz), ProceduralHash31(Ix + 1, Iy + 1, Iz), Fx);
    const float X01 = ProceduralMix(ProceduralHash31(Ix, Iy, Iz + 1), ProceduralHash31(Ix + 1, Iy, Iz + 1), Fx);
    const float X11 = ProceduralMix(ProceduralHash31(Ix, Iy + 1, Iz + 1), ProceduralHash31(Ix + 1, Iy + 1, Iz + 1), Fx);
    return ProceduralMix(ProceduralMix(X00, X10, Fy), ProceduralMix(X01, X11, Fy), Fz);
}

float ProceduralStoneFbm(float X, float Y, float Z)
{
    return .5333f * ProceduralNoise3(X, Y, Z) + .2667f * ProceduralNoise3(X * 2.03f + 7.1f, Y * 2.03f + 7.1f, Z * 2.03f + 7.1f) +
        .1333f * ProceduralNoise3(X * 4.17f + 19.3f, Y * 4.17f + 19.3f, Z * 4.17f + 19.3f) +
        .0667f * ProceduralNoise3(X * 8.31f + 41.f, Y * 8.31f + 41.f, Z * 8.31f + 41.f);
}

ScratchFieldSample EvaluateScratchField(float U, float V, const ScratchFieldParameters &Parameters, float PixelUv)
{
    ScratchFieldSample Sample;
    if (!(Parameters.Density > 0)) return Sample;
    constexpr float Pi = 3.14159265f;
    U *= Parameters.Scale; V *= Parameters.Scale;
    const float CellX = std::floor(U), CellY = std::floor(V);
    const float AntiAlias = std::max(PixelUv * .4f, .00015f);
    float Coverage = 0, Height = 0, RimTotal = 0;
    for (int Layer = 0; Layer < 12; ++Layer)
    {
        const float Batch = float(Layer / 4) * 4;
        if (Batch >= Parameters.Density) break;
        const float LayerSeed = float(Layer) * 113.7f + float(Parameters.Seed);
        const float Occupancy = std::clamp((Parameters.Density - Batch) * .25f, 0.f, 1.f);
        for (int Y = -2; Y <= 2; ++Y)
            for (int X = -2; X <= 2; ++X)
            {
                const float IdX = CellX + float(X), IdY = CellY + float(Y);
                if (!(ProceduralHash31(IdX, IdY, LayerSeed) < Occupancy)) continue;
                const float A = ProceduralHash31(IdX + 17.3f, IdY + 17.3f, LayerSeed + 3);
                const float B = ProceduralHash31(IdX + 39.7f, IdY + 39.7f, LayerSeed + 11);
                const float CenterX = IdX + .08f + .84f * A, CenterY = IdY + .08f + .84f * B;
                const float Angle = Parameters.AngleRadians +
                    (ProceduralHash31(IdX + 81.2f, IdY + 81.2f, LayerSeed) - .5f) * Pi * Parameters.Spread;
                const float DirectionX = std::cos(Angle), DirectionY = std::sin(Angle);
                const float DeltaX = U - CenterX, DeltaY = V - CenterY;
                const float Length = Parameters.Length * ProceduralMix(.18f, 1, A * A);
                const float T = (DeltaX * DirectionX + DeltaY * DirectionY) / std::max(Length, .001f) + .5f;
                const float Along = std::clamp(T, 0.f, 1.f);
                const float Bend = Parameters.Bend * (B - .5f) * 2 * Length / std::max(Parameters.Length, .001f);
                const float Bow = (std::sin(Along * Pi) + .2f * std::sin(Along * 2 * Pi + A * 2) * std::sin(Along * Pi)) * Bend;
                const float Distance = std::abs(-DeltaX * DirectionY + DeltaY * DirectionX - Bow);
                const float Taper = ProceduralSmoothstep(0, .12f, T) * (1 - ProceduralSmoothstep(.84f, 1, T));
                const float Width = Parameters.Width * ProceduralMix(.35f, 1.4f, ProceduralHash31(IdX - 31.1f, IdY - 31.1f, LayerSeed)) *
                    (.2f + .8f * std::sqrt(std::max(Taper, 0.f)));
                const float Depth = Parameters.Depth * ProceduralMix(.35f, 1, B);
                const float Cut = (1 - ProceduralSmoothstep(Width - AntiAlias, Width + AntiAlias, Distance)) * Taper *
                    std::min(1.f, Width / AntiAlias);
                float Bowl = std::pow(std::max(0.f, 1 - Distance / std::max(Width, .0001f)), 1.3f) * Taper;
                Bowl = ProceduralMix(Bowl, Cut * .5f, ProceduralSmoothstep(Width * .4f, Width * 1.5f, AntiAlias));
                const float LipArgument = (Distance - Width * 1.65f) / std::max(Width * .45f, AntiAlias);
                const float Lip = std::exp(-LipArgument * LipArgument) * Taper * std::min(1.f, Width / AntiAlias);
                Height = std::min(Height, -Depth * Bowl);
                RimTotal = std::max(RimTotal, Lip * Depth * .16f);
                Coverage = std::max(Coverage, Cut);
            }
    }
    const float Resolved = 1 - ProceduralSmoothstep(.3f, 1.2f, PixelUv);
    Sample.Coverage = ProceduralMix(Parameters.Density * Parameters.Width * Parameters.Length * .75f, Coverage, Resolved);
    Sample.Height = (Height + RimTotal) * Resolved;
    Sample.Rim = RimTotal;
    return Sample;
}

ScratchBakeMaps BakeScratchMaps(const ScratchFieldParameters &Parameters, uint32_t Extent)
{
    if (!Extent || Extent > 2048) throw std::runtime_error("Scratch bake extent is unbounded.");
    ScratchBakeMaps Maps;
    Maps.Extent = Extent;
    Maps.Height.assign(size_t(Extent) * Extent, 0);
    Maps.Coverage.assign(size_t(Extent) * Extent, 0);
    const float PixelUv = Parameters.Scale / float(Extent);
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const auto Sample = EvaluateScratchField((float(X) + .5f) / float(Extent), (float(Y) + .5f) / float(Extent),
                Parameters, PixelUv);
            Maps.Height[size_t(Y) * Extent + X] = Sample.Height;
            Maps.Coverage[size_t(Y) * Extent + X] = Sample.Coverage;
        }
    return Maps;
}

std::vector<uint8_t> ConvertHeightToNormalRgba(const std::vector<float> &Relief, uint32_t Extent, float Strength)
{
    if (!Extent || Relief.size() != size_t(Extent) * Extent) throw std::runtime_error("Normal source is not a square relief field.");
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const uint32_t Left = X > 0 ? X - 1 : 0, Right = X + 1 < Extent ? X + 1 : Extent - 1;
            const uint32_t Up = Y > 0 ? Y - 1 : 0, Down = Y + 1 < Extent ? Y + 1 : Extent - 1;
            const float Gx = (Relief[size_t(Y) * Extent + Right] - Relief[size_t(Y) * Extent + Left]) * .5f;
            const float Gy = (Relief[size_t(Down) * Extent + X] - Relief[size_t(Up) * Extent + X]) * .5f;
            const float Nx = -Gx * Strength, Ny = -Gy * Strength;
            const float Inverse = 1 / std::sqrt(Nx * Nx + Ny * Ny + 1);
            uint8_t *Pixel = &Rgba[(size_t(Y) * Extent + X) * 4];
            Pixel[0] = uint8_t(std::lround((Nx * Inverse * .5f + .5f) * 255));
            Pixel[1] = uint8_t(std::lround((Ny * Inverse * .5f + .5f) * 255));
            Pixel[2] = uint8_t(std::lround((Inverse * .5f + .5f) * 255));
            Pixel[3] = 255;
        }
    return Rgba;
}

std::vector<uint8_t> ConvertScratchOrmRgba(const std::vector<float> &Coverage, uint32_t Extent, float BaseRoughness)
{
    if (!Extent || Coverage.size() != size_t(Extent) * Extent || !(BaseRoughness > 0) || !(BaseRoughness <= 1))
        throw std::runtime_error("ORM source is not a square coverage field over a unit base roughness.");
    // 📝 ORM green multiplies the authored scalar, so it cannot lift roughness above the base. The authored
    // scalar carries the shader's +0.3 ceiling and green divides by it: field pixels reproduce the base exactly,
    // cuts reach the ceiling exactly, matching the Alloy mix on both ends.
    const float Ceiling = std::min(1.f, BaseRoughness + .3f);
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const float Mask = std::clamp(Coverage[size_t(Y) * Extent + X], 0.f, 1.f);
            uint8_t *Pixel = &Rgba[(size_t(Y) * Extent + X) * 4];
            Pixel[0] = uint8_t(std::lround(std::clamp(1 - .11f * Mask, 0.f, 1.f) * 255));
            Pixel[1] = uint8_t(std::lround(std::clamp(ProceduralMix(BaseRoughness, Ceiling, Mask) / Ceiling, 0.f, 1.f) * 255));
            Pixel[2] = 255;
            Pixel[3] = 255;
        }
    return Rgba;
}

std::vector<std::string> QueryProceduralOps() { return {"scratches", "metalGrain", "polymerGrain"}; }

std::vector<std::string> QueryProceduralOpChannels(std::string_view Op)
{
    if (Op == "scratches") return {"Orm", "Normal"};
    // 📝 Alloy varies no roughness term for bare metal, so metalGrain offers no ORM channel; polymer grain
    // modulates roughness and earns all three.
    if (Op == "metalGrain") return {"Albedo", "Normal"};
    if (Op == "polymerGrain") return {"Albedo", "Orm", "Normal"};
    return {};
}

float QueryProceduralOpCeilingLift(std::string_view Op)
{
    if (Op == "scratches") return .3f;
    if (Op == "polymerGrain") return .096f;
    return 0;
}

std::vector<ProceduralOpParam> QueryProceduralOpParams(std::string_view Op)
{
    // 📝 Ranges mirror the Alloy recipe controls; Param keys keep their uniform spellings, except the direction
    // alias below: type-29 studies target weaveAngle while metal overlays target scratchAngle, and both drive
    // the same radian uniform, so the pool stores one canonical angle in degrees.
    if (Op == "scratches")
        return {{"scratchDensity", "Scratch density", 0, 12, 6, false, "", .1f},
            {"scratchLength", "Scratch length", .1f, 1.65f, 1.15f, false, "", .01f},
            {"scratchWidth", "Scratch width", .002f, .035f, .012f, false, "", .001f},
            {"scratchDepth", "Scratch depth", 0, .006f, .0018f, false, "", .0001f},
            {"scratchSpread", "Direction spread", 0, 1, .8f, false, "", .01f},
            {"scratchBend", "Scratch curvature", 0, .7f, .42f, false, "", .01f},
            {"scratchScale", "Field scale", .01f, 1000, 6, true, "x", .01f},
            {"angle", "Preferred direction", -180, 180, 25, false, "deg", 1},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1},
            {"roughness", "Surface roughness", .045f, 1, .23f, false, "", .01f}};
    // 📝 Grain ranges mirror the Alloy recipe controls: bare-metal tooth and polymer molding share the noise
    // kernels but keep their own scale defaults and wear terms, and only the ORM baker holds roughness.
    if (Op == "metalGrain")
        return {{"grain", "Surface tooth", 0, 1, .25f, false, "", .01f},
            {"detailScale", "Grain scale", 4, 256, 65, false, "x", 1},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "polymerGrain")
        return {{"grain", "Molding grain", 0, 1, .6f, false, "", .01f},
            {"detailScale", "Grain scale", 4, 256, 130, false, "x", 1},
            {"wear", "Polishing wear", 0, 1, 0, false, "", .01f},
            {"wearSoftness", "Wear softness", 0, 1, .65f, false, "", .01f},
            {"wornRoughness", "Worn roughness", .045f, 1, .24f, false, "", .01f},
            {"scratchScale", "Scuff scale", 1, 128, 32, false, "x", 1},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1},
            {"roughness", "Surface roughness", .045f, 1, .5f, false, "", .01f}};
    return {};
}

std::string ResolveProceduralParamKey(std::string_view Op, std::string_view Key)
{
    if (Op == "scratches" && (Key == "weaveAngle" || Key == "scratchAngle")) return "angle";
    return std::string(Key);
}

struct GrainFieldSample
{
    float Albedo = 1, Relief = 0, Roughness = 0;
};

float ProceduralSmoothMinimum(float A, float B, float K)
{
    const float H = std::clamp(.5f + .5f * (B - A) / std::max(K, .0001f), 0.f, 1.f);
    return ProceduralMix(B, A, H) - K * H * (1 - H);
}

// 📝 Grain bake mapping: Alloy samples noise3 in object space (pp·uScale); the baker substitutes tile space
// ppm=(u·tiles, seed, v·tiles) so one map carries `tiles` noise cells. Absolute-rate terms (wear patches,
// scuff marks) keep their frequency RATIOS to the micro term, preserving the shader's relationships without
// object-space units; constant-rate axes collapse onto seed slices.
GrainFieldSample EvaluateMetalGrain(float U, float V, float Grain, float Tiles, float Seed)
{
    GrainFieldSample Sample;
    Sample.Albedo = .92f + .1f * ProceduralNoise3(U * Tiles, Seed, V * Tiles);
    Sample.Relief = ProceduralNoise3(U * Tiles, Seed + 43.97f, V * Tiles) * Grain * .003f / .004f;
    return Sample;
}

struct PolymerFieldParameters
{
    float Grain = 0, Tiles = 64, Wear = 0, WearSoftness = .65f, WornRoughness = .24f, ScuffScale = 32, Seed = 17, Base = .5f;
};

GrainFieldSample EvaluatePolymerGrain(float U, float V, const PolymerFieldParameters &P, uint32_t Extent)
{
    const float Px = U * P.Tiles, Pz = V * P.Tiles, PatchRate = 1 / P.Tiles;
    const float Base = ProceduralNoise3(Px, P.Seed, Pz);
    const float Micro = Base + .3f * ProceduralNoise3(Px * 2.7f, P.Seed, Pz * 2.7f);
    const float PatchNoise = ProceduralNoise3(Px * 5.3f * PatchRate, P.Seed, Pz * 5.3f * PatchRate) +
        .28f * ProceduralNoise3(Px * 11.7f * PatchRate, P.Seed, Pz * 11.7f * PatchRate);
    const float Transition = ProceduralMix(.035f, .32f, P.WearSoftness);
    const float WearField = ProceduralSmoothstep(.5f - Transition, .72f + Transition, PatchNoise);
    const float Ceiling = ProceduralMix(1.35f, .22f, std::pow(P.Wear * WearField, .55f));
    const float Polished = ProceduralMix(Micro,
        ProceduralSmoothMinimum(Micro, Ceiling, ProceduralMix(.035f, .24f, P.WearSoftness)),
        ProceduralSmoothstep(0, .08f, P.Wear));
    const float WearMask = ProceduralSmoothstep(.005f, .24f, std::max(0.f, Micro - Polished));
    const float ScuffRate = 8 * P.ScuffScale * PatchRate;
    const float Scuffs = ProceduralNoise3(Px * ScuffRate, P.Seed + 17.31f, Pz * ScuffRate);
    const float Fine = 1 - ProceduralSmoothstep(.5f, 2, 8 * P.ScuffScale / float(Extent));
    const float ScuffWear = ProceduralSmoothstep(.65f, .87f, Scuffs) * Fine * P.Wear * .2f * WearField;
    GrainFieldSample Sample;
    const float AlbedoBase = 1 - P.Grain * .12f + Micro * P.Grain * .15f;
    Sample.Albedo = ProceduralMix(AlbedoBase, AlbedoBase * 1.18f + .012f, WearMask * .6f + ScuffWear);
    Sample.Relief = (Polished * P.Grain * .004f - ScuffWear * .001f * (1 - WearMask * .85f)) / .004f;
    Sample.Roughness = ProceduralMix(std::clamp(P.Base + P.Grain * (Base - .5f) * .16f, .025f, 1.f), P.WornRoughness, WearMask);
    return Sample;
}

std::vector<uint8_t> ConvertGrainAlbedoRgba(const std::vector<float> &Modulator, uint32_t Extent)
{
    if (!Extent || Modulator.size() != size_t(Extent) * Extent)
        throw std::runtime_error("Albedo source is not a square modulation field.");
    // 📝 Grain albedo bakes the shader's brightness modulator as neutral gray; compile multiplies it under
    // the authored conductor or polymer color.
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Index = 0; Index < Modulator.size(); ++Index)
    {
        const uint8_t Gray = uint8_t(std::lround(std::clamp(Modulator[Index], 0.f, 1.f) * 255));
        Rgba[Index * 4] = Rgba[Index * 4 + 1] = Rgba[Index * 4 + 2] = Gray;
        Rgba[Index * 4 + 3] = 255;
    }
    return Rgba;
}

std::vector<uint8_t> ConvertPolymerOrmRgba(const std::vector<float> &Roughness, uint32_t Extent, float Base, float Grain)
{
    if (!Extent || Roughness.size() != size_t(Extent) * Extent || !(Base > 0) || !(Base <= 1))
        throw std::runtime_error("ORM source is not a square roughness field over a unit base.");
    // 📝 The grain term lifts roughness by at most .08·grain above the base, so the authored scalar carries
    // that ceiling and green divides by it, mirroring the scratch ORM convention at a smaller lift.
    const float Ceiling = std::min(1.f, Base + .08f * Grain);
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Index = 0; Index < Roughness.size(); ++Index)
    {
        uint8_t *Pixel = &Rgba[Index * 4];
        Pixel[0] = 255;
        Pixel[1] = uint8_t(std::lround(std::clamp(Roughness[Index] / Ceiling, 0.f, 1.f) * 255));
        Pixel[2] = 255;
        Pixel[3] = 255;
    }
    return Rgba;
}

std::vector<uint8_t> BakeProceduralChannelRgba(std::string_view Op, std::string_view Channel,
    const std::map<std::string, double> &Params, uint32_t Extent)
{
    const auto Supported = QueryProceduralOpChannels(Op);
    if (std::find(Supported.begin(), Supported.end(), Channel) == Supported.end())
        throw std::runtime_error("Procedural op does not bake the requested channel.");
    const auto Value = [&](const char *Key, double Fallback)
    {
        const auto Found = Params.find(Key);
        return Found != Params.end() ? Found->second : Fallback;
    };
    if (Op == "metalGrain" || Op == "polymerGrain")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Grain bake extent is unbounded.");
        PolymerFieldParameters P;
        P.Grain = float(Value("grain", 0));
        P.Tiles = float(Value("detailScale", 0));
        P.Wear = float(Value("wear", 0));
        P.WearSoftness = float(Value("wearSoftness", .65));
        P.WornRoughness = float(Value("wornRoughness", .24));
        P.ScuffScale = float(Value("scratchScale", 32));
        P.Seed = float(Value("surfaceSeed", 0));
        P.Base = float(std::clamp(Value("roughness", .5), .045, 1.));
        if (!(P.Tiles > 0)) throw std::runtime_error("Procedural grain params cannot bake.");
        std::vector<float> Field(size_t(Extent) * Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const auto Sample = Op == "metalGrain" ? EvaluateMetalGrain(U, V, P.Grain, P.Tiles, P.Seed)
                                                      : EvaluatePolymerGrain(U, V, P, Extent);
                Field[size_t(Y) * Extent + X] =
                    Channel == "Albedo" ? Sample.Albedo : Channel == "Orm" ? Sample.Roughness : Sample.Relief;
            }
        if (Channel == "Albedo") return ConvertGrainAlbedoRgba(Field, Extent);
        if (Channel == "Orm") return ConvertPolymerOrmRgba(Field, Extent, P.Base, P.Grain);
        return ConvertHeightToNormalRgba(Field, Extent, 2.f);
    }
    ScratchFieldParameters Field;
    Field.Density = float(Value("scratchDensity", 0));
    Field.Length = float(Value("scratchLength", 0));
    Field.Width = float(Value("scratchWidth", 0));
    Field.Depth = float(Value("scratchDepth", 0));
    Field.Spread = float(Value("scratchSpread", 0));
    Field.Bend = float(Value("scratchBend", 0));
    Field.Scale = float(Value("scratchScale", 1));
    Field.AngleRadians = float(Value("angle", 0) * 3.141592653589793 / 180);
    Field.Seed = Value("surfaceSeed", 0) < 0 ? 0 : uint32_t(Value("surfaceSeed", 0));
    if (!(Field.Density > 0) || !(Field.Depth > 0) || !(Field.Scale > 0))
        throw std::runtime_error("Procedural scratch params cannot bake.");
    auto Maps = BakeScratchMaps(Field, Extent);
    if (Channel == "Normal")
    {
        for (float &Height : Maps.Height) Height /= Field.Depth;
        return ConvertHeightToNormalRgba(Maps.Height, Extent, 2.f);
    }
    const float Base = float(std::clamp(Value("roughness", .2), .045, 1.));
    return ConvertScratchOrmRgba(Maps.Coverage, Extent, Base);
}

std::vector<uint8_t> EncodeProceduralPng(const std::vector<uint8_t> &Rgba, uint32_t Extent)
{
    if (!Extent || Rgba.size() != size_t(Extent) * Extent * 4) throw std::runtime_error("Procedural PNG source is not square RGBA.");
    int Bytes = 0;
    unsigned char *Encoded = stbi_write_png_to_mem(Rgba.data(), 0, int(Extent), int(Extent), 4, &Bytes);
    if (!Encoded || Bytes <= 0) throw std::runtime_error("Procedural PNG encode failed.");
    std::vector<uint8_t> Png(Encoded, Encoded + Bytes);
    STBIW_FREE(Encoded);
    return Png;
}
}
