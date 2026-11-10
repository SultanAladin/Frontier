import test from "node:test";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

import {
    EmitVehicleCameraFacets,
    EmitCineCameraFacets,
    ComputeCameraOptics,
    FocalLengthFromVerticalFov,
    VerticalFovFromFocalLength
} from "../Source/CameraVisuals.js";
import { ConstructMaterialSpecification } from "../Source/MaterialSpecification.js";
import { CinematicCamera } from "../Source/CinematicCamera.js";
import {
    CreateDefaultArraySpec,
    DecomposeTransform,
    ComposeEulerRad,
    ComputeArrayPlacements
} from "../Source/ArrayModifier.js";
import { TransformGizmoController } from "../Source/TransformGizmo.js";
import { ConstructRigidTransform } from "../Source/TransformProjection.js";

const Here = dirname(fileURLToPath(import.meta.url));
const Root = resolve(Here, "..");

test("low-poly 3D Vehicle Camera and Cine Camera meshes are finite and strictly non-emissive", () =>
{
    const Materials = ConstructMaterialSpecification();
    for (const [Name, Facets] of [
        ["Vehicle Camera", EmitVehicleCameraFacets()],
        ["Cine Camera", EmitCineCameraFacets()]
    ])
    {
        assert.ok(Facets.length >= 24, `${Name} has rich low-poly geometry (${Facets.length} triangles)`);
        for (const Facet of Facets)
        {
            assert.equal(Facet.Corners.length, 3);
            for (const Corner of Facet.Corners)
            {
                assert.ok(Corner.every((V) => Number.isFinite(V)), `${Name} corner coordinates are finite`);
            }
            const Mat = Materials[Facet.Family];
            assert.ok(Mat, `${Name} material family ${Facet.Family} exists`);
            const EmissionSum = (Mat.Emission?.[0] || 0) + (Mat.Emission?.[1] || 0) + (Mat.Emission?.[2] || 0);
            assert.equal(EmissionSum, 0, `${Name} uses non-emissive material family ${Facet.Family}`);
        }
    }
});

test("thin-lens camera optics and vertical FOV conversions round-trip accurately", () =>
{
    const Fov = VerticalFovFromFocalLength(50, 24);
    const Back = FocalLengthFromVerticalFov(Fov, 24);
    assert.ok(Math.abs(Back - 50) < 1e-6);

    const Wide = ComputeCameraOptics(24, 2.0, 5.0);
    const Tele = ComputeCameraOptics(85, 8.0, 5.0);
    assert.ok(Wide.HorizontalFov > Tele.HorizontalFov);
    assert.ok(Wide.VerticalFov > Tele.VerticalFov);
    assert.ok(Tele.Pupil < 85 / 1.4);
    assert.ok(Wide.Near > 0 && Wide.Near < 5.0);
    assert.ok(Wide.Far > 5.0);
});

test("CinematicCamera spline choreography flows continuously across shot boundaries with zero cuts", () =>
{
    const Director = new CinematicCamera();
    const Identity = ConstructRigidTransform([0, 0, 0.55], [0, 0, 0, 1]);
    let Prev = Director.Step(0.0, Identity, 0.0, true);
    let MaxStepJump = 0.0;
    const SeenShots = new Set([Prev.ShotIndex]);

    // Step 60 seconds at 20 Hz across all 6 Lobby studio keyframes + periodic wrap-around
    for (let Step = 0; Step < 1200; ++Step)
    {
        const Next = Director.Step(0.05, Identity, 0.0, true);
        const Jump = Math.hypot(
            Next.Eye[0] - Prev.Eye[0],
            Next.Eye[1] - Prev.Eye[1],
            Next.Eye[2] - Prev.Eye[2]
        );
        if (Jump > MaxStepJump) MaxStepJump = Jump;
        // Studio softbox & dome containment
        assert.ok(Math.hypot(Next.Eye[0], Next.Eye[1]) <= 18.6, "eye stays inside studio cyclorama");
        assert.ok(Next.Eye[2] >= 0.3 && Next.Eye[2] <= 5.45, "eye stays clear of floor and overhead softbox");
        SeenShots.add(Next.ShotIndex);
        Prev = Next;
    }

    assert.ok(SeenShots.size >= 5, "tour visits studio keyframes around the car");
    assert.ok(MaxStepJump < 0.35, `no instantaneous cut across shot boundaries (max 50ms delta = ${MaxStepJump.toFixed(3)} m)`);

    // Manual shot skip blends smoothly from current eye instead of cutting
    const BeforeSkip = Prev.Eye.slice();
    Director.AdvanceShot(true, 2);
    const ImmediatelyAfterSkip = Director.Step(0.016, Identity, 0.0, true);
    const SkipDelta = Math.hypot(
        ImmediatelyAfterSkip.Eye[0] - BeforeSkip[0],
        ImmediatelyAfterSkip.Eye[1] - BeforeSkip[1],
        ImmediatelyAfterSkip.Eye[2] - BeforeSkip[2]
    );
    assert.ok(SkipDelta < 0.25, `manual shot skip starts from current pose smoothly (delta = ${SkipDelta.toFixed(3)} m)`);
});

