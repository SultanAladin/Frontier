/*==========================================================================================================================================
                                                      DISTANCEREINITIALIZATION.JS
==========================================================================================================================================*/
// 🧩 Subcell interface seeding and anisotropic 3D Eikonal fast sweeping across sparse brick boundaries.

import { ValidateVolumeSpecification, ValidateVolumeCoverage } from './SparseVolume.js';

const InteriorBit = 1;
const FixedBit    = 2;
const ZeroBit     = 4;
const BrickWidth  = 8;
const BrickCells  = 512;

export const MaximumDistanceWorkspaceBytes = 384 * 1024 * 1024;

//------------------------------------------------------------------------------------------------------------------------
//                                                        EIKONAL STENCIL
//------------------------------------------------------------------------------------------------------------------------

// 📝 Solves Σ max((d - aᵢ) / hᵢ, 0)² = 1. Shifting by the smallest adjacent distance avoids cancellation.
export function ConstructEikonalStencil(Pitch)
{
    if (!Array.isArray(Pitch) || Pitch.length !== 3 || Pitch.some(Spacing =>
        !Number.isFinite(Spacing) || Spacing <= 0 || (!Number.isFinite(1 / (Spacing * Spacing)) || 1 / (Spacing * Spacing) <= 0))) throw Error('Invalid Eikonal voxel pitch');
    const [SpacingX, SpacingY, SpacingZ] = Pitch;
    const WeightX = 1 / (SpacingX * SpacingX), WeightY = 1 / (SpacingY * SpacingY), WeightZ = 1 / (SpacingZ * SpacingZ);
    return (AdjacentX, AdjacentY, AdjacentZ) =>
    {
        let First = AdjacentX, Second = AdjacentY, Third = AdjacentZ;
        let FirstWeight = WeightX, SecondWeight = WeightY, ThirdWeight = WeightZ;
        let FirstSpacing = SpacingX;
        if (First > Second)
        {
            [First, Second] = [Second, First];
            [FirstWeight, SecondWeight] = [SecondWeight, FirstWeight];
            FirstSpacing = SpacingY;
        }
        if (First > Third)
        {
            [First, Third] = [Third, First];
            [FirstWeight, ThirdWeight] = [ThirdWeight, FirstWeight];
            FirstSpacing = SpacingZ;
        }
        if (Second > Third)
        {
            [Second, Third] = [Third, Second];
            [SecondWeight, ThirdWeight] = [ThirdWeight, SecondWeight];
        }
        if (!Number.isFinite(First)) return Infinity;
        let Distance = FirstSpacing;
        const ΔSecond = Second - First;
        if (Distance > ΔSecond)
        {
            let α = FirstWeight + SecondWeight;
            let β = SecondWeight * ΔSecond;
            let γ = SecondWeight * ΔSecond * ΔSecond - 1;
            Distance = (β + Math.sqrt(Math.max(0, β * β - α * γ))) / α;
            const ΔThird = Third - First;
            if (Distance > ΔThird)
            {
                α += ThirdWeight;
                β += ThirdWeight * ΔThird;
                γ += ThirdWeight * ΔThird * ΔThird;
                Distance = (β + Math.sqrt(Math.max(0, β * β - α * γ))) / α;
            }
        }
        return First + Distance;
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SPARSE ADJACENCY
//------------------------------------------------------------------------------------------------------------------------

function ConstructDistanceAtlas(Volume, MaximumBytes)
{
    const Specification = ValidateVolumeSpecification(Volume.Specification);
    if (Specification.Format !== 'FrontierSparseTSDF/1') throw Error('Redistancing requires a conservative source volume, not an already labelled metric archive.');
    const { Dimensions, Pitch, Band, Origin } = Specification;
    if (Band < 2 * Math.max(...Pitch) * (1 - 1e-7)) throw Error('Subcell reconstruction requires a source band of at least two maximum voxel pitches.');
    const Tiers = ValidateVolumeCoverage(Volume);
    const Span = 2 ** Math.ceil(Math.log2(Math.max(...Dimensions)));
    const Token = (X, Y, Z) => (Z * Span + Y) * Span + X;
    const Bricks = [], Index = new Map();
    let SampledCells = 0, EstimatedBytes = Volume.Leaves.length * 112;
    for (const Leaf of Volume.Leaves)
    {
        if (![1, 2].includes(Leaf.Category) || !(Leaf.Samples instanceof Float32Array) ||
            Leaf.Samples.length !== (Leaf.Category === 1 ? 1 : BrickCells) || (Leaf.Category === 2 && Leaf.Width !== BrickWidth)) throw Error('Invalid source brick layout');
        if (Leaf.Category === 1)
        {
            if (!Number.isFinite(Leaf.Samples[0]) || Math.abs(Math.abs(Leaf.Samples[0]) - Band) > Band * 1e-6) throw Error('Uniform source leaves must be saturated outside the truncation band.');
            continue;
        }
        SampledCells += BrickCells;
        // 📝 Includes source Float32, unsigned Float32, byte predicates and conservative per-brick index/ordering allowance.
        EstimatedBytes += BrickCells * 9 + 640;
    }
    if (!Number.isFinite(MaximumBytes) || MaximumBytes <= 0 || EstimatedBytes > MaximumBytes) throw Error('Metric redistancing exceeds the 384 MiB working-memory budget. Reduce resolution or select a smaller region.');
    for (const Leaf of Volume.Leaves)
    {
        if (Leaf.Category !== 2) continue;
        const Distances = new Float32Array(BrickCells);
        Distances.fill(Band);
        const Brick = { Leaf, Source: Leaf.Samples, Distances, Predicates: new Uint8Array(BrickCells), Adjacent: null };
        Bricks.push(Brick);
        Index.set(Token(Leaf.X, Leaf.Y, Leaf.Z), Brick);
    }
    for (const Brick of Bricks)
    {
        const { X, Y, Z } = Brick.Leaf;
        Brick.Adjacent = [
            X > 0 ? Index.get(Token(X - 8, Y, Z)) : null,
            X + 8 < Dimensions[0] ? Index.get(Token(X + 8, Y, Z)) : null,
            Y > 0 ? Index.get(Token(X, Y - 8, Z)) : null,
            Y + 8 < Dimensions[1] ? Index.get(Token(X, Y + 8, Z)) : null,
            Z > 0 ? Index.get(Token(X, Y, Z - 8)) : null,
            Z + 8 < Dimensions[2] ? Index.get(Token(X, Y, Z + 8)) : null
        ];
    }
    const RetrieveScalar = (X, Y, Z, Exterior) =>
    {
        if (X < 0 || Y < 0 || Z < 0 || X >= Dimensions[0] || Y >= Dimensions[1] || Z >= Dimensions[2])
        {
            if (!Exterior) return null;
            const Distance = Exterior(Origin[0] + (X + .5) * Pitch[0], Origin[1] + (Y + .5) * Pitch[1], Origin[2] + (Z + .5) * Pitch[2]);
            if (!Number.isFinite(Distance)) throw Error('Non-finite exterior field sample.');
            return Distance;
        }
        const BX = X - X % 8, BY = Y - Y % 8, BZ = Z - Z % 8;
        const Brick = Index.get(Token(BX, BY, BZ));
        if (Brick) return Brick.Source[(Z - BZ) * 64 + (Y - BY) * 8 + X - BX];
        for (const [Width, Tier] of Tiers)
        {
            if (Width === 8 && !Tier.size) continue;
            const Leaf = Tier.get(Token(X - X % Width, Y - Y % Width, Z - Z % Width));
            if (Leaf) return Leaf.Samples[0];
        }
        throw Error('Missing source-volume adjacency.');
    };
    return { Bricks, RetrieveScalar, EstimatedBytes, SampledCells };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SUBCELL INTERFACE
//------------------------------------------------------------------------------------------------------------------------

function RefinedInterfaceDistance(
    Position,
    Side,
    Spacing,
    Surface,
    NormalStep,
    SourceSign)
{
    const Axis = Side >> 1, Direction = Side & 1 ? 1 : -1;
    const Sample = Fraction =>
    {
        const Point = [...Position];
        Point[Axis] += Direction * Spacing * Fraction;
        const Scalar = Surface(...Point);
        if (!Number.isFinite(Scalar)) throw Error('Non-finite analytic interface sample.');
        return Scalar;
    };
    const First = Sample(0), Last = Sample(1);
    if (First === 0) return 0;
    if ((First < 0) !== SourceSign || (Last !== 0 && (Last < 0) === SourceSign)) throw Error('Analytic interface disagrees with sampled source signs.');
    let Lower = 0, Upper = 1;
    if (Last !== 0)
    {
        for (let Iteration = 0; Iteration < 20; Iteration++)
        {
            const Middle = (Lower + Upper) * .5;
            if ((Sample(Middle) < 0) === SourceSign) Lower = Middle;
            else Upper = Middle;
        }
    }
    else Lower = 1;
    const Parameter = (Lower + Upper) * .5;
    const Point = [...Position];
    Point[Axis] += Direction * Spacing * Parameter;
    const Gradient = [0, 1, 2].map(Axis =>
    {
        const Minus = [...Point], Plus = [...Point];
        Minus[Axis] -= NormalStep;
        Plus[Axis] += NormalStep;
        const Difference = Surface(...Plus) - Surface(...Minus);
        if (!Number.isFinite(Difference)) throw Error('Non-finite analytic interface normal.');
        return Difference;
    });
    const Magnitude = Math.hypot(...Gradient);
    const NormalProjection = Magnitude > 1e-30 ? Math.abs(Gradient[Axis]) / Magnitude : 1;
    return Parameter * Spacing * NormalProjection;
}

function SeedDistanceBrick(
    Brick,
    Atlas,
    Specification,
    Exterior,
    Surface)
{
    const { Pitch, Band } = Specification;
    const Source = Brick.Source, Distances = Brick.Distances, Predicates = Brick.Predicates;
    const [Left, Right, Below, Above, Back, Front] = Brick.Adjacent;
    let Seeds = 0;
    for (let Z = 0; Z < 8; Z++)
    for (let Y = 0; Y < 8; Y++)
    for (let X = 0; X < 8; X++)
    {
        const Offset = (Z * 8 + Y) * 8 + X;
        const Scalar = Source[Offset];
        if (!Number.isFinite(Scalar) || Math.abs(Scalar) > Band * (1 + 1e-6)) throw Error('Invalid conservative source sample.');
        const Interior = Scalar < 0 ? InteriorBit : 0;
        Predicates[Offset] = Interior;
        if (Scalar === 0)
        {
            Predicates[Offset] |= FixedBit | ZeroBit;
            Distances[Offset] = 0;
            Seeds++;
            continue;
        }
        if (Math.abs(Scalar) >= Band * (1 - 1e-7))
        {
            Predicates[Offset] |= FixedBit;
            continue;
        }
        const WX = Brick.Leaf.X + X, WY = Brick.Leaf.Y + Y, WZ = Brick.Leaf.Z + Z;
        const MinusX = X ? Source[Offset - 1] : Left ? Left.Source[Offset + 7] : Atlas.RetrieveScalar(WX - 1, WY, WZ, Exterior);
        const PlusX = X < 7 ? Source[Offset + 1] : Right ? Right.Source[Offset - 7] : Atlas.RetrieveScalar(WX + 1, WY, WZ, Exterior);
        const MinusY = Y ? Source[Offset - 8] : Below ? Below.Source[Offset + 56] : Atlas.RetrieveScalar(WX, WY - 1, WZ, Exterior);
        const PlusY = Y < 7 ? Source[Offset + 8] : Above ? Above.Source[Offset - 56] : Atlas.RetrieveScalar(WX, WY + 1, WZ, Exterior);
        const MinusZ = Z ? Source[Offset - 64] : Back ? Back.Source[Offset + 448] : Atlas.RetrieveScalar(WX, WY, WZ - 1, Exterior);
        const PlusZ = Z < 7 ? Source[Offset + 64] : Front ? Front.Source[Offset - 448] : Atlas.RetrieveScalar(WX, WY, WZ + 1, Exterior);
        let Nearest = Infinity, Refined = Infinity;
        const Adjacent = [MinusX, PlusX, MinusY, PlusY, MinusZ, PlusZ];
        for (let Side = 0; Side < 6; Side++)
        {
            const Other = Adjacent[Side];
            if (Other === null || (Other !== 0 && (Other < 0) === (Scalar < 0))) continue;
            Nearest = Math.min(Nearest, Pitch[Side >> 1] * Math.abs(Scalar) / (Math.abs(Scalar) + Math.abs(Other)));
            if (Surface)
            {
                const Position = [WX, WY, WZ].map((Coordinate, Axis) => Specification.Origin[Axis] + (Coordinate + .5) * Pitch[Axis]);
                Refined = Math.min(Refined, RefinedInterfaceDistance(Position, Side, Pitch[Side >> 1], Surface, Math.min(...Pitch) * .01, Scalar < 0));
            }
        }
        if (!Number.isFinite(Nearest)) continue;
        const Derivative = (Minus, Plus, Spacing) => Minus === null ? (Plus - Scalar) / Spacing : Plus === null ? (Scalar - Minus) / Spacing : (Plus - Minus) / (2 * Spacing);
        const Gradient = Math.hypot(Derivative(MinusX, PlusX, Pitch[0]), Derivative(MinusY, PlusY, Pitch[1]), Derivative(MinusZ, PlusZ, Pitch[2]));
        // 📝 Affine planar source normals are exact; analytic mode instead projects a refined crossing onto its normal.
        const Linear = Gradient > 1e-30 ? Math.min(Nearest, Math.abs(Scalar) / Gradient) : Nearest;
        const Distance = Math.max(Math.abs(Scalar), Surface ? Refined : Linear);
        Distances[Offset] = Math.min(Band, Distance);
        Predicates[Offset] |= FixedBit;
        Seeds++;
    }
    return Seeds;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        DIRECTIONAL SWEEP
//------------------------------------------------------------------------------------------------------------------------

function SweepDistanceBrick(Brick, Direction, Band, Eikonal)
{
    const Distances = Brick.Distances, Predicates = Brick.Predicates;
    const [Left, Right, Below, Above, Back, Front] = Brick.Adjacent;
    const LeftDistances = Left?.Distances, RightDistances = Right?.Distances;
    const BelowDistances = Below?.Distances, AboveDistances = Above?.Distances;
    const BackDistances = Back?.Distances, FrontDistances = Front?.Distances;
    const StartX = Direction & 1 ? 7 : 0, StepX = Direction & 1 ? -1 : 1;
    const StartY = Direction & 2 ? 7 : 0, StepY = Direction & 2 ? -1 : 1;
    const StartZ = Direction & 4 ? 7 : 0, StepZ = Direction & 4 ? -1 : 1;
    let MaximumChange = 0;
    for (let Z = StartZ; Z >= 0 && Z < 8; Z += StepZ)
    for (let Y = StartY; Y >= 0 && Y < 8; Y += StepY)
    for (let X = StartX; X >= 0 && X < 8; X += StepX)
    {
        const Offset = (Z * 8 + Y) * 8 + X;
        if (Predicates[Offset] & FixedBit) continue;
        const AdjacentX = Math.min(X ? Distances[Offset - 1] : LeftDistances ? LeftDistances[Offset + 7] : Band,
            X < 7 ? Distances[Offset + 1] : RightDistances ? RightDistances[Offset - 7] : Band);
        const AdjacentY = Math.min(Y ? Distances[Offset - 8] : BelowDistances ? BelowDistances[Offset + 56] : Band,
            Y < 7 ? Distances[Offset + 8] : AboveDistances ? AboveDistances[Offset - 56] : Band);
        const AdjacentZ = Math.min(Z ? Distances[Offset - 64] : BackDistances ? BackDistances[Offset + 448] : Band,
            Z < 7 ? Distances[Offset + 64] : FrontDistances ? FrontDistances[Offset - 448] : Band);
        const Original = Distances[Offset];
        if (Math.min(AdjacentX, AdjacentY, AdjacentZ) >= Original) continue;
        const Distance = Math.fround(Math.min(Band, Eikonal(AdjacentX, AdjacentY, AdjacentZ)));
        if (Distance >= Original) continue;
        Distances[Offset] = Distance;
        MaximumChange = Math.max(MaximumChange, Original - Distance);
    }
    return MaximumChange;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SIGNED FINALIZATION
//------------------------------------------------------------------------------------------------------------------------

function FinalizeDistanceBricks(Atlas, Band)
{
    let SignChanges = 0, CollapsedBricks = 0;
    for (const Brick of Atlas.Bricks)
    {
        const Distances = Brick.Distances, Predicates = Brick.Predicates;
        let UniformSign = 0;
        for (let Offset = 0; Offset < BrickCells; Offset++)
        {
            const Expected = Predicates[Offset] & ZeroBit ? 0 : Predicates[Offset] & InteriorBit ? -1 : 1;
            const Distance = Expected * Math.min(Band, Distances[Offset]);
            if (Math.sign(Distance) !== Expected) SignChanges++;
            Distances[Offset] = Distance;
            if (!Offset) UniformSign = Expected;
            if (Expected !== UniformSign || Math.abs(Distances[Offset]) < Band * (1 - 1e-7)) UniformSign = 0;
        }
        if (UniformSign)
        {
            Brick.Leaf.Category = 1;
            Brick.Leaf.Samples = new Float32Array([UniformSign * Band]);
            CollapsedBricks++;
        }
        else Brick.Leaf.Samples = Distances;
    }
    if (SignChanges) throw Error('Metric reinitialization changed a source cell sign. No export was produced.');
    return { SignChanges, CollapsedBricks };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        REINITIALIZATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Consumes the source volume in place. Exterior evaluates the original unclamped bound one cell beyond the box.
// Surface optionally evaluates a same-zero-set analytic residual for refined crossings and normals, not archive samples.
// Without Surface, crossings use linear source reconstruction; without Exterior, cropped boundaries invent no interface.
// The convergence threshold measures Float32 sweep updates, not distance error against an analytic surface.
export function* ReinitializeSparseVolume(Volume, Options = {})
{
    const { Pitch, Band } = Volume.Specification;
    const MaximumCycles = Options.MaximumCycles ?? 6;
    const Tolerance = Options.Tolerance ?? Math.min(...Pitch) * 1e-4;
    if (!Number.isInteger(MaximumCycles) || MaximumCycles < 1 || MaximumCycles > 32 || !Number.isFinite(Tolerance) || Tolerance < 0) throw Error('Invalid metric convergence settings.');
    if (Options.Exterior !== undefined && typeof Options.Exterior !== 'function') throw Error('Invalid exterior field evaluator.');
    if (Options.Surface !== undefined && typeof Options.Surface !== 'function') throw Error('Invalid analytic interface evaluator.');
    const Eikonal = ConstructEikonalStencil(Pitch);
    const Atlas = ConstructDistanceAtlas(Volume, Options.MaximumBytes ?? MaximumDistanceWorkspaceBytes);
    let SeedCells = 0, Completed = 0;
    for (const Brick of Atlas.Bricks)
    {
        SeedCells += SeedDistanceBrick(Brick, Atlas, Volume.Specification, Options.Exterior, Options.Surface);
        Completed++;
        if (Completed % 128 === 0) yield { Phase: 'seeding', Fraction: Completed / Atlas.Bricks.length, SeedCells };
    }
    // 📝 Release raw scalar buffers only after every crossing/normal has been seeded across brick boundaries.
    for (const Brick of Atlas.Bricks) { Brick.Source = null; Brick.Leaf.Samples = null; }
    yield { Phase: 'seeding', Fraction: 1, SeedCells };
    const Orders = Array.from({ length: 8 }, (_, Direction) => [...Atlas.Bricks].sort((A, B) =>
        (Direction & 4 ? -1 : 1) * (A.Leaf.Z - B.Leaf.Z) ||
        (Direction & 2 ? -1 : 1) * (A.Leaf.Y - B.Leaf.Y) ||
        (Direction & 1 ? -1 : 1) * (A.Leaf.X - B.Leaf.X)));
    let Cycles = 0, MaximumChange = 0, Converged = !SeedCells;
    while (!Converged && Cycles < MaximumCycles)
    {
        MaximumChange = 0;
        for (let Direction = 0; Direction < 8; Direction++)
        {
            Completed = 0;
            for (const Brick of Orders[Direction])
            {
                MaximumChange = Math.max(MaximumChange, SweepDistanceBrick(Brick, Direction, Band, Eikonal));
                Completed++;
                if (Completed % 256 === 0) yield { Phase: 'sweeping', Cycle: Cycles + 1, Direction: Direction + 1, MaximumCycles, Fraction: (Direction + Completed / Atlas.Bricks.length) / 8, MaximumChange };
            }
        }
        Cycles++;
        Converged = MaximumChange <= Tolerance;
        yield { Phase: 'sweeping', Cycle: Cycles, Direction: 8, MaximumCycles, Fraction: 1, MaximumChange, Converged };
    }
    if (!Converged) throw Error(`Metric redistancing did not converge in ${MaximumCycles} cycles (change ${MaximumChange}, tolerance ${Tolerance}). No export was produced.`);
    const { SignChanges, CollapsedBricks } = FinalizeDistanceBricks(Atlas, Band);
    Volume.Specification = {
        ...Volume.Specification,
        Format: 'FrontierSparseTSDF/2',
        DistanceMeaning: 'Approximate metric signed distance in world units; first-order anisotropic Eikonal fast sweeping; clamped to [-Band,+Band]. Cell-centre signs preserved; subcell interface is reconstructed, not exact analytic geometry.',
        Redistance: {
            Method: 'AnisotropicFastSweeping/1', Converged, Cycles, MaximumCycles, Tolerance, MaximumChange,
            SeedCells, SignChanges, CollapsedBricks, SampledCells: Atlas.SampledCells,
            EstimatedWorkspaceBytes: Atlas.EstimatedBytes,
            Interface: Options.Surface ? 'AnalyticBracketRefinement/1: twenty bisections per crossing and finite-difference interface normals; source signs preserved.' :
                'LinearSampleCrossings/1: source-sample interpolation and central-difference normal correction; reconstructed subcell geometry.'
        }
    };
    yield { Phase: 'redistanced', Fraction: 1, Redistance: Volume.Specification.Redistance };
}
