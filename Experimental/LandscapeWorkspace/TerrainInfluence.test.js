/*==========================================================================================================================================
                                                         TERRAININFLUENCE.TEST.JS
==========================================================================================================================================*/
// 🧩 Flat-foundation radial terrain, compact support, connected solids, conservative gradients and serialization checks.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { CreateLandform,CompileRegion,EvaluateRegionStack } from './LandformStack.js';
import { TerrainGeneratorCodes,TerrainInfluenceDefaults,RadialWeight,TerrainProfile,TerrainProfileGradient } from './TerrainInfluence.js';
import { VolumeDistance,ValidateDocument } from './DistanceField.js';
const Region={X0:-320,Z0:-320,X1:320,Z1:320,Edge:0};
const CreateLayer=Operations=>({Name:'Radial terrain',Color:'#c6ad76',Visible:true,Depth:100,Caves:false,Resolution:256,Vertical:64,Strokes:[{Points:[[0,0]],Radius:300,Erase:false}],Regions:[{...Region}],Landforms:Operations});
function Surface(Layer,X,Z)
{
    let Lower=-1,Upper=1000;
    for(let Index=0;Index<60;Index++)
    {
        const Midpoint=(Lower+Upper)*.5;
        if(VolumeDistance(Layer,X,Midpoint,Z)<0)Lower=Midpoint;else Upper=Midpoint;
    }
    return (Lower+Upper)*.5;
}
test('all terrain generators compile to continuous radial profiles, not scattered boxes/cones/ellipsoids',()=>
{
    for(const Type of Object.keys(TerrainGeneratorCodes))
    {
        const Layer=CreateLayer([CreateLandform(Type)]),Compiled=CompileRegion(Layer,Layer.Regions[0]);
        Assert.equal(Compiled.TerrainEnabled,true);Assert.equal(Compiled.Steps[0].Primitives.length,1);
        Assert.equal(Compiled.Steps[0].Primitives[0].Category,TerrainGeneratorCodes[Type]);
        Assert.ok(Compiled.Steps[0].Primitives[0].VerticalFrequency>0);
    }
    for(const Type of ['arch','tunnel','stacks','strata'])
    {
        const Layer=CreateLayer([CreateLandform(Type)]);Assert.equal(CompileRegion(Layer,Layer.Regions[0]).TerrainEnabled,false);
    }
});
test('every terrain profile leaves the exact flat foundation outside its radial support',()=>
{
    for(const Type of Object.keys(TerrainGeneratorCodes))
    for(const Mode of ['union','subtract','intersect','blend'])
    {
        const Layer=CreateLayer([{...CreateLandform(Type),InfluenceRadius:.5,Mode}]);
        for(const [X,Z] of [[200,0],[-220,0],[0,230],[180,180]])Assert.ok(Math.abs(Surface(Layer,X,Z))<1e-8,`${Type}/${Mode}`);
    }
});
test('compact radial weight is C1 at the centre, inner falloff contour and outer support',()=>
{
    const Layer=CreateLayer([{...CreateLandform('dunes'),InfluenceRadius:.5,InfluenceFalloff:.6}]);
    const Primitive=CompileRegion(Layer,Layer.Regions[0]).Steps[0].Primitives[0],Radius=Primitive.Radii[0],ε=1e-4;
    Assert.equal(RadialWeight(Primitive,0,0),1);Assert.equal(RadialWeight(Primitive,Radius,0),0);Assert.equal(RadialWeight(Primitive,Radius+100,0),0);
    for(const X of [0,Radius*(1-.6),Radius])Assert.ok(Math.abs((RadialWeight(Primitive,X+ε,0)-RadialWeight(Primitive,X-ε,0))/(2*ε))<1e-7);
    Assert.ok(RadialWeight(Primitive,Radius*.7,0)>0&&RadialWeight(Primitive,Radius*.7,0)<1);
});
test('overlapping dunes stay attached to the foundation, with one solid-to-empty transition per column',()=>
{
    const Layer=CreateLayer([-.16,.16].map((InfluenceX,Index)=>({...CreateLandform('dunes'),Amount:90,Scale:3,InfluenceX,InfluenceRadius:.65,Seed:4029+Index})));
    for(let X=-280;X<=280;X+=35)
    for(let Z=-200;Z<=200;Z+=40)
    {
        if(Math.hypot(X,Z)>280)continue;
        Assert.ok(VolumeDistance(Layer,X,-1,Z)<0,'foundation must not disappear');
        let Empty=false;
        for(let Y=-79;Y<240;Y+=2)
        {
            const Distance=VolumeDistance(Layer,X,Y,Z);
            if(Distance>0)Empty=true;else Assert.equal(Empty,false,`disconnected solid at ${X},${Y},${Z}`);
        }
    }
});
test('vertical noise remains three-dimensional but cannot fold the terrain field, even at maximum amplitude/frequency',()=>
{
    let DifferingProfiles=0;
    for(const Type of Object.keys(TerrainGeneratorCodes))
    {
        const Layer=CreateLayer([{...CreateLandform(Type),Amount:500,Scale:5,Sharpness:1}]);
        const Primitive=CompileRegion(Layer,Layer.Regions[0]).Steps[0].Primitives[0];
        Assert.ok(Primitive.VerticalFrequency*500*TerrainProfileGradient(Primitive.Category,1)<=.450000001);
        for(let Index=0;Index<40;Index++)
        {
            const X=Math.sin(Index*1.3)*200,Z=Math.cos(Index*.7)*150,Y=Index*13-150,ε=.01;
            const Before=TerrainProfile(Primitive,X,Y-ε,Z),After=TerrainProfile(Primitive,X,Y+ε,Z);
            Assert.ok(Math.abs((After-Before)*500/(2*ε))<.451);
            if(Math.abs(After-Before)>1e-10)DifferingProfiles++;
        }
    }
    Assert.ok(DifferingProfiles>250,'profiles must evaluate 3D coordinates rather than a 2D height texture');
});
test('radial stack fields remain conservative under every blend mode, narrow falloffs and mixed explicit SDFs',()=>
{
    for(const Mode of ['union','subtract','intersect','blend'])
    {
        const Layer=CreateLayer([{...CreateLandform('dunes'),InfluenceRadius:.5,InfluenceFalloff:.05},CreateLandform('arch'),{...CreateLandform('badlands'),Mode,InfluenceX:.3,InfluenceFalloff:.15}]);
        for(let Index=0;Index<200;Index++)
        {
            const Point=[Math.sin(Index*1.17)*280,Math.sin(Index*.73)*180,Math.cos(Index*.99)*280],ε=.001;
            const Gradient=Point.map((Coordinate,Axis)=>{const Before=[...Point],After=[...Point];Before[Axis]-=ε;After[Axis]+=ε;return (VolumeDistance(Layer,...After)-VolumeDistance(Layer,...Before))/(2*ε);});
            Assert.ok(Math.hypot(...Gradient)<1.001,`${Mode}: ${Math.hypot(...Gradient)}`);
        }
    }
});
test('radial controls invalidate compilation, move support, bypass at zero strength and round-trip with the document',()=>
{
    const Operation={...CreateLandform('fractal'),InfluenceRadius:.4,InfluenceX:-.4},Layer=CreateLayer([Operation]);
    Assert.ok(Surface(Layer,-128,0)>0);Assert.ok(Math.abs(Surface(Layer,128,0))<1e-8);
    let Compiled=CompileRegion(Layer,Layer.Regions[0]);
    for(const Property of Object.keys(TerrainInfluenceDefaults))
    {
        Operation[Property]+=(Property==='Influence'||Property==='InfluenceFalloff'?-.1:.1);
        const Next=CompileRegion(Layer,Layer.Regions[0]);Assert.notEqual(Next,Compiled,Property);Compiled=Next;
    }
    Operation.InfluenceX=.4;Assert.ok(Surface(Layer,128,0)>0);
    Operation.Influence=0;Assert.ok(Math.abs(Surface(Layer,128,0))<1e-8);
    const Document={Format:'FrontierLandscape/1',Layers:[Layer]};Assert.deepEqual(ValidateDocument(JSON.parse(JSON.stringify(Document))),Document);
});
test('legacy operations acquire radial defaults without mutating JSON; malformed optional radial fields are rejected',()=>
{
    const Operation=CreateLandform('dunes');for(const Property of Object.keys(TerrainInfluenceDefaults))delete Operation[Property];
    const Layer=CreateLayer([Operation]),Before=JSON.stringify(Layer);CompileRegion(Layer,Layer.Regions[0]);Assert.equal(JSON.stringify(Layer),Before);
    for(const [Property,Amount] of [['Influence',1.1],['InfluenceRadius',0],['InfluenceFalloff',0],['InfluenceX',2],['InfluenceZ',NaN]])
    {
        const Invalid=structuredClone(Layer);Invalid.Landforms[0][Property]=Amount;Assert.throws(()=>ValidateDocument({Format:'FrontierLandscape/1',Layers:[Invalid]}),/Invalid radial/);
    }
    Assert.equal(EvaluateRegionStack(Layer,Layer.Regions[0],0,0,0,0)<0,true);
});

