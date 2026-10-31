//==========================================================================================================================================
//                                                      FOG SPACE SLANG
//==========================================================================================================================================
// Couples analytic closed-form exponential height fog + planet-curved surface-distance aerial perspective +
// bounded 3D noise-modulated froxel volumetric fog boxes scattering CloudShadow-attenuated sun shafts,
// photometric IES/spot luminaires, branching lightning channels, and thermal/fire radiant facets.

#include "CloudSynthesis.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4>                     Ensemble;
[[vk::binding(1, 0)]] Sampler2DArray<float4>                       TransLut;
[[vk::binding(2, 0)]] Sampler2DArray<float4>                       MultiLut;
[[vk::binding(3, 0)]] Sampler2DArray<float4>                       CloudShadowLut;
[[vk::binding(4, 0)]] [format("rgba16f")] RWTexture2DArray<float4> VisibilityImage;
[[vk::binding(5, 0)]] [format("rgba16f")] RWTexture2DArray<float4> HdrFrame;

float FogHeightDensityAtY(float4 Hf0, float HeightM)
{
    if (Hf0.x <= 0.0f)
    {
        return 0.0f;
    }
    float ExpArg = clamp(-Hf0.y * (HeightM - Hf0.z), -60.0f, 40.0f);
    return Hf0.x * exp(ExpArg);
}

float EvaluateLocalFogBoxExtinction(StructuredBuffer<float4> Ens,
                                    uint                     FogOffset,
                                    float3                   PointMeters,
                                    float                    TimeSeconds,
                                    out float                OutAlbedo,
                                    out float                Out𝑔)
{
    float4 F0 = Ens[FogOffset + 0u]; // CenterXYZ, RefDensity
    float4 F1 = Ens[FogOffset + 1u]; // HalfXYZ, ScatterAlbedo
    float4 F2 = Ens[FogOffset + 2u]; // NoiseAmp, NoiseFreq, PhaseG, EdgeSoftnessM
    OutAlbedo = F1.w;
    Out𝑔      = F2.z;

    if (F0.w <= 0.0f)
    {
        return 0.0f;
    }
    float3 RelPos   = PointMeters - F0.xyz;
    float3 HalfSpan = max(F1.xyz, float3(1.0e-3f));
    if (abs(RelPos.x) >= HalfSpan.x || abs(RelPos.y) >= HalfSpan.y || abs(RelPos.z) >= HalfSpan.z)
    {
        return 0.0f;
    }
    float SoftEdge = max(F2.w, 1.0e-3f);
    float MaskX    = saturate((HalfSpan.x - abs(RelPos.x)) / SoftEdge);
    float MaskY    = saturate((HalfSpan.y - abs(RelPos.y)) / SoftEdge);
    float MaskZ    = saturate((HalfSpan.z - abs(RelPos.z)) / SoftEdge);
    float Envelope = MaskX * MaskX * (3.0f - 2.0f * MaskX) *
                     MaskY * MaskY * (3.0f - 2.0f * MaskY) *
                     MaskZ * MaskZ * (3.0f - 2.0f * MaskZ);

    float Modulation = 1.0f;
    if (F2.x > 0.0f && F2.y > 0.0f)
    {
        float3 Advected = (PointMeters - float3(1.4f * TimeSeconds, 0.25f * TimeSeconds, 0.6f * TimeSeconds)) * F2.y;
        float  NoiseVal = FbmRotated3D(Advected, 3, 409u);
        Modulation      = max(0.0f, 1.0f + F2.x * (2.0f * NoiseVal - 1.0f));
    }
    return F0.w * Envelope * Modulation;
}

