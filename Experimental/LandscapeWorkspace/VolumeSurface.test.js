/*==========================================================================================================================================
                                                          VOLUMESURFACE.TEST.JS
==========================================================================================================================================*/
// 🧩 Canonical raw zero sets, real XYZ cavities, aligned triangle batches, finite allocations and cooperative cancellation.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { VolumeDistance } from './DistanceField.js';
import { CreateLandform, CompileRegion, LandformCatalogue } from './LandformStack.js';
import { CreateCoastalWorld } from './CoastalWorld.js';
import {
    CompileSurfaceField, ConstructVolumeSurface, PlanVolumeSurface, ContourTetrahedron, ConstructShoreDistance,
    MaximumSurfaceSamples, MaximumSurfaceTriangles, MaximumSurfaceShoreSamples
} from './VolumeSurface.js';
import {
    SurfaceVertexSource, SurfaceFragmentSource, OceanFragmentSource, OceanVertexSource, MaximumGpuBatchVertices, ConstructVisibleGpuInterval
} from './VolumeRasterization.js';

function Author(Type = null)
{
    return {
        Name: 'XYZ surface', Color: '#b4a17d', Visible: true, Depth: 120, Caves: false,
        Resolution: 256, Vertical: 64, Strokes: [{ Points: [[0, 0]], Radius: 150, Erase: false }],
        Regions: [{ X0: -180, Z0: -180, X1: 180, Z1: 180, Edge: 18 }],
        Landforms: Type ? [{ ...CreateLandform(Type), Blend: 0 }] : []
    };
}

function Vertices(Surface)
{
    const Result = [];
    Surface.Chunks.forEach((Chunk, Index) =>
    {
        const Origin = Surface.Origins[Index];
        for (let Offset = 0; Offset < Chunk.length; Offset += 9)
        {
            Result.push({
                Position: [0, 1, 2].map(Axis => Chunk[Offset + Axis] + Origin[Axis]),
                Normal: Array.from(Chunk.subarray(Offset + 3, Offset + 6)),
                Colour: Array.from(Chunk.subarray(Offset + 6, Offset + 9))
            });
        }
    });
    return Result;
}

const Subtract = (First, Second) => First.map((Coordinate, Axis) => Coordinate - Second[Axis]);
const Dot = (First, Second) => First.reduce((Sum, Coordinate, Axis) => Sum + Coordinate * Second[Axis], 0);
const Cross = (First, Second) => [
    First[1] * Second[2] - First[2] * Second[1], First[2] * Second[0] - First[0] * Second[2],
    First[0] * Second[1] - First[1] * Second[0]
];

// 📝 Validation-only triangle intersections count both sides of an actual extracted cavity, not a rendered CPU image.
function Intersections(Surface, Eye, Direction)
{
    const Points = Vertices(Surface), Hits = [];
    for (let Index = 0; Index < Points.length; Index += 3)
    {
        const First = Points[Index].Position;
        const A = Subtract(Points[Index + 1].Position, First), B = Subtract(Points[Index + 2].Position, First);
        const Perpendicular = Cross(Direction, B), Determinant = Dot(A, Perpendicular);
        if (Math.abs(Determinant) < 1e-10) continue;
        const Offset = Subtract(Eye, First), U = Dot(Offset, Perpendicular) / Determinant;
        const Other = Cross(Offset, A), V = Dot(Direction, Other) / Determinant;
        if (U < -1e-8 || V < -1e-8 || U + V > 1 + 1e-8) continue;
        const Distance = Dot(B, Other) / Determinant;
        if (Distance >= 0 && !Hits.some(Previous => Math.abs(Previous - Distance) < 1e-4)) Hits.push(Distance);
    }
    return Hits.sort((First, Second) => First - Second);
}

test('prepared field residuals exactly match canonical raw XYZ stacks across every landform category', () =>
{
    for (const Type of [null, ...LandformCatalogue.map(Entry => Entry.Type)])
    {
        const Layer = Author(Type), Field = CompileSurfaceField([Layer]);
        for (let Index = 0; Index < 96; Index++)
        {
            const Position = [Math.sin(Index * 1.21) * 220, Math.cos(Index * .73) * 180, Math.sin(Index * .37) * 220];
            Assert.ok(Math.abs(Field.Sample(...Position).Distance - VolumeDistance(Layer, ...Position, null, false)) < 1e-10,
                String(Type) + ': ' + Position);
        }
    }
});

