/*==========================================================================================================================================
                                                          PLUMERECORDS.SLANG
                                     ------------------------------------------------------------------
                   The smoke-and-fire cell lattices: the step record, the source and fire-light
                    records, the toroidal addressing shared by every plume program, and the
                                        blackbody colour of a flame.
==========================================================================================================================================*/

// 🧩 Smoke-and-fire records: the Slang form of the demo's PlumeRecords.wgsl, 1:1.
//
// 📝 Two cell lattices follow the vehicle, addressed toroidally exactly as the scene distance field's
//    cascades are: world cell c lives at texel c mod N, so the window moves by relabelling texels,
//    never by copying them (Frontier's SmokeSpace.h relocates its whole lattice by a copy whenever
//    the carrier leaves the guard band).
//
//    | Lattice  | Edge | Carries                                                                       |
//    |----------|------|-------------------------------------------------------------------------------|
//    | velocity | 2h   | xyz velocity [m/s], w 1 where the scene distance field is solid               |
//    | thermo   | h    | r rubber smoke [-], g temperature above ambient [K], b flame [-], a soot [-]  |
//    | light    | 2h   | r transmittance toward the sun, g toward the zenith, through the plume        |
//
//    Two particulates, because a burnout and a tyre fire are different smokes: vapourised rubber
//    condenses into a bright, highly scattering white-grey cloud (albedo ≈ 0.9), while burning rubber
//    sheds black soot that absorbs almost everything it meets (albedo ≈ 0.1). Both ride the same
//    velocity; the march mixes their optics per sample. XY wrap; Z does not — the window's floor is
//    fixed under the course and its height covers the plume.

//------------------------------------------------------------------------------------------------------------------------
//                                                       RECORDS
//------------------------------------------------------------------------------------------------------------------------

public struct PlumeRecord
{
    public int4  VelocityOrigin;       // [cell] xyz world coarse cell of the window's minimum corner (z is 0), w step ordinal
    public int4  PriorVelocityOrigin;  // [cell] xyz the window the lattice state was written in, w source count
    public uint4 VelocityExtent;       // [cell] xyz coarse lattice extent, w unused
    public uint4 ThermoExtent;         // [cell] xyz fine lattice extent, w inspection (1 shows the cells)
    public float4 Lattice;             // x fine edge h [m], y coarse edge 2h [m], z floor height [m], w step duration [s]
    public float4 Forces;              // x buoyancy [m/s²/K], y smoke weight [m/s²], z vorticity confinement [-], w curl turbulence [m/s]
    public float4 Retention;           // [-] x smoke, y heat, z flame, w velocity — multipliers over this step
    public float4 Wind;                // xyz ambient flow [m/s], w elapsed seconds [s]
    public float4 SourceMinimum;       // xyz bounds of every source [m], w frame duration for moving-solid velocity [s]
    public float4 SourceMaximum;       // xyz [m], w flame emission scale [-]
    public float4 Optical;             // x smoke extinction per density [1/m], y smoke albedo [-], z soot extinction per density [1/m], w march samples
    public float4 Sun;                 // xyz unit direction toward the sun, w soot albedo [-]
    public float4 Combustion;          // x soot shed per flame per second [1/s], y heat released per flame per second [K/s], z soot retention [-], w unused
    public float4 Scattering;          // x multiple-scattering strength [-], y phase anisotropy g [-], z ambient scale [-], w fire-light scale [-]
    public float4 Detail;              // x sub-cell detail strength [-], y detail frequency [1/m], z turbulence frequency [1/m], w unused
    public float4 Flame;               // x core temperature [K], y tip temperature [K], z heat at which a cell reaches the core [K], w unused
};

public struct PlumeSource
{
    public float4 PositionRadius;   // xyz [m] world centre, w [m] Gaussian radius
    public float4 Velocity;         // xyz [m/s] injected momentum
    public float4 Rates;            // x smoke [1/s], y heat [K/s], z flame [1/s], w soot [1/s]
};

