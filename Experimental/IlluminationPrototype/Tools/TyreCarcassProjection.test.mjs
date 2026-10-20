import test from "node:test";
import assert from "node:assert/strict";
import { TyreCarcassProjection, CarcassParticleCount } from "../Source/TyreCarcassProjection.js";
import { VehicleSolver } from "../Source/VehicleSolver.js";
import { VehicleVisualState, VehicleRecordBytes } from "../Source/VehicleVisualState.js";
import { SummarizeDurations, TimingComparison } from "../Source/TimingComparison.js";

const Command = { Throttle: 0, Brake: 0, Steer: 0, Handbrake: false };

test("XPBD uses nonzero accumulated constraint multipliers, converges and enforces road nonpenetration", () =>
{
    const A = TyreCarcassProjection.ProjectCompression(0.02, 220000, 80);
    const B = TyreCarcassProjection.ProjectCompression(0.02, 220000, 160);
    assert.ok(A.Multipliers.Contact.some(C => C > 0));
    assert.ok(A.Multipliers.Attachment.some(C => C < 0));
    assert.ok(A.Multipliers.Circumference.some(C => C !== 0));
    assert.ok(A.Multipliers.Volume !== 0);
    assert.ok(Math.abs(A.Reaction / B.Reaction - 1) < 0.001);
    for (let Index = 0; Index < CarcassParticleCount; ++Index)
    {
        const Height = -(0.50855 - A.RadialDisplacement[Index]) * Math.cos(Index * 2 * Math.PI / CarcassParticleCount);
        assert.ok(Height >= -(0.50855 - 0.02) - 1e-9, Height);
        const Mirror = (CarcassParticleCount - Index) % CarcassParticleCount;
        assert.ok(Math.abs(A.RadialDisplacement[Index] - A.RadialDisplacement[Mirror]) < 2e-6);
    }
});

test("radial response is monotone, pressure-sensitive, finite and returns to the rest volume", () =>
{
    const Nominal = new TyreCarcassProjection(220000);
    const Flat = new TyreCarcassProjection(0);
    assert.ok(Nominal.EvaluateReaction(0.02) > Flat.EvaluateReaction(0.02) * 2);
    let Previous = 0;
    for (let Index = 0; Index <= 140; ++Index)
    {
        const Reaction = Nominal.ResolveCompression(Index / 1000);
        assert.ok(Number.isFinite(Reaction) && Reaction >= Previous);
        assert.ok(Nominal.Volume > 0.02 && Nominal.Volume <= 0.034001);
        Previous = Reaction;
    }
    Nominal.ResolveCompression(0);
    assert.equal(Nominal.Reaction, 0);
    assert.equal(Nominal.Volume, 0.034);
    assert.throws(() => Nominal.AssignPressure(NaN));
    assert.throws(() => TyreCarcassProjection.ProjectCompression(-1, 220000));
});

test("XPBD response participates in chassis equilibrium and matches 30/60/120Hz outer timesteps", () =>
{
    const Vehicles = [30, 60, 120].map(Rate =>
    {
        const Vehicle = new VehicleSolver([]);
        Vehicle.Respawn([-100, -34, 0.53], 0);
        for (let Tick = 0; Tick < Rate * 3; ++Tick) Vehicle.Advance(Command, 1 / Rate);
        assert.ok(Vehicle.Wheels.every(Wheel => Wheel.CarcassActive && Wheel.Load > 0));
        return Vehicle;
    });
    for (const Vehicle of Vehicles)
    {
        assert.ok(Math.abs(Vehicle.Position[2] - Vehicles[0].Position[2]) < 1e-5);
        assert.ok(Vehicle.Wheels.every(Wheel => Number.isFinite(Wheel.TyreForce)));
    }
    const Point = new VehicleSolver([]);
    Point.XpbdEnabled = false;
    Point.Respawn([-100, -34, 0.53], 0);
    for (let Tick = 0; Tick < 360; ++Tick) Point.Advance(Command, 1 / 120);
    assert.ok(Math.abs(Point.Wheels[0].TyreDeflection - Vehicles[0].Wheels[0].TyreDeflection) > 1e-4);
    const Vehicle = Vehicles[0];
    Vehicle.ShredTyre(0);
    Vehicle.Advance(Command, 1 / 60);
    assert.equal(Vehicle.Wheels[0].CarcassActive, false);
});

test("render record uploads the exact XPBD lattice used by the tyre force solve", () =>
{
    const Vehicle = new VehicleSolver([]);
    for (let Tick = 0; Tick < 120; ++Tick) Vehicle.Advance(Command, 1 / 120);
    const Encoded = new VehicleVisualState().EncodeRecord({}, Vehicle.Wheels, Vehicle.Orientation);
    assert.equal(Encoded.byteLength, VehicleRecordBytes);
    assert.equal(VehicleRecordBytes, 7888);
    for (let Wheel = 0; Wheel < 4; ++Wheel)
    {
        assert.equal(Encoded[21 + Wheel * 4], 1);
        for (let Particle = 0; Particle < CarcassParticleCount; ++Particle)
            assert.equal(Encoded[52 + Wheel * 32 + Particle], Math.fround(Vehicle.Wheels[Wheel].Carcass.RadialDisplacement[Particle]));
    }
});

test("paired timing capture restores settings and reports CPU/GPU samples separately", () =>
{
    const Host = { Settings: { ClusteredLights: true }, Vehicle: { Frozen: false },
        Metrics: { Enabled: true, SampleIndex: 0, LastDurations: { Direct: 2 } },
        ApplySetting() {}, Exchange: { QueryAdapterDescription: () => "test adapter" },
        ScreenExtent: [100, 100], TraceExtent: [50, 50], GradeName: "low" };
    const Comparison = new TimingComparison(Host);
    Comparison.Start("ClusteredLights", 1, 3);
    for (let Index = 0; Index < 8; ++Index) { ++Host.Metrics.SampleIndex; Comparison.Observe(1); }
    assert.equal(Comparison.Active, false);
    assert.equal(Comparison.Report.Results.length, 2);
    assert.equal(Comparison.Report.Results[0].GpuNamedWorkloadsMs.Median, 2);
    assert.equal(Host.Settings.ClusteredLights, true);
    assert.equal(Host.Vehicle.Frozen, false);
    assert.deepEqual(SummarizeDurations([]), null);
});

test("gas pressure responds to XPBD volume rather than temperature alone", async () =>
{
    const { TyreThermalSolver } = await import('../Source/TyreThermalSolver.js');
    const A = new TyreThermalSolver(), B = new TyreThermalSolver();
    const Reading = { SlidingPower: 0, ContactFraction: 0, Airspeed: 0, Shredded: false, Volume: 0.034 };
    const Ignition = { Enabled: false };
    A.Advance(Array.from({ length: 4 }, () => ({ ...Reading })), 1 / 60, Ignition);
    B.Advance(Array.from({ length: 4 }, () => ({ ...Reading, Volume: 0.030 })), 1 / 60, Ignition);
    assert.ok(B.Wheels[0].GaugePressure > A.Wheels[0].GaugePressure + 30000);
});

test("chase-camera orbit does not decay back behind the vehicle", async () =>
{
    const { ChaseCameraSolver } = await import('../Source/ChaseCameraSolver.js');
    const Camera = new ChaseCameraSolver();
    Camera.OrbitYaw = 1.2;
    for (let Tick = 0; Tick < 600; ++Tick) Camera.AdvanceChase([0, 0, 1], [1, 0, 0], 20, 1 / 60);
    assert.equal(Camera.OrbitYaw, 1.2);
});
