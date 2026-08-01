/*====================================================================================================================================
                                                        SIMPLEXFIELD.JS
====================================================================================================================================*/
// 🧩 Seedable 2D simplex gradient noise — the scalar field the terrain viewport displaces against

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const SkewFactor   = 0.5 * (Math.sqrt(3.0) - 1.0);          // [-] - Forward skew from square to simplex lattice
const UnskewFactor = (3.0 - Math.sqrt(3.0)) / 6.0;          // [-] - Inverse of the skew above
const RadiusSquared = 0.5;                                  // [-] - Squared kernel support radius per corner

// 📝 The twelve edge midpoints of a cube, the canonical 2D-usable gradient set.
const GradientTable = [
    [ 1, 1], [-1, 1], [ 1,-1], [-1,-1],
    [ 1, 0], [-1, 0], [ 1, 0], [-1, 0],
    [ 0, 1], [ 0,-1], [ 0, 1], [ 0,-1]
];

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Construct a permutation-table-backed noise evaluator for the given integer seed.
export function ConstructSimplexField(FieldSeed = 0)
{
    // 📝 Deterministic 32-bit mixing so a seed dial reproduces the same terrain every rebuild.
    let MixingRegister = (FieldSeed | 0) + 0x9e3779b9;
    const AdvanceUniform = () =>
    {
        MixingRegister |= 0;
        MixingRegister = (MixingRegister + 0x6d2b79f5) | 0;
        let Mixed = Math.imul(MixingRegister ^ (MixingRegister >>> 15), 1 | MixingRegister);
        Mixed = (Mixed + Math.imul(Mixed ^ (Mixed >>> 7), 61 | Mixed)) ^ Mixed;
        return ((Mixed ^ (Mixed >>> 14)) >>> 0) / 4294967296;
    };

    const Permutation = new Uint8Array(256);
    for (let SlotIndex = 0; SlotIndex < 256; SlotIndex++) Permutation[SlotIndex] = SlotIndex;
    for (let SlotIndex = 255; SlotIndex > 0; SlotIndex--)
    {
        const SwapIndex = Math.floor(AdvanceUniform() * (SlotIndex + 1));
        const Retained = Permutation[SlotIndex];
        Permutation[SlotIndex] = Permutation[SwapIndex];
        Permutation[SwapIndex] = Retained;
    }

    // Doubled so lattice lookups never need a modulo.
    const PermutationMirror = new Uint8Array(512);
    const GradientIndex     = new Uint8Array(512);
    for (let SlotIndex = 0; SlotIndex < 512; SlotIndex++)
    {
        PermutationMirror[SlotIndex] = Permutation[SlotIndex & 255];
        GradientIndex[SlotIndex]     = PermutationMirror[SlotIndex] % 12;
    }

    return function EvaluateSimplexNoise(SampleX, SampleY)
    {
        // ① Skew the input into simplex lattice space and locate the containing cell.
        const SkewOffset = (SampleX + SampleY) * SkewFactor;
        const CellX = Math.floor(SampleX + SkewOffset);
        const CellY = Math.floor(SampleY + SkewOffset);

        const UnskewOffset = (CellX + CellY) * UnskewFactor;
        const OriginX = SampleX - (CellX - UnskewOffset);
        const OriginY = SampleY - (CellY - UnskewOffset);

        // ② Pick the traversal order of the two triangles composing the cell.
        const UpperTriangleX = OriginX > OriginY ? 1 : 0;
        const UpperTriangleY = OriginX > OriginY ? 0 : 1;

        // ③ Displacements to the remaining two corners.
        const MiddleX = OriginX - UpperTriangleX + UnskewFactor;
        const MiddleY = OriginY - UpperTriangleY + UnskewFactor;
        const FinalX  = OriginX - 1.0 + 2.0 * UnskewFactor;
        const FinalY  = OriginY - 1.0 + 2.0 * UnskewFactor;

        const WrappedX = CellX & 255;
        const WrappedY = CellY & 255;

        // ④ Accumulate the radially-attenuated gradient contribution of each corner.
        let Accumulation = 0.0;

        let Falloff = RadiusSquared - OriginX * OriginX - OriginY * OriginY;
        if (Falloff > 0)
        {
            const Gradient = GradientTable[GradientIndex[WrappedX + PermutationMirror[WrappedY]]];
            Falloff *= Falloff;
            Accumulation += Falloff * Falloff * (Gradient[0] * OriginX + Gradient[1] * OriginY);
        }

        Falloff = RadiusSquared - MiddleX * MiddleX - MiddleY * MiddleY;
        if (Falloff > 0)
        {
            const Gradient = GradientTable[GradientIndex[
                WrappedX + UpperTriangleX + PermutationMirror[WrappedY + UpperTriangleY]]];
            Falloff *= Falloff;
            Accumulation += Falloff * Falloff * (Gradient[0] * MiddleX + Gradient[1] * MiddleY);
        }

        Falloff = RadiusSquared - FinalX * FinalX - FinalY * FinalY;
        if (Falloff > 0)
        {
            const Gradient = GradientTable[GradientIndex[WrappedX + 1 + PermutationMirror[WrappedY + 1]]];
            Falloff *= Falloff;
            Accumulation += Falloff * Falloff * (Gradient[0] * FinalX + Gradient[1] * FinalY);
        }

        // ⑤ Scale into approximately [-1, 1].
        return 70.0 * Accumulation;
    };
}
