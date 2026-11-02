/*====================================================================================================================================
                                            SPECTRAL REFERENCE RENDER.MJS
====================================================================================================================================*/
// 🧩 CPU reference rasterization of the spectral wave field and the global grid solve, written as PNG evidence

import fs from 'fs';
import path from 'path';
import zlib from 'zlib';
import { fileURLToPath, pathToFileURL } from 'url';

const Here = path.dirname(fileURLToPath(import.meta.url));
const SourceRoot = path.resolve(Here, '../../../Experimental/WaterSimulation/src');
const ImageRoot = path.resolve(Here, '../Images');
const Spectrum = await import(pathToFileURL(path.join(SourceRoot, 'OceanSpectrumModel.js')).href);

const TwoPi = Math.PI * 2.0;
const Gravity = 9.81;

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    PNG WRITER
------------------------------------------------------------------------------------------------------------------------------------*/

const CrcTable = new Uint32Array(256);

for (let Index = 0; Index < 256; Index++)
{
    let Value = Index;

    for (let Bit = 0; Bit < 8; Bit++)
    {
        Value = (Value & 1) ? (0xEDB88320 ^ (Value >>> 1)) : (Value >>> 1);
    }

    CrcTable[Index] = Value >>> 0;
}

function Crc32(BufferValue)
{
    let Crc = 0xFFFFFFFF;

    for (const Byte of BufferValue)
    {
        Crc = CrcTable[(Crc ^ Byte) & 0xFF] ^ (Crc >>> 8);
    }

    return (Crc ^ 0xFFFFFFFF) >>> 0;
}

function Chunk(Category, Payload)
{
    const Length = Buffer.alloc(4);
    Length.writeUInt32BE(Payload.length, 0);
    const Body = Buffer.concat([Buffer.from(Category, 'ascii'), Payload]);
    const Check = Buffer.alloc(4);
    Check.writeUInt32BE(Crc32(Body), 0);
    return Buffer.concat([Length, Body, Check]);
}

function WritePng(FilePath, Width, Height, Rgb)
{
    const Stride = Width * 3;
    const Raw = Buffer.alloc((Stride + 1) * Height);

    for (let Row = 0; Row < Height; Row++)
    {
        Raw[Row * (Stride + 1)] = 0;
        Rgb.copy(Raw, Row * (Stride + 1) + 1, Row * Stride, Row * Stride + Stride);
    }

    const Header = Buffer.alloc(13);
    Header.writeUInt32BE(Width, 0);
    Header.writeUInt32BE(Height, 4);
    Header[8] = 8;
    Header[9] = 2;
    Header[10] = 0;
    Header[11] = 0;
    Header[12] = 0;

    const Png = Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
        Chunk('IHDR', Header),
        Chunk('IDAT', zlib.deflateSync(Raw, { level: 9 })),
        Chunk('IEND', Buffer.alloc(0))
    ]);

    fs.mkdirSync(path.dirname(FilePath), { recursive: true });
    fs.writeFileSync(FilePath, Png);
    return Png.length;
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    TRANSFORM CORE
------------------------------------------------------------------------------------------------------------------------------------*/

function ReverseBits(Value, LogSize)
{
    let Result = 0;
    let Cursor = Value;

    for (let Bit = 0; Bit < LogSize; Bit++)
    {
        Result = (Result << 1) | (Cursor & 1);
        Cursor >>= 1;
    }

    return Result;
}

function HashCell(X, Y, Seed)
{
    let Value = (Math.imul(X, 1973) + Math.imul(Y, 9277) + Math.imul(Seed, 26699) + 1) >>> 0;
    Value = (Math.imul(Value, 747796405) + 2891336453) >>> 0;
    const Shift = ((Value >>> 28) + 4) >>> 0;
    const Word = Math.imul(((Value >>> Shift) ^ Value) >>> 0, 277803737) >>> 0;
    return ((Word >>> 22) ^ Word) >>> 0;
}

