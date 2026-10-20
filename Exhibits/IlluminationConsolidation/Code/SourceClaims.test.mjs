// Cross-source regression checks for claims used by the consolidation plan.
import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const Root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");
const Scratch = path.join(Root, "_ClaudeScratch/tmp");
const Sources = JSON.parse(fs.readFileSync(new URL("Sources.json", import.meta.url)));
const Read = (Name, File) => fs.readFileSync(path.join(Scratch, Name, File), "utf8");
const Import = (Name, File) => import(pathToFileURL(path.join(Scratch, Name, "Source", File)).href);

test("all five browser vehicles explicitly use point contact instead of an XPBD lattice", () => {
    for (const Source of Sources) {
        assert.match(Read(Source.Name, "Source/VehicleSolver.js"), /here each corner casts its strut axis/);
        if (Source.Name !== "c6cba543-slate") {
            assert.match(Read(Source.Name, "Source/TyreThermalSolver.js"), /there is no XPBD carcass here/);
        }
    }
});

test("the three Slate variants wire nearest-depth screen tracing into GI and reflections", () => {
    for (const Name of ["d984ba1b-slate", "b55d93c0-slate", "c6cba543-slate"]) {
        for (const File of ["ReservoirTemporal.wgsl", "ReflectionTraversal.wgsl"]) {
            assert.match(Read(Name, "Shader/" + File), /TraverseScreenDepth\(/);
        }
        assert.match(Read(Name, "Source/IlluminationHost.js"), /NearestDepth/);
    }
    for (const Name of ["85b411ac-frontier", "21530081-frontier"]) {
        assert.equal(fs.existsSync(path.join(Scratch, Name, "Shader/ScreenTraversal.wgsl")), false);
    }
});

test("emissive card radiance is separate from actual-light clustering", () => {
    for (const Name of ["d984ba1b-slate", "b55d93c0-slate", "85b411ac-frontier", "21530081-frontier"]) {
        assert.match(Read(Name, "Shader/CardFinal.wgsl"), /Albedo \* InversePi \* \(Direct \+ Indirect\) \+ Emission/);
        const Lighting = Read(Name, "Source/LightingSystem.js");
        assert.match(Lighting, /LowBeam/);
        assert.match(Lighting, /StreetLamp/);
    }
});

test("b55 has real index lists but a cell can drop contributing lights at its cap", async () => {
    const { LightingSystem, ClusterGridBounds, ClusterHeaderCells } = await Import("b55d93c0-slate", "LightingSystem.js");
    const Lighting = new LightingSystem();
    const Centre = ClusterGridBounds.Minimum.map((Minimum, Axis) => Minimum + 0.5 * ClusterGridBounds.Extent[Axis]);
    Lighting.Lights = Array.from({ length: 32 }, () => ({ Position: Centre, Reach: 1000 }));
    Lighting.AssignClusters();
    assert.equal(Lighting.ClusterOverflow, 16 * ClusterGridBounds.CellCount);
    assert.equal(Lighting.ClusterCells[ClusterHeaderCells * 4 + 1], 16);
});

test("d984 bitmask retains all 32 co-located actual lights", async () => {
    globalThis.GPUBufferUsage = { STORAGE: 128, COPY_DST: 8 };
    const { LightClusterSpace, LightClusterBounds } = await Import("d984ba1b-slate", "LightClusterSpace.js");
    const Space = new LightClusterSpace({ ConstructRecords: () => ({}) });
    const Packed = new Float32Array(32 * 16);
    for (let Index = 0; Index < 32; ++Index) {
        const Offset = Index * 16;
        Packed.set(LightClusterBounds.Minimum.map((Minimum, Axis) => Minimum + 0.5 * LightClusterBounds.Extent[Axis]), Offset);
        Packed[Offset + 7] = 1000;
        Packed[Offset + 15] = 1;
    }
    Space.Update({ queue: { writeBuffer() {} } }, Packed);
    assert.ok(Space.Masks.every(Mask => Mask === 0xffffffff));
});

test("the RTX label uses WGSL software BVH traversal and reuses the old screen-trace bit", () => {
    assert.match(Read("21530081-frontier", "Shader/RayTraversal.wgsl"), /fn TraceSceneRay\(/);
    assert.match(Read("21530081-frontier", "Source/IlluminationHost.js"), /RayTracing: 8192/);
    assert.match(Read("b55d93c0-slate", "Source/IlluminationHost.js"), /ScreenTraversal: 8192/);
});