test('foundation starts exactly at mean sea level and all thickness extends below it',()=>
{
    for(const Depth of [30,100,300,3000])
    {
        const Layer=CreateLayer([]);Layer.Depth=Depth;
        for(const [X,Z] of [[0,0],[100,100],[-200,0]])
        {
            Assert.equal(VolumeDistance(Layer,X,0,Z),0);
            Assert.ok(VolumeDistance(Layer,X,.1,Z)>0);
            Assert.ok(VolumeDistance(Layer,X,-.1,Z)<0);
            Assert.equal(VolumeDistance(Layer,X,-Depth,Z),0);
            Assert.ok(VolumeDistance(Layer,X,-Depth-.1,Z)>0);
        }
    }
});
test('underwater depth changes never lift terrain or create an above-water pedestal',()=>
{
    const Layer=CreateLayer([{...CreateLandform('dunes'),InfluenceRadius:.5}]);
    const Heights=[Surface(Layer,0,0),Surface(Layer,50,40),Surface(Layer,210,0)];
    for(const Depth of [30,120,3000])
    {
        Layer.Depth=Depth;
        const Observed=[Surface(Layer,0,0),Surface(Layer,50,40),Surface(Layer,210,0)];
        Observed.forEach((Height,Index)=>Assert.ok(Math.abs(Height-Heights[Index])<1e-8));
        Assert.ok(Math.abs(Observed[2])<1e-8,'outside influence must be at sea level, not on a raised sole');
    }
});
