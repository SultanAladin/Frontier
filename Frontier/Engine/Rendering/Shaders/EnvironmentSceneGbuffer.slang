//==========================================================================================================================================
//                                            ENVIRONMENT SCENE GBUFFER SLANG
//==========================================================================================================================================
// Routes primary camera rays across PortalApertures into (RealmId, RayOrigin, RayDir), evaluates reversed-Z depth,
// dielectric glass transmission masks, and shades opaque surfaces using CloudShadowArray + TransmittanceLut + lunar bounce.

#include "SolarSkyEnvironment.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4> Ensemble;
[[vk::binding(1, 0)]] Sampler2DArray<float4>   TransLut;
[[vk::binding(2, 0)]] Sampler2DArray<float4>   CloudShadowLut;
[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture2DArray<float4> GbufferOut;

bool RaySphereNearest(float3 O, float3 D, float3 C, float R, out float OutT)
{
    float3 Oc  = O - C;
    float  B   = dot(Oc, D);
    float  C0  = dot(Oc, Oc) - R * R;
    float  Det = B * B - C0;
    OutT       = 0.0f;
    if (Det < 0.0f)
    {
        return false;
    }
    float S  = sqrt(Det);
    float T0 = -B - S;
    float T1 = -B + S;
    if (T0 > 1.0e-3f)
    {
        OutT = T0;
        return true;
    }
    if (T1 > 1.0e-3f)
    {
        OutT = T1;
        return true;
    }
    return false;
}

float NearestRealmOpaqueHit(StructuredBuffer<float4> Ens,
                            uint                     RealmId,
                            float3                   Origin,
                            float3                   Dir,
                            out float3               OutNormal,
                            out float3               OutAlbedo)
{
    uint   RBase     = RealmSlotBase(Ens, RealmId);
    float  GroundY   = Ens[RBase + 4u].y;
    float3 GndAlbedo = float3(Ens[RBase + 4u].z, Ens[RBase + 4u].w, Ens[RBase + 5u].x);

    float BestT = 1.0e30f;
    OutNormal   = float3(0.0f, 1.0f, 0.0f);
    OutAlbedo   = GndAlbedo;

    if (Dir.y < -1.0e-5f)
    {
        float Tg = (GroundY - Origin.y) / Dir.y;
        if (Tg > 1.0e-3f)
        {
            BestT     = Tg;
            float3 Hp = Origin + Dir * Tg;
            // Subtle procedural grid modulation on ground so shadows, portals & fog depth cues read crisply
            float2 Cell = floor(Hp.xz * 0.25f);
            float  Chk  = fmod(abs(Cell.x + Cell.y), 2.0f);
            OutAlbedo   = GndAlbedo * lerp(0.86f, 1.14f, Chk);
        }
    }

    uint SphTable = uint(Ens[7].y + 0.5f);
    uint SphFirst = uint(Ens[RBase + 11u].z + 0.5f);
    uint SphCount = uint(Ens[RBase + 11u].w + 0.5f);

    for (uint I = 0u; I < SphCount; ++I)
    {
        uint   SB = SphTable + (SphFirst + I) * kSphereStride;
        float4 S0 = Ens[SB + 0u];
        float4 S1 = Ens[SB + 1u];
        float  Ts;
        if (RaySphereNearest(Origin, Dir, S0.xyz, S0.w, Ts) && Ts < BestT)
        {
            BestT     = Ts;
            OutNormal = SafeNormalize((Origin + Dir * Ts) - S0.xyz);
            OutAlbedo = S1.xyz;
        }
    }
    return BestT;
}

bool IsShadowedBySpheres(StructuredBuffer<float4> Ens, uint RealmId, float3 Pos, float3 LightDir)
{
    uint RBase    = RealmSlotBase(Ens, RealmId);
    uint SphTable = uint(Ens[7].y + 0.5f);
    uint SphFirst = uint(Ens[RBase + 11u].z + 0.5f);
    uint SphCount = uint(Ens[RBase + 11u].w + 0.5f);
    for (uint I = 0u; I < SphCount; ++I)
    {
        uint   SB = SphTable + (SphFirst + I) * kSphereStride;
        float4 S0 = Ens[SB + 0u];
        float  Ts;
        if (RaySphereNearest(Pos + LightDir * 0.02f, LightDir, S0.xyz, S0.w, Ts))
        {
            return true;
        }
    }
    return false;
}

float3 EvaluateGlassTransmission(StructuredBuffer<float4> Ens, uint RealmId, float3 Origin, float3 Dir, float MaxT)
{
    uint   RBase    = RealmSlotBase(Ens, RealmId);
    uint   GlsTable = uint(Ens[7].z + 0.5f);
    uint   GlsFirst = uint(Ens[RBase + 12u].x + 0.5f);
    uint   GlsCount = uint(Ens[RBase + 12u].y + 0.5f);
    float3 Tint     = float3(1.0f, 1.0f, 1.0f);

    for (uint I = 0u; I < GlsCount; ++I)
    {
        uint   GB    = GlsTable + (GlsFirst + I) * kGlassStride;
        float4 G0    = Ens[GB + 0u];
        float4 G1    = Ens[GB + 1u];
        float4 G2    = Ens[GB + 2u];
        float  Denom = dot(Dir, G1.xyz);
        if (abs(Denom) <= 1.0e-5f)
        {
            continue;
        }
        float T = dot(G0.xyz - Origin, G1.xyz) / Denom;
        if (T <= 1.0e-3f || T >= MaxT)
        {
            continue;
        }
        float3 Hit   = Origin + Dir * T;
        float3 Rel   = Hit - G0.xyz;
        float3 UpRef = (abs(G1.y) < 0.95f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        float3 Right = SafeNormalize(cross(UpRef, G1.xyz));
        float3 Up    = SafeNormalize(cross(G1.xyz, Right));
        if (abs(dot(Rel, Right)) <= G0.w && abs(dot(Rel, Up)) <= G1.w)
        {
            float CosI    = abs(Denom);
            float Fresnel = G2.w + (1.0f - G2.w) * pow(1.0f - CosI, 5.0f);
            Tint         *= G2.xyz * (1.0f - Fresnel);
        }
    }
    return Tint;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void RasterizeRealmGbufferMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    uint   RealmCount  = uint(Ensemble[0].x + 0.5f);
    uint   PortalCount = uint(Ensemble[0].y + 0.5f);
    uint   StartRealm  = min(uint(Ensemble[0].w + 0.5f), max(RealmCount, 1u) - 1u);
    float3 CamOrigin   = Ensemble[1].xyz;
    float2 ScreenUv    = (float2(Tid.xy) + 0.5f) / float2(Push.Counts.xy);
    float3 CamDir      = CameraRayDirection(Ensemble, ScreenUv);

    float3 SolidNrm, SolidAlb;
    float  PrimarySolidT = NearestRealmOpaqueHit(Ensemble, StartRealm, CamOrigin, CamDir, SolidNrm, SolidAlb);

    uint   ActiveRealm   = StartRealm;
    int    ActivePortal  = -1;
    bool   HitFrame      = false;
    float3 FrameAlb      = float3(0.0f);
    float  BestPortalT   = PrimarySolidT;
    float3 RoutedOrigin  = CamOrigin;
    float3 RoutedDir     = CamDir;

    uint PortalTable = uint(Ensemble[7].w + 0.5f);
    for (uint PIdx = 0u; PIdx < PortalCount; ++PIdx)
    {
        uint   PB  = PortalTable + PIdx * kPortalStride;
        float4 P0  = Ensemble[PB + 0u];
        uint   Src = uint(P0.x + 0.5f);
        uint   Dst = uint(P0.y + 0.5f);
        if (Src != StartRealm || Dst >= RealmCount)
        {
            continue;
        }
        float4 P1    = Ensemble[PB + 1u];
        float4 P2    = Ensemble[PB + 2u];
        float4 P3    = Ensemble[PB + 3u];
        float4 P4    = Ensemble[PB + 4u];
        float  Denom = dot(CamDir, P2.xyz);
        if (abs(Denom) <= 1.0e-5f)
        {
            continue;
        }
        float T = dot(P1.xyz - CamOrigin, P2.xyz) / Denom;
        if (T <= 1.0e-3f || T >= BestPortalT)
        {
            continue;
        }
        float3 Rel    = (CamOrigin + CamDir * T) - P1.xyz;
        float  Lx     = dot(Rel, P3.xyz);
        float  Ly     = dot(Rel, P4.xyz);
        float  OuterW = P0.z + P1.w;
        float  OuterH = P0.w + P1.w;
        if (abs(Lx) > OuterW || abs(Ly) > OuterH)
        {
            continue;
        }

        BestPortalT  = T;
        ActivePortal = int(PIdx);
        if (abs(Lx) > P0.z || abs(Ly) > P0.w)
        {
            ActiveRealm  = StartRealm;
            HitFrame     = true;
            FrameAlb     = float3(P2.w, P3.w, P4.w);
            RoutedOrigin = CamOrigin;
            RoutedDir    = CamDir;
        }
        else
        {
            float3 TgtOrigin = Ensemble[PB + 5u].xyz;
            float3 TgtRight  = Ensemble[PB + 6u].xyz;
            float3 TgtUp     = Ensemble[PB + 7u].xyz;
            float3 TgtFwd    = Ensemble[PB + 8u].xyz;
            float3 Inward    = -P2.xyz;

            ActiveRealm  = Dst;
            HitFrame     = false;
            RoutedOrigin = TgtOrigin + TgtRight * Lx + TgtUp * Ly;
            RoutedDir    = SafeNormalize(
                TgtRight * dot(CamDir, P3.xyz) +
                TgtUp * dot(CamDir, P4.xyz) +
                TgtFwd * dot(CamDir, Inward));
        }
    }

    float  SurfaceT   = PrimarySolidT;
    float3 SurfaceNrm = SolidNrm;
    float3 SurfaceAlb = SolidAlb;

    if (HitFrame)
    {
        SurfaceT   = BestPortalT;
        SurfaceNrm = -SafeNormalize(Ensemble[PortalTable + uint(ActivePortal) * kPortalStride + 2u].xyz);
        SurfaceAlb = FrameAlb;
    }
    else if (ActivePortal >= 0)
    {
        SurfaceT = NearestRealmOpaqueHit(Ensemble, ActiveRealm, RoutedOrigin, RoutedDir, SurfaceNrm, SurfaceAlb);
    }

    float3 GlassTint = EvaluateGlassTransmission(Ensemble, StartRealm, CamOrigin, CamDir, min(BestPortalT, PrimarySolidT));
    if (!HitFrame && ActivePortal >= 0)
    {
        GlassTint *= EvaluateGlassTransmission(Ensemble, ActiveRealm, RoutedOrigin, RoutedDir, SurfaceT);
    }

    float  ReversedZ       = 0.0f;
    float  HitDistanceM    = -1.0f;
    float3 SurfaceRadiance = float3(0.0f);

    if (SurfaceT < 1.0e25f)
    {
        float TotalCamDistM = (ActivePortal >= 0 && !HitFrame) ? (BestPortalT + SurfaceT) : SurfaceT;
        ReversedZ           = clamp(0.1f / max(TotalCamDistM, 0.1f), 1.0e-6f, 1.0f);
        HitDistanceM        = SurfaceT;

        float3 HitPos = RoutedOrigin + RoutedDir * SurfaceT;
        uint   RBase  = RealmSlotBase(Ensemble, ActiveRealm);
        float4 R0     = Ensemble[RBase + 0u];
        float4 R8     = Ensemble[RBase + 8u];
        float  SpanM  = max(Ensemble[RBase + 13u].z, 1.0f);

        uint SunFirst  = uint(R8.x + 0.5f);
        uint SunCount  = uint(R8.y + 0.5f);
        uint MoonFirst = uint(R8.z + 0.5f);
        uint MoonCount = uint(R8.w + 0.5f);

        float3 Irr = float3(0.0f);
        for (uint S = 0u; S < SunCount; ++S)
        {
            uint   SB     = SunSlotBase(Ensemble, SunFirst + S);
            float3 SunDir = Ensemble[SB + 0u].xyz;
            float3 SunIrr = Ensemble[SB + 1u].xyz;
            float  NdotL  = max(0.0f, dot(SurfaceNrm, SunDir));

            float3 SunTrans = (SunDir.y > -0.05f)
                                  ? SampleTransmittanceLutArray(TransLut, ActiveRealm, R0.x, R0.y, R0.x + max(R0.z, 1.0e-3f), SunDir.y)
                                  : float3(0.0f);

            // Sample orthographic CloudShadowArray[ActiveRealm]
            float2 ShadowUv = clamp(0.5f + 0.5f * (HitPos.xz / SpanM), 0.0f, 1.0f);
            float  CloudTr  = CloudShadowLut.SampleLevel(float3(ShadowUv, float(ActiveRealm)), 0.0f).g;
            float  Vis      = IsShadowedBySpheres(Ensemble, ActiveRealm, HitPos, SunDir) ? 0.0f : 1.0f;

            Irr += SunIrr * SunTrans * (NdotL * CloudTr * Vis);
            // Diffuse sky hemisphere fill
            float Hemi = 0.5f + 0.5f * SurfaceNrm.y;
            Irr += SunIrr * float3(0.045f, 0.068f, 0.110f) * saturate(SunDir.y + 0.18f) * Hemi;
        }

        // Nocturnal moonlit surface illumination + ambient starlight floor
        uint MoonTable = uint(Ensemble[5].z + 0.5f);
        for (uint M = 0u; M < MoonCount; ++M)
        {
            uint   MB      = MoonTable + (MoonFirst + M) * kMoonStride;
            float3 MoonDir = Ensemble[MB + 0u].xyz;
            float3 MoonTint = Ensemble[MB + 1u].xyz;
            float  NdotM   = max(0.0f, dot(SurfaceNrm, MoonDir));
            Irr += MoonTint * (0.014f * NdotM + 0.004f * (0.5f + 0.5f * SurfaceNrm.y));
        }
        Irr += float3(0.0025f, 0.0032f, 0.0048f) * (0.5f + 0.5f * SurfaceNrm.y);

        SurfaceRadiance = (SurfaceAlb / kPi) * Irr * GlassTint;
    }

    GbufferOut[uint3(Tid.xy, 0u)] = float4(SurfaceRadiance, ReversedZ);
    GbufferOut[uint3(Tid.xy, 1u)] = float4(RoutedOrigin, float(ActiveRealm));
    GbufferOut[uint3(Tid.xy, 2u)] = float4(RoutedDir, HitDistanceM);
    GbufferOut[uint3(Tid.xy, 3u)] = float4(GlassTint, float(ActivePortal + 1));
}
