//==========================================================================================================================================
//                                                    DISTANCE TRAVERSAL
//==========================================================================================================================================
// 🧩 Hardware 3D texture (Sampler3D, VK_IMAGE_TYPE_3D R16_SFLOAT) 4-cascade signed distance clipmap sphere traversal:
//    - Single-instruction hardware trilinear 3D texture evaluation across 4 Z-stacked cascades (with guard slices)
//    - Central-difference 6-tap gradient normal, multi-step SDF ambient occlusion, and local softbox cone visibility
//    - Half-resolution (TraceScale = 2, 1/4 pixels) 1-cosine-ray/px diffuse global illumination march

#pragma once

struct HardwareSdfHit
{
    bool   Hit;
    float  TravelM;
    uint   CascadeLevel;
    float3 WorldPos;
    float3 Normal;
};

bool HardwareClipContains(StructuredBuffer<float4> RangeTable, uint Level, float3 WorldPos, float MarginVoxels)
{
    uint   HdrBase = Level * 4u;
    float4 H3      = RangeTable[HdrBase + 3u];
    if (H3.w < 0.5f)
    {
        return false;
    }
    float3 Lo     = RangeTable[HdrBase + 0u].xyz;
    float3 Hi     = RangeTable[HdrBase + 1u].xyz;
    float3 Step   = RangeTable[HdrBase + 2u].xyz;
    float3 Margin = Step * MarginVoxels;
    return all(WorldPos >= (Lo + Margin)) && all(WorldPos <= (Hi - Margin));
}

uint ResolveFinestHardwareClip(StructuredBuffer<float4> RangeTable, float3 WorldPos, float MarginVoxels)
{
    for (uint Level = 0u; Level < 4u; ++Level)
    {
        if (HardwareClipContains(RangeTable, Level, WorldPos, MarginVoxels))
        {
            return Level;
        }
    }
    return 4u;
}

float SampleHardwareClipLevel(Sampler3D SdfVolume, StructuredBuffer<float4> RangeTable, uint Level, float3 WorldPos)
{
    uint   HdrBase = Level * 4u;
    float4 H0      = RangeTable[HdrBase + 0u];
    float4 H1      = RangeTable[HdrBase + 1u];
    float4 H2      = RangeTable[HdrBase + 2u];

    float  NX      = max(1.0f, H0.w);
    float  NY      = max(1.0f, H1.w);
    float  NZ      = max(1.0f, H2.w);
    float3 Clamped = clamp(WorldPos, H0.xyz, H1.xyz);
    float  ExtDist = length(WorldPos - Clamped);

    float3 Span    = max(float3(NX, NY, NZ) * H2.xyz, float3(1.0e-4f));
    float  U       = clamp((Clamped.x - H0.x) / Span.x, 0.5f / NX, 1.0f - 0.5f / NX);
    float  V       = clamp((Clamped.y - H0.y) / Span.y, 0.5f / NY, 1.0f - 0.5f / NY);
    float  LocalZ  = clamp((Clamped.z - H0.z) / max(H2.z, 1.0e-5f), 0.5f, NZ - 0.5f);
    float  PaddedZ = NZ + 2.0f;
    float  TotalZ  = 4.0f * PaddedZ;
    float  W       = (float(Level) * PaddedZ + 1.0f + LocalZ) / TotalZ;

    return SdfVolume.SampleLevel(float3(U, V, W), 0.0f).r + ExtDist;
}

float SampleHardwareSingleVolume(Sampler3D SdfVolume, StructuredBuffer<float4> SdfHeader, float3 WorldPos)
{
    float4 H0 = SdfHeader[0];
    float4 H1 = SdfHeader[1];
    float4 H2 = SdfHeader[2];
    float  NX = max(1.0f, H0.w);
    float  NY = max(1.0f, H1.w);
    float  NZ = max(1.0f, H2.w);
    if (H0.w <= 0.5f)
    {
        return 1.0e5f;
    }
    float3 Clamped = clamp(WorldPos, H0.xyz, H1.xyz);
    float  ExtDist = length(WorldPos - Clamped);
    float3 Span    = max(float3(NX, NY, NZ) * H2.xyz, float3(1.0e-4f));
    float3 Uvw     = clamp((Clamped - H0.xyz) / Span,
                           float3(0.5f / NX, 0.5f / NY, 0.5f / NZ),
                           float3(1.0f - 0.5f / NX, 1.0f - 0.5f / NY, 1.0f - 0.5f / NZ));
    return SdfVolume.SampleLevel(Uvw, 0.0f).r + ExtDist;
}

