/*==========================================================================================================================================
                                                        PHYSICSEXECUTIONTHREAD.CPP
==========================================================================================================================================*/
// 🧩 Implementation of the dedicated background physics thread with fixed-step accumulator, substepping, and interpolated snapshots.

#include "Engine/PhysicalDynamics/PhysicsExecutionThread.h"

#include <algorithm>
#include <chrono>

namespace Frontier
{

PhysicsExecutionThread::~PhysicsExecutionThread() noexcept
{
    Finalize();
}

bool PhysicsExecutionThread::Initialize(const PhysicsThreadConfiguration& Configuration) noexcept
{
    Finalize();

    Config                        = Configuration;
    Config.OuterStepSeconds       = Vehicle::ClampScalar(Config.OuterStepSeconds, 1.0f / 480.0f, 0.1f);
    Config.SubstepCount           = std::max(1u, Config.SubstepCount);
    Config.MaximumCatchUpSteps    = std::max(1u, Config.MaximumCatchUpSteps);
    Config.WorldSettings.FixedStepSeconds = Config.OuterStepSeconds / static_cast<float>(Config.SubstepCount);

    if (!Adapter.Initialize(Config.WorldSettings))
    {
        return false;
    }

    PendingBudgetSeconds = 0.0f;
    AccumulatorSeconds   = 0.0f;
    ProcessingTick       = false;
    PendingCommands.clear();

    AtomicOuterTicks.store(0u, std::memory_order_relaxed);
    AtomicSubsteps.store(0u, std::memory_order_relaxed);
    AtomicOverruns.store(0u, std::memory_order_relaxed);
    AtomicResidualSec.store(0.0f, std::memory_order_relaxed);
    AtomicAlpha.store(0.0f, std::memory_order_relaxed);
    AtomicLatestMs.store(0.0f, std::memory_order_relaxed);
    AtomicPeakMs.store(0.0f, std::memory_order_relaxed);
    AtomicThreadHash.store(0u, std::memory_order_relaxed);
    ThreadRunning.store(false, std::memory_order_release);

    PublishSnapshotInternal(0.0f);
    InitializedCondition = true;
    return true;
}

bool PhysicsExecutionThread::ActivateThread() noexcept
{
    if (!InitializedCondition) return false;
    if (ThreadRunning.load(std::memory_order_acquire)) return true;

    ThreadRunning.store(true, std::memory_order_release);
    ExecutionThread = std::jthread([this](std::stop_token StopToken) {
        ThreadEntryLoop(StopToken);
    });
    return true;
}

void PhysicsExecutionThread::SuspendThread() noexcept
{
    if (ExecutionThread.joinable())
    {
        ExecutionThread.request_stop();
        WakeCondition.notify_all();
        ExecutionThread.join();
    }
    ThreadRunning.store(false, std::memory_order_release);
}

void PhysicsExecutionThread::Finalize() noexcept
{
    SuspendThread();
    {
        std::lock_guard<std::mutex> Lock(IngressMutex);
        PendingCommands.clear();
        PendingBudgetSeconds = 0.0f;
        AccumulatorSeconds   = 0.0f;
        ProcessingTick       = false;
    }
    {
        std::lock_guard<std::mutex> Lock(SnapshotMutex);
        PublishedSnapshot.reset();
    }
    Adapter.Finalize();
    InitializedCondition = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              COMMAND INGRESS QUEUE
//------------------------------------------------------------------------------------------------------------------------

void PhysicsExecutionThread::EnqueueCommand(std::function<void(JoltAdapter&)> Mutation)
{
    if (!Mutation) return;
    {
        std::lock_guard<std::mutex> Lock(IngressMutex);
        PendingCommands.push_back(std::move(Mutation));
    }
    WakeCondition.notify_one();
}

void PhysicsExecutionThread::EnqueueOccupantRegistration(const AdapterOccupantSpecification& Specification)
{
    EnqueueCommand([Specification](JoltAdapter& World) {
        (void)World.RegisterOccupant(Specification);
    });
}

void PhysicsExecutionThread::EnqueueVehicleRegistration(uint64_t                                   VehicleKey,
                                                        const Vehicle::VehicleSolverConfiguration& Configuration,
                                                        const Vehicle::Vec3&                       InitialPosition,
                                                        const Vehicle::Quat&                       InitialOrientation)
{
    EnqueueCommand([VehicleKey, Configuration, InitialPosition, InitialOrientation](JoltAdapter& World) {
        (void)World.RegisterVehicle(VehicleKey, Configuration, InitialPosition, InitialOrientation);
    });
}

void PhysicsExecutionThread::EnqueueDriverInput(uint64_t VehicleKey, const Vehicle::DriverCommand& Command)
{
    EnqueueCommand([VehicleKey, Command](JoltAdapter& World) {
        World.ConfigureDriverInput(VehicleKey, Command);
    });
}

void PhysicsExecutionThread::EnqueueWheelEnclosure(uint64_t VehicleKey, uint32_t WheelIndex, bool EnclosedCondition)
{
    EnqueueCommand([VehicleKey, WheelIndex, EnclosedCondition](JoltAdapter& World) {
        World.ConfigureWheelEnclosure(VehicleKey, WheelIndex, EnclosedCondition);
    });
}

void PhysicsExecutionThread::EnqueueOccupantImpulse(uint64_t OccupantKey, const Vehicle::Vec3& ImpulseNewtonSeconds)
{
    EnqueueCommand([OccupantKey, ImpulseNewtonSeconds](JoltAdapter& World) {
        World.ApplyOccupantImpulse(OccupantKey, ImpulseNewtonSeconds);
    });
}

void PhysicsExecutionThread::EnqueueKinematicTarget(uint64_t             OccupantKey,
                                                    const Vehicle::Vec3& TargetPosition,
                                                    const Vehicle::Quat& TargetOrientation)
{
    EnqueueCommand([OccupantKey, TargetPosition, TargetOrientation](JoltAdapter& World) {
        World.ConfigureKinematicTarget(OccupantKey, TargetPosition, TargetOrientation);
    });
}

void PhysicsExecutionThread::EnqueueForceZone(const AdapterForceZone& Zone)
{
    EnqueueCommand([Zone](JoltAdapter& World) {
        World.RegisterForceZone(Zone);
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                        ACCUMULATOR, SUBSTEPPING & THREAD LOOP
//------------------------------------------------------------------------------------------------------------------------

void PhysicsExecutionThread::SubmitTimeBudget(float DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;
    {
        std::lock_guard<std::mutex> Lock(IngressMutex);
        PendingBudgetSeconds += DeltaSeconds;
    }
    WakeCondition.notify_one();
}

void PhysicsExecutionThread::WaitForIdle() noexcept
{
    if (!ThreadRunning.load(std::memory_order_acquire)) return;
    std::unique_lock<std::mutex> Lock(IngressMutex);
    IdleCondition.wait(Lock, [this]() {
        return PendingCommands.empty() && PendingBudgetSeconds <= 0.0f && !ProcessingTick;
    });
}

uint32_t PhysicsExecutionThread::IntegrateSynchronous(float DeltaSeconds) noexcept
{
    if (ThreadRunning.load(std::memory_order_acquire) || !InitializedCondition) return 0u;

    std::vector<std::function<void(JoltAdapter&)>> LocalCommands;
    float                                          BudgetToConsume = std::max(0.0f, DeltaSeconds);
    {
        std::lock_guard<std::mutex> Lock(IngressMutex);
        LocalCommands.swap(PendingCommands);
        BudgetToConsume     += PendingBudgetSeconds;
        PendingBudgetSeconds = 0.0f;
    }
    return ConsumeTimeBudgetInternal(BudgetToConsume, LocalCommands);
}

uint32_t PhysicsExecutionThread::ConsumeTimeBudgetInternal(
    float                                           AddedSeconds,
    std::vector<std::function<void(JoltAdapter&)>>& ScratchCommands) noexcept
{
    // 1. Drain queued mutations before stepping so new occupants, impulses, and driver pedals apply cleanly on tick start.
    const bool HadMutations = !ScratchCommands.empty();
    for (auto& Command : ScratchCommands)
    {
        Command(Adapter);
    }
    ScratchCommands.clear();

    AccumulatorSeconds += std::max(0.0f, AddedSeconds);
    const float OuterDt = Config.OuterStepSeconds;
    const float MaxSpan = OuterDt * static_cast<float>(Config.MaximumCatchUpSteps);
    if (AccumulatorSeconds > MaxSpan)
    {
        // Clamp spiral-of-death hitch while preserving sub-frame phase continuity.
        const float Fractional = AccumulatorSeconds - std::floor(AccumulatorSeconds / OuterDt) * OuterDt;
        AccumulatorSeconds     = OuterDt * static_cast<float>(Config.MaximumCatchUpSteps) + std::min(Fractional, OuterDt * 0.999f);
        AtomicOverruns.fetch_add(1u, std::memory_order_relaxed);
    }

    uint32_t ExecutedOuterSteps = 0u;
    while (AccumulatorSeconds + 1.0e-7f >= OuterDt && ExecutedOuterSteps < Config.MaximumCatchUpSteps)
    {
        Adapter.ExecuteSubsteppedTick(OuterDt, Config.SubstepCount);
        AccumulatorSeconds = std::max(0.0f, AccumulatorSeconds - OuterDt);
        ++ExecutedOuterSteps;

        WorldPhysicsSnapshot Probe{};
        Adapter.BuildWorldSnapshot(Probe, 1.0f);
        AtomicOuterTicks.store(Probe.OuterTickIndex, std::memory_order_relaxed);
        AtomicSubsteps.store(Probe.TotalSubstepCount, std::memory_order_relaxed);
        AtomicLatestMs.store(Probe.StepWallMilliseconds, std::memory_order_relaxed);

        float PrevPeak = AtomicPeakMs.load(std::memory_order_relaxed);
        while (Probe.StepWallMilliseconds > PrevPeak &&
               !AtomicPeakMs.compare_exchange_weak(PrevPeak, Probe.StepWallMilliseconds, std::memory_order_relaxed))
        {
        }
    }

    const float Alpha = Vehicle::ClampScalar(AccumulatorSeconds / OuterDt, 0.0f, 1.0f);
    AtomicResidualSec.store(AccumulatorSeconds, std::memory_order_relaxed);
    AtomicAlpha.store(Alpha, std::memory_order_relaxed);

    if (ExecutedOuterSteps > 0u || HadMutations)
    {
        PublishSnapshotInternal(Alpha);
    }
    return ExecutedOuterSteps;
}

void PhysicsExecutionThread::PublishSnapshotInternal(float InterpolationAlpha) noexcept
{
    auto NextSnapshot = std::make_shared<WorldPhysicsSnapshot>();
    Adapter.BuildWorldSnapshot(*NextSnapshot, InterpolationAlpha);
    {
        std::lock_guard<std::mutex> Lock(SnapshotMutex);
        PublishedSnapshot = std::move(NextSnapshot);
    }
}

void PhysicsExecutionThread::ThreadEntryLoop(std::stop_token StopToken) noexcept
{
    const uint32_t IdHash = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    AtomicThreadHash.store(IdHash == 0u ? 1u : IdHash, std::memory_order_release);

    std::vector<std::function<void(JoltAdapter&)>> LocalCommands;
    auto PreviousClock = std::chrono::steady_clock::now();

    while (!StopToken.stop_requested())
    {
        float BudgetToConsume = 0.0f;
        {
            std::unique_lock<std::mutex> Lock(IngressMutex);
            if (Config.AutonomousWallClockPace)
            {
                const auto SleepSpan = std::chrono::duration<double>(Config.OuterStepSeconds * 0.5);
                WakeCondition.wait_for(Lock, StopToken, SleepSpan, [this]() {
                    return !PendingCommands.empty() || PendingBudgetSeconds > 0.0f;
                });
            }
            else
            {
                WakeCondition.wait(Lock, StopToken, [this]() {
                    return !PendingCommands.empty() || PendingBudgetSeconds > 0.0f;
                });
            }

            if (StopToken.stop_requested()) break;

            LocalCommands.swap(PendingCommands);
            BudgetToConsume      = PendingBudgetSeconds;
            PendingBudgetSeconds = 0.0f;
            ProcessingTick       = true;
        }

        if (Config.AutonomousWallClockPace)
        {
            const auto  NowClock = std::chrono::steady_clock::now();
            const float WallDt   = static_cast<float>(std::chrono::duration<double>(NowClock - PreviousClock).count());
            PreviousClock        = NowClock;
            BudgetToConsume     += std::clamp(WallDt, 0.0f, 0.1f);
        }

        (void)ConsumeTimeBudgetInternal(BudgetToConsume, LocalCommands);

        {
            std::lock_guard<std::mutex> Lock(IngressMutex);
            ProcessingTick = false;
            if (PendingCommands.empty() && PendingBudgetSeconds <= 0.0f)
            {
                IdleCondition.notify_all();
            }
        }
    }

    ThreadRunning.store(false, std::memory_order_release);
    IdleCondition.notify_all();
}

void PhysicsExecutionThread::AcquireInterpolatedSnapshot(WorldPhysicsSnapshot& OutSnapshot) const
{
    std::shared_ptr<const WorldPhysicsSnapshot> CurrentPtr;
    {
        std::lock_guard<std::mutex> Lock(SnapshotMutex);
        CurrentPtr = PublishedSnapshot;
    }

    if (!CurrentPtr)
    {
        OutSnapshot = WorldPhysicsSnapshot{};
        return;
    }

    OutSnapshot       = *CurrentPtr;
    const float Alpha = AtomicAlpha.load(std::memory_order_acquire);
    JoltAdapter::InterpolateWorldSnapshot(OutSnapshot, Alpha);
}

PhysicsThreadDiagnostics PhysicsExecutionThread::QueryDiagnostics() const noexcept
{
    PhysicsThreadDiagnostics Out{};
    Out.CompletedOuterTicks    = AtomicOuterTicks.load(std::memory_order_relaxed);
    Out.CompletedSubsteps      = AtomicSubsteps.load(std::memory_order_relaxed);
    Out.ClampedOverrunEvents   = AtomicOverruns.load(std::memory_order_relaxed);
    Out.ResidualAccumulatorSec = AtomicResidualSec.load(std::memory_order_relaxed);
    Out.InterpolationFraction  = AtomicAlpha.load(std::memory_order_relaxed);
    Out.LatestStepMilliseconds = AtomicLatestMs.load(std::memory_order_relaxed);
    Out.PeakStepMilliseconds   = AtomicPeakMs.load(std::memory_order_relaxed);
    Out.ThreadIdentifierHash   = AtomicThreadHash.load(std::memory_order_acquire);
    Out.ThreadActiveCondition  = ThreadRunning.load(std::memory_order_acquire);
    return Out;
}

} // namespace Frontier
