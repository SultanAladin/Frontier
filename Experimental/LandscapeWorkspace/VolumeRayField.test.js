/*==========================================================================================================================================
                                                         VOLUMERAYFIELD.TEST.JS
==========================================================================================================================================*/
// 🧩 Canonical field equivalence, authored-only allocation, 3D cavities and horizontal/vertical software ray intersections.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { VolumeDistance } from './DistanceField.js';
import { CreateLandform, CompileRegion, LandformCatalogue } from './LandformStack.js';
import { CreateTerrainWorld } from './TerrainExamples.js';
import { CompileVolumeRayField, EmptyRayDistance, IntersectVolumeRay, ProjectVolumeCamera, ProjectVolumeRay, VolumeRayInterval } from './VolumeRayField.js';

function Author(Type = null)
{
    return {
        Name: 'Software volume', Color: '#b4a17d', Visible: true, Depth: 120, Caves: false,
        Resolution: 256, Vertical: 64, Strokes: [{ Points: [[0, 0]], Radius: 150, Erase: false }],
        Regions: [{ X0: -180, Z0: -180, X1: 180, Z1: 180, Edge: 18 }],
        Landforms: Type ? [CreateLandform(Type)] : []
    };
}

test('the compiled software field matches canonical normalized CPU/export distances for every landform family', () =>
{
    for (const Type of [null, ...LandformCatalogue.map(Entry => Entry.Type)])
    {
        const Layer = Author(Type), Field = CompileVolumeRayField([Layer]);
        for (let Index = 0; Index < 160; Index++)
        {
            const Point = [Math.sin(Index * 1.21) * 220, Math.cos(Index * .73) * 180, Math.sin(Index * .37) * 220];
            const Expected = VolumeDistance(Layer, ...Point);
            Assert.ok(Math.abs(Field.Evaluate(...Point, true)[0] - Expected) < 1e-10, String(Type) + ' at ' + Point);
        }
    }
});

test('scoped stacks, erasers, cavities and overlapping masks retain canonical union distances', () =>
{
    const First = Author('mesa');
    First.Caves = true;
    First.Strokes.push({ Points: [[70, 0]], Radius: 22, Erase: true });
    First.Regions.push({ X0: 40, Z0: -80, X1: 175, Z1: 130, Edge: 12, Landforms: [CreateLandform('stacks')] });
    const Second = Author('arch');
    Second.Regions[0].Landforms = [CreateLandform('tunnel')];
    const Field = CompileVolumeRayField([First, Second]);
    for (let Index = 0; Index < 600; Index++)
    {
        const Point = [Math.sin(Index * .13) * 220, Math.cos(Index * .71) * 160, Math.cos(Index * .31) * 190];
        const Expected = Math.min(VolumeDistance(First, ...Point), VolumeDistance(Second, ...Point));
        Assert.ok(Math.abs(Field.Evaluate(...Point, true)[0] - Expected) < 1e-10);
    }
});

test('spatial acceleration never changes canonical occupancy in the applied generator world', () =>
{
    const Layers = CreateTerrainWorld(), Field = CompileVolumeRayField(Layers);
    for (let Index = 0; Index < 600; Index++)
    {
        const Point = [Math.sin(Index * .7) * 1500, Math.cos(Index * .37) * 260, Math.sin(Index * .91) * 900];
        const Exact = Math.min(...Layers.map(Layer => VolumeDistance(Layer, ...Point)));
        const Bound = Field.Evaluate(...Point)[0];
        Assert.ok(Number.isFinite(Bound));
        Assert.equal(Bound < 0, Exact < 0);
        if (Exact < 0) Assert.ok(Math.abs(Bound - Exact) < 1e-9);
    }
});

test('compiled rendering uses an immutable document snapshot instead of mixing edited masks with old generators', () =>
{
    const Layers = [Author('arch')], Field = CompileVolumeRayField(Layers);
    const Before = Field.Evaluate(0, 0, 0, true)[0];
    Layers[0].Visible = false;
    Layers[0].Strokes.length = 0;
    Layers[0].Landforms[0].Amount = 400;
    Assert.equal(Field.Evaluate(0, 0, 0, true)[0], Before);
    Assert.equal(Field.Layers[0].Visible, true);
});

