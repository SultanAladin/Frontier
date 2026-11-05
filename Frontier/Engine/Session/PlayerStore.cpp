/*==========================================================================================================================================
                                                             PLAYERSTORE.CPP
==========================================================================================================================================*/
// 🧩 PlayerStore roster management implementation.

#include "Frontier/Engine/Session/PlayerStore.h"

#include <algorithm>

namespace Frontier
{

PlayerToken PlayerStore::RegisterPlayer(const std::string& DisplayName, bool IsLocalHost, uint32_t NetworkPeerIdentifier)
{
    PlayerProfile Profile{};
    Profile.Token                 = PlayerToken{ NextTokenSequence++ };
    Profile.DisplayName           = DisplayName.empty() ? "Player" : DisplayName;
    Profile.NetworkPeerIdentifier = NetworkPeerIdentifier;
    Profile.IsLocalHost           = IsLocalHost;
    Profile.Connected             = true;
    Profile.Ready                 = false;
    Profiles.push_back(Profile);
    return Profile.Token;
}

bool PlayerStore::RemovePlayer(PlayerToken Token) noexcept
{
    const auto It = std::find_if(Profiles.begin(), Profiles.end(),
        [Token](const PlayerProfile& Entry) noexcept
        {
            return Entry.Token == Token;
        });
    if (It == Profiles.end())
    {
        return false;
    }
    Profiles.erase(It);
    return true;
}

void PlayerStore::Clear() noexcept
{
    Profiles.clear();
}

PlayerProfile* PlayerStore::FindProfile(PlayerToken Token) noexcept
{
    for (PlayerProfile& Entry : Profiles)
    {
        if (Entry.Token == Token)
        {
            return &Entry;
        }
    }
    return nullptr;
}

const PlayerProfile* PlayerStore::FindProfile(PlayerToken Token) const noexcept
{
    for (const PlayerProfile& Entry : Profiles)
    {
        if (Entry.Token == Token)
        {
            return &Entry;
        }
    }
    return nullptr;
}

PlayerProfile* PlayerStore::FindProfileByPeer(uint32_t NetworkPeerIdentifier) noexcept
{
    for (PlayerProfile& Entry : Profiles)
    {
        if (Entry.NetworkPeerIdentifier == NetworkPeerIdentifier)
        {
            return &Entry;
        }
    }
    return nullptr;
}

const PlayerProfile* PlayerStore::FindProfileByPeer(uint32_t NetworkPeerIdentifier) const noexcept
{
    for (const PlayerProfile& Entry : Profiles)
    {
        if (Entry.NetworkPeerIdentifier == NetworkPeerIdentifier)
        {
            return &Entry;
        }
    }
    return nullptr;
}

PlayerProfile* PlayerStore::FindLocalHostProfile() noexcept
{
    for (PlayerProfile& Entry : Profiles)
    {
        if (Entry.IsLocalHost)
        {
            return &Entry;
        }
    }
    return Profiles.empty() ? nullptr : &Profiles.front();
}

const PlayerProfile* PlayerStore::FindLocalHostProfile() const noexcept
{
    for (const PlayerProfile& Entry : Profiles)
    {
        if (Entry.IsLocalHost)
        {
            return &Entry;
        }
    }
    return Profiles.empty() ? nullptr : &Profiles.front();
}

bool PlayerStore::AssignActiveUnit(PlayerToken Token, RecordToken UnitToken) noexcept
{
    PlayerProfile* Profile = FindProfile(Token);
    if (Profile == nullptr)
    {
        return false;
    }
    Profile->ActiveUnitToken = UnitToken;
    return true;
}

bool PlayerStore::ReleaseActiveUnit(PlayerToken Token, bool PreserveAsStored) noexcept
{
    PlayerProfile* Profile = FindProfile(Token);
    if (Profile == nullptr)
    {
        return false;
    }
    if (PreserveAsStored && Profile->ActiveUnitToken.IsValid())
    {
        Profile->StoredUnitToken = Profile->ActiveUnitToken;
    }
    Profile->ActiveUnitToken = RecordToken{};
    return true;
}

bool PlayerStore::AssignVehicleConfiguration(PlayerToken Token, const VehicleUnitConfiguration& Spec) noexcept
{
    PlayerProfile* Profile = FindProfile(Token);
    if (Profile == nullptr)
    {
        return false;
    }
    Profile->VehicleSpec = Spec;
    return true;
}

bool PlayerStore::AssignReadyState(PlayerToken Token, bool Ready) noexcept
{
    PlayerProfile* Profile = FindProfile(Token);
    if (Profile == nullptr)
    {
        return false;
    }
    Profile->Ready = Ready;
    return true;
}

bool PlayerStore::AreAllConnectedReady() const noexcept
{
    size_t ConnectedCount = 0;
    for (const PlayerProfile& Entry : Profiles)
    {
        if (!Entry.Connected)
        {
            continue;
        }
        ++ConnectedCount;
        if (!Entry.Ready)
        {
            return false;
        }
    }
    return ConnectedCount > 0;
}

size_t PlayerStore::QueryConnectedCount() const noexcept
{
    size_t Count = 0;
    for (const PlayerProfile& Entry : Profiles)
    {
        if (Entry.Connected)
        {
            ++Count;
        }
    }
    return Count;
}

size_t PlayerStore::QueryReadyCount() const noexcept
{
    size_t Count = 0;
    for (const PlayerProfile& Entry : Profiles)
    {
        if (Entry.Connected && Entry.Ready)
        {
            ++Count;
        }
    }
    return Count;
}

void PlayerStore::RestoreProfile(const PlayerProfile& SavedProfile)
{
    if (PlayerProfile* Existing = FindProfile(SavedProfile.Token))
    {
        *Existing = SavedProfile;
    }
    else
    {
        Profiles.push_back(SavedProfile);
    }
    if (SavedProfile.Token.Value >= NextTokenSequence)
    {
        NextTokenSequence = SavedProfile.Token.Value + 1;
    }
}

} // namespace Frontier