// 📝 Burning tyres and exhaust pops as point lights: the march scatters them inside the smoke and the
//    direct-radiance pass lights the car and the road with them.
public struct FireLightRecord
{
    public uint4 Count;                 // x lights in use
    public float4 PositionRadius[4];    // xyz [m], w [m] source radius that softens the inverse square
    public float4 Radiance[4];          // rgb intensity, w unused
};

// 📝 One occupant, for the point velocity a moving solid drags the plume with.
public struct PlumeOccupantRecord
{
    public float4x4 WorldToLocal;
    public float4x4 PreviousLocalToWorld;
    public uint4   Roles;               // x roles bit field; RoleDynamic (8) marks a mover
};

// 📝 The portable stand-in for the demo's cascaded scene distance union: one normalised volume whose
//    texel is the signed distance in cell edges (negative inside a surface) and whose w carries the
//    clip level. The demo's union of four cascades reduces to this whenever the plume only needs the
//    obstruction, which is the only thing any plume program asks of it.
public struct PlumeDistanceRecord
{
    public float4 Minimum;              // xyz world minimum corner [m], w cell edge [m]
    public float4 Extent;               // xyz world extent [m], w reserved
};

public static const float PlumeSpeedCeiling = 40.0;   // [m/s] no cell is allowed to outrun this, whatever the sources say
public static const uint  RoleDynamic = 8u;

[[vk::binding(0, 0)]] public ConstantBuffer<PlumeRecord> Plume;
[[vk::binding(1, 0)]] public ConstantBuffer<PlumeDistanceRecord> PlumeDistance;
[[vk::binding(2, 0)]] public ConstantBuffer<uint4> PlumeCounts;   // x = occupant record count
[[vk::binding(3, 0)]] public Texture3D<float2> PlumeDistanceVolume;
[[vk::binding(4, 0)]] public SamplerState PlumeDistanceSampler;
[[vk::binding(5, 0)]] public StructuredBuffer<PlumeOccupantRecord> PlumeOccupants;

