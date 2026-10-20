import test from "node:test";
import assert from "node:assert/strict";
import { MembraneStiffness, TetrahedralStiffness, ConstructTyreContinuum,
    SolveTyreContact, TyreContinuumElasticity, TyreSurfaceCount } from "../Source/TyreContinuumElasticity.js";
import { VehicleSolver } from "../Source/VehicleSolver.js";
import { VehicleVisualState, VehicleRecordBytes } from "../Source/VehicleVisualState.js";
import { TimingComparison } from "../Source/TimingComparison.js";

const Dot = (A, B) => A.reduce((Sum, Entry, Index) => Sum + Entry * B[Index], 0);
const Energy = (Stiffness, Displacement) => Stiffness.reduce((Sum, Row, Index) => Sum + Displacement[Index] * Dot(Row, Displacement) / 2, 0);
const Command = { Throttle: 0, Brake: 0, Steer: 0, Handbrake: false };

test("CST membrane passes a plane-stress constant-strain energy patch and rigid-motion tests", () =>
{
    const Points = [[0, 0, 0], [1, 0, 0], [0, 1, 0]], Young = 7e6, Poisson = 0.35, Thickness = 0.009;
    const { Stiffness } = MembraneStiffness(Points, Young, Poisson, Thickness);
    const Displacement = Points.flatMap(([First]) => [First * 0.01, 0, 0]);
    const Expected = 0.5 * Thickness * Young / (1 - Poisson ** 2) * 0.01 ** 2 / 2;
    assert.ok(Math.abs(Energy(Stiffness, Displacement) / Expected - 1) < 1e-12);
    for (const Rigid of [Points.flatMap(() => [1, 2, 3]), Points.flatMap(([First, Second]) => [-Second, First, 0])])
        assert.ok(Math.abs(Energy(Stiffness, Rigid)) < 1e-8);
    for (let Row = 0; Row < 9; ++Row)
        for (let Column = 0; Column < 9; ++Column) assert.ok(Math.abs(Stiffness[Row][Column] - Stiffness[Column][Row]) < 1e-8);
});

test("solid tetrahedra pass constant-strain energy, shear and rigid-motion patches", () =>
{
    const Points = [[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]], Young = 7e6, Poisson = 0.35;
    const { Stiffness, Volume } = TetrahedralStiffness(Points, Young, Poisson);
    assert.ok(Math.abs(Volume - 1 / 6) < 1e-15);
    const Lambda = Young * Poisson / ((1 + Poisson) * (1 - 2 * Poisson)), Mu = Young / (2 * (1 + Poisson));
    const Stretch = Points.flatMap(([First]) => [First * 0.01, 0, 0]);
    const Shear = Points.flatMap(([, Second]) => [Second * 0.01, 0, 0]);
    assert.ok(Math.abs(Energy(Stiffness, Stretch) / (Volume * (Lambda + 2 * Mu) * 0.01 ** 2 / 2) - 1) < 1e-12);
    assert.ok(Math.abs(Energy(Stiffness, Shear) / (Volume * Mu * 0.01 ** 2 / 2) - 1) < 1e-12);
    for (const Rigid of [Points.flatMap(() => [1, 2, 3]), Points.flatMap(([First, Second]) => [-Second, First, 0])])
        assert.ok(Math.abs(Energy(Stiffness, Rigid)) < 1e-8);
});

