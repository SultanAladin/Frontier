/**
 * Worker protocol test.
 *
 * The viewport talks to `src/worker/tile-worker.js` through postMessage, and nothing
 * else in the suite executes that file — so a protocol mismatch (wrong field name,
 * missing buffer, transfer list not matching) would only ever show up as a blank
 * planet in a browser. This drives the worker with a `self` shim instead.
 */

import test from 'node:test';
import assert from 'node:assert/strict';
import { createTile } from '../src/core/cube-sphere.js';
import { PlanetField } from '../src/core/planet-field.js';
import { createBrushOp } from '../src/core/brushes.js';

const responses = [];
globalThis.self = {
  onmessage: null,
  postMessage(message, transfer = []) {
    responses.push({ message, transfer });
  },
};

await import('../src/worker/tile-worker.js');

const send = (data) => globalThis.self.onmessage({ data });
const last = (type) => [...responses].reverse().find((entry) => entry.message.type === type);

test('the worker registers a message handler', () => {
  assert.equal(typeof globalThis.self.onmessage, 'function');
});

test('a field message rebuilds the worker field and reports ready', () => {
  const field = new PlanetField({ radius: 900000, seed: 4029 });
  const op = createBrushOp({
    toolId: 'island',
    dir: { x: 0.6, y: 0.5, z: 0.62 },
    radius: 8000,
    strength: 600,
    falloff: 0.5,
    seed: 3,
    field,
  });
  field.addOp(op);

  send({
    type: 'field',
    params: field.params,
    ops: field.serializeEdits(),
    revision: 1,
    options: { cellsPerTile: 16 },
  });

  const ready = last('ready');
  assert.ok(ready, 'no ready message');
  assert.equal(ready.message.revision, 1);
  assert.equal(ready.message.ops, 1, 'the journal was not restored from the message');
});

test('a build message returns a complete transferable patch', () => {
  const tile = createTile(0, 6, 10, 12);
  responses.length = 0;
  send({ type: 'build', jobId: 7, tile });

  const entry = last('tile');
  assert.ok(entry, 'no tile message');
  const message = entry.message;

  assert.equal(message.jobId, 7);
  assert.equal(message.key, tile.key);
  assert.ok(message.vertexCount > 0, 'empty mesh');
  assert.ok(message.triangleCount > 0, 'no triangles');

  for (const name of ['positions', 'normals', 'elevation', 'slope']) {
    const array = message[name];
    assert.ok(array instanceof Float32Array, `${name} is not a Float32Array`);
    assert.equal(
      array.length,
      message.vertexCount * (name === 'elevation' || name === 'slope' ? 1 : 3),
      `${name} length does not match vertexCount ${message.vertexCount}`,
    );
  }
  assert.ok(message.indices instanceof Uint32Array, 'indices are not a Uint32Array');
  assert.equal(message.indices.length, message.triangleCount * 3);

  // Every buffer is transferred, not cloned, and no buffer is listed twice
  // (a duplicate transfer list entry is a DataCloneError in a browser).
  assert.ok(entry.transfer.length >= 5, `only ${entry.transfer.length} buffers transferred`);
  assert.equal(new Set(entry.transfer).size, entry.transfer.length, 'transfer list has duplicates');
  for (const buffer of entry.transfer) {
    assert.ok(buffer instanceof ArrayBuffer && buffer.byteLength > 0, 'transferred a non-buffer');
  }
});

test('an op message keeps the journal in step', () => {
  const field = new PlanetField({ radius: 900000, seed: 4029 });
  const op = createBrushOp({
    toolId: 'raise',
    dir: { x: 0.1, y: 0.2, z: 0.97 },
    radius: 6000,
    strength: 500,
    falloff: 0.5,
    seed: 11,
    field,
  });

  responses.length = 0;
  send({ type: 'op', op: { ...op, id: undefined, order: undefined }, revision: 2 });
  assert.equal(last('resync'), undefined, 'an in-order op asked for a resync');
  assert.equal(last('error'), undefined, 'an in-order op produced an error');
});

test('an out-of-order op asks for a resync instead of corrupting the journal', () => {
  responses.length = 0;
  send({ type: 'op', op: { kind: 'raise' }, revision: 99 });
  const resync = last('resync');
  assert.ok(resync, 'no resync requested');
  assert.equal(resync.message.reason, 'revision');
});