//------------------------------------------------------------------------------------------------------------------------
//                                                   OBSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Signed distance in metres (negative inside a surface) and the clip level that answered.
public float2 SampleSceneDistance(float3 Position)
{
    float3 Coordinates = (Position - PlumeDistance.Minimum.xyz) / PlumeDistance.Extent.xyz;
    if (any(Coordinates < float3(0.0)) || any(Coordinates > float3(1.0)))
        return float2(1.0e6, 4.0);
    float2 Sampled = PlumeDistanceVolume.SampleLevel(PlumeDistanceSampler, Coordinates, 0.0);
    return float2(Sampled.x * PlumeDistance.Minimum.w, Sampled.y);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 TOROIDAL ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

public int3 WrapLattice(int3 Cell, uint3 Extent)
{
    int3 Edge = int3(Extent);
    int2 Wrapped = ((Cell.xy % Edge.xy) + Edge.xy) % Edge.xy;
    return int3(Wrapped, Cell.z);
}

// 📝 Texel of a local velocity cell (0 … extent − 1 inside the current window); local may step one cell outside.
public int3 ResolveVelocityTexel(int3 Local)
{
    return WrapLattice(Plume.VelocityOrigin.xyz + Local, Plume.VelocityExtent.xyz);
}

public int3 ResolveThermoTexel(int3 Local)
{
    return WrapLattice(int3(2 * Plume.VelocityOrigin.xy, 0) + Local, Plume.ThermoExtent.xyz);
}

public float3 ProjectVelocityWorld(uint3 Local)
{
    float3 Cell = float3(Plume.VelocityOrigin.xyz + int3(Local)) + 0.5;
    return float3(Cell.xy * Plume.Lattice.y, Plume.Lattice.z + Cell.z * Plume.Lattice.y);
}

public float3 ProjectThermoWorld(uint3 Local)
{
    float3 Cell = float3(int3(2 * Plume.VelocityOrigin.xy, 0) + int3(Local)) + 0.5;
    return float3(Cell.xy * Plume.Lattice.x, Plume.Lattice.z + Cell.z * Plume.Lattice.x);
}

// 📝 Sampling coordinates: the repeat address mode on U and V performs the wrap; W is clamped.
public float3 ProjectVelocityCoordinates(float3 Position)
{
    float3 Span = float3(Plume.VelocityExtent.xyz) * Plume.Lattice.y;
    return float3(Position.xy / Span.xy, (Position.z - Plume.Lattice.z) / Span.z);
}

public float3 ProjectThermoCoordinates(float3 Position)
{
    float3 Span = float3(Plume.ThermoExtent.xyz) * Plume.Lattice.x;
    return float3(Position.xy / Span.xy, (Position.z - Plume.Lattice.z) / Span.z);
}

// 📝 A position lies in a window's interior when every trilinear tap it takes is a texel of that window —
//    half a cell in from each wrapped face, so a sample beside one edge can never read the opposite
//    edge through the wrap.
public bool ClassifyWindowInterior(float3 Position, int3 Origin, uint3 Extent, float Edge, float Floor)
{
    float3 Local = float3(Position.xy / Edge - float2(Origin.xy), (Position.z - Floor) / Edge);
    return all(Local >= float3(0.5)) && all(Local <= float3(Extent) - float3(0.5));
}

public bool ClassifyPriorVelocity(float3 Position)
{
    return ClassifyWindowInterior(Position, Plume.PriorVelocityOrigin.xyz, Plume.VelocityExtent.xyz, Plume.Lattice.y, Plume.Lattice.z);
}

public bool ClassifyPriorThermo(float3 Position)
{
    return ClassifyWindowInterior(Position, int3(2 * Plume.PriorVelocityOrigin.xy, 0), Plume.ThermoExtent.xyz, Plume.Lattice.x, Plume.Lattice.z);
}

public bool ClassifySourceBounds(float3 Position)
{
    return Plume.PriorVelocityOrigin.w > 0 && all(Position >= Plume.SourceMinimum.xyz) && all(Position <= Plume.SourceMaximum.xyz);
}

// 📐 Gaussian source weight exp(−1.7·d²/r²), r floored at three quarters of a cell so a source narrower than the
//    lattice still deposits; zero beyond 3.5 radii (Frontier's SmokeTransport.slang injection). The centre is lifted
//    0.55 of a cell, as Frontier's is: a contact patch lies on the supporting plane, and a Gaussian centred there
//    would spend half its weight in cells the plane makes solid.
public float ResolveSourceWeight(PlumeSource Source, float3 Position, float Edge)
{
    float Radius = max(Source.PositionRadius.w, 0.75 * Edge);
    float3 Offset = (Position - Source.PositionRadius.xyz - float3(0.0, 0.0, 0.55 * Edge)) / Radius;
    float Squared = dot(Offset, Offset);
    return select(Squared > 12.25, 0.0, exp(-1.7 * Squared));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FLAME COLOUR
//------------------------------------------------------------------------------------------------------------------------

// 📐 The colour of a blackbody at Temperature [K], linear Rec.709, normalised to unit maximum channel: Helland's fit
//    of the Planckian locus (sRGB-encoded) decoded to linear. Valid 1000–6600 K; a tyre fire's soot glows 1100–1500 K
//    at the tips and approaches 1800 K in the core.
public float3 ResolveBlackbodyColour(float Temperature)
{
    float Hecto = clamp(Temperature, 1000.0, 6600.0) / 100.0;
    float Green = clamp((99.4708025861 * log(Hecto) - 161.1195681661) / 255.0, 0.0, 1.0);
    float Blue = select(Hecto <= 19.0, 0.0,
                        clamp((138.5177312231 * log(max(Hecto - 10.0, 1.0)) - 305.0447927307) / 255.0, 0.0, 1.0));
    return pow(float3(1.0, Green, Blue), float3(2.2));
}

// 📐 Stefan–Boltzmann in relative form: radiance rises with the fourth power of temperature, referred to 1500 K.
public float3 ResolveBlackbodyRadiance(float Temperature)
{
    float Ratio = Temperature / 1500.0;
    return ResolveBlackbodyColour(Temperature) * (Ratio * Ratio * Ratio * Ratio);
}
