//==========================================================================================================================================
//                                               SURFACE REPROJECTION VALIDATION
//==========================================================================================================================================
// 🧩 GPU checks of prior-camera distance and temporal history acceptance.

#include "SurfaceReprojection.slang"

[[vk::binding(0, 0)]] StructuredBuffer<float4> CameraTable;
[[vk::binding(1, 0), vk::image_format("rgba16f")]] RWTexture2DArray<float4> ValidationTarget;

[shader("compute")]
[numthreads(1, 1, 1)]
void ValidateSurfaceReprojectionMain(uint3 Invocation : SV_DispatchThreadID)
{
    float Expected = ResolvePreviousSurfaceDistance(CameraTable, float3(4.0f, 2.0f, 7.0f));
    ValidationTarget[Invocation] = float4(Expected,
                                         ClassifyPreviousSurfaceDistance(5.0f, Expected, 3.0f) ? 1.0f : 0.0f,
                                         ClassifyPreviousSurfaceDistance(9.0f, Expected, 3.0f) ? 1.0f : 0.0f,
                                         ClassifyPreviousSurfaceDistance(3.0f, Expected, 3.0f) ? 1.0f : 0.0f);
}
