// Execute the retained solver WGSL and compare all output voxels to the translated Slang/SPIR-V kernels.
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import vm from "node:vm";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { create, globals } from "../../../Experimental/VehicleHmiPrototype/Tools/node_modules/webgpu/index.js";
import { HalfToFloat, ReadBuffer } from "../../../Experimental/VehicleHmiPrototype/Tools/GpuProbe.mjs";

Object.assign(globalThis, globals);
const [ShaderDirectory] = process.argv.slice(2);
assert.ok(ShaderDirectory, "Expected the compiled SPIR-V directory");
const Entries = ["AdvectVelocityCarrier", "AdvectScalarCarrier", "AccumulateInfluence", "InjectScalarSource"];
const VelocityOutputs = new Set(["AdvectVelocityCarrier", "AccumulateInfluence"]);
const Source = fs.readFileSync("Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html", "utf8");
const Header = "const SolverShaderSource = /* wgsl */ `";
const Start = Source.indexOf(Header), End = Source.indexOf("`;", Start + Header.length);
assert.ok(Start >= 0 && End > Start, "Reference SolverShaderSource not found");
const DefaultsStart = Source.indexOf("const SolverParameterization ="), DefaultsEnd = Source.indexOf("const SolverDispatchExtent =", DefaultsStart);
const Defaults = JSON.parse(vm.runInNewContext(Source.slice(DefaultsStart, DefaultsEnd) +
    "\nJSON.stringify({ parameters: SolverParameterization, order: UniformFieldOrder })"));
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
    ...[1,2].map(binding => ({ binding, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: "float", viewDimension: "3d" } })),
    { binding: 3, visibility: GPUShaderStage.COMPUTE, sampler: { type: "filtering" } },
    ...[4,5].map(binding => ({ binding, visibility: GPUShaderStage.COMPUTE,
        storageTexture: { access: "write-only", format: "rgba16float", viewDimension: "3d" } }))
] });
const PipelineLayout = Device.createPipelineLayout({ bindGroupLayouts: [Layout] });
const Pipelines = Entries.map(entryPoint => Device.createComputePipeline({ layout: PipelineLayout, compute: { module: Module, entryPoint } }));
const Sampler = Device.createSampler({ magFilter: "linear", minFilter: "linear", addressModeU: "clamp-to-edge",
                                       addressModeV: "clamp-to-edge", addressModeW: "clamp-to-edge" });
