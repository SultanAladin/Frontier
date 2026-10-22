/*============================================================================================================================================
                                                          MATERIALPROCEDURALBAKING.CPP
============================================================================================================================================*/
// 🧩 Exact CPU evaluation of the Alloy noise and scratch kernels; shared maps keep every baked channel consistent.

#include "Engine/Render/MaterialProceduralBaking.h"
#include <stb_image_write.h>
#include <algorithm>
#include <array>
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

float ProceduralMirror(float T)
{
    // Triangle wave: seamless mirrored tiling for isotropic blotches (spangle, oxide).
    T -= std::floor(T);
    return T < .5f ? T * 2 : 2 - T * 2;
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

namespace
{
float ProceduralFract(float X) { return X - std::floor(X); }

// 📝 GLSL mod floors toward negative infinity, unlike fmod; twill/houndstooth addressing needs the match.
float ProceduralMod(float X, float Y) { return X - Y * std::floor(X / Y); }

float ProceduralStep(float Edge, float X) { return X >= Edge ? 1.f : 0.f; }

// 📝 Alloy bakeSRGB: linear working values encode to sRGB bytes for the albedo upload slot.
float ProceduralLinearToSrgb(float Linear)
{
    const float Clamped = std::max(Linear, 0.f);
    return Clamped <= .0031308f ? Clamped * 12.92f : 1.055f * std::pow(Clamped, 1 / 2.4f) - .055f;
}

struct TwillSample
{
    float Light = 0, Height = 0, Direction = 0, Fiber = 0;
};

// 📝 Verbatim twill(): rotated cell addressing, per-pattern over/under masks, rounded-bundle arch with
// inter-yarn gaps, sub-fiber filtering and the resolved fade. Step is fwidth(uv) in scaled UV per texel.
TwillSample EvaluateTwill(float U, float V, const WeaveFieldParameters &P, float Step)
{
    const float C = std::cos(P.AngleRadians), S = std::sin(P.AngleRadians);
    const float Su = (C * U + S * V) * P.Scale, Sv = (-S * U + C * V) * P.Scale;
    const float CellX = std::floor(Su), CellY = std::floor(Sv);
    const float Fx = Su - CellX, Fy = Sv - CellY;
    float Over = ProceduralStep(2, ProceduralMod(CellX - CellY, 4));
    if (P.Type == 4)
    {
        if (P.Weave == 0) Over = ProceduralMod(CellX + CellY, 2);
        if (P.Weave == 2) Over = ProceduralStep(4, ProceduralMod(CellX + 2 * CellY, 5));
        if (P.Weave == 3) Over = ProceduralMod(std::floor(CellX * .5f) + std::floor(CellY * .5f), 2);
        if (P.Weave == 4) Over = ProceduralMod(CellX + std::floor(CellY * .5f), 2);
        if (P.Weave == 5)
        {
            const float Reverse = ProceduralMod(CellX, 16);
            Over = ProceduralStep(2, ProceduralMod((Reverse < 8 ? Reverse : 15 - Reverse) - CellY, 4));
        }
        if (P.Weave == 6) Over = ProceduralMod(std::floor(CellX * .5f) + CellY, 2);
        if (P.Weave == 8) Over = ProceduralStep(3, ProceduralMod(CellX - CellY, 4));
    }
    const float Across = ProceduralMix(Fx, Fy, Over), Coord = ProceduralMix(Su, Sv, Over);
    const float Edge = std::min(Across, 1 - Across);
    const float Chord = Across * 2 - 1;
    const float Bundle = std::sqrt(std::max(0.f, 1 - Chord * Chord));
    const float Gap = ProceduralSmoothstep(.008f, .045f, Edge);
    const float Along = ProceduralMix(Sv, Su, Over);
    const float FloatLength =
        (P.Type == 4 && P.Weave == 2) ? 5 : (P.Type == 4 && P.Weave == 8) ? 3 : (P.Type == 4 && P.Weave == 0) ? 1 : 2;
    const float Arch = .86f + .14f * std::cos((ProceduralFract(Along / FloatLength) * 2 - 1) * 3.14159f);
    const float Fiber = (std::sin(Coord * 6.283f * P.FiberDetail) * .5f + .5f) *
        (1 - ProceduralSmoothstep(.5f, 3.1f, Step * 6.283f * P.FiberDetail));
    const float Height = Bundle * Gap * Arch;
    float Light = (.32f + .68f * Bundle) * Gap * (.86f + .14f * Fiber);
    if (P.Type == 4 && P.FabricMode == 3) Light *= .88f + .16f * ProceduralNoise3(Su * .6f, Sv * .6f, 3);
    if (P.Type == 4 && P.Weave == 2) Light = (.72f + .28f * Bundle) * (.91f + .09f * Fiber) * ProceduralMix(.9f, 1.f, Gap);
    const float YarnResolved = 1 - ProceduralSmoothstep(.22f, .95f, Step);
    const float MeanDirection = (P.Type == 4 && P.Weave == 2) ? .2f : (P.Type == 4 && P.Weave == 8) ? .25f : .5f;
    return {ProceduralMix((P.Type == 4 && P.Weave == 2) ? .86f : .69f, Light, YarnResolved),
        ProceduralMix(.63f, Height, YarnResolved), ProceduralMix(MeanDirection, Over, YarnResolved), Fiber * YarnResolved};
}

float SegmentDistance(float Px, float Py, float Ax, float Ay, float Bx, float By)
{
    const float Dx = Bx - Ax, Dy = By - Ay;
    const float T = std::clamp(((Px - Ax) * Dx + (Py - Ay) * Dy) / (Dx * Dx + Dy * Dy), 0.f, 1.f);
    const float Ex = Px - Ax - Dx * T, Ey = Py - Ay - Dy * T;
    return std::sqrt(Ex * Ex + Ey * Ey);
}

// 📝 Verbatim knit(): V-loop pair per cell with sub-loop ribbing and the resolved fade to flat jersey.
float EvaluateKnitLoops(float U, float V, const KnitFieldParameters &P, float Step)
{
    const float C = std::cos(P.AngleRadians), S = std::sin(P.AngleRadians);
    const float Su = (C * U + S * V) * P.Scale, Sv = (-S * U + C * V) * P.Scale;
    const float Qx = ProceduralFract(Su) - .5f, Qy = ProceduralFract(Sv) - .5f;
    const float LoopD =
        std::min(SegmentDistance(Qx, Qy, -.34f, .48f, .02f, -.44f), SegmentDistance(Qx, Qy, .34f, .48f, -.02f, -.44f));
    const float Aa = std::max(Step * .35f, .005f);
    const float Yarn = 1 - ProceduralSmoothstep(.065f - Aa, .15f + Aa, LoopD);
    const float Sub = std::sin((Qx + Qy * .35f) * 100) * .5f + .5f;
    const float Ribbed = ProceduralMix(.5f, Sub, 1 - ProceduralSmoothstep(.02f, .1f, Step));
    return ProceduralMix(.47f, Yarn * (.82f + .18f * Ribbed), 1 - ProceduralSmoothstep(.3f, 1.4f, Step));
}

// 📝 Verbatim cellular3(): 3x3x3 Worley neighbourhood, F2-F1 edge metric plus the winning cell's three
// radiance hashes. Polycrystalline solar grain reads the second hash; the full triple stays for parity.
void EvaluateCellular3(float X, float Y, float Z, float Seed, float Out[4])
{
    const float CellX = std::floor(X), CellY = std::floor(Y), CellZ = std::floor(Z);
    const float Fx = X - CellX, Fy = Y - CellY, Fz = Z - CellZ;
    float F1 = 1e8f, F2 = 1e8f, NearestX = 0, NearestY = 0, NearestZ = 0;
    for (int Oz = -1; Oz <= 1; ++Oz)
        for (int Oy = -1; Oy <= 1; ++Oy)
            for (int Ox = -1; Ox <= 1; ++Ox)
            {
                const float IdX = CellX + float(Ox), IdY = CellY + float(Oy), IdZ = CellZ + float(Oz);
                const float SiteX = .08f + .84f * ProceduralHash31(IdX + Seed, IdY + Seed, IdZ + Seed);
                const float SiteY =
                    .08f + .84f * ProceduralHash31(IdX + Seed + 17.31f, IdY + Seed + 17.31f, IdZ + Seed + 17.31f);
                const float SiteZ =
                    .08f + .84f * ProceduralHash31(IdX + Seed + 43.97f, IdY + Seed + 43.97f, IdZ + Seed + 43.97f);
                const float Dx = float(Ox) + SiteX - Fx, Dy = float(Oy) + SiteY - Fy, Dz = float(Oz) + SiteZ - Fz;
                const float Metric = Dx * Dx + Dy * Dy + Dz * Dz;
                if (Metric < F1)
                {
                    F2 = F1;
                    F1 = Metric;
                    NearestX = IdX;
                    NearestY = IdY;
                    NearestZ = IdZ;
                }
                else if (Metric < F2) F2 = Metric;
            }
    Out[0] = std::max(0.f, std::sqrt(F2) - std::sqrt(F1));
    Out[1] = ProceduralHash31(NearestX + Seed + 61.7f, NearestY + Seed + 61.7f, NearestZ + Seed + 61.7f);
    Out[2] = ProceduralHash31(NearestX + Seed + 127.3f, NearestY + Seed + 127.3f, NearestZ + Seed + 127.3f);
    Out[3] = ProceduralHash31(NearestX + Seed + 223.1f, NearestY + Seed + 223.1f, NearestZ + Seed + 223.1f);
}

// 📝 Verbatim dimple(): staggered hex packing searched over a 3x3 neighbourhood, parabolic bowl with the
// resolved fade to the mean. Step is fwidth(uv) in scaled UV per texel.
float EvaluateDimple(float U, float V, float Scale, float Step)
{
    const float Su = U * Scale, Sv = V * Scale;
    const float CellX = std::floor(Su), CellY = std::floor(Sv / .8660254f);
    float Nearest = 2;
    for (int Y = -1; Y <= 1; ++Y)
        for (int X = -1; X <= 1; ++X)
        {
            const float IdX = CellX + float(X), IdY = CellY + float(Y);
            const float CenterX = IdX + .5f * ProceduralMod(IdY, 2), CenterY = IdY * .8660254f;
            const float Dx = Su - CenterX, Dy = Sv - CenterY;
            Nearest = std::min(Nearest, std::sqrt(Dx * Dx + Dy * Dy));
        }
    const float R = Nearest / .44f;
    const float Bowl = std::pow(std::max(0.f, 1 - R * R), 1.6f);
    const float Resolved = 1 - ProceduralSmoothstep(.3f, 1.2f, Step);
    return ProceduralMix(.24f, Bowl, Resolved);
}

struct SandCell
{
    float Metric = 0, Second = 0, Random = 0, Rough = 0, Spec = 0, Radius = 0;
    float DeltaX = 0, DeltaY = 0, IdX = 0, IdY = 0;
};

// 📝 Verbatim sandCellAt(): jittered-site Voronoi over a 5x5 neighbourhood with per-cell size/metric/radiance
// hashes; the winning cell's id drives grain tint, roughness, specular and facet angle.
SandCell EvaluateSandCell(float Qx, float Qy, float Seed, float Variation, float Nominal)
{
    const float CellX = std::floor(Qx), CellY = std::floor(Qy), Fx = Qx - CellX, Fy = Qy - CellY;
    SandCell Best;
    Best.Metric = Best.Second = 1e8f;
    Best.Random = Best.Rough = Best.Spec = Best.Radius = .5f;
    const float Lo = std::max(.0625f, Nominal * (1 - Variation * .75f)) / Nominal;
    const float Hi = std::min(2.f, Nominal * (1 + Variation * .75f)) / Nominal;
    for (int Y = -2; Y <= 2; ++Y)
        for (int X = -2; X <= 2; ++X)
        {
            const float IdX = CellX + X, IdY = CellY + Y;
            const float Radius = .55f * ProceduralMix(Lo, Hi, ProceduralHash31(IdX, IdY, Seed + 9.3f));
            const float SiteX = .08f + .84f * ProceduralHash31(IdX, IdY, Seed);
            const float SiteY = .08f + .84f * ProceduralHash31(IdX + 31.7f, IdY + 31.7f, Seed + 17.1f);
            const float Dx = float(X) + SiteX - Fx, Dy = float(Y) + SiteY - Fy;
            const float Metric = (Dx * Dx + Dy * Dy) / std::max(Radius * Radius, .0001f);
            if (Metric < Best.Metric)
            {
                Best.Second = Best.Metric;
                Best.Metric = Metric;
                Best.DeltaX = Dx;
                Best.DeltaY = Dy;
                Best.IdX = IdX;
                Best.IdY = IdY;
                Best.Radius = Radius;
                Best.Random = ProceduralHash31(IdX, IdY, Seed + 53.9f);
                Best.Rough = ProceduralHash31(IdX + 7.1f, IdY + 7.1f, Seed + 113);
                Best.Spec = ProceduralHash31(IdX + 21.7f, IdY + 21.7f, Seed + 181);
            }
            else if (Metric < Best.Second) Best.Second = Metric;
        }
    return Best;
}
}

bool DecodeHexLinearRgb(std::string_view Hex, float Rgb[3])
{
    // 📝 THREE.Color parity: #rrggbb reads as sRGB and lands in linear working space.
    if (Hex.size() != 7 || Hex[0] != '#') return false;
    for (int Channel = 0; Channel < 3; ++Channel)
    {
        unsigned int Byte = 0;
        for (int Nibble = 0; Nibble < 2; ++Nibble)
        {
            const char Digit = Hex[size_t(1 + Channel * 2 + Nibble)];
            Byte <<= 4;
            if (Digit >= '0' && Digit <= '9') Byte |= unsigned(Digit - '0');
            else if (Digit >= 'a' && Digit <= 'f') Byte |= unsigned(Digit - 'a' + 10);
            else if (Digit >= 'A' && Digit <= 'F') Byte |= unsigned(Digit - 'A' + 10);
            else return false;
        }
        const float Srgb = float(Byte) / 255;
        Rgb[Channel] = Srgb <= .04045f ? Srgb / 12.92f : std::pow((Srgb + .055f) / 1.055f, 2.4f);
    }
    return true;
}

WeaveFieldSample EvaluateWeaveField(float U, float V, const WeaveFieldParameters &Parameters, float PixelStep)
{
    if (!(Parameters.Scale > 0)) throw std::runtime_error("Weave field needs a positive scale.");
    const float C = std::cos(Parameters.AngleRadians), S = std::sin(Parameters.AngleRadians);
    // 📝 fwidth sums |d/dx|+|d/dy|; the rotated coordinate inherits the texel rate times the rotation row.
    const float Step = PixelStep * (std::abs(C) + std::abs(S));
    const TwillSample Weave = EvaluateTwill(U, V, Parameters, Step);
    const float YarnU = (C * U + S * V) * Parameters.Scale, YarnV = (-S * U + C * V) * Parameters.Scale;
    WeaveFieldSample Sample;
    // 📝 Constant-rate axes collapse onto the seed slice (grain-bake convention); pp maps to (u, seed, v).
    const float Grain = ProceduralNoise3(U * Parameters.Scale, Parameters.Seed, V * Parameters.Scale);
    if (Parameters.Type == 7)
    {
        const float Modulator = .18f + Weave.Light * .82f;
        Sample.Albedo[0] = Sample.Albedo[1] = Sample.Albedo[2] = Modulator;
    }
    else if (Parameters.FabricMode == 0 || Parameters.FabricMode == 3 || Parameters.FabricMode == 4)
    {
        float Yarn[3];
        for (int Channel = 0; Channel < 3; ++Channel)
            Yarn[Channel] = ProceduralMix(Parameters.Warp[Channel], Parameters.Weft[Channel], Weave.Direction);
        if (Parameters.Weave == 7)
        {
            const float WarpBand = ProceduralStep(2, ProceduralMod(std::floor(YarnU), 4));
            const float WeftBand = ProceduralStep(2, ProceduralMod(std::floor(YarnV), 4));
            const float ColorDetail = 1 - ProceduralSmoothstep(.35f, 1.5f, Step);
            const float BandMix = ProceduralMix(WarpBand, WeftBand, Weave.Direction);
            for (int Channel = 0; Channel < 3; ++Channel)
                Yarn[Channel] = ProceduralMix((Parameters.Warp[Channel] + Parameters.Weft[Channel]) * .5f,
                    ProceduralMix(Parameters.Warp[Channel], Parameters.Weft[Channel], BandMix), ColorDetail);
        }
        const float Shade = .48f + Weave.Light * .58f;
        for (int Channel = 0; Channel < 3; ++Channel) Sample.Albedo[Channel] = Yarn[Channel] * Shade;
    }
    else
    {
        const float Nap =
            ProceduralNoise3(U * Parameters.Scale * 18, Parameters.Seed, V * Parameters.Scale * 18);
        const float Modulator = .68f + .36f * Nap;
        Sample.Albedo[0] = Sample.Albedo[1] = Sample.Albedo[2] = Modulator;
    }
    const float Arch = (Parameters.Type == 4 && (Parameters.FabricMode == 1 || Parameters.FabricMode == 2))
        ? Grain * .15f
        : Weave.Height;
    Sample.Relief = Arch * Parameters.Relief * .022f / std::max(Parameters.Scale, .01f);
    if (Parameters.Type == 4)
    {
        const float FiberNap =
            ProceduralNoise3(U * Parameters.Scale * 18, Parameters.Seed, V * Parameters.Scale * 18);
        Sample.Relief += FiberNap * Parameters.Fuzz * .0004f;
        const float FuzzShade = 1 - Parameters.Fuzz * .12f + Parameters.Fuzz * FiberNap * .24f;
        for (int Channel = 0; Channel < 3; ++Channel) Sample.Albedo[Channel] *= FuzzShade;
        if (Parameters.Weave == 8)
        {
            const float SlubNoise = ProceduralNoise3(YarnU * 1.4f, YarnV * .09f, 21);
            const float Fade = (.15f + .85f * ProceduralSmoothstep(.3f, .8f, SlubNoise)) * Parameters.DenimFade *
                (1 - Weave.Direction);
            for (int Channel = 0; Channel < 3; ++Channel)
                Sample.Albedo[Channel] = ProceduralMix(Sample.Albedo[Channel], Parameters.Weft[Channel] * .85f, Fade);
            const float SlubShade = 1 + (SlubNoise - .5f) * Parameters.Slub * .4f;
            for (int Channel = 0; Channel < 3; ++Channel) Sample.Albedo[Channel] *= SlubShade;
            Sample.Relief += (SlubNoise - .5f) * Parameters.Slub * .001f * (1 - Weave.Direction);
        }
    }
    return Sample;
}

WeaveBakeMaps BakeWeaveMaps(const WeaveFieldParameters &Parameters, uint32_t Extent)
{
    if (!Extent || Extent > 2048) throw std::runtime_error("Weave bake extent is unbounded.");
    if (!(Parameters.Scale > 0)) throw std::runtime_error("Weave bake needs a positive scale.");
    WeaveBakeMaps Maps;
    Maps.Extent = Extent;
    Maps.Albedo.assign(size_t(Extent) * Extent * 3, 0);
    Maps.Relief.assign(size_t(Extent) * Extent, 0);
    const float PixelStep = Parameters.Scale / float(Extent);
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const auto Sample = EvaluateWeaveField((float(X) + .5f) / float(Extent), (float(Y) + .5f) / float(Extent),
                Parameters, PixelStep);
            for (int Channel = 0; Channel < 3; ++Channel)
                Maps.Albedo[(size_t(Y) * Extent + X) * 3 + Channel] = Sample.Albedo[Channel];
            Maps.Relief[size_t(Y) * Extent + X] = Sample.Relief;
        }
    return Maps;
}

