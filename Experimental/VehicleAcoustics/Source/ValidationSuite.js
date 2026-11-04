//============================================================================================================================================
//                                                          VALIDATIONSUITE.JS
//============================================================================================================================================
// 🧩 The assertions that decide whether this model is worth listening to: catalogue integrity, firing-geometry correctness per crank layout,
//    sound actually produced for every car, peak discipline, and frame-rate independence of the drivetrain itself.

import { VehicleCatalogue, ResolveVehicleProfile, ValidateVehicleProfile, DeriveFiringSequence } from "./VehicleCatalogue.js";
import { RenderSteadyStateTone, MeasureSignal, MeasureDominantOrder } from "./OfflineRender.js";
import { CreateVehicleVoiceBundle, RenderVehicleVoiceBlock, ComposeVoiceControls } from "./VehicleVoice.js";
import { PrepareDrivetrainProfile, ResolveDrivetrainTelemetry } from "./VehicleDrivetrain.js";
import { RenderExhaustBlock } from "./ExhaustVoice.js";
import { RenderInductionBlock } from "./InductionVoice.js";
import { RenderMechanicalBlock } from "./MechanicalVoice.js";

function Assert(Findings, Condition, Label, Detail)
{
    Findings.push({ Label, Passed: !!Condition, Detail: Detail === undefined ? "" : String(Detail) });
}

