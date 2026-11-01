/*==========================================================================================================================================
                                           SURFACE SCATTERING ATLAS DEFINITION
==========================================================================================================================================*/
// 🧩 Authored TextureStudio .material parser, static PNG 2D-array texture resampler, Heitz VNDF CPU oracle, & 8x float4 GPU packer.

#include "Engine/Rendering/SurfaceScatteringAtlas.h"

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
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Frontier
{
namespace
{

constexpr float PiConstant = 3.14159265358979323846f;

std::string TrimWhitespace(std::string S)
{
    size_t Start = 0u;
    while (Start < S.size() && std::isspace(static_cast<unsigned char>(S[Start])) != 0)
    {
        ++Start;
    }
    size_t End = S.size();
    while (End > Start && std::isspace(static_cast<unsigned char>(S[End - 1u])) != 0)
    {
        --End;
    }
    return S.substr(Start, End - Start);
}

std::vector<float> ParseBracketFloats(const std::string& ValueText)
{
    std::vector<float> Out;
    std::string        Cleaned;
    Cleaned.reserve(ValueText.size());
    for (char Ch : ValueText)
    {
        if (Ch == '[' || Ch == ']' || Ch == ',')
        {
            Cleaned.push_back(' ');
        }
        else
        {
            Cleaned.push_back(Ch);
        }
    }
    std::istringstream Stream(Cleaned);
    float              Num = 0.0f;
    while (Stream >> Num)
    {
        Out.push_back(Num);
    }
    return Out;
}

std::string ParseQuotedText(const std::string& ValueText)
{
    const size_t First = ValueText.find('"');
    const size_t Last  = ValueText.rfind('"');
    if (First != std::string::npos && Last != std::string::npos && Last > First)
    {
        return ValueText.substr(First + 1u, Last - First - 1u);
    }
    return TrimWhitespace(ValueText);
}

bool ParseHexColour(std::string_view Hex, std::array<float, 3>& OutLinearRgb)
{
    // 📝 Procedural channel definitions carry their authored swatch as a CSS hex string; that swatch is the
    // fallback base colour when a material is texture-driven but the referenced PNG cannot be resolved.
    std::string Digits;
    for (char Ch : Hex)
    {
        if (Ch == '#')
        {
            continue;
        }
        if (std::isxdigit(static_cast<unsigned char>(Ch)) == 0)
        {
            return false;
        }
        Digits.push_back(Ch);
    }
    if (Digits.size() != 6u)
    {
        return false;
    }
    const auto Channel = [&](size_t Offset) {
        const float Srgb = static_cast<float>(std::stoul(Digits.substr(Offset, 2u), nullptr, 16)) / 255.0f;
        return (Srgb <= 0.04045f) ? (Srgb / 12.92f) : std::pow((Srgb + 0.055f) / 1.055f, 2.4f);
    };
    OutLinearRgb = {Channel(0u), Channel(2u), Channel(4u)};
    return true;
}

std::filesystem::path LocateTextureStudioDirectory(const std::string& ExplicitRoot)
{
    namespace fs = std::filesystem;
    std::error_code Ec;

    auto ProbeDir = [&](const fs::path& Base) -> fs::path {
        if (Base.empty())
        {
            return {};
        }
        const std::array<fs::path, 4> Candidates = {{
            Base / "MaterialArchives" / "TextureStudio",
            Base / "Content" / "MaterialArchives" / "TextureStudio",
            Base / "Projects" / "Everest" / "Content" / "MaterialArchives" / "TextureStudio",
            Base / "EngineContent" / "MaterialArchives" / "TextureStudio",
        }};
        for (const fs::path& Cand : Candidates)
        {
            if (fs::is_directory(Cand, Ec))
            {
                return Cand;
            }
        }
        return {};
    };

    if (!ExplicitRoot.empty())
    {
        if (fs::path Found = ProbeDir(fs::path(ExplicitRoot)); !Found.empty())
        {
            return Found;
        }
    }
    if (const char* EnvDir = std::getenv("FRONTIER_PROJECT_CONTENT_DIR"); EnvDir && EnvDir[0] != '\0')
    {
        if (fs::path Found = ProbeDir(fs::path(EnvDir)); !Found.empty())
        {
            return Found;
        }
    }

    fs::path Cursor = fs::current_path(Ec);
    for (int Depth = 0; Depth < 8 && !Cursor.empty(); ++Depth)
    {
        if (fs::path Found = ProbeDir(Cursor); !Found.empty())
        {
            return Found;
        }
        const fs::path Parent = Cursor.parent_path();
        if (Parent == Cursor)
        {
            break;
        }
        Cursor = Parent;
    }
    return {};
}

bool LoadAndResampleRgbaImage(const std::filesystem::path& FilePath,
                              uint32_t                     TargetDim,
                              std::vector<uint8_t>&        OutRgba)
{
    std::error_code Ec;
    if (!std::filesystem::is_regular_file(FilePath, Ec))
    {
        return false;
    }
    std::ifstream In(FilePath, std::ios::binary | std::ios::ate);
    if (!In.is_open())
    {
        return false;
    }
    const std::streamsize Size = In.tellg();
    if (Size <= 0 || Size > 64 * 1024 * 1024)
    {
        return false;
    }
    std::vector<stbi_uc> Encoded(static_cast<size_t>(Size));
    In.seekg(0, std::ios::beg);
    In.read(reinterpret_cast<char*>(Encoded.data()), Size);

    int      SrcW = 0, SrcH = 0, SrcComp = 0;
    stbi_uc* Pixels = stbi_load_from_memory(Encoded.data(), static_cast<int>(Encoded.size()), &SrcW, &SrcH, &SrcComp, 4);
    if (!Pixels || SrcW <= 0 || SrcH <= 0)
    {
        if (Pixels)
        {
            stbi_image_free(Pixels);
        }
        return false;
    }

    OutRgba.resize(static_cast<size_t>(TargetDim) * TargetDim * 4u);
    for (uint32_t Y = 0u; Y < TargetDim; ++Y)
    {
        const float Fy = (static_cast<float>(Y) + 0.5f) * static_cast<float>(SrcH) / static_cast<float>(TargetDim) - 0.5f;
        const int   Y0 = std::clamp(static_cast<int>(std::floor(Fy)), 0, SrcH - 1);
        const int   Y1 = std::min(Y0 + 1, SrcH - 1);
        const float Ty = std::clamp(Fy - static_cast<float>(Y0), 0.0f, 1.0f);

        for (uint32_t X = 0u; X < TargetDim; ++X)
        {
            const float Fx = (static_cast<float>(X) + 0.5f) * static_cast<float>(SrcW) / static_cast<float>(TargetDim) - 0.5f;
            const int   X0 = std::clamp(static_cast<int>(std::floor(Fx)), 0, SrcW - 1);
            const int   X1 = std::min(X0 + 1, SrcW - 1);
            const float Tx = std::clamp(Fx - static_cast<float>(X0), 0.0f, 1.0f);

            const stbi_uc* P00 = Pixels + (static_cast<size_t>(Y0) * SrcW + X0) * 4u;
            const stbi_uc* P10 = Pixels + (static_cast<size_t>(Y0) * SrcW + X1) * 4u;
            const stbi_uc* P01 = Pixels + (static_cast<size_t>(Y1) * SrcW + X0) * 4u;
            const stbi_uc* P11 = Pixels + (static_cast<size_t>(Y1) * SrcW + X1) * 4u;

            uint8_t* Dst = OutRgba.data() + (static_cast<size_t>(Y) * TargetDim + X) * 4u;
            for (int C = 0; C < 4; ++C)
            {
                const float V0 = static_cast<float>(P00[C]) * (1.0f - Tx) + static_cast<float>(P10[C]) * Tx;
                const float V1 = static_cast<float>(P01[C]) * (1.0f - Tx) + static_cast<float>(P11[C]) * Tx;
                Dst[C]         = static_cast<uint8_t>(std::clamp(std::lround(V0 * (1.0f - Ty) + V1 * Ty), 0l, 255l));
            }
        }
    }

    stbi_image_free(Pixels);
    return true;
}

uint32_t PackRgb8Word(std::array<float, 3> LinearRgb) noexcept
{
    const uint32_t R = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(LinearRgb[0] * 255.0f)), 0, 255));
    const uint32_t G = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(LinearRgb[1] * 255.0f)), 0, 255));
    const uint32_t B = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(LinearRgb[2] * 255.0f)), 0, 255));
    return R | (G << 8u) | (B << 16u);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                          CPU HEITZ VNDF WHITE-FURNACE & MATERIAL LOD CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

