/*==========================================================================================================================================
                                            SURFACE SCATTERING ATLAS DEFINITION
==========================================================================================================================================*/
// 🧩 Validated TextureWorkspace + TextureStudio authored .material TOML / PBR PNG compiler, presets, & GPU float4 packer.

#include "Engine/Rendering/SurfaceScatteringAtlas.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <toml++/toml.hpp>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace Frontier
{
namespace
{

uint8_t EncodeLinearScalarForHardwareSrgb(float LinearValue) noexcept
{
    const float Clamped = std::clamp(LinearValue, 0.0f, 1.0f);
    const float Encoded =
        (Clamped <= 0.0031308f) ? (12.92f * Clamped) : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(Encoded * 255.0f), 0l, 255l));
}

float ReadTomlFloat(const toml::node_view<const toml::node>& View, float Fallback) noexcept
{
    if (const auto Val = View.value<double>())
    {
        return std::isfinite(*Val) ? static_cast<float>(*Val) : Fallback;
    }
    if (const auto IntVal = View.value<int64_t>())
    {
        return static_cast<float>(*IntVal);
    }
    return Fallback;
}

template <size_t Count>
std::array<float, Count> ReadTomlFloatArray(const toml::node_view<const toml::node>& View,
                                            std::array<float, Count>                 Fallback) noexcept
{
    const toml::array* Arr = View.as_array();
    if (!Arr || Arr->size() < Count)
    {
        return Fallback;
    }
    std::array<float, Count> Out = Fallback;
    for (size_t I = 0u; I < Count; ++I)
    {
        Out[I] = ReadTomlFloat(toml::node_view<const toml::node>(Arr->get(I)), Fallback[I]);
    }
    return Out;
}

} // namespace

SurfaceScatteringAtlas::SurfaceScatteringAtlas()
{
    for (uint32_t Idx = 0u; Idx < kMaxClassificationSlots; ++Idx)
    {
        Entries_[Idx].ClassificationId                    = Idx;
        Entries_[Idx].Scattering.Configuration            = {1u, 0u, 0u, 0u};
        Entries_[Idx].Scattering.Strata[0].AlbedoRoughness = {0.52f, 0.54f, 0.57f, 0.42f};
        Entries_[Idx].Scattering.Strata[0].Optics          = {0.0f, 1.50f, 1.0f, 0.0f};
    }
}

bool SurfaceScatteringAtlas::RegisterScatteringEntry(const SurfaceScatteringEntry& Entry)
{
    if (Entry.ClassificationId >= kMaxClassificationSlots)
    {
        return false;
    }
    if (!ValidatePreviewScattering(Entry.Scattering) || !ValidateAutomotivePaint(Entry.Paint))
    {
        return false;
    }
    Entries_[Entry.ClassificationId] = Entry;
    return true;
}

