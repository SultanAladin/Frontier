/*====================================================================================================================================
                                                        TERRAIN SHAPE.JS
====================================================================================================================================*/
// 🧩 CPU mirror of the analytic shoreline profile in shaders/chunks/TerrainShape.glsl

// 🔴 Keep this file bit-for-bit compatible with the GLSL chunk; vessel grounding and probe placement depend on it
function HashScalar(X, Z)
{
    const Value = Math.sin(X * 127.1 + Z * 311.7) * 43758.5453123;
    return Value - Math.floor(Value);
}

function ValueNoise(X, Z)
{
    const CellX = Math.floor(X);
    const CellZ = Math.floor(Z);
    const OffsetX = X - CellX;
    const OffsetZ = Z - CellZ;
    const EasedX = OffsetX * OffsetX * (3.0 - 2.0 * OffsetX);
    const EasedZ = OffsetZ * OffsetZ * (3.0 - 2.0 * OffsetZ);

    const CornerA = HashScalar(CellX, CellZ);
    const CornerB = HashScalar(CellX + 1, CellZ);
    const CornerC = HashScalar(CellX, CellZ + 1);
    const CornerD = HashScalar(CellX + 1, CellZ + 1);

    const Lower = CornerA + (CornerB - CornerA) * EasedX;
    const Upper = CornerC + (CornerD - CornerC) * EasedX;
    return Lower + (Upper - Lower) * EasedZ;
}

function FractalNoise(X, Z, OctaveCount)
{
    let Sum = 0.0;
    let Amplitude = 0.5;
    let Normalization = 0.0;
    let CursorX = X;
    let CursorZ = Z;

    for (let Octave = 0; Octave < Math.min(OctaveCount, 6); Octave++)
    {
        Sum += Amplitude * ValueNoise(CursorX, CursorZ);
        Normalization += Amplitude;
        CursorX = CursorX * 2.03 + 19.3;
        CursorZ = CursorZ * 2.03 + 7.7;
        Amplitude *= 0.5;
    }

    return Sum / Math.max(Normalization, 0.0001);
}

function SmoothStep(EdgeLow, EdgeHigh, Position)
{
    const Span = EdgeHigh - EdgeLow;
    const Ratio = Span === 0 ? 0 : Math.min(1, Math.max(0, (Position - EdgeLow) / Span));
    return Ratio * Ratio * (3.0 - 2.0 * Ratio);
}

export function TerrainHeight(WorldX, WorldZ)
{
    const Landward = SmoothStep(-70.0, 130.0, WorldZ);
    const Base = -11.0 + (6.2 - -11.0) * Landward;

    const BarBand = SmoothStep(-95.0, -18.0, WorldZ) * (1.0 - Landward);
    const SandBar = 0.85 * Math.sin(WorldZ * 0.082 + WorldX * 0.017 + 1.7) * BarBand;

    const DuneFade = SmoothStep(0.58, 1.0, Landward);
    const Dune = (FractalNoise(WorldX * 0.011 + 4.2, WorldZ * 0.011 + 8.9, 4) - 0.5) * 9.0 * DuneFade;

    const Bed = (FractalNoise(WorldX * 0.026 + 17.0, WorldZ * 0.026 + 3.0, 3) - 0.5) * 1.7 * (1.0 - Landward);

    let Island = 0.0;

    const IslandDistance = Math.hypot((WorldX - -104.0) * 1.0, (WorldZ - -66.0) * 0.78);
    Island += 11.0 * (1.0 - SmoothStep(9.0, 52.0, IslandDistance));

    const RockDistance = Math.hypot((WorldX - 74.0) * 1.0, (WorldZ - -128.0) * 1.35);
    Island += 6.5 * (1.0 - SmoothStep(4.0, 26.0, RockDistance));

    const SpireDistance = Math.hypot((WorldX - 118.0) * 0.62, (WorldZ - 6.0) * 1.0);
    Island += 8.5 * (1.0 - SmoothStep(3.0, 19.0, SpireDistance));

    return Base + SandBar + Dune + Bed + Island;
}

export function WaterDepth(WorldX, WorldZ)
{
    return -TerrainHeight(WorldX, WorldZ);
}
