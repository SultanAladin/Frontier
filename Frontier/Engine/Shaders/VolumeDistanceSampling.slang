/*==========================================================================================================================================
                                            VOLUME DISTANCE SAMPLING (SLANG)
                                     ------------------------------------------------------------------
             Trilinear 3D distance-volume sampler supporting both single-grid .sdfspace volumes and
             self-describing 4-cascade clipmap SDFs with trilinear seam blending, analytical primitive
             unions (Box, Sphere, Cylinder), tetrahedral gradient normals, soft solar/sky cones,
             ambient occlusion, and sphere-traced secondary GI ray hits.
==========================================================================================================================================*/
// 🧩 4-cascade clipmap + analytical SDF sampler, cone visibility, AO, & secondary GI sphere-tracer.

#pragma once

float FetchQuadPackedDistance(StructuredBuffer<float4> Field, uint PayloadBaseFloat4, uint NX, uint NY, uint X, uint Y, uint Z)
{
    uint   LinearIdx = (Z * NY + Y) * NX + X;
    float4 Quad      = Field[PayloadBaseFloat4 + (LinearIdx >> 2u)];
    uint   Lane      = LinearIdx & 3u;
    return (Lane == 0u) ? Quad.x : ((Lane == 1u) ? Quad.y : ((Lane == 2u) ? Quad.z : Quad.w));
}

float SampleSingleCascadeGrid(StructuredBuffer<float4> Field,
                              float3                   MinB,
                              float3                   MaxB,
                              float3                   StepB,
                              uint                     NX,
                              uint                     NY,
                              uint                     NZ,
                              uint                     PayloadBaseFloat4,
                              float3                   WorldPos)
{
    if (NX == 0u || NY == 0u || NZ == 0u)
    {
        return 1.0e5f;
    }

    float3 ClampedP = clamp(WorldPos, MinB, MaxB);
    float  Exterior = length(WorldPos - ClampedP);

    float3 GridPos = clamp((ClampedP - MinB) / max(StepB, float3(1.0e-5f, 1.0e-5f, 1.0e-5f)) - 0.5f,
                           float3(0.0f, 0.0f, 0.0f),
                           float3(float(NX - 1u), float(NY - 1u), float(NZ - 1u)));

    uint3  I0 = uint3(GridPos);
    uint3  I1 = min(I0 + uint3(1u, 1u, 1u), uint3(NX - 1u, NY - 1u, NZ - 1u));
    float3 T  = GridPos - float3(I0);

    float C00 = lerp(FetchQuadPackedDistance(Field, PayloadBaseFloat4, NX, NY, I0.x, I0.y, I0.z),
                     FetchQuadPackedDistance(Field, PayloadBaseFloat4, NX, NY, I1.x, I0.y, I0.z), T.x);
    float C10 = lerp(FetchQuadPackedDistance(Field, PayloadBaseFloat4, NX, NY, I0.x, I1.y, I0.z),
                     FetchQuadPackedDistance(Field, PayloadBaseFloat4, NX, NY, I1.x, I1.y, I0.z), T.x);
    float C01 = lerp(FetchQuadPackedDistance(Field, PayloadBaseFloat4, NX, NY, I0.x, I0.y, I1.z),
                     FetchQuadPackedDistance(Field, PayloadBaseFloat4, NX, NY, I1.x, I0.y, I1.z), T.x);
    float C11 = lerp(FetchQuadPackedDistance(Field, PayloadBaseFloat4, NX, NY, I0.x, I1.y, I1.z),
                     FetchQuadPackedDistance(Field, PayloadBaseFloat4, NX, NY, I1.x, I1.y, I1.z), T.x);

    float C0 = lerp(C00, C10, T.y);
    float C1 = lerp(C01, C11, T.y);
    return lerp(C0, C1, T.z) + Exterior;
}

float EvaluateAnalyticalPrimitiveGpu(float4 Prim0, float4 Prim1, float3 WorldPos)
{
    float3 D    = WorldPos - Prim0.xyz;
    float  Code = Prim0.w;
    if (Code > 0.5f && Code < 1.5f)
    {
        return length(D) - max(Prim1.x, 1.0e-4f);
    }
    if (Code >= 1.5f)
    {
        float Radial   = length(D.xz) - max(Prim1.x, 1.0e-4f);
        float Vertical = abs(D.y) - max(Prim1.y, 1.0e-4f);
        float2 Outside = max(float2(Radial, Vertical), float2(0.0f, 0.0f));
        return min(max(Radial, Vertical), 0.0f) + length(Outside);
    }
    float3 Q       = abs(D) - max(Prim1.xyz, float3(1.0e-4f, 1.0e-4f, 1.0e-4f));
    float3 Outside = max(Q, float3(0.0f, 0.0f, 0.0f));
    return length(Outside) + min(max(Q.x, max(Q.y, Q.z)), 0.0f);
}

