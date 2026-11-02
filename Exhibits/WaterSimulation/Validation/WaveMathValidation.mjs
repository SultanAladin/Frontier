/*====================================================================================================================================
                                                WAVE MATH VALIDATION.MJS
====================================================================================================================================*/
// 🧩 Mirrors the shader algorithms on the CPU and checks them against direct transforms and stability bounds

import path from 'path';
import { fileURLToPath, pathToFileURL } from 'url';

const Here = path.dirname(fileURLToPath(import.meta.url));
const SourceRoot = path.resolve(Here, '../../../Experimental/WaterSimulation/src');
const Module = (Name) => pathToFileURL(path.join(SourceRoot, Name)).href;

const Spectrum = await import(Module('OceanSpectrumModel.js'));
const Gravity = 9.81;
const TwoPi = Math.PI * 2.0;

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    BUTTERFLY MIRROR
------------------------------------------------------------------------------------------------------------------------------------*/

// Mirror of HashCell in SpectrumInitialize.frag (PCG integer hash)
function HashCell(X, Y, Seed)
{
    let Value = (Math.imul(X, 1973) + Math.imul(Y, 9277) + Math.imul(Seed, 26699) + 1) >>> 0;
    Value = (Math.imul(Value, 747796405) + 2891336453) >>> 0;
    const Shift = ((Value >>> 28) + 4) >>> 0;
    const Word = Math.imul(((Value >>> Shift) ^ Value) >>> 0, 277803737) >>> 0;
    return ((Word >>> 22) ^ Word) >>> 0;
}

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

// 📝 Line-for-line copy of FftButterfly.frag applied to one axis of an interleaved complex grid
function ButterflyAxis(Field, Size, Stage, Sign, Horizontal)
{
    const Output = new Float64Array(Field.length);
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
            const Angle = Sign * TwoPi * Offset / Length;
            const TwiddleReal = Math.cos(Angle);
            const TwiddleImaginary = Math.sin(Angle);

            const EvenOffset = Horizontal === 1 ? (Line * Size + EvenIndex) * 2 : (EvenIndex * Size + Line) * 2;
            const OddOffset = Horizontal === 1 ? (Line * Size + OddIndex) * 2 : (OddIndex * Size + Line) * 2;
            const OutOffset = Horizontal === 1 ? (Line * Size + Index) * 2 : (Index * Size + Line) * 2;

            const OddReal = Field[OddOffset];
            const OddImaginary = Field[OddOffset + 1];
            let RotatedReal = OddReal * TwiddleReal - OddImaginary * TwiddleImaginary;
            let RotatedImaginary = OddReal * TwiddleImaginary + OddImaginary * TwiddleReal;

            if (Local >= Half)
            {
                RotatedReal = -RotatedReal;
                RotatedImaginary = -RotatedImaginary;
            }

            Output[OutOffset] = Field[EvenOffset] + RotatedReal;
            Output[OutOffset + 1] = Field[EvenOffset + 1] + RotatedImaginary;
        }
    }

    return Output;
}

function TransformGrid(Field, Size, LogSize, Sign)
{
    let Current = Field;

    for (let Stage = 0; Stage < LogSize; Stage++)
    {
        Current = ButterflyAxis(Current, Size, Stage, Sign, 1);
    }

    for (let Stage = 0; Stage < LogSize; Stage++)
    {
        Current = ButterflyAxis(Current, Size, Stage, Sign, 0);
    }

    return Current;
}

