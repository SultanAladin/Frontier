//==========================================================================================================================================
//                                                      PHASE3PROOFIMAGES.CPP
//==========================================================================================================================================
//  Proof images for the Phase 3 ports: a small host-side renderer that draws what the ported code
//  actually produces — the spark sequence's instance table, the effect sequence's anchors — with no
//  GPU, no device, and no re-implementation of any physics. Every spark streak in these pictures is a
//  row of SparkSequence::Instance(); every dot over the car is a row of SpreadEffectCells().
//
//  Build:
//      g++ -std=c++20 -O2 -Wall -Wextra -I. -IFrontier Validation/Phase3ProofImages.cpp
//          Frontier/Engine/Rendering/SparkCollision.cpp Frontier/Engine/Rendering/SparkSequence.cpp
//          Frontier/Engine/Rendering/EffectSequence.cpp
//          Frontier/Engine/Diagnostics/LogCategories.cpp Frontier/Engine/Diagnostics/LogStream.cpp
//          Frontier/Engine/Diagnostics/LogRingStore.cpp -o /tmp/phase3proof
//  Run:
//      /tmp/phase3proof [output directory]        (default VisualProof/Phase3)
//==========================================================================================================================================

#include "Frontier/Engine/Rendering/EffectSequence.h"
#include "Frontier/Engine/Rendering/SparkCollision.h"
#include "Frontier/Engine/Rendering/SparkSequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;

using Rgb = std::array<float, 3>;

//======================================================================================================================================
//                                                          THE FRAMEBUFFER
//======================================================================================================================================

// 📝 Linear-light accumulators: a fresh spark is hundreds of times brighter than the road it flies
//    over, so the buffer is float and tone mapping happens once, at the end.
struct Framebuffer
{
    int Width = 0;
    int Height = 0;
    std::vector<float> Colour;
    std::vector<float> Depth;

    Framebuffer(int InWidth, int InHeight)
        : Width(InWidth),
          Height(InHeight),
          Colour(size_t(InWidth) * size_t(InHeight) * 3u, 0.0f),
          Depth(size_t(InWidth) * size_t(InHeight), 1.0e30f)
    {
    }
};

//======================================================================================================================================
//                                                           THE CAMERA
//======================================================================================================================================

struct Camera
{
    Vec3 Eye = { 6.0f, -6.0f, 2.5f };
    Vec3 Target = { 0.0f, 0.0f, 0.4f };
    Vec3 Up = { 0.0f, 0.0f, 1.0f };
    float FieldOfView = 0.85f;             // [rad] vertical
    int Width = 1280;
    int Height = 800;

    Vec3 Forward = {};
    Vec3 Side = {};
    Vec3 Rise = {};
    float Aspect = 1.6f;

    void Build() noexcept
    {
        Forward = Normalise(Target - Eye);
        Side = Normalise(Cross(Forward, Up));
        Rise = Cross(Side, Forward);
        Aspect = float(Width) / float(Height);
    }

    // 📝 Projects to pixel coordinates (Y down) plus a view-space depth in metres. False behind the eye.
    [[nodiscard]] bool Project(Vec3 Point, float& OutX, float& OutY, float& OutDepth) const noexcept
    {
        const Vec3 Offset = Point - Eye;
        const float Ahead = Dot(Offset, Forward);
        if (Ahead < 0.05f)
            return false;
        const float Tangent = std::tan(FieldOfView * 0.5f);
        OutX = (Dot(Offset, Side) / (Ahead * Tangent * Aspect) * 0.5f + 0.5f) * float(Width);
        OutY = (0.5f - Dot(Offset, Rise) / (Ahead * Tangent) * 0.5f) * float(Height);
        OutDepth = Ahead;
        return true;
    }

    [[nodiscard]] float HorizonRow(int BufferHeight) const noexcept
    {
        float X, Y, Depth;
        if (!Project(Eye + Forward * 1000.0f, X, Y, Depth))
            return float(BufferHeight) * 0.5f;
        return std::clamp(Y, 0.0f, float(BufferHeight));
    }
};

//======================================================================================================================================
//                                                          THE RASTERIZER
//======================================================================================================================================

// 📝 Two passes share one buffer: opaque geometry writes colour and depth, then the emissive pass adds
//    into it with a depth test, which is exactly the order the renderer draws them in.
class Painter
{
  public:
    Painter(int Width, int Height)
        : Surface(Width, Height)
    {
    }

    void Begin(const Camera& View, bool Additive) noexcept
    {
        Eye = View;
        AdditivePass = Additive;
    }

