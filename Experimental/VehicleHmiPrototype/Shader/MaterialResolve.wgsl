//============================================================================================================================================
//                                                          MATERIALRESOLVE.WGSL
//============================================================================================================================================
// 🧩 Phase-3 deferred resolve: visibility identity + barycentrics -> world position / UV / normal -> material.
//    A second entry refracts glass pixels through their front (entry) and peeled back (exit) layers and
//    gathers the resolved opaque image, with Beer absorption and Schlick Fresnel sky reflection.

//! include "FrameDescriptor.wgsl"

@group(0) @binding(1) var<storage, read> Occupants  : array<OccupantRecord>;
@group(0) @binding(2) var<storage, read> CornerPos  : array<vec4f>;   // xyz local position, w = family bitcast to f32
@group(0) @binding(3) var<storage, read> CornerData : array<vec4f>;   // xy UV, zw unused
@group(0) @binding(4) var OpaqueVisTex              : texture_2d<u32>;
@group(0) @binding(5) var OpaqueBaryTex             : texture_2d<f32>;
@group(0) @binding(6) var OpaqueResolved            : texture_storage_2d<rgba16float, write>;
@group(0) @binding(7) var GlassVisTex               : texture_2d<u32>;
@group(0) @binding(8) var GlassBaryTex              : texture_2d<f32>;
@group(0) @binding(9) var GlassBackVisTex           : texture_2d<u32>;
@group(0) @binding(10) var GlassBackBaryTex         : texture_2d<f32>;
@group(0) @binding(11) var FinalResolved            : texture_storage_2d<rgba16float, write>;
@group(0) @binding(12) var OpaqueResolvedRead       : texture_2d<f32>;

const SkyColour = vec3f(0.36, 0.48, 0.66);
const LightDirection = vec3f(0.375, 0.625, 0.5);
const GlassEta = 1.52;

fn FamilyColour(Family : u32) -> vec3f
{
    switch (Family % 8u)
    {
        case 0u: { return vec3f(0.62, 0.65, 0.70); }
        case 1u: { return vec3f(0.55, 0.48, 0.38); }
        case 2u: { return vec3f(0.72, 0.74, 0.78); }
        case 3u: { return vec3f(0.30, 0.34, 0.40); }
        case 4u: { return vec3f(0.66, 0.30, 0.24); }
        case 5u: { return vec3f(0.24, 0.44, 0.62); }
        case 6u: { return vec3f(0.28, 0.56, 0.36); }
        default: { return vec3f(0.70, 0.58, 0.26); }
    }
}

struct SurfacePoint
{
    Valid   : bool,
    World   : vec3f,
    Uv      : vec2f,
    Normal  : vec3f,
    Family  : u32,
}

fn ResolvePoint(VisTex : texture_2d<u32>, BaryTex : texture_2d<f32>, Coord : vec2i) -> SurfacePoint
{
    var Point : SurfacePoint;
    let Id = textureLoad(VisTex, Coord, 0);
    if (Id.x == 0u) { return Point; }
    let TriangleFirst = Id.x - 1u;
    let Occupant = Occupants[Id.y];
    let Bary = textureLoad(BaryTex, Coord, 0).xy;
    let Weights = vec3f(1.0 - Bary.x - Bary.y, Bary.x, Bary.y);
    var World = vec3f(0.0);
    var Uv = vec2f(0.0);
    var Corners = array<vec3f, 3>();
    var Family = 0u;
    for (var C = 0u; C < 3u; C = C + 1u)
    {
        let Corner = CornerPos[TriangleFirst + C];
        Corners[C] = (Occupant.LocalToWorld * vec4f(Corner.xyz, 1.0)).xyz;
        World = World + Corners[C] * Weights[C];
        Uv = Uv + CornerData[TriangleFirst + C].xy * Weights[C];
        Family = bitcast<u32>(Corner.w);
    }
    Point.Valid = true;
    Point.World = World;
    Point.Uv = Uv;
    Point.Normal = normalize(cross(Corners[1] - Corners[0], Corners[2] - Corners[0]));
    Point.Family = Family;
    return Point;
}

