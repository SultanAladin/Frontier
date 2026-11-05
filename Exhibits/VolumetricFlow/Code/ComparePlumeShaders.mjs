// Compare every result of the original WGSL and compiled Slang/SPIR-V pressure and vorticity entries.
// Both run through SwiftShader Vulkan: the reference through Dawn, the native program through Vulkan.
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
const Scratch = fs.mkdtempSync(path.join(os.tmpdir(), "frontier-plume-parity-"));
const Extent = [4, 4, 4], Cells = Extent.reduce((A, B) => A * B);
const Fixture = {
    extent: Extent, origin: [-2, 3, 0], prior: [-1, 3, 0], edge: 0.5, floor: -0.25, confinement: 1.4,
    velocity: Array.from({ length: Cells }, (_, Index) => [Index === 7 ? 64 : Index % 13 - 6,
        Index * 3 % 17 - 8, Index * 5 % 9 - 4, Index % 11 === 0 ? 1 : 0]).flat(),
    pressureA: Array.from({ length: Cells }, (_, Index) => (Index % 7 - 3) * 0.125),
    pressureB: Array.from({ length: Cells }, (_, Index) => (Index % 5 - 2) * 0.25)
};
const FixturePath = path.join(Scratch, "fixture.json"), NativePath = path.join(Scratch, "native.json");
fs.writeFileSync(FixturePath, JSON.stringify(Fixture));
const Native = spawnSync("python3", ["Exhibits/VolumetricFlow/Code/RunPlumeShadersVulkan.py", FixturePath,
                                      path.resolve(ShaderDirectory), NativePath], { encoding: "utf8" });
assert.equal(Native.status, 0, `native Vulkan run failed:\n${Native.stdout}\n${Native.stderr}`);
const Actual = JSON.parse(fs.readFileSync(NativePath, "utf8"));

// Keep the GPU instance alive for the full lifetime of its adapter and device.
const Gpu = create(["backend=vulkan"]);
const Adapter = await Gpu.requestAdapter();
assert.ok(Adapter, "No executing WebGPU Vulkan adapter");
const Device = await Adapter.requestDevice();
const Errors = [];
Device.addEventListener("uncapturederror", Event => Errors.push(Event.error.message));
const Usage = GPUBufferUsage;
const MakeBuffer = (Data, Flags) => {
    const Buffer = Device.createBuffer({ size: Data.byteLength, usage: Flags | Usage.COPY_DST | Usage.COPY_SRC });
    Device.queue.writeBuffer(Buffer, 0, Data);
    return Buffer;
};
const UniformBytes = new ArrayBuffer(256), UniformI = new Int32Array(UniformBytes),
      UniformU = new Uint32Array(UniformBytes), UniformF = new Float32Array(UniformBytes);
UniformI.set([...Fixture.origin, 0], 0); UniformI.set([...Fixture.prior, 0], 4);
UniformU.set([...Extent, 0], 8); UniformU.set(Extent.map(Value => Value * 2).concat(0), 12);
UniformF.set([Fixture.edge / 2, Fixture.edge, Fixture.floor, 1 / 60], 16);
UniformF[22] = Fixture.confinement;
const Uniform = MakeBuffer(UniformBytes, Usage.UNIFORM);
const Divergence = MakeBuffer(new Float32Array(Cells), Usage.STORAGE);
const PressureA = MakeBuffer(new Float32Array(Fixture.pressureA), Usage.STORAGE);
const PressureB = MakeBuffer(new Float32Array(Fixture.pressureB), Usage.STORAGE);
const Velocity = Device.createTexture({ size: Extent, dimension: "3d", format: "rgba16float",
    usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
const Target = Device.createTexture({ size: Extent, dimension: "3d", format: "rgba16float",
    usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC });
// Fixture values are small integers, all exactly representable as IEEE half floats.
function EncodeHalf(Value)
{
    if (Value === 0) return 0;
    const Sign = Value < 0 ? 0x8000 : 0, Absolute = Math.abs(Value);
    const Exponent = Math.floor(Math.log2(Absolute));
    return Sign | ((Exponent + 15) << 10) | Math.round((Absolute / 2 ** Exponent - 1) * 1024);
}
const RowBytes = 256, Texels = new Uint8Array(RowBytes * Extent[1] * Extent[2]);
const TexelView = new DataView(Texels.buffer);
for (let Z = 0; Z < Extent[2]; ++Z)
    for (let Y = 0; Y < Extent[1]; ++Y)
        for (let X = 0; X < Extent[0]; ++X)
            for (let Channel = 0; Channel < 4; ++Channel)
            {
                const Index = ((Z * Extent[1] + Y) * Extent[0] + X) * 4 + Channel;
                TexelView.setUint16((Z * Extent[1] + Y) * RowBytes + X * 8 + Channel * 2,
                                    EncodeHalf(Fixture.velocity[Index]), true);
            }
Device.queue.writeTexture({ texture: Velocity }, Texels, { bytesPerRow: RowBytes, rowsPerImage: Extent[1] }, Extent);
const Layout = Device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
    { binding: 1, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: "float", viewDimension: "3d" } },
    ...[2, 3, 4].map(binding => ({ binding, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } })),
    { binding: 5, visibility: GPUShaderStage.COMPUTE, storageTexture: { access: "write-only", format: "rgba16float", viewDimension: "3d" } }
] });
const MakeGroup = (Source, Destination) => Device.createBindGroup({ layout: Layout, entries: [
    { binding: 0, resource: { buffer: Uniform } }, { binding: 1, resource: Velocity.createView({ dimension: "3d" }) },
    { binding: 2, resource: { buffer: Divergence } }, { binding: 3, resource: { buffer: Source } },
    { binding: 4, resource: { buffer: Destination } }, { binding: 5, resource: Target.createView({ dimension: "3d" }) }
] });
const Groups = [MakeGroup(PressureA, PressureB), MakeGroup(PressureB, PressureA)];
const Source = fs.readFileSync("Experimental/VehicleHmiPrototype/Shader/PlumeRecords.wgsl", "utf8") + "\n" +
               fs.readFileSync("Experimental/VehicleHmiPrototype/Shader/PlumePressure.wgsl", "utf8");
