//============================================================================================================================================
//                                                            RAYTRAVERSAL.WGSL
//============================================================================================================================================
// 🧩 Hardware-agnostic Two-Level BVH Ray-Triangle Intersection (TLAS over scene occupants + Binned-SAH BLAS per mesh),
//    Two-Layer PBR BSDF Importance Sampling, RIS Next-Event Estimation (Sun + IES Clustered Lights + Fire),
//    Multiple Importance Sampling (MIS), Russian Roulette, and Multi-Bounce Path Tracing for ReSTIR GI.

//! include "CardSampling.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "PackingCodec.wgsl"
//! include "DiscrepancySequence.wgsl"
//! include "FireIllumination.wgsl"
//! include "ClusteredLights.wgsl"
//! include "ReservoirRecords.wgsl"

struct BoundingNode
{
    Minimum     : vec3f,
    LeftOrFirst : u32,
    Maximum     : vec3f,
    Count       : u32,
}

@group(3) @binding(0) var<storage, read> BvhNodes     : array<BoundingNode>;
@group(3) @binding(1) var<storage, read> BvhTriangles : array<vec4f>;
@group(3) @binding(2) var<storage, read> RayMaterials : array<MaterialRecord>;
@group(3) @binding(3) var<uniform> RayLights          : LightTable;

const TlasLeafBit        : u32 = 0x80000000u;
const TlasIndexMask      : u32 = 0x7FFFFFFFu;
const EmptyTlasSentinel  : u32 = 0x7FFFFFFFu;
const BvhStackCapacity   : u32 = 48u;
const MaxBvhSteps        : u32 = 384u;
const RayEpsilon         : f32 = 2.0e-3;
const SolarAngularRadius : f32 = 0.0047;

struct RayHit
{
    Position : vec3f,   // [m]   world-space intersection point
    Travel   : f32,     // [m]   ray parameter t
    Normal   : vec3f,   // [-]   interpolated smooth shading normal in world space
    Geometry : vec3f,   // [-]   geometric triangle normal in world space
    Identity : u32,     // [-]   Occupant index, GroundIdentity, or SkyIdentity on miss
    Family   : u32,     // [-]   exact triangle MaterialRecord index
    Steps    : u32,     // [-]   BVH traversal step count
    Hit      : bool,
}

fn SafeReciprocal(Direction: vec3f) -> vec3f
{
    let Safe = select(Direction, vec3f(1.0e-9), abs(Direction) < vec3f(1.0e-9));
    return 1.0 / Safe;
}

