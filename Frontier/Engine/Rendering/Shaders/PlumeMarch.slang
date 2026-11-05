/*==========================================================================================================================================
                                                          PLUMEMARCH.SLANG
                                     ------------------------------------------------------------------
                   Scattering quadrature of the smoke-and-fire lattice at trace resolution:
                    sun, sky, fire light, flame emission and transmittance, clipped to the
                                    coverage box the transport wrote.
==========================================================================================================================================*/

// 🧩 The plume march: the Slang form of the demo's PlumeMarch.wgsl, 1:1.
//
// 📝 Per resampling pixel, the camera ray is clipped to the coverage box the transport wrote and to the
//    opaque depth, then marched at about one sample per cell from a per-pixel offset that rotates every
//    frame, so the temporal antialiasing integrates the quadrature noise away. What each sample
//    receives:
//
//    | Term             | How                                                                                       |
//    |------------------|-------------------------------------------------------------------------------------------|
//    | sun              | cascaded shadow depth × the light lattice's transmittance, in three scattering octaves     |
//    |                  | (Wrenninge): each octave sees a thinner medium and a rounder phase, so dense smoke glows   |
//    |                  | through instead of turning grey                                                           |
//    | sky              | the sky view averaged, occluded by the zenith transmittance — a thick column has a dark   |
//    |                  | belly — plus a sun-lit ground bounce from below                                           |
//    | fire light       | the burning tyres and exhaust pops as point lights inside the volume: smoke above a fire  |
//    |                  | is lit orange from beneath                                                                |
//    | flame            | blackbody emission at a temperature between tip and core set by the cell's heat,          |
//    |                  | flickering with the detail noise                                                          |
//
//    Optics mix per sample: rubber smoke scatters (albedo ≈ 0.9), soot absorbs (≈ 0.1). Sub-cell detail
//    erodes the trilinear blobs of the lattice with a tiling noise volume drifting with the wind and the
//    rise of the plume. The result is premultiplied radiance and transmittance, composited before
//    antialiasing by the recombination.

import PlumeRecords;

//------------------------------------------------------------------------------------------------------------------------
//                                                       BINDINGS
//------------------------------------------------------------------------------------------------------------------------

public struct PlumeFrameRecord
{
    public float4x4 InverseViewProjection;
    public float3   CameraPosition;
    public float    TraceScale;
    public float3   SunDirection;
    public float    SkyScale;
    public float3   SunIlluminance;
    public uint     FrameIndex;
    public float3   NightSkyRadiance;
    public float    Reserved;
    public float2   TraceExtent;
    public float2   ScreenExtent;
    public float2   InverseScreenExtent;
    public float2   Reserved2;
};

public struct CascadeReceiver
{
    public float4x4 LightViewProjection[3];
    public float4   Splits;         // [m] xyz far distance of each cascade, w cascade count
    public float4   TexelWorld;     // [m] world size of one shadow texel per cascade
};

[[vk::binding(0, 1)]] public ConstantBuffer<PlumeFrameRecord> Frame;
[[vk::binding(1, 1)]] public Texture3D<float4> ThermoState;
[[vk::binding(2, 1)]] public SamplerState LatticeSampler;
[[vk::binding(3, 1)]] public Texture2D<float> SurfaceDepth;
[[vk::binding(4, 1)]] public StructuredBuffer<int> Coverage;
[[vk::binding(5, 1)]] public Texture2DArray<float> ShadowDepth;
[[vk::binding(6, 1)]] public SamplerComparisonState ShadowSampler;
[[vk::binding(7, 1)]] public ConstantBuffer<CascadeReceiver> Receiver;
[[vk::binding(8, 1)]] public Texture2D<float4> SkyView;
[[vk::binding(9, 1)]] public SamplerState SkySampler;
[[vk::binding(10, 1)]] public RWTexture2D<float4> PlumeTarget;
[[vk::binding(11, 1)]] public Texture3D<float4> LightState;
[[vk::binding(12, 1)]] public ConstantBuffer<FireLightRecord> FireLights;
[[vk::binding(13, 1)]] public Texture3D<float4> DetailState;
[[vk::binding(14, 1)]] public SamplerState DetailSampler;

public static const float InversePi = 0.3183098861837907;

//------------------------------------------------------------------------------------------------------------------------
//                                                       HELPERS
//------------------------------------------------------------------------------------------------------------------------