    void Backdrop(const Rgb& SkyTop, const Rgb& SkyBottom, const Rgb& Ground) noexcept
    {
        const float Horizon = Eye.HorizonRow(Surface.Height);
        for (int Y = 0; Y < Surface.Height; ++Y)
            for (int X = 0; X < Surface.Width; ++X)
            {
                float* Pixel = Surface.Colour.data() + (size_t(Y) * Surface.Width + X) * 3u;
                if (float(Y) >= Horizon)
                {
                    const float Fade = std::clamp((float(Y) - Horizon) / std::max(float(Surface.Height) - Horizon, 1.0f), 0.0f, 1.0f);
                    const float Shade = 0.30f + 0.70f * Fade;
                    for (uint32_t Channel = 0u; Channel < 3u; ++Channel)
                        Pixel[Channel] = Ground[Channel] * Shade;
                }
                else
                {
                    const float Mix = std::clamp(float(Y) / std::max(Horizon, 1.0f), 0.0f, 1.0f);
                    for (uint32_t Channel = 0u; Channel < 3u; ++Channel)
                        Pixel[Channel] = SkyTop[Channel] + (SkyBottom[Channel] - SkyTop[Channel]) * Mix;
                }
            }
    }

    // 📝 A filled, z-buffered triangle, lit by one key light and a sky term. The normal is the face's
    //    own, so the shading is flat — enough to read the geometry, and it costs nothing.
    void Triangle(Vec3 A, Vec3 B, Vec3 C, const Rgb& Colour) noexcept
    {
        const Vec3 U = B - A;
        const Vec3 V = C - A;
        const Vec3 Face = Cross(U, V);
        const float Span = Length(Face);
        float Shade = 1.0f;
        if (Span > 1.0e-9f)
        {
            const Vec3 Normal = Face * (1.0f / Span);
            const float Key = std::max(0.0f, Frontier::Dot(Normal, KeyLight()));
            const float Sky = 0.5f + 0.5f * Normal.Z;
            Shade = Ambient + KeyLightStrength * Key + SkyStrength * Sky;
        }
        const Rgb Lit = { Colour[0] * Shade, Colour[1] * Shade, Colour[2] * Shade };

        float Ax, Ay, Az, Bx, By, Bz, Cx, Cy, Cz;
        if (!Eye.Project(A, Ax, Ay, Az) || !Eye.Project(B, Bx, By, Bz) || !Eye.Project(C, Cx, Cy, Cz))
            return;

        const float Area = (Bx - Ax) * (Cy - Ay) - (By - Ay) * (Cx - Ax);
        if (std::fabs(Area) < 1.0e-9f)
            return;

        const int LowX = std::max(0, int(std::min({ Ax, Bx, Cx })));
        const int HighX = std::min(Surface.Width - 1, int(std::max({ Ax, Bx, Cx })));
        const int LowY = std::max(0, int(std::min({ Ay, By, Cy })));
        const int HighY = std::min(Surface.Height - 1, int(std::max({ Ay, By, Cy })));

        for (int Y = LowY; Y <= HighY; ++Y)
            for (int X = LowX; X <= HighX; ++X)
            {
                const float Px = float(X) + 0.5f - Ax;
                const float Py = float(Y) + 0.5f - Ay;
                const float WeightB = (Px * (Cy - Ay) - Py * (Cx - Ax)) / Area;
                const float WeightC = ((Bx - Ax) * Py - (By - Ay) * Px) / Area;
                const float WeightA = 1.0f - WeightB - WeightC;
                if (WeightA < 0.0f || WeightB < 0.0f || WeightC < 0.0f)
                    continue;
                Splat(X, Y, WeightA * Az + WeightB * Bz + WeightC * Cz, Lit, 1.0f);
            }
    }

