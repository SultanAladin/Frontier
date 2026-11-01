/*==========================================================================================================================================
                                                   VIEWPORT FLIGHT RIG
                                     ------------------------------------------------------------------
                 Six-degree-of-freedom interactive WASD + RMB mouse flight camera rig for live 3D viewports
==========================================================================================================================================*/
// 🧩 Smooth WASD + Q/E vertical + Shift/Ctrl speed + mouse-wheel scaling + RMB look camera rig with R reset & motion metric.

#pragma once

#include <array>
#include <cstdint>

namespace Frontier
{

struct ViewportFlightInputs
{
    bool  MoveForward      = false; // [-] - W key
    bool  MoveBackward     = false; // [-] - S key
    bool  StrafeLeft       = false; // [-] - A key
    bool  StrafeRight      = false; // [-] - D key
    bool  AscendVertical   = false; // [-] - E / Space key
    bool  DescendVertical  = false; // [-] - Q / C key
    bool  SprintBoost      = false; // [-] - Left/Right Shift (3.0x speed)
    bool  PrecisionSlow    = false; // [-] - Left/Right Ctrl (0.25x speed)
    bool  ResetPose        = false; // [-] - R key
    bool  ReleaseCapture   = false; // [-] - Escape key
    bool  LookActive       = false; // [-] - Right mouse button held inside viewport
    float MouseDeltaX      = 0.0f;  // [px] - horizontal cursor delta
    float MouseDeltaY      = 0.0f;  // [px] - vertical cursor delta
    float WheelStep        = 0.0f;  // [ticks] - mouse wheel scroll adjusts base speed
    float MouseSensitivity = 50.0f; // [0..100] - workspace sensitivity setting
    bool  InvertPitch      = false; // [-] - workspace pitch inversion setting
};

class ViewportFlightRig
{
  public:
    ViewportFlightRig() noexcept;

    void ResetToDefaultHeroPose() noexcept;

    void ConfigurePose(const std::array<float, 3>& OriginMeters,
                       float                       YawRadians,
                       float                       PitchRadians,
                       float                       VerticalFovRadians = 0.74f) noexcept;

    // Integrates one frame of flight input; returns true if position or orientation changed noticeably.
    bool IntegrateInputs(const ViewportFlightInputs& Inputs, double DeltaSeconds) noexcept;

    [[nodiscard]] std::array<float, 3> OriginMeters() const noexcept { return Origin_; }
    [[nodiscard]] std::array<float, 3> ForwardUnit() const noexcept;
    [[nodiscard]] std::array<float, 3> RightUnit() const noexcept;
    [[nodiscard]] std::array<float, 3> TargetMeters(float LookAheadMeters = 5.0f) const noexcept;
    [[nodiscard]] float                YawRadians() const noexcept { return YawRad_; }
    [[nodiscard]] float                PitchRadians() const noexcept { return PitchRad_; }
    [[nodiscard]] float                YawDegrees() const noexcept { return YawRad_ * 57.2957795f; }
    [[nodiscard]] float                PitchDegrees() const noexcept { return PitchRad_ * 57.2957795f; }
    [[nodiscard]] float                VerticalFovRadians() const noexcept { return VerticalFovRad_; }
    [[nodiscard]] float                BaseSpeedMetersPerSec() const noexcept { return BaseSpeedMps_; }
    [[nodiscard]] bool                 IsCapturingLook() const noexcept { return CapturingLook_; }

    void ConfigureBaseSpeed(float SpeedMetersPerSec) noexcept;
    void ConfigureVerticalFov(float VerticalFovRadians) noexcept;

  private:
    std::array<float, 3> Origin_{3.35f, 1.48f, 4.25f};
    float                YawRad_         = -2.46f;
    float                PitchRad_       = -0.16f;
    float                VerticalFovRad_ = 0.74f;
    float                BaseSpeedMps_   = 5.5f;
    bool                 CapturingLook_  = false;
};

} // namespace Frontier
