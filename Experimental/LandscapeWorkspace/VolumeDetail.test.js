/*==========================================================================================================================================
                                                           VOLUMEDETAIL.TEST.JS
==========================================================================================================================================*/
// 🧩 Authored working pitch, nested tetrahedral interfaces, anisotropic normals, true XYZ openings and unchanged finite budgets.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { CreateLandform, CompileRegion } from './LandformStack.js';
import { CompileSurfaceField, MaximumSurfaceSamples, MaximumSurfaceTriangles } from './VolumeSurface.js';
import { PlanWorkingSurface, ConstructWorkingSurface, InterpolateLatticeTetrahedron, WorkingBoundaryWeight } from './VolumeDetail.js';
import { ConstructVolumeLattice } from './VolumeLattice.js';

function Author(Type = null)
{
    return { Name: 'Working XYZ', Color: '#b9a481', Visible: true, Depth: 120, Caves: false, Resolution: 512, Vertical: 96,
        Landforms: Type ? [{ ...CreateLandform(Type), Amount: 140, Blend: 0 }] : [],
        Strokes: [{ Points: [[0, 0]], Radius: 150, Erase: false }],
        Regions: [{ X0: -180, Z0: -180, X1: 180, Z1: 180, Edge: 0, Resolution: [512, 96, 256] }] };
}

function Triangles(Surface, Section)
{
    const Result = [];
    Surface.Chunks.forEach((Chunk, Index) =>
    {
        if (Surface.Sections[Index] !== Section) return;
        for (let Offset = 0; Offset < Chunk.length; Offset += 27)
            Result.push([0, 9, 18].map(Vertex => [0, 1, 2].map(Axis => Chunk[Offset + Vertex + Axis] + Surface.Origins[Index][Axis])));
    });
    return Result;
}

const Subtract = (First, Second) => First.map((Coordinate, Axis) => Coordinate - Second[Axis]);
const Dot = (First, Second) => First.reduce((Sum, Coordinate, Axis) => Sum + Coordinate * Second[Axis], 0);
const Cross = (First, Second) => [First[1] * Second[2] - First[2] * Second[1], First[2] * Second[0] - First[0] * Second[2], First[0] * Second[1] - First[1] * Second[0]];

function Intersections(Surface, Eye, Direction)
{
    const Hits = [];
    for (const Section of [0, 1])
    for (const [First, Second, Third] of Triangles(Surface, Section))
    {
        const A = Subtract(Second, First), B = Subtract(Third, First), Perpendicular = Cross(Direction, B), Determinant = Dot(A, Perpendicular);
        if (Math.abs(Determinant) < 1e-10) continue;
        const Offset = Subtract(Eye, First), U = Dot(Offset, Perpendicular) / Determinant, Other = Cross(Offset, A), V = Dot(Direction, Other) / Determinant;
        if (U < -1e-8 || V < -1e-8 || U + V > 1 + 1e-8) continue;
        const Distance = Dot(B, Other) / Determinant;
        if (Distance < 0) continue;
        const Position = Eye.map((Coordinate, Axis) => Coordinate + Direction[Axis] * Distance);
        if (Section === 0 && Surface.DetailMinimum && Position.every((Coordinate, Axis) =>
            Coordinate > Surface.DetailMinimum[Axis] && Coordinate < Surface.DetailMaximum[Axis])) continue;
        if (!Hits.some(Previous => Math.abs(Previous - Distance) < 1e-4)) Hits.push(Distance);
    }
    return Hits.sort((First, Second) => First - Second);
}

function DistanceToSegment(Point, Segment)
{
    const Direction = Subtract(Segment[1], Segment[0]), Relative = Subtract(Point, Segment[0]);
    const Fraction = Math.max(0, Math.min(1, Dot(Relative, Direction) / Math.max(1e-30, Dot(Direction, Direction))));
    return Math.hypot(...Point.map((Coordinate, Axis) => Coordinate - Segment[0][Axis] - Fraction * Direction[Axis]));
}

