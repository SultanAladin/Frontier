//==========================================================================================================================================
//                                                     SURFACE RASTER
//==========================================================================================================================================
// 🧩 Hardware triangle primary visibility & forward curved dielectric glazing rasterization with hardware ddx/ddy derivatives:
//    - Infinite Reversed-Z (VK_FORMAT_D32_SFLOAT, Near = 0.02 m -> z_ndc = 1.0, Far -> 0.0) sub-pixel matched to compute camera
//    - Unconditional 2x2 fragment quad derivatives ddx/ddy driving trilinear mipmaps (SampleGrad) & LEAN/Toksvig flake anti-sparkle
//    - ForwardRaster curved automotive glazing & crystal vessel 2-interface Snell refraction + Fresnel reflection

#include "BoundingVolumeSearch.slang"
#include "OctetVolumeSearch.slang"
#include "SurfaceRadiositySampling.slang"
#include "SurfaceScatteringEvaluation.slang"

[[vk::binding(0, 0)]] StructuredBuffer<float4> FrameHeader;
[[vk::binding(1, 0)]] StructuredBuffer<float4> BvhOrTopCells;
[[vk::binding(2, 0)]] StructuredBuffer<float4> BvhTriangles;
[[vk::binding(3, 0)]] StructuredBuffer<float4> MaterialTable;
[[vk::binding(4, 0)]] StructuredBuffer<float4> OctetInstances;
[[vk::binding(5, 0)]] StructuredBuffer<float4> OctetBlocks;
[[vk::binding(6, 0)]] Sampler2DArray           MaterialMaps;

struct RasterPushData
{
    uint4 Words0; // x = TriOffset, y = InstanceIndex (0xFFFFFFFFu = world-space), z = PassMode, w = TwoLevelActive
    uint4 Words1; // x = ScreenWidth, y = ScreenHeight, z = FrameIndex, w = Reserved
};

[[vk::push_constant]] RasterPushData Push;

struct SurfaceVarying
{
    float4              ClipPos      : SV_Position;
    float3              WorldPos     : TEXCOORD0;
    float3              GeoNormal    : TEXCOORD1;
    float3              WorldTangent : TEXCOORD2;
    float2              TexCoord     : TEXCOORD3;
    nointerpolation uint ClassId     : TEXCOORD4;
    nointerpolation uint InstIdx     : TEXCOORD5;
    nointerpolation uint GlobalTri   : TEXCOORD6;
};

struct SurfaceFragmentTargets
{
    float4 G0 : SV_Target0;
    float4 G1 : SV_Target1;
    float4 G2 : SV_Target2;
    float4 G3 : SV_Target3;
    float4 G4 : SV_Target4;
    float4 G5 : SV_Target5;
};

float4 ProjectWorldToReversedZClip(float3 WorldPos)
{
    float3 CamOrig  = FrameHeader[0].xyz;
    float  TanHalfV = FrameHeader[0].w;
    float3 CamRight = FrameHeader[1].xyz;
    float  TanHalfH = FrameHeader[1].w;
    float3 CamUp    = FrameHeader[2].xyz;
    float3 CamFwd   = FrameHeader[3].xyz;

    float3 Rel   = WorldPos - CamOrig;
    float  ViewX = dot(Rel, CamRight);
    float  ViewY = dot(Rel, CamUp);
    float  ViewZ = dot(Rel, CamFwd);

    // Infinite Reversed-Z: z_near = 0.02 m maps to z_ndc = 1.0, ViewZ -> +inf maps to z_ndc = 0.0
    const float NearPlaneM = 0.02f;
    return float4(ViewX / max(TanHalfH, 1.0e-6f),
                  -ViewY / max(TanHalfV, 1.0e-6f),
                  NearPlaneM,
                  ViewZ);
}