function DirectInverseTransform(Values, Size)
{
    const Output = new Float64Array(Size * Size * 2);

    for (let Row = 0; Row < Size; Row++)
    {
        for (let Column = 0; Column < Size; Column++)
        {
            let Real = 0.0;
            let Imaginary = 0.0;

            for (let FrequencyRow = 0; FrequencyRow < Size; FrequencyRow++)
            {
                for (let FrequencyColumn = 0; FrequencyColumn < Size; FrequencyColumn++)
                {
                    const Angle = TwoPi * (Row * FrequencyRow + Column * FrequencyColumn) / Size;
                    const Cosine = Math.cos(Angle);
                    const Sine = Math.sin(Angle);
                    const RealValue = Values[(FrequencyRow * Size + FrequencyColumn) * 2];
                    const ImaginaryValue = Values[(FrequencyRow * Size + FrequencyColumn) * 2 + 1];

                    Real += RealValue * Cosine - ImaginaryValue * Sine;
                    Imaginary += RealValue * Sine + ImaginaryValue * Cosine;
                }
            }

            Output[(Row * Size + Column) * 2] = Real;
            Output[(Row * Size + Column) * 2 + 1] = Imaginary;
        }
    }

    return Output;
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    CHECKS
------------------------------------------------------------------------------------------------------------------------------------*/

const Report = [];
let Failures = 0;

function Check(Label, Passed, Detail)
{
    if (!Passed)
    {
        Failures++;
    }

    Report.push({ Label: Label, Result: Passed ? 'PASS' : 'FAIL', Detail: Detail });
}

function RandomComplex(Size, Seed)
{
    const Values = new Float64Array(Size * Size * 2);

    for (let Index = 0; Index < Values.length; Index++)
    {
        const Value = Math.sin((Index + 1) * 12.9898 + Seed) * 43758.5453;
        Values[Index] = (Value - Math.floor(Value)) * 2.0 - 1.0;
    }

    return Values;
}

// --- 1. Butterfly transform against a direct inverse DFT ---
const SmallSize = 16;
const SmallLog = 4;
const Natural = RandomComplex(SmallSize, 3.3);
const BitReversed = new Float64Array(Natural.length);

for (let Row = 0; Row < SmallSize; Row++)
{
    for (let Column = 0; Column < SmallSize; Column++)
    {
        const SourceRow = ReverseBits(Row, SmallLog);
        const SourceColumn = ReverseBits(Column, SmallLog);
        BitReversed[(Row * SmallSize + Column) * 2] = Natural[(SourceRow * SmallSize + SourceColumn) * 2];
        BitReversed[(Row * SmallSize + Column) * 2 + 1] = Natural[(SourceRow * SmallSize + SourceColumn) * 2 + 1];
    }
}

const Transformed = TransformGrid(BitReversed, SmallSize, SmallLog, 1.0);
const Reference = DirectInverseTransform(Natural, SmallSize);

let MaximumError = 0.0;
let MaximumMagnitude = 0.0;

for (let Index = 0; Index < Reference.length; Index++)
{
    MaximumError = Math.max(MaximumError, Math.abs(Transformed[Index] - Reference[Index]));
    MaximumMagnitude = Math.max(MaximumMagnitude, Math.abs(Reference[Index]));
}

const RelativeError = MaximumError / Math.max(MaximumMagnitude, 1e-12);
Check('FFT butterfly vs direct inverse DFT (16²)', RelativeError < 1e-9,
      `max relative error ${RelativeError.toExponential(2)}`);

// --- 2. Spectrum calibration: reconstructed RMS height matches the requested sea state ---
const LakeSize = 64;
const LakeLog = 6;
const Descriptor =
{
    WindSpeed:       9.5,
    WindX:           Math.cos(32 * Math.PI / 180),
    WindZ:           Math.sin(32 * Math.PI / 180),
    ShortWaveCutoff: 0.028,
    SwellWavelength: 96.0,
    SwellStrength:   0.075
};

const WaveHeight = 1.45;
const VarianceTarget = Math.pow(WaveHeight * 0.30, 2.0);
const Scales = Spectrum.ComputeCascadeScales(Descriptor, LakeSize, VarianceTarget, 3);

let CombinedVariance = 0.0;
let CombinedPeak = 0.0;
let HermitianResidual = 0.0;

for (const Scale of Scales)
{
    const Spacing = TwoPi / Scale.TileSize;
    const SeedBits = (Math.round(Scale.RandomSeed * 7919.0) | 0) >>> 0;
    const Initial = new Float64Array(LakeSize * LakeSize * 2);

    // Amplitude per frequency index, exactly as SpectrumInitialize.frag computes it
    for (let Row = 0; Row < LakeSize; Row++)
    {
        const SignedRow = Row >= LakeSize / 2 ? Row - LakeSize : Row;

        for (let Column = 0; Column < LakeSize; Column++)
        {
            const SignedColumn = Column >= LakeSize / 2 ? Column - LakeSize : Column;
            const P = Spectrum.DirectionalSpectrum(SignedColumn * Spacing, SignedRow * Spacing, Descriptor);
            const Amplitude = Math.sqrt(Math.max(P * Scale.SpectrumScale * 0.5, 0.0));
            const FirstBits = HashCell(Column, Row, SeedBits);
            const SecondBits = HashCell(Column + 7919, Row + 104729, (SeedBits ^ 2654435769) >>> 0);
            const First = Math.max(FirstBits / 4294967295.0, 0.000001);
            const Second = SecondBits / 4294967295.0;
            const Radius = Math.sqrt(-2.0 * Math.log(First));
            Initial[(Row * LakeSize + Column) * 2] = Radius * Math.cos(TwoPi * Second) * Amplitude;
            Initial[(Row * LakeSize + Column) * 2 + 1] = Radius * Math.sin(TwoPi * Second) * Amplitude;
        }
    }

    // Bit-reversed storage plus the conjugate partner, mirroring SpectrumEvolution.frag
    const Evolved = new Float64Array(Initial.length);

    for (let TexelRow = 0; TexelRow < LakeSize; TexelRow++)
    {
        for (let TexelColumn = 0; TexelColumn < LakeSize; TexelColumn++)
        {
            const IndexRow = ReverseBits(TexelRow, LakeLog);
            const IndexColumn = ReverseBits(TexelColumn, LakeLog);
            const MirrorRow = (LakeSize - IndexRow) % LakeSize;
            const MirrorColumn = (LakeSize - IndexColumn) % LakeSize;
            // Initial is stored by frequency index, so the conjugate partner is read directly at -k
            const Source = (IndexRow * LakeSize + IndexColumn) * 2;
            const Partner = (MirrorRow * LakeSize + MirrorColumn) * 2;
            const Target = (TexelRow * LakeSize + TexelColumn) * 2;

            Evolved[Target + 0] = Initial[Source + 0] + Initial[Partner + 0];
            Evolved[Target + 1] = Initial[Source + 1] - Initial[Partner + 1];
        }
    }

    // 📝 The butterfly output is the Tessendorf wave sum, matching SpectralResolve.frag (no 1/N² factor)
    const Field = TransformGrid(Evolved, LakeSize, LakeLog, 1.0);
    let SumSquares = 0.0;
    let SumImaginary = 0.0;

    for (let Index = 0; Index < Field.length; Index += 2)
    {
        const Height = Field[Index];
        SumSquares += Height * Height;
        SumImaginary += Math.abs(Field[Index + 1]);
        CombinedPeak = Math.max(CombinedPeak, Math.abs(Height));
    }

    HermitianResidual = Math.max(HermitianResidual, SumImaginary / (LakeSize * LakeSize));
    CombinedVariance += SumSquares / (LakeSize * LakeSize);
}

const ReconstructedRms = Math.sqrt(CombinedVariance);
const TargetRms = Math.sqrt(VarianceTarget);
const RmsRatio = ReconstructedRms / TargetRms;
Check('Spectral RMS matches the requested sea state', RmsRatio > 0.75 && RmsRatio < 1.25,
      `target ${TargetRms.toFixed(3)} m, reconstructed ${ReconstructedRms.toFixed(3)} m (ratio ${RmsRatio.toFixed(2)}), peak ${CombinedPeak.toFixed(2)} m`);
Check('Reconstructed surface is real (Hermitian symmetry)', HermitianResidual < 0.02 * TargetRms,
      `mean |imaginary| ${HermitianResidual.toExponential(2)} m`);

// --- 3. Grid wave-equation stability under the shipped step constants ---
function SimulateWaveField(Steps, DeltaSeconds, Resolution, Domain, CelerityScale, Damping, SeedStrength)
{
    const Height = new Float64Array(Resolution * Resolution);
    const Velocity = new Float64Array(Resolution * Resolution);
    const Texel = Domain / Resolution;
    const Center = Resolution >> 1;
    Height[Center * Resolution + Center] = SeedStrength;
    let MaximumHeight = 0.0;

    for (let Step = 0; Step < Steps; Step++)
    {
        const NextVelocity = new Float64Array(Resolution * Resolution);
        const NextHeight = new Float64Array(Resolution * Resolution);

        for (let Row = 0; Row < Resolution; Row++)
        {
            for (let Column = 0; Column < Resolution; Column++)
            {
                const Index = Row * Resolution + Column;
                const Left = Height[Row * Resolution + Math.max(0, Column - 1)];
                const Right = Height[Row * Resolution + Math.min(Resolution - 1, Column + 1)];
                const Down = Height[Math.max(0, Row - 1) * Resolution + Column];
                const Up = Height[Math.min(Resolution - 1, Row + 1) * Resolution + Column];
                const Laplacian = (Left + Right + Down + Up - 4.0 * Height[Index]) / (Texel * Texel);
                const Celerity = CelerityScale * Math.sqrt(Gravity * 8.0);
                let NewVelocity = Velocity[Index] + DeltaSeconds * Celerity * Celerity * Laplacian;
                NewVelocity *= Math.pow(Damping, DeltaSeconds * 60.0);
                NextVelocity[Index] = NewVelocity;
                NextHeight[Index] = (Height[Index] + DeltaSeconds * NewVelocity) * 0.985;
                MaximumHeight = Math.max(MaximumHeight, Math.abs(NextHeight[Index]));
            }
        }

        Height.set(NextHeight);
        Velocity.set(NextVelocity);
    }

    return MaximumHeight;
}

const ShallowPeak = SimulateWaveField(600, 1.0 / 60.0, 128, 400, 1.0, 0.9985, 1.5);
Check('Grid solve stable at 512² constants (celerity 1.0, damping 0.9985)',
      Number.isFinite(ShallowPeak) && ShallowPeak < 40.0, `peak |height| ${ShallowPeak.toFixed(3)} m after 600 steps`);

const RipplePeak = SimulateWaveField(600, 1.0 / 60.0, 256, 400, 0.42, 0.962, 0.35);
Check('Ripple solve stable at 1024² constants (celerity 0.42, damping 0.962)',
      Number.isFinite(RipplePeak) && RipplePeak < 6.0, `peak |height| ${RipplePeak.toFixed(3)} m after 600 steps`);

// --- 4. Gerstner crest sum stays non-looping across the shipped steepness range ---
const { GerstnerWaveSet } = await import(Module('GerstnerWaveSet.js'));
const Waves = new GerstnerWaveSet();
Waves.Configure(WaveHeight, 32 * Math.PI / 180, 1.6);
let MaximumSteepness = 0.0;

for (let Index = 0; Index < Waves.Count; Index++)
{
    const Wavelength = Waves.Direction[Index * 4 + 2];
    const Steepness = Waves.Direction[Index * 4 + 3];
    const Amplitude = Waves.Shape[Index * 4 + 0];
    const WaveNumber = TwoPi / Wavelength;
    const ChopScale = Steepness / (WaveNumber * Amplitude * Waves.Count);
    MaximumSteepness += ChopScale * WaveNumber * Amplitude;
}

Check('Gerstner crest tightness below the self-intersection limit', MaximumSteepness <= 1.0,
      `max sum(Q·k·A) = ${MaximumSteepness.toFixed(3)}`);

console.log('--- wave mathematics validation ---');

for (const Entry of Report)
{
    console.log(`${Entry.Result.padEnd(5)} ${Entry.Label}`);
    console.log(`      ${Entry.Detail}`);
}

console.log('result:', Failures === 0 ? 'PASS' : `${Failures} FAILURES`);
process.exitCode = Failures === 0 ? 0 : 1;