fn IntersectNodeBounds(Origin: vec3f, Reciprocal: vec3f, Minimum: vec3f, Maximum: vec3f, Limit: f32) -> f32
{
    let Near  = (Minimum - Origin) * Reciprocal;
    let Far   = (Maximum - Origin) * Reciprocal;
    let Low   = min(Near, Far);
    let High  = max(Near, Far);
    let Entry = max(max(Low.x, Low.y), max(Low.z, 0.0));
    let Exit  = min(min(High.x, High.y), min(High.z, Limit));
    return select(1.0e30, Entry, Entry <= Exit);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           TLAS + BLAS CLOSEST-HIT TRAVERSAL
//------------------------------------------------------------------------------------------------------------------------

// 📐 Exact Two-Level BVH ray-triangle intersection (Möller–Trumbore 1997) with near-first child ordering.
fn TraceSceneRay(Origin: vec3f, Direction: vec3f, MinimumTravel: f32, MaximumTravel: f32) -> RayHit
{
    var Result : RayHit;
    Result.Position = Origin + Direction * MaximumTravel;
    Result.Travel   = MaximumTravel;
    Result.Normal   = -Direction;
    Result.Geometry = -Direction;
    Result.Identity = SkyIdentity;
    Result.Family   = 0u;
    Result.Steps    = 0u;
    Result.Hit      = false;

    if (BvhNodes[0].LeftOrFirst == EmptyTlasSentinel && BvhNodes[0].Count == 0u)
    {
        return Result;
    }

    let WorldRecip = SafeReciprocal(Direction);
    var Stack      : array<u32, BvhStackCapacity>;
    var Depth      = 1u;
    Stack[0]       = 0u;
    var BlasFloor  = 0xFFFFFFFFu;

    var Nearest    = MaximumTravel;
    var BestTri    = 0xFFFFFFFFu;
    var BestOcc    = 0xFFFFFFFFu;
    var BestUV     = vec2f(0.0);
    var Steps      = 0u;

    var CurrentOcc  = 0xFFFFFFFFu;
    var OccRoles    = 0u;
    var LocalOrigin = Origin;
    var LocalDir    = Direction;
    var LocalRecip  = WorldRecip;

    while (Depth > 0u && Steps < MaxBvhSteps)
    {
        if (Depth <= BlasFloor)
        {
            BlasFloor = 0xFFFFFFFFu;
        }
        Depth -= 1u;
        let NodeIdx = Stack[Depth];
        let Node    = BvhNodes[NodeIdx];
        Steps += 1u;

        if (BlasFloor == 0xFFFFFFFFu)
        {
            if (IntersectNodeBounds(Origin, WorldRecip, Node.Minimum, Node.Maximum, Nearest) >= Nearest)
            {
                continue;
            }
            if ((Node.Count & TlasLeafBit) != 0u)
            {
                CurrentOcc  = Node.Count & TlasIndexMask;
                let Occ     = Occupants[CurrentOcc];
                OccRoles    = Occ.Roles;
                LocalOrigin = (Occ.WorldToLocal * vec4f(Origin, 1.0)).xyz;
                LocalDir    = (Occ.WorldToLocal * vec4f(Direction, 0.0)).xyz;
                LocalRecip  = SafeReciprocal(LocalDir);
                if (Depth < BvhStackCapacity)
                {
                    BlasFloor    = Depth;
                    Stack[Depth] = Node.LeftOrFirst;
                    Depth += 1u;
                }
            }
            else
            {
                let Left  = Node.LeftOrFirst;
                let Right = Left + 1u;
                let LDist = IntersectNodeBounds(Origin, WorldRecip, BvhNodes[Left].Minimum, BvhNodes[Left].Maximum, Nearest);
                let RDist = IntersectNodeBounds(Origin, WorldRecip, BvhNodes[Right].Minimum, BvhNodes[Right].Maximum, Nearest);
                if (LDist < RDist)
                {
                    if (RDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                    if (LDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                }
                else
                {
                    if (LDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                    if (RDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                }
            }
        }
        else
        {
            if (IntersectNodeBounds(LocalOrigin, LocalRecip, Node.Minimum, Node.Maximum, Nearest) >= Nearest)
            {
                continue;
            }
            if (Node.Count > 0u)
            {
                let FirstTri = Node.LeftOrFirst;
                let TriEnd   = FirstTri + Node.Count;
                for (var Tri = FirstTri; Tri < TriEnd; Tri += 1u)
                {
                    let Base      = Tri * 4u;
                    let T0        = BvhTriangles[Base];
                    let RawFamily = bitcast<u32>(T0.w);
                    if (RawFamily == VehicleGlassFamily)
                    {
                        continue;
                    }
                    if (RawFamily == TyreFamily && (OccRoles & RoleShredded) != 0u)
                    {
                        continue;
                    }
                    let A = T0.xyz;
                    let E1 = BvhTriangles[Base + 1u].xyz;
                    let E2 = BvhTriangles[Base + 2u].xyz;
                    let P   = cross(LocalDir, E2);
                    let Det = dot(E1, P);
                    if (abs(Det) < 1.0e-9)
                    {
                        continue;
                    }
                    let InvDet = 1.0 / Det;
                    let T      = LocalOrigin - A;
                    let U      = dot(T, P) * InvDet;
                    if (U < 0.0 || U > 1.0)
                    {
                        continue;
                    }
                    let Q = cross(T, E1);
                    let V = dot(LocalDir, Q) * InvDet;
                    if (V < 0.0 || U + V > 1.0)
                    {
                        continue;
                    }
                    let Travel = dot(E2, Q) * InvDet;
                    if (Travel >= MinimumTravel && Travel < Nearest)
                    {
                        Nearest = Travel;
                        BestTri = Tri;
                        BestOcc = CurrentOcc;
                        BestUV  = vec2f(U, V);
                    }
                }
            }
            else
            {
                let Left  = Node.LeftOrFirst;
                let Right = Left + 1u;
                let LDist = IntersectNodeBounds(LocalOrigin, LocalRecip, BvhNodes[Left].Minimum, BvhNodes[Left].Maximum, Nearest);
                let RDist = IntersectNodeBounds(LocalOrigin, LocalRecip, BvhNodes[Right].Minimum, BvhNodes[Right].Maximum, Nearest);
                if (LDist < RDist)
                {
                    if (RDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                    if (LDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                }
                else
                {
                    if (LDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                    if (RDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                }
            }
        }
    }

    Result.Steps = Steps;

    if (BestTri != 0xFFFFFFFFu)
    {
        let Base = BestTri * 4u;
        let T0   = BvhTriangles[Base];
        let T1   = BvhTriangles[Base + 1u];
        let T2   = BvhTriangles[Base + 2u];
        let T3   = BvhTriangles[Base + 3u];

        let NA = UnpackOctahedral(bitcast<u32>(T1.w));
        let NB = UnpackOctahedral(bitcast<u32>(T2.w));
        let NC = UnpackOctahedral(bitcast<u32>(T3.w));
        let W  = 1.0 - BestUV.x - BestUV.y;
        let LocalShading = normalize(NA * W + NB * BestUV.x + NC * BestUV.y);

        let Occ        = Occupants[BestOcc];
        var WorldNorm  = normalize((Occ.LocalToWorld * vec4f(LocalShading, 0.0)).xyz);
        var WorldGeom = normalize((Occ.LocalToWorld * vec4f(T3.xyz, 0.0)).xyz);
        if (dot(WorldGeom, Direction) > 0.0)
        {
            WorldGeom = -WorldGeom;
        }
        if (dot(WorldNorm, Direction) > 0.0)
        {
            WorldNorm = -WorldNorm;
        }

        let RawFamily   = bitcast<u32>(T0.w);
        Result.Position = Origin + Direction * Nearest;
        Result.Travel   = Nearest;
        Result.Normal   = WorldNorm;
        Result.Geometry = WorldGeom;
        Result.Family   = select(RawFamily, Occ.MaterialOverride, Occ.MaterialOverride != NoMaterialOverride);
        Result.Identity = select(BestOcc, GroundIdentity, BestOcc == GroundOccupantIndex);
        Result.Hit      = true;
        return Result;
    }

    // Far-field horizon ground fallback beyond the authored 220m mesh
    if (Direction.z < -1.0e-4)
    {
        let GroundTravel = (Distance.GroundHeight - Origin.z) / Direction.z;
        if (GroundTravel >= MinimumTravel && GroundTravel <= MaximumTravel)
        {
            Result.Position = Origin + Direction * GroundTravel;
            Result.Travel   = GroundTravel;
            Result.Normal   = vec3f(0.0, 0.0, 1.0);
            Result.Geometry = vec3f(0.0, 0.0, 1.0);
            Result.Identity = GroundIdentity;
            Result.Family   = SurroundFamily;
            Result.Hit      = true;
        }
    }

    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           TLAS + BLAS ANY-HIT VISIBILITY
//------------------------------------------------------------------------------------------------------------------------

// 📐 Exact any-hit shadow ray traversal: exits on the very first triangle intersection in [MinimumTravel, MaximumTravel).
fn TraceSceneVisibility(Origin: vec3f, Direction: vec3f, MinimumTravel: f32, MaximumTravel: f32) -> f32
{
    if (MaximumTravel <= MinimumTravel)
    {
        return 1.0;
    }
    if (BvhNodes[0].LeftOrFirst == EmptyTlasSentinel && BvhNodes[0].Count == 0u)
    {
        return 1.0;
    }

    let WorldRecip = SafeReciprocal(Direction);
    var Stack      : array<u32, BvhStackCapacity>;
    var Depth      = 1u;
    Stack[0]       = 0u;
    var BlasFloor  = 0xFFFFFFFFu;
    var Steps      = 0u;

    var CurrentOcc = 0xffffffffu;
    var OccRoles    = 0u;
    var LocalOrigin = Origin;
    var LocalDir    = Direction;
    var LocalRecip  = WorldRecip;

    while (Depth > 0u && Steps < MaxBvhSteps)
    {
        if (Depth <= BlasFloor)
        {
            BlasFloor = 0xFFFFFFFFu;
        }
        Depth -= 1u;
        let NodeIdx = Stack[Depth];
        let Node    = BvhNodes[NodeIdx];
        Steps += 1u;

        if (BlasFloor == 0xFFFFFFFFu)
        {
            if (IntersectNodeBounds(Origin, WorldRecip, Node.Minimum, Node.Maximum, MaximumTravel) >= MaximumTravel)
            {
                continue;
            }
            if ((Node.Count & TlasLeafBit) != 0u)
            {
                let OccIdx  = Node.Count & TlasIndexMask;
                CurrentOcc = OccIdx;
                let Occ     = Occupants[OccIdx];
                OccRoles    = Occ.Roles;
                LocalOrigin = (Occ.WorldToLocal * vec4f(Origin, 1.0)).xyz;
                LocalDir    = (Occ.WorldToLocal * vec4f(Direction, 0.0)).xyz;
                LocalRecip  = SafeReciprocal(LocalDir);
                if (Depth < BvhStackCapacity)
                {
                    BlasFloor    = Depth;
                    Stack[Depth] = Node.LeftOrFirst;
                    Depth += 1u;
                }
            }
            else
            {
                let Left  = Node.LeftOrFirst;
                let Right = Left + 1u;
                let LDist = IntersectNodeBounds(Origin, WorldRecip, BvhNodes[Left].Minimum, BvhNodes[Left].Maximum, MaximumTravel);
                let RDist = IntersectNodeBounds(Origin, WorldRecip, BvhNodes[Right].Minimum, BvhNodes[Right].Maximum, MaximumTravel);
                if (LDist < RDist)
                {
                    if (RDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                    if (LDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                }
                else
                {
                    if (LDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                    if (RDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                }
            }
        }
        else
        {
            if (IntersectNodeBounds(LocalOrigin, LocalRecip, Node.Minimum, Node.Maximum, MaximumTravel) >= MaximumTravel)
            {
                continue;
            }
            if (Node.Count > 0u)
            {
                let FirstTri = Node.LeftOrFirst;
                let TriEnd   = FirstTri + Node.Count;
                for (var Tri = FirstTri; Tri < TriEnd; Tri += 1u)
                {
                    let Base      = Tri * 4u;
                    let T0        = BvhTriangles[Base];
                    let RawFamily = bitcast<u32>(T0.w);
                    if (RawFamily == VehicleGlassFamily)
                    {
                        continue;
                    }
                    if (RawFamily == TyreFamily && (OccRoles & RoleShredded) != 0u)
                    {
                        continue;
                    }
                    let A = T0.xyz;
                    let E1 = BvhTriangles[Base + 1u].xyz;
                    let E2 = BvhTriangles[Base + 2u].xyz;
                    let P   = cross(LocalDir, E2);
                    let Det = dot(E1, P);
                    if (abs(Det) < 1.0e-9)
                    {
                        continue;
                    }
                    let InvDet = 1.0 / Det;
                    let T      = LocalOrigin - A;
                    let U      = dot(T, P) * InvDet;
                    if (U < 0.0 || U > 1.0)
                    {
                        continue;
                    }
                    let Q = cross(T, E1);
                    let V = dot(LocalDir, Q) * InvDet;
                    if (V < 0.0 || U + V > 1.0)
                    {
                        continue;
                    }
                    let Travel = dot(E2, Q) * InvDet;
                    if (Travel >= MinimumTravel && Travel < MaximumTravel)
                    {
                        return 0.0;
                    }
                }
            }
            else
            {
                let Left  = Node.LeftOrFirst;
                let Right = Left + 1u;
                let LDist = IntersectNodeBounds(LocalOrigin, LocalRecip, BvhNodes[Left].Minimum, BvhNodes[Left].Maximum, MaximumTravel);
                let RDist = IntersectNodeBounds(LocalOrigin, LocalRecip, BvhNodes[Right].Minimum, BvhNodes[Right].Maximum, MaximumTravel);
                if (LDist < RDist)
                {
                    if (RDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                    if (LDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                }
                else
                {
                    if (LDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                    if (RDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                }
            }
        }
    }

    return 1.0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                     SOLAR CONE, RIS LIGHT NEE & BSDF SAMPLING
//------------------------------------------------------------------------------------------------------------------------

// 📐 Jitters a solar ray within the sun's angular radius (~0.27°) for physically accurate soft penumbrae.
fn SampleSunConeDirection(SunDirection: vec3f, Pair: vec2f) -> vec3f
{
    let Disk  = ProjectUniformDisk(Pair) * SolarAngularRadius;
    let Basis = ConstructTangentFrame(SunDirection);
    return normalize(SunDirection + Basis[0] * Disk.x + Basis[1] * Disk.y);
}

// 📐 Heitz 2018, "Sampling the GGX Distribution of Visible Normals"; view in tangent space, Alpha = Roughness².
fn ProjectVisibleNormal(View: vec3f, Alpha: f32, Pair: vec2f) -> vec3f
{
    let Stretched = normalize(vec3f(Alpha * View.x, Alpha * View.y, max(View.z, 1.0e-4)));
    let LengthSquared = Stretched.x * Stretched.x + Stretched.y * Stretched.y;
    let T1 = select(vec3f(1.0, 0.0, 0.0), vec3f(-Stretched.y, Stretched.x, 0.0) * inverseSqrt(LengthSquared), LengthSquared > 0.0);
    let T2 = cross(Stretched, T1);
    let Radius = sqrt(Pair.x);
    let Phi = 2.0 * Pi * Pair.y;
    let P1 = Radius * cos(Phi);
    let Interpolant = 0.5 * (1.0 + Stretched.z);
    let P2 = mix(sqrt(max(0.0, 1.0 - P1 * P1)), Radius * sin(Phi), Interpolant);
    let Hemisphere = P1 * T1 + P2 * T2 + sqrt(max(0.0, 1.0 - P1 * P1 - P2 * P2)) * Stretched;
    return normalize(vec3f(Alpha * Hemisphere.x, Alpha * Hemisphere.y, max(1.0e-4, Hemisphere.z)));
}

// 📐 GGX VNDF reflection PDF (Heitz 2018): D_v(H) / (4 · (V·H)) = D(H) · G1(V) / (4 · (N·V)).
fn EvaluateGgxVndfPdf(NoV: f32, NoH: f32, VoH: f32, Roughness: f32) -> f32
{
    let Alpha  = max(Roughness * Roughness, 1.0e-3);
    let Alpha2 = Alpha * Alpha;
    let D      = ResolveGgxDistribution(NoH, Roughness);
    let G1     = 2.0 * NoV / max(NoV + sqrt(Alpha2 + (1.0 - Alpha2) * NoV * NoV), 1.0e-4);
    return (D * G1) / max(4.0 * NoV, 1.0e-4);
}

// 📐 Two-layer PBR BSDF importance sampling (GGX VNDF specular/clearcoat lobe + cosine diffuse lobe) with exact mixture PDF.
fn SampleSurfaceBsdf(
    Surface       : SurfaceSample,
    Normal        : vec3f,
    View          : vec3f,
    Seed          : ptr<function, u32>,
    OutDirection  : ptr<function, vec3f>,
    OutThroughput : ptr<function, vec3f>,
    OutPdf        : ptr<function, f32>
) -> bool
{
    let NoV = max(dot(Normal, View), 1.0e-4);
    let F0  = ResolveSpecularColour(Surface);
    let FresnelLum = ResolveLuminance(ResolveFresnel(F0, NoV)) + Surface.CoatWeight * 0.04;
    let DiffuseLum = ResolveLuminance(ResolveDiffuseAlbedo(Surface));

    var SpecProb = FresnelLum / max(FresnelLum + DiffuseLum, 1.0e-4);
    SpecProb = select(clamp(SpecProb, 0.15, 0.85), 0.92, Surface.Metalness > 0.9 && Surface.Roughness < 0.25);

    let EffectiveRough = clamp(
        mix(Surface.Roughness, Surface.CoatRoughness, Surface.CoatWeight * 0.5),
        0.04,
        1.0
    );

    let Basis    = ConstructTangentFrame(Normal);
    let Choice   = AdvanceUniform(Seed);
    let Pair     = vec2f(AdvanceUniform(Seed), AdvanceUniform(Seed));
    var LightDir = Normal;

    if (Choice < SpecProb)
    {
        let ViewLocal = vec3f(dot(View, Basis[0]), dot(View, Basis[1]), max(dot(View, Basis[2]), 1.0e-3));
        let HalfLocal = ProjectVisibleNormal(ViewLocal, EffectiveRough * EffectiveRough, Pair);
        let HalfWorld = normalize(Basis * HalfLocal);
        LightDir      = reflect(-View, HalfWorld);
        if (dot(Normal, LightDir) <= 1.0e-4)
        {
            LightDir = normalize(Basis * ProjectCosineHemisphere(Pair));
        }
    }
    else
    {
        LightDir = normalize(Basis * ProjectCosineHemisphere(Pair));
    }

    let NoL = dot(Normal, LightDir);
    if (NoL <= 1.0e-4)
    {
        return false;
    }

    let Half    = normalize(View + LightDir);
    let NoH     = saturate(dot(Normal, Half));
    let VoH     = max(dot(View, Half), 1.0e-4);
    let DiffPdf = NoL * InversePi;
    let SpecPdf = EvaluateGgxVndfPdf(NoV, NoH, VoH, EffectiveRough);
    let MixPdf  = max(mix(DiffPdf, SpecPdf, SpecProb), 1.0e-4);

    let Brdf = ResolveDirectReflectance(Surface, Normal, View, LightDir);
    *OutDirection  = LightDir;
    *OutThroughput = min(Brdf / MixPdf, vec3f(8.0));
    *OutPdf        = MixPdf;
    return true;
}

// 📐 Next-Event Estimation (NEE) at a path vertex:
//    1. Soft solar cone NEE with exact BVH shadow ray + MIS weight
//    2. Active tyre fire point light NEE
//    3. Resampled Importance Sampling (RIS) over all 32 clustered photometric / IES lights with 1 exact BVH shadow ray
fn EvaluateDirectNextEvent(
    Position     : vec3f,
    Normal       : vec3f,
    View         : vec3f,
    Surface      : SurfaceSample,
    Seed         : ptr<function, u32>,
    TraceShadows : bool
) -> vec3f
{
    var Direct = vec3f(0.0);
    let Origin = Position + Normal * RayEpsilon;

    // 1. Direct solar radiance. ResolveDirectReflectance already contains cosine and inverse pi.
    let Pair   = vec2f(AdvanceUniform(Seed), AdvanceUniform(Seed));
    let SunDir = SampleSunConeDirection(Frame.SunDirection, Pair);
    if (dot(Normal, SunDir) > 0.0)
    {
        let SunBrdf = ResolveDirectReflectance(Surface, Normal, View, SunDir);
        if (any(SunBrdf > vec3f(0.0)))
        {
            var SunVis = 1.0;
            if (TraceShadows && FeatureEnabled(Frame, FeatureDirectShadow))
            {
                SunVis = TraceSceneVisibility(Origin, SunDir, RayEpsilon, 400.0);
            }
            Direct += Frame.SunIlluminance * SunVis * SunBrdf;
        }
    }

    // 2. Tyre Fire NEE
    let Fire = ResolveFireArrival(Position, Normal);
    if (any(Fire.Illuminance > vec3f(0.0)))
    {
        Direct += Fire.Illuminance * ResolveDirectReflectance(Surface, Normal, View, Fire.Direction);
    }

    // 3. Inline RIS over Clustered Photometric / IES Lights (street lamps, headlights, fog beams, roof floodlights)
    var SelectedContrib = vec3f(0.0);
    var SelectedDir     = vec3f(0.0, 0.0, 1.0);
    var SelectedDist    = 0.0;
    var SelectedWeight  = 0.0;
    var WeightSum       = 0.0;

    let LightMask = SelectLightMask(Position, &RayLights);
    for (var LightIdx = 0u; LightIdx < 32u; LightIdx += 1u)
    {
        if ((LightMask & (1u << LightIdx)) == 0u) { continue; }
        let Light = RayLights.Lights[LightIdx];
        if (Light.Params.w < 0.5)
        {
            break;
        }
        let Arrival = EvaluateLightArrival(Light, Position, Normal, &RayLights);
        if (all(Arrival <= vec3f(1.0e-5)))
        {
            continue;
        }
        let Toward  = Light.PositionType.xyz - Position;
        let Dist    = length(Toward);
        let LDir    = Toward / max(Dist, 1.0e-4);
        let Contrib = Arrival / max(dot(Normal, LDir), 1.0e-4) * ResolveDirectReflectance(Surface, Normal, View, LDir);
        let Weight  = ResolveLuminance(Contrib);
        if (Weight <= 1.0e-5)
        {
            continue;
        }
        WeightSum += Weight;
        if (AdvanceUniform(Seed) * WeightSum < Weight)
        {
            SelectedContrib = Contrib;
            SelectedDir     = LDir;
            SelectedDist    = Dist;
            SelectedWeight  = Weight;
        }
    }

    if (SelectedWeight > 0.0)
    {
        var LightVis = 1.0;
        if (TraceShadows)
        {
            LightVis = TraceSceneVisibility(Origin, SelectedDir, RayEpsilon, max(SelectedDist - 0.08, RayEpsilon));
        }
        let RisWeight = WeightSum / SelectedWeight;
        Direct += SelectedContrib * LightVis * RisWeight;
    }

    return Direct;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           MULTI-BOUNCE PATH TRACER
//------------------------------------------------------------------------------------------------------------------------

// 📐 Traces a full multi-bounce path starting along (Origin, FirstDirection), returning the first bounce surface vertex
//    x_s and the complete multi-bounce incoming radiance L_s gathered along the path for ReSTIR GI resampling.
fn TraceMultiBouncePath(Origin: vec3f, FirstDirection: vec3f, Seed: ptr<function, u32>) -> ReservoirSample
{
    let Hit1 = TraceSceneRay(Origin, FirstDirection, RayEpsilon, 500.0);
    if (!Hit1.Hit)
    {
        var SkySample : ReservoirSample;
        SkySample.Position = Origin + FirstDirection * SkyReach;
        SkySample.Normal   = -FirstDirection;
        SkySample.Radiance = SampleSkyRadiance(FirstDirection);
        SkySample.Identity = SkyIdentity;
        return SkySample;
    }

    let Mat1  = RayMaterials[Hit1.Family];
    let Surf1 = ProjectSurfaceSample(Mat1);

    // Bounce 1 direct emission + Next-Event Estimation
    var TotalRadiance = Surf1.Emission * Frame.EmissionScale;
    TotalRadiance += EvaluateDirectNextEvent(Hit1.Position, Hit1.Normal, -FirstDirection, Surf1, Seed, true);

    let MaxBounces     = ResolveMaxBounces(Frame);
    var PathThroughput = vec3f(1.0);
    var CurrPos        = Hit1.Position;
    var CurrNormal     = Hit1.Normal;
    var CurrView       = -FirstDirection;
    var CurrSurf       = Surf1;
    var CurrIdentity   = Hit1.Identity;
    var EscapedToSky   = false;
    var Terminated = false;

    // Secondary & higher-order path bounces (bounce 2 .. MaxBounces)
    for (var Bounce = 2u; Bounce <= MaxBounces; Bounce += 1u)
    {
        var NextDir    = CurrNormal;
        var BounceThru = vec3f(0.0);
        var BsdfPdf    = 1.0;
        if (!SampleSurfaceBsdf(CurrSurf, CurrNormal, CurrView, Seed, &NextDir, &BounceThru, &BsdfPdf))
        {
            Terminated = true;
            break;
        }
        PathThroughput *= BounceThru;

        // Russian Roulette termination
        if (FeatureEnabled(Frame, FeatureRussianRoulette) && Bounce >= 2u)
        {
            let Survival = clamp(max(PathThroughput.r, max(PathThroughput.g, PathThroughput.b)), 0.12, 0.95);
            if (AdvanceUniform(Seed) > Survival)
            {
                Terminated = true;
                break;
            }
            PathThroughput /= Survival;
        }

        let HitB = TraceSceneRay(CurrPos + CurrNormal * RayEpsilon, NextDir, RayEpsilon, 350.0);
        if (!HitB.Hit)
        {
            TotalRadiance += PathThroughput * SampleSkyRadiance(NextDir);
            EscapedToSky = true;
            break;
        }

        let MatB  = RayMaterials[HitB.Family];
        let SurfB = ProjectSurfaceSample(MatB);

        // Emissive surfaces are sampled only by BSDF continuation, not the point-light NEE distribution.
        TotalRadiance += PathThroughput * SurfB.Emission * Frame.EmissionScale;

        if (FeatureEnabled(Frame, FeatureMultiBounceNEE))
        {
            let DirectB = EvaluateDirectNextEvent(HitB.Position, HitB.Normal, -NextDir, SurfB, Seed, Bounce <= 2u);
            TotalRadiance += PathThroughput * DirectB;
        }

        CurrPos      = HitB.Position;
        CurrNormal   = HitB.Normal;
        CurrView     = -NextDir;
        CurrSurf     = SurfB;
        CurrIdentity = HitB.Identity;
    }

    // Infinite-bounce radiosity cache tail beyond MaxBounces
    if (!EscapedToSky && !Terminated && FeatureEnabled(Frame, FeatureCardRadiosity))
    {
        let TailClip = ResolveFinestClip(CurrPos, 1.0);
        let CardTail = SampleCardRadiance(CurrPos, CurrNormal, CurrIdentity, ResolveCardTolerance(TailClip));
        if (CardTail.w > 0.0)
        {
            TotalRadiance += PathThroughput * ResolveDiffuseAlbedo(CurrSurf) * InversePi * CardTail.rgb * 0.35;
        }
    }

    // Firefly & boiling suppression clamp on individual path samples
    if (FeatureEnabled(Frame, FeatureFireflyFilter))
    {
        let Lum = ResolveLuminance(TotalRadiance);
        let MaxLum = 24.0;
        if (Lum > MaxLum)
        {
            TotalRadiance *= MaxLum / Lum;
        }
    }

    var OutSample : ReservoirSample;
    OutSample.Position = Hit1.Position;
    OutSample.Normal   = Hit1.Normal;
    OutSample.Radiance = max(TotalRadiance, vec3f(0.0));
    OutSample.Identity = Hit1.Identity;
    return OutSample;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      MULTI-BOUNCE RAY-TRACED REFLECTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📐 Traces a chained multi-bounce specular / GGX-VNDF reflection path up to ResolveReflectionBounces(Frame) bounces
//    through the TLAS/BLAS triangle hierarchy, shading each hit with emission, direct NEE (Sun + Fire + RIS IES lights
//    with BVH shadow rays), indirect diffuse lighting, and recursive Fresnel reflection continuation.
fn TraceMultiBounceReflection(
    Origin           : vec3f,
    FirstDirection   : vec3f,
    PrimaryRoughness : f32,
    Seed             : ptr<function, u32>
) -> vec3f
{
    let MaxReflBounces   = ResolveReflectionBounces(Frame);
    var TotalRadiance    = vec3f(0.0);
    var Throughput       = vec3f(1.0);
    var RayOrigin        = Origin;
    var RayDir           = FirstDirection;
    var CurrentRoughness = PrimaryRoughness;

    for (var Bounce = 1u; Bounce <= MaxReflBounces; Bounce += 1u)
    {
        let Hit = TraceSceneRay(RayOrigin, RayDir, RayEpsilon, 500.0);
        if (!Hit.Hit)
        {
            var SkyRad = SampleSkyRadiance(RayDir);
            let SunCos = dot(RayDir, Frame.SunDirection);
            if (CurrentRoughness < 0.22 && SunCos > 0.9994)
            {
                SkyRad += Frame.SunIlluminance * smoothstep(0.9994, 0.99995, SunCos) * 45.0;
            }
            TotalRadiance += Throughput * SkyRad;
            break;
        }

        let HitSurf     = ProjectSurfaceSample(RayMaterials[Hit.Family]);
        let ViewDir     = -RayDir;
        var HitRadiance = HitSurf.Emission * Frame.EmissionScale;

        // Direct Next-Event Estimation (Sun cone + Fire + RIS over 32 IES lights with exact BVH shadow rays)
        HitRadiance += EvaluateDirectNextEvent(Hit.Position, Hit.Normal, ViewDir, HitSurf, Seed, Bounce <= 2u) * InversePi;

        // Indirect diffuse illumination on the reflected surface so shadowed faces retain natural colour and depth
        let DiffuseAlbedo = ResolveDiffuseAlbedo(HitSurf);
        if (any(DiffuseAlbedo > vec3f(1.0e-3)))
        {
            var AmbientIrr = SampleSkyRadiance(Hit.Normal) * (0.28 + 0.28 * saturate(Hit.Normal.z));
            if (FeatureEnabled(Frame, FeatureCardRadiosity))
            {
                let Clip = ResolveFinestClip(Hit.Position, 1.0);
                let Card = SampleCardRadiance(Hit.Position, Hit.Normal, Hit.Identity, ResolveCardTolerance(Clip));
                if (Card.w > 0.0)
                {
                    AmbientIrr = max(AmbientIrr, Card.rgb * InversePi);
                }
            }
            HitRadiance += DiffuseAlbedo * AmbientIrr;
        }

        TotalRadiance += Throughput * HitRadiance;

        if (Bounce >= MaxReflBounces)
        {
            break;
        }

        // Recursive specular / clearcoat Fresnel continuation for reflection bounce b + 1
        let NoV          = max(dot(Hit.Normal, ViewDir), 1.0e-4);
        let HitRoughness = select(HitSurf.Roughness, HitSurf.CoatRoughness, HitSurf.CoatWeight > 0.0);
        let F0           = ResolveSpecularColour(HitSurf);
        var SpecWeight   = ResolveFresnel(F0, NoV) * (1.0 - 0.55 * HitSurf.Roughness);
        if (HitSurf.CoatWeight > 0.0)
        {
            let CoatF  = ResolveFresnel(vec3f(0.04), NoV).x * HitSurf.CoatWeight * (1.0 - 0.35 * HitSurf.CoatRoughness);
            SpecWeight = SpecWeight * (1.0 - CoatF) + vec3f(CoatF);
        }
        Throughput *= SpecWeight;
        if (max(Throughput.r, max(Throughput.g, Throughput.b)) < 0.015)
        {
            break;
        }

        let Basis         = ConstructTangentFrame(Hit.Normal);
        let TangentView   = transpose(Basis) * ViewDir;
        let Pair          = vec2f(AdvanceUniform(Seed), AdvanceUniform(Seed));
        let CombinedRough = clamp(sqrt(CurrentRoughness * CurrentRoughness + HitRoughness * HitRoughness), 0.03, 1.0);
        let Half          = Basis * ProjectVisibleNormal(TangentView, CombinedRough * CombinedRough, Pair);
        var NextDir       = reflect(-ViewDir, Half);
        if (dot(NextDir, Hit.Normal) <= 1.0e-4)
        {
            NextDir = reflect(-ViewDir, Hit.Normal);
        }
        if (dot(NextDir, Hit.Normal) <= 1.0e-4)
        {
            break;
        }
        RayOrigin        = Hit.Position + Hit.Normal * RayEpsilon;
        RayDir           = NextDir;
        CurrentRoughness = CombinedRough;
    }

    return min(TotalRadiance, vec3f(200.0));
}
