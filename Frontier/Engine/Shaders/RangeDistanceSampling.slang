//==========================================================================================================================================
//                                                RANGE DISTANCE SAMPLING
//==========================================================================================================================================
// 🧩 Four-cascade camera-centered signed distance clipmap GPU evaluation, cross-cascade smooth trilinear blending,
//    tetrahedron gradient normal, multi-scale ambient occlusion, soft cone visibility, & far-field GI sphere tracing.

#pragma once

float FetchRangeVoxelScalar(StructuredBuffer<float4> RangeTable, uint QuadBase, uint LinearVoxelIndex)
{
    float4 Quad = RangeTable[QuadBase + (LinearVoxelIndex >> 2u)];
    uint   Lane = LinearVoxelIndex & 3u;
    return (Lane == 0u) ? Quad.x : ((Lane == 1u) ? Quad.y : ((Lane == 2u) ? Quad.z : Quad.w));
}

float SampleSingleRangeTrilinear(StructuredBuffer<float4> RangeTable, uint RangeIdx, float3 WorldPoint)
{
    uint   HdrBase = RangeIdx * 4u;
    float4 H3      = RangeTable[HdrBase + 3u];
    if (H3.w < 0.5f)
    {
        return 1.0e6f;
    }
    float4 H0 = RangeTable[HdrBase + 0u];
    float4 H1 = RangeTable[HdrBase + 1u];
    float4 H2 = RangeTable[HdrBase + 2u];

    uint NX = uint(H0.w + 0.5f);
    uint NY = uint(H1.w + 0.5f);
    uint NZ = uint(H2.w + 0.5f);
    if (NX == 0u || NY == 0u || NZ == 0u)
    {
        return 1.0e6f;
    }

    uint   QuadBase = asuint(H3.z);
    float3 ClampedP = clamp(WorldPoint, H0.xyz, H1.xyz);
    float  Exterior = length(WorldPoint - ClampedP);

    float3 GridPos = clamp((ClampedP - H0.xyz) / max(H2.xyz, float3(1.0e-5f)) - 0.5f,
                           float3(0.0f),
                           float3(float(NX - 1u), float(NY - 1u), float(NZ - 1u)));
    uint3  Idx0    = uint3(GridPos);
    uint3  Idx1    = min(Idx0 + uint3(1u, 1u, 1u), uint3(NX - 1u, NY - 1u, NZ - 1u));
    float3 Frac    = GridPos - float3(Idx0);

    uint StrideY = NX;
    uint StrideZ = NX * NY;

    float D000 = FetchRangeVoxelScalar(RangeTable, QuadBase, Idx0.z * StrideZ + Idx0.y * StrideY + Idx0.x);
    float D100 = FetchRangeVoxelScalar(RangeTable, QuadBase, Idx0.z * StrideZ + Idx0.y * StrideY + Idx1.x);
    float D010 = FetchRangeVoxelScalar(RangeTable, QuadBase, Idx0.z * StrideZ + Idx1.y * StrideY + Idx0.x);
    float D110 = FetchRangeVoxelScalar(RangeTable, QuadBase, Idx0.z * StrideZ + Idx1.y * StrideY + Idx1.x);
    float D001 = FetchRangeVoxelScalar(RangeTable, QuadBase, Idx1.z * StrideZ + Idx0.y * StrideY + Idx0.x);
    float D101 = FetchRangeVoxelScalar(RangeTable, QuadBase, Idx1.z * StrideZ + Idx0.y * StrideY + Idx1.x);
    float D011 = FetchRangeVoxelScalar(RangeTable, QuadBase, Idx1.z * StrideZ + Idx1.y * StrideY + Idx0.x);
    float D111 = FetchRangeVoxelScalar(RangeTable, QuadBase, Idx1.z * StrideZ + Idx1.y * StrideY + Idx1.x);

    float C00 = lerp(D000, D100, Frac.x);
    float C10 = lerp(D010, D110, Frac.x);
    float C01 = lerp(D001, D101, Frac.x);
    float C11 = lerp(D101, D111, Frac.x);
    // Note exact C01/C11 interpolation along X:
    C11       = lerp(D011, D111, Frac.x);
    float C0  = lerp(C00, C10, Frac.y);
    float C1  = lerp(C01, C11, Frac.y);
    return lerp(C0, C1, Frac.z) + Exterior;
}

