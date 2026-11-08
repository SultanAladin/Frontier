import assert from "node:assert/strict";
import test from "node:test";
import { CourseConstants, ResolveCourseHeight, ResolveCourseNormal, StuntBounds, StuntPark,
         EmitStuntTabletopFacets, EmitStuntHalfPipeFacets, EmitStuntBowlFacets, EmitStuntBankRingFacets,
         EmitStuntStraightBankFacets, EmitStuntFunboxFacets, EmitStuntWhoopsFacets, EmitStuntEntryWedgeFacets,
         EmitStuntRingFacets, ResolveStuntJoltBoxes } from "../Source/CourseStructure.js";
import { LevelShapes, ResolveLevelPlacements, ResolveConePlacements, TrainingDrills, ArenaZones,
         LevelClearZones, EmitLevelGroundFacets } from "../Source/LevelStructure.js";
import { GroundLayout } from "../Source/FacetStructure.js";
import { RecoveryDirector, RecoverySettingsDefaults } from "../Source/RecoveryDirector.js";

function Close(Actual, Expected, Epsilon = 1.0e-9)
{
    assert.ok(Math.abs(Actual - Expected) <= Epsilon, `expected ${Expected}, got ${Actual}`);
}

// 📝 The pad grew a third wing: the proving grounds (120 m) + the training arena (96 m) + the stunt park (96 m).
test("stunt park extends the pad east and the ground card tiles cover it", () => {
    assert.deepEqual(CourseConstants.PadMaximum, [252.0, 60.0]);
    assert.deepEqual(GroundLayout.TileCount, [26, 10]);
    const CoveredX = GroundLayout.PadMinimum[0] + GroundLayout.TileExtent * GroundLayout.TileCount[0];
    const CoveredY = GroundLayout.PadMinimum[1] + GroundLayout.TileExtent * GroundLayout.TileCount[1];
    assert.ok(CoveredX >= CourseConstants.PadMaximum[0]);
    assert.ok(CoveredY >= CourseConstants.PadMaximum[1]);
    assert.ok(StuntBounds.Maximum[0] <= CourseConstants.PadMaximum[0]);
});

test("legacy relief is unchanged: the jump ramp and the flat survive the stunt wing", () => {
    Close(ResolveCourseHeight(25.0, 0.0), 0.675);
    assert.equal(ResolveCourseHeight(100.0, 0.0), 0.0);
    assert.equal(ResolveCourseHeight(-40.0, 0.0), 0.0);
});

test("tabletops rise quad-up, hold a flat deck and fall quad-down, hard-edged at the sides", () => {
    const Big = StuntPark.Tabletops[2];
    const Lip = Big.X0 + Big.Up, DeckEnd = Lip + Big.Deck, Exit = DeckEnd + Big.Down;
    assert.equal(ResolveCourseHeight(Big.X0, Big.CentreY), 0.0);
    Close(ResolveCourseHeight(Big.X0 + 0.5 * Big.Up, Big.CentreY), 0.25 * Big.Height);
    Close(ResolveCourseHeight(Lip, Big.CentreY), Big.Height);
    Close(ResolveCourseHeight(0.5 * (Lip + DeckEnd), Big.CentreY), Big.Height);
    assert.equal(ResolveCourseHeight(Exit, Big.CentreY), 0.0);
    assert.equal(ResolveCourseHeight(Exit + 2.0, Big.CentreY), 0.0);
    assert.equal(ResolveCourseHeight(Lip, Big.CentreY - Big.HalfWidth - 0.5), 0.0);
    for (const Tabletop of StuntPark.Tabletops)
        Close(ResolveCourseHeight(Tabletop.X0 + Tabletop.Up, Tabletop.CentreY), Tabletop.Height);
});

