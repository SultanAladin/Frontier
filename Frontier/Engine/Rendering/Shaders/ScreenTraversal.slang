/*==========================================================================================================================================
                                                       SCREENTRAVERSAL.SLANG
                                     ------------------------------------------------------------------
                    Screen traces ahead of the scene distance field: a stackless walk over
                     the nearest-depth pyramid of this frame, as Lumen does, followed by
                     the one rule that decides what radiance a sample point sends back.
==========================================================================================================================================*/

// 🧩 Screen traces: the Slang form of the demo's ScreenTraversal.wgsl, 1:1.
//
// 📝 A gather or reflection ray is answered in two parts. It first walks the ray across the screen
//    through a pyramid of nearest depths, which resolves contact detail far finer than any
//    distance-field voxel. A ray the screen cannot settle — it leaves the viewport, passes behind a
//    surface, strikes the back of one, or exhausts its steps — goes on through the ONE scene distance
//    field, starting from the last point the screen proved unoccluded. That point is sound: wherever
//    the ray lies in front of the nearest depth of a tile, nothing rastered stands between it and the
//    eye, and everything inside the viewport is rastered.
//
// 📝 This module owns the screen half only. The card depot and the sky live with the caller, so the
//    exitance resolvers below take the fallback radiance they would otherwise look up: pass
//    SampleCardRadiance / SampleSkyRadiance there and the combination matches the demo exactly.

//------------------------------------------------------------------------------------------------------------------------
//                                                       RECORDS
//------------------------------------------------------------------------------------------------------------------------

public struct ScreenFrameRecord
{
    public float4x4 View;
    public float4x4 ViewProjection;
    public float4x4 InverseViewProjection;
    public float4x4 PreviousUnjitteredViewProjection;
    public float4   InverseScreenExtent;   // xy = 1 / extent; the projection helpers below need it
    public float    CameraNear;
    public uint     Features2;
};

// 📝 One entry per occupant, for carrying a point on a moving occupant back through its own motion.
public struct ScreenOccupantRecord
{
    public float4x4 WorldToLocal;
    public float4x4 PreviousLocalToWorld;
    public uint4    Roles;
};

[[vk::binding(0, 0)]] public ConstantBuffer<ScreenFrameRecord> Frame;
[[vk::binding(1, 0)]] public ConstantBuffer<uint4> ScreenCounts;            // x = occupant record count
[[vk::binding(2, 0)]] public StructuredBuffer<ScreenOccupantRecord> ScreenOccupants;

// 📝 Group 1: what the walk reads (the demo's group 3, bindings 4 to 8).
[[vk::binding(0, 1)]] public Texture2D<float> NearestDepth;       // [-] reversed depth; level L holds the nearest of each 2^L tile
[[vk::binding(1, 1)]] public Texture2D<float> PriorNearestDepth;  // [-] the same pyramid one frame earlier; level 0 is read
[[vk::binding(2, 1)]] public Texture2D<float4> ScreenAttributes;  // [-] octahedral normal in xy, material family in z, identity in w
[[vk::binding(3, 1)]] public Texture2D<float> ScreenIdentity;     // [-] occupant identity (r16float in the demo)
[[vk::binding(4, 1)]] public Texture2D<float4> PriorExitance;     // [-] previous frame's diffuse exitance per pixel

public static const float ScreenSinkTolerance     = 0.0015;
public static const float ScreenRelativeThickness = 0.02;
public static const float ScreenAbsoluteThickness = 0.03;
public static const float ScreenExitanceAgreement = 0.02;
public static const float ScreenUnreachedTravel   = 1.0e30;

public static const uint SkyIdentity = 2045u;
public static const uint ExtendedScreenTraversal = 1u;
public static const uint FeatureTemporalRestart = 2048u;
public static const uint RoleDynamic = 8u;

//------------------------------------------------------------------------------------------------------------------------
//                                                     PROJECTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 World → (u, v, reversed depth) through this frame's jittered projection, the one the depth was rastered with.
public float3 ProjectScreenPoint(float3 Position)
{
    float4 Clip = mul(Frame.ViewProjection, float4(Position, 1.0));
    float3 Ndc = Clip.xyz / Clip.w;
    return float3(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5, Ndc.z);
}

