/*==========================================================================================================================================
                                                         LANDFORMSTACK.TEST.JS
==========================================================================================================================================*/
// 🧩 Pinned-source, region isolation, 3D geometry, edge interpolation, and bake consistency validation.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { CreateLandform, CompileRegion, PrimitiveDistance, InvalidateLandforms, LandformCatalogue, RegionScopes, LayerVerticalBounds } from './LandformStack.js';
import { VolumeDistance, ValidateDocument } from './DistanceField.js';
import { VolumeSpecification, EncodeSparseVolume, DecodeSparseVolume, ReadVolumeCell } from './SparseVolume.js';

const Create = () => ({Name:'Scoped coast',Color:'#b4a17d',Visible:true,Depth:100,Caves:false,Resolution:256,Vertical:64,Strokes:[{Points:[[0,0]],Radius:180,Erase:false}],Regions:[{X0:-200,Z0:-200,X1:200,Z1:200,Edge:0,Landforms:[]}]});
function Author(Layer, Types)
{
    Layer.Regions[0].Landforms=Types.map(CreateLandform);InvalidateLandforms();return Layer;
}

test('reference snapshots exactly match pinned Git blob fingerprints',()=>
{
    const Pin=JSON.parse(readFileSync(new URL('./ReferenceSnapshot/SourcePin.json',import.meta.url)));
    Assert.equal(Pin.Commit,'09b7064f282f4df5c5352f2bc97c82fb1c06a098');
    for(const Entry of Pin.Files)
    {
        const Bytes=readFileSync(new URL('./ReferenceSnapshot/'+Entry.Path.split('/').at(-1),import.meta.url));
        const Hash=createHash('sha1').update(`blob ${Bytes.length}\0`).update(Bytes).digest('hex');Assert.equal(Hash,Entry.GitBlob);
    }
});
test('first five landform defaults preserve the source procedural family defaults',()=>
{
    const Source=readFileSync(new URL('./ReferenceSnapshot/terrain-stack-presets.js',import.meta.url),'utf8');
    const Block=Source.slice(Source.indexOf("  'mountain-ridges':"),Source.indexOf("  'stratigraphic-beds':"));
    for(const Entry of LandformCatalogue.slice(0,5))
    {
        for(const [Property,SourceProperty] of [['Amount','amplitude'],['Scale','scale'],['Sharpness','sharpness'],['Azimuth','azimuth']])
        {
            const Match=Block.match(new RegExp(SourceProperty+': ([0-9.]+)'));Assert.equal(Entry[Property],Number(Match[1]));
        }
    }
});
test('every landform family changes a bounded 3D field',()=>
{
    for(const Entry of LandformCatalogue)
    {
        const Layer=Author(Create(),[Entry.Type]);let Changed=false;
        for(let X=-150;X<=150;X+=30)
        for(let Z=-150;Z<=150;Z+=30)
        for(const Y of [-15,15,35,65])
        {
            const Before=VolumeDistance(Create(),X,Y,Z),After=VolumeDistance(Layer,X,Y,Z);
            Assert.ok(Number.isFinite(After));if(Math.abs(Before-After)>.01)Changed=true;
        }
        Assert.ok(Changed,Entry.Type+' must alter geometry');
    }
});
test('editing one boundary leaves a distant boundary unchanged',()=>
{
    const Layer=Create();Layer.Strokes.push({Points:[[1000,0]],Radius:180,Erase:false});Layer.Regions.push({X0:800,Z0:-200,X1:1200,Z1:200});
    const Before=VolumeDistance(Layer,1000,35,0);Author(Layer,['mesa','stacks']);Layer.Regions[0].Edge=80;
    Assert.equal(VolumeDistance(Layer,1000,35,0),Before);
    Assert.equal(Layer.Regions[1].Landforms,undefined);
});
test('stack cannot create solid outside paint, region bounds, or erased holes',()=>
{
    const Layer=Author(Create(),['mesa','ridges','arch']);Layer.Regions[0].Edge=20;
    Layer.Strokes.push({Points:[[0,0]],Radius:20,Erase:true});
    for(const Point of [[300,20,0],[0,20,300],[0,50,0],[201,35,0]])Assert.ok(VolumeDistance(Layer,...Point)>0);
});
test('edge zero preserves sharp geometry; feathering is inward and continuous',()=>
{
    const Layer=Create(),Region=Layer.Regions[0];const Sharp=VolumeDistance(Layer,175,-2,0);
    Assert.ok(Sharp<0);Region.Edge=40;const Soft=VolumeDistance(Layer,175,-2,0);Assert.ok(Soft>Sharp);
    Assert.ok(VolumeDistance(Layer,185,-2,0)>0);
    for(let X=150;X<200;X+=.5)Assert.ok(Math.abs(VolumeDistance(Layer,X+.001,-2,0)-VolumeDistance(Layer,X,-2,0))<=.00101);
    Region.Edge=0;Assert.equal(VolumeDistance(Layer,175,-2,0),Sharp);
});
test('CSG stack order is meaningful and disabled operations are inert',()=>
{
    const Layer=Author(Create(),['mesa','tunnel']);Layer.Regions[0].Landforms[0].Amount=100;InvalidateLandforms();
    const Before=[];for(let X=-100;X<=100;X+=20)Before.push(VolumeDistance(Layer,X,12,0));
    Layer.Regions[0].Landforms.reverse();InvalidateLandforms();const After=[];for(let X=-100;X<=100;X+=20)After.push(VolumeDistance(Layer,X,12,0));
    Assert.ok(Before.some((Distance,Index)=>Math.abs(Distance-After[Index])>.01));
    for(const Operation of Layer.Regions[0].Landforms)Operation.Enabled=false;InvalidateLandforms();
    Assert.equal(VolumeDistance(Layer,0,35,0),VolumeDistance(Create(),0,35,0));
});
test('arch primitive has solid roof over empty space along the same XZ coordinate',()=>
{
    const Layer=Author(Create(),['arch']),Primitive=CompileRegion(Layer,Layer.Regions[0]).Steps[0].Primitives[0];
    const [X,Y,Z]=Primitive.Centre;
    Assert.ok(PrimitiveDistance(Primitive,X,Y,Z)>0);Assert.ok(PrimitiveDistance(Primitive,X,Y+Primitive.Radii[0],Z)<0);
    Assert.ok(VolumeDistance(Layer,X,Y,Z)>0);Assert.ok(VolumeDistance(Layer,X,Y+Primitive.Radii[0],Z)<0);
});
test('deterministic seed changes signed geometry reproducibly',()=>
{
    const Layer=Author(Create(),['ridges']);const Before=structuredClone(CompileRegion(Layer,Layer.Regions[0]));
    InvalidateLandforms();Assert.deepEqual(CompileRegion(Layer,Layer.Regions[0]),Before);
    Layer.Regions[0].Landforms[0].Seed++;InvalidateLandforms();Assert.notDeepEqual(CompileRegion(Layer,Layer.Regions[0]),Before);
});
test('multi-layer scope enumerates only explicit selected boundaries',()=>
{
    const Layers=[Create(),Create(),Create()];Layers[1].Regions.push({...Layers[1].Regions[0]});
    const Scopes=RegionScopes(Layers,new Set([1]));Assert.deepEqual(Scopes.map(Scope=>[Scope.LayerIndex,Scope.RegionIndex]),[[1,0],[1,1]]);
    Assert.equal(RegionScopes(Layers,new Set()).length,0);
});
test('stack field respects conservative unit Lipschitz bound for sparse classification',()=>
{
    const Layer=Author(Create(),['ridges','mesa','stacks','tunnel']);Layer.Regions[0].Edge=35;
    for(let Index=0;Index<1000;Index++)
    {
        const X=Math.sin(Index*1.3)*230,Y=Math.cos(Index*.7)*150,Z=Math.sin(Index*.8)*230;
        const A=VolumeDistance(Layer,X,Y,Z),B=VolumeDistance(Layer,X+.06,Y-.08,Z+.03);
        Assert.ok(Math.abs(A-B)<=Math.hypot(.06,.08,.03)+1e-8);
    }
});
test('landform bake bounds include arch roof and sampled field reconstructs correctly',()=>
{
    const Layer=Author(Create(),['arch','stacks']),Region=Layer.Regions[0];Region.Edge=20;
    const Bounds=LayerVerticalBounds(Layer,Region),Arch=CompileRegion(Layer,Region).Steps[0].Primitives[0];
    Assert.ok(Bounds[1]>Arch.Centre[1]+Arch.Radii[0]+Arch.Radii[2]);
    const Specification=VolumeSpecification(Layer,Region,24,16),Chunks=[...EncodeSparseVolume(Specification)];
    const Bytes=new Uint8Array(Chunks.reduce((Count,Chunk)=>Count+Chunk.Bytes.length,0));let Offset=0;
    for(const Chunk of Chunks){Bytes.set(Chunk.Bytes,Offset);Offset+=Chunk.Bytes.length;}
    const Volume=DecodeSparseVolume(Bytes);Assert.equal(Chunks.at(-1).Progress.Covered,24*16*24);
    for(let Index=0;Index<300;Index++)
    {
        const Cell=[Index%24,Math.floor(Index/24)%16,(Index*7)%24],Point=Cell.map((Coordinate,Axis)=>Specification.Origin[Axis]+(Coordinate+.5)*Specification.Pitch[Axis]);
        const Expected=Math.max(-Specification.Band,Math.min(Specification.Band,VolumeDistance(Layer,...Point)));
        Assert.ok(Math.abs(ReadVolumeCell(Volume,...Cell)-Expected)<1e-5);
    }
});
test('old documents remain valid; malformed and oversize region stacks are rejected',()=>
{
    const Layer=Create();delete Layer.Regions[0].Landforms;delete Layer.Regions[0].Edge;
    Assert.doesNotThrow(()=>ValidateDocument({Format:'FrontierLandscape/1',Layers:[Layer]}));
    Layer.Regions[0].Edge=-1;Assert.throws(()=>ValidateDocument({Format:'FrontierLandscape/1',Layers:[Layer]}));
    Layer.Regions[0].Edge=20;Layer.Regions[0].Landforms=Array.from({length:9},()=>CreateLandform('ridges'));
    Assert.throws(()=>ValidateDocument({Format:'FrontierLandscape/1',Layers:[Layer]}));
});

