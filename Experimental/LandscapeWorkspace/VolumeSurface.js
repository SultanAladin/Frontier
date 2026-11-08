/*==========================================================================================================================================
                                                               VOLUMESURFACE.JS
==========================================================================================================================================*/
// 🧩 Bounded, cancellable XYZ isosurface preparation. Camera frames never sample the procedural distance field.

import { CompileVolumeRayField } from './VolumeRayField.js';
import { EvaluateSculptStrokes } from './VolumeSculpt.js';
import { MaskDistance, RectangleDistance } from './DistanceField.js';
import { EvaluateCompiledRegionStack, SmoothIntersection } from './LandformStack.js';

export const MaximumSurfaceTriangles = 120000;
export const MaximumSurfaceSamples = 1100000;
export const MaximumSurfaceRegions = 256;
export const SurfacePreparationSliceMilliseconds = 6;
export const MaximumSurfaceShoreSamples = 256;

export class SurfaceCapacityExceeded extends Error
{
    constructor()
    {
        super('The preview surface reached its triangle budget. The last completed GPU surface is retained.');
        this.name = 'SurfaceCapacityExceeded';
    }
}

export const SurfaceCellCorners = [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0], [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]];
export const SurfaceCellTetrahedra = [[0, 1, 2, 6], [0, 2, 3, 6], [0, 3, 7, 6], [0, 7, 4, 6], [0, 4, 5, 6], [0, 5, 1, 6]];
const InspectMilliseconds = () => performance.now();
const YieldExecution = () => new Promise(Resolve => setTimeout(Resolve, 0));
const Clamp = (Coordinate, Minimum, Maximum) => Math.max(Minimum, Math.min(Maximum, Coordinate));

function RegionMinimum(Entry)
{
    return [Entry.Lower[0], Entry.Lower[1], Entry.Lower[2]];
}

function RegionMaximum(Entry)
{
    return [Entry.Upper[0], Entry.Upper[1], Entry.Upper[2]];
}

function IntersectSurfaceRegions(First, Second)
{
    return First.Minimum.every((Coordinate, Axis) => Coordinate <= Second.Maximum[Axis] && First.Maximum[Axis] >= Second.Minimum[Axis]);
}

function AccumulateRegion(First, Second)
{
    First.Minimum = First.Minimum.map((Coordinate, Axis) => Math.min(Coordinate, Second.Minimum[Axis]));
    First.Maximum = First.Maximum.map((Coordinate, Axis) => Math.max(Coordinate, Second.Maximum[Axis]));
    First.Entries.push(...Second.Entries);
}

export function PartitionSurfaceRegions(Field, Pitch)
{
    const Result = [];
    for (const Entry of Field.Entries)
    {
        const Region = {
            Minimum: RegionMinimum(Entry).map(Coordinate => (Math.floor(Coordinate / Pitch) - 1) * Pitch),
            Maximum: RegionMaximum(Entry).map(Coordinate => (Math.ceil(Coordinate / Pitch) + 1) * Pitch),
            Entries: [Entry]
        };
        // Merge transitive overlaps; disconnected worlds stay disconnected rather than allocating the empty space between them.
        for (let Index = 0; Index < Result.length;)
        {
            if (!IntersectSurfaceRegions(Region, Result[Index])) { Index++; continue; }
            AccumulateRegion(Region, Result.splice(Index, 1)[0]);
            Index = 0;
        }
        Result.push(Region);
    }
    for (const Region of Result)
    {
        Region.Cells = Region.Maximum.map((Coordinate, Axis) => Math.round((Coordinate - Region.Minimum[Axis]) / Pitch));
        Region.Samples = Region.Cells.map(Coordinate => Coordinate + 1);
        Region.Count = Region.Samples.reduce((Product, Coordinate) => Product * Coordinate, 1);
    }
    return Result;
}

