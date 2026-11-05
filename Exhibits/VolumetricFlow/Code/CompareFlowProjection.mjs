// Compare the retained editor's actual WGSL against native Slang/SPIR-V, both executed on Vulkan.
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { create, globals } from "../../../Experimental/VehicleHmiPrototype/Tools/node_modules/webgpu/index.js";
import { HalfToFloat, ReadBuffer } from "../../../Experimental/VehicleHmiPrototype/Tools/GpuProbe.mjs";

Object.assign(globalThis, globals);
const [ShaderDirectory] = process.argv.slice(2);
assert.ok(ShaderDirectory, "Expected the compiled SPIR-V directory");
const Entries = ["EvaluateDivergence", "RelaxPressure", "EvaluateResidual", "RestrictResidual",
                 "ProlongateCorrection", "ClearPressureLevel", "SubtractPressureGradient", "RestoreVorticity"];
const Scalar = new Set(Entries.slice(0, 6));
const Pressure = new Set(["RelaxPressure", "ProlongateCorrection", "ClearPressureLevel"]);
const Source = fs.readFileSync("Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html", "utf8");
const Header = "const ProjectionShaderSource = /* wgsl */ `";
const Start = Source.indexOf(Header), End = Source.indexOf("`;", Start + Header.length);
assert.ok(Start >= 0 && End > Start, "Reference ProjectionShaderSource not found");
const Program = Source.slice(Start + Header.length, End);
const Gpu = create(["backend=vulkan"]); // retain the instance through the lifetime of the device
const Adapter = await Gpu.requestAdapter();
assert.ok(Adapter, "No executing WebGPU Vulkan adapter");
const Device = await Adapter.requestDevice();
const Errors = [];
Device.addEventListener("uncapturederror", Event => Errors.push(Event.error.message));
const Module = Device.createShaderModule({ code: Program });
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
const Pipelines = Entries.map(entryPoint => Device.createComputePipeline({
    layout: PipelineLayout, compute: { module: Module, entryPoint }
}));
const Scratch = fs.mkdtempSync(path.join(os.tmpdir(), "frontier-flow-projection-"));
function EncodeHalf(Value)
{
    if (Value === 0) return 0;
    const Sign = Value < 0 ? 0x8000 : 0, Absolute = Math.abs(Value);
    const Exponent = Math.floor(Math.log2(Absolute));
    return Sign | ((Exponent + 15) << 10) | Math.round((Absolute / 2 ** Exponent - 1) * 1024);
}
function UploadTexture(Texture, Extent, Readings, Vector)
{
    const BytesPerTexel = Vector ? 8 : 4, Channels = Vector ? 4 : 1;
    const RowBytes = Math.ceil(Extent * BytesPerTexel / 256) * 256;
    const Bytes = new Uint8Array(RowBytes * Extent * Extent), View = new DataView(Bytes.buffer);
    for (let Z = 0; Z < Extent; ++Z)
        for (let Y = 0; Y < Extent; ++Y)
            for (let X = 0; X < Extent; ++X)
                for (let Channel = 0; Channel < Channels; ++Channel)
                {
                    const Index = ((Z * Extent + Y) * Extent + X) * Channels + Channel;
                    const Offset = (Z * Extent + Y) * RowBytes + X * BytesPerTexel + Channel * (Vector ? 2 : 4);
                    if (Vector) View.setUint16(Offset, EncodeHalf(Readings[Index]), true);
                    else View.setFloat32(Offset, Readings[Index], true);
                }
    Device.queue.writeTexture({ texture: Texture }, Bytes, { bytesPerRow: RowBytes, rowsPerImage: Extent },
                              [Extent, Extent, Extent]);
}
async function ReadVolume(Texture, Extent, Vector)
{
    const BytesPerTexel = Vector ? 8 : 4, Channels = Vector ? 4 : 1;
    const RowBytes = Math.ceil(Extent * BytesPerTexel / 256) * 256;
    const Buffer = Device.createBuffer({ size: RowBytes * Extent * Extent, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC });
    const Encoder = Device.createCommandEncoder();
    Encoder.copyTextureToBuffer({ texture: Texture }, { buffer: Buffer, bytesPerRow: RowBytes, rowsPerImage: Extent },
                                [Extent, Extent, Extent]);
    Device.queue.submit([Encoder.finish()]);
    const Bytes = await ReadBuffer(Device, Buffer), View = new DataView(Bytes), Out = [];
    for (let Z = 0; Z < Extent; ++Z)
        for (let Y = 0; Y < Extent; ++Y)
            for (let X = 0; X < Extent; ++X)
                for (let Channel = 0; Channel < Channels; ++Channel)
                {
                    const Offset = (Z * Extent + Y) * RowBytes + X * BytesPerTexel + Channel * (Vector ? 2 : 4);
                    Out.push(Vector ? HalfToFloat(View.getUint16(Offset, true)) : View.getFloat32(Offset, true));
                }
    Buffer.destroy();
    return Out;
}
function Fixture(Extent, Enabled, Weight)
{
    const Cells = Extent ** 3;
    return { extent: Extent, coarse: Math.floor(Extent / 2), delta: 1 / 30, strength: 1.2, enabled: Enabled, weight: Weight,
        velocity: Array.from({ length: Cells }, (_, I) => [(I % 17 - 8) * 0.5, (I * 3 % 13 - 6) * 0.5,
                                                         (I * 5 % 19 - 9) * 0.5, (I % 3) * 0.5]).flat(),
        pressure: Array.from({ length: Cells }, (_, I) => (I * 7 % 19 - 9) * 0.125),
        divergence: Array.from({ length: Cells }, (_, I) => (I * 11 % 23 - 11) * 0.25),
        residual: Array.from({ length: Cells }, (_, I) => (I * 5 % 29 - 14) * 0.125) };
}
let Compared = 0, Maximum = 0, DeviceName = "";
try
{
    for (const FixtureData of [Fixture(6, 1, 2/3), Fixture(5, 0, 1)])
    {
        const Extent = FixtureData.extent, Cells = Extent ** 3;
        const FixturePath = path.join(Scratch, "fixture.json"), NativePath = path.join(Scratch, "native.json");
        fs.writeFileSync(FixturePath, JSON.stringify(FixtureData));
        const Native = spawnSync("python3", ["Exhibits/VolumetricFlow/Code/RunFlowProjectionVulkan.py", FixturePath,
                                             path.resolve(ShaderDirectory), NativePath], { encoding: "utf8" });
        assert.equal(Native.status, 0, `native Vulkan run failed:\n${Native.stdout}\n${Native.stderr}`);
        const Actual = JSON.parse(fs.readFileSync(NativePath, "utf8"));
        DeviceName = Actual.device;
        const Flow = new Float32Array(48);
        Flow[0] = FixtureData.delta; Flow[3] = FixtureData.strength; Flow[18] = FixtureData.enabled;
        const Level = new Float32Array([Extent, FixtureData.coarse, FixtureData.weight, 0]);
        const MakeBuffer = Scalars => {
            const Buffer = Device.createBuffer({ size: Scalars.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
            Device.queue.writeBuffer(Buffer, 0, Scalars); return Buffer;
        };
        const FlowBuffer = MakeBuffer(Flow), LevelBuffer = MakeBuffer(Level), Textures = [];
        const MakeTexture = Vector => {
            const Texture = Device.createTexture({ size: [Extent, Extent, Extent], dimension: "3d",
                format: Vector ? "rgba16float" : "r32float",
                usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST });
            Textures.push(Texture); return Texture;
        };
        const Inputs = [MakeTexture(true), MakeTexture(false), MakeTexture(false), MakeTexture(false)];
        for (const [Index, Readings] of [FixtureData.velocity, FixtureData.pressure, FixtureData.divergence, FixtureData.residual].entries())
            UploadTexture(Inputs[Index], Extent, Readings, Index === 0);
        const Outputs = Entries.map(Name => MakeTexture(!Scalar.has(Name)));
        const DummyVelocity = MakeTexture(true), DummyPressure = MakeTexture(false), DummyDivergence = MakeTexture(false);
        for (const [Index, Name] of Entries.entries())
            UploadTexture(Outputs[Index], Extent, Array(Cells * (Scalar.has(Name) ? 1 : 4)).fill(-37), !Scalar.has(Name));
        const Encoder = Device.createCommandEncoder();
        for (const [Index, Name] of Entries.entries())
        {
            const VelocityTarget = Scalar.has(Name) ? DummyVelocity : Outputs[Index];
            const PressureTarget = Pressure.has(Name) ? Outputs[Index] : DummyPressure;
            const DivergenceTarget = Scalar.has(Name) && !Pressure.has(Name) ? Outputs[Index] : DummyDivergence;
            const Group = Device.createBindGroup({ layout: Layout, entries: [
                { binding: 0, resource: { buffer: FlowBuffer } }, { binding: 1, resource: Inputs[0].createView() },
                { binding: 2, resource: Inputs[1].createView() }, { binding: 3, resource: Inputs[2].createView() },
                { binding: 4, resource: VelocityTarget.createView() }, { binding: 5, resource: PressureTarget.createView() },
                { binding: 6, resource: DivergenceTarget.createView() }, { binding: 7, resource: { buffer: LevelBuffer } },
                { binding: 8, resource: Inputs[3].createView() }
            ] });
            const Pass = Encoder.beginComputePass();
            Pass.setPipeline(Pipelines[Index]); Pass.setBindGroup(0, Group);
            Pass.dispatchWorkgroups(Math.ceil(Extent / 8), Math.ceil(Extent / 8), Math.ceil(Extent / 4));
            Pass.end();
        }
        Device.queue.submit([Encoder.finish()]);
        for (const [Index, Name] of Entries.entries())
        {
            const Expected = await ReadVolume(Outputs[Index], Extent, !Scalar.has(Name));
            assert.equal(Actual[Name].length, Expected.length);
            if (Name === "RestrictResidual")
                assert.equal(Expected.filter(Value => Value !== -37).length, FixtureData.coarse ** 3,
                             "restriction must write exactly the coarse volume");
            else
                assert.ok(Expected.every(Value => Value !== -37), `${Name} failed to write its entire target`);
            if (Name === "ClearPressureLevel") assert.ok(Expected.every(Value => Value === 0));
            if (Name === "RestoreVorticity")
            {
                const Changed = Array.from({ length: Cells }, (_, Cell) => Cell).some(Cell =>
                    [0,1,2].some(Channel => Expected[Cell * 4 + Channel] !== FixtureData.velocity[Cell * 4 + Channel]));
                assert.equal(Changed, FixtureData.enabled === 1, "vorticity toggle did not control velocity updates");
            }
            let Max = 0;
            for (let Element = 0; Element < Expected.length; ++Element)
            {
                const Difference = Math.abs(Actual[Name][Element] - Expected[Element]);
                assert.ok(Number.isFinite(Difference) && Difference <= (Scalar.has(Name) ? 0.00001 : 0.004),
                          `${Name} extent ${Extent}, index ${Element}: ${Actual[Name][Element]} != ${Expected[Element]}`);
                Max = Math.max(Max, Difference); Compared++;
            }
            Maximum = Math.max(Maximum, Max);
            console.log(`${Name} ${Extent}³: ${Expected.length} scalars, max |Δ| ${Max}`);
        }
        FlowBuffer.destroy(); LevelBuffer.destroy(); Textures.forEach(Texture => Texture.destroy());
    }
    assert.deepEqual(Errors, []);
    console.log(`Flow projection parity: ${Compared} scalars, maximum |Δ| ${Maximum}, device ${DeviceName}.`);
}
finally
{
    Device.destroy();
    fs.rmSync(Scratch, { recursive: true, force: true });
}
