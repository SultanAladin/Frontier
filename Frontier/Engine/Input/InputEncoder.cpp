/*==========================================================================================================================================
                                                             INPUTENCODER.CPP
==========================================================================================================================================*/
// 🧩 GLFW and headless input encoding into normalized InputPacket records.

#include "Frontier/Engine/Input/InputEncoder.h"

#if __has_include(<GLFW/glfw3.h>)
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define FRONTIER_HAS_GLFW 1
#else
#define FRONTIER_HAS_GLFW 0
#endif

#include <algorithm>
#include <cmath>

namespace Frontier
{

InputEncoder::InputEncoder(const InputEncoderSettings& InitialSettings)
    : Settings(InitialSettings)
{
}

void InputEncoder::Configure(const InputEncoderSettings& UpdatedSettings) noexcept
{
    Settings = UpdatedSettings;
}

void InputEncoder::Reset() noexcept
{
    PreviousPacket = InputPacket{};
    LatestPacket   = InputPacket{};
    CursorSeeded   = false;
}

float InputEncoder::ApplyDeadzone(float Value, float Deadzone) noexcept
{
    const float Magnitude = std::fabs(Value);
    if (Magnitude <= Deadzone)
    {
        return 0.0f;
    }
    const float Normalized = (Magnitude - Deadzone) / std::max(1.0e-4f, 1.0f - Deadzone);
    return std::copysign(std::clamp(Normalized, 0.0f, 1.0f), Value);
}

float InputEncoder::ApproachAxis(float Current, float Target, float Rate, float StepSeconds) noexcept
{
    const float MaxDelta = std::max(0.0f, Rate * StepSeconds);
    const float Diff     = Target - Current;
    if (std::fabs(Diff) <= MaxDelta)
    {
        return Target;
    }
    return Current + std::copysign(MaxDelta, Diff);
}

InputPacket InputEncoder::SampleGlfwWindow(GLFWwindow* Window, float StepSeconds) noexcept
{
    RawInputState State{};
#if FRONTIER_HAS_GLFW
    if (Window != nullptr)
    {
        auto Down = [Window](int Key) noexcept -> bool
        {
            return glfwGetKey(Window, Key) == GLFW_PRESS;
        };

        State.KeyW     = Down(GLFW_KEY_W);
        State.KeyS     = Down(GLFW_KEY_S);
        State.KeyA     = Down(GLFW_KEY_A);
        State.KeyD     = Down(GLFW_KEY_D);
        State.KeyUp    = Down(GLFW_KEY_UP);
        State.KeyDown  = Down(GLFW_KEY_DOWN);
        State.KeyLeft  = Down(GLFW_KEY_LEFT);
        State.KeyRight = Down(GLFW_KEY_RIGHT);
        State.KeySpace = Down(GLFW_KEY_SPACE);
        State.KeyShift = Down(GLFW_KEY_LEFT_SHIFT) || Down(GLFW_KEY_RIGHT_SHIFT);
        State.KeyEnter = Down(GLFW_KEY_ENTER) || Down(GLFW_KEY_KP_ENTER);
        State.KeyR     = Down(GLFW_KEY_R);
        State.KeyC     = Down(GLFW_KEY_C);
        State.KeyL     = Down(GLFW_KEY_L);

        double CursorX = 0.0;
        double CursorY = 0.0;
        glfwGetCursorPos(Window, &CursorX, &CursorY);
        if (CursorSeeded)
        {
            State.CursorDeltaX = static_cast<float>(CursorX - PriorCursorX);
            State.CursorDeltaY = static_cast<float>(CursorY - PriorCursorY);
        }
        PriorCursorX = CursorX;
        PriorCursorY = CursorY;
        CursorSeeded = true;

        GLFWgamepadstate Gamepad{};
        if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1) == GLFW_TRUE &&
            glfwGetGamepadState(GLFW_JOYSTICK_1, &Gamepad) == GLFW_TRUE)
        {
            State.GamepadSteer = Gamepad.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
            State.GamepadAccel = 0.5f * (Gamepad.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f);
            State.GamepadBrake = 0.5f * (Gamepad.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]  + 1.0f);
            if (Gamepad.buttons[GLFW_GAMEPAD_BUTTON_A] == GLFW_PRESS)
            {
                State.KeySpace = true;
            }
            if (Gamepad.buttons[GLFW_GAMEPAD_BUTTON_START] == GLFW_PRESS)
            {
                State.KeyEnter = true;
            }
        }
    }
