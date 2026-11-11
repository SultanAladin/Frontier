//============================================================================================================================================
//                                                          UVTILERASTERIZATION.SLANG
//============================================================================================================================================
// 2D UDIM UVTile parameter canvas shader rendering checker tiles, anti-aliased UV faces, VRAM selection bits & 2D GPU Pick IDs

struct UVCanvasVertexRecord
{
    float4 UVAndBaryXY;   // xy = ParameterUV (U, V), zw = Barycentric.xy
    float4 BaryZAndStats; // x = Barycentric.z, y = StretchMetric, z = SignedAreaSign, w = OverlapFlag
    uint4  NetworkPickIDs;// x = InstanceSlot, y = FaceIndex, z = DirectedEdgeIndex, w = PriorityLayerOrdinal
};

struct UVCanvasUniformBlock
{
    float4 CanvasMinMaxUV;    // xy = VisibleMinUV, zw = VisibleMaxUV
    uint4  OverlayAndPick;    // x = DisplayMode (0=Layer, 1=Stretch, 2=Overlap/Flip), y = CheckerEnabled, z = ActiveLayerOrd, w = HoveredFaceIdx
};

[[vk::binding(0, 0)]] ConstantBuffer<UVCanvasUniformBlock>  CanvasUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<UVCanvasVertexRecord> CanvasVertices;
[[vk::binding(2, 0)]] StructuredBuffer<uint>                 FaceSelectionBitmask;

struct UVVertexOutput
{
    float4 ClipPosition       : SV_Position;
    float2 ParameterUV        : TEXCOORD0;
    float3 Barycentric        : TEXCOORD1;
    float3 Diagnostics        : TEXCOORD2;
    nointerpolation uint4 IDs : TEXCOORD3;
};

struct UVFragmentOutput
{
    float4 ColorRGBA : SV_Target0;
    uint4  PickIDs   : SV_Target1;
};

float3 ResolveUVLayerColor(uint LayerOrdinal)
{
    if (LayerOrdinal == 1u) return float3(0.937, 0.957, 0.812); // #EFF4CF Ivory
    if (LayerOrdinal == 2u) return float3(0.620, 0.812, 1.000); // #9ECFFF Sky
    if (LayerOrdinal == 3u) return float3(0.608, 0.855, 0.773); // #9BDAC5 Mint
    if (LayerOrdinal == 4u) return float3(0.910, 0.760, 0.478); // #E8C27A Amber
    return float3(0.52, 0.56, 0.64);
}

[shader("vertex")]
UVVertexOutput VertexEntry(uint VertexID : SV_VertexID)
{
    UVCanvasVertexRecord Rec = CanvasVertices[VertexID];
    float2 MinUV = CanvasUniforms.CanvasMinMaxUV.xy;
    float2 MaxUV = CanvasUniforms.CanvasMinMaxUV.zw;
    float2 Norm  = (Rec.UVAndBaryXY.xy - MinUV) / max(MaxUV - MinUV, float2(1.0e-5, 1.0e-5));

    UVVertexOutput Out;
    // Vulkan NDC: Y=-1 is top, Y=+1 is bottom; map V=Max to top (-1) and V=Min to bottom (+1)
    Out.ClipPosition = float4(Norm.x * 2.0 - 1.0, 1.0 - Norm.y * 2.0, 0.5, 1.0);
    Out.ParameterUV  = Rec.UVAndBaryXY.xy;
    Out.Barycentric  = float3(Rec.UVAndBaryXY.zw, Rec.BaryZAndStats.x);
    Out.Diagnostics  = Rec.BaryZAndStats.yzw;
    Out.IDs          = Rec.NetworkPickIDs;
    return Out;
}

