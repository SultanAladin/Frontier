/*==========================================================================================================================================
                                                         EFFECTRASTER.SLANG
                                     ------------------------------------------------------------------
                    Derez and rez cubes, drawn into the surface projection like any surface
                             — so they are lit, glow and reflect in the GI.
==========================================================================================================================================*/

// 🧩 Derez and rez cubes: the Slang form of the demo's EffectRaster.wgsl, 1:1.
//
// 📝 No particle state lives anywhere. Each cube's whole life is a closed-form function of its emitter,
//    its anchor and the clock: it waits on the surface until its cell's release, launches with the
//    emitter's velocity plus a burst along the surface normal and a hashed scatter, flies under
//    gravity and linear drag (v' = g − k·v, solved exactly), and when it meets the floor it shatters
//    into eight children that tumble out and shrink away. A rez cube flies the other way, from a shell
//    around the emitter onto its anchor, eased and arcing. The vertex program evaluates that function
//    for the frame's clock (and for the clock a frame ago, for motion vectors); nothing is simulated,
//    nothing is read back, and an emitter costs one instanced draw while it lasts.
//
//    Instance = particle (EmitterIndex · 4096 + index), vertex = 36 corners × (parent + eight children).

import EffectRecords;

//------------------------------------------------------------------------------------------------------------------------
//                                                       BINDINGS
//------------------------------------------------------------------------------------------------------------------------

public struct EffectFrameRecord
{
    public float4x4 ViewProjection;
    public float4x4 UnjitteredViewProjection;
    public float4x4 PreviousUnjitteredViewProjection;
    public float3   CameraPosition;
    public float    Reserved;
    public float2   ScreenExtent;
    public float2   ReservedExtent;
};

[[vk::binding(0, 0)]] public ConstantBuffer<EffectFrameRecord> Frame;
[[vk::binding(2, 0)]] public StructuredBuffer<float4> VehicleCells;   // per cell: local anchor (w its facet's family), local normal
[[vk::binding(3, 0)]] public StructuredBuffer<EffectPose> Poses;

public static const float Gravity = 9.81f;
public static const float ChildLife = 1.2f;      // [s]
public static const float Shell = 2.6f;          // [m] rez cubes start this far out

// 📝 The cubes are not occupants, so their identity is the sky's (SkyIdentity): past the occupant array,
//    where a screen trace reads no card for them.
public static const float EffectIdentity = 2045.0;

