/*==========================================================================================================================================
                                                              INPUTDRIVER.CPP
==========================================================================================================================================*/
// 🧩 Human-controlled InputDriver implementation driving a scene Agent.

#include "Frontier/Engine/Input/InputDriver.h"

namespace Frontier
{

InputDriver::InputDriver(PlayerToken OwnerPlayerToken) noexcept
    : OwnerToken(OwnerPlayerToken)
{
}

void InputDriver::BindAgent(Agent* TargetAgent) noexcept
{
    if (BoundAgent != nullptr && BoundAgent != TargetAgent)
    {
        BoundAgent->AssignDriverKind(AgentDriverKind::Unassigned);
    }
    BoundAgent = TargetAgent;
    if (BoundAgent != nullptr)
    {
        BoundAgent->AssignDriverKind(AgentDriverKind::InputDriven);
        if (OwnerToken.IsValid())
        {
            BoundAgent->AssignOwnerPlayerToken(OwnerToken.Value);
        }
    }
}

void InputDriver::UnbindAgent() noexcept
{
    if (BoundAgent != nullptr)
    {
        BoundAgent->AssignDriverKind(AgentDriverKind::Unassigned);
        BoundAgent = nullptr;
    }
}

InputPacket InputDriver::DriveFromWindow(GLFWwindow* Window, float StepSeconds, WorldSpace* World) noexcept
{
    const InputPacket Packet = Encoder.SampleGlfwWindow(Window, StepSeconds);
    DriveFromPacket(Packet, StepSeconds, World);
    return Packet;
}

InputPacket InputDriver::DriveFromState(const RawInputState& State, float StepSeconds, WorldSpace* World) noexcept
{
    const InputPacket Packet = Encoder.EncodeState(State, StepSeconds);
    DriveFromPacket(Packet, StepSeconds, World);
    return Packet;
}

void InputDriver::DriveFromPacket(const InputPacket& Packet, float StepSeconds, WorldSpace* World) noexcept
{
    if (BoundAgent != nullptr)
    {
        BoundAgent->AssignDriverKind(AgentDriverKind::InputDriven);
        BoundAgent->ApplyInputPacket(Packet, StepSeconds, World);
    }
}

} // namespace Frontier
