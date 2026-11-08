/*==========================================================================================================================================
                                                         MASKLAYERSTACK.TEST.JS
==========================================================================================================================================*/
// 🧩 Mask-owned stack scope, preset independence, legacy compatibility, and signed-volume export validation.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { VolumeDistance,ValidateDocument } from './DistanceField.js';
import { CompileRegion,CreateLandform } from './LandformStack.js';
import { LandformPresets,CreatePresetExample,CreatePresetWorld,CreatePresetOperations } from './LandformPresets.js';
import { VolumeSpecification,EncodeSparseVolume,DecodeSparseVolume,ReadVolumeCell } from './SparseVolume.js';

function Create()
{
    const Layer=CreatePresetExample('alpine-ridges');Layer.Landforms=[];
    Layer.Strokes.push({Points:[[800,0]],Radius:155,Erase:false});Layer.Regions.push({X0:590,Z0:-195,X1:1010,Z1:195,Edge:25});return Layer;
}

test('one mask stack compiles for every boundary and leaves other masks untouched',()=>
{
    const Layer=Create(),Other=Create();Layer.Landforms=[CreateLandform('mesa')];
    for(const Region of Layer.Regions){const Compilation=CompileRegion(Layer,Region);Assert.equal(Compilation.Steps.length,1);Assert.equal(Compilation.Operations[0].Type,'mesa');Assert.equal(Region.Landforms,undefined);}
    Assert.equal(CompileRegion(Other,Other.Regions[0]).Steps.length,0);
    const Before=CompileRegion(Layer,Layer.Regions[1]).MaximumY;Layer.Landforms[0].Amount=200;
    Assert.ok(CompileRegion(Layer,Layer.Regions[0]).MaximumY>100);Assert.ok(CompileRegion(Layer,Layer.Regions[1]).MaximumY>Before);
});
test('mask stack can be authored before any region exists without creating terrain',()=>
{
    const Layer=Create();Layer.Regions=[];Layer.Strokes=[];Layer.Landforms=CreatePresetOperations('desert-mesas');
    Assert.doesNotThrow(()=>ValidateDocument({Format:'FrontierLandscape/1',Layers:[Layer]}));Assert.ok(VolumeDistance(Layer,0,0,0)>1e6);
});
test('all six presets validate and return independent operation arrays',()=>
{
    for(const Preset of LandformPresets)
    {
        const First=CreatePresetExample(Preset.Token),Second=CreatePresetExample(Preset.Token);
        Assert.doesNotThrow(()=>ValidateDocument({Format:'FrontierLandscape/1',Layers:[First]}));
        First.Landforms[0].Amount=499;Assert.notEqual(Second.Landforms[0].Amount,499);Assert.notEqual(Preset.Operations[0].Amount,499);
        Assert.ok(Number.isFinite(VolumeDistance(Second,0,30,0)));
    }
    Assert.equal(CreatePresetWorld().length,6);
});
test('six example preview PNGs are distinct actual captured images',()=>
{
    const Digests=[];
    for(const Preset of LandformPresets)
    {
        const Bytes=readFileSync(new URL('./PresetPreviews/'+Preset.Token+'.png',import.meta.url));
        Assert.equal(Bytes.subarray(1,4).toString(),'PNG');Assert.ok(Bytes.length>10000);Digests.push(createHash('sha256').update(Bytes).digest('hex'));
    }
    Assert.equal(new Set(Digests).size,6);
});
test('legacy regional modifiers remain after the mask stack and survive round trip',()=>
{
    const Layer=Create();Layer.Landforms=[CreateLandform('mesa')];Layer.Regions[0].Landforms=[CreateLandform('tunnel')];
    const Before=structuredClone(Layer);ValidateDocument({Format:'FrontierLandscape/1',Layers:[Layer]});Assert.deepEqual(Layer,Before);
    Assert.deepEqual(CompileRegion(Layer,Layer.Regions[0]).Operations.map(Operation=>Operation.Type),['mesa','tunnel']);
    Assert.deepEqual(CompileRegion(Layer,Layer.Regions[1]).Operations.map(Operation=>Operation.Type),['mesa']);
});
test('mask stack and combined legacy limits fail explicitly',()=>
{
    const Layer=Create();Layer.Landforms=Array.from({length:9},()=>CreateLandform('mesa'));Assert.throws(()=>ValidateDocument({Format:'FrontierLandscape/1',Layers:[Layer]}));
    Layer.Landforms=Array.from({length:8},()=>CreateLandform('mesa'));Layer.Regions[1].Landforms=[CreateLandform('tunnel')];
    Assert.throws(()=>ValidateDocument({Format:'FrontierLandscape/1',Layers:[Layer]}),/combined/);
});
test('mask-owned arch retains a true empty space beneath a solid roof',()=>
{
    const Layer=CreatePresetExample('coastal-arches'),Region=Layer.Regions[0];Layer.Landforms=[CreateLandform('arch')];
    const Primitive=CompileRegion(Layer,Region).Steps[0].Primitives[0],[X,Y,Z]=Primitive.Centre;
    Assert.ok(VolumeDistance(Layer,X,Y,Z)>0);Assert.ok(VolumeDistance(Layer,X,Y+Primitive.Radii[0],Z)<0);
});
test('selected-boundary voxel export includes its parent mask stack without neighbouring legacy features',()=>
{
    const Layer=Create();Layer.Landforms=[CreateLandform('arch')];Layer.Regions[1].Landforms=[CreateLandform('stacks')];
    const Region=Layer.Regions[0],Specification=VolumeSpecification(Layer,Region,16,16),Chunks=[...EncodeSparseVolume(Specification)];
    const Bytes=new Uint8Array(Chunks.reduce((Count,Chunk)=>Count+Chunk.Bytes.length,0));let Offset=0;
    for(const Chunk of Chunks){Bytes.set(Chunk.Bytes,Offset);Offset+=Chunk.Bytes.length;}
    const Volume=DecodeSparseVolume(Bytes);Assert.equal(Volume.Specification.Layer.Landforms[0].Type,'arch');
    for(const Cell of [[8,8,8],[9,12,9],[5,10,6]])
    {
        const Point=Cell.map((Coordinate,Axis)=>Specification.Origin[Axis]+(Coordinate+.5)*Specification.Pitch[Axis]);
        const Expected=Math.max(-Specification.Band,Math.min(Specification.Band,VolumeDistance(Layer,...Point,Region)));
        Assert.ok(Math.abs(ReadVolumeCell(Volume,...Cell)-Expected)<1e-5);
    }
});

test('custom operation names round-trip while retaining identical signed geometry',()=>
{
    const Layer=Create();Layer.Landforms=[CreateLandform('mesa')];
    const Before=VolumeDistance(Layer,0,50,0);Layer.Landforms[0].Name='Sandstone <mesa>';
    const Document=ValidateDocument(JSON.parse(JSON.stringify({Format:'FrontierLandscape/1',Layers:[Layer]})));
    Assert.equal(Document.Layers[0].Landforms[0].Name,'Sandstone <mesa>');Assert.equal(VolumeDistance(Document.Layers[0],0,50,0),Before);
});
test('invalid layer names fail document validation explicitly',()=>
{
    const Layer=Create();Layer.Landforms=[CreateLandform('mesa')];
    for(const Name of ['',4,' '.repeat(4),'x'.repeat(97)])
    {
        Layer.Landforms[0].Name=Name;Assert.throws(()=>ValidateDocument({Format:'FrontierLandscape/1',Layers:[Layer]}),/operation name/);
    }
});
