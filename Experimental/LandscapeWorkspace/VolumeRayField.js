/*==========================================================================================================================================
                                                           VOLUMERAYFIELD.JS
==========================================================================================================================================*/
// 🧩 Compiled volumetric field evaluation, bounded perspective rays and 3D surface intersections without a graphics API.

import { MaskDistance, RectangleDistance } from './DistanceField.js';
import { EvaluateSculptStrokes } from './VolumeSculpt.js';
import { CompileRegion, EvaluateCompiledRegionStack, LayerVerticalBounds, SmoothIntersection } from './LandformStack.js';

export const EmptyRayDistance = 1e8;

//------------------------------------------------------------------------------------------------------------------------
//                                                      IMMUTABLE FIELD SNAPSHOT
//------------------------------------------------------------------------------------------------------------------------

function BoxDistance(X, Y, Z, Minimum, Maximum)
{
    const ExtentX = Math.abs(X - (Minimum[0] + Maximum[0]) * .5) - (Maximum[0] - Minimum[0]) * .5;
    const ExtentY = Math.abs(Y - (Minimum[1] + Maximum[1]) * .5) - (Maximum[1] - Minimum[1]) * .5;
    const ExtentZ = Math.abs(Z - (Minimum[2] + Maximum[2]) * .5) - (Maximum[2] - Minimum[2]) * .5;
    return Math.hypot(Math.max(ExtentX, 0), Math.max(ExtentY, 0), Math.max(ExtentZ, 0)) + Math.min(Math.max(ExtentX, ExtentY, ExtentZ), 0);
}

