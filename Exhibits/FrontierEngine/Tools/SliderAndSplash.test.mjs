//============================================================================================================================================
//                                                        SLIDERANDSPLASH.TEST.MJS
//============================================================================================================================================
// 🧩 Unit tests for SliderControl, LevelAtlas, Studio Lobby session, CinematicCamera, HMI SurfaceTextures, SDF ceiling, and LogStream.

import test from "node:test";
import assert from "node:assert/strict";

import { ResolveSliderDigits, QuantizeSliderScalar, ConstructSliderControl } from "../Source/SliderControl.js";
import { LevelAtlas, ResolveLevelKey, QueryLevelEntry, FormatLevelSearch } from "../Source/LevelAtlas.js";
import { ResolveLobbyPlacements, LobbyVehiclePlacements } from "../Source/LobbyStructure.js";
import { LobbyDrivers, LobbyGuestPool } from "../Source/LobbyDrivers.js";
import { LobbySession } from "../Source/LobbySession.js";
import { CinematicCamera } from "../Source/CinematicCamera.js";
import { VehicleAppearance, SurfaceTextures } from "../Source/VehicleAppearance.js";
import { VehicleVisualState } from "../Source/VehicleVisualState.js";
import { SceneStructure } from "../Source/FacetStructure.js";
import { SceneDistanceSpace } from "../Source/SceneDistanceSpace.js";
import { LogStreamEnclosure } from "../Source/LogStreamEnclosure.js";

function InstallMinimalDom()
{
    class TestNode
    {
        constructor(Tag)
        {
            this.tagName = Tag.toUpperCase();
            this.className = "";
            this.textContent = "";
            this.value = "";
            this.type = "";
            this.children = [];
            this.Listeners = {};
            this.Attributes = {};
            this.StyleProperties = {};
            this.style = {
                setProperty: (Name, Scalar) => { this.StyleProperties[Name] = Scalar; }
            };
        }
        appendChild(Node) { this.children.push(Node); return Node; }
        append(...Nodes) { Nodes.forEach((Node) => this.appendChild(Node)); }
        setAttribute(Name, Scalar) { this.Attributes[Name] = String(Scalar); }
        getAttribute(Name) { return this.Attributes[Name]; }
        addEventListener(Name, Listener) { (this.Listeners[Name] ||= []).push(Listener); }
        dispatch(Name, Extra = {})
        {
            for (const Listener of (this.Listeners[Name] || []))
                Listener({ preventDefault() {}, stopPropagation() {}, target: this, ...Extra });
        }
        select() {}
        blur() { globalThis.document.activeElement = null; this.dispatch("blur"); }
    }
    globalThis.document = {
        activeElement: null,
        createElement: (Tag) => new TestNode(Tag)
    };
    return TestNode;
}

test("SliderControl quantizes, formats ValuePill digits, updates fill ratio, and resets on double-click", () =>
{
    const TestNode = InstallMinimalDom();
    assert.equal(ResolveSliderDigits(0, 1), 0);
    assert.equal(ResolveSliderDigits(0, 0.05), 2);
    assert.equal(QuantizeSliderScalar(0.374, 0.0, 1.0, 0.05), 0.35);
    assert.equal(QuantizeSliderScalar(5.0, -2.0, 2.0, 0.25), 2.0);

    const Container = new TestNode("div");
    const Target = { Exposure: 0.5 };
    const Finalized = [];
    const Handle = ConstructSliderControl(Container, Target, "Exposure", {
        Label: "Exposure",
        Minimum: -2.0,
        Maximum: 2.0,
        Step: 0.25,
        Unit: "EV",
        Digits: 2,
        OnFinalize: (Key) => Finalized.push(Key)
    });

    const SliderRoot = Container.children[0];
    const Header = SliderRoot.children[0];
    const Track = SliderRoot.children[1];
    const Pill = Header.children[1];
    const NumberBox = Pill.children[0];
    const UnitCell = Pill.children[1];

    assert.equal(UnitCell.textContent, "EV");
    assert.equal(NumberBox.value, "0.50");
    assert.equal(SliderRoot.StyleProperties["--slider-fill"], "62.500%");

    Track.value = "1.26";
    Track.dispatch("input");
    assert.equal(Target.Exposure, 1.25);
    assert.equal(NumberBox.value, "1.25");
    assert.equal(SliderRoot.StyleProperties["--slider-fill"], "81.250%");

    NumberBox.value = "-1.0";
    NumberBox.dispatch("keydown", { key: "Enter" });
    assert.equal(Target.Exposure, -1.0);
    assert.equal(SliderRoot.StyleProperties["--slider-fill"], "25.000%");

    Track.dispatch("dblclick");
    assert.equal(Target.Exposure, 0.5);
    assert.ok(Finalized.length >= 3);

    Target.Exposure = -2.0;
    Handle.Refresh();
    assert.equal(SliderRoot.StyleProperties["--slider-fill"], "0.000%");
});

test("LevelAtlas resolves Proving Grounds and Studio Lobby keys and formats query parameters", () =>
{
    globalThis.location = { search: "?quality=high" };
    assert.equal(LevelAtlas.length, 2);
    assert.equal(ResolveLevelKey(null), "proving");
    assert.equal(ResolveLevelKey("unknown"), "proving");
    assert.equal(ResolveLevelKey("garage"), "lobby");
    assert.equal(ResolveLevelKey("lobby"), "lobby");
    assert.equal(QueryLevelEntry("lobby").Title, "Lobby · Studio");
    assert.equal(QueryLevelEntry("proving").Relief, true);
    assert.equal(QueryLevelEntry("lobby").Relief, false);

    assert.equal(FormatLevelSearch("lobby", true), "?quality=high&level=lobby&lobby=1&autostart=1");
    assert.equal(FormatLevelSearch("proving", false), "?quality=high&level=proving");
    globalThis.location = { search: "" };
});