test('empty, unpainted, unselected and hidden regions do not allocate any ray-intersectable solid', () =>
{
    for (const Condition of ['empty', 'unpainted', 'unselected', 'hidden'])
    {
        const Layer = Author('ridges');
        if (Condition === 'unpainted') Layer.Strokes = [];
        if (Condition === 'unselected') Layer.Regions = [];
        if (Condition === 'hidden') Layer.Visible = false;
        const Field = CompileVolumeRayField(Condition === 'empty' ? [] : [Layer]);
        Assert.equal(Field.Entries.length, 0);
        Assert.equal(Field.Evaluate(0, -1, 0)[0], EmptyRayDistance);
        Assert.equal(IntersectVolumeRay(Field, [0, 1000, 0], [0, -1, 0]).Found, false);
    }
});

test('software rays intersect the flat sea-level top, underwater bottom and horizontal sides', () =>
{
    const Field = CompileVolumeRayField([Author()]);
    for (const [Eye, Ray, Position, Normal] of [
        [[0, 200, 0], [0, -1, 0], [0, 0, 0], [0, 1, 0]],
        [[0, -300, 0], [0, 1, 0], [0, -120, 0], [0, -1, 0]],
        [[300, -50, 0], [-1, 0, 0], [150, -50, 0], [1, 0, 0]]
    ])
    {
        const Hit = IntersectVolumeRay(Field, Eye, Ray, 160);
        Assert.equal(Hit.Found, true);
        Assert.ok(Hit.Position.every((Coordinate, Axis) => Math.abs(Coordinate - Position[Axis]) < .08));
        Assert.ok(Hit.Normal.every((Coordinate, Axis) => Math.abs(Coordinate - Normal[Axis]) < .01));
        Assert.equal(Hit.LayerIndex, 0);
        Assert.ok(Hit.Steps <= 1024);
    }
});

test('an arch preserves solid-air-solid occupancy in Y and an actual ray-visible empty horizontal opening', () =>
{
    const Layer = Author('arch');
    Layer.Landforms[0].Amount = 140;
    Layer.Landforms[0].Blend = 0;
    Layer.Regions[0].Edge = 0;
    const Field = CompileVolumeRayField([Layer]);
    const Primitive = CompileRegion(Layer, Layer.Regions[0]).Steps[0].Primitives[0];
    const [X, Y, Z] = Primitive.Centre;
    Assert.ok(Field.Evaluate(X, -50, Z, true)[0] < 0);
    Assert.ok(Field.Evaluate(X, Y, Z, true)[0] > 0);
    Assert.ok(Field.Evaluate(X, Y + Primitive.Radii[0], Z, true)[0] < 0);
    const Roof = IntersectVolumeRay(Field, [X, 400, Z], [0, -1, 0], 160);
    Assert.equal(Roof.Found, true);
    Assert.ok(Roof.Position[1] > Y);
    const Opening = IntersectVolumeRay(Field, [X, Y, 400], [0, 0, -1], 160);
    Assert.equal(Opening.Found, false, 'a heightfield would incorrectly block the opening below the roof');
});

test('parallel, outside and backward rays have explicit finite interval rejection', () =>
{
    const Minimum = [-1, -2, -3], Maximum = [1, 2, 3];
    Assert.deepEqual(VolumeRayInterval([0, 0, 8], [0, 0, -1], Minimum, Maximum), [5, 11]);
    Assert.equal(VolumeRayInterval([2, 0, 8], [0, 0, -1], Minimum, Maximum), null);
    Assert.equal(VolumeRayInterval([0, 0, 8], [0, 0, 1], Minimum, Maximum), null);
    Assert.equal(VolumeRayInterval([0, 0, 0], [0, 0, -1], [Infinity, Infinity, Infinity], [-Infinity, -Infinity, -Infinity]), null);
});

test('perspective rays use the same XYZ orbit camera and top-down RGBA pixel convention', () =>
{
    const Camera = { Yaw: -.35, Pitch: .72, Orbit: 500, Target: [40, 20, -70] };
    const Projection = ProjectVolumeCamera(Camera);
    Assert.ok(Math.abs(Math.hypot(...Projection.Eye.map((Coordinate, Axis) => Coordinate - Camera.Target[Axis])) - Camera.Orbit) < 1e-10);
    const Centre = ProjectVolumeRay(Projection, 0, 0, 1, 1);
    Assert.ok(Centre.every((Coordinate, Axis) => Math.abs(Coordinate - Projection.Forward[Axis]) < 1e-15));
    Assert.ok(ProjectVolumeRay(Projection, 1, 0, 3, 3)[1] > ProjectVolumeRay(Projection, 1, 2, 3, 3)[1]);
});
