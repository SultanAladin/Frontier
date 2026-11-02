//==========================================================================================================================================
//                                                   CARD SOLAR VALIDATION
//==========================================================================================================================================
// 🧩 Exercises the production card shadow march with a synthetic sphere and ground plane.

#include "DistanceTraversal.slang"

[[vk::binding(0, 0)]] Sampler3D DistanceVolume;
[[vk::binding(1, 0)]] StructuredBuffer<float4> RangeTable;
[[vk::binding(2, 0), vk::image_format("rgba16f")]] RWTexture2DArray<float4> Result;

[shader("compute")]
[numthreads(1, 1, 1)]
void ValidateCardSolarMain(uint3 Invocation : SV_DispatchThreadID)
{
    float Blocked = EvaluateCardSolarVisibility(DistanceVolume, RangeTable, float3(0.0f, 1.0f, 0.0f),
                                                 float3(0.0f, 1.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), 40.0f);
    float Clear = EvaluateCardSolarVisibility(DistanceVolume, RangeTable, float3(4.0f, 1.0f, 0.0f),
                                               float3(0.0f, 1.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), 40.0f);
    float FarDown = EvaluateCardSolarVisibility(DistanceVolume, RangeTable, float3(300.0f, 10.0f, 0.0f),
                                                 float3(0.0f, 1.0f, 0.0f), float3(0.0f, -1.0f, 0.0f), 400.0f);
    float FarUp = EvaluateCardSolarVisibility(DistanceVolume, RangeTable, float3(300.0f, 10.0f, 0.0f),
                                               float3(0.0f, 1.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), 400.0f);
    Result[Invocation] = float4(Blocked, Clear, FarDown, FarUp);
}