test("Studio Lobby scene builds 5 turntables, 8-driver guest pool, and live LobbySession player joins/leaves", async () =>
{
    assert.equal(LobbyDrivers.length, 5);
    assert.ok(LobbyGuestPool.length >= 8);
    assert.equal(LobbyVehiclePlacements.length, 4);
    assert.ok(ResolveLobbyPlacements().length >= 5);

    globalThis.location = { search: "?lobby=1" };
    try
    {
        const fs = await import("node:fs");
        const RealFetch = globalThis.fetch;
        globalThis.fetch = async (RelPath) =>
        {
            const Bytes = fs.readFileSync(new URL(`../${RelPath}`, import.meta.url));
            return { ok: true, arrayBuffer: async () => Bytes.buffer.slice(Bytes.byteOffset, Bytes.byteOffset + Bytes.byteLength) };
        };
        try
        {
            const Scene = new SceneStructure();
            await Scene.ConstructScene("Archive", 0);
            assert.equal(Scene.GuestBays.length, 4);
            assert.ok(Scene.GuestPlateSets.size >= 8);

            const Session = new LobbySession({ Scene, PriorValid: true });
            Session.Settings.AutoSimulate = false;
            assert.equal(Session.QueryActive(), true);
            assert.equal(Session.QueryOccupiedCount(), 5);

            assert.equal(Session.TriggerLeave(0), true);
            Session.Advance(2.0);
            assert.equal(Session.Bays[0].Phase, "empty");
            assert.equal(Session.QueryOccupiedCount(), 4);

            assert.equal(Session.TriggerJoin(0), true);
            assert.equal(Session.Bays[0].Phase, "entering");
            Session.Advance(2.0);
            assert.equal(Session.Bays[0].Phase, "parked");
            assert.equal(Session.QueryOccupiedCount(), 5);
        }
        finally
        {
            globalThis.fetch = RealFetch;
        }
    }
    finally
    {
        globalThis.location = { search: "" };
    }
});

test("CinematicCamera advances studio and course shots and HMI SurfaceTextures encode cleanly", () =>
{
    const Director = new CinematicCamera();
    const Identity = new Float32Array([1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0.5, 1]);
    const StudioPose = Director.Step(1.2, Identity, 0.0, true);
    assert.ok(Number.isFinite(StudioPose.Eye[0]) && StudioPose.Fov > 20);
    const Next = Director.AdvanceShot(true, 1);
    assert.ok(Next.Name.startsWith("02"));

    assert.equal(SurfaceTextures.length, 4);
    const Appearance = new VehicleAppearance(null, null);
    assert.equal(Appearance.AssignSurfaceTexture(2), 2);
    const Visual = new VehicleVisualState();
    const Encoded = new Float32Array(Visual.EncodeRecord({ BodyTexture: 2 }, [{}, {}, {}, {}], [0, 0, 0, 1]));
    assert.equal(Encoded[2], 2);
});

test("SceneDistanceSpace encodes InverseVoxelExtent and CeilingHeight in the 160-byte DistanceRecord", () =>
{
    const Space = new SceneDistanceSpace(null, null, {
        ClipCount: 4,
        Resolution: 128,
        FinestVoxel: 0.125,
        BandVoxels: 6,
        HitFraction: 0.2
    });
    Space.CeilingHeight = 11.25;
    const Bytes = Space.EncodeDistanceRecord(42, 7);
    assert.equal(Bytes.byteLength, 160);
    const Scalars = new Float32Array(Bytes);
    assert.equal(Scalars[7], 8.0);
    assert.equal(Scalars[15], 4.0);
    assert.equal(Scalars[23], 2.0);
    assert.equal(Scalars[31], 1.0);
    assert.equal(Scalars[39], 11.25);
});

test("LogStreamEnclosure records, deduplicates, filters by severity/channel/verbosity/search, and clears entries", () =>
{
    globalThis.document = { getElementById: () => null };
    const Stream = new LogStreamEnclosure();
    Stream.RecordDiagnostic("Render", "WebGPU device active", "DeviceExchange.js", "info", "display");
    Stream.RecordDiagnostic("Render", "WebGPU device active", "DeviceExchange.js", "info", "display");
    Stream.RecordDiagnostic("Network", "VALKYRIE (DIAMOND II) joined Bay 1", "LobbySession.js", "info", "display");
    Stream.RecordDiagnostic("Tyre", "Tyre burst on rear left (thermal)", "TyreThermalSolver.js", "warning", "warning");
    Stream.RecordDiagnostic("Render", "Shader validation failure", "ShaderInterchange.js", "error", "error");

    assert.equal(Stream.TotalRecorded, 5);
    assert.equal(Stream.Entries.length, 4);
    assert.equal(Stream.Entries[0].RepeatCount, 2);
    assert.equal(Stream.SeverityTallies.warning, 1);
    assert.equal(Stream.SeverityTallies.error, 1);

    Stream.ActiveChannel = "Network";
    assert.equal(Stream.Entries.filter((Entry) => Stream.MatchesFilter(Entry)).length, 1);
    Stream.ClearEntries();
    assert.equal(Stream.Entries.length, 0);
});
