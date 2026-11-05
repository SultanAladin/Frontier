#pragma once
/*==========================================================================================================================================
                                                              PLAYERSTORE.H
==========================================================================================================================================*/
// 🧩 Authoritative roster of joined PlayerProfiles, managing unit assignment, Car Studio specifications, and lobby readiness.

#include "Frontier/Engine/Session/PlayerProfile.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

class PlayerStore
{
public:
    PlayerStore() = default;

    PlayerToken                       RegisterPlayer(const std::string& DisplayName, bool IsLocalHost = false, uint32_t NetworkPeerIdentifier = 0);
    bool                              RemovePlayer(PlayerToken Token) noexcept;
    void                              Clear() noexcept;

    PlayerProfile*                    FindProfile(PlayerToken Token) noexcept;
    const PlayerProfile*              FindProfile(PlayerToken Token) const noexcept;
    PlayerProfile*                    FindProfileByPeer(uint32_t NetworkPeerIdentifier) noexcept;
    const PlayerProfile*              FindProfileByPeer(uint32_t NetworkPeerIdentifier) const noexcept;
    PlayerProfile*                    FindLocalHostProfile() noexcept;
    const PlayerProfile*              FindLocalHostProfile() const noexcept;

    bool                              AssignActiveUnit(PlayerToken Token, RecordToken UnitToken) noexcept;
    bool                              ReleaseActiveUnit(PlayerToken Token, bool PreserveAsStored = true) noexcept;
    bool                              AssignVehicleConfiguration(PlayerToken Token, const VehicleUnitConfiguration& Spec) noexcept;
    bool                              AssignReadyState(PlayerToken Token, bool Ready) noexcept;

    bool                              AreAllConnectedReady() const noexcept;
    size_t                            QueryConnectedCount() const noexcept;
    size_t                            QueryReadyCount() const noexcept;

    const std::vector<PlayerProfile>& QueryProfiles() const noexcept { return Profiles; }
    std::vector<PlayerProfile>&       AccessProfiles() noexcept { return Profiles; }

    void                              RestoreProfile(const PlayerProfile& SavedProfile);

private:
    std::vector<PlayerProfile>        Profiles;
    uint64_t                          NextTokenSequence = 1001;
};

} // namespace Frontier
