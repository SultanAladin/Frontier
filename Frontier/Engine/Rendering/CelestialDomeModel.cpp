/*==========================================================================================================================================
                                               CELESTIAL DOME MODEL DEFINITION
==========================================================================================================================================*/
// 🧩 Implementation of the 41k Hipparcos octahedral star index + Kolmogorov scintillation, 2K 6-body lunar sphere atlas,
//    and multi-sun 3D ringed planetary bodies with simulated spherical Rayleigh/Mie/absorption atmospheres & mutual shadows.

#include "Engine/Rendering/CelestialDomeModel.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace Frontier
{
namespace
{

constexpr float  PiF = 3.14159265358979323846f;
constexpr double PiD = 3.14159265358979323846;

float SmoothStepF(float Edge0, float Edge1, float X) noexcept
{
    const float Denom = (std::abs(Edge1 - Edge0) > 1.0e-7f) ? (Edge1 - Edge0) : 1.0e-7f;
    const float T     = std::clamp((X - Edge0) / Denom, 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

Vec3 LerpVec3(Vec3 A, Vec3 B, float T) noexcept
{
    return A * (1.0f - T) + B * T;
}

Vec3 CompMul(Vec3 A, Vec3 B) noexcept
{
    return {A.X * B.X, A.Y * B.Y, A.Z * B.Z};
}

Vec3 ExpNeg(Vec3 OpticalDepth) noexcept
{
    return {std::exp(-OpticalDepth.X), std::exp(-OpticalDepth.Y), std::exp(-OpticalDepth.Z)};
}

uint8_t LinearToSrgbByte(float Value) noexcept
{
    const float C = std::clamp(Value, 0.0f, 1.0f);
    const float S = C <= 0.0031308f ? (12.92f * C) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(S * 255.0f + 0.5f, 0.0f, 255.0f));
}

bool IntersectRaySphere(Vec3 Ro, Vec3 Dir, Vec3 Centre, float Radius, float& OutT0, float& OutT1) noexcept
{
    const Vec3  Oc   = Ro - Centre;
    const float B    = Dot(Oc, Dir);
    const float C    = Dot(Oc, Oc) - Radius * Radius;
    const float Disc = B * B - C;
    if (Disc < 0.0f)
    {
        return false;
    }
    const float Sq = std::sqrt(Disc);
    OutT0          = -B - Sq;
    OutT1          = -B + Sq;
    return OutT1 > 0.0f;
}

float Hash31(Vec3 P) noexcept
{
    const float S = std::sin(P.X * 127.1f + P.Y * 311.7f + P.Z * 74.7f) * 43758.5453f;
    return S - std::floor(S);
}

float LatticeNoise3D(Vec3 P) noexcept
{
    const Vec3 Fl{std::floor(P.X), std::floor(P.Y), std::floor(P.Z)};
    const Vec3 Fr{P.X - Fl.X, P.Y - Fl.Y, P.Z - Fl.Z};
    const Vec3 U{Fr.X * Fr.X * (3.0f - 2.0f * Fr.X),
                 Fr.Y * Fr.Y * (3.0f - 2.0f * Fr.Y),
                 Fr.Z * Fr.Z * (3.0f - 2.0f * Fr.Z)};

    auto        Corner = [&](float Dx, float Dy, float Dz) { return Hash31({Fl.X + Dx, Fl.Y + Dy, Fl.Z + Dz}); };
    const float N00    = Corner(0, 0, 0) * (1.0f - U.X) + Corner(1, 0, 0) * U.X;
    const float N10    = Corner(0, 1, 0) * (1.0f - U.X) + Corner(1, 1, 0) * U.X;
    const float N01    = Corner(0, 0, 1) * (1.0f - U.X) + Corner(1, 0, 1) * U.X;
    const float N11    = Corner(0, 1, 1) * (1.0f - U.X) + Corner(1, 1, 1) * U.X;
    const float Ny0    = N00 * (1.0f - U.Y) + N10 * U.Y;
    const float Ny1    = N01 * (1.0f - U.Y) + N11 * U.Y;
    return Ny0 * (1.0f - U.Z) + Ny1 * U.Z;
}

float Fbm3D(Vec3 P) noexcept
{
    float Sum = 0.0f;
    float Amp = 0.5f;
    for (int Oct = 0; Oct < 4; ++Oct)
    {
        Sum += Amp * LatticeNoise3D(P);
        P = P * 2.07f;
        Amp *= 0.5f;
    }
    return Sum;
}

void SampleRingRadialProfile(const PlanetaryRingDescriptor& Rings,
                             float                          RadiusInPlanetUnits,
                             float&                         OutNormalTau,
                             Vec3&                          OutRingAlbedo) noexcept
{
    if (!Rings.Enabled || RadiusInPlanetUnits < Rings.InnerRadiusRatio || RadiusInPlanetUnits > Rings.OuterRadiusRatio)
    {
        OutNormalTau  = 0.0f;
        OutRingAlbedo = {0.0f, 0.0f, 0.0f};
        return;
    }

    const float Span     = std::max(0.05f, Rings.OuterRadiusRatio - Rings.InnerRadiusRatio);
    const float U        = std::clamp((RadiusInPlanetUnits - Rings.InnerRadiusRatio) / Span, 0.0f, 1.0f);
    const float Ringlets = 0.84f + 0.16f * std::sin(U * 145.0f) * std::cos(U * 63.0f + 0.7f);

    float Profile = 0.0f;
    float IceFrac = U;
    if (U < 0.22f)
    {
        Profile = 0.24f * SmoothStepF(0.0f, 0.06f, U) * (0.75f + 0.25f * std::sin(U * 90.0f));
        IceFrac = 0.18f;
    }
    else if (U < 0.58f)
    {
        Profile = (0.78f + 0.22f * std::sin((U - 0.22f) * 38.0f)) * SmoothStepF(0.20f, 0.25f, U);
        IceFrac = 0.85f;
    }
    else if (U < 0.65f)
    {
        const float GapBell = std::exp(-std::pow((U - 0.615f) / 0.022f, 4.0f));
        Profile             = 0.62f * (1.0f - 0.94f * GapBell);
        IceFrac             = 0.35f;
    }
    else if (U < 0.94f)
    {
        const float Encke = std::exp(-std::pow((U - 0.855f) / 0.009f, 4.0f));
        Profile           = 0.56f * (1.0f - 0.92f * Encke) * (1.0f - 0.18f * (U - 0.65f) / 0.29f);
        IceFrac           = 0.92f;
    }
    else
    {
        const float Filament = std::exp(-std::pow((U - 0.975f) / 0.008f, 2.0f));
        Profile              = 0.38f * Filament;
        IceFrac              = 0.95f;
    }

    OutNormalTau  = std::max(0.0f, Rings.PeakOpticalDepth * Profile * Ringlets);
    OutRingAlbedo = LerpVec3(Rings.InnerTintRgb, Rings.OuterTintRgb, IceFrac) * (0.88f + 0.12f * Ringlets);
}

float EvaluateRingShadowTransmittance(const PlanetaryRingDescriptor& Rings,
                                      Vec3                           PlanetCentre,
                                      Vec3                           PointInPlanetSpace,
                                      Vec3                           SunDirUnit,
                                      bool&                          OutShadowedByRings) noexcept
{
    OutShadowedByRings = false;
    if (!Rings.Enabled)
    {
        return 1.0f;
    }
    const float Denom = Dot(SunDirUnit, Rings.RingNormalUnit);
    if (std::abs(Denom) < 1.0e-5f)
    {
        return 1.0f;
    }
    const float TPlane = Dot(PlanetCentre - PointInPlanetSpace, Rings.RingNormalUnit) / Denom;
    if (TPlane <= 0.0f)
    {
        return 1.0f;
    }
    const Vec3  RingHit = PointInPlanetSpace + SunDirUnit * TPlane;
    const float Rad     = Length(RingHit - PlanetCentre);
    float       TauN    = 0.0f;
    Vec3        DummyAlb{};
    SampleRingRadialProfile(Rings, Rad, TauN, DummyAlb);
    if (TauN <= 1.0e-4f)
    {
        return 1.0f;
    }
    const float SlantTau = TauN / std::max(0.12f, std::abs(Denom));
    const float Trans    = std::exp(-SlantTau);
    if (Trans < 0.85f)
    {
        OutShadowedByRings = true;
    }
    return Trans;
}

bool LoadRgbBytesFromFile(const std::string& Path, int& OutW, int& OutH, std::vector<uint8_t>& OutSrgbRgb)
{
    std::ifstream File(Path, std::ios::binary | std::ios::ate);
    if (!File.is_open())
    {
        return false;
    }
    const std::streamsize Size = File.tellg();
    if (Size <= 16)
    {
        return false;
    }
    File.seekg(0, std::ios::beg);
    std::vector<uint8_t> Raw(static_cast<size_t>(Size));
    if (!File.read(reinterpret_cast<char*>(Raw.data()), Size))
    {
        return false;
    }

    // Check binary P6 PPM first
    if (Raw[0] == 'P' && Raw[1] == '6')
    {
        LunarAlbedoMap Temp;
        if (!Temp.LoadPpm(Path.c_str()))
        {
            return false;
        }
        OutW = static_cast<int>(Temp.Width());
        OutH = static_cast<int>(Temp.Height());
        OutSrgbRgb.resize(static_cast<size_t>(OutW) * OutH * 3u);
        for (int Y = 0; Y < OutH; ++Y)
        {
            const double Lat = 0.5 * PiD - PiD * (static_cast<double>(Y) + 0.5) / static_cast<double>(OutH);
            for (int X = 0; X < OutW; ++X)
            {
                const double      Lon = 2.0 * PiD * (static_cast<double>(X) + 0.5) / static_cast<double>(OutW) - PiD;
                const SpectrumRgb S   = Temp.Sample(Lon, Lat);
                const size_t      Idx = (static_cast<size_t>(Y) * OutW + X) * 3u;
                OutSrgbRgb[Idx + 0u]  = LinearToSrgbByte(static_cast<float>(S.R));
                OutSrgbRgb[Idx + 1u]  = LinearToSrgbByte(static_cast<float>(S.G));
                OutSrgbRgb[Idx + 2u]  = LinearToSrgbByte(static_cast<float>(S.B));
            }
        }
        return true;
    }

    int      Channels = 0;
    uint8_t* Decoded  = stbi_load_from_memory(Raw.data(), static_cast<int>(Raw.size()), &OutW, &OutH, &Channels, 3);
    if (!Decoded || OutW <= 0 || OutH <= 0)
    {
        if (Decoded)
        {
            stbi_image_free(Decoded);
        }
        return false;
    }
    OutSrgbRgb.assign(Decoded, Decoded + static_cast<size_t>(OutW) * OutH * 3u);
    stbi_image_free(Decoded);
    return true;
}

bool WriteTemporaryPpmInMemoryAndLoad(uint32_t W, uint32_t H, const uint8_t* SrgbRgb, LunarAlbedoMap& OutMap, const std::string& TempPath)
{
    std::ofstream Out(TempPath, std::ios::binary);
    if (!Out.is_open())
    {
        return false;
    }
    char Header[64]{};
    const int Len = std::snprintf(Header, sizeof(Header), "P6\n%u %u\n255\n", W, H);
    Out.write(Header, Len);
    Out.write(reinterpret_cast<const char*>(SrgbRgb), static_cast<std::streamsize>(W) * H * 3);
    Out.close();
    const bool Ok = OutMap.LoadPpm(TempPath.c_str());
    std::remove(TempPath.c_str());
    return Ok;
}

Vec3 EvaluateProceduralMoonAlbedoLinear(LunarSurfacePreset Preset, Vec3 SphereNormal, float BaseLunaLum, float& OutThermalR) noexcept
{
    OutThermalR           = 0.0f;
    const float MareNoise = Fbm3D(SphereNormal * 2.8f + Vec3{1.4f, 2.7f, 0.9f});
    const float Mare      = SmoothStepF(0.42f, 0.62f, MareNoise);
    const float Fine      = 0.72f + 0.56f * BaseLunaLum;
    const float Lineae    = std::abs(std::sin(SphereNormal.X * 19.0f + SphereNormal.Y * 13.0f + 4.2f * MareNoise));

    switch (Preset)
    {
        case LunarSurfacePreset::LunaSelenographic:
            return Vec3{BaseLunaLum, BaseLunaLum * 0.985f, BaseLunaLum * 0.955f};
        case LunarSurfacePreset::EmberVolcanic:
        {
            const float Fissure = std::pow(std::max(0.0f, 1.0f - Lineae * 4.5f), 2.5f);
            OutThermalR         = Fissure;
            return LerpVec3({0.045f, 0.040f, 0.040f}, {0.15f, 0.085f, 0.055f}, Mare) * Fine +
                   Vec3{0.65f, 0.16f, 0.03f} * Fissure;
        }
        case LunarSurfacePreset::GlacierEuropa:
        {
            const float Crack = SmoothStepF(0.14f, 0.02f, Lineae);
            return LerpVec3({0.64f, 0.74f, 0.84f}, {0.88f, 0.92f, 0.96f}, Mare) * (0.85f + 0.15f * Fine) -
                   Vec3{0.20f, 0.30f, 0.38f} * Crack;
        }
        case LunarSurfacePreset::SulfurIo:
            return LerpVec3({0.78f, 0.62f, 0.14f}, {0.94f, 0.86f, 0.34f}, Mare) * (0.80f + 0.25f * Fine);
        case LunarSurfacePreset::ShroudTitan:
        {
            const float Band = 0.5f + 0.5f * SphereNormal.Y;
            return LerpVec3({0.66f, 0.40f, 0.14f}, {0.84f, 0.56f, 0.24f}, Band) * (0.94f + 0.06f * MareNoise);
        }
        case LunarSurfacePreset::ShardCaptured:
        default:
            return LerpVec3({0.055f, 0.055f, 0.060f}, {0.145f, 0.140f, 0.132f}, Mare) * Fine;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           2K 6-BODY LUNAR SPHERE ATLAS
//------------------------------------------------------------------------------------------------------------------------

bool LunarSixBodyAtlas::BuildFromLunaSource(const std::string& LunaJpegOrPpmPath, uint32_t Width, uint32_t Height)
{
    int                  SrcW = 0, SrcH = 0;
    std::vector<uint8_t> SrcSrgb;
    if (!LoadRgbBytesFromFile(LunaJpegOrPpmPath, SrcW, SrcH, SrcSrgb))
    {
        return false;
    }
    Width_  = Width > 0u ? Width : static_cast<uint32_t>(SrcW);
    Height_ = Height > 0u ? Height : static_cast<uint32_t>(SrcH);

    Rgba8Srgb_.assign(static_cast<size_t>(Width_) * Height_ * 4u * LunarAtlasLayerCount, 255u);
    std::vector<uint8_t> LayerSrgbRgb(static_cast<size_t>(Width_) * Height_ * 3u, 0u);

    auto SrgbToLinear = [](uint8_t Byte) -> float {
        const float S = static_cast<float>(Byte) / 255.0f;
        return S <= 0.04045f ? (S / 12.92f) : std::pow((S + 0.055f) / 1.055f, 2.4f);
    };

    for (uint32_t Layer = 0u; Layer < LunarAtlasLayerCount; ++Layer)
    {
        const auto Preset = static_cast<LunarSurfacePreset>(Layer);
        for (uint32_t Y = 0u; Y < Height_; ++Y)
        {
            const float V      = (static_cast<float>(Y) + 0.5f) / static_cast<float>(Height_);
            const float LatRad = (0.5f - V) * PiF;
            const float CosLat = std::cos(LatRad);
            const float SinLat = std::sin(LatRad);
            const int   Sy     = std::clamp(static_cast<int>(V * SrcH), 0, SrcH - 1);

            for (uint32_t X = 0u; X < Width_; ++X)
            {
                const float U      = (static_cast<float>(X) + 0.5f) / static_cast<float>(Width_);
                const float LonRad = (U - 0.5f) * 2.0f * PiF;
                const int   Sx     = std::clamp(static_cast<int>(U * SrcW), 0, SrcW - 1);
                const size_t SrcIdx = (static_cast<size_t>(Sy) * SrcW + Sx) * 3u;

                const size_t RgbIdx  = (static_cast<size_t>(Y) * Width_ + X) * 3u;
                const size_t RgbaIdx = ((static_cast<size_t>(Layer) * Height_ + Y) * Width_ + X) * 4u;

                if (Layer == 0u)
                {
                    const uint8_t R         = SrcSrgb[SrcIdx + 0u];
                    const uint8_t G         = SrcSrgb[SrcIdx + 1u];
                    const uint8_t B         = SrcSrgb[SrcIdx + 2u];
                    LayerSrgbRgb[RgbIdx + 0u] = R;
                    LayerSrgbRgb[RgbIdx + 1u] = G;
                    LayerSrgbRgb[RgbIdx + 2u] = B;
                    Rgba8Srgb_[RgbaIdx + 0u]  = R;
                    Rgba8Srgb_[RgbaIdx + 1u]  = G;
                    Rgba8Srgb_[RgbaIdx + 2u]  = B;
                    Rgba8Srgb_[RgbaIdx + 3u]  = 255u;
                }
                else
                {
                    const float BaseLum = 0.2126f * SrgbToLinear(SrcSrgb[SrcIdx + 0u]) +
                                          0.7152f * SrgbToLinear(SrcSrgb[SrcIdx + 1u]) +
                                          0.0722f * SrgbToLinear(SrcSrgb[SrcIdx + 2u]);
                    const Vec3  SphereN{CosLat * std::sin(LonRad), SinLat, CosLat * std::cos(LonRad)};
                    float       Thermal = 0.0f;
                    const Vec3  Lin     = EvaluateProceduralMoonAlbedoLinear(Preset, SphereN, BaseLum, Thermal);
                    const uint8_t R     = LinearToSrgbByte(Lin.X);
                    const uint8_t G     = LinearToSrgbByte(Lin.Y);
                    const uint8_t B     = LinearToSrgbByte(Lin.Z);
                    LayerSrgbRgb[RgbIdx + 0u] = R;
                    LayerSrgbRgb[RgbIdx + 1u] = G;
                    LayerSrgbRgb[RgbIdx + 2u] = B;
                    Rgba8Srgb_[RgbaIdx + 0u]  = R;
                    Rgba8Srgb_[RgbaIdx + 1u]  = G;
                    Rgba8Srgb_[RgbaIdx + 2u]  = B;
                    Rgba8Srgb_[RgbaIdx + 3u]  = LinearToSrgbByte(Thermal);
                }
            }
        }
        const std::string TempFile = "/tmp/frontier_lunar_layer_" + std::to_string(Layer) + ".ppm";
        if (!WriteTemporaryPpmInMemoryAndLoad(Width_, Height_, LayerSrgbRgb.data(), Layers_[Layer], TempFile))
        {
            return false;
        }
    }
    return true;
}

const LunarAlbedoMap& LunarSixBodyAtlas::LayerMap(LunarSurfacePreset Preset) const noexcept
{
    const size_t Index = std::min<size_t>(static_cast<size_t>(Preset), LunarAtlasLayerCount - 1u);
    return Layers_[Index];
}

//------------------------------------------------------------------------------------------------------------------------
//                                        KELVIN BLACKBODY & OCTAHEDRAL CELL MAPPING
//------------------------------------------------------------------------------------------------------------------------

Vec3 CelestialDomeModel::KelvinToBlackbodyChromaticity(float Kelvin) noexcept
{
    const float TempHecto = std::clamp(Kelvin, 1200.0f, 40000.0f) * 0.01f;
    float       R         = 1.0f;
    float       G         = 1.0f;
    float       B         = 1.0f;

    if (TempHecto <= 66.0f)
    {
        R = 1.0f;
        G = std::clamp(0.39008158f * std::log(std::max(1.0f, TempHecto)) - 0.63184144f, 0.0f, 1.0f);
        B = (TempHecto <= 19.0f)
                ? 0.0f
                : std::clamp(0.54320679f * std::log(std::max(1.0f, TempHecto - 10.0f)) - 1.19625409f, 0.0f, 1.0f);
    }
    else
    {
        R = std::clamp(1.29293618f * std::pow(TempHecto - 60.0f, -0.13320476f), 0.0f, 1.0f);
        G = std::clamp(1.12989086f * std::pow(TempHecto - 60.0f, -0.07551485f), 0.0f, 1.0f);
        B = 1.0f;
    }
    Vec3        Lin{std::pow(R, 2.2f), std::pow(G, 2.2f), std::pow(B, 2.2f)};
    const float Peak = std::max({Lin.X, Lin.Y, Lin.Z, 1.0e-4f});
    return Lin * (1.0f / Peak);
}

uint32_t CelestialDomeModel::OctahedralCellForDirection(Vec3 D) noexcept
{
    const float L1 = std::abs(D.X) + std::abs(D.Y) + std::abs(D.Z);
    if (L1 < 1.0e-8f)
    {
        return 0u;
    }
    float Px = D.X / L1;
    float Py = D.Y / L1;
    if (D.Z < 0.0f)
    {
        const float Ox = (1.0f - std::abs(Py)) * (Px >= 0.0f ? 1.0f : -1.0f);
        const float Oy = (1.0f - std::abs(Px)) * (Py >= 0.0f ? 1.0f : -1.0f);
        Px             = Ox;
        Py             = Oy;
    }
    const float    U   = std::clamp(Px * 0.5f + 0.5f, 0.0f, 0.999999f);
    const float    V   = std::clamp(Py * 0.5f + 0.5f, 0.0f, 0.999999f);
    const uint32_t Col = std::min(OctahedralGridAxis - 1u, static_cast<uint32_t>(U * OctahedralGridAxis));
    const uint32_t Row = std::min(OctahedralGridAxis - 1u, static_cast<uint32_t>(V * OctahedralGridAxis));
    return Row * OctahedralGridAxis + Col;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             LIFECYCLE & STAR CATALOGUE INDEX
//------------------------------------------------------------------------------------------------------------------------

CelestialDomeModel::CelestialDomeModel()
{
    BuildProceduralStarCatalogue(8000u, 0x51A7CA7A109FULL);
}

void CelestialDomeModel::ClearBodies() noexcept
{
    CustomStars_.clear();
    Moons_.clear();
    Planets_.clear();
}

uint32_t CelestialDomeModel::NonEmptyCellCount() const noexcept
{
    uint32_t Active = 0u;
    for (const StarCellRange& Cell : StarCells_)
    {
        if (Cell.Count > 0u)
        {
            ++Active;
        }
    }
    return Active;
}

void CelestialDomeModel::RebuildOctahedralStarIndex(const std::vector<StarRecord>& Records)
{
    BinnedStars_.clear();
    for (auto& Cell : StarCells_)
    {
        Cell = StarCellRange{};
    }
    SourceStarCount_ = static_cast<uint32_t>(Records.size());
    if (Records.empty())
    {
        return;
    }

    constexpr float                               StarGlowRadiusRad = 0.0038f;
    std::vector<std::vector<BinnedCatalogueStar>> Buckets(OctahedralCellCount);

    for (const auto& Sr : Records)
    {
        const Vec3 Dir = Normalise(Vec3{static_cast<float>(Sr.Direction.X),
                                        static_cast<float>(Sr.Direction.Y),
                                        static_cast<float>(Sr.Direction.Z)});

        const float Bv     = std::clamp(static_cast<float>(Sr.ColourIndex), -0.45f, 2.10f);
        const float TempK  = 4600.0f * (1.0f / (0.92f * Bv + 1.70f) + 1.0f / (0.92f * Bv + 0.62f));
        const Vec3  Chroma = KelvinToBlackbodyChromaticity(TempK);
        const float Mag    = static_cast<float>(Sr.Magnitude);
        const float Flux   = std::pow(10.0f, -0.4f * Mag);

        BinnedCatalogueStar Entry{Dir, Chroma, Flux, Mag};

        const Vec3 RefAxis = (std::abs(Dir.Z) < 0.92f) ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
        const Vec3 TanU    = Normalise(Cross(Dir, RefAxis));
        const Vec3 TanV    = Cross(Dir, TanU);

        uint32_t Touched[5]{};
        uint32_t TouchedCount  = 0u;
        auto     AddUniqueCell = [&](uint32_t C) {
            for (uint32_t I = 0u; I < TouchedCount; ++I)
            {
                if (Touched[I] == C)
                {
                    return;
                }
            }
            if (TouchedCount < 5u)
            {
                Touched[TouchedCount++] = C;
            }
        };

        AddUniqueCell(OctahedralCellForDirection(Dir));
        AddUniqueCell(OctahedralCellForDirection(Normalise(Dir + TanU * StarGlowRadiusRad)));
        AddUniqueCell(OctahedralCellForDirection(Normalise(Dir - TanU * StarGlowRadiusRad)));
        AddUniqueCell(OctahedralCellForDirection(Normalise(Dir + TanV * StarGlowRadiusRad)));
        AddUniqueCell(OctahedralCellForDirection(Normalise(Dir - TanV * StarGlowRadiusRad)));

        for (uint32_t I = 0u; I < TouchedCount; ++I)
        {
            Buckets[Touched[I]].push_back(Entry);
        }
    }

    uint32_t Offset = 0u;
    for (uint32_t C = 0u; C < OctahedralCellCount; ++C)
    {
        auto& Vec = Buckets[C];
        std::sort(Vec.begin(), Vec.end(), [](const BinnedCatalogueStar& A, const BinnedCatalogueStar& B) {
            return A.RelativeFlux > B.RelativeFlux;
        });
        StarCells_[C].First = Offset;
        StarCells_[C].Count = static_cast<uint32_t>(Vec.size());
        Offset += StarCells_[C].Count;
    }

    BinnedStars_.reserve(Offset);
    for (uint32_t C = 0u; C < OctahedralCellCount; ++C)
    {
        for (const auto& Entry : Buckets[C])
        {
            BinnedStars_.push_back(Entry);
        }
    }
}

bool CelestialDomeModel::LoadHipparcosCatalogueCsv(const std::string& CsvPath,
                                                   double             JulianUt1,
                                                   double             LatitudeDeg,
                                                   double             LongitudeDeg)
{
    std::ifstream Stream(CsvPath);
    if (!Stream.is_open())
    {
        return false;
    }
    const std::vector<StarRecord> Decoded = DecodeHipparcosCatalogue(Stream, JulianUt1, LatitudeDeg, LongitudeDeg);
    if (Decoded.empty())
    {
        return false;
    }
    RebuildOctahedralStarIndex(Decoded);
    return true;
}

void CelestialDomeModel::BuildProceduralStarCatalogue(uint32_t StarCount, uint64_t Seed)
{
    RebuildOctahedralStarIndex(GenerateStarCatalogue(StarCount, Seed));
}

bool CelestialDomeModel::LoadLunarAtlas(const std::string& LunaJpegOrPpmPath, uint32_t AtlasWidth, uint32_t AtlasHeight)
{
    return LunarAtlas_.BuildFromLunaSource(LunaJpegOrPpmPath, AtlasWidth, AtlasHeight);
}

void CelestialDomeModel::RegisterCustomStar(const CustomStarDescriptor& Star)
{
    CustomStarDescriptor Clean = Star;
    Clean.DirectionUnit        = Normalise(Star.DirectionUnit);
    CustomStars_.push_back(Clean);
}

void CelestialDomeModel::RegisterLunarBody(const LunarBodyDescriptor& Moon)
{
    LunarBodyDescriptor Clean = Moon;
    Clean.DirectionUnit       = Normalise(Moon.DirectionUnit);
    Moons_.push_back(Clean);
}

void CelestialDomeModel::RegisterPlanetaryBody(const PlanetaryBodyDescriptor& Planet)
{
    PlanetaryBodyDescriptor Clean = Planet;
    Clean.DirectionUnit           = Normalise(Planet.DirectionUnit);
    Clean.SpinAxisUnit            = Normalise(Planet.SpinAxisUnit);
    Clean.Rings.RingNormalUnit    = Normalise(Planet.Rings.RingNormalUnit);
    Planets_.push_back(Clean);
}

void CelestialDomeModel::ConfigureStarfieldAppearance(float StarBrightnessGain,
                                                      float MagnitudeLimit,
                                                      float MilkyWayBandStrength,
                                                      float ScintillationAmount,
                                                      float TimeSeconds,
                                                      float ObserverAltitudeKm) noexcept
{
    StarBrightnessGain_   = StarBrightnessGain;
    MagnitudeLimit_       = MagnitudeLimit;
    MilkyWayBandStrength_ = MilkyWayBandStrength;
    ScintillationAmount_  = ScintillationAmount;
    TimeSeconds_          = TimeSeconds;
    ObserverAltitudeKm_   = ObserverAltitudeKm;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      STARS, SCINTILLATION, MILKY WAY & CUSTOM STARS
//------------------------------------------------------------------------------------------------------------------------

Vec3 CelestialDomeModel::EvaluateCatalogueStarsAndMilkyWay(Vec3  D,
                                                           float NightVisibilityWeight,
                                                           float PixelSpanRad) const noexcept
{
    if (NightVisibilityWeight <= 1.0e-4f || StarBrightnessGain_ <= 0.0f || D.Z <= -0.02f)
    {
        return {0.0f, 0.0f, 0.0f};
    }

    const float ElevFade     = SmoothStepF(-0.01f, 0.08f, D.Z);
    const float AirDensity   = std::exp(-std::max(0.0f, ObserverAltitudeKm_) / 8.0f);
    const float Airmass      = 1.0f / std::max(0.10f, D.Z + 0.06f);
    const float Extinction   = std::exp(-0.055f * AirDensity * (Airmass - 1.0f));
    // Kolmogorov turbulence scintillation scales with airmass^(11/12) and atmospheric column density!
    const float ScintScale   = ScintillationAmount_ * AirDensity * std::min(2.6f, 0.16f * std::pow(Airmass, 0.92f));

    const uint32_t      CellIdx = OctahedralCellForDirection(D);
    const StarCellRange Span    = StarCells_[CellIdx];

    const float SigmaRad = std::max(0.00055f, PixelSpanRad * 0.92f);
    const float InvW2    = 1.0f / (SigmaRad * SigmaRad);

    Vec3 StarSum{0.0f, 0.0f, 0.0f};
    for (uint32_t I = 0u; I < Span.Count; ++I)
    {
        const auto& S = BinnedStars_[Span.First + I];
        if (S.Magnitude > MagnitudeLimit_)
        {
            break;
        }
        const Vec3  Delta   = D - S.DirectionUnit;
        const float Radius2 = Dot(Delta, Delta) * InvW2;
        if (Radius2 > 14.0f)
        {
            continue;
        }
        const float Core  = std::exp(-Radius2 * 1.65f);
        const float Halo  = std::exp(-Radius2 * 0.32f) * std::min(0.18f, std::sqrt(S.RelativeFlux) * 0.08f);
        const float Phase = S.DirectionUnit.X * 91.7f + S.DirectionUnit.Y * 213.1f + S.DirectionUnit.Z * 57.3f;
        const float WaveA = std::sin(TimeSeconds_ * 7.3f + Phase);
        const float WaveB = std::cos(TimeSeconds_ * 11.9f - Phase * 1.37f);
        const float Twink = std::max(0.15f, 1.0f + ScintScale * (0.65f * WaveA + 0.35f * WaveB));
        // Subtle atmospheric dispersion chromaticity shift during intense low-elevation twinkling
        const Vec3  ChromaTwink{S.ChromaticityRgb.X * (1.0f + 0.18f * ScintScale * WaveB),
                                S.ChromaticityRgb.Y,
                                S.ChromaticityRgb.Z * (1.0f - 0.18f * ScintScale * WaveB)};
        const float Gain = std::min(16.0f, 0.06f + 4.8f * std::pow(std::max(0.0f, S.RelativeFlux), 0.62f));
        StarSum          = StarSum + ChromaTwink * ((Core + Halo) * Gain * Twink);
    }

    const Vec3  GalacticPole = Normalise(Vec3{0.32f, 0.78f, 0.54f});
    const float GalLat       = Dot(D, GalacticPole);
    const float CloudStruct  = 0.55f + 0.45f * Fbm3D(D * 7.5f + Vec3{3.1f, 1.7f, 5.9f});
    const float Band         = std::exp(-(GalLat * GalLat) / 0.024f) * CloudStruct;
    const float RiftOffset   = GalLat + 0.022f * (LatticeNoise3D(D * 11.0f) - 0.5f);
    const float DustRift     = 1.0f - 0.78f * std::exp(-(RiftOffset * RiftOffset) / 0.0018f) *
                                      (0.65f + 0.35f * LatticeNoise3D(D * 19.0f));
    const Vec3 MilkyRgb = Vec3{0.024f, 0.030f, 0.048f} * (Band * DustRift * MilkyWayBandStrength_);

    return (StarSum * StarBrightnessGain_ + MilkyRgb) * (NightVisibilityWeight * ElevFade * Extinction);
}

Vec3 CelestialDomeModel::EvaluateCustomStarsAlongRay(Vec3 D, float PixelSpanRad) const noexcept
{
    Vec3 Total{0.0f, 0.0f, 0.0f};
    for (const auto& Cs : CustomStars_)
    {
        if (D.Z <= -0.02f)
        {
            continue;
        }
        const float CosAng = std::clamp(Dot(D, Cs.DirectionUnit), -1.0f, 1.0f);
        const float Ang    = std::acos(CosAng);
        if (Ang > 0.35f)
        {
            continue;
        }

        const Vec3  StarCol = KelvinToBlackbodyChromaticity(Cs.TemperatureKelvin);
        const float EffR    = std::max(Cs.AngularRadiusRad, PixelSpanRad * 0.85f);

        float Disc = 0.0f;
        if (Ang < EffR + PixelSpanRad)
        {
            const float Edge = 1.0f - SmoothStepF(std::max(0.0f, EffR - PixelSpanRad), EffR + PixelSpanRad, Ang);
            const float Mu   = std::sqrt(std::max(0.0f, 1.0f - std::min(1.0f, (Ang / EffR) * (Ang / EffR))));
            const float Limb = 0.42f + 0.58f * Mu;
            Disc             = Edge * Limb * 14.0f;
        }

        const Vec3  RefUp       = (std::abs(Cs.DirectionUnit.Z) < 0.95f) ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
        const Vec3  TanU        = Normalise(Cross(Cs.DirectionUnit, RefUp));
        const Vec3  TanV        = Cross(Cs.DirectionUnit, TanU);
        const float Dx          = Dot(D, TanU);
        const float Dy          = Dot(D, TanV);
        const float Theta       = std::atan2(Dy, Dx) + 0.26f;
        const float NeedleWidth = std::max(PixelSpanRad * 1.15f, 0.00085f);
        const float PerpDist    = Ang * std::abs(std::sin(3.0f * Theta));
        const float SpikeNeedle = std::exp(-(PerpDist * PerpDist) / (NeedleWidth * NeedleWidth));
        const float SpikeReach  = std::exp(-Ang / std::max(0.018f, EffR * 15.0f));
        const float Spikes      = SpikeNeedle * SpikeReach;
        const float Corona =
            (std::exp(-Ang / (EffR * 1.65f)) * 0.42f + std::exp(-Ang / (EffR * 5.2f)) * 0.045f + Spikes * 0.52f) *
            Cs.CoronaGlowStrength;

        Total = Total + StarCol * (Cs.RadianceGain * (Disc + Corona) * SmoothStepF(-0.01f, 0.05f, D.Z));
    }
    return Total;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  MULTI-SUN 6-BODY LUNAR ATLAS DISC EVALUATION
//------------------------------------------------------------------------------------------------------------------------

CelestialRaySample CelestialDomeModel::EvaluateSingleMoon(const LunarBodyDescriptor&            Moon,
                                                          Vec3                                  RayDirUnit,
                                                          std::span<const CelestialSolarSource> SolarSources,
                                                          float                                 PixelSpanRad) const noexcept
{
    CelestialRaySample Out{};
    if (Moon.AngularRadiusRad <= 1.0e-5f || RayDirUnit.Z <= -0.02f || Moon.DirectionUnit.Z <= -0.05f)
    {
        return Out;
    }

    const float CosAng = std::clamp(Dot(RayDirUnit, Moon.DirectionUnit), -1.0f, 1.0f);
    const float Ang    = std::acos(CosAng);
    const float R      = Moon.AngularRadiusRad;

    // Multi-sun lunar atmospheric halo
    for (const CelestialSolarSource& SunSrc : SolarSources)
    {
        const Vec3  SunDir     = Normalise(SunSrc.DirectionUnit);
        const float PhaseAngle = std::acos(std::clamp(Dot(Moon.DirectionUnit * -1.0f, SunDir), -1.0f, 1.0f));
        const float IllumFrac  = 0.5f * (1.0f + std::cos(PhaseAngle));
        const float SunNorm    = 1.0f / std::max(1.0f, Length(SunSrc.IrradianceRgb));
        const float Halo = (std::exp(-Ang / (R * 1.8f)) * 0.045f + std::exp(-Ang * 8.0f) * 0.004f) *
                           Moon.SurfaceBrightness * (0.25f + 0.75f * IllumFrac);
        Out.RadianceRgb = Out.RadianceRgb + CompMul(Vec3{0.72f, 0.80f, 0.96f}, SunSrc.IrradianceRgb * SunNorm) * Halo;
    }

    if (Ang > R * (1.0f + 0.08f * Moon.AtmosphericHaze) + PixelSpanRad * 1.5f)
    {
        return Out;
    }

    const Vec3  RefUp = (std::abs(Moon.DirectionUnit.Z) < 0.98f) ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    const Vec3  East  = Normalise(Cross(RefUp, Moon.DirectionUnit));
    const Vec3  North = Cross(Moon.DirectionUnit, East);
    const float SinR  = std::max(1.0e-6f, std::sin(R));
    const float DiscA = Dot(RayDirUnit, East) / SinR;
    const float DiscB = Dot(RayDirUnit, North) / SinR;
    const float Rho2  = DiscA * DiscA + DiscB * DiscB;

    const float Edge = 1.0f - SmoothStepF(R * (1.0f - 0.06f * Moon.AtmosphericHaze) - PixelSpanRad, R + PixelSpanRad, Ang);
    if (Edge <= 1.0e-4f)
    {
        return Out;
    }

    const float DiscZ             = std::sqrt(std::max(0.0f, 1.0f - std::min(1.0f, Rho2)));
    const Vec3  SphereNormalWorld = Normalise(East * DiscA + North * DiscB + Moon.DirectionUnit * DiscZ);

    const float Ct = std::cos(Moon.AxialTiltRad);
    const float St = std::sin(Moon.AxialTiltRad);
    const Vec3  LocalN{DiscA, DiscB * Ct + DiscZ * St, -DiscB * St + DiscZ * Ct};

    const Vec3  SurfP       = LocalN * 14.0f;
    const float CratH0      = Fbm3D(SurfP);
    const float CratHx      = Fbm3D(SurfP + Vec3{0.08f, 0.0f, 0.0f});
    const float CratHy      = Fbm3D(SurfP + Vec3{0.0f, 0.08f, 0.0f});
    const float ReliefScale = (Moon.Preset == LunarSurfacePreset::ShroudTitan) ? 0.02f : 0.24f;
    const Vec3  PerturbedN  = Normalise(SphereNormalWorld + East * ((CratH0 - CratHx) * ReliefScale * 4.0f) +
                                        North * ((CratH0 - CratHy) * ReliefScale * 4.0f));
    const double Mu         = std::max(1.0e-3, static_cast<double>(DiscZ));

    Vec3 SurfaceRadiance{};
    const LunarAlbedoMap* AtlasLayer = LunarAtlas_.Ready() ? &LunarAtlas_.LayerMap(Moon.Preset) : nullptr;

    for (size_t SunIdx = 0u; SunIdx < SolarSources.size(); ++SunIdx)
    {
        const Vec3   SunDir     = Normalise(SolarSources[SunIdx].DirectionUnit);
        const float  SunNorm    = 1.0f / std::max(1.0f, Length(SolarSources[SunIdx].IrradianceRgb));
        const Vec3   SunCol     = SolarSources[SunIdx].IrradianceRgb * (SunNorm * 1.75f);
        const float  PhaseAngle = std::acos(std::clamp(Dot(Moon.DirectionUnit * -1.0f, SunDir), -1.0f, 1.0f));
        const double Mu0        = std::max(0.0, static_cast<double>(Dot(PerturbedN, SunDir)));
        const double Earthshine = (SunIdx == 0u) ? static_cast<double>(Moon.EarthshineStrength) : 0.0;

        if (AtlasLayer && AtlasLayer->Loaded())
        {
            const SpectrumRgb SolarSpec{SunCol.X, SunCol.Y, SunCol.Z};
            const SpectrumRgb Eval = EvaluateLunarDiscRadiance(*AtlasLayer,
                                                               static_cast<double>(DiscA),
                                                               static_cast<double>(DiscB),
                                                               Moon.SpinLongitudeRad,
                                                               Moon.LibrationLatitudeRad + static_cast<double>(Moon.AxialTiltRad * 0.25f),
                                                               Mu0,
                                                               Mu,
                                                               static_cast<double>(PhaseAngle),
                                                               SolarSpec,
                                                               Earthshine);
            SurfaceRadiance = SurfaceRadiance +
                              Vec3{static_cast<float>(Eval.R), static_cast<float>(Eval.G), static_cast<float>(Eval.B)} *
                                  Moon.SurfaceBrightness;
        }
        else
        {
            float      Thermal = 0.0f;
            const Vec3 Albedo  = EvaluateProceduralMoonAlbedoLinear(Moon.Preset, LocalN, 0.16f, Thermal);
            const float Lommel = (Mu0 > 0.0) ? static_cast<float>((2.0 * Mu0) / (Mu0 + Mu)) : 0.0f;
            const float OppDim = std::exp(-0.65f * PhaseAngle);
            const Vec3  EarthS = Vec3{0.045f, 0.062f, 0.095f} * static_cast<float>(Earthshine);
            SurfaceRadiance    = SurfaceRadiance + CompMul(Albedo, SunCol) * (Lommel * OppDim * Moon.SurfaceBrightness * 0.14f) +
                              CompMul(Albedo, EarthS) * Moon.SurfaceBrightness + Vec3{1.4f, 0.28f, 0.04f} * (Thermal * 0.35f);
        }
    }

    if (Moon.Preset == LunarSurfacePreset::EmberVolcanic)
    {
        float Thermal = 0.0f;
        (void)EvaluateProceduralMoonAlbedoLinear(Moon.Preset, LocalN, 0.16f, Thermal);
        SurfaceRadiance = SurfaceRadiance + Vec3{1.6f, 0.32f, 0.04f} * (Thermal * 0.25f);
    }

    const float RimH = std::pow(1.0f - static_cast<float>(Mu), 3.0f) * Moon.AtmosphericHaze * 0.35f;
    SurfaceRadiance  = SurfaceRadiance + Vec3{0.85f, 0.72f, 0.55f} * RimH;

    Out.HitLunarDisc           = true;
    Out.OcclusionTransmittance = 1.0f - Edge;
    Out.RadianceRgb            = Out.RadianceRgb + SurfaceRadiance * Edge;
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                      MULTI-SUN SIMULATED PLANETARY ATMOSPHERE + BANDED BODY + PLANETARY RINGS
//------------------------------------------------------------------------------------------------------------------------

CelestialRaySample CelestialDomeModel::EvaluateSinglePlanetAndRings(
    const PlanetaryBodyDescriptor&        Planet,
    Vec3                                  RayDirUnit,
    std::span<const CelestialSolarSource> SolarSources,
    float                                 PixelSpanRad) const noexcept
{
    CelestialRaySample Out{};
    if (Planet.AngularRadiusRad <= 1.0e-5f || RayDirUnit.Z <= -0.02f || Planet.DirectionUnit.Z <= -0.08f)
    {
        return Out;
    }

    const float SinAlpha     = std::sin(std::clamp(Planet.AngularRadiusRad, 1.0e-4f, 0.85f));
    const float PlanetDist   = 1.0f / SinAlpha;
    const Vec3  PlanetCentre = Planet.DirectionUnit * PlanetDist;
    const float Rp           = 1.0f;
    const float Ra           = Rp * (1.0f + std::max(0.005f, Planet.Atmosphere.ShellHeightFraction));

    const float MaxOuterRatio = Planet.Rings.Enabled ? std::max(Ra, Planet.Rings.OuterRadiusRatio + 0.15f) : (Ra + 0.1f);
    const float MaxConeSin    = std::min(0.99f, MaxOuterRatio / PlanetDist);
    if (Dot(RayDirUnit, Planet.DirectionUnit) < std::sqrt(std::max(0.0f, 1.0f - MaxConeSin * MaxConeSin)) - 0.05f)
    {
        return Out;
    }

    float      SolidT0  = -1.0f;
    float      SolidT1  = -1.0f;
    const bool HitSolid = IntersectRaySphere({0.0f, 0.0f, 0.0f}, RayDirUnit, PlanetCentre, Rp, SolidT0, SolidT1) && (SolidT0 > 0.0f);

    float      AtmoT0  = -1.0f;
    float      AtmoT1  = -1.0f;
    const bool HitAtmo = Planet.Atmosphere.Enabled &&
                         IntersectRaySphere({0.0f, 0.0f, 0.0f}, RayDirUnit, PlanetCentre, Ra, AtmoT0, AtmoT1) && (AtmoT1 > 0.0f);

    // 1. Multi-sun Tilted Ring Plane intersection + mutual planet shadow on rings
    bool  RingHitValid = false;
    float RingT        = 1.0e9f;
    float RingAlpha    = 0.0f;
    Vec3  RingRadRgb{0.0f, 0.0f, 0.0f};

    if (Planet.Rings.Enabled)
    {
        const float Denom = Dot(RayDirUnit, Planet.Rings.RingNormalUnit);
        if (std::abs(Denom) > 1.0e-5f)
        {
            const float TPlane = Dot(PlanetCentre, Planet.Rings.RingNormalUnit) / Denom;
            if (TPlane > 0.0f)
            {
                const Vec3  RingPt = RayDirUnit * TPlane;
                const float RingR  = Length(RingPt - PlanetCentre);
                float       TauN   = 0.0f;
                Vec3        RingAlb{};
                SampleRingRadialProfile(Planet.Rings, RingR, TauN, RingAlb);

                if (TauN > 1.0e-4f)
                {
                    RingHitValid          = true;
                    RingT                 = TPlane;
                    Out.HitPlanetaryRings = true;
                    const float SlantTau  = TauN / std::max(0.08f, std::abs(Denom));
                    RingAlpha             = std::clamp(1.0f - std::exp(-SlantTau), 0.0f, 1.0f);
                    const Vec3 RelRing    = RingPt - PlanetCentre;
                    RingRadRgb            = RingAlb * (0.018f * RingAlpha);

                    for (const CelestialSolarSource& SunSrc : SolarSources)
                    {
                        const Vec3  SunDir  = Normalise(SunSrc.DirectionUnit);
                        const float SunNorm = 1.0f / std::max(1.0f, Length(SunSrc.IrradianceRgb));
                        const Vec3  SunCol  = SunSrc.IrradianceRgb * (SunNorm * 1.75f);
                        const float ProjSun = Dot(RelRing, SunDir);
                        float       SunVis  = 1.0f;
                        Vec3        SunTint = {1.0f, 1.0f, 1.0f};

                        if (ProjSun < 0.0f)
                        {
                            const float PerpDist = Length(RelRing - SunDir * ProjSun);
                            SunVis               = SmoothStepF(Rp - 0.015f, Rp + 0.018f, PerpDist);
                            if (SunVis < 0.5f)
                            {
                                Out.InPlanetShadowOnRings = true;
                            }
                            if (Planet.Atmosphere.Enabled && PerpDist > Rp - 0.01f && PerpDist < Ra)
                            {
                                const float LimbFrac = SmoothStepF(Rp, Ra, PerpDist);
                                SunTint              = LerpVec3({0.85f, 0.28f, 0.08f}, {1.0f, 1.0f, 1.0f}, LimbFrac);
                            }
                        }

                        const float CosScatter = Dot(RayDirUnit, SunDir);
                        const float G          = Planet.Rings.IcePhaseAsymmetryG;
                        const float HgDenom    = std::pow(std::max(0.05f, 1.0f + G * G - 2.0f * G * CosScatter), 1.5f);
                        const float PhaseIce   = 0.65f + 0.35f * ((1.0f - G * G) / HgDenom);
                        const float SunOblique = 0.28f + 0.72f * std::abs(Dot(SunDir, Planet.Rings.RingNormalUnit));

                        RingRadRgb = RingRadRgb + CompMul(CompMul(RingAlb, SunCol), SunTint) *
                                                      (SunVis * SunOblique * PhaseIce * RingAlpha * Planet.SurfaceAlbedoScale * 0.115f);
                    }
                }
            }
        }
    }

    // 2. Banded / Oceanic / Desert / Ice-Giant Planet Surface + multi-sun Ring Shadow on Planet
    Vec3  PlanetBodyRad{0.0f, 0.0f, 0.0f};
    float PlanetEdgeCoverage = 0.0f;
    if (HitSolid)
    {
        Out.HitPlanetBody     = true;
        const float CenterAng = std::acos(std::clamp(Dot(RayDirUnit, Planet.DirectionUnit), -1.0f, 1.0f));
        PlanetEdgeCoverage    = 1.0f - SmoothStepF(Planet.AngularRadiusRad - PixelSpanRad, Planet.AngularRadiusRad + PixelSpanRad, CenterAng);

        const Vec3 HitWorld   = RayDirUnit * SolidT0;
        const Vec3 UnitSphere = Normalise(HitWorld - PlanetCentre);

        const Vec3  AxisZ  = Planet.SpinAxisUnit;
        const Vec3  RefX   = (std::abs(AxisZ.Z) < 0.92f) ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
        const Vec3  AxisX  = Normalise(Cross(RefX, AxisZ));
        const Vec3  AxisY  = Cross(AxisZ, AxisX);
        const float LatSin = std::clamp(Dot(UnitSphere, AxisZ), -1.0f, 1.0f);
        const float Lon    = std::atan2(Dot(UnitSphere, AxisY), Dot(UnitSphere, AxisX)) + Planet.SpinPhaseRad;

        Vec3 CloudDeckAlbedo{0.75f, 0.68f, 0.56f};
        bool OceanSpec = false;

        if (Planet.Archetype == PlanetarySurfaceArchetype::BandedGasGiant)
        {
            const float Warp    = 0.045f * (Fbm3D({std::cos(Lon) * 3.5f, std::sin(Lon) * 3.5f, LatSin * 9.0f}) - 0.5f);
            const float L       = LatSin + Warp;
            const float Band1   = 0.5f + 0.5f * std::cos(L * 22.0f);
            const float Band2   = 0.5f + 0.5f * std::sin(L * 47.0f + 1.1f);
            const Vec3  ZoneCol = {0.90f, 0.83f, 0.70f};
            const Vec3  BeltCol = {0.66f, 0.46f, 0.30f};
            const Vec3  PoleCol = {0.36f, 0.42f, 0.52f};
            CloudDeckAlbedo     = LerpVec3(BeltCol, ZoneCol, 0.65f * Band1 + 0.35f * Band2);
            CloudDeckAlbedo     = LerpVec3(CloudDeckAlbedo, PoleCol, SmoothStepF(0.68f, 0.95f, std::abs(LatSin)));

            const float DLon    = std::sin(0.5f * (Lon - Planet.SpinPhaseRad - 0.35f)) * 2.0f;
            const float DLat    = (LatSin + 0.32f) * 2.6f;
            const float StormR2 = (DLon * DLon) / 0.11f + (DLat * DLat) / 0.045f;
            if (StormR2 < 1.0f)
            {
                CloudDeckAlbedo = LerpVec3(CloudDeckAlbedo, {0.86f, 0.38f, 0.22f}, SmoothStepF(1.0f, 0.15f, StormR2) * 0.82f);
            }
        }
        else if (Planet.Archetype == PlanetarySurfaceArchetype::OceanicTerrestrial)
        {
            const float Cont       = Fbm3D(UnitSphere * 3.4f + Vec3{2.1f, 0.8f, 4.3f});
            const float Polar      = SmoothStepF(0.74f, 0.88f, std::abs(LatSin));
            const bool  Land       = Cont > 0.52f;
            CloudDeckAlbedo        = Land ? LerpVec3({0.14f, 0.32f, 0.12f}, {0.48f, 0.40f, 0.26f}, SmoothStepF(0.52f, 0.74f, Cont))
                                          : Vec3{0.03f, 0.09f, 0.24f};
            CloudDeckAlbedo        = LerpVec3(CloudDeckAlbedo, {0.92f, 0.94f, 0.97f}, Polar);
            const float CloudSwirl = SmoothStepF(0.48f, 0.75f, Fbm3D(UnitSphere * 5.8f + Vec3{6.2f, 1.3f, 2.9f}));
            CloudDeckAlbedo        = LerpVec3(CloudDeckAlbedo, {0.90f, 0.92f, 0.95f}, CloudSwirl * 0.75f);
            OceanSpec              = (!Land && Polar < 0.3f && CloudSwirl < 0.5f);
        }
        else if (Planet.Archetype == PlanetarySurfaceArchetype::RustyDesertWorld)
        {
            const float Basin = Fbm3D(UnitSphere * 4.2f);
            CloudDeckAlbedo   = LerpVec3({0.28f, 0.14f, 0.08f}, {0.72f, 0.36f, 0.16f}, Basin);
            CloudDeckAlbedo   = LerpVec3(CloudDeckAlbedo, {0.92f, 0.90f, 0.88f}, SmoothStepF(0.84f, 0.95f, std::abs(LatSin)));
        }
        else
        {
            const float Band = 0.5f + 0.5f * std::cos(LatSin * 16.0f);
            CloudDeckAlbedo  = LerpVec3({0.16f, 0.48f, 0.78f}, {0.38f, 0.78f, 0.92f}, Band);
        }

        PlanetBodyRad = CloudDeckAlbedo * 0.012f;
        const float NdotV = std::max(0.08f, Dot(UnitSphere, RayDirUnit * -1.0f));

        for (const CelestialSolarSource& SunSrc : SolarSources)
        {
            const Vec3  SunDir        = Normalise(SunSrc.DirectionUnit);
            const float SunNorm       = 1.0f / std::max(1.0f, Length(SunSrc.IrradianceRgb));
            const Vec3  SunCol        = SunSrc.IrradianceRgb * (SunNorm * 1.75f);
            bool        RingShadowHit = false;
            const float RingTrans     = EvaluateRingShadowTransmittance(Planet.Rings, PlanetCentre, HitWorld, SunDir, RingShadowHit);
            if (RingShadowHit && Dot(UnitSphere, SunDir) > 0.02f)
            {
                Out.InRingShadowOnPlanet = true;
            }
            const float NdotL    = std::max(0.0f, Dot(UnitSphere, SunDir));
            const float Minnaert = std::pow(NdotL, 0.84f) * std::pow(NdotV, -0.14f);
            float       Spec     = 0.0f;
            if (OceanSpec)
            {
                const Vec3 HalfV = Normalise(SunDir - RayDirUnit);
                Spec             = std::pow(std::max(0.0f, Dot(UnitSphere, HalfV)), 42.0f) * 0.45f;
            }
            PlanetBodyRad = PlanetBodyRad + CompMul(CloudDeckAlbedo, SunCol) * (Minnaert * RingTrans * Planet.SurfaceAlbedoScale * 0.145f) +
                            SunCol * (Spec * RingTrans * 0.18f);
        }
    }

    // 3. Simulated Spherical Planetary Atmosphere Ray-March across all active suns
    Vec3 AtmoInscatter{0.0f, 0.0f, 0.0f};
    Vec3 AtmoViewTrans{1.0f, 1.0f, 1.0f};
    if (HitAtmo)
    {
        Out.HitPlanetAtmosphere = true;
        const float TStart      = std::max(0.0f, AtmoT0);
        const float TEnd        = HitSolid ? std::min(AtmoT1, SolidT0) : AtmoT1;

        if (TEnd > TStart)
        {
            const auto& AP        = Planet.Atmosphere;
            const float ShellSpan = std::max(1.0e-4f, Ra - Rp);
            const float Hr        = std::max(1.0e-4f, AP.RayleighScaleFraction * Rp);
            const float Hm        = std::max(1.0e-4f, AP.MieScaleFraction * Rp);
            const float PhysScale = AP.OpticalDensityScale;

            const Vec3 BetaR = AP.RayleighScattering;
            const Vec3 BetaM{AP.MieScattering, AP.MieScattering, AP.MieScattering};
            const Vec3 BetaO = AP.AbsorptionCoefficients;

            constexpr int ViewSteps  = 12;
            constexpr int LightSteps = 6;
            const float   Dt         = (TEnd - TStart) / static_cast<float>(ViewSteps);

            float OdR = 0.0f;
            float OdM = 0.0f;

            for (int Step = 0; Step < ViewSteps; ++Step)
            {
                const float Tm  = TStart + (static_cast<float>(Step) + 0.5f) * Dt;
                const Vec3  Pos = RayDirUnit * Tm;
                const float Alt = std::max(0.0f, Length(Pos - PlanetCentre) - Rp);
                if (Alt > ShellSpan * 1.05f)
                {
                    continue;
                }
                const float SegPhys = Dt * PhysScale;
                const float RhoR    = std::exp(-Alt / Hr) * SegPhys;
                const float RhoM    = std::exp(-Alt / Hm) * SegPhys;
                OdR += RhoR;
                OdM += RhoM;

                for (const CelestialSolarSource& SunSrc : SolarSources)
                {
                    const Vec3  SunDir  = Normalise(SunSrc.DirectionUnit);
                    const float SunNorm = 1.0f / std::max(1.0f, Length(SunSrc.IrradianceRgb));
                    const Vec3  SunCol  = SunSrc.IrradianceRgb * (SunNorm * 1.75f);

                    float      SunCore0 = -1.0f, SunCore1 = -1.0f;
                    const bool Blocked = IntersectRaySphere(Pos, SunDir, PlanetCentre, Rp * 0.992f, SunCore0, SunCore1) && (SunCore0 > 0.0f);
                    if (Blocked)
                    {
                        continue;
                    }
                    float SunAtm0 = -1.0f, SunAtm1 = -1.0f;
                    if (!IntersectRaySphere(Pos, SunDir, PlanetCentre, Ra, SunAtm0, SunAtm1) || SunAtm1 <= 0.0f)
                    {
                        continue;
                    }
                    const float DtSun = SunAtm1 / static_cast<float>(LightSteps);
                    float       OlR = 0.0f, OlM = 0.0f;
                    for (int Ls = 0; Ls < LightSteps; ++Ls)
                    {
                        const Vec3  QPos = Pos + SunDir * ((static_cast<float>(Ls) + 0.5f) * DtSun);
                        const float QAlt = std::max(0.0f, Length(QPos - PlanetCentre) - Rp);
                        OlR += std::exp(-QAlt / Hr) * (DtSun * PhysScale);
                        OlM += std::exp(-QAlt / Hm) * (DtSun * PhysScale);
                    }
                    bool        DummyShad = false;
                    const float RingAtt   = EvaluateRingShadowTransmittance(Planet.Rings, PlanetCentre, Pos, SunDir, DummyShad);
                    const Vec3  TauTot    = BetaR * (OdR + OlR) + BetaM * (1.1f * (OdM + OlM)) + BetaO * (OdR + OlR);
                    const Vec3  Att       = ExpNeg(TauTot) * RingAtt;

                    const float Mu  = Dot(RayDirUnit, SunDir);
                    const float G   = AP.MieAnisotropyG;
                    const float PhR = (3.0f / (16.0f * PiF)) * (1.0f + Mu * Mu);
                    const float PhM = (3.0f / (8.0f * PiF)) * ((1.0f - G * G) * (1.0f + Mu * Mu)) /
                                      ((2.0f + G * G) * std::pow(std::max(0.04f, 1.0f + G * G - 2.0f * G * Mu), 1.5f));

                    AtmoInscatter = AtmoInscatter + CompMul(CompMul(Att * RhoR, BetaR) * PhR + CompMul(Att * RhoM, BetaM) * PhM, SunCol) * 1.45f;
                }
            }
            AtmoViewTrans = ExpNeg(BetaR * OdR + BetaM * (1.1f * OdM) + BetaO * OdR);
        }
    }

    const float PlanetMidT    = HitSolid ? SolidT0 : (HitAtmo ? 0.5f * (std::max(0.0f, AtmoT0) + AtmoT1) : 1.0e9f);
    const Vec3  PlanetTotal   = CompMul(PlanetBodyRad, AtmoViewTrans) * PlanetEdgeCoverage + AtmoInscatter;
    const float PlanetTransBg = HitSolid ? ((1.0f - PlanetEdgeCoverage) * AtmoViewTrans.Y) : AtmoViewTrans.Y;

    if (RingHitValid)
    {
        if (RingT < PlanetMidT)
        {
            Out.RadianceRgb            = RingRadRgb + PlanetTotal * (1.0f - RingAlpha);
            Out.OcclusionTransmittance = (1.0f - RingAlpha) * PlanetTransBg;
        }
        else
        {
            const Vec3 BackRingBehindPlanet = CompMul(RingRadRgb, AtmoViewTrans) * (HitSolid ? (1.0f - PlanetEdgeCoverage) : 1.0f);
            Out.RadianceRgb                 = PlanetTotal + BackRingBehindPlanet;
            Out.OcclusionTransmittance      = PlanetTransBg * (1.0f - RingAlpha);
        }
    }
    else
    {
        Out.RadianceRgb            = PlanetTotal;
        Out.OcclusionTransmittance = PlanetTransBg;
    }

    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        UNIFIED MULTI-SUN CELESTIAL RAY EVALUATION
//------------------------------------------------------------------------------------------------------------------------

CelestialRaySample CelestialDomeModel::EvaluateCelestialRay(Vec3                                  DirectionUnit,
                                                            std::span<const CelestialSolarSource> SolarSources,
                                                            float                                 PixelSpanRad) const noexcept
{
    const Vec3 D = Normalise(DirectionUnit);

    // Night starfield visibility is suppressed by the highest-elevation sun in the realm!
    float HighestSunElevDeg = -90.0f;
    for (const CelestialSolarSource& Src : SolarSources)
    {
        const Vec3  Sun = Normalise(Src.DirectionUnit);
        const float El  = std::asin(std::clamp(Sun.Z, -1.0f, 1.0f)) * (180.0f / PiF);
        HighestSunElevDeg = std::max(HighestSunElevDeg, El);
    }
    const float NightVis = 1.0f - SmoothStepF(-13.0f, -1.5f, HighestSunElevDeg);

    const Vec3 BgStarsAndCustom = EvaluateCatalogueStarsAndMilkyWay(D, NightVis, PixelSpanRad) +
                                  EvaluateCustomStarsAlongRay(D, PixelSpanRad);

    CelestialRaySample Final{};
    Final.OcclusionTransmittance = 1.0f;

    Vec3 PlanetAccum{0.0f, 0.0f, 0.0f};
    for (const auto& Planet : Planets_)
    {
        const CelestialRaySample Ps = EvaluateSinglePlanetAndRings(Planet, D, SolarSources, PixelSpanRad);
        PlanetAccum                 = PlanetAccum * Ps.OcclusionTransmittance + Ps.RadianceRgb;
        Final.OcclusionTransmittance *= Ps.OcclusionTransmittance;
        Final.HitPlanetBody         = Final.HitPlanetBody || Ps.HitPlanetBody;
        Final.HitPlanetAtmosphere   = Final.HitPlanetAtmosphere || Ps.HitPlanetAtmosphere;
        Final.HitPlanetaryRings     = Final.HitPlanetaryRings || Ps.HitPlanetaryRings;
        Final.InPlanetShadowOnRings = Final.InPlanetShadowOnRings || Ps.InPlanetShadowOnRings;
        Final.InRingShadowOnPlanet  = Final.InRingShadowOnPlanet || Ps.InRingShadowOnPlanet;
    }

    Vec3  MoonAccum{0.0f, 0.0f, 0.0f};
    float MoonTrans = 1.0f;
    for (const auto& Moon : Moons_)
    {
        const CelestialRaySample Ms = EvaluateSingleMoon(Moon, D, SolarSources, PixelSpanRad);
        MoonAccum                   = MoonAccum * Ms.OcclusionTransmittance + Ms.RadianceRgb;
        MoonTrans *= Ms.OcclusionTransmittance;
        Final.HitLunarDisc          = Final.HitLunarDisc || Ms.HitLunarDisc;
    }

    Final.RadianceRgb            = (BgStarsAndCustom * Final.OcclusionTransmittance + PlanetAccum) * MoonTrans + MoonAccum;
    Final.OcclusionTransmittance *= MoonTrans;
    return Final;
}

CelestialRaySample CelestialDomeModel::EvaluateCelestialRaySingleSun(Vec3  DirectionUnit,
                                                                     Vec3  PrimarySunDirectionUnit,
                                                                     Vec3  PrimarySunRadianceRgb,
                                                                     float SolarElevationDegrees,
                                                                     float PixelSpanRad) const noexcept
{
    Vec3 SunDir = Normalise(PrimarySunDirectionUnit);
    // Honour explicit SolarElevationDegrees when caller passes an independent solar elevation angle
    const float ExplicitSinZ = std::sin(SolarElevationDegrees * (PiF / 180.0f));
    const float HorizLen     = std::hypot(SunDir.X, SunDir.Y);
    const float TargetHoriz  = std::sqrt(std::max(0.0f, 1.0f - ExplicitSinZ * ExplicitSinZ));
    if (HorizLen > 1e-5f)
    {
        SunDir = Normalise(Vec3{SunDir.X * (TargetHoriz / HorizLen), SunDir.Y * (TargetHoriz / HorizLen), ExplicitSinZ});
    }
    const CelestialSolarSource Single{SunDir, PrimarySunRadianceRgb, 0.0046524f};
    return EvaluateCelestialRay(DirectionUnit, std::span<const CelestialSolarSource>(&Single, 1u), PixelSpanRad);
}

Vec3 CelestialDomeModel::EvaluateNocturnalDirectIrradiance(Vec3                                  SurfaceNormalUnit,
                                                           std::span<const CelestialSolarSource> SolarSources) const noexcept
{
    Vec3 TotalIrr{0.0f, 0.0f, 0.0f};

    for (const auto& Cs : CustomStars_)
    {
        if (Cs.DirectionUnit.Z <= 0.0f || Cs.SceneIrradianceGain <= 0.0f)
        {
            continue;
        }
        const float NdotL = std::max(0.0f, Dot(SurfaceNormalUnit, Cs.DirectionUnit));
        if (NdotL > 0.0f)
        {
            TotalIrr = TotalIrr + KelvinToBlackbodyChromaticity(Cs.TemperatureKelvin) * (Cs.SceneIrradianceGain * NdotL);
        }
    }

    for (const auto& Moon : Moons_)
    {
        if (Moon.DirectionUnit.Z <= 0.0f || Moon.SceneBounceIrradiance <= 0.0f)
        {
            continue;
        }
        const float NdotM = std::max(0.0f, Dot(SurfaceNormalUnit, Moon.DirectionUnit));
        if (NdotM <= 0.0f)
        {
            continue;
        }
        for (const CelestialSolarSource& Src : SolarSources)
        {
            const Vec3  Sun       = Normalise(Src.DirectionUnit);
            const float PhaseAng  = std::acos(std::clamp(Dot(Moon.DirectionUnit * -1.0f, Sun), -1.0f, 1.0f));
            const float PhaseGain = std::exp(-0.9f * PhaseAng) * 0.5f * (1.0f + std::cos(PhaseAng));
            const float SunNorm   = 1.0f / std::max(1.0f, Length(Src.IrradianceRgb));
            const Vec3  MoonTint  = CompMul({0.78f, 0.84f, 0.96f}, Src.IrradianceRgb * SunNorm);
            TotalIrr              = TotalIrr + MoonTint * (Moon.SceneBounceIrradiance * (0.08f + 0.92f * PhaseGain) * NdotM);
        }
    }
    return TotalIrr;
}

} // namespace Frontier
