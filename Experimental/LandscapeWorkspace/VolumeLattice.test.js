/*==========================================================================================================================================
                                                          VOLUMELATTICE.TEST.JS
==========================================================================================================================================*/
// 🧩 Independent XYZ region resolutions, legacy defaults, 96 vertical cells and actual anisotropic sparse archive coverage.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { ValidateDocument, VolumeDistance } from './DistanceField.js';
import { ConstructVolumeLattice, ResolveLatticeDimensions, ValidateLatticeDimensions } from './VolumeLattice.js';
import { VolumeSpecification, EncodeSparseVolume, DecodeSparseVolume, ReadVolumeCell } from './SparseVolume.js';
import { CompileVolumeRayField } from './VolumeRayField.js';

function Author()
{
    return { Name: 'Independent lattices', Color: '#b9a481', Visible: true, Depth: 120, Caves: false,
        Resolution: 1024, Vertical: 128, Landforms: [],
        Strokes: [{ Points: [[0, 0], [400, 0]], Radius: 90, Erase: false }],
        Regions: [{ X0: -100, Z0: -100, X1: 100, Z1: 100, Edge: 0 },
            { X0: 300, Z0: -100, X1: 500, Z1: 100, Edge: 0 }] };
}

test('legacy layer defaults resolve to XYZ without mutating or migrating the authoring field', () =>
{
    const Layer = Author(), Before = JSON.stringify(Layer);
    Assert.deepEqual(ResolveLatticeDimensions(Layer, Layer.Regions[0]), [1024, 128, 1024]);
    const Lattice = ConstructVolumeLattice(Layer, Layer.Regions[0]);
    Assert.deepEqual(Lattice.Pitch, Lattice.Maximum.map((Coordinate, Axis) => (Coordinate - Lattice.Minimum[Axis]) / Lattice.Dimensions[Axis]));
    Assert.equal(JSON.stringify(Layer), Before);
    const Resolved = ResolveLatticeDimensions(Layer, Layer.Regions[0]);
    Resolved[0] = 8;
    Assert.equal(Layer.Resolution, 1024);
});

test('each region and mask retain independent X, Y and Z counts including 96 vertical cells', () =>
{
    const Layer = Author(), Second = Author();
    Layer.Regions[0].Resolution = [512, 96, 512];
    Layer.Regions[1].Resolution = [256, 128, 384];
    Second.Vertical = 96;
    const Document = ValidateDocument(JSON.parse(JSON.stringify({ Format: 'FrontierLandscape/1', Layers: [Layer, Second] })));
    Assert.deepEqual(VolumeSpecification(Document.Layers[0], Document.Layers[0].Regions[0]).Dimensions, [512, 96, 512]);
    Assert.deepEqual(VolumeSpecification(Document.Layers[0], Document.Layers[0].Regions[1]).Dimensions, [256, 128, 384]);
    Assert.deepEqual(VolumeSpecification(Document.Layers[1], Document.Layers[1].Regions[0]).Dimensions, [1024, 96, 1024]);
    const Before = Array.from({ length: 50 }, (_, Index) => VolumeDistance(Layer, Index * 4, -Index * 2, Index / 2));
    Layer.Regions[0].Resolution = [4096, 512, 2048];
    Assert.deepEqual(Array.from({ length: 50 }, (_, Index) => VolumeDistance(Layer, Index * 4, -Index * 2, Index / 2)), Before);
    Assert.deepEqual(ResolveLatticeDimensions(Layer, Layer.Regions[1]), [256, 128, 384]);
});

test('malformed per-region counts fail import explicitly instead of silently reverting to layer defaults', () =>
{
    for (const Dimensions of [null, 512, '512x512x96', [512, 96], [512, 96, 512, 8], [512, 95, 512], [0, 96, 512], [512, 96, 8192], [512, NaN, 512], ['512', 96, 512]])
    {
        Assert.throws(() => ValidateLatticeDimensions(Dimensions), /resolution/);
        const Layer = Author();
        Layer.Regions[0].Resolution = Dimensions;
        Assert.throws(() => ValidateDocument({ Format: 'FrontierLandscape/1', Layers: [Layer] }), /resolution/);
    }
    for (const Dimensions of [[8, 8, 8], [24, 96, 40], [4096, 512, 4096]]) Assert.deepEqual(ValidateLatticeDimensions(Dimensions), Dimensions);
});

test('explicit diagnostic export dimensions override a region target and remain honestly labelled 64 cubed', () =>
{
    const Layer = Author();
    Layer.Regions[0].Resolution = [512, 96, 256];
    Assert.deepEqual(VolumeSpecification(Layer, Layer.Regions[0], 64, 64).Dimensions, [64, 64, 64]);
    Assert.deepEqual(Layer.Regions[0].Resolution, [512, 96, 256]);
});

test('actual independently dimensioned sparse archives cover every XYZ cell and preserve metre-scale pitch', () =>
{
    const Layer = Author();
    Layer.Regions[0].Resolution = [24, 96, 32];
    Layer.Regions[1].Resolution = [32, 16, 24];
    for (const Region of Layer.Regions)
    {
        const Specification = VolumeSpecification(Layer, Region), Chunks = [...EncodeSparseVolume(Specification)];
        const Bytes = new Uint8Array(Chunks.reduce((Sum, Chunk) => Sum + Chunk.Bytes.length, 0));
        let Offset = 0;
        for (const Chunk of Chunks) { Bytes.set(Chunk.Bytes, Offset); Offset += Chunk.Bytes.length; }
        const Volume = DecodeSparseVolume(Bytes);
        Assert.deepEqual(Volume.Specification.Dimensions, Region.Resolution);
        Assert.equal(Chunks.at(-1).Progress.Covered, Region.Resolution.reduce((Product, Count) => Product * Count, 1));
        for (const Cell of [[0, 0, 0], Region.Resolution.map(Count => Count / 2), Region.Resolution.map(Count => Count - 1)])
        {
            const Position = Cell.map((Coordinate, Axis) => Specification.Origin[Axis] + (Coordinate + .5) * Specification.Pitch[Axis]);
            const Expected = Math.max(-Specification.Band, Math.min(Specification.Band, VolumeDistance(Layer, ...Position, Region)));
            Assert.ok(Math.abs(ReadVolumeCell(Volume, ...Cell) - Expected) < 1e-5);
        }
    }
});

test('selected-region picking preserves the original layer-scoped cavity origin instead of reordering regions', () =>
{
    const Layer = Author();
    Layer.Caves = true;
    const Field = CompileVolumeRayField([Layer], { LayerIndex: 0, RegionIndex: 1 });
    Assert.equal(Field.Entries.length, 1);
    Assert.equal(Field.Entries[0].Layer.Regions.length, 2);
    Assert.equal(Field.Entries[0].Region.X0, 300);
    for (const Position of [[400, -10, 0], [400, -80, 20], [450, 5, 0]])
        Assert.ok(Math.abs(Field.Evaluate(...Position, true)[2] - VolumeDistance(Layer, ...Position, Layer.Regions[1], false)) < 1e-9);
});
