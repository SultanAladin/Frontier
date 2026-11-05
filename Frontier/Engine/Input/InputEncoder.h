#pragma once
/*==========================================================================================================================================
                                                              INPUTENCODER.H
==========================================================================================================================================*/
// 🧩 Translates GLFW keyboard, mouse, and gamepad state into normalized InputPacket records for DeployableUnit control.

#include "Frontier/Engine/Input/InputPacket.h"

#include <array>
#include <cstdint>

struct GLFWwindow;

namespace Frontier
{

// 📝 Raw digital and analog state snapshot sampled from GLFW (or supplied directly in headless validation).
struct RawInputState
{
    bool  KeyW          = false;
    bool  KeyS          = false;
    bool  KeyA          = false;
    bool  KeyD          = false;
    bool  KeyUp         = false;
    bool  KeyDown       = false;
    bool  KeyLeft       = false;
    bool  KeyRight      = false;
    bool  KeySpace      = false;
    bool  KeyShift      = false;
    bool  KeyEnter      = false;
    bool  KeyR          = false;
    bool  KeyC          = false;
    bool  KeyL          = false;
    float GamepadSteer  = 0.0f;   // [-1..1] left stick X
    float GamepadAccel  = 0.0f;   // [0..1]  right trigger
    float GamepadBrake  = 0.0f;   // [0..1]  left trigger
    float CursorDeltaX  = 0.0f;   // [px]
    float CursorDeltaY  = 0.0f;   // [px]
};

struct InputEncoderSettings
{
    float StickDeadzone     = 0.08f;    // [-]     radial analog deadzone
    float SteerRiseRate     = 4.50f;    // [1/s]   digital steering ramp-in rate
    float SteerReturnRate   = 6.50f;    // [1/s]   digital steering self-centering rate
    float PedalRiseRate     = 6.00f;    // [1/s]   digital throttle/brake ramp-in rate
    float PedalReturnRate   = 8.00f;    // [1/s]   digital throttle/brake release rate
    float LookSensitivity   = 0.0035f;  // [rad/px]
};

class InputEncoder
{
public:
    InputEncoder() = default;
    explicit InputEncoder(const InputEncoderSettings& InitialSettings);

    void                        Configure(const InputEncoderSettings& UpdatedSettings) noexcept;
    const InputEncoderSettings& QuerySettings() const noexcept { return Settings; }

    // 📝 Polls a live GLFWwindow (keyboard + cursor + gamepad 0) and advances the smoothed InputPacket.
    InputPacket                 SampleGlfwWindow(GLFWwindow* Window, float StepSeconds) noexcept;

    // 📝 Encodes an explicit RawInputState snapshot (used by both SampleGlfwWindow and headless tests).
    InputPacket                 EncodeState(const RawInputState& State, float StepSeconds) noexcept;

    void                        Reset() noexcept;

    const InputPacket&          QueryLatestPacket() const noexcept { return LatestPacket; }
    const InputPacket&          QueryPreviousPacket() const noexcept { return PreviousPacket; }

    bool                        WasActionPressed(InputActionBit Bit) const noexcept;
    bool                        WasActionReleased(InputActionBit Bit) const noexcept;

private:
    static float                ApplyDeadzone(float Value, float Deadzone) noexcept;
    static float                ApproachAxis(float Current, float Target, float Rate, float StepSeconds) noexcept;

    InputEncoderSettings        Settings{};
    InputPacket                 PreviousPacket{};
    InputPacket                 LatestPacket{};
    uint64_t                    SequenceCounter = 0;
    double                      PriorCursorX    = 0.0;
    double                      PriorCursorY    = 0.0;
    bool                        CursorSeeded    = false;
};

} // namespace Frontier
