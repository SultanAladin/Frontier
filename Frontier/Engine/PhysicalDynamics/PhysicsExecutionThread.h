/*==========================================================================================================================================
                                                         PHYSICSEXECUTIONTHREAD.H
==========================================================================================================================================*/
// 🧩 Dedicated C++20 background physics thread with fixed-step accumulator, N-way substepping, command ingress, and interpolated snapshots.

#pragma once

#include "Engine/PhysicalDynamics/JoltAdapter.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                              PHYSICS THREAD CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct PhysicsThreadConfiguration
{
    RigidBodyWorldSettings WorldSettings           {};                  // [-] Jolt broadphase, gravity, worker pool
    float                  OuterStepSeconds        = 1.0f / 60.0f;      // [s] outer fixed tick interval (60 Hz default)
    uint32_t               SubstepCount            = 4u;                // [-] inner substeps per outer tick (4 x 60 = 240 Hz)
    uint32_t               MaximumCatchUpSteps     = 6u;                // [-] spiral-of-death cap per wakeup
    bool                   AutonomousWallClockPace = false;             // [-] true = self-paced steady_clock loop; false = SubmitTimeBudget
};

struct PhysicsThreadDiagnostics
{
    uint64_t CompletedOuterTicks      = 0u;     // [-] total outer ticks completed by thread
    uint64_t CompletedSubsteps        = 0u;     // [-] total inner substeps completed by thread
    uint64_t ClampedOverrunEvents     = 0u;     // [-] number of times spiral-of-death cap clamped budget
    float    ResidualAccumulatorSec   = 0.0f;   // [s] unconsumed remainder inside [0, OuterStepSeconds)
    float    InterpolationFraction    = 0.0f;   // [-] ResidualAccumulatorSec / OuterStepSeconds in [0..1]
    float    LatestStepMilliseconds   = 0.0f;   // [ms] wall-clock duration of latest outer tick
    float    PeakStepMilliseconds     = 0.0f;   // [ms] maximum outer tick duration observed
    uint32_t ThreadIdentifierHash     = 0u;     // [-] std::hash<std::thread::id> of background thread
    bool     ThreadActiveCondition    = false;  // [-] true while background std::jthread is running
};

//------------------------------------------------------------------------------------------------------------------------
//                                              PHYSICS EXECUTION THREAD
//------------------------------------------------------------------------------------------------------------------------

class PhysicsExecutionThread
{
  public:
    PhysicsExecutionThread() noexcept = default;
    ~PhysicsExecutionThread() noexcept;

    PhysicsExecutionThread(const PhysicsExecutionThread&)            = delete;
    PhysicsExecutionThread& operator=(const PhysicsExecutionThread&) = delete;

    //--------------------------------------------------------------------------------------------------------------------
    //   LIFECYCLE (initialize -> activate -> loop -> finalize)
    //--------------------------------------------------------------------------------------------------------------------
    bool Initialize(const PhysicsThreadConfiguration& Configuration = {}) noexcept;
    bool ActivateThread() noexcept;
    void SuspendThread() noexcept;
    void Finalize() noexcept;

    [[nodiscard]] bool ThreadActive() const noexcept { return ThreadRunning.load(std::memory_order_acquire); }

    //--------------------------------------------------------------------------------------------------------------------
    //   SYNCHRONOUS SETUP (WHEN THREAD IS SUSPENDED) & THREAD-SAFE COMMAND INGRESS (WHILE RUNNING)
    //--------------------------------------------------------------------------------------------------------------------
    [[nodiscard]] JoltAdapter& AccessAdapterWhenSuspended() noexcept { return Adapter; }

    void EnqueueCommand(std::function<void(JoltAdapter&)> Mutation);
    void EnqueueOccupantRegistration(const AdapterOccupantSpecification& Specification);
    void EnqueueVehicleRegistration(uint64_t                                   VehicleKey,
                                    const Vehicle::VehicleSolverConfiguration& Configuration,
                                    const Vehicle::Vec3&                       InitialPosition,
                                    const Vehicle::Quat&                       InitialOrientation = Vehicle::Quat::Identity());
    void EnqueueDriverInput(uint64_t VehicleKey, const Vehicle::DriverCommand& Command);
    void EnqueueWheelEnclosure(uint64_t VehicleKey, uint32_t WheelIndex, bool EnclosedCondition);
    void EnqueueOccupantImpulse(uint64_t OccupantKey, const Vehicle::Vec3& ImpulseNewtonSeconds);
    void EnqueueKinematicTarget(uint64_t             OccupantKey,
                                const Vehicle::Vec3& TargetPosition,
                                const Vehicle::Quat& TargetOrientation);
    void EnqueueForceZone(const AdapterForceZone& Zone);

    //--------------------------------------------------------------------------------------------------------------------
    //   TIME BUDGET SUBMISSION & SYNCHRONIZATION
    //--------------------------------------------------------------------------------------------------------------------
    // 📝 Submits frame delta seconds to the dedicated background physics thread (non-blocking wakeup).
    void SubmitTimeBudget(float DeltaSeconds) noexcept;

    // 📝 Waits until the background physics thread has drained all pending commands and consumed all full OuterSteps.
    void WaitForIdle() noexcept;

    // 📝 Synchronous fallback step when running single-threaded (ThreadActive() == false).
    uint32_t IntegrateSynchronous(float DeltaSeconds) noexcept;

    //--------------------------------------------------------------------------------------------------------------------
    //   LOCK-FREE / NON-STALLING INTERPOLATED SNAPSHOT READOUT
    //--------------------------------------------------------------------------------------------------------------------
    void                                 AcquireInterpolatedSnapshot(WorldPhysicsSnapshot& OutSnapshot) const;
    [[nodiscard]] PhysicsThreadDiagnostics QueryDiagnostics() const noexcept;

  private:
    void ThreadEntryLoop(std::stop_token StopToken) noexcept;
    uint32_t ConsumeTimeBudgetInternal(float AddedSeconds, std::vector<std::function<void(JoltAdapter&)>>& ScratchCommands) noexcept;
    void PublishSnapshotInternal(float InterpolationAlpha) noexcept;

    PhysicsThreadConfiguration                        Config{};
    JoltAdapter                                       Adapter{};

    mutable std::mutex                                IngressMutex{};
    std::condition_variable_any                       WakeCondition{};
    std::condition_variable                           IdleCondition{};
    std::vector<std::function<void(JoltAdapter&)>>    PendingCommands{};
    float                                             PendingBudgetSeconds = 0.0f;
    float                                             AccumulatorSeconds   = 0.0f;
    bool                                              ProcessingTick       = false;

    mutable std::mutex                                SnapshotMutex{};
    std::shared_ptr<const WorldPhysicsSnapshot>       PublishedSnapshot{};

    std::atomic<uint64_t>                             AtomicOuterTicks{0u};
    std::atomic<uint64_t>                             AtomicSubsteps{0u};
    std::atomic<uint64_t>                             AtomicOverruns{0u};
    std::atomic<float>                                AtomicResidualSec{0.0f};
    std::atomic<float>                                AtomicAlpha{0.0f};
    std::atomic<float>                                AtomicLatestMs{0.0f};
    std::atomic<float>                                AtomicPeakMs{0.0f};
    std::atomic<uint32_t>                             AtomicThreadHash{0u};
    std::atomic<bool>                                 ThreadRunning{false};
    bool                                              InitializedCondition = false;

    std::jthread                                      ExecutionThread{};
};

} // namespace Frontier