    // 📝 A thick, anti-aliased world-space line: how a spark's streak is drawn. The width is in pixels,
    //    so a streak stays thin and bright however close the camera is — the same reason SparkRaster
    //    clamps its width to 0.65–1.35 px rather than letting it grow.
    void Streak(Vec3 From, Vec3 To, const Rgb& Colour, float WidthPixels = 1.5f) noexcept
    {
        float Ax, Ay, Az, Bx, By, Bz;
        if (!Eye.Project(From, Ax, Ay, Az) || !Eye.Project(To, Bx, By, Bz))
            return;
        const float Dx = Bx - Ax;
        const float Dy = By - Ay;
        const float Span = std::sqrt(Dx * Dx + Dy * Dy);
        const float Radius = std::max(WidthPixels, 1.0f) * 0.5f;
        if (Span < 1.0e-3f)
        {
            Splat(int(Ax), int(Ay), Az, Colour, 1.0f);
            return;
        }

        const float UnitX = Dx / Span;
        const float UnitY = Dy / Span;
        const int LowX = int(std::min(Ax, Bx) - Radius - 1.0f);
        const int HighX = int(std::max(Ax, Bx) + Radius + 1.0f);
        const int LowY = int(std::min(Ay, By) - Radius - 1.0f);
        const int HighY = int(std::max(Ay, By) + Radius + 1.0f);

        for (int Y = LowY; Y <= HighY; ++Y)
            for (int X = LowX; X <= HighX; ++X)
            {
                const float Px = float(X) + 0.5f - Ax;
                const float Py = float(Y) + 0.5f - Ay;
                const float Along = std::clamp(Px * UnitX + Py * UnitY, 0.0f, Span);
                const float AcrossX = Px - UnitX * Along;
                const float AcrossY = Py - UnitY * Along;
                const float Coverage = std::clamp(Radius + 0.5f - std::sqrt(AcrossX * AcrossX + AcrossY * AcrossY), 0.0f, 1.0f);
                if (Coverage <= 0.0f)
                    continue;
                const float Fraction = Along / Span;
                Splat(X, Y, Az + (Bz - Az) * Fraction, Colour, Coverage);
            }
    }

    // 📝 A small filled dot in world space: one effect anchor over the car's surface.
    void Dot(Vec3 Centre, float RadiusPixels, const Rgb& Colour) noexcept
    {
        float X, Y, Depth;
        if (!Eye.Project(Centre, X, Y, Depth))
            return;
        const int Span = int(RadiusPixels + 1.0f);
        for (int OffsetY = -Span; OffsetY <= Span; ++OffsetY)
            for (int OffsetX = -Span; OffsetX <= Span; ++OffsetX)
            {
                const float Across = std::sqrt(float(OffsetX) * float(OffsetX) + float(OffsetY) * float(OffsetY));
                const float Coverage = std::clamp(RadiusPixels + 0.5f - Across, 0.0f, 1.0f);
                if (Coverage > 0.0f)
                    Splat(int(X) + OffsetX, int(Y) + OffsetY, Depth, Colour, Coverage);
            }
    }

    [[nodiscard]] const Framebuffer& Surface_() const noexcept { return Surface; }

    // 📝 One key light from above and behind the camera, plus a sky term: the same two-term model the
    //    renderer's direct pass uses before any of the indirect machinery.
    static constexpr float Ambient = 0.28f;
    static constexpr float KeyLightStrength = 0.80f;
    static constexpr float SkyStrength = 0.28f;
    [[nodiscard]] static Vec3 KeyLight() noexcept { return Normalise(Vec3{ 0.35f, -0.55f, 0.76f }); }

  private:
    void Splat(int X, int Y, float Depth, const Rgb& Colour, float Coverage) noexcept
    {
        if (X < 0 || Y < 0 || X >= Surface.Width || Y >= Surface.Height)
            return;
        const size_t Offset = size_t(Y) * size_t(Surface.Width) + size_t(X);
        if (!AdditivePass)
        {
            if (Depth >= Surface.Depth[Offset])
                return;
            Surface.Depth[Offset] = Depth;
            float* Pixel = Surface.Colour.data() + Offset * 3u;
            Pixel[0] = Colour[0];
            Pixel[1] = Colour[1];
            Pixel[2] = Colour[2];
            return;
        }
        // 📝 Emissive geometry is depth-tested but never writes depth, and never occludes itself.
        if (Depth > Surface.Depth[Offset] + 0.02f)
            return;
        float* Pixel = Surface.Colour.data() + Offset * 3u;
        Pixel[0] += Colour[0] * Coverage;
        Pixel[1] += Colour[1] * Coverage;
        Pixel[2] += Colour[2] * Coverage;
    }

    Camera Eye;
    Framebuffer Surface;
    bool AdditivePass = false;
};

//======================================================================================================================================
//                                                           PNG WRITER
//======================================================================================================================================

