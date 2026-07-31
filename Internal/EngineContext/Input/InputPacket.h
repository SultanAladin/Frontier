/*==============================================================================================================================================
                                                                INPUTPACKET.H
==============================================================================================================================================*/
// 🧩 One poll of decoded device input, filled by the platform window each poll and read by the camera / UI. Deliberately tiny and portable:
//    no windowing-library types, no OS types, just a POD of held keys, held mouse buttons, this-poll pointer delta, and this-poll scroll.
//    The pointer delta is a plain cursor-position difference (the cursor stays VISIBLE — no capture, no hide, no acceleration tricks): the
//    same "absolute-position difference" an immediate-mode UI consumes, sourced identically on every platform. Motion the OS delivers at a
//    higher rate than the poll (Win32 WM_INPUT, X11 XI2 raw, Wayland relative-pointer) is summed into PointerDelta between polls, so a slow
//    drag accumulates smoothly rather than collapsing to one rounded reading. Fixed-size arrays → zero per-poll allocation.

#pragma once
#ifndef FRONTIER_INPUT_INPUTPACKET_H
#define FRONTIER_INPUT_INPUTPACKET_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A portable key identity independent of any windowing library. Each platform backend translates its native virtual-key /
//    keysym / scancode into one of these before writing KeyHeld, so nothing above the platform layer references an OS code.
//    Only the keys the editor actually reads are enumerated (the fly camera + a fullscreen toggle); the set widens as bindings
//    grow. KeyCount is the array bound — always last.
enum class KeyIdentity : uint8_t
{
    Unknown = 0,

    // Letter keys the fly camera reads.
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // Modifiers.
    LeftShift, RightShift,
    LeftControl, RightControl,
    LeftAlt, RightAlt,

    // Function keys (F11 = fullscreen toggle).
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // Numpad (debug view toggles).
    Numpad0, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7,

    // Miscellaneous.
    Space, Escape,

    KeyCount
};

// 📝 A portable pointer button identity. Left / Right / Middle cover the DCC gesture set (Right = look-engage, Middle = pan);
//    the two extended buttons round out a common mouse. ButtonCount is the array bound.
enum class PointerButton : uint8_t
{
    Left = 0,
    Right,
    Middle,
    Extended1,
    Extended2,

    ButtonCount
};


//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr int KeyIdentityCount  = static_cast<int>(KeyIdentity::KeyCount);      // [-] - Held-key array bound
constexpr int PointerButtonCount = static_cast<int>(PointerButton::ButtonCount); // [-] - Held-button array bound


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The decoded input for one poll. KeyHeld / ButtonHeld are level state (true while down); PointerDelta / ScrollDelta are
//    accumulators the platform poll fills since the previous poll and the reader consumes each poll. PointerPosition is the
//    absolute cursor position in framebuffer pixels for UI hit-testing. Everything is written by the platform window; readers
//    treat it as immutable for the poll.
struct InputPacket
{
    bool   KeyHeld[KeyIdentityCount]       = {};   // [-]  - True while the key is down, indexed by KeyIdentity
    bool   ButtonHeld[PointerButtonCount]  = {};   // [-]  - True while the button is down, indexed by PointerButton

    double PointerPositionX = 0.0;                 // [px] - Absolute cursor X in framebuffer space
    double PointerPositionY = 0.0;                 // [px] - Absolute cursor Y in framebuffer space
    double PointerDeltaX    = 0.0;                 // [px] - Cursor X motion summed since the previous poll
    double PointerDeltaY    = 0.0;                 // [px] - Cursor Y motion summed since the previous poll
    double ScrollDelta      = 0.0;                 // [-]  - Vertical wheel notches summed since the previous poll
};


//------------------------------------------------------------------------------------------------------------------------
//                                                         INLINE ACCESSORS
//------------------------------------------------------------------------------------------------------------------------

// Level read of one key from the packet. Out-of-range identities read false so a caller never indexes past the array.
inline bool PacketKeyHeld(const InputPacket& Packet, KeyIdentity Key)
{
    const int Index = static_cast<int>(Key);
    return (Index >= 0 && Index < KeyIdentityCount) ? Packet.KeyHeld[Index] : false;
}

// Level read of one pointer button from the packet. Out-of-range buttons read false.
inline bool PacketButtonHeld(const InputPacket& Packet, PointerButton Button)
{
    const int Index = static_cast<int>(Button);
    return (Index >= 0 && Index < PointerButtonCount) ? Packet.ButtonHeld[Index] : false;
}

}   // namespace Frontier

#endif
