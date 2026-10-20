import test from "node:test";
import assert from "node:assert/strict";
import { convertPatternJson, serializeNativeToml } from "./ConvertWebPattern.mjs";

const document = (layers, extra = {}) => ({
  schema: "alloy.pattern.v1", name: "Import test", layers, ...extra,
});

test("converts normalized primitives and cubic paths into native vector instructions", () => {
  const converted = convertPatternJson(document([
    { kind: "star", name: "Gold thread", x: 128, y: 256, width: 80, height: 120,
      starPoints: 7, innerRadius: 0.38, rotation: 25, color: "#c18a2e", strokeWidth: 2,
      stitchRole: "thread", relief: 0.7,
      SurfaceChannels: { Color: { Enabled: false, Strength: 0.35 }, Height: { Enabled: true, Strength: 0.8 },
        Normal: { Enabled: true, Strength: 1.2 }, Metalness: { Enabled: true, Strength: 0.75 } } },
    { kind: "path", name: "Curve", x: 300, y: 250, width: 100, height: 100,
      nodes: [{ x: 10, y: 50, out: { x: 30, y: 5 } }, { x: 90, y: 50, in: { x: 70, y: 95 } }],
      color: "#e0e0e0", strokeWidth: 4 },
  ]));
  assert.equal(converted.format, "frontier.vector_artwork");
  assert.equal(converted.layers.length, 2);
  assert.equal(converted.layers[0].surface_structure, "stitch");
  assert.equal(converted.layers[0].textile_geometry, "stitch_thread");
  assert.equal(converted.layers[0].surface_channels.normal.strength, 1.2);
  assert.equal(converted.layers[0].surface_channels.colour.enabled, false);
  assert.equal(converted.layers[0].surface_channels.colour.strength, 0.35);
  assert.ok(converted.segments.length > 4);
  assert.ok(converted.instructions.length >= 2);
  assert.match(serializeNativeToml(converted), /format = "frontier\.vector_artwork"/);
});

test("preserves boolean fill rules and independent fill/stroke colours", () => {
  const converted = convertPatternJson(document([{
    kind: "path", name: "Outlined", path: "M10 10 L90 10 L90 90 Z", closed: true,
    x: 256, y: 256, width: 100, height: 100, fillRule: "evenodd",
    color: "#ffffff", stroke: "#000000", strokeWidth: 3, paint: true, fillEnabled: true,
  }]));
  assert.equal(converted.instructions.length, 3);
  assert.equal(converted.instructions[0].topology[3], 1);
  assert.equal(converted.layers[0].root, 2);
});

test("converts the remaining built-in vector silhouettes and arc closures", () => {
  const kinds = ["rect", "ellipse", "diamond", "triangle", "flower", "polygon", "arc"];
  const converted = convertPatternJson(document(kinds.map((kind) => ({
    kind, name: kind, x: 256, y: 256, width: 80, height: 90, color: "#4488cc",
    paint: true, fillEnabled: true, arcClosure: kind === "arc" ? "pie" : undefined,
  }))));
  assert.equal(converted.layers.length, kinds.length);
  assert.ok(converted.segments.length >= 30);
});

test("fails closed rather than silently dropping unsupported Pattern features", () => {
  assert.throws(() => convertPatternJson(document([{ kind: "image", src: "data:image/png;base64,AA==" }])), /not yet supported/);
  assert.throws(() => convertPatternJson(document([{ kind: "rect", mask: { category: "ellipse" } }])), /artwork mask/);
  assert.throws(() => convertPatternJson(document([{ kind: "rect", fillSource: { category: "linear", stops: [{ color: "#ffffff", offset: 0 }, { color: "#000000", offset: 1 }] } }])), /linked or gradient/);
  assert.throws(() => convertPatternJson(document([{ kind: "rect", group: "group-1" }])), /folders, linked instances/);
});