// 📝 A PNG writer with no dependency: the image goes out as zlib "stored" blocks (legal, uncompressed),
//    which is thirty lines of code instead of a linked zlib. Proof images are not large.
class PngWriter
{
  public:
    static bool Write(const std::string& Path, const Framebuffer& Surface, float Exposure) noexcept
    {
        std::vector<uint8_t> Pixels(size_t(Surface.Width) * size_t(Surface.Height) * 3u);
        for (size_t Index = 0u; Index < size_t(Surface.Width) * size_t(Surface.Height); ++Index)
            for (uint32_t Channel = 0u; Channel < 3u; ++Channel)
            {
                const float Linear = Surface.Colour[Index * 3u + Channel] * Exposure;
                const float Mapped = std::clamp(Linear / (1.0f + Linear), 0.0f, 1.0f);
                const float Encoded = Mapped <= 0.0031308f
                    ? Mapped * 12.92f
                    : 1.055f * std::pow(Mapped, 1.0f / 2.4f) - 0.055f;
                Pixels[Index * 3u + Channel] = uint8_t(std::clamp(Encoded, 0.0f, 1.0f) * 255.0f + 0.5f);
            }

        std::vector<uint8_t> Raw;
        Raw.reserve(size_t(Surface.Height) * (size_t(Surface.Width) * 3u + 1u));
        for (int Y = 0; Y < Surface.Height; ++Y)
        {
            Raw.push_back(0u);                                          // per-scanline filter: none
            Raw.insert(Raw.end(), Pixels.begin() + std::ptrdiff_t(size_t(Y) * Surface.Width * 3u),
                       Pixels.begin() + std::ptrdiff_t(size_t(Y + 1) * Surface.Width * 3u));
        }

        std::vector<uint8_t> Out(Signature, Signature + sizeof(Signature));
        std::vector<uint8_t> Header;
        PutBig(Header, uint32_t(Surface.Width));
        PutBig(Header, uint32_t(Surface.Height));
        Header.push_back(8u);                                           // bit depth
        Header.push_back(2u);                                           // colour type: truecolour
        Header.push_back(0u);                                           // deflate
        Header.push_back(0u);                                           // adaptive filtering
        Header.push_back(0u);                                           // no interlace
        Chunk(Out, "IHDR", Header);
        Chunk(Out, "IDAT", DeflateStored(Raw));
        Chunk(Out, "IEND", {});

        std::FILE* File = std::fopen(Path.c_str(), "wb");
        if (File == nullptr)
            return false;
        const bool Ok = std::fwrite(Out.data(), 1u, Out.size(), File) == Out.size();
        std::fclose(File);
        return Ok;
    }

  private:
    static constexpr uint8_t Signature[8] = { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };

    static void PutBig(std::vector<uint8_t>& Into, uint32_t Value) noexcept
    {
        Into.push_back(uint8_t(Value >> 24u));
        Into.push_back(uint8_t(Value >> 16u));
        Into.push_back(uint8_t(Value >> 8u));
        Into.push_back(uint8_t(Value));
    }

    static uint32_t Crc32(const uint8_t* Data, size_t Length) noexcept
    {
        uint32_t Running = 0xFFFFFFFFu;
        for (size_t Index = 0u; Index < Length; ++Index)
        {
            Running ^= Data[Index];
            for (uint32_t Bit = 0u; Bit < 8u; ++Bit)
                Running = (Running >> 1u) ^ (0xEDB88320u & (0u - (Running & 1u)));
        }
        return ~Running;
    }

    static void Chunk(std::vector<uint8_t>& Into, const char* Type, const std::vector<uint8_t>& Data) noexcept
    {
        PutBig(Into, uint32_t(Data.size()));
        std::vector<uint8_t> ForCrc(Type, Type + 4u);
        ForCrc.insert(ForCrc.end(), Data.begin(), Data.end());
        Into.insert(Into.end(), Type, Type + 4u);
        Into.insert(Into.end(), Data.begin(), Data.end());
        PutBig(Into, Crc32(ForCrc.data(), ForCrc.size()));
    }

    static std::vector<uint8_t> DeflateStored(const std::vector<uint8_t>& Raw) noexcept
    {
        std::vector<uint8_t> Out{ 0x78u, 0x01u };
        size_t Offset = 0u;
        do
        {
            const size_t Length = std::min<size_t>(Raw.size() - Offset, 65535u);
            const bool Final = Offset + Length >= Raw.size();
            Out.push_back(uint8_t(Final ? 1u : 0u));
            Out.push_back(uint8_t(Length));
            Out.push_back(uint8_t(Length >> 8u));
            Out.push_back(uint8_t(~Length));
            Out.push_back(uint8_t(~Length >> 8u));
            Out.insert(Out.end(), Raw.begin() + std::ptrdiff_t(Offset), Raw.begin() + std::ptrdiff_t(Offset + Length));
            Offset += Length;
        } while (Offset < Raw.size());

        uint32_t A = 1u;
        uint32_t B = 0u;
        for (uint8_t Byte : Raw)
        {
            A = (A + Byte) % 65521u;
            B = (B + A) % 65521u;
        }
        PutBig(Out, (B << 16u) | A);
        return Out;
    }
};