public float3 ReconstructWorldPosition(float2 Pixel, float Depth)
{
    float2 Ndc = float2(Pixel.x * Frame.InverseScreenExtent.x * 2.0 - 1.0, 1.0 - Pixel.y * Frame.InverseScreenExtent.y * 2.0);
    float4 Clip = mul(Frame.InverseViewProjection, float4(Ndc, Depth, 1.0));
    return Clip.xyz / Clip.w;
}

public float ResolveViewDepth(float3 Position)
{
    return length(Position - Frame.CameraPosition);
}

// 📝 Where a direction lands in the equirectangular sky view.
public float2 ProjectSkyCoordinates(float3 Direction)
{
    float Azimuth = atan2(Direction.y, Direction.x);
    float Elevation = asin(clamp(Direction.z, -1.0, 1.0));
    float U = Azimuth * (0.5 / 3.14159265358979) + 0.5;
    float V = 0.5 - 0.5 * sign(Elevation) * sqrt(abs(Elevation) / 1.5707963267949);
    return float2(U, V);
}

public float4 SampleThermo(float3 Position)
{
    return max(ThermoState.SampleLevel(LatticeSampler, ProjectThermoCoordinates(Position), 0.0), float4(0.0));
}

// 📐 Henyey–Greenstein, normalised over the sphere.
public float ResolveHenyeyGreenstein(float Cosine, float Anisotropy)
{
    float Squared = Anisotropy * Anisotropy;
    return (1.0 - Squared) / (12.56637061436 * pow(max(1.0 + Squared - 2.0 * Anisotropy * Cosine, 1.0e-4), 1.5));
}

// 📐 Frontier's smoke phase generalised: a forward lobe (g) mixed 72 % with a backward one (−0.65 g),
//    which is what gives a back-lit plume its silver lining. Frontier's own pair is g = 0.38, backward
//    −0.25.
public float ResolvePhase(float Cosine, float Anisotropy)
{
    return lerp(ResolveHenyeyGreenstein(Cosine, -0.65 * Anisotropy), ResolveHenyeyGreenstein(Cosine, Anisotropy), 0.72);
}

// 📝 One hardware-filtered comparison in the cascade that holds the sample; beyond the last cascade the
//    sun is seen.
public float ResolveSunVisibility(float3 Position)
{
    float ViewDistance = ResolveViewDepth(Position);
    uint Count = uint(Receiver.Splits.w);
    if (Count == 0u || ViewDistance > Receiver.Splits[Count - 1u])
        return 1.0;

    uint Cascade = 0u;
    while (Cascade + 1u < Count && ViewDistance > Receiver.Splits[Cascade])
        ++Cascade;

    float4 Clip = mul(Receiver.LightViewProjection[Cascade], float4(Position, 1.0));
    float3 Ndc = Clip.xyz / Clip.w;
    float2 Coordinates = float2(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5);
    if (any(Coordinates < float2(0.0)) || any(Coordinates > float2(1.0)) || Ndc.z > 1.0)
        return 1.0;
    return ShadowDepth.SampleCmpLevelZero(ShadowSampler, float3(Coordinates, float(Cascade)), Ndc.z);
}

// 📝 Transmittance toward the sun (x) and the zenith (y) through the plume, resolved once per coarse
//    cell by the transport (PlumeSimulation.slang ResolveSunTransmittance) and read here with one
//    filtered sample.
public float2 ResolveSelfShadow(float3 Position)
{
    return clamp(LightState.SampleLevel(LatticeSampler, ProjectVelocityCoordinates(Position), 0.0).xy, float2(0.0), float2(1.0));
}

// 📝 Isotropic sky term: the zenith and four horizon directions of the sky view.
public float3 ResolveSkyAmbient()
{
    float3 Sky = SkyView.SampleLevel(SkySampler, ProjectSkyCoordinates(float3(0.0, 0.0, 1.0)), 0.0).rgb * 2.0;
    for (uint Side = 0u; Side < 4u; ++Side)
    {
        float Theta = float(Side) * 1.5707963;
        Sky += SkyView.SampleLevel(SkySampler, ProjectSkyCoordinates(normalize(float3(cos(Theta), sin(Theta), 0.15))), 0.0).rgb;
    }
    return (Sky / 6.0) * Frame.SkyScale + Frame.NightSkyRadiance;
}

// 📝 The burning tyres and exhaust pops as softened point lights: irradiance over 4π, the phase taken
//    as isotropic.
public float3 ResolveFireLight(float3 Position)
{
    float3 Light = float3(0.0);
    for (uint Index = 0u; Index < min(FireLights.Count.x, 4u); ++Index)
    {
        float3 Offset = FireLights.PositionRadius[Index].xyz - Position;
        float Radius = FireLights.PositionRadius[Index].w;
        Light += FireLights.Radiance[Index].rgb / (dot(Offset, Offset) + Radius * Radius);
    }
    return Light * (0.0795774715 * Plume.Scattering.w);
}

