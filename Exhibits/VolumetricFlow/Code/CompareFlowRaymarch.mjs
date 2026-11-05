// Rasterize retained WGSL and translated Slang raymarch stages and compare every half-float output component.
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import vm from "node:vm";
import { spawnSync } from "node:child_process";
import { create, globals } from "../../../Experimental/VehicleHmiPrototype/Tools/node_modules/webgpu/index.js";
import { HalfToFloat } from "../../../Experimental/VehicleHmiPrototype/Tools/GpuProbe.mjs";
Object.assign(globalThis, globals);
const [Directory, NativeView] = process.argv.slice(2);
assert.ok(Directory && NativeView, "Expected the compiled raymarch directory and native view validation executable");
const Html = fs.readFileSync("Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html", "utf8");
function Slice(Start, End)
{
    const From = Html.indexOf(Start), To = Html.indexOf(End, From);
    assert.ok(From >= 0 && To > From, `Reference section not found: ${Start}`);
    return Html.slice(From, To);
}
const Header = "const RaymarchShaderSource = /* wgsl */ `";
const Start = Html.indexOf(Header), End = Html.indexOf("`;", Start + Header.length);
assert.ok(Start >= 0 && End > Start);
const Shader = Html.slice(Start + Header.length, End);
const ViewReference = Slice("const OrbitCamera =", "const ViewByteCount =") +
    Slice("function TranscribeViewParameterization()", "async function ConstructIrradianceUnit()");
