// Compare the prototype scan timing against the useFrame body extracted from the supplied Truck.tsx.
import assert from "node:assert/strict";
import fs from "node:fs";
import { spawnSync } from "node:child_process";
import { VehicleScanController, VehicleViewMode } from "../../../Experimental/VehicleHmiPrototype/Source/Effects/VehicleScanEffect.js";
import { BuildScanSparkleBuffer, SparkleRecordFloats } from "../../../Experimental/VehicleHmiPrototype/Source/Effects/ScanSparkles.js";

const ReferencePath = process.argv[2];
if (!ReferencePath) throw new Error("Usage: node CompareScanReference.mjs <reference Truck.tsx>");
const Source = fs.readFileSync(ReferencePath, "utf8");
const Header = "  useFrame((state, dtRaw) => {";
const Start = Source.indexOf(Header), End = Source.indexOf("\n  });", Start);
assert.ok(Start >= 0 && End > Start, "reference useFrame body not found");
const Body = Source.slice(Start + Header.length, End);
const EaseMatch = Source.match(/const easeInOutCubic = \(x: number\) => ([^;]+);/);
assert.ok(EaseMatch, "reference ease expression not found");
const ReferenceStep = new Function("scan", "dtRaw", "state", "Refs", `
    const { holoMat, stepRun, paintRun, rimRun, ramp, sweepT, scanU, flagsOn,
            matsRef, clipPlane, edgeMat, curtain, curtainMats, group, THREE } = Refs;
    const easeInOutCubic = x => ${EaseMatch[1]};
    ${Body}
`);
const Refs = {
    holoMat: { uniforms: { uTime: { value: 0 } } }, stepRun: () => {}, paintRun: {}, rimRun: {},
    ramp: { current: 0 }, sweepT: { current: 0 },
    scanU: { uScanX: { value: 3.3 }, uHoloAmt: { value: 0 } },
    flagsOn: { current: false }, matsRef: { current: [{ transparent: false, depthWrite: true }] },
    clipPlane: { constant: 0 }, edgeMat: { opacity: 0 },
    curtain: { current: { position: { x: 3.3 } } }, curtainMats: { current: [{ opacity: 0 }, { opacity: 0 }] },
    group: { current: { position: { y: 0 } } },
    THREE: { MathUtils: { lerp: (A, B, T) => (1 - T) * A + T * B,
        damp: (A, B, Lambda, Dt) => (1 - Math.exp(-Lambda * Dt)) * B + Math.exp(-Lambda * Dt) * A } }
};
const Scan = new VehicleScanController();
let Clock = 0, Prior = false, Checks = 0;
const NativeInputs = [], NativeExpected = [];
const Near = (A, B, Label) => {
    assert.ok(Math.abs(A - B) < 1e-9, `${Label}: ${A} != ${B}`);
    ++Checks;
};
function Advance(Seconds, Enabled)
{
    Clock += Seconds;
    if (Enabled && !Prior) Refs.sweepT.current = 0; // reference useEffect([scan])
    Prior = Enabled;
    ReferenceStep(Enabled, Seconds, { clock: { elapsedTime: Clock } }, Refs);
    Scan.Advance(Seconds, Enabled ? VehicleViewMode.Scan : VehicleViewMode.Solid);
    Near(Scan.Clock, Clock, "clock");
    Near(Scan.SweepTime, Refs.sweepT.current, "sweep time");
    Near(Scan.Ramp, Refs.ramp.current, "ramp");
    Near(Scan.ScanX, Refs.scanU.uScanX.value, "scan X");
    Near(Scan.HoloAmount, Refs.scanU.uHoloAmt.value, "hologram amount");
    Near(Scan.EdgeOpacity, Refs.edgeMat.opacity, "edge opacity");
    Near(Scan.CurtainAlpha0, Refs.curtainMats.current[0].opacity, "wide curtain");
    Near(Scan.CurtainAlpha1, Refs.curtainMats.current[1].opacity, "narrow curtain");
    Near(Scan.SparkleAlpha, Enabled ? 0.55 : 0, "mounted particle opacity");
    NativeInputs.push(`${Seconds} ${Enabled ? 1 : 0}`);
    NativeExpected.push([Clock, Refs.sweepT.current, Refs.ramp.current, Refs.scanU.uScanX.value,
        Refs.scanU.uHoloAmt.value, Refs.edgeMat.opacity, Refs.curtainMats.current[0].opacity,
        Refs.curtainMats.current[1].opacity, Enabled ? 0.55 : 0, Refs.flagsOn.current ? 1 : 0]);
}
Advance(0.2, false);
Advance(0.001, true);
for (let Index = 0; Index < 124; ++Index) Advance(0.05, true); // sweep, hold, fade
Advance(0.5, false); // frozen front; cycle amount jumps back to one
for (let Index = 0; Index < 60; ++Index) Advance(0.05, false);
Advance(0.2, true); // reset sweep; capped animation time but uncapped clock
for (let Index = 0; Index < 142; ++Index) Advance(0.05, true); // wrap into next cycle
Advance(0.05, false);
Advance(0.05, true);

// Drei's random coordinate order is X, Y-up, Z-side; compare known random draws after the Z-up conversion.
const Draws = [0.1, 0.2, 0.3, 0.7, 0.8, 0.9];
let Cursor = 0;
const Actual = BuildScanSparkleBuffer(2, () => Draws[Cursor++]).Scalars;
for (let Index = 0; Index < 2; ++Index)
{
    const Offset = Index * SparkleRecordFloats, R = Draws.slice(Index * 3, Index * 3 + 3);
    const Expected = [(0.5 - R[0]) * 7.5, -(0.5 - R[2]) * 4.2, 0.83 + (0.5 - R[1]) * 3.2,
                      2.2, 1, 1, 1, 0.35];
    for (let Component = 0; Component < Expected.length; ++Component)
    {
        assert.ok(Math.abs(Actual[Offset + Component] - Expected[Component]) < 1e-6);
        ++Checks;
    }
}
assert.equal(Cursor, 6);
if (process.argv[3])
{
    const Native = spawnSync(process.argv[3], [], { input: NativeInputs.join("\n") + "\n", encoding: "utf8" });
    assert.equal(Native.status, 0, Native.stderr);
    const Lines = Native.stdout.trim().split("\n");
    assert.equal(Lines.length, NativeExpected.length);
    for (let Index = 0; Index < Lines.length; ++Index)
    {
        const Readings = Lines[Index].trim().split(/\s+/).map(Number);
        assert.equal(Readings.length, NativeExpected[Index].length);
        for (let Component = 0; Component < Readings.length; ++Component)
        {
            assert.ok(Math.abs(Readings[Component] - NativeExpected[Index][Component]) < 2e-6,
                      `native step ${Index}/${Component}: ${Readings[Component]} != ${NativeExpected[Index][Component]}`);
            ++Checks;
        }
    }
}
console.log(`Reference Truck.tsx scan timing and particle attributes: ${Checks} comparisons passed.`);