SurfaceVarying BuildRasterVarying(uint VertexId, bool RequireGlass)
{
    SurfaceVarying Out;
    uint LocalTri  = VertexId / 3u;
    uint Corner    = VertexId % 3u;
    uint GlobalTri = Push.Words0.x + LocalTri;
    uint InstIdx   = Push.Words0.y;

    uint   TAddr     = GlobalTri * OctetTriangleStride;
    float4 T0        = BvhTriangles[TAddr + 0u];
    float4 T1        = BvhTriangles[TAddr + 1u];
    float4 T2        = BvhTriangles[TAddr + 2u];
    float4 T3        = BvhTriangles[TAddr + 3u];
    uint   ClassPack = asuint(T2.w);
    uint   ClassBase = ClassPack & 0x7FFFFFFFu;
    uint   ClassOv   = 0xFFFFFFFFu;

    if (InstIdx != 0xFFFFFFFFu)
    {
        ClassOv = asuint(OctetInstances[InstIdx * OctetInstanceStride + 7u].z);
    }

    uint ResolvedClass = (ClassOv != 0xFFFFFFFFu) ? ClassOv : ClassBase;
    bool IsGlass       = IsOctetTransmissiveClass(ClassPack, ClassOv) || FetchSurfaceMaterial(MaterialTable, ResolvedClass).TransmissiveGlass;

    if (IsGlass != RequireGlass)
    {
        Out.ClipPos      = float4(2.0f, 2.0f, -1.0f, 1.0f);
        Out.WorldPos     = float3(0.0f);
        Out.GeoNormal    = float3(0.0f, 1.0f, 0.0f);
        Out.WorldTangent = float3(1.0f, 0.0f, 0.0f);
        Out.TexCoord     = float2(0.0f);
        Out.ClassId      = ResolvedClass;
        Out.InstIdx      = InstIdx;
        Out.GlobalTri    = GlobalTri;
        return Out;
    }

    float3 LocalPos = T0.xyz;
    float2 Uv       = float2(T0.w, T1.w);
    if (Corner == 1u)
    {
        LocalPos += T1.xyz;
        Uv        = T3.xy;
    }
    else if (Corner == 2u)
    {
        LocalPos += T2.xyz;
        Uv        = T3.zw;
    }

    float3 LocalN = normalize(cross(T1.xyz, T2.xyz));
    float2 Uv0    = float2(T0.w, T1.w);
    float2 Du1    = T3.xy - Uv0;
    float2 Du2    = T3.zw - Uv0;
    float  DetUv  = Du1.x * Du2.y - Du2.x * Du1.y;
    float3 LocalT = (abs(DetUv) > 1.0e-7f) ? normalize((Du2.y * T1.xyz - Du1.y * T2.xyz) / DetUv)
                                           : ((length(T1.xyz) > 1.0e-6f) ? normalize(T1.xyz) : float3(1.0f, 0.0f, 0.0f));

    float3 WorldPos = LocalPos;
    float3 WorldN   = LocalN;
    float3 WorldT   = LocalT;

    if (InstIdx != 0xFFFFFFFFu)
    {
        uint   IBase = InstIdx * OctetInstanceStride;
        float4 Inv0  = OctetInstances[IBase + 0u];
        float4 Inv1  = OctetInstances[IBase + 1u];
        float4 Inv2  = OctetInstances[IBase + 2u];
        float4 Inv3  = OctetInstances[IBase + 3u];
        WorldPos     = TransformInstancePointToWorld(Inv0, Inv1, Inv2, Inv3, LocalPos);
        WorldN       = TransformInstanceNormalToWorld(Inv0, Inv1, Inv2, LocalN);
        float3 DirT  = TransformInstanceDirToWorld(Inv0, Inv1, Inv2, LocalT);
        WorldT       = (length(DirT) > 1.0e-6f) ? normalize(DirT) : OctetInstances[IBase + 6u].xyz;
    }

    float3 OrthoT = WorldT - WorldN * dot(WorldN, WorldT);
    WorldT        = (length(OrthoT) > 1.0e-6f)
                        ? normalize(OrthoT)
                        : normalize(cross(abs(WorldN.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f), WorldN));

    Out.ClipPos      = ProjectWorldToReversedZClip(WorldPos);
    Out.WorldPos     = WorldPos;
    Out.GeoNormal    = WorldN;
    Out.WorldTangent = WorldT;
    Out.TexCoord     = Uv;
    Out.ClassId      = ResolvedClass;
    Out.InstIdx      = InstIdx;
    Out.GlobalTri    = GlobalTri;
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  OPAQUE SURFACE RASTER (HARDWARE ddx/ddy MIP & FLAKE AA)
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
SurfaceVarying ProjectSurfaceVertexMain(uint VertexId : SV_VulkanVertexID)
{
    return BuildRasterVarying(VertexId, false);
}

[shader("fragment")]
SurfaceFragmentTargets EvaluateSurfaceFragmentMain(SurfaceVarying In)
{
    // Compute hardware 2x2 quad derivatives unconditionally before any material-dependent branching
    float3 WorldPosDdx = ddx(In.WorldPos);
    float3 WorldPosDdy = ddy(In.WorldPos);
    float2 UvDdx       = ddx(In.TexCoord);
    float2 UvDdy       = ddy(In.TexCoord);
    float3 NormDdx     = ddx(In.GeoNormal);
    float3 NormDdy     = ddy(In.GeoNormal);

    float3 CamOrig  = FrameHeader[0].xyz;
    float3 ViewVec  = In.WorldPos - CamOrig;
    float  DistM    = max(1.0e-3f, length(ViewVec));
    float3 RayDir   = ViewVec / DistM;
    float3 GeoN     = normalize(In.GeoNormal);
    if (dot(GeoN, RayDir) > 0.0f)
    {
        GeoN = -GeoN;
    }

    UnpackedSurfaceMaterial Mat = FetchSurfaceMaterial(MaterialTable, In.ClassId);
    float  HardwareMipLod       = 0.0f;
    float3 ShadingN, RotT, RotB;
    ResolveAuthoredMaterialHardwareDerivatives(Mat,
                                               MaterialMaps,
                                               In.WorldPos,
                                               WorldPosDdx,
                                               WorldPosDdy,
                                               In.TexCoord,
                                               UvDdx,
                                               UvDdy,
                                               DistM,
                                               GeoN,
                                               NormDdx,
                                               NormDdy,
                                               In.WorldTangent,
                                               HardwareMipLod,
                                               ShadingN,
                                               RotT,
                                               RotB);

    if (In.InstIdx != 0xFFFFFFFFu)
    {
        uint   IBase = In.InstIdx * OctetInstanceStride;
        float4 Meta  = OctetInstances[IBase + 7u];
        if (asuint(Meta.w) != 0u)
        {
            float4 Optics     = OctetInstances[IBase + 8u];
            Mat.BaseAlbedo   *= Optics.xyz;
            Mat.SelfRadiance += Mat.BaseAlbedo * Optics.w;
        }
    }

    float EncodedTierAndMip = float(Mat.MaterialLodTier) + 0.01f * clamp(floor(HardwareMipLod * 12.0f + 0.5f), 0.0f, 99.0f);

    SurfaceFragmentTargets Out;
    Out.G0 = float4(In.WorldPos, DistM);
    Out.G1 = float4(ShadingN, Mat.BaseRoughness);
    Out.G2 = float4(Mat.SelfRadiance, float(In.ClassId));
    Out.G3 = float4(Mat.Metallic, Mat.CoatCoverage, Mat.CoatRoughness, EncodedTierAndMip);
    Out.G4 = float4(RotT, Mat.Anisotropy);
    Out.G5 = float4(Mat.BaseAlbedo, Mat.AmbientOcclusion);
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                          FORWARD GLAZING RASTER (CURVED DIELECTRIC SNELL REFRACTION + FRESNEL)
//------------------------------------------------------------------------------------------------------------------------

float3 EvaluateForwardCoupledSky(float3 Dir)
{
    float3 Zenith  = FrameHeader[4].xyz;
    float3 Horizon = FrameHeader[5].xyz;
    float3 Ground  = FrameHeader[6].xyz * 0.28f;
    float  UpT     = saturate(Dir.y);
    float3 Sky     = (Dir.y >= 0.0f) ? lerp(Horizon, Zenith, pow(UpT, 0.62f))
                                     : lerp(Horizon * 0.45f, Ground, saturate(-Dir.y * 2.5f));

    uint LumCnt  = uint(FrameHeader[3].w + 0.5f);
    uint LumBase = max(20u, uint(FrameHeader[19].w + 0.5f));
    if (LumCnt > 0u && FrameHeader[LumBase].w < 0.5f)
    {
        float3 SunDir  = normalize(FrameHeader[LumBase].xyz);
        float  CosSun  = dot(Dir, SunDir);
        float  MieHalo = pow( max(0.0f, CosSun), 96.0f) * 0.35f;
        float  Disk    = smoothstep(0.99955f, 0.99985f, CosSun) * 6.0f;
        Sky           += FrameHeader[LumBase + 3u].xyz * (MieHalo + Disk);
    }
    return Sky;
}

float3 ApplyForwardHeightFog(float3 Radiance, float3 RayOrigin, float3 RayDir, float DistanceM)
{
    float FogDensity = FrameHeader[17].w;
    if (FogDensity <= 0.0f || DistanceM <= 0.01f)
    {
        return Radiance;
    }
    float  Falloff   = max(0.02f, FrameHeader[18].x);
    float  AvgHeight = max(0.0f, RayOrigin.y + 0.5f * RayDir.y * min(DistanceM, 120.0f));
    float  EffSigma  = FogDensity * exp(-AvgHeight * Falloff);
    float  Trans     = exp(-EffSigma * min(DistanceM, 240.0f));
    return Radiance * Trans + FrameHeader[17].xyz * (1.0f - Trans);
}

struct GlazingOpaqueHit
{
    bool   Hit;
    float  DistanceM;
    uint   ClassId;
    float3 WorldPos;
    float3 GeoNormal;
    float2 TexCoord;
    float3 InstanceTint;
    float  InstanceSelfRad;
};

GlazingOpaqueHit TraceGlazingSecondaryOpaque(float3 Origin, float3 RayDir, float MinT, float MaxT)
{
    GlazingOpaqueHit Out;
    uint CellCount = uint(FrameHeader[2].w + 0.5f);
    if (FrameHeader[12].x > 0.5f)
    {
        GpuOctetSurfaceHit H = SearchOctetInstancesOpaque(
            BvhOrTopCells, OctetInstances, OctetBlocks, BvhTriangles, CellCount, Origin, RayDir, MinT, MaxT);
        Out.Hit             = H.Hit;
        Out.DistanceM       = H.DistanceM;
        Out.ClassId         = H.ClassId;
        Out.WorldPos        = H.WorldPos;
        Out.GeoNormal       = (dot(H.GeoNormal, RayDir) > 0.0f) ? -H.GeoNormal : H.GeoNormal;
        Out.TexCoord        = H.TexCoord;
        Out.InstanceTint    = H.InstanceTint;
        Out.InstanceSelfRad = H.InstanceSelfRadiance;
    }
    else
    {
        GpuSurfaceHit H     = SearchBoundingVolumeOpaque(BvhOrTopCells, BvhTriangles, CellCount, Origin, RayDir, MinT, MaxT);
        Out.Hit             = H.Hit;
        Out.DistanceM       = H.DistanceM;
        Out.ClassId         = H.ClassId;
        Out.WorldPos        = H.WorldPos;
        Out.GeoNormal       = H.GeoNormal;
        Out.TexCoord        = H.TexCoord;
        Out.InstanceTint    = float3(1.0f);
        Out.InstanceSelfRad = 0.0f;
    }
    return Out;
}

float3 ShadeGlazingSecondaryHit(GlazingOpaqueHit H, float3 ViewDirToOrigin)
{
    UnpackedSurfaceMaterial Mat = FetchSurfaceMaterial(MaterialTable, H.ClassId);
    if (Mat.AlbedoLayer >= 0)
    {
        float Mip = clamp(log2(max(1.0f, H.DistanceM * 0.18f * Mat.UvTilingScale)), 0.0f, 8.0f);
        Mat.BaseAlbedo *= MaterialMaps.SampleLevel(float3(H.TexCoord * Mat.UvTilingScale, float(Mat.AlbedoLayer)), Mip).rgb;
    }
    Mat.BaseAlbedo   *= H.InstanceTint;
    Mat.SelfRadiance += Mat.BaseAlbedo * H.InstanceSelfRad;

    float3 N      = normalize(H.GeoNormal);
    float3 T      = normalize(cross(abs(N.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f), N));
    float3 B      = cross(N, T);
    float3 Result = Mat.SelfRadiance;

    uint LumCount = uint(FrameHeader[3].w + 0.5f);
    uint LumBase  = max(20u, uint(FrameHeader[19].w + 0.5f));
    if (LumCount > 0u)
    {
        float4 L0     = FrameHeader[LumBase + 0u];
        float3 LRec   = FrameHeader[LumBase + 3u].xyz;
        float3 ToSun  = (L0.w < 0.5f) ? normalize(L0.xyz) : normalize(L0.xyz - H.WorldPos);
        float  NdotL  = max(0.0f, dot(N, ToSun));
        if (NdotL > 0.0f)
        {
            uint CellCount = uint(FrameHeader[2].w + 0.5f);
            float3 ShadowT = (FrameHeader[12].x > 0.5f)
                                 ? SearchOctetInstancesShadow(BvhOrTopCells, OctetInstances, OctetBlocks, BvhTriangles, CellCount, H.WorldPos + N * 0.01f, ToSun, 400.0f)
                                 : SearchBoundingVolumeShadowTransmittance(BvhOrTopCells, BvhTriangles, CellCount, H.WorldPos + N * 0.01f, ToSun, 1.0e-3f, 400.0f);
            float3 DiffB, SpecB;
            EvaluateSurfaceSplitBrdf(Mat, N, T, B, ViewDirToOrigin, ToSun, DiffB, SpecB);
            Result += (DiffB + SpecB) * LRec * ShadowT * NdotL;
        }
    }

    uint PatchCount = uint(FrameHeader[12].w + 0.5f);
    float3 Indirect = EvaluateForwardCoupledSky(N) * FrameHeader[13].w;
    Result += Mat.BaseAlbedo * (1.0f - Mat.Metallic) * Indirect * (1.0f / 3.14159265f);
    return Result;
}

[shader("vertex")]
SurfaceVarying ProjectForwardGlazingVertexMain(uint VertexId : SV_VulkanVertexID)
{
    return BuildRasterVarying(VertexId, true);
}

[shader("fragment")]
SurfaceFragmentTargets EvaluateForwardGlazingFragmentMain(SurfaceVarying In)
{
    float3 NormDdx     = ddx(In.GeoNormal);
    float3 NormDdy     = ddy(In.GeoNormal);
    float  CurvRough   = sqrt(0.045f * 0.045f + 0.15f * min(0.04f, dot(NormDdx, NormDdx) + dot(NormDdy, NormDdy)));

    float3 CamOrig     = FrameHeader[0].xyz;
    float3 ViewVec     = In.WorldPos - CamOrig;
    float  GlassFrontT = max(1.0e-3f, length(ViewVec));
    float3 RayDir      = ViewVec / GlassFrontT;
    float3 FrontN      = normalize(In.GeoNormal);
    if (dot(FrontN, RayDir) > 0.0f)
    {
        FrontN = -FrontN;
    }

    UnpackedSurfaceMaterial Mat = FetchSurfaceMaterial(MaterialTable, In.ClassId);
    float4 T4          = BvhTriangles[In.GlobalTri * OctetTriangleStride + 4u];
    float  GlassIor    = (In.ClassId == 18u) ? 1.62f : ((In.ClassId == 12u) ? 1.58f : max(1.02f, Mat.GlassIor));
    float3 GlassAbsorb = (In.ClassId == 18u) ? float3(0.06f, 0.03f, 0.02f)
                                             : ((In.ClassId == 12u) ? float3(0.15f, 0.12f, 0.08f)
                                                                    : ((length(T4.xyz) > 1.0e-4f) ? T4.xyz : float3(0.08f, 0.03f, 0.05f)));
    float  ThicknessM  = (In.ClassId == 18u) ? 0.18f : 0.014f;
    float3 BackN       = -FrontN;

    // Peel exact second interface thickness/normal on closed curved glass vessels when two-level BVH is active
    uint CellCount = uint(FrameHeader[2].w + 0.5f);
    if (FrameHeader[12].x > 0.5f && CellCount > 0u)
    {
        GpuOctetGlassPeel P = SearchOctetInstancesGlassPeel(
            BvhOrTopCells, OctetInstances, OctetBlocks, BvhTriangles, CellCount, CamOrig, RayDir, GlassFrontT + 1.2f);
        if (P.HitGlass)
        {
            ThicknessM  = P.ThicknessM;
            BackN       = P.BackNormal;
            GlassIor    = P.Ior;
            GlassAbsorb = P.AbsorptionPerM;
        }
    }

    float  R1    = 0.04f;
    float3 DirIn = RayDir;
    bool   Tir1  = false;
    EvaluateExactDielectricBoundary(RayDir, FrontN, 1.0f, GlassIor, R1, DirIn, Tir1);

    float3 BackPos  = In.WorldPos + DirIn * ThicknessM;
    float3 ExitNorm = (dot(-DirIn, BackN) > 0.05f) ? BackN : FrontN;
    float  R2       = 0.04f;
    float3 DirExit  = DirIn;
    bool   Tir2     = false;
    EvaluateExactDielectricBoundary(DirIn, ExitNorm, GlassIor, 1.0f, R2, DirExit, Tir2);
    if (Tir2)
    {
        float3 InternalRefl = reflect(DirIn, ExitNorm);
        DirExit             = normalize(lerp(RayDir, InternalRefl, 0.45f));
        ThicknessM         *= 2.2f;
    }

    float3           BeerLambert = exp(-GlassAbsorb * ThicknessM * 4.0f);
    GlazingOpaqueHit Behind      = TraceGlazingSecondaryOpaque(BackPos + DirExit * 0.008f, DirExit, 1.0e-3f, 2000.0f);
    float3           TransRad    = Behind.Hit
                                       ? ApplyForwardHeightFog(ShadeGlazingSecondaryHit(Behind, -DirExit), BackPos, DirExit, Behind.DistanceM)
                                       : EvaluateForwardCoupledSky(DirExit);

    float3           ReflDir = reflect(RayDir, FrontN);
    GlazingOpaqueHit ReflHit = TraceGlazingSecondaryOpaque(In.WorldPos + FrontN * 0.01f, ReflDir, 1.0e-3f, 1500.0f);
    float3           ReflRad = ReflHit.Hit ? ShadeGlazingSecondaryHit(ReflHit, -ReflDir) : EvaluateForwardCoupledSky(ReflDir);

    float  EffectiveR = saturate(R1 + (1.0f - R1) * R2 * 0.65f);
    float3 GlassRad   = ReflRad * EffectiveR + TransRad * BeerLambert * (1.0f - EffectiveR);

    SurfaceFragmentTargets Out;
    Out.G0 = float4(In.WorldPos, -GlassFrontT);
    Out.G1 = float4(FrontN, CurvRough);
    Out.G2 = float4(GlassRad, float(In.ClassId));
    Out.G3 = float4(0.0f, 0.0f, CurvRough, EffectiveR);
    Out.G4 = float4(1.0f, 0.0f, 0.0f, 0.0f);
    Out.G5 = float4(BeerLambert, 1.0f);
    return Out;
}
