/*==========================================================================================================================================
                                                             GLFWINPUTPOLL.CPP
==========================================================================================================================================*/
// 🧩 GLFW polling ingestion for the shared substrate: level-diffed keys/buttons, pointer, modifiers, gamepad.

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#include "Native/GlfwInputPoll.h"

#include <GLFW/glfw3.h>

#include <utility>
#include <vector>

namespace Frontier
{

namespace
{

[[nodiscard]] const std::vector<std::pair<int, InputKeyCategory>>& KeyTable()
{
    static const std::vector<std::pair<int, InputKeyCategory>> Table = []() {
        std::vector<std::pair<int, InputKeyCategory>> Built;
        for (int Index = 0; Index < 26; ++Index)
        {
            Built.emplace_back(GLFW_KEY_A + Index,
                               static_cast<InputKeyCategory>(static_cast<uint32_t>(InputKeyCategory::A) + Index));
        }
        for (int Index = 0; Index < 10; ++Index)
        {
            Built.emplace_back(GLFW_KEY_0 + Index,
                               static_cast<InputKeyCategory>(static_cast<uint32_t>(InputKeyCategory::Digit0) + Index));
        }
        for (int Index = 0; Index < 12; ++Index)
        {
            Built.emplace_back(GLFW_KEY_F1 + Index,
                               static_cast<InputKeyCategory>(static_cast<uint32_t>(InputKeyCategory::F1) + Index));
        }
        Built.emplace_back(GLFW_KEY_ESCAPE, InputKeyCategory::Escape);
        Built.emplace_back(GLFW_KEY_ENTER, InputKeyCategory::Enter);
        Built.emplace_back(GLFW_KEY_KP_ENTER, InputKeyCategory::Enter);
        Built.emplace_back(GLFW_KEY_SPACE, InputKeyCategory::Space);
        Built.emplace_back(GLFW_KEY_TAB, InputKeyCategory::Tab);
        Built.emplace_back(GLFW_KEY_COMMA, InputKeyCategory::Comma);
        Built.emplace_back(GLFW_KEY_PERIOD, InputKeyCategory::Period);
        Built.emplace_back(GLFW_KEY_HOME, InputKeyCategory::Home);
        Built.emplace_back(GLFW_KEY_END, InputKeyCategory::End);
        Built.emplace_back(GLFW_KEY_DELETE, InputKeyCategory::Delete);
        Built.emplace_back(GLFW_KEY_LEFT, InputKeyCategory::Left);
        Built.emplace_back(GLFW_KEY_RIGHT, InputKeyCategory::Right);
        Built.emplace_back(GLFW_KEY_UP, InputKeyCategory::Up);
        Built.emplace_back(GLFW_KEY_DOWN, InputKeyCategory::Down);
        return Built;
    }();
    return Table;
}

[[nodiscard]] bool KeyHeld(GLFWwindow* Window, int Key)
{
    return glfwGetKey(Window, Key) == GLFW_PRESS;
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                                                             POLL
//----------------------------------------------------------------------------------------------------------------------

void GlfwInputPoll::Poll(GLFWwindow* Window, InputSubstrate& Substrate)
{
    if (Window == nullptr)
    {
        return;
    }

    for (const auto& [GlfwKey, Category] : KeyTable())
    {
        const size_t Index = static_cast<size_t>(Category);
        const bool   Down  = KeyHeld(Window, GlfwKey);
        if (Down != PreviousKeys[Index])
        {
            PreviousKeys[Index] = Down;
            Substrate.InjectKeyTransition(Category, Down, false);
        }
    }

    InputModifiers Modifiers;
    Modifiers.Control = KeyHeld(Window, GLFW_KEY_LEFT_CONTROL) || KeyHeld(Window, GLFW_KEY_RIGHT_CONTROL);
    Modifiers.Alt     = KeyHeld(Window, GLFW_KEY_LEFT_ALT) || KeyHeld(Window, GLFW_KEY_RIGHT_ALT);
    Modifiers.Shift   = KeyHeld(Window, GLFW_KEY_LEFT_SHIFT) || KeyHeld(Window, GLFW_KEY_RIGHT_SHIFT);
    Modifiers.Super   = KeyHeld(Window, GLFW_KEY_LEFT_SUPER) || KeyHeld(Window, GLFW_KEY_RIGHT_SUPER);
    Substrate.InjectModifiers(Modifiers);

    static constexpr std::pair<int, PointerButtonCategory> Buttons[] = {
        {GLFW_MOUSE_BUTTON_LEFT, PointerButtonCategory::Left},
        {GLFW_MOUSE_BUTTON_RIGHT, PointerButtonCategory::Right},
        {GLFW_MOUSE_BUTTON_MIDDLE, PointerButtonCategory::Middle},
        {GLFW_MOUSE_BUTTON_4, PointerButtonCategory::SideOne},
        {GLFW_MOUSE_BUTTON_5, PointerButtonCategory::SideTwo},
    };
    for (const auto& [GlfwButton, Category] : Buttons)
    {
        const size_t Index = static_cast<size_t>(Category);
        const bool   Down  = glfwGetMouseButton(Window, GlfwButton) == GLFW_PRESS;
        if (Down != PreviousButtons[Index])
        {
            PreviousButtons[Index] = Down;
            Substrate.InjectPointerButtonTransition(Category, Down);
        }
    }

    double CursorX = 0.0, CursorY = 0.0;
    glfwGetCursorPos(Window, &CursorX, &CursorY);
    Substrate.InjectPointerPosition(static_cast<float>(CursorX), static_cast<float>(CursorY));

    GLFWgamepadstate PadState{};
    const bool       Connected = glfwJoystickIsGamepad(GLFW_JOYSTICK_1) &&
                           glfwGetGamepadState(GLFW_JOYSTICK_1, &PadState) == GLFW_TRUE;
    if (Connected)
    {
        GamepadReading Reading;
        Reading.LeftX        = PadState.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
        Reading.LeftY        = PadState.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
        Reading.RightX       = PadState.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
        Reading.RightY       = PadState.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
        Reading.TriggerLeft  = (PadState.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f;    // [-1..1] -> [0..1]
        Reading.TriggerRight = (PadState.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f;   // [-1..1] -> [0..1]
        for (int Button = 0; Button <= GLFW_GAMEPAD_BUTTON_LAST; ++Button)
        {
            if (PadState.buttons[Button] == GLFW_PRESS)
            {
                Reading.Buttons |= 1u << Button;
            }
        }
        Reading.Connected = true;
        Substrate.InjectGamepad(Reading);
        GamepadWasConnected = true;
    }
    else if (GamepadWasConnected)
    {
        Substrate.InjectGamepad(GamepadReading{});
        GamepadWasConnected = false;
    }
}

} // namespace Frontier
