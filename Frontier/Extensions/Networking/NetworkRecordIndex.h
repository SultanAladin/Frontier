#pragma once
/*==========================================================================================================================================
                                                           NETWORKRECORDINDEX.H
==========================================================================================================================================*/
// 🧩 Bidirectional authority index associating local WorldSpace RecordTokens and PlayerTokens with network replication identifiers.

#include "Frontier/Engine/Scene/RecordEntry.h"
#include "Frontier/Engine/Session/PlayerToken.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Frontier
{

struct NetworkRecordBinding
{
    uint64_t    NetworkIdentifier = 0;
    RecordToken LocalRecordToken{};
    PlayerToken OwnerPlayerToken{};
};

class NetworkRecordIndex
{
public:
    NetworkRecordIndex() = default;

    uint64_t                                 AllocateAndBind(RecordToken LocalToken, PlayerToken OwnerToken = PlayerToken{});
    void                                     BindExplicit(uint64_t NetworkIdentifier, RecordToken LocalToken, PlayerToken OwnerToken = PlayerToken{});
    bool                                     RemoveByNetworkIdentifier(uint64_t NetworkIdentifier) noexcept;
    bool                                     RemoveByLocalToken(RecordToken LocalToken) noexcept;
    void                                     Clear() noexcept;

    RecordToken                              ResolveLocalToken(uint64_t NetworkIdentifier) const noexcept;
    uint64_t                                 ResolveNetworkIdentifier(RecordToken LocalToken) const noexcept;
    PlayerToken                              ResolveOwnerPlayerToken(uint64_t NetworkIdentifier) const noexcept;

    size_t                                   QueryCount() const noexcept { return Bindings.size(); }
    const std::vector<NetworkRecordBinding>& QueryBindings() const noexcept { return Bindings; }

private:
    std::vector<NetworkRecordBinding>        Bindings;
    uint64_t                                 NextNetworkIdentifier = 1;
};

} // namespace Frontier