uint QueryActiveCascadeIndexGpu(StructuredBuffer<float4> Field, float3 WorldPos)
{
    float Mode = Field[3].w;
    if (Mode < 3.5f)
    {
        return 0u;
    }
    for (uint C = 0u; C < 4u; ++C)
    {
        uint   MetaBase = 4u + C * 4u;
        float3 MinB     = Field[MetaBase + 0u].xyz;
        float3 MaxB     = Field[MetaBase + 1u].xyz;
        if (all(WorldPos >= MinB) && all(WorldPos <= MaxB))
        {
            return C;
        }
    }
    return 3u;
}

float SampleVolumeDistanceGpu(StructuredBuffer<float4> Field, float3 WorldPos)
{
    float4 H3 = Field[3];
    if (H3.w < 0.5f)
    {
        return 1.0e5f;
    }

    // Legacy single-grid archive path
    if (H3.w < 3.5f)
    {
        float4 H0 = Field[0];
        float4 H1 = Field[1];
        float4 H2 = Field[2];
        return SampleSingleCascadeGrid(Field, H0.xyz, H1.xyz, H2.xyz, uint(H0.w), uint(H1.w), uint(H2.w), 4u, WorldPos);
    }

    // Self-describing 4-cascade clipmap + analytical primitive union
    uint  Selected   = 3u;
    float NormRadius = 1.0f;

    [unroll]
    for (uint C = 0u; C < 4u; ++C)
    {
        uint   MetaBase = 4u + C * 4u;
        float3 MinB     = Field[MetaBase + 0u].xyz;
        float3 MaxB     = Field[MetaBase + 1u].xyz;
        float3 Center   = Field[MetaBase + 3u].xyz;
        float3 HalfExt  = max(0.5f * (MaxB - MinB), float3(1.0e-4f, 1.0e-4f, 1.0e-4f));
        float3 Rel      = abs(WorldPos - Center) / HalfExt;
        float  Cheb     = max(Rel.x, max(Rel.y, Rel.z));
        if (Cheb <= 1.0f)
        {
            Selected   = C;
            NormRadius = Cheb;
            break;
        }
    }

    uint   Base0   = 4u + Selected * 4u;
    float4 M0      = Field[Base0 + 0u];
    float4 M1      = Field[Base0 + 1u];
    float4 M2      = Field[Base0 + 2u];
    uint   Offset0 = asuint(Field[Base0 + 3u].w);
    float  BestD   = SampleSingleCascadeGrid(Field, M0.xyz, M1.xyz, M2.xyz, uint(M0.w), uint(M1.w), uint(M2.w), Offset0, WorldPos);

    float4 GlobalCtrl = Field[20];
    if (GlobalCtrl.y > 0.5f && Selected < 3u && NormRadius > 0.78f)
    {
        uint   Base1   = 4u + (Selected + 1u) * 4u;
        float4 N0      = Field[Base1 + 0u];
        float4 N1      = Field[Base1 + 1u];
        float4 N2      = Field[Base1 + 2u];
        uint   Offset1 = asuint(Field[Base1 + 3u].w);
        float  CoarseD = SampleSingleCascadeGrid(Field, N0.xyz, N1.xyz, N2.xyz, uint(N0.w), uint(N1.w), uint(N2.w), Offset1, WorldPos);
        float  BlendT  = smoothstep(0.78f, 0.98f, NormRadius);
        BestD          = lerp(BestD, CoarseD, BlendT);
    }

    if (GlobalCtrl.w > -9.0e5f)
    {
        BestD = min(BestD, WorldPos.y - GlobalCtrl.w);
    }

    uint PrimCount = min(uint(max(GlobalCtrl.x, 0.0f)), 64u);
    [loop]
    for (uint P = 0u; P < PrimCount; ++P)
    {
        float4 Prim0 = Field[21u + P * 2u + 0u];
        float4 Prim1 = Field[21u + P * 2u + 1u];
        BestD        = min(BestD, EvaluateAnalyticalPrimitiveGpu(Prim0, Prim1, WorldPos));
    }

    return BestD;
}

