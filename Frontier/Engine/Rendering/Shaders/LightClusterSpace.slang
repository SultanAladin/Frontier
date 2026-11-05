/*==========================================================================================================================================
                                                       LIGHTCLUSTERSPACE.SLANG
                                     ------------------------------------------------------------------
                  Light records binned into a world-space grid: each cell's 32-bit mask names
                    the lights whose reach touches it, and each light's arrival is its beam
                              profile with inverse-square falloff.
==========================================================================================================================================*/

// 🧩 The clustered-light grid: the Slang form of the demo's LightClusterSpace.wgsl, 1:1.
//
// 📝 The host packs LightClusterRecords every frame (Frontier/Engine/Rendering/LightClusterSpace.h):
//    Minimum, Extent, 32 lights, 1024 cell masks, the 23 584-byte LM-63 photometric atlas, and the
//    emissive region. Everything here reads one binding, so the ray tracer, the card relighting and
//    the direct-radiance pass agree on what the lights are without a second upload.

//------------------------------------------------------------------------------------------------------------------------
//                                                       RECORDS
//------------------------------------------------------------------------------------------------------------------------

public struct LightRecord
{
    public float4 PositionType;      // xyz = world position, w = light type (0: omni, 1: street lamp, 2: low beam, 3: high beam, 4: fog, 5: flood)
    public float4 DirectionReach;    // xyz = forward direction, w = reach radius [m]
    public float4 ColourIntensity;   // rgb = linear colour, w = intensity
    public float4 Params;            // x = inner angle cos, y = outer angle cos, z = profile variant, w = active
};

// 📝 The photometric atlas: 7 Type C profiles (IES LM-63, Photometry.h) of 37 × 91 bytes each, peak = 255, four to a u32.
public static const uint PhotometryAzimuthBins      = 37u;    // 10° steps of C-plane around the optical axis
public static const uint PhotometryPolarBins        = 91u;    // 2° steps from the axis
public static const uint PhotometryTexelsPerProfile = 3367u;

// 📝 The emissive region (EmissiveLightSpace.h): emissive triangles per facet set in local space, one record per cluster
//    instance, and per grid cell eight cluster ids.
public struct EmissiveTriangleRecord
{
    public float4 A;    // xyz local corner A, w the triangle's index in the BVH triangle buffer (bitcast u32)
    public float4 E1;   // xyz edge B − A, w the local area
    public float4 E2;   // xyz edge C − A, w the material family (bitcast u32)
};

public struct EmissiveClusterRecord
{
    public float4 CentreReach;   // xyz world centre, w radius
    public uint4  Meta;          // x occupant, y first triangle, z triangle count, w power (bitcast f32; area × luminance, world)
};

public struct LightClusterRecords
{
    public float4 Minimum;                                  // xyz world minimum corner of the grid, w = 1 once the atlas is uploaded
    public float4 Extent;                                   // xyz world size of the grid, w = number of emissive cluster instances
    public LightRecord Lights[32];
    public uint4 CellMasks[256];                            // 16 × 16 × 4 cells, x fastest: bit i of cell c is bit (c & 3) of lane CellMasks[c >> 2]
    public uint4 Atlas[1474];                               // 7 photometric profiles, one byte per texel (23 569 texels, padded to a vec4u)
    public EmissiveTriangleRecord EmissiveTriangles[160];
    public EmissiveClusterRecord EmissiveClusters[224];
    public uint4 EmissiveCells[1024];                       // per cell eight u16 cluster ids, the strongest first (0x7FFF empty)
};

public static const uint3 LightClusterResolution = uint3(16u, 16u, 4u);

// 📝 ExtendedFeature — the second feature word; the same bits SceneRecords.wgsl defines.
public static const uint ExtendedLightGrid     = 8u;    // world light grid culling; off = every receiver tests every light
public static const uint ExtendedEmissiveLights = 16u;  // emissive-triangle clusters are sampled as lights
public static const uint ExtendedIesProfiles   = 32u;   // beams follow their IES profiles instead of the analytic shapes

public struct LightFeatureRecord
{
    public uint Features2;
};

[[vk::binding(0, 0)]] public ConstantBuffer<LightClusterRecords> SceneLights;
[[vk::binding(1, 0)]] public ConstantBuffer<LightFeatureRecord> LightFeatures;