void SurfaceScatteringAtlas::RegisterControlVehicleScatteringPresets()
{
    auto MakeOneStratum = [](std::array<float, 3> Albedo, float Roughness, float Metallic, float Ior = 1.5f)
    {
        PreviewScattering S{};
        S.Configuration                 = {1u, 0u, 0u, 0u};
        S.Strata[0].AlbedoRoughness     = {Albedo[0], Albedo[1], Albedo[2], std::clamp(Roughness, 0.045f, 1.0f)};
        S.Strata[0].Optics              = {std::clamp(Metallic, 0.0f, 1.0f), Ior, 1.0f, 0.0f};
        S.Strata[0].Classification      = {0u, 0u, 0u, 0u};
        return S;
    };

    // Class 0..4: Proving-ground asphalt, architectural concrete, sandstone, basalt, & karst limestone defaults
    {
        SurfaceScatteringEntry Ground{};
        Ground.ClassificationId = 0u;
        Ground.Scattering       = MakeOneStratum({0.26f, 0.27f, 0.29f}, 0.56f, 0.0f, 1.48f);
        Ground.UvTilingScale    = 0.35f;
        (void)RegisterScatteringEntry(Ground);

        SurfaceScatteringEntry Concrete{};
        Concrete.ClassificationId = 1u;
        Concrete.Scattering       = MakeOneStratum({0.64f, 0.62f, 0.58f}, 0.48f, 0.0f, 1.50f);
        Concrete.UvTilingScale    = 0.50f;
        (void)RegisterScatteringEntry(Concrete);

        SurfaceScatteringEntry Sandstone{};
        Sandstone.ClassificationId = 2u;
        Sandstone.Scattering       = MakeOneStratum({0.72f, 0.48f, 0.34f}, 0.62f, 0.0f, 1.50f);
        Sandstone.UvTilingScale    = 0.60f;
        (void)RegisterScatteringEntry(Sandstone);

        SurfaceScatteringEntry Basalt{};
        Basalt.ClassificationId = 3u;
        Basalt.Scattering       = MakeOneStratum({0.20f, 0.21f, 0.22f}, 0.52f, 0.04f, 1.52f);
        Basalt.UvTilingScale    = 0.60f;
        (void)RegisterScatteringEntry(Basalt);

        SurfaceScatteringEntry Limestone{};
        Limestone.ClassificationId = 4u;
        Limestone.Scattering       = MakeOneStratum({0.76f, 0.74f, 0.69f}, 0.55f, 0.0f, 1.50f);
        Limestone.UvTilingScale    = 0.50f;
        (void)RegisterScatteringEntry(Limestone);
    }

    // Class 6: Multi-stratum metallic sapphire-titanium automotive paint + absorbing polyurethane clearcoat
    {
        SurfaceScatteringEntry Body{};
        Body.ClassificationId             = 6u;
        Body.Scattering.Configuration     = {2u, 0u, 0u, 0u};
        Body.Scattering.Strata[0].AlbedoRoughness     = {0.16f, 0.34f, 0.62f, 0.20f};
        Body.Scattering.Strata[0].Optics              = {0.84f, 1.52f, 1.0f, 0.0f};
        Body.Scattering.Strata[0].Classification      = {0u, 0u, 0u, 0u};
        Body.Scattering.Strata[1].AlbedoRoughness     = {1.0f, 1.0f, 1.0f, 0.052f};
        Body.Scattering.Strata[1].Optics              = {0.0f, 1.54f, 0.96f, 0.0f};
        Body.Scattering.Strata[1].AbsorptionThickness = {0.25f, 0.12f, 0.04f, 0.04f};
        Body.Scattering.Strata[1].Classification      = {1u, 0u, 0u, 0u};

        Body.Paint.Configuration = {1u, 3u, 0u, 2u};
        Body.Paint.Distribution  = {0.68f, 380.0f, 34.0f, 0.65f};
        Body.Paint.Finish        = {0.14f, 0.24f, 0.82f, 0.96f};
        Body.Paint.Relief        = {0.18f, 0.52f, 0.12f, 26.0f};
        Body.Paint.Palette[0]    = {0.14f, 0.32f, 0.64f, 0.0f};
        Body.Paint.Palette[1]    = {0.32f, 0.56f, 0.88f, 1.0f};
        Body.Paint.AverageTint   = {0.20f, 0.40f, 0.72f, 0.0f};
        (void)RegisterScatteringEntry(Body);
    }

    // Class 7: Laminated automotive windshield & side glazing
    {
        SurfaceScatteringEntry Glass{};
        Glass.ClassificationId    = 7u;
        Glass.Scattering          = MakeOneStratum({0.90f, 0.96f, 0.98f}, 0.048f, 0.0f, 1.52f);
        Glass.TransmissiveGlass   = true;
        Glass.GlassIor            = 1.52f;
        Glass.GlassAbsorptionPerM = {0.85f, 0.22f, 0.28f};
        (void)RegisterScatteringEntry(Glass);
    }

    // Class 8 & 9: Anisotropic carbon aero weave & matte chassis trim
    {
        SurfaceScatteringEntry Carbon{};
        Carbon.ClassificationId             = 8u;
        Carbon.Scattering.Configuration     = {2u, 0u, 0u, 0u};
        Carbon.Scattering.Strata[0].AlbedoRoughness     = {0.09f, 0.10f, 0.11f, 0.26f};
        Carbon.Scattering.Strata[0].Optics              = {0.28f, 1.52f, 1.0f, 0.0f};
        Carbon.Scattering.Strata[1].AlbedoRoughness     = {1.0f, 1.0f, 1.0f, 0.07f};
        Carbon.Scattering.Strata[1].Optics              = {0.0f, 1.52f, 0.72f, 0.0f};
        Carbon.Scattering.Strata[1].Classification      = {1u, 0u, 0u, 0u};
        Carbon.Anisotropy                               = 0.45f;
        Carbon.UvTilingScale                            = 4.0f;
        (void)RegisterScatteringEntry(Carbon);

        SurfaceScatteringEntry Trim{};
        Trim.ClassificationId = 9u;
        Trim.Scattering       = MakeOneStratum({0.14f, 0.15f, 0.17f}, 0.46f, 0.15f, 1.50f);
        (void)RegisterScatteringEntry(Trim);
    }

    // Class 10: Brushed machined titanium/alloy multi-spoke wheel rim (anisotropic!)
    {
        SurfaceScatteringEntry Rim{};
        Rim.ClassificationId          = 10u;
        Rim.Scattering                = MakeOneStratum({0.84f, 0.86f, 0.90f}, 0.16f, 0.94f, 1.50f);
        Rim.Anisotropy                = 0.65f;
        Rim.AnisotropyRotationRadians = 0.35f;
        Rim.UvTilingScale             = 3.0f;
        (void)RegisterScatteringEntry(Rim);
    }

    // Class 11: Vulcanized treaded tyre rubber
    {
        SurfaceScatteringEntry Tyre{};
        Tyre.ClassificationId = 11u;
        Tyre.Scattering       = MakeOneStratum({0.065f, 0.068f, 0.072f}, 0.74f, 0.0f, 1.48f);
        Tyre.UvTilingScale    = 2.5f;
        (void)RegisterScatteringEntry(Tyre);
    }

    // Class 12: Polycarbonate headlamp outer lens
    {
        SurfaceScatteringEntry Lens{};
        Lens.ClassificationId    = 12u;
        Lens.Scattering          = MakeOneStratum({0.95f, 0.97f, 1.00f}, 0.045f, 0.0f, 1.58f);
        Lens.TransmissiveGlass   = true;
        Lens.GlassIor            = 1.58f;
        Lens.GlassAbsorptionPerM = {0.15f, 0.12f, 0.08f};
        (void)RegisterScatteringEntry(Lens);
    }

    // Class 13 & 14: Smoked titanium LED headlamp fascia & sculpted crimson taillamp bar
    {
        SurfaceScatteringEntry Headlamp{};
        Headlamp.ClassificationId    = 13u;
        Headlamp.Scattering          = MakeOneStratum({0.68f, 0.72f, 0.78f}, 0.16f, 0.55f, 1.50f);
        Headlamp.EmissiveRadianceRgb = {1.35f, 1.48f, 1.72f};
        (void)RegisterScatteringEntry(Headlamp);

        SurfaceScatteringEntry Taillamp{};
        Taillamp.ClassificationId    = 14u;
        Taillamp.Scattering          = MakeOneStratum({0.86f, 0.08f, 0.08f}, 0.14f, 0.15f, 1.54f);
        Taillamp.EmissiveRadianceRgb = {2.4f, 0.16f, 0.14f};
        (void)RegisterScatteringEntry(Taillamp);
    }

    // Class 15: Dark intake grille
    {
        SurfaceScatteringEntry Grille{};
        Grille.ClassificationId = 15u;
        Grille.Scattering       = MakeOneStratum({0.07f, 0.075f, 0.085f}, 0.52f, 0.35f, 1.50f);
        (void)RegisterScatteringEntry(Grille);
    }

    // Class 16: Interior saddle-leather & Alcantara cabin seats with Charlie fabric sheen
    {
        SurfaceScatteringEntry Cabin{};
        Cabin.ClassificationId                 = 16u;
        Cabin.Scattering                       = MakeOneStratum({0.58f, 0.42f, 0.28f}, 0.62f, 0.0f, 1.48f);
        Cabin.Scattering.Strata[0].Sheen       = {0.82f, 0.68f, 0.52f, 0.48f};
        Cabin.Scattering.Strata[0].SheenFinish = {0.38f, 0.0f, 0.0f, 0.0f};
        Cabin.UvTilingScale                    = 3.0f;
        (void)RegisterScatteringEntry(Cabin);
    }

    // Class 17: Polished 24K gold / copper dynamic harmonic & thermal calibration solids
    {
        SurfaceScatteringEntry Gold{};
        Gold.ClassificationId = 17u;
        Gold.Scattering       = MakeOneStratum({0.96f, 0.76f, 0.34f}, 0.10f, 1.0f, 1.50f);
        Gold.Anisotropy       = 0.25f;
        (void)RegisterScatteringEntry(Gold);
    }

    // Class 18: Solid refractive crystal glass vessel (high-dispersion flint IOR = 1.62)
    {
        SurfaceScatteringEntry Crystal{};
        Crystal.ClassificationId    = 18u;
        Crystal.Scattering          = MakeOneStratum({0.97f, 0.99f, 1.00f}, 0.045f, 0.0f, 1.62f);
        Crystal.TransmissiveGlass   = true;
        Crystal.GlassIor            = 1.62f;
        Crystal.GlassAbsorptionPerM = {0.06f, 0.03f, 0.02f};
        (void)RegisterScatteringEntry(Crystal);
    }

    // Class 19: Candy crimson clearcoat secondary showcase sphere
    {
        SurfaceScatteringEntry Crimson{};
        Crimson.ClassificationId             = 19u;
        Crimson.Scattering.Configuration     = {2u, 0u, 0u, 0u};
        Crimson.Scattering.Strata[0].AlbedoRoughness     = {0.68f, 0.07f, 0.10f, 0.22f};
        Crimson.Scattering.Strata[0].Optics              = {0.88f, 1.52f, 1.0f, 0.0f};
        Crimson.Scattering.Strata[1].AlbedoRoughness     = {1.0f, 1.0f, 1.0f, 0.05f};
        Crimson.Scattering.Strata[1].Optics              = {0.0f, 1.55f, 0.98f, 0.0f};
        Crimson.Scattering.Strata[1].AbsorptionThickness = {0.08f, 1.40f, 1.60f, 0.03f};
        Crimson.Scattering.Strata[1].Classification      = {1u, 0u, 0u, 0u};
        Crimson.Paint.Configuration                      = {1u, 3u, 0u, 2u};
        Crimson.Paint.Distribution                       = {0.62f, 360.0f, 32.0f, 0.60f};
        Crimson.Paint.Finish                             = {0.12f, 0.22f, 0.85f, 0.96f};
        (void)RegisterScatteringEntry(Crimson);
    }

    // Class 20: Warm studio/proving-ground overhead softbox luminaire facet
    {
        SurfaceScatteringEntry Softbox{};
        Softbox.ClassificationId    = 20u;
        Softbox.Scattering          = MakeOneStratum({0.92f, 0.91f, 0.88f}, 0.35f, 0.0f, 1.48f);
        Softbox.EmissiveRadianceRgb = {3.8f, 3.65f, 3.45f};
        (void)RegisterScatteringEntry(Softbox);
    }
}

