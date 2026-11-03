/*============================================================================================================================================
                                                        SDFCRUSTFIELD.JS
============================================================================================================================================*/
// 🧩 Signed-distance coastline authoring: continent chains, carved gulfs, island arcs, relief ridges and city foundations

import { EvaluateDomainWarp, EvaluateFractalNoise, EvaluateRidgedFractal, EvaluateSmoothMaximum, EvaluateSmoothMinimum, EvaluateSmoothStep, EvaluateValueNoise } from './NumericField.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

export const PrimitiveCategory = Object.freeze({
    Continent: 'Continent',
    Gulf: 'Gulf',
    IslandArc: 'IslandArc',
    Ridge: 'Ridge',
    Crater: 'Crater',
    CityPad: 'CityPad'
});

export function ConstructCrustConfiguration(Override = {})
{
    return {
        Extent: 409600,                 // [m] - square world domain side length
        SeaLevel: 0,                    // [m] - water plane elevation
        FlatElevation: -260,            // [m] - blank vase elevation before any landmass is authored
        FlatRelief: 0,                  // [m] - residual relief on the blank vase
        OceanFloor: -3400,              // [m] - abyssal elevation
        ShelfWidth: 42000,              // [m] - offshore distance over which the shelf climbs to sea level
        LandElevation: 620,             // [m] - continental plateau elevation
        LandRiseDistance: 35000,        // [m] - inland distance over which the plateau is reached
        SeafloorRelief: 420,            // [m] - abyssal noise amplitude
        SeafloorWavelength: 46000,      // [m]
        TerrainRelief: 130,             // [m] - inland noise amplitude
        TerrainWavelength: 19000,       // [m]
        MountainAmplitude: 2600,        // [m] - ridged noise amplitude
        MountainWavelength: 48000,      // [m]
        CoastBlend: 14000,              // [m] - smooth-union radius between outline capsules
        Seed: 1337,
        ...Override
    };
}

let PrimitiveIdentifierSequence = 1;

