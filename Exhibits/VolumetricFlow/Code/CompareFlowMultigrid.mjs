// Execute C++-planned multi-dispatch V-cycles with the reference WGSL and native Slang/SPIR-V.
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { create, globals } from "../../../Experimental/VehicleHmiPrototype/Tools/node_modules/webgpu/index.js";
import { ReadBuffer } from "../../../Experimental/VehicleHmiPrototype/Tools/GpuProbe.mjs";

Object.assign(globalThis, globals);
const [NativePlanner, ShaderDirectory] = process.argv.slice(2);
assert.ok(NativePlanner && ShaderDirectory, "Expected the native planner executable and compiled shader directory");
const Source = fs.readFileSync("Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html", "utf8");
const Header = "const ProjectionShaderSource = /* wgsl */ `";
const Start = Source.indexOf(Header), End = Source.indexOf("`;", Start + Header.length);
assert.ok(Start >= 0 && End > Start);
const Gpu = create(["backend=vulkan"]);
const Adapter = await Gpu.requestAdapter();
assert.ok(Adapter, "No executing WebGPU Vulkan adapter");
const Device = await Adapter.requestDevice();
const Errors = [];
Device.addEventListener("uncapturederror", Event => Errors.push(Event.error.message));
const Module = Device.createShaderModule({ code: Source.slice(Start + Header.length, End) });
const Diagnostics = await Module.getCompilationInfo();
assert.deepEqual(Diagnostics.messages.filter(Message => Message.type === "error").map(Message => Message.message), []);
const Layout = Device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
    { binding: 1, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: "float", viewDimension: "3d" } },
    ...[2,3].map(binding => ({ binding, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: "unfilterable-float", viewDimension: "3d" } })),
    { binding: 4, visibility: GPUShaderStage.COMPUTE, storageTexture: { access: "write-only", format: "rgba16float", viewDimension: "3d" } },
    ...[5,6].map(binding => ({ binding, visibility: GPUShaderStage.COMPUTE, storageTexture: { access: "write-only", format: "r32float", viewDimension: "3d" } })),
    { binding: 7, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
    { binding: 8, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: "unfilterable-float", viewDimension: "3d" } }
] });
const PipelineLayout = Device.createPipelineLayout({ bindGroupLayouts: [Layout] });
const Names = ["RelaxPressure", "EvaluateResidual", "RestrictResidual", "ClearPressureLevel", "ProlongateCorrection"];
const Pipelines = Object.fromEntries(Names.map(entryPoint => [entryPoint, Device.createComputePipeline({
    layout: PipelineLayout, compute: { module: Module, entryPoint }
})]));
const Scratch = fs.mkdtempSync(path.join(os.tmpdir(), "frontier-flow-cycle-"));
function Upload(Texture, Volume, Vector)
{
    const Extent = Volume.extent, Channels = Vector ? 4 : 1, Bpp = Vector ? 8 : 4;
    const RowBytes = Math.ceil(Extent * Bpp / 256) * 256;
    const Bytes = new Uint8Array(RowBytes * Extent * Extent), View = new DataView(Bytes.buffer);
    for (let Z = 0; Z < Extent; ++Z)
        for (let Y = 0; Y < Extent; ++Y)
            for (let X = 0; X < Extent; ++X)
                for (let C = 0; C < Channels; ++C)
                {
                    const Offset = (Z * Extent + Y) * RowBytes + X * Bpp + C * (Vector ? 2 : 4);
                    const Value = Volume.data[((Z * Extent + Y) * Extent + X) * Channels + C];
                    if (Vector) { assert.equal(Value, 0); View.setUint16(Offset, 0, true); }
                    else View.setFloat32(Offset, Value, true);
                }
    Device.queue.writeTexture({ texture: Texture }, Bytes, { bytesPerRow: RowBytes, rowsPerImage: Extent }, [Extent, Extent, Extent]);
}
async function ReadVolume(Texture, Extent)
{
    const RowBytes = Math.ceil(Extent * 4 / 256) * 256;
    const Buffer = Device.createBuffer({ size: RowBytes * Extent * Extent, usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
    const Encoder = Device.createCommandEncoder();
    Encoder.copyTextureToBuffer({ texture: Texture }, { buffer: Buffer, bytesPerRow: RowBytes, rowsPerImage: Extent },
                                [Extent, Extent, Extent]);
    Device.queue.submit([Encoder.finish()]);
    const Bytes = await ReadBuffer(Device, Buffer), View = new DataView(Bytes), Out = [];
    for (let Z = 0; Z < Extent; ++Z)
        for (let Y = 0; Y < Extent; ++Y)
            for (let X = 0; X < Extent; ++X)
                Out.push(View.getFloat32((Z * Extent + Y) * RowBytes + X * 4, true));
    Buffer.destroy();
    return Out;
}
function ResidualNorm(Pressure, RightHand, Extent)
{
    const At = (X, Y, Z) => Pressure[(Math.max(0, Math.min(Extent - 1, Z)) * Extent +
                                      Math.max(0, Math.min(Extent - 1, Y))) * Extent + Math.max(0, Math.min(Extent - 1, X))];
    let Sum = 0;
    for (let Z = 0; Z < Extent; ++Z)
        for (let Y = 0; Y < Extent; ++Y)
            for (let X = 0; X < Extent; ++X)
            {
                const Index = (Z * Extent + Y) * Extent + X;
                const Applied = At(X+1,Y,Z) + At(X-1,Y,Z) + At(X,Y+1,Z) + At(X,Y-1,Z) + At(X,Y,Z+1) + At(X,Y,Z-1) - 6 * At(X,Y,Z);
                Sum += (RightHand[Index] - Applied) ** 2;
            }
    return Math.sqrt(Sum / RightHand.length);
}
let Compared = 0, Maximum = 0;
try
{
    for (const Cycles of [1, 2])
    {
        const Extent = 16;
        const NativePlan = spawnSync(NativePlanner, [], { input: `${Extent} ${Cycles} 0.14 0 0\n`, encoding: "utf8" });
        assert.equal(NativePlan.status, 0, NativePlan.stderr);
        const Plan = JSON.parse(NativePlan.stdout);
        const Volumes = {};
        const Add = (Name, Width, Vector, Value) => {
            Volumes[Name] = { extent: Width, data: Array(Width ** 3 * (Vector ? 4 : 1)).fill(Value) };
        };
        Add("V0", Extent, true, 0); Add("V1", Extent, true, 0); Add("D", Extent, false, -37);
        for (const [Level, Uniform] of Plan.levels.entries())
        {
            const Width = Uniform[0];
            Add(`P${Level}_0`, Width, false, Level === 0 ? 0 : 7);
            Add(`P${Level}_1`, Width, false, Level === 0 ? 0 : 9);
            Add(`R${Level}`, Width, false, -37); Add(`E${Level}`, Width, false, -37);
        }
        for (let Z = 0; Z < Extent; ++Z)
            for (let Y = 0; Y < Extent; ++Y)
                for (let X = 0; X < Extent; ++X)
                    Volumes.R0.data[(Z * Extent + Y) * Extent + X] = (X - (Extent - 1) * 0.5) / 8;
        const FixturePath = path.join(Scratch, "fixture.json"), NativePath = path.join(Scratch, "native.json");
        fs.writeFileSync(FixturePath, JSON.stringify({ plan: Plan, volumes: Volumes }));
        const NativeRun = spawnSync("python3", ["Exhibits/VolumetricFlow/Code/RunFlowMultigridVulkan.py", FixturePath,
                                                path.resolve(ShaderDirectory), NativePath], { encoding: "utf8", maxBuffer: 8 * 1024 * 1024 });
        assert.equal(NativeRun.status, 0, `Vulkan V-cycle failed:\n${NativeRun.stdout}\n${NativeRun.stderr}`);
        const Native = JSON.parse(fs.readFileSync(NativePath, "utf8"));
        const Buffer = Data => { const Bytes = new Float32Array(Data); const Result = Device.createBuffer({ size: Bytes.byteLength,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }); Device.queue.writeBuffer(Result, 0, Bytes); return Result; };
        const Flow = Buffer(Plan.uniform), Levels = Plan.levels.map(Buffer), Textures = {};
        for (const [Name, Volume] of Object.entries(Volumes))
        {
            const Vector = Name.startsWith("V");
            Textures[Name] = Device.createTexture({ size: [Volume.extent, Volume.extent, Volume.extent], dimension: "3d",
                format: Vector ? "rgba16float" : "r32float",
                usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST });
            Upload(Textures[Name], Volume, Vector);
        }
        const Encoder = Device.createCommandEncoder();
        for (const [Name, Level, Groups, Bindings] of Plan.dispatches)
        {
            const Entries = [{ binding: 0, resource: { buffer: Flow } }, { binding: 7, resource: { buffer: Levels[Level] } }];
            for (const [Index, Binding] of [1,2,3,4,5,6,8].entries())
                Entries.push({ binding: Binding, resource: Textures[Bindings[Index]].createView() });
            const Group = Device.createBindGroup({ layout: Layout, entries: Entries });
            const Pass = Encoder.beginComputePass();
            Pass.setPipeline(Pipelines[Name]); Pass.setBindGroup(0, Group); Pass.dispatchWorkgroups(...Groups); Pass.end();
        }
        Device.queue.submit([Encoder.finish()]);
        const Results = {};
        for (const [Name, Volume] of Object.entries(Volumes))
        {
            if (Name.startsWith("V")) continue;
            Results[Name] = await ReadVolume(Textures[Name], Volume.extent);
            assert.equal(Native.volumes[Name].length, Results[Name].length);
            for (let Index = 0; Index < Results[Name].length; ++Index)
            {
                const Difference = Math.abs(Native.volumes[Name][Index] - Results[Name][Index]);
                assert.ok(Number.isFinite(Difference) && Difference < 2e-5,
                          `${Name} ${Cycles} cycles, cell ${Index}: ${Native.volumes[Name][Index]} != ${Results[Name][Index]}`);
                Maximum = Math.max(Maximum, Difference); ++Compared;
            }
        }
        const InitialResidual = ResidualNorm(Volumes.P0_0.data, Volumes.R0.data, Extent);
        const FinalResidual = ResidualNorm(Results[`P0_${Plan.finalSlots[0]}`], Volumes.R0.data, Extent);
        assert.ok(FinalResidual < InitialResidual, `V-cycle did not reduce residual: ${InitialResidual} -> ${FinalResidual}`);
        console.log(`${Cycles} V-cycle(s): ${Plan.dispatches.length} dispatches, residual ${InitialResidual.toFixed(6)} -> ` +
                    `${FinalResidual.toFixed(6)}, device ${Native.device}`);
        Flow.destroy(); Levels.forEach(Level => Level.destroy()); Object.values(Textures).forEach(Texture => Texture.destroy());
    }
    assert.deepEqual(Errors, []);
    console.log(`Chained multigrid parity: ${Compared} scalar values, maximum |Δ| ${Maximum}.`);
}
finally
{
    Device.destroy();
    fs.rmSync(Scratch, { recursive: true, force: true });
}