export function PlanVolumeSurface(Field, Quality = 32)
{
    const Target = Clamp(Math.round(Number(Quality) || 32), 12, 96);
    if (!Field.Entries.length) return { Pitch: 1, Regions: [], Samples: 0, Quality: Target };
    if (Field.Entries.length > MaximumSurfaceRegions) throw Error('Too many allocated regions for GPU surface preparation.');
    const Longest = Math.max(...Field.Entries.map(Entry => Math.max(
        Entry.Upper[0] - Entry.Lower[0], Entry.Upper[2] - Entry.Lower[2])));
    if (!Number.isFinite(Longest) || Longest <= 0) throw Error('Invalid surface bounds.');
    let Pitch = Math.max(.05, Longest / Target);
    let Result, Samples;
    for (let Attempt = 0; Attempt < 64; Attempt++)
    {
        Result = PartitionSurfaceRegions(Field, Pitch);
        Samples = Result.reduce((Count, Region) => Count + Region.Count, 0);
        if (Number.isSafeInteger(Samples) && Samples <= MaximumSurfaceSamples) break;
        Pitch *= 1.25;
    }
    if (Result.length > MaximumSurfaceRegions || !Number.isSafeInteger(Samples) || Samples > MaximumSurfaceSamples)
        throw Error('The preview surface exceeds its geometry allocation budget. Narrow the visible scene.');
    return { Pitch, Regions: Result, Samples, Quality: Target };
}

function Color(Coordinate)
{
    const Hex = String(Coordinate || '#b9a481').replace('#', '');
    const NumberColor = Number.parseInt(Hex.length === 3 ? Hex.split('').map(Digit => Digit + Digit).join('') : Hex, 16);
    return [(NumberColor >> 16 & 255) / 255, (NumberColor >> 8 & 255) / 255, (NumberColor & 255) / 255];
}

function Normalize(Coordinate)
{
    const Length = Math.hypot(...Coordinate);
    return Length > 1e-15 ? Coordinate.map(Component => Component / Length) : [0, 1, 0];
}

