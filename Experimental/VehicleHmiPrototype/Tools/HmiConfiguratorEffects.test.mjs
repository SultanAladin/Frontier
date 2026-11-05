import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";

import { SceneStructure } from "../Source/FacetStructure.js";
import { ConstructMaterialSpecification, CourseMaterial } from "../Source/MaterialSpecification.js";
import { RayTracingSpace } from "../Source/RayTracingSpace.js";
import { HmiViewportRenderer } from "../Source/HmiViewportRenderer.js";
import { CabinSequence } from "../Source/CabinSequence.js";
import { VehicleAppearance, PaintOptions } from "../Source/VehicleAppearance.js";
import { PickVehicle } from "../Source/VehiclePicking.js";
import { MaterialTransition, PaintRippleDuration, PaintRippleMaxRadius } from "../Source/Effects/MaterialTransition.js";
import { BuildVehicleEdgeBuffer } from "../Source/Effects/DihedralEdges.js";
import { BuildScanSparkleBuffer, DefaultSparkleCount } from "../Source/Effects/ScanSparkles.js";
import { VehicleScanController, VehicleViewMode, ScanStartX, ScanEndX } from "../Source/Effects/VehicleScanEffect.js";

const Here = path.dirname(fileURLToPath(import.meta.url));
const Root = path.resolve(Here, "..");

let CachedLobbyScene = null;
async function LoadLobbyScene()
{
    if (CachedLobbyScene)
        return CachedLobbyScene;
    const PriorLocation = globalThis.location;
    const PriorFetch    = globalThis.fetch;
    globalThis.location = { pathname: "/Lobby.html", search: "?lobby=1" };
    globalThis.fetch = async (Relative) =>
    {
        const Bytes = fs.readFileSync(path.join(Root, Relative));
        return { ok: true, arrayBuffer: async () => Bytes.buffer.slice(Bytes.byteOffset, Bytes.byteOffset + Bytes.byteLength) };
    };
    try
    {
        const Scene = new SceneStructure();
        await Scene.ConstructScene("Archive", 0);
        Scene.ConstructDrawRuns();
        CachedLobbyScene = Scene;
        return Scene;
    }
    finally
    {
        globalThis.location = PriorLocation;
        globalThis.fetch    = PriorFetch;
    }
}

test("HMI ReSTIR viewport isolates strictly 1 hero vehicle from guest lobby fleet in draw runs and BVH TLAS", async () =>
{
    const Scene = await LoadLobbyScene();
    assert.equal(Scene.DisplayOccupants.length, 20);

    const Excluded = HmiViewportRenderer.ResolveExcludedOccupants(Scene);
    assert.equal(Excluded.size, 24); // 4 guest cars × (1 body + 4 wheels + 1 plate)

    const FilteredRuns = Scene.ConstructFilteredDrawRuns(Excluded);
    const DrawnOccupants = new Set();
    for (const Run of FilteredRuns)
        for (let Offset = 0; Offset < Run.Count; ++Offset)
            DrawnOccupants.add(Run.First + Offset);

    for (let Hero = 0; Hero < 5; ++Hero)
        assert.ok(DrawnOccupants.has(Hero), `hero occupant ${Hero} must remain visible`);
    for (const Guest of Scene.DisplayOccupants)
        assert.ok(!DrawnOccupants.has(Guest), `guest occupant ${Guest} must be excluded from HMI viewport`);

    const RayTracing = new RayTracingSpace(null);
    RayTracing.ConstructResources(Scene, null, null);
    const FullInstances = RayTracing.ActiveInstances;
    const FilteredTlas = RayTracing.ConstructFilteredView(Scene, Excluded, null, null);
    assert.ok(FilteredTlas.UsedTlasNodes > 0 && FilteredTlas.UsedTlasNodes < RayTracing.UsedTlasNodes);
    assert.ok(FullInstances >= 25);
});

test("HMI camera orbits around the single hero vehicle and ray-picks 3D body-local drop coordinates", async () =>
{
    const Scene = await LoadLobbyScene();
    const Cabin = new CabinSequence({ CanvasFormat: "rgba8unorm" }, null);
    const Renderer = new HmiViewportRenderer(null, null, "high");

    const StartYaw = Cabin.Preview.Yaw;
    Cabin.Advance(0.5, null);
    assert.ok(Cabin.Preview.Yaw > StartYaw, "turntable auto-spin rotates HMI camera around vehicle");

    const SpunYaw = Cabin.Preview.Yaw;
    Cabin.Preview.Orbit(0.35, 0.08);
    assert.ok(Math.abs(Cabin.Preview.Yaw - (SpunYaw + 0.35)) < 1e-6);
    Cabin.Advance(0.2, null);
    assert.ok(Math.abs(Cabin.Preview.Yaw - (SpunYaw + 0.35)) < 1e-6, "manual drag-orbit holds auto-spin briefly");

    const BodyTransform = Scene.Occupants[0].Transform;
    const Camera = Renderer.ResolveCamera(BodyTransform, Cabin.Preview);
    assert.ok(Math.hypot(Camera.Position[0], Camera.Position[1], Camera.Position[2]) > 5.5);

    Renderer.Exchange = { Device: { queue: { writeBuffer() {} } } };
    Renderer.FrameRecords = {};
    Renderer.FilteredTlas = { TraversalGroup: {} };
    Renderer.NearestDepth = { TraversalGroups: [{}, {}] };
    Renderer.Surface = { RecordSurface() {}, RecordDirect() {}, Views: { Depth: {} } };
    Renderer.Resampling = { RecordResampling() {}, RecordReflection() {} };
    Renderer.Diffuse = { Record() {} };
    Renderer.Glossy = { Record() {} };
    Renderer.Display = { RecordResolve() {}, RecordPresent() {} };
    Renderer.RayTracing = { UpdateTlas() {} };
    Renderer.Distance = { TraversalGroup: {} };
    Renderer.ExcludedOccupants = HmiViewportRenderer.ResolveExcludedOccupants(Scene);
    Cabin.ForceGarage = true;

    Renderer.Record({}, {
        Scene, VertexRecords: {}, Vehicle: { Wheels: [{}, {}, {}, {}] }, BodyTransform,
        Light: { Direction: [0, 0, 1], Illuminance: [1, 1, 1] }, HostSettings: { SkyScale: 0.3 },
        SamplingGroup: {}, Cabin
    });

    const CenterHit = PickVehicle(Scene, Cabin.PreviewInverse, 0.5, 0.52);
    assert.ok(CenterHit !== null, "centre ray hits hero vehicle");
    assert.equal(CenterHit.Occupant, 0);
    assert.equal(CenterHit.BodyPoint.length, 3);
    assert.ok(Math.abs(CenterHit.BodyPoint[0]) <= 3.2 && Math.abs(CenterHit.BodyPoint[1]) <= 1.35);
    assert.equal(PickVehicle(Scene, Cabin.PreviewInverse, 0.02, 0.02), null, "corner ray misses vehicle");
});

