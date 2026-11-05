//============================================================================================================================================
//                                                             SCANSPARKLES.JS
//============================================================================================================================================
// 🧩 Deterministic 130-particle holographic sparkle field matching <Sparkles count={130} scale={[7.5, 3.2, 4.2]}
//    position={[0, 1.35, 0]} size={2.2} speed={0.35} opacity={0.55} color="#8fd8f2" /> in reference Truck.tsx (mapped to Z-up).

export const DefaultSparkleCount   = 130;
export const DefaultSparkleScale   = Object.freeze([7.5, 4.2, 3.2]);   // [m] length (X), width (Y), height (Z) in Z-up body space
export const DefaultSparkleCentre  = Object.freeze([0.0, 0.0, 0.82]);  // [m] body-local centre (~1.35 m above ground)
export const DefaultSparkleSize    = 2.2;
export const DefaultSparkleSpeed   = 0.35;
export const DefaultSparkleOpacity = 0.55;
export const DefaultSparkleColour  = Object.freeze([0.2747, 0.6867, 0.8879]); // linear sRGB of #8fd8f2

// 📝 Each sparkle record packs 8 floats (32 B):
//    [LocalX, LocalY, LocalZ, SizeScale, PhaseX, PhaseY, PhaseZ, SpeedScale]
export const SparkleRecordFloats = 8;
export const SparkleRecordBytes  = 32;

function Mulberry32(Seed)
{
    let State = Seed >>> 0;
    return () =>
    {
        State = (State + 0x6D2B79F5) >>> 0;
        let T = Math.imul(State ^ (State >>> 15), 1 | State);
        T = (T + Math.imul(T ^ (T >>> 7), 61 | T)) ^ T;
        return ((T ^ (T >>> 14)) >>> 0) / 4294967296.0;
    };
}

export function BuildScanSparkleBuffer(Count = DefaultSparkleCount, Seed = 0x5CA48F2)
{
    const Next = Mulberry32(Seed);
    const Scalars = new Float32Array(Count * SparkleRecordFloats);

    for (let Index = 0; Index < Count; ++Index)
    {
        const Base = Index * SparkleRecordFloats;
        Scalars[Base + 0] = DefaultSparkleCentre[0] + (Next() - 0.5) * DefaultSparkleScale[0];
        Scalars[Base + 1] = DefaultSparkleCentre[1] + (Next() - 0.5) * DefaultSparkleScale[1];
        Scalars[Base + 2] = DefaultSparkleCentre[2] + (Next() - 0.5) * DefaultSparkleScale[2];
        Scalars[Base + 3] = DefaultSparkleSize * (0.65 + 0.70 * Next());
        Scalars[Base + 4] = Next() * Math.PI * 2.0;
        Scalars[Base + 5] = Next() * Math.PI * 2.0;
        Scalars[Base + 6] = Next() * Math.PI * 2.0;
        Scalars[Base + 7] = DefaultSparkleSpeed * (0.65 + 0.70 * Next());
    }

    return { Scalars, Count };
}