test("half-pipe floor is flat, walls arc, decks hold, tapers melt to the flat", () => {
    const Pipe = StuntPark.HalfPipe;
    const MidX = 0.5 * (Pipe.X0 + Pipe.X1);
    assert.equal(ResolveCourseHeight(MidX, Pipe.CentreY), 0.0);
    const Rise = Pipe.Radius * (1.0 - Math.cos(Pipe.Angle * Math.PI / 180.0));
    const WallOuter = Pipe.Floor + Pipe.Radius * Math.sin(Pipe.Angle * Math.PI / 180.0);
    assert.ok(ResolveCourseHeight(MidX, Pipe.CentreY + Pipe.Floor + 1.0) > 0.0);
    Close(ResolveCourseHeight(MidX, Pipe.CentreY + WallOuter + 0.5 * Pipe.Deck), Rise);
    assert.equal(ResolveCourseHeight(MidX, Pipe.CentreY + WallOuter + Pipe.Deck + Pipe.Chamfer + 2.0), 0.0);
    assert.equal(ResolveCourseHeight(Pipe.X0, Pipe.CentreY + WallOuter), 0.0);
    assert.equal(ResolveCourseHeight(Pipe.X1, Pipe.CentreY - WallOuter), 0.0);
});

test("bowl floor is flat, rim deck holds, outer chamfer falls to the flat", () => {
    const Bowl = StuntPark.Bowl;
    const [CX, CY] = Bowl.Centre;
    const Rise = Bowl.Radius * (1.0 - Math.cos(Bowl.Angle * Math.PI / 180.0));
    assert.equal(ResolveCourseHeight(CX, CY), 0.0);
    Close(ResolveCourseHeight(CX + Bowl.DeckRadius - 0.5, CY), Rise);
    assert.equal(ResolveCourseHeight(CX + Bowl.DeckRadius + Rise + 1.0, CY), 0.0);
});

test("bank ring holds a 45-degree max bank the struts can drive", () => {
    const Ring = StuntPark.BankRing;
    const [CX, CY] = Ring.Centre;
    assert.equal(ResolveCourseHeight(CX, CY), 0.0);
    Close(ResolveCourseHeight(CX + Ring.Inner + 0.5 * Ring.Run, CY), 0.5 * Ring.Height);
    Close(ResolveCourseHeight(CX + Ring.Inner + Ring.Run + 1.0, CY), Ring.Height);
    // 📝 Smoothstep peaks at 1.5 × mean slope: 1.5 × 2.7 / 4 ≈ 1.01 ≈ 45°. The normal's z there reads the bank.
    const Normal = ResolveCourseNormal(CX + Ring.Inner + 0.5 * Ring.Run, CY);
    assert.ok(Normal[2] > 0.65 && Normal[2] < 0.75);
    Close(Math.hypot(Normal[0], Normal[1], Normal[2]), 1.0);
});

test("straight bank, funbox, whoops and entry wedge hold their profiles", () => {
    const Bank = StuntPark.StraightBank;
    assert.equal(ResolveCourseHeight(180.0, Bank.ToeY - 1.0), 0.0);
    Close(ResolveCourseHeight(180.0, Bank.CrestY + 1.0), Bank.Height);
    const Box = StuntPark.Funbox;
    Close(ResolveCourseHeight(0.5 * (Box.MinimumX + Box.MaximumX), 0.5 * (Box.MinimumY + Box.MaximumY)), Box.Height);
    assert.equal(ResolveCourseHeight(Box.MinimumX - Box.Run - 0.5, -40.0), 0.0);
    const Whoops = StuntPark.Whoops;
    assert.equal(ResolveCourseHeight(Whoops.X0, -49.0), 0.0);
    Close(ResolveCourseHeight(Whoops.X0 + 0.5 * Whoops.Length, -49.0), Whoops.Amplitude);
    const Wedge = StuntPark.EntryWedge;
    Close(ResolveCourseHeight(230.0, Wedge.YHigh), Wedge.Height);
    assert.equal(ResolveCourseHeight(230.0, Wedge.YLow), 0.0);
    assert.equal(ResolveCourseHeight(230.0, Wedge.YLow + 2.0), 0.0);
});

