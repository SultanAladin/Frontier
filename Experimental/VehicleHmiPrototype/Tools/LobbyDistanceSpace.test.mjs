import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { SceneStructure, OccupantRole } from "../Source/FacetStructure.js";
import { OccupantDistanceSpace } from "../Source/OccupantDistanceSpace.js";

const Root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

globalThis.GPUTextureUsage = { STORAGE_BINDING: 1, TEXTURE_BINDING: 2 };
globalThis.GPUBufferUsage = { STORAGE: 1, UNIFORM: 2 };
globalThis.GPUShaderStage = { COMPUTE: 1 };

async function LoadLobbyScene()
{
    const PreviousLocation = globalThis.location, PreviousFetch = globalThis.fetch;
    globalThis.location = { pathname: "/Lobby.html", search: "" };
    globalThis.fetch = async (Relative) =>
    {
        const Bytes = fs.readFileSync(path.join(Root, Relative));
        return { ok: true, arrayBuffer: async () => Bytes.buffer.slice(Bytes.byteOffset, Bytes.byteOffset + Bytes.byteLength) };
    };
    try
    {
        const Scene = new SceneStructure();
        await Scene.ConstructScene("Archive", 0);
        return Scene;
    }
    finally
    {
        globalThis.location = PreviousLocation;
        globalThis.fetch = PreviousFetch;
    }
}

function CopyScene(Scene, ShareVariants = true)
{
    return {
        Occupants: Scene.Occupants,
        FacetSets: Scene.FacetSets.map((Set) => {
            const Copy = { ...Set, VolumeIndex: -1 };
            if (!ShareVariants) delete Copy.DistanceSource;
            return Copy;
        })
    };
}

function MockExchange()
{
    const Stats = { Submissions: 0 };
    const Pass = { setPipeline() {}, setBindGroup() {}, dispatchWorkgroups() {}, end() {} };
    const Device = {
        limits: { maxTextureDimension3D: 8192 },
        queue: { submit() { Stats.Submissions++; }, async onSubmittedWorkDone() {} },
        createCommandEncoder() { return { beginComputePass() { return Pass; }, finish() { return {}; } }; }
    };
    return {
        Stats, Device,
        ConstructTexture(Description) { Stats.TextureExtent = Description.size; return { createView() { return {}; } }; },
        ConstructDescriptorLayout() { return {}; },
        ConstructDescriptorGroup() { return {}; },
        ConstructRecordsFrom() { return { destroy() {} }; }
    };
}

async function Construct(Scene)
{
    const Exchange = MockExchange();
    const Space = new OccupantDistanceSpace(Exchange, { async ConstructComputeProgram() { return {}; } });
    const Progress = [];
    await Space.ConstructVolumes(Scene, 2.2, (_, Fraction) => Progress.push(Fraction));
    return { Space, Exchange, Progress };
}

test("Lobby material variants share only geometry-identical vehicle distance volumes", async () =>
{
    const Lobby = await LoadLobbyScene();
    const Variants = Lobby.FacetSets.map((Set, Index) => ({ Set, Index })).filter(({ Set }) => Set.DistanceSource !== undefined);
    assert.equal(Variants.length, 12); // four bodies and four left/right wheel pairs
    assert.equal(new Set(Variants.map(({ Set }) => Set.DistanceSource)).size, 3);
    for (const { Set } of Variants)
    {
        const Source = Lobby.FacetSets[Set.DistanceSource];
        assert.equal(Set.DistanceVoxel, Source.DistanceVoxel);
        assert.deepEqual(Set.LocalMinimum, Source.LocalMinimum);
        assert.deepEqual(Set.LocalMaximum, Source.LocalMaximum);
        assert.equal(Set.Facets.length, Source.Facets.length);
        assert.ok(Set.Facets.some((Facet, Index) => Facet.Family !== Source.Facets[Index].Family), "material really was remapped");
        for (let Index = 0; Index < Set.Facets.length; ++Index)
            assert.deepEqual(Set.Facets[Index].Corners, Source.Facets[Index].Corners, "triangle geometry and winding must match");
    }

    // The old path allocated a separate volume for each variant. Compare its placement-independent parameters
    // with the shared path for every actual distance occupant, using the real scene and a mock transfer queue.
    const SharedScene = CopyScene(Lobby), SeparateScene = CopyScene(Lobby, false);
    const Shared = await Construct(SharedScene), Separate = await Construct(SeparateScene);
    assert.equal(Separate.Space.Volumes.length - Shared.Space.Volumes.length, 12);
    assert.ok(Shared.Exchange.Stats.Submissions < Separate.Exchange.Stats.Submissions);
    assert.ok(Shared.Space.SpaceExtent[2] < Separate.Space.SpaceExtent[2]);
    assert.equal(Shared.Progress.at(-1), 1);
    assert.ok(Shared.Progress.every((Value, Index, All) => Index === 0 || Value >= All[Index - 1]));
    for (const Occupant of Lobby.Occupants.filter((Item) => Item.Roles & OccupantRole.Distance))
    {
        const A = Shared.Space.QueryVolume(SharedScene, Occupant.FacetSet);
        const B = Separate.Space.QueryVolume(SeparateScene, Occupant.FacetSet);
        assert.ok(A && B);
        assert.equal(A.VoxelExtent, B.VoxelExtent);
        assert.deepEqual(A.Minimum, B.Minimum);
        assert.deepEqual(A.Resolution, B.Resolution);
    }
    for (const { Set, Index } of Variants)
        assert.equal(SharedScene.FacetSets[Index].VolumeIndex, SharedScene.FacetSets[Set.DistanceSource].VolumeIndex);
});

test("a material variant can construct its source volume even without a source occupant", async () =>
{
    const Lobby = await LoadLobbyScene();
    const VariantIndex = Lobby.FacetSets.findIndex((Set) => Set.DistanceSource !== undefined);
    const Scene = CopyScene(Lobby);
    Scene.Occupants = [{ FacetSet: VariantIndex, Roles: OccupantRole.Distance }];
    const { Space, Exchange } = await Construct(Scene);
    assert.equal(Space.Volumes.length, 1);
    assert.equal(Space.Volumes[0].FacetSet, Scene.FacetSets[VariantIndex].DistanceSource);
    assert.equal(Scene.FacetSets[VariantIndex].VolumeIndex, 0);
    assert.ok(Exchange.Stats.Submissions > 0);
});
