import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { RayTracingSpace, UnpackOctahedralCpu } from '../Source/RayTracingSpace.js';
import { ResolveWheelVertexCpu } from '../Source/WheelVertexDeformation.js';
import { VehicleSolver } from '../Source/VehicleSolver.js';
import { VehicleVisualState } from '../Source/VehicleVisualState.js';

const Writes = [];
const Exchange = { Device: { queue: { writeBuffer(...Arguments) { Writes.push(Arguments); } } } };
const Facets = Array.from({length:32}, (_,Index) => {
    const Angle = Index * Math.PI / 16, Next = (Index+1) * Math.PI/16;
    return { Family:9, Corners:[[0.50855*Math.cos(Angle),-0.1,0.50855*Math.sin(Angle)],
        [0.50855*Math.cos(Next),-0.1,0.50855*Math.sin(Next)], [0.45*Math.cos(Angle),0.1,0.45*Math.sin(Angle)]] };
});

test('independent wheel BLAS refits deform once, tightly enclose all triangles and skip unchanged uploads', () => {
    const Scene = {VehicleOccupants:{Wheels:[0,1,2,3]},FacetSets:[{Facets}],Occupants:Array.from({length:4},()=>({FacetSet:0}))};
    const Space = new RayTracingSpace(Exchange); Space.BuildCpuHierarchy(Scene);
    assert.equal(new Set(Space.WheelBlas.map(Blas=>Blas.NodeStart)).size,4);
    assert.ok(Space.WheelBlas.every(Blas=>Space.OccupantBlasRoot[Blas.OccupantIndex]===Blas.NodeStart));
    const Vehicle = new VehicleSolver([]);
    for(let Tick=0;Tick<120;Tick++)Vehicle.Advance({Throttle:0,Brake:0,Steer:0,Handbrake:false},1/120);
    const Encoded = new VehicleVisualState().EncodeRecord({},Vehicle.Wheels,Vehicle.Orientation);
    Space.UpdateWheelGeometry(Scene,Encoded);
    for(const Blas of Space.WheelBlas){
        const Root = Blas.NodeStart*8, Rest = Blas.RestTriangles, Bits=new Uint32Array(Rest.buffer);
        for(let Triangle=0;Triangle<Blas.TriangleCount;Triangle++){
            const Source=Triangle*16, Target=(Blas.TriStart+Triangle)*16;
            for(let Corner=0;Corner<3;Corner++){
                const Position=[0,1,2].map(Axis=>Rest[Source+Axis]+(Corner?Rest[Source+Corner*4+Axis]:0));
                const Expected=ResolveWheelVertexCpu(Encoded,Blas.Wheel,9,Position,UnpackOctahedralCpu(Bits[Source+[7,11,15][Corner]]));
                for(let Axis=0;Axis<3;Axis++){
                    const Actual=Space.AllTriScalars[Target+Axis]+(Corner?Space.AllTriScalars[Target+Corner*4+Axis]:0);
                    assert.ok(Math.abs(Actual-Expected.Position[Axis])<1e-6);
                    assert.ok(Actual>=Space.AllNodeScalars[Root+Axis]&&Actual<=Space.AllNodeScalars[Root+4+Axis]);
                }
            }
        }
        // No blanket 25cm expansion: the root's radial range remains close to the original tyre radius.
        assert.ok(Space.AllNodeScalars[Root+4]-Space.AllNodeScalars[Root]<1.05);
        for(let Node=Blas.NodeStart;Node<Blas.NodeStart+Blas.NodeCount;Node++){
            const Offset=Node*8,First=Space.AllNodeIntegers[Offset+3],Count=Space.AllNodeIntegers[Offset+7];
            if(!Count)for(const Child of [First,First+1])for(let Axis=0;Axis<3;Axis++){
                assert.ok(Space.AllNodeScalars[Offset+Axis]<=Space.AllNodeScalars[Child*8+Axis]);
                assert.ok(Space.AllNodeScalars[Offset+4+Axis]>=Space.AllNodeScalars[Child*8+4+Axis]);
            }
        }
    }
    const Count=Writes.length; Space.UpdateWheelGeometry(Scene,Encoded); assert.equal(Writes.length,Count);
    Encoded[37]+=0.001; Space.UpdateWheelGeometry(Scene,Encoded); assert.equal(Writes.length,Count+2);
});

test('ray shaders consume predeformed triangles without per-candidate deformation or padded node bounds',()=>{
    const Source=fs.readFileSync(new URL('../Shader/RayTraversal.wgsl',import.meta.url),'utf8');
    assert.doesNotMatch(Source,/DeformRayVertex|ResolveWheelVertex|Node\.Minimum - vec3f/);
    assert.match(Source,/let E1 = BvhTriangles\[Base \+ 1u\]\.xyz/);
});

test('cached rest projections agree with uncached deformation for all four tyre models', async () => {
    const { PrepareWheelVertexCpu } = await import('../Source/WheelVertexDeformation.js');
    for (const Model of ['spring', 'xpbd', 'shell', 'solid']) {
        const Vehicle = new VehicleSolver([]);
        Vehicle.TyreModel = Model;
        Vehicle.Respawn([-100,-34,.53],0);
        for (let Tick = 0; Tick < 12; ++Tick) Vehicle.Advance({Throttle:0,Brake:0,Steer:0,Handbrake:false},1/120);
        const Encoded = new VehicleVisualState().EncodeRecord({},Vehicle.Wheels,Vehicle.Orientation);
        for (let Index = 0; Index < 24; ++Index) {
            const Angle = Index * Math.PI / 12;
            const Position = [0.49*Math.cos(Angle), 0.08, 0.49*Math.sin(Angle)];
            const Normal = [Math.cos(Angle), 0.2, Math.sin(Angle)];
            assert.deepEqual(ResolveWheelVertexCpu(Encoded,0,9,Position,Normal,PrepareWheelVertexCpu(Position,Normal)),
                ResolveWheelVertexCpu(Encoded,0,9,Position,Normal));
        }
    }
});
