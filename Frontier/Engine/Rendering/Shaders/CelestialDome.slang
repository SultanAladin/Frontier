//==========================================================================================================================================
//                                                    CELESTIAL DOME SLANG
//==========================================================================================================================================
// Multi-realm sky-view + 6-pt twilight coupling, Hipparcos octahedral starfield + Kolmogorov scintillation + Milky Way,
// 2048x1024x6 sRGB 6-body lunar sphere atlas (phase, earthshine, libration), 3D ringed/atmospheric planets, and multi-sun discs.

#include "SolarSkyEnvironment.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4> Ensemble;
[[vk::binding(1, 0)]] StructuredBuffer<uint2>  StarCellSpans; // 1024 cells: (StartOffset, Count)
[[vk::binding(2, 0)]] StructuredBuffer<float4> StarRecords;   // 2x float4 per star: (DirX, DirY, DirZ, VMag), (TintR, TintG, TintB, Seed)
[[vk::binding(3, 0)]] Sampler2DArray<float4>   TransLut;
[[vk::binding(4, 0)]] Sampler2DArray<float4>   MultiLut;
[[vk::binding(5, 0)]] Sampler2DArray<float4>   SkyViewLut;
[[vk::binding(6, 0)]] Sampler2DArray<float4>   LunarAtlas;    // 2048x1024x6 VK_FORMAT_R8G8B8A8_SRGB
[[vk::binding(7, 0)]] [format("rgba16f")] RWTexture2DArray<float4> Gbuffer;
[[vk::binding(8, 0)]] [format("rgba16f")] RWTexture2DArray<float4> HdrFrame;

static const uint kOctGridDim = 32u;

float2 DirectionToOctUv(float3 D)
{
    float InvL1 = 1.0f / max(abs(D.x) + abs(D.y) + abs(D.z), 1.0e-7f);
    float Ox    = D.x * InvL1;
    float Oy    = D.y * InvL1;
    if (D.z < 0.0f)
    {
        float Tx = (1.0f - abs(Oy)) * ((Ox >= 0.0f) ? 1.0f : -1.0f);
        float Ty = (1.0f - abs(Ox)) * ((Oy >= 0.0f) ? 1.0f : -1.0f);
        Ox       = Tx;
        Oy       = Ty;
    }
    return saturate(float2(0.5f * Ox + 0.5f, 0.5f * Oy + 0.5f));
}

float EvaluateScintillationGpu(float SinAlt, float Seed, float TimeSec, float Strength)
{
    if (Strength <= 0.0f)
    {
        return 1.0f;
    }
    float ClampedSin = max(0.035f, SinAlt);
    float Airmass    = 1.0f / (ClampedSin + 0.025f * exp(-11.0f * ClampedSin));
    float RmsAmp     = min(0.65f, 0.055f * Strength * pow(Airmass, 0.85f));
    float T          = TimeSec;
    float Wave       = 0.50f * sin(T * 7.3f + Seed * 6.28318f) +
                       0.31f * sin(T * 13.7f + Seed * 19.1f) +
                       0.19f * cos(T * 23.1f + Seed * 41.7f);
    return max(0.10f, 1.0f + RmsAmp * Wave);
}

float3 EvaluateMilkyWayGpu(float3 CelestialDir, float Intensity)
{
    if (Intensity <= 0.0f)
    {
        return float3(0.0f);
    }
    float3 GalNorth  = float3(-0.867666f, -0.198076f, 0.455984f);
    float3 GalCenter = float3(-0.054876f, -0.873437f, -0.483835f);
    float  SinLat    = dot(CelestialDir, GalNorth);
    float  Band      = exp(-pow(SinLat / 0.16f, 2.0f));
    if (Band < 1.0e-4f)
    {
        return float3(0.0f);
    }
    float Core   = 0.45f + 0.85f * pow(0.5f + 0.5f * dot(CelestialDir, GalCenter), 2.2f);
    float Cloud  = 0.65f + 0.70f * FbmValue3D(CelestialDir * 6.5f, 4, 307u);
    float Rift   = exp(-pow(SinLat / 0.045f, 2.0f)) * smoothstep(0.42f, 0.78f, FbmValue3D(CelestialDir * 11.0f + float3(3.1f, -1.7f, 2.4f), 4, 911u));
    float Lum    = Intensity * 4.6e-3f * Band * Core * Cloud * (1.0f - 0.68f * Rift);
    return float3(Lum * 1.04f, Lum * 0.99f, Lum * 1.18f);
}

