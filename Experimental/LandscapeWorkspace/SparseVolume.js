/*==========================================================================================================================================
                                                            SPARSEVOLUME.JS
==========================================================================================================================================*/
// 🧩 Adaptive truncated signed-distance volume encoding at authored voxel dimensions.

import { VolumeDistance } from './DistanceField.js';
import { LayerVerticalBounds } from './LandformStack.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                     VOLUME ENCODING
//------------------------------------------------------------------------------------------------------------------------

export function VolumeSpecification(Layer, Region, Horizontal = Layer.Resolution, Vertical = Layer.Vertical)
{
    if (![Horizontal, Vertical].every(Count => Number.isInteger(Count) && Count >= 8 && Count <= 4096 && Count % 8 === 0))
    {
        throw Error('Volume dimensions must be multiples of eight between 8 and 4096.');
    }
    const [MinimumY,MaximumY] = LayerVerticalBounds(Layer,Region);
    const Pitch = [(Region.X1 - Region.X0) / Horizontal, (MaximumY-MinimumY) / Vertical, (Region.Z1 - Region.Z0) / Horizontal];
    return {
        Format: 'FrontierSparseTSDF/1',
        Dimensions: [Horizontal, Vertical, Horizontal],
        Origin: [Region.X0, MinimumY, Region.Z0],
        Pitch,
        Band: Math.max(...Pitch) * 2,
        BrickWidth: 8,
        Region,
        Layer,
        Encoding: 'JSON line then binary leaves; little endian; X fastest, Y next, Z last; cell centres',
        LeafLayout: 'u32 category, u32 X, u32 Y, u32 Z, u32 width; category 1: f32 constant; category 2: width cubed f32 samples',
        DistanceMeaning: 'World-unit CSG signed distance bound clamped to [-Band,+Band]; all in-bounds cells covered'
    };
}

