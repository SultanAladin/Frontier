/*==========================================================================================================================================
                                                            SPARSEVOLUME.JS
==========================================================================================================================================*/
// 🧩 Adaptive signed-volume sampling, shared little-endian leaf encoding, and bounded TSDF archive decoding.

import { VolumeDistance } from './DistanceField.js';
import { ConstructVolumeLattice } from './VolumeLattice.js';

export const MaximumVolumeBytes = 256 * 1024 * 1024;

function ValidateExportBudget(MaximumBytes)
{
    if (!Number.isFinite(MaximumBytes) || MaximumBytes <= 0 || MaximumBytes > MaximumVolumeBytes) throw Error('Invalid sparse-volume export budget');
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        VOLUME SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

export function ValidateVolumeSpecification(Specification)
{
    if (!['FrontierSparseTSDF/1', 'FrontierSparseTSDF/2'].includes(Specification?.Format)) throw Error('Unsupported volume format');
    const { Dimensions, Origin, Pitch, Band, BrickWidth } = Specification;
    if (!Array.isArray(Dimensions) || Dimensions.length !== 3 || Dimensions.some(Count =>
        !Number.isInteger(Count) || Count < 8 || Count > 4096 || Count % 8)) throw Error('Invalid volume dimensions');
    if (!Array.isArray(Origin) || Origin.length !== 3 || Origin.some(Coordinate => !Number.isFinite(Coordinate))) throw Error('Invalid volume origin');
    if (!Array.isArray(Pitch) || Pitch.length !== 3 || Pitch.some(Spacing => !Number.isFinite(Spacing) || Spacing <= 0)) throw Error('Invalid voxel pitch');
    if (!Number.isFinite(Band) || Band <= 0 || !Number.isFinite(Math.fround(Band)) || Math.fround(Band) <= 0 || BrickWidth !== 8) throw Error('Invalid volume band or brick width');
    if (Dimensions.some((Count, Axis) => !Number.isFinite(Origin[Axis] + Count * Pitch[Axis]))) throw Error('Non-finite volume extent');
    if (Specification.Format === 'FrontierSparseTSDF/2')
    {
        const Metric = Specification.Redistance;
        const Total = Dimensions.reduce((Product, Count) => Product * Count, 1);
        if (Metric?.Method !== 'AnisotropicFastSweeping/1' || Metric.Converged !== true || Metric.SignChanges !== 0 ||
            !Number.isInteger(Metric.Cycles) || !Number.isInteger(Metric.MaximumCycles) || Metric.MaximumCycles < 1 || Metric.MaximumCycles > 32 ||
            Metric.Cycles < 0 || Metric.Cycles > Metric.MaximumCycles || !Number.isFinite(Metric.Tolerance) || Metric.Tolerance < 0 ||
            !Number.isFinite(Metric.MaximumChange) || Metric.MaximumChange < 0 || Metric.MaximumChange > Metric.Tolerance ||
            ['SeedCells', 'SampledCells', 'CollapsedBricks', 'EstimatedWorkspaceBytes'].some(Name => !Number.isSafeInteger(Metric[Name]) || Metric[Name] < 0) ||
            Metric.SeedCells > Metric.SampledCells || Metric.SampledCells > Total || Metric.CollapsedBricks > Metric.SampledCells / 512 ||
            (Metric.SeedCells > 0 && Metric.Cycles === 0) ||
            typeof Metric.Interface !== 'string' || !/^(AnalyticBracketRefinement|LinearSampleCrossings)\/1(?::|$)/.test(Metric.Interface)) throw Error('Invalid metric-distance provenance');
    }
    return Specification;
}

export function VolumeSpecification(Layer, Region, Horizontal = null, Vertical = null)
{
    const Lattice = ConstructVolumeLattice(Layer, Horizontal === null && Vertical === null ? Region :
        { ...Region, Resolution: [Horizontal ?? Layer.Resolution, Vertical ?? Layer.Vertical, Horizontal ?? Layer.Resolution] });
    const { Dimensions, Minimum, Pitch } = Lattice;
    return ValidateVolumeSpecification({
        Format: 'FrontierSparseTSDF/1',
        Dimensions,
        Origin: Minimum,
        Pitch,
        Band: Math.max(...Pitch) * 2,
        BrickWidth: 8,
        Region,
        Layer,
        Encoding: 'JSON line then binary leaves; little endian; X fastest, Y next, Z last; cell centres',
        LeafLayout: 'u32 category, u32 X, u32 Y, u32 Z, u32 width; category 1: f32 constant; category 2: width cubed f32 samples',
        DistanceMeaning: 'World-unit CSG signed distance bound clamped to [-Band,+Band]; all in-bounds cells covered'
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        ADAPTIVE SAMPLING
//------------------------------------------------------------------------------------------------------------------------

// 📝 An optional scalar evaluator also permits numerical validation against independent analytic solids.
// It must be a conservative 1-Lipschitz bound for the octree's uniform-leaf classification to be valid.
export function* SampleSparseVolume(
    Specification,
    MaximumBytes = MaximumVolumeBytes,
    Evaluate = null)
{
    ValidateVolumeSpecification(Specification);
    ValidateExportBudget(MaximumBytes);
    if (Specification.Format !== 'FrontierSparseTSDF/1') throw Error('Conservative sampling cannot create a metric-labelled archive; use reinitialization and EncodeSampledVolume.');
    if (Evaluate !== null && typeof Evaluate !== 'function') throw Error('Invalid sparse-volume scalar evaluator');
    const { Dimensions, Origin, Pitch, Band, Layer, Region } = Specification;
    const Scalar = Evaluate || ((X, Y, Z) => VolumeDistance(Layer, X, Y, Z, Region));
    const Total = Dimensions.reduce((Product, Count) => Product * Count, 1);
    let Bytes = new TextEncoder().encode(JSON.stringify(Specification) + '\n').byteLength;
    let Covered = 0, Bricks = 0, Uniform = 0, Evaluations = 0;
    const Pending = [[0, 0, 0, 2 ** Math.ceil(Math.log2(Math.max(...Dimensions)))]];
    const Progress = () => ({ Covered, Total, Bricks, Uniform, Evaluations, Bytes, Phase: 'sampling', Fraction: Covered / Total });
    if (Bytes > MaximumBytes) throw Error('Export exceeds the 256 MiB uncompressed safety budget.');
    yield { Leaf: null, Progress: Progress() };
    while (Pending.length)
    {
        const [X, Y, Z, Width] = Pending.pop();
        if (X >= Dimensions[0] || Y >= Dimensions[1] || Z >= Dimensions[2]) continue;
        const Interior = X + Width <= Dimensions[0] && Y + Width <= Dimensions[1] && Z + Width <= Dimensions[2];
        const Centre = [X + Width / 2, Y + Width / 2, Z + Width / 2].map((Coordinate, Axis) => Origin[Axis] + Coordinate * Pitch[Axis]);
        const Distance = Scalar(...Centre);
        if (!Number.isFinite(Distance)) throw Error('Non-finite signed field during volume sampling.');
        Evaluations++;
        const Radius = Math.hypot(...Pitch.map(Spacing => Spacing * Width / 2));
        const Constant = Interior && Math.abs(Distance) > Radius + Band + 1e-6;
        if (!Constant && Width > 8)
        {
            const Half = Width / 2;
            for (let ΔZ = 0; ΔZ < 2; ΔZ++)
            for (let ΔY = 0; ΔY < 2; ΔY++)
            for (let ΔX = 0; ΔX < 2; ΔX++) Pending.push([X + ΔX * Half, Y + ΔY * Half, Z + ΔZ * Half, Half]);
            continue;
        }
        const EncodedBytes = 20 + (Constant ? 4 : Width ** 3 * 4);
        if (Bytes + EncodedBytes > MaximumBytes) throw Error('Export exceeds the 256 MiB uncompressed safety budget. Reduce resolution or select a smaller region.');
        const Samples = new Float32Array(Constant ? 1 : Width ** 3);
        if (Constant)
        {
            Samples[0] = Math.sign(Distance) * Band;
            Uniform++;
        }
        else
        {
            let Offset = 0;
            for (let ΔZ = 0; ΔZ < Width; ΔZ++)
            for (let ΔY = 0; ΔY < Width; ΔY++)
            for (let ΔX = 0; ΔX < Width; ΔX++)
            {
                const Sample = Scalar(
                    Origin[0] + (X + ΔX + 0.5) * Pitch[0],
                    Origin[1] + (Y + ΔY + 0.5) * Pitch[1],
                    Origin[2] + (Z + ΔZ + 0.5) * Pitch[2]);
                if (!Number.isFinite(Sample)) throw Error('Non-finite signed field during brick sampling.');
                Samples[Offset++] = Math.max(-Band, Math.min(Band, Sample));
                Evaluations++;
            }
            Bricks++;
        }
        Bytes += EncodedBytes;
        Covered += Width ** 3;
        yield { Leaf: { Category: Constant ? 1 : 2, X, Y, Z, Width, Samples }, Progress: Progress() };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        LEAF ENCODING
//------------------------------------------------------------------------------------------------------------------------

export function EncodeVolumeLeaf(Leaf)
{
    const Chunk = new Uint8Array(20 + Leaf.Samples.length * 4);
    const View = new DataView(Chunk.buffer);
    [Leaf.Category, Leaf.X, Leaf.Y, Leaf.Z, Leaf.Width].forEach((Number, Index) => View.setUint32(Index * 4, Number, true));
    Leaf.Samples.forEach((Distance, Index) => View.setFloat32(20 + Index * 4, Distance, true));
    return Chunk;
}

export function* EncodeSparseVolume(
    Specification,
    MaximumBytes = MaximumVolumeBytes,
    Evaluate = null)
{
    for (const Entry of SampleSparseVolume(Specification, MaximumBytes, Evaluate))
    {
        yield {
            Bytes: Entry.Leaf ? EncodeVolumeLeaf(Entry.Leaf) : new TextEncoder().encode(JSON.stringify(Specification) + '\n'),
            Progress: Entry.Progress
        };
    }
}

export function* EncodeSampledVolume(Volume, MaximumBytes = MaximumVolumeBytes)
{
    ValidateVolumeSpecification(Volume.Specification);
    ValidateExportBudget(MaximumBytes);
    ValidateVolumeCoverage(Volume);
    const Header = new TextEncoder().encode(JSON.stringify(Volume.Specification) + '\n');
    const Total = Volume.Specification.Dimensions.reduce((Product, Count) => Product * Count, 1);
    let Bytes = Header.length, Covered = 0, Bricks = 0, Uniform = 0;
    const Progress = () => ({ Total, Covered, Bytes, Bricks, Uniform, Phase: 'encoding', Fraction: Covered / Total });
    if (Bytes > MaximumBytes) throw Error('Volume header exceeds the export safety budget.');
    yield { Bytes: Header, Progress: Progress() };
    for (const Leaf of Volume.Leaves)
    {
        const Length = 20 + Leaf.Samples.length * 4;
        if (Bytes + Length > MaximumBytes) throw Error('Metric export exceeds the 256 MiB uncompressed safety budget.');
        Bytes += Length;
        Covered += Leaf.Width ** 3;
        if (Leaf.Category === 1) Uniform++;
        else Bricks++;
        yield { Bytes: EncodeVolumeLeaf(Leaf), Progress: Progress() };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        VOLUME DECODING
//------------------------------------------------------------------------------------------------------------------------

export function DecodeSparseVolume(Bytes)
{
    if (!(Bytes instanceof Uint8Array) || Bytes.byteLength > MaximumVolumeBytes) throw Error('Invalid or oversized volume archive.');
    const Newline = Bytes.indexOf(10);
    if (Newline < 0 || Newline > 5e6) throw Error('Invalid volume header');
    const Specification = ValidateVolumeSpecification(JSON.parse(new TextDecoder().decode(Bytes.subarray(0, Newline))));
    const Leaves = [];
    const View = new DataView(Bytes.buffer, Bytes.byteOffset, Bytes.byteLength);
    let Offset = Newline + 1;
    while (Offset < Bytes.length)
    {
        if (Offset + 20 > Bytes.length) throw Error('Truncated leaf');
        const [Category, X, Y, Z, Width] = Array.from({ length: 5 }, (_, Index) => View.getUint32(Offset + Index * 4, true));
        if (![1, 2].includes(Category) || Width < 8 || Width > 4096 || (Width & (Width - 1))) throw Error('Invalid leaf');
        if ([X, Y, Z].some((Coordinate, Axis) => Coordinate % Width || Coordinate + Width > Specification.Dimensions[Axis])) throw Error('Leaf outside or misaligned with volume');
        Offset += 20;
        const Count = Category === 1 ? 1 : Width ** 3;
        if (Offset + Count * 4 > Bytes.length) throw Error('Truncated samples');
        const Samples = new Float32Array(Count);
        for (let Index = 0; Index < Count; Index++)
        {
            const Distance = View.getFloat32(Offset + Index * 4, true);
            if (!Number.isFinite(Distance) || Math.abs(Distance) > Specification.Band * (1 + 1e-6)) throw Error('Invalid signed-distance sample');
            Samples[Index] = Distance;
        }
        Leaves.push({ Category, X, Y, Z, Width, Samples });
        Offset += Count * 4;
    }
    ValidateVolumeCoverage({ Specification, Leaves });
    return { Specification, Leaves };
}

export function ValidateVolumeCoverage(Volume)
{
    const { Dimensions, Band } = ValidateVolumeSpecification(Volume.Specification);
    if (!Array.isArray(Volume.Leaves)) throw Error('Invalid volume leaves');
    const Span = 2 ** Math.ceil(Math.log2(Math.max(...Dimensions)));
    const Tiers = new Map();
    let Covered = 0;
    for (const Leaf of Volume.Leaves)
    {
        const { X, Y, Z, Width } = Leaf;
        if (!Number.isInteger(Width) || Width < 8 || Width > Span || (Width & (Width - 1)) ||
            [X, Y, Z].some((Coordinate, Axis) => !Number.isInteger(Coordinate) || Coordinate < 0 || Coordinate % Width || Coordinate + Width > Dimensions[Axis])) throw Error('Invalid volume coverage extent');
        if (![1, 2].includes(Leaf.Category) || !(Leaf.Samples instanceof Float32Array) || Leaf.Samples.length !== (Leaf.Category === 1 ? 1 : Width ** 3)) throw Error('Invalid volume sample layout');
        for (const Distance of Leaf.Samples)
        {
            if (!Number.isFinite(Distance) || Math.abs(Distance) > Band * (1 + 1e-6)) throw Error('Invalid non-finite or out-of-band volume sample');
        }
        const Token = (Z * Span + Y) * Span + X;
        if (!Tiers.has(Width)) Tiers.set(Width, new Map());
        const Tier = Tiers.get(Width);
        if (Tier.has(Token)) throw Error('Overlapping volume leaves');
        Tier.set(Token, Leaf);
        Covered += Width ** 3;
    }
    for (const Leaf of Volume.Leaves)
    for (let Width = Leaf.Width * 2; Width <= Span; Width *= 2)
    {
        const Tier = Tiers.get(Width);
        if (!Tier) continue;
        const X = Leaf.X - Leaf.X % Width, Y = Leaf.Y - Leaf.Y % Width, Z = Leaf.Z - Leaf.Z % Width;
        if (Tier.has((Z * Span + Y) * Span + X)) throw Error('Overlapping volume tiers');
    }
    if (Covered !== Dimensions.reduce((Product, Count) => Product * Count, 1)) throw Error('Incomplete volume coverage');
    return Tiers;
}

export function ReadVolumeCell(Volume, X, Y, Z)
{
    if (![X, Y, Z].every((Coordinate, Axis) => Number.isInteger(Coordinate) && Coordinate >= 0 && Coordinate < Volume.Specification.Dimensions[Axis])) throw Error('Cell outside volume');
    const Leaf = Volume.Leaves.find(Entry => X >= Entry.X && X < Entry.X + Entry.Width && Y >= Entry.Y && Y < Entry.Y + Entry.Width && Z >= Entry.Z && Z < Entry.Z + Entry.Width);
    if (!Leaf) throw Error('Missing volume leaf');
    return Leaf.Category === 1 ? Leaf.Samples[0] : Leaf.Samples[(Z - Leaf.Z) * Leaf.Width ** 2 + (Y - Leaf.Y) * Leaf.Width + X - Leaf.X];
}