float WeaveReliefAmplitude(const WeaveFieldParameters &Parameters)
{
    const float Arch = (Parameters.Type == 4 && (Parameters.FabricMode == 1 || Parameters.FabricMode == 2))
        ? Parameters.Grain * .15f
        : 1.f;
    float Amplitude = Arch * Parameters.Relief * .022f / std::max(Parameters.Scale, .01f);
    if (Parameters.Type == 4)
    {
        Amplitude += Parameters.Fuzz * .0004f;
        if (Parameters.Weave == 8) Amplitude += Parameters.Slub * .0005f;
    }
    return Amplitude;
}

KnitFieldSample EvaluateKnitField(float U, float V, const KnitFieldParameters &Parameters, float PixelStep)
{
    if (!(Parameters.Scale > 0)) throw std::runtime_error("Knit field needs a positive scale.");
    const float C = std::cos(Parameters.AngleRadians), S = std::sin(Parameters.AngleRadians);
    const float Step = PixelStep * (std::abs(C) + std::abs(S));
    const float Loops = EvaluateKnitLoops(U, V, Parameters, Step);
    // 📝 Melange mottling keeps its two in-plane rates; the out-of-plane axis takes the seed slice.
    const float Melange = ProceduralNoise3(U * Parameters.Scale * .25f, Parameters.Seed, V * Parameters.Scale * 2);
    const float Mask = ProceduralSmoothstep(.55f, .9f, Melange) * .32f;
    const float Shade = .48f + Loops * .57f;
    KnitFieldSample Sample;
    for (int Channel = 0; Channel < 3; ++Channel)
        Sample.Albedo[Channel] = ProceduralMix(Parameters.Color[Channel], Parameters.Secondary[Channel], Mask) * Shade;
    Sample.Relief = Loops * Parameters.Grain * .008f / std::max(std::sqrt(Parameters.Scale), .1f) +
        ProceduralNoise3(U * Parameters.Scale * 13, Parameters.Seed, V * Parameters.Scale * 13) * Parameters.Fuzz * .0004f;
    return Sample;
}