#else
    (void)Window;
#endif
    return EncodeState(State, StepSeconds);
}

InputPacket InputEncoder::EncodeState(const RawInputState& State, float StepSeconds) noexcept
{
    PreviousPacket = LatestPacket;
    const float Dt = std::clamp(StepSeconds, 0.0f, 0.25f);

    const float DigitalThrottle = (State.KeyW || State.KeyUp)   ? 1.0f : 0.0f;
    const float DigitalBrake    = (State.KeyS || State.KeyDown) ? 1.0f : 0.0f;
    const float DigitalLeft     = (State.KeyA || State.KeyLeft) ? 1.0f : 0.0f;
    const float DigitalRight    = (State.KeyD || State.KeyRight)? 1.0f : 0.0f;
    const float DigitalSteer    = DigitalRight - DigitalLeft;

    const float AnalogSteer = ApplyDeadzone(State.GamepadSteer, Settings.StickDeadzone);
    const float TargetThrottle = std::clamp(std::max(DigitalThrottle, State.GamepadAccel), 0.0f, 1.0f);
    const float TargetBrake    = std::clamp(std::max(DigitalBrake,    State.GamepadBrake), 0.0f, 1.0f);
    const float TargetSteer    = std::fabs(AnalogSteer) > 1.0e-3f ? AnalogSteer : DigitalSteer;

    InputPacket Next{};
    Next.SequenceIndex = ++SequenceCounter;

    const float ThrottleRate = TargetThrottle > LatestPacket.ThrottleAxis ? Settings.PedalRiseRate : Settings.PedalReturnRate;
    const float BrakeRate    = TargetBrake    > LatestPacket.BrakeAxis    ? Settings.PedalRiseRate : Settings.PedalReturnRate;
    const float SteerRate    = std::fabs(TargetSteer) > std::fabs(LatestPacket.SteerAxis)
                             ? Settings.SteerRiseRate
                             : Settings.SteerReturnRate;

    Next.ThrottleAxis   = std::clamp(ApproachAxis(LatestPacket.ThrottleAxis, TargetThrottle, ThrottleRate, Dt), 0.0f, 1.0f);
    Next.BrakeAxis      = std::clamp(ApproachAxis(LatestPacket.BrakeAxis,    TargetBrake,    BrakeRate,    Dt), 0.0f, 1.0f);
    Next.SteerAxis      = std::clamp(ApproachAxis(LatestPacket.SteerAxis,    TargetSteer,    SteerRate,    Dt), -1.0f, 1.0f);
    Next.HandbrakeAxis  = State.KeySpace ? 1.0f : 0.0f;
    Next.LookYawDelta   = State.CursorDeltaX * Settings.LookSensitivity;
    Next.LookPitchDelta = State.CursorDeltaY * Settings.LookSensitivity;

    uint32_t Bits = InputActionNone;
    if (State.KeySpace) Bits |= InputActionHandbrake;
    if (State.KeyS || State.KeyDown) Bits |= InputActionReverse;
    if (State.KeyShift) Bits |= InputActionBoost;
    if (State.KeyEnter) Bits |= InputActionReady;
    if (State.KeyR)     Bits |= InputActionResetUnit;
    if (State.KeyC)     Bits |= InputActionCycleCamera;
    if (State.KeyL)     Bits |= InputActionLights;
    Next.ActionBits = Bits;

    LatestPacket = Next;
    return LatestPacket;
}

bool InputEncoder::WasActionPressed(InputActionBit Bit) const noexcept
{
    return LatestPacket.HasAction(Bit) && !PreviousPacket.HasAction(Bit);
}

bool InputEncoder::WasActionReleased(InputActionBit Bit) const noexcept
{
    return !LatestPacket.HasAction(Bit) && PreviousPacket.HasAction(Bit);
}

} // namespace Frontier