float3 EvaluatePlanetGpu(StructuredBuffer<float4> Ens,
                         uint                     PB,
                         uint                     SunFirst,
                         uint                     SunCount,
                         float3                   ViewDir,
                         float                    PixelSpanRad,
                         out float                OutAlpha)
{
    OutAlpha         = 0.0f;
    float4 P0        = Ens[PB + 0u];
    float4 P1        = Ens[PB + 1u];
    float4 P2        = Ens[PB + 2u];
    float4 P3        = Ens[PB + 3u];
    float4 P4        = Ens[PB + 4u];
    float4 P5        = Ens[PB + 5u];

    float3 CenterDir = P0.xyz;
    float  AngRad    = max(P0.w, 1.0e-5f);
    float  MaxReach  = max(1.0f + P3.w, P4.w) * 1.15f;
    if (dot(ViewDir, CenterDir) < cos(min(kPi, AngRad * MaxReach + PixelSpanRad * 3.0f)))
    {
        return float3(0.0f);
    }

    float  Dist      = 1.0f / sin(min(AngRad, 1.2f));
    float3 PPos      = CenterDir * Dist;
    float3 Axis      = P1.xyz;
    float3 RefX      = (abs(Axis.y) < 0.9f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 EqX       = SafeNormalize(cross(RefX, Axis));
    float3 EqZ       = SafeNormalize(cross(Axis, EqX));

    float  BodyNear, BodyFar;
    float3 Oc     = -PPos;
    float  B      = dot(Oc, ViewDir);
    float  C      = dot(Oc, Oc) - 1.0f;
    float  Det    = B * B - C;
    bool   HitSph = (Det >= 0.0f) && ((-B - sqrt(max(Det, 0.0f))) > 0.0f);
    BodyNear      = HitSph ? (-B - sqrt(max(Det, 0.0f))) : 1.0e30f;

    // Ring intersection
    bool   HitRing = false;
    float  RingT   = 1.0e30f;
    float  RingAlpha = 0.0f;
    float3 RingCol = float3(0.0f);
    if (P4.w > P4.z && P5.x > 0.0f)
    {
        float Denom = dot(ViewDir, Axis);
        if (abs(Denom) > 1.0e-6f)
        {
            float Tr = dot(PPos, Axis) / Denom;
            if (Tr > 0.0f)
            {
                float3 RHit = ViewDir * Tr - PPos;
                float  Rad  = length(RHit);
                if (Rad >= P4.z && Rad <= P4.w)
                {
                    float U     = (Rad - P4.z) / max(P4.w - P4.z, 1.0e-4f);
                    float Gap   = smoothstep(0.02f, 0.08f, abs(U - 0.56f));
                    float Fine  = 0.65f + 0.35f * sin(U * 95.0f) * cos(U * 37.0f);
                    RingAlpha   = saturate(P5.x * Gap * Fine);
                    if (RingAlpha > 1.0e-4f)
                    {
                        HitRing       = true;
                        RingT         = Tr;
                        float3 BaseRc = P5.yzw * (0.82f + 0.30f * U);
                        for (uint S = 0u; S < SunCount; ++S)
                        {
                            uint   SB     = SunSlotBase(Ens, SunFirst + S);
                            float3 SunDir = Ens[SB + 0u].xyz;
                            float3 SunIrr = Ens[SB + 1u].xyz;
                            // Planet-on-ring shadow check!
                            float  Sb     = dot(RHit, SunDir);
                            float  Sc     = dot(RHit, RHit) - 1.0f;
                            float  Sdet   = Sb * Sb - Sc;
                            bool   InShad = (Sdet > 0.0f) && ((-Sb - sqrt(Sdet)) > 0.0f);
                            if (!InShad)
                            {
                                float Tilt = 0.35f + 0.65f * abs(dot(Axis, SunDir));
                                RingCol   += BaseRc * SunIrr * (0.14f * Tilt);
                            }
                        }
                    }
                }
            }
        }
    }

    float3 BodyCol = float3(0.0f);
    if (HitSph)
    {
        float3 Nrm  = SafeNormalize(ViewDir * BodyNear - PPos);
        float  Lat  = asin(clamp(dot(Nrm, Axis), -1.0f, 1.0f));
        float  Lon  = atan2(dot(Nrm, EqZ), dot(Nrm, EqX)) + P1.w;
        uint   Arch = uint(P4.y + 0.5f);

        float3 Alb = P2.xyz;
        if (Arch == 0u) // BandedGasGiant
        {
            float Zones = 0.5f + 0.5f * sin(Lat * 18.0f + 0.35f * sin(Lon * 4.0f + Lat * 9.0f));
            Alb *= lerp(float3(0.76f, 0.68f, 0.58f), float3(1.16f, 1.10f, 1.02f), Zones);
        }
        else if (Arch == 1u) // OceanicTerrestrial
        {
            float3 Q   = float3(cos(Lat) * cos(Lon), sin(Lat), cos(Lat) * sin(Lon));
            float  Cnt = FbmValue3D(Q * 3.4f, 4, 509u);
            float  Cld = smoothstep(0.52f, 0.78f, FbmValue3D(Q * 5.8f + float3(1.7f, 0.4f, -1.2f), 4, 811u));
            Alb        = (Cnt > 0.53f) ? float3(0.24f, 0.34f, 0.18f) : P2.xyz;
            Alb        = lerp(Alb, float3(0.92f, 0.94f, 0.97f), 0.72f * Cld);
        }
        else if (Arch == 2u) // RustyDesertWorld
        {
            float3 Q = float3(cos(Lat) * cos(Lon), sin(Lat), cos(Lat) * sin(Lon));
            Alb     *= (0.72f + 0.48f * FbmValue3D(Q * 4.2f, 4, 613u));
        }
        else // MethaneIceGiant
        {
            Alb *= (0.90f + 0.14f * cos(Lat * 8.0f));
        }

        float NdotV = max(0.02f, dot(Nrm, -ViewDir));
        for (uint S = 0u; S < SunCount; ++S)
        {
            uint   SB     = SunSlotBase(Ens, SunFirst + S);
            float3 SunDir = Ens[SB + 0u].xyz;
            float3 SunIrr = Ens[SB + 1u].xyz;
            float  NdotL  = max(0.0f, dot(Nrm, SunDir));
            if (NdotL <= 0.0f)
            {
                continue;
            }

            // Ring-on-planet shadow check!
            float RingTrans = 1.0f;
            if (P4.w > P4.z && P5.x > 0.0f)
            {
                float Den = dot(SunDir, Axis);
                if (abs(Den) > 1.0e-5f)
                {
                    float Trs = -dot(Nrm, Axis) / Den;
                    if (Trs > 0.0f)
                    {
                        float RRad = length(Nrm + SunDir * Trs);
                        if (RRad >= P4.z && RRad <= P4.w)
                        {
                            float U   = (RRad - P4.z) / max(P4.w - P4.z, 1.0e-4f);
                            float Gap = smoothstep(0.02f, 0.08f, abs(U - 0.56f));
                            RingTrans = 1.0f - saturate(P5.x * Gap);
                        }
                    }
                }
            }
            float Limb = pow(NdotL, 0.82f) * pow(NdotV, 0.14f);
            BodyCol   += Alb * (P2.w / kPi) * SunIrr * (0.28f * Limb * RingTrans);
        }
        BodyCol += Alb * 2.5e-4f;
    }

    // Spherical planetary atmosphere shell glow!
    float3 AtmoGlow = float3(0.0f);
    if (P3.w > 0.0f && P4.x > 0.0f)
    {
        float AtmoR = 1.0f + P3.w;
        float Adet  = B * B - (dot(Oc, Oc) - AtmoR * AtmoR);
        if (Adet > 0.0f)
        {
            float T0   = max(0.0f, -B - sqrt(Adet));
            float T1   = HitSph ? min(-B + sqrt(Adet), BodyNear) : (-B + sqrt(Adet));
            float Plen = max(0.0f, T1 - T0);
            if (Plen > 0.0f)
            {
                float3 Mid   = ViewDir * (0.5f * (T0 + T1)) - PPos;
                float3 MidUp = SafeNormalize(Mid);
                float  Opt   = P4.x * Plen * exp(-max(0.0f, (length(Mid) - 1.0f) / max(P3.w * 0.45f, 1.0e-3f)));
                for (uint S = 0u; S < SunCount; ++S)
                {
                    uint   SB     = SunSlotBase(Ens, SunFirst + S);
                    float3 SunDir = Ens[SB + 0u].xyz;
                    float3 SunIrr = Ens[SB + 1u].xyz;
                    float  Lit    = smoothstep(-0.22f, 0.35f, dot(MidUp, SunDir));
                    AtmoGlow     += P3.xyz * SunIrr * (0.09f * Opt * Lit);
                }
            }
        }
    }

    float3 Combined = float3(0.0f);
    if (HitSph && HitRing)
    {
        if (RingT < BodyNear)
        {
            Combined = RingCol * RingAlpha + (BodyCol + AtmoGlow) * (1.0f - RingAlpha);
        }
        else
        {
            Combined = BodyCol + AtmoGlow;
        }
        OutAlpha = 1.0f;
    }
    else if (HitSph)
    {
        Combined = BodyCol + AtmoGlow;
        OutAlpha = 1.0f;
    }
    else if (HitRing)
    {
        Combined = RingCol * RingAlpha + AtmoGlow;
        OutAlpha = RingAlpha;
    }
    else
    {
        Combined = AtmoGlow;
        OutAlpha = 0.0f;
    }
    return Combined;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void RasterizeCelestialDomeMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    float4 Gb0 = Gbuffer[uint3(Tid.xy, 0u)];
    if (Gb0.w > 0.0f)
    {
        // Opaque geometry hit: initialize HdrFrame with shaded surface radiance (VolumetricClouds & VolumetricFog composite next)
        HdrFrame[uint3(Tid.xy, 0u)] = float4(Gb0.rgb, 1.0f);
        return;
    }

    uint   RealmId   = uint(Gbuffer[uint3(Tid.xy, 1u)].w + 0.5f);
    float3 ViewDir   = SafeNormalize(Gbuffer[uint3(Tid.xy, 2u)].xyz);
    float3 GlassTint = Gbuffer[uint3(Tid.xy, 3u)].rgb;

    uint   RBase     = RealmSlotBase(Ensemble, RealmId);
    float4 R0        = Ensemble[RBase + 0u];
    float4 R5        = Ensemble[RBase + 5u];
    float4 R8        = Ensemble[RBase + 8u];
    float4 R9        = Ensemble[RBase + 9u];
    float  BottomR   = R0.x;
    float  TopR      = R0.y;
    float  ObsR      = BottomR + max(R0.z, 1.0e-3f);

    uint   SunFirst    = uint(R8.x + 0.5f);
    uint   SunCount    = uint(R8.y + 0.5f);
    uint   MoonFirst   = uint(R8.z + 0.5f);
    uint   MoonCount   = uint(R8.w + 0.5f);
    uint   PlanetFirst = uint(R9.x + 0.5f);
    uint   PlanetCount = uint(R9.y + 0.5f);

    // 1. Coupled Solar + Sky + Twilight scattered atmosphere (SkyViewLut for single-sun fast path; exact 6-pt GL for multi-sun!)
    float3 SkyScattering = float3(0.0f);
    if (SunCount == 1u)
    {
        float3 PrimSun = Ensemble[SunSlotBase(Ensemble, SunFirst)].xyz;
        float2 SkyUv   = DirectionToSkyViewUv(BottomR, ObsR, ViewDir, PrimSun);
        SkyScattering  = SkyViewLut.SampleLevel(float3(SkyUv, float(RealmId)), 0.0f).rgb;
    }
    else if (SunCount > 1u)
    {
        SkyScattering = EvaluateCoupledSolarSky6Pt(Ensemble, TransLut, MultiLut, RealmId, ViewDir, 0.0f);
    }

    float3 SkyTrans = (ViewDir.y > -0.08f)
                          ? SampleTransmittanceLutArray(TransLut, RealmId, BottomR, TopR, ObsR, ViewDir.y)
                          : float3(0.0f);

    float  TanHalfY     = Ensemble[1].w;
    float  PixelSpanRad = (2.0f * TanHalfY) / max(float(Push.Counts.y), 1.0f);
    float  TimeSec      = Ensemble[3].w;

    // 2. Transform ViewDir into Realm's celestial star frame
    float3 Row0         = Ensemble[RBase + 14u].xyz;
    float3 Row1         = Ensemble[RBase + 15u].xyz;
    float3 Row2         = SafeNormalize(cross(Row0, Row1));
    float3 CelestialDir = SafeNormalize(float3(dot(Row0, ViewDir), dot(Row1, ViewDir), dot(Row2, ViewDir)));
    float  StarLimMag   = Ensemble[RBase + 15u].w;

    float3 ExoRadiance = EvaluateMilkyWayGpu(CelestialDir, R5.w);

    // 3. Cell-indexed Hipparcos stars (3x3 octahedral neighborhood) + Kolmogorov scintillation
    if (R5.y > 0.0f && ViewDir.y > -0.02f && uint(Ensemble[8].z + 0.5f) > 0u)
    {
        float2 OctUv = DirectionToOctUv(CelestialDir);
        int    Cx0   = clamp(int(OctUv.x * float(kOctGridDim)), 0, int(kOctGridDim) - 1);
        int    Cy0   = clamp(int(OctUv.y * float(kOctGridDim)), 0, int(kOctGridDim) - 1);
        float  PsfSig = max(0.85f * PixelSpanRad, 3.5e-4f);
        float  Cutoff = 3.6f * PsfSig;

        for (int Dy = -1; Dy <= 1; ++Dy)
        {
            int Cy = Cy0 + Dy;
            if (Cy < 0 || Cy >= int(kOctGridDim))
            {
                continue;
            }
            for (int Dx = -1; Dx <= 1; ++Dx)
            {
                int Cx = Cx0 + Dx;
                if (Cx < 0 || Cx >= int(kOctGridDim))
                {
                    continue;
                }
                uint2 Span = StarCellSpans[uint(Cy) * kOctGridDim + uint(Cx)];
                for (uint K = 0u; K < Span.y; ++K)
                {
                    uint   StIdx = (Span.x + K) * 2u;
                    float4 St0   = StarRecords[StIdx + 0u];
                    if (St0.w > StarLimMag)
                    {
                        continue;
                    }
                    float CosAng = dot(CelestialDir, St0.xyz);
                    if (CosAng <= 0.998f)
                    {
                        continue;
                    }
                    float3 CrossV = cross(CelestialDir, St0.xyz);
                    float  Theta  = atan2(length(CrossV), CosAng);
                    if (Theta > Cutoff)
                    {
                        continue;
                    }
                    float4 St1   = StarRecords[StIdx + 1u];
                    float  Flux  = pow(10.0f, -0.34f * (St0.w - 0.5f)) * 0.28f * R5.y;
                    float  Core  = exp(-0.5f * (Theta * Theta) / (PsfSig * PsfSig));
                    float  Scint = EvaluateScintillationGpu(ViewDir.y, St1.w, TimeSec, R5.z);
                    ExoRadiance += St1.xyz * (Flux * Core * Scint);
                }
            }
        }
    }

    // Custom 6-blade Airy stars
    uint CStarTable = uint(Ensemble[8].y + 0.5f);
    uint CStarFirst = uint(Ensemble[RBase + 13u].x + 0.5f);
    uint CStarCount = uint(Ensemble[RBase + 13u].y + 0.5f);
    for (uint C = 0u; C < CStarCount; ++C)
    {
        uint   CB  = CStarTable + (CStarFirst + C) * kCustomStarStride;
        float4 Cs0 = Ensemble[CB + 0u];
        float4 Cs1 = Ensemble[CB + 1u];
        float  CosA = dot(ViewDir, Cs0.xyz);
        if (CosA > 0.996f)
        {
            float  Theta = atan2(length(cross(ViewDir, Cs0.xyz)), CosA);
            float  Sig   = max(1.1f * PixelSpanRad, 4.0e-4f);
            float  Flux  = pow(10.0f, -0.4f * (Cs0.w + 1.5f)) * 0.045f;
            float  Core  = exp(-0.5f * (Theta * Theta) / (Sig * Sig));
            ExoRadiance += Cs1.xyz * (Flux * Core);
        }
    }

    // 4. Exogenous 3D ringed & atmospheric planets (occlude stars behind them!)
    uint PlanetTable = uint(Ensemble[5].w + 0.5f);
    for (uint P = 0u; P < PlanetCount; ++P)
    {
        float  PAlpha = 0.0f;
        float3 PCol   = EvaluatePlanetGpu(Ensemble, PlanetTable + (PlanetFirst + P) * kPlanetStride, SunFirst, SunCount, ViewDir, PixelSpanRad, PAlpha);
        ExoRadiance   = ExoRadiance * (1.0f - PAlpha) + PCol;
    }

    // 5. 6-Body 2048x1024 sRGB Lunar Atlas spheres (phase, Lommel-Seeliger, earthshine, libration)
    uint MoonTable = uint(Ensemble[5].z + 0.5f);
    for (uint M = 0u; M < MoonCount; ++M)
    {
        uint   MB      = MoonTable + (MoonFirst + M) * kMoonStride;
        float4 M0      = Ensemble[MB + 0u];
        float4 M1      = Ensemble[MB + 1u];
        float4 M2      = Ensemble[MB + 2u];
        float4 M3      = Ensemble[MB + 3u];
        float3 MDir    = M0.xyz;
        float  MAngRad = max(M0.w, 1.0e-5f);

        float  Theta = atan2(length(cross(ViewDir, MDir)), dot(ViewDir, MDir));
        float  Cov   = smoothstep(MAngRad + 0.5f * PixelSpanRad, max(0.0f, MAngRad - 0.5f * PixelSpanRad), Theta);
        if (Cov <= 0.0f)
        {
            continue;
        }

        float  Dist = 1.0f / sin(min(MAngRad, 1.2f));
        float3 MPos = MDir * Dist;
        float3 Oc   = -MPos;
        float  B    = dot(Oc, ViewDir);
        float  Det  = max(0.0f, B * B - (dot(Oc, Oc) - 1.0f));
        float3 Nrm  = SafeNormalize(ViewDir * (-B - sqrt(Det)) - MPos);

        float3 UpRef = (abs(MDir.y) < 0.94f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        float3 MRight = SafeNormalize(cross(UpRef, MDir));
        float3 MUp    = SafeNormalize(cross(MDir, MRight));
        float3 MFront = -MDir;

        float  Lx  = dot(Nrm, MRight);
        float  Ly  = dot(Nrm, MUp);
        float  Lz  = dot(Nrm, MFront);
        float  Lon = atan2(Lx, Lz) + M2.z;
        float  Lat = asin(clamp(Ly, -1.0f, 1.0f)) + M2.w;
        float2 Uv  = float2(frac(Lon / kTwoPi + 0.5f), saturate(0.5f - Lat / kPi));

        uint   Layer = min(uint(M3.x + 0.5f), 5u);
        float3 Alb   = LunarAtlas.SampleLevel(float3(Uv, float(Layer)), 0.0f).rgb * M1.xyz;
        float  Mu    = max(1.0e-3f, dot(Nrm, -ViewDir));
        float3 Lit   = float3(0.0f);

        for (uint S = 0u; S < SunCount; ++S)
        {
            uint   SB     = SunSlotBase(Ensemble, SunFirst + S);
            float3 SunDir = Ensemble[SB + 0u].xyz;
            float3 SunIrr = Ensemble[SB + 1u].xyz;
            float  Mu0    = max(0.0f, dot(Nrm, SunDir));
            if (Mu0 > 0.0f)
            {
                float Ls    = Mu0 / (Mu0 + Mu);
                float Phase = acos(clamp(dot(SunDir, -ViewDir), -1.0f, 1.0f));
                float Opp   = 1.0f + 0.42f * exp(-Phase / 0.14f);
                float PhCurve = exp(-1.10f * Phase) * Opp;
                Lit += Alb * SunIrr * (M1.w * 2.4f * Ls * (0.35f + 0.65f * PhCurve) * M3.y);
            }
        }
        // Earthshine on the unlit hemisphere
        Lit += Alb * float3(0.58f, 0.72f, 1.0f) * (M2.x * (0.4f + 0.6f * Mu));
        ExoRadiance = ExoRadiance * (1.0f - Cov) + Lit * Cov;
    }

    // 6. Finite-pixel limb-darkened multi-sun discs
    for (uint S = 0u; S < SunCount; ++S)
    {
        uint   SB     = SunSlotBase(Ensemble, SunFirst + S);
        float4 S0     = Ensemble[SB + 0u];
        float4 S1     = Ensemble[SB + 1u];
        float3 SunDir = S0.xyz;
        float  AngR   = max(S0.w, 1.0e-5f);
        float  Theta  = atan2(length(cross(ViewDir, SunDir)), dot(ViewDir, SunDir));
        float  HalfPx = 0.5f * PixelSpanRad;
        if (Theta < AngR + HalfPx)
        {
            float Cov  = smoothstep(AngR + HalfPx, max(0.0f, AngR - HalfPx), Theta);
            float RNorm = min(Theta / AngR, 0.999f);
            float Mu   = sqrt(max(0.0f, 1.0f - RNorm * RNorm));
            float Limb = (1.0f - S1.w * (1.0f - Mu)) / max(1.0f - S1.w / 3.0f, 1.0e-3f);
            // Scale disc radiance smoothly for HDR presentation without FP16 overflow
            float3 DiscRad = S1.xyz * (18.0f * Limb * Cov);
            ExoRadiance   += DiscRad;
        }
    }

    float3 TotalSky = (SkyScattering + ExoRadiance * SkyTrans) * GlassTint;
    HdrFrame[uint3(Tid.xy, 0u)] = float4(min(TotalSky, float3(60000.0f)), 1.0f);
}
