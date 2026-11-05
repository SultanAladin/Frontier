/*==========================================================================================================================================
                                                        PARTICLESPACEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The `.particlespace` file: a space holding every one of the library's 52 presets, edited settings, forces and an override
//    origin is written, read and written again; the two texts must be identical, every number must survive bit for bit, the
//    read space must plan the same GPU refreshes as the original, the level's Force Field must join the lattice only while
//    followed, the drag volume and the current must reach the field program, and malformed files must be refused without
//    touching the space they were read into.
//
//     ParticleSpaceValidation <repository root> <scratch folder>

#include "Frontier/Engine/PhysicalDynamics/ParticleDynamics/ParticleSpaceCodec.h"

#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

using namespace Frontier;

namespace
{

int Failures   = 0;
int Assertions = 0;

void Expect(bool Condition, const std::string& Claim)
{
    ++Assertions;
    if (!Condition)
    {
        ++Failures;
        std::printf("  🔴 %s\n", Claim.c_str());
    }
}

bool Same(double Left, double Right)
{
    return std::bit_cast<uint64_t>(Left) == std::bit_cast<uint64_t>(Right);
}

template <size_t Count> bool Same(const std::array<double, Count>& Left, const std::array<double, Count>& Right)
{
    for (size_t Index = 0; Index < Count; ++Index)
        if (!Same(Left[Index], Right[Index])) return false;
    return true;
}

// 📝 The parameters whose values are least likely to print exactly: fractions, colours, lifetimes, fibre stops.
bool SameParameters(const ParticleEffectParameters& Left, const ParticleEffectParameters& Right)
{
    bool Equal = Left.Behaviour == Right.Behaviour && Left.Appearance == Right.Appearance && Left.Capacity == Right.Capacity &&
                 Left.Visible == Right.Visible && Same(Left.ReleaseRate, Right.ReleaseRate) && Same(Left.OriginMetres, Right.OriginMetres) &&
                 Same(Left.Direction, Right.Direction) && Same(Left.SpreadRadians, Right.SpreadRadians) && Same(Left.SpeedMinimum, Right.SpeedMinimum) &&
                 Same(Left.SpeedMaximum, Right.SpeedMaximum) && Same(Left.LifeMinimumSeconds, Right.LifeMinimumSeconds) &&
                 Same(Left.LifeMaximumSeconds, Right.LifeMaximumSeconds) && Same(Left.ColourA, Right.ColourA) && Same(Left.ColourB, Right.ColourB) &&
                 Same(Left.ColourC, Right.ColourC) && Same(Left.IntegrationStepSeconds, Right.IntegrationStepSeconds) &&
                 Left.Transition.has_value() == Right.Transition.has_value() && Left.Attractor.has_value() == Right.Attractor.has_value() &&
                 Left.Lensing.has_value() == Right.Lensing.has_value() && Left.HeatShimmer.has_value() == Right.HeatShimmer.has_value() &&
                 Left.MagneticDipole.has_value() == Right.MagneticDipole.has_value() && Left.LightFibre.has_value() == Right.LightFibre.has_value();
    if (Equal && Left.LightFibre)
    {
        Equal = Left.LightFibre->Stops.size() == Right.LightFibre->Stops.size() && Left.LightFibre->PathShape == Right.LightFibre->PathShape &&
                Same(Left.LightFibre->AccentColour, Right.LightFibre->AccentColour);
        for (size_t Stop = 0; Equal && Stop < Left.LightFibre->Stops.size(); ++Stop)
            Equal = Same(Left.LightFibre->Stops[Stop].Position, Right.LightFibre->Stops[Stop].Position) &&
                    Same(Left.LightFibre->Stops[Stop].Colour, Right.LightFibre->Stops[Stop].Colour);
    }
    if (Equal && Left.Transition) Equal = Same(Left.Transition->BurstShare, Right.Transition->BurstShare) && Left.Transition->Butterflies == Right.Transition->Butterflies;
    if (Equal && Left.Attractor) Equal = Same(Left.Attractor->Strength, Right.Attractor->Strength) && Same(Left.Attractor->Swirl, Right.Attractor->Swirl);
    return Equal;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 3)
    {
        std::printf("usage: ParticleSpaceValidation <repository root> <scratch folder>\n");
        return 2;
    }
    const std::filesystem::path Root    = Arguments[1];
    const std::filesystem::path Scratch = Arguments[2];
    std::filesystem::create_directories(Scratch);

    ParticleEffectLibrary Library;
    std::string           Diagnostic;
    if (!Library.Decode(Root / "EngineContent" / "ParticleArchives" / "ParticleLibrary.toml", Diagnostic))
    {
        std::printf("🔴 library: %s\n", Diagnostic.c_str());
        return 1;
    }

    // ① a space with every preset, edited settings, a disabled force and an override origin
    ParticleSpace Original;
    Original.ConstructDefault(Library);
    for (const ParticlePreset& Preset : Library.Presets) (void)Original.AddEffect(Library, Preset.Identifier);
    Original.Camera                  = { 1.234567890123, 0.4142135623731, 17.25, { 0.1, 2.0 / 3.0, -0.3 } };
    Original.Wind.WindScale          = 1.0 / 3.0;
    Original.Wind.ShowArrows         = true;
    Original.LightningWeb.Enabled    = true;
    Original.LightningWeb.Links      = 3;
    Original.Lightning.Automatic     = false;
    Original.TimeScale               = 0.7;
    Original.Probe                   = { 0.25, 1.75, -2.125 };
    Original.Effects[3].Parameters.Visible = false;
    Original.Effects[5].OverrideOrigin     = std::array<double, 3>{ 1.0 / 7.0, 0.0, -1.0 / 9.0 };
    if (!Original.Fields.empty()) Original.Fields.front().Enabled = false;
    std::printf("space: %zu effects, %zu forces\n", Original.Effects.size(), Original.Fields.size());

    // ② write, read, write: one text
    const std::string First = EncodeParticleSpace(Original);
    ParticleSpace     Read;
    Expect(DecodeParticleSpace(First, Read, Diagnostic), "the written space reads back (" + Diagnostic + ")");
    const std::string Second = EncodeParticleSpace(Read);
    Expect(First == Second, "write → read → write gives the same text");
    std::printf("text: %zu bytes, %s\n", First.size(), First == Second ? "identical after the round trip" : "DIFFERENT after the round trip");

    // ③ every effect, force and setting survives bit for bit
    Expect(Read.Effects.size() == Original.Effects.size(), "effect count");
    size_t SameEffects = 0;
    for (size_t Index = 0; Index < std::min(Read.Effects.size(), Original.Effects.size()); ++Index)
    {
        const ParticleEffectInstance& A = Original.Effects[Index];
        const ParticleEffectInstance& B = Read.Effects[Index];
        const bool Equal = A.Token == B.Token && A.Name == B.Name && A.PresetIdentifier == B.PresetIdentifier && A.Number == B.Number &&
                           A.OverrideOrigin.has_value() == B.OverrideOrigin.has_value() && (!A.OverrideOrigin || Same(*A.OverrideOrigin, *B.OverrideOrigin)) &&
                           SameParameters(A.Parameters, B.Parameters);
        SameEffects += Equal ? 1 : 0;
        Expect(Equal, "effect " + A.Token + " (" + A.PresetIdentifier + ") survives");
    }
    std::printf("effects: %zu / %zu identical\n", SameEffects, Original.Effects.size());
    Expect(Read.Fields.size() == Original.Fields.size(), "force count");
    for (size_t Index = 0; Index < std::min(Read.Fields.size(), Original.Fields.size()); ++Index)
    {
        const ParticleField& A = Original.Fields[Index];
        const ParticleField& B = Read.Fields[Index];
        Expect(A.Category == B.Category && A.Name == B.Name && A.Enabled == B.Enabled && Same(A.Centre, B.Centre) && Same(A.Direction, B.Direction) &&
                   Same(A.Strength, B.Strength) && Same(A.Radius, B.Radius) && A.Reach == B.Reach && A.Falloff == B.Falloff && A.ActsOn == B.ActsOn,
               "force " + std::to_string(Index + 1) + " (" + A.Name + ") survives");
    }
    Expect(Same(Read.Camera.Yaw, Original.Camera.Yaw) && Same(Read.Camera.Pitch, Original.Camera.Pitch) && Same(Read.Camera.Target, Original.Camera.Target),
           "camera");
    Expect(Same(Read.Wind.WindScale, Original.Wind.WindScale) && Read.Wind.ShowArrows, "wind settings");
    Expect(Read.LightningWeb.Enabled && Read.LightningWeb.Links == 3 && !Read.Lightning.Automatic, "lightning settings");
    Expect(Same(Read.TimeScale, 0.7) && Same(Read.Probe, Original.Probe), "clock and probe");
    Expect(Read.NextNumber == Original.NextNumber, "next effect number");

    // ④ the read space plans the same GPU work as the original
    ParticleSpace Fresh;
    Fresh.ConstructDefault(Library);
    for (const ParticlePreset& Preset : Library.Presets) (void)Fresh.AddEffect(Library, Preset.Identifier);
    ParticleSpace FreshRead;
    (void)DecodeParticleSpace(EncodeParticleSpace(Fresh), FreshRead, Diagnostic);
    size_t SameJobs = 0, Jobs = 0;
    for (int Refresh = 0; Refresh < 30; ++Refresh)
    {
        const ParticleRefreshPlan A = Fresh.Advance(1.0 / 60.0, 1280, 720);
        const ParticleRefreshPlan B = FreshRead.Advance(1.0 / 60.0, 1280, 720);
        Expect(A.Jobs.size() == B.Jobs.size(), "refresh " + std::to_string(Refresh) + " job count");
        for (size_t Job = 0; Job < std::min(A.Jobs.size(), B.Jobs.size()); ++Job)
        {
            ++Jobs;
            const bool Equal = std::memcmp(&A.Jobs[Job].Uniform, &B.Jobs[Job].Uniform, sizeof(A.Jobs[Job].Uniform)) == 0 &&
                               std::memcmp(&A.Jobs[Job].FibreUniform, &B.Jobs[Job].FibreUniform, sizeof(A.Jobs[Job].FibreUniform)) == 0 &&
                               A.Jobs[Job].Release == B.Jobs[Job].Release && A.Jobs[Job].FibrePath == B.Jobs[Job].FibrePath;
            SameJobs += Equal ? 1 : 0;
        }
        Expect(std::memcmp(&A.Constants, &B.Constants, sizeof(A.Constants)) == 0 && std::memcmp(&A.Fields, &B.Fields, sizeof(A.Fields)) == 0,
               "refresh " + std::to_string(Refresh) + " constants and fields");
    }
    Expect(SameJobs == Jobs, "every job uniform of 30 refreshes is identical");
    std::printf("refresh plans: %zu / %zu job uniforms identical over 30 refreshes\n", SameJobs, Jobs);

    // ⑤ the level's Force Field joins the lattice as one more prevailing flow while the space follows it
    {
        ParticleSpace Still, Windy, Waiting;
        for (ParticleSpace* Space : { &Still, &Windy, &Waiting }) Space->ConstructDefault(Library);
        Windy.LevelWind.Follow    = true;
        Windy.LevelWind.FlowMps   = std::array<double, 3>{ 6.0, 0.5, -2.0 };
        Waiting.LevelWind.Follow  = true;   // no Force Field sampled yet
        const ParticleRefreshPlan A = Still.Advance(1.0 / 60.0, 640, 360);
        const ParticleRefreshPlan B = Windy.Advance(1.0 / 60.0, 640, 360);
        const ParticleRefreshPlan C = Waiting.Advance(1.0 / 60.0, 640, 360);
        const auto StillFlows = static_cast<uint32_t>(A.Constants[39]), WindyFlows = static_cast<uint32_t>(B.Constants[39]);
        Expect(WindyFlows == StillFlows + 1, "following the level adds one flow (" + std::to_string(StillFlows) + " → " + std::to_string(WindyFlows) + ")");
        Expect(std::memcmp(&A.Components, &C.Components, sizeof(A.Components)) == 0, "following without a sampled flow changes nothing");
        std::printf("level wind: %u flows still, %u following 6.3 m/s\n", StillFlows, WindyFlows);
        ParticleSpace Reread;
        Windy.LevelWind.AnchorMetres = { 120.0, -45.5, 8.0 };
        Expect(DecodeParticleSpace(EncodeParticleSpace(Windy), Reread, Diagnostic) && Reread.LevelWind.Follow && Same(Reread.LevelWind.AnchorMetres, Windy.LevelWind.AnchorMetres) &&
                   !Reread.LevelWind.FlowMps,
               "the file keeps following and the anchor, never the sampled flow");
    }

    // ⑥ the drag volume and the current reach the field program (types 4 and 5) instead of staying authored only
    {
        ParticleSpace Boxes;
        Boxes.ConstructDefault(Library);
        ParticleField Drag = ConstructParticleField(ParticleFieldCategory::Drag);
        Drag.Enabled = true, Drag.Centre = { 0.0, 1.0, 0.0 }, Drag.Radius = 3.0, Drag.Strength = 2.0;
        ParticleField Current = ConstructParticleField(ParticleFieldCategory::Current);
        Current.Enabled = true, Current.Centre = { 1.0, 1.0, 0.0 }, Current.Radius = 5.0, Current.Strength = 4.0, Current.Direction = { 2.0, 0.0, 0.0 };
        Boxes.Fields.push_back(Drag);
        Boxes.Fields.push_back(Current);
        Expect(EvaluateParticleFieldPackable(Drag) && EvaluateParticleFieldPackable(Current), "drag volume and current have a GPU path");
        const ParticleRefreshPlan Plan  = Boxes.Advance(1.0 / 60.0, 640, 360);
        const auto                Count = static_cast<uint32_t>(Plan.Fields[0]);
        bool                      DragPacked = false, CurrentPacked = false;
        for (uint32_t Entry = 0; Entry < Count; ++Entry)
        {
            const float* A = &Plan.Fields[(1u + 3u * Entry) * 4u];
            const float* B = A + 4;
            if (B[0] == 4.0f) DragPacked = A[3] == 3.0f && B[1] == 2.0f;
            if (B[0] == 5.0f) CurrentPacked = A[3] == 5.0f && B[1] == 4.0f && B[2] == 0.0f && B[3] == 0.0f;
        }
        Expect(DragPacked, "the drag volume packs as type 4: half size 3 m, 2 per second");
        Expect(CurrentPacked, "the current packs as type 5 with its flow (4, 0, 0) m/s");
        std::printf("box volumes: drag %s, current %s, %u field entries\n", DragPacked ? "packed" : "MISSING", CurrentPacked ? "packed" : "MISSING", Count);
    }

    // ⑦ the file on disk
    const std::filesystem::path File = Scratch / "Validation.particlespace";
    Expect(WriteParticleSpace(Original, File, Diagnostic), "write " + File.filename().string() + " (" + Diagnostic + ")");
    ParticleSpace FromFile;
    Expect(ReadParticleSpace(File, FromFile, Diagnostic), "read " + File.filename().string() + " (" + Diagnostic + ")");
    Expect(EncodeParticleSpace(FromFile) == First, "the file holds the same text");
    ParticleSpaceSummary Summary;
    Expect(SummarizeParticleSpace(File, Summary, Diagnostic) && Summary.Effects == Original.Effects.size() &&
               Summary.VisibleEffects == Original.Effects.size() - 1 && Summary.Forces == Original.Fields.size(),
           "the inspector's summary");

    // ⑧ malformed files are refused and leave the space alone
    const auto Refused = [&](const std::string& Text, const char* Claim)
    {
        ParticleSpace Kept = Read;
        std::string   Reason;
        const bool    Decoded = DecodeParticleSpace(Text, Kept, Reason);
        Expect(!Decoded && !Reason.empty() && EncodeParticleSpace(Kept) == Second, std::string("refuses ") + Claim + " (" + Reason + ")");
        std::printf("refused %s: %s\n", Claim, Reason.c_str());
    };
    Refused("format = \"frontier.levelspace\"\nversion = 1\n", "another format");
    Refused("format = \"frontier.particlespace\"\nversion = 99\n", "a newer version");
    Refused("format = \"frontier.particlespace\"\nversion = 1\n[[force]]\ncategory = \"whirlpool\"\n", "an unknown force");
    Refused("format = \"frontier.particlespace\"\nversion = 1\n[[effect]]\npreset = \"a\"\nnumber = 2\n[[effect]]\npreset = \"b\"\nnumber = 2\n",
            "a number used twice");
    Refused("format = \"frontier.particlespace\"\nversion = 1\n[[effect]]\npreset = \"a\"\ncapacity = \"many\"\n", "a mistyped parameter");
    Refused("format = \"frontier.particlespace\"\nversion = 1\n[[effect]\n", "broken TOML");

    std::printf("%s %d / %d assertions\n", Failures == 0 ? "🟢" : "🔴", Assertions - Failures, Assertions);
    return Failures == 0 ? 0 : 1;
}
