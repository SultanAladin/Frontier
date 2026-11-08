/*==========================================================================================================================================
                                                            DISTANCEFIELD.JS
==========================================================================================================================================*/
// 🧩 Vector capsule distance evaluation and bounded volumetric sampling.

import { EvaluateRegionStack, SmoothIntersection, ValidateRegionStack, LayerVerticalBounds, InvalidateLandforms, ValidatePrimitiveBudget, ValidateLayerStack, CompileRegion } from './LandformStack.js';
import { ValidateLatticeDimensions } from './VolumeLattice.js';
import { EvaluateSculptStrokes, ValidateSculptStrokes, MaximumSculptSamples } from './VolumeSculpt.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                   DISTANCE EVALUATION
//------------------------------------------------------------------------------------------------------------------------

export function CapsuleDistance(X, Z, A, B, Radius)
{
    const ΔX = B[0] - A[0];
    const ΔZ = B[1] - A[1];
    const Projection = Math.max(0, Math.min(1, ((X - A[0]) * ΔX + (Z - A[1]) * ΔZ) / (ΔX * ΔX + ΔZ * ΔZ || 1)));
    return Math.hypot(X - A[0] - Projection * ΔX, Z - A[1] - Projection * ΔZ) - Radius;
}

export function MaskDistance(Layer, X, Z)
{
    let Distance = 1e8;
    for (const Stroke of Layer.Strokes)
    {
        let StrokeDistance = 1e8;
        for (let Index = 0; Index < Stroke.Points.length; Index++)
        {
            StrokeDistance = Math.min(StrokeDistance, CapsuleDistance(X, Z, Stroke.Points[Math.max(0, Index - 1)], Stroke.Points[Index], Stroke.Radius));
        }
        Distance = Stroke.Erase ? Math.max(Distance, -StrokeDistance) : Math.min(Distance, StrokeDistance);
    }
    return Distance;
}

export function RectangleDistance(Region, X, Z)
{
    const ΔX = Math.abs(X - (Region.X0 + Region.X1) / 2) - (Region.X1 - Region.X0) / 2;
    const ΔZ = Math.abs(Z - (Region.Z0 + Region.Z1) / 2) - (Region.Z1 - Region.Z0) / 2;
    return Math.hypot(Math.max(ΔX, 0), Math.max(ΔZ, 0)) + Math.min(Math.max(ΔX, ΔZ), 0);
}

// 📝 Unnormalized residuals retain the same zero set and supply stable analytic interface normals for metric baking.
export function VolumeDistance(
    Layer,
    X,
    Y,
    Z,
    Boundary = null,
    NormalizeEnabled = true)
{
    if (!Layer.Visible || !Layer.Regions.length) return 1e8;
    const Mask = MaskDistance(Layer,X,Z);
    const Vertical = Math.abs(Y+Layer.Depth*0.5)-Layer.Depth*0.5;
    let Distance = 1e8;
    for (const Region of Boundary ? [Boundary] : Layer.Regions)
    {
        const Edge = Region.Edge || 0;
        const Radius = Math.min(Edge,(Region.X1-Region.X0)*0.49,(Region.Z1-Region.Z0)*0.49);
        const RoundedRegion = {X0:Region.X0+Radius,Z0:Region.Z0+Radius,X1:Region.X1-Radius,Z1:Region.Z1-Radius};
        const Clip = RectangleDistance(RoundedRegion,X,Z)-Radius;
        const Planar = Math.max(Mask,Clip);
        let RegionDistance = Math.hypot(Math.max(Planar,0),Math.max(Vertical,0))+Math.min(Math.max(Planar,Vertical),0);
        RegionDistance = EvaluateRegionStack(Layer,Region,RegionDistance,X,Y,Z,Planar,NormalizeEnabled && !Region.SculptStrokes?.length);
        RegionDistance = (CompileRegion(Layer,Region).TerrainEnabled || !CompileRegion(Layer,Region).Steps.length) ? Math.max(RegionDistance,Planar) : SmoothIntersection(RegionDistance,Planar,Edge);
        if (Region.SculptStrokes?.length)
        {
            RegionDistance = Math.max(EvaluateSculptStrokes(Region, RegionDistance, X, Y, Z), Planar);
            if (NormalizeEnabled) RegionDistance /= Math.max(1, CompileRegion(Layer, Region).FieldBound);
        }
        RegionDistance = Math.max(RegionDistance,-Y-Layer.Depth);
        Distance = Math.min(Distance,RegionDistance);
    }
    if (Layer.Caves)
    {
        const Region = Layer.Regions[0];
        const Cavity = Math.hypot(X-(Region.X0+Region.X1)/2,Y,Z-(Region.Z0+Region.Z1)/2)-Layer.Depth*0.65;
        Distance = Math.max(Distance,-Cavity);
    }
    return Distance;
}

