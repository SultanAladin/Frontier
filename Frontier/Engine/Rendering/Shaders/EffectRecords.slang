/*==========================================================================================================================================
                                                        EFFECTRECORDS.SLANG
                                     ------------------------------------------------------------------
                   The derez / rez effect as both the surface raster and the effect raster read
                        it: emitters, the vehicle mask, the release clock.
==========================================================================================================================================*/

// 🧩 One clock for two rasters: the Slang form of the demo's EffectRecords.wgsl, 1:1.
//
// 📝 One clock decides everything. A point of the car (or of a cone's box) belongs to a cell of a 16³
//    lattice over its bounds; the cell's release time is a wave across the bounds plus a hashed
//    jitter. The surface raster discards a car pixel once its cell is released (glowing white-hot,
//    then cyan, just before), and the effect raster launches a cube from every anchor of that cell at
//    the same instant — so the car comes apart exactly where the cubes leave it, with no state shared
//    between the two but this function. Rez runs it backwards: cubes fly in and each cell of the car
//    appears as its cubes arrive.
//
//    Colour: with Colour.x set, every cube is the car — each anchor carries the material family of the
//    facet it was spread on, so the lime coat comes away as lime cubes, the glass as glass, the tyres
//    as rubber — and glows in that colour (G-buffer Attributes.w = 1 + glow: the recombination adds
//    albedo × glow as emission). Otherwise the Particle Editor's palette families (cyan glow,
//    white-hot core, glitch pink) stand in.
//
//    Momentum: the car keeps moving while it comes apart, so a cell must leave from where the car is
//    when that cell lets go. EffectSequence samples the car's pose and velocity at 60 Hz from the
//    derez on (Colour.z samples from clock Colour.w); a cube launches from the pose at its release
//    time with the car's velocity there (plus the spin about the centre of mass) — the falling blocks
//    carry on with the car.

public struct EffectEmitter
{
    public float4x4 BodyToWorld;   // the emitter's pose when it started (local → world)
    public float4   Velocity;      // xyz [m/s] velocity the cubes inherit, w [s] start on the effect clock
    public float4   Angular;       // xyz [rad/s] spin they inherit, w kind (EffectKind)
    public float4   Extent;        // xyz [m] half extent of the local bounds, w particle count
    public float4   Centre;        // xyz [m] centre of the local bounds, w seed
};

public struct EffectRecord
{
    public float Clock;            // [s]  effect clock now
    public uint  MaskMode;         // [-]  vehicle mask: 0 off, 1 derez, 2 rez, 3 hidden
    public float MaskStart;        // [s]  clock the mask started at
    public uint  Style;            // [-]  0 cubes, 1 glitch
    public float4 BodyMinimum;     // xyz [m] the car's local bounds, w hold offset [s]
    public float4 BodyMaximum;     // xyz [m], w frame seconds (motion vectors)
    public float4 Motion;          // x burst [m/s], y slowest launch [m/s], z fastest launch [m/s], w drag [1/s]
    public float4 Timing;          // x cube life [s], y cube size [m], z wave spread [s], w rez flight [s]
    public float4 Colour;          // x face colours (1) or palette (0), y glow strength [-], z pose samples, w first sample's clock [s]
    // 📝 Eight emitters of 128 bytes each follow the 96-byte header: the whole effect is one uniform
    //    write. EffectSequence puts the mask's emitter in slot 0, because the surface raster reads its
    //    seed from Emitters[0].Centre.w.
    public EffectEmitter Emitters[8];
};

// 📝 The emitter record travels as eight vec4 quads inside the record's tail, so the host can upload the
//    whole effect as one uniform write.
[[vk::binding(1, 0)]] public ConstantBuffer<EffectRecord> Effect;

// 📝 One sample of the car's motion during a derez (EffectSequence::RecordPose), 60 Hz on the effect clock.
public struct EffectPose
{
    public float4 Position;        // xyz [m] centre of mass, w [s] clock
    public float4 Rotation;        // body → world quaternion (x, y, z, w)
    public float4 Velocity;        // xyz [m/s]
    public float4 Angular;         // xyz [rad/s]
};