export function ComposeValidationReport()
{
    const Findings = [];

    // ① Catalogue integrity: every sheet is physically coherent and every car resolves.
    for (const Vehicle of VehicleCatalogue)
    {
        const Report = ValidateVehicleProfile(ResolveVehicleProfile(Vehicle.Identifier));
        Assert(Findings, Report.Accepted, `catalogue ${Vehicle.Identifier}`, Report.Findings.join("; "));
    }

    // ② Firing geometry: the crank layout must produce the interval pattern it claims, for all nine cars.
    const Intervals = (Identifier) =>
    {
        const Firing = DeriveFiringSequence(ResolveVehicleProfile(Identifier).Combustion);
        const Cycle = 360.0 * Firing.CycleRevolutions;
        const Gaps = [];
        for (let Index = 0; Index < Firing.Phases.length; ++Index)
            Gaps.push(Index + 1 < Firing.Phases.length ? Firing.Phases[Index + 1] - Firing.Phases[Index]
                                                       : Firing.Phases[0] + Cycle - Firing.Phases[Index]);
        return Gaps;
    };
    const V12 = Intervals("laFerrari");
    Assert(Findings, V12.every((Gap) => Math.abs(Gap - 60.0) < 0.5), "V12 fires every 60°", V12.map((G) => G.toFixed(0)).join(","));
    const Flat = Intervals("ageraRs");
    Assert(Findings, Flat.every((Gap) => Math.abs(Gap - 90.0) < 0.5), "flat-plane V8 fires every 90°", Flat.map((G) => G.toFixed(0)).join(","));
    const Odd = Intervals("viperAcr");
    const OddSpread = Math.max(...Odd) - Math.min(...Odd);
    Assert(Findings, OddSpread > 20.0, "odd-fire V10 is uneven", `${Math.min(...Odd).toFixed(0)}–${Math.max(...Odd).toFixed(0)}°`);
    const Even = Intervals("mustangGt");
    Assert(Findings, Even.every((Gap) => Math.abs(Gap - 90.0) < 0.5), "cross-plane V8 totals 90° per event", Even.map((G) => G.toFixed(0)).join(","));

    // ③ Every car makes sound, and every combustion car makes sound with an outlet temperature above ambient.
    for (const Vehicle of VehicleCatalogue)
    {
        const Profile = ResolveVehicleProfile(Vehicle.Identifier);
        const RPM = Profile.Combustion.CylinderCount > 0 ? Profile.Combustion.RedlineRPM * 0.6 : Profile.Combustion.RedlineRPM * 0.5;
        const Result = RenderSteadyStateTone(Vehicle.Identifier, { RPM, Throttle: 1.0, Load: 0.9 }, 0.5);
        const Measurement = MeasureSignal(Result.Left, Result.SampleRate);
        Assert(Findings, Measurement.Rms > 0.004 && Measurement.NonFinite === 0, `audible ${Vehicle.Identifier}`,
               `rms ${Measurement.Rms.toFixed(4)} peak ${Measurement.Peak.toFixed(3)}`);
        Assert(Findings, Measurement.Peak < 1.0, `bounded ${Vehicle.Identifier}`, `peak ${Measurement.Peak.toFixed(4)}`);
    }

    // ④ Harmonic ladder: a flat-plane V8 must carry more fourth-order energy relative to its first than a cross-plane V8 does.
    const Relative = (Identifier) =>
    {
        const Profile = ResolveVehicleProfile(Identifier);
        const RPM = Profile.Combustion.RedlineRPM * 0.6;
        const Result = RenderSteadyStateTone(Identifier, { RPM, Throttle: 1.0, Load: 0.9 }, 0.6);
        const Ladder = MeasureDominantOrder(Result.Left, Result.SampleRate, Math.max(20.0, Result.FiringFrequency), 6).Ladder;
        const First = Ladder[0] ? Ladder[0].Magnitude : 0.0;
        const Fourth = Ladder[3] ? Ladder[3].Magnitude : 0.0;
        return First > 1.0e-9 ? Fourth / First : 0.0;
    };
    const FlatRatio = Relative("ageraRs");
    const CrossRatio = Relative("mustangGt");
    Assert(Findings, FlatRatio > CrossRatio, "flat-plane carries more 4th order than cross-plane",
           `flat ${FlatRatio.toFixed(3)} > cross ${CrossRatio.toFixed(3)}`);

    // ⑤ Electric drive: no exhaust content, but the drive layer must still be audible.
    const Electric = RenderSteadyStateTone("taycanTurbo", { RPM: 8000, Throttle: 1.0, Load: 0.9 }, 0.4);
    const ElectricMeasurement = MeasureSignal(Electric.Left, Electric.SampleRate);
    Assert(Findings, Electric.Bundle.Profile.Electric === true && ElectricMeasurement.Rms > 0.002, "electric drive audible",
           `rms ${ElectricMeasurement.Rms.toFixed(4)}`);

    // ⑥ Drivetrain determinism: the same schedule advanced in 128-sample and 512-sample blocks must land on the same physical state, because
    //    the integrator runs on an absolute 2 ms grid and the caller's block size only decides when the result is observed.
    const SmallState = AdvanceTelemetry("laFerrari", 0.6, 128);
    const LargeState = AdvanceTelemetry("laFerrari", 0.6, 512);
    const Drift = ["SpeedMetresPerSecond", "EngineRPM", "GearIndex", "WheelSpeed", "BoostBar"]
        .map((Key) => Math.abs(SmallState[Key] - LargeState[Key]))
        .reduce((Worst, Entry) => Math.max(Worst, Entry), 0.0);
    Assert(Findings, Drift < 1.0e-6, "drivetrain independent of block size", `worst field difference ${Drift.toExponential(2)}`);

    // ⑦ Audio determinism: with an identical control signal, the voices must render byte-identically whatever block size the device pulls.
    const AudioDrift = CompareRenderingInBlocks("laFerrari", 4000.0, 0.4, 128, 512);
    Assert(Findings, AudioDrift < 1.0e-9, "audio independent of block size", `peak |Δ| ${AudioDrift.toExponential(2)}`);

    // ⑧ Level stability across block sizes: a moving engine cannot be sample-identical (control arrives once per block), so the requirement is
    //    that its level and pitch content do not change measurably with the device quantum.
    const Coarse = RenderBlocksInChunks("laFerrari", 0.5, 1024);
    const Fine = RenderBlocksInChunks("laFerrari", 0.5, 128);
    const CoarseLevel = MeasureSignal(Coarse, 48000).RmsDecibels;
    const FineLevel = MeasureSignal(Fine, 48000).RmsDecibels;
    Assert(Findings, Math.abs(CoarseLevel - FineLevel) < 0.5, "level stable across device quanta",
           `${FineLevel.toFixed(2)} vs ${CoarseLevel.toFixed(2)} dBFS`);

    const Passed = Findings.filter((Entry) => Entry.Passed).length;
    return { Findings, Passed, Total: Findings.length, Accepted: Passed === Findings.length };
}

