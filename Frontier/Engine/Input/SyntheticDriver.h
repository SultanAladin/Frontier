/*==========================================================================================================================================
                                                             SYNTHETICDRIVER.H
==========================================================================================================================================*/
// 🧩 Autonomous AI driver (SyntheticDriver) that steers, accelerates, and brakes a controllable scene Agent along a waypoint circuit.
//    Emits standard InputPacket frames so the Agent never distinguishes between human (InputDriver) and AI (SyntheticDriver) control.

#pragma once

#include "Frontier/Engine/Input/InputPacket.h"
#include "Frontier/Engine/Scene/Agent.h"
#include "Frontier/Engine/Session/PlayerToken.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Frontier
{

class WorldSpace;

struct SyntheticWaypoint
{
    std::array<float, 3> PositionMetres    = { 0.0f, 0.0f, 0.0f };
    float                TargetSpeedMps    = 24.0f;
    float                AcceptanceRadiusM = 8.0f;
};

class SyntheticDriver
{
  public:
    SyntheticDriver() = default;
    explicit SyntheticDriver(PlayerToken SyntheticOwnerToken) noexcept;

    void BindAgent(Agent* TargetAgent) noexcept;
    void UnbindAgent() noexcept;

    [[nodiscard]] bool                              HasBoundAgent() const noexcept { return BoundAgent != nullptr; }
    [[nodiscard]] Agent*                            QueryBoundAgent() const noexcept { return BoundAgent; }
    [[nodiscard]] PlayerToken                       QueryPlayerToken() const noexcept { return OwnerToken; }
    [[nodiscard]] const InputPacket&                QueryLatestPacket() const noexcept { return LatestPacket; }
    [[nodiscard]] size_t                            QueryCurrentWaypointIndex() const noexcept { return CurrentWaypointIndex; }
    [[nodiscard]] const std::vector<SyntheticWaypoint>& QueryWaypoints() const noexcept { return Waypoints; }

    void ConfigureWaypoints(std::vector<SyntheticWaypoint> NewWaypoints);
    void ConfigureTargetSpeedMps(float TargetSpeed) noexcept { DefaultTargetSpeedMps = TargetSpeed; }

    // Evaluates steering, throttle, and braking toward the active waypoint and drives the bound Agent for StepSeconds.
    InputPacket StepDrive(float StepSeconds, WorldSpace* World = nullptr) noexcept;

  private:
    PlayerToken                    OwnerToken{};
    Agent*                         BoundAgent            = nullptr;
    std::vector<SyntheticWaypoint> Waypoints{};
    size_t                         CurrentWaypointIndex  = 0;
    float                          DefaultTargetSpeedMps = 22.0f;
    uint64_t                       StepCounter           = 0;
    double                         AccumulatedSeconds    = 0.0;
    InputPacket                    LatestPacket{};
};

} // namespace Frontier
