import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm, open } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import {
  BRICK_HEADER_BYTES,
  FileIo,
  MemoryIo,
  PAGE_BYTES,
  PAGE_HEADER_BYTES,
  TILES_PER_PAGE,
  TileStore,
  createBrick,
  crc32,
  decodePage,
  encodePage,
  pageForTile,
  slotForTile,
  tileHash,
} from '../src/core/tile-store.js';

function makeBrick({ size = 16, min = -40, max = 65.5, key = 7, seed = 1 } = {}) {
  const values = new Float32Array(size * size * size);
  let state = seed;
  for (let i = 0; i < values.length; i++) {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    values[i] = min + (state / 4294967296) * (max - min);
  }
  return createBrick({ nx: size, ny: size, nz: size, min, max, values, key });
}

test('a brick survives quantisation inside one quantisation step', () => {
  const brick = makeBrick();
  const page = encodePage(3, new Map([[0, brick]]));
  const decoded = decodePage(page, 3);
  const restored = decoded.bricks.get(0);
  const step = (brick.max - brick.min) / 65534;

  assert.equal(restored.nx, brick.nx);
  assert.equal(restored.key, brick.key);
  assert.equal(restored.values.length, brick.values.length);

  let worst = 0;
  for (let i = 0; i < brick.values.length; i++) {
    worst = Math.max(worst, Math.abs(restored.values[i] - brick.values[i]));
  }
  assert.ok(worst <= step, `worst error ${worst} above step ${step}`);
});

test('sixteen bricks fit one page exactly', () => {
  const bricks = new Map();
  for (let slot = 0; slot < TILES_PER_PAGE; slot++) bricks.set(slot, makeBrick({ seed: slot + 3 }));
  const page = encodePage(0, bricks);
  assert.equal(page.length, PAGE_BYTES);
  const decoded = decodePage(page, 0);
  assert.equal(decoded.bricks.size, TILES_PER_PAGE);

  const perBrick = BRICK_HEADER_BYTES + 16 * 16 * 16 * 2;
  assert.ok(PAGE_HEADER_BYTES + TILES_PER_PAGE * perBrick <= PAGE_BYTES, 'layout does not fit');
  assert.ok(PAGE_BYTES - (PAGE_HEADER_BYTES + TILES_PER_PAGE * perBrick) < 32 * 1024, 'page wastes too much');
});

test('an oversized brick is rejected instead of silently truncated', () => {
  const big = makeBrick({ size: 32 });
  const bricks = new Map();
  for (let slot = 0; slot < TILES_PER_PAGE; slot++) bricks.set(slot, big);
  assert.throws(() => encodePage(9, bricks), /overflow/);
});

test('a torn read is detected by the CRC instead of corrupting terrain', () => {
  const bricks = new Map([[2, makeBrick()]]);
  const page = encodePage(11, bricks);

  const torn = page.slice();
  torn[PAGE_HEADER_BYTES + 200] ^= 0xff;
  assert.throws(() => decodePage(torn, 11), /CRC/);

  const wrongId = page.slice();
  assert.throws(() => decodePage(wrongId, 12), /does not match/);

  const badMagic = page.slice();
  badMagic[0] ^= 0xff;
  assert.throws(() => decodePage(badMagic, 11), /magic/);

  const badVersion = page.slice();
  badVersion[4] = 99;
  assert.throws(() => decodePage(badVersion, 11), /version/);
});

test('CRC32 matches the reference check value', () => {
  const text = new Uint8Array([...Buffer.from('123456789')]);
  assert.equal(crc32(text), 0xcbf43926);
});

test('page addressing groups a 4x4 block of tiles and never collides', () => {
  const seen = new Map();
  for (let lod = 0; lod <= 6; lod++) {
    const n = 1 << lod;
    for (let face = 0; face < 6; face++) {
      for (let iy = 0; iy < n; iy++) {
        for (let ix = 0; ix < n; ix++) {
          const page = pageForTile(face, lod, ix, iy);
          const slot = slotForTile(ix, iy);
          assert.ok(slot >= 0 && slot < TILES_PER_PAGE);
          const key = `${page}:${slot}`;
          assert.ok(!seen.has(key), `page/slot collision at ${key}`);
          seen.set(key, true);
        }
      }
    }
  }
  assert.ok(seen.size > 1000);
});

test('the in-memory store round-trips bricks through a page', async () => {
  const store = new TileStore(new MemoryIo());
  await store.open();
  const brick = makeBrick({ key: 42 });
  const bytes = await store.writeTile(4, 5, 12, 9, brick);
  assert.equal(bytes, PAGE_BYTES);

  const pageId = pageForTile(4, 5, 12, 9);
  const bricks = await store.readPage(pageId);
  assert.ok(bricks.has(slotForTile(12, 9)));
  // One read to merge the new brick into the page, one for the verification read.
  assert.equal(store.stats.pageReads, 2);
  assert.equal(store.stats.bytesRead, PAGE_BYTES);
  assert.equal(store.stats.crcFailures, 0);
  await store.close();
});

test('the file backend stores pages at fixed offsets in one container', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'frontier-tiles-'));
  try {
    const io = new FileIo(dir, { fileName: 'planet.tiles' });
    await io.open();
    const store = new TileStore(io);

    const brick = makeBrick({ key: 5 });
    await store.writePage(3, new Map([[1, brick]]));
    await store.writePage(4, new Map([[2, makeBrick({ seed: 8 })]]));
    await io.flush();

    const three = await store.readPage(3);
    const four = await store.readPage(4);
    assert.ok(three.get(1), 'page 3 slot 1 missing');
    assert.ok(four.get(2), 'page 4 slot 2 missing');
    assert.equal(await store.readPage(99), null, 'beyond EOF must read as missing');

    assert.equal(io.stats.writeOps, 2);
    assert.equal(io.stats.bytesWritten, PAGE_BYTES * 2);
    assert.ok(io.stats.readMs >= 0);
    await io.close();
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});

test('a corrupted page on disk surfaces as a store failure, not bad data', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'frontier-tiles-'));
  try {
    const io = new FileIo(dir, { fileName: 'planet.tiles' });
    await io.open();
    const store = new TileStore(io);
    await store.writePage(0, new Map([[0, makeBrick()]]));
    await io.flush();
    await io.close();

    // Flip one payload byte the way a power-cut or bad sector would.
    const handle = await open(join(dir, 'planet.tiles'), 'r+');
    const buffer = Buffer.alloc(1, 0xff);
    await handle.write(buffer, 0, 1, PAGE_HEADER_BYTES + 100);
    await handle.close();

    const reopened = new FileIo(dir, { fileName: 'planet.tiles' });
    await reopened.open();
    const reopenedStore = new TileStore(reopened);
    await assert.rejects(() => reopenedStore.readPage(0), /CRC/);
    assert.equal(reopenedStore.stats.crcFailures, 1);
    await reopened.close();
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});

test('tile hashes are stable and spread', () => {
  const hashes = new Set();
  for (let i = 0; i < 400; i++) {
    hashes.add(tileHash(i % 6, i % 13, i, i * 3));
  }
  assert.ok(hashes.size > 390, `hash collisions: ${400 - hashes.size}`);
  assert.equal(tileHash(1, 2, 3, 4), tileHash(1, 2, 3, 4));
});