float3 SampleVolumeGradientNormalGpu(StructuredBuffer<float4> Field, float3 WorldPos)
{
    float  Eps = max(0.025f, 0.5f * min(Field[2].x, min(Field[2].y, Field[2].z)));
    float  Dxp = SampleVolumeDistanceGpu(Field, WorldPos + float3( Eps, -Eps, -Eps));
    float  Dxm = SampleVolumeDistanceGpu(Field, WorldPos + float3(-Eps, -Eps,  Eps));
    float  Dyp = SampleVolumeDistanceGpu(Field, WorldPos + float3(-Eps,  Eps, -Eps));
    float  Dzp = SampleVolumeDistanceGpu(Field, WorldPos + float3( Eps,  Eps,  Eps));
    float3 Grad = float3(Dxp - Dxm - Dyp + Dzp, -Dxp - Dxm + Dyp + Dzp, -Dxp + Dxm - Dyp + Dzp);
    float  Len  = length(Grad);
    return (Len > 1.0e-6f) ? (Grad / Len) : float3(0.0f, 1.0f, 0.0f);
}

float TraceVolumeConeVisibilityGpu(StructuredBuffer<float4> Field,
                                   float3                   Origin,
                                   float3                   Direction,
                                   float                    MaxDistance,
                                   float                    ConeSharpness,
                                   uint                     MaxSteps)
{
    if (Field[3].w < 0.5f)
    {
        return 1.0f;
    }

    float StepScale = (Field[3].w > 3.5f) ? clamp(Field[20].z, 0.4f, 1.25f) : 0.85f;
    float VoxelStep = max(0.04f, 0.85f * max(Field[2].x, max(Field[2].y, Field[2].z)));
    float Travel    = VoxelStep * 1.4f;
    float Vis       = 1.0f;

    [loop]
    for (uint Step = 0u; Step < MaxSteps && Travel < MaxDistance; ++Step)
    {
        float Dist = SampleVolumeDistanceGpu(Field, Origin + Direction * Travel);
        if (Dist <= 0.003f)
        {
            return 0.0f;
        }
        Vis    = min(Vis, saturate(ConeSharpness * Dist / max(Travel, 0.05f)));
        Travel += max(Dist * StepScale, VoxelStep * 0.55f);
    }
    return Vis;
}

float EvaluateVolumeAmbientOcclusionGpu(StructuredBuffer<float4> Field, float3 WorldPos, float3 WorldNormal)
{
    if (Field[3].w < 0.5f)
    {
        return 1.0f;
    }

    float Occlusion = 0.0f;
    float Weight    = 0.55f;
    [unroll]
    for (int Tap = 1; Tap <= 5; ++Tap)
    {
        float ProbeDist = 0.08f * float(Tap * Tap);
        float SignedD   = SampleVolumeDistanceGpu(Field, WorldPos + WorldNormal * ProbeDist);
        Occlusion += Weight * saturate((ProbeDist - SignedD) / max(ProbeDist, 1.0e-3f));
        Weight *= 0.55f;
    }
    return saturate(1.0f - Occlusion);
}

struct VolumeSphereTraceHit
{
    bool   Hit;
    float  DistanceMeters;
    float3 WorldPosition;
    float3 WorldNormal;
    uint   CascadeIndex;
};

VolumeSphereTraceHit TraceVolumeRayHitGpu(StructuredBuffer<float4> Field,
                                          float3                   Origin,
                                          float3                   Direction,
                                          float                    MinDistance,
                                          float                    MaxDistance,
                                          uint                     MaxSteps)
{
    VolumeSphereTraceHit Out;
    Out.Hit            = false;
    Out.DistanceMeters = MaxDistance;
    Out.WorldPosition  = Origin + Direction * MaxDistance;
    Out.WorldNormal    = -Direction;
    Out.CascadeIndex   = 3u;

    if (Field[3].w < 0.5f)
    {
        return Out;
    }

    float StepScale = (Field[3].w > 3.5f) ? clamp(Field[20].z, 0.45f, 1.2f) : 0.90f;
    float BaseVoxel = max(0.03f, 0.65f * min(Field[2].x, min(Field[2].y, Field[2].z)));
    float Travel    = max(MinDistance, BaseVoxel * 1.15f);

    [loop]
    for (uint Step = 0u; Step < MaxSteps && Travel < MaxDistance; ++Step)
    {
        float3 P    = Origin + Direction * Travel;
        float  Dist = SampleVolumeDistanceGpu(Field, P);
        float  HitThreshold = max(0.012f, 0.003f * Travel);
        if (Dist <= HitThreshold)
        {
            Out.Hit            = true;
            Out.DistanceMeters = Travel;
            Out.WorldPosition  = P;
            Out.WorldNormal    = SampleVolumeGradientNormalGpu(Field, P);
            if (dot(Out.WorldNormal, Direction) > 0.0f)
            {
                Out.WorldNormal = -Out.WorldNormal;
            }
            Out.CascadeIndex = QueryActiveCascadeIndexGpu(Field, P);
            return Out;
        }
        Travel += max(Dist * StepScale, BaseVoxel * 0.45f);
    }
    return Out;
}
