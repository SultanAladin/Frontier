/*==============================================================================================================================================
                                                                WORKERPOOL.H
==============================================================================================================================================*/
// 🧩 A fixed pool of worker threads that off-critical-path work is enqueued onto, so SDF bakes and CPU physics share threads
// 📝 The platform threading facility PLAN-RenderGroundwork §8 reserves: "so SDF-bake and CPU-physics workers share one pool
//    rather than each spawning threads." One pool is provisioned once at startup with a fixed worker count derived from the
//    hardware, and callers enqueue independent units of work (a std::function<void()> each). Two waiting styles are offered so
//    the two consumers §4.2 names both fit: EnqueueWorkerTask returns a ticket the caller can AwaitWorkerTask on (the SDF-bake
//    hand-off, which waits on a specific brick), and AwaitAllWorkerTasks drains the whole queue (a barrier for a batch bake).
//    The CPU-physics worker (§7a.2) runs its fixed-step loop as one long-lived enqueued unit that publishes double-buffered
//    and never blocks the render thread. This facility owns threads only — it knows nothing about Vulkan, geometry, or physics;
//    it is a pure Internal/Platform concern, struct + free-function style like the rest of the platform tier.
#pragma once
#ifndef FRONTIER_PLATFORM_CONCURRENCY_WORKERPOOL_H
#define FRONTIER_PLATFORM_CONCURRENCY_WORKERPOOL_H

#include <cstdint>
#include <functional>

//------------------------------------------------------------------------------------------------------------------------
//                                                            TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 One unit of work handed to the pool. Must be self-contained (own its inputs, publish its outputs) — the pool imposes no
//    ordering between units, so a unit that depends on another's result must be enqueued only after that one is awaited.
using WorkerTask = std::function<void()>;

// 📝 A ticket identifying one enqueued unit, returned by EnqueueWorkerTask and consumed by AwaitWorkerTask. Monotonic and
//    never reused within a pool's lifetime; 0 is the reserved "no task" ticket a failed / unsupported enqueue returns.
using WorkerTicket = uint64_t;

constexpr WorkerTicket WorkerTicketNone = 0;   // [-] - returned when the pool is unsupported or the enqueue was rejected

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The pool handle. The worker threads, the pending queue, the completion bookkeeping, and the shutdown flag all live
//    behind OpaqueImplementation so callers carry only a pointer, exactly like FrameTimingLedger / PolygonSurface. WorkerCount
//    is surfaced for the timing / degradation code to read without reaching into the implementation. Supported is false when
//    the pool never initialized (or the platform reports zero usable threads): every Enqueue then returns WorkerTicketNone and
//    runs the unit INLINE on the calling thread, so a caller written against the pool still executes correctly with no pool.
struct WorkerPool
{
    void*    OpaqueImplementation = nullptr;   // [-] - internal thread array + queue + completion state, opaque to callers
    uint32_t WorkerCount          = 0;         // [-] - live worker-thread count (0 when unsupported → inline execution)
    bool     Supported            = false;     // [-] - pool provisioned real threads (else Enqueue runs the unit inline)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Provision the worker threads. RequestedWorkerCount of 0 means "derive from the hardware" (hardware_concurrency minus the
// main + render threads, floored at 1). A count that resolves to 0 usable threads leaves Supported=false and every later
// enqueue runs inline — the facility still functions, just single-threaded. Call once after platform startup.
bool InitializeWorkerPool(WorkerPool& Pool, uint32_t RequestedWorkerCount);

// Enqueue one independent unit of work and return its ticket. When the pool is unsupported the unit runs INLINE immediately
// and WorkerTicketNone is returned, so the caller never branches on Supported. The unit must own its inputs and publish its
// outputs (§4.4 double-buffered hand-off) — the pool guarantees no ordering between units.
WorkerTicket EnqueueWorkerTask(WorkerPool& Pool, WorkerTask Task);

// Block the calling thread until the unit identified by Ticket has finished. Returns immediately for WorkerTicketNone (an
// inline-executed unit is already done) or a ticket already completed. This is the targeted wait the SDF-bake hand-off uses.
void AwaitWorkerTask(WorkerPool& Pool, WorkerTicket Ticket);

// Block the calling thread until every currently-enqueued unit has finished — a batch barrier (e.g. a full clipmap rebake).
// No-op when unsupported (inline units are already done). Units enqueued AFTER this call are not awaited by it.
void AwaitAllWorkerTasks(WorkerPool& Pool);

// Signal every worker to drain and exit, join the threads, and release the pool. Any not-yet-started units are dropped, so
// callers that need results must AwaitAllWorkerTasks first. Null-safe on a pool that never initialized.
void FinalizeWorkerPool(WorkerPool& Pool);

#endif