float IntegrateCpuVndfWhiteFurnace(float CosThetaV, float Roughness, float Anisotropy, uint32_t SampleCount) noexcept
{
    const float ClampedCosV = std::clamp(CosThetaV, 0.05f, 1.0f);
    const float SinThetaV   = std::sqrt(std::max(0.0f, 1.0f - ClampedCosV * ClampedCosV));
    const float VecVx = SinThetaV;
    const float VecVy = 0.0f;
    const float VecVz = ClampedCosV;

    const float R      = std::clamp(Roughness, 0.04f, 1.0f);
    const float Aniso  = std::clamp(Anisotropy, -0.9f, 0.9f);
    const float AlphaX = std::max(0.02f, R * R * std::sqrt(1.0f + 0.9f * Aniso));
    const float AlphaY = std::max(0.02f, R * R * std::sqrt(1.0f - 0.9f * Aniso));

    auto Lambda = [&](float Wx, float Wy, float Wz) noexcept -> float {
        if (Wz <= 1.0e-6f)
        {
            return 0.0f;
        }
        const float Tan2 = (AlphaX * AlphaX * Wx * Wx + AlphaY * AlphaY * Wy * Wy) / (Wz * Wz);
        return 0.5f * (-1.0f + std::sqrt(1.0f + Tan2));
    };

    const float LambdaV = Lambda(VecVx, VecVy, VecVz);
    const float G1V     = 1.0f / (1.0f + LambdaV);

    // Transform V to stretched hemisphere
    float Vhx = AlphaX * VecVx;
    float Vhy = AlphaY * VecVy;
    float Vhz = VecVz;
    {
        const float Len = std::sqrt(Vhx * Vhx + Vhy * Vhy + Vhz * Vhz);
        Vhx /= Len;
        Vhy /= Len;
        Vhz /= Len;
    }

    const float Lensq = Vhx * Vhx + Vhy * Vhy;
    const float T1x   = (Lensq > 1.0e-7f) ? (-Vhy / std::sqrt(Lensq)) : 1.0f;
    const float T1y   = (Lensq > 1.0e-7f) ? (Vhx / std::sqrt(Lensq)) : 0.0f;
    const float T1z   = 0.0f;
    const float T2x   = Vhy * T1z - Vhz * T1y;
    const float T2y   = Vhz * T1x - Vhx * T1z;
    const float T2z   = Vhx * T1y - Vhy * T1x;

    const uint32_t N   = std::max(SampleCount, 16u);
    double         Sum = 0.0;

    for (uint32_t I = 0u; I < N; ++I)
    {
        // Hammersley 2D low-discrepancy sequence
        const float U1 = (static_cast<float>(I) + 0.5f) / static_cast<float>(N);
        uint32_t    Bits = I;
        Bits             = (Bits << 16u) | (Bits >> 16u);
        Bits             = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
        Bits             = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
        Bits             = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
        Bits             = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
        const float U2   = static_cast<float>(Bits) * 2.3283064365386963e-10f;

        const float Rad  = std::sqrt(U1);
        const float Phi  = 2.0f * PiConstant * U2;
        const float T1   = Rad * std::cos(Phi);
        float       T2   = Rad * std::sin(Phi);
        const float S    = 0.5f * (1.0f + Vhz);
        T2               = (1.0f - S) * std::sqrt(std::max(0.0f, 1.0f - T1 * T1)) + S * T2;
        const float T3   = std::sqrt(std::max(0.0f, 1.0f - T1 * T1 - T2 * T2));

        const float Nhx = T1 * T1x + T2 * T2x + T3 * Vhx;
        const float Nhy = T1 * T1y + T2 * T2y + T3 * Vhy;
        const float Nhz = std::max(0.0f, T1 * T1z + T2 * T2z + T3 * Vhz);

        float Hx = AlphaX * Nhx;
        float Hy = AlphaY * Nhy;
        float Hz = std::max(0.0f, Nhz);
        const float HLen = std::sqrt(std::max(Hx * Hx + Hy * Hy + Hz * Hz, 1.0e-12f));
        Hx /= HLen;
        Hy /= HLen;
        Hz /= HLen;

        const float VdotH = VecVx * Hx + VecVy * Hy + VecVz * Hz;
        const float Lx    = 2.0f * VdotH * Hx - VecVx;
        const float Ly    = 2.0f * VdotH * Hy - VecVy;
        const float Lz    = 2.0f * VdotH * Hz - VecVz;

        if (Lz > 0.0f && VdotH > 0.0f)
        {
            // Exact single-sample weight for height-correlated GGX with VNDF sampling: G2(V, L) / G1(V)
            const float G2 = 1.0f / (1.0f + LambdaV + Lambda(Lx, Ly, Lz));
            Sum += static_cast<double>(G2 / std::max(G1V, 1.0e-6f));
        }
    }

    return static_cast<float>(Sum / static_cast<double>(N));
}

