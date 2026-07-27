/*==============================================================================================================================================
                                                                INPUTENCODER.H
==============================================================================================================================================*/
// 🧩 Derives normalized, edge-detected engine input from the per-integration InputPacket — the downstream half of the input seam. The
//    platform window (PlatformWindow) already pumps the OS event queue and publishes one integration of decoded level state (held keys/buttons,
//    pointer delta, scroll) into an InputPacket; this component NEVER touches the OS. It retains the previous integration's level state and, each
//    time a new snapshot is integrated, computes the transitions game/UI code consumes: which keys/buttons went down THIS integration (pressed
//    edge), which came up (released edge), and how long each has been held. Motion (pointer delta / scroll) is passed through verbatim — those are
//    already per-integration accumulators the packet resets each poll. Fixed-size arrays mirror InputPacket → zero allocation, no OS types.
#pragma once
#ifndef FRONTIER_PLATFORM_INPUTPROVIDER_INPUTENCODER_H
#define FRONTIER_PLATFORM_INPUTPROVIDER_INPUTENCODER_H

#include <cstdint>

#include "EngineContext/Input/InputPacket.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The encoder retains one integration of history so it can compute edges. PreviousKeyHeld / PreviousButtonHeld are the prior
//    integration's level state; CurrentKeyHeld / CurrentButtonHeld are this one's (copied from the integrated snapshot). KeyHeldDuration
//    / ButtonHeldDuration count consecutive integrations each input has been down (0 while up, 1 on the pressed edge, incrementing while
//    held) so a caller can implement repeat / long-press without its own bookkeeping. PointerDelta / Scroll are the passed-through
//    motion accumulators. Everything is fixed-size POD indexed by KeyIdentity / PointerButton — no heap, no OS type, copyable.
struct InputEncoder
{
    bool     PreviousKeyHeld[KeyIdentityCount]        = {};   // [-]      - Key level state last integration
    bool     CurrentKeyHeld[KeyIdentityCount]         = {};   // [-]      - Key level state this integration
    bool     PreviousButtonHeld[PointerButtonCount]   = {};   // [-]      - Button level state last integration
    bool     CurrentButtonHeld[PointerButtonCount]    = {};   // [-]      - Button level state this integration

    uint32_t KeyHeldDuration[KeyIdentityCount]        = {};   // [integrations] - Consecutive integrations each key has been down
    uint32_t ButtonHeldDuration[PointerButtonCount]   = {};   // [integrations] - Consecutive integrations each button has been down

    double   PointerPositionX = 0.0;                          // [px]     - Absolute cursor X (passed through from the snapshot)
    double   PointerPositionY = 0.0;                          // [px]     - Absolute cursor Y (passed through from the snapshot)
    double   PointerDeltaX    = 0.0;                          // [px]     - This-integration pointer X motion (passed through)
    double   PointerDeltaY    = 0.0;                          // [px]     - This-integration pointer Y motion (passed through)
    double   ScrollDelta      = 0.0;                          // [-]      - This-integration wheel notches (passed through)

    bool     Initialized      = false;                        // [-]      - False until the first snapshot is integrated
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Reset the encoder to a clean neutral integration (all keys/buttons up, zero held-duration counts, zero motion). Call once before
// the first IntegrateInputPacket. Idempotent — safe to call again to discard accumulated edge history.
void InitializeInputEncoder(InputEncoder& Encoder) noexcept;

// Advance one integration: roll this integration's level state into the previous, copy the new packet's level state in, recompute
// the held-duration counters (reset to 0 on release, ++ while held), and pass the motion accumulators through. On the FIRST call the
// previous integration is seeded from the same packet so nothing reports a spurious pressed edge at startup. Call once per poll,
// after PollPlatformEvents has refilled the packet.
void IntegrateInputPacket(InputEncoder& Encoder, const InputPacket& Packet) noexcept;

// True on the single integration a key transitioned up → down (up the prior integration, down this one). Out-of-range → false.
[[nodiscard]] bool KeyPressed(const InputEncoder& Encoder, KeyIdentity Key) noexcept;

// True on the single integration a key transitioned down → up. Out-of-range → false.
[[nodiscard]] bool KeyReleased(const InputEncoder& Encoder, KeyIdentity Key) noexcept;

// True while a key is down this integration (level, not edge). Out-of-range → false.
[[nodiscard]] bool KeyHeldNow(const InputEncoder& Encoder, KeyIdentity Key) noexcept;

// Consecutive integrations the key has been held (0 while up, 1 on the pressed edge, incrementing thereafter). Out-of-range → 0.
[[nodiscard]] uint32_t KeyHeldDurationCount(const InputEncoder& Encoder, KeyIdentity Key) noexcept;

// True on the single integration a pointer button transitioned up → down. Out-of-range → false.
[[nodiscard]] bool ButtonPressed(const InputEncoder& Encoder, PointerButton Button) noexcept;

// True on the single integration a pointer button transitioned down → up. Out-of-range → false.
[[nodiscard]] bool ButtonReleased(const InputEncoder& Encoder, PointerButton Button) noexcept;

// True while a pointer button is down this integration (level, not edge). Out-of-range → false.
[[nodiscard]] bool ButtonHeldNow(const InputEncoder& Encoder, PointerButton Button) noexcept;

// Consecutive integrations the button has been held (0 while up, 1 on the pressed edge, incrementing thereafter). Out-of-range → 0.
[[nodiscard]] uint32_t ButtonHeldDurationCount(const InputEncoder& Encoder, PointerButton Button) noexcept;

}   // namespace Frontier

#endif