test('cached compilation follows direct operation and boundary edits without stale geometry',()=>
{
    const Layer=Author(Create(),['mesa']),Region=Layer.Regions[0];const Before=structuredClone(CompileRegion(Layer,Region));
    Region.Landforms[0].Amount=200;Assert.notDeepEqual(CompileRegion(Layer,Region),Before);
    const Position=CompileRegion(Layer,Region).Steps[0].Primitives[0].Centre[0];
    Region.X0+=100;Region.X1+=100;Assert.equal(CompileRegion(Layer,Region).Steps[0].Primitives[0].Centre[0],Position+100);
});
test('bake support bounds include strongly blended tall thin features',()=>
{
    const Layer=Author(Create(),['dunes','stacks']),Region=Layer.Regions[0];
    for(const Operation of Region.Landforms){Operation.Amount=500;Operation.Blend=100;Operation.Scale=5;}
    const Bounds=LayerVerticalBounds(Layer,Region);Assert.ok(Bounds[1]>500);
    for(let X=-100;X<=100;X+=25)
    for(let Z=-100;Z<=100;Z+=25)Assert.ok(VolumeDistance(Layer,X,Bounds[1],Z)>0);
});

test('a selected-boundary bake excludes overlapping neighbouring boundary landforms',()=>
{
    const Layer=Create(),Selected=Layer.Regions[0];Layer.Regions.push({X0:-100,Z0:-150,X1:200,Z1:150,Landforms:[{...CreateLandform('mesa'),Amount:200,Scale:1,Blend:0}]});
    let Difference=false;
    for(let X=-50;X<=150;X+=20)
    for(let Z=-80;Z<=80;Z+=20)
    {
        const Local=VolumeDistance(Layer,X,60,Z,Selected),Combined=VolumeDistance(Layer,X,60,Z);
        Assert.ok(Local>0);if(Combined<0)Difference=true;
    }
    Assert.ok(Difference);
    const Specification=VolumeSpecification(Layer,Selected,16,16),Chunks=[...EncodeSparseVolume(Specification)];
    const Bytes=new Uint8Array(Chunks.reduce((Count,Chunk)=>Count+Chunk.Bytes.length,0));let Offset=0;
    for(const Chunk of Chunks){Bytes.set(Chunk.Bytes,Offset);Offset+=Chunk.Bytes.length;}
    const Volume=DecodeSparseVolume(Bytes);
    for(const Cell of [[8,12,8],[6,10,6],[10,13,10]])
    {
        const Point=Cell.map((Coordinate,Axis)=>Specification.Origin[Axis]+(Coordinate+.5)*Specification.Pitch[Axis]);
        const Expected=Math.max(-Specification.Band,Math.min(Specification.Band,VolumeDistance(Layer,...Point,Selected)));
        Assert.ok(Math.abs(ReadVolumeCell(Volume,...Cell)-Expected)<1e-5);
    }
});
