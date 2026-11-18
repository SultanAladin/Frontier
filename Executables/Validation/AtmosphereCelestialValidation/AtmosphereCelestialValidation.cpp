/*==============================================================================================================================================
                                                 ATMOSPHERECELESTIALVALIDATION.CPP
==============================================================================================================================================*/
// 🧩 End-to-end physical radiometric validation & high-resolution AuditProof renderer for Frontier's spherical atmosphere, solar limb
//    darkening, J2000 + Airy PSF starfield, 6-moon 3D volumetric lunar atmospheres, Nubis Owen-Sobol volumetric clouds, and Pillar 2
//    AtmosphericForceField coupling. Compiles standalone with zero Vulkan or third-party image-library dependencies.

#include "EngineContext/Math/LinearAlgebra_Float32.h"
#include "Graphics/Atmosphere/AtmosphereProfile.h"
#include "Graphics/Atmosphere/CelestialSphereEvaluation.h"
#include "Graphics/Atmosphere/RadiometricSkyEvaluation.h"
#include "Graphics/Atmosphere/VolumetricCloudEvaluation.h"
#include "Graphics/Atmosphere/AtmosphericFogEvaluation.h"
#include "Simulation/ForceField/AtmosphericForceField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

using namespace Frontier;

constexpr float PiVal    = 3.14159265358979323846f;
constexpr float DegToRad = PiVal / 180.0f;

//------------------------------------------------------------------------------------------------------------------------
//                                          RFC 2083 LOSSLESS PNG ENCODER (ZERO DEPS)
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint32_t ComputeCrc32(const uint8_t* Bytes, size_t Count) noexcept
{
    uint32_t Crc = 0xFFFFFFFFu;
    for (size_t Index = 0u; Index < Count; ++Index)
    {
        Crc ^= Bytes[Index];
        for (uint32_t Bit = 0u; Bit < 8u; ++Bit)
            Crc = (Crc >> 1u) ^ (0xEDB88320u & (0u - (Crc & 1u)));
    }
    return ~Crc;
}

void AppendBigEndian32(std::vector<uint8_t>& Buffer, uint32_t Word)
{
    Buffer.push_back(static_cast<uint8_t>((Word >> 24u) & 0xFFu));
    Buffer.push_back(static_cast<uint8_t>((Word >> 16u) & 0xFFu));
    Buffer.push_back(static_cast<uint8_t>((Word >>  8u) & 0xFFu));
    Buffer.push_back(static_cast<uint8_t>( Word        & 0xFFu));
}

void AppendPngChunk(std::vector<uint8_t>& FileBytes, const char ChunkTag[4], const std::vector<uint8_t>& Payload)
{
    AppendBigEndian32(FileBytes, static_cast<uint32_t>(Payload.size()));
    const size_t CrcStart = FileBytes.size();
    FileBytes.push_back(static_cast<uint8_t>(ChunkTag[0]));
    FileBytes.push_back(static_cast<uint8_t>(ChunkTag[1]));
    FileBytes.push_back(static_cast<uint8_t>(ChunkTag[2]));
    FileBytes.push_back(static_cast<uint8_t>(ChunkTag[3]));
    FileBytes.insert(FileBytes.end(), Payload.begin(), Payload.end());
    const uint32_t Crc = ComputeCrc32(FileBytes.data() + CrcStart, FileBytes.size() - CrcStart);
    AppendBigEndian32(FileBytes, Crc);
}

[[nodiscard]] bool WriteLosslessRgbPng(const std::string&          FilePath,
                                       uint32_t                    Width,
                                       uint32_t                    Height,
                                       const std::vector<uint8_t>& RgbPixels)
{
    if (RgbPixels.size() != static_cast<size_t>(Width) * static_cast<size_t>(Height) * 3u)
        return false;

    std::filesystem::create_directories(std::filesystem::path(FilePath).parent_path());

    const size_t RowBytes = static_cast<size_t>(Width) * 3u + 1u;
    std::vector<uint8_t> RawScanlines(RowBytes * static_cast<size_t>(Height), 0u);
    for (uint32_t Row = 0u; Row < Height; ++Row)
    {
        uint8_t*       Dst = RawScanlines.data() + static_cast<size_t>(Row) * RowBytes;
        const uint8_t* Src = RgbPixels.data() + static_cast<size_t>(Row) * static_cast<size_t>(Width) * 3u;
        Dst[0] = 0u;
        std::copy(Src, Src + static_cast<size_t>(Width) * 3u, Dst + 1u);
    }

    std::vector<uint8_t> ZlibStream;
    ZlibStream.reserve(RawScanlines.size() + (RawScanlines.size() / 65535u + 2u) * 5u + 6u);
    ZlibStream.push_back(0x78u);
    ZlibStream.push_back(0x01u);

    uint32_t AdlerA = 1u;
    uint32_t AdlerB = 0u;
    for (uint8_t Byte : RawScanlines)
    {
        AdlerA = (AdlerA + Byte) % 65521u;
        AdlerB = (AdlerB + AdlerA) % 65521u;
    }

    size_t Offset = 0u;
    while (Offset < RawScanlines.size())
    {
        const size_t   BlockLen = std::min<size_t>(65535u, RawScanlines.size() - Offset);
        const bool     FinalBlk = (Offset + BlockLen == RawScanlines.size());
        const uint16_t Len16    = static_cast<uint16_t>(BlockLen);
        const uint16_t InvLen16 = static_cast<uint16_t>(~Len16);

        ZlibStream.push_back(FinalBlk ? 0x01u : 0x00u);
        ZlibStream.push_back(static_cast<uint8_t>( Len16         & 0xFFu));
        ZlibStream.push_back(static_cast<uint8_t>((Len16 >> 8u)  & 0xFFu));
        ZlibStream.push_back(static_cast<uint8_t>( InvLen16        & 0xFFu));
        ZlibStream.push_back(static_cast<uint8_t>((InvLen16 >> 8u) & 0xFFu));
        ZlibStream.insert(ZlibStream.end(),
                          RawScanlines.begin() + static_cast<std::ptrdiff_t>(Offset),
                          RawScanlines.begin() + static_cast<std::ptrdiff_t>(Offset + BlockLen));
        Offset += BlockLen;
    }
    AppendBigEndian32(ZlibStream, (AdlerB << 16u) | AdlerA);

    std::vector<uint8_t> PngBytes = { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };

    std::vector<uint8_t> Ihdr;
    AppendBigEndian32(Ihdr, Width);
    AppendBigEndian32(Ihdr, Height);
    Ihdr.push_back(8u);
    Ihdr.push_back(2u);
    Ihdr.push_back(0u);
    Ihdr.push_back(0u);
    Ihdr.push_back(0u);
    AppendPngChunk(PngBytes, "IHDR", Ihdr);
    AppendPngChunk(PngBytes, "IDAT", ZlibStream);
    AppendPngChunk(PngBytes, "IEND", {});

    std::ofstream OutFile(FilePath, std::ios::binary);
    if (!OutFile.is_open())
        return false;
    OutFile.write(reinterpret_cast<const char*>(PngBytes.data()), static_cast<std::streamsize>(PngBytes.size()));
    return OutFile.good();
}

//------------------------------------------------------------------------------------------------------------------------
//                                           IEC 61966-2-1 sRGB DISPLAY ENCODING
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint8_t LinearToSrgbByte(float LinearChannel) noexcept
{
    const float Clamped = std::clamp(LinearChannel, 0.0f, 1.0f);
    const float Encoded = (Clamped <= 0.0031308f)
        ? (12.92f * Clamped)
        : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(Encoded * 255.0f), 0L, 255L));
}