test("stunt relief max-composites: run-ups, plaza and landing corridor stay flat", () => {
    assert.equal(ResolveCourseHeight(166.0, 8.0), 0.0);      // plaza teleport
    assert.equal(ResolveCourseHeight(150.0, -17.0), 0.0);    // Big Air run-up
    assert.equal(ResolveCourseHeight(220.0, -17.0), 0.0);    // landing corridor
    assert.equal(ResolveCourseHeight(240.0, -17.0), 0.0);
    // 📝 The bank toe laps the Big Air tabletop's north edge: the max wins, never a hole, never a spike.
    const Lapped = ResolveCourseHeight(190.0, -13.5);
    assert.ok(Lapped >= 0.0 && Lapped <= StuntPark.Tabletops[2].Height);
});

function CheckFacets(Name, Facets)
{
    assert.ok(Facets.length > 100, `${Name} emits geometry`);
    for (const Facet of Facets)
    {
        assert.equal(Facet.Corners.length, 3);
        assert.ok(Number.isInteger(Facet.Family));
        for (const Corner of Facet.Corners)
            assert.ok(Corner.every(Number.isFinite));
        const E1 = [Facet.Corners[1][0] - Facet.Corners[0][0], Facet.Corners[1][1] - Facet.Corners[0][1], Facet.Corners[1][2] - Facet.Corners[0][2]];
        const E2 = [Facet.Corners[2][0] - Facet.Corners[0][0], Facet.Corners[2][1] - Facet.Corners[0][1], Facet.Corners[2][2] - Facet.Corners[0][2]];
        const Geometric = [E1[1] * E2[2] - E1[2] * E2[1], E1[2] * E2[0] - E1[0] * E2[2], E1[0] * E2[1] - E1[1] * E2[0]];
        assert.ok(Math.hypot(...Geometric) > 1.0e-9, `${Name} has no degenerate facet`);
        assert.equal(Facet.Normals.length, 3);
        // 📝 Each corner normal is unit (the average across a kink legitimately is not — the crease shades honestly).
        for (const Normal of Facet.Normals)
            Close(Math.hypot(...Normal), 1.0, 1.0e-6);
        const Average = [(Facet.Normals[0][0] + Facet.Normals[1][0] + Facet.Normals[2][0]) / 3,
                         (Facet.Normals[0][1] + Facet.Normals[1][1] + Facet.Normals[2][1]) / 3,
                         (Facet.Normals[0][2] + Facet.Normals[1][2] + Facet.Normals[2][2]) / 3];
        assert.ok(Geometric[0] * Average[0] + Geometric[1] * Average[1] + Geometric[2] * Average[2] > 0.0,
                  `${Name} winds counter-clockwise outward`);
    }
}

test("stunt emitters: finite corners, unit normals, outward winding, no degenerate facets", () => {
    CheckFacets("tabletop S", EmitStuntTabletopFacets(0));
    CheckFacets("tabletop M", EmitStuntTabletopFacets(1));
    CheckFacets("tabletop L", EmitStuntTabletopFacets(2));
    CheckFacets("half-pipe", EmitStuntHalfPipeFacets());
    CheckFacets("bowl", EmitStuntBowlFacets());
    CheckFacets("bank ring", EmitStuntBankRingFacets());
    CheckFacets("straight bank", EmitStuntStraightBankFacets());
    CheckFacets("funbox", EmitStuntFunboxFacets());
    CheckFacets("whoops", EmitStuntWhoopsFacets());
    CheckFacets("entry wedge", EmitStuntEntryWedgeFacets());
    CheckFacets("stunt ring", EmitStuntRingFacets());
});

