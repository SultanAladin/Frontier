/*==========================================================================================================================================
                                                            SYNTHETICDRIVER.CPP
==========================================================================================================================================*/
// 🧩 Autonomous SyntheticDriver waypoint guidance and InputPacket synthesis.

#include "Frontier/Engine/Input/SyntheticDriver.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

SyntheticDriver::SyntheticDriver(PlayerToken SyntheticOwnerToken) noexcept
    : OwnerToken(SyntheticOwnerToken)
{
}

void SyntheticDriver::BindAgent(Agent* TargetAgent) noexcept
{
    if (BoundAgent != nullptr && BoundAgent != TargetAgent)
    {
        BoundAgent->AssignDriverKind(AgentDriverKind::Unassigned);
    }
    BoundAgent = TargetAgent;
    if (BoundAgent != nullptr)
    {
        BoundAgent->AssignDriverKind(AgentDriverKind::SyntheticDriven);
        if (OwnerToken.IsValid())
        {
            BoundAgent->AssignOwnerPlayerToken(OwnerToken.Value);
        }
    }
}

void SyntheticDriver::UnbindAgent() noexcept
{
    if (BoundAgent != nullptr)
    {
        BoundAgent->AssignDriverKind(AgentDriverKind::Unassigned);
        BoundAgent = nullptr;
    }
}

void SyntheticDriver::ConfigureWaypoints(std::vector<SyntheticWaypoint> NewWaypoints)
{
    Waypoints            = std::move(NewWaypoints);
    CurrentWaypointIndex = 0;
}

InputPacket SyntheticDriver::StepDrive(float StepSeconds, WorldSpace* World) noexcept
{
    ++StepCounter;
    AccumulatedSeconds += static_cast<double>(std::max(0.0f, StepSeconds));

    InputPacket Packet{};
    Packet.SequenceIndex = StepCounter;

    if (BoundAgent == nullptr)
    {
        LatestPacket = Packet;
        return Packet;
    }

    float TargetSpeed = DefaultTargetSpeedMps;
    float SteerCommand = 0.0f;

    if (!Waypoints.empty())
    {
        if (CurrentWaypointIndex >= Waypoints.size())
        {
            CurrentWaypointIndex = 0;
        }
        const SyntheticWaypoint& Target = Waypoints[CurrentWaypointIndex];
        const auto& Pos = BoundAgent->QueryPositionMetres();
        const float Dx  = Target.PositionMetres[0] - Pos[0];
        const float Dy  = Target.PositionMetres[1] - Pos[1];
        const float Dist = std::sqrt(Dx * Dx + Dy * Dy);

        if (Dist <= Target.AcceptanceRadiusM && Waypoints.size() > 1)
        {
            CurrentWaypointIndex = (CurrentWaypointIndex + 1) % Waypoints.size();
        }

        const SyntheticWaypoint& ActiveTarget = Waypoints[CurrentWaypointIndex];
        TargetSpeed = ActiveTarget.TargetSpeedMps;

        const float DesiredYawDeg = std::atan2(ActiveTarget.PositionMetres[1] - Pos[1],
                                               ActiveTarget.PositionMetres[0] - Pos[0]) * (180.0f / 3.14159265358979323846f);
        float YawErrorDeg = DesiredYawDeg - BoundAgent->QueryYawDegrees();
        while (YawErrorDeg > 180.0f)  YawErrorDeg -= 360.0f;
        while (YawErrorDeg < -180.0f) YawErrorDeg += 360.0f;

        SteerCommand = std::clamp(YawErrorDeg / 35.0f, -1.0f, 1.0f);
    }

    const float CurrentSpeed = BoundAgent->QueryForwardSpeedMps();
    const float CornerPenalty = 1.0f - 0.35f * std::fabs(SteerCommand);
    const float EffectiveTargetSpeed = TargetSpeed * CornerPenalty;

    Packet.SteerAxis = SteerCommand;
    if (CurrentSpeed < EffectiveTargetSpeed - 0.5f)
    {
        Packet.ThrottleAxis = std::clamp((EffectiveTargetSpeed - CurrentSpeed) / 10.0f, 0.25f, 1.0f);
        Packet.BrakeAxis    = 0.0f;
    }
    else if (CurrentSpeed > EffectiveTargetSpeed + 1.5f)
    {
        Packet.ThrottleAxis = 0.0f;
        Packet.BrakeAxis    = std::clamp((CurrentSpeed - EffectiveTargetSpeed) / 12.0f, 0.1f, 1.0f);
    }
    else
    {
        Packet.ThrottleAxis = 0.22f;
        Packet.BrakeAxis    = 0.0f;
    }

    LatestPacket = Packet;
    BoundAgent->AssignDriverKind(AgentDriverKind::SyntheticDriven);
    BoundAgent->ApplyInputPacket(Packet, StepSeconds, World);
    return Packet;
}

} // namespace Frontier