public static bool ExtendedEnabled(uint Bit)
{
    return (LightFeatures.Features2 & Bit) != 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   GRID CULLING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Points outside the grid take every light: the mask is then all ones, so no light is skipped.
public uint ResolveLightCandidates(float3 Position)
{
    if (!ExtendedEnabled(ExtendedLightGrid))
        return 0xFFFFFFFFu;   // grid off: every receiver tests every light (inactive lights contribute nothing)

    float3 Local = (Position - SceneLights.Minimum.xyz) / SceneLights.Extent.xyz;
    if (any(Local < float3(0.0)) || any(Local >= float3(1.0)))
        return 0xFFFFFFFFu;

    uint3 Cell = min(uint3(Local * float3(LightClusterResolution)), LightClusterResolution - uint3(1u));
    uint Index = Cell.x + LightClusterResolution.x * (Cell.y + LightClusterResolution.y * Cell.z);
    uint Lane = SceneLights.CellMasks[Index >> 2u][Index & 3u];
    return Lane;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PHOTOMETRY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which photometric profile a light follows: its type picks the luminaire (roadway, low beam, high beam, fog beam), the
//    flood type's variant picks flood or spot pods. 0 means none (an omnidirectional point, or a type without a file).
public uint ResolvePhotometricProfile(LightRecord Light)
{
    uint Type = uint(Light.PositionType.w + 0.5);
    if (Type == 1u) return 1u;
    if (Type == 2u) return 3u;
    if (Type == 3u) return 4u;
    if (Type == 4u) return 5u;
    if (Type == 5u) return (Light.Params.z > 0.5) ? 7u : 6u;
    return 0u;
}

public float LoadPhotometricTexel(uint Profile, uint Azimuth, uint Polar)
{
    uint Texel = (Profile - 1u) * PhotometryTexelsPerProfile + Azimuth * PhotometryPolarBins + Polar;
    uint Word = SceneLights.Atlas[Texel >> 4u][(Texel >> 2u) & 3u];
    return float((Word >> ((Texel & 3u) * 8u)) & 255u) * (1.0 / 255.0);
}

// 📝 The profile's relative intensity towards FromFixture (unit, from the light to the point). Type C: the polar angle is
//    measured from the optical axis; C0 is up around it (world +Z; +X for a fixture that looks straight up or down), C90 to
//    its right. Bilinear reconstruction removes banding at a beam's cutoff.
public float SamplePhotometry(LightRecord Light, uint Profile, float3 FromFixture)
{
    float3 Axis = normalize(Light.DirectionReach.xyz);
    float3 Reference = (abs(Axis.z) > 0.99) ? float3(1.0, 0.0, 0.0) : float3(0.0, 0.0, 1.0);
    float3 Right = normalize(cross(Axis, Reference));
    float3 Up = cross(Right, Axis);

    float Polar = acos(clamp(dot(FromFixture, Axis), -1.0, 1.0)) * (float(PhotometryPolarBins - 1u) / 3.141592653589793);
    float Angle = atan2(dot(FromFixture, Right), dot(FromFixture, Up));
    if (Angle < 0.0) Angle += 6.283185307179586;
    float Azimuth = Angle * (float(PhotometryAzimuthBins - 1u) / 6.283185307179586);

    uint H0 = min(uint(floor(Azimuth)), PhotometryAzimuthBins - 2u);
    uint V0 = min(uint(floor(Polar)), PhotometryPolarBins - 2u);
    float H = clamp(Azimuth - float(H0), 0.0, 1.0);
    float V = clamp(Polar - float(V0), 0.0, 1.0);

    float A = lerp(LoadPhotometricTexel(Profile, H0, V0), LoadPhotometricTexel(Profile, H0, V0 + 1u), V);
    float B = lerp(LoadPhotometricTexel(Profile, H0 + 1u, V0), LoadPhotometricTexel(Profile, H0 + 1u, V0 + 1u), V);
    return lerp(A, B, H);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    BEAM SHAPES
//------------------------------------------------------------------------------------------------------------------------

public float EvaluateBeamProfile(LightRecord Light, float3 TowardPoint, float Distance)
{
    uint Type = uint(Light.PositionType.w + 0.5);
    if (Type == 0u)
        return 1.0;   // omnidirectional point

    // 📝 Measured (or authored) photometry where the atlas is loaded and the switch is on: the file's shape, bounded by
    //    the same cone the analytic beams respect.
    uint Profile = ResolvePhotometricProfile(Light);
    if (Profile != 0u && SceneLights.Minimum.w > 0.5 && ExtendedEnabled(ExtendedIesProfiles))
    {
        float3 FromFixture = TowardPoint / max(Distance, 1.0e-4);
        float Shape = SamplePhotometry(Light, Profile, FromFixture);
        if (Type == 5u)
            return Shape * smoothstep(Light.Params.y, Light.Params.x, dot(FromFixture, normalize(Light.DirectionReach.xyz)));
        return Shape;
    }

    float3 LDir = TowardPoint / max(Distance, 1.0e-4);
    float3 Forward = normalize(Light.DirectionReach.xyz);
    float CosTheta = dot(LDir, Forward);

    // Type 1: street lamp / roadway batwing (roadway luminaire, Type II / III spread)
    if (Type == 1u)
    {
        float CosDown = -LDir.z;
        if (CosDown <= 0.05) return 0.0;
        float Theta = acos(clamp(CosDown, 0.0, 1.0));
        float Batwing = sin(Theta * 1.8) * CosDown * 2.2;
        return max(0.0, Batwing);
    }

    // Type 2: automotive low beam (asymmetric sharp cutoff, right-side kick, wide horizontal spread)
    if (Type == 2u)
    {
        if (CosTheta <= 0.08) return 0.0;
        float3 Right = normalize(cross(Forward, float3(0.0, 0.0, 1.0)));
        float3 Up = cross(Right, Forward);
        float LocalY = dot(LDir, Right);
        float LocalZ = dot(LDir, Up);

        // Asymmetric beam cutoff (ECE / DOT standard: lower on the oncoming lane, kicks up towards the shoulder)
        float CutoffZ = -0.02 - 0.04 * max(0.0, LocalY);
        float TopFade = clamp((CutoffZ + 0.035 - LocalZ) / 0.035, 0.0, 1.0);
        float HorizAngle = atan2(LocalY, CosTheta);
        float HorizFade = max(0.0, cos(clamp(HorizAngle * 1.6, -1.57, 1.57)));
        return TopFade * HorizFade * (CosTheta * CosTheta);
    }

    // Type 3: automotive high beam (collimated pencil spotlight with soft halo)
    if (Type == 3u)
    {
        if (CosTheta <= 0.15) return 0.0;
        return pow(max(0.0, CosTheta), 14.0);
    }

    // Type 4: automotive fog light (wide fan, razor-sharp top cutoff to prevent glare in fog/smoke)
    if (Type == 4u)
    {
        if (CosTheta <= 0.08) return 0.0;
        float3 Right = normalize(cross(Forward, float3(0.0, 0.0, 1.0)));
        float3 Up = cross(Right, Forward);
        float LocalY = dot(LDir, Right);
        float LocalZ = dot(LDir, Up);
        if (LocalZ > -0.005) return 0.0;
        float HorizAngle = atan2(LocalY, CosTheta);
        return max(0.0, cos(clamp(HorizAngle * 1.3, -1.57, 1.57))) * pow(CosTheta, 4.0);
    }

    // Type 5: roof floodlight (multi-lens LED bar or dual rally pods)
    if (Type == 5u)
    {
        float InnerCos = Light.Params.x;
        float OuterCos = Light.Params.y;
        return smoothstep(OuterCos, InnerCos, CosTheta);
    }

    return 1.0;
}

// 📐 Inverse-square illuminance with a smooth window at the reach; the cosine of the surface is inside the result.
public float3 EvaluateLightArrival(LightRecord Light, float3 Position, float3 Normal)
{
    if (Light.Params.w < 0.5)
        return float3(0.0);

    float3 LightPos = Light.PositionType.xyz;
    float Reach = Light.DirectionReach.w;
    float3 Toward = LightPos - Position;
    float D2 = dot(Toward, Toward);

    if (D2 >= Reach * Reach || Reach <= 0.0)
        return float3(0.0);

    float Distance = sqrt(D2);
    float3 LightDir = Toward / max(Distance, 1.0e-4);
    float NdotL = max(dot(Normal, LightDir), 0.0);

    if (NdotL <= 0.0)
        return float3(0.0);

    // Window function: C2 continuous polynomial smoothly dropping to 0 at Reach
    float Ratio = D2 / (Reach * Reach);
    float Window = (1.0 - Ratio * Ratio) * (1.0 - Ratio * Ratio);

    float Profile = EvaluateBeamProfile(Light, -Toward, Distance);
    if (Profile <= 0.0)
        return float3(0.0);

    float Intensity = Light.ColourIntensity.w;
    float3 Colour = Light.ColourIntensity.rgb;

    // Standard physical inverse-square falloff with radius margin to avoid singularity
    float Falloff = Window / max(D2, 0.36);
    return Colour * (Intensity * Falloff * Profile * NdotL);
}
