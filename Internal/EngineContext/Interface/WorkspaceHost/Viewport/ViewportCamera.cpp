/*==============================================================================================================================================
                                                              VIEWPORTCAMERA.CPP
==============================================================================================================================================*/
// 🧩 Advances the shared orbit camera from pointer input. Pure math, no ImGui — the viewport translates raw pointer deltas into these calls.

#include "ViewportCamera.h"

#include <math.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    const float OrbitSpeed   = 0.008f;    // [rad/px] - Orbit sensitivity
    const float PanSpeed     = 0.0015f;   // [-/px]   - Pan sensitivity (scaled by Distance)
    const float DollySpeed   = 0.10f;     // [-/notch]- Dolly sensitivity (fraction of Distance)
    const float MinDistance  = 1.0f;      // [cm]     - Closest dolly
    const float MaxDistance  = 500000.0f; // [cm]     - Farthest dolly
    const float PitchLimit   = 1.55334f;  // [rad]    - ~89deg, keeps the camera off the poles

    float Clamp(float Value, float Low, float High)
    {
        if (Value < Low)  { return Low; }
        if (Value > High) { return High; }
        return Value;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

PanelViewportCamera ResolveDefaultPerspectivePanelCamera()
{
    PanelViewportCamera Camera = {};
    Camera.TargetX      = 0.0f;
    Camera.TargetY      = 0.0f;
    Camera.TargetZ      = 0.0f;
    Camera.Distance     = 600.0f;
    Camera.Yaw          = 0.7853982f;    // 45deg
    Camera.Pitch        = 0.5235988f;    // 30deg
    Camera.FieldOfView  = 0.7853982f;    // 45deg vertical
    Camera.Orthographic = false;
    return Camera;
}


PanelViewportCamera ResolveDefaultOrthographicPanelCamera()
{
    PanelViewportCamera Camera = {};
    Camera.TargetX      = 0.0f;
    Camera.TargetY      = 0.0f;
    Camera.TargetZ      = 0.0f;
    Camera.Distance     = 400.0f;
    Camera.Yaw          = 0.0f;
    Camera.Pitch        = 1.5707963f;    // straight down
    Camera.FieldOfView  = 0.0f;
    Camera.Orthographic = true;
    return Camera;
}


void OrbitPanelViewportCamera(PanelViewportCamera& Camera, float DeltaX, float DeltaY)
{
    if (Camera.Orthographic)
    {
        return;
    }
    Camera.Yaw  -= DeltaX * OrbitSpeed;
    Camera.Pitch = Clamp(Camera.Pitch - DeltaY * OrbitSpeed, -PitchLimit, PitchLimit);
}


void PanPanelViewportCamera(PanelViewportCamera& Camera, float DeltaX, float DeltaY)
{
    // 📝 Pan tracks the cursor: scale by Distance so a drag covers the same screen span at any zoom.
    const float Scale = PanSpeed * Camera.Distance;
    const float CosYaw = cosf(Camera.Yaw);
    const float SinYaw = sinf(Camera.Yaw);

    // 📝 Move Target along the camera's right + up projected onto the ground plane (simple, orientation-aware).
    Camera.TargetX -= (CosYaw * DeltaX) * Scale;
    Camera.TargetY += (SinYaw * DeltaX) * Scale;
    Camera.TargetZ += DeltaY * Scale;
}


void DollyPanelViewportCamera(PanelViewportCamera& Camera, float WheelDelta)
{
    Camera.Distance = Clamp(Camera.Distance * (1.0f - WheelDelta * DollySpeed), MinDistance, MaxDistance);
}

}   // namespace Frontier
