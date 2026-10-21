/*============================================================================================================================================
                                                          MATERIALPROCEDURALBAKING.CPP
============================================================================================================================================*/
// 🧩 Exact CPU evaluation of the Alloy noise and scratch kernels; shared maps keep every baked channel consistent.

#include "Engine/Render/MaterialProceduralBaking.h"
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
    if (!Extent || Coverage.size() != size_t(Extent) * Extent || !(BaseRoughness > 0))
        throw std::runtime_error("ORM source is not a square coverage field over a positive base roughness.");
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
}
