/**
 * Pool <-> worker round trip.
 *
 * `tile-worker.test.mjs` proves the worker answers correctly; the core suite proves
 * the manager consumes patches correctly. This closes the middle: it drives the real
 * `TileWorkerPool` through a Worker shim that executes the real worker module, so a
 * protocol mismatch between the two — the exact kind of bug that shows up as an empty
 * planet rather than an error — fails here instead of in a browser.
 */

import test from 'node:test';
import assert from 'node:assert/strict';
import { register } from 'node:module';

register('./support/three-loader.mjs', import.meta.url);

const { TileWorkerPool } = await import('../src/app/macro-viewport.js');
const { createTile } = await import('../src/core/cube-sphere.js');

// One in-process copy of the real worker module, addressed through a `self` shim.
// The stub swaps `globalThis.self` around each delivery, which is safe because the
// worker's handler runs to completion synchronously.
const inbox = { onmessage: null };
const outbox = [];
const workerSelf = {
  get onmessage() {
    return inbox.onmessage;
  },
  set onmessage(fn) {
    inbox.onmessage = fn;
  },
  postMessage(message, transfer = []) {
    outbox.push({ message, transfer });
  },
};

const hadSelf = 'self' in globalThis;
const previousSelf = globalThis.self;
globalThis.self = workerSelf;
await import('../src/worker/tile-worker.js');
if (hadSelf) globalThis.self = previousSelf;
else delete globalThis.self;

const hadWorker = 'Worker' in globalThis;
const previousWorker = globalThis.Worker;

class WorkerStub {
  constructor() {
    this.onmessage = null;
    this.onerror = null;
    this.posted = [];
    this.terminated = false;
  }

  postMessage(message) {
    this.posted.push(message);
    outbox.length = 0;
    globalThis.self = workerSelf;
    try {
      inbox.onmessage({ data: message });
    } finally {
      if (hadSelf) globalThis.self = previousSelf;
      else delete globalThis.self;
    }
    for (const { message: reply } of outbox.splice(0)) {
      queueMicrotask(() => this.onmessage?.({ data: reply }));
    }
  }

  terminate() {
    this.terminated = true;
  }
}

test('a pool build round-trips a real worker patch', async () => {
  globalThis.Worker = WorkerStub;
  const diagnostics = [];
  const pool = new TileWorkerPool(1, {
    onDiagnostic: (message) => diagnostics.push(message),
    fallback: () => {
      throw new Error('the fallback must not run when workers work');
    },
  });

  const { PlanetField } = await import('../src/core/planet-field.js');
  const field = new PlanetField({ radius: 900000, seed: 4029 });
  pool.setField(field.params, field.serializeEdits(), 1, { cellsPerTile: 16 });

  const mesh = await pool.build(createTile(0, 6, 10, 12));

  assert.ok(mesh.positions instanceof Float32Array, 'positions did not survive the round trip');
  assert.ok(mesh.indices instanceof Uint32Array, 'indices did not survive the round trip');
  assert.ok(mesh.vertexCount > 0 && mesh.triangleCount > 0, 'the worker returned an empty patch');
  assert.equal(mesh.indices.length, mesh.triangleCount * 3);
  assert.ok(mesh.positions.length >= mesh.vertexCount * 3);
  assert.deepEqual(diagnostics, [], 'a healthy pool reported a problem');
  assert.equal(pool.degraded, false);

  pool.dispose();
  assert.ok(pool.workers.length === 0);
});

test('a worker that cannot post degrades to the main-thread fallback', async () => {
  class BlockedWorker {
    constructor() {
      this.onmessage = null;
      this.onerror = null;
    }

    postMessage() {
      throw new Error('module workers are blocked');
    }

    terminate() {}
  }

  globalThis.Worker = BlockedWorker;
  const built = [];
  const diagnostics = [];
  const pool = new TileWorkerPool(1, {
    onDiagnostic: (message) => diagnostics.push(message),
    fallback: (tile) => {
      built.push(tile.key);
      return {
        positions: new Float32Array([0, 0, 0]),
        normals: new Float32Array([0, 1, 0]),
        elevation: new Float32Array([0]),
        slope: new Float32Array([0]),
        indices: new Uint32Array([]),
        vertexCount: 1,
        triangleCount: 0,
        stats: { buildMs: 0 },
      };
    },
  });

  const first = await pool.build(createTile(0, 6, 11, 12));
  assert.equal(built.length, 1, 'the first failure did not fall back');
  assert.ok(first.vertexCount === 1);
  assert.equal(diagnostics.length, 1, 'the failure was not reported to the editor');

  await pool.build(createTile(0, 6, 11, 13));
  assert.equal(pool.degraded, true, 'the pool did not degrade after repeated failures');

  // Once degraded, the fallback runs without touching a worker at all.
  const postsBefore = pool.workers[0].posted?.length ?? 0;
  await pool.build(createTile(0, 6, 11, 14));
  assert.equal(built.length, 3);
  assert.equal(pool.workers[0].posted?.length ?? 0, postsBefore, 'a degraded pool still posted jobs');

  if (hadWorker) globalThis.Worker = previousWorker;
  else delete globalThis.Worker;
});
