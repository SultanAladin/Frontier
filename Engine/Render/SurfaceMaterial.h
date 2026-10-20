/*==========================================================================================================================================
                                                          SURFACEMATERIAL.H
==========================================================================================================================================*/
// 🧩 Bounded authored material definitions, typed texture pyramids and immutable shader parameter compilation.

#pragma once
#include "ScatteringSequence.h"
#include "AutomotivePaint.h"
#include <filesystem>
#include <span>
#include <string_view>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL AND SHADER CONTRACT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each stratum selects albedo/ORM/normal array slices; radiance remains material-wide.
// The appended fields preserve the lighting prefix while extending the authored material ABI.
struct alignas(16) MaterialScattering
{
    PreviewScattering Illumination;
    std::array<float,4> UvScaleOffset{1,1,0,0};
    std::array<float,4> RadianceNormal{0,0,0,1};
    std::array<std::array<uint32_t,4>,8> TextureUse{};
    std::array<std::array<float,4>,8> StratumUv = []
    {
        std::array<std::array<float,4>,8> Coordinates{};
        Coordinates.fill({1,1,0,0});
        return Coordinates;
    }();
    std::array<std::array<float,4>,8> StratumNormal = []
    {
        std::array<std::array<float,4>,8> Normals{};
        Normals.fill({1,1,0,0});
        return Normals;
    }();
    std::array<std::array<float,4>,8> DirectionalOptics = []
    {
        std::array<std::array<float,4>,8> Optics{};
        Optics.fill({0,0,0,.5f});
        return Optics;
    }(); // [-,rad,enum,0-1] - anisotropy, rotation, exact Fresnel, dielectric specular level
    AutomotivePaint Paint;
    std::array<uint32_t, 4> PreviewConfiguration{}; // Runtime-only specimen classification and studio lighting; reserved zero.
};
static_assert(sizeof(MaterialScattering) == 1504);
static_assert(offsetof(MaterialScattering,Paint) == 1216);
static_assert(offsetof(MaterialScattering,UvScaleOffset) == 672);
static_assert(offsetof(MaterialScattering,TextureUse) == 704);
static_assert(offsetof(MaterialScattering,StratumUv) == 832);
static_assert(offsetof(MaterialScattering,StratumNormal) == 960);
static_assert(offsetof(MaterialScattering,DirectionalOptics) == 1088);

enum class MaterialTextureCategory : uint32_t
{
    Albedo = 0,
    OcclusionRoughnessMetallic = 1,
    TangentNormal = 2,
    Radiance = 3
};

struct MaterialDefinition
{
    std::string Name = "Untitled material";
    MaterialScattering Scattering;
    std::array<std::string,4> TextureSources;
    std::array<std::array<std::string,3>,8> StratumTextureSources;
};

struct MaterialMipRegion
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    size_t Offset = 0;
};

struct MaterialTexturePyramid
{
    MaterialTextureCategory Category = MaterialTextureCategory::Albedo;
    std::vector<uint8_t> Rgba;
    std::vector<MaterialMipRegion> Levels;
    uint32_t Layers = 1;
    std::vector<float> FloatRgba;
};

enum class MaterialAuthoringMap : uint32_t
{
    Colour = 0,
    TangentNormal = 1,
    Height = 2,
    Roughness = 3,
    Metalness = 4,
    Occlusion = 5
};

struct CompiledSurfaceMaterial
{
    std::string Name;
    MaterialScattering Scattering;
    std::array<MaterialTexturePyramid,4> Textures;
    std::array<MaterialTexturePyramid,6> AuthoringMaps;
};