float PointSegmentDistSq(float3 Point, float3 SegStart, float3 SegEnd)
{
    float3 Seg  = SegEnd - SegStart;
    float  Len2 = dot(Seg, Seg);
    if (Len2 <= 1.0e-12f)
    {
        float3 Diff = Point - SegStart;
        return dot(Diff, Diff);
    }
    float  Proj    = saturate(dot(Point - SegStart, Seg) / Len2);
    float3 Closest = SegStart + Seg * Proj;
    float3 Diff    = Point - Closest;
    return dot(Diff, Diff);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateFogSpaceMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    float4 Vis1      = VisibilityImage[uint3(Tid.xy, 1u)];
    float4 Vis2      = VisibilityImage[uint3(Tid.xy, 2u)];
    float3 GlassTint = VisibilityImage[uint3(Tid.xy, 3u)].rgb;
    uint   RealmId   = uint(Vis1.w + 0.5f);
    float3 RayOrigin = Vis1.xyz;
    float3 RayDir    = SafeNormalize(Vis2.xyz);
    float  HitDistM  = Vis2.w;
    bool   IsSky     = (HitDistM < 0.0f);

    uint   RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    float4 R0          = Ensemble[RealmOffset + 0u];
    float4 Hf0         = Ensemble[RealmOffset + 6u]; // RefDensity, Falloff, RefHeightM, MaxOpacity
    float4 Hf1         = Ensemble[RealmOffset + 7u]; // StartDistM, PhaseG, Albedo, EnableAerialPerspective
    float4 R8          = Ensemble[RealmOffset + 8u];
    float4 R10         = Ensemble[RealmOffset + 10u];
    float4 R11         = Ensemble[RealmOffset + 11u];
    float4 R12         = Ensemble[RealmOffset + 12u];
    float  TimeSeconds = Ensemble[3].w;

    uint SunFirst    = uint(R8.x + 0.5f);
    uint SunCount    = uint(R8.y + 0.5f);
    uint FogBoxTable = uint(Ensemble[6].y + 0.5f);
    uint FogBoxFirst = uint(R10.x + 0.5f);
    uint FogBoxCount = uint(R10.y + 0.5f);
    uint LumTable    = uint(Ensemble[6].z + 0.5f);
    uint LumFirst    = uint(R10.z + 0.5f);
    uint LumCount    = uint(R10.w + 0.5f);
    uint BoltTable   = uint(Ensemble[6].w + 0.5f);
    uint BoltFirst   = uint(R11.x + 0.5f);
    uint BoltCount   = uint(R11.y + 0.5f);
    uint SphereTable = uint(Ensemble[7].y + 0.5f);
    uint SphereFirst = uint(R11.z + 0.5f);
    uint SphereCount = uint(R11.w + 0.5f);
    uint FacetTable  = uint(Ensemble[7].x + 0.5f);
    uint FacetFirst  = uint(R12.z + 0.5f);
    uint FacetCount  = uint(R12.w + 0.5f);
    uint ForceCount  = uint(Ensemble[RealmOffset + 17u].y + 0.5f);

    float3 SurfaceHdr = HdrFrame[uint3(Tid.xy, 0u)].rgb;

    // 1. Planet-curved surface-distance aerial perspective (on opaque surfaces)
    if (!IsSky && Hf1.w > 0.5f && HitDistM > 5.0f)
    {
        float  DistKm    = HitDistM * 1.0e-3f;
        float3 AerialIn  = EvaluateCoupledSolarSky6Pt(Ensemble, TransLut, MultiLut, RealmId, RayDir, DistKm);
        float3 σRayleigh, σExtinction;
        float  σMie;
        EvaluateMediumCoefficientsGpu(Ensemble, RealmOffset, max(0.0f, R0.z), σRayleigh, σMie, σExtinction);
        float3 AerialTrans = exp(-σExtinction * DistKm);
        SurfaceHdr         = SurfaceHdr * AerialTrans + AerialIn * GlassTint;
    }

    float DefaultSpan = (ForceCount > 0u) ? 520.0f : 120.0f;
    float MaxMarchM   = IsSky ? ((ForceCount > 0u) ? 520.0f : 64.0f) : min(HitDistM, DefaultSpan);
    if (Hf0.x <= 0.0f && FogBoxCount == 0u && ForceCount == 0u)
    {
        HdrFrame[uint3(Tid.xy, 0u)] = float4(SurfaceHdr, 1.0f);
        return;
    }

    // 2. Quadratic froxel volumetric fog integration (height fog + local 3D fog boxes + ForceField dust/debris + shafts + emitters)
    const int kSlices       = 36;
    float     Jitter        = frac(52.9829189f * frac(0.06711056f * float(Tid.x) + 0.00583715f * float(Tid.y)));
    float     Transmittance = 1.0f;
    float3    InScatter     = float3(0.0f);

    for (int Slice = 0; Slice < kSlices; ++Slice)
    {
        float U0 = float(Slice) / float(kSlices);
        float U1 = float(Slice + 1) / float(kSlices);
        float T0 = MaxMarchM * (U0 * U0);
        float T1 = MaxMarchM * (U1 * U1);
        float Δs = T1 - T0;
        if (Δs <= 1.0e-5f)
        {
            continue;
        }
        float  UMid      = (float(Slice) + Jitter) / float(kSlices);
        float  TMid      = MaxMarchM * (UMid * UMid);
        float3 SamplePos = RayOrigin + RayDir * TMid;

        float  σHeight     = (TMid >= Hf1.x) ? FogHeightDensityAtY(Hf0, SamplePos.y) : 0.0f;
        float  σExt        = σHeight;
        float3 σScatterRgb = float3(σHeight * Hf1.z);
        float  σScatterSum = σHeight * Hf1.z;
        float  Weighted𝑔   = σScatterSum * Hf1.y;

        for (uint FIdx = 0u; FIdx < FogBoxCount; ++FIdx)
        {
            float BoxAlbedo, Box𝑔;
            float σBox = EvaluateLocalFogBoxExtinction(
                Ensemble, FogBoxTable + (FogBoxFirst + FIdx) * kFogBoxStride, SamplePos, TimeSeconds, BoxAlbedo, Box𝑔);
            if (σBox > 0.0f)
            {
                σExt        += σBox;
                σScatterRgb += float3(σBox * BoxAlbedo);
                σScatterSum += σBox * BoxAlbedo;
                Weighted𝑔   += σBox * BoxAlbedo * Box𝑔;
            }
        }
        float  DustFraction = 0.0f;
        if (ForceCount > 0u)
        {
            float3 DustAlbedo;
            float  Dust𝑔;
            float  σDust = EvaluateForceFieldParticulateExtinction(
                Ensemble, RealmId, SamplePos, TimeSeconds, DustAlbedo, Dust𝑔);
            if (σDust > 0.0f)
            {
                float MeanDustAlb = (DustAlbedo.x + DustAlbedo.y + DustAlbedo.z) * (1.0f / 3.0f);
                σExt        += σDust;
                σScatterRgb += σDust * DustAlbedo;
                σScatterSum += σDust * MeanDustAlb;
                Weighted𝑔   += σDust * MeanDustAlb * Dust𝑔;
                DustFraction = saturate(σDust / max(σExt, 1.0e-6f));
            }
        }
        if (σExt <= 1.0e-7f)
        {
            continue;
        }
        float 𝑔Mean = (σScatterSum > 1.0e-7f) ? (Weighted𝑔 / σScatterSum) : 0.0f;

        float3 Luminance = float3(0.012f, 0.016f, 0.024f);

        // Solar crepuscular shafts + diffuse multi-scattered skylight for lofted sand/dust/debris particulates
        for (uint SunIdx = 0u; SunIdx < SunCount; ++SunIdx)
        {
            uint   SunOffset       = SunSlotOffset(Ensemble, SunFirst + SunIdx);
            float3 SolarDirection  = Ensemble[SunOffset + 0u].xyz;
            float3 SolarIrradiance = Ensemble[SunOffset + 1u].xyz;
            if (SolarDirection.y <= -0.05f)
            {
                continue;
            }
            bool Blocked = false;
            for (uint SpIdx = 0u; SpIdx < SphereCount; ++SpIdx)
            {
                float4 S0    = Ensemble[SphereTable + (SphereFirst + SpIdx) * kSphereStride];
                float3 Oc    = SamplePos - S0.xyz;
                float  HalfB = dot(Oc, SolarDirection);
                float  Disc  = HalfB * HalfB - (dot(Oc, Oc) - S0.w * S0.w);
                if (Disc > 0.0f && (-HalfB - sqrt(Disc)) > 0.0f)
                {
                    Blocked = true;
                    break;
                }
            }
            if (Blocked)
            {
                continue;
            }
            float  CloudTr = SampleFogCloudTransmittance(
                Ensemble, CloudShadowLut, RealmId, SamplePos, SolarDirection);
            float3 AtmoTr  = SampleTransmittanceLutArray(
                TransLut, RealmId, R0.x, R0.y, R0.x + max(R0.z, 1.0e-3f), SolarDirection.y);
            float  Phase   = HenyeyGreensteinValue(dot(RayDir, SolarDirection), 𝑔Mean);
            Luminance     += SolarIrradiance * AtmoTr * (CloudTr * Phase);
            if (DustFraction > 0.0f)
            {
                // Multi-scattered overcast storm skylight + warm ground bounce illuminating lofted ochre dust/debris
                float2 MultiUv  = saturate(float2(0.5f + 0.5f * SolarDirection.y, max(R0.z, 1.0e-3f) / max(R0.y - R0.x, 1.0e-3f)));
                float3 ΨMulti   = MultiLut.SampleLevel(float3(MultiUv, float(RealmId)), 0.0f).rgb;
                Luminance      += DustFraction * SolarIrradiance * (ΨMulti * 0.95f + AtmoTr * 0.085f);
            }
        }

        // Clustered IES / spot luminaires
        for (uint LIdx = 0u; LIdx < LumCount; ++LIdx)
        {
            uint   LumOffset = LumTable + (LumFirst + LIdx) * kLuminaireStride;
            float4 L0        = Ensemble[LumOffset + 0u];
            float4 L1        = Ensemble[LumOffset + 1u];
            float4 L2        = Ensemble[LumOffset + 2u];
            float3 ToPoint   = SamplePos - L0.xyz;
            float  Dist2     = dot(ToPoint, ToPoint);
            if (Dist2 < L2.w * L2.w)
            {
                float  Dist    = sqrt(max(Dist2, 1.0e-3f));
                float3 LightDir = ToPoint / Dist;
                float  cosCone = dot(LightDir, L1.xyz);
                if (cosCone >= L1.w)
                {
                    float Cone = smoothstep(L1.w, min(1.0f, L1.w + 0.22f), cosCone);
                    float Win  = pow(saturate(1.0f - Dist2 / max(L2.w * L2.w, 1.0e-2f)), 2.0f);
                    float Ph   = HenyeyGreensteinValue(dot(RayDir, -LightDir), 𝑔Mean);
                    Luminance += L2.xyz * (L0.w * Cone * Win / (Dist2 + 0.25f) * Ph);
                }
            }
        }

        // Branching lightning bolt channels
        for (uint BIdx = 0u; BIdx < BoltCount; ++BIdx)
        {
            uint   BoltOffset = BoltTable + (BoltFirst + BIdx) * kLightningStride;
            float4 B0         = Ensemble[BoltOffset + 0u];
            float4 B1         = Ensemble[BoltOffset + 1u];
            float4 B2         = Ensemble[BoltOffset + 2u];
            float  Dist       = sqrt(PointSegmentDistSq(SamplePos, B0.xyz, B1.xyz));
            float  Atten      = exp(-B2.w * max(0.0f, Dist - B1.w)) / (1.0f + 0.08f * Dist * Dist);
            Luminance        += B2.xyz * (B0.w * Atten * kInv4π);
        }

        // Thermal fire / radiant area facets
        for (uint FIdx = 0u; FIdx < FacetCount; ++FIdx)
        {
            uint   FacetOffset = FacetTable + (FacetFirst + FIdx) * kFacetStride;
            float4 F0          = Ensemble[FacetOffset + 0u];
            float4 F1          = Ensemble[FacetOffset + 1u];
            float4 F2          = Ensemble[FacetOffset + 2u];
            float3 ToPoint     = SamplePos - F0.xyz;
            float  Dist2       = dot(ToPoint, ToPoint);
            if (Dist2 < F1.w * F1.w)
            {
                float3 Dir  = ToPoint * rsqrt(max(Dist2, 1.0e-3f));
                float  cosE = (F2.w > 0.5f) ? abs(dot(F1.xyz, Dir)) : max(0.0f, dot(F1.xyz, Dir));
                float  Win  = saturate(1.0f - sqrt(Dist2) / max(F1.w, 1.0e-2f));
                Luminance  += F2.xyz * (F0.w * cosE * Win * Win / (Dist2 + 0.5f) * kInv4π);
            }
        }

        float  StepTr = exp(-σExt * Δs);
        float3 IntW   = (σScatterRgb / σExt) * (1.0f - StepTr);
        InScatter     += Transmittance * Luminance * IntW;
        Transmittance *= StepTr;
    }

    // Analytic closed-form exponential height fog tail beyond MaxMarchM up to distant surface
    if (!IsSky && Hf0.x > 0.0f && HitDistM > MaxMarchM)
    {
        float3 StartPos = RayOrigin + RayDir * MaxMarchM;
        float  RemDistM = HitDistM - MaxMarchM;
        float  Δh       = RayDir.y * RemDistM;
        float  ρStart   = FogHeightDensityAtY(Hf0, StartPos.y);
        float  τRem     = (abs(Hf0.y * Δh) < 1.0e-3f)
                              ? (ρStart * RemDistM * (1.0f - 0.5f * Hf0.y * Δh))
                              : (ρStart * RemDistM * (1.0f - exp(-Hf0.y * Δh)) / (Hf0.y * Δh));
        float  TailTr   = max(1.0f - Hf0.w, exp(-max(0.0f, τRem)));
        InScatter     += Transmittance * (1.0f - TailTr) * float3(0.035f, 0.045f, 0.060f) * Hf1.z;
        Transmittance *= TailTr;
    }

    Transmittance = max(Transmittance, 1.0f - saturate(Hf0.w));
    HdrFrame[uint3(Tid.xy, 0u)] = float4(SurfaceHdr * Transmittance + InScatter * GlassTint, 1.0f);
}
