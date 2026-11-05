/*==========================================================================================================================================
                                                             SESSIONPOLICY.H
==========================================================================================================================================*/
// 🧩 Project Everest custom SessionPolicy rules: EverestLobbyPolicy (RespawnRule::Disabled in Lobby), EverestProvingGroundsPolicy,
//    EverestRacePolicy (RespawnRule::LastCheckpoint in ProvingGrounds), and EverestEliminationPolicy (RespawnRule::SpawnGrid).

#pragma once

#include "Frontier/Engine/Input/SyntheticDriver.h"
#include "Frontier/Engine/Session/SessionPolicy.h"

#include <memory>
#include <string_view>
#include <vector>

namespace Frontier::Everest
{

class EverestLobbyPolicy final : public LobbyPolicy
{
  public:
    EverestLobbyPolicy();
    ~EverestLobbyPolicy() override = default;
};

class EverestProvingGroundsPolicy final : public ProvingGroundsPolicy
{
  public:
    EverestProvingGroundsPolicy();
    ~EverestProvingGroundsPolicy() override = default;
};

class EverestRacePolicy final : public RacePolicy
{
  public:
    explicit EverestRacePolicy(uint32_t TargetLapCount = 2);
    ~EverestRacePolicy() override = default;
};

class EverestEliminationPolicy final : public EliminationPolicy
{
  public:
    explicit EverestEliminationPolicy(float IntervalSeconds = 15.0f);
    ~EverestEliminationPolicy() override = default;
};

[[nodiscard]] std::vector<SyntheticWaypoint> ConstructEverestCircuitWaypoints();
[[nodiscard]] std::unique_ptr<SessionPolicy> ConstructEverestSessionPolicy(std::string_view PolicyName);

} // namespace Frontier::Everest