test('raw union evaluation preserves scoped operations, erasers, layer cavities and independently coloured masks', () =>
{
    const First = Author('mesa');
    First.Caves = true;
    First.Strokes.push({ Points: [[70, 0]], Radius: 22, Erase: true });
    First.Regions.push({ X0: 40, Z0: -80, X1: 175, Z1: 130, Edge: 12, Landforms: [CreateLandform('stacks')] });
    const Second = Author('arch');
    Second.Color = '#789eae';
    Second.Regions[0].Landforms = [CreateLandform('tunnel')];
    const Field = CompileSurfaceField([First, Second]);
    for (let Index = 0; Index < 320; Index++)
    {
        const Position = [Math.sin(Index * .13) * 220, Math.cos(Index * .71) * 160, Math.cos(Index * .31) * 190];
        const Distances = [First, Second].map(Layer => VolumeDistance(Layer, ...Position, null, false));
        const Result = Field.Sample(...Position);
        Assert.ok(Math.abs(Result.Distance - Math.min(...Distances)) < 1e-10);
        Assert.equal(Result.LayerIndex, Distances[0] <= Distances[1] ? 0 : 1);
    }
});

test('all sixteen tetrahedral sign cases interpolate only bracketed three-dimensional edges', () =>
{
    const Positions = [[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]];
    for (let Combination = 0; Combination < 16; Combination++)
    {
        const Triangles = [];
        const Input = Positions.map((Position, Index) => ({
            Position, Normal: [0, 1, 0], Color: [.6, .5, .4], Distance: Combination & 1 << Index ? -1 : 1
        }));
        ContourTetrahedron(Input, (...Triangle) => Triangles.push(Triangle));
        const Count = Input.filter(Vertex => Vertex.Distance < 0).length;
        Assert.equal(Triangles.length, Count === 0 || Count === 4 ? 0 : Count === 2 ? 2 : 1);
        for (const Triangle of Triangles)
        for (const Vertex of Triangle)
        {
            Assert.ok(Vertex.Position.every(Number.isFinite));
            Assert.equal(Vertex.Position.reduce((Sum, Coordinate) => Sum + Coordinate, 0) > 0, true);
            Assert.ok(Vertex.Position.every(Coordinate => Coordinate >= 0 && Coordinate <= 1));
            Assert.ok(Math.abs(Math.hypot(...Vertex.Normal) - 1) < 1e-12);
        }
    }
});

test('flat foundations have exact sea-level tops, finite underwater bottoms and no infinite solid', async () =>
{
    const Layer = Author(), Surface = await ConstructVolumeSurface(CompileSurfaceField([Layer]), 24);
    const Points = Vertices(Surface);
    Assert.ok(Surface.Triangles > 1000);
    Assert.ok(Points.some(Vertex => Math.abs(Vertex.Position[1]) < 1e-6));
    Assert.ok(Points.some(Vertex => Math.abs(Vertex.Position[1] + 120) < 1e-6));
    Assert.ok(Points.every(Vertex => Vertex.Position[1] >= -120.00001 && Vertex.Position[1] <= .00001));
    Assert.ok(Points.every(Vertex => Math.abs(Vertex.Position[0]) <= 150.0001 && Math.abs(Vertex.Position[2]) <= 150.0001));
    Assert.deepEqual(Intersections(Surface, [.137, 200, .293], [0, -1, 0]).map(Distance => Math.round(Distance)), [200, 320]);
});

test('arch triangles preserve four vertical interfaces and an empty horizontal opening that a heightmap would fill', async () =>
{
    const Layer = Author('arch');
    Layer.Landforms[0].Amount = 140;
    Layer.Regions[0].Edge = 0;
    const Primitive = CompileRegion(Layer, Layer.Regions[0]).Steps[0].Primitives[0];
    const Surface = await ConstructVolumeSurface(CompileSurfaceField([Layer]), 48);
    const [X, Y, Z] = Primitive.Centre;
    const Vertical = Intersections(Surface, [X + .137, 400, Z + .293], [0, -1, 0]);
    Assert.equal(Vertical.length, 4, String(Vertical));
    Assert.ok(400 - Vertical[0] > Y && 400 - Vertical[1] > Y);
    Assert.ok(Math.abs(400 - Vertical[2]) < 1e-4);
    Assert.ok(Math.abs(400 - Vertical[3] + 120) < 1e-4);
    Assert.deepEqual(Intersections(Surface, [X + .137, Y, Z + 400], [0, 0, -1]), []);
});