const Scratch = fs.mkdtempSync(path.join(os.tmpdir(), "frontier-flow-transport-"));
function EncodeHalf(Value)
{
    if (Value === 0) return 0;
    const Sign = Value < 0 ? 0x8000 : 0, Absolute = Math.abs(Value), Exponent = Math.floor(Math.log2(Absolute));
    return Sign | ((Exponent + 15) << 10) | Math.round((Absolute / 2 ** Exponent - 1) * 1024);
}
function Upload(Texture, Extent, Readings)
{
    const RowBytes = Math.ceil(Extent * 8 / 256) * 256;
    const Bytes = new Uint8Array(RowBytes * Extent * Extent), View = new DataView(Bytes.buffer);
    for (let Z = 0; Z < Extent; ++Z)
        for (let Y = 0; Y < Extent; ++Y)
            for (let X = 0; X < Extent; ++X)
                for (let C = 0; C < 4; ++C)
                    View.setUint16((Z * Extent + Y) * RowBytes + X * 8 + C * 2,
                                   EncodeHalf(Readings[((Z * Extent + Y) * Extent + X) * 4 + C]), true);
    Device.queue.writeTexture({ texture: Texture }, Bytes, { bytesPerRow: RowBytes, rowsPerImage: Extent }, [Extent, Extent, Extent]);
}
async function ReadVolume(Texture, Extent)
{
    const RowBytes = Math.ceil(Extent * 8 / 256) * 256;
    const Buffer = Device.createBuffer({ size: RowBytes * Extent * Extent, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC });
    const Encoder = Device.createCommandEncoder();
    Encoder.copyTextureToBuffer({ texture: Texture }, { buffer: Buffer, bytesPerRow: RowBytes, rowsPerImage: Extent },
                                [Extent, Extent, Extent]);
    Device.queue.submit([Encoder.finish()]);
    const Bytes = await ReadBuffer(Device, Buffer), View = new DataView(Bytes), Out = [];
    for (let Z = 0; Z < Extent; ++Z)
        for (let Y = 0; Y < Extent; ++Y)
            for (let X = 0; X < Extent; ++X)
                for (let C = 0; C < 4; ++C)
                    Out.push(HalfToFloat(View.getUint16((Z * Extent + Y) * RowBytes + X * 8 + C * 2, true)));
    Buffer.destroy();
    return Out;
}
function Fixture(Shape)
{
    const Extent = Shape === 1 ? 5 : 6, Cells = Extent ** 3;
    const Parameters = { ...Defaults.parameters, VoxelExtent: Extent, "Δτ": 0.14, ElapsedInterval: 1.5,
        AdvectionDeltatau: Shape === 1 ? 0.25 : 0.09, MacCormackEnabled: Shape === 1 ? 0 : 1,
        CombustionEnabled: Shape === 1 ? 0 : 1, WindGustStrength: Shape === 0 ? 0.6 : 0,
        WindLongitudinal: 0.2, WindVertical: -0.1, WindLateral: 0.15, SwirlStrength: 0.45, InjectionVelocity: 0.35,
        SourceLongitude: 0.5, SourceAltitude: 0.5, SourceLatitude: 0.5, InjectionRadius: 0.38,
        SourceMinorScale: 0.4, SourceShape: Shape, NoiseGain: Shape === 1 ? 0 : 0.55,
        CarrierCeiling: 6, VelocityCeiling: 0.8 };
    const Uniform = Array.from(new Float32Array([...Defaults.order.map(Field => Parameters[Field]), 0]));
    return { extent: Extent, uniform: Uniform,
        velocity: Array.from({ length: Cells }, (_, I) => [(I % 7 - 3) * 0.125, (I * 3 % 9 - 4) * 0.125,
                                                         (I * 5 % 11 - 5) * 0.125, 0]).flat(),
        scalar: Array.from({ length: Cells }, (_, I) => [0.25 + (I % 9) * 0.125, 0.25 + (I * 3 % 13) * 0.125,
                                                       0.125 + (I * 7 % 9) * 0.125, (I % 5) * 0.125]).flat() };
}
let Compared = 0, Maximum = 0;
try
{
    for (let Shape = 0; Shape <= 4; ++Shape)
    {
        const FixtureData = Fixture(Shape), Extent = FixtureData.extent, Cells = Extent ** 3;
        const FixturePath = path.join(Scratch, "fixture.json"), NativePath = path.join(Scratch, "native.json");
        fs.writeFileSync(FixturePath, JSON.stringify(FixtureData));
        const NativeRun = spawnSync("python3", ["Exhibits/VolumetricFlow/Code/RunFlowTransportVulkan.py", FixturePath,
                                                path.resolve(ShaderDirectory), NativePath], { encoding: "utf8" });
        assert.equal(NativeRun.status, 0, `native Vulkan run failed:\n${NativeRun.stdout}\n${NativeRun.stderr}`);
        const Native = JSON.parse(fs.readFileSync(NativePath, "utf8"));
        const Flow = Device.createBuffer({ size: 192, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        Device.queue.writeBuffer(Flow, 0, new Float32Array(FixtureData.uniform));
        const Textures = [];
        const MakeTexture = () => { const Texture = Device.createTexture({ size: [Extent, Extent, Extent], dimension: "3d",
            format: "rgba16float", usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING |
                                         GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST });
            Textures.push(Texture); return Texture; };
        const InputVelocity = MakeTexture(), InputScalar = MakeTexture();
        Upload(InputVelocity, Extent, FixtureData.velocity); Upload(InputScalar, Extent, FixtureData.scalar);
        const Outputs = Entries.map(MakeTexture), DummyVelocity = MakeTexture(), DummyScalar = MakeTexture();
        for (const Output of Outputs) Upload(Output, Extent, Array(Cells * 4).fill(-37));
        const Encoder = Device.createCommandEncoder();
        for (const [Index, Name] of Entries.entries())
        {
            const Group = Device.createBindGroup({ layout: Layout, entries: [
                { binding: 0, resource: { buffer: Flow } }, { binding: 1, resource: InputVelocity.createView() },
                { binding: 2, resource: InputScalar.createView() }, { binding: 3, resource: Sampler },
                { binding: 4, resource: (VelocityOutputs.has(Name) ? Outputs[Index] : DummyVelocity).createView() },
                { binding: 5, resource: (VelocityOutputs.has(Name) ? DummyScalar : Outputs[Index]).createView() }
            ] });
            const Pass = Encoder.beginComputePass();
            Pass.setPipeline(Pipelines[Index]); Pass.setBindGroup(0, Group);
            Pass.dispatchWorkgroups(Math.ceil(Extent / 8), Math.ceil(Extent / 8), Math.ceil(Extent / 4)); Pass.end();
        }
        Device.queue.submit([Encoder.finish()]);
        for (const [Index, Name] of Entries.entries())
        {
            const Reference = await ReadVolume(Outputs[Index], Extent);
            assert.equal(Native[Name].length, Reference.length);
            assert.ok(Reference.every(Value => Value !== -37), `${Name} failed to write its entire target`);
            const Original = VelocityOutputs.has(Name) ? FixtureData.velocity : FixtureData.scalar;
            assert.ok(Reference.some((Value, Position) => Value !== Original[Position]), `${Name} produced no changes`);
            let Max = 0;
            for (let Element = 0; Element < Reference.length; ++Element)
            {
                const Difference = Math.abs(Native[Name][Element] - Reference[Element]);
                assert.ok(Number.isFinite(Difference) && Difference < 0.02,
                          `${Name}, shape ${Shape}, index ${Element}: ${Native[Name][Element]} != ${Reference[Element]}`);
                Max = Math.max(Max, Difference); Compared++;
            }
            Maximum = Math.max(Maximum, Max);
            console.log(`${Name}, shape ${Shape}: ${Reference.length} scalars, max |Δ| ${Max}`);
        }
        Flow.destroy(); Textures.forEach(Texture => Texture.destroy());
    }
    assert.deepEqual(Errors, []);
    console.log(`Flow transport parity: ${Compared} scalars over all five source shapes, maximum |Δ| ${Maximum}.`);
}
finally
{
    Device.destroy();
    fs.rmSync(Scratch, { recursive: true, force: true });
}
