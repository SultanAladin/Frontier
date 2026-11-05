/*==========================================================================================================================================
                                                         SESSIONCONFIGURATION.CPP
==========================================================================================================================================*/
// 🧩 TOML codec for SessionConfiguration.

#include "Frontier/Engine/Session/SessionConfiguration.h"

#include <sstream>
#include <toml++/toml.hpp>

namespace Frontier
{

std::string SessionConfiguration::SerializeToToml() const
{
    toml::table Root;
    toml::table SessionTable;
    SessionTable.insert("title",                    SessionTitle);
    SessionTable.insert("lobby_level_path",         LobbyLevelPath);
    SessionTable.insert("proving_grounds_path",     ProvingGroundsLevelPath);
    SessionTable.insert("lobby_policy",             LobbyPolicyName);
    SessionTable.insert("match_policy",             MatchPolicyName);
    SessionTable.insert("maximum_players",          static_cast<int64_t>(MaximumPlayers));
    SessionTable.insert("lobby_countdown_seconds",  static_cast<double>(LobbyCountdownSeconds));
    SessionTable.insert("allow_synthetic_backfill", AllowSyntheticBackfill);
    Root.insert("session", std::move(SessionTable));

    std::ostringstream Stream;
    Stream << Root;
    return Stream.str();
}

bool SessionConfiguration::DeserializeFromToml(std::string_view TomlText)
{
    try
    {
        const toml::table Root = toml::parse(TomlText);
        if (const auto* SessionTable = Root["session"].as_table())
        {
            if (auto Val = (*SessionTable)["title"].value<std::string>())
                SessionTitle = *Val;
            if (auto Val = (*SessionTable)["lobby_level_path"].value<std::string>())
                LobbyLevelPath = *Val;
            if (auto Val = (*SessionTable)["proving_grounds_path"].value<std::string>())
                ProvingGroundsLevelPath = *Val;
            if (auto Val = (*SessionTable)["lobby_policy"].value<std::string>())
                LobbyPolicyName = *Val;
            if (auto Val = (*SessionTable)["match_policy"].value<std::string>())
                MatchPolicyName = *Val;
            if (auto Val = (*SessionTable)["maximum_players"].value<int64_t>())
                MaximumPlayers = static_cast<uint32_t>(*Val);
            if (auto Val = (*SessionTable)["lobby_countdown_seconds"].value<double>())
                LobbyCountdownSeconds = static_cast<float>(*Val);
            if (auto Val = (*SessionTable)["allow_synthetic_backfill"].value<bool>())
                AllowSyntheticBackfill = *Val;
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace Frontier
