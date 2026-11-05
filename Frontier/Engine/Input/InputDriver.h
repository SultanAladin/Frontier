/*==========================================================================================================================================
                                                               INPUTDRIVER.H
==========================================================================================================================================*/
// 🧩 Human-controlled driver that binds a PlayerToken and InputEncoder to a controllable scene Agent.
//    Samples either a GLFW window or an injected RawInputState/InputPacket and drives the possessed Agent.

#pragma once

#include "Frontier/Engine/Input/InputEncoder.h"
#include "Frontier/Engine/Input/InputPacket.h"
#include "Frontier/Engine/Scene/Agent.h"
#include "Frontier/Engine/Session/PlayerToken.h"

struct GLFWwindow;

namespace Frontier
{

class WorldSpace;

class InputDriver
{
  public:
    InputDriver() = default;
    explicit InputDriver(PlayerToken OwnerPlayerToken) noexcept;

    void BindAgent(Agent* TargetAgent) noexcept;
    void UnbindAgent() noexcept;

    [[nodiscard]] bool                HasBoundAgent() const noexcept { return BoundAgent != nullptr; }
    [[nodiscard]] Agent*              QueryBoundAgent() const noexcept { return BoundAgent; }
    [[nodiscard]] PlayerToken         QueryPlayerToken() const noexcept { return OwnerToken; }
    [[nodiscard]] const InputPacket&  QueryLatestPacket() const noexcept { return Encoder.QueryLatestPacket(); }
    [[nodiscard]] InputEncoder&       AccessEncoder() noexcept { return Encoder; }
    [[nodiscard]] const InputEncoder& QueryEncoder() const noexcept { return Encoder; }

    // Samples GLFW window hardware via InputEncoder and drives the bound Agent for StepSeconds.
    InputPacket DriveFromWindow(GLFWwindow* Window, float StepSeconds, WorldSpace* World = nullptr) noexcept;

    // Encodes a headless RawInputState via InputEncoder and drives the bound Agent for StepSeconds.
    InputPacket DriveFromState(const RawInputState& State, float StepSeconds, WorldSpace* World = nullptr) noexcept;

    // Applies an already-encoded InputPacket (e.g. received from a remote peer) to the bound Agent.
    void DriveFromPacket(const InputPacket& Packet, float StepSeconds, WorldSpace* World = nullptr) noexcept;

  private:
    PlayerToken  OwnerToken{};
    Agent*       BoundAgent = nullptr;
    InputEncoder Encoder{};
};

} // namespace Frontier