MaterialDetailTier ClassifyMaterialDetailTier(float DistanceMeters,
                                              float NearDetailMeters,
                                              float FarProxyMeters,
                                              float MaterialQualityScale) noexcept
{
    const float Scale   = std::clamp(MaterialQualityScale, 0.25f, 3.0f);
    const float NearCut = std::max(1.0f, NearDetailMeters * Scale);
    const float FarCut  = std::max(NearCut + 1.0f, FarProxyMeters * Scale);
    if (DistanceMeters <= NearCut)
    {
        return MaterialDetailTier::FullStrata;
    }
    if (DistanceMeters <= FarCut)
    {
        return MaterialDetailTier::FilteredToksvig;
    }
    return MaterialDetailTier::CompactProxy;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      SURFACE SCATTERING ATLAS IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

SurfaceScatteringAtlas::SurfaceScatteringAtlas()
{
    LoadControlVehicleDefaults();
}

uint32_t SurfaceScatteringAtlas::RegisterPbrTextureSet(const std::string& BaseDirectory,
                                                       const std::string& AlbedoRelPath,
                                                       const std::string& OrmRelPath,
                                                       const std::string& NormalRelPath,
                                                       uint32_t&          OutChannelMask)
{
    OutChannelMask = 0u;
    if (AlbedoLayersRgba_.size() >= MaximumTextureArrayLayers)
    {
        return 0u;
    }

    const size_t         TexelBytes = static_cast<size_t>(TextureAtlasDimension) * TextureAtlasDimension * 4u;
    std::vector<uint8_t> AlbedoLayer(TexelBytes, 255u);
    std::vector<uint8_t> NormalLayer(TexelBytes, 128u);
    std::vector<uint8_t> OrmLayer(TexelBytes, 255u);

    for (size_t Px = 0u; Px < static_cast<size_t>(TextureAtlasDimension) * TextureAtlasDimension; ++Px)
    {
        NormalLayer[Px * 4u + 0u] = 128u;
        NormalLayer[Px * 4u + 1u] = 128u;
        NormalLayer[Px * 4u + 2u] = 255u;
        NormalLayer[Px * 4u + 3u] = 255u;

        OrmLayer[Px * 4u + 0u] = 255u; // AO = 1
        OrmLayer[Px * 4u + 1u] = 255u; // Roughness multiplier = 1
        OrmLayer[Px * 4u + 2u] = 255u; // Metallic multiplier = 1
        OrmLayer[Px * 4u + 3u] = 255u;
    }

    const std::filesystem::path Base(BaseDirectory);
    if (!AlbedoRelPath.empty() && LoadAndResampleRgbaImage(Base / AlbedoRelPath, TextureAtlasDimension, AlbedoLayer))
    {
        OutChannelMask |= 1u;
    }
    if (!OrmRelPath.empty() && LoadAndResampleRgbaImage(Base / OrmRelPath, TextureAtlasDimension, OrmLayer))
    {
        OutChannelMask |= 2u;
    }
    if (!NormalRelPath.empty() && LoadAndResampleRgbaImage(Base / NormalRelPath, TextureAtlasDimension, NormalLayer))
    {
        OutChannelMask |= 4u;
    }

    if (OutChannelMask == 0u)
    {
        return 0u;
    }

    const uint32_t AssignedLayer = static_cast<uint32_t>(AlbedoLayersRgba_.size());
    AlbedoLayersRgba_.push_back(std::move(AlbedoLayer));
    NormalLayersRgba_.push_back(std::move(NormalLayer));
    OrmLayersRgba_.push_back(std::move(OrmLayer));
    return AssignedLayer;
}

bool SurfaceScatteringAtlas::LoadAuthoredMaterialFile(uint32_t           ClassificationId,
                                                      const std::string& MaterialFilePath,
                                                      std::string*       OutDiagnostic)
{
    if (ClassificationId >= MaximumClassificationSlots)
    {
        return false;
    }

    std::filesystem::path ResolvedPath(MaterialFilePath);
    std::error_code       Ec;
    if (!std::filesystem::is_regular_file(ResolvedPath, Ec))
    {
        const std::filesystem::path StudioDir = LocateTextureStudioDirectory("");
        if (!StudioDir.empty())
        {
            ResolvedPath = StudioDir / MaterialFilePath;
        }
    }
    if (!std::filesystem::is_regular_file(ResolvedPath, Ec))
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "Could not locate authored .material file: " + MaterialFilePath;
        }
        return false;
    }

    std::ifstream In(ResolvedPath);
    if (!In.is_open())
    {
        return false;
    }

    ScatteringSequenceSlot Parsed = Slots_[ClassificationId];

    enum class SectionContext
    {
        Root,
        Strata,
        StrataTextures,
        StrataChannelParams,
        AutomotivePaint,
    };

    SectionContext Section      = SectionContext::Root;
    std::string    StrataKind   = "Opaque";
    std::string    TexAlbedoRel = {};
    std::string    TexOrmRel    = {};
    std::string    TexNormalRel = {};
    std::string    Line;
    std::array<float, 3> ChannelSwatch          = {0.0f, 0.0f, 0.0f};
    bool                 ChannelSwatchResolved  = false;

    while (std::getline(In, Line))
    {
        const size_t HashPos = Line.find('#');
        if (HashPos != std::string::npos && Line.find('"') == std::string::npos)
        {
            Line.erase(HashPos);
        }
        Line = TrimWhitespace(Line);
        if (Line.empty())
        {
            continue;
        }

        if (Line == "[[Strata]]")
        {
            Section    = SectionContext::Strata;
            StrataKind = "Opaque";
            continue;
        }
        if (Line == "[Strata.Textures]")
        {
            Section = SectionContext::StrataTextures;
            continue;
        }
        if (Line == "[[Strata.Channels]]" || Line == "[Strata.Channels.Params]")
        {
            Section = SectionContext::StrataChannelParams;
            continue;
        }
        if (Line == "[AutomotivePaint]")
        {
            Section = SectionContext::AutomotivePaint;
            continue;
        }
        if (Line.front() == '[')
        {
            Section = SectionContext::Root;
            continue;
        }

        const size_t Eq = Line.find('=');
        if (Eq == std::string::npos)
        {
            continue;
        }
        const std::string Key = TrimWhitespace(Line.substr(0u, Eq));
        const std::string Val = TrimWhitespace(Line.substr(Eq + 1u));

        if (Section == SectionContext::Root && Key == "Name")
        {
            Parsed.PresetName = ParseQuotedText(Val);
        }
        else if (Section == SectionContext::Strata)
        {
            if (Key == "Category")
            {
                StrataKind = ParseQuotedText(Val);
            }
            else if (Key == "Albedo")
            {
                const auto V = ParseBracketFloats(Val);
                if (V.size() >= 3u)
                {
                    if (StrataKind == "Coating")
                    {
                        Parsed.CoatTint = {V[0], V[1], V[2]};
                    }
                    else if (StrataKind == "Emissive")
                    {
                        Parsed.EmissionRadiance = {V[0], V[1], V[2]};
                    }
                    else
                    {
                        Parsed.BaseAlbedo = {V[0], V[1], V[2]};
                    }
                }
            }
            else if (Key == "Roughness")
            {
                const auto V = ParseBracketFloats(Val);
                if (!V.empty())
                {
                    if (StrataKind == "Coating")
                    {
                        Parsed.CoatRoughness = V[0];
                    }
                    else
                    {
                        Parsed.BaseRoughness = V[0];
                    }
                }
            }
            else if (Key == "Metallic")
            {
                const auto V = ParseBracketFloats(Val);
                if (!V.empty() && StrataKind != "Coating")
                {
                    Parsed.Metallic = V[0];
                }
            }
            else if (Key == "Ior")
            {
                const auto V = ParseBracketFloats(Val);
                if (!V.empty())
                {
                    if (StrataKind == "Coating")
                    {
                        Parsed.CoatRefractiveIndex = V[0];
                    }
                    else
                    {
                        Parsed.BaseRefractiveIndex = V[0];
                    }
                }
            }
            else if (Key == "Coverage")
            {
                const auto V = ParseBracketFloats(Val);
                if (!V.empty())
                {
                    if (StrataKind == "Coating")
                    {
                        Parsed.CoatCoverage = V[0];
                    }
                    else if (StrataKind == "Translucent" || StrataKind == "Glass")
                    {
                        Parsed.TransmissiveFraction = V[0];
                    }
                    else if (StrataKind == "Sheen")
                    {
                        Parsed.SheenWeight = V[0];
                    }
                    else if (StrataKind == "Subsurface")
                    {
                        Parsed.SubsurfaceWrap = V[0];
                    }
                }
            }
            else if (Key == "Anisotropy")
            {
                const auto V = ParseBracketFloats(Val);
                if (!V.empty())
                {
                    Parsed.Anisotropy = V[0];
                }
            }
            else if (Key == "EmissionStrength" || Key == "Strength")
            {
                const auto V = ParseBracketFloats(Val);
                if (!V.empty() && StrataKind == "Emissive")
                {
                    Parsed.EmissionStrength = V[0];
                }
            }
            else if (Key == "UvScaleOffset")
            {
                const auto V = ParseBracketFloats(Val);
                if (V.size() >= 2u)
                {
                    Parsed.UvScale = {std::max(V[0], 0.05f), std::max(V[1], 0.05f)};
                }
            }
        }
        else if (Section == SectionContext::StrataTextures)
        {
            if (Key == "Albedo")
            {
                TexAlbedoRel = ParseQuotedText(Val);
            }
            else if (Key == "Orm")
            {
                TexOrmRel = ParseQuotedText(Val);
            }
            else if (Key == "Normal")
            {
                TexNormalRel = ParseQuotedText(Val);
            }
        }
        else if (Section == SectionContext::StrataChannelParams)
        {
            if (!ChannelSwatchResolved &&
                (Key == "color" || Key == "warpColor" || Key == "weftColor" || Key == "fillColor" ||
                 Key == "baseColor" || Key == "secondaryColor"))
            {
                std::array<float, 3> ParsedSwatch{};
                if (ParseHexColour(ParseQuotedText(Val), ParsedSwatch))
                {
                    ChannelSwatch         = ParsedSwatch;
                    ChannelSwatchResolved = true;
                }
            }
            if (Key == "angle")
            {
                const auto V = ParseBracketFloats(Val);
                if (!V.empty())
                {
                    Parsed.AnisotropyAngleDeg = V[0];
                }
            }
            else if (Key == "detailScale")
            {
                const auto V = ParseBracketFloats(Val);
                if (!V.empty())
                {
                    Parsed.MicroDetailScale = std::clamp(V[0] / 300.0f, 0.05f, 1.0f);
                }
            }
        }
        else if (Section == SectionContext::AutomotivePaint)
        {
            if (Key == "Enabled")
            {
                Parsed.AutomotiveFlakeActive = (Val == "true" || Val == "1");
            }
            else if (Key == "Distribution")
            {
                const auto V = ParseBracketFloats(Val);
                if (V.size() >= 2u)
                {
                    Parsed.FlakeCoverage = std::clamp(V[0], 0.05f, 1.0f);
                    Parsed.FlakeDensity  = std::clamp(V[1], 20.0f, 2000.0f);
                }
            }
            else if (Key == "Finish")
            {
                const auto V = ParseBracketFloats(Val);
                if (V.size() >= 2u)
                {
                    Parsed.FlakeRoughness  = std::clamp(V[0], 0.02f, 0.65f);
                    Parsed.FlakeTiltSpread = std::clamp(V[1], 0.02f, 0.85f);
                }
            }
            else if (Key == "Palette")
            {
                const auto V = ParseBracketFloats(Val);
                if (V.size() >= 7u)
                {
                    // First stop enhances base flake albedo; second stop drives grazing flip iridescence!
                    Parsed.BaseAlbedo = {
                        0.45f * Parsed.BaseAlbedo[0] + 0.55f * V[0],
                        0.45f * Parsed.BaseAlbedo[1] + 0.55f * V[1],
                        0.45f * Parsed.BaseAlbedo[2] + 0.55f * V[2],
                    };
                    Parsed.FlakeSecondaryTint = {V[4], V[5], V[6]};
                }
            }
        }
    }

    if (std::getenv("FRONTIER_MATERIAL_DEBUG") != nullptr)
    {
        std::fprintf(stderr, "[material] id=%u name=%s texA='%s' swatch=%d\n", ClassificationId,
                     Parsed.PresetName.c_str(), TexAlbedoRel.c_str(),
                     ChannelSwatchResolved ? 1 : 0);
    }
    if (!TexAlbedoRel.empty() || !TexOrmRel.empty() || !TexNormalRel.empty())
    {
        uint32_t Mask            = 0u;
        const uint32_t LayerIdx  = RegisterPbrTextureSet(
            ResolvedPath.parent_path().string(), TexAlbedoRel, TexOrmRel, TexNormalRel, Mask);
        if (Mask != 0u)
        {
            Parsed.TextureLayerIndex  = LayerIdx;
            Parsed.TextureChannelMask = Mask;
        }

        // 📝 TextureStudio procedural channels write their pattern into the Albedo PNG as a modulation mask and
        // carry the actual colour in the channel definition's swatch. The renderer multiplies base albedo by the
        // sampled map, so the swatch has to land in BaseAlbedo; leaving the authored numeric albedo at its
        // texture-mask value of (1,1,1) rendered carbon-fibre trim and concrete as untextured white panels.
        if (ChannelSwatchResolved)
        {
            Parsed.BaseAlbedo = ChannelSwatch;
        }
        else if (std::min({Parsed.BaseAlbedo[0], Parsed.BaseAlbedo[1], Parsed.BaseAlbedo[2]}) > 0.92f)
        {
            // A texture-mask material with no resolvable swatch keeps a mid-neutral base so it can never read
            // as a pure-white surface.
            Parsed.BaseAlbedo = {0.32f, 0.33f, 0.34f};
        }
    }

    Slots_[ClassificationId] = Parsed;
    return true;
}

