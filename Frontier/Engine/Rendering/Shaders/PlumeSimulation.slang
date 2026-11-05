/*==========================================================================================================================================
                                                       PLUMESIMULATION.SLANG
                                     ------------------------------------------------------------------
                  Semi-Lagrangian transport of both plume lattices, obstruction by the scene
                  distance field, combustion, sources, decay and coverage; then vorticity
                  confinement; then the warm-started pressure projection.
==========================================================================================================================================*/

// 🧩 The plume step: the Slang form of the demo's PlumeAdvection.wgsl, PlumeVorticity.wgsl and
//    PlumePressure.wgsl, 1:1 in every recurrence.
//
// 📝 AdvectVelocity (coarse), AdvectThermo (fine) and ResolveSunTransmittance (coarse) share one
//    layout; the host binds, per step parity,
//      AdvectVelocity:           1 = velocity state,  2 = thermo state,  4 = next velocity
//      AdvectThermo:             1 = final velocity,  2 = thermo state,  4 = next thermo
//      ResolveSunTransmittance:  1 = final velocity,  2 = next thermo,   4 = light
//    A cell the ONE scene distance field calls solid — the ground plane, a wall, a container, the
//    moving ControlVehicle — holds no gas, and a velocity cell inside a moving occupant takes that
//    occupant's point velocity, so the car's body drags and parts the plume. Frontier gathers the same
//    obstruction through a separate intake (GasSceneDistanceIntake.h); here it is one sample of the
//    field the GI already traces.

import PlumeRecords;

//------------------------------------------------------------------------------------------------------------------------
//                                                       BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 1)]] public Texture3D<float4> VelocityState;
[[vk::binding(1, 1)]] public Texture3D<float4> ThermoState;
[[vk::binding(2, 1)]] public SamplerState LatticeSampler;
[[vk::binding(3, 1)]] public RWTexture3D<float4> LatticeTarget;
[[vk::binding(4, 1)]] public StructuredBuffer<PlumeSource> Sources;
[[vk::binding(5, 1)]] public RWStructuredBuffer<int> Coverage;   // 8 words: low xyz, 0, high xyz, 0

[[vk::binding(0, 2)]] public RWStructuredBuffer<float4> Curl;

[[vk::binding(0, 3)]] public RWStructuredBuffer<float> Divergence;
[[vk::binding(1, 3)]] public RWStructuredBuffer<float> PressureSource;
[[vk::binding(2, 3)]] public RWStructuredBuffer<float> PressureTarget;

// 📝 Sub-grid buoyant rise: a hot thread of gas or a flame tongue rises faster than the coarse lattice
//    resolves, so the thermo backtrace adds a slip toward +Z that saturates with the cell's heat and
//    flame.
public static const float VelocitySolidReach = 0.0;    // [cells]
public static const float ThermoSolidReach = -0.25;    // [cells]
public static const float SubgridHeatRise = 1.4;       // [m/s] at 40 K and above
public static const float SubgridFlameRise = 1.1;      // [m/s] at unit flame and above