fn ShadeOpaque(Point : SurfacePoint) -> vec3f
{
    var Colour = FamilyColour(Point.Family);
    if (any(Point.Uv != vec2f(0.0)))
    {
        let Checker = select(0.45, 1.0, (i32(floor(Point.Uv.x * 24.0)) + i32(floor(Point.Uv.y * 24.0))) % 2 == 0);
        Colour = Colour * Checker;
    }
    let Diffuse = max(dot(Point.Normal, normalize(LightDirection)), 0.0);
    return Colour * (0.32 + Diffuse);
}

@compute @workgroup_size(8, 8)
fn ResolveOpaqueVisibility(@builtin(global_invocation_id) ID : vec3u)
{
    let Extent = textureDimensions(OpaqueResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = vec2i(ID.xy);
    let Point = ResolvePoint(OpaqueVisTex, OpaqueBaryTex, Coord);
    let Colour = select(SkyColour, ShadeOpaque(Point), Point.Valid);
    textureStore(OpaqueResolved, Coord, vec4f(Colour, 1.0));
}

fn RefractDirection(ViewDir : vec3f, Normal : vec3f, EtaRatio : f32) -> vec3f
{
    let CosI = -dot(ViewDir, Normal);
    let K = 1.0 - EtaRatio * EtaRatio * (1.0 - CosI * CosI);
    if (K < 0.0) { return vec3f(0.0); }
    return EtaRatio * ViewDir + (EtaRatio * CosI - sqrt(K)) * Normal;
}

@compute @workgroup_size(8, 8)
fn ResolveRefractionGather(@builtin(global_invocation_id) ID : vec3u)
{
    let Extent = textureDimensions(FinalResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = vec2i(ID.xy);
    var Colour = textureLoad(OpaqueResolvedRead, Coord, 0).xyz;
    let Front = ResolvePoint(GlassVisTex, GlassBaryTex, Coord);
    if (Front.Valid)
    {
        let Back = ResolvePoint(GlassBackVisTex, GlassBackBaryTex, Coord);
        var ViewDir = Front.World - Frame.CameraPosition;
        let ViewLength = length(ViewDir);
        ViewDir = ViewDir / max(ViewLength, 1.0e-6);
        var Normal = Front.Normal;
        if (dot(Normal, ViewDir) > 0.0) { Normal = -Normal; }
        let Inside = RefractDirection(ViewDir, Normal, 1.0 / GlassEta);
        var Gathered = SkyColour;
        var Thickness = 0.25;
        if (any(Inside != vec3f(0.0)))
        {
            var ExitPoint = Front.World + Inside * Thickness;
            var Outside = Inside;
            if (Back.Valid)
            {
                Thickness = length(Back.World - Front.World);
                ExitPoint = Back.World;
                var ExitNormal = Back.Normal;
                if (dot(ExitNormal, Inside) < 0.0) { ExitNormal = -ExitNormal; }
                let Outgoing = RefractDirection(Inside, -ExitNormal, GlassEta);
                if (any(Outgoing != vec3f(0.0))) { Outside = Outgoing; }
            }
            let SampleWorld = ExitPoint + Outside * 2.0;
            let Clip = Frame.UnjitteredViewProjection * vec4f(SampleWorld, 1.0);
            if (Clip.w > 1.0e-4)
            {
                let Ndc = Clip.xy / Clip.w;
                let SampleCoord = vec2i(vec2f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5) * vec2f(Extent));
                if (all(SampleCoord >= vec2i(0)) && all(SampleCoord < vec2i(Extent)))
                {
                    Gathered = textureLoad(OpaqueResolvedRead, SampleCoord, 0).xyz;
                }
            }
        }
        let Absorb = exp(vec3f(-0.22, -0.10, -0.16) * Thickness);
        let CosView = clamp(abs(dot(Normal, ViewDir)), 0.0, 1.0);
        let F0 = pow((GlassEta - 1.0) / (GlassEta + 1.0), 2.0);
        let Fresnel = F0 + (1.0 - F0) * pow(1.0 - CosView, 5.0);
        Colour = Gathered * Absorb * (1.0 - Fresnel) + SkyColour * Fresnel;
    }
    textureStore(FinalResolved, Coord, vec4f(Colour, 1.0));
}