void SurfaceScatteringAtlas::LoadControlVehicleDefaults(const std::string& ContentSearchRoot)
{
    AlbedoLayersRgba_.clear();
    NormalLayersRgba_.clear();
    OrmLayersRgba_.clear();

    // Reserve Layer 0 as neutral white / flat-normal / identity-ORM so untextured slots never sample garbage.
    const size_t         TexelBytes = static_cast<size_t>(TextureAtlasDimension) * TextureAtlasDimension * 4u;
    std::vector<uint8_t> NeutralAlbedo(TexelBytes, 255u);
    std::vector<uint8_t> NeutralNormal(TexelBytes, 128u);
    std::vector<uint8_t> NeutralOrm(TexelBytes, 255u);
    for (size_t Px = 0u; Px < static_cast<size_t>(TextureAtlasDimension) * TextureAtlasDimension; ++Px)
    {
        NeutralNormal[Px * 4u + 2u] = 255u;
        NeutralNormal[Px * 4u + 3u] = 255u;
    }
    AlbedoLayersRgba_.push_back(std::move(NeutralAlbedo));
    NormalLayersRgba_.push_back(std::move(NeutralNormal));
    OrmLayersRgba_.push_back(std::move(NeutralOrm));

    // 📝 Every classification slot starts as dark neutral trim rather than the light-grey default, so a
    // classification without an authored/crafted entry (grille, splitter, bolts) reads as dark composite
    // instead of a bright white placeholder patch on the vehicle.
    for (auto& Slot : Slots_)
    {
        Slot                 = ScatteringSequenceSlot{};
        Slot.PresetName      = "UnclassifiedCompositeTrim";
        Slot.BaseAlbedo      = {0.155f, 0.160f, 0.170f};
        Slot.BaseRoughness   = 0.58f;
        Slot.Metallic        = 0.10f;
        Slot.BaseRefractiveIndex = 1.50f;
    }

    // 📝 Slot numbering follows the established ControlVehicle classification contract from
    // ScreenSpaceClusterRefinement::RegisterControlVehicleMaterials: 6 body, 7 glazing, 10 rim,
    // 11 tyre, 16 cabin. Authored TextureStudio materials are then layered on top of these slots.

    // Slot 0: Proving-ground asphalt / board-form concrete slab
    Slots_[0] = ScatteringSequenceSlot{
        .PresetName          = "ProvingGroundConcrete",
        .BaseAlbedo          = {0.26f, 0.27f, 0.28f},
        .BaseRoughness       = 0.78f,
        .Metallic            = 0.0f,
        .BaseRefractiveIndex = 1.50f,
        .MicroDetailScale    = 0.35f,
        .UvScale             = {8.0f, 8.0f},
    };

    // Slot 6: Automotive metallic clearcoat bodywork (3D Voronoi flakes + absorbing clearcoat)
    Slots_[6] = ScatteringSequenceSlot{
        .PresetName            = "AutomotiveBodyPaint",
        .BaseAlbedo            = {0.52f, 0.14f, 0.06f},
        .BaseRoughness         = 0.24f,
        .Metallic              = 0.72f,
        .BaseRefractiveIndex   = 1.52f,
        .CoatTint              = {1.0f, 0.98f, 0.97f},
        .CoatCoverage          = 1.0f,
        .CoatRoughness         = 0.045f,
        .CoatRefractiveIndex   = 1.52f,
        .MicroDetailScale      = 0.18f,
        .AutomotiveFlakeActive = true,
        .FlakeDensity          = 420.0f,
        .FlakeCoverage         = 0.62f,
        .FlakeTiltSpread       = 0.24f,
        .FlakeRoughness        = 0.11f,
        .FlakeSecondaryTint    = {0.86f, 0.40f, 0.16f},
    };

    // Slot 7 & 12: Laminated acoustic glazing (2-interface Snell + TIR + Beer-Lambert)
    ScatteringSequenceSlot CurvedGlass{
        .PresetName           = "LaminatedAcousticGlass",
        .BaseAlbedo           = {0.94f, 0.97f, 0.96f},
        .BaseRoughness        = 0.025f,
        .Metallic             = 0.0f,
        .BaseRefractiveIndex  = 1.52f,
        .CoatCoverage         = 1.0f,
        .CoatRoughness        = 0.02f,
        .CoatRefractiveIndex  = 1.52f,
        .TransmissiveFraction = 0.88f,
        .AbsorptionPerMeter   = {0.14f, 0.04f, 0.06f},
    };
    Slots_[7]  = CurvedGlass;
    Slots_[12] = CurvedGlass;
    Slots_[12].BaseRefractiveIndex = 1.58f;

    // Slot 8: Carbon-fibre aero trim / underbody
    Slots_[8] = ScatteringSequenceSlot{
        .PresetName          = "CarbonFibreTrim",
        .BaseAlbedo          = {0.06f, 0.065f, 0.07f},
        .BaseRoughness       = 0.42f,
        .Metallic            = 0.18f,
        .BaseRefractiveIndex = 1.48f,
        .Anisotropy          = 0.35f,
        .CoatCoverage        = 0.35f,
        .CoatRoughness       = 0.16f,
        .MicroDetailScale    = 0.28f,
        .UvScale             = {6.0f, 6.0f},
    };

    // Slot 10: Brushed forged alloy rim (anisotropic stretched-ellipsoid GGX)
    Slots_[10] = ScatteringSequenceSlot{
        .PresetName          = "BrushedAlloyRim",
        .BaseAlbedo          = {0.72f, 0.74f, 0.77f},
        .BaseRoughness       = 0.22f,
        .Metallic            = 0.94f,
        .BaseRefractiveIndex = 2.20f,
        .Anisotropy          = 0.55f,
        .AnisotropyAngleDeg  = 25.0f,
        .CoatCoverage        = 0.30f,
        .CoatRoughness       = 0.06f,
        .MicroDetailScale    = 0.22f,
        .UvScale             = {6.0f, 3.0f},
    };

    // Slot 11: Vulcanised tyre rubber with microfiber sheen
    Slots_[11] = ScatteringSequenceSlot{
        .PresetName          = "VulcanisedTyreRubber",
        .BaseAlbedo          = {0.048f, 0.050f, 0.054f},
        .BaseRoughness       = 0.84f,
        .Metallic            = 0.0f,
        .BaseRefractiveIndex = 1.53f,
        .SheenWeight         = 0.16f,
        .MicroDetailScale    = 0.50f,
    };

    // Slot 15 / 17..19: dark composite intake Grille, undertray, and seals
    for (uint32_t Idx : {15u, 17u, 18u, 19u})
    {
        Slots_[Idx].PresetName    = "DarkCompositeIntake";
        Slots_[Idx].BaseAlbedo    = {0.075f, 0.078f, 0.086f};
        Slots_[Idx].BaseRoughness = 0.52f;
        Slots_[Idx].Metallic      = 0.30f;
    }

    // Slot 13: Headlamp / fascia housing. 📝 Class 13 covers the whole front fascia band in the ControlVehicle
    // archive, so authoring it as a bright LED emitter turned the entire front end into a white glowing slab.
    // The housing is dark metal; only the small projector facets carry a bounded warm emission.
    Slots_[13] = ScatteringSequenceSlot{
        .PresetName          = "HeadlampHousingMetal",
        .BaseAlbedo          = {0.30f, 0.31f, 0.34f},
        .BaseRoughness       = 0.28f,
        .Metallic            = 0.62f,
        .BaseRefractiveIndex = 1.55f,
        .CoatCoverage        = 0.45f,
        .CoatRoughness       = 0.08f,
        .EmissionRadiance    = {1.0f, 0.88f, 0.72f},
        .EmissionStrength    = 0.35f,
    };

    // Slot 14: Ruby taillight / terracotta radiosity baffle (directly visible colour bleed probe)
    Slots_[14] = ScatteringSequenceSlot{
        .PresetName       = "TerracottaRadiosityBaffle",
        .BaseAlbedo       = {0.70f, 0.20f, 0.13f},
        .BaseRoughness    = 0.70f,
        .BaseRefractiveIndex = 1.50f,
        .SubsurfaceWrap   = 0.10f,
        .EmissionStrength = 0.0f,
    };

    // Slot 16: Interior Alcantara cabin with Charlie fabric sheen
    Slots_[16] = ScatteringSequenceSlot{
        .PresetName          = "AlcantaraCabin",
        .BaseAlbedo          = {0.68f, 0.48f, 0.30f},
        .BaseRoughness       = 0.62f,
        .Metallic            = 0.0f,
        .BaseRefractiveIndex = 1.48f,
        .SheenWeight         = 0.42f,
        .MicroDetailScale    = 0.20f,
    };

    // Slot 20: Overhead studio strip luminaire (flux-weighted emissive facet CDF source)
    Slots_[20] = ScatteringSequenceSlot{
        .PresetName       = "StudioStripLuminaire",
        .BaseAlbedo       = {0.96f, 0.96f, 0.98f},
        .BaseRoughness    = 0.15f,
        .BaseRefractiveIndex = 1.50f,
        .EmissionRadiance = {1.0f, 0.94f, 0.84f},
        .EmissionStrength = 12.0f,
    };

    // Enrich with real authored TextureStudio .material + PNG texture layers when staged or in repo!
    const std::filesystem::path StudioDir = LocateTextureStudioDirectory(ContentSearchRoot);
    if (!StudioDir.empty())
    {
        (void)LoadAuthoredMaterialFile(0u, (StudioDir / "board-form-concrete.material").string());
        Slots_[0].BaseAlbedo = {0.26f, 0.27f, 0.28f};

        (void)LoadAuthoredMaterialFile(6u, (StudioDir / "candy-crimson.material").string());
        (void)LoadAuthoredMaterialFile(8u, (StudioDir / "carbon-fiber.material").string());
        (void)LoadAuthoredMaterialFile(10u, (StudioDir / "brushed-titanium.material").string());
        Slots_[10].BaseAlbedo    = {0.72f, 0.74f, 0.77f};
        Slots_[10].BaseRoughness = 0.22f;
        (void)LoadAuthoredMaterialFile(11u, (StudioDir / "performance-rubber.material").string());
        Slots_[11].BaseAlbedo = {0.048f, 0.050f, 0.054f};
    }
}

