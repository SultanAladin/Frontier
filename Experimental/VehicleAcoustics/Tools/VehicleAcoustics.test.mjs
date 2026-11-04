//============================================================================================================================================
//                                                    VEHICLEACOUSTICS.TEST.MJS
//============================================================================================================================================
// 🧩 Node form of the validation suite. Run with `node --test Tools/`. The browser runs the same assertions through Source/ValidationSuite.js so
//    the two cannot disagree about what "passing" means.

import test from "node:test";
import assert from "node:assert/strict";

import { ComposeValidationReport } from "../Source/ValidationSuite.js";
import { RenderSchedule, RenderSteadyStateTone, MeasureSignal, MeasureDominantOrder, MeasureSpectralSummary,
         ComposeStandardSchedule, EncodeWaveFile } from "../Source/OfflineRender.js";
import { VehicleCatalogue, ResolveVehicleProfile, DeriveFiringSequence } from "../Source/VehicleCatalogue.js";
import { AdvanceRandomSequence } from "../Source/ExhaustVoice.js";
import { WriteRingBlock, QueryRingFill, RingLayout } from "../Source/AudioKernel.js";

test("the whole embedded validation suite passes", () =>
{
    const Report = ComposeValidationReport();
    const Failures = Report.Findings.filter((Entry) => !Entry.Passed)
                                    .map((Entry) => `${Entry.Label}: ${Entry.Detail}`);
    assert.deepEqual(Failures, [], `failing checks: ${Failures.join(" | ")}`);
    assert.ok(Report.Total >= 30, `expected at least 30 checks, saw ${Report.Total}`);
});