// 📝 Octahedral encode, matching PackingCodec.wgsl (and the decode in ScreenTraversal.slang).
public float2 EncodeOctahedral(float3 Normal)
{
    const float2 Projected = Normal.xy / (abs(Normal.x) + abs(Normal.y) + abs(Normal.z));
    if (Normal.z >= 0.0)
        return Projected;
    return (1.0 - abs(Projected.yx)) * select(Projected >= float2(0.0), float2(1.0), float2(-1.0));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       FLIGHT
//------------------------------------------------------------------------------------------------------------------------

public struct EffectVarying
{
    public float4 Position : SV_Position;
    public float3 Normal : TEXCOORD0;
    public uint Family : TEXCOORD1;
    public float4 CurrentClip : TEXCOORD2;
    public float4 PreviousClip : TEXCOORD3;
    public float3 WorldPosition : TEXCOORD4;
    public float Glow : TEXCOORD5;
};

public struct CubeState
{
    public float3 Centre;
    public float Size;         // [m] edge; 0 hides the cube
    public float4 Spin;        // xyz axis, w angle
    public uint Family;
    public float Glow;         // [-] emission as a multiple of the family's albedo (face colours)
};

// 📝 The car's pose at Clock, interpolated between the 60 Hz samples (EffectPose); clamped to the
//    recorded span.
public EffectPose SamplePose(float Clock)
{
    const uint Count = uint(Effect.Colour.z);
    const float Cursor = clamp((Clock - Effect.Colour.w) / EffectPoseSpacing, 0.0, float(Count - 1u));
    const uint Index = min(uint(floor(Cursor)), Count - 2u);
    const float Fraction = clamp(Cursor - float(Index), 0.0, 1.0);
    const EffectPose A = Poses[Index];
    const EffectPose B = Poses[Index + 1u];

    EffectPose Pose;
    Pose.Position = lerp(A.Position, B.Position, Fraction);
    const float4 Aligned = (dot(A.Rotation, B.Rotation) < 0.0) ? -B.Rotation : B.Rotation;
    Pose.Rotation = float4(normalize(lerp(A.Rotation.xyz, Aligned.xyz, Fraction)), 0.0);
    Pose.Velocity = lerp(A.Velocity, B.Velocity, Fraction);
    Pose.Angular = lerp(A.Angular, B.Angular, Fraction);
    return Pose;
}

// 📝 The family a cube wears: the facet it was spread on (the car), the cone's band by height (a cone),
//    else the emitter's own (BodyToWorld[0].w, which the affine transform leaves free).
public uint ResolveFaceFamily(EffectEmitter Emitter, uint Kind, uint Particle, float3 Unit)
{
    if (Kind == EffectKindVehicleDerez || Kind == EffectKindVehicleRez)
        return uint(VehicleCells[Particle * 2u].w + 0.5);

    const uint Family = uint(Emitter.BodyToWorld[0].w + 0.5);
    if (Family == FamilyConeOrange)
    {
        if (Unit.z < 0.12)
            return FamilyConeBase;
        if (Unit.z > 0.45 && Unit.z < 0.66)
            return FamilyConeBand;
    }
    return Family;
}

public float3 RotateAxisAngle(float3 Vector, float3 Axis, float Angle)
{
    const float C = cos(Angle);
    const float S = sin(Angle);
    return Vector * C + cross(Axis, Vector) * S + Axis * dot(Axis, Vector) * (1.0 - C);
}

// 📝 A uniform direction on the sphere, from two hashes.
public float3 HashDirection(uint Seed)
{
    const float U = HashEffect(Seed);
    const float V = HashEffect(Seed ^ 0x68E31DA4u);
    const float Z = 2.0 * U - 1.0;
    const float R = sqrt(max(0.0, 1.0 - Z * Z));
    const float Phi = 6.2831853 * V;
    return float3(R * cos(Phi), R * sin(Phi), Z);
}

// 📐 Linear drag k under gravity: v(τ) = g/k + (v₀ − g/k)·e^(−kτ),  x(τ) = x₀ + (g/k)·τ + (v₀ − g/k)·(1 − e^(−kτ))/k.
public float3 Ballistic(float3 Origin, float3 Velocity, float Drag, float Time)
{
    const float3 Terminal = float3(0.0, 0.0, -Gravity) / Drag;
    return Origin + Terminal * Time + (Velocity - Terminal) * (1.0 - exp(-Drag * Time)) / Drag;
}

public float3 BallisticVelocity(float3 Velocity, float Drag, float Time)
{
    const float3 Terminal = float3(0.0, 0.0, -Gravity) / Drag;
    return Terminal + (Velocity - Terminal) * exp(-Drag * Time);
}

// 📝 First time the cube's bottom meets the floor (z = Floor), by bisection on the ballistic height;
//    Limit if never.
public float ResolveImpact(float3 Origin, float3 Velocity, float Drag, float Floor, float Limit)
{
    if (Ballistic(Origin, Velocity, Drag, Limit).z > Floor)
        return Limit;

    // 📝 Height rises then falls, so step to the first sample below the floor, then bisect that bracket.
    float Low = 0.0;
    float High = Limit;
    for (uint Step = 1u; Step <= 8u; ++Step)
    {
        const float Time = Limit * float(Step) / 8.0;
        if (Ballistic(Origin, Velocity, Drag, Time).z <= Floor)
        {
            High = Time;
            break;
        }
        Low = Time;
    }
    for (uint Iteration = 0u; Iteration < 10u; ++Iteration)
    {
        const float Middle = 0.5 * (Low + High);
        if (Ballistic(Origin, Velocity, Drag, Middle).z <= Floor)
            High = Middle;
        else
            Low = Middle;
    }
    return High;
}

public float3 ResolveAnchor(EffectEmitter Emitter, uint Kind, uint Particle, inout float3 LocalNormal)
{
    if (Kind == EffectKindVehicleDerez || Kind == EffectKindVehicleRez)
    {
        LocalNormal = VehicleCells[Particle * 2u + 1u].xyz;
        return VehicleCells[Particle * 2u].xyz;
    }

    // 📝 A box emitter: a 6 × 6 grid on each of its six faces.
    const uint Face = min(Particle / 36u, 5u);
    const uint Cell = Particle % 36u;
    const float2 Grid = (float2(float(Cell % 6u), float(Cell / 6u)) + 0.5) / 6.0 * 2.0 - 1.0;
    const uint Axis = Face >> 1u;
    const float Sign = ((Face & 1u) == 1u) ? 1.0 : -1.0;

    float3 Local = float3(0.0);
    float3 Facing = float3(0.0);
    if (Axis == 0u)
    {
        Local = float3(Sign, Grid.x, Grid.y);
        Facing = float3(Sign, 0.0, 0.0);
    }
    else if (Axis == 1u)
    {
        Local = float3(Grid.x, Sign, Grid.y);
        Facing = float3(0.0, Sign, 0.0);
    }
    else
    {
        Local = float3(Grid.x, Grid.y, Sign);
        Facing = float3(0.0, 0.0, Sign);
    }
    LocalNormal = Facing;
    return Emitter.Centre.xyz + Local * Emitter.Extent.xyz;
}

public CubeState ResolveCube(EffectEmitter Emitter, uint Particle, uint Child, float Clock)
{
    CubeState Cube;
    Cube.Centre = float3(0.0);
    Cube.Size = 0.0;
    Cube.Family = FamilyDerezGlow;
    Cube.Spin = float4(0.0, 0.0, 1.0, 0.0);
    Cube.Glow = 0.0;

    const uint Kind = uint(Emitter.Angular.w);
    const uint Count = uint(Emitter.Extent.w);
    if (Particle >= Count)
        return Cube;

    const uint Seed = uint(Emitter.Centre.w) ^ (Particle * 9781u);
    float3 LocalNormal = float3(0.0, 0.0, 1.0);
    const float3 Anchor = ResolveAnchor(Emitter, Kind, Particle, LocalNormal);
    const bool Vehicle = Kind == EffectKindVehicleDerez || Kind == EffectKindVehicleRez;
    const float3 Minimum = Vehicle ? Effect.BodyMinimum.xyz : (Emitter.Centre.xyz - Emitter.Extent.xyz);
    const float3 Maximum = Vehicle ? Effect.BodyMaximum.xyz : (Emitter.Centre.xyz + Emitter.Extent.xyz);
    const float3 Unit = (Anchor - Minimum) / max(Maximum - Minimum, float3(1.0e-3));
    const float Scale = Vehicle ? 1.0 : 0.45;

    float3 World = mul(Emitter.BodyToWorld, float4(Anchor, 1.0)).xyz;
    float3 Normal = normalize(mul(Emitter.BodyToWorld, float4(LocalNormal, 0.0)).xyz);
    const float Elapsed = Clock - Emitter.Velocity.w;
    const bool Faces = Effect.Colour.x > 0.5;
    const uint FaceFamily = ResolveFaceFamily(Emitter, Kind, Particle, Unit);
    const bool Debris = Kind == EffectKindDebris;
    const float Size = Effect.Timing.y * (Vehicle ? 1.0 : (Debris ? 0.6 : 0.75)) * (0.75 + 0.5 * HashEffect(Seed ^ 0x51u));
    const float3 SpinAxis = HashDirection(Seed ^ 0x3Au);
    const float SpinRate = 4.0 + 10.0 * HashEffect(Seed ^ 0x7Bu);

    if (Kind == EffectKindVehicleRez || Kind == EffectKindBoxRez)
    {
        // 📝 Rez: from the shell onto the anchor, arriving when the cell completes; only the parent flies.
        if (Child != 0u)
            return Cube;

        const float Arrival = ResolveRezArrival(Unit, uint(Emitter.Centre.w), Effect.Style, Scale);
        if (Elapsed < 0.0 || Elapsed >= Arrival)
            return Cube;

        const float3 Centre = mul(Emitter.BodyToWorld, float4(Emitter.Centre.xyz, 1.0)).xyz;
        const float3 Outward = normalize(normalize(World - Centre) + 0.6 * HashDirection(Seed ^ 0x2Cu) + float3(0.0, 0.0, 0.35));
        const float3 Start = World + Outward * Shell * Scale * (0.7 + 0.6 * HashEffect(Seed ^ 0x1Du));
        const float Progress = clamp(Elapsed / Arrival, 0.0, 1.0);
        const float Eased = 1.0 - pow(1.0 - Progress, 3.0);
        Cube.Centre = lerp(Start, World, Eased) + float3(0.0, 0.0, 0.5 * Scale * sin(3.14159265 * Progress));
        Cube.Size = Size * (0.6 + 0.4 * Progress);
        Cube.Spin = float4(SpinAxis, SpinRate * (Arrival - Elapsed));
        Cube.Family = (Progress > 0.82) ? FamilyDerezHot : FamilyDerezGlow;
        if (Effect.Style == 1u)
        {
            Cube.Family = (Progress > 0.82) ? FamilyDerezHot : FamilyDerezGlitch;
            Cube.Centre += 0.06 * (float3(HashEffect(Seed ^ uint(Clock * 12.0)), HashEffect(Seed ^ uint(Clock * 12.0) ^ 7u), 0.5) - 0.5);
        }
        if (Faces)
        {
            // 📝 Assembling: each cube already wears its facet's colour, glowing brighter as it closes on
            //    its place.
            Cube.Family = FaceFamily;
            Cube.Glow = Effect.Colour.y * (0.35 + 2.0 * smoothstep(0.7, 1.0, Progress));
        }
        return Cube;
    }

    // 📝 Derez: wait for the release, launch, fall, shatter.
    const float Release = Debris ? 0.04 * HashEffect(Seed ^ 0x13u)
                                 : ResolveDerezRelease(Unit, uint(Emitter.Centre.w), Effect.Style, Scale);
    const float Flight = Elapsed - Release;
    const float Life = Debris ? 2.2 + 0.6 * HashEffect(Seed ^ 0x4Eu) : Effect.Timing.x * (0.85 + 0.3 * HashEffect(Seed ^ 0x4Eu));
    if (Flight < 0.0 || Flight > Life)
        return Cube;

    const float Drag = Debris ? 0.5 : max(Effect.Motion.w, 0.05);
    const float Launch = Debris ? 2.0 + 5.0 * HashEffect(Seed ^ 0x65u) : lerp(Effect.Motion.y, Effect.Motion.z, HashEffect(Seed ^ 0x65u));
    float3 Inherited = Emitter.Velocity.xyz + cross(Emitter.Angular.xyz, World - Emitter.BodyToWorld[3].xyz);
    if (Kind == EffectKindVehicleDerez && Effect.Colour.z >= 2.0)
    {
        // 📝 The car kept moving: leave from where it was at this cell's release, at its velocity there.
        const EffectPose Pose = SamplePose(Emitter.Velocity.w + Release);
        World = Pose.Position.xyz + RotateByQuaternion(Pose.Rotation, Anchor);
        Normal = normalize(RotateByQuaternion(Pose.Rotation, LocalNormal));
        Inherited = Pose.Velocity.xyz + cross(Pose.Angular.xyz, World - Pose.Position.xyz);
    }
    const float3 Velocity = Inherited + Normal * (Debris ? 3.0 : Effect.Motion.x)
                          + HashDirection(Seed ^ 0x99u) * Launch + float3(0.0, 0.0, 0.6 * Launch);
    const float Floor = 0.5 * Size;
    const float Impact = ResolveImpact(World, Velocity, Drag, Floor, Life);

    if (Child == 0u)
    {
        if (Flight >= Impact)
            return Cube;
        Cube.Centre = Ballistic(World, Velocity, Drag, Flight);
        Cube.Size = Size;
        Cube.Spin = float4(SpinAxis, SpinRate * Flight);
        Cube.Family = (Flight < EffectHotSeconds) ? FamilyDerezHot : FamilyDerezGlow;
        if (Effect.Style == 1u)
        {
            Cube.Family = ((uint(Clock * 12.0) + Particle) % 3u == 0u) ? FamilyDerezGlow : FamilyDerezGlitch;
            Cube.Centre += 0.06 * (float3(HashEffect(Seed ^ uint(Clock * 12.0)), HashEffect(Seed ^ uint(Clock * 12.0) ^ 7u), 0.5) - 0.5);
        }
        if (Faces || Debris)
        {
            Cube.Family = FaceFamily;
            Cube.Glow = Debris ? 0.0 : ResolveFaceGlow(Flight, Effect.Colour.y);
        }
        return Cube;
    }

    // 📝 Children: born at the impact point with the parent's bounce and a corner's direction,
    //    ground-clamped.
    const float ChildTime = Flight - Impact;
    if (Impact >= Life || ChildTime < 0.0 || ChildTime > ChildLife)
        return Cube;

    const float3 Hit = Ballistic(World, Velocity, Drag, Impact);
    const float3 Incoming = BallisticVelocity(Velocity, Drag, Impact);
    const float3 Corner = float3((((Child - 1u) & 1u) != 0u) ? 1.0 : -1.0,
                                 (((Child - 1u) & 2u) != 0u) ? 1.0 : -1.0,
                                 (((Child - 1u) & 4u) != 0u) ? 1.0 : -1.0);
    const float3 Scatter = normalize(Corner + 0.6 * HashDirection(Seed ^ (Child * 131u)));
    const float3 Bounce = float3(Incoming.xy * 0.35, abs(Incoming.z) * 0.25);
    const float3 ChildVelocity = Bounce + Scatter * (0.8 + 1.0 * HashEffect(Seed ^ (Child * 17u))) + float3(0.0, 0.0, 0.9);
    float3 Position = Ballistic(Hit + Corner * 0.25 * Size, ChildVelocity, 1.2, ChildTime);
    const float ChildSize = 0.45 * Size * (1.0 - smoothstep(0.65 * ChildLife, ChildLife, ChildTime));
    Position.z = max(Position.z, 0.5 * ChildSize);
    Cube.Centre = Position;
    Cube.Size = ChildSize;
    Cube.Spin = float4(HashDirection(Seed ^ (Child * 29u)), 9.0 * ChildTime);
    Cube.Family = (Effect.Style == 1u && (Child & 1u) == 0u) ? FamilyDerezGlitch : FamilyDerezGlow;
    if (Faces || Debris)
    {
        // 📝 The shards keep the colour of what they broke from, a last ember of the glow fading as they
        //    shrink.
        Cube.Family = FaceFamily;
        Cube.Glow = Debris ? 0.0 : 0.4 * Effect.Colour.y * exp(-ChildTime / 0.4);
    }
    return Cube;
}

// 📝 The 36 corners of a unit cube (six faces of two triangles, counter-clockwise from outside) and
//    their normals.
public float3 ResolveCubeCorner(uint Corner, inout float3 LocalNormal)
{
    const uint Face = Corner / 6u;
    const uint Index = Corner % 6u;
    float2 Square[6] = { float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
                         float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0) };
    const float2 Q = Square[Index];
    const float Sign = ((Face & 1u) == 1u) ? 1.0 : -1.0;
    const uint Axis = Face >> 1u;

    float3 Point = float3(0.0);
    if (Axis == 0u)
    {
        Point = float3(Sign, Q.x * Sign, Q.y);
        LocalNormal = float3(Sign, 0.0, 0.0);
    }
    else if (Axis == 1u)
    {
        Point = float3(-Q.x * Sign, Sign, Q.y);
        LocalNormal = float3(0.0, Sign, 0.0);
    }
    else
    {
        Point = float3(Q.x, Q.y * Sign, Sign);
        LocalNormal = float3(0.0, 0.0, Sign);
    }
    return 0.5 * Point;
}

