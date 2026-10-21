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

double OptionalScalar(const MaterialRecipeParameters &Recipe, const char *Key, double Fallback)
{
    const auto Value = Recipe.Scalars.find(Key);
    return Value != Recipe.Scalars.end() ? Value->second : Fallback;
}

std::string RecipeText(const MaterialRecipeParameters &Recipe, const char *Key, const std::string &Fallback = "")
{
    const auto Value = Recipe.Texts.find(Key);
    return Value != Recipe.Texts.end() ? Value->second : Fallback;
}

int PatternIndex(const std::string &Pattern)
{
    if (Pattern == "plain") return 0;
    if (Pattern == "twill") return 1;
    if (Pattern == "satin") return 2;
    if (Pattern == "basket") return 3;
    if (Pattern == "rib") return 4;
    if (Pattern == "herringbone") return 5;
    if (Pattern == "oxford") return 6;
    if (Pattern == "houndstooth") return 7;
    if (Pattern == "denim") return 8;
    throw std::runtime_error("Unknown weave pattern in validation.");
}

// 📝 Cell-center warp/weft dominance: 'R' where the baked albedo leans warp, 'B' where it leans weft.
// Patterns resolve through color selection alone, so the asserts never touch float equality.
char WeaveCell(int Weave, int I, int J)
{
    WeaveFieldParameters P;
    P.Type = 4;
    P.Weave = Weave;
    P.Scale = 38;
    P.Relief = .65f;
    P.FiberDetail = 12;
    P.Warp[0] = 1;
    P.Warp[1] = P.Warp[2] = 0;
    P.Weft[2] = 1;
    P.Weft[0] = P.Weft[1] = 0;
    const auto Sample = EvaluateWeaveField((float(I) + .5f) / 38, (float(J) + .5f) / 38, P, 38.f / 512);
    return Sample.Albedo[0] > Sample.Albedo[2] ? 'R' : 'B';
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
        // Weave patterns: every over/under mask resolves at cell centers exactly as the Alloy twill().
        Confirm(WeaveCell(0, 0, 0) == 'R' && WeaveCell(0, 1, 0) == 'B' && WeaveCell(0, 0, 1) == 'B' &&
                WeaveCell(0, 1, 1) == 'R',
            "Plain weave checker drifted.");
        Confirm(WeaveCell(1, 0, 0) == 'R' && WeaveCell(1, 2, 0) == 'B' && WeaveCell(1, 0, 1) == 'B' &&
                WeaveCell(1, 1, 1) == 'R',
            "Twill diagonal drifted.");
        Confirm(WeaveCell(2, 4, 0) == 'B' && WeaveCell(2, 0, 0) == 'R' && WeaveCell(2, 2, 1) == 'B',
            "Satin dot drifted.");
        Confirm(WeaveCell(3, 0, 0) == 'R' && WeaveCell(3, 2, 0) == 'B' && WeaveCell(3, 0, 2) == 'B' &&
                WeaveCell(3, 2, 2) == 'R',
            "Basket blocks drifted.");
        Confirm(WeaveCell(4, 0, 0) == 'R' && WeaveCell(4, 1, 0) == 'B' && WeaveCell(4, 0, 1) == 'R' &&
                WeaveCell(4, 0, 2) == 'B',
            "Rib stripes drifted.");
        Confirm(WeaveCell(5, 0, 0) == 'R' && WeaveCell(5, 2, 0) == 'B' && WeaveCell(5, 8, 0) == 'B' &&
                WeaveCell(5, 10, 0) == 'R' && WeaveCell(5, 16, 0) == 'R',
            "Herringbone chevron drifted.");
        Confirm(WeaveCell(6, 0, 0) == 'R' && WeaveCell(6, 2, 0) == 'B' && WeaveCell(6, 0, 1) == 'B' &&
                WeaveCell(6, 2, 1) == 'R',
            "Oxford bricks drifted.");
        Confirm(WeaveCell(7, 2, 0) == 'R' && WeaveCell(7, 0, 1) == 'R' && WeaveCell(7, 2, 1) == 'B' &&
                WeaveCell(7, 0, 2) == 'B' && WeaveCell(7, 0, 3) == 'R' && WeaveCell(7, 1, 3) == 'B',
            "Houndstooth teeth drifted.");
        Confirm(WeaveCell(8, 0, 0) == 'R' && WeaveCell(8, 3, 0) == 'B' && WeaveCell(8, 0, 1) == 'B',
            "Denim steep twill drifted.");
        // Weave fields: carbon/nap modulation stays grayscale, relief keeps its analytic amplitude.
        WeaveFieldParameters Carbon;
        Carbon.Type = 7;
        Carbon.Scale = 14;
        Carbon.Relief = .7f;
        Carbon.FiberDetail = 12;
        const auto CarbonSample = EvaluateWeaveField(.3f, .6f, Carbon, 14.f / 512);
        Confirm(CarbonSample.Albedo[0] == CarbonSample.Albedo[1] && CarbonSample.Albedo[1] == CarbonSample.Albedo[2],
            "Carbon weave modulation carries hue.");
        WeaveFieldParameters Nap = Carbon;
        Nap.Type = 4;
        Nap.FabricMode = 2;
        Nap.Scale = 36;
        Nap.Fuzz = 1;
        Nap.Seed = 17;
        const auto NapSample = EvaluateWeaveField(.3f, .6f, Nap, 36.f / 512);
        Confirm(NapSample.Albedo[0] == NapSample.Albedo[1] && NapSample.Albedo[1] == NapSample.Albedo[2],
            "Nap weave modulation carries hue.");
        WeaveFieldParameters Denim;
        Denim.Type = 4;
        Denim.Weave = 8;
        Denim.Scale = 160;
        Denim.Relief = .65f;
        Denim.FiberDetail = 12;
        Denim.Fuzz = .35f;
        Denim.Slub = .35f;
        Denim.DenimFade = .08f;
        Denim.Seed = 17;
        const float DenimAmplitude = WeaveReliefAmplitude(Denim);
        Confirm(DenimAmplitude > 0, "Denim relief amplitude collapsed.");
        const auto DenimMaps = BakeWeaveMaps(Denim, 64);
        const auto DenimRepeat = BakeWeaveMaps(Denim, 64);
        Confirm(DenimMaps.Albedo == DenimRepeat.Albedo && DenimMaps.Relief == DenimRepeat.Relief,
            "Weave bake is not deterministic.");
        for (const float Height : DenimMaps.Relief)
            Confirm(std::abs(Height) <= DenimAmplitude * 1.001f, "Weave relief escapes its amplitude.");
        bool RejectedWeave = false;
        try { (void)BakeWeaveMaps(WeaveFieldParameters{}, 32); }
        catch (const std::exception &) { RejectedWeave = true; }
        Confirm(RejectedWeave, "Weave bake accepted an empty scale.");
        // Knit fields: loops modulate the albedo, relief keeps its analytic amplitude.
        KnitFieldParameters Jersey;
        Jersey.Scale = 22;
        Jersey.Grain = .6f;
        Jersey.Seed = 17;
        Jersey.Color[0] = Jersey.Color[1] = Jersey.Color[2] = .5f;
        const float KnitAmplitude = KnitReliefAmplitude(Jersey);
        Confirm(KnitAmplitude > 0, "Knit relief amplitude collapsed.");
        const auto JerseyMaps = BakeKnitMaps(Jersey, 64);
        float KnitMinimum = 1, KnitMaximum = 0;
        for (size_t Pixel = 0; Pixel < JerseyMaps.Relief.size(); ++Pixel)
        {
            Confirm(std::abs(JerseyMaps.Relief[Pixel]) <= KnitAmplitude * 1.001f, "Knit relief escapes its amplitude.");
            KnitMinimum = std::min(KnitMinimum, JerseyMaps.Albedo[Pixel * 3]);
            KnitMaximum = std::max(KnitMaximum, JerseyMaps.Albedo[Pixel * 3]);
        }
        Confirm(KnitMaximum - KnitMinimum > .05f, "Knit loops leave no albedo modulation.");
        Confirm(JerseyMaps.Albedo == BakeKnitMaps(Jersey, 64).Albedo, "Knit bake is not deterministic.");
        bool RejectedKnit = false;
        try { (void)BakeKnitMaps(KnitFieldParameters{}, 32); }
        catch (const std::exception &) { RejectedKnit = true; }
        Confirm(RejectedKnit, "Knit bake accepted an empty scale.");
        // Sand fields: zero coverage reproduces the base exactly, live beds stay in range, seeds separate.
        SandFieldParameters Dune;
        Dune.Count = 1;
        Dune.Seed = 41;
        Dune.BaseColor[0] = Dune.BaseColor[1] = Dune.BaseColor[2] = .5f;
        auto &DuneBed = Dune.Beds[0];
        DuneBed.Color[0] = DuneBed.Color[1] = DuneBed.Color[2] = .8f;
        DuneBed.Pale[0] = DuneBed.Pale[1] = DuneBed.Pale[2] = .9f;
        DuneBed.Size = .7f;
        DuneBed.SizeVariation = .55f;
        DuneBed.Coverage = 0;
        DuneBed.Thickness = 1;
        DuneBed.Roughness = .58f;
        DuneBed.RoughnessVariation = .3f;
        DuneBed.Specular = .8f;
        DuneBed.Tilt = .32f;
        DuneBed.Relief = .85f;
        DuneBed.BedSeed = SandBedSeed("sand-bed-1");
        const auto BareSand = BakeSandMaps(Dune, 16);
        for (size_t Pixel = 0; Pixel < BareSand.Relief.size(); ++Pixel)
        {
            Confirm(BareSand.Tint[Pixel * 3] == .5f && BareSand.Roughness[Pixel] == std::min(1.f, .58f + .16f) &&
                    BareSand.Relief[Pixel] == 0 && BareSand.Facet[Pixel * 2] == 0,
                "Bare sand escapes its base.");
        }
        DuneBed.Coverage = 1;
        const float SandAmplitude = SandReliefAmplitude(Dune);
        Confirm(SandAmplitude > 0, "Sand relief amplitude collapsed.");
        // 📝 Grains resolve past extent ~88 at this frequency; coarser bakes fade to the mean like the
        // shader does with distance, so seed sensitivity asserts at a resolved extent.
        const auto LiveSand = BakeSandMaps(Dune, 128);
        const auto LiveRepeat = BakeSandMaps(Dune, 128);
        Confirm(LiveSand.Tint == LiveRepeat.Tint && LiveSand.Roughness == LiveRepeat.Roughness &&
                LiveSand.Relief == LiveRepeat.Relief && LiveSand.Facet == LiveRepeat.Facet,
            "Sand bake is not deterministic.");
        SandFieldParameters ReseededDune = Dune;
        ReseededDune.Seed = 42;
        Confirm(BakeSandMaps(ReseededDune, 128).Tint != LiveSand.Tint, "Sand bake ignores its seed.");
        bool GrainDiffers = false;
        for (size_t Pixel = 0; Pixel < LiveSand.Relief.size(); ++Pixel)
        {
            Confirm(LiveSand.Tint[Pixel * 3] >= 0 && LiveSand.Roughness[Pixel] >= .04f &&
                    LiveSand.Roughness[Pixel] <= 1 && LiveSand.Relief[Pixel] >= 0 &&
                    LiveSand.Relief[Pixel] <= SandAmplitude * 1.001f &&
                    std::abs(LiveSand.Facet[Pixel * 2]) <= DuneBed.Tilt + .01f,
                "Sand grain escapes its range.");
            GrainDiffers = GrainDiffers || LiveSand.Tint[Pixel * 3] != .5f;
        }
        Confirm(GrainDiffers, "Live sand reproduces its base.");
        bool RejectedSand = false;
        try { (void)BakeSandMaps(SandFieldParameters{}, 32); }
        catch (const std::exception &) { RejectedSand = true; }
        Confirm(RejectedSand, "Sand bake accepted zero beds.");
        Confirm(SandBedSeed("sand-bed-1") != SandBedSeed("sand-bed-2"), "Sand bed seeds collide.");
        // Recipe color and converter encoding: sRGB round-trips through linear, ORM/normal stay exact.
        float Indigo[3] = {0, 0, 0};
        Confirm(DecodeHexLinearRgb("#163353", Indigo), "Recipe indigo rejected.");
        const double IndigoRed = .0862745 > .04045 ? std::pow((.0862745 + .055) / 1.055, 2.4) : .0862745 / 12.92;
        Confirm(std::abs(double(Indigo[0]) - IndigoRed) < 1e-6, "Hex decode drifts from sRGB.");
        float BadHex[3] = {0, 0, 0};
        Confirm(!DecodeHexLinearRgb("#xyz123", BadHex) && !DecodeHexLinearRgb("163353", BadHex) &&
                !DecodeHexLinearRgb("#12345", BadHex),
            "Malformed hex accepted.");
        const std::vector<float> LinearGray{0, .5f, 1};
        const auto EncodedGray = ConvertLinearAlbedoRgba(LinearGray, 1);
        Confirm(EncodedGray.size() == 4 && EncodedGray[0] == 0 && EncodedGray[2] == 255 && EncodedGray[3] == 255 &&
                std::abs(int(EncodedGray[1]) - 188) <= 1,
            "Linear albedo encoding drifted.");
        const std::vector<float> RoughField{0, .5f, 1, .25f};
        const auto SandOrm = ConvertSandOrmRgba(RoughField, 2);
        Confirm(SandOrm[0] == 255 && SandOrm[1] == 0 && SandOrm[2] == 255 &&
                SandOrm[5] >= 127 && SandOrm[5] <= 129 && SandOrm[9] == 255 && SandOrm[13] >= 63 && SandOrm[13] <= 65,
            "Sand ORM encoding drifted.");
        const std::vector<float> FlatRelief(16 * 16, 0), ZeroFacet(16 * 16 * 2, 0);
        const auto FlatFaceted = ConvertFacetNormalRgba(FlatRelief, ZeroFacet, 16, 2);
        Confirm(FlatFaceted[0] == 128 && FlatFaceted[1] == 128 && FlatFaceted[2] == 255,
            "Flat faceted normal is not straight up.");
        std::vector<float> TiltFacet(16 * 16 * 2, 0);
        for (size_t Pixel = 0; Pixel < 16 * 16; ++Pixel) TiltFacet[Pixel * 2] = 1;
        const auto Tilted = ConvertFacetNormalRgba(FlatRelief, TiltFacet, 16, 2);
        Confirm(Tilted[0] > 128 && Tilted[1] == 128, "Facet tilt leans the wrong way.");
        // Recipe coverage: every shipped material parses its SourceRecipe; the pilot resolves exactly.
        MaterialLibrary Library;
        Confirm(Library.ConfigureDirectory("EngineContent/MaterialArchives/TextureStudio") && Library.Refresh(),
            "Material library refresh failed.");
        Confirm(Library.QueryEntries().size() == 127, "Shipped material count changed.");
        std::set<std::string> RecipeIds;
        const MaterialCatalogueEntry *PilotEntry = nullptr, *SandEntry = nullptr, *LayeredEntry = nullptr,
                                     *DenimEntry = nullptr;
        for (const auto &Entry : Library.QueryEntries())
        {
            if (!Entry.Recipe.RecipeId.empty()) RecipeIds.insert(Entry.Recipe.RecipeId);
            if (Entry.Recipe.RecipeId == "scratches") PilotEntry = &Entry;
            if (Entry.RelativePath == "sand.material") SandEntry = &Entry;
            if (Entry.RelativePath == "layered-sand.material") LayeredEntry = &Entry;
            if (Entry.RelativePath == "raw-selvedge-denim.material") DenimEntry = &Entry;
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
        Confirm(DenimEntry && RecipeText(DenimEntry->Recipe, "weavePattern") == "denim" &&
                RecipeText(DenimEntry->Recipe, "warpColor") == "#163353",
            "Denim weave texts misparsed.");
        Confirm(SandEntry && SandEntry->Recipe.SandBeds.size() == 1, "Single-grain sand bed count changed.");
        const auto &LoneBed = SandEntry->Recipe.SandBeds.front();
        Confirm(LoneBed.Id == "sand-bed-1" && LoneBed.Color == "#c9aa79" && LoneBed.SecondaryColor == "#eee2c7" &&
                std::abs(LoneBed.Size - .7) < 1e-9 && std::abs(LoneBed.Roughness - .58) < 1e-9 &&
                std::abs(LoneBed.Coverage - 1) < 1e-9 && std::abs(LoneBed.Thickness - 1) < 1e-9,
            "Single-grain sand bed misparsed.");
        Confirm(LayeredEntry && LayeredEntry->Recipe.SandBeds.size() == 3, "Layered sand bed count changed.");
        const auto &Beds = LayeredEntry->Recipe.SandBeds;
        Confirm(Beds[0].Id == "sand-bed-1" && std::abs(Beds[0].Size - .65) < 1e-9 &&
                std::abs(Beds[0].Coverage - .85) < 1e-9 && Beds[1].Id == "sand-bed-2" &&
                std::abs(Beds[1].Size - .25) < 1e-9 && std::abs(Beds[1].Coverage - .92) < 1e-9 &&
                Beds[2].Id == "sand-bed-3" && std::abs(Beds[2].Size - .95) < 1e-9 &&
                std::abs(Beds[2].Thickness - 1.3) < 1e-9,
            "Layered sand beds misparsed.");
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
        // Fail-closed regeneration across every baked family: each committed weave/knit/sand PNG must equal
        // a fresh bake from the parsed recipe. Param mapping mirrors the offline baker key for key.
        const auto BakeLibraryWeave = [&](const MaterialRecipeParameters &R, const std::string &Stem)
        {
            WeaveFieldParameters P;
            P.Type = int(OptionalScalar(R, "type", 4));
            P.Weave = PatternIndex(RecipeText(R, "weavePattern", "twill"));
            P.FabricMode = int(OptionalScalar(R, "fabricMode", 0));
            P.Scale = float(OptionalScalar(R, "detailScale", 0));
            P.AngleRadians = float(OptionalScalar(R, "weaveAngle", 0) * 3.141592653589793 / 180);
            P.Relief = float(OptionalScalar(R, "weaveRelief", 0));
            P.FiberDetail = float(OptionalScalar(R, "fiberDetail", 12));
            P.Fuzz = float(OptionalScalar(R, "fuzz", 0));
            P.Slub = float(OptionalScalar(R, "slub", 0));
            P.DenimFade = float(OptionalScalar(R, "denimFade", 0));
            P.Grain = float(OptionalScalar(R, "grain", 0));
            P.Seed = float(OptionalScalar(R, "surfaceSeed", 0));
            Confirm(DecodeHexLinearRgb(RecipeText(R, "warpColor"), P.Warp), "Library warp color invalid.");
            Confirm(DecodeHexLinearRgb(RecipeText(R, "weftColor"), P.Weft), "Library weft color invalid.");
            if (!DecodeHexLinearRgb(RecipeText(R, "secondaryColor"), P.Secondary))
                P.Secondary[0] = P.Secondary[1] = P.Secondary[2] = 0;
            auto Maps = BakeWeaveMaps(P, 512);
            const float Amplitude = WeaveReliefAmplitude(P);
            if (Amplitude > 0)
                for (float &Height : Maps.Relief) Height /= Amplitude;
            return std::vector<std::pair<std::vector<uint8_t>, std::string>>{
                {ConvertLinearAlbedoRgba(Maps.Albedo, 512), Stem + "-albedo.png"},
                {ConvertHeightToNormalRgba(Maps.Relief, 512, 2.f), Stem + "-normal.png"}};
        };
        const auto BakeLibraryKnit = [&](const MaterialRecipeParameters &R, const std::string &Stem)
        {
            KnitFieldParameters P;
            P.Scale = float(OptionalScalar(R, "detailScale", 0));
            P.Grain = float(OptionalScalar(R, "grain", 0));
            P.Fuzz = float(OptionalScalar(R, "fuzz", 0));
            P.Seed = float(OptionalScalar(R, "surfaceSeed", 0));
            P.AngleRadians = float(OptionalScalar(R, "weaveAngle", 0) * 3.141592653589793 / 180);
            Confirm(DecodeHexLinearRgb(RecipeText(R, "color"), P.Color), "Library knit color invalid.");
            if (!DecodeHexLinearRgb(RecipeText(R, "secondaryColor"), P.Secondary))
                P.Secondary[0] = P.Secondary[1] = P.Secondary[2] = 0;
            auto Maps = BakeKnitMaps(P, 512);
            const float Amplitude = KnitReliefAmplitude(P);
            if (Amplitude > 0)
                for (float &Height : Maps.Relief) Height /= Amplitude;
            return std::vector<std::pair<std::vector<uint8_t>, std::string>>{
                {ConvertLinearAlbedoRgba(Maps.Albedo, 512), Stem + "-albedo.png"},
                {ConvertHeightToNormalRgba(Maps.Relief, 512, 2.f), Stem + "-normal.png"}};
        };
        const auto BakeLibrarySand = [&](const MaterialRecipeParameters &R, const std::string &Stem)
        {
            Confirm(!R.SandBeds.empty() && R.SandBeds.size() <= 4, "Library sand beds missing.");
            SandFieldParameters P;
            P.Count = int(R.SandBeds.size());
            for (size_t Bed = 0; Bed < R.SandBeds.size(); ++Bed)
            {
                const auto &Source = R.SandBeds[Bed];
                auto &Target = P.Beds[Bed];
                Confirm(DecodeHexLinearRgb(Source.Color, Target.Color) &&
                        DecodeHexLinearRgb(Source.SecondaryColor, Target.Pale),
                    "Library sand bed colors invalid.");
                Target.Size = float(Source.Size);
                Target.SizeVariation = float(Source.SizeVariation);
                Target.Coverage = float(Source.Coverage);
                Target.Thickness = float(Source.Thickness);
                Target.Roughness = float(Source.Roughness);
                Target.RoughnessVariation = float(Source.RoughnessVariation);
                Target.Specular = float(Source.Specular);
                Target.SpecularVariation = float(Source.SpecularVariation);
                Target.Ior = float(Source.Ior);
                Target.Roundness = float(Source.Roundness);
                Target.Tilt = float(Source.Tilt);
                Target.Relief = float(Source.Relief);
                Target.BedSeed = SandBedSeed(Source.Id);
            }
            P.BandsMode = RecipeText(R, "sandArrangement") == "bands" ? 1 : 0;
            P.Scale = float(OptionalScalar(R, "sandScale", 1));
            P.Seed = float(OptionalScalar(R, "surfaceSeed", 0));
            P.BandFrequency = float(OptionalScalar(R, "sandBandFrequency", 1.5));
            P.BandWarp = float(OptionalScalar(R, "sandBandWarp", .2));
            P.BandAngleRadians = float(OptionalScalar(R, "sandBandAngle", 0) * 3.141592653589793 / 180);
            Confirm(DecodeHexLinearRgb(RecipeText(R, "color"), P.BaseColor), "Library sand base color invalid.");
            auto Maps = BakeSandMaps(P, 512);
            const float Amplitude = SandReliefAmplitude(P);
            if (Amplitude > 0)
                for (float &Height : Maps.Relief) Height /= Amplitude;
            return std::vector<std::pair<std::vector<uint8_t>, std::string>>{
                {ConvertLinearAlbedoRgba(Maps.Tint, 512), Stem + "-albedo.png"},
                {ConvertSandOrmRgba(Maps.Roughness, 512), Stem + "-orm.png"},
                {ConvertFacetNormalRgba(Maps.Relief, Maps.Facet, 512, 2.f), Stem + "-normal.png"}};
        };
        static const std::set<std::string> LibraryWeaveIds{"woven", "carbon", "suede", "velvet", "cotton", "denim",
            "silk", "linen", "wool"};
        size_t PinnedTextures = 0;
        for (const auto &Entry : Library.QueryEntries())
        {
            const auto &Id = Entry.Recipe.RecipeId;
            const std::string Stem = Entry.RelativePath.stem().string();
            std::vector<std::pair<std::vector<uint8_t>, std::string>> Fresh;
            if (LibraryWeaveIds.contains(Id)) Fresh = BakeLibraryWeave(Entry.Recipe, Stem);
            else if (Id == "jersey") Fresh = BakeLibraryKnit(Entry.Recipe, Stem);
            else if (Id == "sand" || Id == "layeredSand") Fresh = BakeLibrarySand(Entry.Recipe, Stem);
            else continue;
            for (const auto &[Pixels, File] : Fresh)
            {
                const auto Encoded = EncodeProceduralPng(Pixels, 512);
                const auto Committed =
                    ReadBytes(std::filesystem::path("EngineContent/MaterialArchives/TextureStudio/Textures") / File);
                Confirm(Committed == Encoded, "Committed weave/knit/sand texture drifted from its recipe bake.");
                ++PinnedTextures;
            }
        }
        Confirm(PinnedTextures == 44, "Baked texture family count changed.");
        std::cout << "{\"result\":\"passed\",\"checks\":" << Assertions << "}\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cout << "{\"result\":\"failed\",\"error\":\"" << Failure.what() << "\"}\n";
        return 1;
    }
}