const Module = Device.createShaderModule({ code: Source });
const Messages = await Module.getCompilationInfo();
assert.deepEqual(Messages.messages.filter(Message => Message.type === "error").map(Message => Message.message), []);
const PipelineLayout = Device.createPipelineLayout({ bindGroupLayouts: [Layout] });
const Pipelines = ["ResolveDivergence", "RelaxPressure", "ProjectVelocity"].map(entryPoint =>
    Device.createComputePipeline({ layout: PipelineLayout, compute: { module: Module, entryPoint } }));
const Encoder = Device.createCommandEncoder();
for (let Index = 0; Index < Pipelines.length; ++Index)
{
    const Pass = Encoder.beginComputePass();
    Pass.setPipeline(Pipelines[Index]); Pass.setBindGroup(0, Groups[Index < 2 ? 0 : 1]);
    Pass.dispatchWorkgroups(...Extent.map(Value => Math.ceil(Value / 4))); Pass.end();
}
const Readback = Device.createBuffer({ size: RowBytes * Extent[1] * Extent[2], usage: Usage.COPY_DST | Usage.MAP_READ });
Encoder.copyTextureToBuffer({ texture: Target }, { buffer: Readback, bytesPerRow: RowBytes, rowsPerImage: Extent[1] }, Extent);
Device.queue.submit([Encoder.finish()]);
const Expected = {
    divergence: Array.from(new Float32Array(await ReadBuffer(Device, Divergence))),
    pressureA: Array.from(new Float32Array(await ReadBuffer(Device, PressureA))),
    pressureB: Array.from(new Float32Array(await ReadBuffer(Device, PressureB))),
};
await Readback.mapAsync(GPUMapMode.READ);
const Output = new DataView(Readback.getMappedRange());
Expected.velocity = [];
for (let Z = 0; Z < Extent[2]; ++Z)
    for (let Y = 0; Y < Extent[1]; ++Y)
        for (let X = 0; X < Extent[0]; ++X)
            for (let Channel = 0; Channel < 4; ++Channel)
                Expected.velocity.push(HalfToFloat(Output.getUint16((Z * Extent[1] + Y) * RowBytes + X * 8 + Channel * 2, true)));
Readback.unmap();

// Execute the original two vorticity entries against the same input and compare all four curl channels.
const Curl = MakeBuffer(new Float32Array(Cells * 4), Usage.STORAGE);
const VorticityLayout = Device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
    { binding: 1, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: "float", viewDimension: "3d" } },
    { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } },
    { binding: 3, visibility: GPUShaderStage.COMPUTE, storageTexture: { access: "write-only", format: "rgba16float", viewDimension: "3d" } }
] });
const VorticityGroup = Device.createBindGroup({ layout: VorticityLayout, entries: [
    { binding: 0, resource: { buffer: Uniform } }, { binding: 1, resource: Velocity.createView({ dimension: "3d" }) },
    { binding: 2, resource: { buffer: Curl } }, { binding: 3, resource: Target.createView({ dimension: "3d" }) }
] });
const VorticitySource = fs.readFileSync("Experimental/VehicleHmiPrototype/Shader/PlumeRecords.wgsl", "utf8") + "\n" +
                        fs.readFileSync("Experimental/VehicleHmiPrototype/Shader/PlumeVorticity.wgsl", "utf8");
