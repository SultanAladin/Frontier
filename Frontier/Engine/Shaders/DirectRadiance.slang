//==========================================================================================================================================
//                                                     DIRECT RADIANCE
//==========================================================================================================================================
// 🧩 Deterministic direct illumination (ShadowRaster 3-cascade hardware shadow depth + analytic area/clustered luminaires):
//    - Zero DI Monte Carlo noise in default Hybrid mode (sub-texel 3x3 bilinear-weighted PCF + cascade cross-fade)
//    - Karis / Frostbite representative-point rectangular studio softboxes (silky smooth clearcoat area highlights on Frame 0)
//    - Hardware 3D SDF local softbox contact occlusion

#pragma once

#include "DistanceTraversal.slang"
#include "SurfaceScatteringEvaluation.slang"

float SampleSingleCascadePcf(Sampler2DArray           ShadowDepthArray,
                             StructuredBuffer<float4> FrameHeader,
                             uint                     Cascade,
                             float3                   WorldPos,
                             float3                   SurfaceNormal)
{
    float  ShadowRes  = max(64.0f, FrameHeader[19].x);
    float  InvRes     = 1.0f / ShadowRes;
    float  TexelWorld = FrameHeader[33][Cascade];
    float3 OffsetPos  = WorldPos + SurfaceNormal * (1.65f * TexelWorld);

    uint   RowBase = 20u + Cascade * 4u;
    float4 World4  = float4(OffsetPos, 1.0f);
    float4 Clip    = float4(dot(FrameHeader[RowBase + 0u], World4),
                            dot(FrameHeader[RowBase + 1u], World4),
                            dot(FrameHeader[RowBase + 2u], World4),
                            dot(FrameHeader[RowBase + 3u], World4));
    float3 Ndc     = Clip.xyz / max(Clip.w, 1.0e-6f);
    // Notice: Vulkan viewport maps Clip.y directly to UV.y = 0.5 * Ndc.y + 0.5
    float2 Uv      = float2(0.5f * Ndc.x + 0.5f, 0.5f * Ndc.y + 0.5f);

    if (any(Uv <= float2(InvRes * 2.0f)) || any(Uv >= float2(1.0f - InvRes * 2.0f)) || Ndc.z <= 0.0f || Ndc.z >= 1.0f)
    {
        return 1.0f;
    }

    float  RefDepth  = Ndc.z - 0.0012f;
    float2 TexCoord  = Uv * ShadowRes - 0.5f;
    float2 BaseTexel = floor(TexCoord);
    float2 Frac      = TexCoord - BaseTexel;

    // 4x4 texel footprint -> 3x3 bilinear-interpolated percentage-closer taps (100% deterministic, zero grain!)
    float LitSum    = 0.0f;
    float WeightSum = 0.0f;
    [unroll]
    for (int Dy = -1; Dy <= 2; ++Dy)
    {
        float Wy = (Dy == -1) ? (1.0f - Frac.y) : ((Dy == 2) ? Frac.y : 1.0f);
        [unroll]
        for (int Dx = -1; Dx <= 2; ++Dx)
        {
            float  Wx      = (Dx == -1) ? (1.0f - Frac.x) : ((Dx == 2) ? Frac.x : 1.0f);
            float2 TapUv   = (BaseTexel + float2(float(Dx), float(Dy)) + 0.5f) * InvRes;
            float  StoredZ = ShadowDepthArray.SampleLevel(float3(TapUv, float(Cascade)), 0.0f).r;
            float  Lit     = (RefDepth <= StoredZ) ? 1.0f : 0.0f;
            float  W       = Wx * Wy;
            LitSum        += Lit * W;
            WeightSum     += W;
        }
    }
    return (WeightSum > 1.0e-5f) ? (LitSum / WeightSum) : 1.0f;
}

float ResolveDeterministicSolarVisibility(Sampler2DArray           ShadowDepthArray,
                                          StructuredBuffer<float4> FrameHeader,
                                          float3                   WorldPos,
                                          float3                   SurfaceNormal,
                                          float                    CameraDistanceM)
{
    uint CascadeCount = uint(FrameHeader[32].w + 0.5f);
    if (CascadeCount == 0u || FrameHeader[33].w < 0.5f)
    {
        return 1.0f;
    }

    float3 Splits  = FrameHeader[32].xyz;
    uint   Cascade = 0u;
    if (CameraDistanceM > Splits.x)
    {
        Cascade = (CameraDistanceM > Splits.y) ? 2u : 1u;
    }
    if (Cascade >= CascadeCount || CameraDistanceM > Splits.z)
    {
        return 1.0f;
    }

    float Vis0     = SampleSingleCascadePcf(ShadowDepthArray, FrameHeader, Cascade, WorldPos, SurfaceNormal);
    float SplitFar = FrameHeader[32][Cascade];
    if (Cascade + 1u < CascadeCount)
    {
        float Blend = saturate((CameraDistanceM - 0.82f * SplitFar) / max(0.18f * SplitFar, 0.1f));
        if (Blend > 1.0e-3f)
        {
            float Vis1 = SampleSingleCascadePcf(ShadowDepthArray, FrameHeader, Cascade + 1u, WorldPos, SurfaceNormal);
            Vis0       = lerp(Vis0, Vis1, Blend);
        }
    }
    return Vis0;
}

