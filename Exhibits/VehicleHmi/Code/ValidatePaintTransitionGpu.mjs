// Execute the actual material-ripple WGSL on a Vulkan device at the origin, feather band and far side.
import fs from "node:fs";
import assert from "node:assert/strict";
import { create, globals } from "../../../Experimental/VehicleHmiPrototype/Tools/node_modules/webgpu/index.js";
import { EncodeMaterialRecords } from "../../../Experimental/VehicleHmiPrototype/Source/MaterialSpecification.js";
import { MaterialTransition } from "../../../Experimental/VehicleHmiPrototype/Source/Effects/MaterialTransition.js";
import { ReadBuffer } from "../../../Experimental/VehicleHmiPrototype/Tools/GpuProbe.mjs";

Object.assign(globalThis, globals);
const Gpu = create(["backend=vulkan"]);
const Adapter = await Gpu.requestAdapter();
assert.ok(Adapter, "an executing Vulkan adapter is required");
const Device = await Adapter.requestDevice();
const Errors = [];
Device.addEventListener("uncapturederror", Event => Errors.push(Event.error.message));
const SceneSource = fs.readFileSync("Experimental/VehicleHmiPrototype/Shader/SceneRecords.wgsl", "utf8");
const ReflectanceSource = fs.readFileSync("Experimental/VehicleHmiPrototype/Shader/SurfaceReflectance.wgsl", "utf8");
const MaterialStruct = SceneSource.slice(SceneSource.indexOf("struct MaterialRecord"), SceneSource.indexOf("\n}\n", SceneSource.indexOf("struct MaterialRecord")) + 3);
const Reflectance = ReflectanceSource.slice(ReflectanceSource.indexOf("struct SurfaceSample"), ReflectanceSource.indexOf("fn ResolveDiffuseAlbedo"));
assert.ok(MaterialStruct.startsWith("struct MaterialRecord") && Reflectance.includes("ProjectRippledSurfaceSample"));
const Program = `${MaterialStruct}\n${Reflectance}
@group(0) @binding(0) var<storage, read> Materials: array<MaterialRecord>;
@group(0) @binding(1) var<storage, read_write> Results: array<vec4f>;
@compute @workgroup_size(1)
fn Inspect(@builtin(global_invocation_id) Id: vec3u)
{
    let Point = select(select(2.5, 1.725, Id.x == 2u), 0.0, Id.x == 0u);
    var State = Materials[2];
    if (Id.x == 3u) { State = Materials[4]; }
    let Surface = ProjectRippledSurfaceSample(Materials[0], Materials[1], State, Materials[3], vec3f(Point, 0.0, 0.0));
    Results[2u * Id.x] = vec4f(Surface.Albedo, Surface.Roughness);
    Results[2u * Id.x + 1u] = vec4f(Surface.Metalness, Surface.CoatWeight, Surface.CoatRoughness, 0.0);
}`;
const Old = { Albedo: [0.1, 0.2, 0.3], Roughness: 0.26, Emission: [0, 0, 0], Metalness: 0.9,
              CoatWeight: 1, CoatRoughness: 0.12, SpecularLevel: 0.04, Transmission: 0, IndexOfRefraction: 0 };
const Next = { ...Old, Albedo: [0.8, 0.4, 0.2], Roughness: 0.72, Metalness: 0.35, CoatWeight: 0.1, CoatRoughness: 0.65 };
const [, InitialState, Identity] = MaterialTransition.ConstructInitialRecords(Old);
const Active = { ...InitialState, Albedo: [0, 0, 0], Roughness: 2, Metalness: 1, CoatWeight: 0.55 };
const Idle = { ...Active, Metalness: 0 };
const Encoded = EncodeMaterialRecords([Next, Old, Active, Identity, Idle]);
const Materials = Device.createBuffer({ size: Encoded.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
const Results = Device.createBuffer({ size: 8 * 16, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
Device.queue.writeBuffer(Materials, 0, Encoded);
const Module = Device.createShaderModule({ code: Program });
const Diagnostics = await Module.getCompilationInfo();
assert.deepEqual(Diagnostics.messages.filter(Message => Message.type === "error").map(Message => Message.message), []);
const Pipeline = Device.createComputePipeline({ layout: "auto", compute: { module: Module, entryPoint: "Inspect" } });
const Group = Device.createBindGroup({ layout: Pipeline.getBindGroupLayout(0), entries: [
    { binding: 0, resource: { buffer: Materials } }, { binding: 1, resource: { buffer: Results } }
] });
const Encoder = Device.createCommandEncoder(), Pass = Encoder.beginComputePass();
Pass.setPipeline(Pipeline); Pass.setBindGroup(0, Group); Pass.dispatchWorkgroups(4); Pass.end();
Device.queue.submit([Encoder.finish()]);
const Readings = Array.from(new Float32Array(await ReadBuffer(Device, Results)));
const Expected = [
    [0.8, 0.4, 0.2, 0.72, 0.35, 1, 0.12, 0],
    [0.1, 0.2, 0.3, 0.26, 0.9, 1, 0.12, 0],
    [0.45, 0.3, 0.25, 0.49, 0.625, 1, 0.12, 0],
    [0.8, 0.4, 0.2, 0.72, 0.35, 0.1, 0.65, 0],
].flat();
for (let Index = 0; Index < Expected.length; ++Index)
    assert.ok(Math.abs(Readings[Index] - Expected[Index]) < 0.00001,
              `reading ${Index}: ${Readings[Index]} != ${Expected[Index]}`);
assert.deepEqual(Errors, []);
console.log("Executed 4 paint samples: front, far side, feather midpoint and completed transition match the reference material rules.");
Device.destroy();
