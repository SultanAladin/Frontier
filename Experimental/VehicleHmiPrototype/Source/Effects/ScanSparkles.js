//============================================================================================================================================
//                                                             SCANSPARKLES.JS
//============================================================================================================================================
// 🧩 Particle attributes from Drei 10.7.9 Sparkles as configured by Truck.tsx, mapped from Y-up to Z-up.
//    Positions are independently random on each scan activation, as in the reference component's mount.

export const DefaultSparkleCount   = 130;
export const DefaultSparkleScale   = Object.freeze([7.5, 4.2, 3.2]);
// Reference group offset -0.62, floor -0.63, centre 1.35; body centre is 0.53 above our floor.
export const DefaultSparkleCentre  = Object.freeze([0.0, 0.0, 0.83]);
export const DefaultSparkleSize    = 2.2;
export const DefaultSparkleSpeed   = 0.35;
export const DefaultSparkleOpacity = 0.55;
export const DefaultSparkleColour  = Object.freeze([0.274677, 0.686685, 0.887923]); // linear sRGB of #8fd8f2

// 📝 Eight floats: body-relative xyz, point size, three noise components, speed.
export const SparkleRecordFloats = 8;
export const SparkleRecordBytes  = 32;

export function BuildScanSparkleBuffer(Count = DefaultSparkleCount, NextRandom = Math.random)
{
    const Scalars = new Float32Array(Count * SparkleRecordFloats);
    for (let Index = 0; Index < Count; ++Index)
    {
        const Base = Index * SparkleRecordFloats;
        // Drei consumes random X, Y, Z in that order. The reference-to-engine map is (x, y, z) -> (x, -z, y).
        const X = (0.5 - NextRandom()) * DefaultSparkleScale[0];
        const Height = (0.5 - NextRandom()) * DefaultSparkleScale[2];
        const Side = (0.5 - NextRandom()) * DefaultSparkleScale[1];
        Scalars[Base + 0] = DefaultSparkleCentre[0] + X;
        Scalars[Base + 1] = DefaultSparkleCentre[1] - Side;
        Scalars[Base + 2] = DefaultSparkleCentre[2] + Height;
        Scalars[Base + 3] = DefaultSparkleSize;
        Scalars[Base + 4] = 1.0;
        Scalars[Base + 5] = 1.0;
        Scalars[Base + 6] = 1.0;
        Scalars[Base + 7] = DefaultSparkleSpeed;
    }
    return { Scalars, Count };
}
