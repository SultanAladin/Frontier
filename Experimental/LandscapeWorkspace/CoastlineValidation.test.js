/*==========================================================================================================================================
                                                       COASTLINEVALIDATION.TEST.JS
==========================================================================================================================================*/
// 🧩 Regression checks for shoreline residuals, sea-level contact, edge-width cache invalidation, and conservative 3D gradients.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { CreateLandform, CompileRegion } from './LandformStack.js';
import { VolumeDistance, MaskDistance, ValidateDocument } from './DistanceField.js';
import { TerrainCategories } from './TerrainNoise.js';

const ConstructLayer = Type => ({
    Name: 'Coast regression', Color: '#b9a481', Visible: true, Depth: 120, Caves: false,
    Resolution: 1024, Vertical: 128,
    Strokes: [{ Points: [[-40, 0], [40, 0]], Radius: 240, Erase: false }],
    Regions: [{ X0: -320, Z0: -280, X1: 320, Z1: 280, Edge: 65 }],
    Landforms: Type ? [{ ...CreateLandform(Type), Amount: 180, InfluenceRadius: 1.7, Blend: 0 }] : []
});

function Surface(Layer, X, Z)
{
    let Lower = -Math.min(10, Layer.Depth * .5), Upper = 600;
    for (let Iteration = 0; Iteration < 50; Iteration++)
    {
        const Middle = (Lower + Upper) * .5;
        if (VolumeDistance(Layer, X, Middle, Z) < 0) Lower = Middle;
        else Upper = Middle;
    }
    return (Lower + Upper) * .5;
}

test('feathered terrain reaches sea level at the painted shoreline, not an elevated sole', () =>
{
    for (const Type of Object.keys(TerrainCategories))
    {
        const Layer = ConstructLayer(Type);
        const Elevations = [15, 5, 1, .1, .01].map(Inset => Surface(Layer, 0, 240 - Inset));
        Assert.ok(Elevations.every(Number.isFinite));
        Assert.ok(Elevations.at(-1) >= -1e-8 && Elevations.at(-1) < .00002, `${Type}: ${Elevations}`);
        Assert.ok(Elevations[2] < .15, `${Type}: shoreline was lifted`);
        Assert.ok(VolumeDistance(Layer, 0, .1, 240.1) > 0, `${Type}: escaped the painted mask`);
    }
});

test('widening the coast feather changes the actual terrain, not just the rectangle corners', () =>
{
    const Layer = ConstructLayer('fractal');
    const Narrow = Surface(Layer, 0, 230);
    const Before = CompileRegion(Layer, Layer.Regions[0]);
    Layer.Regions[0].Edge = 150;
    const After = CompileRegion(Layer, Layer.Regions[0]);
    const Wide = Surface(Layer, 0, 230);
    Assert.notEqual(Before, After, 'edge edits must invalidate the compiled gradient bounds');
    Assert.ok(Wide < Narrow * .3, `wide ${Wide}, narrow ${Narrow}`);
});

test('sharp coast remains available explicitly at zero feather', () =>
{
    const Layer = ConstructLayer('fractal');
    const Soft = Surface(Layer, 0, 239);
    Layer.Regions[0].Edge = 0;
    const Sharp = Surface(Layer, 0, 239);
    Assert.ok(Sharp > Soft * 10);
});

test('a feathered flat foundation has no above-water thickness, including near the edge', () =>
{
    const Layer = ConstructLayer();
    for (const Depth of [30, 120, 500, 3000])
    {
        Layer.Depth = Depth;
        for (const Z of [0, 200, 235, 239.9])
        {
            Assert.ok(MaskDistance(Layer, 0, Z) < 0);
            Assert.equal(VolumeDistance(Layer, 0, 0, Z), 0);
            Assert.ok(VolumeDistance(Layer, 0, -.01, Z) < 0);
            Assert.ok(VolumeDistance(Layer, 0, .01, Z) > 0);
        }
    }
});

test('underwater thickness cannot move a feathered generator surface', () =>
{
    const Layer = ConstructLayer('sand-dunes');
    const Samples = [[0, 0], [80, 100], [0, 225], [0, 239.9]];
    const Original = Samples.map(([X, Z]) => Surface(Layer, X, Z));
    for (const Depth of [30, 250, 3000])
    {
        Layer.Depth = Depth;
        Samples.forEach(([X, Z], Index) => Assert.ok(Math.abs(Surface(Layer, X, Z) - Original[Index]) < 1e-8));
    }
});

test('coast-weighted volumes retain conservative gradients in all three dimensions', () =>
{
    const ε = .002;
    for (const Type of Object.keys(TerrainCategories))
    for (const Width of [1, 25, 150])
    {
        const Layer = ConstructLayer(Type);
        Layer.Regions[0].Edge = Width;
        for (let Index = 0; Index < 80; Index++)
        {
            const Position = [Math.sin(Index * 1.7) * 95, 40 * Math.sin(Index * .57), 240 - Index % 35 * Width / 40];
            const Gradient = Position.map((Coordinate, Axis) =>
            {
                const Lower = [...Position], Upper = [...Position];
                Lower[Axis] -= ε;
                Upper[Axis] += ε;
                return (VolumeDistance(Layer, ...Upper) - VolumeDistance(Layer, ...Lower)) / (2 * ε);
            });
            Assert.ok(Math.hypot(...Gradient) <= 1.002, `${Type}/${Width}: ${Gradient}`);
        }
    }
});

test('zero generator strength preserves the entire flat volume, including smooth-union settings', () =>
{
    const Layer = ConstructLayer('badlands');
    Layer.Landforms[0].Influence = 0;
    Layer.Landforms[0].Blend = 80;
    for (const [X, Z] of [[0, 0], [100, 100], [0, 239]]) Assert.ok(Math.abs(Surface(Layer, X, Z)) < 1e-8);
});

test('boundary stacks cannot modify a neighbouring boundary or a different mask', () =>
{
    const First = ConstructLayer('fractal'), Second = ConstructLayer('fractal');
    First.Regions = [{ X0: -280, Z0: -260, X1: -20, Z1: 260, Edge: 30 }, { X0: 20, Z0: -260, X1: 280, Z1: 260, Edge: 30 }];
    const Original = VolumeDistance(First, 130, 15, 0);
    const OtherMask = VolumeDistance(Second, -130, 15, 0);
    First.Regions[0].Landforms = [{ ...CreateLandform('sand-dunes'), Amount: 260, Blend: 0 }];
    Assert.equal(VolumeDistance(First, 130, 15, 0), Original);
    Assert.equal(VolumeDistance(Second, -130, 15, 0), OtherMask);
    Assert.notEqual(VolumeDistance(First, -130, 15, 0), VolumeDistance({ ...First, Regions: [{ ...First.Regions[0], Landforms: [] }] }, -130, 15, 0));
    Assert.doesNotThrow(() => ValidateDocument({ Format: 'FrontierLandscape/1', Layers: [First, Second] }));
});
