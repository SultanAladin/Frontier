/*==========================================================================================================================================
                                                              PHYSICSTHREAD.H
==========================================================================================================================================*/
// 🧩 The physics thread: a dedicated thread that steps the simulation at a fixed rate (240 Hz for vehicles) whatever the editor's
//    refresh rate, runs whole fixed steps for the real time that has passed (a catch-up cap stops a hitch from snowballing), and
//    publishes a snapshot once per wake. Inputs travel the other way through a mailbox. Ported from Project Drive's
//    VehiclePhysicsThread (SultanAladin/Frontier- 3f14808), which mirrored GRIT's Chaos physics-thread callback.
//
//    Thread contract: Start, Stop and QueryMeasurements belong to the caller's thread; the step and wake callbacks run on the
//    physics thread and touch only what it owns plus the mailbox and the snapshot exchange.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAILBOX
//------------------------------------------------------------------------------------------------------------------------

// 📝 One slot handed from one thread to another: Write replaces it, Read takes it (empties the slot), Peek copies it. Meant for
//    small inputs written once per refresh (driver controls, an edited specification).
template <typename Contents>
class PhysicsMailbox
{
  public:
    void Write(const Contents& Incoming)
    {
        std::lock_guard<std::mutex> Guard(Lock);
        Slot = Incoming;
        Full = true;
    }

    [[nodiscard]] bool Read(Contents& Outgoing)
    {
        std::lock_guard<std::mutex> Guard(Lock);
        if (!Full) return false;
        Outgoing = Slot;
        Full     = false;
        return true;
    }

    [[nodiscard]] bool Peek(Contents& Outgoing) const
    {
        std::lock_guard<std::mutex> Guard(Lock);
        if (!Full) return false;
        Outgoing = Slot;
        return true;
    }

  private:
    mutable std::mutex Lock;
    Contents           Slot{};
    bool               Full = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                SNAPSHOT EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Three slots: the physics thread fills its own, Publish hands it over, Acquire takes the newest on the reading side. Neither
//    side waits for the other or copies a snapshot; only slot indices change hands under the lock. The producer rewrites its whole
//    slot before each Publish (slots rotate, so it holds an older snapshot until then).
template <typename Snapshot>
class PhysicsSnapshotExchange
{
  public:
    [[nodiscard]] Snapshot& AccessProduced() noexcept { return Slots[Produced]; }

    void Publish()
    {
        std::lock_guard<std::mutex> Guard(Lock);
        std::swap(Produced, Ready);
        Fresh = true;
        ++Published;
    }

    // 📝 True when a newer snapshot than the last acquired one was taken.
    bool Acquire()
    {
        std::lock_guard<std::mutex> Guard(Lock);
        if (!Fresh) return false;
        std::swap(Consumed, Ready);
        Fresh = false;
        return true;
    }

    [[nodiscard]] const Snapshot& QueryConsumed() const noexcept { return Slots[Consumed]; }
    [[nodiscard]] uint64_t        QueryPublished() const
    {
        std::lock_guard<std::mutex> Guard(Lock);
        return Published;
    }

  private:
    std::array<Snapshot, 3> Slots{};
    mutable std::mutex      Lock;
    int                     Produced  = 0;
    int                     Ready     = 1;
    int                     Consumed  = 2;
    bool                    Fresh     = false;
    uint64_t                Published = 0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 PHYSICS THREAD
//------------------------------------------------------------------------------------------------------------------------

struct PhysicsThreadConfiguration
{
    double   StepHz              = 240.0;   // [Hz] fixed step rate; each step callback receives 1 / StepHz
    uint32_t MaximumCatchUpSteps = 8;       // [-]  steps one wake may run before the rest of the backlog is dropped
    bool     Spin                = false;   // [-]  true yields instead of sleeping between steps (tighter cadence, a busy core)
};

struct PhysicsThreadMeasurements
{
    uint64_t Steps               = 0;       // [-]  fixed steps run since Start
    uint32_t StepsLastWake       = 0;       // [-]
    uint64_t DroppedSteps        = 0;       // [-]  steps given up by the catch-up cap (hitches)
    uint64_t Wakes               = 0;       // [-]
    double   LastStepMicroseconds = 0.0;    // [µs] wall time of the last step callback
    double   MeanStepMicroseconds = 0.0;    // [µs] running mean
    double   RealtimeRatio       = 1.0;     // [-]  simulated ÷ real elapsed time (1 keeps pace; below 1 falls behind)
    bool     Running             = false;
};

class PhysicsThread
{
  public:
    using StepCallback = std::function<void(uint64_t Step, double Seconds)>;
    using WakeCallback = std::function<void(uint64_t Step)>;

    PhysicsThread() = default;
    ~PhysicsThread() { Stop(); }

    PhysicsThread(const PhysicsThread&)            = delete;
    PhysicsThread& operator=(const PhysicsThread&) = delete;

    // 📝 Starts stepping. OnWake (optional) runs after each wake's steps, e.g. to publish a snapshot. False when already running,
    //    without a step callback, or with a rate that is not positive.
    bool Start(const PhysicsThreadConfiguration& Configuration, StepCallback OnStep, WakeCallback OnWake = {});
    // 📝 Finishes the step in progress and joins the thread; safe to call twice and from the destructor.
    void Stop();

    [[nodiscard]] bool                      QueryRunning() const noexcept { return Running.load(std::memory_order_acquire); }
    [[nodiscard]] PhysicsThreadMeasurements QueryMeasurements() const;
    [[nodiscard]] double                    QueryStepSeconds() const noexcept { return 1.0 / Configuration_.StepHz; }

  private:
    void Loop();

    PhysicsThreadConfiguration Configuration_{};
    StepCallback               OnStep_{};
    WakeCallback               OnWake_{};
    std::thread                Stepping{};
    std::atomic<bool>          Running{ false };
    std::atomic<bool>          StopRequested{ false };
    mutable std::mutex         MeasurementLock;
    PhysicsThreadMeasurements  Measurements{};
};

} // namespace Frontier