for (const Category of ["shell", "solid"])
{
    test(`${Category} topology and assembled elastic energy retain rigid-motion null modes`, () =>
    {
        const Continuum = ConstructTyreContinuum(Category);
        assert.equal(Continuum.Points.length, Category === "shell" ? 112 : 224);
        assert.equal(Continuum.Triangles.length, 192);
        assert.equal(Continuum.Tetrahedra.length, Category === "shell" ? 0 : 576);
        const Translate = Continuum.Points.flatMap(() => [0.1, -0.3, 0.2]);
        const Rotate = Continuum.Points.flatMap(([First, Second]) => [-Second * 0.1, First * 0.1, 0]);
        for (const Rigid of [Translate, Rotate])
        {
            const Energy = Continuum.Rows.reduce((Sum, Row, Index) => Sum + Rigid[Index] *
                [...Row].reduce((Product, [Column, Coefficient]) => Product + Coefficient * Rigid[Column], 0) / 2, 0);
            assert.ok(Math.abs(Energy) < 1e-6, `${Category}: ${Energy}`);
        }
    });

    test(`${Category} FEM solves contact equilibrium, fixed beads, symmetry, pressure and nonpenetration`, () =>
    {
        const Continuum = ConstructTyreContinuum(Category);
        const Nominal = new TyreContinuumElasticity(Category, 220000), Flat = new TyreContinuumElasticity(Category, 0);
        assert.equal(Nominal.ResolveCompression(0), 0);
        assert.equal(Nominal.Volume, 0.034);
        let Previous = 0;
        for (const Sample of Nominal.Curve)
        {
            assert.ok(Number.isFinite(Sample.Reaction) && Sample.Reaction >= Previous - 1e-3);
            assert.ok(Sample.Residual < 0.005, `KKT residual ${Sample.Residual} N`);
            Previous = Sample.Reaction;
        }
        const Solved = SolveTyreContact(Continuum, 220000, 0.02);
        const Converged = SolveTyreContact(Continuum, 220000, 0.02, 1600);
        assert.ok(Math.abs(Solved.Reaction / Converged.Reaction - 1) < 1e-5);
        assert.ok(Solved.Contacts > 0 && Solved.Volume < 0.034 && Solved.Volume > 0.02);
        assert.ok(Nominal.EvaluateReaction(0.02) > Flat.EvaluateReaction(0.02));
        for (let Corner = 0; Corner < Continuum.Points.length; ++Corner)
        {
            const Shift = Solved.Displacement.slice(Corner * 3, Corner * 3 + 3);
            if (Continuum.Fixed[Corner]) assert.ok(Shift.every(Entry => Entry === 0));
            else assert.ok(Continuum.Points[Corner][2] + Shift[2] >= -0.50855 + 0.02 - 1e-8);
            const Section = Corner % 7, Angle = Math.floor((Corner % TyreSurfaceCount) / 7);
            const Mirror = Math.floor(Corner / TyreSurfaceCount) * TyreSurfaceCount + ((16 - Angle) % 16) * 7 + Section;
            assert.ok(Math.abs(Shift[2] - Solved.Displacement[Mirror * 3 + 2]) < 1e-6);
        }
        Nominal.ResolveCompression(0.14);
        for (let Corner = 0; Corner < TyreSurfaceCount; ++Corner)
            assert.ok(Math.hypot(...Nominal.SurfaceDisplacement.slice(Corner * 4, Corner * 4 + 3)) < 0.25);
        assert.throws(() => Nominal.AssignPressure(NaN));
        assert.throws(() => SolveTyreContact(Continuum, -1, 0.02));
    });

    test(`${Category} FEM pressure interpolation avoids nearest-bin force steps`, () =>
    {
        const Tyre = new TyreContinuumElasticity(Category, 229999);
        const Before = Tyre.ResolveCompression(0.02);
        Tyre.AssignPressure(230001);
        const After = Tyre.ResolveCompression(0.02);
        assert.ok(Math.abs(After - Before) < 0.2);
        assert.ok(After > Before);
    });

    test(`${Category} FEM participates in chassis equilibrium, shader upload and damage fallback`, () =>
    {
        const Vehicle = new VehicleSolver([]);
        Vehicle.TyreModel = Category;
        Vehicle.Respawn([-100, -34, 0.53], 0);
        for (let Tick = 0; Tick < 360; ++Tick) Vehicle.Advance(Command, 1 / 120);
        assert.ok(Vehicle.Wheels.every(Wheel => Wheel.CarcassActive && Wheel.Load > 0 && Wheel.Carcass.Category === Category));
        assert.ok(Math.abs(Vehicle.LinearVelocity[2]) < 0.001);
        assert.ok(Math.hypot(...Vehicle.LinearVelocity) < 0.02);
        const Encoded = new VehicleVisualState().EncodeRecord({}, Vehicle.Wheels, Vehicle.Orientation);
        assert.equal(Encoded.byteLength, VehicleRecordBytes);
        assert.equal(VehicleRecordBytes, 7888);
        for (let Wheel = 0; Wheel < 4; ++Wheel)
        {
            assert.equal(Encoded[21 + Wheel * 4], Category === "shell" ? 2 : 3);
            for (let Coordinate = 0; Coordinate < TyreSurfaceCount * 4; ++Coordinate)
                assert.equal(Encoded[180 + Wheel * TyreSurfaceCount * 4 + Coordinate],
                    Math.fround(Vehicle.Wheels[Wheel].Carcass.SurfaceDisplacement[Coordinate]));
        }
        Vehicle.ShredTyre(0); Vehicle.Advance(Command, 1 / 60);
        assert.equal(Vehicle.Wheels[0].CarcassActive, false);
        for (const Next of ["spring", "xpbd", Category])
        {
            Vehicle.TyreModel = Next; Vehicle.Advance(Command, 1 / 60);
            assert.equal(Vehicle.Wheels[1].CarcassActive, Next !== "spring");
            assert.equal(Vehicle.Wheels[1].Carcass.Category || "xpbd", ["shell", "solid"].includes(Next) ? Next : "xpbd");
        }
    });
}

test("model A/B comparison runs physics, selects distinct models and restores settings", () =>
{
    let Resets = 0;
    const Host = { Settings: { TyreModel: "xpbd", XpbdTyres: false },
        Vehicle: { Frozen: false, Position: [1, 2, 3], QueryForward: () => [1, 0, 0], Respawn() { ++Resets; } },
        Metrics: { Enabled: false, SampleIndex: 0 }, ApplySetting() {},
        Exchange: { QueryAdapterDescription: () => "test" } };
    const Comparison = new TimingComparison(Host);
    Comparison.Start("TyreModel", 0, 2, ["shell", "solid"]);
    assert.equal(Host.Settings.TyreModel, "shell"); assert.equal(Host.Vehicle.Frozen, false);
    Comparison.Observe(1); Comparison.Observe(1);
    assert.equal(Host.Settings.TyreModel, "solid");
    Comparison.Observe(2); Comparison.Observe(2);
    assert.equal(Resets, 2);
    assert.equal(Host.Settings.TyreModel, "xpbd"); assert.equal(Host.Settings.XpbdTyres, false);
    assert.equal(Comparison.Report.VehicleFrozen, false);
    assert.deepEqual(Comparison.Report.Results.map(Result => Result.Model), ["shell", "solid"]);
});
