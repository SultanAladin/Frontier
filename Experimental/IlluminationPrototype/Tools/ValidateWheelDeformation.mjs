// Executes the real raster WGSL wheel function against its CPU BLAS counterpart, on Dawn Vulkan.
import { create, globals } from 'webgpu';
import fs from 'node:fs';
import assert from 'node:assert/strict';
import { VehicleSolver } from '../Source/VehicleSolver.js';
import { VehicleVisualState } from '../Source/VehicleVisualState.js';
import { ResolveWheelVertexCpu } from '../Source/WheelVertexDeformation.js';
Object.assign(globalThis,globals);
const Gpu=create(['backend='+(process.env.BACKEND||'vulkan')]);
const Adapter=await Gpu.requestAdapter(); const Device=await Adapter.requestDevice();
const Errors=[]; Device.addEventListener('uncapturederror',Event=>Errors.push(Event.error.message));
const Source=fs.readFileSync(new URL('../Shader/VehicleVisual.wgsl',import.meta.url),'utf8');
const Shader=Device.createShaderModule({code:`const TyreFamily:u32=9u; const TabletHousingFamily:u32=100u; const DisplayGlassFamily:u32=101u; const SteeringWheelFamily:u32=102u;
@group(0) @binding(0) var<uniform> VehicleState:VehicleRecord;
@group(0) @binding(1) var<storage,read> Inputs:array<vec4f>;
@group(0) @binding(2) var<storage,read_write> Outputs:array<vec4f>;
${Source}
@compute @workgroup_size(64) fn Evaluate(@builtin(global_invocation_id) Id:vec3u){
 if(Id.x>=arrayLength(&Inputs)/2u){return;}
 let P=Inputs[Id.x*2u];let N=Inputs[Id.x*2u+1u];
 let V=ResolveWheelVertex(u32(P.w),u32(N.w),P.xyz,N.xyz);
 Outputs[Id.x*2u]=vec4f(V.Position,1.0);Outputs[Id.x*2u+1u]=vec4f(V.Normal,0.0);
}`});
const Pipeline=await Device.createComputePipelineAsync({layout:'auto',compute:{module:Shader,entryPoint:'Evaluate'}});
const Vertices=[];
for(let Wheel=0;Wheel<4;Wheel++)for(let Angle=0;Angle<32;Angle++)for(const [Radius,Lateral] of [[0.50855,0],[0.43,0.13],[0.3448,0.105]]){
 const Phi=Angle*2*Math.PI/32;
 Vertices.push([Radius*Math.cos(Phi),Lateral,Radius*Math.sin(Phi),Wheel,Math.cos(Phi),0,Math.sin(Phi),9]);
}
Vertices.push([0.4,0,0,0,1,0,0,1]);
const Input=new Float32Array(Vertices.flat());
function Buffer(Bytes,Usage){const B=Device.createBuffer({size:Bytes,usage:Usage});return B;}
const InputBuffer=Buffer(Input.byteLength,GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_DST);Device.queue.writeBuffer(InputBuffer,0,Input);
const Output=Buffer(Input.byteLength,GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_SRC);
const Readback=Buffer(Input.byteLength,GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ);
const Vehicle=new VehicleSolver([]), Visual=new VehicleVisualState(), Results=[];
for(const Model of ['spring','xpbd','shell','solid']){
 Vehicle.TyreModel=Model;Vehicle.Respawn([-100,-34,.53],0);
 for(let Tick=0;Tick<360;Tick++)Vehicle.Advance({Throttle:0,Brake:0,Steer:0,Handbrake:false},1/120);
 const Encoded=Visual.EncodeRecord({},Vehicle.Wheels,Vehicle.Orientation);
 const Uniform=Buffer(Encoded.byteLength,GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST);Device.queue.writeBuffer(Uniform,0,Encoded);
 const Group=Device.createBindGroup({layout:Pipeline.getBindGroupLayout(0),entries:[Uniform,InputBuffer,Output].map((B,Index)=>({binding:Index,resource:{buffer:B}}))});
 const Encoder=Device.createCommandEncoder();const Pass=Encoder.beginComputePass();Pass.setPipeline(Pipeline);Pass.setBindGroup(0,Group);Pass.dispatchWorkgroups(Math.ceil(Vertices.length/64));Pass.end();
 Encoder.copyBufferToBuffer(Output,0,Readback,0,Input.byteLength);Device.queue.submit([Encoder.finish()]);await Readback.mapAsync(GPUMapMode.READ);
 const Actual=new Float32Array(Readback.getMappedRange().slice(0));Readback.unmap();let PositionError=0,NormalError=0, Worst=null;
 Vertices.forEach((_,Index)=>{
  const Offset=Index*8,P=Array.from(Input.subarray(Offset,Offset+3)),N=Array.from(Input.subarray(Offset+4,Offset+7));
  const Expected=ResolveWheelVertexCpu(Encoded,Input[Offset+3],Input[Offset+7],P,N);
  for(let Axis=0;Axis<3;Axis++){
   const Error=Math.abs(Expected.Position[Axis]-Actual[Offset+Axis]);
   if(Error>PositionError){PositionError=Error;Worst={P,N,Expected:Expected.Position,Actual:Array.from(Actual.subarray(Offset,Offset+3))};}
   NormalError=Math.max(NormalError,Math.abs(Expected.Normal[Axis]-Actual[Offset+4+Axis]));
  }
 });
 console.log(Model,{PositionError,NormalError,Worst});
 Results.push({Model,Vertices:Vertices.length,MaximumPositionErrorM:PositionError,MaximumNormalComponentError:NormalError});Uniform.destroy();
}
// SwiftShader trigonometric approximations reconstruct radial positions with ~0.1 mm error.
// This is an explicit geometric tolerance, not bitwise identity across CPU and GPU math.
assert.ok(Results.every(Result=>Result.MaximumPositionErrorM<0.0003 && Result.MaximumNormalComponentError<0.003));
assert.deepEqual(Errors,[]);const Report={Backend:process.env.BACKEND||'vulkan',PositionToleranceM:0.0003,NormalComponentTolerance:0.003,Results,Errors};console.log(JSON.stringify(Report,null,2));
if(process.env.OUT)fs.writeFileSync(process.env.OUT,JSON.stringify(Report,null,2)+'\n');
Device.destroy();