//======================================================================================================================================
//                                                             GEOMETRY
//======================================================================================================================================

struct Shape
{
    std::vector<Vec3> Triangles;
    std::vector<Rgb> TriangleColours;
    std::vector<EffectFacet> Facets;
};

[[nodiscard]] std::array<float, 3> ToArray(Vec3 Value) noexcept
{
    return { Value.X, Value.Y, Value.Z };
}

void AppendFacet(Shape& Into, Vec3 A, Vec3 B, Vec3 C, uint32_t Family, const Rgb& Colour) noexcept
{
    Into.Triangles.push_back(A);
    Into.Triangles.push_back(B);
    Into.Triangles.push_back(C);
    Into.TriangleColours.push_back(Colour);

    EffectFacet Facet;
    Facet.Corners[0] = ToArray(A);
    Facet.Corners[1] = ToArray(B);
    Facet.Corners[2] = ToArray(C);
    Facet.Family = Family;
    Into.Facets.push_back(Facet);
}

void AppendQuad(Shape& Into, Vec3 A, Vec3 B, Vec3 C, Vec3 D, uint32_t Family, const Rgb& Colour) noexcept
{
    AppendFacet(Into, A, B, C, Family, Colour);
    AppendFacet(Into, A, C, D, Family, Colour);
}

void AppendBox(Shape& Into, Vec3 Minimum, Vec3 Maximum, uint32_t Family, const Rgb& Colour) noexcept
{
    const Vec3 Corner[8] = {
        { Minimum.X, Minimum.Y, Minimum.Z }, { Maximum.X, Minimum.Y, Minimum.Z },
        { Maximum.X, Maximum.Y, Minimum.Z }, { Minimum.X, Maximum.Y, Minimum.Z },
        { Minimum.X, Minimum.Y, Maximum.Z }, { Maximum.X, Minimum.Y, Maximum.Z },
        { Maximum.X, Maximum.Y, Maximum.Z }, { Minimum.X, Maximum.Y, Maximum.Z }
    };
    AppendQuad(Into, Corner[0], Corner[1], Corner[2], Corner[3], Family, Colour);   // floor
    AppendQuad(Into, Corner[4], Corner[7], Corner[6], Corner[5], Family, Colour);   // lid
    AppendQuad(Into, Corner[0], Corner[4], Corner[5], Corner[1], Family, Colour);   // -Y
    AppendQuad(Into, Corner[3], Corner[2], Corner[6], Corner[7], Family, Colour);   // +Y
    AppendQuad(Into, Corner[1], Corner[5], Corner[6], Corner[2], Family, Colour);   // +X
    AppendQuad(Into, Corner[0], Corner[3], Corner[7], Corner[4], Family, Colour);   // -X
}

// 📝 A wheel: a sixteen-sided tread band and two rim faces, its axis along Y.
void AppendWheel(Shape& Into, Vec3 Centre, float Radius, float HalfWidth, uint32_t TyreFamily, uint32_t RimFamily) noexcept
{
    constexpr uint32_t Segments = 16u;
    const Rgb Tyre = { 0.020f, 0.020f, 0.022f };
    const Rgb Rim = { 0.095f, 0.100f, 0.112f };
    for (uint32_t Index = 0u; Index < Segments; ++Index)
    {
        const float First = 6.283185307f * float(Index) / float(Segments);
        const float Second = 6.283185307f * float(Index + 1u) / float(Segments);
        const Vec3 A = Centre + Vec3{ std::cos(First) * Radius, -HalfWidth, std::sin(First) * Radius };
        const Vec3 B = Centre + Vec3{ std::cos(Second) * Radius, -HalfWidth, std::sin(Second) * Radius };
        const Vec3 C = Centre + Vec3{ std::cos(Second) * Radius, HalfWidth, std::sin(Second) * Radius };
        const Vec3 D = Centre + Vec3{ std::cos(First) * Radius, HalfWidth, std::sin(First) * Radius };
        AppendQuad(Into, A, B, C, D, TyreFamily, Tyre);
        AppendFacet(Into, Centre + Vec3{ 0.0f, HalfWidth, 0.0f }, D, C, RimFamily, Rim);
        AppendFacet(Into, Centre + Vec3{ 0.0f, -HalfWidth, 0.0f }, B, A, RimFamily, Rim);
    }
}

