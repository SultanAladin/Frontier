//============================================================================================================================================
//                                                         REFLECTIONTRAVERSAL.WGSL
//============================================================================================================================================
// 🧩 Glossy reflection rays: a GGX visible-normal sample per resampling pixel, traced through the scene distance field.

// 📝 The specular counterpart of the indirect candidate: the reflected ray reads card radiance where it lands and the
//    sky where it escapes. Surfaces rougher than RoughnessCutoff are left to the resolve, which substitutes the
//    diffuse irradiance there exactly as Lumen reuses its diffuse gather for rough reflections.

//! include "CardSampling.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "PackingCodec.wgsl"
//! include "DiscrepancySequence.wgsl"
//! include "RayTraversal.wgsl"
//! include "ScreenTraversal.wgsl"

@group(0) @binding(1) var<storage, read> Materials : array<MaterialRecord>;
@group(0) @binding(2) var SurfaceAttributes        : texture_2d<f32>;
@group(0) @binding(3) var SurfaceDepth             : texture_depth_2d;
@group(0) @binding(4) var ReflectionTarget         : texture_storage_2d<rgba16float, write>;

const RoughnessCutoff : f32 = 0.5;

fn ResolveGlassTransmission(Position: vec3f, Normal: vec3f, View: vec3f, Surface: SurfaceSample) -> vec3f
{
    let Root = sqrt(clamp(Surface.SpecularLevel, 0.0, 0.99));
    let Ior = (1.0 + Root) / (1.0 - Root);
    let Inside = refract(-View, Normal, 1.0 / Ior);
    if (dot(Inside, Inside) < 1.0e-8) { return vec3f(0.0); }
    let Exit = TraceSceneRay(Position - Normal * RayEpsilon * 2.0, Inside, RayEpsilon, 5.0);
    if (!Exit.Hit || RayMaterials[Exit.Family].Transmission < 0.5) { return vec3f(0.0); }
    // TraceSceneRay orients the exit normal against the incident ray, as refract requires.
    let Outside = refract(Inside, Exit.Normal, Ior);
    if (dot(Outside, Outside) < 1.0e-8) { return vec3f(0.0); }
    let Origin = Exit.Position + Outside * RayEpsilon * 2.0;
    let Hit = TraceSceneRay(Origin, Outside, RayEpsilon, 300.0);
    if (!Hit.Hit) { return SampleSkyRadiance(Outside); }
    let Tolerance = ResolveCardTolerance(ResolveFinestClip(Hit.Position, 1.0));
    return SampleCardRadiance(Hit.Position, Hit.Normal, Hit.Identity, Tolerance).rgb;
}

@compute @workgroup_size(8, 8)
fn TraverseReflection(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TracePixel = Invocation.xy;
    if (any(vec2f(TracePixel) >= Frame.TraceExtent))
    {
        return;
    }
    let ScreenPixel = min(vec2i(TracePixel * Frame.TraceScale), vec2i(Frame.ScreenExtent) - vec2i(1));
    let Depth = textureLoad(SurfaceDepth, ScreenPixel, 0);
    if (Depth <= 0.0)
    {
        textureStore(ReflectionTarget, vec2i(TracePixel), vec4f(0.0));
        return;
    }
    let Attributes = textureLoad(SurfaceAttributes, ScreenPixel, 0);
    let Surface = ProjectSurfaceSample(Materials[u32(Attributes.z + 0.5)]);
    let Roughness = select(Surface.Roughness, Surface.CoatRoughness, Surface.CoatWeight > 0.0);
    let UseRtReflections = FeatureEnabled(Frame, FeatureRayTracing) && FeatureEnabled(Frame, FeatureRayTracedReflections);
    let Cutoff = select(RoughnessCutoff, ResolveReflectionRoughness(Frame), UseRtReflections);
    if (Roughness > Cutoff)
    {
        textureStore(ReflectionTarget, vec2i(TracePixel), vec4f(0.0));
        return;
    }
    let Position = ReconstructWorldPosition(vec2f(ScreenPixel) + 0.5, Depth);
    let Normal = DecodeOctahedral(Attributes.xy);
    let View = normalize(Frame.CameraPosition - Position);
    let Frame3 = ConstructTangentFrame(Normal);
    let TangentView = transpose(Frame3) * View;
    let Pair = ProjectTemporalPair(TracePixel, Frame.FrameIndex, 29u);
    let Half = Frame3 * ProjectVisibleNormal(TangentView, Roughness * Roughness, Pair);
    var Direction = reflect(-View, Half);
    if (dot(Direction, Normal) <= 0.0)
    {
        Direction = reflect(-View, Normal);
    }

    var Radiance = vec3f(0.0);
    var Strike : ScreenHit;
        var ScreenExitance = vec4f(0.0);
    if (FeatureEnabled(Frame, FeatureScreenTraversal) && FeatureEnabled(Frame, FeatureScreenReflection))
    {
        Strike = TraverseScreenDepth(Position + Normal * ResolveScreenBias(ResolveViewDepth(Position)), Direction, 300.0, ScreenTraversalSteps);
    }
    if (Strike.Hit) { ScreenExitance = ResolveStrikeExitance(Strike); }
        if (Strike.Hit && ScreenExitance.w > 0.0)
    {
        Radiance = ScreenExitance.rgb;
    }
    else if (UseRtReflections)
    {
        var Seed = ConstructPixelSeed(TracePixel, Frame.FrameIndex, 41u);
        Radiance = TraceMultiBounceReflection(Position + Normal * RayEpsilon, Direction, Roughness, &Seed);
    }
    else
    {
        let Bias = ResolveSurfaceBias(Position);
        let Hit  = TraverseSceneDistance(Position + Normal * Bias, Direction, 0.5 * Bias, 300.0, Roughness * 0.05);
        Radiance = ResolveTraversalRadiance(Hit, Direction);
    }
    if (Surface.Transmission > 0.5)
    {
        let Fresnel = ResolveFresnel(vec3f(Surface.SpecularLevel), max(dot(Normal, View), 0.0));
        Radiance = Fresnel * Radiance + (vec3f(1.0) - Fresnel) * ResolveGlassTransmission(Position, Normal, View, Surface);
    }
    textureStore(ReflectionTarget, vec2i(TracePixel), vec4f(min(Radiance, vec3f(200.0)), 1.0));
}
