/*==============================================================================================================================================
                                                            TOKENAUTHENTICATION.H
==============================================================================================================================================*/
// 🧩 Canonical stale-token guard for any dense record store: a RecordToken is authentic only when its slot is in bounds, occupied, AND its
//    generation still matches the slot's live generation. ResolveEntry hands back the entry pointer on success or nullptr on a stale/invalid
//    token, so a token that outlived its entry fails cleanly instead of mis-resolving. Templated on the store so every pillar shares one guard.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_MICROUTILS_TOKENAUTHENTICATION_H
#define FRONTIER_ENGINECONTEXT_MICROUTILS_TOKENAUTHENTICATION_H

#include <cstddef>
#include <cstdint>

#include "EngineContext/MicroUtils/RecordToken.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 These operate over any store that exposes the record-store contract: parallel Generations (per-slot live generation) and
//    Occupancy (per-slot 1/0) arrays, plus an Entries array whose element type the caller reads. Templating on the store type
//    keeps identity logic in one place while binding to no single pillar's struct (C++17 duck-typed; no concepts).

// True when the token names a live entry: populated, in bounds, occupied, and generation-matched. A token that outlived its
// entry (slot reclaimed, generation advanced) fails here rather than mis-resolving.
template <typename RecordStoreType>
[[nodiscard]] bool AuthenticateToken(const RecordStoreType& Store, const RecordToken& Token) noexcept
{
    if (!PopulatedToken(Token))
    {
        return false;
    }

    if (static_cast<std::size_t>(Token.SlotIndex) >= Store.Generations.size())
    {
        return false;
    }

    if (static_cast<std::size_t>(Token.SlotIndex) >= Store.Occupancy.size() || Store.Occupancy[Token.SlotIndex] == 0u)
    {
        return false;
    }

    return Store.Generations[Token.SlotIndex] == Token.Generation;
}

// Resolve a token to its entry, or nullptr when the token is stale/invalid. The pointer is valid only until the next mutation
// of the store (which may reallocate the entry array) — callers hold tokens, not pointers, across edits.
template <typename RecordStoreType>
[[nodiscard]] auto ResolveEntry(RecordStoreType& Store, const RecordToken& Token) -> decltype(&Store.Entries[0])
{
    if (!AuthenticateToken(Store, Token))
    {
        return nullptr;
    }

    return &Store.Entries[Token.SlotIndex];
}

// Const overload for read-only consumers (an outliner construction pass takes the store by const reference).
template <typename RecordStoreType>
[[nodiscard]] auto ResolveEntry(const RecordStoreType& Store, const RecordToken& Token) -> decltype(&Store.Entries[0])
{
    if (!AuthenticateToken(Store, Token))
    {
        return nullptr;
    }

    return &Store.Entries[Token.SlotIndex];
}

} // namespace Frontier

#endif