function ButterflyAxisField(Real, Imaginary, Size, Stage, Horizontal)
{
    const OutReal = new Float64Array(Real.length);
    const OutImaginary = new Float64Array(Imaginary.length);
    const Half = 1 << Stage;
    const Length = Half << 1;

    for (let Line = 0; Line < Size; Line++)
    {
        for (let Index = 0; Index < Size; Index++)
        {
            const Local = Index % Length;
            const Offset = Local >= Half ? Local - Half : Local;
            const Base = Index - Local;
            const EvenIndex = Base + Offset;
            const OddIndex = EvenIndex + Half;
            const Angle = TwoPi * Offset / Length;
            const Cos = Math.cos(Angle);
            const Sin = Math.sin(Angle);

            const EvenOffset = Horizontal ? Line * Size + EvenIndex : EvenIndex * Size + Line;
            const OddOffset = Horizontal ? Line * Size + OddIndex : OddIndex * Size + Line;
            const OutOffset = Horizontal ? Line * Size + Index : Index * Size + Line;

            let RotatedReal = Real[OddOffset] * Cos - Imaginary[OddOffset] * Sin;
            let RotatedImaginary = Real[OddOffset] * Sin + Imaginary[OddOffset] * Cos;

            if (Local >= Half)
            {
                RotatedReal = -RotatedReal;
                RotatedImaginary = -RotatedImaginary;
            }

            OutReal[OutOffset] = Real[EvenOffset] + RotatedReal;
            OutImaginary[OutOffset] = Imaginary[EvenOffset] + RotatedImaginary;
        }
    }

    return { Real: OutReal, Imaginary: OutImaginary };
}

