// Execute a complete C++-planned flow solver step with both retained WGSL and native Slang/SPIR-V.
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { create, globals } from "../../../Experimental/VehicleHmiPrototype/Tools/node_modules/webgpu/index.js";
import { HalfToFloat, ReadBuffer } from "../../../Experimental/VehicleHmiPrototype/Tools/GpuProbe.mjs";

Object.assign(globalThis, globals);
const [NativePlanner, ProjectionDirectory, TransportDirectory] = process.argv.slice(2);
assert.ok(NativePlanner && ProjectionDirectory && TransportDirectory, "Expected the native planner and both shader directories");
const Source = fs.readFileSync("Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html", "utf8");
function ShaderSource(Name)
{
    const Header = `const ${Name} = /* wgsl */ \``;
    const Start = Source.indexOf(Header), End = Source.indexOf("`;", Start + Header.length);
    assert.ok(Start >= 0 && End > Start, `${Name} missing`);
    return Source.slice(Start + Header.length, End);
}
const Gpu = create(["backend=vulkan"]);
const Adapter = await Gpu.requestAdapter();
assert.ok(Adapter, "No executing WebGPU Vulkan adapter");
const Device = await Adapter.requestDevice();
const Errors = [];
Device.addEventListener("uncapturederror", Event => Errors.push(Event.error.message));
const Modules = { transport: Device.createShaderModule({ code: ShaderSource("SolverShaderSource") }),
                  projection: Device.createShaderModule({ code: ShaderSource("ProjectionShaderSource") }) };
for (const Module of Object.values(Modules))
{
    const Diagnostics = await Module.getCompilationInfo();
    assert.deepEqual(Diagnostics.messages.filter(Message => Message.type === "error").map(Message => Message.message), []);
}
const ProjectionLayout = Device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
    { binding: 1, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: "float", viewDimension: "3d" } },
    ...[2,3].map(binding => ({ binding, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: "unfilterable-float", viewDimension: "3d" } })),
    { binding: 4, visibility: GPUShaderStage.COMPUTE, storageTexture: { access: "write-only", format: "rgba16float", viewDimension: "3d" } },
    ...[5,6].map(binding => ({ binding, visibility: GPUShaderStage.COMPUTE, storageTexture: { access: "write-only", format: "r32float", viewDimension: "3d" } })),
    { binding: 7, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
    { binding: 8, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: "unfilterable-float", viewDimension: "3d" } }
] });
const TransportLayout = Device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
    ...[1,2].map(binding => ({ binding, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: "float", viewDimension: "3d" } })),
    { binding: 3, visibility: GPUShaderStage.COMPUTE, sampler: { type: "filtering" } },
    ...[4,5].map(binding => ({ binding, visibility: GPUShaderStage.COMPUTE,
        storageTexture: { access: "write-only", format: "rgba16float", viewDimension: "3d" } }))
] });
const Layouts = { transport: TransportLayout, projection: ProjectionLayout };
const PipelineLayouts = Object.fromEntries(Object.entries(Layouts).map(([Name, Layout]) =>
    [Name, Device.createPipelineLayout({ bindGroupLayouts: [Layout] })]));
const Entries = { transport: ["AdvectVelocityCarrier", "AdvectScalarCarrier", "AccumulateInfluence", "InjectScalarSource"],
                  projection: ["EvaluateDivergence", "RelaxPressure", "EvaluateResidual", "RestrictResidual", "ProlongateCorrection",
                               "ClearPressureLevel", "SubtractPressureGradient", "RestoreVorticity"] };
const Pipelines = {};
for (const [Category, Names] of Object.entries(Entries))
    for (const Name of Names)
        Pipelines[Name] = Device.createComputePipeline({ layout: PipelineLayouts[Category], compute: { module: Modules[Category], entryPoint: Name } });
const Sampler = Device.createSampler({ magFilter: "linear", minFilter: "linear", addressModeU: "clamp-to-edge",
                                       addressModeV: "clamp-to-edge", addressModeW: "clamp-to-edge" });