void DrawShape(Painter& Target, const Shape& Drawn) noexcept
{
    for (size_t Index = 0u; Index + 2u < Drawn.Triangles.size(); Index += 3u)
        Target.Triangle(Drawn.Triangles[Index], Drawn.Triangles[Index + 1u], Drawn.Triangles[Index + 2u],
                        Drawn.TriangleColours[Index / 3u]);
}

// 📝 The car: a body, a greenhouse, and four wheels, with the families the demo's material table gives
//    them (BodyCoat 6, CabinLeather 70, Tyre 9, RimBase 81).
struct Vehicle
{
    Shape Body;
    std::array<Shape, 4> Wheels;
    std::array<Vec3, 4> Hubs = { Vec3{ 1.45f, -0.86f, 0.35f }, Vec3{ 1.45f, 0.86f, 0.35f },
                                 Vec3{ -1.45f, -0.86f, 0.35f }, Vec3{ -1.45f, 0.86f, 0.35f } };
};

[[nodiscard]] Vehicle BuildVehicle() noexcept
{
    Vehicle Car;
    const Rgb Paint = { 0.055f, 0.068f, 0.090f };
    const Rgb Glass = { 0.010f, 0.013f, 0.017f };
    AppendBox(Car.Body, { -2.20f, -0.95f, 0.20f }, { 2.20f, 0.95f, 1.02f }, 6u, Paint);
    AppendBox(Car.Body, { -0.95f, -0.84f, 1.02f }, { 1.33f, 0.84f, 1.52f }, 70u, Glass);
    for (uint32_t Index = 0u; Index < 4u; ++Index)
        AppendWheel(Car.Wheels[Index], Car.Hubs[Index], 0.35f, 0.125f, 9u, 81u);
    return Car;
}

// 📝 The family colours, in the scene's linear radiance units: what an anchor's cube wears.
[[nodiscard]] Rgb FamilyColour(float Family) noexcept
{
    switch (uint32_t(Family))
    {
        case 6u:  return { 0.55f, 0.68f, 0.85f };      // body coat
        case 70u: return { 0.42f, 0.30f, 0.24f };      // cabin leather
        case 9u:  return { 0.34f, 0.34f, 0.36f };      // tyre
        case 81u: return { 0.85f, 0.86f, 0.90f };      // rim
        default:  return { 0.7f, 0.7f, 0.7f };
    }
}

//======================================================================================================================================
//                                                             SCENES
//======================================================================================================================================

// 📝 A dim road grid, drawn additively: without it the ground is a flat field and neither the streaks
//    nor the cubes read as lying on a surface.
void DrawRoadGrid(Painter& Target, float Extent, float Spacing) noexcept
{
    const Rgb Line = { 0.030f, 0.032f, 0.040f };
    for (float At = -Extent; At <= Extent + 0.001f; At += Spacing)
    {
        Target.Streak({ -Extent, At, 0.0f }, { Extent, At, 0.0f }, Line, 1.0f);
        Target.Streak({ At, -Extent, 0.0f }, { At, Extent, 0.0f }, Line, 1.0f);
    }
}

// 📝 One long exposure of a scrape: every frame's instance table, added into the framebuffer. The tail
//    is drawn where SparkRaster draws it — lerped toward the head by the spark's shutter — so a spark
//    born late in the frame leaves a shorter streak, exactly as it does on the device.
void AccumulateSparks(Painter& Target,
                      const Camera& View,
                      std::span<const SparkScrape> Scrapes,
                      const SparkCourseSurface* Course,
                      std::span<const SparkBox> Boxes,
                      uint32_t Frames,
                      float Exposure,
                      float WidthPixels) noexcept
{
    SparkSequence Sparks;
    Target.Begin(View, true);
    for (uint32_t Frame = 0u; Frame < Frames; ++Frame)
    {
        Sparks.Emit(Scrapes, 1.0f / 60.0f);
        Sparks.Advance(1.0f / 60.0f, Boxes, {}, Course);
        for (uint32_t Index = 0u; Index < Sparks.Live(); ++Index)
        {
            const SparkInstance Instance = Sparks.Instance(Index);
            const Vec3 Head = Instance.Head;
            const Vec3 Tail = Instance.Tail + (Head - Instance.Tail) * std::clamp(Instance.Shutter, 0.0f, 1.0f);
            const Rgb Radiance = { Instance.Radiance.X * Exposure,
                                   Instance.Radiance.Y * Exposure,
                                   Instance.Radiance.Z * Exposure };
            Target.Streak(Tail, Head, Radiance, WidthPixels);
        }
    }
}

