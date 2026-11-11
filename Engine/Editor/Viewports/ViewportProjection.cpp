/*============================================================================================================================================
                                                          VIEWPORTPROJECTION.CPP
============================================================================================================================================*/
// 🧩 Implementation of ViewportProjection camera rig and 1:1 aspect-locked planar UDIM/canvas window math

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Viewports/ViewportProjection.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Editor
{

void ViewportProjection::ConfigureSplitMode(ViewportSplitMode Mode) noexcept
{
    SplitMode = Mode;
}

void ViewportProjection::ConfigureSplitRatio(float NormalizedRatio) noexcept
{
    SplitRatio = std::clamp(NormalizedRatio, 0.20f, 0.80f);
}

void ViewportProjection::ConfigureSpatialCamera(const SpatialCameraRig& Rig) noexcept
{
    SpatialCamera = Rig;
}

void ViewportProjection::ConfigurePlanarWindow(const PlanarWindowRig& Rig) noexcept
{
    PlanarWindow = Rig;
}

void ViewportProjection::ConfigurePlanarDomainSpan(float SpanU, float SpanV) noexcept
{
    PlanarWindow.SpanU = std::max(0.1f, SpanU);
    PlanarWindow.SpanV = std::max(0.1f, SpanV);
}

void ViewportProjection::OrbitSpatialCamera(float DeltaYawRad, float DeltaPitchRad) noexcept
{
    SpatialCamera.YawRadians  += DeltaYawRad;
    SpatialCamera.PitchRadians = std::clamp(
        SpatialCamera.PitchRadians + DeltaPitchRad,
        -1.45f,
         1.45f);
}

void ViewportProjection::DollySpatialCamera(float DistanceMultiplier) noexcept
{
    SpatialCamera.DistanceMeters = std::clamp(
        SpatialCamera.DistanceMeters * std::max(0.1f, DistanceMultiplier),
        0.35f,
        250.0f);
}

void ViewportProjection::PanPlanarWindow(Mathematics::UVAxes DeltaUV) noexcept
{
    PlanarWindow.PanOffsetUV.U += DeltaUV.U;
    PlanarWindow.PanOffsetUV.V += DeltaUV.V;
}

void ViewportProjection::ZoomPlanarWindow(float ZoomMultiplier) noexcept
{
    PlanarWindow.ZoomScale = std::clamp(
        PlanarWindow.ZoomScale * std::max(0.1f, ZoomMultiplier),
        0.15f,
        16.0f);
}

Mathematics::Axis3D ViewportProjection::EvaluateCameraEyePosition() const noexcept
{
    const float CosPitch = std::cos(SpatialCamera.PitchRadians);
    const float SinPitch = std::sin(SpatialCamera.PitchRadians);
    const float CosYaw   = std::cos(SpatialCamera.YawRadians);
    const float SinYaw   = std::sin(SpatialCamera.YawRadians);

    return {
        SpatialCamera.TargetPivot.X + SpatialCamera.DistanceMeters * CosPitch * SinYaw,
        SpatialCamera.TargetPivot.Y + SpatialCamera.DistanceMeters * SinPitch,
        SpatialCamera.TargetPivot.Z + SpatialCamera.DistanceMeters * CosPitch * CosYaw,
    };
}

void ViewportProjection::EvaluateAspectLockedPlanarBounds(
    uint32_t             CanvasWidthPx,
    uint32_t             CanvasHeightPx,
    Mathematics::UVAxes& OutMinUV,
    Mathematics::UVAxes& OutMaxUV) const noexcept
{
    const float InvZoom      = 1.0f / std::max(0.15f, PlanarWindow.ZoomScale);
    const float DesiredSpanU = (PlanarWindow.SpanU + 2.0f * PlanarWindow.PaddingUV) * InvZoom;
    const float DesiredSpanV = (PlanarWindow.SpanV + 2.0f * PlanarWindow.PaddingUV) * InvZoom;
    const float Aspect       = (CanvasHeightPx > 0u)
        ? (static_cast<float>(std::max(1u, CanvasWidthPx)) / static_cast<float>(CanvasHeightPx))
        : 1.0f;

    float EffectiveU = DesiredSpanU;
    float EffectiveV = DesiredSpanV;
    if ((DesiredSpanU / DesiredSpanV) > Aspect)
    {
        EffectiveV = DesiredSpanU / Aspect;
    }
    else
    {
        EffectiveU = DesiredSpanV * Aspect;
    }

    const float CenterU = 0.5f * PlanarWindow.SpanU + PlanarWindow.PanOffsetUV.U;
    const float CenterV = 0.5f * PlanarWindow.SpanV + PlanarWindow.PanOffsetUV.V;
    OutMinUV = { CenterU - 0.5f * EffectiveU, CenterV - 0.5f * EffectiveV };
    OutMaxUV = { CenterU + 0.5f * EffectiveU, CenterV + 0.5f * EffectiveV };
}

} // namespace Frontier::Editor