const VorticityModule = Device.createShaderModule({ code: VorticitySource });
const VorticityMessages = await VorticityModule.getCompilationInfo();
assert.deepEqual(VorticityMessages.messages.filter(Message => Message.type === "error").map(Message => Message.message), []);
const VorticityPipelineLayout = Device.createPipelineLayout({ bindGroupLayouts: [VorticityLayout] });
const VorticityPipelines = ["ResolveCurl", "ConfineVorticity"].map(entryPoint => Device.createComputePipeline({
    layout: VorticityPipelineLayout, compute: { module: VorticityModule, entryPoint }
}));
const VorticityEncoder = Device.createCommandEncoder();
for (const Pipeline of VorticityPipelines)
{
    const Pass = VorticityEncoder.beginComputePass();
    Pass.setPipeline(Pipeline); Pass.setBindGroup(0, VorticityGroup);
    Pass.dispatchWorkgroups(...Extent.map(Reading => Math.ceil(Reading / 4))); Pass.end();
}
const VorticityReadback = Device.createBuffer({ size: RowBytes * Extent[1] * Extent[2], usage: Usage.COPY_DST | Usage.MAP_READ });
VorticityEncoder.copyTextureToBuffer({ texture: Target },
    { buffer: VorticityReadback, bytesPerRow: RowBytes, rowsPerImage: Extent[1] }, Extent);
Device.queue.submit([VorticityEncoder.finish()]);
Expected.curl = Array.from(new Float32Array(await ReadBuffer(Device, Curl)));
await VorticityReadback.mapAsync(GPUMapMode.READ);
const VorticityOutput = new DataView(VorticityReadback.getMappedRange());
Expected.confined = [];
for (let Z = 0; Z < Extent[2]; ++Z)
    for (let Y = 0; Y < Extent[1]; ++Y)
        for (let X = 0; X < Extent[0]; ++X)
            for (let Channel = 0; Channel < 4; ++Channel)
                Expected.confined.push(HalfToFloat(VorticityOutput.getUint16(
                    (Z * Extent[1] + Y) * RowBytes + X * 8 + Channel * 2, true)));
VorticityReadback.unmap();
assert.deepEqual(Errors, [], "WebGPU must execute without validation errors");
for (const Field of Object.keys(Expected))
{
    assert.equal(Actual[Field].length, Expected[Field].length);
    let MaxDifference = 0;
    for (let Index = 0; Index < Expected[Field].length; ++Index)
    {
        const Difference = Math.abs(Actual[Field][Index] - Expected[Field][Index]);
        assert.ok(Number.isFinite(Difference) && Difference <= 0.00001,
                  `${Field}[${Index}]: native ${Actual[Field][Index]}, WGSL ${Expected[Field][Index]}`);
        MaxDifference = Math.max(MaxDifference, Difference);
    }
    console.log(`${Field}: ${Expected[Field].length} values, max absolute difference ${MaxDifference}`);
}
// Check that the fixture really exercises the interesting paths, rather than merely agreeing on zeros.
assert.ok(Actual.divergence.some(Value => Value !== 0));
assert.ok(Actual.pressureA.filter(Value => Value === 0).length > Fixture.pressureA.filter(Value => Value === 0).length,
          "newly exposed cells must clear warm-start pressure");
let Solids = 0, Fastest = 0, Confined = 0;
for (let Index = 0; Index < Fixture.velocity.length; Index += 4)
{
    if (Fixture.velocity[Index + 3] > 0.5)
    {
        ++Solids;
        assert.deepEqual(Actual.velocity.slice(Index, Index + 4), Fixture.velocity.slice(Index, Index + 4),
                         "solid cells retain their prescribed velocity and mask");
        assert.deepEqual(Actual.confined.slice(Index, Index + 4), Fixture.velocity.slice(Index, Index + 4),
                         "vorticity must not move solid cells");
    }
    else
    {
        Fastest = Math.max(Fastest, Math.hypot(...Actual.velocity.slice(Index, Index + 3)));
        if (Actual.confined.slice(Index, Index + 3).some((Reading, Channel) => Reading !== Fixture.velocity[Index + Channel])) ++Confined;
    }
}
assert.ok(Solids > 0 && Fastest >= 39.9 && Fastest <= 40.02, `speed clamp was not exercised: ${Fastest}`);
assert.ok(Confined > 0 && Actual.curl.some(Reading => Reading !== 0), "vorticity confinement must be exercised");
console.log(`Real Vulkan/Dawn parity passed on ${Actual.device}: moved/negative origin, prior-window clearing, ${Solids} solids, boundaries, 40 m/s limit and ${Confined} confined cells.`);
Device.destroy();
fs.rmSync(Scratch, { recursive: true, force: true });