KnitBakeMaps BakeKnitMaps(const KnitFieldParameters &Parameters, uint32_t Extent)
{
    if (!Extent || Extent > 2048) throw std::runtime_error("Knit bake extent is unbounded.");
    if (!(Parameters.Scale > 0)) throw std::runtime_error("Knit bake needs a positive scale.");
    KnitBakeMaps Maps;
    Maps.Extent = Extent;
    Maps.Albedo.assign(size_t(Extent) * Extent * 3, 0);
    Maps.Relief.assign(size_t(Extent) * Extent, 0);
    const float PixelStep = Parameters.Scale / float(Extent);
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const auto Sample = EvaluateKnitField((float(X) + .5f) / float(Extent), (float(Y) + .5f) / float(Extent),
                Parameters, PixelStep);
            for (int Channel = 0; Channel < 3; ++Channel)
                Maps.Albedo[(size_t(Y) * Extent + X) * 3 + Channel] = Sample.Albedo[Channel];
            Maps.Relief[size_t(Y) * Extent + X] = Sample.Relief;
        }
    return Maps;
}

float KnitReliefAmplitude(const KnitFieldParameters &Parameters)
{
    return Parameters.Grain * .008f / std::max(std::sqrt(std::max(Parameters.Scale, 0.f)), .1f) +
        Parameters.Fuzz * .0004f;
}

double SandBedSeed(std::string_view BedId)
{
    // 📝 sandLayerSeed parity: FNV-1a over the id bytes, reduced mod 8191, times .013.
    uint32_t Hash = 2166136261u;
    for (const unsigned char Byte : BedId) Hash = (Hash ^ Byte) * 16777619u;
    return double(Hash % 8191) * .013;
}

