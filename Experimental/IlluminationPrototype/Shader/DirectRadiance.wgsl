//============================================================================================================================================
//                                                           DIRECTRADIANCE.WGSL
//============================================================================================================================================
// 🧩 Direct sun radiance and clustered lights at every primary surface, cascaded shadow depth, and sky background.

//! include "FrameDescriptor.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "PackingCodec.wgsl"
//! include "SkyProjection.wgsl"
//! include "FireIllumination.wgsl"
//! include "ClusteredLights.wgsl"
//! include "RayTraversal.wgsl"

struct CascadeReceiver
{
    LightViewProjection : array<mat4x4f, 3>,
    Splits              : vec4f,   // [m]   xyz far distance of each cascade, w cascade count
    TexelWorld          : vec4f,   // [m]   world size of one shadow texel per cascade
}

@group(0) @binding(1) var<storage, read> Materials     : array<MaterialRecord>;
@group(0) @binding(2) var SurfaceAttributes            : texture_2d<f32>;
@group(0) @binding(3) var SurfaceDepth                 : texture_depth_2d;
@group(0) @binding(4) var ShadowDepth                  : texture_depth_2d_array;
@group(0) @binding(5) var ShadowSampler                : sampler_comparison;
@group(0) @binding(6) var<uniform> Receiver            : CascadeReceiver;
@group(0) @binding(7) var DirectSkyView                : texture_2d<f32>;
@group(0) @binding(8) var DirectSkySampler             : sampler;
@group(0) @binding(9) var DirectTarget                 : texture_storage_2d<rgba16float, write>;
@group(0) @binding(10) var<uniform> SceneLights  : LightTable;
@group(0) @binding(11) var DiffuseDirectTarget : texture_storage_2d<rgba16float, write>;

// 📐 3×3 percentage-closer taps, each a hardware bilinear comparison, after a normal offset of 1.5 texels.
fn ResolveCascadeVisibility(Position: vec3f, Normal: vec3f) -> f32
{
    let ViewDistance = ResolveViewDepth(Position);
    let Count = u32(Receiver.Splits.w);
    var Cascade = 0u;
    while (Cascade + 1u < Count && ViewDistance > Receiver.Splits[Cascade])
    {
        Cascade++;
    }
    if (ViewDistance > Receiver.Splits[Count - 1u])
    {
        return 1.0;
    }
    let Offset = Position + Normal * (1.5 * Receiver.TexelWorld[Cascade]);
    let Clip = Receiver.LightViewProjection[Cascade] * vec4f(Offset, 1.0);
    let Ndc = Clip.xyz / Clip.w;
    let Coordinates = vec2f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5);
    if (any(Coordinates < vec2f(0.0)) || any(Coordinates > vec2f(1.0)) || Ndc.z > 1.0)
    {
        return 1.0;
    }
    let Texel = 1.0 / vec2f(textureDimensions(ShadowDepth).xy);
    var Lit = 0.0;
    for (var Y = -1; Y <= 1; Y++)
    {
        for (var X = -1; X <= 1; X++)
        {
            Lit += textureSampleCompareLevel(ShadowDepth, ShadowSampler, Coordinates + vec2f(f32(X), f32(Y)) * Texel, Cascade, Ndc.z);
        }
    }
    return Lit / 9.0;
}

fn ResolveBackgroundRadiance(Direction: vec3f) -> vec3f
{
    var Radiance = textureSampleLevel(DirectSkyView, DirectSkySampler, ProjectSkyCoordinates(Direction), 0.0).rgb * Frame.SkyScale
                 + Frame.NightSkyRadiance;
    // 📝 The solar disc (0.27° radius) is added only here, never to escaped rays, so it is not counted twice.
    let Cosine = dot(Direction, Frame.SunDirection);
    if (Cosine > 0.99998)
    {
        Radiance += Frame.SunIlluminance * 2500.0;
    }
    return Radiance;
}