// 📐 Sub-cell detail: three noise octaves (one per channel) folded into a value around 0.5, drifting
//    downwind and upward with the plume so the detail reads as motion below the lattice. Erosion takes
//    density away where the noise is low, more of it where the cell is thin — edges turn to wisps while
//    a dense core keeps its mass.
public float4 ResolveDetail(float3 Position)
{
    float Time = Plume.Wind.w;
    float3 Drift = float3(-Plume.Wind.x * 0.35, -Plume.Wind.y * 0.35, -0.55) * Time;
    float3 Coordinates = (Position + Drift) * Plume.Detail.y;
    return DetailState.SampleLevel(DetailSampler, Coordinates, 0.0);
}

public float ErodeDensity(float Density, float Noise)
{
    float Strength = Plume.Detail.x;
    float Thin = 1.0 - clamp(Density * 0.8, 0.0, 1.0);
    return max(0.0, Density * (1.0 + Strength * (Noise - 0.5) * 1.6)
                    - Strength * Thin * (1.0 - Noise) * 0.35 * min(Density, 1.0));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        MARCH
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
public void MarchPlume(uint3 Invocation : SV_DispatchThreadID)
{
    int2 TracePixel = int2(Invocation.xy);
    if (any(float2(TracePixel) >= Frame.TraceExtent))
        return;

    int3 Low = int3(Coverage[0], Coverage[1], Coverage[2]);
    int3 High = int3(Coverage[4], Coverage[5], Coverage[6]);
    if (any(Low > High))
    {
        PlumeTarget[TracePixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // ① the ray, clipped to the coverage box (grown by a cell) and to the opaque surface
    float Edge = Plume.Lattice.x;
    float3 Origin = float3(float2(2 * Plume.VelocityOrigin.xy) * Edge, Plume.Lattice.z);
    float3 BoxMinimum = Origin + (float3(Low) - 1.0) * Edge;
    float3 BoxMaximum = Origin + (float3(High) + 2.0) * Edge;
    int2 ScreenPixel = min(int2(float2(TracePixel) * Frame.TraceScale), int2(Frame.ScreenExtent) - int2(1));
    float Depth = SurfaceDepth.Load(int3(ScreenPixel, 0));
    float3 Far = ReconstructWorldPosition(float2(ScreenPixel) + 0.5, max(Depth, 1.0e-6));
    float3 Toward = Far - Frame.CameraPosition;
    float Reach = length(Toward);
    float3 Direction = Toward / max(Reach, 1.0e-9);
    float Opaque = select(Depth <= 0.0, 1.0e5, Reach);
    float3 Inverse = 1.0 / select(abs(Direction) < float3(1.0e-9), float3(1.0e-9), Direction);
    float3 Near = (BoxMinimum - Frame.CameraPosition) * Inverse;
    float3 Distant = (BoxMaximum - Frame.CameraPosition) * Inverse;
    float Enter = max(max(min(Near.x, Distant.x), min(Near.y, Distant.y)), max(min(Near.z, Distant.z), 0.0));
    float Exit = min(min(max(Near.x, Distant.x), max(Near.y, Distant.y)), min(max(Near.z, Distant.z), Opaque));
    if (Exit <= Enter)
    {
        PlumeTarget[TracePixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // ② about one sample per cell, from an interleaved-gradient offset rotated by the golden ratio each frame
    uint Count = uint(clamp(ceil((Exit - Enter) / (0.9 * Edge)), 4.0, max(Plume.Optical.w, 4.0)));
    float Step = (Exit - Enter) / float(Count);
    float Offset = frac(52.9829189 * frac(0.06711056 * float(TracePixel.x) + 0.00583715 * float(TracePixel.y))
                        + 0.61803398875 * float(Frame.FrameIndex % 256u));
    float Cosine = dot(Direction, Frame.SunDirection);
    float Anisotropy = Plume.Scattering.y;
    float3 PhaseOctaves = float3(ResolvePhase(Cosine, Anisotropy),
                                 ResolvePhase(Cosine, 0.5 * Anisotropy),
                                 ResolvePhase(Cosine, 0.25 * Anisotropy));
    float3 SkyAmbient = ResolveSkyAmbient() * Plume.Scattering.z;
    float3 GroundBounce = Frame.SunIlluminance * (max(Frame.SunDirection.z, 0.0) * 0.12 * InversePi * Plume.Scattering.z);
    bool Cells = Plume.ThermoExtent.w == 1u;
    float Strength = Plume.Scattering.x;
    float Flicker = 0.5 + 0.5 * sin(Plume.Wind.w * 23.0);

    float3 Radiance = float3(0.0);
    float Transmittance = 1.0;
    for (uint Index = 0u; Index < Count; ++Index)
    {
        if (Transmittance < 0.01)
            break;

        float3 Position = Frame.CameraPosition + Direction * (Enter + (float(Index) + Offset) * Step);
        float4 Thermo = SampleThermo(Position);
        if (Thermo.r < 1.0e-3 && Thermo.a < 1.0e-3 && Thermo.b < 1.0e-3)
            continue;

        if (Cells)
        {
            // 🔍 the lattice itself: smoke cyan, soot violet, heat red, flame yellow, unlit
            float Absorbed = 1.0 - exp(-(Thermo.r * 1.5 + Thermo.a * 1.5 + Thermo.b) * Step);
            float3 Colour = float3(0.35, 0.75, 1.0) * min(Thermo.r, 2.0) + float3(0.55, 0.3, 0.9) * min(Thermo.a, 2.0)
                          + float3(1.0, 0.25, 0.05) * min(Thermo.g * 0.02, 2.0) + float3(1.0, 0.85, 0.2) * min(Thermo.b, 2.0);
            Radiance += Transmittance * Colour * Absorbed * 4.0;
            Transmittance *= 1.0 - Absorbed;
            continue;
        }

        // ③ sub-cell detail on both particulates; the flame flickers with the noise's fourth channel
        if (Plume.Detail.x > 0.0)
        {
            float4 Noise = ResolveDetail(Position);
            float Folded = dot(Noise.rgb, float3(0.5, 0.3, 0.2));
            Thermo.r = ErodeDensity(Thermo.r, Folded);
            Thermo.a = ErodeDensity(Thermo.a, Folded);
            Thermo.b *= lerp(1.0, 0.45 + 1.1 * lerp(Noise.a, Flicker, 0.25), min(Plume.Detail.x, 1.0));
        }

        // ④ optics of the mixture
        float SmokeExtinction = Thermo.r * Plume.Optical.x;
        float SootExtinction = Thermo.a * Plume.Optical.z;
        float Particulate = SmokeExtinction + SootExtinction;
        float Extinction = Particulate + Thermo.b * 0.8;
        if (Extinction < 1.0e-4)
            continue;

        float Absorbed = 1.0 - exp(-Extinction * Step);
        float Albedo = (SmokeExtinction * Plume.Optical.y + SootExtinction * Plume.Sun.w) / max(Particulate, 1.0e-5);
        float Share = Particulate / Extinction;

        // ⑤ sun in three octaves, sky occluded from above, fire light from within
        float2 Shadow = ResolveSelfShadow(Position);
        float Visibility = ResolveSunVisibility(Position);
        // 📝 Each further octave is light that scattered again inside the medium, so it is weighted by the
        //    albedo once more: white rubber smoke glows through, soot — which absorbs at every event —
        //    keeps only its single bounce.
        float Octaves = PhaseOctaves.x * Shadow.x
                      + Strength * Albedo * (0.6 * PhaseOctaves.y * pow(Shadow.x, 0.25)
                                           + 0.36 * PhaseOctaves.z * pow(Shadow.x, 0.0625));
        float3 Sunlight = Frame.SunIlluminance * (Visibility * Octaves);
        float3 Sky = SkyAmbient * (0.18 + 0.82 * pow(Shadow.y, 0.6)) + GroundBounce;
        float3 Scattered = (Sunlight + Sky + ResolveFireLight(Position)) * (Albedo * Share * Absorbed);

        // ⑥ flame: blackbody between tip and core, brighter where the gas is hotter
        float Heat = Thermo.g / (Thermo.g + Plume.Flame.z);
        float Temperature = lerp(Plume.Flame.y, Plume.Flame.x, Heat);
        float3 Emission = ResolveBlackbodyRadiance(Temperature) * (Plume.SourceMaximum.w * min(Thermo.b, 1.5) * Step);

        Radiance += Transmittance * (Scattered + Emission);
        Transmittance *= 1.0 - Absorbed;
    }
    PlumeTarget[TracePixel] = float4(Radiance, Transmittance);
}
