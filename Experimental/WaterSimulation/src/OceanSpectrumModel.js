/*====================================================================================================================================
                                                    OCEAN SPECTRUM MODEL.JS
====================================================================================================================================*/
// 🧩 CPU twin of the GPU directional spectrum; supplies the variance normalization and the swept component table

const Gravity = 9.81;
const TwoPi = 6.28318530718;

// 📝 Mirrors SpectrumInitialize.frag; any change here must land in the shader too
export function DirectionalSpectrum(WaveX, WaveZ, Descriptor)
{
    const WaveNumber = Math.hypot(WaveX, WaveZ);

    if (WaveNumber < 0.00002)
    {
        return 0.0;
    }

    const LargestWave = (Descriptor.WindSpeed * Descriptor.WindSpeed) / Gravity;
    const Alignment = Math.max((WaveX * Descriptor.WindX + WaveZ * Descriptor.WindZ) / WaveNumber, 0.0);
    const Envelope = Math.exp(-1.0 / Math.max(WaveNumber * WaveNumber * LargestWave * LargestWave, 0.000001));
    let WindSea = (Envelope * Math.pow(Alignment, 2.4)) / Math.pow(WaveNumber, 4.0);
    WindSea *= Math.exp(-WaveNumber * WaveNumber * Descriptor.ShortWaveCutoff * Descriptor.ShortWaveCutoff);

    const SwellWaveNumber = TwoPi / Math.max(Descriptor.SwellWavelength, 1.0);
    const SwellOffset = (WaveNumber - SwellWaveNumber) / (SwellWaveNumber * 0.42);
    const SwellRing = Math.exp(-(SwellOffset * SwellOffset));
    const Swell = (Descriptor.SwellStrength * SwellRing * Math.pow(Alignment, 12.0)) / Math.max(Math.pow(WaveNumber, 3.0), 0.000001);

    return WindSea + Swell;
}

// 📝 A 64² stand-in transform is enough to measure how much of the spectral variance survives reconstruction
function MeasureReconstruction(Descriptor, TileSize, SpectrumScale, Size)
{
    const LogSize = Math.round(Math.log2(Size));
    const Spacing = TwoPi / TileSize;
    const Real = new Float64Array(Size * Size);
    const Imaginary = new Float64Array(Size * Size);

    for (let Row = 0; Row < Size; Row++)
    {
        const SignedRow = Row >= Size / 2 ? Row - Size : Row;

        for (let Column = 0; Column < Size; Column++)
        {
            const SignedColumn = Column >= Size / 2 ? Column - Size : Column;
            const P = DirectionalSpectrum(SignedColumn * Spacing, SignedRow * Spacing, Descriptor);
            const Amplitude = Math.sqrt(Math.max(P * SpectrumScale * 0.5, 0.0));
            const Angle = 2.399963 * (Row * Size + Column);
            Real[Row * Size + Column] = Math.cos(Angle) * Amplitude;
            Imaginary[Row * Size + Column] = Math.sin(Angle) * Amplitude;
        }
    }

    let PredictedVariance = 0.0;

    for (let Index = 0; Index < Real.length; Index++)
    {
        PredictedVariance += 2.0 * (Real[Index] * Real[Index] + Imaginary[Index] * Imaginary[Index]);
    }

    // Hermitian completion, then an unnormalized inverse transform of both axes
    const CompleteReal = new Float64Array(Real.length);
    const CompleteImaginary = new Float64Array(Real.length);

    for (let Row = 0; Row < Size; Row++)
    {
        for (let Column = 0; Column < Size; Column++)
        {
            const MirrorRow = (Size - Row) % Size;
            const MirrorColumn = (Size - Column) % Size;
            CompleteReal[Row * Size + Column] = Real[Row * Size + Column] + Real[MirrorRow * Size + MirrorColumn];
            CompleteImaginary[Row * Size + Column] = Imaginary[Row * Size + Column] - Imaginary[MirrorRow * Size + MirrorColumn];
        }
    }

    let FieldReal = CompleteReal;
    let FieldImaginary = CompleteImaginary;

    for (let Axis = 0; Axis < 2; Axis++)
    {
        for (let Stage = 0; Stage < LogSize; Stage++)
        {
            const Half = 1 << Stage;
            const Length = Half << 1;
            const NextReal = new Float64Array(Size * Size);
            const NextImaginary = new Float64Array(Size * Size);

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
                    const EvenOffset = Axis === 0 ? Line * Size + EvenIndex : EvenIndex * Size + Line;
                    const OddOffset = Axis === 0 ? Line * Size + OddIndex : OddIndex * Size + Line;
                    const Target = Axis === 0 ? Line * Size + Index : Index * Size + Line;

                    let RotatedReal = FieldReal[OddOffset] * Cos - FieldImaginary[OddOffset] * Sin;
                    let RotatedImaginary = FieldReal[OddOffset] * Sin + FieldImaginary[OddOffset] * Cos;

                    if (Local >= Half)
                    {
                        RotatedReal = -RotatedReal;
                        RotatedImaginary = -RotatedImaginary;
                    }

                    NextReal[Target] = FieldReal[EvenOffset] + RotatedReal;
                    NextImaginary[Target] = FieldImaginary[EvenOffset] + RotatedImaginary;
                }
            }

            FieldReal = NextReal;
            FieldImaginary = NextImaginary;
        }
    }

    let MeasuredVariance = 0.0;

    for (let Index = 0; Index < FieldReal.length; Index++)
    {
        MeasuredVariance += FieldReal[Index] * FieldReal[Index];
    }

    MeasuredVariance /= Size * Size;
    return PredictedVariance > 0.0 ? MeasuredVariance / PredictedVariance : 1.0;
}