int32_t SurfaceScatteringAtlas::LoadAndResampleTextureLayer(const std::string& FullPngPath, bool EncodeLinearForHardwareSrgb)
{
    for (size_t Idx = 0u; Idx < LoadedTextureKeys_.size(); ++Idx)
    {
        if (LoadedTextureKeys_[Idx] == FullPngPath)
        {
            return static_cast<int32_t>(Idx);
        }
    }
    if (TextureLayerCount_ >= 48u)
    {
        return -1;
    }

    std::ifstream File(FullPngPath, std::ios::binary | std::ios::ate);
    if (!File.is_open())
    {
        return -1;
    }
    const std::streamsize FileSize = File.tellg();
    if (FileSize <= 0 || FileSize > 32 * 1024 * 1024)
    {
        return -1;
    }
    File.seekg(0, std::ios::beg);
    std::vector<uint8_t> EncodedBytes(static_cast<size_t>(FileSize));
    if (!File.read(reinterpret_cast<char*>(EncodedBytes.data()), FileSize))
    {
        return -1;
    }

    int      SrcW = 0, SrcH = 0, Channels = 0;
    uint8_t* Pixels = stbi_load_from_memory(
        EncodedBytes.data(), static_cast<int>(EncodedBytes.size()), &SrcW, &SrcH, &Channels, 4);
    if (!Pixels || SrcW <= 0 || SrcH <= 0)
    {
        return -1;
    }

    const uint32_t LayerIndex  = TextureLayerCount_++;
    const size_t   LayerPixels = static_cast<size_t>(TextureMapResolution) * TextureMapResolution * 4u;
    PackedTextureBytes_.resize(static_cast<size_t>(TextureLayerCount_) * LayerPixels);
    uint8_t* Dst = PackedTextureBytes_.data() + static_cast<size_t>(LayerIndex) * LayerPixels;

    for (uint32_t Y = 0u; Y < TextureMapResolution; ++Y)
    {
        const float Vf = (static_cast<float>(Y) + 0.5f) / static_cast<float>(TextureMapResolution) * static_cast<float>(SrcH) - 0.5f;
        const int   Y0 = std::clamp(static_cast<int>(std::floor(Vf)), 0, SrcH - 1);
        const int   Y1 = std::min(Y0 + 1, SrcH - 1);
        const float Fy = std::clamp(Vf - static_cast<float>(Y0), 0.0f, 1.0f);

        for (uint32_t X = 0u; X < TextureMapResolution; ++X)
        {
            const float Uf = (static_cast<float>(X) + 0.5f) / static_cast<float>(TextureMapResolution) * static_cast<float>(SrcW) - 0.5f;
            const int   X0 = std::clamp(static_cast<int>(std::floor(Uf)), 0, SrcW - 1);
            const int   X1 = std::min(X0 + 1, SrcW - 1);
            const float Fx = std::clamp(Uf - static_cast<float>(X0), 0.0f, 1.0f);

            const uint8_t* P00 = Pixels + (Y0 * SrcW + X0) * 4;
            const uint8_t* P10 = Pixels + (Y0 * SrcW + X1) * 4;
            const uint8_t* P01 = Pixels + (Y1 * SrcW + X0) * 4;
            const uint8_t* P11 = Pixels + (Y1 * SrcW + X1) * 4;

            uint8_t* OutPx = Dst + (Y * TextureMapResolution + X) * 4u;
            for (int C = 0; C < 3; ++C)
            {
                const float Top = static_cast<float>(P00[C]) + (static_cast<float>(P10[C]) - static_cast<float>(P00[C])) * Fx;
                const float Bot = static_cast<float>(P01[C]) + (static_cast<float>(P11[C]) - static_cast<float>(P01[C])) * Fx;
                const float Val = (Top + (Bot - Top) * Fy) / 255.0f;
                OutPx[C]        = EncodeLinearForHardwareSrgb
                                      ? EncodeLinearScalarForHardwareSrgb(Val)
                                      : static_cast<uint8_t>(std::clamp(std::lround(Val * 255.0f), 0l, 255l));
            }
            OutPx[3] = 255u;
        }
    }

    stbi_image_free(Pixels);
    LoadedTextureKeys_.push_back(FullPngPath);
    return static_cast<int32_t>(LayerIndex);
}