export function* EncodeSparseVolume(Specification, MaximumBytes = 256 * 1024 * 1024)
{
    const { Dimensions, Origin, Pitch, Band, Layer, Region } = Specification;
    const Header = new TextEncoder().encode(JSON.stringify(Specification) + '\n');
    const Total = Dimensions.reduce((Product, Count) => Product * Count, 1);
    let Bytes = Header.byteLength;
    let Covered = 0;
    let Bricks = 0;
    let Uniform = 0;
    let Evaluations = 0;
    const Pending = [[0, 0, 0, 2 ** Math.ceil(Math.log2(Math.max(...Dimensions)))]];
    const Progress = () => ({ Covered, Total, Bricks, Uniform, Evaluations, Bytes });
    yield { Bytes: Header, Progress: Progress() };
    while (Pending.length)
    {
        const [X, Y, Z, Width] = Pending.pop();
        if (X >= Dimensions[0] || Y >= Dimensions[1] || Z >= Dimensions[2]) continue;
        const Interior = X + Width <= Dimensions[0] && Y + Width <= Dimensions[1] && Z + Width <= Dimensions[2];
        const Centre = [X + Width / 2, Y + Width / 2, Z + Width / 2].map((Coordinate, Axis) => Origin[Axis] + Coordinate * Pitch[Axis]);
        const Distance = VolumeDistance(Layer, ...Centre, Region);
        Evaluations++;
        const Radius = Math.hypot(...Pitch.map(Spacing => Spacing * Width / 2));
        // 📝 CSG is 1-Lipschitz; the enclosing sphere proves every cell is beyond the truncation band.
        const Constant = Interior && Math.abs(Distance) > Radius + Band + 1e-6;
        if (!Constant && Width > 8)
        {
            const Half = Width / 2;
            for (let ΔZ = 0; ΔZ < 2; ΔZ++)
            for (let ΔY = 0; ΔY < 2; ΔY++)
            for (let ΔX = 0; ΔX < 2; ΔX++) Pending.push([X + ΔX * Half, Y + ΔY * Half, Z + ΔZ * Half, Half]);
            continue;
        }
        const Chunk = new Uint8Array(20 + (Constant ? 4 : Width ** 3 * 4));
        const View = new DataView(Chunk.buffer);
        [Constant ? 1 : 2, X, Y, Z, Width].forEach((Number, Index) => View.setUint32(Index * 4, Number, true));
        if (Constant)
        {
            View.setFloat32(20, Math.sign(Distance) * Band, true);
            Uniform++;
        }
        else
        {
            let Offset = 20;
            for (let ΔZ = 0; ΔZ < Width; ΔZ++)
            for (let ΔY = 0; ΔY < Width; ΔY++)
            for (let ΔX = 0; ΔX < Width; ΔX++)
            {
                const Sample = VolumeDistance(Layer,
                    Origin[0] + (X + ΔX + 0.5) * Pitch[0],
                    Origin[1] + (Y + ΔY + 0.5) * Pitch[1],
                    Origin[2] + (Z + ΔZ + 0.5) * Pitch[2],Region);
                View.setFloat32(Offset, Math.max(-Band, Math.min(Band, Sample)), true);
                Offset += 4;
                Evaluations++;
            }
            Bricks++;
        }
        Bytes += Chunk.byteLength;
        if (Bytes > MaximumBytes) throw Error('Export exceeds the 256 MiB uncompressed safety budget. Reduce resolution or select a smaller region.');
        Covered += Width ** 3;
        yield { Bytes: Chunk, Progress: Progress() };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     VOLUME DECODING
//------------------------------------------------------------------------------------------------------------------------

export function DecodeSparseVolume(Bytes)
{
    const Newline = Bytes.indexOf(10);
    if (Newline < 0 || Newline > 5e6) throw Error('Invalid volume header');
    const Specification = JSON.parse(new TextDecoder().decode(Bytes.subarray(0, Newline)));
    if (Specification.Format !== 'FrontierSparseTSDF/1') throw Error('Unsupported volume format');
    const Leaves = [];
    const View = new DataView(Bytes.buffer, Bytes.byteOffset, Bytes.byteLength);
    let Offset = Newline + 1;
    while (Offset < Bytes.length)
    {
        if (Offset + 20 > Bytes.length) throw Error('Truncated leaf');
        const [Category, X, Y, Z, Width] = Array.from({ length: 5 }, (_, Index) => View.getUint32(Offset + Index * 4, true));
        if (![1, 2].includes(Category) || Width < 8 || Width > 4096 || (Width & (Width - 1))) throw Error('Invalid leaf');
        Offset += 20;
        const Count = Category === 1 ? 1 : Width ** 3;
        if (Offset + Count * 4 > Bytes.length) throw Error('Truncated samples');
        const Samples = new Float32Array(Count);
        for (let Index = 0; Index < Count; Index++) Samples[Index] = View.getFloat32(Offset + Index * 4, true);
        Leaves.push({ Category, X, Y, Z, Width, Samples });
        Offset += Count * 4;
    }
    return { Specification, Leaves };
}

export function ReadVolumeCell(Volume, X, Y, Z)
{
    if (![X, Y, Z].every((Coordinate, Axis) => Number.isInteger(Coordinate) && Coordinate >= 0 && Coordinate < Volume.Specification.Dimensions[Axis])) throw Error('Cell outside volume');
    const Leaf = Volume.Leaves.find(Entry => X >= Entry.X && X < Entry.X + Entry.Width && Y >= Entry.Y && Y < Entry.Y + Entry.Width && Z >= Entry.Z && Z < Entry.Z + Entry.Width);
    if (!Leaf) throw Error('Missing volume leaf');
    return Leaf.Category === 1 ? Leaf.Samples[0] : Leaf.Samples[(Z - Leaf.Z) * Leaf.Width ** 2 + (Y - Leaf.Y) * Leaf.Width + X - Leaf.X];
}
