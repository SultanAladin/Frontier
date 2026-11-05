// Execute the retained irradiance recorder and compare its staging, swaps, and dispatches with C++.
import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";
import { spawnSync } from "node:child_process";

const Executable = process.argv[2];
assert.ok(Executable, "Expected the FlowIrradianceValidation executable");
const Html = fs.readFileSync("Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html", "utf8");
function Slice(Start, End)
{
    const From = Html.indexOf(Start), To = Html.indexOf(End, From);
    assert.ok(From >= 0 && To > From, `Reference section not found: ${Start}`);
    return Html.slice(From, To);
}
const View = Slice("const ViewParameterization =", "const ViewByteCount =");
const Cache = Slice("const ShadowDirection =", "//------------------------------------------------------------------------------------------------------------------------\n//                                                    COLOUR STAMPS")
    .replace("const IrradianceCacheExtent = 32;", "const IrradianceCacheExtent = Case.extent;");
const Recorder = Slice("function TranscribeIrradianceParameterization()", "async function ConstructRaymarchUnit()");
const Cases = [
    { extent: 32, passes: 4, scalar: 0, cache: 0, zeroDirection: 0 },
    { extent: 5, passes: 0, scalar: 1, cache: 1, zeroDirection: 0 },
    { extent: 6, passes: 1, scalar: 0, cache: 1, zeroDirection: 1 },
    { extent: 17, passes: 3, scalar: 1, cache: 0, zeroDirection: 0 },
    { extent: 64, passes: 7, scalar: 0, cache: 0, zeroDirection: 0 }
];
const Native = spawnSync(Executable, [], { encoding: "utf8", input: Cases.map(C =>
    [C.extent, C.passes, C.scalar, C.cache, C.zeroDirection].join(" ")).join("\n") + "\n" });
assert.equal(Native.status, 0, Native.stderr);
const Outputs = Native.stdout.trim().split("\n").map(Line => JSON.parse(Line));
assert.equal(Outputs.length, Cases.length);
let Total = 0;
for (let Index = 0; Index < Cases.length; ++Index)
{
    const Case = Cases[Index], Context = vm.createContext({ Case });
    vm.runInContext(`${View}\n${Cache}\n${Recorder}\n
        if (Case.zeroDirection) ShadowDirection.splice(0, 3, 0, 0, 0);
        IrradianceProfile.PropagationPasses = Case.passes;
        const Texture = (slot) => ({ slot, createView() { return this; } });
        const VoxelStore = { ScalarRead: Texture(Case.scalar), LinearSampler: {} };
        IrradianceAssembly.SeedUnit = 'SeedIrradiance';
        IrradianceAssembly.PropagateUnit = 'PropagateIrradiance';
        IrradianceAssembly.CacheRead = Texture(Case.cache);
        IrradianceAssembly.CacheWrite = Texture(1 - Case.cache);
        let Uniform = [], Pipeline, Group;
        const DeviceProfile = { Device: { queue: { writeBuffer(_buffer, _offset, data) { Uniform = Array.from(data); } },
                                          createBindGroup(group) { return group; } } };
        const Dispatches = [];
        const CommandRecorder = { beginComputePass() { return {
            setPipeline(value) { Pipeline = value; }, setBindGroup(_index, value) { Group = value; },
            dispatchWorkgroups(x,y,z) { Dispatches.push([Pipeline, x,y,z, Group.entries[1].resource.slot,
                Group.entries[2].resource.slot, Group.entries[4].resource.slot]); }, end() {} }; } };
        RecordIrradianceSolve(CommandRecorder);
        globalThis.Result = { uniform: Uniform, dispatches: Dispatches, finalSlot: IrradianceAssembly.CacheRead.slot };
    `, Context);
    const Expected = JSON.parse(JSON.stringify(Context.Result));
    assert.deepEqual(Outputs[Index], Expected, `Irradiance plan case ${Index}`);
    Total += Expected.dispatches.length;
}
console.log(`Irradiance plan parity: ${Cases.length} cases, ${Cases.length * 20} uniform values and ${Total} dispatches match the retained JavaScript.`);