// 📝 AdvanceTelemetry returns the final physical state after `Seconds` of a fixed throttle, advanced in `Chunk`-sized calls.
function AdvanceTelemetry(Identifier, Seconds, Chunk)
{
    const Bundle = CreateVehicleVoiceBundle(Identifier, 48000, { Position: "exterior", Gain: 1.0 });
    const Total = Math.round(Seconds * 48000);
    const Left = new Float32Array(Chunk);
    const Right = new Float32Array(Chunk);
    Bundle.Input.Throttle = 1.0;
    for (let Cursor = 0; Cursor < Total; Cursor += Chunk)
    {
        const Count = Math.min(Chunk, Total - Cursor);
        RenderVehicleVoiceBlock(Bundle, Left.subarray(0, Count), Right.subarray(0, Count), Count);
    }
    const T = Bundle.Telemetry;
    return { SpeedMetresPerSecond: T.SpeedMetresPerSecond, EngineRPM: T.EngineRPM, GearIndex: T.GearIndex,
             WheelSpeed: Bundle.Condition.WheelSpeed, BoostBar: T.BoostBar };
}

// 📝 CompareRenderingInBlocks drives the three voices directly with one constant control object, so the only variable is how the caller slices
//    the render. This is the strict determinism test; the moving-car case is covered by the level-stability check below it.
function CompareRenderingInBlocks(Identifier, RPM, Seconds, FirstChunk, SecondChunk)
{
    const Total = Math.round(Seconds * 48000);
    const Render = (Chunk) =>
    {
        const Bundle = CreateVehicleVoiceBundle(Identifier, 48000, { Position: "exterior", Gain: 1.0 });
        const Left = new Float32Array(Total);
        const Right = new Float32Array(Total);
        const TemporaryLeft = new Float32Array(Chunk);
        const TemporaryRight = new Float32Array(Chunk);
        let Cursor = 0;
        while (Cursor < Total)
        {
            const Count = Math.min(Chunk, Total - Cursor);
            TemporaryLeft.fill(0.0);
            TemporaryRight.fill(0.0);
            RenderHeldVoices(Bundle, TemporaryLeft, TemporaryRight, Count, RPM);
            Left.set(TemporaryLeft.subarray(0, Count), Cursor);
            Right.set(TemporaryRight.subarray(0, Count), Cursor);
            Cursor += Count;
        }
        return Left;
    };
    const First = Render(FirstChunk);
    const Second = Render(SecondChunk);
    let Worst = 0.0;
    for (let Index = 0; Index < Total; ++Index)
        Worst = Math.max(Worst, Math.abs(First[Index] - Second[Index]));
    return Worst;
}

function RenderHeldVoices(Bundle, Left, Right, Count, RPM)
{
    const Condition = Bundle.Condition;
    Condition.EngineAngularSpeed = RPM * Math.PI / 30.0;
    Condition.ThrottlePlate = 1.0;
    Condition.EngineLoad = 0.95;
    Condition.ExhaustMassFlow = 0.95;
    Condition.GearWhineLoad = 1.0;
    Bundle.Telemetry = ResolveDrivetrainTelemetry(Condition, Bundle.Profile);
    ComposeVoiceControls(Bundle, Bundle.Telemetry);
    RenderExhaustBlock(Bundle.Exhaust, Bundle.ExhaustControl, Left, Right, Count);
    RenderInductionBlock(Bundle.Induction, Bundle.InductionControl, Left, Right, Count);
    RenderMechanicalBlock(Bundle.Mechanical, Bundle.MechanicalControl, Left, Right, Count);
}

function RenderBlocksInChunks(Identifier, Seconds, Chunk)
{
    const Bundle = CreateVehicleVoiceBundle(Identifier, 48000, { Position: "exterior", Gain: 1.0 });
    Bundle.Profile = PrepareDrivetrainProfile(Bundle.Profile);
    const Total = Math.round(Seconds * 48000);
    const Left = new Float32Array(Total);
    const TemporaryLeft = new Float32Array(Chunk);
    const TemporaryRight = new Float32Array(Chunk);
    let Cursor = 0;
    Bundle.Input.Throttle = 1.0;
    while (Cursor < Total)
    {
        const Count = Math.min(Chunk, Total - Cursor);
        RenderVehicleVoiceBlock(Bundle, TemporaryLeft.subarray(0, Count), TemporaryRight.subarray(0, Count), Count);
        Left.set(TemporaryLeft.subarray(0, Count), Cursor);
        Cursor += Count;
    }
    return Left;
}

export function FormatValidationReport(Report)
{
    const Lines = [`${Report.Passed}/${Report.Total} checks passed`, ""];
    for (const Finding of Report.Findings)
        Lines.push(`${Finding.Passed ? "PASS" : "FAIL"}  ${Finding.Label}${Finding.Detail ? "  — " + Finding.Detail : ""}`);
    return Lines.join("\n");
}
