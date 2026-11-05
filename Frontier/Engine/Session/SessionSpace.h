/*==========================================================================================================================================
                                                               SESSIONSPACE.H
==========================================================================================================================================*/
// 🧩 SessionSpace owns the PlayerStore, SessionConfiguration, active SessionPolicy, spawned Agents, InputDrivers (human),
//    and SyntheticDrivers (AI) across a multiplayer session.

#pragma once

#include "Frontier/Engine/Input/InputDriver.h"
#include "Frontier/Engine/Input/SyntheticDriver.h"
#include "Frontier/Engine/Scene/Agent.h"
#include "Frontier/Engine/Session/PlayerStore.h"
#include "Frontier/Engine/Session/SessionConfiguration.h"
#include "Frontier/Engine/Session/SessionPolicy.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Frontier
{

class WorldSpace;

class SessionSpace
{
  public:
    SessionSpace() = default;
    explicit SessionSpace(SessionConfiguration InitialConfiguration);

    [[nodiscard]] const SessionConfiguration&       QueryConfiguration() const noexcept { return Configuration; }
    [[nodiscard]] SessionConfiguration&             AccessConfiguration() noexcept { return Configuration; }
    [[nodiscard]] const PlayerStore&                QueryPlayerStore() const noexcept { return Players; }
    [[nodiscard]] PlayerStore&                      AccessPlayerStore() noexcept { return Players; }
    [[nodiscard]] const std::vector<Agent>&         QueryAgents() const noexcept { return Agents; }
    [[nodiscard]] std::vector<Agent>&               AccessAgents() noexcept { return Agents; }
    [[nodiscard]] const std::vector<InputDriver>&   QueryInputDrivers() const noexcept { return HumanDrivers; }
    [[nodiscard]] const std::vector<SyntheticDriver>& QuerySyntheticDrivers() const noexcept { return AiDrivers; }
    [[nodiscard]] const SessionPolicy*              QueryPolicy() const noexcept { return ActivePolicy.get(); }
    [[nodiscard]] SessionPolicy*                    QueryPolicy() noexcept { return ActivePolicy.get(); }
    [[nodiscard]] const std::string&                QueryActiveLevelPath() const noexcept { return ActiveLevelPath; }
    [[nodiscard]] uint64_t                          QueryTransitionEpoch() const noexcept { return TransitionEpoch; }

    void ConfigureActiveLevelPath(std::string LevelPath) { ActiveLevelPath = std::move(LevelPath); }
    void IncrementTransitionEpoch() noexcept { ++TransitionEpoch; }

    void BeginPolicy(std::unique_ptr<SessionPolicy> Policy, WorldSpace& World);

    PlayerToken AddPlayer(WorldSpace&      World,
                          std::string_view DisplayName,
                          std::string_view NetworkEndpoint = "local",
                          bool             IsLocalHost     = false);

    PlayerToken AddSyntheticPlayer(WorldSpace&                         World,
                                   std::string_view                    DisplayName,
                                   const std::vector<SyntheticWaypoint>& Waypoints = {});

    bool RemovePlayer(WorldSpace& World, PlayerToken Token);

    void SubmitPlayerInput(PlayerToken Token, const InputPacket& Packet);
    void StepSession(WorldSpace& World, float StepSeconds);

    // Enforces active SessionPolicy::RespawnRules (disabled in Lobby, allowed in ProvingGrounds).
    bool RequestAgentRespawn(WorldSpace& World, PlayerToken Token);

    // Spawns or binds a VehicleRecord in WorldSpace to an Agent owned by OwnerToken and attaches its InputDriver or SyntheticDriver.
    Agent& SpawnVehicleAgentForPlayer(WorldSpace&                 World,
                                      PlayerToken                 OwnerToken,
                                      std::string                 RecordName,
                                      const std::array<float, 3>& PositionMetres,
                                      float                       YawDegrees);

    bool ReleaseAgentForPlayer(WorldSpace& World, PlayerToken OwnerToken);

    // Switches possession of an Agent between a human InputDriver and an AI SyntheticDriver.
    InputDriver*     BindInputDriverToAgent(PlayerToken Token, Agent& TargetAgent);
    SyntheticDriver* BindSyntheticDriverToAgent(PlayerToken Token, Agent& TargetAgent, const std::vector<SyntheticWaypoint>& Waypoints = {});

    [[nodiscard]] Agent*           FindAgentByOwner(PlayerToken OwnerToken) noexcept;
    [[nodiscard]] const Agent*     FindAgentByOwner(PlayerToken OwnerToken) const noexcept;
    [[nodiscard]] InputDriver*     FindInputDriver(PlayerToken OwnerToken) noexcept;
    [[nodiscard]] SyntheticDriver* FindSyntheticDriver(PlayerToken OwnerToken) noexcept;

    void ClearAgents() noexcept;

  private:
    void RebindAllDriverPointers() noexcept;

    SessionConfiguration          Configuration{};
    PlayerStore                   Players{};
    std::vector<Agent>            Agents{};
    std::vector<InputDriver>      HumanDrivers{};
    std::vector<SyntheticDriver>  AiDrivers{};
    std::unique_ptr<SessionPolicy> ActivePolicy{};
    std::string                   ActiveLevelPath{};
    uint64_t                      TransitionEpoch = 1;
};

} // namespace Frontier