//------------------------------------------------------------------------------------------------------------------------
//                                                     OBSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Returns (velocity, 1) inside geometry and (0, 0) in open air. A velocity cell is solid once its
//    centre lies inside a surface, a thermo cell once its centre is a quarter cell inside, so the
//    first layer of gas above the road and around a tyre survives and a wall still holds no smoke.
//    The point velocity of a moving occupant is the displacement of the material point now at Position
//    since the previous frame's transform, over that frame.
public float4 ResolveSolid(float3 Position, float Edge)
{
    float2 Sampled = SampleSceneDistance(Position);
    if (Sampled.x >= VelocitySolidReach * Edge)
        return float4(0.0);

    float3 Velocity = float3(0.0);
    uint Level = uint(Sampled.y);
    if (Level < 4u && Plume.SourceMinimum.w > 0.0)
    {
        // 📝 The portable volume carries the identity of the nearest surface in its second channel; the
        //    demo resolves it per cascade clip. Both answer the same question: which occupant is here?
        uint Identity = uint(Sampled.y + 0.5);
        if (Identity < PlumeCounts.x)
        {
            PlumeOccupantRecord Occupant = PlumeOccupants[Identity];
            if ((Occupant.Roles.x & RoleDynamic) != 0u)
            {
                float3 Prior = mul(Occupant.PreviousLocalToWorld, mul(Occupant.WorldToLocal, float4(Position, 1.0))).xyz;
                Velocity = (Position - Prior) / Plume.SourceMinimum.w;
                float Speed = length(Velocity);
                Velocity = select(Speed > PlumeSpeedCeiling, Velocity * (PlumeSpeedCeiling / Speed), Velocity);
            }
        }
    }
    return float4(Velocity, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  VELOCITY ADVECTION
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(4, 4, 4)]
public void AdvectVelocity(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
        return;

    float Edge = Plume.Lattice.y;
    float Seconds = Plume.Lattice.w;
    float3 Position = ProjectVelocityWorld(Invocation);
    int3 Texel = ResolveVelocityTexel(int3(Invocation));
    float4 Solid = ResolveSolid(Position, Edge);
    if (Solid.w > 0.5)
    {
        LatticeTarget[Texel] = Solid;
        return;
    }

    // ① self-advection; a departure point outside the previous window brings the ambient wind
    float3 Wind = Plume.Wind.xyz;
    float3 Velocity = Wind;
    if (ClassifyPriorVelocity(Position))
    {
        float3 Own = VelocityState.SampleLevel(LatticeSampler, ProjectVelocityCoordinates(Position), 0.0).xyz;
        float3 Departure = Position - Own * Seconds;
        if (ClassifyPriorVelocity(Departure))
            Velocity = VelocityState.SampleLevel(LatticeSampler, ProjectVelocityCoordinates(Departure), 0.0).xyz;
    }

    // ② viscous relaxation toward the ambient flow, then buoyancy: hot gas rises, particulate sinks.
    //    The lift saturates with temperature — Frontier's heat scale puts a burning tyre near 75 K,
    //    which at 2.4 m/s²/K would fire the flame out of its cells at 180 m/s²; flames rise at metres
    //    per second, so the lift tends to 8× the coefficient.
    Velocity = Wind + (Velocity - Wind) * Plume.Retention.w;
    if (ClassifyPriorThermo(Position))
    {
        float4 Thermo = ThermoState.SampleLevel(LatticeSampler, ProjectThermoCoordinates(Position), 0.0);
        float Lift = Plume.Forces.x * Thermo.g / (1.0 + 0.125 * Thermo.g);
        Velocity.z += (Lift - Plume.Forces.y * (Thermo.r + Thermo.a)) * Seconds;
    }

    // ③ sources impart their momentum as a relaxation toward the source velocity, so a strong source
    //    cannot overshoot
    if (ClassifySourceBounds(Position))
    {
        for (uint Index = 0u; Index < uint(Plume.PriorVelocityOrigin.w); ++Index)
        {
            PlumeSource Source = Sources[Index];
            float Weight = ResolveSourceWeight(Source, Position, Edge);
            float Push = 1.0 - exp(-Weight * Seconds * (4.0 + 0.15 * (Source.Rates.x + Source.Rates.w) + 0.05 * Source.Rates.z));
            Velocity = lerp(Velocity, Source.Velocity.xyz, Push);
        }
    }

    float Speed = length(Velocity);
    Velocity = select(Speed > PlumeSpeedCeiling, Velocity * (PlumeSpeedCeiling / Speed), Velocity);
    LatticeTarget[Texel] = float4(Velocity, 0.0);
}

// 📝 The coarse velocity plus divergence-free detail below the lattice: the curl of a world-anchored
//    trigonometric potential, each component omitting its own coordinate (Frontier's
//    SmokeTransport.slang FineVelocity), now in two octaves and scaled by how much is happening at
//    the point — clean air stays still, a boiling plume churns.
public float3 SampleLatticeVelocity(float3 Position, float Activity)
{
    float3 Velocity = VelocityState.SampleLevel(LatticeSampler, ProjectVelocityCoordinates(Position), 0.0).xyz;
    float Time = Plume.Wind.w;
    float Frequency = Plume.Detail.z;
    float3 First = Position * Frequency + float3(0.43, -0.31, 0.27) * Time;
    float3 Second = Position * (Frequency * 2.37) + float3(-0.61, 0.53, -0.89) * Time + float3(1.7, 4.1, 2.9);
    float3 Swirl = float3(sin(First.y) - sin(First.z), sin(First.z) - sin(First.x), sin(First.x) - sin(First.y))
                 + 0.45 * float3(sin(Second.y) - sin(Second.z), sin(Second.z) - sin(Second.x), sin(Second.x) - sin(Second.y));
    return Velocity + Plume.Forces.w * (0.3 + 0.7 * Activity) * Swirl;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THERMO ADVECTION
//------------------------------------------------------------------------------------------------------------------------

groupshared int CoverageLow[3];
groupshared int CoverageHigh[3];

[shader("compute")]
[numthreads(4, 4, 4)]
public void AdvectThermo(uint3 Invocation : SV_DispatchThreadID, uint Lane : SV_GroupIndex)
{
    if (Lane < 3u)
    {
        CoverageLow[Lane] = 2147483647;
        CoverageHigh[Lane] = -2147483647;
    }
    GroupMemoryBarrierWithGroupSync();

    if (all(Invocation < Plume.ThermoExtent.xyz))
    {
        float Edge = Plume.Lattice.x;
        float Seconds = Plume.Lattice.w;
        float3 Position = ProjectThermoWorld(Invocation);
        float4 Thermo = float4(0.0);
        if (SampleSceneDistance(Position).x >= ThermoSolidReach * Edge)
        {
            // ① second-order (midpoint) backtrace through the final velocity of this step
            float Activity = 0.0;
            float2 Rise = float2(0.0);
            if (ClassifyPriorThermo(Position))
            {
                float4 Here = ThermoState.SampleLevel(LatticeSampler, ProjectThermoCoordinates(Position), 0.0);
                Activity = clamp(0.6 * (Here.r + Here.a) + 0.02 * Here.g + Here.b, 0.0, 1.0);
                Rise = clamp(float2(Here.g / 40.0, Here.b), float2(0.0), float2(1.0));
            }
            float3 Slip = float3(0.0, 0.0, SubgridHeatRise * Rise.x + SubgridFlameRise * Rise.y);
            float3 Midpoint = Position - (SampleLatticeVelocity(Position, Activity) + Slip) * (0.5 * Seconds);
            float3 Departure = Position - (SampleLatticeVelocity(Midpoint, Activity) + Slip) * Seconds;
            if (ClassifyPriorThermo(Departure))
                Thermo = ThermoState.SampleLevel(LatticeSampler, ProjectThermoCoordinates(Departure), 0.0);

            // ② combustion: a flame cell keeps heating its gas and sheds soot as it burns — a tyre
            //    fire's black column
            float Burning = max(Thermo.b, 0.0) * Seconds;
            Thermo.g += Burning * Plume.Combustion.y;
            Thermo.a += Burning * Plume.Combustion.x;

            // ③ per-second losses, converted for this step by the host
            Thermo = float4(Thermo.r * Plume.Retention.x, Thermo.g * Plume.Retention.y,
                            Thermo.b * Plume.Retention.z, Thermo.a * Plume.Combustion.z);

            // ④ sources: smoke, heat, flame and soot per second at each Gaussian centre
            if (ClassifySourceBounds(Position))
            {
                for (uint Index = 0u; Index < uint(Plume.PriorVelocityOrigin.w); ++Index)
                {
                    PlumeSource Source = Sources[Index];
                    Thermo += Source.Rates * (ResolveSourceWeight(Source, Position, Edge) * Seconds);
                }
            }
            Thermo = clamp(Thermo, float4(0.0), float4(64.0, 4096.0, 16.0, 64.0));
        }
        LatticeTarget[ResolveThermoTexel(int3(Invocation))] = Thermo;

        // ⑤ coverage: the march only enters the box of cells that hold anything visible
        //    (the host's reduction of the workgroup atomics, which the march reads once per pixel)
        if (Thermo.r > 0.002 || Thermo.a > 0.002 || Thermo.b > 0.01)
        {
            Coverage[0] = min(Coverage[0], int(Invocation.x));
            Coverage[1] = min(Coverage[1], int(Invocation.y));
            Coverage[2] = min(Coverage[2], int(Invocation.z));
            Coverage[4] = max(Coverage[4], int(Invocation.x));
            Coverage[5] = max(Coverage[5], int(Invocation.y));
            Coverage[6] = max(Coverage[6], int(Invocation.z));
        }
    }

    GroupMemoryBarrierWithGroupSync();
    if (Lane < 3u)
    {
        int Low = CoverageLow[Lane];
        int High = CoverageHigh[Lane];
        if (Low <= High)
        {
            InterlockedMin(Coverage[Lane], Low);
            InterlockedMax(Coverage[4u + Lane], High);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SUN TRANSMITTANCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Transmittance through the plume once per coarse cell per step, toward the sun (r) and toward the
//    zenith (g): eight steps each that lengthen with distance through the new thermo lattice. The march
//    then reads both with one filtered sample per point — sunlight self-shadowing, and the sky occluded
//    by the smoke above, which is what darkens the belly of a thick column.
public float ResolveOpticalDepth(float3 Position, float3 Toward)
{
    float Depth = 0.0;
    float Travel = 0.25 * Plume.Lattice.y;
    for (uint Step = 0u; Step < 8u; ++Step)
    {
        float Length = Plume.Lattice.x * (1.0 + 0.6 * float(Step));
        float3 Sample = Position + Toward * (Travel + 0.5 * Length);
        if (!ClassifyWindowInterior(Sample, int3(2 * Plume.VelocityOrigin.xy, 0),
                                    Plume.ThermoExtent.xyz, Plume.Lattice.x, Plume.Lattice.z))
            break;
        float4 Thermo = max(ThermoState.SampleLevel(LatticeSampler, ProjectThermoCoordinates(Sample), 0.0), float4(0.0));
        Depth += (Thermo.r * Plume.Optical.x + Thermo.a * Plume.Optical.z) * Length;
        Travel += Length;
    }
    return Depth;
}

[shader("compute")]
[numthreads(4, 4, 4)]
public void ResolveSunTransmittance(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
        return;
    float3 Position = ProjectVelocityWorld(Invocation);
    float Sun = exp(-ResolveOpticalDepth(Position, Plume.Sun.xyz) * 0.65);
    float Zenith = exp(-ResolveOpticalDepth(Position, float3(0.0, 0.0, 1.0)) * 0.5);
    LatticeTarget[ResolveVelocityTexel(int3(Invocation))] = float4(Sun, Zenith, 0.0, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 VORTICITY CONFINEMENT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Semi-Lagrangian transport smears small eddies away within a few steps. Confinement
//    (Fedkiw, Stam & Jensen 2001) measures ω = ∇ × u, finds N = ∇|ω| / ‖∇|ω|‖ pointing toward stronger
//    rotation, and adds ε·h·(N × ω), which spins the eddies that remain back up — the rolling billows
//    of tyre smoke, at no cost in lattice resolution. Cells outside the window read as their own
//    neighbour (zero gradient).

public float4 LoadVelocity(int3 Local)
{
    int3 Clamped = clamp(Local, int3(0), int3(Plume.VelocityExtent.xyz) - int3(1));
    return VelocityState.Load(int4(ResolveVelocityTexel(Clamped), 0));
}

public uint ResolveLocalIndex(int3 Local)
{
    int3 Extent = int3(Plume.VelocityExtent.xyz);
    int3 Clamped = clamp(Local, int3(0), Extent - int3(1));
    return uint((Clamped.z * Extent.y + Clamped.y) * Extent.x + Clamped.x);
}

[shader("compute")]
[numthreads(4, 4, 4)]
public void ResolveCurl(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
        return;
    int3 Local = int3(Invocation);
    float Scale = 0.5 / Plume.Lattice.y;
    float3 Xp = LoadVelocity(Local + int3(1, 0, 0)).xyz;
    float3 Xm = LoadVelocity(Local - int3(1, 0, 0)).xyz;
    float3 Yp = LoadVelocity(Local + int3(0, 1, 0)).xyz;
    float3 Ym = LoadVelocity(Local - int3(0, 1, 0)).xyz;
    float3 Zp = LoadVelocity(Local + int3(0, 0, 1)).xyz;
    float3 Zm = LoadVelocity(Local - int3(0, 0, 1)).xyz;
    float3 Omega = float3((Yp.z - Ym.z) - (Zp.y - Zm.y),
                          (Zp.x - Zm.x) - (Xp.z - Xm.z),
                          (Xp.y - Xm.y) - (Yp.x - Ym.x)) * Scale;
    Curl[ResolveLocalIndex(Local)] = float4(Omega, length(Omega));
}

[shader("compute")]
[numthreads(4, 4, 4)]
public void ConfineVorticity(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
        return;
    int3 Local = int3(Invocation);
    float4 Own = LoadVelocity(Local);
    float3 Velocity = Own.xyz;
    if (Own.w < 0.5 && Plume.Forces.z > 0.0)
    {
        float Scale = 0.5 / Plume.Lattice.y;
        float3 Eta = float3(Curl[ResolveLocalIndex(Local + int3(1, 0, 0))].w - Curl[ResolveLocalIndex(Local - int3(1, 0, 0))].w,
                            Curl[ResolveLocalIndex(Local + int3(0, 1, 0))].w - Curl[ResolveLocalIndex(Local - int3(0, 1, 0))].w,
                            Curl[ResolveLocalIndex(Local + int3(0, 0, 1))].w - Curl[ResolveLocalIndex(Local - int3(0, 0, 1))].w) * Scale;
        float Length = length(Eta);
        if (Length > 1.0e-5)
        {
            float3 Omega = Curl[ResolveLocalIndex(Local)].xyz;
            Velocity += Plume.Forces.z * Plume.Lattice.y * cross(Eta / Length, Omega) * Plume.Lattice.w;
        }
    }
    LatticeTarget[ResolveVelocityTexel(Local)] = float4(Velocity, Own.w);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PRESSURE PROJECTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Solves ∇²q = ∇·u and replaces u with u − ∇q, the divergence-free part. Boundary conditions per
//    neighbour: a solid cell answers with the centre's q (no flow through the wall — Neumann); a
//    neighbour outside the window sideways or above is open air at q = 0 (Dirichlet); below the floor
//    is ground (Neumann). q persists between steps at toroidal texel addresses, so each step starts
//    from the last solution of the same world cells — a few sweeps then do the work of many
//    (Frontier's CoarseGasField.h starts every step from zero on the CPU). A cell the window has only
//    just covered restarts from zero.

public bool ClassifyInside(int3 Local)
{
    return all(Local >= int3(0)) && all(Local < int3(Plume.VelocityExtent.xyz));
}

public uint ResolvePressureIndex(int3 Local)
{
    int3 Extent = int3(Plume.VelocityExtent.xyz);
    int3 Texel = ResolveVelocityTexel(Local);
    return uint((Texel.z * Extent.y + Texel.y) * Extent.x + Texel.x);
}

// 📝 q at a neighbour under the boundary rules above; Centre is the cell's own q.
public float ResolveNeighbourPressure(int3 Local, float Centre)
{
    if (Local.z < 0)
        return Centre;
    if (!ClassifyInside(Local))
        return 0.0;
    if (LoadVelocity(Local).w > 0.5)
        return Centre;
    return PressureSource[ResolvePressureIndex(Local)];
}

[shader("compute")]
[numthreads(4, 4, 4)]
public void ResolveDivergence(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
        return;
    int3 Local = int3(Invocation);
    float4 Own = LoadVelocity(Local);
    float Scale = 0.5 / Plume.Lattice.y;
    float Flux = (LoadVelocity(Local + int3(1, 0, 0)).x - LoadVelocity(Local - int3(1, 0, 0)).x)
               + (LoadVelocity(Local + int3(0, 1, 0)).y - LoadVelocity(Local - int3(0, 1, 0)).y)
               + (LoadVelocity(Local + int3(0, 0, 1)).z - LoadVelocity(Local - int3(0, 0, 1)).z);
    Divergence[ResolveLocalIndex(Local)] = select(Own.w > 0.5, 0.0, Flux * Scale);

    float3 Position = ProjectVelocityWorld(Invocation);
    if (!ClassifyWindowInterior(Position, Plume.PriorVelocityOrigin.xyz, Plume.VelocityExtent.xyz, Plume.Lattice.y, Plume.Lattice.z))
    {
        uint Index = ResolvePressureIndex(Local);
        PressureSource[Index] = 0.0;
        PressureTarget[Index] = 0.0;
    }
}

[shader("compute")]
[numthreads(4, 4, 4)]
public void RelaxPressure(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
        return;
    int3 Local = int3(Invocation);
    uint Index = ResolvePressureIndex(Local);
    float Centre = PressureSource[Index];
    if (LoadVelocity(Local).w > 0.5)
    {
        PressureTarget[Index] = Centre;
        return;
    }
    float Sum = ResolveNeighbourPressure(Local + int3(1, 0, 0), Centre) + ResolveNeighbourPressure(Local - int3(1, 0, 0), Centre)
              + ResolveNeighbourPressure(Local + int3(0, 1, 0), Centre) + ResolveNeighbourPressure(Local - int3(0, 1, 0), Centre)
              + ResolveNeighbourPressure(Local + int3(0, 0, 1), Centre) + ResolveNeighbourPressure(Local - int3(0, 0, 1), Centre);
    float Edge = Plume.Lattice.y;
    PressureTarget[Index] = (Sum - Divergence[ResolveLocalIndex(Local)] * Edge * Edge) / 6.0;
}

[shader("compute")]
[numthreads(4, 4, 4)]
public void ProjectVelocity(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
        return;
    int3 Local = int3(Invocation);
    float4 Own = LoadVelocity(Local);
    if (Own.w > 0.5)
    {
        LatticeTarget[ResolveVelocityTexel(Local)] = Own;
        return;
    }
    float Centre = PressureSource[ResolvePressureIndex(Local)];
    float Scale = 0.5 / Plume.Lattice.y;
    float3 Gradient = float3(ResolveNeighbourPressure(Local + int3(1, 0, 0), Centre) - ResolveNeighbourPressure(Local - int3(1, 0, 0), Centre),
                             ResolveNeighbourPressure(Local + int3(0, 1, 0), Centre) - ResolveNeighbourPressure(Local - int3(0, 1, 0), Centre),
                             ResolveNeighbourPressure(Local + int3(0, 0, 1), Centre) - ResolveNeighbourPressure(Local - int3(0, 0, 1), Centre)) * Scale;
    float3 Velocity = Own.xyz - Gradient;
    float Speed = length(Velocity);
    Velocity = select(Speed > PlumeSpeedCeiling, Velocity * (PlumeSpeedCeiling / Speed), Velocity);
    LatticeTarget[ResolveVelocityTexel(Local)] = float4(Velocity, 0.0);
}
