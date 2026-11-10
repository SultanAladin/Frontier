// Road Editor — project save/load + OBJ export.
import { RoadGraph } from './graph.js';

export function serializeProject(graph, meta = {}) {
  return JSON.stringify({ app: 'frontier-road-editor', version: 1, meta, data: graph.toJSON() }, null, 1);
}

export function deserializeProject(text) {
  const obj = JSON.parse(text);
  if (obj.app !== 'frontier-road-editor') throw new Error('Not a Frontier road project');
  return RoadGraph.fromJSON(obj.data);
}

export function downloadText(filename, text, mime = 'application/json') {
  const blob = new Blob([text], { type: mime });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = filename;
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}

// Minimal OBJ export (positions + normals + groups per material bucket).
export function exportOBJ(meshes) {
  const lines = ['# Frontier Road Editor — road network', 'o RoadNetwork'];
  let offset = 1;
  for (const [mat, b] of Object.entries(meshes.buckets)) {
    lines.push(`g ${mat}`);
    for (let i = 0; i < b.count; i++) {
      lines.push(`v ${b.positions[i * 3].toFixed(3)} ${b.positions[i * 3 + 1].toFixed(3)} ${b.positions[i * 3 + 2].toFixed(3)}`);
    }
    for (let i = 0; i < b.count; i++) {
      lines.push(`vn ${b.normals[i * 3].toFixed(3)} ${b.normals[i * 3 + 1].toFixed(3)} ${b.normals[i * 3 + 2].toFixed(3)}`);
    }
    for (let f = 0; f < b.index.length; f += 3) {
      const a = b.index[f] + offset, c = b.index[f + 1] + offset, d = b.index[f + 2] + offset;
      lines.push(`f ${a}//${a} ${c}//${c} ${d}//${d}`);
    }
    offset += b.count;
  }
  return lines.join('\n');
}
