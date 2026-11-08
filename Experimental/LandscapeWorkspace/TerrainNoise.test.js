/*==========================================================================================================================================
                                                            TERRAINNOISE.TEST.JS
==========================================================================================================================================*/
// 🧩 Procedural terrain spectrum, conservative volume gradients, and camera-relative navigation verification.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { TerrainNoise,TerrainCategories,LatticeNoise } from './TerrainNoise.js';
import { CreateTerrainWorld } from './TerrainExamples.js';
import { CompileRegion,PrimitiveDistance,CreateLandform } from './LandformStack.js';
import { ValidateDocument,VolumeDistance } from './DistanceField.js';
import { TranslateCamera } from './CameraNavigation.js';
import { EncodeSparseVolume,VolumeSpecification,DecodeSparseVolume,ReadVolumeCell } from './SparseVolume.js';

test('lattice noise is deterministic and continuous at negative and positive cell boundaries',()=>
{
    for(const Boundary of [-3,-1,0,1,4])
    {
        const Left=LatticeNoise(Boundary-1e-7,.37,-2.12,4029),Right=LatticeNoise(Boundary+1e-7,.37,-2.12,4029);
        Assert.ok(Math.abs(Left-Right)<1e-6);Assert.equal(Left,LatticeNoise(Boundary-1e-7,.37,-2.12,4029));
    }
});
test('seven terrain spectra are distinct, seed-dependent and bounded',()=>
{
    const Spectra=[];
    for(const Category of Object.values(TerrainCategories))
    {
        const Samples=Array.from({length:50},(_,Index)=>TerrainNoise(Index*.29,.5,Index*-.37,Category,4029,.65));
        Assert.ok(Samples.every(Sample=>Sample>=0&&Sample<=1));Assert.ok(Math.max(...Samples)-Math.min(...Samples)>.01);
        Assert.notEqual(Samples[10],TerrainNoise(2.9,.5,-3.7,Category,5031,.65));Spectra.push(JSON.stringify(Samples));
    }
    Assert.equal(new Set(Spectra).size,7);
});
test('eight generator examples already contain independent mask-owned terrain operations',()=>
{
    const Layers=CreateTerrainWorld();Assert.equal(Layers.length,8);Assert.doesNotThrow(()=>ValidateDocument({Format:'FrontierLandscape/1',Layers}));
    Assert.deepEqual(Layers.map(Layer=>Layer.Landforms.at(-1).Type),[...Object.keys(TerrainCategories),'craters']);
    Assert.ok(Layers.every(Layer=>Layer.Landforms.length&&Layer.Strokes.length&&Layer.Regions.length));
});
test('noise displacement depends on all three coordinates, not a stored or sampled heightmap',()=>
{
    for(const Type of Object.keys(TerrainCategories))
    {
        const Layer=CreateTerrainWorld()[0];Layer.Landforms=[CreateLandform(Type)];const Primitive=CompileRegion(Layer,Layer.Regions[0]).Steps[0].Primitives[0];
        const [X,Y,Z]=Primitive.Centre;const Middle=PrimitiveDistance(Primitive,X+80,Y+50,Z+30);
        Assert.notEqual(Middle,PrimitiveDistance(Primitive,X+80,Y+51,Z+30));
        Assert.ok(VolumeDistance(Layer,X,-20,Z)<0);Assert.ok(VolumeDistance(Layer,X,Y+1000,Z)>0);Assert.ok(VolumeDistance(Layer,X+1000,Y,Z)>0);
    }
});
test('procedural signed fields obey conservative unit gradient bounds across all seven spectra',()=>
{
    for(const Type of Object.keys(TerrainCategories))
    {
        const Layer=CreateTerrainWorld()[0];Layer.Landforms=[{...CreateLandform(Type),Amount:300,Scale:5,Sharpness:1,Blend:0}];
        const Primitive=CompileRegion(Layer,Layer.Regions[0]).Steps[0].Primitives[0];const [X,Y,Z]=Primitive.Centre;
        for(let Index=0;Index<180;Index++)
        {
            const Point=[X+Math.sin(Index*1.3)*300,Y+Math.cos(Index*.77)*200,Z+Math.sin(Index*.27)*300];
            const Displacement=[Math.sin(Index)*.01,Math.cos(Index*.3)*.01,Math.cos(Index)*.01];
            Assert.ok(Math.abs(PrimitiveDistance(Primitive,...Point)-PrimitiveDistance(Primitive,...Point.map((Coordinate,Axis)=>Coordinate+Displacement[Axis])))<=Math.hypot(...Displacement)*1.0001,Type);
        }
    }
});
test('sparse noise-volume export reconstructs sampled distances and stores the generator stack',()=>
{
    const Layer=CreateTerrainWorld()[2],Region=Layer.Regions[0],Specification=VolumeSpecification(Layer,Region,16,16);
    const Chunks=[...EncodeSparseVolume(Specification)],Bytes=new Uint8Array(Chunks.reduce((Length,Chunk)=>Length+Chunk.Bytes.length,0));let Offset=0;
    for(const Chunk of Chunks){Bytes.set(Chunk.Bytes,Offset);Offset+=Chunk.Bytes.length;}const Volume=DecodeSparseVolume(Bytes);
    Assert.equal(Volume.Specification.Layer.Landforms[0].Type,'ridged-multifractal');
    for(const Cell of [[3,4,5],[8,8,8],[12,9,12]])
    {
        const Point=Cell.map((Coordinate,Axis)=>Specification.Origin[Axis]+(Coordinate+.5)*Specification.Pitch[Axis]);
        const Expected=Math.max(-Specification.Band,Math.min(Specification.Band,VolumeDistance(Layer,...Point,Region)));
        Assert.ok(Math.abs(ReadVolumeCell(Volume,...Cell)-Expected)<1e-5);
    }
});
test('camera-relative WASD translates using yaw, normalizes diagonals and leaves orbit unchanged',()=>
{
    const Camera={Yaw:0,Orbit:500,Pitch:.6,Target:[0,0,0]};TranslateCamera(Camera,{Right:0,Forward:1,Up:0},1,100);Assert.deepEqual(Camera.Target,[0,0,-100]);
    Camera.Target=[0,0,0];TranslateCamera(Camera,{Right:1,Forward:1,Up:0},1,100);Assert.ok(Math.abs(Math.hypot(...Camera.Target)-100)<1e-10);
    Camera.Target=[0,0,0];Camera.Yaw=Math.PI/2;TranslateCamera(Camera,{Right:0,Forward:1,Up:0},1,100);Assert.ok(Math.abs(Camera.Target[0]+100)<1e-10);Assert.equal(Camera.Orbit,500);
});