test("ArrayModifier computes Linear, Circular, Radial, and Grid 3D placements and round-trips transforms", () =>
{
    const Quat = ComposeEulerRad(0.15, -0.22, 0.65);
    const Mat = ConstructRigidTransform([3.5, -2.0, 1.25], Quat, [1.4, 0.8, 1.1]);
    const Decomp = DecomposeTransform(Mat);
    assert.ok(Math.hypot(Decomp.Position[0] - 3.5, Decomp.Position[1] + 2.0, Decomp.Position[2] - 1.25) < 1e-4);
    assert.ok(Math.abs(Decomp.Scale[0] - 1.4) < 1e-4 && Math.abs(Decomp.Scale[1] - 0.8) < 1e-4);

    const Linear = ComputeArrayPlacements(Mat, CreateDefaultArraySpec({ Enabled: true, Mode: "linear", Count: 5, StepX: 2.0 }));
    assert.equal(Linear.All.length, 5);
    assert.equal(Linear.Extra.length, 4);

    const Circular = ComputeArrayPlacements(Mat, CreateDefaultArraySpec({ Enabled: true, Mode: "circular", Count: 8, Radius: 6.0 }));
    assert.equal(Circular.All.length, 8);
    assert.equal(Circular.Extra.length, 7);

    const Radial = ComputeArrayPlacements(Mat, CreateDefaultArraySpec({ Enabled: true, Mode: "radial", Count: 6, Rings: 3, Radius: 4.0 }));
    assert.equal(Radial.All.length, 18);
    assert.equal(Radial.Extra.length, 17);

    const Grid = ComputeArrayPlacements(Mat, CreateDefaultArraySpec({
        Enabled: true, Mode: "grid", Columns: 3, Rows: 2, Layers: 2, ScaleProfile: "linear", EndScale: 0.5
    }));
    assert.equal(Grid.All.length, 12);
    assert.equal(Grid.Extra.length, 11);
});

test("TransformGizmoController G/R/S shortcuts work in EDIT mode and never collide with WASD camera flight", () =>
{
    let AppliedPose = null;
    const MockHost = {
        Settings: { RunMode: "EDIT" },
        FreeCamera: false,
        RmbFlying: false,
        CameraState: { Position: [0, -8, 2], Forward: [0, 1, 0], Right: [1, 0, 0], Up: [0, 0, 1], TanY: 0.5, Aspect: 1.6 },
        Canvas: { getBoundingClientRect: () => ({ left: 0, top: 0, width: 800, height: 500 }) },
        Shell: { Selected: "prop-0" },
        QueryEntityTransformPose: () => ({ Position: [0, 0, 1], Quaternion: [0, 0, 0, 1], Rotation: [0, 0, 0], Scale: [1, 1, 1] }),
        ApplyEntityTransformPose: (_Id, Pose) => { AppliedPose = Pose; }
    };

    const SavedDoc = globalThis.document;
    globalThis.document = { getElementById: () => null };
    try
    {
        const Gizmo = new TransformGizmoController(MockHost);
        Gizmo.PointerClient = [420, 260];

        // 1. In PLAY mode, G/R/S are ignored so WASD driving is untouched
        MockHost.Settings.RunMode = "PLAY";
        assert.equal(Gizmo.OnKeyDown({ code: "KeyS", preventDefault() {} }), false);
        assert.equal(Gizmo.OnKeyDown({ code: "KeyG", preventDefault() {} }), false);

        // 2. In EDIT mode while RMB flying or F8 FreeCamera is active, KeyS NEVER steals backward camera flight
        MockHost.Settings.RunMode = "EDIT";
        Gizmo.SetRmbHeld(true);
        assert.equal(Gizmo.OnKeyDown({ code: "KeyS", preventDefault() {} }), false);
        Gizmo.SetRmbHeld(false);
        MockHost.FreeCamera = true;
        assert.equal(Gizmo.OnKeyDown({ code: "KeyS", preventDefault() {} }), false);

        // 3. In normal EDIT mode (not flying), S / G / R enter Blender modal and axis constraints X/Y/Z work
        MockHost.FreeCamera = false;
        Gizmo.LastFlyAt = -10000;
        assert.equal(Gizmo.OnKeyDown({ code: "KeyS", preventDefault() {} }), true);
        assert.equal(Gizmo.Modal?.ModalKind, "scale");
        assert.equal(Gizmo.OnKeyDown({ code: "KeyX", shiftKey: false, preventDefault() {} }), true);
        assert.equal(Gizmo.Modal?.Constraint, "x");
        Gizmo.CancelModal(true);
        assert.equal(Gizmo.Modal, null);
        assert.ok(AppliedPose !== null);
    }
    finally
    {
        globalThis.document = SavedDoc;
    }
});

test("Lobby Studio brings up cleanly under HeadlessHarness with studio lights, 3D cameras, primitives, arrays, and cockpit exposure", () =>
{
    const Run = spawnSync(process.execPath, ["Tools/HeadlessHarness.mjs", ".", "5", "capture=1&lobby=1"], {
        cwd: Root,
        encoding: "utf8",
        env: { ...process.env, BACKEND: "null", SIZE: "320x180", HOOKS: resolve(Here, "EditorAuthoringHooks.mjs") },
        timeout: 45000
    });
    assert.equal(Run.status, 0, `Lobby harness exited cleanly:\n${Run.stdout}\n${Run.stderr}`);
    assert.match(Run.stdout, /0 errors, 0 warnings/);
});
