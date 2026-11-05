/*==========================================================================================================================================
                                                          SESSIONCONFIGURATION.H
==========================================================================================================================================*/
// 🧩 Session configuration (capacity, startup level, proving grounds level, active SessionPolicy, NetPolicy, and countdown).

#pragma once

#include <cstdint>
#include <string>

namespace Frontier
{

struct SessionConfiguration
{
    std::string SessionTitle            = "Everest Car Studio Session";
    std::string LobbyLevelPath          = "Content/Levels/Default.levelspace";
    std::string ProvingGroundsLevelPath = "Content/Levels/ProvingGroundVehicle.levelspace";
    std::string LobbyPolicyName         = "LobbyPolicy";
    std::string MatchPolicyName         = "RacePolicy";
    uint32_t    MaximumPlayers          = 8;
    float       LobbyCountdownSeconds   = 3.0f;
    bool        AllowSyntheticBackfill  = true;

    [[nodiscard]] std::string SerializeToToml() const;
    bool                      DeserializeFromToml(std::string_view TomlText);
};

} // namespace Frontier
