#pragma once
/*==========================================================================================================================================
                                                               INPUTPACKET.H
==========================================================================================================================================*/
// 🧩 Normalized per-step control state produced by InputEncoder and consumed by DeployableUnit and SessionSpace.

#include <cstdint>

namespace Frontier
{

enum InputActionBit : uint32_t
{
    InputActionNone        = 0u,
    InputActionHandbrake   = 1u << 0,
    InputActionReverse     = 1u << 1,
    InputActionBoost       = 1u << 2,
    InputActionReady       = 1u << 3,
    InputActionResetUnit   = 1u << 4,
    InputActionCycleCamera = 1u << 5,
    InputActionLights      = 1u << 6
};

// 📝 Value packet representing one player's control state for a single simulation step.
struct InputPacket
{
    uint64_t SequenceIndex  = 0;      // [-]     monotonic step counter
    float    ThrottleAxis   = 0.0f;   // [0..1]  forward drive demand
    float    BrakeAxis      = 0.0f;   // [0..1]  service brake demand
    float    SteerAxis      = 0.0f;   // [-1..1] left (-1) to right (+1) steering demand
    float    HandbrakeAxis  = 0.0f;   // [0..1]  rear mechanical lock demand
    float    LookYawDelta   = 0.0f;   // [rad]   camera yaw increment
    float    LookPitchDelta = 0.0f;   // [rad]   camera pitch increment
    uint32_t ActionBits     = 0u;     // [-]     bitmask of InputActionBit flags

    constexpr bool HasAction(InputActionBit Bit) const noexcept
    {
        return (ActionBits & static_cast<uint32_t>(Bit)) != 0u;
    }
};

} // namespace Frontier