function FaceSegments(Input, Axis, Coordinate, Minimum, Maximum)
{
    const Result = [];
    for (const Triangle of Input)
    {
        const Points = [];
        for (let Edge = 0; Edge < 3; Edge++)
        {
            const First = Triangle[Edge], Second = Triangle[(Edge + 1) % 3];
            const A = First[Axis] - Coordinate, B = Second[Axis] - Coordinate;
            if (Math.abs(A) < 1e-5 && !Points.some(Point => Math.hypot(...Subtract(Point, First)) < 1e-5)) Points.push(First);
            if (A * B < 0)
            {
                const Fraction = A / (A - B);
                const Point = First.map((Part, Direction) => Part + Fraction * (Second[Direction] - Part));
                if (!Points.some(Previous => Math.hypot(...Subtract(Previous, Point)) < 1e-5)) Points.push(Point);
            }
        }
        if (Points.length !== 2) continue;
        const Direction = Subtract(Points[1], Points[0]);
        let Lower = 0, Upper = 1;
        for (const Other of [0, 1, 2].filter(Other => Other !== Axis))
        {
            if (Math.abs(Direction[Other]) < 1e-10)
            {
                if (Points[0][Other] < Minimum[Other] - 1e-5 || Points[0][Other] > Maximum[Other] + 1e-5) Upper = -1;
                continue;
            }
            const A = (Minimum[Other] - Points[0][Other]) / Direction[Other], B = (Maximum[Other] - Points[0][Other]) / Direction[Other];
            Lower = Math.max(Lower, Math.min(A, B));
            Upper = Math.min(Upper, Math.max(A, B));
        }
        if (Lower <= Upper) Result.push([Lower, Upper].map(Fraction => Points[0].map((Part, DirectionIndex) => Part + Fraction * Direction[DirectionIndex])));
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      LATTICE AND SEAM MATHEMATICS
//------------------------------------------------------------------------------------------------------------------------

test('working planning uses the selected region XYZ pitch, not a global 64-cell terrain claim', () =>
{
    const First = Author(), Second = Author();
    Second.Regions[0].Resolution = [256, 128, 512];
    Second.Strokes[0].Points[0][0] += 1e9;
    Second.Regions[0].X0 += 1e9;
    Second.Regions[0].X1 += 1e9;
    const Field = CompileSurfaceField([First, Second]);
    for (const LayerIndex of [0, 1])
    {
        const Plan = PlanWorkingSurface(Field, { LayerIndex, RegionIndex: 0, Force: true });
        const Lattice = ConstructVolumeLattice(Field.Layers[LayerIndex], Field.Layers[LayerIndex].Regions[0]);
        Assert.deepEqual(Plan.Working.Pitch, Lattice.Pitch);
        Assert.deepEqual(Plan.Working.Dimensions, Lattice.Dimensions);
        Assert.equal(Plan.Regions.length, 2);
        Assert.ok(Plan.Samples + 67 ** 3 < MaximumSurfaceSamples);
        Assert.ok(Plan.Working.Cells.every(Count => Count <= 64));
        Assert.ok(Plan.Regions.every(Region => Region.Factors.every(Factor => Factor === Region.Factors[0])));
        Assert.ok(Plan.Working.Minimum.every((Coordinate, Axis) => Math.abs((Coordinate - Lattice.Minimum[Axis]) / Lattice.Pitch[Axis] -
            Math.round((Coordinate - Lattice.Minimum[Axis]) / Lattice.Pitch[Axis])) < 1e-5));
    }
    const Near = PlanWorkingSurface(Field, { LayerIndex: 0, RegionIndex: 0, Distance: 50, PixelHeight: 800 });
    const Far = PlanWorkingSurface(Field, { LayerIndex: 0, RegionIndex: 0, Distance: 20000, PixelHeight: 800 });
    Assert.ok(Far.Working.Pitch[0] > Near.Working.Pitch[0]);
    Assert.notEqual(Near.Key, Far.Key);
});

test('coarse field interpolation follows the actual six tetrahedra rather than trilinear cube interpolation', () =>
{
    const Lattice = { Minimum: [0, 0, 0], Pitch: [1, 1, 1], Samples: [2, 2, 2], Distances: new Float32Array([-2, 3, 12, 16, 20, 7, -4, -1]) };
    Assert.equal(InterpolateLatticeTetrahedron(Lattice, [.75, .25, .5]), 1.75);
});

test('nested anisotropic refinement reproduces coarse tetrahedral contours on all six arbitrarily offset working faces', () =>
{
    const Coarse = { Minimum: [10, -80, 200], Pitch: [8, 24, 4], Samples: [4, 4, 4], Distances: new Float32Array(64) };
    for (let Index = 0; Index < 64; Index++) Coarse.Distances[Index] = Math.sin(Index * .31) + Math.cos(Index * 1.7);
    const Fine = { Minimum: Coarse.Minimum, Pitch: Coarse.Pitch.map(Spacing => Spacing / 4), Samples: [13, 13, 13], Distances: new Float32Array(13 ** 3) };
    for (let Z = 0; Z < 13; Z++)
    for (let Y = 0; Y < 13; Y++)
    for (let X = 0; X < 13; X++)
        Fine.Distances[(Z * 13 + Y) * 13 + X] = InterpolateLatticeTetrahedron(Coarse, [X, Y, Z].map((Coordinate, Axis) => Fine.Minimum[Axis] + Coordinate * Fine.Pitch[Axis]));
    const Working = { Minimum: [1, 2, 3].map((Coordinate, Axis) => Fine.Minimum[Axis] + Coordinate * Fine.Pitch[Axis]),
        Maximum: [8, 9, 10].map((Coordinate, Axis) => Fine.Minimum[Axis] + Coordinate * Fine.Pitch[Axis]), Pitch: Fine.Pitch };
    for (let Axis = 0; Axis < 3; Axis++)
    for (const Boundary of [Working.Minimum, Working.Maximum])
    for (let Index = 0; Index < 97; Index++)
    {
        const Position = Working.Minimum.map((Coordinate, Direction) => Coordinate + (Working.Maximum[Direction] - Coordinate) *
            ((Math.sin(Index * (Direction + 1) * 2.7) + 1) / 2));
        Position[Axis] = Boundary[Axis];
        Assert.equal(WorkingBoundaryWeight(Working, Position), 0);
        Assert.ok(Math.abs(InterpolateLatticeTetrahedron(Fine, Position) - InterpolateLatticeTetrahedron(Coarse, Position)) < 2e-6);
    }
    const Interior = { Minimum: [0, 0, 0], Maximum: [16, 48, 8], Pitch: [2, 6, 1] };
    Assert.equal(WorkingBoundaryWeight(Interior, [8, 24, 4]), 1);
});

test('anisotropic finite differences retain the correct world-space plane normal throughout working and context batches', async () =>
{
    const Field = CompileSurfaceField([Author()]), Gradient = [.25, 1, -.4], Length = Math.hypot(...Gradient);
    Field.Sample = (X, Y, Z) => ({ Distance: Dot([X, Y, Z], Gradient), LayerIndex: 0 });
    Field.Planar = () => 0;
    const Surface = await ConstructWorkingSurface(Field, { LayerIndex: 0, RegionIndex: 0, Point: [0, 0, 0], Force: true });
    Assert.ok(Surface.Working.Resident);
    Assert.ok(Surface.DetailTriangles > 0 && Surface.ContextTriangles > 0);
    for (const Chunk of Surface.Chunks)
    for (let Offset = 0; Offset < Chunk.length; Offset += 9)
        Assert.ok(Math.hypot(...Gradient.map((Component, Axis) => Component / Length - Chunk[Offset + 3 + Axis])) < 2e-5);
});

//------------------------------------------------------------------------------------------------------------------------
//                                                      ACTUAL XYZ GEOMETRY AND BOUNDS
//------------------------------------------------------------------------------------------------------------------------

test('authored-pitch working arch retains four vertical surfaces, an empty horizontal opening and matching extracted face cuts', async () =>
{
    const Layer = Author('arch'), Before = JSON.stringify(Layer), Field = CompileSurfaceField([Layer]);
    const Primitive = CompileRegion(Layer, Layer.Regions[0]).Steps[0].Primitives[0], [X, Y, Z] = Primitive.Centre;
    const Surface = await ConstructWorkingSurface(Field, { LayerIndex: 0, RegionIndex: 0, Point: [X, Y, Z], Force: true });
    Assert.equal(JSON.stringify(Layer), Before);
    Assert.deepEqual(Surface.Working.Pitch, ConstructVolumeLattice(Layer, Layer.Regions[0]).Pitch);
    const Hits = Intersections(Surface, [X + .137, 400, Z + .293], [0, -1, 0]);
    Assert.equal(Hits.length, 4, String(Hits));
    Assert.ok(400 - Hits[0] > Y && 400 - Hits[1] > Y);
    Assert.deepEqual(Intersections(Surface, [X + .137, Y, Z + 400], [0, 0, -1]), []);
    const Context = Triangles(Surface, 0), Fine = Triangles(Surface, 1);
    let Compared = 0;
    for (let Axis = 0; Axis < 3; Axis++)
    for (const Boundary of [Surface.DetailMinimum, Surface.DetailMaximum])
    {
        const CoarseCuts = FaceSegments(Context, Axis, Boundary[Axis], Surface.DetailMinimum, Surface.DetailMaximum);
        const FineCuts = FaceSegments(Fine, Axis, Boundary[Axis], Surface.DetailMinimum, Surface.DetailMaximum);
        if (!CoarseCuts.length && !FineCuts.length) continue;
        Assert.ok(CoarseCuts.length && FineCuts.length, `axis ${Axis}: ${CoarseCuts.length} / ${FineCuts.length}`);
        for (const [Segments, Other] of [[FineCuts, CoarseCuts], [CoarseCuts, FineCuts]])
        for (const Segment of Segments)
        for (const Fraction of [0, .25, .5, .75, 1])
        {
            const Point = Segment[0].map((Coordinate, Direction) => Coordinate + Fraction * (Segment[1][Direction] - Coordinate));
            Assert.ok(Math.min(...Other.map(OtherSegment => DistanceToSegment(Point, OtherSegment))) < .001, `axis ${Axis}: ${Point}`);
            Compared++;
        }
    }
    Assert.ok(Compared > 20);
    Assert.ok(Surface.Samples <= MaximumSurfaceSamples && Surface.Triangles <= MaximumSurfaceTriangles);
    Assert.ok(Surface.Chunks.every(Chunk => Chunk.length % 27 === 0 && Chunk.length / 9 <= 4095 && Chunk.every(Number.isFinite)));
});

test('billion-metre region translations keep working vertices local and authored spacing unchanged', async () =>
{
    const Layer = Author(), Shift = 1e9;
    Layer.Strokes[0].Points[0][0] += Shift;
    Layer.Regions[0].X0 += Shift;
    Layer.Regions[0].X1 += Shift;
    const Surface = await ConstructWorkingSurface(CompileSurfaceField([Layer]), { LayerIndex: 0, RegionIndex: 0, Point: [Shift, 0, 0], Force: true });
    Assert.ok(Surface.Working.Resident);
    Assert.ok(Surface.Origins.every(Origin => Math.abs(Origin[0] - Shift) < 200));
    for (const Chunk of Surface.Chunks)
    for (let Offset = 0; Offset < Chunk.length; Offset += 9) Assert.ok(Math.abs(Chunk[Offset]) < 220);
});

test('working sampling cancels cooperatively before any superseded geometry is returned', async () =>
{
    let Current = true, Yields = 0;
    await Assert.rejects(ConstructWorkingSurface(CompileSurfaceField([Author('arch')]), { Force: true },
        { Current: () => Current, YieldExecution: () => { Yields++; Current = false; } }), Failure => Failure.name === 'AbortError');
    Assert.equal(Yields, 1);
});

test('a deliberately unresolved 3D checker field shrinks coverage or retains context without raising triangle or sample ceilings', async () =>
{
    const Field = CompileSurfaceField([Author()]), Lattice = ConstructVolumeLattice(Field.Layers[0], Field.Layers[0].Regions[0]);
    Field.Sample = (X, Y, Z) => ({ Distance: Math.cos((X - Lattice.Minimum[0]) / Lattice.Pitch[0] * Math.PI) *
        Math.cos((Y - Lattice.Minimum[1]) / Lattice.Pitch[1] * Math.PI) * Math.cos((Z - Lattice.Minimum[2]) / Lattice.Pitch[2] * Math.PI), LayerIndex: 0 });
    Field.Planar = () => 0;
    const Surface = await ConstructWorkingSurface(Field, { LayerIndex: 0, RegionIndex: 0, Point: [0, -30, 0], Force: true });
    Assert.ok(Surface.Triangles <= MaximumSurfaceTriangles && Surface.Samples <= MaximumSurfaceSamples);
    Assert.ok(Surface.Working.Limited);
    Assert.deepEqual(Surface.Working.Pitch, Lattice.Pitch);
    if (!Surface.Working.Resident) Assert.equal(Surface.DetailMinimum, null);
    else Assert.ok(Surface.Working.Cells.every(Count => Count < 64));
});

test('moving a working brick reuses only the bounded immutable context, never geometry from a different document snapshot', async () =>
{
    const Field = CompileSurfaceField([Author()]);
    const First = await ConstructWorkingSurface(Field, { LayerIndex: 0, RegionIndex: 0, Point: [0, 0, 0], Force: true });
    const Second = await ConstructWorkingSurface(Field, { LayerIndex: 0, RegionIndex: 0, Point: [8, 0, 0], Force: true });
    Assert.equal(First.ContextReused, false);
    Assert.equal(Second.ContextReused, true);
    Assert.equal(First.ContextTriangles, Second.ContextTriangles);
    Assert.notEqual(First.DetailKey, Second.DetailKey);
    Assert.ok(Second.Evaluations < Second.Samples);
    const Replacement = await ConstructWorkingSurface(CompileSurfaceField([Author()]), { Force: true });
    Assert.equal(Replacement.ContextReused, false);
});

test('multiple independent arch regions coarsen context before consuming the working triangle reserve', async () =>
{
    const Layer = Author('arch');
    Layer.Strokes[0].Points.push([1180, 0]);
    Layer.Regions.push({ X0: 1000, Z0: -180, X1: 1360, Z1: 180, Edge: 0, Resolution: [32, 96, 24] });
    const Centre = CompileRegion(Layer, Layer.Regions[0]).Steps[0].Primitives[0].Centre;
    const Surface = await ConstructWorkingSurface(CompileSurfaceField([Layer]), { LayerIndex: 0, RegionIndex: 0, Point: Centre, Force: true });
    Assert.ok(Surface.ContextExpansion > 1);
    Assert.ok(Surface.ContextTriangles <= MaximumSurfaceTriangles * .75);
    Assert.ok(Surface.Working.Resident && Surface.DetailTriangles > 0);
    Assert.deepEqual(Surface.Working.Pitch, ConstructVolumeLattice(Layer, Layer.Regions[0]).Pitch);
    Assert.ok(Surface.Samples <= MaximumSurfaceSamples && Surface.Triangles <= MaximumSurfaceTriangles);
});
