/*==========================================================================================================================================
                                                          SURFACEMATERIAL.CPP
==========================================================================================================================================*/
// 🧩 Strict TOML material authoring, contained PNG/JPEG ingestion and colour-correct/normal-aware mip compilation.

#include "SurfaceMaterial.h"
#include <toml++/toml.hpp>
#include <fstream>
#include <sstream>
#include <memory>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Frontier
{
namespace
{
void ValidateKeys(const toml::table &Table,std::initializer_list<std::string_view> Allowed)
{
    for (const auto &[Key,Content] : Table)
    {
        (void)Content;
        if (std::find(Allowed.begin(),Allowed.end(),Key.str()) == Allowed.end())
            throw std::runtime_error("Unsupported material field: "+std::string(Key.str()));
    }
}

template<size_t Count>
std::array<float,Count> DecodeSpectrum(const toml::node_view<toml::node> &Field,std::array<float,Count> Fallback)
{
    if (!Field) return Fallback;
    const auto *Channels = Field.as_array();
    if (!Channels || Channels->size() != Count) throw std::runtime_error("Material vector has the wrong channel count.");
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const auto Coefficient = Channels->get(Index)->value<double>();
        if (!Coefficient || !std::isfinite(*Coefficient) || std::abs(*Coefficient) > 1e6)
            throw std::runtime_error("Material vector channels must be finite bounded numbers.");
        Fallback[Index] = float(*Coefficient);
    }
    return Fallback;
}

float DecodeCoefficient(const toml::node_view<toml::node> &Field,float Fallback)
{
    if (!Field) return Fallback;
    const auto Coefficient = Field.value<double>();
    if (!Coefficient || !std::isfinite(*Coefficient) || std::abs(*Coefficient) > 1e6)
        throw std::runtime_error("Material coefficient must be a finite bounded number.");
    return float(*Coefficient);
}

template<size_t Count>
toml::array EncodeSpectrum(const std::array<float,Count> &Channels)
{
    toml::array Result;
    for (const auto Channel : Channels) Result.push_back(double(Channel));
    return Result;
}

double DecodeSrgb(double Encoded)
{
    return Encoded <= .04045 ? Encoded/12.92 : std::pow((Encoded+.055)/1.055,2.4);
}
double EncodeSrgb(double Linear)
{
    return Linear <= .0031308 ? Linear*12.92 : 1.055*std::pow(Linear,1/2.4)-.055;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     STRICT AUTHORED TOML
//------------------------------------------------------------------------------------------------------------------------

MaterialDefinition DecodeSurfaceMaterial(std::string_view Text)
{
    if (Text.size() > 1048576) throw std::runtime_error("Material TOML exceeds 1 MiB.");
    auto Table = toml::parse(Text);
    ValidateKeys(Table,{"Format","Name","UvScaleOffset","Radiance","NormalStrength","Textures","Strata","AutomotivePaint"});
    if (Table["Format"].value<int64_t>() != 1) throw std::runtime_error("Unsupported material format; expected Format = 1.");
    MaterialDefinition Material;
    const auto Name = Table["Name"].value<std::string>();
    if (!Name || Name->empty() || Name->size() > 128) throw std::runtime_error("Material Name must contain 1..128 bytes.");
    Material.Name = *Name;
    Material.Scattering.UvScaleOffset = DecodeSpectrum(Table["UvScaleOffset"],Material.Scattering.UvScaleOffset);
    const auto Radiance = DecodeSpectrum<3>(Table["Radiance"],{0,0,0});
    std::copy(Radiance.begin(),Radiance.end(),Material.Scattering.RadianceNormal.begin());
    Material.Scattering.RadianceNormal[3] = DecodeCoefficient(Table["NormalStrength"],1);
    if (Table.contains("Textures"))
    {
        const auto *Textures = Table["Textures"].as_table();
        if (!Textures) throw std::runtime_error("Textures must be a table.");
        ValidateKeys(*Textures,{"Albedo","Orm","Normal","Radiance"});
        const std::array<const char *,4> Names{"Albedo","Orm","Normal","Radiance"};
        for (size_t Index = 0; Index < Names.size(); ++Index)
            if (Textures->contains(Names[Index]))
            {
                const auto Source = (*Textures)[Names[Index]].value<std::string>();
                if (!Source || Source->empty() || Source->size() > 512) throw std::runtime_error("Invalid material texture path.");
                Material.TextureSources[Index] = *Source;
            }
    }
    if (Table.contains("AutomotivePaint"))
    {
        auto *PaintTable = Table["AutomotivePaint"].as_table();
        if (!PaintTable) throw std::runtime_error("AutomotivePaint must be a table.");
        ValidateKeys(*PaintTable, {"Enabled", "Layers", "ColourMode", "Distribution", "Finish", "Relief", "Palette"});
        auto &Paint = Material.Scattering.Paint;
        const auto Enabled = (*PaintTable)["Enabled"].value<bool>();
        const auto Layers = (*PaintTable)["Layers"].value<int64_t>();
        const auto Mode = (*PaintTable)["ColourMode"].value<std::string>();
        if (!Enabled || !Layers || *Layers < 1 || *Layers > 4 || !Mode ||
            (*Mode != "Palette" && *Mode != "Ramp" && *Mode != "Single"))
            throw std::runtime_error("Paint requires Enabled, Layers (1..4) and ColourMode (Palette, Ramp or Single).");
        Paint.Configuration[0] = *Enabled ? 1 : 0;
        Paint.Configuration[1] = uint32_t(*Layers);
        Paint.Configuration[2] = *Mode == "Single" ? 2 : *Mode == "Ramp" ? 1 : 0;
        Paint.Distribution = DecodeSpectrum<4>((*PaintTable)["Distribution"], Paint.Distribution);
        Paint.Finish = DecodeSpectrum<4>((*PaintTable)["Finish"], Paint.Finish);
        Paint.Relief = DecodeSpectrum<4>((*PaintTable)["Relief"], Paint.Relief);
        auto *Palette = (*PaintTable)["Palette"].as_array();
        if (!Palette || Palette->empty() || Palette->size() > 12) throw std::runtime_error("Paint requires 1..12 linear RGBA palette stops.");
        Paint.Configuration[3] = uint32_t(Palette->size());
        for (size_t Index = 0; Index < Palette->size(); ++Index)
            Paint.Palette[Index] = DecodeSpectrum<4>(toml::node_view<toml::node>(Palette->get(Index)), {});
    }
    auto *Strata = Table["Strata"].as_array();
    if (!Strata || Strata->empty() || Strata->size() > 8) throw std::runtime_error("Material requires 1..8 ordered Strata.");
    Material.Scattering.Illumination.Configuration[0] = uint32_t(Strata->size());
    for (size_t Index = 0; Index < Strata->size(); ++Index)
    {
        auto *Stratum = Strata->get(Index)->as_table();
        if (!Stratum) throw std::runtime_error("Stratum must be a table.");
        ValidateKeys(*Stratum,{"Category","Albedo","Roughness","Metallic","Ior","Coverage","Absorption","Thickness","TextureMask","Textures","UvScaleOffset","NormalStrength","Anisotropy","Rotation","Fresnel"});
        const auto Category = (*Stratum)["Category"].value<std::string>();
        if (!Category || (*Category != "Opaque" && *Category != "Coating"))
            throw std::runtime_error("Unsupported scattering Category; supported: Opaque, Coating. Transmission/SSS are not implemented.");
        auto &Compiled = Material.Scattering.Illumination.Strata[Index];
        Compiled.Classification[0] = *Category == "Coating" ? 1 : 0;
        const auto Albedo = DecodeSpectrum<3>((*Stratum)["Albedo"],{.5f,.5f,.5f});
        std::copy(Albedo.begin(),Albedo.end(),Compiled.AlbedoRoughness.begin());
        Compiled.AlbedoRoughness[3] = DecodeCoefficient((*Stratum)["Roughness"],.4f);
        Compiled.Optics = {DecodeCoefficient((*Stratum)["Metallic"],0),DecodeCoefficient((*Stratum)["Ior"],1.5f),
                           DecodeCoefficient((*Stratum)["Coverage"],1),0};
        const auto Absorption = DecodeSpectrum<3>((*Stratum)["Absorption"],{0,0,0});
        std::copy(Absorption.begin(),Absorption.end(),Compiled.AbsorptionThickness.begin());
        Compiled.AbsorptionThickness[3] = DecodeCoefficient((*Stratum)["Thickness"],0);
        const auto Mask = (*Stratum)["TextureMask"].value<int64_t>();
        if ((*Stratum).contains("TextureMask") && (!Mask || *Mask < 0 || *Mask > 15))
            throw std::runtime_error("TextureMask must be an integer within 0..15.");
        Material.Scattering.TextureUse[Index][0] = uint32_t(Mask.value_or(0));
        Material.Scattering.StratumUv[Index] = DecodeSpectrum<4>((*Stratum)["UvScaleOffset"],{1,1,0,0});
        Material.Scattering.StratumNormal[Index][0] = DecodeCoefficient((*Stratum)["NormalStrength"],1);
        auto &Directional = Material.Scattering.DirectionalOptics[Index];
        Directional[0] = DecodeCoefficient((*Stratum)["Anisotropy"],0);
        Directional[1] = DecodeCoefficient((*Stratum)["Rotation"],0);
        if ((*Stratum).contains("Fresnel"))
        {
            const auto Fresnel = (*Stratum)["Fresnel"].value<std::string>();
            if (!Fresnel || (*Fresnel != "Schlick" && *Fresnel != "Dielectric"))
                throw std::runtime_error("Fresnel must be Schlick or Dielectric; exact conductor optical constants are not implemented.");
            Directional[2] = *Fresnel == "Dielectric" ? 1.f : 0.f;
        }
        if ((*Stratum).contains("Textures"))
        {
            const auto *Textures = (*Stratum)["Textures"].as_table();
            if (!Textures) throw std::runtime_error("Stratum Textures must be a table.");
            ValidateKeys(*Textures,{"Albedo","Orm","Normal"});
            const std::array<const char *,3> Names{"Albedo","Orm","Normal"};
            for (size_t Slot = 0; Slot < 3; ++Slot)
                if (Textures->contains(Names[Slot]))
                {
                    const auto Source = (*Textures)[Names[Slot]].value<std::string>();
                    if (!Source || Source->empty() || Source->size() > 512) throw std::runtime_error("Invalid stratum texture path.");
                    Material.StratumTextureSources[Index][Slot] = *Source;
                }
        }
    }
    if (!ValidateMaterialScattering(Material.Scattering)) throw std::runtime_error("Material coefficients violate scattering limits.");
    return Material;
}

std::string EncodeSurfaceMaterial(const MaterialDefinition &Material)
{
    if (!ValidateMaterialScattering(Material.Scattering) || Material.Name.empty() || Material.Name.size() > 128)
        throw std::runtime_error("Cannot encode invalid material.");
    toml::table Table{{"Format",1},{"Name",Material.Name},{"UvScaleOffset",EncodeSpectrum(Material.Scattering.UvScaleOffset)},
        {"Radiance",EncodeSpectrum<3>({Material.Scattering.RadianceNormal[0],Material.Scattering.RadianceNormal[1],Material.Scattering.RadianceNormal[2]})},
        {"NormalStrength",double(Material.Scattering.RadianceNormal[3])}};
    const auto &Paint = Material.Scattering.Paint;
    if (Paint.Configuration[3])
    {
        toml::array Palette;
        for (size_t Index = 0; Index < Paint.Configuration[3]; ++Index) Palette.push_back(EncodeSpectrum(Paint.Palette[Index]));
        Table.insert("AutomotivePaint", toml::table{{"Enabled", bool(Paint.Configuration[0])},
            {"Layers", int64_t(Paint.Configuration[1])}, {"ColourMode", Paint.Configuration[2] == 2 ? "Single" : Paint.Configuration[2] == 1 ? "Ramp" : "Palette"},
            {"Distribution", EncodeSpectrum(Paint.Distribution)}, {"Finish", EncodeSpectrum(Paint.Finish)},
            {"Relief", EncodeSpectrum(Paint.Relief)}, {"Palette", std::move(Palette)}});
    }
    toml::table Textures;
    const std::array<const char *,4> Names{"Albedo","Orm","Normal","Radiance"};
    for (size_t Index = 0; Index < 4; ++Index)
        if (!Material.TextureSources[Index].empty()) Textures.insert(Names[Index],Material.TextureSources[Index]);
    Table.insert("Textures",std::move(Textures));
    toml::array Strata;
    for (size_t Index = 0; Index < Material.Scattering.Illumination.Configuration[0]; ++Index)
    {
        const auto &Stratum = Material.Scattering.Illumination.Strata[Index];
        Strata.push_back(toml::table{{"Category",Stratum.Classification[0] ? "Coating" : "Opaque"},
            {"Albedo",EncodeSpectrum<3>({Stratum.AlbedoRoughness[0],Stratum.AlbedoRoughness[1],Stratum.AlbedoRoughness[2]})},
            {"Roughness",double(Stratum.AlbedoRoughness[3])},{"Metallic",double(Stratum.Optics[0])},
            {"Ior",double(Stratum.Optics[1])},{"Coverage",double(Stratum.Optics[2])},
            {"Absorption",EncodeSpectrum<3>({Stratum.AbsorptionThickness[0],Stratum.AbsorptionThickness[1],Stratum.AbsorptionThickness[2]})},
            {"Thickness",double(Stratum.AbsorptionThickness[3])},{"TextureMask",int64_t(Material.Scattering.TextureUse[Index][0])}});
    }
    for (size_t Index = 0; Index < Material.Scattering.Illumination.Configuration[0]; ++Index)
    {
        auto &Stratum = *Strata.get(Index)->as_table();
        Stratum.insert("UvScaleOffset",EncodeSpectrum(Material.Scattering.StratumUv[Index]));
        Stratum.insert("NormalStrength",double(Material.Scattering.StratumNormal[Index][0]));
        Stratum.insert("Anisotropy",double(Material.Scattering.DirectionalOptics[Index][0]));
        Stratum.insert("Rotation",double(Material.Scattering.DirectionalOptics[Index][1]));
        Stratum.insert("Fresnel",Material.Scattering.DirectionalOptics[Index][2] == 1 ? "Dielectric" : "Schlick");
        toml::table Textures;
        const std::array<const char *,3> Names{"Albedo","Orm","Normal"};
        for (size_t Slot = 0; Slot < 3; ++Slot)
            if (!Material.StratumTextureSources[Index][Slot].empty())
                Textures.insert(Names[Slot],Material.StratumTextureSources[Index][Slot]);
        if (!Textures.empty()) Stratum.insert("Textures",std::move(Textures));
    }
    Table.insert("Strata",std::move(Strata));
    std::ostringstream Output;
    Output << toml::toml_formatter{Table};
    return Output.str();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     COLOUR-CORRECT TEXTURE PYRAMIDS
//------------------------------------------------------------------------------------------------------------------------

MaterialTexturePyramid ConstructMaterialPyramid(const SurfaceTextureImage &Image,MaterialTextureCategory Category)
{
    if (!ValidateSurfaceTextureImage(Image) || uint32_t(Category) > 3) throw std::runtime_error("Invalid material texture image.");
    MaterialTexturePyramid Pyramid{Category,Image.Rgba,{{Image.Width,Image.Height,0}}};
    uint32_t Width = Image.Width, Height = Image.Height;
    while (Width > 1 || Height > 1)
    {
        const auto Previous = Pyramid.Levels.back();
        const uint32_t Columns = std::max(1u,Width/2), Rows = std::max(1u,Height/2);
        const size_t Start = Pyramid.Rgba.size();
        Pyramid.Levels.push_back({Columns,Rows,Start});
        Pyramid.Rgba.resize(Start+size_t(Columns)*Rows*4);
        for (uint32_t Row = 0; Row < Rows; ++Row)
            for (uint32_t Column = 0; Column < Columns; ++Column)
            {
                std::array<double,4> Spectrum{};
                double Total = 0;
                const double Left = double(Column)*Width/Columns, Right = double(Column+1)*Width/Columns;
                const double Top = double(Row)*Height/Rows, Bottom = double(Row+1)*Height/Rows;
                for (uint32_t Vertical = uint32_t(Top); Vertical < uint32_t(std::ceil(Bottom)); ++Vertical)
                    for (uint32_t Horizontal = uint32_t(Left); Horizontal < uint32_t(std::ceil(Right)); ++Horizontal)
                    {
                        const double Weight = (std::min(Right,double(Horizontal+1))-std::max(Left,double(Horizontal)))*
                                              (std::min(Bottom,double(Vertical+1))-std::max(Top,double(Vertical)));
                        for (size_t Channel = 0; Channel < 4; ++Channel)
                        {
                            double Coefficient = Pyramid.Rgba[Previous.Offset+(size_t(Vertical)*Width+Horizontal)*4+Channel]/255.0;
                            if (Channel < 3 && (Category == MaterialTextureCategory::Albedo || Category == MaterialTextureCategory::Radiance))
                                Coefficient = DecodeSrgb(Coefficient);
                            if (Channel < 3 && Category == MaterialTextureCategory::TangentNormal) Coefficient = Coefficient*2-1;
                            Spectrum[Channel] += Coefficient*Weight;
                        }
                        Total += Weight;
                    }
                for (auto &Channel : Spectrum) Channel /= Total;
                if (Category == MaterialTextureCategory::TangentNormal)
                {
                    const double Length = std::sqrt(Spectrum[0]*Spectrum[0]+Spectrum[1]*Spectrum[1]+Spectrum[2]*Spectrum[2]);
                    for (size_t Channel = 0; Channel < 3; ++Channel)
                        Spectrum[Channel] = (Length > 1e-8 ? Spectrum[Channel]/Length : double(Channel == 2))*.5+.5;
                }
                for (size_t Channel = 0; Channel < 4; ++Channel)
                {
                    double Coefficient = Spectrum[Channel];
                    if (Channel < 3 && (Category == MaterialTextureCategory::Albedo || Category == MaterialTextureCategory::Radiance))
                        Coefficient = EncodeSrgb(Coefficient);
                    Pyramid.Rgba[Start+(size_t(Row)*Columns+Column)*4+Channel] = uint8_t(std::lround(std::clamp(Coefficient,0.0,1.0)*255));
                }
            }
        Width = Columns;
        Height = Rows;
    }
    return Pyramid;
}

CompiledSurfaceMaterial CompileSurfaceMaterial(const MaterialDefinition &Material,const std::filesystem::path &ContentDirectory)
{
    if (!ValidateMaterialScattering(Material.Scattering)) throw std::runtime_error("Material scattering cannot be compiled.");
    CompiledSurfaceMaterial Compiled{Material.Name,Material.Scattering,{}};
    auto &Paint = Compiled.Scattering.Paint;
    Paint.AverageTint = {};
    if (Paint.Configuration[0])
        for (uint32_t Sample = 0; Sample < 48; ++Sample)
        {
            const float Position = (float(Sample) + .5f) / 48;
            auto Spectrum = Paint.Palette[0];
            if (Paint.Configuration[2] == 0) Spectrum = Paint.Palette[std::min(Paint.Configuration[3] - 1, uint32_t(Position * Paint.Configuration[3]))];
            else if (Paint.Configuration[2] == 1)
                for (size_t Index = 1; Index < Paint.Configuration[3]; ++Index)
                {
                    const float Blend = std::clamp((Position - Paint.Palette[Index - 1][3]) /
                        std::max(.0001f, Paint.Palette[Index][3] - Paint.Palette[Index - 1][3]), 0.f, 1.f);
                    for (size_t Channel = 0; Channel < 3; ++Channel) Spectrum[Channel] = std::lerp(Spectrum[Channel], Paint.Palette[Index][Channel], Blend);
                }
            for (size_t Channel = 0; Channel < 3; ++Channel) Paint.AverageTint[Channel] += Spectrum[Channel] / 48;
        }
    const std::array<std::array<uint8_t,4>,4> Defaults{{{255,255,255,255},{255,255,255,255},{128,128,255,255},{255,255,255,255}}};
    const auto Directory = std::filesystem::weakly_canonical(ContentDirectory);
    const auto DecodeTexture = [&](const std::string &Source,size_t Slot)
    {
        SurfaceTextureImage Image{1,1,{Defaults[Slot].begin(),Defaults[Slot].end()}};
        if (!Source.empty())
        {
            const std::filesystem::path Relative(Source);
            if (Relative.is_absolute() || Source.find(':') != std::string::npos || Source.find('\\') != std::string::npos)
                throw std::runtime_error("Material texture paths must be portable ContentDirectory-relative paths.");
            const auto Path = std::filesystem::weakly_canonical(Directory/Relative);
            const auto Within = Path.lexically_relative(Directory);
            if (Within.empty() || *Within.begin() == "..") throw std::runtime_error("Material texture escapes ContentDirectory.");
            std::ifstream Input(Path,std::ios::binary | std::ios::ate);
            const auto Bytes = Input.tellg();
            if (!Input || Bytes <= 0 || Bytes > 67108864) throw std::runtime_error("Missing/oversized material texture: "+Source);
            std::vector<uint8_t> Encoded(static_cast<size_t>(Bytes));
            Input.seekg(0);
            Input.read(reinterpret_cast<char *>(Encoded.data()),Bytes);
            int Width = 0, Height = 0, Channels = 0;
            if (!Input || !stbi_info_from_memory(Encoded.data(),int(Bytes),&Width,&Height,&Channels) ||
                Width < 1 || Height < 1 || Width > 4096 || Height > 4096)
                throw std::runtime_error("Unsupported/oversized PNG or JPEG material texture: "+Source);
            std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> Pixels(stbi_load_from_memory(Encoded.data(),int(Bytes),&Width,&Height,&Channels,4),stbi_image_free);
            if (!Pixels) throw std::runtime_error("Material texture decode failed: "+Source);
            Image = {uint32_t(Width),uint32_t(Height),std::vector<uint8_t>(Pixels.get(),Pixels.get()+size_t(Width)*Height*4)};
        }
        return ConstructMaterialPyramid(Image,MaterialTextureCategory(Slot));
    };
    size_t TotalBytes = 0;
    size_t ResidentBytes = 0;
    constexpr size_t MaximumBytes = 268435456;
    for (size_t Slot = 0; Slot < 4; ++Slot)
    {
        std::vector<std::string> Sources;
        std::vector<MaterialTexturePyramid> Slices;
        const size_t Count = Slot == 3 ? 1 : Material.Scattering.Illumination.Configuration[0];
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const auto &Source = Slot < 3 && !Material.StratumTextureSources[Index][Slot].empty() ?
                Material.StratumTextureSources[Index][Slot] : Material.TextureSources[Slot];
            auto Position = std::find(Sources.begin(),Sources.end(),Source);
            size_t Slice = size_t(Position-Sources.begin());
            if (Position == Sources.end())
            {
                auto Texture = DecodeTexture(Source,Slot);
                if (Texture.Rgba.size() > MaximumBytes-TotalBytes) throw std::runtime_error("Material texture compilation exceeds 256 MiB.");
                TotalBytes += Texture.Rgba.size();
                Sources.push_back(Source);
                Slices.push_back(std::move(Texture));
            }
            if (Slot < 3) Compiled.Scattering.TextureUse[Index][Slot+1] = uint32_t(Slice);
        }
        uint32_t Width = 1, Height = 1;
        for (const auto &Slice : Slices)
        {
            Width = std::max(Width,Slice.Levels[0].Width);
            Height = std::max(Height,Slice.Levels[0].Height);
        }
        for (const auto &Slice : Slices)
            if ((Slice.Levels[0].Width != Width || Slice.Levels[0].Height != Height) &&
                (Slice.Levels[0].Width != 1 || Slice.Levels[0].Height != 1))
                throw std::runtime_error("Stratum textures in each slot must share dimensions; neutral 1x1 images may expand.");
        MaterialTexturePyramid Combined;
        Combined.Category = MaterialTextureCategory(Slot);
        Combined.Layers = uint32_t(Slices.size());
        size_t RequiredBytes = 0;
        for (uint32_t Columns = Width, Rows = Height;; Columns = std::max(1u,Columns/2), Rows = std::max(1u,Rows/2))
        {
            RequiredBytes += size_t(Columns)*Rows*4*Slices.size();
            if (Columns == 1 && Rows == 1) break;
        }
        if (RequiredBytes > MaximumBytes-ResidentBytes)
            throw std::runtime_error("Compiled material texture arrays exceed 256 MiB.");
        Combined.Rgba.reserve(RequiredBytes);
        size_t Level = 0;
        while (true)
        {
            Combined.Levels.push_back({Width,Height,Combined.Rgba.size()});
            const size_t Bytes = size_t(Width)*Height*4;
            if (Combined.Rgba.size()+Bytes*Slices.size() > MaximumBytes)
                throw std::runtime_error("Material array texture exceeds 256 MiB.");
            for (const auto &Slice : Slices)
            {
                if (Slice.Levels[0].Width == 1 && Slice.Levels[0].Height == 1)
                    for (size_t Pixel = 0; Pixel < Bytes/4; ++Pixel)
                        Combined.Rgba.insert(Combined.Rgba.end(),Slice.Rgba.begin(),Slice.Rgba.begin()+4);
                else
                {
                    const auto Start = Slice.Rgba.begin()+Slice.Levels[Level].Offset;
                    Combined.Rgba.insert(Combined.Rgba.end(),Start,Start+Bytes);
                }
            }
            if (Width == 1 && Height == 1) break;
            Width = std::max(1u,Width/2);
            Height = std::max(1u,Height/2);
            ++Level;
        }
        if (Combined.Rgba.size() > MaximumBytes-ResidentBytes)
            throw std::runtime_error("Compiled material texture arrays exceed 256 MiB.");
        ResidentBytes += Combined.Rgba.size();
        Compiled.Textures[Slot] = std::move(Combined);
    }
    return Compiled;
}
} // namespace Frontier
