/**
 * On-disk tile store: fixed-size pages of quantised SDF bricks.
 *
 * Why pages, and why this shape
 * -----------------------------
 * A spinning disk charges ~8 ms per seek and then streams at 100-200 MB/s. A
 * design that issues one small read per tile (8 KB here) pays a seek per tile and
 * collapses to ~1 MB/s. A design that reads 160 KB per operation pays one seek
 * per sixteen tiles and stays near the streaming bandwidth. Everything in this
 * file exists to make the second design the only one the app can express:
 *
 *   * tiles are grouped into fixed 160 KiB pages, spatially (4x4 tile blocks), so
 *     a page is almost always useful in full;
 *   * the container is one preallocated file with pages at fixed offsets, so a
 *     read is `pread(pageId * PAGE_BYTES, PAGE_BYTES)` — a single syscall, and
 *     consecutive pages are physically adjacent;
 *   * payloads are int16-quantised narrow-band values with a per-page CRC32, so a
 *     torn read on a busy disk is detected instead of silently corrupting terrain;
 *   * reads are asynchronous, budgeted and prioritised by `stream-scheduler.js`.
 *
 * The base planet needs none of this: it is analytic plus a tiny edit journal. The
 * store carries *baked detail* — the erosion output, hand-authored high-resolution
 * patches, and anything that cannot be recomputed from the journal.
 */

export const PAGE_MAGIC = 0x46525450; // 'FRTP'
export const PAGE_VERSION = 1;
// 160 KiB = 40 disk sectors, and exactly what sixteen 16^3 int16 bricks plus
// their headers need (16 * 8216 + 80 = 131,536 bytes) with headroom.
export const PAGE_BYTES = 160 * 1024;
export const TILES_PER_PAGE = 16;
export const BRICK_HEADER_BYTES = 24;
export const PAGE_HEADER_BYTES = 16 + TILES_PER_PAGE * 4;

/** Quantisation floor: int16 across the brick's own [min, max] range. */
const QUANT_MAX = 65534;

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[i] = c >>> 0;
  }
  return table;
})();