test('disconnected authored regions do not allocate the empty space between them, even at billion-metre coordinates', async () =>
{
    const First = Author(), Second = Author();
    const Shift = 1e9;
    Second.Strokes[0].Points[0][0] += Shift;
    Second.Regions[0].X0 += Shift;
    Second.Regions[0].X1 += Shift;
    const Field = CompileSurfaceField([First, Second]), Plan = PlanVolumeSurface(Field, 24);
    Assert.equal(Plan.Regions.length, 2);
    Assert.ok(Plan.Samples < 40000);
    const Surface = await ConstructVolumeSurface(Field, 24);
    Assert.ok(Surface.Origins.some(Origin => Math.abs(Origin[0] - Shift) < 30));
    for (let Index = 0; Index < Surface.Chunks.length; Index++)
    {
        const Chunk = Surface.Chunks[Index];
        for (let Offset = 0; Offset < Chunk.length; Offset += 9) Assert.ok(Math.abs(Chunk[Offset]) < 200);
    }
    Assert.equal(Intersections(Surface, [Shift + .137, 200, .293], [0, -1, 0]).length, 2);
});

test('empty, hidden, unpainted and unallocated documents never invent preview geometry', async () =>
{
    for (const Condition of ['empty', 'hidden', 'unpainted', 'unallocated', 'erased'])
    {
        const Layer = Author('fractal');
        if (Condition === 'hidden') Layer.Visible = false;
        if (Condition === 'unpainted') Layer.Strokes = [];
        if (Condition === 'unallocated') Layer.Regions = [];
        if (Condition === 'erased') Layer.Strokes.push({ Points: [[0, 0]], Radius: 200, Erase: true });
        const Field = CompileSurfaceField(Condition === 'empty' ? [] : [Layer]);
        const Surface = await ConstructVolumeSurface(Field, 16);
        Assert.equal(Surface.Triangles, 0, Condition);
        Assert.equal(Surface.Bytes, 0);
    }
});

test('the actual coastal startup produces capped, triangle-aligned GPU buffers without mutating the authored document', async () =>
{
    const Layers = CreateCoastalWorld(), Before = JSON.stringify(Layers);
    const Field = CompileSurfaceField(Layers);
    Layers[0].Landforms[0].Amount = 400;
    const Surface = await ConstructVolumeSurface(Field, 48);
    Assert.equal(Field.Layers[0].Landforms[0].Amount, 185);
    Assert.equal(JSON.stringify(CreateCoastalWorld()), Before);
    Assert.ok(Surface.Triangles > 30000 && Surface.Triangles <= MaximumSurfaceTriangles);
    Assert.ok(Surface.Samples <= MaximumSurfaceSamples);
    Assert.equal(Surface.Evaluations, Surface.Samples);
    Assert.equal(Surface.Chunks.reduce((Count, Chunk) => Count + Chunk.length / 27, 0), Surface.Triangles);
    for (const Chunk of Surface.Chunks)
    {
        Assert.equal(Chunk.length % 27, 0, 'a split triangle would leave visible holes at batch boundaries');
        Assert.ok(Chunk.length / 9 <= MaximumGpuBatchVertices);
        Assert.ok(Chunk.every(Number.isFinite));
    }
    Assert.ok(Vertices(Surface).every(Vertex => Math.abs(Math.hypot(...Vertex.Normal) - 1) < 1e-5));
});

test('surface planning reduces sampling density before allocating an oversized volume', () =>
{
    const Layer = Author('arch');
    Layer.Depth = 10000;
    const Plan = PlanVolumeSurface(CompileSurfaceField([Layer]), 96);
    Assert.ok(Plan.Samples <= MaximumSurfaceSamples);
    Assert.ok(Plan.Pitch > 360 / 96);
    Assert.ok(Plan.Regions.every(Region => Number.isSafeInteger(Region.Count) && Region.Count > 0));
});

test('cancellation rejects a superseded surface at a cooperative yield rather than publishing stale triangles', async () =>
{
    let Current = true, Yields = 0;
    const Field = CompileSurfaceField(CreateCoastalWorld());
    await Assert.rejects(ConstructVolumeSurface(Field, 48, {
        Current: () => Current,
        YieldExecution: async () => { Yields++; Current = false; }
    }), Failure => Failure.name === 'AbortError');
    Assert.equal(Yields, 1);
});

