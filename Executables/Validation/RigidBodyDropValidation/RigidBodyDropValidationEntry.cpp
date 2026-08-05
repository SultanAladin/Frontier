/*============================================================================================================================================
                                                      RIGIDBODYDROPVALIDATIONENTRY.CPP
============================================================================================================================================*/
// 🧩 Physics validation host: authors the drop scene as workspace documents, lets RenderExtension load them through its ORDINARY saved-scene path,
//    stands up a Jolt world from the decoded placements, and advances it each frame — rewriting the instance transforms the visibility raster draws.
//
//    🔴 NOTHING IS HARDCODED IN THE ENGINE, AND THE ENGINE IS NOT MODIFIED. RenderExtension loads a fixed set of document FILENAMES out of a
//       relative "Assets" dir (FRONTIER_SCENE_ASSET_DIR, baked into Graphics.lib — this target cannot redefine it, since RenderExtension.cpp is
//       compiled into the pillar, not here). So the seam is the FILE, not the code: this host authors its scene INTO those very filenames beside the
//       exe. The renderer then loads "the scene it always loads" and gets the drop rig, with not one engine line changed.
//
//         SuzanneRadial.wsdoc   <- the crates: unit-box block + the tower's dynamic objects + the wrecker  (SceneChoice::RadialArray names this one)
//         CheckerFloor.wsdoc    <- the ground: slab block + one static object
//
//    The chain, and why each link is what it is:
//
//      AuthorRigidBodyDropDocuments      writes both documents beside the exe, under the names the loader already looks for
//      InitializeRenderExtension         decodes + uploads them exactly as it does any saved scene
//      LoadWorkspaceScene                this host re-decodes the crate document itself, for the instances + the mesh it must fit bounds over
//      InitializeRigidBodySimulation     one Jolt body per decoded placed object; the document IS the initial condition
//      DriveRigidBodyPresentLoop         advance -> writeback -> re-upload -> refit cull -> draw the control window, once per frame
//
//    🔴 The re-upload is cheap because the visibility raster's instance buffer is host-visible and mapped: UploadVisibilityScene is a memcpy into
//       mapped memory, not a staged transfer, which is what makes driving it every frame viable at all.

#include "Graphics/RenderExtension/RenderExtension.h"
#include "Graphics/Scene/WorkspaceDocumentDecoder.h"
#include "Graphics/Visibility/VisibilityRasterization.h"
#include "RigidBodyControlWindow.h"
#include "RigidBodyCullRefit.h"
#include "RigidBodyFrameDriver.h"
#include "RigidBodySceneAuthor.h"
#include "RigidBodySimulation.h"

#include "imgui.h"

#include <cstdio>
#include <string>

namespace
{
    // 🔴 These are the engine's OWN document names, not names of this host's choosing. RenderExtension.cpp resolves
    //    FRONTIER_SCENE_ASSET_DIR (default "Assets", relative to the working dir) + one of these. Authoring into them is the whole substitution
    //    mechanism — see the file header. Changing either string here without changing it in the engine silently loads the Suzanne heads instead.
    const char* const AssetDirectory     = "Assets";
    const char* const CrateDocumentName  = "SuzanneRadial.wsdoc";   // SuzanneSceneChoice::RadialArray, the default SceneChoice
    const char* const GroundDocumentName = "CheckerFloor.wsdoc";     // the floor slot, loaded unconditionally
}

