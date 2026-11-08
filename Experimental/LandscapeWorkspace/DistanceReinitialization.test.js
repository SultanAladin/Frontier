/*==========================================================================================================================================
                                                   DISTANCEREINITIALIZATION.TEST.JS
==========================================================================================================================================*/
// 🧩 Independent analytic-distance, anisotropic stencil, sparse-boundary, topology and metric-format regressions.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { ConstructEikonalStencil, ReinitializeSparseVolume } from './DistanceReinitialization.js';
import { SampleSparseVolume, EncodeSampledVolume, DecodeSparseVolume, ReadVolumeCell, ValidateVolumeCoverage, VolumeSpecification, EncodeSparseVolume } from './SparseVolume.js';
import { VolumeDistance } from './DistanceField.js';
import { CreateLandform } from './LandformStack.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                        ANALYTIC FIXTURES
//------------------------------------------------------------------------------------------------------------------------

const ConstructSpecification = (Dimensions = [32, 32, 32], Pitch = [.5, .5, .5], Centre = [0, 0, 0]) => ({
    Format: 'FrontierSparseTSDF/1', Dimensions, Pitch,
    Origin: Dimensions.map((Count, Axis) => Centre[Axis] - Count * Pitch[Axis] * .5),
    Band: 2 * Math.max(...Pitch), BrickWidth: 8
});

function Sample(Specification, Scalar)
{
    const Leaves = [];
    for (const Entry of SampleSparseVolume(Specification, undefined, Scalar)) if (Entry.Leaf) Leaves.push(Entry.Leaf);
    return { Specification, Leaves };
}

function Reinitialize(Volume, Scalar, Options = {})
{
    const Progress = [...ReinitializeSparseVolume(Volume, { Exterior: Scalar, ...Options })];
    return { Volume, Progress, Redistance: Volume.Specification.Redistance };
}

function DenseSamples(Volume)
{
    const [Width, Height, Depth] = Volume.Specification.Dimensions;
    const Samples = new Float32Array(Width * Height * Depth);
    for (const Leaf of Volume.Leaves)
    for (let Z = 0; Z < Leaf.Width; Z++)
    for (let Y = 0; Y < Leaf.Width; Y++)
    for (let X = 0; X < Leaf.Width; X++)
    {
        Samples[((Leaf.Z + Z) * Height + Leaf.Y + Y) * Width + Leaf.X + X] = Leaf.Category === 1 ? Leaf.Samples[0] : Leaf.Samples[(Z * Leaf.Width + Y) * Leaf.Width + X];
    }
    return Samples;
}