export function crc32(bytes, start = 0, end = bytes.length) {
  let crc = 0xffffffff;
  for (let i = start; i < end; i++) crc = CRC_TABLE[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

/**
 * A brick is a small Cartesian SDF block: values plus its world bounds.
 * `values` is Float32Array of nx*ny*nz signed distances in metres.
 */
export function createBrick({ nx, ny, nz, min, max, values, key = 0 }) {
  return { nx, ny, nz, min, max, values, key };
}

/** Bytes one brick takes in a page payload (header + quantised samples). */
export function brickByteSize(brick) {
  return BRICK_HEADER_BYTES + brick.values.length * 2;
}

/** Quantise + pack one brick. Returns a Uint8Array view into `target` at `offset`. */
export function encodeBrick(brick, target, offset) {
  const count = brick.values.length;
  const view = new DataView(target.buffer, target.byteOffset + offset, BRICK_HEADER_BYTES + count * 2);
  view.setUint32(0, brick.key >>> 0, true);
  view.setUint16(4, brick.nx, true);
  view.setUint16(6, brick.ny, true);
  view.setUint16(8, brick.nz, true);
  view.setFloat32(10, brick.min, true);
  view.setFloat32(14, brick.max, true);
  const span = brick.max - brick.min;
  const inv = span > 1e-9 ? QUANT_MAX / span : 0;

  for (let i = 0; i < count; i++) {
    const q = span > 1e-9 ? Math.round((brick.values[i] - brick.min) * inv) - 32767 : 0;
    view.setInt16(BRICK_HEADER_BYTES + i * 2, Math.max(-32768, Math.min(32767, q)), true);
  }
  return offset + BRICK_HEADER_BYTES + count * 2;
}

export function decodeBrick(bytes, offset) {
  const view = new DataView(bytes.buffer, bytes.byteOffset + offset, BRICK_HEADER_BYTES);
  const key = view.getUint32(0, true);
  const nx = view.getUint16(4, true);
  const ny = view.getUint16(6, true);
  const nz = view.getUint16(8, true);
  const min = view.getFloat32(10, true);
  const max = view.getFloat32(14, true);
  const count = nx * ny * nz;

  const values = new Float32Array(count);
  const span = max - min;
  const scale = span / QUANT_MAX;
  const sampleView = new DataView(bytes.buffer, bytes.byteOffset + offset + BRICK_HEADER_BYTES, count * 2);
  for (let i = 0; i < count; i++) {
    values[i] = min + (sampleView.getInt16(i * 2, true) + 32767) * scale;
  }
  return { brick: { nx, ny, nz, min, max, values, key }, bytesUsed: BRICK_HEADER_BYTES + count * 2 };
}

/**
 * Encode a page. `bricks` is a sparse map slot -> brick (0..15).
 * Missing slots are zero length; the page is always exactly PAGE_BYTES.
 */
export function encodePage(pageId, bricks) {
  const page = new Uint8Array(PAGE_BYTES);
  const view = new DataView(page.buffer);
  view.setUint32(0, PAGE_MAGIC, true);
  view.setUint8(4, PAGE_VERSION);
  view.setUint8(5, 0);
  view.setUint16(6, 0, true);
  view.setUint32(8, pageId >>> 0, true);

  let payloadOffset = PAGE_HEADER_BYTES;
  let count = 0;
  for (let slot = 0; slot < TILES_PER_PAGE; slot++) {
    const brick = bricks.get(slot);
    if (!brick) {
      view.setUint32(16 + slot * 4, 0xffffffff, true);
      continue;
    }
    if (payloadOffset + brickByteSize(brick) > PAGE_BYTES) {
      throw new Error(`page ${pageId} overflow at slot ${slot}: ${brickByteSize(brick)} bytes`);
    }
    view.setUint32(16 + slot * 4, payloadOffset, true);
    payloadOffset = encodeBrick(brick, page, payloadOffset);
    count++;
  }

  view.setUint16(6, count, true);
  // CRC covers the whole fixed-size page (unused tail included), so decode can
  // validate the exact buffer it was handed without carrying a length field.
  view.setUint32(12, 0, true);
  view.setUint32(12, crc32(page, PAGE_HEADER_BYTES, PAGE_BYTES), true);
  return page;
}

/** Decode a page. Throws on magic, version or CRC mismatch (torn read). */
export function decodePage(bytes, expectedPageId = null) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, PAGE_HEADER_BYTES);
  if (view.getUint32(0, true) !== PAGE_MAGIC) throw new Error('page magic mismatch');
  const version = view.getUint8(4);
  if (version !== PAGE_VERSION) throw new Error(`page version ${version} unsupported`);
  const count = view.getUint16(6, true);
  const pageId = view.getUint32(8, true);
  if (expectedPageId !== null && pageId !== expectedPageId) {
    throw new Error(`page id ${pageId} does not match requested ${expectedPageId}`);
  }

  const storedCrc = view.getUint32(12, true);
  const actualCrc = crc32(bytes, PAGE_HEADER_BYTES, PAGE_BYTES);
  if (storedCrc !== actualCrc) throw new Error(`page ${pageId} failed CRC (torn read)`);

  const bricks = new Map();
  for (let slot = 0; slot < TILES_PER_PAGE; slot++) {
    const offset = view.getUint32(16 + slot * 4, true);
    if (offset === 0xffffffff) continue;
    const { brick } = decodeBrick(bytes, offset);
    bricks.set(slot, brick);
  }
  if (bricks.size !== count) throw new Error(`page ${pageId} declares ${count} bricks but holds ${bricks.size}`);
  return { pageId, bricks };
}

/**
 * Spatial page addressing: page = 4x4 block of tiles of the same LOD on one face,
 * so a page is one contiguous piece of ground and a page read is one seek.
 */
export function pageForTile(face, lod, ix, iy) {
  // Each LOD owns a disjoint page range, so a page never mixes resolutions.
  let base = 0;
  for (let level = 0; level < lod; level++) {
    const blocks = Math.max(1, Math.ceil((1 << level) / 4));
    base += 6 * blocks * blocks;
  }
  const blocksPerAxis = Math.max(1, Math.ceil((1 << lod) / 4));
  const bx = Math.floor(ix / 4);
  const by = Math.floor(iy / 4);
  return (base + face * blocksPerAxis * blocksPerAxis + by * blocksPerAxis + bx) >>> 0;
}

export function slotForTile(ix, iy) {
  return (iy % 4) * 4 + (ix % 4);
}