SandFieldSample EvaluateSandField(float U, float V, const SandFieldParameters &Parameters, uint32_t Extent)
{
    if (Parameters.Count <= 0 || Parameters.Count > 4) throw std::runtime_error("Sand field needs one to four beds.");
    if (!Extent) throw std::runtime_error("Sand field needs a nonzero extent.");
    SandFieldSample Sample;
    for (int Channel = 0; Channel < 3; ++Channel) Sample.Tint[Channel] = Parameters.BaseColor[Channel];
    const SandBedParameters &Deepest = Parameters.Beds[Parameters.Count - 1];
    Sample.Roughness = std::min(1.f, Deepest.Roughness + .16f);
    float TotalBand = 0;
    for (int Bed = 0; Bed < Parameters.Count; ++Bed) TotalBand += Parameters.Beds[Bed].Thickness;
    const float C = std::cos(Parameters.BandAngleRadians), S = std::sin(Parameters.BandAngleRadians);
    const float BandX = C * U + S * V, BandY = -S * U + C * V;
    const float BandNoise = ProceduralNoise3(BandX * .7f, BandY * .7f, Parameters.Seed * .013f);
    const float Band = ProceduralFract(BandY * Parameters.BandFrequency + (BandNoise - .5f) * Parameters.BandWarp);
    float BandEdge = 0;
    int Selected = 0;
    for (int Bed = 0; Bed < 4; ++Bed)
        if (Bed < Parameters.Count)
        {
            const float End = BandEdge + Parameters.Beds[Bed].Thickness / std::max(TotalBand, .001f);
            if (Band >= BandEdge && Band < End) Selected = Bed;
            BandEdge = End;
        }
    // 📝 Bed 0 is uppermost: lower beds evaluate first and the selected top grain occludes them.
    for (int Reverse = 0; Reverse < 4; ++Reverse)
    {
        const int Layer = 3 - Reverse;
        if (Layer >= Parameters.Count || (Parameters.BandsMode == 1 && Layer != Selected)) continue;
        const SandBedParameters &Bed = Parameters.Beds[Layer];
        if (!(Bed.Coverage > 0)) continue;
        const float Frequency = 110 * Parameters.Scale / std::max(Bed.Size, .0625f);
        float DomainX = U * Frequency + float(Bed.BedSeed * 71.3);
        float DomainY = V * Frequency + float(Bed.BedSeed * 29.7);
        // 📝 Both warp taps read the pre-warp domain; GLSL evaluates the pair simultaneously.
        const float WarpX = ProceduralNoise3(DomainX * .19f, DomainY * .19f, Parameters.Seed);
        const float WarpY = ProceduralNoise3(DomainX * .19f + 19.1f, DomainY * .19f + 19.1f, Parameters.Seed);
        DomainX += (WarpX - .5f) * .45f;
        DomainY += (WarpY - .5f) * .45f;
        // 📝 The domain warp is low-rate, so the footprint converges to the nominal grain texel rate.
        const float Footprint = Frequency / float(Extent);
        const float Resolved = 1 - ProceduralSmoothstep(.55f, 1.8f, Footprint);
        float Mean[3], Tint[3];
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            Mean[Channel] = Bed.Color[Channel] * .81f + Bed.Pale[Channel] * .17f + Bed.Color[Channel] * .38f * .02f;
            Tint[Channel] = Mean[Channel];
        }
        float Opacity = Bed.Coverage * .78f, Cap = 0, RoughGrain = Bed.Roughness, TiltX = 0, TiltY = 0;
        if (Resolved > .001f)
        {
            const float CellSeed = Parameters.Seed + float(Bed.BedSeed * 97.1);
            const SandCell Cell = EvaluateSandCell(DomainX, DomainY, CellSeed, Bed.SizeVariation, Bed.Size);
            const float Distance = std::sqrt(Cell.Metric), Aa = std::max(Footprint * .65f, .01f);
            const float Contact = ProceduralSmoothstep(0.f, std::max(.04f, Aa), std::sqrt(Cell.Second) - Distance);
            const float Outline = 1 - ProceduralSmoothstep(1 - Aa, 1 + Aa, Distance);
            const float Occupied =
                ProceduralStep(1 - Bed.Coverage, ProceduralHash31(Cell.IdX, Cell.IdY, CellSeed + 271));
            const float GrainMask = Outline * Contact * Occupied;
            for (int Channel = 0; Channel < 3; ++Channel)
                Tint[Channel] =
                    ProceduralMix(Bed.Color[Channel], Bed.Pale[Channel], ProceduralSmoothstep(.7f, .92f, Cell.Random));
            const float Dark = ProceduralStep(.97f, Cell.Random);
            const float Bright = .86f + .28f * Cell.Rough;
            for (int Channel = 0; Channel < 3; ++Channel)
                Tint[Channel] = ProceduralMix(Tint[Channel], Tint[Channel] * .38f, Dark) * Bright;
            const float Rounded =
                std::pow(std::max(0.f, 1 - Cell.Metric), ProceduralMix(.2f, .85f, Bed.Roundness));
            Cap = Rounded * GrainMask * Bed.Size * .01f * Bed.Relief * .62f / std::max(Parameters.Scale, .1f);
            RoughGrain = std::clamp(Bed.Roughness + (Cell.Rough - .5f) * Bed.RoughnessVariation, .04f, 1.f);
            const float Angle = Cell.Random * 6.2831853f;
            TiltX = std::cos(Angle) * Bed.Tilt * (.35f + .65f * Cell.Spec);
            TiltY = std::sin(Angle) * Bed.Tilt * (.35f + .65f * Cell.Spec);
            Opacity = ProceduralMix(Opacity, GrainMask, Resolved);
            for (int Channel = 0; Channel < 3; ++Channel) Tint[Channel] = ProceduralMix(Mean[Channel], Tint[Channel], Resolved);
            RoughGrain = ProceduralMix(Bed.Roughness, RoughGrain, Resolved);
        }
        for (int Channel = 0; Channel < 3; ++Channel)
            Sample.Tint[Channel] = ProceduralMix(Sample.Tint[Channel], Tint[Channel], Opacity);
        Sample.Relief = ProceduralMix(Sample.Relief, Cap, Opacity);
        Sample.Roughness = ProceduralMix(Sample.Roughness, RoughGrain, Opacity);
        Sample.Facet[0] = ProceduralMix(Sample.Facet[0], TiltX * Resolved, Opacity);
        Sample.Facet[1] = ProceduralMix(Sample.Facet[1], TiltY * Resolved, Opacity);
    }
    return Sample;
}

SandBakeMaps BakeSandMaps(const SandFieldParameters &Parameters, uint32_t Extent)
{
    if (!Extent || Extent > 2048) throw std::runtime_error("Sand bake extent is unbounded.");
    if (Parameters.Count <= 0 || Parameters.Count > 4) throw std::runtime_error("Sand bake needs one to four beds.");
    SandBakeMaps Maps;
    Maps.Extent = Extent;
    Maps.Tint.assign(size_t(Extent) * Extent * 3, 0);
    Maps.Roughness.assign(size_t(Extent) * Extent, 0);
    Maps.Relief.assign(size_t(Extent) * Extent, 0);
    Maps.Facet.assign(size_t(Extent) * Extent * 2, 0);
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const auto Sample = EvaluateSandField((float(X) + .5f) / float(Extent), (float(Y) + .5f) / float(Extent),
                Parameters, Extent);
            for (int Channel = 0; Channel < 3; ++Channel)
                Maps.Tint[(size_t(Y) * Extent + X) * 3 + Channel] = Sample.Tint[Channel];
            Maps.Roughness[size_t(Y) * Extent + X] = Sample.Roughness;
            Maps.Relief[size_t(Y) * Extent + X] = Sample.Relief;
            Maps.Facet[(size_t(Y) * Extent + X) * 2] = Sample.Facet[0];
            Maps.Facet[(size_t(Y) * Extent + X) * 2 + 1] = Sample.Facet[1];
        }
    return Maps;
}

float SandReliefAmplitude(const SandFieldParameters &Parameters)
{
    float Amplitude = 0;
    for (int Bed = 0; Bed < Parameters.Count; ++Bed)
        Amplitude = std::max(Amplitude, Parameters.Beds[Bed].Size * .01f * Parameters.Beds[Bed].Relief * .62f /
                std::max(Parameters.Scale, .1f));
    return Amplitude;
}

std::vector<uint8_t> ConvertLinearAlbedoRgba(const std::vector<float> &Linear, uint32_t Extent)
{
    if (!Extent || Linear.size() != size_t(Extent) * Extent * 3)
        throw std::runtime_error("Albedo source is not a square linear-RGB field.");
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Pixel = 0; Pixel < size_t(Extent) * Extent; ++Pixel)
    {
        uint8_t *Out = &Rgba[Pixel * 4];
        for (int Channel = 0; Channel < 3; ++Channel)
            Out[Channel] = uint8_t(std::lround(std::clamp(ProceduralLinearToSrgb(Linear[Pixel * 3 + Channel]), 0.f, 1.f) * 255));
        Out[3] = 255;
    }
    return Rgba;
}

std::vector<uint8_t> ConvertSandOrmRgba(const std::vector<float> &Roughness, uint32_t Extent)
{
    if (!Extent || Roughness.size() != size_t(Extent) * Extent)
        throw std::runtime_error("ORM source is not a square roughness field.");
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Pixel = 0; Pixel < Roughness.size(); ++Pixel)
    {
        uint8_t *Out = &Rgba[Pixel * 4];
        Out[0] = 255;
        Out[1] = uint8_t(std::lround(std::clamp(Roughness[Pixel], 0.f, 1.f) * 255));
        Out[2] = 255;
        Out[3] = 255;
    }
    return Rgba;
}

std::vector<uint8_t> ConvertFacetNormalRgba(const std::vector<float> &Relief, const std::vector<float> &Facet,
    uint32_t Extent, float Strength)
{
    if (!Extent || Relief.size() != size_t(Extent) * Extent || Facet.size() != size_t(Extent) * Extent * 2)
        throw std::runtime_error("Normal source is not a square relief/facet field pair.");
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const uint32_t Left = X > 0 ? X - 1 : 0, Right = X + 1 < Extent ? X + 1 : Extent - 1;
            const uint32_t Up = Y > 0 ? Y - 1 : 0, Down = Y + 1 < Extent ? Y + 1 : Extent - 1;
            const float Gx = (Relief[size_t(Y) * Extent + Right] - Relief[size_t(Y) * Extent + Left]) * .5f;
            const float Gy = (Relief[size_t(Down) * Extent + X] - Relief[size_t(Up) * Extent + X]) * .5f;
            const float Nx = -Gx * Strength + Facet[(size_t(Y) * Extent + X) * 2];
            const float Ny = -Gy * Strength + Facet[(size_t(Y) * Extent + X) * 2 + 1];
            const float Inverse = 1 / std::sqrt(Nx * Nx + Ny * Ny + 1);
            uint8_t *Pixel = &Rgba[(size_t(Y) * Extent + X) * 4];
            Pixel[0] = uint8_t(std::lround((Nx * Inverse * .5f + .5f) * 255));
            Pixel[1] = uint8_t(std::lround((Ny * Inverse * .5f + .5f) * 255));
            Pixel[2] = uint8_t(std::lround((Inverse * .5f + .5f) * 255));
            Pixel[3] = 255;
        }
    return Rgba;
}

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     CODED FIELDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Line-for-line FieldSpecification.js port in double precision; see header note on the sin hash.
double CodedClamp(double Quantity) { return std::max(0., std::min(1., Quantity)); }