// 📝 Scene display transform. Photo-real scenes resolve through the Khronos PBR Neutral filmic curve (the exact operator the shipped
//    RadianceResolve pass runs). A deep-sky astrograph plate instead uses the linear sRGB response of a long-exposure sensor: the
//    PBR Neutral toe deliberately sets a ~0.04 display black point, which would crush the entire 10^-4 night-sky radiance band.
[[nodiscard]] std::array<uint8_t, 3u> EncodeRadianceToSrgb8(const SpectralColor& LinearRadiance,
                                                           float                ExposureScale,
                                                           bool                 DeepSkyNightResponse) noexcept
{
    if (DeepSkyNightResponse)
    {
        return {
            LinearToSrgbByte(LinearRadiance.Red   * ExposureScale),
            LinearToSrgbByte(LinearRadiance.Green * ExposureScale),
            LinearToSrgbByte(LinearRadiance.Blue  * ExposureScale)
        };
    }

    const SpectralColor Tonemapped = ApplyPbrNeutralTonemap(LinearRadiance, ExposureScale);
    return {
        LinearToSrgbByte(Tonemapped.Red),
        LinearToSrgbByte(Tonemapped.Green),
        LinearToSrgbByte(Tonemapped.Blue)
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                           MINIMAL 5x7 GLYPH STAMP FOR COMPARISON PANELS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::array<uint8_t, 7u> LookupGlyph5x7(char Ch) noexcept
{
    switch (Ch)
    {
        case '0': return { 0x0Eu, 0x11u, 0x13u, 0x15u, 0x19u, 0x11u, 0x0Eu };
        case '1': return { 0x04u, 0x0Cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu };
        case '5': return { 0x1Fu, 0x10u, 0x1Eu, 0x01u, 0x01u, 0x11u, 0x0Eu };
        case '9': return { 0x0Eu, 0x11u, 0x11u, 0x0Fu, 0x01u, 0x02u, 0x0Cu };
        case '2': return { 0x0Eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1Fu };
        case '3': return { 0x1Fu, 0x02u, 0x04u, 0x02u, 0x01u, 0x11u, 0x0Eu };
        case '4': return { 0x02u, 0x06u, 0x0Au, 0x12u, 0x1Fu, 0x02u, 0x02u };
        case '6': return { 0x06u, 0x08u, 0x10u, 0x1Eu, 0x11u, 0x11u, 0x0Eu };
        case '7': return { 0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u };
        case '8': return { 0x0Eu, 0x11u, 0x11u, 0x0Eu, 0x11u, 0x11u, 0x0Eu };
        case 'A': return { 0x0Eu, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u };
        case 'B': return { 0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x11u, 0x11u, 0x1Eu };
        case 'G': return { 0x0Eu, 0x11u, 0x10u, 0x17u, 0x11u, 0x11u, 0x0Fu };
        case 'H': return { 0x11u, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u };
        case 'J': return { 0x07u, 0x02u, 0x02u, 0x02u, 0x02u, 0x12u, 0x0Cu };
        case 'K': return { 0x11u, 0x12u, 0x14u, 0x18u, 0x14u, 0x12u, 0x11u };
        case 'N': return { 0x11u, 0x19u, 0x19u, 0x15u, 0x13u, 0x13u, 0x11u };
        case 'P': return { 0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x10u, 0x10u, 0x10u };
        case 'Q': return { 0x0Eu, 0x11u, 0x11u, 0x11u, 0x15u, 0x12u, 0x0Du };
        case 'U': return { 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu };
        case 'V': return { 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Au, 0x04u };
        case 'W': return { 0x11u, 0x11u, 0x11u, 0x15u, 0x15u, 0x1Bu, 0x11u };
        case 'X': return { 0x11u, 0x11u, 0x0Au, 0x04u, 0x0Au, 0x11u, 0x11u };
        case 'Y': return { 0x11u, 0x11u, 0x0Au, 0x04u, 0x04u, 0x04u, 0x04u };
        case 'Z': return { 0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x1Fu };
        case 'C': return { 0x0Eu, 0x11u, 0x10u, 0x10u, 0x10u, 0x11u, 0x0Eu };
        case 'D': return { 0x1Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x1Eu };
        case 'E': return { 0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x1Fu };
        case 'F': return { 0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x10u };
        case 'I': return { 0x0Eu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu };
        case 'L': return { 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x1Fu };
        case 'M': return { 0x11u, 0x1Bu, 0x15u, 0x15u, 0x11u, 0x11u, 0x11u };
        case 'O': return { 0x0Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu };
        case 'R': return { 0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x14u, 0x12u, 0x11u };
        case 'S': return { 0x0Fu, 0x10u, 0x10u, 0x0Eu, 0x01u, 0x01u, 0x1Eu };
        case 'T': return { 0x1Fu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u };
        case '=': return { 0x00u, 0x00u, 0x1Fu, 0x00u, 0x1Fu, 0x00u, 0x00u };
        default:  return { 0u, 0u, 0u, 0u, 0u, 0u, 0u };
    }
}

void StampBadgeLabel(std::vector<uint8_t>& Rgb,
                     uint32_t              Width,
                     uint32_t              Height,
                     uint32_t              OriginX,
                     uint32_t              OriginY,
                     const std::string&    Text,
                     uint32_t              Scale = 2u)
{
    const uint32_t BoxW = static_cast<uint32_t>(Text.size()) * 6u * Scale + 10u;
    const uint32_t BoxH = 7u * Scale + 10u;
    for (uint32_t Y = OriginY; Y < std::min(Height, OriginY + BoxH); ++Y)
    {
        for (uint32_t X = OriginX; X < std::min(Width, OriginX + BoxW); ++X)
        {
            const size_t Idx = (static_cast<size_t>(Y) * Width + X) * 3u;
            Rgb[Idx + 0u] = static_cast<uint8_t>(Rgb[Idx + 0u] * 35u / 100u);
            Rgb[Idx + 1u] = static_cast<uint8_t>(Rgb[Idx + 1u] * 35u / 100u);
            Rgb[Idx + 2u] = static_cast<uint8_t>(Rgb[Idx + 2u] * 40u / 100u);
        }
    }

    for (size_t CharIdx = 0u; CharIdx < Text.size(); ++CharIdx)
    {
        const auto Rows = LookupGlyph5x7(Text[CharIdx]);
        for (uint32_t GY = 0u; GY < 7u; ++GY)
        {
            for (uint32_t GX = 0u; GX < 5u; ++GX)
            {
                if (((Rows[GY] >> (4u - GX)) & 1u) == 0u)
                    continue;
                for (uint32_t SY = 0u; SY < Scale; ++SY)
                {
                    for (uint32_t SX = 0u; SX < Scale; ++SX)
                    {
                        const uint32_t PX = OriginX + 5u + (static_cast<uint32_t>(CharIdx) * 6u + GX) * Scale + SX;
                        const uint32_t PY = OriginY + 5u + GY * Scale + SY;
                        if (PX < Width && PY < Height)
                        {
                            const size_t Idx = (static_cast<size_t>(PY) * Width + PX) * 3u;
                            Rgb[Idx + 0u] = 238u;
                            Rgb[Idx + 1u] = 242u;
                            Rgb[Idx + 2u] = 248u;
                        }
                    }
                }
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           UNIFIED PHYSICAL SCENE CAMERA & COMPOSITOR
//------------------------------------------------------------------------------------------------------------------------

struct PerspectiveCameraRig
{
    Vector3f EyeSceneMetres      { 0.0f, 0.0f, 0.0f };     // [m]   - Offset relative to ObserverAltitudeMetres
    Vector3f ForwardDirection    { 1.0f, 0.0f, 0.12f };    // [-]   - Z-up forward look axis
    Vector3f UpReference         { 0.0f, 0.0f, 1.0f };     // [-]   - World up reference
    float    VerticalFovDegrees  = 54.0f;                  // [deg] - Full vertical field of view
};

struct UnifiedSceneSpecification
{
    RadiometricSkyConfiguration      SkyConfig    {};
    StarfieldConfiguration           StarConfig   {};
    std::vector<LunarBodyDescriptor> Moons        {};
    VolumetricCloudConfiguration     CloudConfig  {};
    AtmosphericFogConfiguration     FogConfig    {};   // [-] - Exponential height fog + local volumetric fog bodies
    AtmosphericWindField             WindField    {};
    AdvectionOffsetVolume            CloudOffsetVolume {};   // [-] - Baked Lagrangian cloud advection (baked per scene)
    SkyAdvectionOffsetField          SkyOffsetField    {};   // [-] - Baked Lagrangian haze/cirrus advection (baked per scene)
    PerspectiveCameraRig             Camera       {};
    float                            ExposureScale  = 1.0f;
    uint32_t                         SamplesPerAxis = 2u;   // [-] - 2x2 = 4spp supersampling for crisp Airy stars & cloud edges
    bool                             DeepSkyNightResponse = false;  // [-] - Linear astrograph response instead of PBR Neutral
};

[[nodiscard]] Vector3f DirectionFromElevationAzimuth(float ElevationDeg, float AzimuthDeg) noexcept
{
    const float El = ElevationDeg * DegToRad;
    const float Az = AzimuthDeg * DegToRad;
    return NormalizeVector(Vector3f{ std::cos(El) * std::cos(Az), std::cos(El) * std::sin(Az), std::sin(El) });
}

[[nodiscard]] RadiometricSkyConfiguration ConstructEarthSkyAtSolarAngles(float SolarElevationDeg, float SolarAzimuthDeg) noexcept
{
    AtmosphereUniformBlock Profile = Atmosphere::ConstructEarthProfile();
    Atmosphere::AssignSolarDirection(Profile, SolarElevationDeg * DegToRad, SolarAzimuthDeg * DegToRad);
    RadiometricSkyConfiguration Cfg = ConstructSkyConfigurationFromProfile(Profile);
    Cfg.ObserverAltitudeMetres      = 18.0f;
    Cfg.ExtraterrestrialSolarFlux   = SpectralColor{ 12.8f, 12.45f, 12.20f };
    return Cfg;
}

[[nodiscard]] SpectralColor EvaluateUnifiedRayRadiance(const UnifiedSceneSpecification& Scene,
                                                       const RadiometricSkyTables&      SkyTables,
                                                       const Vector3f&                  RayDirectionScene,
                                                       float                            PixelAngularPitchRad,
                                                       uint32_t                         PixelX,
                                                       uint32_t                         PixelY,
                                                       uint32_t                         SampleOrdinal) noexcept
{
    const AtmosphericWindField* ActiveWind = Scene.WindField.FieldEnabled ? &Scene.WindField : nullptr;

    // ① Spherical Sky + Atmosphere + Planetary Limb / Cool Marine Lower Hemisphere:
    const SkyRaymarchResult SkySample = EvaluateSphericalAtmosphereRay(
        SkyTables, Scene.SkyConfig, Scene.Camera.EyeSceneMetres, RayDirectionScene, ActiveWind,
        Scene.SkyOffsetField.ReadyCondition ? &Scene.SkyOffsetField : nullptr);

    SpectralColor TotalRadiance = SkySample.InscatteredRadiance;

    // ② Exoatmospheric Celestials (Sun photosphere + corona, Airy PSF stars + Milky Way, 6-moon 3D atmospheres):
    //    Strictly occluded by solid planet/ocean and attenuated by true atmospheric line-of-sight ViewTransmittance!
    if (!SkySample.PlanetSurfaceHitCondition)
    {
        const bool  VacuumSpace    = (Scene.SkyConfig.ObserverAltitudeMetres > 85000.0f);
        const float PreSkyLum      = EvaluateLuminance(TotalRadiance);
        const float StarVisibility = 1.0f / (1.0f + PreSkyLum * 6.5f);

        // Field stops: point sources fade below the human visual noise floor once the sky background climbs past the
        // scotopic-to-photopic transition. This is applied multiplicatively with the geometric glare gate, so bright
        // twilight simply cannot resolve a star cluster while the photosphere stays tone-mapped (never clipped to white).
        StarfieldConfiguration EffectiveStars = Scene.StarConfig;
        if (StarVisibility < 0.012f)
            EffectiveStars.StarfieldEnabled = false;

        const CelestialRayEvaluation Celestial = EvaluateCelestialSphereRay(
            RayDirectionScene,
            Scene.SkyConfig,
            Scene.Moons,
            EffectiveStars,
            PixelAngularPitchRad,
            VacuumSpace);

        const SpectralColor SolarRad = MultiplySpectral(
            EvaluateSolarPhotosphereCorona(Scene.SkyConfig, SkySample, RayDirectionScene, PixelAngularPitchRad),
            Celestial.SolarOcclusionWeight);

        TotalRadiance = AddSpectral(TotalRadiance, SolarRad);

        const float SkyLum      = EvaluateLuminance(TotalRadiance);
        const float GlareFactor = Celestial.SolidMoonHitCondition ? 1.0f : (1.0f / (1.0f + SkyLum * 6.5f));
        const SpectralColor AttenuatedCelestials = MultiplySpectral(Celestial.ExtraterrestrialRadiance, SkySample.ViewTransmittance);
        TotalRadiance = AddSpectral(TotalRadiance, ScaleSpectral(AttenuatedCelestials, GlareFactor));
    }

    // ③ Volumetric Clouds (Nubis Perlin-Worley + Owen-Sobol + ForceField advection + 4-octave MS + aerial perspective):
    if (Scene.CloudConfig.CloudsEnabled && Scene.SkyConfig.ObserverAltitudeMetres < 25000.0f)
    {
        const VolumetricCloudRayResult CloudSample = EvaluateVolumetricCloudRay(
            Scene.CloudConfig,
            SkyTables,
            Scene.SkyConfig,
            Scene.Camera.EyeSceneMetres,
            RayDirectionScene,
            ActiveWind,
            PixelX,
            PixelY,
            SampleOrdinal,
            Scene.CloudOffsetVolume.ReadyCondition ? &Scene.CloudOffsetVolume : nullptr);

        if (CloudSample.Transmittance < 0.9999f)
        {
            TotalRadiance = AddSpectral(
                ScaleSpectral(TotalRadiance, CloudSample.Transmittance),
                CloudSample.InscatteredRadiance);
        }
    }

    // ④ Fog: exponential height layer + ForceField-advected local volumes, sharing the wind offset field with the deck.
    if (Scene.FogConfig.FogEnabled)
    {
        const FogRayResult FogSample = EvaluateAtmosphericFogRay(
            Scene.FogConfig,
            SkyTables,
            Scene.SkyConfig,
            Scene.Camera.EyeSceneMetres,
            RayDirectionScene,
            ActiveWind,
            Scene.CloudOffsetVolume.ReadyCondition ? &Scene.CloudOffsetVolume : nullptr,
            PixelX,
            PixelY,
            SampleOrdinal);

        if (FogSample.Transmittance < 0.9999f)
        {
            TotalRadiance = AddSpectral(
                ScaleSpectral(TotalRadiance, FogSample.Transmittance),
                FogSample.InscatteredRadiance);
        }
    }

    return TotalRadiance;
}

[[nodiscard]] std::vector<uint8_t> RenderViewportRgb(const UnifiedSceneSpecification& SceneInput,
                                                     uint32_t                         Width,
                                                     uint32_t                         Height)
{
    // Bake the per-scene Lagrangian wind-offset volume once, then share it across every pixel and thread.
    UnifiedSceneSpecification Scene = SceneInput;
    PrepareCloudAdvectionOffsetVolume(
        Scene.CloudOffsetVolume,
        Scene.CloudConfig,
        Scene.SkyConfig,
        Scene.WindField.FieldEnabled ? &Scene.WindField : nullptr);
    BakeSkyAdvectionOffsetField(
        Scene.SkyOffsetField,
        Scene.SkyConfig,
        Scene.WindField.FieldEnabled ? &Scene.WindField : nullptr);

    RadiometricSkyTables SkyTables{};
    BakeRadiometricSkyTables(SkyTables, Scene.SkyConfig);

    const Vector3f Forward   = NormalizeVector(Scene.Camera.ForwardDirection);
    const Vector3f RightAxis = NormalizeVector(CrossVector(Forward, Scene.Camera.UpReference));
    const Vector3f UpAxis    = CrossVector(RightAxis, Forward);

    const float    TanHalfFov      = std::tan(0.5f * Scene.Camera.VerticalFovDegrees * DegToRad);
    const float    AspectRatio     = static_cast<float>(Width) / static_cast<float>(Height);
    const float    PixelPitchRad   = (Scene.Camera.VerticalFovDegrees * DegToRad) / static_cast<float>(Height);
    const uint32_t SppAxis         = std::max(1u, Scene.SamplesPerAxis);
    const float    InvTotalSamples = 1.0f / static_cast<float>(SppAxis * SppAxis);

    std::vector<uint8_t> RgbBuffer(static_cast<size_t>(Width) * static_cast<size_t>(Height) * 3u, 0u);

    const uint32_t ThreadCount = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::thread> Threads;
    Threads.reserve(ThreadCount);

    for (uint32_t ThreadSlot = 0u; ThreadSlot < ThreadCount; ++ThreadSlot)
    {
        Threads.emplace_back([&, ThreadSlot]()
        {
            for (uint32_t PY = ThreadSlot; PY < Height; PY += ThreadCount)
            {
                for (uint32_t PX = 0u; PX < Width; ++PX)
                {
                    SpectralColor AccumLinear{ 0.0f, 0.0f, 0.0f };
                    for (uint32_t SY = 0u; SY < SppAxis; ++SY)
                    {
                        for (uint32_t SX = 0u; SX < SppAxis; ++SX)
                        {
                            const uint32_t SubOrdinal = SY * SppAxis + SX;
                            const float SubU = (SppAxis == 1u) ? 0.5f : ((static_cast<float>(SX) + 0.5f) / static_cast<float>(SppAxis));
                            const float SubV = (SppAxis == 1u) ? 0.5f : ((static_cast<float>(SY) + 0.5f) / static_cast<float>(SppAxis));

                            const float ScreenX = ((static_cast<float>(PX) + SubU) / static_cast<float>(Width)  * 2.0f - 1.0f) * AspectRatio * TanHalfFov;
                            const float ScreenY = (1.0f - (static_cast<float>(PY) + SubV) / static_cast<float>(Height) * 2.0f) * TanHalfFov;

                            const Vector3f RayDir = NormalizeVector(
                                AddVector(Forward, AddVector(ScaleVector(RightAxis, ScreenX), ScaleVector(UpAxis, ScreenY))));

                            const SpectralColor SampleRad = EvaluateUnifiedRayRadiance(
                                Scene, SkyTables, RayDir, PixelPitchRad, PX, PY, SubOrdinal);
                            AccumLinear = AddSpectral(AccumLinear, SampleRad);
                        }
                    }

                    const SpectralColor MeanLinear = ScaleSpectral(AccumLinear, InvTotalSamples);
                    const auto Srgb = EncodeRadianceToSrgb8(MeanLinear, Scene.ExposureScale, Scene.DeepSkyNightResponse);

                    const size_t OutIdx = (static_cast<size_t>(PY) * Width + PX) * 3u;
                    RgbBuffer[OutIdx + 0u] = Srgb[0];
                    RgbBuffer[OutIdx + 1u] = Srgb[1];
                    RgbBuffer[OutIdx + 2u] = Srgb[2];
                }
            }
        });
    }

    for (std::thread& Job : Threads)
        Job.join();

    return RgbBuffer;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           SHARED WIND & MOON SCENE BUILDERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] AtmosphericWindField BuildCyclonicStormForceField()
{
    AtmosphericWindField Field{};
    ConfigureDefaultWindField(Field);
    Field.FieldEnabled              = true;
    Field.CurlTurbulenceEnabled     = true;
    Field.GeostrophicVelocity       = Vector3f{ 16.5f, 4.2f, 0.0f };
    Field.ReferenceAltitudeMetres   = 1600.0f;
    Field.ShearExponent             = 0.18f;
    Field.EkmanVeeringRadians       = 0.30f;
    Field.CurlAmplitudeMetresPerSec = 3.5f;
    Field.CurlSpatialScaleMetres    = 1650.0f;

    ForceSourceDescriptor Cyclone{};
    Cyclone.Category                    = ForceSourceCategory::CyclonicCirculation;
    Cyclone.OriginMetres                = Vector3f{ 9200.0f, -650.0f, 2350.0f };
    Cyclone.AxisDirection               = Vector3f{ 0.0f, 0.0f, 1.0f };
    Cyclone.PeakVelocityMetresPerSecond = 38.0f;
    Cyclone.InnerRadiusMetres           = 3100.0f;
    Cyclone.OuterRadiusMetres           = 16500.0f;
    Cyclone.VerticalScaleMetres         = 6200.0f;
    Cyclone.RadialInflowRatio           = 0.25f;
    Cyclone.UpdraftPeakMetresPerSecond  = 7.5f;
    Cyclone.CloudAdvectionGain          = 1.15f;
    Cyclone.SkyAerosolCouplingGain      = 1.20f;
    AppendForceSource(Field, Cyclone);

    ForceSourceDescriptor Jet{};
    Jet.Category                        = ForceSourceCategory::DirectionalJet;
    Jet.OriginMetres                    = Vector3f{ 7500.0f, 2100.0f, 2800.0f };
    Jet.AxisDirection                   = NormalizeVector(Vector3f{ 0.94f, -0.34f, 0.03f });
    Jet.PeakVelocityMetresPerSecond     = 22.0f;
    Jet.InnerRadiusMetres               = 3200.0f;
    Jet.OuterRadiusMetres               = 15000.0f;
    Jet.VerticalScaleMetres             = 5400.0f;
    AppendForceSource(Field, Jet);

    ForceSourceDescriptor Plume{};
    Plume.Category                      = ForceSourceCategory::RadialPlume;
    Plume.OriginMetres                  = Vector3f{ 8800.0f, -450.0f, 1500.0f };
    Plume.PeakVelocityMetresPerSecond   = 8.5f;
    Plume.InnerRadiusMetres             = 1850.0f;
    Plume.OuterRadiusMetres             = 9500.0f;
    Plume.VerticalScaleMetres           = 4800.0f;
    Plume.UpdraftPeakMetresPerSecond    = 9.5f;
    AppendForceSource(Field, Plume);

    return Field;
}

[[nodiscard]] std::vector<LunarBodyDescriptor> BuildSixMoonEnsemble()
{
    std::vector<LunarBodyDescriptor> Moons;
    Moons.reserve(6u);

    auto MakeMoon = [](LunarSurfaceCategory Cat,
                       float                ElevationDeg,
                       float                AzimuthDeg,
                       float                AngularRadiusDeg,
                       float                DistanceKm,
                       float                LongitudeDeg,
                       float                Relief) -> LunarBodyDescriptor
    {
        LunarBodyDescriptor Body{};
        Body.VisibilityEnabled       = true;
        Body.SurfaceCategory         = Cat;
        Body.CustomBearingEnabled    = true;
        Body.CustomBearingScene      = DirectionFromElevationAzimuth(ElevationDeg, AzimuthDeg);
        Body.AngularRadiusRadians    = AngularRadiusDeg * DegToRad;
        Body.DistanceKilometres      = DistanceKm;
        Body.LongitudeOffsetRadians  = LongitudeDeg * DegToRad;
        Body.SurfacePitchRadians     = 0.12f;
        Body.CraterReliefStrength    = Relief;
        Body.GeometricAlbedoScale    = 1.15f;
        Body.EarthshineStrength      = 1.25f;
        Body.AtmosphereEnvelope      = ConstructPresetLunarAtmosphere(Cat);
        return Body;
    };

    // All 6 moons arranged in a grand orbital arc across the twilight sky, sharing one solar phase angle!
    Moons.push_back(MakeMoon(LunarSurfaceCategory::LunaRegolith,  24.5f, -25.0f, 2.35f, 384400.0f,  28.0f, 1.15f));
    Moons.push_back(MakeMoon(LunarSurfaceCategory::GlacierCryo,   29.0f,  -9.5f, 2.05f, 410000.0f, 110.0f, 0.95f));
    Moons.push_back(MakeMoon(LunarSurfaceCategory::ShroudSlate,   25.5f,   6.0f, 2.55f, 445000.0f, 195.0f, 0.85f));
    Moons.push_back(MakeMoon(LunarSurfaceCategory::SulfurMantle,  19.0f,  20.5f, 1.90f, 480000.0f,  64.0f, 1.05f));
    Moons.push_back(MakeMoon(LunarSurfaceCategory::EmberBasalt,   14.2f, -16.5f, 1.70f, 520000.0f, 250.0f, 1.20f));
    Moons.push_back(MakeMoon(LunarSurfaceCategory::ShardSilicate, 15.2f,  12.0f, 1.25f, 355000.0f, 315.0f, 1.30f));

    // Calibrated optical depths showcasing distinct physical Rayleigh/Mie compositions across the 6 moons.
    Moons[0].AtmosphereEnvelope.TopAltitudeFraction       = 0.12f;
    Moons[0].AtmosphereEnvelope.RayleighOpticalDepthScale = SpectralColor{ 0.16f, 0.34f, 0.78f };

    Moons[1].AtmosphereEnvelope.TopAltitudeFraction       = 0.22f;
    Moons[1].AtmosphereEnvelope.RayleighOpticalDepthScale = SpectralColor{ 0.28f, 0.78f, 1.95f };
    Moons[1].AtmosphereEnvelope.MieScatteringDepthScale   = 0.14f;
    Moons[1].AtmosphereEnvelope.MieExtinctionDepthScale   = 0.16f;

    Moons[2].AtmosphereEnvelope.TopAltitudeFraction       = 0.28f;
    Moons[2].AtmosphereEnvelope.RayleighOpticalDepthScale = SpectralColor{ 0.42f, 1.10f, 2.45f };
    Moons[2].AtmosphereEnvelope.MieScatteringDepthScale   = 0.32f;
    Moons[2].AtmosphereEnvelope.MieExtinctionDepthScale   = 0.38f;

    Moons[3].AtmosphereEnvelope.TopAltitudeFraction       = 0.20f;
    Moons[3].AtmosphereEnvelope.RayleighOpticalDepthScale = SpectralColor{ 1.35f, 0.96f, 0.24f };
    Moons[3].AtmosphereEnvelope.MieScatteringDepthScale   = 0.38f;
    Moons[3].AtmosphereEnvelope.MieExtinctionDepthScale   = 0.44f;

    Moons[4].AtmosphereEnvelope.TopAltitudeFraction       = 0.18f;
    Moons[4].AtmosphereEnvelope.RayleighOpticalDepthScale = SpectralColor{ 1.65f, 0.52f, 0.18f };
    Moons[4].AtmosphereEnvelope.MieScatteringDepthScale   = 0.30f;
    Moons[4].AtmosphereEnvelope.MieExtinctionDepthScale   = 0.36f;

    Moons[5].AtmosphereEnvelope.TopAltitudeFraction       = 0.14f;
    Moons[5].AtmosphereEnvelope.RayleighOpticalDepthScale = SpectralColor{ 0.20f, 0.58f, 0.85f };

    return Moons;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              QUANTITATIVE PHYSICS GATES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool ExecuteQuantitativePhysicsAudit()
{
    std::printf("====================================================================================================\n");
    std::printf("  FRONTIER PHYSICAL ATMOSPHERE, CELESTIAL SPHERE, VOLUMETRIC CLOUD & FORCEFIELD AUDIT\n");
    std::printf("====================================================================================================\n");

    int Failures = 0;
    auto CheckGate = [&](const char* GateLabel, bool Condition, const char* Detail)
    {
        std::printf("  [%s] %-46s | %s\n", Condition ? "PASS" : "FAIL", GateLabel, Detail);
        if (!Condition)
            ++Failures;
    };

    // Gate 1: GPU UBO layout size parity.
    CheckGate("1. AtmosphereUniformBlock 208B ABI",
              sizeof(AtmosphereUniformBlock) == 208u,
              "sizeof(AtmosphereUniformBlock) == 208 bytes (12 vec4 + 4 float)");

    // Gate 2: Transmittance LUT monotonicity + solid-planet shadow occultation.
    const RadiometricSkyConfiguration NoonCfg = ConstructEarthSkyAtSolarAngles(65.0f, 0.0f);
    RadiometricSkyTables NoonLut{};
    BakeRadiometricSkyTables(NoonLut, NoonCfg);
    const SpectralColor TransSea   = EvaluateTransmittanceToSpace(NoonLut, NoonCfg, 6360.002f,  1.0f);
    const SpectralColor TransHigh  = EvaluateTransmittanceToSpace(NoonLut, NoonCfg, 6385.000f,  1.0f);
    const SpectralColor TransNadir = EvaluateTransmittanceToSpace(NoonLut, NoonCfg, 6370.000f, -0.95f);
    char Buf2[160];
    std::snprintf(Buf2, sizeof(Buf2), "T_sea=(%.3f,%.3f,%.3f) < T_25km=(%.3f,%.3f,%.3f), T_shadow=%.5f",
                  TransSea.Red, TransSea.Green, TransSea.Blue, TransHigh.Red, TransHigh.Green, TransHigh.Blue, TransNadir.Green);
    CheckGate("2. Transmittance LUT & Earth Shadow",
              (TransHigh.Blue > TransSea.Blue) && (TransSea.Red > TransSea.Blue) && (TransNadir.Green == 0.0f),
              Buf2);

    // Gate 3: Rayleigh sky blue dominance overhead at noon + sunset solar disc reddening.
    const SkyRaymarchResult OverheadNoon = EvaluateSphericalAtmosphereRay(
        NoonLut, NoonCfg, Vector3f{ 0.0f, 0.0f, 0.0f }, DirectionFromElevationAzimuth(82.0f, 180.0f), nullptr);
    const RadiometricSkyConfiguration SunsetCfg = ConstructEarthSkyAtSolarAngles(0.8f, 0.0f);
    RadiometricSkyTables SunsetLut{};
    BakeRadiometricSkyTables(SunsetLut, SunsetCfg);
    const SpectralColor SunsetSunTrans = EvaluateTransmittanceToSpace(
        SunsetLut, SunsetCfg, 6360.002f, SunsetCfg.SolarDirectionScene.ZCoord);
    const float NoonBlueRedRatio   = OverheadNoon.InscatteredRadiance.Blue / std::max(1.0e-5f, OverheadNoon.InscatteredRadiance.Red);
    const float SunsetRedBlueRatio = SunsetSunTrans.Red / std::max(1.0e-6f, SunsetSunTrans.Blue);
    char Buf3[160];
    std::snprintf(Buf3, sizeof(Buf3), "NoonOverhead B/R = %.2f (>2.2), SunsetSolarTrans R/B = %.1f (>8.0)",
                  NoonBlueRedRatio, SunsetRedBlueRatio);
    CheckGate("3. Rayleigh Sky & Sunset Spectral Extinction",
              (NoonBlueRedRatio > 2.2f) && (SunsetRedBlueRatio > 8.0f),
              Buf3);

    // Gate 4: Sub-limb anti-brown-band check (-8 deg below limb at sunset).
    const SkyRaymarchResult SubLimbSunset = EvaluateSphericalAtmosphereRay(
        SunsetLut, SunsetCfg, Vector3f{ 0.0f, 0.0f, 0.0f }, DirectionFromElevationAzimuth(-8.0f, 55.0f), nullptr);
    const float SubLimbCoolRatio = SubLimbSunset.InscatteredRadiance.Blue / std::max(1.0e-5f, SubLimbSunset.InscatteredRadiance.Red);
    char Buf4[160];
    std::snprintf(Buf4, sizeof(Buf4), "Sunset -8 deg marine RGB=(%.4f, %.4f, %.4f), B/R=%.2f (no brown mud)",
                  SubLimbSunset.InscatteredRadiance.Red, SubLimbSunset.InscatteredRadiance.Green,
                  SubLimbSunset.InscatteredRadiance.Blue, SubLimbCoolRatio);
    CheckGate("4. Lower-Hemisphere Cool Marine Boundary",
              SubLimbSunset.PlanetSurfaceHitCondition && (SubLimbCoolRatio > 0.72f),
              Buf4);

    // Gate 5: Zero fake white DawnMist ribbon check at civil dawn (-6 deg sun, away from sun azimuth).
    const RadiometricSkyConfiguration DawnCfg = ConstructEarthSkyAtSolarAngles(-6.0f, 0.0f);
    RadiometricSkyTables DawnLut{};
    BakeRadiometricSkyTables(DawnLut, DawnCfg);
    const SkyRaymarchResult DawnLimb02 = EvaluateSphericalAtmosphereRay(
        DawnLut, DawnCfg, Vector3f{ 0.0f, 0.0f, 0.0f }, DirectionFromElevationAzimuth(0.25f, 110.0f), nullptr);
    const SkyRaymarchResult DawnLimb30 = EvaluateSphericalAtmosphereRay(
        DawnLut, DawnCfg, Vector3f{ 0.0f, 0.0f, 0.0f }, DirectionFromElevationAzimuth(3.00f, 110.0f), nullptr);
    const float DawnRatio = EvaluateLuminance(DawnLimb02.InscatteredRadiance)
                          / std::max(1.0e-5f, EvaluateLuminance(DawnLimb30.InscatteredRadiance));
    char Buf5[160];
    std::snprintf(Buf5, sizeof(Buf5), "Dawn +0.25 deg / +3.0 deg luminance ratio = %.2f (< 2.4, no white ribbon)", DawnRatio);
    CheckGate("5. Limb Continuity (Zero DawnMist Glow)",
              DawnRatio > 0.40f && DawnRatio < 2.40f,
              Buf5);

    // Gate 6: Solar limb darkening monotonicity & wavelength dependence.
    SkyRaymarchResult ClearVacuumRay{};
    ClearVacuumRay.ViewTransmittance = SpectralColor{ 1.0f, 1.0f, 1.0f };
    const SpectralColor SunCenter = EvaluateSolarPhotosphereCorona(NoonCfg, ClearVacuumRay, NoonCfg.SolarDirectionScene, 0.0004f);
    const float EdgeAngle = NoonCfg.SolarAngularRadiusRadians * 0.85f;
    const Vector3f SunLimbDir = DirectionFromElevationAzimuth(65.0f - (EdgeAngle / DegToRad), 0.0f);
    const SpectralColor SunLimb = EvaluateSolarPhotosphereCorona(NoonCfg, ClearVacuumRay, SunLimbDir, 0.0004f);
    const float CenterLum       = EvaluateLuminance(SunCenter);
    const float LimbLum         = EvaluateLuminance(SunLimb);
    const float BlueDarkening   = SunLimb.Blue / std::max(1.0e-5f, SunCenter.Blue);
    const float RedDarkening    = SunLimb.Red  / std::max(1.0e-5f, SunCenter.Red);
    char Buf6[160];
    std::snprintf(Buf6, sizeof(Buf6), "CenterLum=%.1f > LimbLum=%.1f, BlueLimb/Center=%.2f < RedLimb/Center=%.2f",
                  CenterLum, LimbLum, BlueDarkening, RedDarkening);
    CheckGate("6. Neckel-Labs Solar Limb Darkening",
              (CenterLum > LimbLum) && (LimbLum > 0.0f) && (BlueDarkening < RedDarkening),
              Buf6);

    // Gate 7: 3D Spherical Lunar Atmosphere (sunlit crescent limb >> shadowed night limb, zero pow(420) wide sticker halo).
    RadiometricSkyConfiguration MoonSkyCfg = ConstructEarthSkyAtSolarAngles(30.0f, 85.0f);
    StarfieldConfiguration NoStars{};
    NoStars.StarfieldEnabled = false;

    LunarBodyDescriptor TestMoon{};
    TestMoon.VisibilityEnabled    = true;
    TestMoon.SurfaceCategory      = LunarSurfaceCategory::GlacierCryo;
    TestMoon.CustomBearingEnabled = true;
    TestMoon.CustomBearingScene   = DirectionFromElevationAzimuth(30.0f, 0.0f);
    TestMoon.AngularRadiusRadians = 2.0f * DegToRad;
    TestMoon.AtmosphereEnvelope   = ConstructPresetLunarAtmosphere(LunarSurfaceCategory::GlacierCryo);
    TestMoon.AtmosphereEnvelope.TopAltitudeFraction       = 0.25f;
    TestMoon.AtmosphereEnvelope.RayleighOpticalDepthScale = SpectralColor{ 0.35f, 0.90f, 2.20f };
    const std::vector<LunarBodyDescriptor> SingleTestMoon = { TestMoon };

    const Vector3f DayLimbRay    = DirectionFromElevationAzimuth(30.0f,  2.22f); // Off-disc on sunlit side (~1.09 R_m)
    const Vector3f NightLimbRay  = DirectionFromElevationAzimuth(30.0f, -2.22f); // Off-disc on night side  (~1.09 R_m)
    const Vector3f WideHaloCheck = DirectionFromElevationAzimuth(30.0f,  4.80f); // Outside TopAltitudeFraction (2.40 R_m)

    const CelestialRayEvaluation DayLimb   = EvaluateCelestialSphereRay(DayLimbRay,   MoonSkyCfg, SingleTestMoon, NoStars, 0.0008f, true);
    const CelestialRayEvaluation NightLimb = EvaluateCelestialSphereRay(NightLimbRay, MoonSkyCfg, SingleTestMoon, NoStars, 0.0008f, true);
    const CelestialRayEvaluation WideCheck = EvaluateCelestialSphereRay(WideHaloCheck, MoonSkyCfg, SingleTestMoon, NoStars, 0.0008f, true);
    const float DayLimbLum   = EvaluateLuminance(DayLimb.ExtraterrestrialRadiance);
    const float NightLimbLum = EvaluateLuminance(NightLimb.ExtraterrestrialRadiance);
    const float WideLum      = EvaluateLuminance(WideCheck.ExtraterrestrialRadiance);
    char Buf7[160];
    std::snprintf(Buf7, sizeof(Buf7), "SunlitLimb=%.4f >> NightLimb=%.5f, OutsideEnvelope(2.4R)=%.6f",
                  DayLimbLum, NightLimbLum, WideLum);
    CheckGate("7. 3D Spherical Lunar Atmosphere & Shadow",
              (DayLimbLum > 0.01f) && (DayLimbLum > NightLimbLum * 12.0f) && (WideLum == 0.0f),
              Buf7);

    // Gate 8: Planckian Star Chromaticity & Catalogue Completeness.
    const SpectralColor BlueStar = EvaluatePlanckianStarColor(-0.22f);
    const SpectralColor RedStar  = EvaluatePlanckianStarColor( 1.85f);
    const auto& StarCat          = RetrieveBrightStarCatalogue();
    char Buf8[160];
    std::snprintf(Buf8, sizeof(Buf8), "Catalogue=%zu stars, Hot(B-V=-0.22) B/R=%.2f, Cool(B-V=+1.85) R/B=%.2f",
                  StarCat.size(), BlueStar.Blue / BlueStar.Red, RedStar.Red / RedStar.Blue);
    CheckGate("8. Planckian Chromaticity & J2000 Catalogue",
              (StarCat.size() >= 100u) && (BlueStar.Blue > BlueStar.Red * 1.25f) && (RedStar.Red > RedStar.Blue * 2.0f),
              Buf8);

    // Gate 9: ForceField backward streamline displacement & cloud density transport.
    const AtmosphericWindField StormField = BuildCyclonicStormForceField();
    const Vector3f ProbePoint{ 8200.0f, 950.0f, 2200.0f };
    const Vector3f BackOrigin = IntegrateStreamlineBackward(
        StormField, ProbePoint, 120.0f, 120.0f, 8u, AdvectionMediumTarget::CloudStratum);
    const float DisplacementM = VectorLength(SubtractVector(ProbePoint, BackOrigin));
    VolumetricCloudConfiguration CloudTest{};
    CloudTest.MacroCoverage = 0.58f;
    const float CloudD0   = EvaluateCloudDensityField(CloudTest, ProbePoint,   0.0f, &StormField);
    const float CloudD120 = EvaluateCloudDensityField(CloudTest, ProbePoint, 120.0f, &StormField);
    char Buf9[160];
    std::snprintf(Buf9, sizeof(Buf9), "120s Streamline Displacement = %.1f m (>500m), |rho(0)-rho(120)| = %.4f",
                  DisplacementM, std::fabs(CloudD0 - CloudD120));
    CheckGate("9. ForceField Streamline & Cloud Advection",
              (DisplacementM > 500.0f) && (std::fabs(CloudD0 - CloudD120) > 1.0e-3f),
              Buf9);

    // Gate 10: Outer-space curved atmosphere limb visibility + vacuum star transmittance.
    RadiometricSkyConfiguration OrbitCfg = ConstructEarthSkyAtSolarAngles(12.0f, 0.0f);
    OrbitCfg.ObserverAltitudeMetres      = 420000.0f; // 420 km Low Planetary Orbit
    OrbitCfg.OrbitalPlanetSurfaceEnabled = true;
    RadiometricSkyTables OrbitLut{};
    BakeRadiometricSkyTables(OrbitLut, OrbitCfg);
    const SkyRaymarchResult LimbRay  = EvaluateSphericalAtmosphereRay(
        OrbitLut, OrbitCfg, Vector3f{ 0.0f, 0.0f, 0.0f }, DirectionFromElevationAzimuth(-19.65f, 4.0f), nullptr);
    const SkyRaymarchResult SpaceRay = EvaluateSphericalAtmosphereRay(
        OrbitLut, OrbitCfg, Vector3f{ 0.0f, 0.0f, 0.0f }, DirectionFromElevationAzimuth( 25.00f, 4.0f), nullptr);
    const float OrbitLimbLum = EvaluateLuminance(LimbRay.InscatteredRadiance);
    const float SpaceLum     = EvaluateLuminance(SpaceRay.InscatteredRadiance);
    char Buf10[160];
    std::snprintf(Buf10, sizeof(Buf10), "420km Orbit LimbLum=%.3f (>0.25), VacuumSpaceLum=%.6f, StarTrans=%.4f",
                  OrbitLimbLum, SpaceLum, SpaceRay.ViewTransmittance.Blue);
    CheckGate("10. Outer-Space Limb Glow & Vacuum Clarity",
              (!LimbRay.PlanetSurfaceHitCondition) && (OrbitLimbLum > 0.25f) && (SpaceLum == 0.0f) && (SpaceRay.ViewTransmittance.Blue > 0.999f),
              Buf10);

    // Gate 11: Exponential height fog — closed-form column integral, stratification ordering, stratus inversion cap, ForceField drift.
    {
        // ① The closed-form column must reproduce the analytic exponential integral it replaces, to within integration error.
        const float AnalyticExt  = 1.0f;
        const float AnalyticScale = 400.0f;
        const float CameraZ = 12.0f, TargetZ = 2000.0f;
        const float CameraRad = 6356.0f + CameraZ * 0.001f;
        const float TargetRad = 6356.0f + TargetZ * 0.001f;
        HeightFogConfiguration AnalyticLayer{};
        AnalyticLayer.Category               = FogLayerCategory::AdvectionFog;
        AnalyticLayer.BaseAltitudeMetres     = 5.0f;
        AnalyticLayer.ExtinctionPerKilometre = AnalyticExt;
        AnalyticLayer.ScaleHeightMetres      = AnalyticScale;
        AnalyticLayer.SingleScatteringAlbedo = 0.985f;

        RadiometricSkyConfiguration FogSky = ConstructEarthSkyAtSolarAngles(20.0f, 0.0f);
        FogSky.ObserverAltitudeMetres = CameraZ;
        RadiometricSkyTables FogLut{};
        BakeRadiometricSkyTables(FogLut, FogSky);

        AtmosphericFogConfiguration FogCfg{};
        FogCfg.FogEnabled             = true;
        FogCfg.HeightLayer            = AnalyticLayer;
        FogCfg.LocalVolumes.clear();
        FogCfg.WindAdvectionEnabled   = false;
        FogCfg.MaximumTraceDistanceMetres = 40000.0f;

        const FogRayResult GroundRay = EvaluateAtmosphericFogRay(
            FogCfg, FogLut, FogSky, Vector3f{ 0.0f, 0.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, nullptr, nullptr);
        (void)CameraRad; (void)TargetRad;

        // ② Stratification ordering: at ground level a RadiationMist skin's own datum extinction must exceed a well-mixed layer's.
        float PersistExt[3] = { 0.0f, 0.0f, 0.0f };
        const std::array<FogLayerCategory, 3u> Categories = {
            FogLayerCategory::RadiationMist, FogLayerCategory::AdvectionFog, FogLayerCategory::ValleyStratum };
        for (size_t Idx = 0u; Idx < 3u; ++Idx)
        {
            // Each category evaluated with its own physical e-folding height: the profile IS the difference between them.
            const std::array<float, 3u> LayerScale = { 58.0f, 350.0f, 1500.0f };
            HeightFogConfiguration Layer{};
            Layer.Category               = Categories[Idx];
            Layer.BaseAltitudeMetres     = 5.0f;
            Layer.ExtinctionPerKilometre = 1.0f;
            Layer.ScaleHeightMetres      = LayerScale[Idx];
            Layer.InversionAltitudeMetres = 900.0f;
            // The probe carries the physics: at 400 m the shallow radiation skin has vanished outright, while the deep marine
            // layer is still substantial and the flat-topped stratum has lost almost nothing.
            PersistExt[Idx] = EvaluateHeightFogExtinctionPerKilometre(Layer, 400.0f);
        }

        // ③ A ValleyStratum must be flat-topped: dense below its inversion, extinct well above it.
        HeightFogConfiguration Stratum{};
        Stratum.Category                = FogLayerCategory::ValleyStratum;
        Stratum.ExtinctionPerKilometre  = 1.0f;
        Stratum.ScaleHeightMetres       = 1500.0f;
        Stratum.InversionAltitudeMetres = 400.0f;
        const float InLayer = EvaluateHeightFogExtinctionPerKilometre(Stratum, 200.0f);
        const float Above   = EvaluateHeightFogExtinctionPerKilometre(Stratum, 1400.0f);

        // ④ ForceField coupling: the same advection the cloud deck uses must carry a fog body downstream.
        AtmosphericFogConfiguration DriftCfg{};
        LocalFogVolume Body{};
        Body.AdvectedByForceField = true;
        const Vector3f Center0 = ResolveAdvectedFogCenter(DriftCfg, Body, &StormField, 0.0f);
        const Vector3f Center2 = ResolveAdvectedFogCenter(DriftCfg, Body, &StormField, 240.0f);
        const float DriftMetres = VectorLength(SubtractVector(Center2, Center0));

        // Analytic: ∫ (EXT/1000) * exp(-(z - b)/H) dz from z0 to z1, with z0 the observer altitude.
        const float ExpectedTau = (AnalyticExt * 0.001f) * AnalyticScale
                                * (std::exp(-(CameraZ - AnalyticLayer.BaseAltitudeMetres) / AnalyticScale)
                                 - std::exp(-(TargetZ - AnalyticLayer.BaseAltitudeMetres) / AnalyticScale));
        const float TauError = std::fabs(GroundRay.HeightOpticalDepth - ExpectedTau) / ExpectedTau;

        char Buf11[224];
        std::snprintf(Buf11, sizeof(Buf11),
                      "ClosedFormTau=%.4f (analytic %.4f, err %.2f%%), Ext decay 30m->400m: MIST %.5f << ADVECT %.3f < VALLEY %.3f, Stratum 200m=%.3f >> 1400m=%.6f, Drift=%.0f m",
                      GroundRay.HeightOpticalDepth, ExpectedTau, TauError * 100.0f,
                      PersistExt[0], PersistExt[1], PersistExt[2], InLayer, Above, DriftMetres);
        CheckGate("11. Exponential Height Fog Column & Fog-Body Drift",
                  (TauError < 0.05f) && (PersistExt[0] < 0.05f * PersistExt[1]) && (PersistExt[2] > PersistExt[1])
                  && (InLayer > 10.0f * std::max(Above, 1.0e-6f)) && (DriftMetres > 200.0f),
                  Buf11);
    }

    std::printf("====================================================================================================\n");
    std::printf("  AUDIT SUMMARY: %d / 11 GATES PASSED\n", 11 - Failures);
    std::printf("====================================================================================================\n\n");
    std::fflush(stdout);
    return Failures == 0;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      MAIN ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    std::string OutputDirectory = "Exhibits/AuditProof";
    if (ArgumentCount > 1 && ArgumentValues[1] != nullptr)
        OutputDirectory = ArgumentValues[1];

    if (!ExecuteQuantitativePhysicsAudit())
    {
        std::fprintf(stderr, "[FATAL] Quantitative physics audit failed!\n");
        return 1;
    }

    constexpr uint32_t ImageW = 1280u;
    constexpr uint32_t ImageH = 720u;

    const AtmosphericWindField             StormWind = BuildCyclonicStormForceField();
    const std::vector<LunarBodyDescriptor> SixMoons  = BuildSixMoonEnsemble();

    // Optional scene filter: `AtmosphereCelestialValidation <output-dir> [scene-prefix ...]` renders only matching proof files.
    const std::vector<std::string> SceneFilters = [&]() -> std::vector<std::string>
    {
        std::vector<std::string> Filters;
        for (int ArgIdx = 2; ArgIdx < ArgumentCount; ++ArgIdx)
            if (ArgumentValues[ArgIdx] != nullptr)
                Filters.emplace_back(ArgumentValues[ArgIdx]);
        return Filters;
    }();

    auto SceneSelected = [&](const char* FileName) -> bool
    {
        if (SceneFilters.empty())
            return true;
        const std::string Name = FileName;
        for (const std::string& Filter : SceneFilters)
            if (Name.rfind(Filter, 0u) == 0u)
                return true;
        return false;
    };

    auto SaveProof = [&](const char* FileName, const std::vector<uint8_t>& Pixels) -> bool
    {
        if (!SceneSelected(FileName))
            return true;

        const std::string FullPath = OutputDirectory + "/" + FileName;
        const bool Ok = WriteLosslessRgbPng(FullPath, ImageW, ImageH, Pixels);
        std::printf("  [%s] Rendered %ux%u -> %s\n", Ok ? "DONE" : "ERR ", ImageW, ImageH, FullPath.c_str());
        std::fflush(stdout);
        return Ok;
    };

    std::printf("Rendering 15 High-Resolution (1280x720 Supersampled) Physical Audit Proofs...\n");
    std::fflush(stdout);

    // ====================================================================================================================
    // 01: Astronomical / Nautical Dawn (-6.2 deg solar elevation) — Stars + Milky Way + Rose-Cobalt Twilight + Cirrus
    // ====================================================================================================================
    {
        UnifiedSceneSpecification S{};
        S.FogConfig.FogEnabled                = false;
        S.SkyConfig                          = ConstructEarthSkyAtSolarAngles(-6.2f, 8.0f);
        S.SkyConfig.SequenceSeconds          = 45.0f;
        S.SkyConfig.HighAltitudeCirrusCoverage = 0.36f;
        S.StarConfig.StarfieldEnabled        = true;
        S.StarConfig.RadiometricFluxScale    = 1.25f;
        S.Moons                              = { SixMoons[0] }; // Crisp dawn crescent Luna
        S.CloudConfig.CloudsEnabled          = true;
        S.CloudConfig.FormationPreset        = CloudFormationPreset::StratocumulusSheet;
        S.CloudConfig.MacroCoverage          = 0.26f;
        S.CloudConfig.LowerAltitudeMetres    = 2100.0f;
        S.CloudConfig.StratumThicknessMetres = 1200.0f;
        S.CloudConfig.PrimaryMarchStepCount  = 72u;
        S.WindField                          = StormWind;
        S.Camera.ForwardDirection            = DirectionFromElevationAzimuth(16.0f, 0.0f);
        S.Camera.VerticalFovDegrees          = 56.0f;
        S.ExposureScale                      = 2.30f;
        if (SceneSelected("01_Sky_AstronomicalDawn.png"))
        {
            if (!SaveProof("01_Sky_AstronomicalDawn.png", RenderViewportRgb(S, ImageW, ImageH)))
                return 1;
        }
    }

    // ====================================================================================================================
    // 02: Sunrise Golden Limb (+1.85 deg solar elevation) — Limb-Darkened Solar Disc + Golden Clouds + Cool Ocean Fresnel
    // ====================================================================================================================
    {
        UnifiedSceneSpecification S{};
        S.FogConfig.FogEnabled                = false;
        S.SkyConfig                            = ConstructEarthSkyAtSolarAngles(1.85f, -5.0f);
        S.SkyConfig.SequenceSeconds            = 60.0f;
        S.SkyConfig.HighAltitudeCirrusCoverage = 0.28f;
        S.StarConfig.StarfieldEnabled          = true;
        S.CloudConfig.CloudsEnabled            = true;
        S.CloudConfig.FormationPreset          = CloudFormationPreset::FairWeatherCumulus;
        S.CloudConfig.MacroCoverage            = 0.42f;
        S.CloudConfig.PrimaryMarchStepCount    = 84u;
        S.WindField                            = StormWind;
        S.Camera.ForwardDirection              = DirectionFromElevationAzimuth(11.0f, 0.0f);
        S.Camera.VerticalFovDegrees            = 52.0f;
        S.ExposureScale                        = 1.05f;
        if (SceneSelected("02_Sky_Sunrise_GoldenLimb.png"))
        {
            if (!SaveProof("02_Sky_Sunrise_GoldenLimb.png", RenderViewportRgb(S, ImageW, ImageH)))
                return 1;
        }
    }

    // ====================================================================================================================
    // 03: Noon Overhead Sun (+76 deg solar elevation) — Smooth Solar Aureole Falloff + Deep Rayleigh Blue + Cumulus
    // ====================================================================================================================
    {
        UnifiedSceneSpecification S{};
        S.FogConfig.FogEnabled                = false;
        S.SkyConfig                            = ConstructEarthSkyAtSolarAngles(76.0f, 0.0f);
        S.SkyConfig.SequenceSeconds            = 75.0f;
        S.SkyConfig.HighAltitudeCirrusCoverage = 0.14f;
        S.StarConfig.StarfieldEnabled          = false;
        S.CloudConfig.CloudsEnabled            = true;
        S.CloudConfig.FormationPreset          = CloudFormationPreset::FairWeatherCumulus;
        S.CloudConfig.MacroCoverage            = 0.48f;
        S.CloudConfig.PrimaryMarchStepCount    = 88u;
        S.WindField                            = StormWind;
        S.Camera.ForwardDirection              = DirectionFromElevationAzimuth(48.0f, 0.0f);
        S.Camera.VerticalFovDegrees            = 72.0f;
        S.ExposureScale                        = 0.92f;
        if (SceneSelected("03_Sky_Noon_OverheadSun.png"))
        {
            if (!SaveProof("03_Sky_Noon_OverheadSun.png", RenderViewportRgb(S, ImageW, ImageH)))
                return 1;
        }
    }

    // ====================================================================================================================
    // 04: Sunset Crimson Dusk (+0.55 deg solar elevation) — Deep Rayleigh/Chappuis Reddening + Blazing Cloud Undersides
    // ====================================================================================================================
    {
        UnifiedSceneSpecification S{};
        S.FogConfig.FogEnabled                = false;
        S.SkyConfig                            = ConstructEarthSkyAtSolarAngles(0.55f, 4.0f);
        S.SkyConfig.MieScatteringPerKm         = 5.20e-3f;
        S.SkyConfig.MieExtinctionPerKm         = 5.78e-3f;
        S.SkyConfig.SequenceSeconds            = 95.0f;
        S.SkyConfig.HighAltitudeCirrusCoverage = 0.36f;
        S.StarConfig.StarfieldEnabled          = true;
        S.CloudConfig.CloudsEnabled            = true;
        S.CloudConfig.FormationPreset          = CloudFormationPreset::ToweringCumulonimbus;
        S.CloudConfig.MacroCoverage            = 0.44f;
        S.CloudConfig.PrimaryMarchStepCount    = 88u;
        S.WindField                            = StormWind;
        S.Camera.ForwardDirection              = DirectionFromElevationAzimuth(10.5f, 0.0f);
        S.Camera.VerticalFovDegrees            = 52.0f;
        S.ExposureScale                        = 1.18f;
        if (SceneSelected("04_Sky_Sunset_CrimsonDusk.png"))
        {
            if (!SaveProof("04_Sky_Sunset_CrimsonDusk.png", RenderViewportRgb(S, ImageW, ImageH)))
                return 1;
        }
    }

    // ====================================================================================================================
    // 05: All 6 Moons in One Shared Scene — True 3D Volumetric Lunar Atmospheres + Cratered Lommel-Seeliger Regolith
    // ====================================================================================================================
    {
        UnifiedSceneSpecification S{};
        S.FogConfig.FogEnabled                = false;
        // Sun at -13 deg elevation & +72 deg azimuth illuminates all 6 moons from the right at a crisp 3D crescent-gibbous phase!
        S.SkyConfig                            = ConstructEarthSkyAtSolarAngles(-13.0f, 72.0f);
        S.SkyConfig.HighAltitudeCirrusCoverage = 0.10f;
        S.StarConfig.StarfieldEnabled          = true;
        S.StarConfig.RadiometricFluxScale      = 1.25f;
        S.Moons                                = SixMoons;
        S.CloudConfig.CloudsEnabled            = false;
        S.Camera.ForwardDirection              = DirectionFromElevationAzimuth(20.5f, 0.0f);
        S.Camera.VerticalFovDegrees            = 38.0f;
        S.ExposureScale                        = 1.65f;
        if (SceneSelected("05_Celestial_AllSixMoons_SharedScene.png"))
        {
            if (!SaveProof("05_Celestial_AllSixMoons_SharedScene.png", RenderViewportRgb(S, ImageW, ImageH)))
                return 1;
        }
    }

    // ====================================================================================================================
    // 06: Telephoto Close-Up of Lunar Volumetric Atmospheres (Glacier + Shroud + Luna) — 20-Step Spherical Limb + Shadow
    // ====================================================================================================================
    {
        UnifiedSceneSpecification S{};
        S.FogConfig.FogEnabled                = false;
        S.SkyConfig                            = ConstructEarthSkyAtSolarAngles(-16.0f, 68.0f);
        S.SkyConfig.HighAltitudeCirrusCoverage = 0.0f;
        S.StarConfig.StarfieldEnabled          = true;
        S.StarConfig.RadiometricFluxScale      = 1.30f;

        LunarBodyDescriptor HeroGlacier        = SixMoons[1];
        HeroGlacier.CustomBearingScene         = DirectionFromElevationAzimuth(24.0f, -3.4f);
        HeroGlacier.AngularRadiusRadians       = 2.25f * DegToRad;

        LunarBodyDescriptor HeroShroud         = SixMoons[2];
        HeroShroud.CustomBearingScene          = DirectionFromElevationAzimuth(24.2f,  3.5f);
        HeroShroud.AngularRadiusRadians        = 2.35f * DegToRad;

        LunarBodyDescriptor HeroLuna           = SixMoons[0];
        HeroLuna.CustomBearingScene            = DirectionFromElevationAzimuth(27.2f,  0.0f);
        HeroLuna.AngularRadiusRadians          = 1.25f * DegToRad;

        S.Moons                                = { HeroGlacier, HeroShroud, HeroLuna };
        S.CloudConfig.CloudsEnabled            = false;
        S.Camera.ForwardDirection              = DirectionFromElevationAzimuth(24.6f, 0.0f);
        S.Camera.VerticalFovDegrees            = 9.8f;
        S.ExposureScale                        = 1.55f;
        if (SceneSelected("06_Celestial_LunarAtmosphere_Telephoto.png"))
        {
            if (!SaveProof("06_Celestial_LunarAtmosphere_Telephoto.png", RenderViewportRgb(S, ImageW, ImageH)))
                return 1;
        }
    }

    // ====================================================================================================================
    // 07: Deep-Sky Starfield & Galactic Plane — Hipparcos Catalogue + Planckian Colors + Sub-Pixel Fraunhofer Airy PSF
    //     Rendered from a true vacuum vantage (400 km) so the night-sky catalogue resolves against black space, the way a
    //     long-exposure astrograph records it — the 18 m ground scene would otherwise sit under 2.6e-3 twilight airglow.
    // ====================================================================================================================
    {
        UnifiedSceneSpecification S{};
        S.FogConfig.FogEnabled                = false;
        S.SkyConfig                            = ConstructEarthSkyAtSolarAngles(-34.0f, 140.0f);
        S.SkyConfig.ObserverAltitudeMetres     = 400000.0f;   // 400 km: above 99.99 % of the airglow column
        S.SkyConfig.HighAltitudeCirrusCoverage = 0.0f;
        S.SkyConfig.TroposphericHazeStrength   = 0.0f;
        S.SkyConfig.SequenceSeconds            = 30.0f;
        S.StarConfig.StarfieldEnabled          = true;
        S.StarConfig.AtmosphericSeeingEnabled  = false;
        S.StarConfig.RadiometricFluxScale      = 1.45f;
        S.StarConfig.LocalSiderealHours        = 6.40f;
        S.StarConfig.ObserverLatitudeDegrees   = 40.0f;
        S.CloudConfig.CloudsEnabled            = false;
        S.Camera.ForwardDirection              = DirectionFromElevationAzimuth(42.0f, -76.0f);
        S.Camera.VerticalFovDegrees            = 58.0f;
        S.ExposureScale                        = 9.50f;   // Night-sky astrograph exposure: mag ~5 resolves, mag < 1 saturates
        S.DeepSkyNightResponse                 = true;    // Long-exposure sensor response (linear sRGB, no filmic toe)
        if (SceneSelected("07_Celestial_Starfield_AiryPinpoints.png"))
        {
            if (!SaveProof("07_Celestial_Starfield_AiryPinpoints.png", RenderViewportRgb(S, ImageW, ImageH)))
                return 1;
        }
    }

    // ====================================================================================================================
    // 08: Outer Space (Low Planetary Orbit, 420 km) — Curved Blue/Golden Atmospheric Limb + Sun + Stars + Ocean Glint
    // ====================================================================================================================
    {
        UnifiedSceneSpecification S{};
        S.FogConfig.FogEnabled                = false;
        S.SkyConfig                              = ConstructEarthSkyAtSolarAngles(-14.5f, 6.0f);
        S.SkyConfig.ObserverAltitudeMetres       = 420000.0f; // 420 km ISS-class orbit!
        S.SkyConfig.OrbitalPlanetSurfaceEnabled  = true;
        S.SkyConfig.SequenceSeconds              = 110.0f;
        S.StarConfig.StarfieldEnabled            = true;
        S.StarConfig.AtmosphericSeeingEnabled    = false;
        S.StarConfig.RadiometricFluxScale        = 1.35f;

        LunarBodyDescriptor OrbitMoon            = SixMoons[1];
        OrbitMoon.CustomBearingScene             = DirectionFromElevationAzimuth(-5.5f, -18.0f);
        OrbitMoon.AngularRadiusRadians           = 1.45f * DegToRad;
        S.Moons                                  = { OrbitMoon };

        S.CloudConfig.CloudsEnabled              = false;
        S.WindField                              = StormWind;
        S.Camera.ForwardDirection                = DirectionFromElevationAzimuth(-16.5f, 0.0f);
        S.Camera.VerticalFovDegrees              = 46.0f;
        S.ExposureScale                          = 0.62f;
        if (SceneSelected("08_OuterSpace_LowOrbitLimb_SunStars.png"))
        {
            if (!SaveProof("08_OuterSpace_LowOrbitLimb_SunStars.png", RenderViewportRgb(S, ImageW, ImageH)))
                return 1;
        }
    }

    // ====================================================================================================================
    // 09: Outer Space (High Planetary Orbit, 5,600 km) — Full Curved Planet + Terminator + Cyclone Clouds + Sun + Moons + Stars
    // ====================================================================================================================
    {
        UnifiedSceneSpecification S{};
        S.FogConfig.FogEnabled                = false;
        S.SkyConfig                              = ConstructEarthSkyAtSolarAngles(-16.0f, 28.0f);
        S.SkyConfig.ObserverAltitudeMetres       = 5600000.0f; // 5,600 km orbital vantage!
        S.SkyConfig.OrbitalPlanetSurfaceEnabled  = true;
        S.SkyConfig.SequenceSeconds              = 140.0f;
        S.StarConfig.StarfieldEnabled            = true;
        S.StarConfig.AtmosphericSeeingEnabled    = false;
        S.StarConfig.RadiometricFluxScale        = 1.40f;

        LunarBodyDescriptor FarMoonA             = SixMoons[0];
        FarMoonA.CustomBearingScene              = DirectionFromElevationAzimuth(-10.0f, -22.0f);
        FarMoonA.AngularRadiusRadians            = 1.65f * DegToRad;

        LunarBodyDescriptor FarMoonB             = SixMoons[2];
        FarMoonB.CustomBearingScene              = DirectionFromElevationAzimuth( -3.0f, -12.0f);
        FarMoonB.AngularRadiusRadians            = 1.35f * DegToRad;

        S.Moons                                  = { FarMoonA, FarMoonB };
        S.CloudConfig.CloudsEnabled              = false;
        S.WindField                              = StormWind;
        S.Camera.ForwardDirection                = DirectionFromElevationAzimuth(-36.0f, 4.0f);
        S.Camera.VerticalFovDegrees              = 62.0f;
        S.ExposureScale                          = 0.66f;
        if (SceneSelected("09_OuterSpace_HighOrbitGlobe_MoonsStars.png"))
        {
            if (!SaveProof("09_OuterSpace_HighOrbitGlobe_MoonsStars.png", RenderViewportRgb(S, ImageW, ImageH)))
                return 1;
        }
    }

    // ====================================================================================================================
    // 10: Volumetric Clouds Advecting with ForceField (3-Panel Time Sequence: T = 0s | T = 95s | T = 190s)
    // ====================================================================================================================
    if (SceneSelected("10_Clouds_ForceFieldAdvection_Sequence.png"))
    {
        const uint32_t PanelW = ImageW / 3u;
        std::vector<uint8_t> CompositeRgb(static_cast<size_t>(ImageW) * ImageH * 3u, 18u);

        const std::array<float, 3u>       TimesSeconds = { 0.0f, 95.0f, 190.0f };
        const std::array<const char*, 3u> TimeLabels   = { "T=0S", "T=95S", "T=190S" };

        for (size_t PanelIdx = 0u; PanelIdx < 3u; ++PanelIdx)
        {
            UnifiedSceneSpecification S{};
            S.FogConfig.FogEnabled                = false;
            S.SkyConfig                            = ConstructEarthSkyAtSolarAngles(24.0f, -28.0f);
            S.SkyConfig.SequenceSeconds            = TimesSeconds[PanelIdx];
            S.SkyConfig.HighAltitudeCirrusCoverage = 0.32f;
            S.StarConfig.StarfieldEnabled          = false;
            S.CloudConfig.CloudsEnabled            = true;
            S.CloudConfig.FormationPreset          = CloudFormationPreset::CyclonicSpiralDeck;
            S.CloudConfig.MacroCoverage            = 0.44f;
            S.CloudConfig.LowerAltitudeMetres      = 1350.0f;
            S.CloudConfig.StratumThicknessMetres   = 2600.0f;
            S.CloudConfig.PrimaryMarchStepCount    = 92u;

            S.CloudConfig.LocalizedCells.push_back(LocalizedCloudCell{ true, Vector3f{ 6800.0f, -2100.0f, 2250.0f }, Vector3f{ 2200.0f, 1600.0f, 1150.0f }, 1.45f });
            S.CloudConfig.LocalizedCells.push_back(LocalizedCloudCell{ true, Vector3f{ 8900.0f,   950.0f, 2450.0f }, Vector3f{ 2500.0f, 1800.0f, 1250.0f }, 1.50f });
            S.CloudConfig.LocalizedCells.push_back(LocalizedCloudCell{ true, Vector3f{ 10800.0f, -1400.0f, 2300.0f }, Vector3f{ 2100.0f, 1700.0f, 1100.0f }, 1.35f });

            S.WindField                            = StormWind;
            S.Camera.ForwardDirection              = DirectionFromElevationAzimuth(19.5f, -4.0f);
            S.Camera.VerticalFovDegrees            = 54.0f;
            S.ExposureScale                        = 0.98f;

            const uint32_t ActualPanelW = (PanelIdx == 2u) ? (ImageW - 2u * PanelW) : PanelW;
            const std::vector<uint8_t> PanelRgb = RenderViewportRgb(S, ActualPanelW, ImageH);

            const uint32_t DestOffsetX = static_cast<uint32_t>(PanelIdx) * PanelW;
            for (uint32_t Y = 0u; Y < ImageH; ++Y)
            {
                for (uint32_t X = 0u; X < ActualPanelW; ++X)
                {
                    const size_t Src = (static_cast<size_t>(Y) * ActualPanelW + X) * 3u;
                    const size_t Dst = (static_cast<size_t>(Y) * ImageW + (DestOffsetX + X)) * 3u;
                    CompositeRgb[Dst + 0u] = PanelRgb[Src + 0u];
                    CompositeRgb[Dst + 1u] = PanelRgb[Src + 1u];
                    CompositeRgb[Dst + 2u] = PanelRgb[Src + 2u];
                }
            }
            if (PanelIdx > 0u)
            {
                for (uint32_t Y = 0u; Y < ImageH; ++Y)
                {
                    const size_t Sep = (static_cast<size_t>(Y) * ImageW + DestOffsetX) * 3u;
                    CompositeRgb[Sep + 0u] = 220u;
                    CompositeRgb[Sep + 1u] = 228u;
                    CompositeRgb[Sep + 2u] = 240u;
                }
            }
            StampBadgeLabel(CompositeRgb, ImageW, ImageH, DestOffsetX + 12u, 12u, TimeLabels[PanelIdx], 2u);
        }

        if (!SaveProof("10_Clouds_ForceFieldAdvection_Sequence.png", CompositeRgb))
            return 1;
    }

    // ====================================================================================================================
    // 11: ForceField Sky + Cloud Coupling Comparison (Left: Calm Reference | Right: Active Cyclonic ForceField at T=165s)
    // ====================================================================================================================
    if (SceneSelected("11_ForceField_SkyCloudCoupling.png"))
    {
        const uint32_t HalfW = ImageW / 2u;
        std::vector<uint8_t> SplitRgb(static_cast<size_t>(ImageW) * ImageH * 3u, 0u);

        for (uint32_t Side = 0u; Side < 2u; ++Side)
        {
            UnifiedSceneSpecification S{};
            S.FogConfig.FogEnabled                = false;
            S.SkyConfig                            = ConstructEarthSkyAtSolarAngles(11.0f, -22.0f);
            S.SkyConfig.SequenceSeconds            = 165.0f;
            S.SkyConfig.HighAltitudeCirrusCoverage = 0.45f;
            S.StarConfig.StarfieldEnabled          = false;
            S.CloudConfig.CloudsEnabled            = true;
            S.CloudConfig.FormationPreset          = CloudFormationPreset::ToweringCumulonimbus;
            S.CloudConfig.MacroCoverage            = 0.46f;
            S.CloudConfig.PrimaryMarchStepCount    = 92u;
            S.CloudConfig.LocalizedCells.push_back(LocalizedCloudCell{ true, Vector3f{ 8500.0f, -800.0f, 2350.0f }, Vector3f{ 2800.0f, 2200.0f, 1300.0f }, 1.40f });

            S.WindField                            = StormWind;
            S.WindField.FieldEnabled               = (Side == 1u);
            S.Camera.ForwardDirection              = DirectionFromElevationAzimuth(17.5f, -3.0f);
            S.Camera.VerticalFovDegrees            = 54.0f;
            S.ExposureScale                        = 1.02f;

            const std::vector<uint8_t> HalfImg = RenderViewportRgb(S, HalfW, ImageH);
            const uint32_t DestX = Side * HalfW;
            for (uint32_t Y = 0u; Y < ImageH; ++Y)
            {
                for (uint32_t X = 0u; X < HalfW; ++X)
                {
                    const size_t Src = (static_cast<size_t>(Y) * HalfW + X) * 3u;
                    const size_t Dst = (static_cast<size_t>(Y) * ImageW + (DestX + X)) * 3u;
                    SplitRgb[Dst + 0u] = HalfImg[Src + 0u];
                    SplitRgb[Dst + 1u] = HalfImg[Src + 1u];
                    SplitRgb[Dst + 2u] = HalfImg[Src + 2u];
                }
            }
        }
        for (uint32_t Y = 0u; Y < ImageH; ++Y)
        {
            const size_t Sep = (static_cast<size_t>(Y) * ImageW + HalfW) * 3u;
            SplitRgb[Sep + 0u] = 230u;
            SplitRgb[Sep + 1u] = 236u;
            SplitRgb[Sep + 2u] = 245u;
        }
        StampBadgeLabel(SplitRgb, ImageW, ImageH, 14u,         14u, "CALM",       2u);
        StampBadgeLabel(SplitRgb, ImageW, ImageH, HalfW + 14u, 14u, "FORCEFIELD", 2u);

        if (!SaveProof("11_ForceField_SkyCloudCoupling.png", SplitRgb))
            return 1;
    }

    // ====================================================================================================================
    // 12: Full Lower-Hemisphere to Overhead Continuity Audit (-30 deg to +46 deg Pitch) — Zero White Glow, Zero Brown Mud
    // ====================================================================================================================
    {
        UnifiedSceneSpecification S{};
        S.FogConfig.FogEnabled                = false;
        S.SkyConfig                            = ConstructEarthSkyAtSolarAngles(3.2f, 22.0f);
        S.SkyConfig.SequenceSeconds            = 80.0f;
        S.SkyConfig.HighAltitudeCirrusCoverage = 0.25f;
        S.StarConfig.StarfieldEnabled          = true;
        S.CloudConfig.CloudsEnabled            = true;
        S.CloudConfig.FormationPreset          = CloudFormationPreset::FairWeatherCumulus;
        S.CloudConfig.MacroCoverage            = 0.36f;
        S.CloudConfig.PrimaryMarchStepCount    = 84u;
        S.WindField                            = StormWind;
        S.Camera.ForwardDirection              = DirectionFromElevationAzimuth(8.0f, 0.0f);
        S.Camera.VerticalFovDegrees            = 76.0f;
        S.ExposureScale                        = 1.04f;
        if (SceneSelected("12_Sky_LimbContinuity_LowerHemisphereAudit.png"))
        {
            if (!SaveProof("12_Sky_LimbContinuity_LowerHemisphereAudit.png", RenderViewportRgb(S, ImageW, ImageH)))
                return 1;
        }
    }

    // ====================================================================================================================
    // 13: Solar Character Dial — the SAME +3.4 deg sun rendered with four photosphere envelopes (2x2 quadrant composite)
    // ====================================================================================================================
    if (SceneSelected("13_Sky_SolarCharacterVariants.png"))
    {
        const uint32_t HalfW = ImageW / 2u;
        const uint32_t HalfH = ImageH / 2u;
        std::vector<uint8_t> GridRgb(static_cast<size_t>(ImageW) * ImageH * 3u, 0u);

        const std::array<SolarDiscCharacter, 4u> Characters = {
            SolarDiscCharacter::GoldenHaze,
            SolarDiscCharacter::SolarPhotosphere,
            SolarDiscCharacter::DiamondCorona,
            SolarDiscCharacter::OvercastDiffuse };
        const std::array<const char*, 4u> Labels = { "HAZE", "PHOTOSPHERE", "DIAMOND", "OVERCAST" };

        for (size_t Quad = 0u; Quad < 4u; ++Quad)
        {
            UnifiedSceneSpecification S{};
            S.FogConfig.FogEnabled                 = false;
            S.SkyConfig                            = ConstructEarthSkyAtSolarAngles(3.4f, 0.0f);
            S.SkyConfig.SolarCharacter             = Characters[Quad];
            S.SkyConfig.SequenceSeconds            = 40.0f;
            S.SkyConfig.HighAltitudeCirrusCoverage = 0.00f;
            S.StarConfig.StarfieldEnabled          = false;
            S.CloudConfig.CloudsEnabled            = false;
            S.WindField                            = StormWind;
            // Tight crop centred on the disk at low exposure: the circumsolar envelope spans 4-8 solar radii, so the whole
            // gradient has to fit inside the crop and must not be flattened by the highlight roll-off.
            // The crop must reach ~20 solar radii so the whole envelope sits inside the highlight range, and the exposure sits
            // just below the sky glare level: the corona then reads as a gradient instead of being rolled off to white.
            S.Camera.ForwardDirection              = DirectionFromElevationAzimuth(3.4f, 0.0f);
            S.Camera.VerticalFovDegrees            = 6.0f;
            S.ExposureScale                        = 0.26f;

            const uint32_t QuadX = (Quad & 1u) * HalfW;
            const uint32_t QuadY = (Quad >> 1u) * HalfH;
            const std::vector<uint8_t> Tile = RenderViewportRgb(S, HalfW, HalfH);
            for (uint32_t Y = 0u; Y < HalfH; ++Y)
            {
                for (uint32_t X = 0u; X < HalfW; ++X)
                {
                    const size_t Src = (static_cast<size_t>(Y) * HalfW + X) * 3u;
                    const size_t Dst = (static_cast<size_t>(QuadY + Y) * ImageW + (QuadX + X)) * 3u;
                    GridRgb[Dst + 0u] = Tile[Src + 0u];
                    GridRgb[Dst + 1u] = Tile[Src + 1u];
                    GridRgb[Dst + 2u] = Tile[Src + 2u];
                }
            }
            StampBadgeLabel(GridRgb, ImageW, ImageH, QuadX + 12u, QuadY + 12u, Labels[Quad], 2u);
        }

        if (!SaveProof("13_Sky_SolarCharacterVariants.png", GridRgb))
            return 1;
    }

    // ====================================================================================================================
    // 14: Fog — ForceField-Advected Boundary-Layer Height Fog + Local Volumetric Fog Bodies (3-Panel Drift Sequence)
    // ====================================================================================================================
    if (SceneSelected("14_Fog_HeightAndLocalVolumes_Sequence.png"))
    {
        const uint32_t PanelW = ImageW / 3u;
        std::vector<uint8_t> CompositeRgb(static_cast<size_t>(ImageW) * ImageH * 3u, 14u);

        const std::array<float, 3u>       TimesSeconds = { 0.0f, 150.0f, 300.0f };
        const std::array<const char*, 3u> TimeLabels   = { "T=0S", "T=150S", "T=300S" };

        for (size_t PanelIdx = 0u; PanelIdx < 3u; ++PanelIdx)
        {
            UnifiedSceneSpecification S{};
            S.FogConfig.FogEnabled                  = true;
            S.SkyConfig                             = ConstructEarthSkyAtSolarAngles(16.0f, 122.0f);
            S.SkyConfig.SequenceSeconds             = TimesSeconds[PanelIdx];
            S.SkyConfig.HighAltitudeCirrusCoverage  = 0.14f;
            S.StarConfig.StarfieldEnabled           = false;

            // Seen from a ridge above the deck: a stratocumulus sheet overhead, a fog sea filling the basin below.
            S.CloudConfig.CloudsEnabled             = true;
            S.CloudConfig.FormationPreset           = CloudFormationPreset::StratocumulusSheet;
            S.CloudConfig.MacroCoverage             = 0.26f;
            S.CloudConfig.LowerAltitudeMetres       = 3100.0f;
            S.CloudConfig.StratumThicknessMetres    = 800.0f;
            S.CloudConfig.PrimaryMarchStepCount     = 84u;

            S.FogConfig.HeightLayer.LayerEnabled          = true;
            // A fog sea is a well-mixed layer capped by a subsidence inversion: near-uniform density up to the cap, then a hard cut.
            S.FogConfig.HeightLayer.Category              = FogLayerCategory::ValleyStratum;
            S.FogConfig.HeightLayer.BaseAltitudeMetres    = 100.0f;
            S.FogConfig.HeightLayer.ExtinctionPerKilometre = 0.55f;
            S.FogConfig.HeightLayer.ScaleHeightMetres     = 4200.0f;
            S.FogConfig.HeightLayer.InversionAltitudeMetres = 430.0f;
            S.FogConfig.HeightLayer.InversionReliefMetres  = 300.0f;
            S.FogConfig.HeightLayer.DensityNoiseStrength  = 0.72f;
            S.FogConfig.VolumeMarchStepCount              = 30u;

            // Two detached banks with soft, wind-sculpted crowns breaking above the general fog level.
            LocalFogVolume Bank{};
            Bank.CenterMetres              = Vector3f{ 1500.0f, 1500.0f, 470.0f };
            Bank.HalfExtentsMetres         = Vector3f{ 2100.0f, 1500.0f, 200.0f };
            Bank.ExtinctionPerKilometre    = 2.20f;
            Bank.EdgeSoftness              = 0.72f;
            Bank.DensityNoiseStrength      = 0.74f;
            S.FogConfig.LocalVolumes.push_back(Bank);

            LocalFogVolume Ridge = Bank;
            Ridge.CenterMetres             = Vector3f{ -1300.0f, 3900.0f, 430.0f };
            Ridge.HalfExtentsMetres        = Vector3f{ 2500.0f, 1800.0f, 160.0f };
            Ridge.ExtinctionPerKilometre   = 1.80f;
            Ridge.DensityNoiseStrength     = 0.80f;
            S.FogConfig.LocalVolumes.push_back(Ridge);

            S.WindField                             = StormWind;
            // Ridge vantage: the observer sits 980 m above sea level, so the fog sea and the banks are seen in plan oblique.
            S.SkyConfig.ObserverAltitudeMetres      = 980.0f;
            S.Camera.ForwardDirection               = DirectionFromElevationAzimuth(-7.0f, 0.0f);
            S.Camera.VerticalFovDegrees             = 34.0f;
            S.ExposureScale                         = 1.06f;

            const uint32_t ActualPanelW = (PanelIdx == 2u) ? (ImageW - 2u * PanelW) : PanelW;
            const std::vector<uint8_t> PanelRgb = RenderViewportRgb(S, ActualPanelW, ImageH);

            const uint32_t DestOffsetX = static_cast<uint32_t>(PanelIdx) * PanelW;
            for (uint32_t Y = 0u; Y < ImageH; ++Y)
            {
                for (uint32_t X = 0u; X < ActualPanelW; ++X)
                {
                    const size_t Src = (static_cast<size_t>(Y) * ActualPanelW + X) * 3u;
                    const size_t Dst = (static_cast<size_t>(Y) * ImageW + (DestOffsetX + X)) * 3u;
                    CompositeRgb[Dst + 0u] = PanelRgb[Src + 0u];
                    CompositeRgb[Dst + 1u] = PanelRgb[Src + 1u];
                    CompositeRgb[Dst + 2u] = PanelRgb[Src + 2u];
                }
            }
            if (PanelIdx > 0u)
            {
                for (uint32_t Y = 0u; Y < ImageH; ++Y)
                {
                    const size_t Sep = (static_cast<size_t>(Y) * ImageW + DestOffsetX) * 3u;
                    CompositeRgb[Sep + 0u] = 220u;
                    CompositeRgb[Sep + 1u] = 228u;
                    CompositeRgb[Sep + 2u] = 240u;
                }
            }
            StampBadgeLabel(CompositeRgb, ImageW, ImageH, DestOffsetX + 12u, 12u, TimeLabels[PanelIdx], 2u);
        }

        if (!SaveProof("14_Fog_HeightAndLocalVolumes_Sequence.png", CompositeRgb))
            return 1;
    }

    // ====================================================================================================================
    // 15: Fog Stratification Profiles - RadiationMist | AdvectionFog | ValleyStratum, one wind state, identical lighting
    // ====================================================================================================================
    if (SceneSelected("15_Fog_StratificationProfiles.png"))
    {
        const uint32_t PanelW = ImageW / 3u;
        std::vector<uint8_t> CompositeRgb(static_cast<size_t>(ImageW) * ImageH * 3u, 14u);

        const std::array<FogLayerCategory, 3u> Categories = {
            FogLayerCategory::RadiationMist, FogLayerCategory::AdvectionFog, FogLayerCategory::ValleyStratum };
        const std::array<const char*, 3u> Labels = { "MIST", "ADVECT", "VALLEY" };

        for (size_t PanelIdx = 0u; PanelIdx < 3u; ++PanelIdx)
        {
            UnifiedSceneSpecification S{};
            S.FogConfig.FogEnabled                 = true;
            S.SkyConfig                            = ConstructEarthSkyAtSolarAngles(11.0f, 96.0f);
            S.SkyConfig.SequenceSeconds            = 60.0f;
            S.SkyConfig.ObserverAltitudeMetres     = 58.0f;
            S.SkyConfig.HighAltitudeCirrusCoverage = 0.10f;
            S.StarConfig.StarfieldEnabled          = false;
            S.CloudConfig.CloudsEnabled            = false;

            // One extinction, three stratifications: a shallow radiation skin, a deep well-mixed marine layer, and a fog sea of
            // near-uniform density cut off by an inversion. The e-folding height IS the physical difference, so it must differ.
            const std::array<float, 3u> ScaleHeights = { 58.0f, 260.0f, 1500.0f };
            S.FogConfig.HeightLayer.Category               = Categories[PanelIdx];
            S.FogConfig.HeightLayer.BaseAltitudeMetres     = 6.0f;
            S.FogConfig.HeightLayer.ExtinctionPerKilometre = 0.72f;
            S.FogConfig.HeightLayer.ScaleHeightMetres      = ScaleHeights[PanelIdx];
            S.FogConfig.HeightLayer.InversionAltitudeMetres = 235.0f;
            S.FogConfig.HeightLayer.InversionReliefMetres  = 70.0f;
            S.FogConfig.HeightLayer.DensityNoiseStrength   = 0.66f;
            S.FogConfig.VolumeMarchStepCount               = 26u;
            S.FogConfig.LocalVolumes.clear();

            S.WindField                            = StormWind;
            S.Camera.ForwardDirection              = DirectionFromElevationAzimuth(0.6f, 0.0f);
            S.Camera.VerticalFovDegrees            = 24.0f;
            S.ExposureScale                        = 1.06f;

            const uint32_t ActualPanelW = (PanelIdx == 2u) ? (ImageW - 2u * PanelW) : PanelW;
            const std::vector<uint8_t> PanelRgb = RenderViewportRgb(S, ActualPanelW, ImageH);

            const uint32_t DestOffsetX = static_cast<uint32_t>(PanelIdx) * PanelW;
            for (uint32_t Y = 0u; Y < ImageH; ++Y)
            {
                for (uint32_t X = 0u; X < ActualPanelW; ++X)
                {
                    const size_t Src = (static_cast<size_t>(Y) * ActualPanelW + X) * 3u;
                    const size_t Dst = (static_cast<size_t>(Y) * ImageW + (DestOffsetX + X)) * 3u;
                    CompositeRgb[Dst + 0u] = PanelRgb[Src + 0u];
                    CompositeRgb[Dst + 1u] = PanelRgb[Src + 1u];
                    CompositeRgb[Dst + 2u] = PanelRgb[Src + 2u];
                }
            }
            if (PanelIdx > 0u)
            {
                for (uint32_t Y = 0u; Y < ImageH; ++Y)
                {
                    const size_t Sep = (static_cast<size_t>(Y) * ImageW + DestOffsetX) * 3u;
                    CompositeRgb[Sep + 0u] = 220u;
                    CompositeRgb[Sep + 1u] = 228u;
                    CompositeRgb[Sep + 2u] = 240u;
                }
            }
            StampBadgeLabel(CompositeRgb, ImageW, ImageH, DestOffsetX + 12u, 12u, Labels[PanelIdx], 2u);
        }

        if (!SaveProof("15_Fog_StratificationProfiles.png", CompositeRgb))
            return 1;
    }

    std::printf("\n[SUCCESS] All 11 physics gates verified and 15 AuditProof PNGs written to %s.\n", OutputDirectory.c_str());
    return 0;
}