function TransformGrid(Real, Imaginary, Size, LogSize)
{
    let Current = { Real: Real, Imaginary: Imaginary };

    for (let Stage = 0; Stage < LogSize; Stage++)
    {
        Current = ButterflyAxisField(Current.Real, Current.Imaginary, Size, Stage, 1);
    }

    for (let Stage = 0; Stage < LogSize; Stage++)
    {
        Current = ButterflyAxisField(Current.Real, Current.Imaginary, Size, Stage, 0);
    }

    return Current;
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    SPECTRAL FIELD
------------------------------------------------------------------------------------------------------------------------------------*/

const FieldSize = 256;
const FieldLog = 8;
const Descriptor =
{
    WindSpeed:       11.5,
    WindX:           Math.cos(38 * Math.PI / 180),
    WindZ:           Math.sin(38 * Math.PI / 180),
    ShortWaveCutoff: 0.028,
    SwellWavelength: 96.0,
    SwellStrength:   0.075
};

const WaveHeight = 2.2;
const VarianceTarget = Math.pow(WaveHeight * 0.30, 2.0);
const Scale = Spectrum.ComputeCascadeScales(Descriptor, FieldSize, VarianceTarget, 2)[1];
const Spacing = TwoPi / Scale.TileSize;
const SeedBits = (Math.round(Scale.RandomSeed * 7919.0) | 0) >>> 0;

const InitialReal = new Float64Array(FieldSize * FieldSize);
const InitialImaginary = new Float64Array(FieldSize * FieldSize);

for (let Row = 0; Row < FieldSize; Row++)
{
    const SignedRow = Row >= FieldSize / 2 ? Row - FieldSize : Row;

    for (let Column = 0; Column < FieldSize; Column++)
    {
        const SignedColumn = Column >= FieldSize / 2 ? Column - FieldSize : Column;
        const P = Spectrum.DirectionalSpectrum(SignedColumn * Spacing, SignedRow * Spacing, Descriptor);
        const Amplitude = Math.sqrt(Math.max(P * Scale.SpectrumScale * 0.5, 0.0));
        const First = (HashCell(Column, Row, SeedBits) + 1) / 4294967297.0;
        const Second = HashCell(Column + 7919, Row + 104729, (SeedBits ^ 2654435769) >>> 0) / 4294967296.0;
        const Radius = Math.sqrt(-2.0 * Math.log(First));
        InitialReal[Row * FieldSize + Column] = Radius * Math.cos(TwoPi * Second) * Amplitude;
        InitialImaginary[Row * FieldSize + Column] = Radius * Math.sin(TwoPi * Second) * Amplitude;
    }
}

function EvolveField(Time, Choppiness)
{
    const Real = new Float64Array(FieldSize * FieldSize);
    const Imaginary = new Float64Array(FieldSize * FieldSize);
    const HorizontalReal = new Float64Array(FieldSize * FieldSize);
    const HorizontalImaginary = new Float64Array(FieldSize * FieldSize);

    for (let TexelRow = 0; TexelRow < FieldSize; TexelRow++)
    {
        for (let TexelColumn = 0; TexelColumn < FieldSize; TexelColumn++)
        {
            const IndexRow = ReverseBits(TexelRow, FieldLog);
            const IndexColumn = ReverseBits(TexelColumn, FieldLog);
            const SignedRow = IndexRow >= FieldSize / 2 ? IndexRow - FieldSize : IndexRow;
            const SignedColumn = IndexColumn >= FieldSize / 2 ? IndexColumn - FieldSize : IndexColumn;
            const WaveVectorX = SignedColumn * Spacing;
            const WaveVectorZ = SignedRow * Spacing;
            const WaveNumber = Math.hypot(WaveVectorX, WaveVectorZ);
            const Omega = Math.sqrt(Gravity * WaveNumber);
            const Cos = Math.cos(Omega * Time);
            const Sin = Math.sin(Omega * Time);

            const Source = IndexRow * FieldSize + IndexColumn;
            const MirrorRow = (FieldSize - IndexRow) % FieldSize;
            const MirrorColumn = (FieldSize - IndexColumn) % FieldSize;
            const Partner = MirrorRow * FieldSize + MirrorColumn;
            const Target = TexelRow * FieldSize + TexelColumn;

            const HeightReal = InitialReal[Source] * Cos - InitialImaginary[Source] * Sin
                             + InitialReal[Partner] * Cos + InitialImaginary[Partner] * Sin;
            const HeightImaginary = InitialReal[Source] * Sin + InitialImaginary[Source] * Cos
                                  + InitialImaginary[Partner] * Cos - InitialReal[Partner] * Sin;

            const DirectionX = WaveNumber > 1e-6 ? WaveVectorX / WaveNumber : 0.0;
            const DirectionZ = WaveNumber > 1e-6 ? WaveVectorZ / WaveNumber : 0.0;

            Real[Target] = HeightReal;
            Imaginary[Target] = HeightImaginary;
            HorizontalReal[Target] = HeightImaginary * DirectionX * Choppiness;
            HorizontalImaginary[Target] = -HeightReal * DirectionX * Choppiness;

            // Horizontal z component shares the same rotation, so pack it into the mirror pass below
            HorizontalReal[Target] += 0.0;
            HorizontalImaginary[Target] += 0.0;
        }
    }

    // Second horizontal component needs its own pair; recompute inline for the render
    const DisplacementZReal = new Float64Array(FieldSize * FieldSize);
    const DisplacementZImaginary = new Float64Array(FieldSize * FieldSize);

    for (let TexelRow = 0; TexelRow < FieldSize; TexelRow++)
    {
        for (let TexelColumn = 0; TexelColumn < FieldSize; TexelColumn++)
        {
            const IndexRow = ReverseBits(TexelRow, FieldLog);
            const IndexColumn = ReverseBits(TexelColumn, FieldLog);
            const SignedRow = IndexRow >= FieldSize / 2 ? IndexRow - FieldSize : IndexRow;
            const SignedColumn = IndexColumn >= FieldSize / 2 ? IndexColumn - FieldSize : IndexColumn;
            const WaveVectorX = SignedColumn * Spacing;
            const WaveVectorZ = SignedRow * Spacing;
            const WaveNumber = Math.hypot(WaveVectorX, WaveVectorZ);
            const Omega = Math.sqrt(Gravity * WaveNumber);
            const Cos = Math.cos(Omega * Time);
            const Sin = Math.sin(Omega * Time);
            const Source = IndexRow * FieldSize + IndexColumn;
            const MirrorRow = (FieldSize - IndexRow) % FieldSize;
            const MirrorColumn = (FieldSize - IndexColumn) % FieldSize;
            const Partner = MirrorRow * FieldSize + MirrorColumn;
            const Target = TexelRow * FieldSize + TexelColumn;
            const HeightReal = InitialReal[Source] * Cos - InitialImaginary[Source] * Sin
                             + InitialReal[Partner] * Cos + InitialImaginary[Partner] * Sin;
            const HeightImaginary = InitialReal[Source] * Sin + InitialImaginary[Source] * Cos
                                  + InitialImaginary[Partner] * Cos - InitialReal[Partner] * Sin;
            const DirectionZ = WaveNumber > 1e-6 ? WaveVectorZ / WaveNumber : 0.0;
            DisplacementZReal[Target] = HeightImaginary * DirectionZ * Choppiness;
            DisplacementZImaginary[Target] = -HeightReal * DirectionZ * Choppiness;
        }
    }

    const Height = TransformGrid(Real, Imaginary, FieldSize, FieldLog);
    const DisplacementX = TransformGrid(HorizontalReal, HorizontalImaginary, FieldSize, FieldLog);
    const DisplacementZ = TransformGrid(DisplacementZReal, DisplacementZImaginary, FieldSize, FieldLog);

    return {
        Height: Height.Real,
        Horizontal: DisplacementX.Real,
        Lateral: DisplacementZ.Real
    };
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    SHADING AND OUTPUT
------------------------------------------------------------------------------------------------------------------------------------*/

function ShadeSurface(Field, OutputSize)
{
    const Rgb = Buffer.alloc(OutputSize * OutputSize * 3);
    const Half = OutputSize >> 1;
    const SunDirection = Normalize3([0.42, 0.66, -0.32]);
    const ScaleFactor = FieldSize / OutputSize;
    const TexelWorld = Scale.TileSize / FieldSize;

    // Normalize the field so the shading range is stable frame to frame
    let Peak = 0.0001;

    for (const Value of Field.Height)
    {
        Peak = Math.max(Peak, Math.abs(Value));
    }

    for (let Row = 0; Row < OutputSize; Row++)
    {
        for (let Column = 0; Column < OutputSize; Column++)
        {
            const SourceRow = Math.min(FieldSize - 1, Math.floor(Row * ScaleFactor));
            const SourceColumn = Math.min(FieldSize - 1, Math.floor(Column * ScaleFactor));
            const Index = SourceRow * FieldSize + SourceColumn;
            const Height = Field.Height[Index];
            const Left = Field.Height[SourceRow * FieldSize + Math.max(0, SourceColumn - 1)];
            const Right = Field.Height[SourceRow * FieldSize + Math.min(FieldSize - 1, SourceColumn + 1)];
            const Down = Field.Height[Math.max(0, SourceRow - 1) * FieldSize + SourceColumn];
            const Up = Field.Height[Math.min(FieldSize - 1, SourceRow + 1) * FieldSize + SourceColumn];

            const Normal = Normalize3([(Left - Right) / (2.0 * TexelWorld), 1.0, (Down - Up) / (2.0 * TexelWorld)]);
            const Light = Math.max(0.0, Normal[0] * SunDirection[0] + Normal[1] * SunDirection[1] + Normal[2] * SunDirection[2]);
            const Facing = Clamp01(Normal[1]);

            const Dxx = (Field.Horizontal[SourceRow * FieldSize + Math.min(FieldSize - 1, SourceColumn + 1)]
                       - Field.Horizontal[SourceRow * FieldSize + Math.max(0, SourceColumn - 1)]) / (2.0 * TexelWorld);
            const Dzz = (Field.Lateral[Math.min(FieldSize - 1, SourceRow + 1) * FieldSize + SourceColumn]
                       - Field.Lateral[Math.max(0, SourceRow - 1) * FieldSize + SourceColumn]) / (2.0 * TexelWorld);
            const Jacobian = (1.0 + Dxx) * (1.0 + Dzz);
            const Foam = Clamp01((0.66 - Jacobian) / 0.6);

            const NormalizedHeight = 0.5 + Height / (2.0 * Peak);
            const Body = Mix3([0.020, 0.085, 0.150], [0.075, 0.330, 0.400], Clamp01(NormalizedHeight));
            const SunContribution = Light * Light * 0.85;
            let Color = [Body[0] * (0.5 + SunContribution), Body[1] * (0.5 + SunContribution), Body[2] * (0.5 + SunContribution)];
            Color = Add3(Color, [0.55 * Math.pow(Light, 24.0), 0.60 * Math.pow(Light, 24.0), 0.66 * Math.pow(Light, 24.0)]);
            Color = Mix3(Color, [0.94, 0.97, 1.0], Foam * 0.9);
            Color = Mix3(Color, [0.30, 0.46, 0.66], (1.0 - Facing) * 0.45);

            const Offset = (Row * OutputSize + Column) * 3;
            Rgb[Offset + 0] = Math.round(Clamp01(Color[0]) * 255);
            Rgb[Offset + 1] = Math.round(Clamp01(Color[1]) * 255);
            Rgb[Offset + 2] = Math.round(Clamp01(Color[2]) * 255);
        }
    }

    return Rgb;
}

function Clamp01(Value)
{
    return Math.min(1.0, Math.max(0.0, Value));
}

function Mix3(A, B, Factor)
{
    return [A[0] + (B[0] - A[0]) * Factor, A[1] + (B[1] - A[1]) * Factor, A[2] + (B[2] - A[2]) * Factor];
}

function Add3(A, B)
{
    return [A[0] + B[0], A[1] + B[1], A[2] + B[2]];
}

function Normalize3(V)
{
    const Length = Math.hypot(V[0], V[1], V[2]) || 1.0;
    return [V[0] / Length, V[1] / Length, V[2] / Length];
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    GRID SOLVE FIELD
------------------------------------------------------------------------------------------------------------------------------------*/

function SimulateGridSolve()
{
    const Size = 192;
    const Domain = 400.0;
    const Texel = Domain / Size;
    const DeltaSeconds = 1.0 / 60.0;
    const Celerity = Math.sqrt(Gravity * 8.0);
    const Damping = 0.9985;
    let Height = new Float64Array(Size * Size);
    let Velocity = new Float64Array(Size * Size);
    const Path = [];

    for (let Step = 0; Step < 900; Step++)
    {
        const Angle = Step * 0.006;
        const BoatX = Math.sin(Angle) * 46.0;
        const BoatZ = Math.cos(Angle) * 46.0 - 20.0;
        Path.push([BoatX, BoatZ]);

        const NextHeight = new Float64Array(Size * Size);
        const NextVelocity = new Float64Array(Size * Size);

        for (let Row = 0; Row < Size; Row++)
        {
            for (let Column = 0; Column < Size; Column++)
            {
                const Index = Row * Size + Column;
                const Left = Height[Row * Size + Math.max(0, Column - 1)];
                const Right = Height[Row * Size + Math.min(Size - 1, Column + 1)];
                const Down = Height[Math.max(0, Row - 1) * Size + Column];
                const Up = Height[Math.min(Size - 1, Row + 1) * Size + Column];
                const Laplacian = (Left + Right + Down + Up - 4.0 * Height[Index]) / (Texel * Texel);
                let NewVelocity = Velocity[Index] + DeltaSeconds * Celerity * Celerity * Laplacian;
                NewVelocity *= Math.pow(Damping, DeltaSeconds * 60.0);

                const Edge = Math.min(Math.min(Column, Size - 1 - Column), Math.min(Row, Size - 1 - Row)) / Size;
                NewVelocity *= Math.min(1.0, Math.max(0.02, Edge / 0.07));

                NextVelocity[Index] = NewVelocity;
                NextHeight[Index] = Height[Index] + DeltaSeconds * NewVelocity;
            }
        }

        // Hull wake: two offset impulses at the stern
        const CenterX = Math.round((BoatX / Domain + 0.5) * Size);
        const CenterZ = Math.round((BoatZ / Domain + 0.5) * Size);

        for (let OffsetZ = -1; OffsetZ <= 1; OffsetZ++)
        {
            const Column = Math.min(Size - 1, Math.max(0, CenterX));
            const Row = Math.min(Size - 1, Math.max(0, CenterZ + OffsetZ));

            if (Row >= 0 && Row < Size && Column >= 0 && Column < Size)
            {
                NextVelocity[Row * Size + Column] -= 0.55;
            }
        }

        Height = NextHeight;
        Velocity = NextVelocity;
    }

    return { Height, Velocity, Size, Domain, Path };
}

function ShadeGridSolve(Simulation, OutputSize)
{
    const Rgb = Buffer.alloc(OutputSize * OutputSize * 3);
    const Size = Simulation.Size;
    const Texel = Simulation.Domain / Size;
    const SunDirection = Normalize3([0.45, 0.62, -0.35]);
    let Peak = 0.0001;

    for (const Value of Simulation.Height)
    {
        Peak = Math.max(Peak, Math.abs(Value));
    }

    for (let Row = 0; Row < OutputSize; Row++)
    {
        for (let Column = 0; Column < OutputSize; Column++)
        {
            const SourceRow = Math.min(Size - 1, Math.floor(Row * Size / OutputSize));
            const SourceColumn = Math.min(Size - 1, Math.floor(Column * Size / OutputSize));
            const Index = SourceRow * Size + SourceColumn;
            const Height = Simulation.Height[Index];
            const Left = Simulation.Height[SourceRow * Size + Math.max(0, SourceColumn - 1)];
            const Right = Simulation.Height[SourceRow * Size + Math.min(Size - 1, SourceColumn + 1)];
            const Down = Simulation.Height[Math.max(0, SourceRow - 1) * Size + SourceColumn];
            const Up = Simulation.Height[Math.min(Size - 1, SourceRow + 1) * Size + SourceColumn];

            const Normal = Normalize3([(Left - Right) / (2.0 * Texel), 1.0, (Down - Up) / (2.0 * Texel)]);
            const Light = Math.max(0.0, Normal[0] * SunDirection[0] + Normal[1] * SunDirection[1] + Normal[2] * SunDirection[2]);
            const Curvature = (Left + Right + Down + Up - 4.0 * Height) / (Texel * Texel);
            const Foam = Clamp01(-Curvature * 9.0);

            const NormalizedHeight = 0.5 + Height / (2.0 * Peak);
            const Deep = [0.01, 0.07, 0.13];
            const Crest = [0.16, 0.52, 0.56];
            let Color = Mix3(Deep, Crest, Clamp01(NormalizedHeight * NormalizedHeight * 1.6));
            Color = Add3(Color, [Light * 0.4, Light * 0.4, Light * 0.42]);
            Color = Mix3(Color, [0.95, 0.97, 1.0], Foam * 0.9);

            const Offset = (Row * OutputSize + Column) * 3;
            Rgb[Offset + 0] = Math.round(Clamp01(Color[0]) * 255);
            Rgb[Offset + 1] = Math.round(Clamp01(Color[1]) * 255);
            Rgb[Offset + 2] = Math.round(Clamp01(Color[2]) * 255);
        }
    }

    return { Rgb: Rgb, Peak: Peak };
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    RUN
------------------------------------------------------------------------------------------------------------------------------------*/

const OutputSize = 768;
const Field = EvolveField(6.4, 0.85);
const SurfaceBytes = WritePng(path.join(ImageRoot, 'SpectralSurfaceReference.png'), OutputSize, OutputSize,
                              ShadeSurface(Field, OutputSize));

const Simulation = SimulateGridSolve();
const Grid = ShadeGridSolve(Simulation, OutputSize);
const GridBytes = WritePng(path.join(ImageRoot, 'GridSolveReference.png'), OutputSize, OutputSize, Grid.Rgb);

let FieldPeak = 0.0;
let FieldSum = 0.0;

for (const Value of Field.Height)
{
    FieldPeak = Math.max(FieldPeak, Math.abs(Value));
    FieldSum += Value * Value;
}

console.log('--- reference render ---');
console.log('spectral tile             :', Scale.TileSize, 'm, scale', Scale.SpectrumScale.toExponential(3));
console.log('spectral peak |height|    :', FieldPeak.toFixed(3), 'm');
console.log('spectral RMS height       :', Math.sqrt(FieldSum / Field.Height.length).toFixed(3), 'm');
console.log('grid solve peak |height|  :', Grid.Peak.toFixed(3), 'm after 900 steps at 192²');
console.log('wrote                     :', 'SpectralSurfaceReference.png', SurfaceBytes, 'bytes');
console.log('wrote                     :', 'GridSolveReference.png', GridBytes, 'bytes');
