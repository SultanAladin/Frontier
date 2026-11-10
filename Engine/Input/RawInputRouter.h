/*============================================================================================================================================
                                                      RAWINPUTROUTER.H
============================================================================================================================================*/
// 🧩 Cross-platform OS and mobile hardware signal ring queue and concept-constrained input router

#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Frontier::Input
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr std::size_t DefaultSignalQueueCapacity = 1024; // [count] - Power-of-two ring slot count per tick
inline constexpr uint32_t    MaximumScanCodeCount       = 512;  // [count] - Bounded hardware keyboard scancode ceiling
inline constexpr uint32_t    MaximumTouchContactSlots   = 16;   // [count] - Simultaneous multi-touch contact ceiling

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class InputSignalCategory : uint8_t
{
    KeyboardKey   = 0, // [-] - Physical scancode press, repeat, or release
    PointerMotion = 1, // [-] - Desktop cursor or relative motion delta
    PointerButton = 2, // [-] - Mouse or stylus button transition
    ScrollWheel   = 3, // [-] - Precision two-axis wheel or trackpad scroll
    TouchContact  = 4, // [-] - Android / touchscreen multi-touch contact sample
    GamepadAxis   = 5, // [-] - Normalized analog stick or trigger deflection
    GamepadButton = 6, // [-] - Digital gamepad button transition
};

enum class ButtonTransitionCategory : uint8_t
{
    Released = 0, // [-] - Contact or switch opened
    Pressed  = 1, // [-] - Initial actuation edge
    Repeated = 2, // [-] - OS auto-repeat pulse while held
};

enum class TouchContactCategory : uint8_t
{
    Began      = 0, // [-] - Finger or stylus touched surface
    Moved      = 1, // [-] - Active contact translated on surface
    Stationary = 2, // [-] - Contact held without translation
    Ended      = 3, // [-] - Finger or stylus lifted cleanly
    Cancelled  = 4, // [-] - Gesture interrupted by OS surface loss
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) InputSignalPacket
{
    uint64_t                 TimestampNanoseconds = 0;                                // [ns]  - Monotonic hardware sample timestamp
    float                    PrimaryAxis          = 0.0f;                             // [px]  - Cursor X, scroll X, touch X, or axis deflection
    float                    SecondaryAxis        = 0.0f;                             // [px]  - Cursor Y, scroll Y, touch Y, or secondary deflection
    float                    DeltaPrimary         = 0.0f;                             // [px]  - Relative X motion or contact pressure [0-1]
    float                    DeltaSecondary       = 0.0f;                             // [px]  - Relative Y motion or contact radius [px]
    uint32_t                 DeviceIdentifier     = 0;                                // [idx] - Originating peripheral or touch digitizer ID
    uint32_t                 ControlCode          = 0;                                // [idx] - Scancode, button index, axis index, or finger ID
    uint16_t                 ModifierBitmask      = 0;                                // [mask]- Shift/Ctrl/Alt/Super bitfield
    InputSignalCategory      Category             = InputSignalCategory::KeyboardKey; // [-]   - Discriminator for packet payload
    ButtonTransitionCategory Transition           = ButtonTransitionCategory::Released; // [-] - Digital switch transition
    TouchContactCategory     TouchPhase           = TouchContactCategory::Began;      // [-]   - Multi-touch lifecycle phase
    uint8_t                  ReservedPadding[3]   = { 0, 0, 0 };                      // [B]   - Explicit alignment padding to 48 bytes
};

static_assert(sizeof(InputSignalPacket) == 48, "InputSignalPacket size must remain 48 bytes across all target architectures.");
static_assert(alignof(InputSignalPacket) == 16, "InputSignalPacket alignment must remain 16 bytes.");

//------------------------------------------------------------------------------------------------------------------------
//                                                      CONCEPTS
//------------------------------------------------------------------------------------------------------------------------

template <typename ReceiverType>
concept InputSignalSink = requires(ReceiverType& Receiver, const InputSignalPacket& Packet)
{
    { Receiver.AcceptInputSignal(Packet) } noexcept -> std::same_as<void>;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  ROUTER STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

class RawInputRouter
{
public:
    RawInputRouter() noexcept = default;

    //--------------------------------------------------------------------------------------------------------------------
    //                                                FUNCTION PROTOTYPES
    //--------------------------------------------------------------------------------------------------------------------

    void Initialize() noexcept;
    void Reset() noexcept;
    void Finalize() noexcept;

    [[nodiscard]] bool        IntakeSignal(const InputSignalPacket& Packet) noexcept;
    [[nodiscard]] std::size_t IntakeSignalBatch(std::span<const InputSignalPacket> Packets) noexcept;

    [[nodiscard]] std::size_t PendingSignalCount() const noexcept;
    [[nodiscard]] uint64_t    DroppedSignalCount() const noexcept;
    [[nodiscard]] uint64_t    RoutedSignalTotal()  const noexcept;

    template <InputSignalSink SinkType>
    std::size_t RouteQueuedSignals(SinkType&& DestinationSink) noexcept;

private:
    std::array<InputSignalPacket, DefaultSignalQueueCapacity> SignalRing{};          // [-]     - Contiguous fixed-capacity ring storage
    std::size_t                                               ReadCursor        = 0; // [idx]   - Next ring slot to drain
    std::size_t                                               WriteCursor       = 0; // [idx]   - Next ring slot to populate
    std::size_t                                               QueuedCount       = 0; // [count] - Unrouted signals currently resident
    uint64_t                                                  DroppedCount      = 0; // [count] - Signals rejected due to full ring saturation
    uint64_t                                                  TotalRoutedCount  = 0; // [count] - Cumulative signals routed since Initialize
    bool                                                      InitializedActive = false; // [-] - Lifecycle activation guard
};

//------------------------------------------------------------------------------------------------------------------------
//                                              TEMPLATE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

template <InputSignalSink SinkType>
std::size_t RawInputRouter::RouteQueuedSignals(SinkType&& DestinationSink) noexcept
{
    if (!InitializedActive || QueuedCount == 0)
    {
        return 0;
    }

    const std::size_t ToDrainCount = QueuedCount;
    for (std::size_t StepIndex = 0; StepIndex < ToDrainCount; ++StepIndex)
    {
        DestinationSink.AcceptInputSignal(SignalRing[ReadCursor]);
        ReadCursor = (ReadCursor + 1) & (DefaultSignalQueueCapacity - 1);
    }

    QueuedCount       = 0;
    TotalRoutedCount += ToDrainCount;
    return ToDrainCount;
}

} // namespace Frontier::Input