float SampleHardwareSceneDistance(Sampler3D                SdfVolume,
                                  StructuredBuffer<float4> RangeOrSdfTable,
                                  bool                     ClipmapActive,
                                  float                    GroundHeightM,
                                  float3                   WorldPos,
                                  out uint                 OutLevel,
                                  out float                OutVoxelExtent)
{
    if (!ClipmapActive)
    {
        OutLevel       = 0u;
        OutVoxelExtent = max(0.05f, RangeOrSdfTable[2].x);
        return SampleHardwareSingleVolume(SdfVolume, RangeOrSdfTable, WorldPos);
    }

    uint Level = ResolveFinestHardwareClip(RangeOrSdfTable, WorldPos, 1.25f);
    if (Level >= 4u)
    {
        OutLevel       = 4u;
        OutVoxelExtent = max(0.5f, RangeOrSdfTable[3u * 4u + 2u].x);
        return WorldPos.y - GroundHeightM;
    }

    OutLevel       = Level;
    OutVoxelExtent = max(0.04f, RangeOrSdfTable[Level * 4u + 2u].x);
    float D0       = SampleHardwareClipLevel(SdfVolume, RangeOrSdfTable, Level, WorldPos);

    // Smooth outer-20% trilinear cascade blend into Level + 1 to eliminate cascade seam steps
    if (Level < 3u && RangeOrSdfTable[(Level + 1u) * 4u + 3u].w > 0.5f)
    {
        uint   HBase  = Level * 4u;
        float3 Center = 0.5f * (RangeOrSdfTable[HBase + 0u].xyz + RangeOrSdfTable[HBase + 1u].xyz);
        float  HalfEx = max(0.1f, RangeOrSdfTable[HBase + 3u].x);
        float3 AbsRel = abs(WorldPos - Center);
        float  Cheb   = max(AbsRel.x, max(AbsRel.y, AbsRel.z));
        float  Blend  = saturate((Cheb - 0.78f * HalfEx) / (0.20f * HalfEx));
        if (Blend > 1.0e-3f)
        {
            float D1 = SampleHardwareClipLevel(SdfVolume, RangeOrSdfTable, Level + 1u, WorldPos);
            D0       = lerp(D0, D1, Blend);
        }
    }
    return D0;
}

float3 ResolveHardwareSdfNormal(Sampler3D                SdfVolume,
                                StructuredBuffer<float4> RangeOrSdfTable,
                                bool                     ClipmapActive,
                                float                    GroundHeightM,
                                uint                     HintLevel,
                                float3                   WorldPos)
{
    if (ClipmapActive && HintLevel >= 4u)
    {
        return float3(0.0f, 1.0f, 0.0f);
    }
    float Voxel = ClipmapActive ? max(0.04f, RangeOrSdfTable[min(HintLevel, 3u) * 4u + 2u].x)
                                : max(0.05f, RangeOrSdfTable[2].x);
    float Step  = Voxel * 0.75f;
    uint  U0;
    float V0;
    float Dx = SampleHardwareSceneDistance(SdfVolume, RangeOrSdfTable, ClipmapActive, GroundHeightM, WorldPos + float3(Step, 0.0f, 0.0f), U0, V0) -
               SampleHardwareSceneDistance(SdfVolume, RangeOrSdfTable, ClipmapActive, GroundHeightM, WorldPos - float3(Step, 0.0f, 0.0f), U0, V0);
    float Dy = SampleHardwareSceneDistance(SdfVolume, RangeOrSdfTable, ClipmapActive, GroundHeightM, WorldPos + float3(0.0f, Step, 0.0f), U0, V0) -
               SampleHardwareSceneDistance(SdfVolume, RangeOrSdfTable, ClipmapActive, GroundHeightM, WorldPos - float3(0.0f, Step, 0.0f), U0, V0);
    float Dz = SampleHardwareSceneDistance(SdfVolume, RangeOrSdfTable, ClipmapActive, GroundHeightM, WorldPos + float3(0.0f, 0.0f, Step), U0, V0) -
               SampleHardwareSceneDistance(SdfVolume, RangeOrSdfTable, ClipmapActive, GroundHeightM, WorldPos - float3(0.0f, 0.0f, Step), U0, V0);
    float3 G   = float3(Dx, Dy, Dz);
    float  Len = length(G);
    return (Len > 1.0e-6f) ? (G / Len) : float3(0.0f, 1.0f, 0.0f);
}