public float3 PlaceCorner(CubeState Cube, float3 Local)
{
    return Cube.Centre + RotateAxisAngle(Local * Cube.Size, Cube.Spin.xyz, Cube.Spin.w);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ENTRIES
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
public EffectVarying EffectVertex(uint VertexIndex : SV_VertexID, uint InstanceIndex : SV_InstanceID)
{
    const uint EmitterIndex = min(InstanceIndex / 4096u, 7u);
    const uint Particle = InstanceIndex % 4096u;
    const EffectEmitter Emitter = Effect.Emitters[EmitterIndex];
    const uint Child = VertexIndex / 36u;

    float3 LocalNormal = float3(0.0, 0.0, 1.0);
    const float3 Local = ResolveCubeCorner(VertexIndex % 36u, LocalNormal);
    const CubeState Now = ResolveCube(Emitter, Particle, Child, Effect.Clock);
    const CubeState Before = ResolveCube(Emitter, Particle, Child, Effect.Clock - Effect.BodyMaximum.w);

    EffectVarying Varying;
    Varying.Normal = float3(0.0, 0.0, 1.0);
    Varying.Family = FamilyDerezGlow;
    Varying.CurrentClip = float4(0.0);
    Varying.PreviousClip = float4(0.0);
    Varying.WorldPosition = float3(0.0);
    Varying.Glow = 0.0;

    if (Now.Size <= 0.0)
    {
        Varying.Position = float4(0.0, 0.0, -2.0, 1.0);       // outside the clip volume: culled
        return Varying;
    }

    const float3 World = PlaceCorner(Now, Local);
    const float3 Previous = (Before.Size > 0.0) ? PlaceCorner(Before, Local) : World;
    Varying.Position = mul(Frame.ViewProjection, float4(World, 1.0));
    Varying.CurrentClip = mul(Frame.UnjitteredViewProjection, float4(World, 1.0));
    Varying.PreviousClip = mul(Frame.PreviousUnjitteredViewProjection, float4(Previous, 1.0));
    Varying.Normal = RotateAxisAngle(LocalNormal, Now.Spin.xyz, Now.Spin.w);
    Varying.Family = Now.Family;
    Varying.WorldPosition = World;
    Varying.Glow = Now.Glow;
    return Varying;
}

public float2 ProjectEffectPixel(float4 Clip)
{
    const float2 Ndc = Clip.xy / Clip.w;
    return float2(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5) * Frame.ScreenExtent;
}

public struct EffectTargets
{
    public float4 Attributes : SV_Target0;
    public float2 Motion : SV_Target1;
    public float Identity : SV_Target2;
};

[shader("fragment")]
public EffectTargets EffectFragment(EffectVarying Varying)
{
    float3 Normal = normalize(Varying.Normal);
    if (dot(Normal, Frame.CameraPosition - Varying.WorldPosition) < 0.0)
        Normal = -Normal;

    EffectTargets Targets;
    Targets.Attributes = float4(EncodeOctahedral(Normal), float(Varying.Family), 1.0 + max(Varying.Glow, 0.0));
    Targets.Motion = ProjectEffectPixel(Varying.CurrentClip) - ProjectEffectPixel(Varying.PreviousClip);
    Targets.Identity = EffectIdentity;
    return Targets;
}
