/*==========================================================================================================================================
                                                             PHYSICSTHREAD.CPP
==========================================================================================================================================*/
// 🧩 The fixed-rate loop: accrue real time, run whole steps under the catch-up cap, publish once per wake, sleep out the rest.

#include "Frontier/Extensions/Physics/PhysicsThread.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Frontier
{

using SteadyClock = std::chrono::steady_clock;

bool PhysicsThread::Start(const PhysicsThreadConfiguration& Configuration, StepCallback OnStep, WakeCallback OnWake)
{
    if (Running.load(std::memory_order_acquire) || !OnStep || !(Configuration.StepHz > 0.0)) return false;
    Configuration_                     = Configuration;
    Configuration_.MaximumCatchUpSteps = std::max(1u, Configuration_.MaximumCatchUpSteps);
    OnStep_                            = std::move(OnStep);
    OnWake_                            = std::move(OnWake);
    {
        std::lock_guard<std::mutex> Guard(MeasurementLock);
        Measurements         = {};
        Measurements.Running = true;
    }
    StopRequested.store(false, std::memory_order_release);
    Running.store(true, std::memory_order_release);
    Stepping = std::thread([this] { Loop(); });
    return true;
}

void PhysicsThread::Stop()
{
    StopRequested.store(true, std::memory_order_release);
    if (Stepping.joinable()) Stepping.join();
    Running.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> Guard(MeasurementLock);
    Measurements.Running = false;
}

PhysicsThreadMeasurements PhysicsThread::QueryMeasurements() const
{
    std::lock_guard<std::mutex> Guard(MeasurementLock);
    return Measurements;
}

void PhysicsThread::Loop()
{
    const double StepSeconds = 1.0 / Configuration_.StepHz;
    const auto   Started     = SteadyClock::now();
    auto         Previous    = Started;
    double       Owed        = 0.0;   // [s] real time not yet simulated
    uint64_t     Step        = 0;
    double       StepSum     = 0.0;   // [µs]

    while (!StopRequested.load(std::memory_order_acquire))
    {
        const auto Now = SteadyClock::now();
        Owed += std::chrono::duration<double>(Now - Previous).count();
        Previous = Now;

        uint32_t Ran         = 0;
        double   LastMicros  = 0.0;
        while (Owed >= StepSeconds && Ran < Configuration_.MaximumCatchUpSteps && !StopRequested.load(std::memory_order_acquire))
        {
            const auto StepStart = SteadyClock::now();
            OnStep_(Step, StepSeconds);
            LastMicros = std::chrono::duration<double, std::micro>(SteadyClock::now() - StepStart).count();
            StepSum += LastMicros;
            Owed -= StepSeconds;
            ++Step;
            ++Ran;
        }
        // 📝 A backlog beyond the cap is time the simulation will never make up: count it and resynchronise.
        uint64_t Dropped = 0;
        if (Owed >= StepSeconds)
        {
            Dropped = static_cast<uint64_t>(Owed / StepSeconds);
            Owed    = std::fmod(Owed, StepSeconds);
        }
        if (Ran > 0 && OnWake_) OnWake_(Step);
        if (Ran > 0 || Dropped > 0)
        {
            const double RealSeconds = std::chrono::duration<double>(Now - Started).count();
            std::lock_guard<std::mutex> Guard(MeasurementLock);
            Measurements.Steps                = Step;
            Measurements.StepsLastWake        = Ran;
            Measurements.DroppedSteps        += Dropped;
            Measurements.Wakes               += 1;
            if (Ran > 0) Measurements.LastStepMicroseconds = LastMicros;
            Measurements.MeanStepMicroseconds = Step > 0 ? StepSum / static_cast<double>(Step) : 0.0;
            Measurements.RealtimeRatio        = RealSeconds > 0.0 ? static_cast<double>(Step) * StepSeconds / RealSeconds : 1.0;
        }
        if (Configuration_.Spin)
        {
            std::this_thread::yield();
            continue;
        }
        // 📝 Sleep a little short of the next step to absorb scheduler lateness; the next accrual catches the rest.
        const double Remaining = StepSeconds - Owed;
        if (Remaining > 0.0)
            std::this_thread::sleep_for(std::chrono::duration_cast<SteadyClock::duration>(std::chrono::duration<double>(Remaining * 0.9)));
    }
}

} // namespace Frontier
