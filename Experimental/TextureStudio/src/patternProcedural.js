/*============================================================================================================================================
                                                          PATTERNPROCEDURAL.JS
============================================================================================================================================*/
// 🧩 Composable modifier-stack primitives for pattern motifs: Array/Inset, Mirror and Align-to-edge.
// A "motif" is just an array of layer-shaped objects (whatever `layerFactory(kind, extra)` returns,
// e.g. patternDocument.js's `patternLayer`); every operation below is a pure function over that
// array, so pattern starters compose a few reusable operations over one base shape instead of each
// one hand-listing every instance's absolute coordinates. Downstream rendering/export never changes:
// the output is still a flat layer array, exactly what patternStarter() already produced by hand.

// 📝 Array/Inset: repeats the base layer `count` times, each step the same shape inset by a fixed
// amount (`sizeStep` px off width/height, not a ratio) so repeated rings read as nested "inner and
// outer" copies of one motif. `colors` cycles across the steps (e.g. [accent, ground] alternates a
// woven look); an optional `role` tag records which colour slot produced each step, so a later
// Align-to-edge can recolour just the accent rings per edge without touching the ground rings.
export function arrayInset(layerFactory, kind, base, options = {}) {
  const { count = 1, sizeStep = 0, stepX = 0, stepY = 0, colors, roles } = options;
  if (!Number.isFinite(count) || count < 1) throw new Error("arrayInset needs a positive count.");
  const startWidth = base.width ?? 160;
  const startHeight = base.height ?? 160;
  const layers = [];
  for (let index = 0; index < count; index++) {
    const width = Math.max(0, startWidth - sizeStep * index);
    const height = Math.max(0, startHeight - sizeStep * index);
    const color = colors && colors.length ? colors[index % colors.length] : base.color;
    const role = roles && roles.length ? roles[index % roles.length] : undefined;
    layers.push(
      layerFactory(kind, {
        ...base,
        x: (base.x ?? 256) + stepX * index,
        y: (base.y ?? 256) + stepY * index,
        width,
        height,
        color,
        ...(role ? { proceduralRole: role } : {}),
      }),
    );
  }
  return layers;
}

// 📝 Mirror: reflects every layer in `motif` across a horizontal ("y") or vertical ("x") line at
// `through`, returning the original motif plus its mirrored copy -- e.g. building one row and
// mirroring it top/bottom instead of hand-authoring both rows.
export function mirrorMotif(motif, options = {}) {
  const { axis = "y", through = 256 } = options;
  if (axis !== "x" && axis !== "y") throw new Error("mirrorMotif axis must be \"x\" or \"y\".");
  const reflected = motif.map((layer) => ({
    ...layer,
    x: axis === "x" ? 2 * through - layer.x : layer.x,
    y: axis === "y" ? 2 * through - layer.y : layer.y,
    // 📝 A single-axis reflection always flips handedness, so any non-zero rotation negates.
    rotation: layer.rotation ? -layer.rotation : layer.rotation,
  }));
  return [...motif, ...reflected];
}

// 📝 Align-to-edge: places one translated copy of `motif` per anchor, so a cluster is positioned
// relative to the tile's edges/quarters (anchor.x/anchor.y are the translation applied to the
// motif's own local coordinates, which are authored as offsets from the anchor, not the tile
// centre) instead of a hand-picked absolute coordinate. `anchor.recolor` maps a `proceduralRole`
// (as tagged by arrayInset) to a replacement colour, so each edge can carry its own accent while
// sharing the same ring structure.
export function alignToEdges(motif, anchors) {
  if (!Array.isArray(anchors) || !anchors.length) throw new Error("alignToEdges needs at least one anchor.");
  const layers = [];
  for (const anchor of anchors) {
    for (const layer of motif) {
      const { proceduralRole, ...rest } = layer;
      const recolored = proceduralRole && anchor.recolor && anchor.recolor[proceduralRole];
      layers.push({
        ...rest,
        x: layer.x + (anchor.x ?? 0),
        y: layer.y + (anchor.y ?? 0),
        color: recolored || layer.color,
      });
    }
  }
  return layers;
}
