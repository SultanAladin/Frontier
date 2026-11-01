/*==========================================================================================================================================
                                               VIEWPORT FLIGHT RIG DEFINITION
==========================================================================================================================================*/
// 🧩 Six-degree-of-freedom interactive WASD + Q/E + Shift/Ctrl + RMB mouse look flight rig for Everest live viewport.

#include "Engine/GeometricRaster/ViewportFlightRig.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

ViewportFlightRig::ViewportFlightRig() noexcept
{
    ResetToDefaultHeroPose();
}

void ViewportFlightRig::ResetToDefaultHeroPose() noexcept
{
    Origin_         = {3.35f, 1.48f, 4.25f};
    const float Dx  = -0.08f - Origin_[0];
    const float Dy  = 0.72f - Origin_[1];
    const float Dz  = -0.15f - Origin_[2];
    const float H   = std::sqrt(Dx * Dx + Dz * Dz);
    YawRad_         = std::atan2(Dz, Dx);
    PitchRad_       = std::atan2(Dy, std::max(1.0e-5f, H));
    VerticalFovRad_ = 0.74f;
    BaseSpeedMps_   = 5.5f;
    CapturingLook_  = false;
}

void ViewportFlightRig::ConfigurePose(const std::array<float, 3>& OriginMeters,
                                      float                       YawRadians,
                                      float                       PitchRadians,
                                      float                       VerticalFovRadians) noexcept
{
    Origin_         = OriginMeters;
    YawRad_         = YawRadians;
    PitchRad_       = std::clamp(PitchRadians, -1.50f, 1.50f);
    VerticalFovRad_ = std::clamp(VerticalFovRadians, 0.25f, 1.85f);
}

std::array<float, 3> ViewportFlightRig::ForwardUnit() const noexcept
{
    const float CosP = std::cos(PitchRad_);
    return {
        CosP * std::cos(YawRad_),
        std::sin(PitchRad_),
        CosP * std::sin(YawRad_),
    };
}

std::array<float, 3> ViewportFlightRig::RightUnit() const noexcept
{
    const auto  Fwd = ForwardUnit();
    // Right = normalize(cross(Fwd, WorldUp(0,1,0))) = normalize((-Fwd.z, 0, Fwd.x))
    const float Rx  = -Fwd[2];
    const float Rz  = Fwd[0];
    const float Len = std::sqrt(Rx * Rx + Rz * Rz);
    if (Len <= 1.0e-6f)
    {
        return {1.0f, 0.0f, 0.0f};
    }
    return {Rx / Len, 0.0f, Rz / Len};
}

std::array<float, 3> ViewportFlightRig::TargetMeters(float LookAheadMeters) const noexcept
{
    const auto Fwd = ForwardUnit();
    return {
        Origin_[0] + Fwd[0] * LookAheadMeters,
        Origin_[1] + Fwd[1] * LookAheadMeters,
        Origin_[2] + Fwd[2] * LookAheadMeters,
    };
}

void ViewportFlightRig::ConfigureBaseSpeed(float SpeedMetersPerSec) noexcept
{
    BaseSpeedMps_ = std::clamp(SpeedMetersPerSec, 0.35f, 80.0f);
}

void ViewportFlightRig::ConfigureVerticalFov(float VerticalFovRadians) noexcept
{
    VerticalFovRad_ = std::clamp(VerticalFovRadians, 0.25f, 1.85f);
}

bool ViewportFlightRig::IntegrateInputs(const ViewportFlightInputs& Inputs, double DeltaSeconds) noexcept
{
    if (Inputs.ResetPose)
    {
        ResetToDefaultHeroPose();
        return true;
    }

    if (Inputs.ReleaseCapture)
    {
        CapturingLook_ = false;
    }
    else
    {
        CapturingLook_ = Inputs.LookActive;
    }

    bool Changed = false;

    if (std::abs(Inputs.WheelStep) > 1.0e-4f && CapturingLook_)
    {
        BaseSpeedMps_ = std::clamp(BaseSpeedMps_ * std::pow(1.18f, Inputs.WheelStep), 0.35f, 80.0f);
    }

    if (CapturingLook_ && (std::abs(Inputs.MouseDeltaX) > 1.0e-4f || std::abs(Inputs.MouseDeltaY) > 1.0e-4f))
    {
        const float SensScale = 0.0012f + 0.0042f * (std::clamp(Inputs.MouseSensitivity, 1.0f, 100.0f) / 100.0f);
        const float PitchSign = Inputs.InvertPitch ? 1.0f : -1.0f;
        YawRad_               = std::remainder(YawRad_ + Inputs.MouseDeltaX * SensScale, 6.283185307f);
        PitchRad_             = std::clamp(PitchRad_ + Inputs.MouseDeltaY * SensScale * PitchSign, -1.48f, 1.48f);
        Changed               = true;
    }

    const float Dt = static_cast<float>(std::clamp(DeltaSeconds, 0.0, 0.12));
    if (Dt <= 0.0f)
    {
        return Changed;
    }

    float FwdAxis   = (Inputs.MoveForward ? 1.0f : 0.0f) - (Inputs.MoveBackward ? 1.0f : 0.0f);
    float RightAxis = (Inputs.StrafeRight ? 1.0f : 0.0f) - (Inputs.StrafeLeft ? 1.0f : 0.0f);
    float UpAxis    = (Inputs.AscendVertical ? 1.0f : 0.0f) - (Inputs.DescendVertical ? 1.0f : 0.0f);

    const auto Fwd   = ForwardUnit();
    const auto Right = RightUnit();
    float      MoveX = Fwd[0] * FwdAxis + Right[0] * RightAxis;
    float      MoveY = Fwd[1] * FwdAxis + UpAxis;
    float      MoveZ = Fwd[2] * FwdAxis + Right[2] * RightAxis;

    const float MoveLen = std::sqrt(MoveX * MoveX + MoveY * MoveY + MoveZ * MoveZ);
    if (MoveLen > 1.0e-5f)
    {
        const float SpeedMult = Inputs.SprintBoost ? 3.0f : (Inputs.PrecisionSlow ? 0.25f : 1.0f);
        const float Step      = (BaseSpeedMps_ * SpeedMult * Dt) / std::max(1.0f, MoveLen);
        Origin_[0] += MoveX * Step;
        Origin_[1] = std::max(0.18f, Origin_[1] + MoveY * Step);
        Origin_[2] += MoveZ * Step;
        Changed = true;
    }

    return Changed;
}

} // namespace Frontier
