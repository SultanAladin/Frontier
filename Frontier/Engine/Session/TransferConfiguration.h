/*==========================================================================================================================================
                                                          TRANSFERCONFIGURATION.H
==========================================================================================================================================*/
// 🧩 Seamless level transition configuration preserving PlayerStore profiles, customized VehicleUnitConfigurations, and TargetPolicyName.

#pragma once

#include "Frontier/Engine/Session/PlayerProfile.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

struct TransferConfiguration
{
    std::string                SourceLevelPath{};
    std::string                TargetLevelPath{};
    std::string                TargetPolicyName{};
    uint64_t                   TransitionEpoch = 0;
    std::vector<PlayerProfile> PreservedProfiles{};

    [[nodiscard]] std::string SerializeToToml() const;
    bool                      DeserializeFromToml(std::string_view TomlText);
};

} // namespace Frontier
