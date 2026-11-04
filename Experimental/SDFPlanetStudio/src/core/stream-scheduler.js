/**
 * Streaming scheduler: turns "these pages are on screen" into as few, as
 * sequential, as well-ordered disk reads as the budget allows.
 *
 * The HDD rules this implements
 * ----------------------------
 *  1. Never read per tile. Wants are grouped by page (160 KiB), so one syscall
 *     fetches sixteen tiles.
 *  2. Sort by direction of travel, not by "whatever asked first". Requests are
 *     ordered by page id, which is spatially monotone, so a frame's reads are
 *     consecutive and the drive's own readahead can work with us.
 *  3. Prefetch along the flight path, not into a sphere. Read-ahead pages are the
 *     ones the camera is about to reach (next pages in sorted order), and they are
 *     dropped the moment the camera turns away.
 *  4. Keep the queue shallow. At most `maxInFlight` reads are ever outstanding,
 *     because a deep queue on a single spindle only reorders seeks harmlessly at
 *     best, and at worst thrashes.
 *  5. Budget bytes per tick. `bytesPerTick` bounds how much the stream can steal
 *     from a frame; the caller can lower it when frame time rises.
 *  6. Cancel before starting. A want that disappears between frames is never
 *     issued — the cheapest possible IO.
 *
 * The scheduler is deliberately transport-agnostic: it only sees `pageId`s and a
 * `TileStore`. Browser builds plug in an IndexedDB/OPFS or HTTP-range IO with the
 * same page contract.
 */

import { PAGE_BYTES } from './tile-store.js';

export const DEFAULT_SCHEDULER_OPTIONS = Object.freeze({
  bytesPerTick: 512 * 1024,   // 4 pages per pump
  maxInFlight: 2,
  readaheadPages: 2,
  prefetchPriorityBoost: 0,
});

export class StreamScheduler {
  constructor({ store, cache, options = {} } = {}) {
    if (!store) throw new Error('StreamScheduler needs a store');
    this.store = store;
    this.cache = cache ?? null;
    this.options = { ...DEFAULT_SCHEDULER_OPTIONS, ...options };

    this.wants = new Map(); // pageId -> priority (lower is more urgent)
    this.inFlight = new Set();
    this.queue = []; // ordered pageIds awaiting issue
    this.failed = new Set();
    this.stats = {
      ticks: 0,
      issues: 0,
      bytes: 0,
      cancelled: 0,
      hits: 0,
      misses: 0,
      sequentialRuns: 0,
      readsByPriority: 0,
      prefetches: 0,
      ioMs: 0,
      lastBatch: [],
    };
    this.previousIssued = null;
  }

  get pending() {
    return this.queue.length;
  }

  /**
   * Replace the desired page set for this frame.
   * `wants` is an iterable of pageIds or {pageId, priority} records.
   */
  setWants(wants) {
    this.wants.clear();
    for (const want of wants) {
      const pageId = typeof want === 'number' ? want : want.pageId;
      const priority = typeof want === 'number' ? 0 : (want.priority ?? 0);
      const existing = this.wants.get(pageId);
      if (existing === undefined || priority < existing) this.wants.set(pageId, priority);
    }

    // Drop queued pages that are no longer wanted: never pay for a stale read.
    const before = this.queue.length;
    this.queue = this.queue.filter((pageId) => this.wants.has(pageId));
    this.stats.cancelled += before - this.queue.length;

    this.#rebuildQueue();
  }

  #rebuildQueue() {
    const cached = (pageId) => Boolean(this.cache?.has(pageId));
    const candidates = [];
    for (const [pageId, priority] of this.wants) {
      if (cached(pageId) || this.inFlight.has(pageId)) {
        this.stats.hits++;
        continue;
      }
      candidates.push({ pageId, priority, prefetch: false });
    }

    candidates.sort((a, b) => a.priority - b.priority || a.pageId - b.pageId);
    this.queue = candidates.map((c) => c.pageId);

    // Read-ahead: the pages just after each wanted page, at lower priority.
    if (this.options.readaheadPages > 0) {
      const seen = new Set(this.queue);
      for (const pageId of [...this.queue]) {
        for (let step = 1; step <= this.options.readaheadPages; step++) {
          const next = pageId + step;
          if (seen.has(next) || cached(next) || this.inFlight.has(next)) continue;
          seen.add(next);
          this.queue.push(next);
        }
      }
    }
  }

  /**
   * Issue what the budget allows. Returns the pageIds actually started.
   * Call `settle()` (or await the returned promise) to complete them.
   */
  async pump() {
    this.stats.ticks++;
    const budgetPages = Math.max(1, Math.floor(this.options.bytesPerTick / PAGE_BYTES));
    let issued = 0;
    const batch = [];
    const promises = [];

    while (this.queue.length > 0 && issued < budgetPages && this.inFlight.size < this.options.maxInFlight) {
      const pageId = this.queue.shift();
      if (this.inFlight.has(pageId)) continue;
      if (this.cache?.has(pageId)) continue;
      if (this.failed.has(pageId)) continue;

      this.inFlight.add(pageId);
      issued++;
      batch.push(pageId);
      promises.push(
        this.#read(pageId).finally(() => {
          this.inFlight.delete(pageId);
        }),
      );
    }

    // Sequentiality: consecutive issued ids mean the drive gets one seek followed
    // by streaming, which is the whole goal. Continuity is tracked across batches
    // as well, because the queue is refilled every frame.
    for (const pageId of batch) {
      if (this.previousIssued !== null && pageId === this.previousIssued + 1) this.stats.sequentialRuns++;
      this.previousIssued = pageId;
    }
    this.stats.issues += batch.length;
    this.stats.bytes += batch.length * PAGE_BYTES;
    this.stats.lastBatch = batch;

    await Promise.all(promises);
    return batch;
  }

  async #read(pageId) {
    const started = nowMs();
    try {
      const bricks = await this.store.readPage(pageId);
      if (!bricks) {
        this.failed.add(pageId);
        return null;
      }
      this.stats.misses++;
      if (this.cache) this.cache.set(pageId, bricks, PAGE_BYTES);
      return bricks;
    } catch (error) {
      this.failed.add(pageId);
      this.stats.lastError = String(error?.message ?? error);
      return null;
    } finally {
      this.stats.ioMs += nowMs() - started;
    }
  }

  /** Drop all state (used when jumping to another region of the planet). */
  reset() {
    this.wants.clear();
    this.queue = [];
    this.inFlight.clear();
    this.failed.clear();
    this.previousIssued = null;
  }

  get summary() {
    const reads = this.stats.issues;
    return {
      reads,
      megabytes: +(this.stats.bytes / (1024 * 1024)).toFixed(2),
      sequentialBatches: this.stats.sequentialRuns,
      sequentiality: reads > 1 ? +Math.min(1, this.stats.sequentialRuns / Math.max(1, reads - 1)).toFixed(3) : 1,
      cancelled: this.stats.cancelled,
      hits: this.stats.hits,
      misses: this.stats.misses,
      ioMs: +this.stats.ioMs.toFixed(1),
      failed: this.failed.size,
    };
  }
}

function nowMs() {
  if (typeof performance !== 'undefined' && performance.now) return performance.now();
  return Number(process.hrtime.bigint() / 1000n) / 1000;
}
