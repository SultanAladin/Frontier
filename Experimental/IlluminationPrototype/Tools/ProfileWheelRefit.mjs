// CPU cost of moving-wheel BLAS refits on the real authored geometry; mock queue excludes GPU upload cost.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import os from 'node:os';
import {SceneStructure} from '../Source/FacetStructure.js';
import {RayTracingSpace} from '../Source/RayTracingSpace.js';
import {VehicleSolver} from '../Source/VehicleSolver.js';
import {VehicleVisualState} from '../Source/VehicleVisualState.js';
import {SummarizeDurations} from '../Source/TimingComparison.js';
const Root=fileURLToPath(new URL('..',import.meta.url));
globalThis.fetch=async Resource=>new Response(fs.readFileSync(path.join(Root,String(Resource))));
const Scene=new SceneStructure();await Scene.ConstructScene('Archive',8);
let Writes=0;
const Space=new RayTracingSpace({Device:{queue:{writeBuffer(){++Writes;}}}});Space.BuildCpuHierarchy(Scene);
const Vehicle=new VehicleSolver([]),Visual=new VehicleVisualState(),Results={};
for(const Model of ['spring','xpbd','shell','solid']){
 Vehicle.TyreModel=Model;Vehicle.Respawn([-100,-34,.53],0);
 for(let Tick=0;Tick<360;Tick++)Vehicle.Advance({Throttle:0,Brake:0,Steer:0,Handbrake:false},1/120);
 let Encoded=Visual.EncodeRecord({},Vehicle.Wheels,Vehicle.Orientation);Space.UpdateWheelGeometry(Scene,Encoded);
 const Samples=[];
 for(let Tick=0;Tick<60;Tick++){
  for(const Wheel of Vehicle.Wheels)Wheel.SpinAngle+=.025;
  Encoded=Visual.EncodeRecord({},Vehicle.Wheels,Vehicle.Orientation);
  const Start=performance.now();Space.UpdateWheelGeometry(Scene,Encoded);Samples.push(performance.now()-Start);
 }
 Results[Model]={CpuMsPerFourWheelRefit:SummarizeDurations(Samples),RawMs:Samples};
}
const Report={Cpu:os.cpus()[0]?.model,Node:process.version,Results,Writes,
 TrianglesPerWheel:Space.WheelBlas.map(Blas=>Blas.TriangleCount),
 Note:'60 moving-contact-angle samples per model. Real authored triangles, mock queue; excludes GPU upload, physics and rendering. Unchanged geometry skips the refit.'};
console.log(JSON.stringify(Report,null,2));
const Output=fileURLToPath(new URL('../../../Exhibits/IlluminationConsolidation/Results/RendererRepair/WheelRefitCpu.json',import.meta.url));
fs.writeFileSync(Output,JSON.stringify(Report,null,2)+'\n');
