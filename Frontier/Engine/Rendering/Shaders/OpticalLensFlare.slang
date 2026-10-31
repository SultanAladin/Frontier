//==========================================================================================================================================
//                                                 OPTICAL LENS FLARE SLANG
//==========================================================================================================================================
// 1. EvaluateFlareVisibilityMain: 1x1-per-source GPU visibility reduction integrating 64 limb-darkened 0.53 deg disc rays
//    against reversed-Z depth, cross-realm portal aperture clipping, volumetric cloud transmittance, and dielectric glass tint.
// 2. CompositeFlareApertureMain: 8-element multi-ghost chromatic aberration, antipode spectral halo ring, warm rotatable streak,
//    blue cylindrical anamorphic streak, two-lobe PSF glow, veiling glare, and polygonal aperture diffraction starburst.

#include "EnvironmentEnsemble.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4> Ensemble;
[[vk::binding(1, 0)]] [format("rgba16f")] RWTexture2DArray<float4> Gbuffer;
[[vk::binding(2, 0)]] [format("rgba16f")] RWTexture2DArray<float4> CloudHistoryTarget; // Layer 1.w = VolumetricCloud transmittance
[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture2DArray<float4> FlareVisibility;    // (MaxFlareSources x 1 x 1) RGBA16F
[[vk::binding(4, 0)]] [format("rgba16f")] RWTexture2DArray<float4> HdrFrame;

//------------------------------------------------------------------------------------------------------------------------
//                             1. 1x1-PER-SOURCE GPU SOLAR DISC VISIBILITY REDUCTION
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(1, 1, 1)]
void EvaluateFlareVisibilityMain(uint3 Tid : SV_DispatchThreadID)
{
    uint SourceCount = uint(Ensemble[0].z + 0.5f);
    if (Tid.x >= SourceCount)
    {
        return;
    }

    uint   FlareTable = uint(Ensemble[8].x + 0.5f);
    uint   FB         = FlareTable + Tid.x * kFlareSourceStride;
    float4 F0         = Ensemble[FB + 0u]; // (RealmId, SunIdxInRealm, PortalIdPlusOne, GlobalSunSlot)
    float4 F1         = Ensemble[FB + 1u]; // (CamWorldDirXYZ, AngularRadiusRad)
    float4 F3         = Ensemble[FB + 3u]; // (IrrB, LimbDarkeningU, 0, 0)

    float3 CenterDir  = SafeNormalize(F1.xyz);
    float  AngRad     = max(F1.w, 1.0e-4f);
    float  LimbU      = F3.y;
    int    ReqPortal  = int(F0.z + 0.5f); // 0 = direct in camera realm; >= 1 = must traverse Portal (ReqPortal - 1)

    float3 UpRef      = (abs(CenterDir.y) < 0.94f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 TangentU   = SafeNormalize(cross(UpRef, CenterDir));
    float3 TangentV   = SafeNormalize(cross(CenterDir, TangentU));

    float  TanHalfY   = Ensemble[1].w;
    float3 CamRight   = Ensemble[2].xyz;
    float  Aspect     = Ensemble[2].w;
    float3 CamUp      = Ensemble[3].xyz;
    float3 CamFwd     = Ensemble[4].xyz;
    uint   ViewW      = uint(Ensemble[9].x + 0.5f);
    uint   ViewH      = uint(Ensemble[9].y + 0.5f);

    // 8 radial rings x 8 azimuths = 64 stratified limb-darkening-weighted solar disc visibility rays
    const int kRings  = 8;
    const int kAzim   = 8;
    float3    VisRgb  = float3(0.0f);
    float     WeightT = 0.0f;

    for (int R = 0; R < kRings; ++R)
    {
        float RNorm = sqrt((float(R) + 0.5f) / float(kRings));
        float Theta = RNorm * AngRad;
        float Mu    = sqrt(max(0.0f, 1.0f - RNorm * RNorm));
        float LimbW = 1.0f - LimbU * (1.0f - Mu);

        for (int A = 0; A < kAzim; ++A)
        {
            float  Phi   = kTwoPi * (float(A) + 0.5f * float(R & 1)) / float(kAzim);
            float3 RayD  = SafeNormalize(CenterDir * cos(Theta) + (TangentU * cos(Phi) + TangentV * sin(Phi)) * sin(Theta));
            WeightT     += LimbW;

            float Cz = dot(RayD, CamFwd);
            if (Cz <= 1.0e-4f)
            {
                continue;
            }
            float NdcX = (dot(RayD, CamRight) / Cz) / max(Aspect * TanHalfY, 1.0e-5f);
            float NdcY = (dot(RayD, CamUp) / Cz) / max(TanHalfY, 1.0e-5f);
            float2 Uv  = float2(0.5f + 0.5f * NdcX, 0.5f - 0.5f * NdcY);
            if (Uv.x < 0.0f || Uv.x >= 1.0f || Uv.y < 0.0f || Uv.y >= 1.0f)
            {
                continue;
            }

            uint2 Px       = uint2(min(uint(Uv.x * float(ViewW)), ViewW - 1u),
                                   min(uint(Uv.y * float(ViewH)), ViewH - 1u));
            float RevZ     = Gbuffer[uint3(Px, 0u)].w;
            if (RevZ > 0.0f)
            {
                // Occluded by reversed-Z solid geometry (or portal frame)!
                continue;
            }

            int PixPortal = int(Gbuffer[uint3(Px, 3u)].w + 0.5f);
            if (PixPortal != ReqPortal)
            {
                // Direct realm sun blocked behind a portal, OR portal sun outside its portal window!
                continue;
            }

            float  CloudTr = CloudHistoryTarget[uint3(Px, 1u)].w;
            float3 GlassTr = Gbuffer[uint3(Px, 3u)].rgb;
            VisRgb        += GlassTr * (CloudTr * LimbW);
        }
    }

    float3 NormVis = (WeightT > 0.0f) ? (VisRgb / WeightT) : float3(0.0f);
    float  ScalarV = dot(NormVis, float3(0.2126f, 0.7152f, 0.0722f));
    FlareVisibility[uint3(Tid.x, 0u, 0u)] = float4(NormVis, ScalarV);
}

//------------------------------------------------------------------------------------------------------------------------
//                          2. MULTI-ELEMENT CHROMATIC APERTURE & GHOST COMPOSITE
//------------------------------------------------------------------------------------------------------------------------

float PolygonalGhostMask(float2 Delta, float Radius, float BladeCount, float Roundness, float RotRad)
{
    float R = length(Delta);
    if (R >= Radius * 1.15f)
    {
        return 0.0f;
    }
    float Ang    = atan2(Delta.y, Delta.x) + RotRad;
    float Sector = kTwoPi / max(BladeCount, 3.0f);
    float LocalA = fmod(abs(Ang) + kTwoPi, Sector) - 0.5f * Sector;
    float PolyR  = Radius * cos(0.5f * Sector) / max(cos(LocalA), 0.25f);
    float EffR   = lerp(PolyR, Radius, saturate(Roundness));
    float Edge   = smoothstep(EffR, EffR * 0.78f, R);
    float Rim    = 0.68f + 0.52f * smoothstep(0.35f * EffR, 0.92f * EffR, R);
    return Edge * Rim;
}

float3 EvaluateSingleFlareOptics(StructuredBuffer<float4> Ens,
                                 uint                     GlobalSunIdx,
                                 float2                   ScreenUv,
                                 float2                   SunUv,
                                 float                    Aspect,
                                 float3                   VisTint,
                                 float3                   SunIrr)
{
    uint   SB    = SunSlotBase(Ens, GlobalSunIdx);
    float4 S2    = Ens[SB + 2u]; // UvX, UvY, MasterGain, GhostIntensity
    float4 S3    = Ens[SB + 3u]; // ChromaticShift, HaloIntensity, HaloRadius, HaloWidth
    float4 S4    = Ens[SB + 4u]; // StreakIntensity, StreakLength, StreakAngleRad, AnamorphicIntensity
    float4 S5    = Ens[SB + 5u]; // StarburstIntensity, BladeCount, Roundness, VeilingGlare

    float2 Axis      = float2(0.5f, 0.5f) - SunUv;
    float  OffAxis   = length(float2(Axis.x * Aspect, Axis.y));
    float  EdgeVig   = 1.0f - smoothstep(0.65f, 1.35f, OffAxis);
    if (EdgeVig <= 1.0e-4f)
    {
        return float3(0.0f);
    }

    float2 ToSun = float2((ScreenUv.x - SunUv.x) * Aspect, ScreenUv.y - SunUv.y);
    float  DistS = length(ToSun);

    // 1. Two-lobe PSF solar corona glow + subtle physical veiling glare (LensFlareModel.h scale)
    float  Glow  = 0.028f / (DistS * DistS + 0.0025f) + 0.012f * exp(-DistS * 6.0f);
    float  Veil  = S5.w * (0.003f + 0.006f * exp(-(DistS * DistS) / 1.44f));
    float3 Total = float3(1.05f, 0.96f, 0.84f) * (0.14f * Glow) + float3(1.0f, 0.97f, 0.92f) * Veil;

    // 2. Polygonal N-blade aperture diffraction starburst
    if (S5.x > 0.0f && DistS > 1.0e-4f)
    {
        float Ang    = atan2(ToSun.y, ToSun.x) + S4.z;
        float Spikes = (int(S5.y + 0.5f) % 2 == 1) ? (S5.y * 2.0f) : S5.y;
        float Ray    = pow(abs(cos(0.5f * Spikes * Ang)), 26.0f);
        float Fall   = exp(-DistS * 7.5f) / (1.0f + 18.0f * DistS);
        // Subtle spectral radial dispersion along diffraction spikes
        float3 Spec  = float3(
            0.85f + 0.30f * sin(DistS * 42.0f),
            0.90f + 0.25f * sin(DistS * 42.0f + 2.1f),
            0.95f + 0.30f * sin(DistS * 42.0f + 4.2f));
        Total += Spec * (S5.x * 0.42f * Ray * Fall);
    }

    // 3. Rotatable warm streak + cylindrical blue anamorphic streak
    if (S4.x > 0.0f)
    {
        float  Ca   = cos(S4.z);
        float  Sa   = sin(S4.z);
        float2 Rot  = float2(ToSun.x * Ca + ToSun.y * Sa, -ToSun.x * Sa + ToSun.y * Ca);
        float  Prof = exp(-abs(Rot.y) * 95.0f) * exp(-abs(Rot.x) / max(S4.y, 0.05f));
        Total      += float3(1.12f, 0.88f, 0.62f) * (S4.x * 0.34f * Prof);
    }
    if (S4.w > 0.0f)
    {
        float Ana = exp(-abs(ToSun.y) * 130.0f) * exp(-abs(ToSun.x) * 2.6f);
        Total    += float3(0.46f, 0.76f, 1.28f) * (S4.w * 0.40f * Ana);
    }

    // 4. Antipode spectral halo ring
    if (S3.y > 0.0f)
    {
        float2 AntiUv = float2(0.5f, 0.5f) + Axis * 0.85f;
        float2 DAnti  = float2((ScreenUv.x - AntiUv.x) * Aspect, ScreenUv.y - AntiUv.y);
        float  R      = length(DAnti);
        float  Shift  = S3.x * 1.8f;
        float  Hr     = exp(-pow((R - (S3.z + Shift)) / max(S3.w, 0.008f), 2.0f));
        float  Hg     = exp(-pow((R - S3.z) / max(S3.w, 0.008f), 2.0f));
        float  Hb     = exp(-pow((R - (S3.z - Shift)) / max(S3.w, 0.008f), 2.0f));
        Total        += float3(Hr, Hg, Hb) * (S3.y * 0.22f);
    }

    // 5. 8-element multi-ghost chromatic aberration train along optical axis
    if (S2.w > 0.0f)
    {
        static const float  kGhostT[8]   = {-1.45f, -0.92f, -0.55f, -0.24f, 0.32f, 0.68f, 1.12f, 1.58f};
        static const float  kGhostRad[8] = {0.095f, 0.062f, 0.044f, 0.028f, 0.036f, 0.074f, 0.052f, 0.115f};
        static const float3 kCoatTint[8] = {
            float3(0.55f, 0.85f, 1.15f),
            float3(0.95f, 0.62f, 1.08f),
            float3(0.58f, 1.08f, 0.78f),
            float3(1.10f, 0.88f, 0.56f),
            float3(0.62f, 0.92f, 1.14f),
            float3(1.06f, 0.68f, 0.82f),
            float3(0.72f, 1.08f, 0.66f),
            float3(0.84f, 0.74f, 1.16f),
        };

        [unroll]
        for (int G = 0; G < 8; ++G)
        {
            float2 GCenter = float2(0.5f, 0.5f) + Axis * kGhostT[G];
            float2 Delta   = float2((ScreenUv.x - GCenter.x) * Aspect, ScreenUv.y - GCenter.y);
            float2 ChromV  = SafeNormalize(float3(Axis.x * Aspect, Axis.y, 0.0f)).xy * S3.x;
            float  Mr      = PolygonalGhostMask(Delta - ChromV, kGhostRad[G], S5.y, S5.z, S4.z);
            float  Mg      = PolygonalGhostMask(Delta, kGhostRad[G], S5.y, S5.z, S4.z);
            float  Mb      = PolygonalGhostMask(Delta + ChromV, kGhostRad[G], S5.y, S5.z, S4.z);
            Total         += float3(Mr, Mg, Mb) * kCoatTint[G] * (S2.w * 0.16f);
        }
    }

    return Total * VisTint * SunIrr * (S2.z * EdgeVig);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CompositeFlareApertureMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    uint SourceCount = uint(Ensemble[0].z + 0.5f);
    if (SourceCount == 0u)
    {
        return;
    }

    float2 ScreenUv   = (float2(Tid.xy) + 0.5f) / float2(Push.Counts.xy);
    float  Aspect     = Ensemble[2].w;
    uint   FlareTable = uint(Ensemble[8].x + 0.5f);

    float3 FlareSum = float3(0.0f);
    for (uint I = 0u; I < SourceCount; ++I)
    {
        float4 Vis = FlareVisibility[uint3(I, 0u, 0u)];
        if (Vis.w <= 1.0e-4f)
        {
            continue;
        }
        uint   FB        = FlareTable + I * kFlareSourceStride;
        uint   GlobalSun = uint(Ensemble[FB + 0u].w + 0.5f);
        float4 F2        = Ensemble[FB + 2u];
        float4 F3        = Ensemble[FB + 3u];
        float2 SunUv     = F2.xy;
        float3 SunIrr    = float3(F2.z, F2.w, F3.x);
        FlareSum        += EvaluateSingleFlareOptics(Ensemble, GlobalSun, ScreenUv, SunUv, Aspect, Vis.rgb, SunIrr);
    }

    if (dot(FlareSum, float3(1.0f)) > 0.0f)
    {
        float3 Cur = HdrFrame[uint3(Tid.xy, 0u)].rgb;
        HdrFrame[uint3(Tid.xy, 0u)] = float4(min(Cur + FlareSum, float3(60000.0f)), 1.0f);
    }
}