export function SegmentCount(Layers)
{
    return Layers.reduce((Count, Layer) => Count + Layer.Strokes.reduce((Total, Stroke) => Total + Stroke.Points.length, 0), 0);
}

export function ValidateDocument(Document)
{
    if (Document.Format !== 'FrontierLandscape/1' || !Array.isArray(Document.Layers) || Document.Layers.length > 16 || !Document.Layers.length) throw Error('Unsupported landscape document');
    let SculptSamples = 0;
    for (const Layer of Document.Layers)
    {
        ValidateLayerStack(Layer);
        if (Layer.DistanceMode !== undefined && !['bound', 'metric'].includes(Layer.DistanceMode)) throw Error('Invalid volume distance mode');
        if (typeof Layer.Name !== 'string' || !/^#[0-9a-f]{6}$/i.test(Layer.Color) || typeof Layer.Visible !== 'boolean' || !Number.isFinite(Layer.Depth) || Layer.Depth < 1 || Layer.Depth > 10000 || !Array.isArray(Layer.Strokes) || !Array.isArray(Layer.Regions) || Layer.Regions.length > 16) throw Error('Invalid layer');
        if (![256,512,1024,2048,4096].includes(Layer.Resolution) || ![64,96,128,256,512].includes(Layer.Vertical)) throw Error('Invalid requested resolution');
        for (const Stroke of Layer.Strokes)
        {
            if (!Number.isFinite(Stroke.Radius) || Stroke.Radius <= 0 || !Array.isArray(Stroke.Points) || !Stroke.Points.length || Stroke.Points.some(Point => !Array.isArray(Point) || Point.length !== 2 || Point.some(Coordinate => !Number.isFinite(Coordinate) || Math.abs(Coordinate) > 1e9))) throw Error('Invalid stroke');
        }
        for (const Region of Layer.Regions)
        {
            ValidateRegionStack(Region);
            SculptSamples += ValidateSculptStrokes(Region);
            if (Region.Resolution !== undefined) ValidateLatticeDimensions(Region.Resolution);
            if (['X0', 'X1', 'Z0', 'Z1'].some(Key => !Number.isFinite(Region[Key])) || Region.X1 <= Region.X0 || Region.Z1 <= Region.Z0) throw Error('Invalid bounds');
        }
    }
    if (SculptSamples > MaximumSculptSamples) throw Error(`Maximum ${MaximumSculptSamples} XYZ sculpt samples per document.`);
    if (SegmentCount(Document.Layers) > 512) throw Error('This GPU prototype supports 512 capsule samples. Split the document into smaller projects.');
    InvalidateLandforms();
    ValidatePrimitiveBudget(Document.Layers);
    return Document;
}

export function SampleSparseBricks(Layer, Region, Horizontal = 64, Vertical = 32)
{
    const Bricks = [];
    const BrickWidth = 8;
    const [MinimumY,MaximumY] = LayerVerticalBounds(Layer,Region);
    for (let BZ = 0; BZ < Horizontal; BZ += BrickWidth)
    for (let BY = 0; BY < Vertical; BY += BrickWidth)
    for (let BX = 0; BX < Horizontal; BX += BrickWidth)
    {
        const Samples = [];
        let Occupied = false;
        for (let Z = BZ; Z < Math.min(BZ + BrickWidth, Horizontal); Z++)
        for (let Y = BY; Y < Math.min(BY + BrickWidth, Vertical); Y++)
        for (let X = BX; X < Math.min(BX + BrickWidth, Horizontal); X++)
        {
            const Distance = VolumeDistance(Layer, Region.X0 + (X + 0.5) / Horizontal * (Region.X1 - Region.X0), MinimumY + (Y + 0.5) / Vertical * (MaximumY-MinimumY), Region.Z0 + (Z + 0.5) / Horizontal * (Region.Z1 - Region.Z0),Region);
            Samples.push(Distance);
            if (Distance < 0) Occupied = true;
        }
        if (Occupied) Bricks.push({ Origin: [BX, BY, BZ], Samples });
    }
    return { Format: 'FrontierSparseSDF/1', Dimensions: [Horizontal, Vertical, Horizontal], BrickWidth, Region, VerticalBounds: [MinimumY, MaximumY], SampleOrder: 'X fastest, then Y, then Z; cell centres; absent bricks are empty', Bricks };
}
