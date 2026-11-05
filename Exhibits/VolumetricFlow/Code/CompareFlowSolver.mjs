// Execute the retained editor's real uniform transcription and pressure-solve recorder, comparing native plans.
import fs from "node:fs";
import vm from "node:vm";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";

const [Executable] = process.argv.slice(2);
assert.ok(Executable, "Expected the native flow solver validation executable");
const Fixtures = [
    [8, 0, 0.01, 0, 0], [16, 1.49, 0.14, 2.75, 65537], [64, 3, 0.14, 14.8, 131072],
    [96, 1.5, 0.14, 0.6, 196613], [63, 2, 0.9, 8, 1], [256, 2.5, 0.14, 42, 10],
    [192, 1, 1.0, 100, 7], [48, -1, 0, 0, 2]
];
const Native = spawnSync(Executable, [], { input: Fixtures.map(Case => Case.join(" ")).join("\n") + "\n",
                                            encoding: "utf8", maxBuffer: 8 * 1024 * 1024 });
assert.equal(Native.status, 0, Native.stderr);
const NativeResults = Native.stdout.trim().split("\n").map(Line => JSON.parse(Line));
assert.equal(NativeResults.length, Fixtures.length);
const Source = fs.readFileSync("Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html", "utf8");
function Section(Start, End)
{
    const First = Source.indexOf(Start), Last = Source.indexOf(End, First + Start.length);
    assert.ok(First >= 0 && Last > First, `Reference section missing: ${Start}`);
    return Source.slice(First, Last);
}
const ReferenceCode = [
    Section("const CoarsestLevelExtent =", "function ComputeResidentByteCount("),
    Section("const SolverParameterization =", "const SolverDispatchExtent ="),
    Section("const CourantSpeedEstimate =", "function ComputeDispatchCount("),
    Section("function ComputeDispatchCount(", "function ConstructAdvectionBinding("),
    Section("const MultigridProfile =", "function RecordSolverStep("),
    Section("function RecordSolverStep(", "const OrbitCamera =")
].join("\n");
let DispatchCount = 0;
for (const [FixtureIndex, [Extent, Cycles, Timestep, Elapsed, Mask]] of Fixtures.entries())
{
    const Context = vm.createContext({});
    vm.runInContext(ReferenceCode, Context);
    const Code = `
        SolverParameterization.Δτ = ${Timestep};
        SolverParameterization.ProjectionIterations = ${Cycles};
        const UniformStaging = new Float32Array(48);
        const SolverDispatchExtent = { X: 8, Y: 8, Z: 4 };
        const SolverAssembly = {
            UniformBuffer: {}, ElapsedInterval: ${Elapsed},
            RelaxPressure: "RelaxPressure", EvaluateResidual: "EvaluateResidual", RestrictResidual: "RestrictResidual",
            ClearPressureLevel: "ClearPressureLevel", ProlongateCorrection: "ProlongateCorrection",
            InjectSource: "InjectScalarSource", AccumulateInfluence: "AccumulateInfluence", AdvectVelocity: "AdvectVelocityCarrier",
            RestoreVorticity: "RestoreVorticity", EvaluateDivergence: "EvaluateDivergence",
            SubtractGradient: "SubtractPressureGradient", AdvectScalar: "AdvectScalarCarrier"
        };
        let ReferenceUniform = [];
        const DeviceProfile = { Device: { queue: { writeBuffer(_buffer, _offset, Data) { ReferenceUniform = [...Data]; } } } };
        const Extents = DeriveLevelExtents(${Extent});
        const Slots = Extents.map((_, Index) => (${Mask} >> Index) & 1);
        let VelocitySlot = 0, ScalarSlot = 0;
        const VoxelStore = { VoxelExtent: ${Extent}, DivergenceIdle: "D", DivergenceStore: "R0", ResidualStore: "E0",
            get VelocityRead() { return "V" + VelocitySlot; }, get VelocityWrite() { return "V" + (1 - VelocitySlot); },
            get ScalarRead() { return "S" + ScalarSlot; }, get ScalarWrite() { return "S" + (1 - ScalarSlot); },
            get PressureRead() { return "P0_" + Slots[0]; }, get PressureWrite() { return "P0_" + (1 - Slots[0]); } };
        function ExchangeVelocityCarrier() { VelocitySlot ^= 1; }
        function ExchangeScalarCarrier() { ScalarSlot ^= 1; }
        VoxelStore.Pyramid = Extents.map((LevelExtent, LevelIndex) => ({
            LevelExtent, CoarseExtent: Extents[LevelIndex + 1] ?? LevelExtent, LevelUniform: LevelIndex,
            get PressureRead() { return "P" + LevelIndex + "_" + Slots[LevelIndex]; },
            get PressureWrite() { return "P" + LevelIndex + "_" + (1 - Slots[LevelIndex]); },
            ExchangePressure() { Slots[LevelIndex] ^= 1; },
            RightHandStore: "R" + LevelIndex, ResidualStore: "E" + LevelIndex
        }));
        function InscribeCourantReadouts() {}
        let LevelUniforms = [];
        function TranscribeLevelWeights(Weight) {
            LevelUniforms = VoxelStore.Pyramid.map(Level => [...new Float32Array([Level.LevelExtent, Level.CoarseExtent, Weight, 0])]);
        }
        function ConstructProjectionBinding(VIn, PIn, DIn, VOut, POut, DOut, Level, Residual) {
            return { Layout: "projection", Level: Level ?? 0, Volumes: [VIn, PIn, DIn, VOut, POut, DOut, Residual ?? "E0"] };
        }
        function ConstructAdvectionBinding(VIn, SIn, VOut, SOut) {
            return { Layout: "transport", Level: 0, Volumes: [VIn, SIn, VOut, SOut] };
        }
        let RecordingStep = false;
        const Dispatches = [];
        const Recorder = { beginComputePass() {
            let Pipeline, Binding;
            return { setPipeline(Value) { Pipeline = Value; }, setBindGroup(_Slot, Value) { Binding = Value; },
                     dispatchWorkgroups(X, Y, Z) { Dispatches.push(RecordingStep
                         ? [Pipeline, Binding.Layout, Binding.Level, [X,Y,Z], Binding.Volumes]
                         : [Pipeline, Binding.Level, [X,Y,Z], Binding.Volumes]); }, end() {} };
        } };
        TranscribeParameterization();
        const Courant = DeriveAdvectionSubstepCount();
        RecordPressureSolve(Recorder, ComputeDispatchCount(${Extent}));
        const PressureDispatches = Dispatches.slice(), PressureSlots = Slots.slice();
        for (let Index = 0; Index < Slots.length; ++Index) Slots[Index] = (${Mask} >> Index) & 1;
        VelocitySlot = (${Mask} >> 16) & 1; ScalarSlot = (${Mask} >> 17) & 1;
        Dispatches.length = 0; RecordingStep = true;
        RecordSolverStep(Recorder);
        JSON.stringify({ uniform: ReferenceUniform, substeps: Courant.SubstepCount, demanded: Courant.DemandedCount,
                         courant: Courant.ResidualCourant, levels: LevelUniforms, dispatches: PressureDispatches, finalSlots: PressureSlots,
                         stepDispatches: Dispatches, stepSlots: { velocity: VelocitySlot, scalar: ScalarSlot, pressure: Slots } });`;
    const Reference = JSON.parse(vm.runInContext(Code, Context));
    const Result = NativeResults[FixtureIndex];
    assert.deepEqual(Result.uniform, Reference.uniform, `uniform fixture ${FixtureIndex}`);
    assert.equal(Result.substeps, Reference.substeps, `substeps fixture ${FixtureIndex}`);
    assert.equal(Result.demanded, Reference.demanded, `demand fixture ${FixtureIndex}`);
    assert.ok(Math.abs(Result.courant - Reference.courant) < 1e-12, `Courant fixture ${FixtureIndex}`);
    assert.deepEqual(Result.levels, Reference.levels, `levels fixture ${FixtureIndex}`);
    assert.deepEqual(Result.dispatches, Reference.dispatches, `dispatches fixture ${FixtureIndex}`);
    assert.deepEqual(Result.finalSlots, Reference.finalSlots, `pressure exchanges fixture ${FixtureIndex}`);
    assert.deepEqual(Result.stepDispatches, Reference.stepDispatches, `full solver dispatches fixture ${FixtureIndex}`);
    assert.deepEqual(Result.stepSlots, Reference.stepSlots, `carrier exchanges fixture ${FixtureIndex}`);
    DispatchCount += Result.dispatches.length + Result.stepDispatches.length;
    console.log(`${Extent}³, ${Cycles} cycles, ${Result.substeps}/${Result.demanded} substeps: ` +
                `${Result.levels.length} levels, ${Result.dispatches.length} pressure and ${Result.stepDispatches.length} step dispatches match`);
}
console.log(`Flow solver: ${Fixtures.length} uniform, pressure and full-step snapshots and ${DispatchCount} dispatches match the retained JavaScript.`);
