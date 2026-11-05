/*==========================================================================================================================================
                                                          NETWORKRECORDINDEX.CPP
==========================================================================================================================================*/
// 🧩 NetworkRecordIndex implementation.

#include "Frontier/Extensions/Networking/NetworkRecordIndex.h"

#include <algorithm>

namespace Frontier
{

uint64_t NetworkRecordIndex::AllocateAndBind(RecordToken LocalToken, PlayerToken OwnerToken)
{
    const uint64_t Existing = ResolveNetworkIdentifier(LocalToken);
    if (Existing != 0)
    {
        BindExplicit(Existing, LocalToken, OwnerToken);
        return Existing;
    }
    const uint64_t Allocated = NextNetworkIdentifier++;
    BindExplicit(Allocated, LocalToken, OwnerToken);
    return Allocated;
}

void NetworkRecordIndex::BindExplicit(uint64_t NetworkIdentifier, RecordToken LocalToken, PlayerToken OwnerToken)
{
    if (NetworkIdentifier >= NextNetworkIdentifier)
    {
        NextNetworkIdentifier = NetworkIdentifier + 1;
    }
    for (NetworkRecordBinding& Entry : Bindings)
    {
        if (Entry.NetworkIdentifier == NetworkIdentifier || Entry.LocalRecordToken == LocalToken)
        {
            Entry.NetworkIdentifier = NetworkIdentifier;
            Entry.LocalRecordToken  = LocalToken;
            Entry.OwnerPlayerToken  = OwnerToken;
            return;
        }
    }
    Bindings.push_back(NetworkRecordBinding{ NetworkIdentifier, LocalToken, OwnerToken });
}

bool NetworkRecordIndex::RemoveByNetworkIdentifier(uint64_t NetworkIdentifier) noexcept
{
    const auto It = std::find_if(Bindings.begin(), Bindings.end(),
        [NetworkIdentifier](const NetworkRecordBinding& Entry) noexcept
        {
            return Entry.NetworkIdentifier == NetworkIdentifier;
        });
    if (It == Bindings.end())
    {
        return false;
    }
    Bindings.erase(It);
    return true;
}

bool NetworkRecordIndex::RemoveByLocalToken(RecordToken LocalToken) noexcept
{
    const auto It = std::find_if(Bindings.begin(), Bindings.end(),
        [LocalToken](const NetworkRecordBinding& Entry) noexcept
        {
            return Entry.LocalRecordToken == LocalToken;
        });
    if (It == Bindings.end())
    {
        return false;
    }
    Bindings.erase(It);
    return true;
}

void NetworkRecordIndex::Clear() noexcept
{
    Bindings.clear();
}

RecordToken NetworkRecordIndex::ResolveLocalToken(uint64_t NetworkIdentifier) const noexcept
{
    for (const NetworkRecordBinding& Entry : Bindings)
    {
        if (Entry.NetworkIdentifier == NetworkIdentifier)
        {
            return Entry.LocalRecordToken;
        }
    }
    return RecordToken{};
}

uint64_t NetworkRecordIndex::ResolveNetworkIdentifier(RecordToken LocalToken) const noexcept
{
    for (const NetworkRecordBinding& Entry : Bindings)
    {
        if (Entry.LocalRecordToken == LocalToken)
        {
            return Entry.NetworkIdentifier;
        }
    }
    return 0;
}

PlayerToken NetworkRecordIndex::ResolveOwnerPlayerToken(uint64_t NetworkIdentifier) const noexcept
{
    for (const NetworkRecordBinding& Entry : Bindings)
    {
        if (Entry.NetworkIdentifier == NetworkIdentifier)
        {
            return Entry.OwnerPlayerToken;
        }
    }
    return PlayerToken{};
}

} // namespace Frontier