// 📝 The night road the sparks are thrown into: dark enough that a 2.2 mm streak at two thousand kelvin
//    is the brightest thing in the frame, which is what it looks like on the device.
const Rgb SkyTop = { 0.0016f, 0.0022f, 0.0045f };
const Rgb SkyBottom = { 0.0080f, 0.0100f, 0.0160f };
const Rgb GroundColour = { 0.0075f, 0.0078f, 0.0092f };

// 📝 A long exposure of one grinding wheel: the flat tyre has let the rim touch, so the contact is metal
//    on asphalt at 16 m/s with the wheel's own surface speed behind it, and the trail streams back from
//    the trailing edge of a rolling wheel.
void RenderSparksOnRim(const std::string& Path, int Width, int Height) noexcept
{
    const Vehicle Car = BuildVehicle();
    Painter Target(Width, Height);

    Camera View;
    View.Eye = { 1.55f, -2.20f, 0.58f };
    View.Target = { -1.32f, -0.82f, 0.26f };
    View.FieldOfView = 0.74f;
    View.Width = Width;
    View.Height = Height;
    View.Build();

    Target.Begin(View, false);
    Target.Backdrop(SkyTop, SkyBottom, GroundColour);
    for (const Shape& Wheel : Car.Wheels)
        DrawShape(Target, Wheel);
    DrawShape(Target, Car.Body);

    Target.Begin(View, true);
    DrawRoadGrid(Target, 24.0f, 1.0f);

    FlatSparkCourse Pad;
    SparkScrape Scrape;
    Scrape.Point = { -1.45f, -0.86f, 0.02f };
    Scrape.Velocity = { -16.0f, 0.0f, 0.0f };
    Scrape.Normal = { 0.0f, 0.0f, 1.0f };
    Scrape.Strength = 0.7f;
    Scrape.Rim = true;
    Scrape.Wheel = 2;
    Scrape.Forward = { 1.0f, 0.0f, 0.0f };
    Scrape.SpinSpeed = -22.0f;
    Scrape.Radius = 0.35f;
    const std::array<SparkScrape, 1> Scrapes{ Scrape };

    AccumulateSparks(Target, View, Scrapes, &Pad, {}, 8u, 1.0f / 10.0f, 1.35f);

    if (!PngWriter::Write(Path, Target.Surface_(), 1.0f))
        std::fprintf(stderr, "could not write %s\n", Path.c_str());
}

// 📝 The hull scraping a barrier: sparks fanned along the slide and kicked off the wall, most of them
//    skipping along the road before they cool below the visible threshold.
void RenderSparksOnHull(const std::string& Path, int Width, int Height) noexcept
{
    const Vehicle Car = BuildVehicle();
    Shape Barrier;
    AppendBox(Barrier, { -8.0f, -1.70f, 0.0f }, { 8.0f, -0.98f, 1.35f }, 6u, Rgb{ 0.055f, 0.050f, 0.045f });

    Painter Target(Width, Height);
    Camera View;
    View.Eye = { 5.30f, -4.40f, 1.55f };
    View.Target = { -0.90f, -1.05f, 0.45f };
    View.FieldOfView = 0.80f;
    View.Width = Width;
    View.Height = Height;
    View.Build();

    Target.Begin(View, false);
    Target.Backdrop(SkyTop, SkyBottom, GroundColour);
    DrawShape(Target, Barrier);
    for (const Shape& Wheel : Car.Wheels)
        DrawShape(Target, Wheel);
    DrawShape(Target, Car.Body);

    Target.Begin(View, true);
    DrawRoadGrid(Target, 24.0f, 1.0f);

    FlatSparkCourse Pad;
    SparkScrape Scrape;
    Scrape.Point = { -0.30f, -0.95f, 0.55f };
    Scrape.Velocity = { 24.0f, 0.0f, 0.0f };
    Scrape.Normal = { 0.0f, -1.0f, 0.0f };
    Scrape.Strength = 0.7f;
    const std::array<SparkScrape, 1> Scrapes{ Scrape };

    AccumulateSparks(Target, View, Scrapes, &Pad, {}, 8u, 1.0f / 10.0f, 1.30f);

    if (!PngWriter::Write(Path, Target.Surface_(), 1.0f))
        std::fprintf(stderr, "could not write %s\n", Path.c_str());
}

