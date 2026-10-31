//==========================================================================================================================================
//                                                    DIAGNOSTIC RASTER SLANG
//==========================================================================================================================================
// Editor & Development ONLY GPU diagnostic rasterizer (never compiled or shipped in Shipping builds):
// Pass 1 (BinDiagnosticTilesMain): Bins active cut clusters into 16x16 screen tiles via projected cluster bounds.
// Pass 2 (ResolveDiagnosticViewMain): Exact perspective-correct triangle visibility + 2-layer glass peel + simple sky/atmosphere
//                                     backdrop + 3-tier virtualized material shading + 10 diagnostic inspection modes.

struct DiagnosticPushHeader
{
    uint4  Counts; // x = Width, y = Height, z = ActiveClusterCount, w = ViewMode
    float4 Solar;  // xyz = SolarDirectionUnit, w = ExposureScale
};

[[vk::push_constant]] DiagnosticPushHeader Push;

// CameraAndSky[0]: OriginXYZ, TanHalfV
// CameraAndSky[1]: RightXYZ, TanHalfH
// CameraAndSky[2]: UpXYZ, BaseErrorThresholdPx
// CameraAndSky[3]: ForwardXYZ, MaxHierarchyTier
// CameraAndSky[4]: ZenithSkyRgb, GroundShadowEnabled
// CameraAndSky[5]: HorizonSkyRgb, WireframeOpacity
//
// Per-cluster GPU header (4 float4s per active cut cluster, starting at slot 6):
//   Slot + 0: ScreenBoundsPx (MinX, MinY, MaxX, MaxY)
//   Slot + 1: TriStartIndex, TriCount, GlobalClusterId, HierarchyTierIndex
//   Slot + 2: AlbedoRgb, EffectiveRoughness
//   Slot + 3: Metallic, ClearcoatOrIor, PackedFlags (bit0 = Glass, bits 1..2 = MaterialTier), ProjectedErrorPx
[[vk::binding(0, 0)]] StructuredBuffer<float4>                     ClusterSceneHeader;

// Packed world-space triangles (4 float4s per triangle):
//   Tri + 0: P0.xyz, U0
//   Tri + 1: P1.xyz, V0
//   Tri + 2: P2.xyz, U1
//   Tri + 3: V1, U2, V2, AbsorptionStrength
[[vk::binding(1, 0)]] StructuredBuffer<float4>                     TriangleGeometry;

// Tile bin table: 64 uints per 16x16 tile: [0] = Count (<= 63), [1..63] = ActiveClusterIdx
[[vk::binding(2, 0)]] RWStructuredBuffer<uint>                     TileClusterBins;

