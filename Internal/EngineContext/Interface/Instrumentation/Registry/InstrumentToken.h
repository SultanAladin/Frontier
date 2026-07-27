/*==============================================================================================================================================
                                                             INSTRUMENTTOKEN.H
==============================================================================================================================================*/
// 🧩 A stable, stale-detecting identity for one instrument in the store: a slot index plus a reuse count, so a token held across a remove+re-add of the same slot resolves as expired instead of silently addressing the wrong instrument.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTTOKEN_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTTOKEN_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Packed identity: SlotIndex locates the dense record; Generation is the slot's reuse count at issue time. A lookup is
//    valid only while the live slot's generation still equals the token's — so a token outliving its instrument fails the
//    match rather than aliasing whatever took the slot next. Value-typed and trivially copyable; callers hold it by value.
struct InstrumentToken
{
    uint32_t SlotIndex  = 0xFFFFFFFFu;   // [-] - dense-store slot this token addresses (0xFFFFFFFF = never issued)
    uint32_t Generation = 0u;            // [-] - slot reuse count captured when this token was issued
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// True when the token was never issued (the default-constructed sentinel). A vacant token addresses no instrument.
inline bool VacantToken(const InstrumentToken& Token)
{
    return Token.SlotIndex == 0xFFFFFFFFu;
}

// True when two tokens name the same instrument at the same generation — the identity comparison the store performs.
inline bool EquivalentToken(const InstrumentToken& Left, const InstrumentToken& Right)
{
    return Left.SlotIndex == Right.SlotIndex && Left.Generation == Right.Generation;
}

}   // namespace Frontier

#endif