double CodedHashCell(double Column, double Row, double Seed)
{
    const double Product = std::sin(Column * 127.1 + Row * 311.7 + Seed * 17.13) * 43758.5453;
    return Product - std::floor(Product);
}

double CodedSmoothNoise(double Horizontal, double Vertical, double Seed)
{
    const double Column = std::floor(Horizontal), Row = std::floor(Vertical);
    const double FractionX = Horizontal - Column, FractionY = Vertical - Row;
    const double BlendX = FractionX * FractionX * (3 - 2 * FractionX);
    const double BlendY = FractionY * FractionY * (3 - 2 * FractionY);
    const double Top = CodedHashCell(Column, Row, Seed) * (1 - BlendX) + CodedHashCell(Column + 1, Row, Seed) * BlendX;
    const double Bottom = CodedHashCell(Column, Row + 1, Seed) * (1 - BlendX) + CodedHashCell(Column + 1, Row + 1, Seed) * BlendX;
    return Top * (1 - BlendY) + Bottom * BlendY;
}

double CodedCamouflage(const CodedFieldParameters &Parameters, double Horizontal, double Vertical, bool Smooth)
{
    // 📝 Periodic coordinate warping keeps opposite tile edges identical without image assets.
    const double Frequency = std::max(1., std::round(Parameters.Scale)), Seed = Parameters.Seed;
    const auto Periodic = [](double X, double Y, double Period, double Salt)
    {
        const double Column = std::floor(X), Row = std::floor(Y), FractionX = X - Column, FractionY = Y - Row;
        const double BlendX = FractionX * FractionX * (3 - 2 * FractionX), BlendY = FractionY * FractionY * (3 - 2 * FractionY);
        const auto Cell = [Period, Salt](double CellColumn, double CellRow)
        {
            const auto Wrap = [Period](double Coordinate)
            {
                const double Remainder = std::fmod(Coordinate, Period);
                return std::fmod(Remainder + Period, Period);
            };
            return CodedHashCell(Wrap(CellColumn), Wrap(CellRow), Salt);
        };
        return (Cell(Column, Row) * (1 - BlendX) + Cell(Column + 1, Row) * BlendX) * (1 - BlendY) +
               (Cell(Column, Row + 1) * (1 - BlendX) + Cell(Column + 1, Row + 1) * BlendX) * BlendY;
    };
    const double Grid = 256 / std::max(1., Parameters.PixelSize);
    const double PositionX = (Smooth ? Horizontal + .5 : std::floor((Horizontal + .5) * Grid) / Grid) * Frequency;
    const double PositionY = (Smooth ? Vertical + .5 : std::floor((Vertical + .5) * Grid) / Grid) * Frequency;
    const double WarpedX = PositionX + (Periodic(PositionX, PositionY, Frequency, Seed + 17) - .5) * 2.4;
    const double WarpedY = PositionY + (Periodic(PositionX, PositionY, Frequency, Seed + 71) - .5) * 2.4;
    const double Field = Periodic(WarpedX, WarpedY, Frequency, Seed) * .78 +
                         Periodic(WarpedX * 3, WarpedY * 3, Frequency * 3, Seed + 43) * .22;
    const uint32_t Count = std::max(2u, std::min(8u, Parameters.ColourCount));
    double Band = 0;
    for (uint32_t Index = 0; Index + 1 < Count; ++Index)
    {
        const double Threshold = Count == 2 ? .5 : .36 + double(Index) * .28 / double(Count - 2);
        const double Blend = CodedClamp((Field - Threshold) / std::max(.001, Parameters.Softness) + .5);
        Band += Smooth ? Blend * Blend * (3 - 2 * Blend) : (Field >= Threshold ? 1. : 0.);
    }
    return Band / double(Count - 1);
}

} // namespace

double EvaluateCodedField(std::string_view Identity, const CodedFieldParameters &Parameters,
    double Horizontal, double Vertical)
{
    double Sample = 0;
    if (Identity == "camouflage" || Identity == "camouflage-smooth")
        Sample = CodedCamouflage(Parameters, Horizontal, Vertical, Identity == "camouflage-smooth");
    else if (QueryCodedField(Identity))
    {
        const double Frequency = Parameters.Scale;
        const double PositionX = (Horizontal + .5) * Frequency, PositionY = (Vertical + .5) * Frequency;
        if (Identity == "stripes")
            Sample = .5 + .5 * std::sin(PositionY * 3.141592653589793 * 2 + Parameters.Seed);
        else if (Identity == "checker")
        {
            const double Parity = std::fmod(std::floor(PositionX) + std::floor(PositionY) + Parameters.Seed, 2.);
            Sample = std::abs(Parity);
        }
        else if (Identity == "cells")
        {
            const double Column = std::floor(PositionX), Row = std::floor(PositionY);
            double Distance = 2;
            for (int OffsetY = -1; OffsetY <= 1; ++OffsetY)
                for (int OffsetX = -1; OffsetX <= 1; ++OffsetX)
                {
                    const double CellX = Column + OffsetX, CellY = Row + OffsetY;
                    const double CenterX = CellX + CodedHashCell(CellX, CellY, Parameters.Seed);
                    const double CenterY = CellY + CodedHashCell(CellX, CellY, Parameters.Seed + 31);
                    Distance = std::min(Distance, std::hypot(PositionX - CenterX, PositionY - CenterY));
                }
            Sample = CodedClamp(Distance);
        }
        else
        {
            double Accumulated = 0, Amplitude = .5, Sum = 0;
            for (int Octave = 0; Octave < 4; ++Octave)
            {
                const double Rate = std::ldexp(1., Octave);
                Accumulated += CodedSmoothNoise(PositionX * Rate, PositionY * Rate, Parameters.Seed + Octave) * Amplitude;
                Sum += Amplitude;
                Amplitude *= .5;
            }
            const double Fractal = Accumulated / Sum;
            if (Identity == "ridged") Sample = 1 - std::abs(Fractal * 2 - 1);
            else if (Identity == "rings")
                Sample = .5 + .5 * std::sin(std::hypot(Horizontal, Vertical) * Frequency * 3.141592653589793 * 2 +
                             Fractal * 2 + Parameters.Seed);
            else Sample = Fractal;
        }
    }
    return std::isfinite(Sample) ? CodedClamp(Sample) : 0;
}

double EvaluateCodedFieldPosition(std::string_view Identity, const CodedFieldParameters &Parameters,
    double U, double V, double AngleDegrees, double Amount)
{
    const double Radians = AngleDegrees * 3.141592653589793 / 180;
    const double RotatedX = (U - .5) * std::cos(Radians) - (V - .5) * std::sin(Radians);
    const double RotatedY = (U - .5) * std::sin(Radians) + (V - .5) * std::cos(Radians);
    const double Sample = EvaluateCodedField(Identity, Parameters, RotatedX, RotatedY);
    return .5 + (Sample - .5) * Amount;
}

std::vector<std::string> QueryProceduralOps()
{
    std::vector<std::string> Ops{"scratches", "metalGrain", "polymerGrain", "ribbedSheet", "solar", "dimples",
        "stucco", "weave", "knit", "scratchedMetal", "latheMetal", "clay"};
    for (const char *Identity : CodedFieldIdentities) Ops.emplace_back(Identity);
    return Ops;
}

std::vector<std::string> QueryProceduralOpChannels(std::string_view Op)
{
    if (Op == "scratches") return {"Orm", "Normal"};
    // 📝 Alloy varies no roughness term for bare metal, so metalGrain offers no ORM channel; polymer grain
    // modulates roughness and earns all three.
    if (Op == "metalGrain") return {"Albedo", "Normal"};
    if (Op == "polymerGrain") return {"Albedo", "Orm", "Normal"};
    if (Op == "ribbedSheet") return {"Albedo", "Orm", "Normal"};
    // 📝 Alloy varies no ORM term for dimpled covers, stucco, clay aggregate or yarn construction, so those
    // ops offer albedo plus normals; solar contacts and scratch overlays earn the full triple.
    if (Op == "solar" || Op == "scratchedMetal" || Op == "latheMetal") return {"Albedo", "Orm", "Normal"};
    if (Op == "dimples" || Op == "stucco" || Op == "weave" || Op == "knit" || Op == "clay") return {"Albedo", "Normal"};
    // 📝 Coded fields bake colour (palette/ramp projection) and relief normals; ORM stays scalar-authored
    // because the HTML field carries no roughness ceiling convention the ORM ratio could divide by.
    if (QueryCodedField(Op)) return {"Albedo", "Normal"};
    return {};
}

