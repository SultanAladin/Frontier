/*============================================================================================================================================
                                                     RAWINPUTROUTER.CPP
============================================================================================================================================*/
// 🧩 Implementation of deterministic fixed-capacity raw input intake and batch queueing

#include "Engine/Input/RawInputRouter.h"

namespace Frontier::Input
{

static_assert((DefaultSignalQueueCapacity & (DefaultSignalQueueCapacity - 1)) == 0,
              "DefaultSignalQueueCapacity must be a power of two for bitmask wraparound.");

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIFECYCLE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void RawInputRouter::Initialize() noexcept
{
    ReadCursor        = 0;
    WriteCursor       = 0;
    QueuedCount       = 0;
    DroppedCount      = 0;
    TotalRoutedCount  = 0;
    InitializedActive = true;
}

void RawInputRouter::Reset() noexcept
{
    ReadCursor  = 0;
    WriteCursor = 0;
    QueuedCount = 0;
}

void RawInputRouter::Finalize() noexcept
{
    Reset();
    InitializedActive = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SIGNAL INTAKE
//------------------------------------------------------------------------------------------------------------------------

bool RawInputRouter::IntakeSignal(const InputSignalPacket& Packet) noexcept
{
    if (!InitializedActive)
    {
        return false;
    }

    if (QueuedCount >= DefaultSignalQueueCapacity)
    {
        ++DroppedCount;
        return false;
    }

    SignalRing[WriteCursor] = Packet;
    WriteCursor             = (WriteCursor + 1) & (DefaultSignalQueueCapacity - 1);
    ++QueuedCount;
    return true;
}

std::size_t RawInputRouter::IntakeSignalBatch(std::span<const InputSignalPacket> Packets) noexcept
{
    std::size_t AcceptedCount = 0;
    for (const InputSignalPacket& Packet : Packets)
    {
        if (!IntakeSignal(Packet))
        {
            break;
        }
        ++AcceptedCount;
    }
    return AcceptedCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  TELEMETRY QUERIES
//------------------------------------------------------------------------------------------------------------------------

std::size_t RawInputRouter::PendingSignalCount() const noexcept
{
    return QueuedCount;
}

uint64_t RawInputRouter::DroppedSignalCount() const noexcept
{
    return DroppedCount;
}

uint64_t RawInputRouter::RoutedSignalTotal() const noexcept
{
    return TotalRoutedCount;
}

} // namespace Frontier::Input