public float3 ReconstructScreenPoint(float3 Point)
{
    float4 Clip = mul(Frame.InverseViewProjection, float4(Point.x * 2.0 - 1.0, 1.0 - Point.y * 2.0, Point.z, 1.0));
    return Clip.xyz / Clip.w;
}

// 📝 Linear view distance along −Z of a world position.
public float ResolveViewDepth(float3 Position)
{
    return -mul(Frame.View, float4(Position, 1.0)).z;
}

// 📝 Octahedral decode, matching PackingCodec.wgsl.
public float3 DecodeOctahedral(float2 Encoded)
{
    float2 Signed = Encoded * 2.0 - float2(1.0, 1.0);
    float2 Absolute = abs(Signed);
    float3 Normal = float3(Signed.x, Signed.y, 1.0 - Absolute.x - Absolute.y);
    float T = max(-Normal.z, 0.0);
    Normal.x += (Normal.x >= 0.0) ? -T : T;
    Normal.y += (Normal.y >= 0.0) ? -T : T;
    return normalize(Normal);
}

// 📝 Normal offset of a screen-trace origin: about two pixels of footprint, far below the distance-field
//    bias, so the walk keeps the creases the distance field cannot resolve.
public float ResolveScreenBias(float ViewDepth)
{
    return 0.004 + 0.002 * ViewDepth;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  NEAREST-DEPTH WALK
//------------------------------------------------------------------------------------------------------------------------

public struct ScreenHit
{
    public float3 Position;   // [m]   on the ray, at the struck surface
    public float  Travel;     // [m]   ray parameter at the strike, or at the last point the screen proved unoccluded
    public float3 Normal;     // [-]   shading normal of the struck pixel
    public uint   Identity;   // [-]   occupant identity of the struck pixel (SkyIdentity when nothing was struck)
    public bool   Hit;        // [-]   a validated strike
};

// 📐 Stackless walk over the pyramid in (u, v, d) space, where a world segment stays a segment because the
//    projection is projective. At level L the ray either ① lies in front of the tile's nearest depth and
//    crosses to the tile's exit boundary — then it climbs a level — or ② meets the tile's nearest-depth
//    plane first, or already lies behind it — then it descends. Descending below level 0 means the ray has
//    met the depth surface at one pixel, which is a strike if the ray lies within the surface thickness,
//    faces the surface, and has left its own neighbourhood.
public ScreenHit TraverseScreenDepth(float3 Origin, float3 Direction, float MaximumTravel, uint StepLimit)
{
    ScreenHit Result;
    Result.Hit = false;
    Result.Travel = 0.0;
    Result.Position = Origin;
    Result.Normal = -Direction;
    Result.Identity = SkyIdentity;

    // ① keep the far end in front of the eye, then clip the projected segment to the viewport
    float OriginDepth = ResolveViewDepth(Origin);
    float DepthRate = -mul(Frame.View, float4(Direction, 0.0)).z;
    float Reach = MaximumTravel;
    if (DepthRate < 0.0)
        Reach = min(Reach, (OriginDepth - 2.0 * Frame.CameraNear) / -DepthRate);
    if (OriginDepth <= 2.0 * Frame.CameraNear || Reach <= 0.0)
        return Result;

    float3 Start = ProjectScreenPoint(Origin);
    if (any(Start.xy <= float2(0.0)) || any(Start.xy >= float2(1.0)))
        return Result;

    float3 Projected = ProjectScreenPoint(Origin + Direction * Reach) - Start;
    float Exit = 1.0;
    for (uint Axis = 0u; Axis < 2u; ++Axis)
    {
        if (Projected[Axis] > 0.0)
            Exit = min(Exit, (1.0 - Start[Axis]) / Projected[Axis]);
        else if (Projected[Axis] < 0.0)
            Exit = min(Exit, -Start[Axis] / Projected[Axis]);
    }
    // 📝 Re-parameterised over the visible part only, so one pixel is never a vanishing fraction of the parameter.
    float3 Delta = Projected * Exit;

    uint BaseWidth = 0u;
    uint BaseHeight = 0u;
    uint LevelCount = 0u;
    NearestDepth.GetDimensions(0u, BaseWidth, BaseHeight, LevelCount);
    float2 BaseExtent = float2(float(BaseWidth), float(BaseHeight));

    bool3 Moving = abs(Delta) > float3(1.0e-12);
    float3 Inverse = select(Moving, 1.0 / select(Moving, Delta, float3(1.0)), float3(ScreenUnreachedTravel));
    // 📝 A component that does not move takes the forward side, so its boundary lies at +∞ rather than −∞.
    bool2 Receding = (Delta.xy < float2(0.0)) & Moving.xy;
    float2 ExitSide = select(Receding, float2(0.0), float2(1.0));
    float2 Nudge = select(Receding, float2(-0.005), float2(0.005)) / BaseExtent;

    // ② leave the origin pixel before the first comparison: a surface never occludes itself
    float2 StartPixel = floor(Start.xy * BaseExtent);
    float2 FirstCrossing = ((StartPixel + ExitSide) / BaseExtent + Nudge - Start.xy) * Inverse.xy;
    float Travel = min(FirstCrossing.x, FirstCrossing.y);

    // ③ the walk
    int Level = 0;
    bool Descended = false;
    for (uint Iteration = 0u; Iteration < StepLimit; ++Iteration)
    {
        if (Travel > 1.0)
            break;

        float3 Point = Start + Delta * Travel;
        float2 Resolution = BaseExtent * exp2(-float(Level));
        float2 Cell = floor(Point.xy * Resolution);

        uint LevelWidth = 0u;
        uint LevelHeight = 0u;
        NearestDepth.GetDimensions(uint(max(Level, 0)), LevelWidth, LevelHeight, LevelCount);
        int2 Texel = clamp(int2(Cell), int2(0), int2(int(LevelWidth), int(LevelHeight)) - int2(1));

        float Tolerated = NearestDepth.Load(int3(Texel, max(Level, 0))).r * (1.0 - ScreenSinkTolerance);
        float2 Crossing = ((Cell + ExitSide) / Resolution + Nudge - Start.xy) * Inverse.xy;
        float Boundary = min(Crossing.x, Crossing.y);

        float Sinking = ScreenUnreachedTravel;
        if (Delta.z < 0.0 && Moving.z)
            Sinking = (Tolerated - Start.z) * Inverse.z;

        bool InFront = Point.z > Tolerated;
        if (InFront)
            Travel = min(Boundary, Sinking);

        if (InFront && Boundary < Sinking)
            Level = min(Level + 1, int(LevelCount) - 1);
        else
        {
            --Level;
            if (Level < 0)
            {
                Descended = true;
                break;
            }
        }
    }

    // ④ the last point the walk proved unoccluded is where the distance field takes over
    float3 Settled = Start + Delta * clamp(Travel, 0.0, 1.0);
    Result.Travel = max(dot(ReconstructScreenPoint(Settled) - Origin, Direction), 0.0);
    if (!Descended)
        return Result;

    // ⑤ validate the strike
    int2 Pixel = int2(floor(Settled.xy * BaseExtent));
    if (any(Pixel < int2(0)) || any(Pixel >= int2(BaseExtent)) || all(abs(Pixel - int2(StartPixel)) <= int2(1)))
        return Result;

    float SurfaceDepth = NearestDepth.Load(int3(Pixel, 0)).r;
    if (SurfaceDepth <= 0.0)
        return Result;

    float4 Attributes = ScreenAttributes.Load(int3(Pixel, 0));
    float3 Normal = DecodeOctahedral(Attributes.xy);
    if (dot(Normal, Direction) >= 0.0)
        return Result;

    float SurfaceView = Frame.CameraNear / SurfaceDepth;
    float RayView = Frame.CameraNear / max(Settled.z, 1.0e-12);
    if (RayView - SurfaceView > max(ScreenRelativeThickness * SurfaceView, ScreenAbsoluteThickness))
        return Result;

    Result.Hit = true;
    Result.Position = Origin + Direction * Result.Travel;
    Result.Normal = Normal;
    Result.Identity = uint(Attributes.w + 0.5);
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SAMPLE EXITANCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The previous frame's diffuse exitance at a world point, or w = 0 where it cannot be trusted.
//    ① This frame's screen must show the point at its pixel — same depth, same occupant — so a point
//    hidden now never borrows the surface in front of it. ② A point on a moving occupant is carried
//    back by that occupant's previous transform. ③ The previous frame's depth must agree at the
//    reprojected pixel, which rejects every disocclusion.
public float4 ResolvePriorExitance(float3 Position, uint Identity)
{
    float3 Point = ProjectScreenPoint(Position);
    if (any(Point.xy <= float2(0.0)) || any(Point.xy >= float2(1.0)) || Point.z <= 0.0)
        return float4(0.0);

    uint Width = 0u;
    uint Height = 0u;
    NearestDepth.GetDimensions(Width, Height);
    float2 Extent = float2(float(Width), float(Height));
    int2 Pixel = int2(Point.xy * Extent);

    float Surface = NearestDepth.Load(int3(Pixel, 0)).r;
    if (Surface <= 0.0 || abs(Point.z - Surface) > ScreenExitanceAgreement * Surface)
        return float4(0.0);
    if (uint(ScreenIdentity.Load(int3(Pixel, 0)).r + 0.5) != Identity)
        return float4(0.0);

    float3 Prior = Position;
    if (Identity < ScreenCounts.x)
    {
        ScreenOccupantRecord Occupant = ScreenOccupants[Identity];
        if ((Occupant.Roles.x & RoleDynamic) != 0u)
            Prior = mul(Occupant.PreviousLocalToWorld, mul(Occupant.WorldToLocal, float4(Position, 1.0))).xyz;
    }

    float4 PriorClip = mul(Frame.PreviousUnjitteredViewProjection, float4(Prior, 1.0));
    if (PriorClip.w <= Frame.CameraNear)
        return float4(0.0);
    float3 PriorNdc = PriorClip.xyz / PriorClip.w;
    float2 PriorPoint = float2(PriorNdc.x * 0.5 + 0.5, 0.5 - PriorNdc.y * 0.5);
    if (any(PriorPoint <= float2(0.0)) || any(PriorPoint >= float2(1.0)))
        return float4(0.0);

    int2 PriorPixel = int2(PriorPoint * Extent);
    float PriorSurface = PriorNearestDepth.Load(int3(PriorPixel, 0)).r;
    if (PriorSurface <= 0.0 || abs(PriorNdc.z - PriorSurface) > ScreenExitanceAgreement * PriorSurface)
        return float4(0.0);

    return float4(PriorExitance.Load(int3(PriorPixel, 0)).rgb, 1.0);
}

// 📝 Radiance leaving a sample point, by the one rule above. Fallback is what the caller's cards (or sky)
//    answer where the screen cannot. A temporal restart (a lighting discontinuity, a resize) leaves the
//    previous frame stale, so the fallback answers alone.
public float4 ResolveSampleExitance(float3 Position, float3 Normal, uint Identity, float4 Fallback)
{
    if ((Frame.Features2 & ExtendedScreenTraversal) != 0u && (Frame.Features2 & FeatureTemporalRestart) == 0u)
    {
        float4 Screen = ResolvePriorExitance(Position, Identity);
        if (Screen.w > 0.0)
            return Screen;
    }
    return Fallback;
}

// 📝 What a screen strike brings back: the sample exitance at the strike, or nothing where the screen
//    has no trustworthy radiance for it.
public float3 ResolveStrikeRadiance(ScreenHit Strike, float4 CardFallback)
{
    float4 Exitance = ResolveSampleExitance(Strike.Position, Strike.Normal, Strike.Identity, CardFallback);
    return (Exitance.w > 0.0) ? Exitance.rgb : float3(0.0);
}