// Deterministic noise-free evaluation of solar + rectangular area softboxes + point/omni lights
float3 EvaluateDeterministicDirectRadiance(StructuredBuffer<float4> FrameHeader,
                                           Sampler2DArray           ShadowDepthArray,
                                           Sampler3D                SdfClipmapVolume,
                                           StructuredBuffer<float4> DistanceOrRangeTable,
                                           UnpackedSurfaceMaterial  Mat,
                                           float3                   WorldPos,
                                           float                    HitT,
                                           float3                   N,
                                           float3                   T,
                                           float3                   B,
                                           float3                   ViewDir,
                                           out float                OutPrimaryShadow)
{
    OutPrimaryShadow     = 1.0f;
    uint   LumCount      = uint(FrameHeader[3].w + 0.5f);
    uint   LumBase       = max(20u, uint(FrameHeader[19].w + 0.5f));
    bool   HasSdf        = (FrameHeader[7].z > 0.5f);
    bool   ClipmapActive = (FrameHeader[12].y > 0.5f);
    float  GroundHeightM = FrameHeader[19].z;
    float3 DirectTotal   = float3(0.0f);
    float3 ReflRay       = reflect(-ViewDir, N);

    for (uint Idx = 0u; Idx < LumCount; ++Idx)
    {
        uint   Base = LumBase + Idx * 4u;
        float4 L0   = FrameHeader[Base + 0u];
        float4 L1   = FrameHeader[Base + 1u];
        float4 L2   = FrameHeader[Base + 2u];
        float3 Emit = FrameHeader[Base + 3u].xyz;
        uint   Type = uint(L0.w + 0.5f);

        if (Type == 0u) // Directional solar luminaire with hardware 3-cascade PCF shadow map
        {
            float3 LDir  = normalize(L0.xyz);
            float  NdotL = max(0.0f, dot(N, LDir));
            if (NdotL <= 0.0f)
            {
                if (Idx == 0u) OutPrimaryShadow = 0.0f;
                continue;
            }
            float Vis = ResolveDeterministicSolarVisibility(ShadowDepthArray, FrameHeader, WorldPos, N, HitT);
            if (Idx == 0u)
            {
                OutPrimaryShadow = Vis;
            }
            if (Vis > 1.0e-4f)
            {
                float3 DiffBrdf, SpecBrdf;
                EvaluateSurfaceSplitBrdf(Mat, N, T, B, ViewDir, LDir, DiffBrdf, SpecBrdf);
                DirectTotal += (DiffBrdf + SpecBrdf) * Emit * (Vis * NdotL);
            }
        }
        else if (Type == 1u) // Rectangular area softbox luminaire: analytic solid-angle diffuse + Karis representative-point specular
        {
            float3 Center  = L0.xyz;
            float3 AxisU   = L1.xyz;
            float3 AxisV   = L2.xyz;
            float  HalfU   = max(1.0e-3f, length(AxisU));
            float  HalfV   = max(1.0e-3f, length(AxisV));
            float3 UnitU   = AxisU / HalfU;
            float3 UnitV   = AxisV / HalfV;
            float3 EmitN   = normalize(cross(UnitU, UnitV));
            if (dot(EmitN, WorldPos - Center) < 0.0f)
            {
                EmitN = -EmitN;
            }

            // 1. Analytic 5-tap stratified quadrature over rectangle for zero-noise diffuse area irradiance
            static const float2 RectOffsets[5] = {
                float2(0.0f, 0.0f),
                float2(-0.58f, -0.58f),
                float2(0.58f, -0.58f),
                float2(-0.58f, 0.58f),
                float2(0.58f, 0.58f),
            };
            float  AreaM2    = 4.0f * HalfU * HalfV;
            float3 DiffAccum = float3(0.0f);
            [unroll]
            for (uint Tap = 0u; Tap < 5u; ++Tap)
            {
                float3 Pt     = Center + AxisU * RectOffsets[Tap].x + AxisV * RectOffsets[Tap].y;
                float3 ToPt   = Pt - WorldPos;
                float  DistSq = max(0.04f, dot(ToPt, ToPt));
                float  Dist   = sqrt(DistSq);
                float3 LDir   = ToPt / Dist;
                float  NdotL  = max(0.0f, dot(N, LDir));
                float  CosEm  = max(0.0f, dot(EmitN, -LDir));
                if (NdotL > 0.0f && CosEm > 0.0f)
                {
                    float SolidAngle = (AreaM2 * 0.2f * CosEm) / DistSq;
                    float3 DiffB, UnusedSpec;
                    EvaluateSurfaceSplitBrdf(Mat, N, T, B, ViewDir, LDir, DiffB, UnusedSpec);
                    DiffAccum += DiffB * Emit * (NdotL * min(SolidAngle, 1.25f));
                }
            }

            // 2. Karis representative-point closest point on 3D rectangle to reflection ray R = reflect(-V, N)
            float  Denom   = dot(EmitN, ReflRay);
            float  PlaneT  = (abs(Denom) > 1.0e-4f) ? dot(Center - WorldPos, EmitN) / Denom : -1.0f;
            float3 PlanePt = (PlaneT > 0.0f) ? (WorldPos + ReflRay * PlaneT)
                                             : (Center + ReflRay * max(0.0f, dot(WorldPos - Center, ReflRay)));
            float3 RelRect = PlanePt - Center;
            float  ClampedU = clamp(dot(RelRect, UnitU), -HalfU, HalfU);
            float  ClampedV = clamp(dot(RelRect, UnitV), -HalfV, HalfV);
            float3 RepPt    = Center + UnitU * ClampedU + UnitV * ClampedV;

            float3 ToRep    = RepPt - WorldPos;
            float  RepDist2 = max(0.06f, dot(ToRep, ToRep));
            float  RepDist  = sqrt(RepDist2);
            float3 RepDir   = ToRep / RepDist;
            float  RepNdotL = max(0.0f, dot(N, RepDir));
            float  RepCosEm = max(0.0f, dot(EmitN, -RepDir));
            float3 SpecTerm = float3(0.0f);
            if (RepNdotL > 0.0f && RepCosEm > 0.0f)
            {
                UnpackedSurfaceMaterial AreaMat = Mat;
                float AvgRadius       = sqrt(HalfU * HalfV);
                float SolidAngleNorm  = saturate(AreaM2 * RepCosEm / (RepDist2 * 3.14159265f + AreaM2));
                AreaMat.BaseRoughness = clamp(Mat.BaseRoughness + 0.35f * AvgRadius / RepDist, 0.045f, 1.0f);
                AreaMat.CoatRoughness = clamp(Mat.CoatRoughness + 0.25f * AvgRadius / RepDist, 0.045f, 1.0f);
                float3 UnusedDiff, SpecB;
                EvaluateSurfaceSplitBrdf(AreaMat, N, T, B, ViewDir, RepDir, UnusedDiff, SpecB);
                SpecTerm = SpecB * Emit * (RepNdotL * min(AreaM2 * RepCosEm / RepDist2, 1.4f)) * (0.35f + 0.65f * SolidAngleNorm);
            }

            float SoftOcc = 1.0f;
            if (HasSdf)
            {
                float3 ToCenter = Center - WorldPos;
                float  CDist    = max(0.05f, length(ToCenter));
                SoftOcc         = EvaluateHardwareSdfConeVisibility(
                    SdfClipmapVolume, DistanceOrRangeTable, ClipmapActive, GroundHeightM, WorldPos, N, ToCenter / CDist, CDist, 0.14f);
            }
            DirectTotal += (DiffAccum + SpecTerm) * SoftOcc;
        }
        else // Point / Sphere thermal & dynamic emitter
        {
            float3 ToLight = L0.xyz - WorldPos;
            float  DistSq  = max(0.05f, dot(ToLight, ToLight));
            float  Dist    = sqrt(DistSq);
            float3 LDir    = ToLight / Dist;
            float  NdotL   = max(0.0f, dot(N, LDir));
            if (NdotL > 0.0f)
            {
                float Occ = HasSdf
                                ? EvaluateHardwareSdfConeVisibility(
                                      SdfClipmapVolume, DistanceOrRangeTable, ClipmapActive, GroundHeightM, WorldPos, N, LDir, Dist, 0.08f)
                                : 1.0f;
                float3 DiffB, SpecB;
                EvaluateSurfaceSplitBrdf(Mat, N, T, B, ViewDir, LDir, DiffB, SpecB);
                DirectTotal += (DiffB + SpecB) * (Emit / DistSq) * (NdotL * Occ);
            }
        }
    }
    return DirectTotal;
}