export const CascadeProfile =
[
    { TileSize:  46.0, VarianceShare: 0.30, RandomSeed: 11.7 },
    { TileSize: 148.0, VarianceShare: 0.42, RandomSeed: 37.3 },
    { TileSize: 470.0, VarianceShare: 0.28, RandomSeed: 71.9 }
];

// 💡 Total variance of the reconstructed field equals Scale * ΣP, so the scale is solved, not guessed
export function ComputeCascadeScales(Descriptor, Resolution, VarianceTarget, ActiveCascadeCount)
{
    const Scales = [];
    const Size = Resolution;

    for (let Cascade = 0; Cascade < ActiveCascadeCount; Cascade++)
    {
        const Profile = CascadeProfile[Cascade];
        const Spacing = TwoPi / Profile.TileSize;
        let Sum = 0.0;

        for (let Row = 0; Row < Size; Row++)
        {
            const SignedRow = Row >= Size / 2 ? Row - Size : Row;

            for (let Column = 0; Column < Size; Column++)
            {
                const SignedColumn = Column >= Size / 2 ? Column - Size : Column;
                Sum += DirectionalSpectrum(SignedColumn * Spacing, SignedRow * Spacing, Descriptor);
            }
        }

        // 📝 σ² = 2·Scale·ΣP, but the discrete reconstruction keeps only part of it (aliasing and the
        //    Gaussian realization), so the surviving fraction is measured and compensated per cascade
        const Share = Profile.VarianceShare;
        const IdealScale = Sum > 0.0 ? (VarianceTarget * Share) / (2.0 * Sum) : 0.0;
        const Retention = MeasureReconstruction(Descriptor, Profile.TileSize, IdealScale, 64);
        const Compensation = Math.min(1.8, Math.max(0.55, 1.0 / Math.max(Retention, 0.35)));
        Scales.push({
            TileSize:      Profile.TileSize,
            SpectrumScale: IdealScale * Compensation,
            Retention:     Retention,
            Compensation:  Compensation,
            RandomSeed:    Profile.RandomSeed
        });
    }

    return Scales;
}

// 📝 Validation helper: reconstruct one texel of the displaced surface on the CPU from the same coefficients
export function SampleSpectrumVariance(Descriptor, Resolution, SpectrumScale)
{
    const Size = Resolution;
    const Spacing = TwoPi / Descriptor.TileSize;
    let Variance = 0.0;

    for (let Row = 0; Row < Size; Row++)
    {
        const SignedRow = Row >= Size / 2 ? Row - Size : Row;

        for (let Column = 0; Column < Size; Column++)
        {
            const SignedColumn = Column >= Size / 2 ? Column - Size : Column;
            Variance += DirectionalSpectrum(SignedColumn * Spacing, SignedRow * Spacing, Descriptor) * SpectrumScale;
        }
    }

    return Variance;
}

export function DescribeWaves(WaveHeight, WindSpeed, DirectionRadians)
{
    const PeakWavelength = (TwoPi * WindSpeed * WindSpeed) / Gravity;
    return {
        PeakWavelength:  PeakWavelength,
        SignificantHeight: WaveHeight * 2.0,
        WindX:           Math.cos(DirectionRadians),
        WindZ:           Math.sin(DirectionRadians)
    };
}