function TriangleNormal(First, Second, Third)
{
    const A = Second.map((Coordinate, Axis) => Coordinate - First[Axis]);
    const B = Third.map((Coordinate, Axis) => Coordinate - First[Axis]);
    return [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
}

function Gradient(Distances, X, Y, Z, Shape)
{
    const [SamplesX, SamplesY] = Shape;
    const At = (A, B, C) => Distances[(C * SamplesY + B) * SamplesX + A];
    const Result = [
        (At(Math.min(X + 1, SamplesX - 1), Y, Z) - At(Math.max(0, X - 1), Y, Z)) / (X > 0 && X < SamplesX - 1 ? 2 : 1),
        (At(X, Math.min(Y + 1, SamplesY - 1), Z) - At(X, Math.max(0, Y - 1), Z)) / (Y > 0 && Y < SamplesY - 1 ? 2 : 1),
        (At(X, Y, Math.min(Z + 1, Shape[2] - 1)) - At(X, Y, Math.max(0, Z - 1))) /
            (Z > 0 && Z < Shape[2] - 1 ? 2 : 1)
    ];
    return Normalize(Result);
}

function InterpolateSurfaceEdge(First, Second)
{
    const Fraction = Clamp(First.Distance / (First.Distance - Second.Distance), 0, 1);
    return {
        Position: First.Position.map((Coordinate, Axis) => Coordinate + (Second.Position[Axis] - Coordinate) * Fraction),
        Normal: Normalize(First.Normal.map((Coordinate, Axis) => Coordinate + (Second.Normal[Axis] - Coordinate) * Fraction)),
        Color: First.Distance < 0 ? First.Color : Second.Color
    };
}

export function ContourTetrahedron(Vertices, AccumulateTriangle)
{
    const Inside = Vertices.filter(Vertex => Vertex.Distance < 0);
    const Outside = Vertices.filter(Vertex => Vertex.Distance >= 0);
    if (!Inside.length || !Outside.length) return;
    if (Inside.length === 1 || Outside.length === 1)
    {
        const Single = Inside.length === 1 ? Inside[0] : Outside[0];
        const Others = Inside.length === 1 ? Outside : Inside;
        AccumulateTriangle(...Others.map(Other => InterpolateSurfaceEdge(Single, Other)));
        return;
    }
    const A = InterpolateSurfaceEdge(Inside[0], Outside[0]), B = InterpolateSurfaceEdge(Inside[0], Outside[1]);
    const C = InterpolateSurfaceEdge(Inside[1], Outside[0]), D = InterpolateSurfaceEdge(Inside[1], Outside[1]);
    AccumulateTriangle(A, B, C);
    AccumulateTriangle(B, D, C);
}

export function SurfaceBufferAccumulator(Origin, MaximumTriangles = MaximumSurfaceTriangles)
{
    const Chunks = [], Origins = [];
    let CurrentOrigin = Origin;
    const ChunkFloats = 4095 * 9;
    let Chunk = new Float32Array(ChunkFloats), Offset = 0, Triangles = 0;
    function AccumulateTriangle(First, Second, Third)
    {
        let Face = TriangleNormal(First.Position, Second.Position, Third.Position);
        if (Math.hypot(...Face) < 1e-12) return;
        if (++Triangles > MaximumTriangles)
            throw new SurfaceCapacityExceeded();
        const Average = First.Normal.map((Coordinate, Axis) => Coordinate + Second.Normal[Axis] + Third.Normal[Axis]);
        if (Face.reduce((Sum, Coordinate, Axis) => Sum + Coordinate * Average[Axis], 0) < 0)
        {
            [Second, Third] = [Third, Second];
            Face = Face.map(Coordinate => -Coordinate);
        }
        Face = Normalize(Face);
        for (const Vertex of [First, Second, Third])
        {
            if (Offset + 9 > Chunk.length)
            {
                Chunks.push(Chunk.subarray(0, Offset));
                Origins.push(CurrentOrigin);
                Chunk = new Float32Array(ChunkFloats);
                Offset = 0;
            }
            Chunk.set(Vertex.Position.map((Coordinate, Axis) => Coordinate - CurrentOrigin[Axis]), Offset);
            Chunk.set(Vertex.Normal.every(Number.isFinite) ? Vertex.Normal : Face, Offset + 3);
            Chunk.set(Vertex.Color, Offset + 6);
            Offset += 9;
        }
    }
    function Finalize()
    {
        if (Offset) { Chunks.push(Chunk.subarray(0, Offset)); Origins.push(CurrentOrigin); }
        return { Chunks, Origins, Triangles, Bytes: Chunks.reduce((Sum, Part) => Sum + Part.byteLength, 0) };
    }
    function ConfigureOrigin(NextOrigin)
    {
        if (Offset) { Chunks.push(Chunk.subarray(0, Offset)); Origins.push(CurrentOrigin); }
        Chunk = new Float32Array(ChunkFloats);
        Offset = 0;
        CurrentOrigin = NextOrigin;
    }
    return { AccumulateTriangle, Finalize, ConfigureOrigin };
}

export async function ConstructVolumeSurface(Field, Quality = 32, Options = {})
{
    const Start = InspectMilliseconds();
    const Plan = PlanVolumeSurface(Field, Quality);
    const Origin = Field.Entries.length ? [
        Math.round((Field.Minimum[0] + Field.Maximum[0]) * .5 / Plan.Pitch) * Plan.Pitch, 0,
        Math.round((Field.Minimum[2] + Field.Maximum[2]) * .5 / Plan.Pitch) * Plan.Pitch
    ] : [0, 0, 0];
    const Accumulator = SurfaceBufferAccumulator(Origin);
    const Colors = Field.Layers.map(Layer => Color(Layer.Color));
    const Valid = Options.Current || (() => true);
    let Slice = InspectMilliseconds(), MaximumSliceMilliseconds = 0, Evaluations = 0;
    let Completed = 0;
    async function Checkpoint(Force = false)
    {
        if (!Valid()) throw new DOMException('Surface preparation superseded.', 'AbortError');
        const Elapsed = InspectMilliseconds() - Slice;
        if (!Force && Elapsed < SurfacePreparationSliceMilliseconds) return;
        MaximumSliceMilliseconds = Math.max(MaximumSliceMilliseconds, Elapsed);
        Options.Progress?.({ Completed, Samples: Plan.Samples, Quality: Plan.Quality, Phase: 'geometry' });
        await (Options.YieldExecution || YieldExecution)();
        if (!Valid()) throw new DOMException('Surface preparation superseded.', 'AbortError');
        Slice = InspectMilliseconds();
    }
    for (const Region of Plan.Regions)
    {
        Accumulator.ConfigureOrigin([
            Math.round((Region.Minimum[0] + Region.Maximum[0]) * .5 / Plan.Pitch) * Plan.Pitch, 0,
            Math.round((Region.Minimum[2] + Region.Maximum[2]) * .5 / Plan.Pitch) * Plan.Pitch
        ]);
        const [SamplesX, SamplesY, SamplesZ] = Region.Samples;
        const Distances = new Float32Array(Region.Count);
        const Indices = new Uint8Array(Region.Count);
        const Planars = Region.Entries.map(() => new Float64Array(SamplesX * SamplesZ));
        for (let Z = 0; Z < SamplesZ; Z++)
        for (let X = 0; X < SamplesX; X++)
        {
            Region.Entries.forEach((Entry, EntryIndex) => {
                Planars[EntryIndex][Z * SamplesX + X] = Field.Planar(Entry,
                    Region.Minimum[0] + X * Plan.Pitch, Region.Minimum[2] + Z * Plan.Pitch);
            });
            if ((X & 15) === 0) await Checkpoint();
        }
        const Column = new Float64Array(Region.Entries.length);
        let Index = 0;
        for (let Z = 0; Z < SamplesZ; Z++)
        for (let Y = 0; Y < SamplesY; Y++)
        for (let X = 0; X < SamplesX; X++)
        {
            Planars.forEach((Part, EntryIndex) => Column[EntryIndex] = Part[Z * SamplesX + X]);
            const Result = Field.Sample(
                Region.Minimum[0] + X * Plan.Pitch,
                Region.Minimum[1] + Y * Plan.Pitch,
                Region.Minimum[2] + Z * Plan.Pitch, Region.Entries, Column);
            Distances[Index] = Result.Distance;
            Indices[Index++] = Result.LayerIndex;
            Evaluations++;
            Completed++;
            if ((Index & 31) === 0) await Checkpoint();
        }
        const Vertex = (X, Y, Z) => {
            const Offset = (Z * SamplesY + Y) * SamplesX + X;
            return {
                Position: [X, Y, Z].map((Coordinate, Axis) => Region.Minimum[Axis] + Coordinate * Plan.Pitch),
                Normal: Gradient(Distances, X, Y, Z, Region.Samples),
                Distance: Distances[Offset], Color: Colors[Indices[Offset]] || [.72, .64, .5]
            };
        };
        for (let Z = 0; Z < SamplesZ - 1; Z++)
        for (let Y = 0; Y < SamplesY - 1; Y++)
        for (let X = 0; X < SamplesX - 1; X++)
        {
            const Cell = SurfaceCellCorners.map(([DX, DY, DZ]) => (Z + DZ) * SamplesX * SamplesY + (Y + DY) * SamplesX + X + DX);
            const Negative = Cell.some(Offset => Distances[Offset] < 0);
            const Positive = Cell.some(Offset => Distances[Offset] >= 0);
            if (Negative && Positive)
            {
                const Vertices = SurfaceCellCorners.map(([DX, DY, DZ]) => Vertex(X + DX, Y + DY, Z + DZ));
                for (const Tetrahedron of SurfaceCellTetrahedra) ContourTetrahedron(Tetrahedron.map(Offset => Vertices[Offset]), Accumulator.AccumulateTriangle);
            }
            if ((X & 15) === 0) await Checkpoint();
        }
    }
    await Checkpoint(true);
    return {
        ...Accumulator.Finalize(), Origin, Pitch: Plan.Pitch, Quality: Plan.Quality,
        Samples: Plan.Samples, Evaluations, Regions: Plan.Regions.length,
        PreparationMilliseconds: InspectMilliseconds() - Start, MaximumSliceMilliseconds
    };
}

export function CompileSurfaceField(Layers)
{
    const Field = CompileVolumeRayField(Layers);
    function Planar(Entry, X, Z)
    {
        return Math.max(MaskDistance(Entry.Layer, X, Z), RectangleDistance(Entry.Rounded, X, Z) - Entry.Radius);
    }
    function SampleEntry(Entry, X, Y, Z, Clip = Planar(Entry, X, Z))
    {
        const { Layer, Compilation, Region } = Entry;
        const Vertical = Math.abs(Y + Layer.Depth * .5) - Layer.Depth * .5;
        const Foundation = Math.hypot(Math.max(Clip, 0), Math.max(Vertical, 0)) + Math.min(Math.max(Clip, Vertical), 0);
        let Distance = EvaluateCompiledRegionStack(Compilation, Foundation, X, Y, Z, Clip, false);
        Distance = Compilation.TerrainEnabled || !Compilation.Steps.length ? Math.max(Distance, Clip) :
            SmoothIntersection(Distance, Clip, Region.Edge || 0);
        if (Region.SculptStrokes?.length) Distance = Math.max(EvaluateSculptStrokes(Region, Distance, X, Y, Z), Clip);
        Distance = Math.max(Distance, -Y - Layer.Depth);
        if (Layer.Caves)
        {
            const First = Layer.Regions[0];
            Distance = Math.max(Distance, Layer.Depth * .65 - Math.hypot(
                X - (First.X0 + First.X1) * .5, Y, Z - (First.Z0 + First.Z1) * .5));
        }
        return Distance;
    }
    // 📝 Raw canonical residuals: changing a boundary's Lipschitz normalization cannot move its interpolated interface.
    function Sample(X, Y, Z, Entries = Field.Entries, Planars = null)
    {
        let Distance = 1e20, LayerIndex = 0;
        Entries.forEach((Entry, Index) => {
            const Next = SampleEntry(Entry, X, Y, Z, Planars ? Planars[Index] : undefined);
            if (Next < Distance) { Distance = Next; LayerIndex = Entry.LayerIndex; }
        });
        return { Distance, LayerIndex };
    }
    return { ...Field, Planar, SampleEntry, Sample };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHORE METADATA, NOT TERRAIN HEIGHT
//------------------------------------------------------------------------------------------------------------------------

export async function ConstructShoreDistance(Field, Options = {})
{
    const Size = Field.Entries.length ? MaximumSurfaceShoreSamples : 1;
    const Minimum = Field.Entries.length ? [Field.Minimum[0] - 32, Field.Minimum[2] - 32] : [0, 0];
    const Span = Field.Entries.length ? [Field.Maximum[0] - Minimum[0] + 32, Field.Maximum[2] - Minimum[1] + 32] : [1, 1];
    const Distances = new Float32Array(Size * Size);
    let Slice = performance.now();
    for (let Z = 0; Z < Size; Z++)
    for (let X = 0; X < Size; X++)
    {
        if (Options.Current && !Options.Current()) throw new DOMException('Shore preparation superseded.', 'AbortError');
        Distances[Z * Size + X] = Field.Shore(
            Minimum[0] + (X + .5) / Size * Span[0], Minimum[1] + (Z + .5) / Size * Span[1]);
        if ((X & 15) === 0 && performance.now() - Slice >= SurfacePreparationSliceMilliseconds)
        {
            await new Promise(Resolve => setTimeout(Resolve, 0));
            Slice = performance.now();
        }
    }
    return { Size, Minimum, Span, Distances };
}
