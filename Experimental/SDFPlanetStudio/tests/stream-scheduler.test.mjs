import { test } from 'node:test';
import assert from 'node:assert/strict';

import { PAGE_BYTES } from '../src/core/tile-store.js';
import { TileCache } from '../src/core/tile-cache.js';
import { StreamScheduler } from '../src/core/stream-scheduler.js';

/** Store double: records every read, with an optional per-read cost. */
class RecordingStore {
  constructor({ latencyMs = 0, missing = new Set() } = {}) {
    this.latencyMs = latencyMs;
    this.missing = missing;
    this.reads = [];
    this.bytes = 0;
  }

  async readPage(pageId) {
    this.reads.push(pageId);
    this.bytes += PAGE_BYTES;
    if (this.latencyMs > 0) await new Promise((r) => setTimeout(r, this.latencyMs));
    if (this.missing.has(pageId)) return null;
    return new Map([[0, { pageId }]]);
  }
}

test('the cache is LRU and never exceeds its byte budget', () => {
  const cache = new TileCache({ budgetBytes: 3 });
  cache.set('a', 1, 1);
  cache.set('b', 2, 1);
  cache.set('c', 3, 1);

  assert.equal(cache.get('a'), 1); // 'a' becomes most recent
  cache.set('d', 4, 1); // must evict 'b'
  assert.equal(cache.has('b'), false);
  assert.deepEqual(cache.keysByRecency(), ['c', 'a', 'd']);
  assert.ok(cache.bytes <= 3);
  assert.equal(cache.stats.evictions, 1);
  assert.equal(cache.get('missing'), undefined);
  assert.equal(cache.stats.misses, 1);
});

test('an object larger than the budget is refused rather than thrashing', () => {
  const cache = new TileCache({ budgetBytes: 10 });
  assert.equal(cache.set('huge', {}, 11), false);
  assert.equal(cache.size, 0);
  assert.equal(cache.bytes, 0);
  cache.set('ok', {}, 10);
  assert.equal(cache.utilisation, 1);
  cache.setBudget(5);
  assert.equal(cache.size, 0, 'shrinking the budget evicts immediately');
});

test('one pump respects both the byte budget and the in-flight cap', async () => {
  const store = new RecordingStore();
  const cache = new TileCache({ budgetBytes: 1024 * 1024 });
  const scheduler = new StreamScheduler({
    store,
    cache,
    options: { bytesPerTick: PAGE_BYTES * 3, maxInFlight: 2, readaheadPages: 0 },
  });

  scheduler.setWants([1, 2, 3, 4, 5, 6]);
  const issued = await scheduler.pump();
  assert.ok(issued.length <= 2, `issued ${issued.length} with maxInFlight 2`);
  assert.equal(store.reads.length, issued.length);
});

test('wants are served in priority order, then by page id', async () => {
  const store = new RecordingStore();
  const scheduler = new StreamScheduler({
    store,
    options: { bytesPerTick: PAGE_BYTES * 8, maxInFlight: 8, readaheadPages: 0 },
  });
  scheduler.setWants([
    { pageId: 40, priority: 5 },
    { pageId: 12, priority: 1 },
    { pageId: 30, priority: 5 },
    { pageId: 11, priority: 1 },
  ]);
  const issued = await scheduler.pump();
  assert.deepEqual(issued, [11, 12, 30, 40]);
});

test('duplicate requests for one page cost one read', async () => {
  const store = new RecordingStore();
  const scheduler = new StreamScheduler({
    store,
    options: { bytesPerTick: PAGE_BYTES * 8, maxInFlight: 8, readaheadPages: 0 },
  });
  scheduler.setWants([7, 7, 7, { pageId: 7, priority: 0 }]);
  await scheduler.pump();
  assert.deepEqual(store.reads, [7]);
});

test('readahead queues the pages the camera is about to reach', async () => {
  const store = new RecordingStore();
  const scheduler = new StreamScheduler({
    store,
    options: { bytesPerTick: PAGE_BYTES * 8, maxInFlight: 8, readaheadPages: 2 },
  });
  scheduler.setWants([100]);
  const issued = await scheduler.pump();
  assert.deepEqual(issued, [100, 101, 102]);
});

