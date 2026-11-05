// Execute the prototype's actual particle WGSL on Vulkan, including its vertex, fragment and alpha blending.
import assert from "node:assert/strict";
import fs from "node:fs";
import { create, globals } from "../../../Experimental/VehicleHmiPrototype/Tools/node_modules/webgpu/index.js";
import { ReadBuffer, ReadTexture } from "../../../Experimental/VehicleHmiPrototype/Tools/GpuProbe.mjs";
import { DefaultSparkleColour } from "../../../Experimental/VehicleHmiPrototype/Source/Effects/ScanSparkles.js";

Object.assign(globalThis, globals);
const Gpu = create(["backend=vulkan"]);
const Adapter = await Gpu.requestAdapter();
assert.ok(Adapter, "an executing Vulkan adapter is required");
const Device = await Adapter.requestDevice();
const Errors = [];
Device.addEventListener("uncapturederror", Event => Errors.push(Event.error.message));
const Source = fs.readFileSync("Experimental/VehicleHmiPrototype/Shader/VehicleScan.wgsl", "utf8");
function Struct(Name)
{
    const Start = Source.indexOf(`struct ${Name}\n`), End = Source.indexOf("\n}\n", Start);
    assert.ok(Start >= 0 && End > Start, `${Name} not found`);
    return Source.slice(Start, End + 3);
}
const ParticleCode = Source.slice(Source.indexOf("fn ProjectSparkleWorld("));
assert.ok(ParticleCode.startsWith("fn ProjectSparkleWorld(") && ParticleCode.includes("fn SparkleQuadFragment"));
const Code = `
struct FrameRecord { ViewProjection: mat4x4f, View: mat4x4f, InverseScreenExtent: vec2f, Padding: vec2f }
struct OccupantRecord { LocalToWorld: mat4x4f }
${Struct("ScanEffectRecord")}
${Struct("ScanSparkleRecord")}
@group(0) @binding(0) var<uniform> Frame: FrameRecord;
@group(0) @binding(1) var<storage, read> Occupants: array<OccupantRecord>;
@group(0) @binding(4) var<uniform> ScanEffect: ScanEffectRecord;
@group(0) @binding(6) var<storage, read> Sparkles: array<ScanSparkleRecord>;
${ParticleCode}
struct ProbeInput { PositionTime: vec4f, NoiseSpeed: vec4f, SizeRatioDepth: vec4f, Corner: vec4f }
@group(1) @binding(0) var<storage, read> Inputs: array<ProbeInput>;
@group(1) @binding(1) var<storage, read_write> Results: array<vec4f>;
@compute @workgroup_size(1)
fn Inspect(@builtin(global_invocation_id) Id: vec3u)
{
    let Input = Inputs[Id.x];
    Results[2u * Id.x] = vec4f(ProjectSparkleWorld(Input.PositionTime.xyz, Input.PositionTime.w,
                                                   Input.NoiseSpeed.w, Input.NoiseSpeed.xyz), 0.0);
    Results[2u * Id.x + 1u] = vec4f(ResolveSparklePixelSize(Input.SizeRatioDepth.x, Input.SizeRatioDepth.y, Input.SizeRatioDepth.z),
                                     ResolveSparkleAlpha(Input.Corner.xy, 0.55), 0.0, 0.0);
}`;
const Module = Device.createShaderModule({ code: Code });
const Diagnostics = await Module.getCompilationInfo();
assert.deepEqual(Diagnostics.messages.filter(Message => Message.type === "error").map(Message => Message.message), []);
const Compute = Device.createComputePipeline({ layout: "auto", compute: { module: Module, entryPoint: "Inspect" } });
const Cases = [
    [0, 0, -5, 0, 1, 1, 1, 0.35, 2.2, 1, 5, 0, 0, 0, 0, 0],
    [0.23, 1.1, -4, 1.7, 1, 1, 1, 0.35, 2.2, 2, 4, 0, 0.4, 0.2, 0, 0],
    [-1.8, -0.5, -8, 7.2, 0.3, 0.7, 1.4, 0.35, 2.2, 1.5, 8, 0, 0.8, 0, 0, 0],
    [3.2, 1.7, -11, 33, 1, 1, 1, 0.35, 2.2, 1, 11, 0, 1, 1, 0, 0],
    [0.1, 0.2, -90, 4, 1, 1, 1, 0.35, 2.2, 1, 90, 0, 0.02, 0, 0, 0],
];
const Encoded = new Float32Array(Cases.flat());
const InputBuffer = Device.createBuffer({ size: Encoded.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
const ResultBuffer = Device.createBuffer({ size: Cases.length * 32, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
Device.queue.writeBuffer(InputBuffer, 0, Encoded);
const ProbeGroup = Device.createBindGroup({ layout: Compute.getBindGroupLayout(1), entries: [
    { binding: 0, resource: { buffer: InputBuffer } }, { binding: 1, resource: { buffer: ResultBuffer } }
] });
let Encoder = Device.createCommandEncoder();
let Pass = Encoder.beginComputePass();
Pass.setPipeline(Compute); Pass.setBindGroup(1, ProbeGroup); Pass.dispatchWorkgroups(Cases.length); Pass.end();
Device.queue.submit([Encoder.finish()]);
const Readings = new Float32Array(await ReadBuffer(Device, ResultBuffer));
const Clamp = (V, Lo, Hi) => Math.max(Lo, Math.min(Hi, V));
for (let Index = 0; Index < Cases.length; ++Index)
{
    const [X, Y, Z, Time, Nx, Ny, Nz, Speed, Size, Ratio, Depth, , Cx, Cy] = Cases[Index];
    const T = Time * Speed;
    const Expected = [X + Math.cos(T + X * Nz * 100) * 0.2,
                      Y - Math.cos(T + X * Ny * 100) * 0.2,
                      Z + Math.sin(T + X * Nx * 100) * 0.2, 0,
                      Math.max(1, Size * 25 * Ratio / Depth),
                      Clamp((0.05 / Math.max(Math.hypot(Cx, Cy) * 0.5, 1e-6) - 0.1) * 0.55, 0, 1), 0, 0];
    for (let Component = 0; Component < Expected.length; ++Component)
        assert.ok(Math.abs(Readings[Index * 8 + Component] - Expected[Component]) < 0.00005,
                  `probe ${Index}/${Component}: ${Readings[Index * 8 + Component]} != ${Expected[Component]}`);
}

const Identity = new Float32Array([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]);
// This focal length aligns the projected centre exactly to a 1/16-pixel raster subpixel.
const Projection = new Float32Array([0.9765625,0,0,0, 0,0.9765625,0,0, 0,0,0,-1, 0,0,0.1,0]);
const Camera = new Float32Array(36);
Camera.set(Projection, 0); Camera.set(Identity, 16); Camera.set([1/64, 1/64], 32);
const Uniform = new Float32Array(28);
Uniform[7] = 0.55; Uniform.set(DefaultSparkleColour, 16); Uniform[24] = 1;
const Particle = new Float32Array([0, 0, -5, 2.2, 1, 1, 1, 0.35]);
function Upload(Scalars, Usage)
{
    const Buffer = Device.createBuffer({ size: Scalars.byteLength, usage: Usage | GPUBufferUsage.COPY_DST });
    Device.queue.writeBuffer(Buffer, 0, Scalars);
    return Buffer;
}
const FrameBuffer = Upload(Camera, GPUBufferUsage.UNIFORM);
const ScanBuffer = Upload(Uniform, GPUBufferUsage.UNIFORM);
const OccupantBuffer = Upload(Identity, GPUBufferUsage.STORAGE);
const ParticleBuffer = Upload(Particle, GPUBufferUsage.STORAGE);
const Blend = { color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
                alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" } };
const Raster = Device.createRenderPipeline({ layout: "auto", vertex: { module: Module, entryPoint: "SparkleQuadVertex" },
    fragment: { module: Module, entryPoint: "SparkleQuadFragment", targets: [{ format: "rgba16float", blend: Blend }] },
    primitive: { topology: "triangle-list", cullMode: "none" },
    depthStencil: { format: "depth32float", depthWriteEnabled: false, depthCompare: "greater-equal" } });
const RasterGroup = Device.createBindGroup({ layout: Raster.getBindGroupLayout(0), entries: [
    { binding: 0, resource: { buffer: FrameBuffer } }, { binding: 1, resource: { buffer: OccupantBuffer } },
    { binding: 4, resource: { buffer: ScanBuffer } }, { binding: 6, resource: { buffer: ParticleBuffer } }
] });
const Target = Device.createTexture({ size: [64, 64], format: "rgba16float", usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
const DepthTarget = Device.createTexture({ size: [64, 64], format: "depth32float", usage: GPUTextureUsage.RENDER_ATTACHMENT });
const Background = [0.2, 0.3, 0.4];
async function Draw(Ratio, ClearDepth)
{
    Uniform[24] = Ratio; Device.queue.writeBuffer(ScanBuffer, 0, Uniform);
    Encoder = Device.createCommandEncoder();
    Pass = Encoder.beginRenderPass({
        colorAttachments: [{ view: Target.createView(), clearValue: [...Background, 1], loadOp: "clear", storeOp: "store" }],
        depthStencilAttachment: { view: DepthTarget.createView(), depthClearValue: ClearDepth, depthLoadOp: "clear", depthStoreOp: "store" }
    });
    Pass.setPipeline(Raster); Pass.setBindGroup(0, RasterGroup); Pass.draw(6, 1); Pass.end();
    Device.queue.submit([Encoder.finish()]);
    return ReadTexture(Device, Target, 64, 64, "rgba16float");
}
const Pixel = (Pixels, X, Y) => Array.from(Pixels.slice((Y * 64 + X) * 4, (Y * 64 + X) * 4 + 3));
function ComparePixel(Pixels, X, Y, Ratio, Visible)
{
    const Center = 33.25; // reference motion sends (0,0,-5) to (0.2,-0.2,-5)
    const PointSize = 11 * Ratio;
    const Distance = Math.hypot(X + 0.5 - Center, Y + 0.5 - Center) / PointSize;
    const InSquare = Math.abs(X + 0.5 - Center) <= PointSize / 2 && Math.abs(Y + 0.5 - Center) <= PointSize / 2;
    const Alpha = Visible && InSquare ? Clamp((0.05 / Math.max(Distance, 1e-6) - 0.1) * 0.55, 0, 1) : 0;
    const Actual = Pixel(Pixels, X, Y);
    for (let C = 0; C < 3; ++C)
    {
        const Expected = DefaultSparkleColour[C] * Alpha + Background[C] * (1 - Alpha);
        assert.ok(Math.abs(Actual[C] - Expected) < 0.002, `pixel ${X},${Y}/${C}: ${Actual[C]} != ${Expected}`);
    }
}
const Visible = await Draw(1, 0);
for (const [X, Y] of [[33,33], [35,33], [37,33], [38,33], [39,33], [33,38]]) ComparePixel(Visible, X, Y, 1, true);
const DoubleRatio = await Draw(2, 0);
for (const [X, Y] of [[33,33], [37,33], [40,33], [43,33], [45,33]]) ComparePixel(DoubleRatio, X, Y, 2, true);
const Occluded = await Draw(1, 0.05); // point depth 0.02, in reversed depth
ComparePixel(Occluded, 33, 33, 1, false);
assert.deepEqual(Errors, []);
console.log("Executed particle WGSL: 5 motion/size/alpha probes and 12 raster samples matched; depth occlusion passed.");
Device.destroy();
