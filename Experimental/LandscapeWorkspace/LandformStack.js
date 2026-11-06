/*==========================================================================================================================================
                                                            LANDFORMSTACK.JS
==========================================================================================================================================*/
// 🧩 Mask-scoped volumetric landform compilation and conservative signed CSG evaluation.

//------------------------------------------------------------------------------------------------------------------------
//                                                      OPERATION CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

import { TerrainCategories,TerrainNoise,TerrainGradientBound } from './TerrainNoise.js';
import { TerrainGeneratorCodes,TerrainInfluenceDefaults,RadialWeight,TerrainProfile,TerrainProfileGradient } from './TerrainInfluence.js';

export const LandformCatalogue = [
    { Type:'ridges', Name:'Arête ridges', Amount:48, Scale:2.2, Sharpness:0.60, Azimuth:28, Mode:'union', Reference:'mountain-ridges / generator 0' },
    { Type:'mesa', Name:'Mesa / plateau', Amount:48, Scale:2.2, Sharpness:0.60, Azimuth:28, Mode:'union', Reference:'mountain-ridges / generator 1' },
    { Type:'fault', Name:'Fault escarpment', Amount:48, Scale:2.2, Sharpness:0.60, Azimuth:28, Mode:'union', Reference:'mountain-ridges / generator 2' },
    { Type:'dunes', Name:'Aeolian dunes', Amount:48, Scale:2.2, Sharpness:0.60, Azimuth:28, Mode:'union', Reference:'mountain-ridges / generator 3' },
    { Type:'craters', Name:'Impact crater bowls', Amount:48, Scale:2.2, Sharpness:0.60, Azimuth:28, Mode:'subtract', Reference:'mountain-ridges / generator 4' },
    ...Object.keys(TerrainCategories).map(Type=>({Type,Name:{fractal:'Fractal (fBm)',multifractal:'Multifractal','ridged-multifractal':'Ridged multifractal',badlands:'Badlands','ridge-noise':'Ridge noise','fractal-ridges':'Fractal ridges','sand-dunes':'Sand dunes'}[Type],Amount:140,Scale:2,Sharpness:.5,Azimuth:0,Mode:'union',Reference:'Local volumetric lattice-noise adaptation'})),
    { Type:'stacks', Name:'Rock stacks & hoodoos', Amount:48, Scale:2.2, Sharpness:0.42, Azimuth:35, Mode:'union', Reference:'rock-stacks' },
    { Type:'strata', Name:'Stratigraphic ledges', Amount:48, Scale:2.2, Sharpness:0.60, Azimuth:28, Mode:'union', Reference:'stratigraphic-beds' },
    { Type:'tunnel', Name:'SDF caves & undercuts', Amount:24, Scale:2.3, Sharpness:0.52, Azimuth:28, Mode:'subtract', Reference:'sdf-caves' },
    { Type:'arch', Name:'Volumetric arch', Amount:80, Scale:1.5, Sharpness:0.5, Azimuth:0, Mode:'union', Reference:'Additional torus SDF shape' }
];
export const CombineModes = ['union','subtract','intersect','blend'];
export const MaximumPrimitives = 2048;
const CatalogueByType = new Map(LandformCatalogue.map(Entry => [Entry.Type,Entry]));
let CompilationCache = new WeakMap();
const OperationProperties = ['Type','Enabled','Amount','Scale','Sharpness','Azimuth','Mode','Blend','Seed',...Object.keys(TerrainInfluenceDefaults)];

export function InvalidateLandforms()
{
    CompilationCache = new WeakMap();
}

export function CreateLandform(Type)
{
    const Entry = CatalogueByType.get(Type);
    if (!Entry) throw Error('Unknown landform');
    return { Type, Enabled:true, Amount:Entry.Amount, Scale:Entry.Scale, Sharpness:Entry.Sharpness, Azimuth:Entry.Azimuth, Mode:Entry.Mode, Blend:8, Seed:4029,...(TerrainGeneratorCodes[Type]!==undefined?TerrainInfluenceDefaults:{}) };
}