float SampleRangeDistanceClipmap(StructuredBuffer<float4> RangeTable, float3 WorldPoint, out uint OutActiveRange, out float OutVoxelStep)
{
    OutActiveRange = 0u;
    OutVoxelStep   = 0.5f;

    [unroll]
    for (uint R = 0u; R < 4u; ++R)
    {
        float4 H3 = RangeTable[R * 4u + 3u];
        if (H3.w < 0.5f)
        {
            continue;
        }
        float3 Lo      = RangeTable[R * 4u + 0u].xyz;
        float3 Hi      = RangeTable[R * 4u + 1u].xyz;
        float3 StepS   = RangeTable[R * 4u + 2u].xyz;
        float3 Center  = 0.5f * (Lo + Hi);
        float  HalfExt = H3.x;
        float3 AbsDiff = abs(WorldPoint - Center);
        float  Cheb    = max(AbsDiff.x, max(AbsDiff.y, AbsDiff.z));

        bool NextActive = (R + 1u < 4u) && (RangeTable[(R + 1u) * 4u + 3u].w > 0.5f);
        if (Cheb <= HalfExt * 0.96f || !NextActive)
        {
            OutActiveRange = R;
            OutVoxelStep   = max(0.05f, max(StepS.x, max(StepS.y, StepS.z)));
            float FineD    = SampleSingleRangeTrilinear(RangeTable, R, WorldPoint);
            float InnerLim = HalfExt * 0.76f;
            if (Cheb > InnerLim && NextActive)
            {
                float BlendT  = saturate((Cheb - InnerLim) / max(1.0e-3f, HalfExt * 0.20f));
                float CoarseD = SampleSingleRangeTrilinear(RangeTable, R + 1u, WorldPoint);
                return lerp(FineD, CoarseD, BlendT);
            }
            return FineD;
        }
    }
    return 1.0e6f;
}

float3 SampleRangeDistanceNormal(StructuredBuffer<float4> RangeTable, float3 WorldPoint)
{
    uint  ActiveR   = 0u;
    float VoxelStep = 0.2f;
    SampleRangeDistanceClipmap(RangeTable, WorldPoint, ActiveR, VoxelStep);
    float Eps = max(0.04f, 0.55f * VoxelStep);

    uint  DummyR = 0u;
    float DummyS = 0.0f;
    float Dxp    = SampleRangeDistanceClipmap(RangeTable, WorldPoint + float3( Eps, -Eps, -Eps), DummyR, DummyS);
    float Dxm    = SampleRangeDistanceClipmap(RangeTable, WorldPoint + float3(-Eps, -Eps,  Eps), DummyR, DummyS);
    float Dyp    = SampleRangeDistanceClipmap(RangeTable, WorldPoint + float3(-Eps,  Eps, -Eps), DummyR, DummyS);
    float Dzp    = SampleRangeDistanceClipmap(RangeTable, WorldPoint + float3( Eps,  Eps,  Eps), DummyR, DummyS);

    float3 Grad = float3(Dxp - Dxm - Dyp + Dzp, -Dxp - Dxm + Dyp + Dzp, -Dxp + Dxm - Dyp + Dzp);
    float  Len  = length(Grad);
    return (Len > 1.0e-6f) ? (Grad / Len) : float3(0.0f, 1.0f, 0.0f);
}

float EvaluateRangeDistanceOcclusion(StructuredBuffer<float4> RangeTable, float3 OriginMeters, float3 SurfaceNormal, float MaxRadiusM)
{
    if (RangeTable[3].w < 0.5f)
    {
        return 1.0f;
    }
    float Occlusion = 0.0f;
    float Weight    = 0.5f;
    for (int Tap = 1; Tap <= 5; ++Tap)
    {
        float QueryDist = MaxRadiusM * (float(Tap) / 5.0f);
        uint  DummyR    = 0u;
        float DummyS    = 0.0f;
        float ActualD   = SampleRangeDistanceClipmap(RangeTable, OriginMeters + SurfaceNormal * QueryDist, DummyR, DummyS);
        Occlusion += Weight * saturate((QueryDist - ActualD) / max(QueryDist, 1.0e-3f));
        Weight *= 0.58f;
    }
    return saturate(1.0f - 1.15f * Occlusion);
}

bool TraceRangeDistanceSphereRay(StructuredBuffer<float4> RangeTable,
                                 float3                   OriginMeters,
                                 float3                   RayDirUnit,
                                 float                    MaxDistanceMeters,
                                 int                      MaxSteps,
                                 out float3               OutHitPos,
                                 out float3               OutHitNormal,
                                 out float                OutTravelMeters,
                                 out uint                 OutRangeIndex)
{
    OutHitPos       = OriginMeters;
    OutHitNormal    = float3(0.0f, 1.0f, 0.0f);
    OutTravelMeters = MaxDistanceMeters;
    OutRangeIndex   = 0u;

    if (RangeTable[3].w < 0.5f)
    {
        return false;
    }

    uint  StartR    = 0u;
    float StartStep = 0.25f;
    SampleRangeDistanceClipmap(RangeTable, OriginMeters, StartR, StartStep);
    float Travel = max(0.16f, StartStep * 0.85f);

    for (int Step = 0; Step < MaxSteps; ++Step)
    {
        if (Travel >= MaxDistanceMeters)
        {
            break;
        }
        float3 Pos      = OriginMeters + RayDirUnit * Travel;
        uint   ActiveR  = 0u;
        float  VoxStep  = 0.25f;
        float  Dist     = SampleRangeDistanceClipmap(RangeTable, Pos, ActiveR, VoxStep);
        float  HitEps   = max(0.02f, VoxStep * 0.22f);
        if (Dist <= HitEps)
        {
            OutHitPos       = Pos;
            OutHitNormal    = SampleRangeDistanceNormal(RangeTable, Pos);
            OutTravelMeters = Travel;
            OutRangeIndex   = ActiveR;
            return true;
        }
        Travel += max(Dist * 0.86f, VoxStep * 0.35f);
    }
    return false;
}