@compute @workgroup_size(8, 8)
fn IntegrateDirectRadiance(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Pixel = Invocation.xy;
    if (any(vec2f(Pixel) >= Frame.ScreenExtent))
    {
        return;
    }
    if (FeatureEnabled(Frame, FeatureScreenTraversal)) { textureStore(DiffuseDirectTarget, vec2i(Pixel), vec4f(0.0)); }
    let Depth = textureLoad(SurfaceDepth, vec2i(Pixel), 0);
    let Centre = vec2f(Pixel) + 0.5;
    if (Depth <= 0.0)
    {
        let Far = ReconstructWorldPosition(Centre, 1.0e-6);
        textureStore(DirectTarget, vec2i(Pixel), vec4f(ResolveBackgroundRadiance(normalize(Far - Frame.CameraPosition)), 0.0));
        return;
    }
    let Position     = ReconstructWorldPosition(Centre, Depth);
    let Attributes   = textureLoad(SurfaceAttributes, vec2i(Pixel), 0);
    let Normal       = DecodeOctahedral(Attributes.xy);
    let Surface      = ProjectSurfaceSample(Materials[u32(Attributes.z + 0.5)]);
    let View         = normalize(Frame.CameraPosition - Position);
    let UseRtShadows = FeatureEnabled(Frame, FeatureRayTracing) && FeatureEnabled(Frame, FeatureRayTracedShadows);
    let RayOrigin    = Position + Normal * RayEpsilon;

    var Visibility = 1.0;
    var SunDir     = Frame.SunDirection;
    if (FeatureEnabled(Frame, FeatureDirectShadow))
    {
        if (UseRtShadows)
        {
            SunDir     = SampleSunConeDirection(Frame.SunDirection, ProjectTemporalPair(Pixel, Frame.FrameIndex, 17u));
            Visibility = TraceSceneVisibility(RayOrigin, SunDir, RayEpsilon, 400.0);
        }
        else
        {
            Visibility = ResolveCascadeVisibility(Position, Normal);
        }
    }
    var Radiance = Frame.SunIlluminance * Visibility * ResolveDirectReflectance(Surface, Normal, View, SunDir);
    let DiffuseAlbedo = ResolveDiffuseAlbedo(Surface) * InversePi;
    var DiffuseArrival = Frame.SunIlluminance * Visibility * max(dot(Normal, SunDir), 0.0);
    let Fire = ResolveFireArrival(Position, Normal);
    if (any(Fire.Illuminance > vec3f(0.0)))
    {
        DiffuseArrival += Fire.Illuminance * max(dot(Normal, Fire.Direction), 0.0);
        Radiance += Fire.Illuminance * ResolveDirectReflectance(Surface, Normal, View, Fire.Direction);
    }

    // Clustered light evaluations with photometric / IES profiles and optional BVH ray-traced shadows
    let NumLights = 32u;
    let LightMask = SelectLightMask(Position, &SceneLights);
    for (var i = 0u; i < NumLights; i++)
    {
        if ((LightMask & (1u << i)) == 0u) { continue; }
        let Light = SceneLights.Lights[i];
        if (Light.Params.w < 0.5)
        {
            break;
        }
        let LightIllum = EvaluateLightArrival(Light, Position, Normal, &SceneLights);
        if (any(LightIllum > vec3f(0.0)))
        {
            let Offset      = Light.PositionType.xyz - Position;
            let LightDist   = length(Offset);
            let TowardLight = Offset / max(LightDist, 1.0e-4);
            var LightVis    = 1.0;
            if (UseRtShadows)
            {
                LightVis = TraceSceneVisibility(RayOrigin, TowardLight, RayEpsilon, max(LightDist - 0.08, RayEpsilon));
            }
            DiffuseArrival += LightIllum * LightVis;
            Radiance += LightIllum / max(dot(Normal, TowardLight), 1.0e-4) * LightVis * ResolveDirectReflectance(Surface, Normal, View, TowardLight);
        }
    }

    textureStore(DirectTarget, vec2i(Pixel), vec4f(Radiance, 1.0));
    if (FeatureEnabled(Frame, FeatureScreenTraversal)) {
        // View-independent diffuse exitance only: specular peaks must never feed back as diffuse GI.
        textureStore(DiffuseDirectTarget, vec2i(Pixel), vec4f(DiffuseAlbedo * DiffuseArrival, 1.0));
    }
}