void SurfaceScatteringAtlas::ApplyRefinementProfiles(const ScreenSpaceClusterRefinement& Refinement)
{
    // 📝 Refinement profiles describe geometry-side optics metadata (glass classification, IOR, absorption,
    // clearcoat coverage), not the authored TextureWorkspace albedo. Overwriting the authored colour here
    // turned every painted body panel into the neutral grey profile default, so this merge is additive only.
    for (uint32_t ClassId = 0u; ClassId < MaximumClassificationSlots; ++ClassId)
    {
        const ClusterMaterialProfile& Prof = Refinement.FindMaterial(ClassId);
        if (Prof.ClassificationId != ClassId)
        {
            continue;
        }
        ScatteringSequenceSlot& Dst = Slots_[ClassId];

        if (Prof.TransmissiveGlass)
        {
            Dst.TransmissiveFraction = std::max(Dst.TransmissiveFraction, 0.85f);
            Dst.BaseRefractiveIndex  = Prof.RefractiveIndexIor;
            Dst.AbsorptionPerMeter   = {Prof.SpectralAbsorptionPerMeter.X,
                                        Prof.SpectralAbsorptionPerMeter.Y,
                                        Prof.SpectralAbsorptionPerMeter.Z};
        }
        if (Prof.ClearcoatWeight > 1.0e-3f)
        {
            Dst.CoatCoverage  = std::max(Dst.CoatCoverage, Prof.ClearcoatWeight);
            Dst.CoatRoughness = std::min(Dst.CoatRoughness, Prof.ClearcoatRoughness);
        }
        if (Prof.EmissiveRadianceRgb.X + Prof.EmissiveRadianceRgb.Y + Prof.EmissiveRadianceRgb.Z > 1.0e-4f)
        {
            Dst.EmissionRadiance = {Prof.EmissiveRadianceRgb.X, Prof.EmissiveRadianceRgb.Y, Prof.EmissiveRadianceRgb.Z};
            Dst.EmissionStrength = std::max(Dst.EmissionStrength, 1.0f);
        }
        if (Dst.MicroDetailScale <= 1.0e-4f)
        {
            Dst.MicroDetailScale = Prof.ProceduralDetailStrength;
        }
    }
}