const Scratch = fs.mkdtempSync(path.join(os.tmpdir(), "frontier-flow-step-"));
function IsVector(Name) { return Name.startsWith("V") || Name.startsWith("S"); }
function EncodeHalf(Value)
{
    if (Value === 0) return 0;
    const Sign = Value < 0 ? 0x8000 : 0, Absolute = Math.abs(Value), Exponent = Math.floor(Math.log2(Absolute));
    return Sign | ((Exponent + 15) << 10) | Math.round((Absolute / 2 ** Exponent - 1) * 1024);
}
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
                    if (Vector) View.setUint16(Offset, EncodeHalf(Value), true); else View.setFloat32(Offset, Value, true);
                }
    Device.queue.writeTexture({ texture: Texture }, Bytes, { bytesPerRow: RowBytes, rowsPerImage: Extent }, [Extent, Extent, Extent]);
}
async function ReadVolume(Texture, Extent, Vector)
{
    const Bpp = Vector ? 8 : 4, Channels = Vector ? 4 : 1;
    const RowBytes = Math.ceil(Extent * Bpp / 256) * 256;
    const Buffer = Device.createBuffer({ size: RowBytes * Extent * Extent, usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
    const Encoder = Device.createCommandEncoder();
    Encoder.copyTextureToBuffer({ texture: Texture }, { buffer: Buffer, bytesPerRow: RowBytes, rowsPerImage: Extent }, [Extent, Extent, Extent]);
    Device.queue.submit([Encoder.finish()]);
    const Bytes = await ReadBuffer(Device, Buffer), View = new DataView(Bytes), Out = [];
    for (let Z = 0; Z < Extent; ++Z)
        for (let Y = 0; Y < Extent; ++Y)
            for (let X = 0; X < Extent; ++X)
                for (let C = 0; C < Channels; ++C)
                {
                    const Offset = (Z * Extent + Y) * RowBytes + X * Bpp + C * (Vector ? 2 : 4);
                    Out.push(Vector ? HalfToFloat(View.getUint16(Offset, true)) : View.getFloat32(Offset, true));
                }
    Buffer.destroy();
    return Out;
}
let Compared = 0, Maximum = 0;
try
{
    for (const [Extent, Mask] of [[8, 0], [16, 65537]])
    {
        const NativePlan = spawnSync(NativePlanner, [], { input: `${Extent} 1 0.14 0.7 ${Mask}\n`, encoding: "utf8", maxBuffer: 4 * 1024 * 1024 });
        assert.equal(NativePlan.status, 0, NativePlan.stderr);
        const Plan = JSON.parse(NativePlan.stdout), Cells = Extent ** 3, Volumes = {};
        const Add = (Name, Width, Vector, Value) => { Volumes[Name] = { extent: Width, data: Array(Width ** 3 * (Vector ? 4 : 1)).fill(Value) }; };
        for (const Name of ["V0", "V1", "S0", "S1"]) Add(Name, Extent, true, -37);
        Add("D", Extent, false, -37);
        for (const [Level, Uniform] of Plan.levels.entries())
        {
            const Width = Uniform[0];
            Add(`P${Level}_0`, Width, false, Level === 0 ? 0 : 7);
            Add(`P${Level}_1`, Width, false, Level === 0 ? 0 : 9);
            Add(`R${Level}`, Width, false, -37); Add(`E${Level}`, Width, false, -37);
        }
        const InitialVelocity = (Mask >> 16) & 1, InitialScalar = (Mask >> 17) & 1;
        Volumes[`V${InitialVelocity}`].data = Array.from({ length: Cells }, (_, I) =>
            [(I % 7 - 3) * 0.0625, (I * 3 % 9 - 4) * 0.0625, (I * 5 % 11 - 5) * 0.0625, 0]).flat();
        Volumes[`S${InitialScalar}`].data = Array.from({ length: Cells }, (_, I) =>
            [0.25 + (I % 9) * 0.125, 0.5 + (I * 3 % 9) * 0.125, 0.125 + (I * 7 % 7) * 0.125, (I % 5) * 0.125]).flat();
        const FixturePath = path.join(Scratch, "fixture.json"), NativePath = path.join(Scratch, "native.json");
        fs.writeFileSync(FixturePath, JSON.stringify({ plan: Plan, volumes: Volumes }));
        const NativeRun = spawnSync("python3", ["Exhibits/VolumetricFlow/Code/RunFlowStepVulkan.py", FixturePath,
            path.resolve(ProjectionDirectory), path.resolve(TransportDirectory), NativePath], { encoding: "utf8", maxBuffer: 8 * 1024 * 1024 });
        assert.equal(NativeRun.status, 0, `Vulkan flow step failed:\n${NativeRun.stdout}\n${NativeRun.stderr}`);
        const Native = JSON.parse(fs.readFileSync(NativePath, "utf8"));
        const Buffer = Data => { const Bytes = new Float32Array(Data); const Result = Device.createBuffer({ size: Bytes.byteLength,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }); Device.queue.writeBuffer(Result, 0, Bytes); return Result; };
        const Flow = Buffer(Plan.uniform), Levels = Plan.levels.map(Buffer), Textures = {};
        for (const [Name, Volume] of Object.entries(Volumes))
        {
            const Vector = IsVector(Name);
            Textures[Name] = Device.createTexture({ size: [Volume.extent, Volume.extent, Volume.extent], dimension: "3d",
                format: Vector ? "rgba16float" : "r32float",
                usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST });
            Upload(Textures[Name], Volume, Vector);
        }
        const Encoder = Device.createCommandEncoder();
        for (const [Name, Category, Level, Groups, Bindings] of Plan.stepDispatches)
        {
            const Entries = [{ binding: 0, resource: { buffer: Flow } }];
            const Slots = Category === "transport" ? [1,2,4,5] : [1,2,3,4,5,6,8];
            if (Category === "transport") Entries.push({ binding: 3, resource: Sampler });
            else Entries.push({ binding: 7, resource: { buffer: Levels[Level] } });
            for (const [Index, Binding] of Slots.entries()) Entries.push({ binding: Binding, resource: Textures[Bindings[Index]].createView() });
            const Group = Device.createBindGroup({ layout: Layouts[Category], entries: Entries });
            const Pass = Encoder.beginComputePass();
            Pass.setPipeline(Pipelines[Name]); Pass.setBindGroup(0, Group); Pass.dispatchWorkgroups(...Groups); Pass.end();
        }
        Device.queue.submit([Encoder.finish()]);
        const Results = {};
        for (const [Name, Volume] of Object.entries(Volumes))
        {
            Results[Name] = await ReadVolume(Textures[Name], Volume.extent, IsVector(Name));
            assert.equal(Native.volumes[Name].length, Results[Name].length);
            for (let Index = 0; Index < Results[Name].length; ++Index)
            {
                const Difference = Math.abs(Native.volumes[Name][Index] - Results[Name][Index]);
                assert.ok(Number.isFinite(Difference) && Difference < (IsVector(Name) ? 0.02 : 0.0001),
                          `${Name} ${Extent}³ cell ${Index}: ${Native.volumes[Name][Index]} != ${Results[Name][Index]}`);
                Maximum = Math.max(Maximum, Difference); ++Compared;
            }
        }
        const FinalV = Results[`V${Plan.stepSlots.velocity}`], FinalS = Results[`S${Plan.stepSlots.scalar}`];
        assert.ok(FinalV.every(Value => Value !== -37) && FinalS.every(Value => Value !== -37), "final carriers must be fully written");
        assert.ok(FinalV.some((Value, Index) => Value !== Volumes[`V${InitialVelocity}`].data[Index]), "velocity did not evolve");
        assert.ok(FinalS.some((Value, Index) => Value !== Volumes[`S${InitialScalar}`].data[Index]), "scalar carrier did not evolve");
        console.log(`${Extent}³: ${Plan.stepDispatches.length} full-step dispatches, device ${Native.device}`);
        Flow.destroy(); Levels.forEach(Level => Level.destroy()); Object.values(Textures).forEach(Texture => Texture.destroy());
    }
    assert.deepEqual(Errors, []);
    console.log(`Complete flow step parity: ${Compared} scalar values, maximum |Δ| ${Maximum}.`);
}
finally
{
    Device.destroy();
    fs.rmSync(Scratch, { recursive: true, force: true });
}
