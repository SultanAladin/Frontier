/*============================================================================================================================================
                                                          MATERIALPROCEDURALBAKE.CPP
============================================================================================================================================*/
// 🧩 Offline baker for procedural material pilots: evaluate the CPU kernels from each entry's parsed SourceRecipe
// and write the shared ORM/normal PNGs the preview samples. Regenerate with:
// MaterialProceduralBake [ContentDirectory]; the validation test fails closed when committed bytes drift.

#include "Engine/ContentInterchange/MaterialLibrary.h"
#include "Engine/Render/MaterialProceduralBaking.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace Frontier;

namespace
{
double Scalar(const MaterialRecipeParameters &Recipe, const char *Key, double Fallback = 0)
{
    const auto Value = Recipe.Scalars.find(Key);
    return Value != Recipe.Scalars.end() ? Value->second : Fallback;
}

void WritePng(const std::filesystem::path &Path, const std::vector<uint8_t> &Rgba, uint32_t Extent)
{
    const auto Encoded = EncodeProceduralPng(Rgba, Extent);
    std::ofstream Output(Path, std::ios::binary | std::ios::trunc);
    Output.write(reinterpret_cast<const char *>(Encoded.data()), Encoded.size());
    if (!Output) throw std::runtime_error("Procedural PNG write failed.");
}

bool BakeScratches(const MaterialCatalogueEntry &Entry, const std::filesystem::path &Textures)
{
    // Shared-field pilot: one scratch evaluation feeds height-derived normals plus roughness/occlusion,
    // so every channel answers to the same density/length/width/depth/spread/bend/scale/seed definition.
    ScratchFieldParameters Parameters;
    Parameters.Density = float(Scalar(Entry.Recipe, "scratchDensity"));
    Parameters.Length = float(Scalar(Entry.Recipe, "scratchLength"));
    Parameters.Width = float(Scalar(Entry.Recipe, "scratchWidth"));
    Parameters.Depth = float(Scalar(Entry.Recipe, "scratchDepth"));
    Parameters.Spread = float(Scalar(Entry.Recipe, "scratchSpread"));
    Parameters.Bend = float(Scalar(Entry.Recipe, "scratchBend"));
    Parameters.Scale = float(Scalar(Entry.Recipe, "scratchScale", 1));
    const double Type = Scalar(Entry.Recipe, "type", 29);
    const double Degrees = Type == 29 ? Scalar(Entry.Recipe, "weaveAngle") : Scalar(Entry.Recipe, "scratchAngle");
    Parameters.AngleRadians = float(Degrees * 3.141592653589793 / 180);
    Parameters.Seed = uint32_t(Scalar(Entry.Recipe, "surfaceSeed"));
    if (!(Parameters.Density > 0) || !(Parameters.Depth > 0) || !(Parameters.Scale > 0)) return false;
    constexpr uint32_t Extent = 512;
    auto Maps = BakeScratchMaps(Parameters, Extent);
    for (float &Height : Maps.Height) Height /= Parameters.Depth;
    const auto Normal = ConvertHeightToNormalRgba(Maps.Height, Extent, 2.f);
    const auto Orm = ConvertScratchOrmRgba(Maps.Coverage, Extent, float(Scalar(Entry.Recipe, "roughness", .2)));
    const auto Stem = Entry.RelativePath.stem().string();
    WritePng(Textures / (Stem + "-normal.png"), Normal, Extent);
    WritePng(Textures / (Stem + "-orm.png"), Orm, Extent);
    std::cout << "baked " << Stem << " (scratches, seed " << Parameters.Seed << ")\n";
    return true;
}
}

int main(int ArgumentCount, char **Arguments)
{
    try
    {
        const std::filesystem::path Content = ArgumentCount > 1 ? Arguments[1] : "EngineContent/MaterialArchives/TextureStudio";
        MaterialLibrary Library;
        if (!Library.ConfigureDirectory(Content) || !Library.Refresh()) throw std::runtime_error("Material library refresh failed.");
        const auto Textures = Content / "Textures";
        std::filesystem::create_directories(Textures);
        size_t Baked = 0;
        for (const auto &Entry : Library.QueryEntries())
        {
            if (Entry.Recipe.RecipeId == "scratches" && BakeScratches(Entry, Textures)) ++Baked;
        }
        std::cout << "{\"result\":\"passed\",\"baked\":" << Baked << "}\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cout << "{\"result\":\"failed\",\"error\":\"" << Failure.what() << "\"}\n";
        return 1;
    }
}