inline bool ValidateMaterialScattering(const MaterialScattering &Material)
{
    if (Material.PreviewConfiguration[0] > 4 || Material.PreviewConfiguration[1] > 1 || Material.PreviewConfiguration[2] || Material.PreviewConfiguration[3]) return false;
    if (!ValidateAutomotivePaint(Material.Paint)) return false;
    if (!ValidatePreviewScattering(Material.Illumination) || Material.Illumination.Configuration[1] != 0) return false;
    for (float Coefficient : Material.UvScaleOffset)
        if (!std::isfinite(Coefficient) || std::abs(Coefficient) > 1024) return false;
    for (size_t Channel = 0; Channel < 4; ++Channel)
        if (!std::isfinite(Material.RadianceNormal[Channel]) || Material.RadianceNormal[Channel] < 0 ||
            Material.RadianceNormal[Channel] > (Channel == 3 ? 4 : 64)) return false;
    for (size_t Index = 0; Index < 8; ++Index)
    {
        const auto &Selection = Material.TextureUse[Index];
        if (Selection[0] > 15 || Selection[1] > 7 || Selection[2] > 7 || Selection[3] > 7) return false;
        for (float Coefficient : Material.StratumUv[Index])
            if (!std::isfinite(Coefficient) || std::abs(Coefficient) > 1024) return false;
        const auto &Normal = Material.StratumNormal[Index];
        if (!std::isfinite(Normal[0]) || Normal[0] < 0 || Normal[0] > 4 ||
            !std::isfinite(Normal[1]) || Normal[1] < 0 || Normal[1] > 4 || Normal[2] || Normal[3]) return false;
        const auto &Optics = Material.DirectionalOptics[Index];
        for (float Coefficient : Optics) if (!std::isfinite(Coefficient)) return false;
        if (std::abs(Optics[0]) > .9f || std::abs(Optics[1]) > 100 || (Optics[2] != 0 && Optics[2] != 1) ||
            Optics[3] < 0 || Optics[3] > 1) return false;
        if (Index < Material.Illumination.Configuration[0] && Optics[2] == 1 && Material.Illumination.Strata[Index].Optics[0] != 0) return false;
    }
    return true;
}

MaterialDefinition DecodeSurfaceMaterial(std::string_view Text);
std::string EncodeSurfaceMaterial(const MaterialDefinition &Material);
MaterialTexturePyramid ConstructMaterialPyramid(const SurfaceTextureImage &Image,MaterialTextureCategory Category);
inline bool ValidateMaterialPyramid(const MaterialTexturePyramid &Texture)
{
    if (uint32_t(Texture.Category) > 3 || Texture.Levels.empty() || Texture.Levels.size() > 13 || !Texture.Layers || Texture.Layers > 8 ||
        (Texture.Rgba.empty() == Texture.FloatRgba.empty())) return false;
    const size_t BytesPerComponent = Texture.FloatRgba.empty() ? sizeof(uint8_t) : sizeof(float);
    uint32_t Width = Texture.Levels[0].Width, Height = Texture.Levels[0].Height;
    if (!Width || !Height || Width > 4096 || Height > 4096) return false;
    size_t Offset = 0;
    for (const auto &Level : Texture.Levels)
    {
        if (Level.Width != Width || Level.Height != Height || Level.Offset != Offset) return false;
        if (Width == 1 && Height == 1 && &Level != &Texture.Levels.back()) return false;
        Offset += size_t(Width) * Height * 4 * Texture.Layers * BytesPerComponent;
        Width = std::max(1u, Width / 2);
        Height = std::max(1u, Height / 2);
    }
    if (Texture.Levels.back().Width != 1 || Texture.Levels.back().Height != 1 ||
        Offset != (Texture.FloatRgba.empty() ? Texture.Rgba.size() : Texture.FloatRgba.size() * sizeof(float))) return false;
    return std::all_of(Texture.FloatRgba.begin(), Texture.FloatRgba.end(), [](float Sample) { return std::isfinite(Sample); });
}

MaterialTexturePyramid ConstructFloatMaterialPyramid(uint32_t Width, uint32_t Height,
    MaterialTextureCategory Category, std::span<const float> FloatRgba);
std::array<MaterialTexturePyramid,6> ConstructAuthoringMaterialMaps(uint32_t Width, uint32_t Height,
    const std::array<std::span<const float>,6> &FloatRgba);

CompiledSurfaceMaterial CompileSurfaceMaterial(const MaterialDefinition &Material,const std::filesystem::path &ContentDirectory);
} // namespace Frontier
