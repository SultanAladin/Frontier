/*==============================================================================================================================================
                                                                RECORDTOKEN.H
==============================================================================================================================================*/
// 🧩 Canonical generational identity value for any dense record store: a slot index paired with the generation live when the token was minted,
//    so a released-and-reused slot resolves as detectably stale rather than silently mis-pointing. Generation 0 is reserved null. Trivial POD,
//    passed by value; shared by Scene, Revision, and Instrumentation stores so every pillar names identity the same way (no forked copy).

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_MICROUTILS_RECORDTOKEN_H
#define FRONTIER_ENGINECONTEXT_MICROUTILS_RECORDTOKEN_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The unit of identity. Never a bare index into a store — always a token that both names a slot AND carries the generation
//    live when it was minted. Generation 0 is reserved as "never-valid", so a zero-initialized token is the null token.
struct RecordToken
{
    uint32_t SlotIndex  = 0u;   // [idx] - slot in the packed record store
    uint32_t Generation = 0u;   // [-]   - generation the slot held when this token was minted (0 == null)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                             CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// [-] - the canonical null token; a zero-initialized RecordToken compares equal to this.
constexpr RecordToken NullRecordToken = RecordToken{ 0u, 0u };

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Value identity — tokens are trivially comparable, storable in selection sets, and passed by value.
[[nodiscard]] constexpr bool operator==(const RecordToken& Left, const RecordToken& Right) noexcept
{
    return Left.SlotIndex == Right.SlotIndex && Left.Generation == Right.Generation;
}

[[nodiscard]] constexpr bool operator!=(const RecordToken& Left, const RecordToken& Right) noexcept
{
    return !(Left == Right);
}

// True when the token could name a live entry — a generation of 0 grants nothing and is the null token by construction.
[[nodiscard]] constexpr bool PopulatedToken(const RecordToken& Token) noexcept
{
    return Token.Generation != 0u;
}

} // namespace Frontier

#endif
