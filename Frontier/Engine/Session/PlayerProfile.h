/*==========================================================================================================================================
                                                              PLAYERPROFILE.H
==========================================================================================================================================*/
// 🧩 Per-player identity, readiness, customized Car Studio VehicleUnitConfiguration, and possessed Agent RecordTokens.

#pragma once

#include "Frontier/Engine/Input/InputPacket.h"
#include "Frontier/Engine/Scene/Agent.h"
#include "Frontier/Engine/Scene/RecordEntry.h"
#include "Frontier/Engine/Session/PlayerToken.h"

#include <cstdint>
#include <string>

namespace Frontier
{

struct PlayerProfile
{
    PlayerToken              Token{};
    std::string              DisplayName           = "Driver";
    std::string              NetworkEndpoint       = "local";
    uint32_t                 NetworkPeerIdentifier = 0;
    bool                     IsLocalHost           = false;
    bool                     SyntheticAi           = false; // True when driven by SyntheticDriver, false when driven by InputDriver
    bool                     Ready                 = false;
    bool                     Connected             = true;
    RecordToken              ActiveUnitToken{};
    RecordToken              StoredUnitToken{};
    VehicleUnitConfiguration VehicleSpec{};
    InputPacket              LatestInput{};
    int32_t                  Score                 = 0;
};

} // namespace Frontier