public static const float EffectPoseSpacing = 1.0 / 60.0;   // [s]

public static const uint EffectKindVehicleDerez = 0u;
public static const uint EffectKindVehicleRez = 1u;
public static const uint EffectKindBoxDerez = 2u;
public static const uint EffectKindBoxRez = 3u;
public static const uint EffectKindDebris = 4u;      // a burst tyre's rubber: released at once, thrown hard, never glowing

public static const float EffectCellsPerAxis = 16.0;
public static const float EffectHotSeconds = 0.07;   // [s] white-hot just before release / after arrival
public static const float EffectGlowSeconds = 0.22;  // [s] cyan glow before that

public static const uint FamilyDerezGlow = 63u;
public static const uint FamilyDerezHot = 64u;
public static const uint FamilyDerezGlitch = 65u;
public static const uint FamilyConeOrange = 60u;
public static const uint FamilyConeBand = 61u;
public static const uint FamilyConeBase = 62u;

public float3 RotateByQuaternion(float4 Rotation, float3 Vector)
{
    const float3 Twice = 2.0 * cross(Rotation.xyz, Vector);
    return Vector + Rotation.w * Twice + cross(Rotation.xyz, Twice);
}

// 📝 A flash at release, then an afterglow a third of a second long.
public float ResolveFaceGlow(float Seconds, float Strength)
{
    return Strength * ((Seconds < EffectHotSeconds) ? 2.5 : 0.0) + Strength * exp(-max(Seconds, 0.0) / 0.35);
}

public float HashEffect(uint Value)
{
    uint State = Value * 747796405u + 2891336453u;
    State = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    State = (State >> 22u) ^ State;
    return float(State) / 4294967296.0;
}

public float HashEffectCell(uint3 Cell, uint Seed)
{
    return HashEffect((Cell.x * 73856093u) ^ (Cell.y * 19349663u) ^ (Cell.z * 83492791u) ^ (Seed * 2654435761u));
}

// 📝 The cell of a point given as Unit, its position within the emitter's bounds in [0, 1]³. Glitch
//    style uses coarser, uneven 7 × 5 × 7 cells and rises from the floor; cubes style sweeps the
//    diagonal of 16³ cells.
public uint3 ResolveEffectCell(float3 Unit, uint Style)
{
    const float3 Divisions = (Style == 1u) ? float3(7.0, 5.0, 7.0) : float3(EffectCellsPerAxis);
    return uint3(clamp(Unit, float3(0.0), float3(0.9999)) * Divisions);
}

public float ResolveEffectWave(float3 Unit, uint Style)
{
    const float3 Divisions = (Style == 1u) ? float3(7.0, 5.0, 7.0) : float3(EffectCellsPerAxis);
    const float3 Centre = (float3(ResolveEffectCell(Unit, Style)) + 0.5) / Divisions;
    return (Style == 1u) ? 0.4 * Centre.z : (Centre.x + Centre.y + Centre.z) / 3.0;
}

// 📝 Seconds after the derez starts at which this cell lets go. Scale shortens it for small emitters
//    (cones).
public float ResolveDerezRelease(float3 Unit, uint Seed, uint Style, float Scale)
{
    const float Jitter = HashEffectCell(ResolveEffectCell(Unit, Style), Seed);
    const float Spread = (Style == 1u) ? 1.0 * Jitter : 0.15 * Jitter;
    return Scale * (Effect.BodyMinimum.w + Effect.Timing.z * (ResolveEffectWave(Unit, Style) + Spread));
}

// 📝 Seconds after the rez starts at which this cell is complete again.
public float ResolveRezArrival(float3 Unit, uint Seed, uint Style, float Scale)
{
    const float Jitter = HashEffectCell(ResolveEffectCell(Unit, Style), Seed ^ 0x9E3779B9u);
    return Scale * (0.3 + Effect.Timing.w * (ResolveEffectWave(Unit, Style) + 0.15 * Jitter));
}
