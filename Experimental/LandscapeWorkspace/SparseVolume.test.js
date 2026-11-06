/*==========================================================================================================================================
                                                          SPARSEVOLUME.TEST.JS
==========================================================================================================================================*/
// 🧩 Sparse-volume coverage, numeric reconstruction, and budget validation.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { VolumeDistance } from './DistanceField.js';
import { VolumeSpecification, EncodeSparseVolume, DecodeSparseVolume, ReadVolumeCell } from './SparseVolume.js';
const Create = () => ({ Name:'Coast',Color:'#b4a17d',Visible:true,Depth:100,Caves:false,Resolution:256,Vertical:64,Strokes:[{ Points:[[0,0],[100,0]],Radius:40,Erase:false }],Regions:[{X0:-100,Z0:-100,X1:200,Z1:200}] });
function Bake(Layer, Horizontal = 32, Vertical = 16)
{
    const Specification=VolumeSpecification(Layer,Layer.Regions[0],Horizontal,Vertical);
    const Chunks=[...EncodeSparseVolume(Specification)];
    const Bytes=new Uint8Array(Chunks.reduce((Count,Chunk)=>Count+Chunk.Bytes.length,0));
    let Offset=0;
    for(const Chunk of Chunks){Bytes.set(Chunk.Bytes,Offset);Offset+=Chunk.Bytes.length;}
    return { Volume:DecodeSparseVolume(Bytes),Progress:Chunks.at(-1).Progress,Bytes };
}

test('adaptive leaves cover every cell exactly once and equal analytic truncated field',()=>
{
    const Layer=Create(), {Volume,Progress}=Bake(Layer);
    const {Origin,Pitch,Band,Dimensions}=Volume.Specification;
    Assert.equal(Progress.Covered,Dimensions.reduce((Product,Count)=>Product*Count,1));
    Assert.ok(Progress.Uniform>0);
    const Occupancy=new Uint8Array(Progress.Total);
    for(const Leaf of Volume.Leaves)
    for(let Z=Leaf.Z;Z<Leaf.Z+Leaf.Width;Z++)
    for(let Y=Leaf.Y;Y<Leaf.Y+Leaf.Width;Y++)
    for(let X=Leaf.X;X<Leaf.X+Leaf.Width;X++)Occupancy[(Z*Dimensions[1]+Y)*Dimensions[0]+X]++;
    Assert.ok(Occupancy.every(Count=>Count===1));
    for(let Z=0;Z<Dimensions[2];Z++)
    for(let Y=0;Y<Dimensions[1];Y++)
    for(let X=0;X<Dimensions[0];X++)
    {
        const Expected=Math.max(-Band,Math.min(Band,VolumeDistance(Layer,Origin[0]+(X+.5)*Pitch[0],Origin[1]+(Y+.5)*Pitch[1],Origin[2]+(Z+.5)*Pitch[2])));
        Assert.ok(Math.abs(ReadVolumeCell(Volume,X,Y,Z)-Expected)<1e-5);
    }
});
test('3D cave and eraser survive sampled export',()=>
{
    const Layer=Create();Layer.Caves=true;Layer.Strokes.push({Points:[[0,0]],Radius:10,Erase:true});
    const {Volume}=Bake(Layer),{Origin,Pitch,Band}=Volume.Specification;
    for(const Cell of [[10,5,10],[14,10,10],[16,12,10]])
    {
        const Position=Cell.map((Coordinate,Axis)=>Origin[Axis]+(Coordinate+.5)*Pitch[Axis]);
        Assert.ok(Math.abs(ReadVolumeCell(Volume,...Cell)-Math.max(-Band,Math.min(Band,VolumeDistance(Layer,...Position))))<1e-5);
    }
});
test('empty mask collapses to constant positive leaves',()=>
{
    const Layer=Create();Layer.Strokes=[];const {Volume,Progress}=Bake(Layer);
    Assert.equal(Progress.Bricks,0);Assert.ok(ReadVolumeCell(Volume,0,0,0)>0);
});
test('noncubic grid and partial octree coverage remain exact',()=>
{
    const {Volume,Progress}=Bake(Create(),24,8);Assert.equal(Progress.Covered,24*24*8);Assert.ok(Number.isFinite(ReadVolumeCell(Volume,23,7,23)));
});
test('invalid dimensions, truncated files and output budget fail explicitly',()=>
{
    const Layer=Create();Assert.throws(()=>VolumeSpecification(Layer,Layer.Regions[0],17,16));
    const Specification=VolumeSpecification(Layer,Layer.Regions[0],32,16);
    Assert.throws(()=>[...EncodeSparseVolume(Specification,1024)],/safety budget/);
    const {Bytes,Volume}=Bake(Layer);Assert.throws(()=>DecodeSparseVolume(Bytes.subarray(0,Bytes.length-1)),/Truncated/);
    Assert.throws(()=>ReadVolumeCell(Volume,-1,0,0));
});