[shader("fragment")]
UVFragmentOutput FragmentEntry(UVVertexOutput In)
{
    UVFragmentOutput Out;

    // Background UDIM Checker & Grid Pass (NetworkPickIDs.x == 0u)
    if (In.IDs.x == 0u)
    {
        float2 UV       = In.ParameterUV;
        bool   InTile   = (UV.x >= 0.0 && UV.y >= 0.0);
        float3 BgBase   = InTile ? float3(0.072, 0.076, 0.088) : float3(0.050, 0.052, 0.060);

        if (CanvasUniforms.OverlayAndPick.y != 0u && InTile)
        {
            int2  Cell   = int2(floor(UV * 16.0));
            float Parity = float((Cell.x + Cell.y) & 1);
            BgBase       = lerp(float3(0.068, 0.072, 0.084), float3(0.098, 0.104, 0.120), Parity);
        }

        float2 SubDeriv  = max(fwidth(UV * 4.0), float2(1.0e-4, 1.0e-4));
        float2 SubGrid   = abs(frac(UV * 4.0 - 0.5) - 0.5) / SubDeriv;
        float  SubLine   = 1.0 - saturate(min(SubGrid.x, SubGrid.y));

        float2 TileDeriv = max(fwidth(UV), float2(1.0e-4, 1.0e-4));
        float2 TileGrid  = abs(frac(UV - 0.5) - 0.5) / TileDeriv;
        float  TileLine  = 1.0 - smoothstep(0.0, 1.6, min(TileGrid.x, TileGrid.y));

        float3 FinalBg = lerp(BgBase, float3(0.16, 0.18, 0.22), SubLine * 0.55);
        FinalBg        = lerp(FinalBg, float3(0.36, 0.56, 0.86), TileLine * 0.85);
        Out.ColorRGBA  = float4(FinalBg, 1.0);
        Out.PickIDs    = uint4(0u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u);
        return Out;
    }

    uint FaceIdx      = In.IDs.y;
    uint LayerOrdinal = In.IDs.w;
    bool Selected     = (FaceSelectionBitmask[FaceIdx >> 5u] & (1u << (FaceIdx & 31u))) != 0u;
    bool LayerActive  = (CanvasUniforms.OverlayAndPick.z > 0u) && (LayerOrdinal == CanvasUniforms.OverlayAndPick.z);
    bool Hovered      = (CanvasUniforms.OverlayAndPick.w == FaceIdx);

    uint   DisplayMode = CanvasUniforms.OverlayAndPick.x;
    float3 FillColor   = ResolveUVLayerColor(LayerOrdinal) * 0.52;

    if (DisplayMode == 1u) // Stretch Heatmap
    {
        float Err = saturate(abs(In.Diagnostics.x - 1.0) * 2.2);
        FillColor = lerp(float3(0.16, 0.66, 0.42), float3(0.92, 0.24, 0.22), Err);
    }
    else if (DisplayMode == 2u) // Winding Flip / Overlap Check
    {
        bool Flipped   = In.Diagnostics.y <= 0.0;
        bool Overlaps  = In.Diagnostics.z > 0.5;
        if (Flipped)       FillColor = float3(0.92, 0.18, 0.32);
        else if (Overlaps) FillColor = float3(0.96, 0.58, 0.16);
        else               FillColor = float3(0.18, 0.62, 0.42);
    }

    if (Selected || LayerActive)
    {
        FillColor = lerp(FillColor, float3(0.937, 0.957, 0.812), 0.42);
    }
    else if (Hovered)
    {
        FillColor = lerp(FillColor, float3(0.62, 0.82, 1.00), 0.28);
    }

    float3 BaryDeriv = max(fwidth(In.Barycentric), float3(1.0e-4));
    float3 EdgeDists = In.Barycentric / BaryDeriv;
    float  MinEdge   = min(EdgeDists.x, min(EdgeDists.y, EdgeDists.z));
    float  EdgeMask  = 1.0 - smoothstep(0.4, 1.45, MinEdge);
    float3 EdgeColor = (Selected || LayerActive) ? float3(0.98, 0.99, 0.88) : ResolveUVLayerColor(LayerOrdinal);

    float3 FinalRGB = lerp(FillColor, EdgeColor, EdgeMask * 0.92);
    Out.ColorRGBA   = float4(FinalRGB, 1.0);
    Out.PickIDs     = uint4(In.IDs.x, In.IDs.y, In.IDs.z, 0u);
    return Out;
}
