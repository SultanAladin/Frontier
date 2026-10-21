/*============================================================================================================================================
                                                      MATERIALPROCEDURALBAKINGVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Prove the CPU procedural port matches the Alloy kernels: noise parity against an independent double-precision
// reference, deterministic seeded scratch fields shared across channels, correct normal/ORM encoding, parsed
// recipe coverage for every shipped material, and byte-exact regeneration of the committed pilot textures.

#include "Engine/ContentInterchange/MaterialLibrary.h"
#include "Engine/Render/MaterialProceduralBaking.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

using namespace Frontier;

namespace
{
uint32_t Assertions = 0;

void Confirm(bool Condition, const char *Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Assertions;
}

// Independent double-precision reference for the GLSL hash31/noise3 pair; the engine must track the true
// function within float rounding, not merely agree with itself.
double ReferenceHash31(double X, double Y, double Z)
{
    X = X * .1031; Y = Y * .1031; Z = Z * .1031;
    X -= std::floor(X); Y -= std::floor(Y); Z -= std::floor(Z);
    const double Dot = X * (Y + 33.33) + Y * (Z + 33.33) + Z * (X + 33.33);
    X += Dot; Y += Dot; Z += Dot;
    const double Hash = (X + Y) * Z;
    return Hash - std::floor(Hash);
}

double ReferenceNoise3(double X, double Y, double Z)
{
    const double Ix = std::floor(X), Iy = std::floor(Y), Iz = std::floor(Z);
    double Fx = X - Ix, Fy = Y - Iy, Fz = Z - Iz;
    Fx = Fx * Fx * (3 - 2 * Fx); Fy = Fy * Fy * (3 - 2 * Fy); Fz = Fz * Fz * (3 - 2 * Fz);
    const auto Mix = [](double A, double B, double T) { return A + (B - A) * T; };
    const double X00 = Mix(ReferenceHash31(Ix, Iy, Iz), ReferenceHash31(Ix + 1, Iy, Iz), Fx);
    const double X10 = Mix(ReferenceHash31(Ix, Iy + 1, Iz), ReferenceHash31(Ix + 1, Iy + 1, Iz), Fx);
    const double X01 = Mix(ReferenceHash31(Ix, Iy, Iz + 1), ReferenceHash31(Ix + 1, Iy, Iz + 1), Fx);
    const double X11 = Mix(ReferenceHash31(Ix, Iy + 1, Iz + 1), ReferenceHash31(Ix + 1, Iy + 1, Iz + 1), Fx);
    return Mix(Mix(X00, X10, Fy), Mix(X01, X11, Fy), Fz);
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes <= 0) throw std::runtime_error("Committed procedural texture is missing.");
    std::vector<uint8_t> Data(size_t(Bytes), uint8_t(0));
    Input.seekg(0);
    Input.read(reinterpret_cast<char *>(Data.data()), Bytes);
    return Data;
}

double Scalar(const MaterialRecipeParameters &Recipe, const char *Key)
{
    const auto Value = Recipe.Scalars.find(Key);
    if (Value == Recipe.Scalars.end()) throw std::runtime_error("Parsed recipe is missing a pilot uniform.");
    return Value->second;
}
}

int main()
{
    try
    {
        // Noise parity: the double reference proves the formula at small magnitude; past that, hash31 chaos
        // amplifies float32 rounding on the CPU exactly as highp does on the GPU, so large coordinates assert
        // range, determinism and avalanche instead of false-precision equality with infinite precision.
        for (const auto &[X, Y, Z] : {std::array<double, 3>{0, 0, 0}, {1, 2, 3}, {.3, .7, .1}})
            Confirm(std::abs(double(ProceduralHash31(float(X), float(Y), float(Z))) - ReferenceHash31(X, Y, Z)) < 2e-4,
                "Procedural hash31 drifted from the Alloy kernel.");
        for (const auto &[X, Y, Z] :
            {std::array<double, 3>{-4.5, 17.25, 113.7}, {81.2, -31.1, 3}, {5.25, -3.75, 11.125}, {220, 220, 220}})
        {
            const float Hash = ProceduralHash31(float(X), float(Y), float(Z));
            Confirm(Hash >= 0 && Hash < 1, "Procedural hash31 escaped its unit range.");
            Confirm(Hash == ProceduralHash31(float(X), float(Y), float(Z)), "Procedural hash31 is not deterministic.");
        }
        Confirm(ProceduralHash31(220, 220, 220) != ProceduralHash31(221, 220, 220), "Procedural hash31 fails to avalanche.");
        for (const auto &[X, Y, Z] : {std::array<double, 3>{0, 0, 0}, {.3, .7, .1}, {1, 2, 3}})
            Confirm(std::abs(double(ProceduralNoise3(float(X), float(Y), float(Z))) - ReferenceNoise3(X, Y, Z)) < 2e-4,
                "Procedural noise3 drifted from the Alloy kernel.");
        for (const auto &[X, Y, Z] : {std::array<double, 3>{5.25, -3.75, 11.125}, {220, 220, 220}})
        {
            const double Engine = ProceduralNoise3(float(X), float(Y), float(Z));
            Confirm(Engine >= 0 && Engine < 1, "Procedural noise3 escaped its unit range.");
        }
        Confirm(ProceduralNoise3(1, 2, 3) == ProceduralNoise3(1, 2, 3), "Procedural noise3 is not deterministic.");
        const float Fbm = ProceduralStoneFbm(3.5f, -1.25f, 7);
        Confirm(Fbm >= 0 && Fbm < 1, "Procedural stone fbm escaped its unit range.");
        Confirm(Fbm == ProceduralStoneFbm(3.5f, -1.25f, 7), "Procedural stone fbm is not deterministic.");
        // Scratch field: empty without density, deterministic per seed, sensitive across seeds.
        ScratchFieldParameters Empty;
        const auto Bare = EvaluateScratchField(.3f, .6f, Empty, .01f);
        Confirm(Bare.Coverage == 0 && Bare.Height == 0 && Bare.Rim == 0, "Empty scratch field is not bare.");
        ScratchFieldParameters Pilot{6, 1.15f, .01f, .0018f, .8f, .42f, 6, float(25 * 3.141592653589793 / 180), 17};
        const auto First = EvaluateScratchField(.3f, .6f, Pilot, 6.f / 512);
        Confirm(First.Coverage == EvaluateScratchField(.3f, .6f, Pilot, 6.f / 512).Coverage, "Scratch field is not deterministic.");
        ScratchFieldParameters Reseeded = Pilot;
        Reseeded.Seed = 18;
        const auto Altered = BakeScratchMaps(Reseeded, 32);
        const auto Sealed = BakeScratchMaps(Pilot, 32);
        bool SeedSensitive = Altered.Height.size() == Sealed.Height.size();
        for (size_t Pixel = 0; SeedSensitive && Pixel < Sealed.Height.size(); ++Pixel)
            SeedSensitive = Altered.Height[Pixel] == Sealed.Height[Pixel] && Altered.Coverage[Pixel] == Sealed.Coverage[Pixel];
        Confirm(!SeedSensitive, "Scratch field ignores its seed.");
        // Groove assertions resolve at 256: coarser bakes anti-alias sub-texel cuts away, exactly as the
        // shader fades them with distance.
        const auto Maps = BakeScratchMaps(Pilot, 256);
        Confirm(Maps.Extent == 256 && Maps.Height.size() == size_t(256) * 256 && Maps.Coverage.size() == Maps.Height.size(),
            "Scratch bake shape changed.");
        float MinimumHeight = 0, MaximumCoverage = 0;
        bool Shared = true, Groove = false;
        for (size_t Pixel = 0; Pixel < Maps.Height.size(); ++Pixel)
        {
            MinimumHeight = std::min(MinimumHeight, Maps.Height[Pixel]);
            MaximumCoverage = std::max(MaximumCoverage, Maps.Coverage[Pixel]);
            Confirm(Maps.Coverage[Pixel] >= 0 && Maps.Coverage[Pixel] <= 1, "Scratch coverage escaped its mask range.");
            Confirm(Maps.Height[Pixel] <= Pilot.Depth * .17f, "Scratch rim exceeds the kernel lip budget.");
            // Shared-definition property: every groove pixel carries mask, so roughness and occlusion answer
            // to the same cuts as the height-derived normals.
            if (Maps.Height[Pixel] < -Pilot.Depth * .1f)
            {
                Groove = true;
                Shared = Shared && Maps.Coverage[Pixel] > 0;
            }
        }
        Confirm(Groove && MinimumHeight < -Pilot.Depth * .5f, "Scratch bake holds no groove.");
        Confirm(MaximumCoverage > .5f, "Scratch bake holds no cut core.");
        Confirm(Shared, "Scratch groove escapes its shared mask.");
        bool Rejected = false;
        try { (void)BakeScratchMaps(Pilot, 0); }
        catch (const std::exception &) { Rejected = true; }
        Confirm(Rejected, "Scratch bake accepted a zero extent.");
        // Normal encoding: flat relief points up, ramps tilt against the gradient, grooves dent inward.
        const std::vector<float> Flat(16 * 16, 0);
        const auto FlatNormal = ConvertHeightToNormalRgba(Flat, 16, 2);
        Confirm(FlatNormal[0] == 128 && FlatNormal[1] == 128 && FlatNormal[2] == 255 && FlatNormal[3] == 255,
            "Flat relief normal is not straight up.");
        std::vector<float> Ramp(16 * 16, 0);
        for (uint32_t Y = 0; Y < 16; ++Y)
            for (uint32_t X = 0; X < 16; ++X) Ramp[size_t(Y) * 16 + X] = float(X) / 15;
        const auto RampNormal = ConvertHeightToNormalRgba(Ramp, 16, 2);
        Confirm(RampNormal[(size_t(8) * 16 + 8) * 4] < 128, "Uphill ramp normal tilts the wrong way.");
        std::vector<float> Relief = Maps.Height;
        for (float &Height : Relief) Height /= Pilot.Depth;
        const auto GrooveNormal = ConvertHeightToNormalRgba(Relief, 256, 2);
        size_t Dented = 0;
        for (size_t Pixel = 2; Pixel < GrooveNormal.size(); Pixel += 4) Dented += GrooveNormal[Pixel] < 250 ? 1 : 0;
        Confirm(Dented > 100, "Baked grooves leave no normal dent.");
        // ORM encoding: field pixels preserve the base through the ceiling ratio, cuts reach the ceiling.
        const std::vector<float> BareMask(8 * 8, 0), CutMask(8 * 8, 1);
        const auto FieldOrm = ConvertScratchOrmRgba(BareMask, 8, .23f);
        Confirm(FieldOrm[0] == 255 && FieldOrm[1] == uint8_t(std::lround(.23f / .53f * 255)) && FieldOrm[2] == 255,
            "ORM field pixel does not preserve the authored base.");
        const auto CutOrm = ConvertScratchOrmRgba(CutMask, 8, .23f);
        Confirm(CutOrm[0] == uint8_t(std::lround(.89f * 255)) && CutOrm[1] == 255 && CutOrm[2] == 255,
            "ORM cut pixel misses the shader ceiling.");
        Confirm(FieldOrm[1] * .53f / 255 < .24f && FieldOrm[1] * .53f / 255 > .22f, "ORM field ratio drifts from the base.");
        // Grain ops: registry shape, Alloy formula parity, determinism, seed sensitivity, and the wear-disabled
        // passthrough that keeps unworn polymer out of the worn-roughness branch.
        Confirm(QueryProceduralOps().size() == 3, "Procedural op registry changed.");
        Confirm(QueryProceduralOpChannels("metalGrain") == std::vector<std::string>{"Albedo", "Normal"},
            "Bare-metal op channel set changed.");
        Confirm(QueryProceduralOpChannels("polymerGrain").size() == 3, "Polymer op channel set changed.");
        Confirm(QueryProceduralOpCeilingLift("scratches") == .3f && QueryProceduralOpCeilingLift("polymerGrain") == .096f &&
                QueryProceduralOpCeilingLift("metalGrain") == 0 && QueryProceduralOpCeilingLift("unknown") == 0,
            "ORM ceiling lift changed.");
        bool RejectedGrain = false;
        try { (void)BakeProceduralChannelRgba("metalGrain", "Orm", {}, 32); }
        catch (const std::exception &) { RejectedGrain = true; }
        Confirm(RejectedGrain, "Bare-metal op baked a channel its shader never varies.");
        const std::map<std::string, double> Metal{{"grain", .5}, {"detailScale", 8}, {"surfaceSeed", 17}};
        const auto MetalAlbedo = BakeProceduralChannelRgba("metalGrain", "Albedo", Metal, 32);
        const double MetalSpot = .92 + .1 * ReferenceNoise3(.5 / 32 * 8, 17, .5 / 32 * 8);
        Confirm(std::abs(MetalAlbedo[0] / 255. - MetalSpot) < 2. / 255, "Bare-metal albedo drifted from the Alloy type-1 term.");
        Confirm(MetalAlbedo == BakeProceduralChannelRgba("metalGrain", "Albedo", Metal, 32), "Grain bake is not deterministic.");
        const std::map<std::string, double> ReseededMetal{{"grain", .5}, {"detailScale", 8}, {"surfaceSeed", 18}};
        Confirm(MetalAlbedo != BakeProceduralChannelRgba("metalGrain", "Albedo", ReseededMetal, 32), "Grain bake ignores its seed.");
        const std::map<std::string, double> Polymer{{"grain", .6}, {"detailScale", 8}, {"wear", 0}, {"wearSoftness", .65},
            {"wornRoughness", .24}, {"scratchScale", 32}, {"surfaceSeed", 17}, {"roughness", .5}};
        const auto PolymerOrm = BakeProceduralChannelRgba("polymerGrain", "Orm", Polymer, 32);
        uint8_t MinimumGreen = 255, MaximumGreen = 0;
        double GreenSum = 0;
        for (size_t Pixel = 0; Pixel < PolymerOrm.size(); Pixel += 4)
        {
            Confirm(PolymerOrm[Pixel] == 255 && PolymerOrm[Pixel + 2] == 255 && PolymerOrm[Pixel + 3] == 255,
                "Polymer ORM carries occlusion or metal it never bakes.");
            MinimumGreen = std::min(MinimumGreen, PolymerOrm[Pixel + 1]);
            MaximumGreen = std::max(MaximumGreen, PolymerOrm[Pixel + 1]);
            GreenSum += PolymerOrm[Pixel + 1];
        }
        const double GreenMean = GreenSum / (PolymerOrm.size() / 4);
        Confirm(MinimumGreen >= 190 && MaximumGreen - MinimumGreen > 4 && GreenMean > 225 && GreenMean < 241,
            "Polymer ORM grain escapes its ceiling or drifts from the base ratio.");
        const auto PolymerAlbedo = BakeProceduralChannelRgba("polymerGrain", "Albedo", Polymer, 32);
        uint8_t MinimumGray = 255, MaximumGray = 0;
        for (size_t Pixel = 0; Pixel < PolymerAlbedo.size(); Pixel += 4)
            MinimumGray = std::min(MinimumGray, PolymerAlbedo[Pixel]), MaximumGray = std::max(MaximumGray, PolymerAlbedo[Pixel]);
        Confirm(MinimumGray >= 225 && MaximumGray == 255 && MinimumGray < MaximumGray, "Polymer albedo modulator range changed.");
        const std::map<std::string, double> BarePolymer{{"grain", 0}, {"detailScale", 8}, {"wear", 0}, {"surfaceSeed", 17}};
        const auto BarePolymerNormal = BakeProceduralChannelRgba("polymerGrain", "Normal", BarePolymer, 32);
        bool BareUp = true;
        for (size_t Pixel = 0; Pixel < BarePolymerNormal.size(); Pixel += 4)
            BareUp = BareUp && BarePolymerNormal[Pixel] == 128 && BarePolymerNormal[Pixel + 1] == 128 &&
                BarePolymerNormal[Pixel + 2] == 255;
        Confirm(BareUp, "Grain-free polymer relief tilts.");
        // Recipe coverage: every shipped material parses its SourceRecipe; the pilot resolves exactly.
        MaterialLibrary Library;
        Confirm(Library.ConfigureDirectory("EngineContent/MaterialArchives/TextureStudio") && Library.Refresh(),
            "Material library refresh failed.");
        Confirm(Library.QueryEntries().size() == 127, "Shipped material count changed.");
        std::set<std::string> RecipeIds;
        const MaterialCatalogueEntry *PilotEntry = nullptr;
        for (const auto &Entry : Library.QueryEntries())
        {
            if (!Entry.Recipe.RecipeId.empty()) RecipeIds.insert(Entry.Recipe.RecipeId);
            if (Entry.Recipe.RecipeId == "scratches") PilotEntry = &Entry;
        }
        Confirm(RecipeIds.size() == 51, "Parsed recipe family count changed.");
        Confirm(PilotEntry && PilotEntry->RelativePath == "scratches.material", "Scratch pilot entry is missing.");
        const auto &Recipe = PilotEntry->Recipe;
        Confirm(Scalar(Recipe, "type") == 29 && Scalar(Recipe, "surfaceSeed") == 17, "Pilot integers misparsed.");
        Confirm(std::abs(Scalar(Recipe, "scratchLength") - 1.15) < 1e-9 && std::abs(Scalar(Recipe, "scratchWidth") - .01) < 1e-9 &&
                std::abs(Scalar(Recipe, "scratchDepth") - .0018) < 1e-12 && Scalar(Recipe, "scratchDensity") == 6 &&
                std::abs(Scalar(Recipe, "scratchSpread") - .8) < 1e-9 && std::abs(Scalar(Recipe, "scratchBend") - .42) < 1e-9 &&
                Scalar(Recipe, "scratchScale") == 6 && Scalar(Recipe, "weaveAngle") == 25 &&
                std::abs(Scalar(Recipe, "roughness") - .23) < 1e-9,
            "Pilot scratch uniforms misparsed.");
        Confirm(Scalar(Recipe, "metalScratches") == 0 && Scalar(Recipe, "clothMapping") == 0, "Pilot flags misparsed.");
        Confirm(Recipe.Texts.at("color") == "#b9bec0", "Pilot color text misparsed.");
        // Fail-closed regeneration: committed pilot PNGs must equal a fresh bake from the parsed recipe.
        ScratchFieldParameters LibraryPilot{float(Scalar(Recipe, "scratchDensity")), float(Scalar(Recipe, "scratchLength")),
            float(Scalar(Recipe, "scratchWidth")), float(Scalar(Recipe, "scratchDepth")), float(Scalar(Recipe, "scratchSpread")),
            float(Scalar(Recipe, "scratchBend")), float(Scalar(Recipe, "scratchScale")),
            float(Scalar(Recipe, "weaveAngle") * 3.141592653589793 / 180), uint32_t(Scalar(Recipe, "surfaceSeed"))};
        auto LibraryMaps = BakeScratchMaps(LibraryPilot, 512);
        for (float &Height : LibraryMaps.Height) Height /= LibraryPilot.Depth;
        const auto LibraryNormal = ConvertHeightToNormalRgba(LibraryMaps.Height, 512, 2.f);
        const auto LibraryOrm = ConvertScratchOrmRgba(LibraryMaps.Coverage, 512, float(Scalar(Recipe, "roughness")));
        for (const auto &[Pixels, Stem] :
            {std::pair<const std::vector<uint8_t> &, const char *>{LibraryNormal, "scratches-normal.png"}, {LibraryOrm, "scratches-orm.png"}})
        {
            const auto Encoded = EncodeProceduralPng(Pixels, 512);
            const auto Committed = ReadBytes(std::filesystem::path("EngineContent/MaterialArchives/TextureStudio/Textures") / Stem);
            Confirm(Committed == Encoded, "Committed pilot texture drifted from its recipe bake.");
        }
        std::cout << "{\"result\":\"passed\",\"checks\":" << Assertions << "}\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cout << "{\"result\":\"failed\",\"error\":\"" << Failure.what() << "\"}\n";
        return 1;
    }
}
