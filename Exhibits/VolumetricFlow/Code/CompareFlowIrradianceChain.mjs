// Run the C++-planned seed plus flood chain on Vulkan and compare both carriers against the retained WGSL.
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { create, globals } from "../../../Experimental/VehicleHmiPrototype/Tools/node_modules/webgpu/index.js";
Object.assign(globalThis, globals);
const [Executable, IrradianceDirectory, ProjectionDirectory] = process.argv.slice(2);
assert.ok(Executable && IrradianceDirectory && ProjectionDirectory, "Expected planner executable and irradiance/projection shader directories");
const Html = fs.readFileSync("Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html", "utf8");
const Header = "const IrradianceShaderSource = /* wgsl */ `";
const Start = Html.indexOf(Header) + Header.length, End = Html.indexOf("`", Start);
assert.ok(Start >= Header.length && End > Start);
const Gpu = create([]), Adapter = await Gpu.requestAdapter();
assert.ok(Adapter);
const Device = await Adapter.requestDevice();
const Module = Device.createShaderModule({ code: Html.slice(Start, End) });
assert.deepEqual((await Module.getCompilationInfo()).messages.filter(Message => Message.type === "error"), []);
const Layout = Device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
    ...[1,2].map(binding => ({ binding, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: "float", viewDimension: "3d" } })),
    { binding: 3, visibility: GPUShaderStage.COMPUTE, sampler: { type: "filtering" } },
    { binding: 4, visibility: GPUShaderStage.COMPUTE, storageTexture: { access: "write-only", format: "rgba16float", viewDimension: "3d" } }
] });
const PipelineLayout = Device.createPipelineLayout({ bindGroupLayouts: [Layout] });
const Pipelines = Object.fromEntries(["SeedIrradiance", "PropagateIrradiance"].map(Name => [Name,
    Device.createComputePipeline({ layout: PipelineLayout, compute: { module: Module, entryPoint: Name } })]));
const Sampler = Device.createSampler({ minFilter: "linear", magFilter: "linear", addressModeU: "clamp-to-edge",
                                       addressModeV: "clamp-to-edge", addressModeW: "clamp-to-edge" });
function Half(Value)
{
    const Buffer = new ArrayBuffer(4), Float = new Float32Array(Buffer), Bits = new Uint32Array(Buffer);
    Float[0] = Value;
    const Sign = (Bits[0] >>> 16) & 0x8000, Exponent = ((Bits[0] >>> 23) & 0xff) - 127 + 15, Mantissa = Bits[0] & 0x7fffff;
    if (Exponent <= 0) return Sign;
    if (Exponent >= 31) return Sign | 0x7c00;
    return Sign | (Exponent << 10) | (Mantissa >>> 13);
}
function Unhalf(Value)
{
    const Sign = Value & 0x8000 ? -1 : 1, Exponent = (Value >>> 10) & 31, Mantissa = Value & 1023;
    return Exponent === 0 ? Sign * 2 ** -14 * (Mantissa / 1024) :
           Exponent === 31 ? (Mantissa ? NaN : Sign * Infinity) : Sign * 2 ** (Exponent - 15) * (1 + Mantissa / 1024);
}
function Upload(Texture, Extent, Values)
{
    const RowBytes = Math.ceil(Extent * 8 / 256) * 256, Bytes = new Uint16Array(RowBytes * Extent * Extent / 2);
    for (let Z = 0; Z < Extent; ++Z) for (let Y = 0; Y < Extent; ++Y) for (let X = 0; X < Extent; ++X)
        for (let C = 0; C < 4; ++C) Bytes[(Z * Extent + Y) * RowBytes / 2 + X * 4 + C] = Half(Values[((Z * Extent + Y) * Extent + X) * 4 + C]);
    Device.queue.writeTexture({ texture: Texture }, Bytes, { bytesPerRow: RowBytes, rowsPerImage: Extent }, [Extent,Extent,Extent]);
}
const Cases = [ { cache: 5, scalar: 8, passes: 0, read: 1 }, { cache: 5, scalar: 8, passes: 4, read: 0 },
                { cache: 8, scalar: 16, passes: 4, read: 1 }, { cache: 7, scalar: 11, passes: 2, read: 0 } ];
