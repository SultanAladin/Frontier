/*==============================================================================================================================================
                                                                WORKERPOOL.CPP
==============================================================================================================================================*/
// 🧩 Fixed worker-thread pool implementation: a mutex-guarded queue, one condition variable to wake workers, per-ticket done state

#include "WorkerPool.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

//------------------------------------------------------------------------------------------------------------------------
//                                                            INTERNAL STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One queued unit paired with its ticket, so a worker can mark exactly that ticket complete when the unit returns.
struct WorkerEntry
{
    WorkerTicket Ticket = WorkerTicketNone;   // [-] - identity awaited by AwaitWorkerTask
    WorkerTask   Task;                          // [-] - the work to run (owns its own inputs / outputs)
};

// 📝 The opaque bundle behind WorkerPool. One mutex guards the pending deque, the completed-ticket set, and NextTicket; the
//    WorkAvailable variable wakes idle workers on enqueue or on shutdown; the WorkCompleted variable wakes waiters when any
//    unit finishes so AwaitWorkerTask / AwaitAllWorkerTasks re-check their predicate. Completed holds finished tickets so a
//    wait that arrives after its unit already ran returns at once; ActiveCount tracks units currently running (not just queued)
//    so AwaitAllWorkerTasks does not return while a worker is still mid-unit with an empty queue.
struct WorkerPoolImplementation
{
    std::vector<std::thread>        Workers;                       // [-] - the fixed worker threads
    std::deque<WorkerEntry>         Pending;                       // [-] - units awaiting a free worker (FIFO)
    std::unordered_set<WorkerTicket> Completed;                    // [-] - tickets whose unit has finished
    std::mutex                      Lock;                          // [-] - guards Pending, Completed, NextTicket, ActiveCount
    std::condition_variable         WorkAvailable;                 // [-] - workers wait here for a unit or shutdown
    std::condition_variable         WorkCompleted;                 // [-] - awaiters wait here for a ticket / drain
    WorkerTicket                    NextTicket    = 1;             // [-] - monotonic ticket source (0 reserved as None)
    uint32_t                        ActiveCount   = 0;             // [-] - units currently executing on a worker
    bool                            ShutdownRequested = false;     // [-] - set by Finalize to drain + exit the workers
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Recover the concrete bundle from the opaque handle. Null when the pool never initialized / is unsupported.
static WorkerPoolImplementation* ResolveImplementation(const WorkerPool& Pool)
{
    return static_cast<WorkerPoolImplementation*>(Pool.OpaqueImplementation);
}

// 📝 The loop each worker thread runs: wait for a unit (or shutdown), pull it under the lock, run it OUTSIDE the lock, then
//    record its ticket complete and wake any awaiters. ActiveCount brackets the actual run so a drain wait sees "still busy".
static void RunWorkerLoop(WorkerPoolImplementation* Implementation)
{
    for (;;)
    {
        WorkerEntry Entry;
        {
            std::unique_lock<std::mutex> Guard(Implementation->Lock);
            Implementation->WorkAvailable.wait(Guard, [Implementation]()
            {
                return Implementation->ShutdownRequested || !Implementation->Pending.empty();
            });

            if (Implementation->ShutdownRequested && Implementation->Pending.empty())
                return;

            Entry = std::move(Implementation->Pending.front());
            Implementation->Pending.pop_front();
            Implementation->ActiveCount += 1;
        }

        if (Entry.Task)
            Entry.Task();

        {
            std::unique_lock<std::mutex> Guard(Implementation->Lock);
            Implementation->ActiveCount -= 1;
            Implementation->Completed.insert(Entry.Ticket);
        }
        Implementation->WorkCompleted.notify_all();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeWorkerPool(WorkerPool& Pool, uint32_t RequestedWorkerCount)
{
    Pool.OpaqueImplementation = nullptr;
    Pool.WorkerCount          = 0;
    Pool.Supported            = false;

    // ① Resolve the worker count. 0 means derive from the hardware, reserving the main + render threads, floored at 1.
    uint32_t ResolvedCount = RequestedWorkerCount;
    if (ResolvedCount == 0)
    {
        uint32_t HardwareThreads = uint32_t(std::thread::hardware_concurrency());
        ResolvedCount = (HardwareThreads > 3) ? (HardwareThreads - 2) : 1;
    }
    if (ResolvedCount == 0)
        return false;   // 📝 unsupported → caller's Enqueue runs units inline, still correct

    WorkerPoolImplementation* Implementation = new WorkerPoolImplementation();
    Implementation->Workers.reserve(ResolvedCount);
    for (uint32_t WorkerIndex = 0; WorkerIndex < ResolvedCount; ++WorkerIndex)
        Implementation->Workers.emplace_back(RunWorkerLoop, Implementation);

    Pool.OpaqueImplementation = Implementation;
    Pool.WorkerCount          = ResolvedCount;
    Pool.Supported            = true;
    return true;
}

WorkerTicket EnqueueWorkerTask(WorkerPool& Pool, WorkerTask Task)
{
    WorkerPoolImplementation* Implementation = ResolveImplementation(Pool);

    // 📝 No pool (or shutting down): run the unit inline so a caller written against the pool still executes correctly.
    if (!Pool.Supported || Implementation == nullptr)
    {
        if (Task)
            Task();
        return WorkerTicketNone;
    }

    WorkerTicket Ticket;
    {
        std::unique_lock<std::mutex> Guard(Implementation->Lock);
        if (Implementation->ShutdownRequested)
        {
            Guard.unlock();
            if (Task)
                Task();
            return WorkerTicketNone;
        }
        Ticket = Implementation->NextTicket;
        Implementation->NextTicket += 1;
        Implementation->Pending.push_back(WorkerEntry{ Ticket, std::move(Task) });
    }
    Implementation->WorkAvailable.notify_one();
    return Ticket;
}

void AwaitWorkerTask(WorkerPool& Pool, WorkerTicket Ticket)
{
    WorkerPoolImplementation* Implementation = ResolveImplementation(Pool);
    if (!Pool.Supported || Implementation == nullptr || Ticket == WorkerTicketNone)
        return;   // 📝 inline-executed units (and the reserved None ticket) are already done

    std::unique_lock<std::mutex> Guard(Implementation->Lock);
    Implementation->WorkCompleted.wait(Guard, [Implementation, Ticket]()
    {
        return Implementation->Completed.count(Ticket) != 0;
    });
}

void AwaitAllWorkerTasks(WorkerPool& Pool)
{
    WorkerPoolImplementation* Implementation = ResolveImplementation(Pool);
    if (!Pool.Supported || Implementation == nullptr)
        return;

    std::unique_lock<std::mutex> Guard(Implementation->Lock);
    Implementation->WorkCompleted.wait(Guard, [Implementation]()
    {
        return Implementation->Pending.empty() && Implementation->ActiveCount == 0;
    });
}

void FinalizeWorkerPool(WorkerPool& Pool)
{
    WorkerPoolImplementation* Implementation = ResolveImplementation(Pool);
    if (Implementation == nullptr)
        return;

    {
        std::unique_lock<std::mutex> Guard(Implementation->Lock);
        Implementation->ShutdownRequested = true;
    }
    Implementation->WorkAvailable.notify_all();

    for (std::thread& Worker : Implementation->Workers)
    {
        if (Worker.joinable())
            Worker.join();
    }

    delete Implementation;
    Pool.OpaqueImplementation = nullptr;
    Pool.WorkerCount          = 0;
    Pool.Supported            = false;
}