void SurfaceScatteringAtlas::SetSlot(uint32_t ClassificationId, const ScatteringSequenceSlot& Slot)
{
    if (ClassificationId < MaximumClassificationSlots)
    {
        Slots_[ClassificationId] = Slot;
    }
}

const ScatteringSequenceSlot& SurfaceScatteringAtlas::QuerySlot(uint32_t ClassificationId) const noexcept
{
    return Slots_[std::min(ClassificationId, MaximumClassificationSlots - 1u)];
}

void SurfaceScatteringAtlas::PackForGpu(std::vector<std::array<float, 4>>& OutMaterialFloat4Table) const
{
    OutMaterialFloat4Table.clear();
    OutMaterialFloat4Table.reserve(MaximumClassificationSlots * Float4StridePerSlot);

    for (const ScatteringSequenceSlot& S : Slots_)
    {
        const uint32_t AngleQuant = static_cast<uint32_t>(
            std::clamp(static_cast<int>(std::lround(std::fmod(S.AnisotropyAngleDeg + 360.0f, 180.0f) / 180.0f * 15.0f)), 0, 15));
        const uint32_t PackedTexFlags = (S.TextureLayerIndex & 0xFFu) |
                                        ((S.TextureChannelMask & 0x07u) << 8u) |
                                        (S.AutomotiveFlakeActive ? (1u << 11u) : 0u) |
                                        (AngleQuant << 12u);
        const uint32_t PackedFlipRgb  = PackRgb8Word(S.FlakeSecondaryTint);

        OutMaterialFloat4Table.push_back({S.BaseAlbedo[0], S.BaseAlbedo[1], S.BaseAlbedo[2], S.BaseRoughness});
        OutMaterialFloat4Table.push_back({S.Metallic, S.BaseRefractiveIndex, S.Anisotropy, S.SheenWeight});
        OutMaterialFloat4Table.push_back({S.CoatTint[0], S.CoatTint[1], S.CoatTint[2], S.CoatCoverage});
        OutMaterialFloat4Table.push_back({S.CoatRoughness, S.CoatRefractiveIndex, S.TransmissiveFraction, S.SubsurfaceWrap});
        OutMaterialFloat4Table.push_back({S.EmissionRadiance[0], S.EmissionRadiance[1], S.EmissionRadiance[2], S.EmissionStrength});
        OutMaterialFloat4Table.push_back({S.AbsorptionPerMeter[0], S.AbsorptionPerMeter[1], S.AbsorptionPerMeter[2], S.MicroDetailScale});
        OutMaterialFloat4Table.push_back({std::max(S.UvScale[0], 0.05f),
                                          std::max(S.UvScale[1], 0.05f),
                                          std::bit_cast<float>(PackedTexFlags),
                                          S.FlakeDensity});
        OutMaterialFloat4Table.push_back({S.FlakeCoverage,
                                          S.FlakeTiltSpread,
                                          S.FlakeRoughness,
                                          std::bit_cast<float>(PackedFlipRgb)});
    }
}