export function CompileVolumeRayField(Layers, Selection = null)
{
    const Minimum = [Infinity, Infinity, Infinity], Maximum = [-Infinity, -Infinity, -Infinity];
    const Entries = [], SurfaceResidual = new Float64Array(1), Snapshot = structuredClone(Layers);
    Snapshot.forEach((Layer, LayerIndex) =>
    {
        if (!Layer.Visible || !Layer.Strokes.length) return;
        for (const [RegionIndex, Region] of Layer.Regions.entries())
        {
            if (Selection && (Selection.LayerIndex !== LayerIndex || Selection.RegionIndex !== RegionIndex)) continue;
            const Compilation = CompileRegion(Layer, Region);
            const Vertical = LayerVerticalBounds(Layer, Region);
            const Radius = Math.min(Region.Edge || 0, (Region.X1 - Region.X0) * .49, (Region.Z1 - Region.Z0) * .49);
            const Rounded = { X0: Region.X0 + Radius, Z0: Region.Z0 + Radius, X1: Region.X1 - Radius, Z1: Region.Z1 - Radius };
            const Lower = [Region.X0, Vertical[0], Region.Z0], Upper = [Region.X1, Vertical[1], Region.Z1];
            const Colour = [1, 3, 5].map(Offset => parseInt(Layer.Color.slice(Offset, Offset + 2), 16) / 255);
            Entries.push({ Layer, LayerIndex, Region, Compilation, Radius, Rounded, Lower, Upper, Colour });
            for (let Axis = 0; Axis < 3; Axis++)
            {
                Minimum[Axis] = Math.min(Minimum[Axis], Lower[Axis]);
                Maximum[Axis] = Math.max(Maximum[Axis], Upper[Axis]);
            }
        }
    });

    function Shore(X, Z)
    {
        let Distance = EmptyRayDistance;
        for (const Entry of Entries)
        {
            const Planar = Math.max(MaskDistance(Entry.Layer, X, Z), RectangleDistance(Entry.Rounded, X, Z) - Entry.Radius);
            Distance = Math.min(Distance, Planar);
        }
        return Distance;
    }

    function Evaluate(X, Y, Z, Exact = false, Result = new Float64Array(3))
    {
        let Nearest = EmptyRayDistance, Residual = EmptyRayDistance, LayerIndex = -1;
        for (const Entry of Entries)
        {
            const { Layer, Region, Compilation } = Entry;
            const Box = BoxDistance(X, Y, Z, Entry.Lower, Entry.Upper);
            if (!Exact && Box > Nearest) continue;
            const Planar = Math.max(MaskDistance(Layer, X, Z), RectangleDistance(Entry.Rounded, X, Z) - Entry.Radius);
            const Bottom = -Y - Layer.Depth;
            const Coarse = Math.max(Planar, Bottom, Box);
            if (!Exact && Coarse >= Nearest) continue;
            if (!Exact && Box > 2)
            {
                Nearest = Coarse;
                Residual = EmptyRayDistance;
                LayerIndex = Entry.LayerIndex;
                continue;
            }
            const Vertical = Math.abs(Y + Layer.Depth * .5) - Layer.Depth * .5;
            const Foundation = Math.hypot(Math.max(Planar, 0), Math.max(Vertical, 0)) + Math.min(Math.max(Planar, Vertical), 0);
            let Distance = EvaluateCompiledRegionStack(Compilation, Foundation, X, Y, Z, Planar, !Region.SculptStrokes?.length, SurfaceResidual);
            let Surface = SurfaceResidual[0];
            if (Compilation.TerrainEnabled || !Compilation.Steps.length)
            {
                Distance = Math.max(Distance, Planar);
                Surface = Math.max(Surface, Planar);
            }
            else
            {
                Distance = SmoothIntersection(Distance, Planar, Region.Edge || 0);
                Surface = Distance;
            }
            if (Region.SculptStrokes?.length)
            {
                Surface = Math.max(EvaluateSculptStrokes(Region, Surface, X, Y, Z), Planar);
                Distance = Surface / Math.max(1, Compilation.FieldBound);
            }
            Distance = Math.max(Distance, Bottom);
            Surface = Math.max(Surface, Bottom);
            if (Layer.Caves)
            {
                const First = Layer.Regions[0];
                const Cavity = -(Math.hypot(X - (First.X0 + First.X1) * .5, Y, Z - (First.Z0 + First.Z1) * .5) - Layer.Depth * .65);
                Distance = Math.max(Distance, Cavity);
                Surface = Math.max(Surface, Cavity);
            }
            if (Distance < Nearest) { Nearest = Distance; Residual = Surface; LayerIndex = Entry.LayerIndex; }
        }
        Result[0] = Nearest;
        Result[1] = LayerIndex;
        Result[2] = Residual;
        return Result;
    }

    return { Evaluate, Shore, Minimum, Maximum, Entries, Layers: Snapshot };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PERSPECTIVE AND INTERSECTIONS
//------------------------------------------------------------------------------------------------------------------------

export function NormalizeDirection(Direction)
{
    const Length = Math.hypot(...Direction) || 1;
    return Direction.map(Coordinate => Coordinate / Length);
}

export function ProjectVolumeCamera(Camera)
{
    const Offset = [Math.cos(Camera.Pitch) * Math.sin(Camera.Yaw), Math.sin(Camera.Pitch), Math.cos(Camera.Pitch) * Math.cos(Camera.Yaw)];
    const Eye = Camera.Target.map((Coordinate, Axis) => Coordinate + Camera.Orbit * Offset[Axis]);
    const Forward = NormalizeDirection(Offset.map(Coordinate => -Coordinate));
    const Right = NormalizeDirection([-Forward[2], 0, Forward[0]]);
    const Up = [Right[1] * Forward[2] - Right[2] * Forward[1], Right[2] * Forward[0] - Right[0] * Forward[2], Right[0] * Forward[1] - Right[1] * Forward[0]];
    return { Eye, Forward, Right, Up };
}

export function ProjectVolumeRay(
    Projection,
    X,
    Y,
    Width,
    Height)
{
    const ScreenX = ((X + .5) * 2 - Width) / Height;
    const ScreenY = (Height - (Y + .5) * 2) / Height;
    return NormalizeDirection(Projection.Forward.map((Coordinate, Axis) => Coordinate * 1.65 + Projection.Right[Axis] * ScreenX + Projection.Up[Axis] * ScreenY));
}

export function VolumeRayInterval(
    Eye,
    Ray,
    Minimum,
    Maximum)
{
    let Near = 0, Far = Infinity;
    for (let Axis = 0; Axis < 3; Axis++)
    {
        if (Minimum[Axis] > Maximum[Axis]) return null;
        if (Math.abs(Ray[Axis]) < 1e-10)
        {
            if (Eye[Axis] < Minimum[Axis] || Eye[Axis] > Maximum[Axis]) return null;
            continue;
        }
        const First = (Minimum[Axis] - Eye[Axis]) / Ray[Axis], Second = (Maximum[Axis] - Eye[Axis]) / Ray[Axis];
        Near = Math.max(Near, Math.min(First, Second));
        Far = Math.min(Far, Math.max(First, Second));
        if (Near > Far) return null;
    }
    return Far >= 0 ? [Near, Far] : null;
}

export function IntersectVolumeRay(
    Field,
    Eye,
    Ray,
    PixelHeight = 80,
    MaximumSteps = 1024)
{
    const Interval = VolumeRayInterval(Eye, Ray, Field.Minimum, Field.Maximum);
    if (!Interval) return { Found: false, Distance: EmptyRayDistance, Steps: 0 };
    const Sample = new Float64Array(3);
    let Travel = Interval[0], Previous = Travel;
    for (let Step = 0; Step < MaximumSteps && Travel <= Interval[1]; Step++)
    {
        const X = Eye[0] + Ray[0] * Travel, Y = Eye[1] + Ray[1] * Travel, Z = Eye[2] + Ray[2] * Travel;
        Field.Evaluate(X, Y, Z, false, Sample);
        const Distance = Sample[0], Residual = Sample[2];
        if (Residual <= .025)
        {
            let Lower = Previous, Upper = Travel;
            for (let Refinement = 0; Refinement < 10; Refinement++)
            {
                const Middle = (Lower + Upper) * .5;
                const Surface = Field.Evaluate(Eye[0] + Ray[0] * Middle, Eye[1] + Ray[1] * Middle, Eye[2] + Ray[2] * Middle, false, Sample)[2];
                if (Surface > 0) Lower = Middle;
                else Upper = Middle;
            }
            Travel = (Lower + Upper) * .5;
            const Position = Eye.map((Coordinate, Axis) => Coordinate + Ray[Axis] * Travel);
            const LayerIndex = Field.Evaluate(...Position, false, Sample)[1];
            const ε = Math.max(.12, Math.min(.65, Travel / PixelHeight * .15));
            const Gradient = [0, 1, 2].map(Axis =>
            {
                const Positive = [...Position], Negative = [...Position];
                Positive[Axis] += ε;
                Negative[Axis] -= ε;
                const Upper = Field.Evaluate(...Positive, true, Sample)[2];
                const Lower = Field.Evaluate(...Negative, true, Sample)[2];
                return Upper - Lower;
            });
            return { Found: true, Distance: Travel, Position, Normal: NormalizeDirection(Gradient), LayerIndex, Steps: Step + 1 };
        }
        const Footprint = Math.max(.08, Math.min(.45, Travel / PixelHeight * .24));
        let Length = Math.max(Distance * .85, Footprint);
        if (Distance < 3 && Residual < 1e6)
        {
            const Ahead = Field.Evaluate(X + Ray[0] * .65, Y + Ray[1] * .65, Z + Ray[2] * .65, false, Sample)[2];
            const Derivative = (Ahead - Residual) / .65;
            if (Derivative < -.05) Length = Math.max(Length, Math.min(Residual / -Derivative * .8, 3));
        }
        Previous = Travel;
        Travel += Length;
    }
    return { Found: false, Distance: EmptyRayDistance, Steps: MaximumSteps };
}