/** Stable 32-bit tile identity (FNV-1a). Kept browser-safe: no node crypto. */
export function tileHash(face, lod, ix, iy) {
  let hash = 0x811c9dc5;
  const text = `${face}:${lod}:${ix}:${iy}`;
  for (let i = 0; i < text.length; i++) {
    hash ^= text.charCodeAt(i);
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash >>> 0;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                      IO BACKENDS
//----------------------------------------------------------------------------------------------------------------------

/** In-memory pages: used by tests, by the browser prototype and by tools. */
export class MemoryIo {
  constructor({ seed = null } = {}) {
    this.pages = new Map();
    this.stats = { readOps: 0, writeOps: 0, bytesRead: 0, bytesWritten: 0 };
    this.seed = seed;
  }

  async open() {
    return this;
  }

  async close() {}

  async readPage(pageId) {
    this.stats.readOps++;
    const page = this.pages.get(pageId);
    if (!page) return null;
    this.stats.bytesRead += page.length;
    return page;
  }

  async writePage(pageId, bytes) {
    this.stats.writeOps++;
    this.stats.bytesWritten += bytes.length;
    this.pages.set(pageId, bytes);
  }
}

/**
 * Node file backend: one container file, fixed-size pages at fixed offsets.
 * `pread` of one page is a single syscall, and page N+1 is physically next to N,
 * which is exactly what a readahead-friendly scheduler wants.
 */
export class FileIo {
  constructor(directory, { fileName = 'planet.tiles' } = {}) {
    this.directory = directory;
    this.fileName = fileName;
    this.handle = null;
    this.stats = { readOps: 0, writeOps: 0, bytesRead: 0, bytesWritten: 0, readMs: 0 };
  }

  get path() {
    return `${this.directory}/${this.fileName}`;
  }

  async open() {
    const fs = await import('node:fs/promises');
    await fs.mkdir(this.directory, { recursive: true });
    // 'a+' ignores the position argument on POSIX, which would silently turn every
    // page write into an append. Open for positional writes instead.
    try {
      this.handle = await fs.open(this.path, 'r+');
    } catch {
      this.handle = await fs.open(this.path, 'w+');
    }
    return this;
  }

  async close() {
    if (this.handle) await this.handle.close();
    this.handle = null;
  }

  async readPage(pageId) {
    if (!this.handle) throw new Error('FileIo is not open');
    const buffer = Buffer.allocUnsafe(PAGE_BYTES);
    const started = process.hrtime.bigint();
    const { bytesRead } = await this.handle.read(buffer, 0, PAGE_BYTES, pageId * PAGE_BYTES);
    const elapsed = Number(process.hrtime.bigint() - started) / 1e6;
    this.stats.readOps++;
    this.stats.readMs += elapsed;
    if (bytesRead === 0) return null;
    if (bytesRead !== PAGE_BYTES) throw new Error(`short read on page ${pageId}: ${bytesRead}/${PAGE_BYTES}`);
    this.stats.bytesRead += bytesRead;
    return new Uint8Array(buffer.buffer, buffer.byteOffset, PAGE_BYTES);
  }

  async writePage(pageId, bytes) {
    if (!this.handle) throw new Error('FileIo is not open');
    if (bytes.length !== PAGE_BYTES) throw new Error(`page must be ${PAGE_BYTES} bytes, got ${bytes.length}`);
    const started = process.hrtime.bigint();
    await this.handle.write(bytes, 0, PAGE_BYTES, pageId * PAGE_BYTES);
    this.stats.writeOps++;
    this.stats.bytesWritten += bytes.length;
    this.stats.readMs += Number(process.hrtime.bigint() - started) / 1e6;
  }

  async flush() {
    if (this.handle) await this.handle.sync();
  }
}

//----------------------------------------------------------------------------------------------------------------------
//                                                         STORE
//----------------------------------------------------------------------------------------------------------------------

/**
 * Store façade: page → bricks, with the counters the HUD and the benchmark read.
 * Reads are the only hot path; writes exist for the bake CLI.
 */
export class TileStore {
  constructor(io) {
    this.io = io;
    this.stats = {
      pageReads: 0,
      cacheHits: 0,
      cacheMisses: 0,
      bytesRead: 0,
      crcFailures: 0,
      decodeMs: 0,
    };
  }

  async open() {
    await this.io.open();
    return this;
  }

  async close() {
    await this.io.close();
  }

  /** @returns {Promise<Map<number, object>|null>} slot -> brick */
  async readPage(pageId) {
    const bytes = await this.io.readPage(pageId);
    this.stats.pageReads++;
    if (!bytes) return null;
    this.stats.bytesRead += bytes.length;
    const started = nowMs();
    try {
      const decoded = decodePage(bytes, pageId);
      this.stats.decodeMs += nowMs() - started;
      return decoded.bricks;
    } catch (error) {
      this.stats.crcFailures++;
      throw error;
    }
  }

  async writePage(pageId, bricksBySlot) {
    const page = encodePage(pageId, bricksBySlot);
    await this.io.writePage(pageId, page);
    return page.length;
  }

  /** Convenience for the bake path: write one brick into its page slot. */
  async writeTile(face, lod, ix, iy, brick) {
    const pageId = pageForTile(face, lod, ix, iy);
    const slot = slotForTile(ix, iy);
    let bricks = null;
    try {
      bricks = await this.readPage(pageId);
    } catch {
      bricks = null;
    }
    const map = bricks ?? new Map();
    map.set(slot, brick);
    return this.writePage(pageId, map);
  }
}

function nowMs() {
  if (typeof performance !== 'undefined' && performance.now) return performance.now();
  return Number(process.hrtime.bigint() / 1000n) / 1000;
}
