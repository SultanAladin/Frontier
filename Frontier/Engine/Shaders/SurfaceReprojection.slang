//==========================================================================================================================================
//                                                    SURFACE REPROJECTION
//==========================================================================================================================================
// 🧩 Prior-camera distance and history validation for camera-motion reprojection.

#pragma once

// 📝 Guides store ray distance, so use the prior camera origin even when its field of view has changed.
float ResolvePreviousSurfaceDistance(StructuredBuffer<float4> CameraTable, float3 WorldPosition)
{
    return length(WorldPosition - CameraTable[11].xyz);
}

bool ClassifyPreviousSurfaceDistance(float PriorDistance, float ExpectedDistance, float CurrentDistance)
{
    return (abs(PriorDistance - ExpectedDistance) < 0.08f * ExpectedDistance + 0.03f) ||
           (abs(PriorDistance - CurrentDistance) < 0.08f * CurrentDistance + 0.03f);
}
