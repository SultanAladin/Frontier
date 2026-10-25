/*============================================================================================================================================
                                       BAKINGWORKSPACEPANELCONSTRUCTIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Stage 1 regression coverage for the Baking Studio native port:
//    (1) BakingWorkspacePanel constructs/Initializes/Activates cleanly, exactly as TextureStudioApplication does
//        unconditionally at startup (mirrors StampWorkspacePanelConstructionValidation's startup-crash guard).
//    (2) the mesh-bake engine (BakeMesh.h) actually bakes the three built-in demo shapes with a 100% ray-hit
//        rate for every implemented map — this is the exact ray-envelope self-bake bug caught during this
//        feature's development (a flat/convex low==high self-bake can silently miss every ray), so it stays
//        under test rather than relying on manual re-inspection in future changes.

#include "Engine/ContentInterchange/BakeMesh.h"
#include "UI/TextureStudio/BakingWorkspacePanel.h"
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

void ValidateShape(const std::string &Shape)
{
    using namespace Frontier;
    const DemoBakeAssets Assets = BuildDemoBakeAssets(Shape);
    Require(!Assets.Low.Parts.empty(), (Shape + ": low mesh must have at least one part").c_str());
    Require(!Assets.High.Parts.empty(), (Shape + ": high mesh must have at least one part").c_str());

    BakeSettings Settings;
    Settings.Resolution = 64; // small for a fast test; production default is 256+
    Settings.Channels = ImplementedMeshMapKeys();

    std::vector<const BakePart *> Targets;
    for (const auto &Part : Assets.Low.Parts) Targets.push_back(&Part);

    const auto Results = RunMeshBake(Targets, Assets.High, Settings);
    Require(Results.size() == Targets.size(), (Shape + ": one result expected per target part").c_str());
    for (const auto &Result : Results)
    {
        Require(Result.Stats.Hits > 0, (Shape + "/" + Result.Name + ": expected at least some ray hits").c_str());
        // 📝 The regression this guards: a one-directional ray envelope misses every single texel when
        // baking a mesh against an identical (or near-identical) high mesh, because a convex/flat surface
        // has nothing strictly "in front of" itself within a small search distance.
        const uint64_t Total = Result.Stats.Hits + Result.Stats.Misses;
        Require(Total == 0 || double(Result.Stats.Hits) / double(Total) > 0.95,
            (Shape + "/" + Result.Name + ": ray-envelope hit rate regressed below 95%").c_str());
        Require(!Result.Maps.empty(), (Shape + "/" + Result.Name + ": expected baked map images").c_str());
        for (const auto &Map : Result.Maps)
            Require(Map.Rgba.size() == size_t(Result.Resolution) * Result.Resolution * 4,
                (Shape + "/" + Result.Name + "/" + Map.Key + ": unexpected pixel buffer size").c_str());
    }
}
} // namespace

int main()
{
    using namespace Frontier;
    try
    {
        BakingWorkspacePanel Panel;
        Require(Panel.Initialize(), "BakingWorkspacePanel::Initialize must succeed");
        Require(Panel.Activate(), "BakingWorkspacePanel::Activate must succeed");

        Require(IsMeshMapImplemented("normal"), "Tangent normal must be reported as implemented");
        Require(IsMeshMapImplemented("world-normal"), "World normal must be reported as implemented");
        Require(IsMeshMapImplemented("coverage"), "UV coverage must be reported as implemented");
        Require(IsMeshMapImplemented("alpha"), "Alpha/hit mask must be reported as implemented");
        Require(!IsMeshMapImplemented("curvature"), "Curvature is Stage 2+ scope and must not claim implemented");

        ValidateShape("Cube");
        ValidateShape("Sphere");
        ValidateShape("Teapot");
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "BakingWorkspacePanelConstructionValidation failed: " << Failure.what() << "\n";
        return 1;
    }
    std::cout << "BakingWorkspacePanelConstructionValidation passed.\n";
    return 0;
}
