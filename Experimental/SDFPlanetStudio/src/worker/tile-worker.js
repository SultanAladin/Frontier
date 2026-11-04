/**
 * Tile worker: owns one `PlanetField` copy and meshes patches off the main thread.
 *
 * Protocol (main -> worker)
 *   {type:'field',  params, ops, revision, options}  rebuild the field from scratch
 *   {type:'op',     op, revision}                     append one authored op
 *   {type:'build',  jobId, tile}                      mesh one patch
 *
 * Protocol (worker -> main)
 *   {type:'ready',  revision}
 *   {type:'resync', revision}   journal out of sync; main resends {type:'field'}
 *   {type:'tile',   jobId, key, revision, positions, normals, elevation, slope, indices, stats}
 *   {type:'error',  jobId, message}
 *
 * Buffers are transferred, not copied: a 20k-triangle patch costs one message,
 * not ~1.5 MB of structured clone.
 */

import { PlanetField } from '../core/planet-field.js';
import { buildTileMesh } from '../core/narrow-band.js';

let field = null;
let revision = -1;
let options = { cellsPerTile: 48 };

function post(message, transfer = []) {
  self.postMessage(message, transfer);
}

self.onmessage = (event) => {
  const message = event.data;

  switch (message.type) {
    case 'field': {
      field = new PlanetField(message.params ?? {});
      if (message.ops?.length) field.loadEdits(message.ops);
      revision = message.revision ?? 0;
      if (message.options) options = { ...options, ...message.options };
      post({ type: 'ready', revision, ops: field.ops.length });
      break;
    }

    case 'op': {
      if (!field) {
        post({ type: 'resync', reason: 'no-field' });
        return;
      }
      if (message.revision !== revision + 1) {
        post({ type: 'resync', reason: 'revision', revision, expected: message.revision });
        return;
      }
      field.addOp(message.op);
      revision = message.revision;
      break;
    }

    case 'build': {
      if (!field) {
        post({ type: 'resync', reason: 'no-field' });
        return;
      }
      try {
        const mesh = buildTileMesh(field, message.tile, {
          cellsPerTile: options.cellsPerTile,
          skirts: options.skirts ?? true,
          warp: 'tangent',
        });

        const transfer = [
          mesh.positions.buffer,
          mesh.normals.buffer,
          mesh.elevation.buffer,
          mesh.slope.buffer,
          mesh.indices.buffer,
        ];

        post(
          {
            type: 'tile',
            jobId: message.jobId,
            key: message.tile.key,
            tile: message.tile,
            revision,
            positions: mesh.positions,
            normals: mesh.normals,
            elevation: mesh.elevation,
            slope: mesh.slope,
            indices: mesh.indices,
            vertexCount: mesh.vertexCount,
            triangleCount: mesh.triangleCount,
            stats: mesh.stats,
          },
          transfer,
        );
      } catch (error) {
        post({ type: 'error', jobId: message.jobId, message: String(error?.message ?? error) });
      }
      break;
    }

    default:
      break;
  }
};