test('a want that disappears before issue is cancelled, not read', async () => {
  const store = new RecordingStore();
  const scheduler = new StreamScheduler({
    store,
    options: { bytesPerTick: PAGE_BYTES * 1, maxInFlight: 1, readaheadPages: 0 },
  });
  scheduler.setWants([1, 2, 3]);
  await scheduler.pump();
  assert.deepEqual(store.reads, [1]);

  scheduler.setWants([3]);
  assert.equal(scheduler.stats.cancelled, 1, 'page 2 should have been cancelled');
  await scheduler.pump();
  assert.deepEqual(store.reads, [1, 3]);
});

test('cached pages are never requested twice', async () => {
  const store = new RecordingStore();
  const cache = new TileCache({ budgetBytes: PAGE_BYTES * 4 });
  const scheduler = new StreamScheduler({
    store,
    cache,
    options: { bytesPerTick: PAGE_BYTES * 4, maxInFlight: 4, readaheadPages: 0 },
  });

  scheduler.setWants([5, 6]);
  await scheduler.pump();
  scheduler.setWants([5, 6]);
  await scheduler.pump();
  assert.deepEqual(store.reads, [5, 6]);
  assert.ok(scheduler.summary.hits >= 2);
});

test('a missing page is not retried every frame', async () => {
  const store = new RecordingStore({ missing: new Set([4]) });
  const scheduler = new StreamScheduler({
    store,
    options: { bytesPerTick: PAGE_BYTES * 4, maxInFlight: 4, readaheadPages: 0 },
  });
  scheduler.setWants([4]);
  await scheduler.pump();
  scheduler.setWants([4]);
  await scheduler.pump();
  assert.deepEqual(store.reads, [4]);
  assert.equal(scheduler.summary.failed, 1);
});

test('a flying camera produces sequential batches, a jumping one does not', async () => {
  const makeScheduler = () =>
    new StreamScheduler({
      store: new RecordingStore(),
      options: { bytesPerTick: PAGE_BYTES * 4, maxInFlight: 4, readaheadPages: 0 },
    });

  const flying = makeScheduler();
  for (let frame = 0; frame < 12; frame++) {
    flying.setWants([frame * 4, frame * 4 + 1, frame * 4 + 2, frame * 4 + 3]);
    await flying.pump();
  }

  const jumping = makeScheduler();
  for (let frame = 0; frame < 12; frame++) {
    jumping.setWants([frame * 97, frame * 97 + 1, frame * 97 + 2, frame * 97 + 3]);
    await jumping.pump();
  }

  assert.ok(flying.summary.sequentiality > 0.9, `flying sequentiality ${flying.summary.sequentiality}`);
  assert.ok(
    jumping.summary.sequentiality < flying.summary.sequentiality,
    'random access must not look sequential',
  );
});

test('budget accounting matches the bytes the store actually served', async () => {
  const store = new RecordingStore();
  const cache = new TileCache({ budgetBytes: PAGE_BYTES * 16 });
  const scheduler = new StreamScheduler({
    store,
    cache,
    options: { bytesPerTick: PAGE_BYTES * 4, maxInFlight: 2, readaheadPages: 1 },
  });

  scheduler.setWants([10, 11, 12, 13, 14, 15]);
  for (let i = 0; i < 4; i++) await scheduler.pump();

  const summary = scheduler.summary;
  assert.equal(summary.reads, store.reads.length);
  assert.equal(summary.megabytes, +((store.bytes / (1024 * 1024)).toFixed(2)));
  assert.ok(summary.reads >= 6);
});

test('reset clears wants, queue and failure memory for a teleport', async () => {
  const store = new RecordingStore();
  const scheduler = new StreamScheduler({ store, options: { readaheadPages: 0 } });
  scheduler.setWants([1, 2]);
  await scheduler.pump();
  scheduler.reset();
  assert.equal(scheduler.pending, 0);
  scheduler.setWants([2]);
  await scheduler.pump();
  assert.equal(store.reads.at(-1), 2);
});