bool SurfaceScatteringAtlas::LoadAuthoredMaterialFile(uint32_t           ClassificationId,
                                                      const std::string& MaterialTomlPath,
                                                      const std::string& TextureContentDirectory)
{
    if (ClassificationId >= kMaxClassificationSlots)
    {
        return false;
    }
    std::ifstream File(MaterialTomlPath, std::ios::binary);
    if (!File.is_open())
    {
        return false;
    }
    std::string SourceText((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
    if (SourceText.empty() || SourceText.size() > 1024u * 1024u)
    {
        return false;
    }

    toml::table Parsed;
    try
    {
        Parsed = toml::parse(SourceText);
    }
    catch (...)
    {
        return false;
    }
    const toml::table& Root = Parsed;

    SurfaceScatteringEntry Entry = Entries_[ClassificationId];
    Entry.ClassificationId       = ClassificationId;
    Entry.NormalStrength         = std::clamp(ReadTomlFloat(Root["NormalStrength"], 1.0f), 0.0f, 4.0f);
    Entry.EmissiveRadianceRgb    = ReadTomlFloatArray<3>(Root["Radiance"], Entry.EmissiveRadianceRgb);
    const auto RootUv            = ReadTomlFloatArray<4>(Root["UvScaleOffset"], {1.0f, 1.0f, 0.0f, 0.0f});
    Entry.UvTilingScale          = std::max(0.05f, 0.5f * (std::abs(RootUv[0]) + std::abs(RootUv[1])));

    std::string AlbedoRel{};
    std::string NormalRel{};
    std::string OrmRel{};
    if (const toml::table* RootTex = Root["Textures"].as_table())
    {
        if (const auto S = (*RootTex)["Albedo"].value<std::string>()) AlbedoRel = *S;
        if (const auto S = (*RootTex)["Normal"].value<std::string>()) NormalRel = *S;
        if (const auto S = (*RootTex)["Orm"].value<std::string>()) OrmRel = *S;
    }

    if (const toml::array* StrataArr = Root["Strata"].as_array(); StrataArr && !StrataArr->empty())
    {
        const uint32_t StratumCount = std::min<uint32_t>(static_cast<uint32_t>(StrataArr->size()), 2u);
        Entry.Scattering.Configuration[0] = StratumCount;

        for (uint32_t SIdx = 0u; SIdx < StratumCount; ++SIdx)
        {
            const toml::table* STbl = StrataArr->get(SIdx) ? StrataArr->get(SIdx)->as_table() : nullptr;
            if (!STbl)
            {
                continue;
            }
            const std::string Category = (*STbl)["Category"].value_or(std::string("Opaque"));
            const bool        IsCoat   = (Category == "Coating");

            const auto  Alb   = ReadTomlFloatArray<3>((*STbl)["Albedo"], {0.5f, 0.5f, 0.5f});
            const float Rough = std::clamp(ReadTomlFloat((*STbl)["Roughness"], IsCoat ? 0.08f : 0.45f), 0.045f, 1.0f);
            const float Met   = IsCoat ? 0.0f : std::clamp(ReadTomlFloat((*STbl)["Metallic"], 0.0f), 0.0f, 1.0f);
            const float Ior   = std::clamp(ReadTomlFloat((*STbl)["Ior"], 1.50f), 1.02f, 3.0f);
            const float Cov   = std::clamp(ReadTomlFloat((*STbl)["Coverage"], 1.0f), 0.0f, 1.0f);

            ScatteringStratum& Dst  = Entry.Scattering.Strata[SIdx];
            Dst.AlbedoRoughness     = {Alb[0], Alb[1], Alb[2], Rough};
            Dst.Optics              = {Met, Ior, Cov, 0.0f};
            Dst.Classification[0]   = IsCoat ? 1u : 0u;

            if (IsCoat)
            {
                const auto  Absorb = ReadTomlFloatArray<3>((*STbl)["Absorption"], {0.0f, 0.0f, 0.0f});
                const float Thick  = std::clamp(ReadTomlFloat((*STbl)["Thickness"], 0.01f), 0.0f, 1.0f);
                Dst.AbsorptionThickness = {Absorb[0], Absorb[1], Absorb[2], Thick};
            }
            else if (SIdx == 0u)
            {
                Entry.Anisotropy                = std::clamp(ReadTomlFloat((*STbl)["Anisotropy"], Entry.Anisotropy), -0.9f, 0.9f);
                Entry.AnisotropyRotationRadians = ReadTomlFloat((*STbl)["Rotation"], Entry.AnisotropyRotationRadians);
                const auto StratumUv            = ReadTomlFloatArray<4>((*STbl)["UvScaleOffset"], {1.0f, 1.0f, 0.0f, 0.0f});
                Entry.UvTilingScale *= std::max(0.05f, 0.5f * (std::abs(StratumUv[0]) + std::abs(StratumUv[1])));

                if (const toml::table* STex = (*STbl)["Textures"].as_table())
                {
                    if (const auto S = (*STex)["Albedo"].value<std::string>()) AlbedoRel = *S;
                    if (const auto S = (*STex)["Normal"].value<std::string>()) NormalRel = *S;
                    if (const auto S = (*STex)["Orm"].value<std::string>()) OrmRel = *S;
                }
            }
        }
    }

    const std::filesystem::path BaseDir(TextureContentDirectory);
    if (!AlbedoRel.empty())
    {
        Entry.AlbedoTextureLayer = LoadAndResampleTextureLayer((BaseDir / AlbedoRel).string(), false);
    }
    if (!NormalRel.empty())
    {
        Entry.NormalTextureLayer = LoadAndResampleTextureLayer((BaseDir / NormalRel).string(), true);
    }
    if (!OrmRel.empty())
    {
        Entry.OrmTextureLayer = LoadAndResampleTextureLayer((BaseDir / OrmRel).string(), true);
    }

    return RegisterScatteringEntry(Entry);
}

uint32_t SurfaceScatteringAtlas::LoadProvingGroundAuthoredMaterials(const std::string& RootOrContentDirectory)
{
    std::vector<std::filesystem::path> CandidateDirs = {
        std::filesystem::path(RootOrContentDirectory) / "MaterialArchives/TextureStudio",
        std::filesystem::path(RootOrContentDirectory) / "Content/MaterialArchives/TextureStudio",
        std::filesystem::path(RootOrContentDirectory) / "EngineContent/MaterialArchives/TextureStudio",
    };

    std::filesystem::path StudioDir{};
    std::error_code       ErrorCode;
    for (const auto& Candidate : CandidateDirs)
    {
        if (std::filesystem::is_directory(Candidate, ErrorCode))
        {
            StudioDir = Candidate;
            break;
        }
    }
    if (StudioDir.empty())
    {
        return 0u;
    }

    struct PresetMapping
    {
        uint32_t    ClassId;
        const char* Filename;
        float       TilingOverride;
    };
    static constexpr std::array<PresetMapping, 9> Mappings = {{
        {0u,  "icelandic-black-sand.material",      0.35f},
        {1u,  "board-form-concrete.material",       0.45f},
        {2u,  "canyon-banded-sandstone.material",   0.65f},
        {3u,  "columnar-olivine-basalt.material",   0.65f},
        {4u,  "weathered-karst-limestone.material", 0.55f},
        {8u,  "carbon-fiber.material",              4.50f},
        {10u, "brushed-titanium.material",          3.50f},
        {11u, "performance-rubber.material",        2.80f},
        {16u, "bull-grain-saddle-leather.material", 3.20f},
    }};

    uint32_t LoadedCount = 0u;
    for (const PresetMapping& Map : Mappings)
    {
        const std::filesystem::path FilePath = StudioDir / Map.Filename;
        if (std::filesystem::is_regular_file(FilePath, ErrorCode))
        {
            if (LoadAuthoredMaterialFile(Map.ClassId, FilePath.string(), StudioDir.string()))
            {
                Entries_[Map.ClassId].UvTilingScale = Map.TilingOverride;
                ++LoadedCount;
            }
        }
    }
    return LoadedCount;
}

const SurfaceScatteringEntry& SurfaceScatteringAtlas::FindEntry(uint32_t ClassificationId) const noexcept
{
    return Entries_[std::min(ClassificationId, kMaxClassificationSlots - 1u)];
}

CompiledSurfaceOptics SurfaceScatteringAtlas::Slot(uint32_t ClassificationId) const noexcept
{
    const SurfaceScatteringEntry& E = FindEntry(ClassificationId);
    return {
        {E.Scattering.Strata[0].AlbedoRoughness[0],
         E.Scattering.Strata[0].AlbedoRoughness[1],
         E.Scattering.Strata[0].AlbedoRoughness[2]},
        E.EmissiveRadianceRgb,
    };
}

ScatteringSpectrum SurfaceScatteringAtlas::EvaluateBrdfCpu(uint32_t             ClassificationId,
                                                           std::array<float, 3> SurfaceNormal,
                                                           std::array<float, 3> ViewDirectionToEye,
                                                           std::array<float, 3> LightDirectionToSource) const noexcept
{
    const SurfaceScatteringEntry& Entry = FindEntry(ClassificationId);
    const double Nv = static_cast<double>(SurfaceNormal[0]) * ViewDirectionToEye[0] +
                      static_cast<double>(SurfaceNormal[1]) * ViewDirectionToEye[1] +
                      static_cast<double>(SurfaceNormal[2]) * ViewDirectionToEye[2];
    const double Nl = static_cast<double>(SurfaceNormal[0]) * LightDirectionToSource[0] +
                      static_cast<double>(SurfaceNormal[1]) * LightDirectionToSource[1] +
                      static_cast<double>(SurfaceNormal[2]) * LightDirectionToSource[2];
    if (Nv <= 0.0 || Nl <= 0.0)
    {
        return {0.0, 0.0, 0.0};
    }
    double Hx = static_cast<double>(ViewDirectionToEye[0]) + LightDirectionToSource[0];
    double Hy = static_cast<double>(ViewDirectionToEye[1]) + LightDirectionToSource[1];
    double Hz = static_cast<double>(ViewDirectionToEye[2]) + LightDirectionToSource[2];
    const double HLen = std::sqrt(Hx * Hx + Hy * Hy + Hz * Hz);
    if (HLen > 1.0e-12)
    {
        Hx /= HLen;
        Hy /= HLen;
        Hz /= HLen;
    }
    const double Nh = std::clamp(SurfaceNormal[0] * Hx + SurfaceNormal[1] * Hy + SurfaceNormal[2] * Hz, 0.0, 1.0);
    const double Vh = std::clamp(ViewDirectionToEye[0] * Hx + ViewDirectionToEye[1] * Hy + ViewDirectionToEye[2] * Hz, 0.0, 1.0);
    return EvaluateScatteringSequence(Entry.Scattering, Nv, Nl, Nh, Vh);
}

void SurfaceScatteringAtlas::PackForGpu(std::vector<std::array<float, 4>>& OutMaterialFloat4Table) const
{
    OutMaterialFloat4Table.clear();
    OutMaterialFloat4Table.reserve(kMaxClassificationSlots * kFloat4StridePerSlot);

    for (uint32_t Idx = 0u; Idx < kMaxClassificationSlots; ++Idx)
    {
        const SurfaceScatteringEntry& E    = Entries_[Idx];
        const ScatteringStratum&      Base = E.Scattering.Strata[0];
        const bool                    HasCoat =
            (E.Scattering.Configuration[0] >= 2u && E.Scattering.Strata[1].Classification[0] == 1u);
        const ScatteringStratum& Coat = E.Scattering.Strata[1];

        const float CoatCov   = HasCoat ? Coat.Optics[2] : 0.0f;
        const float CoatRough = HasCoat ? Coat.AlbedoRoughness[3] : 0.08f;
        const float CoatIor   = HasCoat ? Coat.Optics[1] : 1.50f;
        const float CoatThick = HasCoat ? Coat.AbsorptionThickness[3] : 0.0f;

        const float FlakeCov  = (E.Paint.Configuration[0] != 0u) ? E.Paint.Distribution[0] : 0.0f;
        const float FlakeFreq = (E.Paint.Configuration[0] != 0u) ? E.Paint.Distribution[1] : 1.0f;
        const float FlakeTilt = (E.Paint.Configuration[0] != 0u) ? std::min(0.032f, E.Paint.Finish[0] * 0.22f) : 0.0f;

        OutMaterialFloat4Table.push_back(Base.AlbedoRoughness);
        OutMaterialFloat4Table.push_back({Base.Optics[0], Base.Optics[1], CoatCov, CoatRough});
        OutMaterialFloat4Table.push_back({HasCoat ? Coat.AbsorptionThickness[0] * CoatThick : 0.0f,
                                          HasCoat ? Coat.AbsorptionThickness[1] * CoatThick : 0.0f,
                                          HasCoat ? Coat.AbsorptionThickness[2] * CoatThick : 0.0f,
                                          CoatIor});
        OutMaterialFloat4Table.push_back(Base.Sheen);
        OutMaterialFloat4Table.push_back({E.EmissiveRadianceRgb[0],
                                          E.EmissiveRadianceRgb[1],
                                          E.EmissiveRadianceRgb[2],
                                          Base.SheenFinish[0]});
        OutMaterialFloat4Table.push_back({FlakeCov,
                                          FlakeFreq,
                                          E.TransmissiveGlass ? 1.0f : 0.0f,
                                          E.GlassIor});
        OutMaterialFloat4Table.push_back({E.Anisotropy,
                                          E.AnisotropyRotationRadians,
                                          FlakeTilt,
                                          std::max(0.05f, E.UvTilingScale)});
        OutMaterialFloat4Table.push_back({static_cast<float>(E.AlbedoTextureLayer),
                                          static_cast<float>(E.NormalTextureLayer),
                                          static_cast<float>(E.OrmTextureLayer),
                                          E.NormalStrength});
    }
}

void SurfaceScatteringAtlas::PackTextureMapsForGpu(uint32_t&             OutWidth,
                                                   uint32_t&             OutHeight,
                                                   uint32_t&             OutLayerCount,
                                                   std::vector<uint8_t>& OutRgbaBytes) const
{
    OutWidth  = TextureMapResolution;
    OutHeight = TextureMapResolution;
    if (TextureLayerCount_ == 0u || PackedTextureBytes_.empty())
    {
        OutLayerCount = 1u;
        OutRgbaBytes.assign(static_cast<size_t>(TextureMapResolution) * TextureMapResolution * 4u, 255u);
        return;
    }
    OutLayerCount = TextureLayerCount_;
    OutRgbaBytes  = PackedTextureBytes_;
}

} // namespace Frontier