test("1:1 3D cubic-out material ripple transitions paint across 6.4 m over 1.35 s without extra Materials slot writes", () =>
{
    const Materials = ConstructMaterialSpecification();
    const Writes = [];
    const RippleUploads = [];
    const Ripple = new MaterialTransition(Materials.length, (Offset, Data) => RippleUploads.push([Offset, Data.byteLength]));
    const Appearance = new VehicleAppearance(
        Materials,
        (Offset, Data) => Writes.push([Offset, Data.byteLength]),
        (Prev, Next, Origin) => Ripple.Begin(Prev, Next, Origin)
    );

    Appearance.Apply({ Colour: PaintOptions[0].Hex, Matte: PaintOptions[0].Matte });
    assert.equal(Ripple.Active, false);
    assert.deepEqual(Writes.map(([Offset]) => Offset), [6 * 64, 10 * 64]);

    const DropOrigin = [1.2, -0.6, 0.75];
    Appearance.Apply({ Colour: PaintOptions[4].Hex, Matte: PaintOptions[4].Matte, RippleOrigin: DropOrigin });
    assert.equal(Ripple.Active, true);
    assert.equal(Ripple.Duration, PaintRippleDuration);
    assert.equal(Ripple.MaxRadius, PaintRippleMaxRadius);
    assert.deepEqual(Ripple.Origin, DropOrigin);

    for (let Step = 0; Step < 4; ++Step)
        Ripple.Advance(0.10, null);
    assert.ok(Ripple.Radius > 2.5 && Ripple.Radius < 5.5);
    assert.equal(Ripple.EvaluateMixAtLocalPoint(DropOrigin), 1.0);
    assert.equal(Ripple.EvaluateMixAtLocalPoint([-3.1, 1.2, 0.2]), 0.0);

    for (let Step = 0; Step < 12; ++Step)
        Ripple.Advance(0.10, null);
    assert.equal(Ripple.Active, false);
    assert.equal(Ripple.Radius, PaintRippleMaxRadius);
    assert.equal(Ripple.EvaluateMixAtLocalPoint([-3.1, 1.2, 0.2]), 1.0);
});

test("Effects/ X-Ray Scan extracts 14°/28° dihedral edges, 130 #8fd8f2 sparkles, and 6.6 s wavefront cycle", async () =>
{
    const Scene = await LoadLobbyScene();
    const Edges = BuildVehicleEdgeBuffer(Scene);
    assert.ok(Edges.BodyEdgeCount > 400, `expected >400 14° body edges, got ${Edges.BodyEdgeCount}`);
    assert.ok(Edges.WheelEdgeCount > 200, `expected >200 28° wheel edges, got ${Edges.WheelEdgeCount}`);
    assert.equal(Edges.EdgeCount, Edges.BodyEdgeCount + 4 * Edges.WheelEdgeCount);

    const Sparkles = BuildScanSparkleBuffer();
    assert.equal(Sparkles.Count, DefaultSparkleCount);
    assert.equal(Sparkles.Count, 130);

    const Scan = new VehicleScanController();
    for (let Step = 0; Step < 10; ++Step)
        Scan.Advance(0.05, VehicleViewMode.Scan);
    assert.ok(Scan.Active);
    assert.ok(Scan.ScanX < ScanStartX && Scan.ScanX > ScanEndX);
    assert.ok(Math.abs(Scan.CurtainAlpha1 / Scan.CurtainAlpha0 - 4.0) < 1e-5, "dual wavefront curtains maintain 0.30 / 0.075 ratio");
    assert.ok(Scan.SparkleAlpha > 0.0);

    for (let Step = 0; Step < 85; ++Step)
        Scan.Advance(0.05, VehicleViewMode.Scan);
    assert.ok(Math.abs(Scan.ScanX - ScanEndX) < 1e-5);
    assert.ok(Scan.HoloAmount > 0.95);
});

test("Dawn WebGPU validates full 1024x640 ReSTIR HMI configurator + X-Ray Scan + Material Ripple with 0 errors", () =>
{
    const Harness = path.join(Here, "HeadlessHarness.mjs");
    const Output = execFileSync(process.execPath, [Harness, Root, "10", "?lobby=1&hmi=garage&xray=1&capture=1"], {
        cwd: Root,
        encoding: "utf8",
        timeout: 120000
    });
    assert.match(Output, /=== harness: 10 frames, 0 errors, 0 warnings/);
});