[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture2DArray<float4> DiagnosticTarget;

static const uint  kTileSizePx       = 16u;
static const uint  kTileSlotStride   = 64u;
static const uint  kClusterBaseSlot  = 6u;
static const uint  kClusterStride    = 4u;
static const uint  kTriangleStride   = 4u;
static const float π                 = 3.14159265358979323846f;

float3 SafeNorm(float3 V)
{
    float LenSq = dot(V, V);
    return (LenSq > 1.0e-20f) ? (V * rsqrt(LenSq)) : float3(0.0f, 1.0f, 0.0f);
}

float3 EvaluateSimpleDiagnosticSky(float3 RayDir, float3 SunDir, float3 ZenithRgb, float3 HorizonRgb)
{
    float  HorizonT   = pow(1.0f - saturate(max(RayDir.y, 0.0f)), 3.2f);
    float3 BaseGrad   = lerp(ZenithRgb, HorizonRgb, HorizonT);
    if (RayDir.y < 0.0f)
    {
        float GroundFade = saturate(-RayDir.y * 5.0f);
        BaseGrad         = lerp(HorizonRgb, float3(0.09f, 0.10f, 0.12f), GroundFade);
    }

    float cosθ       = dot(RayDir, SunDir);
    float MieGlow    = 0.24f * pow(saturate(0.5f + 0.5f * cosθ), 10.0f);
    float Corona     = 0.85f * exp(-max(0.0f, 1.0f - cosθ) * 650.0f);
    float SolarDisc  = smoothstep(0.99986f, 0.99993f, cosθ) * 14.0f;
    return BaseGrad + float3(1.05f, 0.95f, 0.82f) * (MieGlow + Corona + SolarDisc);
}

float3 HashClusterColor(uint ClusterId)
{
    uint H = (ClusterId + 1u) * 0x9E3779B1u;
    H ^= H >> 16u;
    H *= 0x85EBCA6Bu;
    H ^= H >> 13u;
    return float3(
        0.24f + 0.68f * float((H >> 0u) & 0xFFu) / 255.0f,
        0.24f + 0.68f * float((H >> 8u) & 0xFFu) / 255.0f,
        0.24f + 0.68f * float((H >> 16u) & 0xFFu) / 255.0f);
}

float3 EvaluateHeatGradient(float NormalizedValue)
{
    float T = saturate(NormalizedValue);
    // Cool slate-blue (0.0) -> cyan-azure (0.33) -> warm amber (0.68) -> coral-crimson (1.0)
    if (T < 0.33f)
    {
        return lerp(float3(0.16f, 0.30f, 0.68f), float3(0.18f, 0.68f, 0.88f), T / 0.33f);
    }
    if (T < 0.68f)
    {
        return lerp(float3(0.18f, 0.68f, 0.88f), float3(0.94f, 0.72f, 0.20f), (T - 0.33f) / 0.35f);
    }
    return lerp(float3(0.94f, 0.72f, 0.20f), float3(0.94f, 0.24f, 0.22f), (T - 0.68f) / 0.32f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void BinDiagnosticTilesMain(uint3 Tid : SV_DispatchThreadID)
{
    uint TilesX = (Push.Counts.x + kTileSizePx - 1u) / kTileSizePx;
    uint TilesY = (Push.Counts.y + kTileSizePx - 1u) / kTileSizePx;
    if (Tid.x >= TilesX || Tid.y >= TilesY)
    {
        return;
    }

    float TileMinX = float(Tid.x * kTileSizePx);
    float TileMinY = float(Tid.y * kTileSizePx);
    float TileMaxX = TileMinX + float(kTileSizePx);
    float TileMaxY = TileMinY + float(kTileSizePx);

    uint BaseIdx        = (Tid.y * TilesX + Tid.x) * kTileSlotStride;
    uint MatchedCount   = 0u;
    uint ActiveClusters = Push.Counts.z;

    for (uint ClIdx = 0u; ClIdx < ActiveClusters; ++ClIdx)
    {
        float4 Bounds = ClusterSceneHeader[kClusterBaseSlot + ClIdx * kClusterStride + 0u];
        if (Bounds.z < TileMinX || Bounds.x > TileMaxX || Bounds.w < TileMinY || Bounds.y > TileMaxY)
        {
            continue;
        }
        if (MatchedCount + 1u < kTileSlotStride)
        {
            ++MatchedCount;
            TileClusterBins[BaseIdx + MatchedCount] = ClIdx;
        }
    }
    TileClusterBins[BaseIdx] = MatchedCount;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ResolveDiagnosticViewMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    float3 CamOrigin = ClusterSceneHeader[0].xyz;
    float  TanHalfV  = ClusterSceneHeader[0].w;
    float3 CamRight  = ClusterSceneHeader[1].xyz;
    float  TanHalfH  = ClusterSceneHeader[1].w;
    float3 CamUp     = ClusterSceneHeader[2].xyz;
    float  ErrRefPx  = max(ClusterSceneHeader[2].w, 0.1f);
    float3 CamFwd    = ClusterSceneHeader[3].xyz;
    float  MaxTier   = max(ClusterSceneHeader[3].w, 1.0f);
    float3 ZenithRgb = ClusterSceneHeader[4].xyz;
    float3 HorizRgb  = ClusterSceneHeader[5].xyz;

    float2 PixelCenter = float2(Tid.xy) + 0.5f;
    float  NdcX        = (PixelCenter.x / float(Push.Counts.x)) * 2.0f - 1.0f;
    float  NdcY        = 1.0f - (PixelCenter.y / float(Push.Counts.y)) * 2.0f;
    float3 RayDir      = SafeNorm(CamFwd + CamRight * (NdcX * TanHalfH) + CamUp * (NdcY * TanHalfV));
    float3 SunDir      = SafeNorm(Push.Solar.xyz);
    uint   ViewMode    = Push.Counts.w;

    uint TilesX       = (Push.Counts.x + kTileSizePx - 1u) / kTileSizePx;
    uint TileX        = Tid.x / kTileSizePx;
    uint TileY        = Tid.y / kTileSizePx;
    uint TileBase     = (TileY * TilesX + TileX) * kTileSlotStride;
    uint TileClusters = TileClusterBins[TileBase];

    // Nearest opaque hit + two-layer (front + back) transmissive glass peel
    float  OpaqueT          = 1.0e30f;
    uint   OpaqueClusterIdx = 0u;
    float3 OpaqueNormal     = float3(0.0f, 1.0f, 0.0f);
    float2 OpaqueUv         = float2(0.0f);
    float  OpaqueEdgePx     = 1.0e6f;

    float  GlassFrontT      = 1.0e30f;
    float  GlassBackT       = 0.0f;
    uint   GlassLayerCount  = 0u;
    uint   GlassClusterIdx  = 0u;
    float3 GlassFrontNormal = float3(0.0f, 1.0f, 0.0f);
    float  GlassEdgePx      = 1.0e6f;
    float  GlassAbsorbRate  = 0.12f;

    for (uint Slot = 0u; Slot < TileClusters; ++Slot)
    {
        uint   ClIdx    = TileClusterBins[TileBase + 1u + Slot];
        uint   ClBase   = kClusterBaseSlot + ClIdx * kClusterStride;
        float4 Meta1    = ClusterSceneHeader[ClBase + 1u];
        float4 Meta3    = ClusterSceneHeader[ClBase + 3u];
        uint   TriStart = uint(Meta1.x + 0.5f);
        uint   TriCount = uint(Meta1.y + 0.5f);
        uint   Flags    = uint(Meta3.z + 0.5f);
        bool   IsGlass  = (Flags & 1u) != 0u;

        for (uint LocalTri = 0u; LocalTri < TriCount; ++LocalTri)
        {
            uint   TriBase = (TriStart + LocalTri) * kTriangleStride;
            float4 T0      = TriangleGeometry[TriBase + 0u];
            float4 T1      = TriangleGeometry[TriBase + 1u];
            float4 T2      = TriangleGeometry[TriBase + 2u];
            float4 T3      = TriangleGeometry[TriBase + 3u];

            float3 P0  = T0.xyz;
            float3 P1  = T1.xyz;
            float3 P2  = T2.xyz;
            float3 E1  = P1 - P0;
            float3 E2  = P2 - P0;
            float3 Pvec = cross(RayDir, E2);
            float  Det  = dot(E1, Pvec);
            if (abs(Det) <= 1.0e-9f)
            {
                continue;
            }
            float  InvDet = 1.0f / Det;
            float3 Tvec   = CamOrigin - P0;
            float  BaryU  = dot(Tvec, Pvec) * InvDet;
            if (BaryU < 0.0f || BaryU > 1.0f)
            {
                continue;
            }
            float3 Qvec  = cross(Tvec, E1);
            float  BaryV = dot(RayDir, Qvec) * InvDet;
            if (BaryV < 0.0f || (BaryU + BaryV) > 1.0f)
            {
                continue;
            }
            float HitT = dot(E2, Qvec) * InvDet;
            if (HitT <= 0.04f)
            {
                continue;
            }

            // Screen-space pixel distance to closest triangle edge for crisp 1 px diagnostic wireframe
            float  BaryW     = 1.0f - BaryU - BaryV;
            float  PixelSize = (2.0f * TanHalfV * HitT) / max(float(Push.Counts.y), 1.0f);
            float  Area2     = length(cross(E1, E2));
            float  H0        = Area2 / max(length(P2 - P1), 1.0e-5f);
            float  H1        = Area2 / max(length(E2), 1.0e-5f);
            float  H2        = Area2 / max(length(E1), 1.0e-5f);
            float  EdgeDistM = min(BaryW * H0, min(BaryU * H1, BaryV * H2));
            float  EdgePx    = EdgeDistM / max(PixelSize, 1.0e-5f);
            float3 GeoNorm   = SafeNorm(cross(E1, E2));
            if (dot(GeoNorm, RayDir) > 0.0f)
            {
                GeoNorm = -GeoNorm;
            }

            if (IsGlass)
            {
                ++GlassLayerCount;
                if (HitT < GlassFrontT)
                {
                    GlassBackT       = min(GlassFrontT, 1.0e20f);
                    GlassFrontT      = HitT;
                    GlassClusterIdx  = ClIdx;
                    GlassFrontNormal = GeoNorm;
                    GlassEdgePx      = EdgePx;
                    GlassAbsorbRate  = T3.w;
                }
                else if (HitT > GlassFrontT + 0.005f)
                {
                    GlassBackT = max(GlassBackT, HitT);
                }
            }
            else if (HitT < OpaqueT)
            {
                OpaqueT          = HitT;
                OpaqueClusterIdx = ClIdx;
                OpaqueNormal     = GeoNorm;
                OpaqueEdgePx     = EdgePx;
                float2 Uv0       = float2(T0.w, T1.w);
                float2 Uv1       = float2(T2.w, T3.x);
                float2 Uv2       = float2(T3.y, T3.z);
                OpaqueUv         = Uv0 * BaryW + Uv1 * BaryU + Uv2 * BaryV;
            }
        }
    }

    bool HasOpaque = (OpaqueT < 1.0e25f);
    bool HasGlass  = (GlassFrontT < OpaqueT);

    float3 SkyRadiance = EvaluateSimpleDiagnosticSky(RayDir, SunDir, ZenithRgb, HorizRgb);
    if (!HasOpaque && !HasGlass)
    {
        if (ViewMode == 2u) // Blueprint dark slate backdrop in pure wireframe mode
        {
            SkyRadiance = float3(0.032f, 0.038f, 0.048f);
        }
        DiagnosticTarget[uint3(Tid.xy, 0u)] = float4(SkyRadiance, 1.0f);
        return;
    }

    float3 ResolvedColor = SkyRadiance;

    if (HasOpaque)
    {
        uint   ClBase      = kClusterBaseSlot + OpaqueClusterIdx * kClusterStride;
        float4 Meta1       = ClusterSceneHeader[ClBase + 1u];
        float4 Meta2       = ClusterSceneHeader[ClBase + 2u];
        float4 Meta3       = ClusterSceneHeader[ClBase + 3u];
        uint   GlobalId    = uint(Meta1.z + 0.5f);
        float  TierIndex   = Meta1.w;
        float3 Albedo      = Meta2.xyz;
        float  Roughness   = clamp(Meta2.w, 0.04f, 1.0f);
        float  Metallic    = saturate(Meta3.x);
        float  Clearcoat   = saturate(Meta3.y);
        uint   Flags       = uint(Meta3.z + 0.5f);
        uint   MatTier     = (Flags >> 1u) & 3u;
        float  ProjErrPx   = Meta3.w;
        float3 HitPos      = CamOrigin + RayDir * OpaqueT;

        // Subtle UV grid micro-modulation on FullStrata (Tier 0) to make 3-tier material scaling visually verifiable
        if (MatTier == 0u)
        {
            float2 GridCell = frac(OpaqueUv * 4.0f);
            float  Micro    = 0.94f + 0.10f * smoothstep(0.04f, 0.12f, min(GridCell.x, GridCell.y));
            Albedo *= Micro;
        }

        float3 VDir   = -RayDir;
        float3 HVec   = SafeNorm(SunDir + VDir);
        float  NdotL  = max(0.0f, dot(OpaqueNormal, SunDir));
        float  NdotV  = max(0.02f, dot(OpaqueNormal, VDir));
        float  NdotH  = max(0.0f, dot(OpaqueNormal, HVec));

        // Soft analytical vehicle contact ambient-occlusion shadow on ground plane around origin
        float GroundContactAo = 1.0f;
        if (HitPos.y < 0.08f)
        {
            float2 Ellip = float2(HitPos.x / 1.32f, HitPos.z / 3.15f);
            float  Rad2  = dot(Ellip, Ellip);
            GroundContactAo = lerp(0.24f, 1.0f, smoothstep(0.45f, 1.35f, Rad2));
        }

        // GGX microfacet + clearcoat diagnostic surface evaluation paired with simple sky hemisphere irradiance
        float  Alpha    = Roughness * Roughness;
        float  AlphaSq  = Alpha * Alpha;
        float  DenomD   = NdotH * NdotH * (AlphaSq - 1.0f) + 1.0f;
        float  DistGGX  = AlphaSq / max(π * DenomD * DenomD, 1.0e-5f);
        float3 F0       = lerp(float3(0.04f), Albedo, Metallic);
        float3 Fresnel  = F0 + (1.0f - F0) * pow(1.0f - max(0.0f, dot(HVec, VDir)), 5.0f);
        float3 Diffuse  = Albedo * (1.0f - Metallic) * (1.0f / π);
        float3 Specular = Fresnel * (DistGGX * 0.25f / max(NdotV, 0.08f));

        // Clearcoat secondary lobe on FullStrata / FilteredToksvig automotive paint
        if (Clearcoat > 0.01f && MatTier < 2u)
        {
            float CoatAlphaSq = 0.0036f;
            float CoatDen     = NdotH * NdotH * (CoatAlphaSq - 1.0f) + 1.0f;
            float CoatD       = CoatAlphaSq / max(π * CoatDen * CoatDen, 1.0e-5f);
            Specular         += float3(0.04f) * (Clearcoat * CoatD * 0.18f);
        }

        float3 SkyHemi    = lerp(float3(0.10f, 0.11f, 0.13f), ZenithRgb * 0.85f + HorizRgb * 0.45f, 0.5f + 0.5f * OpaqueNormal.y);
        float3 EnvRefl    = EvaluateSimpleDiagnosticSky(reflect(RayDir, OpaqueNormal), SunDir, ZenithRgb, HorizRgb);
        float3 LitSurface = (Diffuse + Specular) * float3(2.85f, 2.72f, 2.55f) * NdotL * GroundContactAo +
                            Albedo * (1.0f - 0.65f * Metallic) * SkyHemi * GroundContactAo +
                            EnvRefl * Fresnel * (1.0f - 0.72f * Roughness) * (0.35f + 0.45f * Clearcoat);

        float WireMask = 1.0f - smoothstep(0.45f, 1.25f, OpaqueEdgePx);

        if (ViewMode == 0u) // ShadedPreview
        {
            ResolvedColor = LitSurface;
        }
        else if (ViewMode == 1u) // ShadedWireframe
        {
            float3 EdgeTint = lerp(float3(0.86f, 0.92f, 1.00f), EvaluateHeatGradient(TierIndex / MaxTier), 0.45f);
            ResolvedColor   = lerp(LitSurface, EdgeTint, WireMask * 0.82f);
        }
        else if (ViewMode == 2u) // WireframeTopology
        {
            float3 BaseSlate = float3(0.055f, 0.065f, 0.082f) * (0.55f + 0.45f * NdotL);
            float3 WireColor = lerp(float3(0.38f, 0.72f, 0.98f), float3(0.95f, 0.72f, 0.28f), saturate(TierIndex / MaxTier));
            ResolvedColor    = lerp(BaseSlate, WireColor, WireMask);
        }
        else if (ViewMode == 3u) // ClusterIdentity
        {
            float3 ClusterCol = HashClusterColor(GlobalId) * (0.38f + 0.62f * (0.4f + 0.6f * NdotL));
            ResolvedColor     = lerp(ClusterCol, ClusterCol * 0.25f, WireMask * 0.55f);
        }
        else if (ViewMode == 4u) // HierarchyTierHeat (Tier 0 finest = warm coral-amber -> coarsest = cool slate-blue)
        {
            float3 Heat   = EvaluateHeatGradient(1.0f - saturate(TierIndex / MaxTier));
            ResolvedColor = Heat * (0.42f + 0.58f * (0.35f + 0.65f * NdotL));
        }
        else if (ViewMode == 5u) // ProjectedErrorHeat
        {
            float3 Heat   = EvaluateHeatGradient(saturate(ProjErrPx / (ErrRefPx * 1.5f)));
            ResolvedColor = Heat * (0.42f + 0.58f * (0.35f + 0.65f * NdotL));
        }
        else if (ViewMode == 6u) // MaterialTierHeat (0 = FullStrata cyan-blue, 1 = FilteredToksvig amber, 2 = CompactProxy slate-violet)
        {
            float3 MatCol = (MatTier == 0u) ? float3(0.20f, 0.74f, 0.96f)
                          : ((MatTier == 1u) ? float3(0.94f, 0.68f, 0.22f) : float3(0.56f, 0.42f, 0.84f));
            ResolvedColor = MatCol * (0.40f + 0.60f * (0.35f + 0.65f * NdotL));
        }
        else if (ViewMode == 7u) // SurfaceNormals
        {
            ResolvedColor = 0.5f + 0.5f * OpaqueNormal;
        }
        else if (ViewMode == 8u) // TextureCoordinates (UV checker)
        {
            int2   Cell    = int2(floor(OpaqueUv * 4.0f));
            float  Checker = float((Cell.x + Cell.y) & 1);
            float3 UvTint  = float3(frac(OpaqueUv.x), frac(OpaqueUv.y), 0.35f + 0.45f * Checker);
            ResolvedColor  = UvTint * (0.45f + 0.55f * (0.35f + 0.65f * NdotL));
        }
        else // ViewMode == 9u: GlassLayerPeel
        {
            ResolvedColor = LitSurface * 0.78f;
        }
    }

    // Composite two-layer dielectric glass peel over background / interior cabin geometry
    if (HasGlass)
    {
        uint   GClBase   = kClusterBaseSlot + GlassClusterIdx * kClusterStride;
        float4 GMeta2    = ClusterSceneHeader[GClBase + 2u];
        float4 GMeta3    = ClusterSceneHeader[GClBase + 3u];
        float  Ior       = max(1.02f, GMeta3.y);
        float  CosThetaI = saturate(dot(-RayDir, GlassFrontNormal));
        float  F0Scalar  = pow((Ior - 1.0f) / (Ior + 1.0f), 2.0f);
        float  FresnelG  = F0Scalar + (1.0f - F0Scalar) * pow(1.0f - CosThetaI, 5.0f);

        float  ExitT     = (GlassBackT > GlassFrontT && GlassBackT < OpaqueT) ? GlassBackT : (GlassFrontT + 0.018f);
        float  Thickness = clamp(ExitT - GlassFrontT, 0.008f, 0.45f);
        float3 BeerTr    = exp(-float3(0.85f, 0.24f, 0.30f) * (GlassAbsorbRate * 6.0f * Thickness)) * GMeta2.xyz;
        float3 GlassRefl = EvaluateSimpleDiagnosticSky(reflect(RayDir, GlassFrontNormal), SunDir, ZenithRgb, HorizRgb);
        float  GlassWire = 1.0f - smoothstep(0.45f, 1.25f, GlassEdgePx);

        if (ViewMode == 3u) // ClusterIdentity: crisp translucent cyan overlay so interior clusters behind glass remain visible
        {
            ResolvedColor = lerp(ResolvedColor * float3(0.72f, 0.94f, 1.0f), float3(0.24f, 0.86f, 0.98f), 0.34f + 0.45f * GlassWire);
        }
        else if (ViewMode == 9u) // GlassLayerPeel: single-layer cyan vs two-layer amber-gold dielectric peel thickness
        {
            float3 PeelTint = (GlassLayerCount >= 2u) ? float3(0.96f, 0.76f, 0.24f) : float3(0.22f, 0.82f, 0.96f);
            ResolvedColor   = lerp(ResolvedColor, PeelTint, 0.46f + 0.35f * GlassWire);
        }
        else if (ViewMode == 2u)
        {
            ResolvedColor = lerp(ResolvedColor, float3(0.32f, 0.90f, 0.98f), max(0.18f, GlassWire));
        }
        else
        {
            ResolvedColor = ResolvedColor * BeerTr * (1.0f - FresnelG) + GlassRefl * FresnelG;
            if (ViewMode == 1u)
            {
                ResolvedColor = lerp(ResolvedColor, float3(0.52f, 0.92f, 1.00f), GlassWire * 0.72f);
            }
        }
    }

    DiagnosticTarget[uint3(Tid.xy, 0u)] = float4(max(ResolvedColor, float3(0.0f)), 1.0f);
}
