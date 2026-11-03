/*============================================================================================================================================
                                                         NUMERICFIELD.JS
============================================================================================================================================*/
// 🧩 Deterministic lattice noise and fractal bases shared by crust elevation and ocean forcing

//------------------------------------------------------------------------------------------------------------------------
//                                                      HASH PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

export function EvaluateLatticeHash(CellX, CellZ, Seed)
{
    let Accumulator = Math.imul(CellX, 374761393) ^ Math.imul(CellZ, 668265263) ^ Math.imul(Seed, 1274126177);
    Accumulator = Math.imul(Accumulator ^ (Accumulator >>> 13), 1274126177);
    Accumulator ^= Accumulator >>> 16;
    return (Accumulator >>> 0) / 4294967296;
}

export function EvaluateQuintic(Parameter)
{
    return Parameter * Parameter * Parameter * (Parameter * (Parameter * 6 - 15) + 10);
}

export function EvaluateSmoothStep(LowerBoundary, UpperBoundary, Sample)
{
    const Span = UpperBoundary - LowerBoundary;
    const Parameter = Math.max(0, Math.min(1, (Sample - LowerBoundary) / (Math.abs(Span) < 1e-9 ? 1e-9 : Span)));
    return Parameter * Parameter * (3 - 2 * Parameter);
}

export function EvaluateSmoothMinimum(First, Second, BlendRadius)
{
    if (BlendRadius <= 0) return Math.min(First, Second);
    const Half = Math.max(0, BlendRadius - Math.abs(First - Second)) / BlendRadius;
    return Math.min(First, Second) - Half * Half * BlendRadius * 0.25;
}

export function EvaluateSmoothMaximum(First, Second, BlendRadius)
{
    if (BlendRadius <= 0) return Math.max(First, Second);
    const Half = Math.max(0, BlendRadius - Math.abs(First - Second)) / BlendRadius;
    return Math.max(First, Second) + Half * Half * BlendRadius * 0.25;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     NOISE BASES
//------------------------------------------------------------------------------------------------------------------------

export function EvaluateValueNoise(SampleX, SampleZ, Seed)
{
    const CellX = Math.floor(SampleX);
    const CellZ = Math.floor(SampleZ);
    const FractionX = SampleX - CellX;
    const FractionZ = SampleZ - CellZ;
    const WeightX = EvaluateQuintic(FractionX);
    const WeightZ = EvaluateQuintic(FractionZ);
    const SouthWest = EvaluateLatticeHash(CellX, CellZ, Seed);
    const SouthEast = EvaluateLatticeHash(CellX + 1, CellZ, Seed);
    const NorthWest = EvaluateLatticeHash(CellX, CellZ + 1, Seed);
    const NorthEast = EvaluateLatticeHash(CellX + 1, CellZ + 1, Seed);
    const South = SouthWest + (SouthEast - SouthWest) * WeightX;
    const North = NorthWest + (NorthEast - NorthWest) * WeightX;
    return South + (North - South) * WeightZ;
}

export function EvaluateFractalNoise(SampleX, SampleZ, Seed, OctaveCount = 5, Lacunarity = 2.03, Gain = 0.5)
{
    let Amplitude = 1;
    let Frequency = 1;
    let Accumulator = 0;
    let Weight = 0;
    for (let OctaveIndex = 0; OctaveIndex < OctaveCount; ++OctaveIndex)
    {
        Accumulator += Amplitude * (EvaluateValueNoise(SampleX * Frequency, SampleZ * Frequency, Seed + OctaveIndex * 1013) * 2 - 1);
        Weight += Amplitude;
        Amplitude *= Gain;
        Frequency *= Lacunarity;
    }
    return Accumulator / Weight;
}

export function EvaluateRidgedFractal(SampleX, SampleZ, Seed, OctaveCount = 5)
{
    let Amplitude = 1;
    let Frequency = 1;
    let Accumulator = 0;
    let Weight = 0;
    for (let OctaveIndex = 0; OctaveIndex < OctaveCount; ++OctaveIndex)
    {
        const Folded = 1 - Math.abs(EvaluateValueNoise(SampleX * Frequency, SampleZ * Frequency, Seed + OctaveIndex * 733) * 2 - 1);
        Accumulator += Amplitude * Folded * Folded;
        Weight += Amplitude;
        Amplitude *= 0.5;
        Frequency *= 2.07;
    }
    return Accumulator / Weight;
}

// 📝 Domain warp displaces the sampling coordinate before the coastline distance query; this produces
//    ragged peninsulas and embayments without storing a hand-drawn outline at vertex resolution.
export function EvaluateDomainWarp(SampleX, SampleZ, Seed, Wavelength, Amplitude)
{
    const CoordinateX = SampleX / Wavelength;
    const CoordinateZ = SampleZ / Wavelength;
    const OffsetX = EvaluateFractalNoise(CoordinateX + 11.7, CoordinateZ - 4.3, Seed, 4) * Amplitude;
    const OffsetZ = EvaluateFractalNoise(CoordinateX - 27.1, CoordinateZ + 19.4, Seed + 517, 4) * Amplitude;
    return [SampleX + OffsetX, SampleZ + OffsetZ];
}