float QueryProceduralOpCeilingLift(std::string_view Op)
{
    if (Op == "scratches") return .3f;
    if (Op == "polymerGrain") return .096f;
    if (Op == "ribbedSheet") return .35f;
    // 📝 Solar contacts lift roughness from the .2 base to .22; metal scratch overlays reuse the study's
    // +0.3 ceiling so the shared roughness slider keeps one convention across the family.
    if (Op == "solar") return .02f;
    if (Op == "scratchedMetal" || Op == "latheMetal") return .3f;
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
    // 📝 Rib ranges mirror the Alloy sheet controls (ribDepth/ribProfile/detailScale/weaveAngle/sheetSpangle/sheetOxidation).
    if (Op == "ribbedSheet")
        return {{"ribs", "Ribs per tile", 1, 12, 4, false, "", 1},
            {"ribDepth", "Rib depth", 0, .4f, .2f, false, "scene units", .01f},
            {"ribProfile", "Rib profile", 0, 1, 0, false, "0 round - 1 IBR", 1},
            {"weaveAngle", "Rib direction", -180, 180, 0, false, "deg", 1},
            {"sheetSpangle", "Spangle character", 0, 1, .3f, false, "", .01f},
            {"sheetOxidation", "Weathering", 0, 1, .1f, false, "", .01f},
            {"roughness", "Surface roughness", .045f, 1, .4f, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    // 📝 Coded-field ranges mirror the HTML generator sheet (MaterialPanel GeneratorInputs limits).
    if (QueryCodedField(Op))
    {
        std::vector<ProceduralOpParam> Params;
        Params.reserve(5);
        Params.emplace_back(ProceduralOpParam{"scale", "Scale", 1, 200, 8, false, "", 1});
        Params.emplace_back(ProceduralOpParam{"seed", "Seed", 0, 9999, 12, false, "", 1});
        Params.emplace_back(ProceduralOpParam{"angle", "Angle", -180, 180, 0, false, "deg", 1});
        Params.emplace_back(ProceduralOpParam{"amount", Op.starts_with("camouflage") ? "Patch contrast" : "Amount", 0, 1, 1, false, "", .01f});
        if (Op == "camouflage-smooth") Params.emplace_back(ProceduralOpParam{"softness", "Softness", .001f, .1f, .012f, false, "", .001f});
        if (Op == "camouflage") Params.emplace_back(ProceduralOpParam{"pixelSize", "Pixel size", 1, 16, 4, false, "", 1});
        return Params;
    }
    return {};
}

std::vector<ProceduralOpTextParam> QueryProceduralOpTextParams(std::string_view Op)
{
    if (Op == "ribbedSheet") return {{"color", "Sheet colour", "#b5c0c2"}};
    if (Op == "camouflage" || Op == "camouflage-smooth")
        return {{"colour0", "Sand colour", "#b5a477"}, {"colour1", "Olive colour", "#566044"},
            {"colour2", "Earth colour", "#514334"}, {"colour3", "Shadow colour", "#232921"}};
    return {};
}

std::string ResolveProceduralParamKey(std::string_view Op, std::string_view Key)
{
    if (Op == "scratches" && (Key == "weaveAngle" || Key == "scratchAngle")) return "angle";
    // 📝 Metal scratch overlays store the same canonical angle; their inspector addresses it as the
    // independent scratch direction, never the construction thread direction.
    if ((Op == "scratchedMetal" || Op == "latheMetal") && Key == "scratchAngle") return "angle";
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

std::vector<uint8_t> ConvertRibbedOrmRgba(const std::vector<float> &Roughness, const std::vector<float> &Occlusion,
    const std::vector<float> &Metallic, uint32_t Extent, float Ceiling)
{
    if (!Extent || Roughness.size() != size_t(Extent) * Extent || Occlusion.size() != Roughness.size() ||
        Metallic.size() != Roughness.size() || !(Ceiling > 0) || !(Ceiling <= 1))
        throw std::runtime_error("Ribbed ORM source is not square fields over a unit ceiling.");
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Index = 0; Index < Roughness.size(); ++Index)
    {
        uint8_t *Pixel = &Rgba[Index * 4];
        Pixel[0] = uint8_t(std::lround(std::clamp(Occlusion[Index], 0.f, 1.f) * 255));
        Pixel[1] = uint8_t(std::lround(std::clamp(Roughness[Index] / Ceiling, 0.f, 1.f) * 255));
        Pixel[2] = uint8_t(std::lround(std::clamp(Metallic[Index], 0.f, 1.f) * 255));
        Pixel[3] = 255;
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

std::vector<uint8_t> ConvertSolarOrmRgba(const std::vector<float> &Roughness, const std::vector<float> &Metallic,
    uint32_t Extent, float Ceiling)
{
    if (!Extent || Roughness.size() != size_t(Extent) * Extent || Metallic.size() != Roughness.size() ||
        !(Ceiling > 0) || !(Ceiling <= 1))
        throw std::runtime_error("Solar ORM source is not square fields over a unit ceiling.");
    // 📝 Green divides by the contact ceiling like the scratch convention; blue carries Alloy's absolute
    // cell/contact metallic mix, so the authored scalar stays 1.
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Index = 0; Index < Roughness.size(); ++Index)
    {
        uint8_t *Pixel = &Rgba[Index * 4];
        Pixel[0] = 255;
        Pixel[1] = uint8_t(std::lround(std::clamp(Roughness[Index] / Ceiling, 0.f, 1.f) * 255));
        Pixel[2] = uint8_t(std::lround(std::clamp(Metallic[Index], 0.f, 1.f) * 255));
        Pixel[3] = 255;
    }
    return Rgba;
}

int WeavePatternIndex(std::string_view Pattern)
{
    if (Pattern == "plain") return 0;
    if (Pattern == "twill") return 1;
    if (Pattern == "satin") return 2;
    if (Pattern == "basket") return 3;
    if (Pattern == "rib") return 4;
    if (Pattern == "herringbone") return 5;
    if (Pattern == "oxford") return 6;
    if (Pattern == "houndstooth") return 7;
    if (Pattern == "denim") return 8;
    throw std::runtime_error("Weave pattern is not a known construction.");
}

std::vector<uint8_t> BakeProceduralChannelRgba(std::string_view Op, std::string_view Channel,
    const std::map<std::string, double> &Params, uint32_t Extent)
{
    return BakeProceduralChannelRgba(Op, Channel, Params, {}, Extent);
}

std::vector<uint8_t> BakeProceduralChannelRgba(std::string_view Op, std::string_view Channel,
    const std::map<std::string, double> &Params, const std::map<std::string, std::string> &Texts, uint32_t Extent)
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
    if (Op == "ribbedSheet")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Ribbed-sheet bake extent is unbounded.");
        const double Ribs = std::clamp(Value("ribs", 4), 1., 12.);
        const double Depth = std::clamp(Value("ribDepth", .2), 0., .4);
        // 📝 Alloy ribDepth is scene units against a .2 reference: full relief at .2, flat at 0.
        const float Amplitude = Depth <= 0 ? 0 : std::clamp(float(Depth * 5.), .3f, 1.f);
        const bool Trapezoid = Value("ribProfile", 0) > .5;
        const double Radians = Value("weaveAngle", 0) * 3.141592653589793 / 180;
        const float CosA = float(std::cos(Radians)), SinA = float(std::sin(Radians));
        const float Spangle = float(std::clamp(Value("sheetSpangle", .3), 0., 1.));
        const float Oxidation = float(std::clamp(Value("sheetOxidation", .1), 0., 1.));
        const float Base = float(std::clamp(Value("roughness", .4), .045, 1.));
        const float Seed = float(Value("surfaceSeed", 17));
        float SheetRgb[3]{1, 1, 1};
        if (Channel == "Albedo")
        {
            const auto Found = Texts.find("color");
            if (!DecodeHexLinearRgb(Found != Texts.end() ? Found->second : "#b5c0c2", SheetRgb))
                throw std::runtime_error("Ribbed-sheet colour is not #rrggbb.");
        }
        std::vector<float> Height(size_t(Extent) * Extent), Roughness(size_t(Extent) * Extent),
            Occlusion(size_t(Extent) * Extent), Metal(size_t(Extent) * Extent),
            Linear(size_t(Extent) * Extent * 3);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const float Along = U * CosA + V * SinA;
                float Phase = float(Along * Ribs);
                Phase -= std::floor(Phase);
                const float Sine = std::sin(Phase * 2 * 3.141592653589793f);
                const float Rib = Trapezoid ? std::copysign(std::pow(std::abs(Sine), .55f), Sine) : Sine;
                const float Relief01 = Rib * .5f + .5f;
                const float Mu = ProceduralMirror(U * 3 + Seed * .13f), Mv = ProceduralMirror(V * 3 + Seed * .29f);
                const float SpangleField = ProceduralStoneFbm(Mu * 4 + Seed, Mv * 4, Seed * 1.7f);
                const float Oxide = ProceduralSmoothstep(.68f, .95f,
                    ProceduralStoneFbm(Mu * 5 + Seed + 13.7f, Mv * 5 + 7.3f, Seed + 5.1f));
                const float Ao = .88f + .12f * Relief01;
                const float Tone = std::max(0.f,
                    Ao * (1 + (SpangleField - .5f) * .14f * Spangle) * (1 - Oxide * .5f * Oxidation));
                const size_t Index = size_t(Y) * Extent + X;
                Height[Index] = Rib * Amplitude;
                Roughness[Index] =
                    std::clamp(Base + (SpangleField - .5f) * .1f * Spangle + Oxide * .3f * Oxidation, .05f, 1.f);
                Occlusion[Index] = Ao;
                Metal[Index] = std::clamp(1 - Oxide * Oxidation, 0.f, 1.f);
                Linear[Index * 3] = SheetRgb[0] * Tone;
                Linear[Index * 3 + 1] = SheetRgb[1] * Tone;
                Linear[Index * 3 + 2] = SheetRgb[2] * Tone;
            }
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Linear, Extent);
        if (Channel == "Orm")
        {
            const float Ceiling = std::min(1.f, Base + .05f * Spangle + .3f * Oxidation);
            return ConvertRibbedOrmRgba(Roughness, Occlusion, Metal, Extent, Ceiling);
        }
        return ConvertHeightToNormalRgba(Height, Extent, 6.f);
    }
    if (QueryCodedField(Op))
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Coded field bake extent is unbounded.");
        CodedFieldParameters Field;
        Field.Scale = Value("scale", 8);
        Field.Seed = Value("seed", 12);
        Field.Softness = Value("softness", .012);
        Field.PixelSize = Value("pixelSize", 4);
        const double Angle = Value("angle", 0), Amount = Value("amount", 1);
        // 📝 Palette colours decode like THREE.Color(hex): sRGB text into linear working triples, then the
        // shared albedo converter sRGB-encodes for upload, so the authored hex survives the round trip.
        std::vector<std::array<float, 3>> Palette;
        for (const auto &TextParam : QueryProceduralOpTextParams(Op))
        {
            const auto Found = Texts.find(TextParam.Key);
            float Rgb[3]{1, 1, 1};
            if (!DecodeHexLinearRgb(Found != Texts.end() ? Found->second : TextParam.Default, Rgb))
                throw std::runtime_error("Coded field palette colour is not #rrggbb.");
            Palette.push_back({Rgb[0], Rgb[1], Rgb[2]});
        }
        Field.ColourCount = Palette.empty() ? 4 : uint32_t(Palette.size());
        std::vector<float> Positions(size_t(Extent) * Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
                Positions[size_t(Y) * Extent + X] = float(EvaluateCodedFieldPosition(Op, Field,
                    double(X) / Extent, double(Y) / Extent, Angle, Amount));
        if (Channel == "Normal") return ConvertHeightToNormalRgba(Positions, Extent, 2.f);
        std::vector<float> Linear(size_t(Extent) * Extent * 3);
        for (size_t Index = 0; Index < Positions.size(); ++Index)
        {
            const double Position = std::clamp(double(Positions[Index]), 0., 1.);
            std::array<float, 3> Colour{float(Position), float(Position), float(Position)};
            if (!Palette.empty())
            {
                const double Slot = Position * double(Palette.size() - 1);
                if (Op == "camouflage-smooth")
                {
                    const size_t First = std::min(Palette.size() - 2, size_t(std::floor(Slot)));
                    const float Blend = float(Slot - double(First));
                    for (size_t Component = 0; Component < 3; ++Component)
                        Colour[Component] = Palette[First][Component] +
                            (Palette[First + 1][Component] - Palette[First][Component]) * Blend;
                }
                else Colour = Palette[size_t(std::lround(Slot))];
            }
            Linear[Index * 3] = Colour[0];
            Linear[Index * 3 + 1] = Colour[1];
            Linear[Index * 3 + 2] = Colour[2];
        }
        return ConvertLinearAlbedoRgba(Linear, Extent);
    }
    if (Op == "solar")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Solar bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float BusWidth = float(Value("busbarWidth", .016));
        const double PanelMode = Value("panelMode", 0);
        const float Base = float(std::clamp(Value("roughness", .2), .045, 1.));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural solar params cannot bake.");
        float CellRgb[3]{1, 1, 1}, ContactRgb[3]{1, 1, 1};
        const auto CellText = Texts.find("color"), ContactText = Texts.find("secondaryColor");
        if (!DecodeHexLinearRgb(CellText != Texts.end() ? CellText->second : "#172742", CellRgb) ||
            !DecodeHexLinearRgb(ContactText != Texts.end() ? ContactText->second : "#bec6c4", ContactRgb))
            throw std::runtime_error("Solar cell colours are not #rrggbb.");
        // 📝 Single-face bake of the uType==17 block: cell separators, triple busbars, collection fingers
        // and the polycrystalline Worley grain, with contact-driven roughness and metallic answers.
        std::vector<float> Linear(size_t(Extent) * Extent * 3), Roughness(size_t(Extent) * Extent),
            Metal(size_t(Extent) * Extent), Height(size_t(Extent) * Extent);
        const float Step = Tiles / float(Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float UvX = (float(X) + .5f) / float(Extent) * Tiles, UvY = (float(Y) + .5f) / float(Extent) * Tiles;
                const float Qx = ProceduralFract(UvX), Qy = ProceduralFract(UvY);
                const float Edge = std::min(std::min(Qx, 1 - Qx), std::min(Qy, 1 - Qy));
                const float CellMask = ProceduralSmoothstep(.013f, .013f + std::max(Step, .004f), Edge);
                const float Bus = 1 - ProceduralSmoothstep(BusWidth, BusWidth + std::max(Step, .002f),
                    std::abs(ProceduralFract(UvX * 3) - .5f) / 3);
                const float Fingers = ProceduralMix(.05f,
                    1 - ProceduralSmoothstep(.025f, .025f + std::max(Step * 36, .025f),
                        std::abs(ProceduralFract(UvY * 36) - .5f)),
                    1 - ProceduralSmoothstep(.2f, .8f, Step * 36));
                const float Contact = CellMask * std::max(Bus, Fingers * .7f);
                float Silicon[3]{CellRgb[0], CellRgb[1], CellRgb[2]};
                if (PanelMode > .5 && PanelMode < 1.5)
                {
                    float Crystal[4]{0, 0, 0, 0};
                    EvaluateCellular3(UvX * 9, UvY * 9, .3f, 7, Crystal);
                    for (int Component = 0; Component < 3; ++Component) Silicon[Component] *= .65f + Crystal[1] * .75f;
                }
                const size_t Index = size_t(Y) * Extent + X;
                for (int Component = 0; Component < 3; ++Component)
                    Linear[Index * 3 + size_t(Component)] = ProceduralMix(
                        ProceduralMix(.008f, Silicon[Component], CellMask), ContactRgb[Component], Contact);
                Roughness[Index] = ProceduralMix(Base, .22f, Contact);
                Metal[Index] = ProceduralMix(.25f, .96f, Contact);
                Height[Index] = CellMask * .001f + Contact * .0007f;
            }
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Linear, Extent);
        if (Channel == "Orm") return ConvertSolarOrmRgba(Roughness, Metal, Extent, std::min(1.f, Base + .02f));
        for (float &Relief : Height) Relief /= .0017f;
        return ConvertHeightToNormalRgba(Height, Extent, 2.f);
    }
    if (Op == "dimples")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Dimple bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float Depth = float(Value("dimpleDepth", 0));
        const float Seed = float(Value("surfaceSeed", 0));
        if (!(Tiles > 0) || !(Depth > 0)) throw std::runtime_error("Procedural dimple params cannot bake.");
        // 📝 Single-face bake of the uType==18 block; the albedo carries the cover grain modulator under
        // the authored colour, like the bare-metal tooth.
        std::vector<float> Field(size_t(Extent) * Extent);
        const float Step = Tiles / float(Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const float Bowl = EvaluateDimple(U, V, Tiles, Step);
                Field[size_t(Y) * Extent + X] =
                    Channel == "Albedo" ? .97f + ProceduralNoise3(U * Tiles, Seed, V * Tiles) * .04f : -Bowl * Depth / Depth;
            }
        if (Channel == "Albedo") return ConvertGrainAlbedoRgba(Field, Extent);
        return ConvertHeightToNormalRgba(Field, Extent, 2.f);
    }
    if (Op == "stucco")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Stucco bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float Grain = float(Value("grain", 0));
        const float Seed = float(Value("surfaceSeed", 0));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural stucco params cannot bake.");
        // 📝 Single-face bake of the uType==26 block: sand aggregate over a broad cloud drift; the fixed
        // .011 nominal keeps the grain slider driving bump strength instead of normalizing it away.
        std::vector<float> Field(size_t(Extent) * Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const float Sand = ProceduralStoneFbm(U * Tiles, Seed, V * Tiles);
                const float Cloud = ProceduralStoneFbm(U * 2.8f, Seed, V * 2.8f);
                Field[size_t(Y) * Extent + X] = Channel == "Albedo" ? .8f + Cloud * .28f + Sand * .09f
                    : (Sand - .5f) * Grain * .022f / .011f;
            }
        if (Channel == "Albedo") return ConvertGrainAlbedoRgba(Field, Extent);
        return ConvertHeightToNormalRgba(Field, Extent, 2.f);
    }
    if (Op == "clay")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Clay bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float Grain = float(Value("grain", 0));
        const float Moisture = float(Value("moisture", 0));
        const float Seed = float(Value("surfaceSeed", 0));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural clay params cannot bake.");
        // 📝 Single-face bake of the uType==12 block: fine aggregate over finger-scale hand variation, with
        // moisture darkening the tone and damping the relief. Relief keeps the shader's grain/moisture
        // relationship at the baker's unit nominal (the .006 scene factor belongs to the GL bump).
        std::vector<float> Field(size_t(Extent) * Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const float Aggregate = ProceduralNoise3(U * Tiles, Seed, V * Tiles) +
                    .35f * ProceduralNoise3(U * Tiles * 3.7f, Seed, V * Tiles * 3.7f);
                const float Hand = ProceduralNoise3(U * 2.7f, Seed, V * 2.7f);
                Field[size_t(Y) * Extent + X] = Channel == "Albedo"
                    ? (.8f + .23f * Aggregate + .08f * Hand) * (1 - Moisture * .18f)
                    : (Aggregate - .5f) * Grain * 2 * (1 - Moisture * .5f);
            }
        if (Channel == "Albedo") return ConvertGrainAlbedoRgba(Field, Extent);
        return ConvertHeightToNormalRgba(Field, Extent, 2.f);
    }
    if (Op == "weave")
    {
        WeaveFieldParameters P;
        P.Type = 4;
        const auto Pattern = Texts.find("weavePattern");
        P.Weave = WeavePatternIndex(Pattern != Texts.end() ? Pattern->second : "twill");
        P.FabricMode = int(Value("fabricMode", 0));
        P.Scale = float(Value("detailScale", 0));
        P.AngleRadians = float(Value("weaveAngle", 0) * 3.141592653589793 / 180);
        P.Relief = float(Value("weaveRelief", 0));
        P.FiberDetail = float(Value("fiberDetail", 12));
        P.Fuzz = float(Value("fuzz", 0));
        P.Slub = float(Value("slub", 0));
        P.DenimFade = float(Value("denimFade", 0));
        P.Grain = float(Value("grain", 0));
        P.Seed = float(Value("surfaceSeed", 0));
        const auto WarpText = Texts.find("warpColor"), WeftText = Texts.find("weftColor");
        if (!DecodeHexLinearRgb(WarpText != Texts.end() ? WarpText->second : "#b5b0a2", P.Warp) ||
            !DecodeHexLinearRgb(WeftText != Texts.end() ? WeftText->second : "#b5b0a2", P.Weft))
            throw std::runtime_error("Weave yarn colours are not #rrggbb.");
        auto Maps = BakeWeaveMaps(P, Extent);
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Maps.Albedo, Extent);
        const float Amplitude = WeaveReliefAmplitude(P);
        if (Amplitude > 0)
            for (float &Relief : Maps.Relief) Relief /= Amplitude;
        return ConvertHeightToNormalRgba(Maps.Relief, Extent, 2.f);
    }
    if (Op == "knit")
    {
        KnitFieldParameters P;
        P.Scale = float(Value("detailScale", 0));
        P.Grain = float(Value("grain", 0));
        P.Fuzz = float(Value("fuzz", 0));
        P.Seed = float(Value("surfaceSeed", 0));
        P.AngleRadians = float(Value("weaveAngle", 0) * 3.141592653589793 / 180);
        const auto DyeText = Texts.find("color"), MelangeText = Texts.find("secondaryColor");
        if (!DecodeHexLinearRgb(DyeText != Texts.end() ? DyeText->second : "#8d9294", P.Color) ||
            !DecodeHexLinearRgb(MelangeText != Texts.end() ? MelangeText->second : "#e2ddd0", P.Secondary))
            throw std::runtime_error("Knit yarn colours are not #rrggbb.");
        auto Maps = BakeKnitMaps(P, Extent);
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Maps.Albedo, Extent);
        const float Amplitude = KnitReliefAmplitude(P);
        if (Amplitude > 0)
            for (float &Relief : Maps.Relief) Relief /= Amplitude;
        return ConvertHeightToNormalRgba(Maps.Relief, Extent, 2.f);
    }
    if (Op == "scratchedMetal" || Op == "latheMetal")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Metal bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural metal params cannot bake.");
        const float Seed = float(Value("surfaceSeed", 0));
        const float Base = float(std::clamp(Value("roughness", .3), .045, 1.));
        ScratchFieldParameters Cuts;
        Cuts.Density = float(Value("scratchDensity", 0));
        Cuts.Length = float(Value("scratchLength", 0));
        Cuts.Width = float(Value("scratchWidth", 0));
        Cuts.Depth = float(Value("scratchDepth", 0));
        Cuts.Spread = float(Value("scratchSpread", 0));
        Cuts.Bend = float(Value("scratchBend", 0));
        Cuts.Scale = float(Value("scratchScale", 1));
        Cuts.AngleRadians = float(Value("angle", 0) * 3.141592653589793 / 180);
        Cuts.Seed = Value("surfaceSeed", 0) < 0 ? 0 : uint32_t(Value("surfaceSeed", 0));
        if (Cuts.Density > 0 && (!(Cuts.Depth > 0) || !(Cuts.Scale > 0)))
            throw std::runtime_error("Procedural scratch params cannot bake.");
        // 📝 Bare type-1 tooth (or type-6 lathe rings) under the optional study cuts. Uncut metal matches
        // the metalGrain bytes exactly; the shared .004 nominal preserves Alloy's tooth-to-cut ratio.
        const float Tooth = Op == "scratchedMetal" ? float(Value("grain", 0)) : 0;
        const float Depth = Op == "latheMetal" ? float(Value("depth", 0)) : 0;
        const float RingStep = Tiles * 3.3f / float(Extent);
        std::vector<float> Field(size_t(Extent) * Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const auto Cut = EvaluateScratchField(U, V, Cuts, Cuts.Scale / float(Extent));
                const size_t Index = size_t(Y) * Extent + X;
                if (Channel == "Orm")
                {
                    Field[Index] = Cut.Coverage;
                    continue;
                }
                if (Op == "scratchedMetal")
                {
                    const auto Bare = EvaluateMetalGrain(U, V, Tooth, Tiles, Seed);
                    Field[Index] = Channel == "Albedo" ? Bare.Albedo : Bare.Relief + Cut.Height / .004f;
                    continue;
                }
                const float Rx = (U - .5f) * Tiles, Ry = (V - .5f) * Tiles;
                const float Phase = std::sqrt(Rx * Rx + Ry * Ry) * 3.3f;
                const float Lines =
                    std::sin(Phase) * .5f * (1 - ProceduralSmoothstep(.5f, 3.1f, RingStep)) + .5f;
                Field[Index] = Channel == "Albedo" ? .84f + Lines * .14f : (Lines * .0001f * Depth + Cut.Height) / .004f;
            }
        if (Channel == "Albedo") return ConvertGrainAlbedoRgba(Field, Extent);
        if (Channel == "Orm") return ConvertScratchOrmRgba(Field, Extent, Base);
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

static void AppendProceduralPngBytes(void *Context, void *Data, int Size)
{
    auto *Png = static_cast<std::vector<uint8_t> *>(Context);
    const auto *Bytes = static_cast<const uint8_t *>(Data);
    Png->insert(Png->end(), Bytes, Bytes + Size);
}

std::vector<uint8_t> EncodeProceduralPng(const std::vector<uint8_t> &Rgba, uint32_t Extent)
{
    if (!Extent || Rgba.size() != size_t(Extent) * Extent * 4) throw std::runtime_error("Procedural PNG source is not square RGBA.");
    std::vector<uint8_t> Png;
    if (!stbi_write_png_to_func(AppendProceduralPngBytes, &Png, int(Extent), int(Extent), 4, Rgba.data(), 0)) throw std::runtime_error("Procedural PNG encode failed.");
    return Png;
}
}