export function ValidateRegionStack(Region)
{
    if (Region.Edge !== undefined && (!Number.isFinite(Region.Edge) || Region.Edge < 0 || Region.Edge > 300)) throw Error('Invalid boundary edge width');
    if (Region.Landforms === undefined) return;
    if (!Array.isArray(Region.Landforms) || Region.Landforms.length > 8) throw Error('Maximum eight landforms per boundary');
    for (const Operation of Region.Landforms)
    {
        if(Operation?.Name!==undefined&&(typeof Operation.Name!=='string'||!Operation.Name.trim()||Operation.Name.length>96))throw Error('Invalid operation name');
        if (!Operation || !CatalogueByType.has(Operation.Type) || typeof Operation.Enabled !== 'boolean' || !CombineModes.includes(Operation.Mode)) throw Error('Invalid landform operation');
        for (const [Property,Minimum,Maximum] of [['Amount',1,500],['Scale',0.5,5],['Sharpness',0,1],['Azimuth',-180,180],['Blend',0,100],['Seed',0,1000000]])
        {
            if (!Number.isFinite(Operation[Property]) || Operation[Property] < Minimum || Operation[Property] > Maximum) throw Error(`Invalid landform ${Property}`);
        }
        for(const [Property,Minimum,Maximum] of [['Influence',0,1],['InfluenceRadius',.05,2],['InfluenceFalloff',.05,1],['InfluenceX',-1,1],['InfluenceZ',-1,1]])
        {if(Operation[Property]!==undefined&&(!Number.isFinite(Operation[Property])||Operation[Property]<Minimum||Operation[Property]>Maximum))throw Error(`Invalid radial ${Property}`);}
        if (!Number.isInteger(Operation.Seed)) throw Error('Landform seed must be an integer');
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SIGNED VOLUME PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

export function SmoothUnion(A, B, Width)
{
    if (Width <= 0) return Math.min(A,B);
    const Interpolation = Math.max(Width - Math.abs(A-B),0) / Width;
    return Math.min(A,B) - Interpolation * Interpolation * Width * 0.25;
}

export function SmoothIntersection(A, B, Width)
{
    return -SmoothUnion(-A,-B,Width);
}

export function CombineDistance(Distance, Feature, Mode, Width)
{
    if (Mode === 'subtract') return SmoothIntersection(Distance,-Feature,Width);
    if (Mode === 'intersect') return SmoothIntersection(Distance,Feature,Width);
    if (Mode === 'blend') return (Distance+Feature)*0.5;
    return SmoothUnion(Distance,Feature,Width);
}

export function PrimitiveDistance(Primitive, X, Y, Z)
{
    const ΔX = X-Primitive.Centre[0];
    let ΔY = Y-Primitive.Centre[1];
    const ΔZ = Z-Primitive.Centre[2];
    const LocalX = ΔX*Primitive.Cosine+ΔZ*Primitive.Sine;
    const LocalZ = -ΔX*Primitive.Sine+ΔZ*Primitive.Cosine;
    const [RadiusX,RadiusY,RadiusZ] = Primitive.Radii;
    if(Primitive.Category>=26)
    {
        const Bound=1+RadiusY*Primitive.Bevel*TerrainProfileGradient(Primitive.Category,Primitive.Detail);
        return (ΔY-RadiusY*TerrainProfile(Primitive,X,Y,Z))/Bound;
    }
    if(Primitive.Category>=6)
    {
        const Frequency=Primitive.Bevel;
        const Noise=TerrainNoise(LocalX*Frequency,ΔY*Frequency*.2,LocalZ*Frequency,Primitive.Category,Primitive.Phase,Primitive.Detail);
        const Bound=1+RadiusY*.85*Frequency*TerrainGradientBound(Primitive.Category,Primitive.Detail);
        return Math.max((ΔY-RadiusY*(.15+.85*Noise))/Bound,ΔY-RadiusY);
    }
    const Frequency = 2/Math.min(RadiusX,RadiusY,RadiusZ);
    const Detail = Primitive.Detail || 0;
    const Phase = Primitive.Phase || 0;
    const First = Math.sin(LocalX*Frequency+Phase)*Math.sin(LocalZ*Frequency-Phase)*Math.sin(ΔY*Frequency*.7+Phase);
    const Second = Math.sin(LocalX*Frequency*2-Phase)*Math.sin(LocalZ*Frequency*2+Phase)*Math.sin(ΔY*Frequency*1.4);
    ΔY += Detail*(First+Second*.35);
    const Bound = 1+Detail*Frequency*Math.sqrt(2.49)*1.7;
    if (Primitive.Category === 0)
    {
        // 📝 Conservative ellipsoid distance: unlike the reference's approximation, its Lipschitz bound is one.
        return (Math.hypot(LocalX/RadiusX,ΔY/RadiusY,LocalZ/RadiusZ)-1)*Math.min(RadiusX,RadiusY,RadiusZ)/Bound;
    }
    if (Primitive.Category === 1)
    {
        const QX = Math.abs(LocalX)-RadiusX;
        const QY = Math.abs(ΔY)-RadiusY;
        const QZ = Math.abs(LocalZ)-RadiusZ;
        return (Math.hypot(Math.max(QX,0),Math.max(QY,0),Math.max(QZ,0)) + Math.min(Math.max(QX,QY,QZ),0)-Primitive.Bevel)/Bound;
    }
    if (Primitive.Category === 2)
    {
        const Radial = Math.hypot(LocalX/RadiusX,LocalZ/RadiusZ);
        const Cone = (Radial+ΔY/RadiusY-1)/Math.hypot(1/Math.min(RadiusX,RadiusZ),1/RadiusY);
        return Math.max(Cone,-ΔY-RadiusY)/Bound;
    }
    if (Primitive.Category === 5)
    {
        // 📝 Normalised half-space intersection follows the pinned polyhedron evaluator's geometric mechanism.
        const SlopeX = RadiusY/RadiusX;
        const SlopeZ = RadiusY/RadiusZ;
        return Math.max((Math.abs(LocalX)*SlopeX+ΔY-RadiusY)/Math.hypot(SlopeX,1),
            (Math.abs(LocalZ)*SlopeZ+ΔY-RadiusY)/Math.hypot(SlopeZ,1),-ΔY-RadiusY)/Bound;
    }
    if (Primitive.Category === 3)
    {
        return (Math.hypot(Math.hypot(LocalX,ΔY)-RadiusX,LocalZ)-RadiusZ)/Bound;
    }
    // Capsule oriented along local X; RadiusX is the half-length, RadiusY is the cross-section radius.
    return (Math.hypot(LocalX-Math.max(-RadiusX,Math.min(RadiusX,LocalX)),ΔY,LocalZ)-RadiusY)/Bound;
}

function SeedSequence(Seed)
{
    let Sequence = Seed >>> 0;
    return () =>
    {
        Sequence = (Math.imul(Sequence,1664525)+1013904223) >>> 0;
        return Sequence/4294967296;
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      REGION COMPILATION
//------------------------------------------------------------------------------------------------------------------------

export function CompileRegion(Layer, Region)
{
    const Operations = Layer.Landforms?.length ? [...Layer.Landforms,...(Region.Landforms||[])] : (Region.Landforms||[]);
    const Cached = CompilationCache.get(Region);
    if (Cached && Cached.Depth === Layer.Depth && Cached.Bounds[0] === Region.X0 && Cached.Bounds[1] === Region.Z0 && Cached.Bounds[2] === Region.X1 && Cached.Bounds[3] === Region.Z1)
    {
        let Unchanged = Cached.Operations.length === Operations.length;
        for (let Index=0;Unchanged && Index<Operations.length;Index++)
        for (const Property of OperationProperties)
        {
            if (Cached.Operations[Index][Property] !== Operations[Index][Property]) {Unchanged=false;break;}
        }
        if (Unchanged) return Cached;
    }
    const Steps = [];
    const SpanX = Region.X1-Region.X0;
    const SpanZ = Region.Z1-Region.Z0;
    const CentreX = (Region.X0+Region.X1)/2;
    const CentreZ = (Region.Z0+Region.Z1)/2;
    const Top = 0; // [m] - Foundation surface coincides with mean ocean level; thickness extends downward.
    let MaximumY = Top;
    let MinimumY = -Layer.Depth;
    let VerticalSlope = 1,FieldBound=1,EnvelopeSlope=1,EnvelopeTop=Top,TerrainEnabled=false;
    for (const Operation of Operations)
    {
        if (!Operation.Enabled) continue;
        const Random = SeedSequence(Operation.Seed);
        const Angle = Operation.Azimuth*Math.PI/180;
        const Cosine = Math.cos(Angle);
        const Sine = Math.sin(Angle);
        const Primitives = [];
        let FeatureMaximum = -1e8;
        let FeatureMinimum = 1e8;
        let FeatureSlope = 1;
        const Count = Operation.Type === 'tunnel' || Operation.Type === 'arch' || Operation.Type === 'fault' ? 1 : Math.min(4,Math.max(1,Math.round(Operation.Scale)));
        const CellX = SpanX/Count;
        const CellZ = SpanZ/Count;
        const Add = (Category,Centre,Radii,Bevel = 0) =>
        {
            const Detail = ['arch','tunnel'].includes(Operation.Type) ? 0 : Math.min(...Radii)*(Operation.Type==='ridges'?.28:.12)*Operation.Sharpness;
            const Phase = (Operation.Seed%997)*0.1;
            Primitives.push({Category,Centre,Radii,Cosine,Sine,Bevel,Detail,Phase});
            // Conservative bounds, including smoothing expansion and any rotated support.
            const Radius = (Category === 3 ? Radii[0]+Radii[2] : Radii[1])+Bevel+Detail*1.35;
            FeatureMaximum = Math.max(FeatureMaximum,Centre[1]+Radius);
            FeatureMinimum = Math.min(FeatureMinimum,Centre[1]-Radius);
            const Bound = 1+Detail*(2/Math.min(...Radii))*Math.sqrt(2.49)*1.7;
            const Slope = Category === 0 ? Math.min(...Radii)/Radii[1] : Category === 2 || Category === 5 ? 1/Math.hypot(1,Radii[1]/Math.min(Radii[0],Radii[2])) : 1;
            FeatureSlope = Math.min(FeatureSlope,Slope/Bound);
        };
        if(TerrainGeneratorCodes[Operation.Type]!==undefined)
        {
            TerrainEnabled=true;const Settings={...TerrainInfluenceDefaults,...Operation};
            const Category=TerrainGeneratorCodes[Operation.Type],Frequency=Operation.Scale*3/Math.min(SpanX,SpanZ),Radius=Math.min(SpanX,SpanZ)*.5*Settings.InfluenceRadius;
            const Primitive={Category,Centre:[CentreX+Settings.InfluenceX*SpanX*.5,Top,CentreZ+Settings.InfluenceZ*SpanZ*.5],Radii:[Radius,Operation.Amount,Settings.InfluenceFalloff],Cosine,Sine,Bevel:Frequency,Detail:Operation.Sharpness,Phase:Operation.Seed,Influence:Settings.Influence};
            // 📝 Bound vertical noise distortion below 0.45: terrain-only stacks cannot fold into disconnected slabs.
            Primitive.VerticalFrequency=Math.min(Frequency*.2,.45/(Operation.Amount*TerrainProfileGradient(Category,Operation.Sharpness)));
            Primitive.FieldBound=1+Operation.Amount*Frequency*TerrainProfileGradient(Category,Operation.Sharpness);
            Primitives.push(Primitive);FeatureMaximum=Top+Operation.Amount;FeatureMinimum=-Layer.Depth;
            const Limit=Operation.Amount+Operation.Blend*.25,Gradient=Settings.Influence*1.5/(Radius*Settings.InfluenceFalloff);
            if(Settings.Influence>0)FieldBound=Math.max(FieldBound,1+Operation.Amount*Frequency*TerrainProfileGradient(Category,Operation.Sharpness))+Limit*Gradient;
            EnvelopeSlope+=Limit*Gradient;
        }
        else for (let Z = 0; Z < Count; Z++)
        for (let X = 0; X < Count; X++)
        {
            const PositionX = Region.X0+(X+0.5+(Random()-0.5)*0.5)*CellX;
            const PositionZ = Region.Z0+(Z+0.5+(Random()-0.5)*0.5)*CellZ;
            const Height = Operation.Amount*(0.65+Random()*0.35);
            const Radius = Math.min(CellX,CellZ)*(0.3+Random()*0.15);
            if (Operation.Type === 'ridges')
            {
                Add(2,[PositionX,Top-Height*0.15,PositionZ],[Radius*1.7,Height,Radius*1.35]);
                for (let Spur=0;Spur<2;Spur++)
                {
                    const SpurAngle=Random()*Math.PI*2;
                    Add(2,[PositionX+Math.cos(SpurAngle)*Radius,Top-Height*.1,PositionZ+Math.sin(SpurAngle)*Radius*.8],
                        [Radius,Height*(.35+Random()*.3),Radius*.85]);
                }
            }
            else if (Operation.Type === 'mesa') Add(1,[PositionX,Top+Height*0.35,PositionZ],[Radius,Height*0.55,Radius*0.8],Height*(1-Operation.Sharpness)*0.15);
            else if (Operation.Type === 'fault') Add(5,[CentreX,Top,CentreZ],[SpanX*.65,Height,SpanZ*.45]);
            else if (Operation.Type === 'dunes') Add(0,[PositionX,Top-Height*.3,PositionZ],[Radius*1.65,Height,Radius*.65]);
            else if (Operation.Type === 'craters') Add(0,[PositionX,Top+Height*.15,PositionZ],[Radius,Height*.65,Radius]);
            else if (Operation.Type === 'stacks')
            {
                Add(1,[PositionX,Top+Height*.35,PositionZ],[Radius*.3,Height*.6,Radius*.3],Height*.04);
                Add(0,[PositionX+Height*.1,Top+Height*.9,PositionZ],[Radius*.6,Height*.23,Radius*.6]);
            }
            else if (Operation.Type === 'strata')
            {
                for (let Tier = 0; Tier < 4; Tier++) Add(1,[PositionX,Top+Height*(Tier+.3)*.23,PositionZ],
                    [Radius*(Tier%2 ? .85 : 1.1),Height*.12,Radius*(Tier%2 ? .85 : 1.1)],Height*.025);
            }
            else if (Operation.Type === 'tunnel') Add(4,[CentreX,Top-Height*.2,CentreZ],[Math.hypot(SpanX,SpanZ),Height*.5,Height*.5]);
            else Add(3,[CentreX,Top+Height*.35,CentreZ],[Height*.65,Height*.65,Height*.18]);
        }
        if (Operation.Mode === 'union' || Operation.Mode === 'blend')
        {
            VerticalSlope = Math.min(VerticalSlope,FeatureSlope);
            // 📝 Smooth CSG width is measured in conservative field units, not necessarily Euclidean metres.
            const Expansion = Operation.Mode === 'union' ? Operation.Blend/(4*VerticalSlope) : 0;
            MaximumY = Math.max(MaximumY,FeatureMaximum)+Expansion;
            MinimumY = Math.min(MinimumY,FeatureMinimum)-Expansion;
        }
        if(TerrainGeneratorCodes[Operation.Type]===undefined&&(Operation.Mode==='union'||Operation.Mode==='blend'))EnvelopeTop=Math.max(EnvelopeTop,FeatureMaximum)+Operation.Blend/(4*FeatureSlope);
        Steps.push({Mode:Operation.Mode,Blend:Operation.Blend,Primitives});
    }
    const Result = {Depth:Layer.Depth,TerrainEnabled,FieldBound,EnvelopeSlope,EnvelopeTop,Steps,MaximumY,MinimumY,Bounds:[Region.X0,Region.Z0,Region.X1,Region.Z1],Operations:Operations.map(Operation=>({...Operation}))};
    CompilationCache.set(Region,Result);
    return Result;
}

export function EvaluateRegionStack(Layer, Region, Distance, X, Y, Z)
{
    const Compilation=CompileRegion(Layer,Region);
    let Envelope=Compilation.EnvelopeTop;
    if(Compilation.TerrainEnabled)Distance=Y;
    for (const Step of Compilation.Steps)
    {
        const Terrain=Step.Primitives[0];
        if(Terrain?.Category>=26)
        {
            const Weight=RadialWeight(Terrain,X,Z),Limit=Terrain.Radii[1]+Step.Blend*.25;
            if(Weight>0)
            {
                const Relief=Terrain.Radii[1]*TerrainProfile(Terrain,X,Y,Z);
                const Feature=Y-Terrain.Centre[1]+(Step.Mode==='subtract'?Relief:-Relief);
                const Candidate=Step.Mode==='subtract'||Step.Mode==='intersect'?SmoothIntersection(Distance,Feature,Step.Blend):Step.Mode==='blend'?(Distance+Feature)*.5:SmoothUnion(Distance,Feature,Step.Blend);
                Distance+=Weight*Math.max(-Limit,Math.min(Limit,Candidate-Distance));
                Envelope+=Limit*Weight;
            }
        }
        else
        {
            let Feature = 1e8;
            for (const Primitive of Step.Primitives) Feature = Math.min(Feature,PrimitiveDistance(Primitive,X,Y,Z));
            Distance = CombineDistance(Distance,Feature,Step.Mode,Step.Blend);
        }
    }
    if(Compilation.TerrainEnabled)Distance=Math.max(Distance/Compilation.FieldBound,(Y-Envelope)/Compilation.EnvelopeSlope);
    return Distance;
}

export function LayerVerticalBounds(Layer, Region)
{
    const Compilation = CompileRegion(Layer,Region);
    if (!Compilation.Steps.length) return [-Layer.Depth*1.1,Layer.Depth*.1];
    return [Compilation.MinimumY-Layer.Depth*.1,Compilation.MaximumY+Layer.Depth*.1];
}

export function RegionScopes(Layers, Selected)
{
    return Layers.flatMap((Layer,LayerIndex) => Selected.has(LayerIndex)
        ? Layer.Regions.map((Region,RegionIndex) => ({Layer,Region,LayerIndex,RegionIndex})) : []);
}

export function ValidatePrimitiveBudget(Layers)
{
    let Count = 0;
    for (const Layer of Layers)
    for (const Region of Layer.Regions)
    {
        if ((Layer.Landforms?.length||0)+(Region.Landforms?.length||0)>8) throw Error('Maximum eight combined mask-layer and legacy boundary operations');
        for (const Step of CompileRegion(Layer,Region).Steps) Count += Step.Primitives.length;
    }
    if (Count > MaximumPrimitives) throw Error(`Maximum ${MaximumPrimitives} compiled landform primitives per document`);
    return Count;
}

export function ValidateLayerStack(Layer)
{
    ValidateRegionStack({Landforms:Layer.Landforms});
}