export function ConstructPrimitive(Category, Override = {})
{
    const Primitive = {
        Identifier: PrimitiveIdentifierSequence++,
        Category,
        Label: Category,
        Enabled: true,
        CenterX: 0,                     // [m]
        CenterZ: 0,                     // [m]
        Rotation: 0,                    // [rad]
        Outline: [],                    // [[m,m,m], ...] - capsule chain nodes with local radii
        RadiiX: 32000,                  // [m]
        RadiiZ: 22000,                  // [m]
        Width: 12000,                   // [m] - ridge / crater / pad falloff
        Elevation: 900,                 // [m] - ridge amplitude or city pad target elevation
        Blend: 12000,                   // [m] - boolean blend radius
        WarpAmplitude: 9000,            // [m] - coastline domain-warp amplitude
        WarpWavelength: 78000,          // [m]
        Seed: 71,
        ...Override
    };
    return Primitive;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DISTANCE PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

function TransformToLocal(WorldX, WorldZ, Primitive)
{
    const OffsetX = WorldX - Primitive.CenterX;
    const OffsetZ = WorldZ - Primitive.CenterZ;
    const Cosine = Math.cos(-Primitive.Rotation);
    const Sine = Math.sin(-Primitive.Rotation);
    return [OffsetX * Cosine - OffsetZ * Sine, OffsetX * Sine + OffsetZ * Cosine];
}

// 📝 A closed continental outline is a filled region: the even-odd test makes the interior
//    negative so the capsule radius only displaces the coastline, instead of hollowing the mass.
function EvaluateCapsuleChainDistance(LocalX, LocalZ, Outline, Filled = false)
{
    if (Outline.length === 0) return 1e12;
    if (Outline.length === 1)
    {
        const OutlineNode = Outline[0];
        return Math.hypot(LocalX - OutlineNode[0], LocalZ - OutlineNode[1]) - OutlineNode[2];
    }
    let MinimumDistance = 1e12;
    let ClosestRadius = 0;
    for (let NodeIndex = 0; NodeIndex < Outline.length; ++NodeIndex)
    {
        const First = Outline[NodeIndex];
        const Second = Outline[(NodeIndex + 1) % Outline.length];
        const SegmentX = Second[0] - First[0];
        const SegmentZ = Second[1] - First[1];
        const LengthSquared = SegmentX * SegmentX + SegmentZ * SegmentZ;
        const Parameter = Math.max(0, Math.min(1, ((LocalX - First[0]) * SegmentX + (LocalZ - First[1]) * SegmentZ) / (LengthSquared || 1)));
        const ClosestX = First[0] + SegmentX * Parameter;
        const ClosestZ = First[1] + SegmentZ * Parameter;
        const InterpolatedRadius = First[2] + (Second[2] - First[2]) * Parameter;
        const Distance = Math.hypot(LocalX - ClosestX, LocalZ - ClosestZ);
        if (Distance < MinimumDistance) { MinimumDistance = Distance; ClosestRadius = InterpolatedRadius; }
    }
    if (!Filled) return MinimumDistance - ClosestRadius;
    let Interior = false;
    for (let NodeIndex = 0, PreviousIndex = Outline.length - 1; NodeIndex < Outline.length; PreviousIndex = NodeIndex++)
    {
        const First = Outline[NodeIndex];
        const Second = Outline[PreviousIndex];
        if ((First[1] > LocalZ) !== (Second[1] > LocalZ)
            && LocalX < (Second[0] - First[0]) * (LocalZ - First[1]) / (Second[1] - First[1]) + First[0]) Interior = !Interior;
    }
    return Interior ? -(MinimumDistance + ClosestRadius) : MinimumDistance - ClosestRadius;
}

function EvaluateEllipseDistance(LocalX, LocalZ, RadiiX, RadiiZ)
{
    const Normalized = Math.hypot(LocalX / RadiiX, LocalZ / RadiiZ);
    return (Normalized - 1) * Math.min(RadiiX, RadiiZ);
}

function EvaluateWarpedCoordinate(WorldX, WorldZ, Primitive)
{
    if (Primitive.WarpAmplitude <= 0) return [WorldX, WorldZ];
    return EvaluateDomainWarp(WorldX, WorldZ, Primitive.Seed, Primitive.WarpWavelength, Primitive.WarpAmplitude);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CRUST FIELD
//------------------------------------------------------------------------------------------------------------------------

export class CrustSignedDistanceField
{
    constructor(Configuration = ConstructCrustConfiguration())
    {
        this.Configuration = Configuration;
        this.Primitives = [];
    }

    Configure(Override)
    {
        Object.assign(this.Configuration, Override);
    }

    AppendPrimitive(Primitive)
    {
        this.Primitives.push(Primitive);
        return Primitive;
    }

    RemovePrimitive(Identifier)
    {
        const Index = this.Primitives.findIndex(Primitive => Primitive.Identifier === Identifier);
        if (Index >= 0) this.Primitives.splice(Index, 1);
    }

    ResolvePrimitive(Identifier)
    {
        return this.Primitives.find(Primitive => Primitive.Identifier === Identifier) || null;
    }

    EnumerateActiveCoastPrimitives()
    {
        return this.Primitives.filter(Primitive => Primitive.Enabled &&
            (Primitive.Category === PrimitiveCategory.Continent ||
             Primitive.Category === PrimitiveCategory.Gulf ||
             Primitive.Category === PrimitiveCategory.IslandArc));
    }

    // 📝 Negative distance is inland; zero is the coastline; positive distance is open water.
    EvaluateCoastDistance(WorldX, WorldZ)
    {
        let CoastDistance = 1e12;
        let HasUnion = false;
        for (const Primitive of this.Primitives)
        {
            if (!Primitive.Enabled) continue;
            if (Primitive.Category === PrimitiveCategory.Continent)
            {
                const [WarpedX, WarpedZ] = EvaluateWarpedCoordinate(WorldX, WorldZ, Primitive);
                const [LocalX, LocalZ] = TransformToLocal(WarpedX, WarpedZ, Primitive);
                const Candidate = EvaluateCapsuleChainDistance(LocalX, LocalZ, Primitive.Outline, true);
                CoastDistance = HasUnion ? EvaluateSmoothMinimum(CoastDistance, Candidate, Primitive.Blend) : Candidate;
                HasUnion = true;
            }
            else if (Primitive.Category === PrimitiveCategory.IslandArc)
            {
                const [WarpedX, WarpedZ] = EvaluateWarpedCoordinate(WorldX, WorldZ, Primitive);
                const [LocalX, LocalZ] = TransformToLocal(WarpedX, WarpedZ, Primitive);
                const Candidate = EvaluateEllipseDistance(LocalX, LocalZ, Primitive.RadiiX, Primitive.RadiiZ);
                CoastDistance = HasUnion ? EvaluateSmoothMinimum(CoastDistance, Candidate, Primitive.Blend) : Candidate;
                HasUnion = true;
            }
        }
        if (!HasUnion) return 1e12;
        for (const Primitive of this.Primitives)
        {
            if (!Primitive.Enabled || Primitive.Category !== PrimitiveCategory.Gulf) continue;
            const [WarpedX, WarpedZ] = EvaluateWarpedCoordinate(WorldX, WorldZ, Primitive);
            const [LocalX, LocalZ] = TransformToLocal(WarpedX, WarpedZ, Primitive);
            const GulfInterior = EvaluateEllipseDistance(LocalX, LocalZ, Primitive.RadiiX, Primitive.RadiiZ);
            CoastDistance = EvaluateSmoothMaximum(CoastDistance, -GulfInterior, Primitive.Blend);
        }
        return CoastDistance;
    }

    EvaluateElevation(WorldX, WorldZ)
    {
        const Configuration = this.Configuration;
        const CoastDistance = this.EvaluateCoastDistance(WorldX, WorldZ);
        const InlandDistance = -CoastDistance;
        const HasCoast = Number.isFinite(CoastDistance) && CoastDistance < 1e11;
        const ShoreRise = EvaluateSmoothStep(-Configuration.ShelfWidth, 0, InlandDistance);
        const LandRise = EvaluateSmoothStep(0, Configuration.LandRiseDistance, InlandDistance);
        let Elevation;
        if (HasCoast)
        {
            Elevation = Configuration.OceanFloor
                + (Configuration.SeaLevel - Configuration.OceanFloor) * ShoreRise
                + (Configuration.LandElevation - Configuration.SeaLevel) * LandRise;
            const SeabedRelief = EvaluateFractalNoise(WorldX / Configuration.SeafloorWavelength, WorldZ / Configuration.SeafloorWavelength, Configuration.Seed, 4)
                * Configuration.SeafloorRelief * (1 - ShoreRise);
            const InlandRelief = EvaluateFractalNoise(WorldX / Configuration.TerrainWavelength, WorldZ / Configuration.TerrainWavelength, Configuration.Seed + 31, 5)
                * Configuration.TerrainRelief * LandRise;
            const MountainMask = EvaluateSmoothStep(Configuration.LandRiseDistance, Configuration.LandRiseDistance * 3, InlandDistance);
            const MountainProfile = EvaluateRidgedFractal(WorldX / Configuration.MountainWavelength, WorldZ / Configuration.MountainWavelength, Configuration.Seed + 71, 5);
            Elevation += SeabedRelief + InlandRelief + MountainProfile * MountainProfile * Configuration.MountainAmplitude * MountainMask * LandRise;
        }
        else
        {
            Elevation = Configuration.FlatElevation
                + EvaluateValueNoise(WorldX / Configuration.SeafloorWavelength, WorldZ / Configuration.SeafloorWavelength, Configuration.Seed) * Configuration.FlatRelief;
        }

        for (const Primitive of this.Primitives)
        {
            if (!Primitive.Enabled) continue;
            const [LocalX, LocalZ] = TransformToLocal(WorldX, WorldZ, Primitive);
            if (Primitive.Category === PrimitiveCategory.Ridge)
            {
                const RidgeDistance = EvaluateCapsuleChainDistance(LocalX, LocalZ, Primitive.Outline);
                const Profile = Math.exp(-(RidgeDistance * RidgeDistance) / (Primitive.Width * Primitive.Width));
                Elevation += Primitive.Elevation * Profile * (HasCoast ? Math.max(0.15, LandRise) : 1);
            }
            else if (Primitive.Category === PrimitiveCategory.Crater)
            {
                const Radial = Math.hypot(LocalX, LocalZ);
                const Ring = (Radial - Primitive.RadiiX) / Primitive.Width;
                Elevation -= Primitive.Elevation * Math.exp(-Ring * Ring);
            }
            else if (Primitive.Category === PrimitiveCategory.CityPad)
            {
                const Radial = Math.hypot(LocalX / Primitive.RadiiX, LocalZ / Primitive.RadiiZ);
                const PadWeight = 1 - EvaluateSmoothStep(0.55, 1, Radial);
                Elevation += (Primitive.Elevation - Elevation) * PadWeight;
            }
        }
        return Elevation;
    }

    // 📝 Heightfield-derived volumetric signed distance; the isosurface is the crust boundary.
    EvaluateSignedVolume(WorldX, WorldY, WorldZ)
    {
        return WorldY - this.EvaluateElevation(WorldX, WorldZ);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PRESETS
//------------------------------------------------------------------------------------------------------------------------

function ConstructNormalizedOutline(Nodes, Extent)
{
    return Nodes.map(OutlineNode => [OutlineNode[0] * Extent, OutlineNode[1] * Extent, OutlineNode[2] * Extent]);
}

export function ConstructPreset(Identifier, Extent)
{
    const Result = [];
    if (Identifier === 'BlankVase') return Result;

    if (Identifier === 'AfricaBlockout')
    {
        const Outline = ConstructNormalizedOutline([
            [-0.30, -0.34, 0.115], [-0.08, -0.42, 0.145], [0.14, -0.39, 0.135],
            [0.275, -0.27, 0.105], [0.245, -0.12, 0.095], [0.355, -0.035, 0.055],
            [0.205, 0.055, 0.085], [0.145, 0.205, 0.090], [0.045, 0.395, 0.065],
            [-0.055, 0.275, 0.095], [-0.215, 0.145, 0.115], [-0.335, 0.005, 0.115],
            [-0.285, -0.175, 0.130]
        ], Extent * 0.86);
        Result.push(ConstructPrimitive(PrimitiveCategory.Continent, { Label: 'Continental Mass', Outline, WarpAmplitude: Extent * 0.026, WarpWavelength: Extent * 0.19, Blend: Extent * 0.035, Seed: 19 }));
        Result.push(ConstructPrimitive(PrimitiveCategory.Gulf, { Label: 'Western Gulf', CenterX: -Extent * 0.355, CenterZ: Extent * 0.045, RadiiX: Extent * 0.105, RadiiZ: Extent * 0.062, Rotation: -0.18, Blend: Extent * 0.02, WarpAmplitude: Extent * 0.012 }));
        Result.push(ConstructPrimitive(PrimitiveCategory.Gulf, { Label: 'Northern Inland Sea', CenterX: Extent * 0.055, CenterZ: -Extent * 0.245, RadiiX: Extent * 0.075, RadiiZ: Extent * 0.038, Rotation: 0.22, Blend: Extent * 0.014, WarpAmplitude: Extent * 0.008 }));
        Result.push(ConstructPrimitive(PrimitiveCategory.IslandArc, { Label: 'Eastern Island', CenterX: Extent * 0.315, CenterZ: Extent * 0.245, RadiiX: Extent * 0.022, RadiiZ: Extent * 0.072, Rotation: 0.28, Blend: Extent * 0.008, WarpAmplitude: Extent * 0.006 }));
        Result.push(ConstructPrimitive(PrimitiveCategory.Ridge, { Label: 'Southern Escarpment', CenterX: 0, CenterZ: Extent * 0.19, Outline: ConstructNormalizedOutline([[-0.22, 0.16, 0], [0.0, 0.27, 0], [0.19, 0.14, 0]], Extent), Width: Extent * 0.045, Elevation: 1450, Blend: Extent * 0.02 }));
        Result.push(ConstructPrimitive(PrimitiveCategory.CityPad, { Label: 'Coastal City Vase', CenterX: -Extent * 0.055, CenterZ: Extent * 0.105, RadiiX: Extent * 0.032, RadiiZ: Extent * 0.028, Elevation: 72 }));
        return Result;
    }

    if (Identifier === 'Archipelago')
    {
        for (let IslandIndex = 0; IslandIndex < 11; ++IslandIndex)
        {
            const Angle = IslandIndex * 2.399963;
            const Radial = Extent * (0.07 + 0.28 * Math.sqrt(IslandIndex / 11));
            Result.push(ConstructPrimitive(PrimitiveCategory.IslandArc, {
                Label: `Island ${IslandIndex + 1}`,
                CenterX: Math.cos(Angle) * Radial,
                CenterZ: Math.sin(Angle) * Radial * 0.8,
                RadiiX: Extent * (0.018 + 0.011 * ((IslandIndex * 7) % 5) / 5),
                RadiiZ: Extent * (0.014 + 0.020 * ((IslandIndex * 3) % 7) / 7),
                Rotation: Angle,
                Blend: Extent * 0.012,
                WarpAmplitude: Extent * 0.012,
                Seed: 200 + IslandIndex * 13
            }));
        }
        return Result;
    }

    if (Identifier === 'PangaeaBlockout')
    {
        Result.push(ConstructPrimitive(PrimitiveCategory.Continent, {
            Label: 'Northern Mass',
            CenterX: -Extent * 0.10, CenterZ: -Extent * 0.08,
            Outline: ConstructNormalizedOutline([[-0.30, -0.28, 0.12], [-0.04, -0.36, 0.14], [0.20, -0.25, 0.13], [0.16, -0.02, 0.10], [-0.10, 0.04, 0.11], [-0.32, -0.06, 0.12]], Extent),
            WarpAmplitude: Extent * 0.03, WarpWavelength: Extent * 0.18, Blend: Extent * 0.04
        }));
        Result.push(ConstructPrimitive(PrimitiveCategory.Continent, {
            Label: 'Southern Mass',
            CenterX: Extent * 0.10, CenterZ: Extent * 0.16,
            Outline: ConstructNormalizedOutline([[-0.20, -0.10, 0.11], [0.10, -0.16, 0.12], [0.26, 0.06, 0.10], [0.14, 0.32, 0.09], [-0.10, 0.26, 0.10], [-0.24, 0.08, 0.10]], Extent),
            WarpAmplitude: Extent * 0.028, WarpWavelength: Extent * 0.17, Blend: Extent * 0.04, Seed: 211
        }));
        Result.push(ConstructPrimitive(PrimitiveCategory.Gulf, { Label: 'Interior Seaway', CenterX: Extent * 0.02, CenterZ: Extent * 0.035, RadiiX: Extent * 0.20, RadiiZ: Extent * 0.028, Rotation: 0.25, Blend: Extent * 0.02 }));
        return Result;
    }
    return Result;
}