function InspectErrors(Volume, Analytic, Margin = 0)
{
    const { Dimensions, Origin, Pitch, Band } = Volume.Specification;
    const Samples = DenseSamples(Volume);
    let Maximum = 0, Sum = 0, Count = 0, SignChanges = 0, SeamMaximum = 0;
    for (let Z = Margin; Z < Dimensions[2] - Margin; Z++)
    for (let Y = Margin; Y < Dimensions[1] - Margin; Y++)
    for (let X = Margin; X < Dimensions[0] - Margin; X++)
    {
        const Expected = Analytic(Origin[0] + (X + .5) * Pitch[0], Origin[1] + (Y + .5) * Pitch[1], Origin[2] + (Z + .5) * Pitch[2]);
        const Observed = Samples[(Z * Dimensions[1] + Y) * Dimensions[0] + X];
        if (Math.sign(Expected) !== Math.sign(Observed)) SignChanges++;
        const Error = Math.abs(Observed - Math.max(-Band, Math.min(Band, Expected)));
        Maximum = Math.max(Maximum, Error);
        Sum += Error;
        Count++;
        if ([X, Y, Z].some(Coordinate => Coordinate % 8 === 0 || Coordinate % 8 === 7)) SeamMaximum = Math.max(SeamMaximum, Error);
    }
    return { Maximum, Mean: Sum / Count, Count, SignChanges, SeamMaximum };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        EIKONAL OPERATOR
//------------------------------------------------------------------------------------------------------------------------

test('anisotropic Godunov stencil activates the correct one, two or three directions', () =>
{
    Assert.ok(Math.abs(ConstructEikonalStencil([1, 1, 1])(0, 0, 0) - 1 / Math.sqrt(3)) < 1e-14);
    Assert.equal(ConstructEikonalStencil([.25, 2, 3])(2, Infinity, Infinity), 2.25);
    for (const Pitch of [[.3, 1.4, .7], [1, 1, 1], [12, .1, 3]])
    for (let Index = 0; Index < 100; Index++)
    {
        const Adjacent = [0, 1, 2].map(Axis => Math.abs(Math.sin(Index * 4.73 + Axis * 7.91)) * 5);
        const Distance = ConstructEikonalStencil(Pitch)(...Adjacent);
        const Residual = Adjacent.reduce((Sum, Adjacent, Axis) => Sum + Math.max((Distance - Adjacent) / Pitch[Axis], 0) ** 2, 0);
        Assert.ok(Math.abs(Residual - 1) < 1e-10, `${Pitch}: ${Residual}`);
    }
    Assert.equal(ConstructEikonalStencil([1, 1, 1])(Infinity, Infinity, Infinity), Infinity);
    Assert.throws(() => ConstructEikonalStencil([0, 1, 1]), /pitch/);
});

test('a severely scaled flat field regains metric distance, not just its signs', () =>
{
    const Specification = ConstructSpecification([32, 24, 32], [.7, 1.1, 1.3]);
    const Analytic = (X, Y) => Y - .37;
    const Scalar = (X, Y, Z) => Analytic(X, Y, Z) * .025;
    const { Volume, Redistance } = Reinitialize(Sample(Specification, Scalar), Scalar);
    const Errors = InspectErrors(Volume, Analytic);
    Assert.ok(Errors.Maximum < 2e-6, JSON.stringify(Errors));
    Assert.equal(Errors.SignChanges, 0);
    Assert.equal(Redistance.SignChanges, 0);
    Assert.ok(Redistance.Converged && Redistance.SeedCells > 0);
    Assert.ok(Redistance.CollapsedBricks > 0);
});

test('oblique planar distance remains metric across brick boundaries and nonuniform voxel pitch', () =>
{
    const Normal = [.3, .7, -.4], Length = Math.hypot(...Normal);
    const Analytic = (X, Y, Z) => (X * Normal[0] + Y * Normal[1] + Z * Normal[2]) / Length - .173;
    const Scalar = (X, Y, Z) => Analytic(X, Y, Z) * .03;
    const { Volume } = Reinitialize(Sample(ConstructSpecification([32, 32, 32], [.5, .9, .7]), Scalar), Scalar);
    const Errors = InspectErrors(Volume, Analytic, 5);
    // Cropped oblique fronts can transmit a small boundary-stencil error into the interior.
    Assert.ok(Errors.Maximum < 2e-4 && Errors.Mean < 1e-6, JSON.stringify(Errors));
    Assert.equal(Errors.SignChanges, 0);
});

test('nonmetric quadratic sphere is reconstructed to a world-unit narrow-band distance', () =>
{
    const Radius = 4.6;
    const Analytic = (X, Y, Z) => Math.hypot(X, Y, Z) - Radius;
    const Scalar = (X, Y, Z) => .08 * (X * X + Y * Y + Z * Z - Radius * Radius) / 32;
    const { Volume } = Reinitialize(Sample(ConstructSpecification([48, 48, 48], [.35, .35, .35]), Scalar), Scalar);
    const Errors = InspectErrors(Volume, Analytic);
    Assert.ok(Errors.Maximum < .35 * .3, JSON.stringify(Errors));
    Assert.equal(Errors.SignChanges, 0);
    Assert.ok(Errors.SeamMaximum < .35 * .3);
});

test('anisotropic sphere uses world pitch, not voxel-index distance', () =>
{
    const Analytic = (X, Y, Z) => Math.hypot(X, Y, Z) - 4.6;
    const Scalar = (X, Y, Z) => Analytic(X, Y, Z) * .04;
    const { Volume } = Reinitialize(Sample(ConstructSpecification([48, 24, 32], [.35, .8, .55]), Scalar), Scalar);
    const Errors = InspectErrors(Volume, Analytic);
    Assert.ok(Errors.Maximum < .8 * .55, JSON.stringify(Errors));
    Assert.equal(Errors.SignChanges, 0);
});

test('a torus retains solid–air–solid re-entry at one X/Z coordinate', () =>
{
    const Analytic = (X, Y, Z) => Math.hypot(Math.hypot(X, Y) - 3.3, Z) - 1.3;
    const Scalar = (X, Y, Z) => Analytic(X, Y, Z) * .05;
    const { Volume } = Reinitialize(Sample(ConstructSpecification([64, 64, 64], [.2, .2, .2]), Scalar), Scalar);
    const Errors = InspectErrors(Volume, Analytic);
    Assert.ok(Errors.Maximum < .2 * .6, JSON.stringify(Errors));
    Assert.equal(Errors.SignChanges, 0);
    Assert.ok(ReadVolumeCell(Volume, 32, 15, 32) < 0);
    Assert.ok(ReadVolumeCell(Volume, 32, 31, 32) > 0);
    Assert.ok(ReadVolumeCell(Volume, 32, 48, 32) < 0);
});

test('distant world coordinates do not change reconstructed metric units', () =>
{
    const Centre = [1000000, -300000, 12500000];
    const Analytic = (X, Y, Z) => Math.hypot(X - Centre[0], Y - Centre[1], Z - Centre[2]) - 3;
    const Scalar = (X, Y, Z) => Analytic(X, Y, Z) * .02;
    const { Volume } = Reinitialize(Sample(ConstructSpecification([32, 32, 32], [.25, .25, .25], Centre), Scalar), Scalar);
    const Errors = InspectErrors(Volume, Analytic);
    Assert.ok(Errors.Maximum < .25 * .4, JSON.stringify(Errors));
    Assert.equal(Errors.SignChanges, 0);
});

test('a full allocated box has correct exterior-halo seeding and no false volume-edge surface', () =>
{
    const Region = { X0: -4, Z0: -4, X1: 4, Z1: 4, Edge: 0 };
    const Layer = { Visible: true, Depth: 6, Landforms: [], Regions: [Region], Strokes: [{ Points: [[0, 0]], Radius: 200, Erase: false }] };
    const Scalar = (X, Y, Z) => VolumeDistance(Layer, X, Y, Z, Region);
    const { Volume } = Reinitialize(Sample(VolumeSpecification(Layer, Region, 32, 32), Scalar), Scalar);
    const Errors = InspectErrors(Volume, Scalar);
    // First-order upwinding rounds a sharp box's interior medial ridges by a fraction of a voxel.
    Assert.ok(Errors.Maximum < .25 * .5, JSON.stringify(Errors));
    Assert.ok(Math.abs(ReadVolumeCell(Volume, 0, 16, 16) + .125) < 1e-6);
    Assert.equal(Errors.SignChanges, 0);
});

test('positive and negative uniform volumes remain sparse and saturated without invented interfaces', () =>
{
    for (const Sign of [-1, 1])
    {
        const Scalar = () => Sign * 10000;
        const { Volume, Redistance } = Reinitialize(Sample(ConstructSpecification([24, 8, 24]), Scalar), Scalar);
        Assert.equal(Redistance.Cycles, 0);
        Assert.equal(Redistance.SeedCells, 0);
        Assert.ok(Volume.Leaves.every(Leaf => Leaf.Category === 1 && Leaf.Samples[0] === Sign));
        Assert.equal(Volume.Leaves.reduce((Count, Leaf) => Count + Leaf.Width ** 3, 0), 24 * 8 * 24);
    }
});

test('metric archives retain full coverage, signs and explicit method/convergence metadata', () =>
{
    const Scalar = (X, Y) => (Y - .17) * .03;
    const { Volume } = Reinitialize(Sample(ConstructSpecification(), Scalar), Scalar);
    const Chunks = [...EncodeSampledVolume(Volume)].map(Entry => Entry.Bytes);
    const Bytes = new Uint8Array(Chunks.reduce((Sum, Chunk) => Sum + Chunk.length, 0));
    let Offset = 0;
    for (const Chunk of Chunks) { Bytes.set(Chunk, Offset); Offset += Chunk.length; }
    const Decoded = DecodeSparseVolume(Bytes);
    Assert.equal(Decoded.Specification.Format, 'FrontierSparseTSDF/2');
    Assert.equal(Decoded.Specification.Redistance.Method, 'AnisotropicFastSweeping/1');
    Assert.equal(Decoded.Specification.Redistance.SignChanges, 0);
    Assert.ok(InspectErrors(Decoded, (X, Y) => Y - .17).Maximum < 2e-6);
    Assert.equal(Decoded.Leaves.reduce((Sum, Leaf) => Sum + Leaf.Width ** 3, 0), 32 ** 3);
});

test('budgets, convergence exhaustion, nonfinite fields and coverage corruption fail explicitly', () =>
{
    const Scalar = (X, Y, Z) => (Math.hypot(X, Y, Z) - 4) * .03;
    const Volume = Sample(ConstructSpecification(), Scalar);
    Assert.throws(() => [...ReinitializeSparseVolume(Volume, { MaximumBytes: 1 })], /working-memory budget/);
    Assert.throws(() => [...ReinitializeSparseVolume(Volume, { MaximumCycles: 0 })], /convergence/);
    Assert.throws(() => [...ReinitializeSparseVolume(Volume, { MaximumCycles: 1, Exterior: Scalar })], /did not converge/);
    Assert.throws(() => [...SampleSparseVolume(ConstructSpecification(), undefined, () => NaN)], /Non-finite/);
    const Broken = Sample(ConstructSpecification(), Scalar);
    Broken.Leaves.push(Broken.Leaves[0]);
    Assert.throws(() => ValidateVolumeCoverage(Broken), /Overlapping/);
    Broken.Leaves.pop();
    Broken.Leaves.pop();
    Assert.throws(() => ValidateVolumeCoverage(Broken), /Incomplete/);
});


test('analytic interface refinement corrects the zero shift caused by sign-dependent conservative scaling', () =>
{
    const Analytic = (X, Y) => Y - .173;
    const Scalar = (X, Y, Z) => Analytic(X, Y, Z) * (Analytic(X, Y, Z) > 0 ? .2 : .025);
    const Volume = Sample(ConstructSpecification([32, 24, 32], [.7, 1.1, 1.3]), Scalar);
    Reinitialize(Volume, Scalar, { Surface: Analytic });
    const Errors = InspectErrors(Volume, Analytic);
    Assert.ok(Errors.Maximum < 2e-6, JSON.stringify(Errors));
    Assert.equal(Errors.SignChanges, 0);
    Assert.match(Volume.Specification.Redistance.Interface, /AnalyticBracketRefinement/);
});

test('an analytic evaluator with a different zero set is rejected rather than moving source topology', () =>
{
    const Scalar = (X, Y) => Y * .03;
    const Volume = Sample(ConstructSpecification(), Scalar);
    Assert.throws(() => [...ReinitializeSparseVolume(Volume, { Surface: (X, Y) => Y + 100, Exterior: Scalar })], /disagrees/);
});


test('metric codec rejects fabricated convergence metadata and invalid structured samples before encoding', () =>
{
    const Scalar = (X, Y) => Y * .02;
    const Volume = Sample(ConstructSpecification(), Scalar);
    Reinitialize(Volume, Scalar);
    for (const Change of [{ Cycles: -1 }, { MaximumCycles: 0 }, { MaximumChange: 10 }, { Tolerance: NaN }, { SeedCells: -1 }, { SampledCells: 1 }, { Interface: 'unverified' }])
    {
        const Invalid = { Specification: { ...Volume.Specification, Redistance: { ...Volume.Specification.Redistance, ...Change } }, Leaves: Volume.Leaves };
        Assert.throws(() => [...EncodeSampledVolume(Invalid)], /provenance/);
    }
    const Corrupt = { ...Volume, Leaves: Volume.Leaves.map(Leaf => ({ ...Leaf, Samples: Leaf.Samples.slice() })) };
    Corrupt.Leaves[0].Samples[0] = NaN;
    Assert.throws(() => [...EncodeSampledVolume(Corrupt)], /sample/);
    Assert.throws(() => [...EncodeSampledVolume({ ...Volume, Leaves: Volume.Leaves.slice(1) })], /coverage/);
});


test('unnormalized terrain residuals and conservative marching bounds retain matching voxel signs for interface refinement', () =>
{
    const Types = ['fractal', 'multifractal', 'ridged-multifractal', 'badlands', 'ridge-noise', 'fractal-ridges', 'sand-dunes', 'mesa', 'fault', 'craters'];
    for (const Type of Types)
    for (const Mode of ['union', 'subtract', 'intersect', 'blend'])
    {
        const Layer = {
            Depth: 100, Caves: true,
            Strokes: [{ Points: [[-30, 0], [30, 0]], Radius: 110, Erase: false }],
            Regions: [{ X0: -160, Z0: -130, X1: 160, Z1: 130, Edge: 55 }],
            Landforms: [{ ...CreateLandform(Type), Amount: 90, Mode, InfluenceRadius: .55, Blend: 12 }]
        };
        for (const X of [-145, -110, -60, 0, 45, 110, 145])
        for (const Y of [-101, -75, -40, -.01, .01, 20, 80, 140])
        {
            const Bound = VolumeDistance(Layer, X, Y, 13, Layer.Regions[0]);
            const Residual = VolumeDistance(Layer, X, Y, 13, Layer.Regions[0], false);
            Assert.equal(Math.sign(Bound), Math.sign(Residual), `${Type}/${Mode} at ${X},${Y}`);
        }
    }
});


test('sampled sign-uniform volumes with no interface encode a valid saturated zero-cycle metric result', () =>
{
    for (const Sign of [-1, 1])
    {
        const Scalar = () => Sign * .01;
        const Volume = Sample(ConstructSpecification([16, 16, 16]), Scalar);
        Assert.ok(Volume.Leaves.every(Leaf => Leaf.Category === 2));
        Reinitialize(Volume, Scalar);
        Assert.equal(Volume.Specification.Redistance.SeedCells, 0);
        Assert.equal(Volume.Specification.Redistance.Cycles, 0);
        const Decoded = DecodeSparseVolume(Buffer.concat([...EncodeSampledVolume(Volume)].map(Entry => Entry.Bytes)));
        Assert.ok(Decoded.Leaves.every(Leaf => Leaf.Category === 1 && Leaf.Samples[0] === Sign * Volume.Specification.Band));
    }
});

test('conservative resampling cannot reuse a metric label or bypass the archive safety budget', () =>
{
    const Scalar = (X, Y) => Y * .02;
    const Volume = Sample(ConstructSpecification(), Scalar);
    Reinitialize(Volume, Scalar);
    Assert.throws(() => [...EncodeSparseVolume(Volume.Specification, undefined, Scalar)], /metric-labelled/);
    for (const MaximumBytes of [0, NaN, Infinity, 257 * 1024 * 1024])
    {
        Assert.throws(() => [...SampleSparseVolume(ConstructSpecification(), MaximumBytes, Scalar)], /budget/);
        Assert.throws(() => [...EncodeSampledVolume(Volume, MaximumBytes)], /budget/);
    }
    for (const Band of [1e100, 1e-100]) Assert.throws(() => [...SampleSparseVolume({ ...ConstructSpecification(), Band }, undefined, Scalar)], /band/);
});
