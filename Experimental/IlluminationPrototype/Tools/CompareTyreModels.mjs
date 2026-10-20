// Reproducible force curves and rotating-order CPU comparisons of all four retained tyre models.
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import { fileURLToPath } from "node:url";
import { TyreContinuumElasticity } from "../Source/TyreContinuumElasticity.js";
import { TyreCarcassProjection } from "../Source/TyreCarcassProjection.js";
import { VehicleSolver } from "../Source/VehicleSolver.js";
import { SummarizeDurations } from "../Source/TimingComparison.js";

const Output = path.resolve(process.argv[2] || fileURLToPath(new URL("../../../Exhibits/IlluminationConsolidation/Results/TyreModels", import.meta.url)));
fs.mkdirSync(Output, { recursive: true });
const Models = ["spring", "xpbd", "shell", "solid"], Pressure = 220000;
const Cold = {}, Samples = Object.fromEntries(Models.map(Model => [Model, []]));
const StartXpbd = performance.now(), Xpbd = new TyreCarcassProjection(Pressure);
Cold.xpbd = performance.now() - StartXpbd;
const Solvers = { xpbd: Xpbd };
for (const Model of ["shell", "solid"])
{
    const Start = performance.now();
    Solvers[Model] = new TyreContinuumElasticity(Model, Pressure);
    Cold[Model] = performance.now() - Start;
}
const ForceCurves = Array.from({ length: 81 }, (_, Index) =>
{
    const CompressionM = Index / 1000;
    return { CompressionM, spring: (25000 + 0.9 * Pressure) * CompressionM,
        ...Object.fromEntries(Object.entries(Solvers).map(([Model, Solver]) => [Model, Solver.EvaluateReaction(CompressionM)])) };
});
const Settled = {};
for (const Model of Models)
{
    const Vehicle = new VehicleSolver([]); Vehicle.TyreModel = Model;
    Vehicle.Respawn([-100, -34, 0.53], 0);
    for (let Tick = 0; Tick < 360; ++Tick) Vehicle.Advance({ Throttle: 0, Brake: 0, Steer: 0, Handbrake: false }, 1 / 120);
    Settled[Model] = { BodyHeightM: Vehicle.Position[2], Wheels: Vehicle.Wheels.map(Wheel => ({
        DeflectionM: Wheel.TyreDeflection, ForceN: Wheel.TyreForce, VolumeM3: Wheel.CarcassActive ? Wheel.Carcass.Volume : 0.034 })) };
}
const Steps = 2400, Repetitions = 7;
for (let Repetition = -1; Repetition < Repetitions; ++Repetition)
{
    for (let Position = 0; Position < Models.length; ++Position)
    {
        const Model = Models[(Position + Repetition + Models.length) % Models.length];
        const Vehicle = new VehicleSolver([]); Vehicle.TyreModel = Model;
        Vehicle.Respawn([-100, -34, 0.53], 0);
        const Start = performance.now();
        for (let Tick = 0; Tick < Steps; ++Tick)
            Vehicle.Advance({ Throttle: 0.25, Brake: 0, Steer: 0.12 * Math.sin(Tick / 120), Handbrake: false }, 1 / 120);
        if (Repetition >= 0) Samples[Model].push(performance.now() - Start);
    }
}
const Report = { Platform: process.platform, Architecture: process.arch, Cpu: os.cpus()[0]?.model, Node: process.version,
    PressurePa: Pressure, ColdConstructionMs: Cold, Steps, Repetitions, OuterStepS: 1 / 120, InternalStepS: 1 / 480,
    Results: Object.fromEntries(Models.map(Model => [Model, { Milliseconds: SummarizeDurations(Samples[Model]), RawMs: Samples[Model] }])),
    Settled, ForceCurves,
    Note: "CPU physics only, rotating model order, one discarded warmup each. Includes FEM display displacement evaluation, excludes actual GPU rendering. Shared pressure curves prewarmed; cold construction is separately reported. Not calibrated tyre material or a target-device FPS comparison." };
fs.writeFileSync(path.join(Output, "CpuTyreModels.json"), JSON.stringify(Report, null, 2) + "\n");
const Colours = { spring: "#64748b", xpbd: "#d97706", shell: "#0284c7", solid: "#9333ea" };
const Maximum = 40000;
let Svg = `<svg xmlns="http://www.w3.org/2000/svg" width="900" height="540" viewBox="0 0 900 540"><rect width="900" height="540" fill="white"/><g font-family="sans-serif" fill="#172033"><text x="65" y="32" font-size="21">Tyre force vs compression — executed solver outputs at 220 kPa</text><text x="65" y="55" font-size="13">Coarse quasi-static FEM; uncalibrated materials. Spring line excludes rim-contact stiffening.</text>`;
for (let Tick = 0; Tick <= 4; ++Tick)
{
    const Height = 440 - Tick * 85;
    Svg += `<path d="M65 ${Height} H825" stroke="#e2e8f0"/><text x="12" y="${Height + 5}" font-size="13">${Tick * 10} kN</text>`;
}
for (let Tick = 0; Tick <= 8; ++Tick)
{
    const Abscissa = 65 + Tick * 95;
    Svg += `<text x="${Abscissa - 6}" y="462" font-size="13">${Tick * 10}</text>`;
}
Svg += `<path d="M65 100 V440 H825" fill="none" stroke="#172033"/><text x="340" y="486" font-size="14">Radial compression (mm)</text>`;
for (const [Index, Model] of Models.entries())
{
    const Points = ForceCurves.map(Entry => `${65 + Entry.CompressionM / 0.08 * 760},${440 - Entry[Model] / Maximum * 340}`).join(" ");
    Svg += `<polyline points="${Points}" fill="none" stroke="${Colours[Model]}" stroke-width="3"/><text x="${65 + Index * 185}" y="519" fill="${Colours[Model]}" font-size="15">${Model}: ${Report.Results[Model].Milliseconds.Median.toFixed(1)} ms CPU</text>`;
}
Svg += `</g></svg>`;
fs.writeFileSync(path.join(Output, "TyreForceCurves.svg"), Svg);
console.log(JSON.stringify({ ColdConstructionMs: Cold, Results: Report.Results, Settled }, null, 2));