let Compared = 0, Maximum = 0;
const Temp = fs.mkdtempSync(path.join(os.tmpdir(), "flow-irradiance-chain-"));
try
{
    for (const Case of Cases)
    {
        const NativePlan = spawnSync(Executable, [], { encoding: "utf8", input: `${Case.cache} ${Case.passes} 0 ${Case.read} 0\n` });
        assert.equal(NativePlan.status, 0, NativePlan.stderr);
        const Plan = JSON.parse(NativePlan.stdout);
        assert.equal(Plan.dispatches.length, Case.passes + 1);
        const Scalar = Array.from({ length: Case.scalar ** 3 }, (_, I) => [0.125 + (I % 9) * 0.125,
            0.125 + (I * 3 % 13) * 0.125, 0.125 + (I * 7 % 9) * 0.125, (I % 5) * 0.125]).flat();
        const CacheData = Slot => Array.from({ length: Case.cache ** 3 }, (_, I) => [0.25 + (I % 7) * 0.125 + Slot * 0.25,
            0.125 + (I * 3 % 9) * 0.125, 0.25 + (I * 5 % 11) * 0.125, 1]).flat();
        const Volumes = { S0: { extent: Case.scalar, data: Scalar }, S1: { extent: Case.scalar, data: Scalar.map(() => -2) },
                          V0: { extent: Case.cache, data: CacheData(0) }, V1: { extent: Case.cache, data: CacheData(1) } };
        // Reuse the generic Vulkan transport runner: its sixth storage binding is an unused, disjoint dummy for these five-binding shaders.
        const Fixture = { volumes: Volumes, plan: { uniform: Plan.uniform, levels: [], stepDispatches: Plan.dispatches.map(D =>
            [D[0], "transport", 0, D.slice(1,4), [`S${D[4]}`, `V${D[5]}`, `V${D[6]}`, "S1"]]) } };
        const FixturePath = path.join(Temp, "fixture.json"), OutputPath = path.join(Temp, "output.json");
        fs.writeFileSync(FixturePath, JSON.stringify(Fixture));
        const Run = spawnSync("python3", ["Exhibits/VolumetricFlow/Code/RunFlowStepVulkan.py", FixturePath,
            ProjectionDirectory, IrradianceDirectory, OutputPath], { encoding: "utf8", maxBuffer: 1024 * 1024 });
        assert.equal(Run.status, 0, Run.stderr);
        const Native = JSON.parse(fs.readFileSync(OutputPath, "utf8")).volumes;
        const Textures = Object.fromEntries(Object.entries(Volumes).map(([Name, Volume]) => {
            const Texture = Device.createTexture({ size: [Volume.extent, Volume.extent, Volume.extent], dimension: "3d", format: "rgba16float",
                usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_DST | GPUTextureUsage.COPY_SRC });
            Upload(Texture, Volume.extent, Volume.data); return [Name, Texture]; }));
        const Uniform = Device.createBuffer({ size: 80, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        Device.queue.writeBuffer(Uniform, 0, new Float32Array(Plan.uniform));
        const Encoder = Device.createCommandEncoder();
        for (const [Name, X,Y,Z, ScalarSlot, CacheRead, CacheWrite] of Plan.dispatches)
        {
            assert.notEqual(CacheRead, CacheWrite);
            const Group = Device.createBindGroup({ layout: Layout, entries: [
                { binding: 0, resource: { buffer: Uniform } }, { binding: 1, resource: Textures[`S${ScalarSlot}`].createView() },
                { binding: 2, resource: Textures[`V${CacheRead}`].createView() }, { binding: 3, resource: Sampler },
                { binding: 4, resource: Textures[`V${CacheWrite}`].createView() } ] });
            const Pass = Encoder.beginComputePass(); Pass.setPipeline(Pipelines[Name]); Pass.setBindGroup(0, Group);
            Pass.dispatchWorkgroups(X,Y,Z); Pass.end();
        }
        const Readbacks = Object.fromEntries(Object.entries(Volumes).map(([Name, Volume]) => {
            const Row = Math.ceil(Volume.extent * 8 / 256) * 256;
            const Buffer = Device.createBuffer({ size: Row * Volume.extent ** 2, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
            Encoder.copyTextureToBuffer({ texture: Textures[Name] }, { buffer: Buffer, bytesPerRow: Row, rowsPerImage: Volume.extent },
                                        [Volume.extent, Volume.extent, Volume.extent]); return [Name, { Buffer, Row }]; }));
        Device.queue.submit([Encoder.finish()]);
        let Changed = 0;
        for (const [Name, Volume] of Object.entries(Volumes))
        {
            const { Buffer, Row } = Readbacks[Name]; await Buffer.mapAsync(GPUMapMode.READ);
            const Data = new Uint16Array(Buffer.getMappedRange());
            for (let Z = 0; Z < Volume.extent; ++Z) for (let Y = 0; Y < Volume.extent; ++Y)
                for (let X = 0; X < Volume.extent; ++X) for (let C = 0; C < 4; ++C)
                {
                    const Index = ((Z * Volume.extent + Y) * Volume.extent + X) * 4 + C;
                    const Expected = Unhalf(Data[(Z * Volume.extent + Y) * Row / 2 + X * 4 + C]), Actual = Native[Name][Index];
                    assert.ok(Number.isFinite(Expected) && Number.isFinite(Actual));
                    const Delta = Math.abs(Expected - Actual), Tolerance = 0.001 + Math.abs(Expected) * 0.002;
                    assert.ok(Delta <= Tolerance, `${Name}[${Index}], cache ${Case.cache}: ${Expected} vs ${Actual}`);
                    if (Name === `V${Plan.finalSlot}` && Expected !== Volume.data[Index]) ++Changed;
                    Maximum = Math.max(Maximum, Delta); ++Compared;
                }
            Buffer.unmap(); Buffer.destroy(); Textures[Name].destroy();
        }
        assert.ok(Changed > 0, "Final cache was not updated");
        Uniform.destroy();
        console.log(`Irradiance chain: ${Case.scalar}³ scalar → ${Case.cache}³ cache, ${Plan.dispatches.length} passes, final slot ${Plan.finalSlot}`);
    }
    console.log(`Irradiance chain parity: ${Compared} scalars, maximum |Δ| ${Maximum}.`);
}
finally { fs.rmSync(Temp, { recursive: true, force: true }); Device.destroy(); }