int main()
{
    using namespace Frontier;

    // --- Author the scene documents ------------------------------------------------------------------------------------
    const RigidBodySceneProportions Proportions = {};
    const std::string CratePath  = std::string(AssetDirectory) + "/" + CrateDocumentName;
    const std::string GroundPath = std::string(AssetDirectory) + "/" + GroundDocumentName;
    if (!AuthorRigidBodyDropDocuments(Proportions, CratePath, GroundPath))
    {
        std::printf("[rigid-body-drop] could not author the scene documents into '%s' — is the directory present?\n", AssetDirectory);
        return 1;
    }
    std::printf("[rigid-body-drop] authored '%s' + '%s'\n", CratePath.c_str(), GroundPath.c_str());

    // --- Re-decode the crate document ourselves ------------------------------------------------------------------------
    // The renderer decodes these documents internally but keeps nothing past load, so this host decodes the crate document a second time for the two
    // things it needs: the placed instances it will drive, and the mesh stream it fits cull bounds over. Same file, same decoder, so the instance
    // ordering matches the renderer's byte for byte — which is what lets a writeback address the renderer's records by index.
    RenderVertexStream                CrateGeometry;
    std::vector<SuzanneSceneInstance> CrateInstances;
    WorkspaceDocument                 CrateDocument;
    if (!LoadWorkspaceScene(CratePath.c_str(), CrateGeometry, CrateInstances, &CrateDocument))
    {
        std::printf("[rigid-body-drop] could not decode '%s'.\n", CratePath.c_str());
        return 1;
    }
    if (CrateInstances.empty())
    {
        std::printf("[rigid-body-drop] the crate document decoded no instances — nothing to simulate.\n");
        return 1;
    }

    WorkspaceDocument GroundDocument;
    if (!DecodeWorkspaceDocument(GroundPath.c_str(), GroundDocument))
    {
        std::printf("[rigid-body-drop] could not decode '%s' for the ground collider.\n", GroundPath.c_str());
        return 1;
    }

    if (CrateGeometry.Vertices.empty())
    {
        std::printf("[rigid-body-drop] the crate document decoded no geometry — cannot fit cull bounds.\n");
        return 1;
    }

    // Fit the crate mesh's local bounds ONCE. The mesh never changes; only the transforms do, so every per-frame refit reuses this.
    // RenderVertex is interleaved position/normal/uv at stride 32, so the stride in FLOATS is 8 — the fit strides over that to read positions only.
    RigidBodyLocalBounds CrateBounds;
    FitRigidBodyLocalBounds(CrateGeometry.Vertices[0].Position, (uint32_t)CrateGeometry.Vertices.size(),
                            (uint32_t)(sizeof(RenderVertex) / sizeof(float)), CrateBounds);

    // --- Stand up the renderer over those documents --------------------------------------------------------------------
    RenderExtension Extension;
    Extension.SceneChoice = SuzanneSceneChoice::RadialArray;   // names SuzanneRadial.wsdoc — the crate document authored above

    if (!InitializeRenderExtension(Extension, "Frontier — Rigid Body Drop Validation", 1600, 900))
    {
        std::printf("[rigid-body-drop] render extension init failed.\n");
        return 1;
    }

    // --- Stand up the Jolt world from the DECODED placements -----------------------------------------------------------
    // Slot base 0: the crate document is the primary scene, so its instances start at the beginning of the instance vector. The ground is static and
    // takes no writeback slot at all, so its base is immaterial (passed 0 for the same reason).
    RigidBodyTuning     Tuning     = {};
    RigidBodySimulation Simulation = {};
    if (!InitializeRigidBodySimulation(Simulation, Proportions, CrateDocument, 0u, GroundDocument, 0u, Tuning))
    {
        std::printf("[rigid-body-drop] jolt world init failed.\n");
        FinalizeRenderExtension(Extension);
        return 1;
    }
    std::printf("[rigid-body-drop] %u dynamic bodies from '%s'\n", (unsigned)Simulation.Links.size(), CrateDocumentName);

    // --- The per-frame seam --------------------------------------------------------------------------------------------
    // The record buffer's persistent mapping. Acquired lazily inside the first refit and released after the loop, so no frame pays a map.
    RigidBodyCullMapping CullMapping = {};

    const auto DriveOneFrame = [&Extension, &Simulation, &Tuning, &CrateInstances, &CrateBounds, &CullMapping]()
    {
        // ImGui's own delta is the frame interval the substrate just presented — no separate clock to drift against it.
        const float FrameSeconds = ImGui::GetIO().DeltaTime;

        const RigidBodyWindowOutcome Outcome = DrawRigidBodyControlWindow(Extension.ImguiTheme, Tuning, Simulation.Readout);
        if (Outcome.ReseedRequested)
            ReseedRigidBodySimulation(Simulation, Tuning);

        if (Outcome.SingleAdvance)
        {
            // Step-once has to bypass the pause guard, so it advances through a locally un-paused copy of the tuning for exactly one interval.
            RigidBodyTuning SingleStep = Tuning;
            SingleStep.Advancing       = true;
            AdvanceRigidBodySimulation(Simulation, SingleStep, SingleStep.StepSeconds);
        }
        else
        {
            AdvanceRigidBodySimulation(Simulation, Tuning, FrameSeconds);
        }

        // Rewrite ONLY the transform fields of our decoded records, then re-upload. Tint / material / partition / mesh ordinal stay exactly as the
        // decoder wrote them, so every instance keeps the identity the overlay and shade passes resolve it by.
        TransferRigidBodyTransforms(Simulation, CrateInstances);
        UploadVisibilityScene(Extension.VisibilityRaster, CrateInstances);
        // 🔴 Both, every frame. The instance upload feeds the raster; the cull refit feeds the GPU cull, whose records are the mesh-local bounds
        //    transformed by Model and therefore stale the moment a body moves. Skipping this culls each body once it leaves its spawn bounds — and
        //    because the TLAS is rebuilt from live Model, the body keeps casting a correct shadow while rasterizing nothing at all.
        RefitRigidBodyCullRecords(Extension.InstanceCull, CullMapping, CrateInstances, CrateBounds);
    };

    DriveRigidBodyPresentLoop(Extension, DriveOneFrame);

    // 🔴 Unmap BEFORE FinalizeRenderExtension: it frees RecordMemory, and unmapping a freed allocation is undefined.
    ReleaseRigidBodyCullMapping(Extension.InstanceCull, CullMapping);

    // --- Report + tear down -------------------------------------------------------------------------------------------
    std::printf("[rigid-body-drop] closed after %.2f s simulated (%u steps); %u/%u bodies awake, %.2f J residual\n",
                (double)Simulation.Readout.ElapsedSeconds, (unsigned)Simulation.Readout.AdvanceOrdinal,
                (unsigned)Simulation.Readout.AwakeCount, (unsigned)Simulation.Readout.BodyCount,
                (double)Simulation.Readout.KineticEnergy);

    FinalizeRigidBodySimulation(Simulation);
    FinalizeRenderExtension(Extension);
    return 0;
}