test('pathological alternating-sign topology stops at its triangle budget instead of allocating an unbounded output', async () =>
{
    const Field = {
        Entries: [{ Lower: [0, 0, 0], Upper: [32, 32, 32] }], Minimum: [0, 0, 0], Maximum: [32, 32, 32],
        Layers: [{ Color: '#b4a17d' }], Planar: () => -1,
        Sample: (X, Y, Z) => ({ Distance: (Math.round(X) + Math.round(Y) + Math.round(Z)) % 2 === 0 ? -1 : 1, LayerIndex: 0 })
    };
    await Assert.rejects(ConstructVolumeSurface(Field, 32), /triangle budget/);
});

test('shore metadata is a bounded signed XZ mask-distance texture, not a terrain elevation substitute', async () =>
{
    const Field = CompileSurfaceField([Author('arch')]);
    const Shore = await ConstructShoreDistance(Field);
    Assert.equal(Shore.Size, MaximumSurfaceShoreSamples);
    Assert.equal(Shore.Distances.length, Shore.Size ** 2);
    Assert.ok(Shore.Distances.every(Number.isFinite));
    for (const [X, Z] of [[0, 0], [32, 64], [90, 30]])
    {
        const Expected = Field.Shore(Shore.Minimum[0] + (X + .5) / Shore.Size * Shore.Span[0],
            Shore.Minimum[1] + (Z + .5) / Shore.Size * Shore.Span[1]);
        Assert.ok(Math.abs(Shore.Distances[Z * Shore.Size + X] - Expected) < 1e-5);
    }
    Assert.equal('Heights' in Shore, false);
});

test('terrain GPU shaders have no procedural field, ray march or generator loop; ocean spectrum executes only per vertex', () =>
{
    for (const Source of [SurfaceVertexSource, SurfaceFragmentSource, OceanFragmentSource])
        Assert.doesNotMatch(Source, /LatticeNoise|RegionStack|VolumeDistance|PrimitiveDistance|for\s*\(|while\s*\(|OceanEvaluation/);
    Assert.match(OceanVertexSource, /OceanVertexEvaluation\(World/);
    Assert.doesNotMatch(SurfaceFragmentSource + OceanFragmentSource, /gl_FragDepth/, 'retain early depth rejection; floating reverse depth separates sea level');
});

test('floating reverse depth separates the sea-level foundation from water without disabling early depth rejection', () =>
{
    const Source = readFileSync(new URL('./VolumeRasterization.js', import.meta.url), 'utf8');
    Assert.match(Source, /GL\.DEPTH_COMPONENT32F/);
    Assert.match(Source, /GL\.depthFunc\(GL\.GREATER\)/);
    Assert.doesNotMatch(SurfaceFragmentSource + OceanFragmentSource, /gl_FragDepth/);
    const Near = .1, Far = 15000;
    const ReverseDepth = Distance => Near * (Far / Distance - 1) / (Far - Near);
    Assert.ok(Math.abs(ReverseDepth(Near) - 1) < 1e-14);
    Assert.equal(ReverseDepth(Far), 0);
    Assert.ok(Math.fround(ReverseDepth(1500)) > Math.fround(ReverseDepth(1500.09)));
});

test('GPU fence deadlines exclude Mask/background time even if an image was still in flight when suspended', () =>
{
    let Milliseconds = 100;
    const Interval = ConstructVisibleGpuInterval(() => Milliseconds);
    Interval.Reset();
    Milliseconds += 20;
    Interval.Suspend();
    Milliseconds += 90000;
    Interval.Suspend();
    Assert.equal(Interval.Elapsed(), 20);
    Interval.Resume();
    Interval.Resume();
    Milliseconds += 35;
    Assert.equal(Interval.Elapsed(), 55);
    Interval.Reset();
    Assert.equal(Interval.Elapsed(), 0);
});

test('the refined coastal XYZ surface resolves more detail without increasing the triangle or sampling ceilings', async () =>
{
    const Field = CompileSurfaceField(CreateCoastalWorld());
    const Surface = await ConstructVolumeSurface(Field, 64);
    Assert.equal(Surface.Triangles, 96798);
    Assert.ok(Surface.Bytes <= MaximumSurfaceTriangles * 27 * 4);
    Assert.ok(Surface.Samples <= MaximumSurfaceSamples);
    Assert.ok(Surface.Pitch < 11);
    await Assert.rejects(ConstructVolumeSurface(Field, 72), Failure => Failure.name === 'SurfaceCapacityExceeded');
});
