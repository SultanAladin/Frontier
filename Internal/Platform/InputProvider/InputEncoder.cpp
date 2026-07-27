/*==============================================================================================================================================
                                                                INPUTENCODER.CPP
==============================================================================================================================================*/
// 🧩 Edge + held-duration derivation over the per-integration InputPacket: retain previous level state, diff the new packet, pass motion through

#include "InputEncoder.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Bounds-checked index for a key. Returns -1 when the identity falls outside the held array so every accessor reads
    //    false / 0 for an unknown key rather than indexing past the fixed-size arrays.
    [[nodiscard]] int ResolveKeyIndex(KeyIdentity Key) noexcept
    {
        const int Index = static_cast<int>(Key);
        return (Index >= 0 && Index < KeyIdentityCount) ? Index : -1;
    }

    // 📝 Bounds-checked index for a pointer button, same contract as ResolveKeyIndex.
    [[nodiscard]] int ResolveButtonIndex(PointerButton Button) noexcept
    {
        const int Index = static_cast<int>(Button);
        return (Index >= 0 && Index < PointerButtonCount) ? Index : -1;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeInputEncoder(InputEncoder& Encoder) noexcept
{
    Encoder = InputEncoder{};
}

void IntegrateInputPacket(InputEncoder& Encoder, const InputPacket& Packet) noexcept
{
    // ① On the first integration seed BOTH history slots from this packet, so a key already down at startup does not report
    //    a spurious pressed edge on the first integration. Held-duration counters start at 1 for anything already down.
    if (!Encoder.Initialized)
    {
        for (int Index = 0; Index < KeyIdentityCount; ++Index)
        {
            Encoder.PreviousKeyHeld[Index]  = Packet.KeyHeld[Index];
            Encoder.CurrentKeyHeld[Index]   = Packet.KeyHeld[Index];
            Encoder.KeyHeldDuration[Index]  = Packet.KeyHeld[Index] ? 1u : 0u;
        }
        for (int Index = 0; Index < PointerButtonCount; ++Index)
        {
            Encoder.PreviousButtonHeld[Index]  = Packet.ButtonHeld[Index];
            Encoder.CurrentButtonHeld[Index]   = Packet.ButtonHeld[Index];
            Encoder.ButtonHeldDuration[Index]  = Packet.ButtonHeld[Index] ? 1u : 0u;
        }
        Encoder.PointerPositionX = Packet.PointerPositionX;
        Encoder.PointerPositionY = Packet.PointerPositionY;
        Encoder.PointerDeltaX    = Packet.PointerDeltaX;
        Encoder.PointerDeltaY    = Packet.PointerDeltaY;
        Encoder.ScrollDelta      = Packet.ScrollDelta;
        Encoder.Initialized      = true;
        return;
    }

    // ② Roll this integration's level state into the previous, adopt the new packet's level state, and recompute held-duration
    //    counts: a still-held input increments, a released input resets to 0, a freshly-pressed input starts at 1.
    for (int Index = 0; Index < KeyIdentityCount; ++Index)
    {
        Encoder.PreviousKeyHeld[Index] = Encoder.CurrentKeyHeld[Index];
        Encoder.CurrentKeyHeld[Index]  = Packet.KeyHeld[Index];
        Encoder.KeyHeldDuration[Index] = Packet.KeyHeld[Index] ? (Encoder.KeyHeldDuration[Index] + 1u) : 0u;
    }
    for (int Index = 0; Index < PointerButtonCount; ++Index)
    {
        Encoder.PreviousButtonHeld[Index] = Encoder.CurrentButtonHeld[Index];
        Encoder.CurrentButtonHeld[Index]  = Packet.ButtonHeld[Index];
        Encoder.ButtonHeldDuration[Index] = Packet.ButtonHeld[Index] ? (Encoder.ButtonHeldDuration[Index] + 1u) : 0u;
    }

    // ③ Motion is already a per-poll accumulator on the packet — pass it through verbatim, no history needed.
    Encoder.PointerPositionX = Packet.PointerPositionX;
    Encoder.PointerPositionY = Packet.PointerPositionY;
    Encoder.PointerDeltaX    = Packet.PointerDeltaX;
    Encoder.PointerDeltaY    = Packet.PointerDeltaY;
    Encoder.ScrollDelta      = Packet.ScrollDelta;
}

bool KeyPressed(const InputEncoder& Encoder, KeyIdentity Key) noexcept
{
    const int Index = ResolveKeyIndex(Key);
    return (Index >= 0) && Encoder.CurrentKeyHeld[Index] && !Encoder.PreviousKeyHeld[Index];
}

bool KeyReleased(const InputEncoder& Encoder, KeyIdentity Key) noexcept
{
    const int Index = ResolveKeyIndex(Key);
    return (Index >= 0) && !Encoder.CurrentKeyHeld[Index] && Encoder.PreviousKeyHeld[Index];
}

bool KeyHeldNow(const InputEncoder& Encoder, KeyIdentity Key) noexcept
{
    const int Index = ResolveKeyIndex(Key);
    return (Index >= 0) && Encoder.CurrentKeyHeld[Index];
}

uint32_t KeyHeldDurationCount(const InputEncoder& Encoder, KeyIdentity Key) noexcept
{
    const int Index = ResolveKeyIndex(Key);
    return (Index >= 0) ? Encoder.KeyHeldDuration[Index] : 0u;
}

bool ButtonPressed(const InputEncoder& Encoder, PointerButton Button) noexcept
{
    const int Index = ResolveButtonIndex(Button);
    return (Index >= 0) && Encoder.CurrentButtonHeld[Index] && !Encoder.PreviousButtonHeld[Index];
}

bool ButtonReleased(const InputEncoder& Encoder, PointerButton Button) noexcept
{
    const int Index = ResolveButtonIndex(Button);
    return (Index >= 0) && !Encoder.CurrentButtonHeld[Index] && Encoder.PreviousButtonHeld[Index];
}

bool ButtonHeldNow(const InputEncoder& Encoder, PointerButton Button) noexcept
{
    const int Index = ResolveButtonIndex(Button);
    return (Index >= 0) && Encoder.CurrentButtonHeld[Index];
}

uint32_t ButtonHeldDurationCount(const InputEncoder& Encoder, PointerButton Button) noexcept
{
    const int Index = ResolveButtonIndex(Button);
    return (Index >= 0) ? Encoder.ButtonHeldDuration[Index] : 0u;
}

}   // namespace Frontier
