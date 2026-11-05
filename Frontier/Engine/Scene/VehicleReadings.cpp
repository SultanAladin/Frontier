/*==========================================================================================================================================
                                                           VEHICLEREADINGS.CPP
==========================================================================================================================================*/
// 🧩 One reading per vehicle record behind a lock (published on the editor's thread, read on the same thread or a validation's).

#include "Engine/Scene/VehicleReadings.h"

#include <mutex>
#include <unordered_map>

namespace Frontier
{

namespace
{

std::mutex& ReadingLock()
{
    static std::mutex Lock;
    return Lock;
}

std::unordered_map<uint64_t, VehicleReading>& Readings()
{
    static std::unordered_map<uint64_t, VehicleReading> Store;
    return Store;
}

} // namespace

void PublishVehicleReading(RecordToken Vehicle, const VehicleReading& Reading)
{
    std::lock_guard<std::mutex> Guard(ReadingLock());
    Readings()[Vehicle.Value] = Reading;
}

bool QueryVehicleReading(RecordToken Vehicle, VehicleReading& Reading)
{
    std::lock_guard<std::mutex> Guard(ReadingLock());
    const auto Found = Readings().find(Vehicle.Value);
    if (Found == Readings().end()) return false;
    Reading = Found->second;
    return true;
}

void ClearVehicleReadings()
{
    std::lock_guard<std::mutex> Guard(ReadingLock());
    Readings().clear();
}

} // namespace Frontier
