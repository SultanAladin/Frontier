/*==============================================================================================================================================
                                                    CELESTIALSPHEREEVALUATION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the J2000 Hipparcos bright starfield, Galactic-plane-weighted Pogson stars, 3D dust-extinguished Milky Way,
//    sub-pixel Fraunhofer Airy / Moffat optical PSF, 2K cratered lunar regolith atlas, and 3D spherical lunar atmosphere radiative transfer.

#include "Graphics/Atmosphere/CelestialSphereEvaluation.h"

#if __has_include("stb_image.h")
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"
#define FRONTIER_CELESTIAL_STB_AVAILABLE 1
#else
#define FRONTIER_CELESTIAL_STB_AVAILABLE 0
#endif

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr float    PiConstant      = 3.14159265358979323846f;
constexpr float    TauConstant     = 6.28318530717958647692f;
constexpr float    DegToRad        = PiConstant / 180.0f;
constexpr uint32_t LunarTileWidth  = 1024u;
constexpr uint32_t LunarTileHeight = 512u;
constexpr uint32_t LunarPresetCount = 6u;

[[nodiscard]] uint32_t HashCelestialInt3(int32_t XIdx, int32_t YIdx, int32_t ZIdx) noexcept
{
    uint32_t Word = static_cast<uint32_t>(XIdx) * 374761393u
                  + static_cast<uint32_t>(YIdx) * 668265263u
                  + static_cast<uint32_t>(ZIdx) * 2147483647u;
    Word = (Word ^ (Word >> 13u)) * 1274126177u;
    return Word ^ (Word >> 16u);
}

[[nodiscard]] float HashCelestialUnit(int32_t XIdx, int32_t YIdx, int32_t ZIdx) noexcept
{
    return static_cast<float>(HashCelestialInt3(XIdx, YIdx, ZIdx)) * (1.0f / 4294967296.0f);
}

[[nodiscard]] float EvaluateLatticeNoise3(float XCoord, float YCoord, float ZCoord) noexcept
{
    const int32_t IX = static_cast<int32_t>(std::floor(XCoord));
    const int32_t IY = static_cast<int32_t>(std::floor(YCoord));
    const int32_t IZ = static_cast<int32_t>(std::floor(ZCoord));

    const float FX = XCoord - static_cast<float>(IX);
    const float FY = YCoord - static_cast<float>(IY);
    const float FZ = ZCoord - static_cast<float>(IZ);

    const float UX = FX * FX * (3.0f - 2.0f * FX);
    const float UY = FY * FY * (3.0f - 2.0f * FY);
    const float UZ = FZ * FZ * (3.0f - 2.0f * FZ);

    auto Mix = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };
    const float N000 = HashCelestialUnit(IX, IY, IZ),         N100 = HashCelestialUnit(IX + 1, IY, IZ);
    const float N010 = HashCelestialUnit(IX, IY + 1, IZ),     N110 = HashCelestialUnit(IX + 1, IY + 1, IZ);
    const float N001 = HashCelestialUnit(IX, IY, IZ + 1),     N101 = HashCelestialUnit(IX + 1, IY, IZ + 1);
    const float N011 = HashCelestialUnit(IX, IY + 1, IZ + 1), N111 = HashCelestialUnit(IX + 1, IY + 1, IZ + 1);

    return Mix(
        Mix(Mix(N000, N100, UX), Mix(N010, N110, UX), UY),
        Mix(Mix(N001, N101, UX), Mix(N011, N111, UX), UY),
        UZ);
}

[[nodiscard]] float EvaluateRotatedCelestialFbm(const Vector3f& Point, uint32_t Octaves) noexcept
{
    float Sum = 0.0f;
    float Amp = 0.5f;
    float PX = Point.XCoord, PY = Point.YCoord, PZ = Point.ZCoord;
    for (uint32_t Octave = 0u; Octave < Octaves; ++Octave)
    {
        Sum += Amp * EvaluateLatticeNoise3(PX, PY, PZ);
        const float RX =  0.80f * PX - 0.48f * PY + 0.36f * PZ;
        const float RY =  0.48f * PX + 0.86f * PY + 0.16f * PZ;
        const float RZ = -0.36f * PX + 0.04f * PY + 0.93f * PZ;
        PX = RX * 2.05f + 11.3f;
        PY = RY * 2.05f - 6.7f;
        PZ = RZ * 2.05f + 4.9f;
        Amp *= 0.5f;
    }
    return Sum;
}

[[nodiscard]] float DecodeSrgbByteToLinear(uint8_t ByteVal) noexcept
{
    const float Encoded = static_cast<float>(ByteVal) * (1.0f / 255.0f);
    return (Encoded <= 0.04045f) ? (Encoded / 12.92f) : std::pow((Encoded + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] SpectralColor RetrievePresetMineralTint(LunarSurfaceCategory Category) noexcept
{
    switch (Category)
    {
        case LunarSurfaceCategory::LunaRegolith:  return SpectralColor{ 0.96f, 0.94f, 0.90f };
        case LunarSurfaceCategory::EmberBasalt:   return SpectralColor{ 1.00f, 0.62f, 0.38f };
        case LunarSurfaceCategory::GlacierCryo:   return SpectralColor{ 0.82f, 0.93f, 1.00f };
        case LunarSurfaceCategory::SulfurMantle:  return SpectralColor{ 1.00f, 0.89f, 0.46f };
        case LunarSurfaceCategory::ShroudSlate:   return SpectralColor{ 0.76f, 0.80f, 0.88f };
        case LunarSurfaceCategory::ShardSilicate: return SpectralColor{ 0.84f, 0.98f, 0.94f };
    }
    return SpectralColor{ 0.95f, 0.94f, 0.90f };
}

//------------------------------------------------------------------------------------------------------------------------
//                                            2K LUNAR ATLAS + CRATER ELEVATION STORE
//------------------------------------------------------------------------------------------------------------------------

struct LunarAtlasStore
{
    std::vector<SpectralColor> AlbedoTexels    {};   // [-] - 1024 x (512 * 6) linear RGB regolith reflectance
    std::vector<float>         ElevationTexels {};   // [-] - 1024 x (512 * 6) normalized crater relief [-1..1]
};

// 📝 Procedural multi-basin + ringed impact crater + ray-ejecta fallback when EngineContent/Celestial/Moons is absent.
void SynthesizeProceduralLunarTile(LunarAtlasStore& Store, uint32_t PresetOrdinal) noexcept
{
    const SpectralColor MineralTint = RetrievePresetMineralTint(static_cast<LunarSurfaceCategory>(PresetOrdinal));
    const float         SeedShift   = static_cast<float>(PresetOrdinal) * 9.37f;

    // 14 major impact craters/basins per moon with raised rims, flat floors, central peaks, and bright ejecta rays.
    struct ImpactBasin
    {
        Vector3f CenterUnit {};
        float    AngularRadius = 0.2f;
        float    DepthScale    = 1.0f;
        bool     RaySystem     = false;
    };
    std::array<ImpactBasin, 14u> Basins{};
    for (uint32_t BIdx = 0u; BIdx < 14u; ++BIdx)
    {
        const float U1 = HashCelestialUnit(static_cast<int32_t>(PresetOrdinal * 31u + BIdx), 17, 53);
        const float U2 = HashCelestialUnit(static_cast<int32_t>(PresetOrdinal * 31u + BIdx), 89, 101);
        const float U3 = HashCelestialUnit(static_cast<int32_t>(PresetOrdinal * 31u + BIdx), 137, 211);
        const float ZC = U1 * 1.72f - 0.86f;
        const float RC = std::sqrt(std::max(0.0f, 1.0f - ZC * ZC));
        const float Ph = U2 * TauConstant;
        Basins[BIdx].CenterUnit    = Vector3f{ RC * std::cos(Ph), RC * std::sin(Ph), ZC };
        Basins[BIdx].AngularRadius = 0.065f + 0.28f * std::pow(U3, 1.6f);
        Basins[BIdx].DepthScale    = 0.45f + 0.55f * U3;
        Basins[BIdx].RaySystem     = (BIdx >= 10u);
    }

    for (uint32_t Y = 0u; Y < LunarTileHeight; ++Y)
    {
        const float Theta = ((static_cast<float>(Y) + 0.5f) / static_cast<float>(LunarTileHeight)) * PiConstant;
        const float SinT  = std::sin(Theta);
        const float CosT  = std::cos(Theta);

        for (uint32_t X = 0u; X < LunarTileWidth; ++X)
        {
            const float Phi = ((static_cast<float>(X) + 0.5f) / static_cast<float>(LunarTileWidth)) * TauConstant;
            const Vector3f UnitSphere{ SinT * std::cos(Phi), SinT * std::sin(Phi), CosT };

            const float MareField = EvaluateRotatedCelestialFbm(
                Vector3f{ UnitSphere.XCoord * 1.85f + SeedShift, UnitSphere.YCoord * 1.85f - 2.1f, UnitSphere.ZCoord * 1.85f }, 5u);
            const float MareMask  = std::clamp((0.48f - MareField) / 0.14f, 0.0f, 1.0f);

            const float HighlandRough = EvaluateRotatedCelestialFbm(
                Vector3f{ UnitSphere.XCoord * 14.0f - SeedShift, UnitSphere.YCoord * 14.0f + 3.4f, UnitSphere.ZCoord * 14.0f }, 4u);
            const float MicroCraters  = EvaluateRotatedCelestialFbm(
                Vector3f{ UnitSphere.XCoord * 42.0f + 1.9f, UnitSphere.YCoord * 42.0f - SeedShift, UnitSphere.ZCoord * 42.0f }, 3u);

            float CraterElevation = (HighlandRough - 0.5f) * (0.55f - 0.35f * MareMask) + (MicroCraters - 0.5f) * 0.24f;
            float EjectaBright    = 0.0f;

            for (const ImpactBasin& Basin : Basins)
            {
                const float AngDist = std::acos(std::clamp(DotVector(UnitSphere, Basin.CenterUnit), -1.0f, 1.0f));
                const float Ratio   = AngDist / Basin.AngularRadius;
                if (Ratio < 2.6f)
                {
                    if (Ratio < 1.0f)
                    {
                        const float Bowl = (Ratio * Ratio - 1.0f) * 0.68f;
                        const float Peak = 0.20f * std::exp(-Ratio * Ratio * 24.0f);
                        CraterElevation += (Bowl + Peak) * Basin.DepthScale;
                    }
                    const float RimRing = 0.34f * std::exp(-(Ratio - 1.02f) * (Ratio - 1.02f) / 0.032f);
                    CraterElevation += RimRing * Basin.DepthScale;

                    if (Basin.RaySystem && Ratio > 0.85f)
                    {
                        const Vector3f TangentVec = NormalizeVector(CrossVector(Basin.CenterUnit, UnitSphere));
                        const float    AzRay      = EvaluateLatticeNoise3(
                            TangentVec.XCoord * 14.0f + SeedShift,
                            TangentVec.YCoord * 14.0f,
                            TangentVec.ZCoord * 14.0f);
                        EjectaBright += std::clamp((AzRay - 0.52f) * 2.4f, 0.0f, 1.0f) * std::exp(-(Ratio - 0.85f) * 1.45f) * 0.28f;
                    }
                }
            }

            const float AlbedoScalar = std::clamp(
                (0.76f - 0.42f * MareMask) + 0.14f * CraterElevation + EjectaBright,
                0.16f,
                1.0f);

            const std::size_t Index = (static_cast<std::size_t>(PresetOrdinal * LunarTileHeight + Y) * LunarTileWidth) + X;
            Store.AlbedoTexels[Index]    = ScaleSpectral(MineralTint, AlbedoScalar);
            Store.ElevationTexels[Index] = std::clamp(CraterElevation - 0.25f * MareMask, -1.0f, 1.0f);
        }
    }
}

void ResampleRgbBufferIntoTile(const uint8_t*   SourceRgb,
                               uint32_t         SourceWidth,
                               uint32_t         SourceHeight,
                               uint32_t         SourceChannels,
                               LunarAtlasStore& Store,
                               uint32_t         PresetOrdinal) noexcept
{
    for (uint32_t Y = 0u; Y < LunarTileHeight; ++Y)
    {
        const float FY = ((static_cast<float>(Y) + 0.5f) / static_cast<float>(LunarTileHeight)) * static_cast<float>(SourceHeight) - 0.5f;
        const int32_t Y0 = std::clamp(static_cast<int32_t>(std::floor(FY)), 0, static_cast<int32_t>(SourceHeight) - 1);
        const int32_t Y1 = std::min(Y0 + 1, static_cast<int32_t>(SourceHeight) - 1);
        const float TY   = std::clamp(FY - std::floor(FY), 0.0f, 1.0f);

        const float Theta = ((static_cast<float>(Y) + 0.5f) / static_cast<float>(LunarTileHeight)) * PiConstant;
        const float SinT  = std::sin(Theta);
        const float CosT  = std::cos(Theta);

        for (uint32_t X = 0u; X < LunarTileWidth; ++X)
        {
            const float FX = ((static_cast<float>(X) + 0.5f) / static_cast<float>(LunarTileWidth)) * static_cast<float>(SourceWidth) - 0.5f;
            const int32_t X0 = ((static_cast<int32_t>(std::floor(FX)) % static_cast<int32_t>(SourceWidth)) + static_cast<int32_t>(SourceWidth))
                             % static_cast<int32_t>(SourceWidth);
            const int32_t X1 = (X0 + 1) % static_cast<int32_t>(SourceWidth);
            const float TX   = std::clamp(FX - std::floor(FX), 0.0f, 1.0f);

            auto FetchLinear = [&](int32_t Col, int32_t Row) noexcept -> SpectralColor
            {
                const std::size_t Off = (static_cast<std::size_t>(Row) * SourceWidth + static_cast<std::size_t>(Col)) * SourceChannels;
                return SpectralColor{
                    DecodeSrgbByteToLinear(SourceRgb[Off + 0u]),
                    DecodeSrgbByteToLinear(SourceRgb[Off + 1u]),
                    DecodeSrgbByteToLinear(SourceRgb[Off + 2u]) };
            };

            const SpectralColor C00 = FetchLinear(X0, Y0);
            const SpectralColor C10 = FetchLinear(X1, Y0);
            const SpectralColor C01 = FetchLinear(X0, Y1);
            const SpectralColor C11 = FetchLinear(X1, Y1);

            const SpectralColor Sampled{
                (C00.Red   + (C10.Red   - C00.Red)   * TX) * (1.0f - TY) + (C01.Red   + (C11.Red   - C01.Red)   * TX) * TY,
                (C00.Green + (C10.Green - C00.Green) * TX) * (1.0f - TY) + (C01.Green + (C11.Green - C01.Green) * TX) * TY,
                (C00.Blue  + (C10.Blue  - C00.Blue)  * TX) * (1.0f - TY) + (C01.Blue  + (C11.Blue  - C01.Blue)  * TX) * TY };

            // Combine texture selenographic luminance relief with fine 3D micro-crater detail for crisp terminator normals.
            const float Phi = ((static_cast<float>(X) + 0.5f) / static_cast<float>(LunarTileWidth)) * TauConstant;
            const Vector3f UnitSphere{ SinT * std::cos(Phi), SinT * std::sin(Phi), CosT };
            const float MicroDetail = EvaluateRotatedCelestialFbm(
                Vector3f{ UnitSphere.XCoord * 48.0f, UnitSphere.YCoord * 48.0f, UnitSphere.ZCoord * 48.0f }, 3u) - 0.5f;
            const float LumaHeight  = std::sqrt(std::max(0.0f, EvaluateLuminance(Sampled))) * 1.15f - 0.45f;

            const std::size_t DstIdx     = (static_cast<std::size_t>(PresetOrdinal * LunarTileHeight + Y) * LunarTileWidth) + X;
            Store.AlbedoTexels[DstIdx]    = Sampled;
            Store.ElevationTexels[DstIdx] = std::clamp(LumaHeight + 0.22f * MicroDetail, -1.0f, 1.0f);
        }
    }
}

[[nodiscard]] bool TryDecodeBinaryPpmIntoTile(const std::filesystem::path& FilePath,
                                              LunarAtlasStore&             Store,
                                              uint32_t                     PresetOrdinal)
{
    std::ifstream Stream(FilePath, std::ios::binary);
    if (!Stream.is_open())
        return false;

    std::string MagicHeader;
    Stream >> MagicHeader;
    if (MagicHeader != "P6")
        return false;

    auto ConsumeComments = [&]()
    {
        while (Stream >> std::ws && Stream.peek() == '#')
        {
            std::string CommentLine;
            std::getline(Stream, CommentLine);
        }
    };

    uint32_t SrcW = 0u, SrcH = 0u, MaxByte = 0u;
    ConsumeComments(); Stream >> SrcW;
    ConsumeComments(); Stream >> SrcH;
    ConsumeComments(); Stream >> MaxByte;
    Stream.get();
    if (!Stream || SrcW == 0u || SrcH == 0u || MaxByte != 255u)
        return false;

    std::vector<uint8_t> RawBytes(static_cast<std::size_t>(SrcW) * SrcH * 3u);
    Stream.read(reinterpret_cast<char*>(RawBytes.data()), static_cast<std::streamsize>(RawBytes.size()));
    if (!Stream)
        return false;

    ResampleRgbBufferIntoTile(RawBytes.data(), SrcW, SrcH, 3u, Store, PresetOrdinal);
    return true;
}

[[nodiscard]] LunarAtlasStore BuildLunarAtlasStore()
{
    LunarAtlasStore Store{};
    const std::size_t TotalTexels = static_cast<std::size_t>(LunarTileWidth) * LunarTileHeight * LunarPresetCount;
    Store.AlbedoTexels.assign(TotalTexels, SpectralColor{ 0.5f, 0.5f, 0.5f });
    Store.ElevationTexels.assign(TotalTexels, 0.0f);

    static constexpr std::array<const char*, LunarPresetCount> StemNames {{
        "luna_2k", "ember_2k", "glacier_2k", "sulfur_2k", "shroud_2k", "shard_2k"
    }};

    const std::array<std::filesystem::path, 2u> SearchRoots {{
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "EngineContent/Celestial/Moons",
        std::filesystem::path("EngineContent/Celestial/Moons")
    }};

    for (uint32_t PresetIdx = 0u; PresetIdx < LunarPresetCount; ++PresetIdx)
    {
        bool Loaded = false;
        for (const std::filesystem::path& Dir : SearchRoots)
        {
            if (Loaded)
                break;

            Loaded = TryDecodeBinaryPpmIntoTile(Dir / (std::string(StemNames[PresetIdx]) + ".ppm"), Store, PresetIdx);
#if FRONTIER_CELESTIAL_STB_AVAILABLE
            if (!Loaded)
            {
                const std::filesystem::path JpgPath = Dir / (std::string(StemNames[PresetIdx]) + ".jpg");
                int SrcW = 0, SrcH = 0, SrcComp = 0;
                uint8_t* Decoded = stbi_load(JpgPath.string().c_str(), &SrcW, &SrcH, &SrcComp, 3);
                if (Decoded != nullptr && SrcW > 0 && SrcH > 0)
                {
                    ResampleRgbBufferIntoTile(
                        Decoded, static_cast<uint32_t>(SrcW), static_cast<uint32_t>(SrcH), 3u, Store, PresetIdx);
                    stbi_image_free(Decoded);
                    Loaded = true;
                }
            }
#endif
        }

        if (!Loaded)
            SynthesizeProceduralLunarTile(Store, PresetIdx);
    }

    return Store;
}

[[nodiscard]] const LunarAtlasStore& QueryLunarAtlasStore()
{
    static const LunarAtlasStore CachedStore = BuildLunarAtlasStore();
    return CachedStore;
}

struct LunarSurfaceSample
{
    SpectralColor Albedo         {};
    float         HeightValue    = 0.0f;
    float         SlopeEast      = 0.0f;
    float         SlopeNorth     = 0.0f;
};

[[nodiscard]] LunarSurfaceSample SampleLunarSurfaceBilinear(const LunarAtlasStore& Store,
                                                            uint32_t               PresetOrdinal,
                                                            float                  LongitudeU,
                                                            float                  LatitudeV) noexcept
{
    const uint32_t ClampedPreset = std::min(PresetOrdinal, LunarPresetCount - 1u);
    float WrappedU = LongitudeU - std::floor(LongitudeU);
    if (WrappedU < 0.0f)
        WrappedU += 1.0f;
    const float ClampedV = std::clamp(LatitudeV, 0.0f, 1.0f);

    const float FX = WrappedU * static_cast<float>(LunarTileWidth) - 0.5f;
    const float FY = ClampedV * static_cast<float>(LunarTileHeight - 1u);

    const int32_t WInt = static_cast<int32_t>(LunarTileWidth);
    const int32_t HInt = static_cast<int32_t>(LunarTileHeight);
    const int32_t X0   = ((static_cast<int32_t>(std::floor(FX)) % WInt) + WInt) % WInt;
    const int32_t X1   = (X0 + 1) % WInt;
    const int32_t XM1  = (X0 - 1 + WInt) % WInt;
    const int32_t Y0   = std::clamp(static_cast<int32_t>(std::floor(FY)), 0, HInt - 1);
    const int32_t Y1   = std::min(Y0 + 1, HInt - 1);
    const int32_t YM1  = std::max(Y0 - 1, 0);

    const float TX = std::clamp(FX - std::floor(FX), 0.0f, 1.0f);
    const float TY = std::clamp(FY - std::floor(FY), 0.0f, 1.0f);
    const std::size_t RowOffset = static_cast<std::size_t>(ClampedPreset) * LunarTileHeight;

    auto FetchAlbedo = [&](int32_t Col, int32_t Row) noexcept -> const SpectralColor&
    {
        return Store.AlbedoTexels[(RowOffset + static_cast<std::size_t>(Row)) * LunarTileWidth + static_cast<std::size_t>(Col)];
    };
    auto FetchHeight = [&](int32_t Col, int32_t Row) noexcept -> float
    {
        return Store.ElevationTexels[(RowOffset + static_cast<std::size_t>(Row)) * LunarTileWidth + static_cast<std::size_t>(Col)];
    };

    const SpectralColor& C00 = FetchAlbedo(X0, Y0);
    const SpectralColor& C10 = FetchAlbedo(X1, Y0);
    const SpectralColor& C01 = FetchAlbedo(X0, Y1);
    const SpectralColor& C11 = FetchAlbedo(X1, Y1);

    LunarSurfaceSample Out{};
    Out.Albedo = SpectralColor{
        (C00.Red   + (C10.Red   - C00.Red)   * TX) * (1.0f - TY) + (C01.Red   + (C11.Red   - C01.Red)   * TX) * TY,
        (C00.Green + (C10.Green - C00.Green) * TX) * (1.0f - TY) + (C01.Green + (C11.Green - C01.Green) * TX) * TY,
        (C00.Blue  + (C10.Blue  - C00.Blue)  * TX) * (1.0f - TY) + (C01.Blue  + (C11.Blue  - C01.Blue)  * TX) * TY };

    const float H00 = FetchHeight(X0, Y0);
    const float H10 = FetchHeight(X1, Y0);
    const float H01 = FetchHeight(X0, Y1);
    const float H11 = FetchHeight(X1, Y1);
    Out.HeightValue = (H00 + (H10 - H00) * TX) * (1.0f - TY) + (H01 + (H11 - H01) * TX) * TY;
    Out.SlopeEast   = (FetchHeight(X1, Y0) - FetchHeight(XM1, Y0)) * 0.5f;
    Out.SlopeNorth  = (FetchHeight(X0, YM1) - FetchHeight(X0, Y1)) * 0.5f;
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  3D SPHERICAL LUNAR ATMOSPHERE RADIATIVE TRANSFER
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::array<float, 2> IntersectSphereRelative(const Vector3f& RayOriginRelCenter,
                                                           const Vector3f& RayDirection,
                                                           float           SphereRadius) noexcept
{
    const float ProjB        = DotVector(RayOriginRelCenter, RayDirection);
    const float TermC        = DotVector(RayOriginRelCenter, RayOriginRelCenter) - SphereRadius * SphereRadius;
    const float Discriminant = ProjB * ProjB - TermC;
    if (Discriminant < 0.0f)
        return { -1.0f, -1.0f };
    const float Root = std::sqrt(Discriminant);
    return { -ProjB - Root, -ProjB + Root };
}

[[nodiscard]] SpectralColor IntegrateLunarSolarTransmittance(const LunarAtmosphereProfile& Env,
                                                             const Vector3f&               SampleRelCenterNorm,
                                                             const Vector3f&               SunDir,
                                                             float                         TopRadiusNorm) noexcept
{
    // Solid lunar shadow cylinder test against unit solid moon sphere R == 1.0!
    const auto [SolidNear, SolidFar] = IntersectSphereRelative(SampleRelCenterNorm, SunDir, 1.0f);
    (void)SolidFar;
    if (SolidNear > 0.0f)
        return SpectralColor{ 0.0f, 0.0f, 0.0f };

    const auto [EnvNear, EnvFar] = IntersectSphereRelative(SampleRelCenterNorm, SunDir, TopRadiusNorm);
    (void)EnvNear;
    if (EnvFar <= 0.0f)
        return SpectralColor{ 1.0f, 1.0f, 1.0f };

    constexpr uint32_t SunSteps = 8u;
    const float StepNorm = EnvFar / static_cast<float>(SunSteps);
    const float RayH     = std::max(0.002f, Env.RayleighScaleHeightFraction);
    const float MieH     = std::max(0.001f, Env.MieScaleHeightFraction);

    SpectralColor OpticalDepth{ 0.0f, 0.0f, 0.0f };
    for (uint32_t S = 0u; S < SunSteps; ++S)
    {
        const float    Travel = (static_cast<float>(S) + 0.5f) * StepNorm;
        const Vector3f Pos    = AddVector(SampleRelCenterNorm, ScaleVector(SunDir, Travel));
        const float    Alt    = std::max(0.0f, VectorLength(Pos) - 1.0f);
        const float    RhoR   = std::exp(-Alt / RayH);
        const float    RhoM   = std::exp(-Alt / MieH);
        const float    MieExt = Env.MieExtinctionDepthScale * RhoM;

        OpticalDepth.Red   += (Env.RayleighOpticalDepthScale.Red   * RhoR + MieExt) * StepNorm;
        OpticalDepth.Green += (Env.RayleighOpticalDepthScale.Green * RhoR + MieExt) * StepNorm;
        OpticalDepth.Blue  += (Env.RayleighOpticalDepthScale.Blue  * RhoR + MieExt) * StepNorm;
    }

    return SpectralColor{
        std::exp(-OpticalDepth.Red),
        std::exp(-OpticalDepth.Green),
        std::exp(-OpticalDepth.Blue) };
}

struct SingleMoonEvaluation
{
    SpectralColor OutgoingRadiance       { 0.0f, 0.0f, 0.0f };
    SpectralColor BackgroundTransmission { 1.0f, 1.0f, 1.0f };
    bool          SolidSurfaceHit        = false;
};

[[nodiscard]] SingleMoonEvaluation EvaluateSingle3DMoon(const LunarBodyDescriptor&         Moon,
                                                        const Vector3f&                    ViewDir,
                                                        const RadiometricSkyConfiguration& SkyCfg,
                                                        float                              PixelAngularPitchRadians) noexcept
{
    SingleMoonEvaluation Eval{};
    const Vector3f MoonDir = ResolveLunarBearingScene(Moon);
    const float    SinAng  = std::sin(std::clamp(Moon.AngularRadiusRadians, 0.0008f, 1.2f));
    // Work in solid-moon-radius units (R_solid == 1.0), placing moon center at distance D = 1 / sin(AngularRadius).
    const float    CenterDistNorm = 1.0f / std::max(1.0e-4f, SinAng);
    const float    TopRadiusNorm  = Moon.AtmosphereEnvelope.EnvelopeEnabled
                                  ? (1.0f + std::max(0.008f, Moon.AtmosphereEnvelope.TopAltitudeFraction))
                                  : 1.0f;

    const float MaxOuterAngle = std::asin(std::min(0.999f, TopRadiusNorm / CenterDistNorm)) + 2.5f * PixelAngularPitchRadians;
    if (DotVector(ViewDir, MoonDir) < std::cos(std::min(PiConstant, MaxOuterAngle)))
        return Eval;

    // Ray origin in moon-centered normalized coordinates: OriginRel = -MoonCenter = -MoonDir * CenterDistNorm.
    const Vector3f OriginRel = ScaleVector(MoonDir, -CenterDistNorm);
    const Vector3f SunDir    = Moon.CustomIlluminationEnabled
                             ? NormalizeVector(Moon.CustomSolarDirectionScene)
                             : NormalizeVector(SkyCfg.SolarDirectionScene);

    // Sub-pixel analytic solid-limb coverage for crisp anti-aliased regolith silhouette.
    const Vector3f ClosestVec     = AddVector(OriginRel, ScaleVector(ViewDir, -DotVector(OriginRel, ViewDir)));
    const float    ImpactParam    = VectorLength(ClosestVec);
    const float    PixelWorldNorm = std::max(0.002f, PixelAngularPitchRadians * CenterDistNorm);
    const float    SolidCoverage  = std::clamp((1.0f + 0.5f * PixelWorldNorm - ImpactParam) / PixelWorldNorm, 0.0f, 1.0f);

    const auto [SolidNearRaw, SolidFarRaw] = IntersectSphereRelative(OriginRel, ViewDir, 1.0f);
    (void)SolidFarRaw;
    const bool HitsSolidSphere = (SolidNearRaw > 0.0f);

    // ① Integrate 3D spherical lunar atmosphere along [EnvStart, EnvEnd] via Beer-Lambert + Rayleigh/Mie phase functions!
    SpectralColor LunarAtmoInscatter{ 0.0f, 0.0f, 0.0f };
    SpectralColor LunarAtmoTrans{ 1.0f, 1.0f, 1.0f };

    if (Moon.AtmosphereEnvelope.EnvelopeEnabled && TopRadiusNorm > 1.001f)
    {
        const auto [EnvNear, EnvFar] = IntersectSphereRelative(OriginRel, ViewDir, TopRadiusNorm);
        if (EnvFar > 0.0f)
        {
            const float SegStart = std::max(0.0f, EnvNear);
            const float SegEnd   = HitsSolidSphere ? SolidNearRaw : EnvFar;
            if (SegEnd > SegStart)
            {
                const LunarAtmosphereProfile& Env = Moon.AtmosphereEnvelope;
                const float CosTheta  = DotVector(ViewDir, SunDir);
                const float PhaseRay  = (3.0f / (16.0f * PiConstant)) * (1.0f + CosTheta * CosTheta);
                const float G         = Env.MieAsymmetry;
                const float G2        = G * G;
                const float PhaseMie  = (3.0f / (8.0f * PiConstant)) * ((1.0f - G2) * (1.0f + CosTheta * CosTheta))
                                      / ((2.0f + G2) * std::pow(std::max(1.0e-4f, 1.0f + G2 - 2.0f * G * CosTheta), 1.5f));

                constexpr uint32_t EnvelopeSteps = 20u;
                const float StepNorm = (SegEnd - SegStart) / static_cast<float>(EnvelopeSteps);
                const float RayH     = std::max(0.002f, Env.RayleighScaleHeightFraction);
                const float MieH     = std::max(0.001f, Env.MieScaleHeightFraction);

                for (uint32_t S = 0u; S < EnvelopeSteps; ++S)
                {
                    const float    Travel = SegStart + (static_cast<float>(S) + 0.5f) * StepNorm;
                    const Vector3f PosRel = AddVector(OriginRel, ScaleVector(ViewDir, Travel));
                    const float    RadLen = std::max(1.0f, VectorLength(PosRel));
                    const float    Alt    = RadLen - 1.0f;

                    const float RhoR     = std::exp(-Alt / RayH);
                    const float RhoM     = std::exp(-Alt / MieH);
                    const SpectralColor ScatR = ScaleSpectral(Env.RayleighOpticalDepthScale, RhoR);
                    const float         ScatM = Env.MieScatteringDepthScale * RhoM;
                    const float         ExtM  = Env.MieExtinctionDepthScale * RhoM;

                    const SpectralColor ExtTotal{
                        std::max(1.0e-6f, ScatR.Red   + ExtM),
                        std::max(1.0e-6f, ScatR.Green + ExtM),
                        std::max(1.0e-6f, ScatR.Blue  + ExtM) };

                    const SpectralColor StepT{
                        std::exp(-ExtTotal.Red   * StepNorm),
                        std::exp(-ExtTotal.Green * StepNorm),
                        std::exp(-ExtTotal.Blue  * StepNorm) };

                    const SpectralColor SunT = IntegrateLunarSolarTransmittance(Env, PosRel, SunDir, TopRadiusNorm);

                    // Secondary multiple-scattering twilight wrap near the lunar terminator.
                    const float LocalSunMu = DotVector(ScaleVector(PosRel, 1.0f / RadLen), SunDir);
                    const float MultiWrap  = Env.MultipleScatterApproximation * (1.0f / (4.0f * PiConstant))
                                           * std::clamp((LocalSunMu + 0.24f) / 0.65f, 0.0f, 1.0f);

                    const SpectralColor InScatter{
                        (ScatR.Red   * PhaseRay + ScatM * PhaseMie) * SunT.Red   + (ScatR.Red   + ScatM) * MultiWrap,
                        (ScatR.Green * PhaseRay + ScatM * PhaseMie) * SunT.Green + (ScatR.Green + ScatM) * MultiWrap,
                        (ScatR.Blue  * PhaseRay + ScatM * PhaseMie) * SunT.Blue  + (ScatR.Blue  + ScatM) * MultiWrap };

                    const SpectralColor StepIntegral{
                        (1.0f - StepT.Red)   / ExtTotal.Red,
                        (1.0f - StepT.Green) / ExtTotal.Green,
                        (1.0f - StepT.Blue)  / ExtTotal.Blue };

                    LunarAtmoInscatter.Red   += LunarAtmoTrans.Red   * InScatter.Red   * StepIntegral.Red;
                    LunarAtmoInscatter.Green += LunarAtmoTrans.Green * InScatter.Green * StepIntegral.Green;
                    LunarAtmoInscatter.Blue  += LunarAtmoTrans.Blue  * InScatter.Blue  * StepIntegral.Blue;

                    LunarAtmoTrans = MultiplySpectral(LunarAtmoTrans, StepT);
                }
            }
        }
    }

    const SpectralColor ScaledInscatter = MultiplySpectral(LunarAtmoInscatter, SkyCfg.ExtraterrestrialSolarFlux);

    // ② Shade solid 3D cratered lunar regolith sphere when SolidCoverage > 0.
    SpectralColor SolidRegolithRadiance{ 0.0f, 0.0f, 0.0f };
    if (SolidCoverage > 0.0f)
    {
        const float HitT = HitsSolidSphere ? SolidNearRaw : (-DotVector(OriginRel, ViewDir));
        const Vector3f SurfNormal = NormalizeVector(AddVector(OriginRel, ScaleVector(ViewDir, HitT)));

        // Construct selenographic local basis aligned with MoonDir, SurfaceRollRadians, SurfacePitchRadians, and LongitudeOffsetRadians.
        const Vector3f RefUp      = (std::fabs(MoonDir.ZCoord) < 0.98f) ? Vector3f{ 0.0f, 0.0f, 1.0f } : Vector3f{ 1.0f, 0.0f, 0.0f };
        const Vector3f LocalEast  = NormalizeVector(CrossVector(RefUp, MoonDir));
        const Vector3f LocalNorth = CrossVector(MoonDir, LocalEast);

        const float CosRoll = std::cos(Moon.SurfaceRollRadians);
        const float SinRoll = std::sin(Moon.SurfaceRollRadians);
        const Vector3f RollEast  = AddVector(ScaleVector(LocalEast, CosRoll), ScaleVector(LocalNorth, SinRoll));
        const Vector3f RollNorth = SubtractVector(ScaleVector(LocalNorth, CosRoll), ScaleVector(LocalEast, SinRoll));
        const Vector3f TowardObs = ScaleVector(MoonDir, -1.0f);

        const float LocalX = DotVector(SurfNormal, RollEast);
        const float LocalY = DotVector(SurfNormal, RollNorth);
        const float LocalZ = DotVector(SurfNormal, TowardObs);

        const float CosPitch = std::cos(Moon.SurfacePitchRadians);
        const float SinPitch = std::sin(Moon.SurfacePitchRadians);
        const float SelenY   = LocalY * CosPitch - LocalZ * SinPitch;
        const float SelenZ   = LocalY * SinPitch + LocalZ * CosPitch;

        const float LonU = (std::atan2(LocalX, SelenZ) + Moon.LongitudeOffsetRadians) / TauConstant + 0.5f;
        const float LatV = std::acos(std::clamp(SelenY, -1.0f, 1.0f)) / PiConstant;

        const LunarSurfaceSample SurfSample = SampleLunarSurfaceBilinear(
            QueryLunarAtlasStore(), static_cast<uint32_t>(Moon.SurfaceCategory), LonU, LatV);

        // Perturb 3D surface normal along selenographic east/north tangents for crisp terminator crater rim shadows.
        const Vector3f TangentEast  = NormalizeVector(CrossVector(RollNorth, SurfNormal));
        const Vector3f TangentNorth = CrossVector(SurfNormal, TangentEast);
        const float    ReliefScale  = 2.65f * Moon.CraterReliefStrength;
        const Vector3f CraterNormal = NormalizeVector(AddVector(
            SurfNormal,
            AddVector(ScaleVector(TangentEast, -SurfSample.SlopeEast * ReliefScale),
                      ScaleVector(TangentNorth, -SurfSample.SlopeNorth * ReliefScale))));

        const float MuIncident  = std::max(0.0f, DotVector(CraterNormal, SunDir));
        const float MuMacro     = DotVector(SurfNormal, SunDir);
        const float MuOutgoing  = std::max(0.06f, -DotVector(CraterNormal, ViewDir));
        const float MacroShadow = std::clamp((MuMacro + 0.055f) / 0.095f, 0.0f, 1.0f);

        // Hapke / Lommel-Seeliger porous regolith backscatter + coherent opposition surge (no flat Lambertian clipping!).
        const float PhaseAngleRad   = std::acos(std::clamp(-DotVector(ViewDir, SunDir), -1.0f, 1.0f));
        const float OppositionSurge = 1.0f + 0.34f * std::exp(-PhaseAngleRad / 0.14f);
        const float BackscatterP    = 0.82f + 0.28f * std::cos(PhaseAngleRad);
        const float LommelSeeliger  = (MuIncident > 0.0f)
            ? ((MuIncident / (MuIncident + 0.42f * MuOutgoing)) * BackscatterP * OppositionSurge * MacroShadow)
            : 0.0f;

        // Direct sunlight reaching the lunar surface IS attenuated through the moon's own spherical atmosphere!
        const SpectralColor SurfSunTrans = (Moon.AtmosphereEnvelope.EnvelopeEnabled && MuMacro > -0.06f)
            ? IntegrateLunarSolarTransmittance(
                  Moon.AtmosphereEnvelope, ScaleVector(SurfNormal, 1.0005f), SunDir, TopRadiusNorm)
            : SpectralColor{ 1.0f, 1.0f, 1.0f };

        // Subtle diffuse illumination from the moon's own sunlit sky dome + planetary Earthshine on the unlit hemisphere.
        const float AtmoSkyFill = Moon.AtmosphereEnvelope.EnvelopeEnabled
            ? (0.045f * std::clamp((MuMacro + 0.20f) / 0.70f, 0.0f, 1.0f) * Moon.AtmosphereEnvelope.MultipleScatterApproximation * 3.0f)
            : 0.0f;
        const float Earthshine  = 0.0022f * Moon.EarthshineStrength;

        // Physical lunar geometric albedo calibration (~0.14 for Luna regolith -> crisp unclipped surface relief under SolarFlux ~ 12.8).
        const float ReflectanceGain = 0.24f * Moon.GeometricAlbedoScale;
        const SpectralColor DirectTerm{
            SurfSample.Albedo.Red   * ReflectanceGain * LommelSeeliger * SurfSunTrans.Red   * SkyCfg.ExtraterrestrialSolarFlux.Red,
            SurfSample.Albedo.Green * ReflectanceGain * LommelSeeliger * SurfSunTrans.Green * SkyCfg.ExtraterrestrialSolarFlux.Green,
            SurfSample.Albedo.Blue  * ReflectanceGain * LommelSeeliger * SurfSunTrans.Blue  * SkyCfg.ExtraterrestrialSolarFlux.Blue };

        const SpectralColor AmbientFill{
            SurfSample.Albedo.Red   * (Earthshine * 0.72f + AtmoSkyFill * Moon.AtmosphereEnvelope.RayleighOpticalDepthScale.Red)   * SkyCfg.ExtraterrestrialSolarFlux.Red,
            SurfSample.Albedo.Green * (Earthshine * 0.88f + AtmoSkyFill * Moon.AtmosphereEnvelope.RayleighOpticalDepthScale.Green) * SkyCfg.ExtraterrestrialSolarFlux.Green,
            SurfSample.Albedo.Blue  * (Earthshine * 1.15f + AtmoSkyFill * Moon.AtmosphereEnvelope.RayleighOpticalDepthScale.Blue)  * SkyCfg.ExtraterrestrialSolarFlux.Blue };

        SolidRegolithRadiance = AddSpectral(DirectTerm, AmbientFill);
        Eval.SolidSurfaceHit  = (SolidCoverage > 0.5f);
    }

    // Composite solid sphere + spherical lunar atmosphere with exact sub-pixel solid limb anti-aliasing:
    //   • Over solid core: L_inscatter + T_lunar * L_regolith, background occluded by SolidCoverage.
    //   • Off-limb in space: L_inscatter, background attenuated by T_lunar!
    const SpectralColor OnDiscTotal = AddSpectral(ScaledInscatter, MultiplySpectral(SolidRegolithRadiance, LunarAtmoTrans));
    Eval.OutgoingRadiance = SpectralColor{
        ScaledInscatter.Red   * (1.0f - SolidCoverage) + OnDiscTotal.Red   * SolidCoverage,
        ScaledInscatter.Green * (1.0f - SolidCoverage) + OnDiscTotal.Green * SolidCoverage,
        ScaledInscatter.Blue  * (1.0f - SolidCoverage) + OnDiscTotal.Blue  * SolidCoverage };

    Eval.BackgroundTransmission = SpectralColor{
        LunarAtmoTrans.Red   * (1.0f - SolidCoverage),
        LunarAtmoTrans.Green * (1.0f - SolidCoverage),
        LunarAtmoTrans.Blue  * (1.0f - SolidCoverage) };

    return Eval;
}

//------------------------------------------------------------------------------------------------------------------------
//                      FRAUNHOFER AIRY DIFFRACTION + MOFFAT SEEING PSF + SPATIAL STAR CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Minimax polynomial approximation to 2 * J1(x) / x for Fraunhofer circular-pupil diffraction [2*J1(x)/x]^2.
[[nodiscard]] float EvaluateFraunhoferAiryIntensity(float NormalizedArgX) noexcept
{
    const float AbsX = std::fabs(NormalizedArgX);
    if (AbsX < 1.0e-4f)
        return 1.0f;
    if (AbsX < 3.0f)
    {
        const float T = (AbsX / 3.0f) * (AbsX / 3.0f);
        const float J1OverX = 0.5f + T * (-0.56249985f + T * (0.21093573f + T * (-0.03954289f
                            + T * (0.00443319f + T * (-0.00031761f + T * 0.00001109f)))));
        const float Amp = 2.0f * J1OverX;
        return Amp * Amp;
    }
    if (AbsX > 18.0f)
        return 0.0f;
    // Asymptotic Hankel envelope for outer Airy diffraction rings (first dark ring at x = 3.8317, first bright ring peak = 0.0175).
    const float CosWave = std::cos(AbsX - 0.75f * PiConstant);
    const float Amp     = (2.0f / AbsX) * std::sqrt(2.0f / (PiConstant * AbsX)) * CosWave;
    return Amp * Amp;
}

// 📝 Sub-pixel solid-angle-integrated chromatic Fraunhofer Airy + atmospheric Moffat point-spread function (zero cross sprites!).
[[nodiscard]] SpectralColor EvaluateOpticalStarPsf(float                 AngularSeparationRad,
                                                   float                 PixelAngularPitchRad,
                                                   float                 AiryFirstZeroGreenRad,
                                                   float                 SeeingHalfWidthRad,
                                                   bool                  SeeingEnabled,
                                                   const SpectralColor&  StarChromaticity,
                                                   float                 BolometricFlux) noexcept
{
    const float SafePitch = std::max(0.00020f, PixelAngularPitchRad);
    // Effective sub-pixel sampling kernel radius (box-convolved sensor pixel solid angle + optical Airy core).
    const float SensorSigma = SafePitch * 0.42f;
    const float CutoffAngle = std::max(SafePitch * 3.2f, AiryFirstZeroGreenRad * 3.8f + SeeingHalfWidthRad * 3.5f);
    if (AngularSeparationRad >= CutoffAngle)
        return SpectralColor{ 0.0f, 0.0f, 0.0f };

    auto EvaluateWavelengthBand = [&](float WavelengthScale) noexcept -> float
    {
        const float BandAiryZero       = std::max(1.0e-5f, AiryFirstZeroGreenRad * WavelengthScale);
        const float EffectiveAirySigma = std::sqrt(SensorSigma * SensorSigma + (0.42f * BandAiryZero) * (0.42f * BandAiryZero));
        const float AirySeparation     = AngularSeparationRad / EffectiveAirySigma;

        // Solid-angle conservation: Peak scales as (SafePitch^2) / (2 * pi * EffectiveAirySigma^2) so resolved vs sub-pixel stars conserve total flux!
        const float SolidAngleNorm = (SafePitch * SafePitch) / (TauConstant * EffectiveAirySigma * EffectiveAirySigma);
        float PsfWeight = SolidAngleNorm * std::exp(-0.5f * AirySeparation * AirySeparation);

        // Resolved Fraunhofer outer Airy diffraction rings (chromatic wavelength dispersion lambda_R > lambda_G > lambda_B!).
        if (AngularSeparationRad > EffectiveAirySigma * 0.85f)
        {
            const float AiryX    = PiConstant * AngularSeparationRad / BandAiryZero;
            const float AiryRing = EvaluateFraunhoferAiryIntensity(AiryX);
            const float RingNorm = std::min(1.0f, (SafePitch * SafePitch) / std::max(1.0e-9f, BandAiryZero * BandAiryZero));
            PsfWeight += AiryRing * RingNorm * 0.85f;
        }

        // Atmospheric Kolmogorov/Moffat turbulence seeing aureole (only present inside an atmosphere, absent in vacuum space!).
        if (SeeingEnabled && SeeingHalfWidthRad > 1.0e-6f)
        {
            const float MoffatAlpha = std::max(SafePitch * 0.65f, SeeingHalfWidthRad);
            const float RatioSq     = (AngularSeparationRad * AngularSeparationRad) / (MoffatAlpha * MoffatAlpha);
            constexpr float Beta    = 2.8f;
            const float MoffatNorm  = ((Beta - 1.0f) / (PiConstant * MoffatAlpha * MoffatAlpha)) * (SafePitch * SafePitch);
            PsfWeight = 0.76f * PsfWeight + 0.24f * MoffatNorm * std::pow(1.0f + RatioSq, -Beta);
        }
        return PsfWeight;
    };

    const float WeightRed   = EvaluateWavelengthBand(650.0f / 550.0f);
    const float WeightGreen = EvaluateWavelengthBand(1.0f);
    const float WeightBlue  = EvaluateWavelengthBand(450.0f / 550.0f);

    return SpectralColor{
        BolometricFlux * StarChromaticity.Red   * WeightRed,
        BolometricFlux * StarChromaticity.Green * WeightGreen,
        BolometricFlux * StarChromaticity.Blue  * WeightBlue };
}

// 📝 J2000 Equatorial unit vector -> Galactic unit vector (IAU 1958 / Hipparcos J2000 matrix).
[[nodiscard]] Vector3f TransformEquatorialToGalactic(const Vector3f& EqDir) noexcept
{
    return Vector3f{
        -0.05487556f * EqDir.XCoord - 0.87343709f * EqDir.YCoord - 0.48383502f * EqDir.ZCoord,
         0.49410943f * EqDir.XCoord - 0.44482963f * EqDir.YCoord + 0.74698224f * EqDir.ZCoord,
        -0.86766615f * EqDir.XCoord - 0.19807637f * EqDir.YCoord + 0.45598378f * EqDir.ZCoord };
}

// 📝 Scene Z-up view ray -> J2000 Equatorial unit vector for ObserverLatitudeDegrees and LocalSiderealHours.
[[nodiscard]] Vector3f TransformSceneToEquatorial(const Vector3f& SceneDir,
                                                  float           ObserverLatitudeDegrees,
                                                  float           LocalSiderealHours) noexcept
{
    // Scene convention: +X = East, +Y = North, +Z = Up.
    const float LatRad = ObserverLatitudeDegrees * DegToRad;
    const float SinLat = std::sin(LatRad);
    const float CosLat = std::cos(LatRad);

    // Local meridian equatorial coordinates (HourAngle = 0 along +X_meridian):
    //   Up    = (CosLat, 0, SinLat)
    //   North = (-SinLat, 0, CosLat)
    //   East  = (0, 1, 0)
    const float MerX = SceneDir.ZCoord * CosLat - SceneDir.YCoord * SinLat;
    const float MerY = SceneDir.XCoord;
    const float MerZ = SceneDir.ZCoord * SinLat + SceneDir.YCoord * CosLat;

    const float LstRad = LocalSiderealHours * (TauConstant / 24.0f);
    const float CosLst = std::cos(LstRad);
    const float SinLst = std::sin(LstRad);

    return NormalizeVector(Vector3f{
        MerX * CosLst - MerY * SinLst,
        MerX * SinLst + MerY * CosLst,
        MerZ });
}

void ProjectDirectionToCubeFace(const Vector3f& Dir, int32_t& OutFace, float& OutU, float& OutV) noexcept
{
    const float AX = std::fabs(Dir.XCoord);
    const float AY = std::fabs(Dir.YCoord);
    const float AZ = std::fabs(Dir.ZCoord);

    if (AX >= AY && AX >= AZ)
    {
        const float Inv = 1.0f / std::max(1.0e-6f, AX);
        if (Dir.XCoord >= 0.0f) { OutFace = 0; OutU = -Dir.ZCoord * Inv; OutV = Dir.YCoord * Inv; }
        else                    { OutFace = 1; OutU =  Dir.ZCoord * Inv; OutV = Dir.YCoord * Inv; }
    }
    else if (AY >= AX && AY >= AZ)
    {
        const float Inv = 1.0f / std::max(1.0e-6f, AY);
        if (Dir.YCoord >= 0.0f) { OutFace = 2; OutU = Dir.XCoord * Inv; OutV = -Dir.ZCoord * Inv; }
        else                    { OutFace = 3; OutU = Dir.XCoord * Inv; OutV =  Dir.ZCoord * Inv; }
    }
    else
    {
        const float Inv = 1.0f / std::max(1.0e-6f, AZ);
        if (Dir.ZCoord >= 0.0f) { OutFace = 4; OutU = Dir.XCoord * Inv; OutV =  Dir.YCoord * Inv; }
        else                    { OutFace = 5; OutU = Dir.XCoord * Inv; OutV = -Dir.YCoord * Inv; }
    }
}

[[nodiscard]] Vector3f UnprojectCubeFaceToDirection(int32_t Face, float FaceU, float FaceV) noexcept
{
    switch (Face)
    {
        case 0:  return NormalizeVector(Vector3f{  1.0f,  FaceV, -FaceU });
        case 1:  return NormalizeVector(Vector3f{ -1.0f,  FaceV,  FaceU });
        case 2:  return NormalizeVector(Vector3f{  FaceU,  1.0f, -FaceV });
        case 3:  return NormalizeVector(Vector3f{  FaceU, -1.0f,  FaceV });
        case 4:  return NormalizeVector(Vector3f{  FaceU,  FaceV,  1.0f });
        default: return NormalizeVector(Vector3f{  FaceU, -FaceV, -1.0f });
    }
}

struct PreparedCatalogStar
{
    Vector3f      EquatorialDir {};
    float         ApparentMag   = 2.0f;
    SpectralColor Chromaticity  {};
    float         PhaseSalt     = 0.0f;
};

struct SpatialBrightStarIndex
{
    static constexpr int32_t BinRes = 24;
    std::vector<PreparedCatalogStar>              Stars      {};
    std::array<std::vector<uint16_t>, 6 * 24 * 24> CellLists {};
};

[[nodiscard]] const SpatialBrightStarIndex& QuerySpatialBrightStarIndex()
{
    static const SpatialBrightStarIndex Indexed = []()
    {
        SpatialBrightStarIndex Table{};
        const auto& RawList = RetrieveBrightStarCatalogue();
        Table.Stars.reserve(RawList.size());

        for (std::size_t Idx = 0u; Idx < RawList.size(); ++Idx)
        {
            const BrightStarEntry& Entry = RawList[Idx];
            const float RaRad  = Entry.RightAscensionHours * (TauConstant / 24.0f);
            const float DecRad = Entry.DeclinationDegrees * DegToRad;
            const float CosDec = std::cos(DecRad);

            PreparedCatalogStar Prepared{};
            Prepared.EquatorialDir = Vector3f{ CosDec * std::cos(RaRad), CosDec * std::sin(RaRad), std::sin(DecRad) };
            Prepared.ApparentMag   = Entry.ApparentMagnitude;
            Prepared.Chromaticity  = EvaluatePlanckianStarColor(Entry.ColorIndexBv);
            Prepared.PhaseSalt     = static_cast<float>(Idx) * 0.618034f;
            Table.Stars.push_back(Prepared);

            int32_t Face = 0;
            float   FU = 0.0f, FV = 0.0f;
            ProjectDirectionToCubeFace(Prepared.EquatorialDir, Face, FU, FV);
            const int32_t BX = std::clamp(static_cast<int32_t>(std::floor((FU * 0.5f + 0.5f) * SpatialBrightStarIndex::BinRes)),
                                          0, SpatialBrightStarIndex::BinRes - 1);
            const int32_t BY = std::clamp(static_cast<int32_t>(std::floor((FV * 0.5f + 0.5f) * SpatialBrightStarIndex::BinRes)),
                                          0, SpatialBrightStarIndex::BinRes - 1);
            Table.CellLists[static_cast<std::size_t>((Face * SpatialBrightStarIndex::BinRes + BY) * SpatialBrightStarIndex::BinRes + BX)]
                .push_back(static_cast<uint16_t>(Idx));
        }
        return Table;
    }();
    return Indexed;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

LunarAtmosphereProfile ConstructPresetLunarAtmosphere(LunarSurfaceCategory Category) noexcept
{
    LunarAtmosphereProfile Env{};
    Env.EnvelopeEnabled = true;
    switch (Category)
    {
        case LunarSurfaceCategory::LunaRegolith:
            // Tenuous lunar sodium/dust exosphere + subtle forward coronagraphic limb scattering.
            Env.TopAltitudeFraction          = 0.075f;
            Env.RayleighScaleHeightFraction  = 0.018f;
            Env.MieScaleHeightFraction       = 0.008f;
            Env.RayleighOpticalDepthScale    = SpectralColor{ 0.045f, 0.085f, 0.175f };
            Env.MieScatteringDepthScale      = 0.025f;
            Env.MieExtinctionDepthScale      = 0.030f;
            Env.MieAsymmetry                 = 0.74f;
            Env.MultipleScatterApproximation = 0.10f;
            break;

        case LunarSurfaceCategory::EmberBasalt:
            // Volcanic SO2 + basaltic ash atmosphere with rich amber-crimson sunset terminator and glowing limb.
            Env.TopAltitudeFraction          = 0.19f;
            Env.RayleighScaleHeightFraction  = 0.038f;
            Env.MieScaleHeightFraction       = 0.016f;
            Env.RayleighOpticalDepthScale    = SpectralColor{ 0.92f, 0.36f, 0.11f };
            Env.MieScatteringDepthScale      = 0.24f;
            Env.MieExtinctionDepthScale      = 0.29f;
            Env.MieAsymmetry                 = 0.78f;
            Env.MultipleScatterApproximation = 0.24f;
            break;

        case LunarSurfaceCategory::GlacierCryo:
            // Dense nitrogen-methane cryo-atmosphere (Triton/Enceladus plume class) with crisp sapphire Rayleigh shell.
            Env.TopAltitudeFraction          = 0.22f;
            Env.RayleighScaleHeightFraction  = 0.042f;
            Env.MieScaleHeightFraction       = 0.015f;
            Env.RayleighOpticalDepthScale    = SpectralColor{ 0.16f, 0.48f, 1.24f };
            Env.MieScatteringDepthScale      = 0.14f;
            Env.MieExtinctionDepthScale      = 0.16f;
            Env.MieAsymmetry                 = 0.76f;
            Env.MultipleScatterApproximation = 0.26f;
            break;

        case LunarSurfaceCategory::SulfurMantle:
            // Titan/Io photochemical golden-ochre haze envelope with detached high-altitude aerosol scattering.
            Env.TopAltitudeFraction          = 0.20f;
            Env.RayleighScaleHeightFraction  = 0.040f;
            Env.MieScaleHeightFraction       = 0.020f;
            Env.RayleighOpticalDepthScale    = SpectralColor{ 0.84f, 0.66f, 0.18f };
            Env.MieScatteringDepthScale      = 0.28f;
            Env.MieExtinctionDepthScale      = 0.34f;
            Env.MieAsymmetry                 = 0.79f;
            Env.MultipleScatterApproximation = 0.28f;
            break;

        case LunarSurfaceCategory::ShroudSlate:
            // Deep cobalt-violet Rayleigh + high stratospheric haze layer.
            Env.TopAltitudeFraction          = 0.24f;
            Env.RayleighScaleHeightFraction  = 0.046f;
            Env.MieScaleHeightFraction       = 0.018f;
            Env.RayleighOpticalDepthScale    = SpectralColor{ 0.24f, 0.56f, 1.36f };
            Env.MieScatteringDepthScale      = 0.18f;
            Env.MieExtinctionDepthScale      = 0.21f;
            Env.MieAsymmetry                 = 0.75f;
            Env.MultipleScatterApproximation = 0.30f;
            break;

        case LunarSurfaceCategory::ShardSilicate:
            // Cyan-emerald methane/diatomic-carbon scattering atmosphere.
            Env.TopAltitudeFraction          = 0.18f;
            Env.RayleighScaleHeightFraction  = 0.036f;
            Env.MieScaleHeightFraction       = 0.014f;
            Env.RayleighOpticalDepthScale    = SpectralColor{ 0.14f, 0.82f, 0.76f };
            Env.MieScatteringDepthScale      = 0.15f;
            Env.MieExtinctionDepthScale      = 0.18f;
            Env.MieAsymmetry                 = 0.76f;
            Env.MultipleScatterApproximation = 0.22f;
            break;
    }
    return Env;
}

SpectralColor EvaluatePlanckianStarColor(float ColorIndexBv) noexcept
{
    // Ballesteros (2012) Europhysics Letters 97: stellar (B-V) -> effective photospheric temperature T_eff [K].
    const float ClampedBv = std::clamp(ColorIndexBv, -0.38f, 2.00f);
    const float TempKelvin = 4600.0f * (1.0f / (0.92f * ClampedBv + 1.70f) + 1.0f / (0.92f * ClampedBv + 0.62f));

    // Integrate 3 reference wavelengths (650 nm, 550 nm, 450 nm) via Wien/Planck law normalized to D65 white at 6500 K.
    auto PlanckChannel = [](float WavelengthNm, float Kelvin) noexcept -> float
    {
        const float Exponent = std::clamp(1.438777e7f / (WavelengthNm * std::max(1500.0f, Kelvin)), 0.05f, 40.0f);
        return 1.0f / (std::pow(WavelengthNm * 1.0e-3f, 5.0f) * (std::exp(Exponent) - 1.0f));
    };

    const float RawR = PlanckChannel(645.0f, TempKelvin) / PlanckChannel(645.0f, 6500.0f);
    const float RawG = PlanckChannel(545.0f, TempKelvin) / PlanckChannel(545.0f, 6500.0f);
    const float RawB = PlanckChannel(445.0f, TempKelvin) / PlanckChannel(445.0f, 6500.0f);

    const float Luma = std::max(1.0e-5f, 0.2126f * RawR + 0.7152f * RawG + 0.0722f * RawB);
    return SpectralColor{ RawR / Luma, RawG / Luma, RawB / Luma };
}

const std::vector<BrightStarEntry>& RetrieveBrightStarCatalogue()
{
    // 📝 Real J2000 Hipparcos / Yale Bright Star coordinates: Orion, Canis Major, Taurus + Pleiades + Hyades, Gemini, Auriga,
    //    Carina, Crux, Centaurus, Scorpius, Sagittarius, Lyra, Cygnus, Aquila, Ursa Major, Cassiopeia, Bootes, Virgo, Leo, Piscis Austrinus.
    static const std::vector<BrightStarEntry> Catalogue = {
        { "Sirius",          6.7525f, -16.716f, -1.46f,  0.00f },
        { "Canopus",         6.3992f, -52.696f, -0.74f,  0.15f },
        { "RigilKentaurus", 14.6601f, -60.834f, -0.27f,  0.71f },
        { "Arcturus",       14.2610f,  19.182f, -0.05f,  1.23f },
        { "Vega",           18.6156f,  38.784f,  0.03f,  0.00f },
        { "Capella",         5.2782f,  45.998f,  0.08f,  0.80f },
        { "Rigel",           5.2423f,  -8.202f,  0.13f, -0.03f },
        { "Procyon",         7.6550f,   5.225f,  0.34f,  0.42f },
        { "Achernar",        1.6286f, -57.237f,  0.46f, -0.16f },
        { "Betelgeuse",      5.9195f,   7.407f,  0.50f,  1.85f },
        { "Hadar",          14.0637f, -60.373f,  0.61f, -0.23f },
        { "Altair",         19.8464f,   8.868f,  0.76f,  0.22f },
        { "Acrux",          12.4433f, -63.099f,  0.77f, -0.24f },
        { "Aldebaran",       4.5987f,  16.509f,  0.86f,  1.54f },
        { "Antares",        16.4901f, -26.432f,  0.96f,  1.83f },
        { "Spica",          13.4199f, -11.161f,  0.97f, -0.23f },
        { "Pollux",          7.7553f,  28.026f,  1.14f,  1.00f },
        { "Fomalhaut",      22.9608f, -29.622f,  1.16f,  0.09f },
        { "Deneb",          20.6905f,  45.280f,  1.25f,  0.09f },
        { "Mimosa",         12.7953f, -59.689f,  1.25f, -0.23f },
        { "Regulus",        10.1395f,  11.967f,  1.36f, -0.11f },
        { "Adhara",          6.9771f, -28.972f,  1.50f, -0.21f },
        { "Castor",          7.5767f,  31.888f,  1.58f,  0.03f },
        { "Gacrux",         12.5194f, -57.113f,  1.63f,  1.59f },
        { "Shaula",         17.5601f, -37.104f,  1.62f, -0.22f },
        { "Bellatrix",       5.4188f,   6.350f,  1.64f, -0.22f },
        { "Elnath",          5.4382f,  28.608f,  1.65f, -0.13f },
        { "Miaplacidus",     9.2200f, -69.717f,  1.67f,  0.00f },
        { "Alnilam",         5.6036f,  -1.202f,  1.69f, -0.18f },
        { "Alnair",         22.1372f, -46.961f,  1.74f, -0.13f },
        { "Alnitak",         5.6793f,  -1.943f,  1.77f, -0.21f },
        { "Alioth",         12.9005f,  55.960f,  1.77f, -0.02f },
        { "Dubhe",          11.0621f,  61.751f,  1.79f,  1.07f },
        { "Mirfak",          3.4054f,  49.861f,  1.79f,  0.48f },
        { "Wezen",           7.1399f, -26.393f,  1.83f,  0.67f },
        { "Sargas",         17.6220f, -42.998f,  1.86f,  0.40f },
        { "KausAustralis",  18.4029f, -34.385f,  1.85f, -0.03f },
        { "Avior",           8.3752f, -59.510f,  1.86f,  1.28f },
        { "Alkaid",         13.7923f,  49.313f,  1.86f, -0.19f },
        { "Menkalinan",      5.9921f,  44.947f,  1.90f,  0.08f },
        { "Atria",          16.8111f, -69.028f,  1.91f,  1.44f },
        { "Alhena",          6.6285f,  16.399f,  1.93f,  0.00f },
        { "Peacock",        20.4275f, -56.735f,  1.94f, -0.20f },
        { "Alsephina",       8.7451f, -54.709f,  1.96f,  0.04f },
        { "Mirzam",          6.3783f, -17.956f,  1.98f, -0.24f },
        { "Polaris",         2.5303f,  89.264f,  1.98f,  0.60f },
        { "Alphard",         9.4598f,  -8.659f,  1.98f,  1.44f },
        { "Hamal",           2.1196f,  23.462f,  2.00f,  1.15f },
        { "Diphda",          0.7265f, -17.987f,  2.04f,  1.02f },
        { "Nunki",          18.9211f, -26.297f,  2.05f, -0.21f },
        { "Menkent",        14.1114f, -36.370f,  2.06f,  1.01f },
        { "Alpheratz",       0.1398f,  29.091f,  2.06f, -0.11f },
        { "Mirach",          1.1622f,  35.621f,  2.07f,  1.58f },
        { "Saiph",           5.7959f,  -9.670f,  2.09f, -0.17f },
        { "Kochab",         14.8451f,  74.156f,  2.08f,  1.47f },
        { "Rasalhague",     17.5822f,  12.560f,  2.08f,  0.15f },
        { "Algol",           3.1361f,  40.956f,  2.12f, -0.05f },
        { "Almach",          2.0650f,  42.330f,  2.10f,  1.37f },
        { "Denebola",       11.8177f,  14.572f,  2.14f,  0.09f },
        { "Suhail",          9.1333f, -43.433f,  2.21f,  1.65f },
        { "Mintaka",         5.5334f,  -0.299f,  2.23f, -0.22f },
        { "Schedar",         0.6751f,  56.537f,  2.24f,  1.17f },
        { "Naos",            8.0598f, -40.003f,  2.25f, -0.26f },
        { "Sadr",           20.3705f,  40.257f,  2.23f,  0.67f },
        { "Caph",            0.1529f,  59.150f,  2.28f,  0.34f },
        { "Dschubba",       16.0056f, -22.622f,  2.32f, -0.12f },
        { "Larawag",        16.8361f, -34.293f,  2.29f,  1.15f },
        { "Merak",          11.0307f,  56.382f,  2.37f,  0.00f },
        { "Ankaa",           0.4381f, -42.306f,  2.39f,  1.09f },
        { "Girtab",         17.7082f, -39.030f,  2.41f, -0.22f },
        { "Enif",           21.7364f,   9.875f,  2.39f,  1.53f },
        { "Scheat",         23.0629f,  28.083f,  2.42f,  1.67f },
        { "Phecda",         11.8972f,  53.695f,  2.44f,  0.00f },
        { "Aludra",          7.4016f, -29.303f,  2.45f, -0.08f },
        { "Markab",         23.0793f,  15.205f,  2.49f, -0.04f },
        { "Navi",            0.9451f,  60.717f,  2.47f, -0.15f },
        { "Acrab",          16.0906f, -19.805f,  2.62f, -0.07f },
        { "Hatysa",          5.5906f,  -5.910f,  2.77f, -0.24f },
        { "Meissa",          5.5856f,   9.934f,  3.33f, -0.18f },
        { "Tabit",           4.8308f,   6.961f,  3.19f,  0.45f },
        { "Arneb",           5.5453f, -17.822f,  2.58f,  0.21f },
        { "Nihal",           5.4708f, -20.759f,  2.84f,  0.82f },
        { "Phact",           5.6612f, -34.074f,  2.64f, -0.12f },
        { "Wazn",            5.8491f, -35.768f,  3.12f,  1.16f },
        { "Furud",           6.3382f, -30.063f,  3.02f, -0.19f },
        { "Gomeisa",         7.4525f,   8.289f,  2.89f, -0.09f },
        { "Tejat",           6.3826f,  22.514f,  2.88f,  1.64f },
        { "Mebsuta",         6.7322f,  25.131f,  2.98f,  1.40f },
        { "Tianguan",        5.6274f,  21.143f,  3.00f, -0.19f },
        { "Ain",             4.4769f,  19.180f,  3.53f,  1.01f },
        { "HyadumI",         4.3303f,  15.628f,  3.65f,  0.98f },
        { "Theta2Tauri",     4.4777f,  15.871f,  3.40f,  0.18f },
        // Pleiades (M45 Seven Sisters open cluster — resolved naked-eye sparkling jewel box!)
        { "Alcyone",         3.7914f,  24.105f,  2.87f, -0.09f },
        { "Atlas",           3.8197f,  24.053f,  3.63f, -0.09f },
        { "Electra",         3.7479f,  24.113f,  3.70f, -0.11f },
        { "Maia",            3.7639f,  24.368f,  3.87f, -0.07f },
        { "Merope",          3.7720f,  23.948f,  4.18f, -0.06f },
        { "Taygeta",         3.7534f,  24.467f,  4.30f, -0.11f },
        { "Pleione",         3.8203f,  24.137f,  5.09f, -0.08f },
        { "Celaeno",         3.7469f,  24.289f,  5.46f, -0.05f },
        // Orion's Sword & Belt surroundings
        { "Theta1OrionisC",  5.5881f,  -5.390f,  5.13f, -0.01f },
        { "NairAlSaif",      5.5906f,  -5.910f,  2.77f, -0.24f },
        { "EtaOrionis",      5.4079f,  -2.397f,  3.35f, -0.17f },
        { "SigmaOrionis",    5.6458f,  -2.600f,  3.80f, -0.24f },
        // Southern Cross + Centaurus + Carina + Vela
        { "Imai",           12.2524f, -58.749f,  2.80f, -0.23f },
        { "Ginan",          12.3539f, -60.401f,  3.59f,  1.39f },
        { "Aspidiske",       9.2848f, -59.275f,  2.25f,  0.18f },
        { "Markeb",          9.3683f, -55.011f,  2.48f, -0.18f },
        // Cygnus / Lyra / Aquila Summer Triangle companions
        { "AlbireoA",       19.5120f,  27.960f,  3.08f,  1.13f },
        { "GienahCygni",    20.7702f,  33.970f,  2.46f,  1.03f },
        { "DeltaCygni",     19.7496f,  45.131f,  2.87f, -0.03f },
        { "Sulafat",        18.9823f,  32.690f,  3.24f, -0.05f },
        { "Sheliak",        18.8347f,  33.363f,  3.52f, -0.05f },
        { "Tarazed",        19.7710f,  10.613f,  2.72f,  1.52f },
        // Ursa Major + Cassiopeia remaining stars
        { "Megrez",         12.2570f,  57.033f,  3.31f,  0.08f },
        { "Mizar",          13.3987f,  54.925f,  2.27f,  0.02f },
        { "Alcor",          13.4204f,  54.988f,  3.99f,  0.16f },
        { "Ruchbah",         1.4303f,  60.235f,  2.68f,  0.13f },
        { "Segin",           1.9066f,  63.670f,  3.37f, -0.15f }
    };
    return Catalogue;
}

Vector3f ResolveLunarBearingScene(const LunarBodyDescriptor& Moon) noexcept
{
    if (Moon.CustomBearingEnabled)
        return NormalizeVector(Moon.CustomBearingScene);

    const float CosElev = std::cos(Moon.ElevationRadians);
    return NormalizeVector(Vector3f{
        CosElev * std::cos(Moon.AzimuthRadians),
        CosElev * std::sin(Moon.AzimuthRadians),
        std::sin(Moon.ElevationRadians) });
}

CelestialRayEvaluation EvaluateCelestialSphereRay(const Vector3f&                         RayDirectionScene,
                                                  const RadiometricSkyConfiguration&      SkyConfiguration,
                                                  const std::vector<LunarBodyDescriptor>& Moons,
                                                  const StarfieldConfiguration&           StarConfiguration,
                                                  float                                   PixelAngularPitchRadians,
                                                  bool                                    VacuumSpaceCondition) noexcept
{
    CelestialRayEvaluation Result{};
    const Vector3f ViewDir    = NormalizeVector(RayDirectionScene);
    const float    PixelPitch = std::max(0.00020f, PixelAngularPitchRadians);

    // ① Evaluate deep-space starfield (J2000 Bright Star Catalogue + 3-tier Galactic Pogson stars + dust-extinguished Milky Way).
    SpectralColor DeepSpaceRadiance{ 0.0f, 0.0f, 0.0f };
    if (StarConfiguration.StarfieldEnabled)
    {
        const Vector3f EqDir  = TransformSceneToEquatorial(
            ViewDir, StarConfiguration.ObserverLatitudeDegrees, StarConfiguration.LocalSiderealHours);
        const Vector3f GalDir = TransformEquatorialToGalactic(EqDir);
        const float    GalSinB = std::clamp(GalDir.ZCoord, -1.0f, 1.0f);
        const float    GalPlaneConcentration = std::exp(-(GalSinB * GalSinB) / 0.042f);

        // Fraunhofer Airy first dark ring theta_0 = 1.22 * lambda / D (at 550 nm green).
        const float ApertureMetres      = std::max(0.004f, StarConfiguration.ApertureDiameterMillimetres * 0.001f);
        const float AiryFirstZeroGreen  = 1.22f * 550.0e-9f / ApertureMetres;
        const bool  SeeingActive        = StarConfiguration.AtmosphericSeeingEnabled && !VacuumSpaceCondition;
        const float AirmassApprox       = SeeingActive ? (1.0f / std::max(0.08f, ViewDir.ZCoord + 0.06f)) : 1.0f;
        const float SeeingHalfWidthRad  = SeeingActive
            ? (StarConfiguration.SeeingFullWidthArcseconds * 0.5f * (PiConstant / (180.0f * 3600.0f)) * std::pow(AirmassApprox, 0.6f))
            : 0.0f;

        // Pogson zero-magnitude flux scale. A point source deposits its whole flux inside one sensor footprint, whereas the Milky
        // Way is a SURFACE brightness spanning the same footprint — so the star scale must be ~2 orders of magnitude higher than the
        // unresolved-star-cloud scale for the two to coexist in one radiometrically consistent exposure (m = 0 saturates, m = 6.5 ~ 30/255).
        const float ZeroMagFlux = 0.9000f * StarConfiguration.RadiometricFluxScale;

        auto EvaluateScintillation = [&](float Salt) noexcept -> float
        {
            if (!SeeingActive)
                return 1.0f;
            const float ScintDepth = std::min(0.48f, 0.055f * std::pow(AirmassApprox, 1.15f));
            const float WaveA      = std::sin(SkyConfiguration.SequenceSeconds * 5.3f + Salt * 47.0f);
            const float WaveB      = std::cos(SkyConfiguration.SequenceSeconds * 8.9f - Salt * 31.0f);
            return 1.0f + ScintDepth * 0.5f * (WaveA + WaveB);
        };

        // 1a. Spatially indexed J2000 Hipparcos Bright Star Catalogue.
        if (StarConfiguration.BrightCatalogueEnabled)
        {
            const SpatialBrightStarIndex& StarIndex = QuerySpatialBrightStarIndex();
            int32_t Face = 0;
            float   FU = 0.0f, FV = 0.0f;
            ProjectDirectionToCubeFace(EqDir, Face, FU, FV);
            const int32_t CenterBX = std::clamp(
                static_cast<int32_t>(std::floor((FU * 0.5f + 0.5f) * SpatialBrightStarIndex::BinRes)),
                0, SpatialBrightStarIndex::BinRes - 1);
            const int32_t CenterBY = std::clamp(
                static_cast<int32_t>(std::floor((FV * 0.5f + 0.5f) * SpatialBrightStarIndex::BinRes)),
                0, SpatialBrightStarIndex::BinRes - 1);

            for (int32_t DY = -1; DY <= 1; ++DY)
            {
                const int32_t BY = CenterBY + DY;
                if (BY < 0 || BY >= SpatialBrightStarIndex::BinRes)
                    continue;
                for (int32_t DX = -1; DX <= 1; ++DX)
                {
                    const int32_t BX = CenterBX + DX;
                    if (BX < 0 || BX >= SpatialBrightStarIndex::BinRes)
                        continue;

                    const auto& Bucket = StarIndex.CellLists[
                        static_cast<std::size_t>((Face * SpatialBrightStarIndex::BinRes + BY) * SpatialBrightStarIndex::BinRes + BX)];
                    for (uint16_t StarOrd : Bucket)
                    {
                        const PreparedCatalogStar& Star = StarIndex.Stars[StarOrd];
                        if (Star.ApparentMag > StarConfiguration.LimitingMagnitude)
                            continue;
                        const float Chord = VectorLength(SubtractVector(EqDir, Star.EquatorialDir));
                        if (Chord > 0.035f)
                            continue;
                        const float AngSep = 2.0f * std::asin(std::clamp(0.5f * Chord, 0.0f, 1.0f));
                        const float Flux   = ZeroMagFlux * std::pow(10.0f, -0.4f * Star.ApparentMag)
                                           * EvaluateScintillation(Star.PhaseSalt);
                        DeepSpaceRadiance = AddSpectral(
                            DeepSpaceRadiance,
                            EvaluateOpticalStarPsf(
                                AngSep, PixelPitch, AiryFirstZeroGreen, SeeingHalfWidthRad, SeeingActive, Star.Chromaticity, Flux));
                    }
                }
            }
        }

        // 1b. Multi-tier Galactic-plane-weighted Pogson stellar population (~14,000 resolved background stars across the celestial sphere).
        struct StellarGridTier
        {
            int32_t GridResolution;
            float   MeanOccupancy;
            float   GalacticBoost;
            float   BrightMagFloor;
            float   FaintMagCeiling;
            int32_t TierSalt;
        };
        static constexpr std::array<StellarGridTier, 3u> Tiers {{
            {  88, 0.085f, 0.14f, 2.35f, 4.40f,  19 },
            { 148, 0.120f, 0.28f, 4.20f, 5.85f,  73 },
            { 232, 0.140f, 0.48f, 5.65f, 7.25f, 151 }
        }};

        int32_t CubeFace = 0;
        float   FaceU = 0.0f, FaceV = 0.0f;
        ProjectDirectionToCubeFace(EqDir, CubeFace, FaceU, FaceV);

        for (const StellarGridTier& Tier : Tiers)
        {
            if (Tier.BrightMagFloor > StarConfiguration.LimitingMagnitude)
                continue;

            const float GridX = (FaceU * 0.5f + 0.5f) * static_cast<float>(Tier.GridResolution);
            const float GridY = (FaceV * 0.5f + 0.5f) * static_cast<float>(Tier.GridResolution);
            const int32_t CX  = std::clamp(static_cast<int32_t>(std::floor(GridX)), 0, Tier.GridResolution - 1);
            const int32_t CY  = std::clamp(static_cast<int32_t>(std::floor(GridY)), 0, Tier.GridResolution - 1);

            const float OccupancyProb = StarConfiguration.GalacticStructureEnabled
                ? (Tier.MeanOccupancy + Tier.GalacticBoost * GalPlaneConcentration)
                : Tier.MeanOccupancy;

            for (int32_t DY = -1; DY <= 1; ++DY)
            {
                const int32_t NY = CY + DY;
                if (NY < 0 || NY >= Tier.GridResolution)
                    continue;
                for (int32_t DX = -1; DX <= 1; ++DX)
                {
                    const int32_t NX = CX + DX;
                    if (NX < 0 || NX >= Tier.GridResolution)
                        continue;

                    const int32_t SeedX = CubeFace * 509 + NX;
                    const float   RollProb = HashCelestialUnit(SeedX, NY, Tier.TierSalt);
                    if (RollProb >= OccupancyProb)
                        continue;

                    const float JitterU = HashCelestialUnit(SeedX + 17, NY - 11, Tier.TierSalt + 3);
                    const float JitterV = HashCelestialUnit(SeedX - 23, NY + 29, Tier.TierSalt + 7);
                    const float MagSeed = HashCelestialUnit(SeedX + 41, NY + 13, Tier.TierSalt + 11);
                    const float BvSeed  = HashCelestialUnit(SeedX - 37, NY - 19, Tier.TierSalt + 19);

                    const float CellSU = ((static_cast<float>(NX) + 0.14f + 0.72f * JitterU) / static_cast<float>(Tier.GridResolution)) * 2.0f - 1.0f;
                    const float CellSV = ((static_cast<float>(NY) + 0.14f + 0.72f * JitterV) / static_cast<float>(Tier.GridResolution)) * 2.0f - 1.0f;
                    const Vector3f StarEqDir = UnprojectCubeFaceToDirection(CubeFace, CellSU, CellSV);

                    const float Chord = VectorLength(SubtractVector(EqDir, StarEqDir));
                    if (Chord > PixelPitch * 4.0f)
                        continue;

                    // Exponential star-count power law: many more faint stars near FaintMagCeiling than bright stars near BrightMagFloor.
                    const float MagVal = Tier.BrightMagFloor
                                       + (Tier.FaintMagCeiling - Tier.BrightMagFloor) * std::pow(MagSeed, 0.58f);
                    if (MagVal > StarConfiguration.LimitingMagnitude)
                        continue;

                    // Realistic HR-diagram (B-V) distribution: rare O/B (-0.28), main-sequence A/F/G (0.1..0.8), K/M giants (1.0..1.75).
                    const float ColorBv = -0.26f + 1.98f * std::pow(BvSeed, 1.25f);
                    const SpectralColor StarChrom = EvaluatePlanckianStarColor(ColorBv);
                    const float AngSep = 2.0f * std::asin(std::clamp(0.5f * Chord, 0.0f, 1.0f));
                    const float Flux   = ZeroMagFlux * std::pow(10.0f, -0.4f * MagVal) * EvaluateScintillation(JitterU + JitterV);

                    DeepSpaceRadiance = AddSpectral(
                        DeepSpaceRadiance,
                        EvaluateOpticalStarPsf(
                            AngSep, PixelPitch, AiryFirstZeroGreen, SeeingHalfWidthRad, SeeingActive, StarChrom, Flux));
                }
            }
        }

        // 1c. 3D Volumetric Milky Way Unresolved Star-Clouds + Great Rift Molecular Dust Extinction.
        if (StarConfiguration.GalacticStructureEnabled && std::fabs(GalSinB) < 0.46f)
        {
            const float ThinDisk     = std::exp(-(GalSinB * GalSinB) / 0.0085f);
            const float ThickDisk    = std::exp(-(GalSinB * GalSinB) / 0.045f);
            const float BulgeCos     = std::max(0.0f, GalDir.XCoord);
            const float CentralBulge = std::pow(BulgeCos, 3.2f) * std::exp(-(GalSinB * GalSinB) / 0.022f);

            const float CloudStructure = EvaluateRotatedCelestialFbm(
                Vector3f{ GalDir.XCoord * 5.2f + 1.3f, GalDir.YCoord * 5.2f - 2.4f, GalDir.ZCoord * 14.0f }, 5u);
            const float DustFilament   = EvaluateRotatedCelestialFbm(
                Vector3f{ GalDir.XCoord * 9.5f - 3.7f, GalDir.YCoord * 9.5f + 1.9f, GalDir.ZCoord * 24.0f + 0.8f }, 5u);

            // Narrow dark molecular dust lane splitting the midplane (Great Rift / Coalsack extinction).
            const float MidplaneDustPlane = std::exp(-(GalSinB * GalSinB) / 0.0022f);
            const float DustOpticalDepth  = MidplaneDustPlane * std::clamp((DustFilament - 0.40f) * 2.1f, 0.0f, 1.10f);
            const SpectralColor DustTrans{
                std::exp(-DustOpticalDepth * 0.72f),
                std::exp(-DustOpticalDepth * 1.00f),
                std::exp(-DustOpticalDepth * 1.26f) };

            // Integrated starlight of the Galactic disk + bulge: a faint surface brightness ~3 orders of magnitude below a m = 0 star.
            const float DiskSurfaceBrightness = (0.000260f * ThinDisk * (0.55f + 0.95f * CloudStructure)
                                              +  0.000110f * ThickDisk) * StarConfiguration.RadiometricFluxScale;
            const float BulgeBrightness       = 0.000330f * CentralBulge * (0.65f + 0.70f * CloudStructure)
                                              * StarConfiguration.RadiometricFluxScale;

            DeepSpaceRadiance.Red   += (DiskSurfaceBrightness * 0.94f + BulgeBrightness * 1.10f) * DustTrans.Red;
            DeepSpaceRadiance.Green += (DiskSurfaceBrightness * 1.00f + BulgeBrightness * 1.00f) * DustTrans.Green;
            DeepSpaceRadiance.Blue  += (DiskSurfaceBrightness * 1.10f + BulgeBrightness * 0.88f) * DustTrans.Blue;
        }
    }

    // ② Sort active 3D moons back-to-front by DistanceKilometres and composite with Beer-Lambert lunar atmospheric pass-through!
    std::vector<std::size_t> ActiveIndices{};
    ActiveIndices.reserve(Moons.size());
    for (std::size_t Idx = 0u; Idx < Moons.size(); ++Idx)
    {
        if (Moons[Idx].VisibilityEnabled)
            ActiveIndices.push_back(Idx);
    }
    std::sort(ActiveIndices.begin(), ActiveIndices.end(), [&](std::size_t Left, std::size_t Right)
    {
        return Moons[Left].DistanceKilometres > Moons[Right].DistanceKilometres;
    });

    SpectralColor AccumulatedRadiance = DeepSpaceRadiance;
    SpectralColor SolarTransmission{ 1.0f, 1.0f, 1.0f };

    for (std::size_t MoonOrd : ActiveIndices)
    {
        const SingleMoonEvaluation MoonEval = EvaluateSingle3DMoon(
            Moons[MoonOrd], ViewDir, SkyConfiguration, PixelPitch);

        AccumulatedRadiance = AddSpectral(
            MultiplySpectral(AccumulatedRadiance, MoonEval.BackgroundTransmission),
            MoonEval.OutgoingRadiance);
        SolarTransmission = MultiplySpectral(SolarTransmission, MoonEval.BackgroundTransmission);

        if (MoonEval.SolidSurfaceHit)
            Result.SolidMoonHitCondition = true;
    }

    Result.ExtraterrestrialRadiance = AccumulatedRadiance;
    Result.SolarOcclusionWeight     = SolarTransmission;
    return Result;
}

} // namespace Frontier