float EvaluateHardwareSdfAmbientOcclusion(Sampler3D                SdfVolume,
                                          StructuredBuffer<float4> RangeOrSdfTable,
                                          bool                     ClipmapActive,
                                          float                    GroundHeightM,
                                          float3                   WorldPos,
                                          float3                   Normal)
{
    uint  L0;
    float BaseVox = 0.20f;
    SampleHardwareSceneDistance(SdfVolume, RangeOrSdfTable, ClipmapActive, GroundHeightM, WorldPos, L0, BaseVox);

    float Occl   = 0.0f;
    float Weight = 0.55f;
    [unroll]
    for (uint StepIdx = 1u; StepIdx <= 3u; ++StepIdx)
    {
        float ProbeT = BaseVox * (0.85f * float(StepIdx * StepIdx));
        uint  Lv;
        float Vx;
        float Dist   = SampleHardwareSceneDistance(SdfVolume, RangeOrSdfTable, ClipmapActive, GroundHeightM, WorldPos + Normal * ProbeT, Lv, Vx);
        Occl        += Weight * saturate((ProbeT - Dist) / max(ProbeT, 1.0e-3f));
        Weight      *= 0.55f;
    }
    return clamp(1.0f - 0.82f * Occl, 0.14f, 1.0f);
}

float EvaluateHardwareSdfConeVisibility(Sampler3D                SdfVolume,
                                        StructuredBuffer<float4> RangeOrSdfTable,
                                        bool                     ClipmapActive,
                                        float                    GroundHeightM,
                                        float3                   Origin,
                                        float3                   Normal,
                                        float3                   DirToLight,
                                        float                    MaxDistM,
                                        float                    ConeTanHalfAngle)
{
    uint  L0;
    float StartVox = 0.18f;
    SampleHardwareSceneDistance(SdfVolume, RangeOrSdfTable, ClipmapActive, GroundHeightM, Origin, L0, StartVox);

    float3 BiasedOrigin = Origin + Normal * (StartVox * 1.15f);
    float  Travel       = StartVox * 0.85f;
    float  Visibility   = 1.0f;

    for (uint Step = 0u; Step < 10u; ++Step)
    {
        if (Travel >= MaxDistM || Visibility <= 0.02f)
        {
            break;
        }
        uint  Lv;
        float Vx;
        float Dist       = SampleHardwareSceneDistance(SdfVolume, RangeOrSdfTable, ClipmapActive, GroundHeightM, BiasedOrigin + DirToLight * Travel, Lv, Vx);
        float ConeRadius = max(Vx * 0.45f, Travel * ConeTanHalfAngle);
        Visibility       = min(Visibility, saturate(0.5f + 0.5f * (Dist / ConeRadius)));
        Travel          += max(Dist * 0.90f, Vx * 0.55f);
    }
    return Visibility;
}

HardwareSdfHit TraverseHardwareSdfClipmap(Sampler3D                SdfVolume,
                                          StructuredBuffer<float4> RangeOrSdfTable,
                                          bool                     ClipmapActive,
                                          float                    GroundHeightM,
                                          float3                   SurfacePos,
                                          float3                   SurfaceNormal,
                                          float3                   RayDir,
                                          float                    MaxDistanceM)
{
    HardwareSdfHit Out;
    Out.Hit          = false;
    Out.TravelM      = MaxDistanceM;
    Out.CascadeLevel = 4u;
    Out.WorldPos     = SurfacePos;
    Out.Normal       = float3(0.0f, 1.0f, 0.0f);

    uint  StartLv  = 0u;
    float StartVox = 0.20f;
    SampleHardwareSceneDistance(SdfVolume, RangeOrSdfTable, ClipmapActive, GroundHeightM, SurfacePos, StartLv, StartVox);

    float3 Origin = SurfacePos + SurfaceNormal * (StartVox * 1.30f);
    float  Travel = StartVox * 0.65f;

    for (uint Step = 0u; Step < 40u; ++Step)
    {
        if (Travel >= MaxDistanceM)
        {
            break;
        }
        float3 P = Origin + RayDir * Travel;
        uint   Lv;
        float  Vx;
        float  Dist      = SampleHardwareSceneDistance(SdfVolume, RangeOrSdfTable, ClipmapActive, GroundHeightM, P, Lv, Vx);
        float  Threshold = max(0.02f, Vx * 0.22f + Travel * 0.012f);
        if (Dist <= Threshold)
        {
            Out.Hit          = true;
            Out.TravelM      = Travel;
            Out.CascadeLevel = Lv;
            Out.WorldPos     = P;
            Out.Normal       = ResolveHardwareSdfNormal(SdfVolume, RangeOrSdfTable, ClipmapActive, GroundHeightM, Lv, P);
            return Out;
        }
        Travel += max(Dist * 0.92f, Vx * 0.34f);
    }

    // Outside cascade horizon fallback to analytic ground plane when ray points downward
    if (ClipmapActive && RayDir.y < -1.0e-3f)
    {
        float PlaneT = (GroundHeightM - Origin.y) / RayDir.y;
        if (PlaneT > Travel && PlaneT < MaxDistanceM)
        {
            Out.Hit          = true;
            Out.TravelM      = PlaneT;
            Out.CascadeLevel = 4u;
            Out.WorldPos     = Origin + RayDir * PlaneT;
            Out.Normal       = float3(0.0f, 1.0f, 0.0f);
        }
    }
    return Out;
}
