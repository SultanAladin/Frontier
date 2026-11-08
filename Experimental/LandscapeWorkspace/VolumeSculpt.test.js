/*==========================================================================================================================================
                                                            VOLUMESCULPT.TEST.JS
==========================================================================================================================================*/
// 🧩 Persistent true-XYZ sculpt ownership, canonical sampling/picking, caves, sweep continuity and bounded document validation.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { ValidateDocument, VolumeDistance } from './DistanceField.js';
import { CompileSurfaceField } from './VolumeSurface.js';
import { CompileVolumeRayField, IntersectVolumeRay } from './VolumeRayField.js';
import { LayerVerticalBounds } from './LandformStack.js';
import { SculptBrushCentre, SculptCapsuleDistance, MaximumSculptSamples } from './VolumeSculpt.js';

function Fixture()
{
    return { Format: 'FrontierLandscape/1', Layers: [{ Name: 'Sculpt owner', Color: '#b9a481', Visible: true, Resolution: 512, Vertical: 96,
        Depth: 24, Caves: false, Landforms: [], Strokes: [{ Radius: 23, Points: [[0, 0]], Erase: false }],
        Regions: [{ X0: -24, X1: 24, Z0: -24, Z1: 24, Edge: 0 }] }] };
}
const Brush = (Mode = 'add', Points = [[0, -2, 0]], Radius = 4) => ({ Mode, Points, Radius, Strength: .5 });

test('clay displaces genuine XYZ geometry and expands vertical bounds, without allocating a dense lattice', () =>
{
    const Doc = Fixture(), Layer = Doc.Layers[0], Region = Layer.Regions[0];
    Assert.ok(VolumeDistance(Layer, 0, 1, 0) > 0);
    Region.SculptStrokes = [Brush()]; ValidateDocument(Doc);
    Assert.ok(VolumeDistance(Layer, 0, 1, 0) < 0);
    Assert.ok(VolumeDistance(Layer, 0, 3, 0) > 0);
    Assert.ok(LayerVerticalBounds(Layer, Region)[1] >= 2.8);
    Assert.deepEqual(Region.Resolution, undefined);
});

test('carving an internal sphere retains solid–air–solid at the same X/Z rather than editing a heightmap', () =>
{
    const Doc = Fixture(), Layer = Doc.Layers[0];
    Layer.Regions[0].SculptStrokes = [Brush('remove', [[0, -12, 0]], 4)]; ValidateDocument(Doc);
    Assert.ok(VolumeDistance(Layer, 0, -3, 0) < 0);
    Assert.ok(VolumeDistance(Layer, 0, -12, 0) > 0);
    Assert.ok(VolumeDistance(Layer, 0, -21, 0) < 0);
    Assert.ok(VolumeDistance(Layer, 7, -12, 0) < 0);
});

test('swept clay/carve strokes interpolate continuous 3D capsules, not detached pointer-event spheres', () =>
{
    Assert.equal(SculptCapsuleDistance([0, 0, 0], [-5, 0, 0], [5, 0, 0], 2), -2);
    Assert.equal(SculptCapsuleDistance([0, 3, 0], [-5, 0, 0], [5, 0, 0], 2), 1);
    const Doc = Fixture(), Layer = Doc.Layers[0];
    Layer.Regions[0].SculptStrokes = [Brush('remove', [[-10, -12, 0], [10, -12, 0]], 2)]; ValidateDocument(Doc);
    for (const X of [-9, -5, 0, 5, 9]) Assert.ok(VolumeDistance(Layer, X, -12, 0) > 0);
});

test('shared raw sculpt field agrees in surface preparation, normal export sampling and compiled ray residuals', () =>
{
    const Doc = Fixture(), Layer = Doc.Layers[0], Region = Layer.Regions[0];
    Region.SculptStrokes = [Brush(), Brush('remove', [[1, 0, 0]], 1.5)]; ValidateDocument(Doc);
    const Surface = CompileSurfaceField(Doc.Layers), Ray = CompileVolumeRayField(Doc.Layers);
    for (const X of [-20, -3, 0, 1, 5])
    for (const Y of [-22, -2, 0, 1, 4])
    {
        const Canonical = VolumeDistance(Layer, X, Y, 0, Region, false);
        Assert.ok(Math.abs(Surface.Sample(X, Y, 0).Distance - Canonical) < 1e-8, `${X},${Y}`);
        Assert.ok(Math.abs(Ray.Evaluate(X, Y, 0, true)[2] - Canonical) < 1e-8, `${X},${Y}`);
    }
});

test('one region owns its sculpt; neighbouring regions, other layers and protected outside allocation stay unchanged', () =>
{
    const Doc = Fixture(), Layer = Doc.Layers[0];
    const Other = { ...structuredClone(Layer), Name: 'Unselected owner' }; Doc.Layers.push(Other);
    Layer.Regions.push({ X0: 30, X1: 50, Z0: -24, Z1: 24, Edge: 0 });
    Layer.Regions[0].SculptStrokes = [Brush('add', [[22, 0, 0]], 8)]; ValidateDocument(Doc);
    Assert.equal(Other.Regions[0].SculptStrokes, undefined);
    Assert.equal(Layer.Regions[1].SculptStrokes, undefined);
    Assert.ok(VolumeDistance(Layer, 26, 1, 0, Layer.Regions[0], false) > 0);
    Assert.ok(VolumeDistance(Other, 0, 1, 0) > 0);
});

test('picking intersects the actual sculpted surface and project serialization retains exact XYZ strokes', () =>
{
    const Doc = Fixture(); Doc.Layers[0].Regions[0].SculptStrokes = [Brush()]; ValidateDocument(Doc);
    const Roundtrip = ValidateDocument(JSON.parse(JSON.stringify(Doc)));
    Assert.deepEqual(Roundtrip, Doc);
    const Hit = IntersectVolumeRay(CompileVolumeRayField(Roundtrip.Layers), [0, 15, 0], [0, -1, 0], 512);
    Assert.equal(Hit.Found, true);
    Assert.ok(Math.abs(Hit.Position[1] - 2) < .05, JSON.stringify(Hit));
    Assert.deepEqual(SculptBrushCentre([0, 0, 0], [0, 1, 0], 4, .5, 'add'), [0, -2, 0]);
    Assert.deepEqual(SculptBrushCentre([0, 0, 0], [0, 1, 0], 4, .5, 'remove'), [0, 2, 0]);
});

test('invalid or oversized sculpt documents reject before being applied', () =>
{
    for (const Change of [{ Mode: 'height' }, { Radius: 0 }, { Strength: 0 }, { Points: [[0, Infinity, 0]] }, { Points: [[0, 0]] }])
    {
        const Doc = Fixture(); Doc.Layers[0].Regions[0].SculptStrokes = [{ ...Brush(), ...Change }];
        Assert.throws(() => ValidateDocument(Doc), /sculpt/);
    }
    const Doc = Fixture(); Doc.Layers[0].Regions[0].SculptStrokes = [Brush('add', Array.from({ length: MaximumSculptSamples + 1 }, () => [0, 0, 0]))];
    Assert.throws(() => ValidateDocument(Doc), /sculpt/);
});