// 📝 A close look at the streaks themselves: a few sparks near enough that their blackbody colour and
//    their length — the distance travelled in one frame — are both visible.
void RenderSparkCloseUp(const std::string& Path, int Width, int Height) noexcept
{
    Painter Target(Width, Height);
    Camera View;
    View.Eye = { 1.40f, -1.90f, 1.00f };
    View.Target = { -0.70f, 0.0f, 0.35f };
    View.FieldOfView = 0.70f;
    View.Width = Width;
    View.Height = Height;
    View.Build();

    Target.Begin(View, false);
    Target.Backdrop(SkyTop, SkyBottom, GroundColour);

    Target.Begin(View, true);
    DrawRoadGrid(Target, 8.0f, 0.5f);

    FlatSparkCourse Pad;
    SparkScrape Scrape;
    Scrape.Point = { -0.10f, 0.0f, 0.02f };
    Scrape.Velocity = { -9.0f, 0.0f, 0.0f };
    Scrape.Normal = { 0.0f, 0.0f, 1.0f };
    Scrape.Strength = 0.50f;
    Scrape.Rim = true;
    Scrape.Wheel = 0;
    Scrape.Forward = { 1.0f, 0.0f, 0.0f };
    Scrape.SpinSpeed = -12.0f;
    Scrape.Radius = 0.35f;
    const std::array<SparkScrape, 1> Scrapes{ Scrape };

    AccumulateSparks(Target, View, Scrapes, &Pad, {}, 14u, 1.0f / 6.0f, 1.9f);

    if (!PngWriter::Write(Path, Target.Surface_(), 1.0f))
        std::fprintf(stderr, "could not write %s\n", Path.c_str());
}

// 📝 The derez's anchors: 1 536 spread over the body by area and 96 over each wheel, each carrying the
//    family of the facet it came from — the colour of the cube that will take its place.
void RenderVehicleAnchors(const std::string& Path, int Width, int Height) noexcept
{
    const Vehicle Car = BuildVehicle();
    Painter Target(Width, Height);

    Camera View;
    View.Eye = { 4.30f, -3.55f, 1.85f };
    View.Target = { -0.05f, -0.05f, 0.55f };
    View.FieldOfView = 0.70f;
    View.Width = Width;
    View.Height = Height;
    View.Build();

    Target.Begin(View, false);
    Target.Backdrop(SkyTop, SkyBottom, GroundColour);
    DrawRoadGrid(Target, 16.0f, 1.0f);
    for (const Shape& Wheel : Car.Wheels)
        DrawShape(Target, Wheel);
    DrawShape(Target, Car.Body);

    // 📝 The anchors: the same call the host makes when it builds the car's cell table.
    std::vector<float> Cells;
    const std::vector<float> BodyCells = SpreadEffectCells(Car.Body.Facets, EffectVehicleBodyCells);
    Cells.insert(Cells.end(), BodyCells.begin(), BodyCells.end());
    for (uint32_t Index = 0u; Index < 4u; ++Index)
    {
        const std::vector<float> WheelCells = SpreadEffectCells(Car.Wheels[Index].Facets, EffectVehicleWheelCells);
        Cells.insert(Cells.end(), WheelCells.begin(), WheelCells.end());
    }

    Target.Begin(View, true);
    for (size_t Index = 0u; Index + EffectCellFloats <= Cells.size(); Index += EffectCellFloats)
    {
        const float* Cell = Cells.data() + Index;
        const Rgb Colour = FamilyColour(Cell[3]);
        Target.Dot({ Cell[0], Cell[1], Cell[2] }, 2.0f, Colour);
    }

    if (!PngWriter::Write(Path, Target.Surface_(), 1.0f))
        std::fprintf(stderr, "could not write %s\n", Path.c_str());
}

} // namespace

int main(int argc, char** argv)
{
    std::string Directory = "VisualProof/Phase3";
    if (argc > 1)
        Directory = argv[1];

    const int Width = 1280;
    const int Height = 800;

    RenderSparksOnRim(Directory + "/3d-sparks-rim.png", Width, Height);
    RenderSparksOnHull(Directory + "/3d-sparks-hull.png", Width, Height);
    RenderSparkCloseUp(Directory + "/3d-sparks-closeup.png", Width, Height);
    RenderVehicleAnchors(Directory + "/3d-effect-anchors.png", Width, Height);

    std::printf("Phase3ProofImages: four images in %s\n", Directory.c_str());
    return 0;
}