test("stunt ring stands over the Big Air flight line with drive-under clearance", () => {
    const Ring = StuntPark.Ring;
    const Facets = EmitStuntRingFacets();
    assert.equal(Facets.length, 28 * 8 * 2);
    for (const Facet of Facets)
        for (const Corner of Facet.Corners)
        {
            const Radial = Math.hypot(Corner[1] - Ring.Centre[1], Corner[2] - Ring.Centre[2]) - Ring.Radius;
            const Distance = Math.hypot(Corner[0] - Ring.Centre[0], Radial);
            assert.ok(Math.abs(Distance - Ring.Tube) < 0.02);
        }
    assert.ok(Ring.Centre[2] - Ring.Radius - Ring.Tube > 2.0);
});

test("Jolt proxies: one 8-corner cloud per ramp chord, grounded, finite", () => {
    const Boxes = ResolveStuntJoltBoxes();
    assert.equal(Boxes.length, 9 + 8 + 32 + 3 + 5 + 1);
    for (const Corners of Boxes)
    {
        assert.equal(Corners.length, 8);
        const Depths = Corners.map((C) => C[2]);
        assert.ok(Corners.every((C) => C.every(Number.isFinite)));
        assert.ok(Math.min(...Depths) < 0.0);
        assert.ok(Math.max(...Depths) > 0.2);
    }
});

test("level wiring: stunt shapes, placements, cones, drill, zones and clear zone", () => {
    for (const Shape of ["StuntTableS", "StuntTableM", "StuntTableL", "StuntPipe", "StuntBowl", "StuntBankRing",
                         "StuntBank", "StuntFunbox", "StuntWhoops", "StuntWedge", "StuntRing", "StuntPylon"])
        assert.ok(LevelShapes[Shape], `${Shape} is a level shape`);
    const Placements = ResolveLevelPlacements();
    assert.ok(Placements.some((P) => P.Shape === "StuntTableL" && P.Position[0] === 0.0 && P.Position[1] === 0.0));
    const Pylons = Placements.filter((P) => P.Shape === "StuntPylon");
    assert.equal(Pylons.length, 2);
    assert.ok(Pylons.every((P) => P.Collider));
    assert.ok(!Placements.some((P) => P.Shape === "StuntTableL" && P.Collider));
    assert.ok(!Placements.some((P) => P.Shape === "Jersey" && P.Position[0] === 154.5));
    assert.ok(Placements.some((P) => P.Shape === "Jersey" && P.Position[0] === 250.5));
    const Cones = ResolveConePlacements().filter((C) => C.Group === "stunt");
    assert.equal(Cones.length, 8);
    for (const Cone of Cones)
        assert.equal(ResolveCourseHeight(Cone.Position[0], Cone.Position[1]), 0.0);
    const Drill = TrainingDrills.find((D) => D.Id === "bigair");
    assert.ok(Drill && Drill.Lip === 191.0 && Drill.LipY === -17.0 && Drill.LipHalf === 4.5);
    assert.ok(ArenaZones.some((Z) => Z.Name === "Stunt Park"));
    assert.ok(ArenaZones.some((Z) => Z.Name === "Big Air"));
    assert.ok(LevelClearZones.some((Z) => Z[0] === 158.0 && Z[2] === 252.0));
    assert.ok(EmitLevelGroundFacets().length > 0);
});

test("recovery budgets relax inside the stunt park and respawns land on the relief", () => {
    assert.ok(RecoverySettingsDefaults.StuntAirborneSeconds > RecoverySettingsDefaults.AirborneSeconds);
    assert.ok(RecoverySettingsDefaults.StuntTumbleSeconds > RecoverySettingsDefaults.TumbleSeconds);
    const Director = new RecoveryDirector({ Vehicle: { Position: [166.0, 8.0, 0.0] } });
    assert.ok(Director.WithinStunt([166.0, 8.0, 0.0]));
    assert.ok(!Director.WithinStunt([-40.0, 0.0, 0.0]));
});
