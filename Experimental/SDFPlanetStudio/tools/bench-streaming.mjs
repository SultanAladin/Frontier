#!/usr/bin/env node
/**
 * Streaming benchmark: measured IO on this machine, plus an explicitly labelled
 * HDD seek model on top of the same measured batch latency.
 *
 * What is measured (real): syscalls, bytes, wall time and throughput for three
 * access patterns over an identical working set of 512 tiles in a container file.
 *
 * What is modelled (not measured): the effect of rotational seek latency. The
 * sandbox disk has no seek penalty, so `--simulate-seek-ms` adds a fixed cost per
 * *read operation* on top of the measured service time. Every number produced
 * with a non-zero seek value is labelled "modelled" in the output.
 *
 * Usage:
 *   node tools/bench-streaming.mjs [--tiles 512] [--seek-ms 8] [--repeat 5]
 */

import { mkdtemp, rm, open, stat } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { writeFile, mkdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const EVIDENCE_DIR = join(HERE, '..', '..', '..', 'Exhibits', 'SDFPlanetStudio');

const { PAGE_BYTES } = await import('../src/core/tile-store.js');

const args = process.argv.slice(2);
const readArg = (name, fallback) => {
  const index = args.indexOf(name);
  return index >= 0 ? Number(args[index + 1]) : fallback;
};

const TILE_BYTES = 8 * 1024;
const TILES_PER_PAGE = Math.floor(PAGE_BYTES / TILE_BYTES);
const TILE_COUNT = readArg('--tiles', 4096);
const SEEK_MS = readArg('--seek-ms', 8);
const REPEAT = readArg('--repeat', 5);
const PAGE_COUNT = Math.ceil(TILE_COUNT / TILES_PER_PAGE);

function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

async function buildContainer(path) {
  const page = Buffer.alloc(PAGE_BYTES);
  const random = mulberry32(4242);
  const handle = await open(path, 'w+');
  for (let p = 0; p < PAGE_COUNT; p++) {
    // Realistic payload: incompressible quantised SDF bits, not zeros, so the
    // disk cannot cheat with hole-sparse reads.
    for (let i = 0; i < PAGE_BYTES; i += 4) page.writeUInt32LE((random() * 0xffffffff) >>> 0, i);
    await handle.write(page, 0, PAGE_BYTES, p * PAGE_BYTES);
  }
  await handle.sync();
  await handle.close();
  return (await stat(path)).size;
}

/** Does this read continue the previous one on the same track? */
function continues(previousOffset, offset, width) {
  return previousOffset !== null && offset === previousOffset + width;
}

async function measure(label, plan, path, { tileReads }) {
  const handle = await open(path, 'r');
  const rss0 = process.memoryUsage().rss;
  const times = [];
  let ops = 0;
  let bytes = 0;

  for (let run = 0; run < REPEAT; run++) {
    const buffer = Buffer.allocUnsafe(tileReads ? TILE_BYTES : PAGE_BYTES);
    ops = 0;
    bytes = 0;
    const started = process.hrtime.bigint();
    for (const offset of plan) {
      await handle.read(buffer, 0, buffer.length, offset);
      ops++;
      bytes += buffer.length;
    }
    times.push(Number(process.hrtime.bigint() - started) / 1e6);
  }

  await handle.close();
  times.sort((a, b) => a - b);
  const median = times[Math.floor(times.length / 2)];
  const best = times[0];
  const measuredMBs = bytes / (1024 * 1024) / (median / 1000);

  // Modelled only, and deliberately kinematic: a read that continues the previous
  // one only costs the spindle's rotational latency (~25% of a seek); any other
  // read costs a full seek. Service time is the measured one.
  let fullSeeks = 0;
  let continuations = 0;
  let previousOffset = null;
  for (const offset of plan) {
    if (continues(previousOffset, offset, tileReads ? TILE_BYTES : PAGE_BYTES)) continuations++;
    else fullSeeks++;
    previousOffset = offset;
  }
  const modelledSeek = SEEK_MS > 0 ? (fullSeeks * SEEK_MS + continuations * SEEK_MS * 0.25) / 1000 : 0;
  const modelledMs = median + modelledSeek * 1000;
  const modelledMBs = bytes / (1024 * 1024) / (modelledMs / 1000);

  const rss = process.memoryUsage().rss;
  return {
    label,
    reads: ops,
    megabytes: +(bytes / (1024 * 1024)).toFixed(2),
    medianBatchMs: +median.toFixed(2),
    bestBatchMs: +best.toFixed(2),
    measuredThroughputMBs: +measuredMBs.toFixed(1),
    seekModelMs: +modelledSeek.toFixed(0),
    fullSeeks,
    continuations,
    modelledTotalMs: +modelledMs.toFixed(0),
    modelledThroughputMBs: +modelledMBs.toFixed(2),
    rssDeltaMB: +((rss - rss0) / (1024 * 1024)).toFixed(1),
  };
}

async function main() {
  const dir = await mkdtemp(join(tmpdir(), 'frontier-bench-'));
  const path = join(dir, 'planet.tiles');
  const bytes = await buildContainer(path);

  // One shared working set: every tile in the container, in pseudo-random order.
  const random = mulberry32(90210);
  const tileOrder = Array.from({ length: TILE_COUNT }, (_, i) => i);
  for (let i = tileOrder.length - 1; i > 0; i--) {
    const j = Math.floor(random() * (i + 1));
    [tileOrder[i], tileOrder[j]] = [tileOrder[j], tileOrder[i]];
  }

  const pageOf = (tile) => Math.floor(tile / TILES_PER_PAGE);
  const slotOf = (tile) => tile % TILES_PER_PAGE;

  const perTilePlan = tileOrder.map((tile) => pageOf(tile) * PAGE_BYTES + slotOf(tile) * TILE_BYTES);
  const randomPages = [...new Set(tileOrder.map(pageOf))];
  const randomPagePlan = randomPages.map((page) => page * PAGE_BYTES);
  const sequentialPages = [...randomPages].sort((a, b) => a - b).map((page) => page * PAGE_BYTES);

  const results = [];
  results.push(await measure('per-tile 8 KiB reads (naive)', perTilePlan, path, { tileReads: true }));
  results.push(await measure('page reads, random order', randomPagePlan, path, { tileReads: false }));
  results.push(await measure('page reads, sorted (scheduler order)', sequentialPages, path, { tileReads: false }));

  const perTile = results[0];
  const sorted = results[2];
  const summary = {
    environment: {
      runtime: process.version,
      platform: `${process.platform} ${process.arch}`,
      pageBytes: PAGE_BYTES,
      tilesPerPage: TILES_PER_PAGE,
      containerMB: +(bytes / (1024 * 1024)).toFixed(1),
      tiles: TILE_COUNT,
      repeats: REPEAT,
      seekModelMs: SEEK_MS,
    },
    results,
    ratios: {
      syscallsSavedVsPerTile: +(perTile.reads / sorted.reads).toFixed(2),
      measuredSpeedupSortedVsPerTile: +(sorted.measuredThroughputMBs / perTile.measuredThroughputMBs).toFixed(2),
      modelledSpeedupSortedVsPerTile: +(sorted.modelledThroughputMBs / perTile.modelledThroughputMBs).toFixed(2),
      modelledSpeedupSortedVsRandomPages: +(
        sorted.modelledThroughputMBs / results[1].modelledThroughputMBs
      ).toFixed(2),
    },
    notes: [
      'measured = real file IO in this sandbox (no seek penalty on this disk)',
      SEEK_MS > 0
        ? `modelled = measured service time + ${SEEK_MS} ms per non-contiguous read and ${(SEEK_MS * 0.25).toFixed(1)} ms per contiguous continuation (7200 rpm seek / rotational latency)`
        : 'modelled disabled (--seek-ms 0)',
      'identical working set and identical bytes in all three patterns',
    ],
  };

  await mkdir(EVIDENCE_DIR, { recursive: true });
  await writeFile(join(EVIDENCE_DIR, 'streaming-bench.json'), `${JSON.stringify(summary, null, 2)}\n`);

  const pad = (value, width) => String(value).padStart(width);
  console.log('');
  console.log('pattern                              reads    MB   measured MB/s   modelled MB/s');
  console.log('-------------------------------------------------------------------------------');
  for (const r of results) {
    console.log(
      `${r.label.padEnd(34)} ${pad(r.reads, 6)} ${pad(r.megabytes, 6)} ${pad(r.measuredThroughputMBs, 14)} ${pad(r.modelledThroughputMBs, 15)}`,
    );
  }
  console.log('');
  console.log(`syscalls saved: ${summary.ratios.syscallsSavedVsPerTile}x fewer reads than per-tile`);
  console.log(`measured speedup (sorted pages vs per-tile): ${summary.ratios.measuredSpeedupSortedVsPerTile}x`);
  console.log(`modelled speedup (${SEEK_MS} ms seek, sorted pages vs per-tile): ${summary.ratios.modelledSpeedupSortedVsPerTile}x`);
  console.log(`modelled sorted-vs-random pages: ${summary.ratios.modelledSpeedupSortedVsRandomPages}x`);
  console.log('');
  console.log(`evidence: ${join(EVIDENCE_DIR, 'streaming-bench.json')}`);

  await rm(dir, { recursive: true, force: true });
}

await main();