test("catalogue carries all nine requested vehicles", () =>
{
    assert.equal(VehicleCatalogue.length, 9);
    const Identifiers = VehicleCatalogue.map((Entry) => Entry.Identifier);
    for (const Required of ["laFerrari", "gt-rNismoGt3", "mustangGt", "ageraRs", "spyder918", "taycanTurbo",
                            "demonSrt", "viperAcr", "challengerScat"])
        assert.ok(Identifiers.includes(Required), `${Required} missing`);
    const Indexes = VehicleCatalogue.map((Entry) => Entry.SortieIndex);
    assert.deepEqual(Indexes, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
});

test("flat-plane and cross-plane V8s differ only where the crank layout says they should", () =>
{
    const Flat = DeriveFiringSequence(ResolveVehicleProfile("ageraRs").Combustion);
    const Cross = DeriveFiringSequence(ResolveVehicleProfile("mustangGt").Combustion);
    assert.equal(Flat.EventCount, Cross.EventCount);
    assert.ok(Flat.BankGaps.every((Gap) => Math.abs(Gap - 180.0) < 0.5), "flat-plane banks must alternate at 180°");
    const Spread = Math.max(...Cross.BankGaps) - Math.min(...Cross.BankGaps);
    assert.ok(Spread > 150.0, `cross-plane banks must be uneven, spread was ${Spread}`);
});

test("an electric vehicle has no exhaust content", () =>
{
    const Result = RenderSteadyStateTone("taycanTurbo", { RPM: 9000, Throttle: 1.0, Load: 1.0 }, 0.5);
    const Ladder = MeasureDominantOrder(Result.Left, Result.SampleRate, 600.0, 4).Ladder;
    const Total = Ladder.reduce((Sum, Entry) => Sum + Entry.Magnitude, 0.0);
    assert.ok(Total < 0.01, `electric car produced firing content: ${Total}`);
});

test("the six-cylinder turbo is quieter off boost than on boost", () =>
{
    const OffBoost = RenderSteadyStateTone("gt-rNismoGt3", { RPM: 3000, Throttle: 0.05, Load: 0.1 }, 0.5);
    const OnBoost = RenderSteadyStateTone("gt-rNismoGt3", { RPM: 5000, Throttle: 1.0, Load: 1.0, Boost: 1.8,
                                                            TurbineSpeed: 0.9 }, 0.5);
    const Off = MeasureSignal(OffBoost.Left, OffBoost.SampleRate).Rms;
    const On = MeasureSignal(OnBoost.Left, OnBoost.SampleRate).Rms;
    assert.ok(On > Off * 1.4, `boost should raise level: ${Off.toFixed(4)} -> ${On.toFixed(4)}`);
    const OffBrightness = MeasureSpectralSummary(OffBoost.Left, OffBoost.SampleRate).CentroidHz;
    const OnBrightness = MeasureSpectralSummary(OnBoost.Left, OnBoost.SampleRate).CentroidHz;
    assert.ok(OnBrightness > OffBrightness * 1.2, `boost should brighten: ${OffBrightness.toFixed(0)} -> ${OnBrightness.toFixed(0)} Hz`);
});

test("random sequence is deterministic and bounded", () =>
{
    let Sequence = 12345;
    const Samples = [];
    for (let Index = 0; Index < 1000; ++Index)
    {
        const Result = AdvanceRandomSequence(Sequence);
        Sequence = Result.Sequence;
        Samples.push(Result.Sample);
    }
    assert.ok(Samples.every((Entry) => Entry >= -1.0 && Entry <= 1.0));
    let Second = 12345;
    const Repeat = [];
    for (let Index = 0; Index < 1000; ++Index)
    {
        const Result = AdvanceRandomSequence(Second);
        Second = Result.Sequence;
        Repeat.push(Result.Sample);
    }
    assert.deepEqual(Samples, Repeat);
});

test("ring writer never overwrites unread samples", () =>
{
    const Capacity = 256;
    const Control = new Int32Array(RingLayout.ControlWords);
    const Plane = new Float32Array(Capacity * 2);
    const Left = new Float32Array(200).fill(0.5);
    const Right = new Float32Array(200).fill(-0.5);
    const Written = WriteRingBlock(Control, Plane, Capacity, Left, Right, 200);
    assert.equal(Written, 200);
    assert.equal(QueryRingFill(Control, Capacity), 200);
    const Second = WriteRingBlock(Control, Plane, Capacity, Left, Right, 200);
    assert.ok(Second <= Capacity - 1 - 200, `wrote ${Second} past capacity`);
    assert.equal(Atomics.load(Control, RingLayout.OverrunWord) > 0, true);
});

test("wave encoding is a valid 16-bit stereo RIFF of the requested length", () =>
{
    const Frames = 480;
    const Left = new Float32Array(Frames).fill(0.25);
    const Right = new Float32Array(Frames).fill(-0.25);
    const Bytes = EncodeWaveFile(Left, Right, 48000);
    const View = new DataView(Bytes.buffer);
    const Text = (Offset, Length) => String.fromCharCode(...Bytes.subarray(Offset, Offset + Length));
    assert.equal(Text(0, 4), "RIFF");
    assert.equal(Text(8, 4), "WAVE");
    assert.equal(Text(12, 4), "fmt ");
    assert.equal(View.getUint16(20, true), 1);
    assert.equal(View.getUint16(22, true), 2);
    assert.equal(View.getUint32(24, true), 48000);
    assert.equal(View.getUint16(34, true), 16);
    assert.equal(View.getUint32(40, true), Frames * 4);
    assert.equal(Bytes.length, 44 + Frames * 4);
    assert.equal(View.getInt16(44, true), Math.round(0.25 * 32767));
});

test("a full standard-schedule audition renders finite audio for every car", () =>
{
    for (const Vehicle of VehicleCatalogue)
    {
        const Result = RenderSchedule(Vehicle.Identifier, ComposeStandardSchedule(), { Duration: 1.5, SampleRate: 24000, BlockSize: 256 });
        const Measurement = MeasureSignal(Result.Left, Result.SampleRate);
        assert.equal(Measurement.NonFinite, 0, `${Vehicle.Identifier} produced non-finite samples`);
        assert.ok(Measurement.Peak > 0.01, `${Vehicle.Identifier} was silent`);
        assert.ok(Measurement.Peak < 1.0, `${Vehicle.Identifier} clipped at ${Measurement.Peak}`);
        assert.ok(Result.Telemetry.length > 10);
    }
});
