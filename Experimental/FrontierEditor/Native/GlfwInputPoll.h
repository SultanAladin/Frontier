/*==========================================================================================================================================
                                                              GLFWINPUTPOLL.H
==========================================================================================================================================*/
// 🧩 GLFW polling ingestion for the shared substrate: level-diffed keys/buttons, pointer, modifiers, gamepad.

#pragma once

#include "Input/InputSubstrate.h"

#include <array>
#include <cstddef>

struct GLFWwindow;

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                       GLFW INPUT POLL
//----------------------------------------------------------------------------------------------------------------------

// 📝 Polling rather than GLFW callbacks by design: imgui_impl_glfw owns the callback slots, and levels
//    diffed once per cycle give the substrate exactly the same edges without callback chaining. Wheel
//    has no polled level in GLFW, so the host forwards ImGui IO's accumulated wheel separately.
//    Translation emits stable InputKeyCategory tokens — platform key codes never leave this unit.
class GlfwInputPoll
{
  public:
    void Poll(GLFWwindow* Window, InputSubstrate& Substrate);

  private:
    static constexpr size_t KeyCount    = static_cast<size_t>(InputKeyCategory::Count);
    static constexpr size_t ButtonCount = static_cast<size_t>(PointerButtonCategory::Count);

    std::array<bool, KeyCount>    PreviousKeys{};
    std::array<bool, ButtonCount> PreviousButtons{};
    bool                          GamepadWasConnected = false;
};

} // namespace Frontier
