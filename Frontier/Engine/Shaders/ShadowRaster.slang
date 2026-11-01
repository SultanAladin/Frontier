//==========================================================================================================================================
//                                                      SHADOW RASTER
//==========================================================================================================================================
// 🧩 Hardware depth-only solar cascade triangle rasterizer (VK_FORMAT_D32_SFLOAT, 3 texel-snapped orthographic cascades):
//    - Pulls local/world triangles directly from BvhTriangles and live dynamic transforms from OctetInstances
//    - Excludes transmissive glazing facets so sunlight illuminates vehicle cabin interiors cleanly

#include "OctetVolumeSearch.slang"

[[vk::binding(0, 0)]] StructuredBuffer<float4> FrameHeader;
[[vk::binding(1, 0)]] StructuredBuffer<float4> BvhOrTopCells;
[[vk::binding(2, 0)]] StructuredBuffer<float4> BvhTriangles;
[[vk::binding(3, 0)]] StructuredBuffer<float4> MaterialTable;
[[vk::binding(4, 0)]] StructuredBuffer<float4> OctetInstances;
[[vk::binding(5, 0)]] StructuredBuffer<float4> OctetBlocks;
[[vk::binding(6, 0)]] Sampler2DArray           MaterialMaps;

struct RasterPushData
{
    uint4 Words0; // x = TriOffset, y = InstanceIndex (0xFFFFFFFFu = world-space), z = CascadeOrMode, w = TwoLevelActive
    uint4 Words1; // reserved
};

[[vk::push_constant]] RasterPushData Push;

[shader("vertex")]
float4 ProjectShadowCascadeVertexMain(uint VertexId : SV_VulkanVertexID) : SV_Position
{
    uint   LocalTri  = VertexId / 3u;
    uint   Corner    = VertexId % 3u;
    uint   GlobalTri = Push.Words0.x + LocalTri;
    uint   InstIdx   = Push.Words0.y;
    uint   Cascade   = min(Push.Words0.z, 2u);

    uint   TAddr     = GlobalTri * OctetTriangleStride;
    float4 T0        = BvhTriangles[TAddr + 0u];
    float4 T1        = BvhTriangles[TAddr + 1u];
    float4 T2        = BvhTriangles[TAddr + 2u];
    uint   ClassPack = asuint(T2.w);
    uint   ClassOv   = 0xFFFFFFFFu;

    if (InstIdx != 0xFFFFFFFFu)
    {
        ClassOv = asuint(OctetInstances[InstIdx * OctetInstanceStride + 7u].z);
    }

    // See-through glazing casts no opaque shadow map depth so solar light reaches cabin seats
    if (IsOctetTransmissiveClass(ClassPack, ClassOv))
    {
        return float4(2.0f, 2.0f, 2.0f, 1.0f);
    }

    float3 LocalPos = T0.xyz;
    if (Corner == 1u)
    {
        LocalPos += T1.xyz;
    }
    else if (Corner == 2u)
    {
        LocalPos += T2.xyz;
    }

    float3 WorldPos = LocalPos;
    if (InstIdx != 0xFFFFFFFFu)
    {
        uint   IBase = InstIdx * OctetInstanceStride;
        WorldPos     = TransformInstancePointToWorld(
            OctetInstances[IBase + 0u], OctetInstances[IBase + 1u], OctetInstances[IBase + 2u], OctetInstances[IBase + 3u], LocalPos);
    }

    uint   RowBase = 20u + Cascade * 4u;
    float4 R0      = FrameHeader[RowBase + 0u];
    float4 R1      = FrameHeader[RowBase + 1u];
    float4 R2      = FrameHeader[RowBase + 2u];
    float4 R3      = FrameHeader[RowBase + 3u];
    float4 World4  = float4(WorldPos, 1.0f);

    return float4(dot(R0, World4), dot(R1, World4), clamp(dot(R2, World4), 0.0f, 1.0f), dot(R3, World4));
}