bool SurfaceScatteringAtlas::UploadTextureArraysToGpu(VulkanComputeExchange& Device,
                                                      DeviceImageArraySlot&  OutAlbedoSrgbArray,
                                                      DeviceImageArraySlot&  OutNormalUnormArray,
                                                      DeviceImageArraySlot&  OutOrmUnormArray) const
{
    if (!Device.Ready() || AlbedoLayersRgba_.empty())
    {
        return false;
    }

    const uint32_t LayerCount   = static_cast<uint32_t>(AlbedoLayersRgba_.size());
    const size_t   LayerBytes   = static_cast<size_t>(TextureAtlasDimension) * TextureAtlasDimension * 4u;
    const size_t   TotalBytes   = LayerBytes * LayerCount;

    std::vector<uint8_t> FlatAlbedo(TotalBytes);
    std::vector<uint8_t> FlatNormal(TotalBytes);
    std::vector<uint8_t> FlatOrm(TotalBytes);

    for (uint32_t L = 0u; L < LayerCount; ++L)
    {
        std::memcpy(FlatAlbedo.data() + L * LayerBytes, AlbedoLayersRgba_[L].data(), LayerBytes);
        std::memcpy(FlatNormal.data() + L * LayerBytes, NormalLayersRgba_[L].data(), LayerBytes);
        std::memcpy(FlatOrm.data() + L * LayerBytes, OrmLayersRgba_[L].data(), LayerBytes);
    }

    return Device.AllocateSrgb8Array(TextureAtlasDimension, TextureAtlasDimension, LayerCount, OutAlbedoSrgbArray) &&
           Device.UploadSrgb8Array(OutAlbedoSrgbArray, FlatAlbedo) &&
           Device.AllocateUnorm8Array(TextureAtlasDimension, TextureAtlasDimension, LayerCount, OutNormalUnormArray) &&
           Device.UploadUnorm8Array(OutNormalUnormArray, FlatNormal) &&
           Device.AllocateUnorm8Array(TextureAtlasDimension, TextureAtlasDimension, LayerCount, OutOrmUnormArray) &&
           Device.UploadUnorm8Array(OutOrmUnormArray, FlatOrm);
}

} // namespace Frontier