const Gpu = create([]), Adapter = await Gpu.requestAdapter();
assert.ok(Adapter);
const Device = await Adapter.requestDevice();
const Module = Device.createShaderModule({ code: Shader });
assert.deepEqual((await Module.getCompilationInfo()).messages.filter(Message => Message.type === "error"), []);
const Layout = Device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
    ...[1,2,5].map(binding => ({ binding, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float", viewDimension: "3d" } })),
    { binding: 3, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } },
    { binding: 4, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float", viewDimension: "2d" } }
] });
const Pipeline = Device.createRenderPipeline({ layout: Device.createPipelineLayout({ bindGroupLayouts: [Layout] }),
    vertex: { module: Module, entryPoint: "ProjectFullscreenCorner" },
    fragment: { module: Module, entryPoint: "IntegrateVolumeRadiance", targets: [{ format: "rgba16float" }] },
    primitive: { topology: "triangle-strip", cullMode: "none" } });
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
function Upload(Texture, Volume)
{
    const [Width, Height, Depth] = Volume.extent, Row = Math.ceil(Width * 8 / 256) * 256;
    const Data = new Uint16Array(Row * Height * Depth / 2);
    for (let Z = 0; Z < Depth; ++Z) for (let Y = 0; Y < Height; ++Y) for (let X = 0; X < Width; ++X)
        for (let C = 0; C < 4; ++C) Data[(Z * Height + Y) * Row / 2 + X * 4 + C] = Half(Volume.data[((Z * Height + Y) * Width + X) * 4 + C]);
    Device.queue.writeTexture({ texture: Texture }, Data, { bytesPerRow: Row, rowsPerImage: Height }, [Width,Height,Depth]);
}
function ViewUniform(Case, Width, Height)
{
    const Context = vm.createContext({ Case, Width, Height });
    vm.runInContext(`${ViewReference}\n
        const ViewStaging = new Float32Array(52), ShadowDirection = [-0.42, 0.80, 0.43];
        const SurfaceExtent = { PixelWidth: Width, PixelHeight: Height }, SolverAssembly = { ElapsedInterval: 1.75 };
        const RenderAssembly = { ViewBuffer: {} };
        const DeviceProfile = { Device: { queue: { writeBuffer() {} } } };
        ViewParameterization.MarchStepCount = 10;
        ViewParameterization.ShadowStepCount = 2;
        ViewParameterization.DensityScale = 4.5;
        ViewParameterization.TurbulenceStrength = Case.turbulence;
        ViewParameterization.TurbulenceDetail = 8;
        ViewParameterization.ChannelSelector = Case.selector;
        ViewParameterization.PaletteBlend = Case.palette;
        TranscribeViewParameterization();
        globalThis.Result = Array.from(ViewStaging);
    `, Context);
    return Array.from(Context.Result);
}
const Cases = [
    { name: "scattering and emission", turbulence: 0, selector: 0, palette: 0.85, clear: false },
    { name: "turbulence", turbulence: 0.65, selector: 0, palette: 0.85, clear: false },
    { name: "velocity channel", turbulence: 0, selector: 1, palette: 0.85, clear: false },
    { name: "empty medium", turbulence: 0, selector: 0, palette: 0, clear: true }
];
const Size = [24, 18], Extent = 8, Cache = 5;
const Temp = fs.mkdtempSync(path.join(os.tmpdir(), "flow-raymarch-"));
let Compared = 0, Maximum = 0;
const Results = [];
try
{
    for (const Case of Cases)
    {
        const Scalar = Array.from({ length: Extent ** 3 }, (_, I) => Case.clear ? [0,0,0,0] :
            [0.0625 + (I % 5) * 0.0625, 0.25 + (I * 3 % 9) * 0.0625,
             0.0625 + (I * 7 % 5) * 0.0625, (I % 4) * 0.0625]).flat();
        const Velocity = Array.from({ length: Extent ** 3 }, (_, I) =>
            [((I % 8) - 4) * 0.0625, ((Math.floor(I / 8) % 8) - 4) * 0.0625,
             ((Math.floor(I / 64) % 8) - 4) * 0.0625, 0]).flat();
        const Irradiance = Array.from({ length: Cache ** 3 }, (_, I) =>
            [0.25 + (I % 5) * 0.125, 0.125 + (I % 4) * 0.125, 0.25 + (I % 3) * 0.125, 1]).flat();
        const Palette = Array.from({ length: 32 }, (_, I) => I < 16 ?
            [0.25 + (I % 16) * 0.125, 0.125 + (I % 16) * 0.0625, 0.0625 + (I % 16) * 0.0625, 1] :
            [0.75 - (I % 16) * 0.03125, 0.8125 - (I % 16) * 0.03125, 0.875 - (I % 16) * 0.03125, 1]).flat();
        const Textures = { scalar: { extent: [Extent,Extent,Extent], data: Scalar }, velocity: { extent: [Extent,Extent,Extent], data: Velocity },
                           palette: { extent: [16,2,1], data: Palette }, irradiance: { extent: [Cache,Cache,Cache], data: Irradiance } };
        const ReferenceUniform = ViewUniform(Case, ...Size);
        const NativeRun = spawnSync(NativeView, [], { encoding: "utf8", input: [Size[0], Size[1], 0.62, 0.24, 2.05, 0.62,
            1.75, Case.turbulence, 4.5, Case.palette, Case.selector, 10, 2, 8].join(" ") + "\n" });
        assert.equal(NativeRun.status, 0, NativeRun.stderr);
        const NativeUniform = JSON.parse(NativeRun.stdout);
        for (let Slot = 0; Slot < 52; ++Slot)
            assert.ok(Math.abs(NativeUniform[Slot] - ReferenceUniform[Slot]) <= 1e-6 * Math.max(1, Math.abs(ReferenceUniform[Slot])),
                      `${Case.name}, native view slot ${Slot}`);
        const Fixture = { width: Size[0], height: Size[1], uniform: NativeUniform, textures: Textures };
        const FixturePath = path.join(Temp, "fixture.json"), ResultPath = path.join(Temp, "output.json");
        fs.writeFileSync(FixturePath, JSON.stringify(Fixture));
        const Native = spawnSync("python3", ["Exhibits/VolumetricFlow/Code/RunFlowRaymarchVulkan.py", FixturePath, Directory, ResultPath],
                                 { encoding: "utf8", maxBuffer: 1024 * 1024 });
        assert.equal(Native.status, 0, Native.stderr);
        const Actual = JSON.parse(fs.readFileSync(ResultPath, "utf8")).pixels;
        const Frame = Device.createBuffer({ size: 208, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        Device.queue.writeBuffer(Frame, 0, new Float32Array(ReferenceUniform));
        const Images = Object.fromEntries(Object.entries(Textures).map(([Name, Volume]) => {
            const Texture = Device.createTexture({ size: Volume.extent, dimension: Name === "palette" ? "2d" : "3d", format: "rgba16float",
                usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
            Upload(Texture, Volume); return [Name, Texture]; }));
        const Output = Device.createTexture({ size: Size, format: "rgba16float", usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
        const Group = Device.createBindGroup({ layout: Layout, entries: [
            { binding: 0, resource: { buffer: Frame } }, { binding: 1, resource: Images.scalar.createView() },
            { binding: 2, resource: Images.velocity.createView() }, { binding: 3, resource: Sampler },
            { binding: 4, resource: Images.palette.createView() }, { binding: 5, resource: Images.irradiance.createView() } ] });
        const Row = Math.ceil(Size[0] * 8 / 256) * 256;
        const Readback = Device.createBuffer({ size: Row * Size[1], usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        const Encoder = Device.createCommandEncoder();
        const Pass = Encoder.beginRenderPass({ colorAttachments: [{ view: Output.createView(), loadOp: "clear", storeOp: "store",
                                                                   clearValue: { r: -2, g: -2, b: -2, a: -2 } }] });
        Pass.setPipeline(Pipeline); Pass.setBindGroup(0, Group); Pass.draw(4); Pass.end();
        Encoder.copyTextureToBuffer({ texture: Output }, { buffer: Readback, bytesPerRow: Row, rowsPerImage: Size[1] }, Size);
        Device.queue.submit([Encoder.finish()]); await Readback.mapAsync(GPUMapMode.READ);
        const Data = new Uint16Array(Readback.getMappedRange()), Reference = [];
        let Lit = 0, CaseMaximum = 0;
        for (let Y = 0; Y < Size[1]; ++Y) for (let X = 0; X < Size[0]; ++X) for (let C = 0; C < 4; ++C)
        {
            const Index = (Y * Size[0] + X) * 4 + C, Expected = HalfToFloat(Data[Y * Row / 2 + X * 4 + C]);
            assert.ok(Number.isFinite(Expected) && Number.isFinite(Actual[Index]), `${Case.name}, nonfinite ${Index}`);
            const Delta = Math.abs(Expected - Actual[Index]);
            assert.ok(Delta <= 0.004 + Math.abs(Expected) * 0.01,
                `${Case.name}, pixel (${X},${Y}) channel ${C}: WGSL ${Expected}, Slang ${Actual[Index]}`);
            if (C < 3 && Expected > 0.005) ++Lit;
            if (C === 3) assert.equal(Expected, 1, "fullscreen triangle strip did not cover the image");
            CaseMaximum = Math.max(CaseMaximum, Delta); Maximum = Math.max(Maximum, Delta); ++Compared;
            Reference.push(Expected);
        }
        if (!Case.clear) assert.ok(Lit > 25, `${Case.name} produced no visible signal`);
        Results.push(Reference);
        console.log(`${Case.name}: ${Size[0]}×${Size[1]}, ${Lit} lit components, max |Δ| ${CaseMaximum}`);
        Readback.unmap(); Readback.destroy(); Frame.destroy(); Output.destroy(); Object.values(Images).forEach(Image => Image.destroy());
    }
    for (const [Left, Right, Label] of [[0,1,"turbulence"], [0,2,"velocity inspection"], [0,3,"empty medium"]])
    {
        const Changed = Results[Left].filter((Value, Index) => Math.abs(Value - Results[Right][Index]) > 0.005).length;
        assert.ok(Changed > 10, `${Label} did not affect the output`);
    }
    console.log(`Raymarch graphics parity: ${Compared} components over four fixtures, maximum |Δ| ${Maximum}.`);
}
finally { fs.rmSync(Temp, { recursive: true, force: true }); Device.destroy(); }
