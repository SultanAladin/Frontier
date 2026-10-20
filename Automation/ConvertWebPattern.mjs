#!/usr/bin/env node
// Convert the geometry subset of a validated Texture Studio Pattern JSON document
// into the strict native Vector Authoring TOML interchange format.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { validatePattern } from "../Experimental/TextureStudio/src/patternDocument.js";
import {
  patternPathContours,
  patternPrimitiveNodes,
  patternSizedPath,
} from "../Experimental/TextureStudio/src/patternGeometry.js";

const Channels = ["Colour", "Height", "Normal", "Roughness", "Metalness", "Opacity", "Occlusion"];
const NativeChannel = Object.fromEntries(Channels.map((name, index) => [name, index]));
const fail = (message) => { throw new Error(message); };
const finite = (value, fallback = 0) => Number.isFinite(Number(value)) ? Number(value) : fallback;

function srgbToLinear(byte) {
  const value = byte / 255;
  return value <= 0.04045 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4;
}
function parseColour(value, label) {
  if (typeof value !== "string" || !/^#[\da-f]{6}(?:[\da-f]{2})?$/i.test(value))
    fail(`${label} must be a literal #RRGGBB or #RRGGBBAA colour; linked palettes and gradients are not yet importable.`);
  const hex = value.slice(1);
  return [0, 2, 4, 6].map((offset, index) => index === 3
    ? (hex.length === 8 ? parseInt(hex.slice(6, 8), 16) / 255 : 1)
    : srgbToLinear(parseInt(hex.slice(offset, offset + 2), 16)));
}
function flowerNodes(layer) {
  const circle = patternPrimitiveNodes({ kind: "ellipse" }).nodes;
  const petal = (point) => ({
    x: (point.x - 50) * 0.36,
    y: 27 + (point.y - 50) * 0.54 - 50,
  });
  return Array.from({ length: 5 }, (_, index) => {
    const angle = index * 72 * Math.PI / 180;
    const rotate = (point) => {
      const p = petal(point);
      return { x: 50 + p.x * Math.cos(angle) - p.y * Math.sin(angle), y: 50 + p.x * Math.sin(angle) + p.y * Math.cos(angle) };
    };
    return { closed: true, nodes: circle.map((node) => ({
      ...rotate(node),
      ...(node.in ? { in: rotate(node.in) } : {}),
      ...(node.out ? { out: rotate(node.out) } : {}),
    })) };
  });
}
function contoursFor(layer) {
  let pathData;
  if (layer.kind === "flower") {
    return flowerNodes(layer).map(({ nodes }) => ({
      nodes: nodes.map((node) => ({
        ...node,
        x: ((node.x - 50) * layer.width) / 100,
        y: ((node.y - 50) * layer.height) / 100,
        ...(node.in ? { in: { x: ((node.in.x - 50) * layer.width) / 100, y: ((node.in.y - 50) * layer.height) / 100 } } : {}),
        ...(node.out ? { out: { x: ((node.out.x - 50) * layer.width) / 100, y: ((node.out.y - 50) * layer.height) / 100 } } : {}),
      })),
      closed: true,
    }));
  }
  try { pathData = patternSizedPath(layer); }
  catch (error) { fail(`Layer “${layer.name}”: ${error.message}`); }
  if (typeof pathData !== "string" || !pathData.trim())
    fail(`Layer “${layer.name}” (${layer.kind}) has no native-convertible vector path.`);
  const contours = patternPathContours(pathData, 400);
  if (!contours?.length) fail(`Layer “${layer.name}”: SVG path could not be parsed.`);
  return contours;
}
function cubicSegments(contours, layer) {
  const result = [];
  const local = (point) => [
    ((point.x / layer.width) * (layer.flipX ? -1 : 1)),
    ((point.y / layer.height) * (layer.flipY ? -1 : 1)),
  ];
  for (const contour of contours) {
    const nodes = contour.nodes;
    if (!Array.isArray(nodes) || nodes.length < 2) fail(`Layer “${layer.name}” contains an incomplete contour.`);
    const edgeCount = contour.closed ? nodes.length : nodes.length - 1;
    for (let index = 0; index < edgeCount; index++) {
      const start = nodes[index], end = nodes[(index + 1) % nodes.length];
      const a = local(start), b = local(end);
      const c1 = local(start.out || { x: start.x + (end.x - start.x) / 3, y: start.y + (end.y - start.y) / 3 });
      const c2 = local(end.in || { x: start.x + (end.x - start.x) * 2 / 3, y: start.y + (end.y - start.y) * 2 / 3 });
      result.push({ start_control: [...a, ...c1], control_end: [...c2, ...b] });
      if (result.length > 4000) fail(`Layer “${layer.name}” exceeds the native 4,000-segment limit.`);
    }
  }
  return result;
}
function channelData(layer) {
  const source = layer.SurfaceChannels || {};
  const result = {};
  for (const name of Channels) {
    const item = source[name === "Colour" ? "Color" : name] || {};
    result[name] = { enabled: name === "Occlusion" ? item.Enabled === true : item.Enabled !== false,
      strength: Math.max(0, Math.min(name === "Normal" ? 4 : 1, finite(item.Strength, 1))) };
  }
  result.Occlusion = { enabled: source.Occlusion?.Enabled === true,
    strength: Math.max(0, Math.min(1, finite(source.Occlusion?.Strength, 1))) };
  return result;
}
function layerStructure(layer) {
  if (layer.stitchRole) {
    const geometry = layer.stitchRole === "holes" ? "needle_entry" : layer.stitchRole === "thread" ? "stitch_thread" : "upper_thread_pass";
    return { structure: "stitch", geometry };
  }
  if (layer.weaveRole === "yarn") return { structure: "weave", geometry: "weave_yarn" };
  return { structure: "freeform", geometry: "unclassified" };
}
function instruction({ layer, root, segmentStart, segmentCount, colour, filled, strokeWidth, fillRule }) {
  return {
    topology: [6, segmentStart, segmentCount, fillRule === "evenodd" ? 1 : 0],
    bounds: [finite(layer.x), finite(layer.y), finite(layer.width), finite(layer.height)],
    transform: [finite(layer.rotation) * Math.PI / 180, strokeWidth, finite(layer.opacity, 1), 0],
    colour, colour_end: colour,
    paint: [0, 0, 0, filled ? 1 : 0],
    root,
  };
}
function nativeLayer(layer, token, root, contribution, structure) {
  const channels = channelData(layer);
  const surface_channels = Object.fromEntries(Channels.map((name) => [name.toLowerCase(), channels[name]]));
  return {
    token, name: String(layer.name || `Pattern Layer ${token}`).slice(0, 80),
    visible: layer.visible !== false, locked: layer.locked === true,
    root, surface_channels, surface_structure: structure.structure,
    textile_geometry: structure.geometry, surface_relief: Math.max(-1, Math.min(1, finite(layer.relief, 0))),
    ...(contribution ? { channel_override: contribution } : {}),
  };
}

/** Convert a validated Pattern JSON value to native vector-authoring TOML. */
export function convertPatternJson(input) {
  const pattern = validatePattern(input);
  if (!pattern || !Array.isArray(pattern.layers)) fail("Pattern JSON did not normalize to a layer document.");
  if (pattern.layers.length > 64) fail("Native Pattern import supports at most 64 web layers per document.");
  if (pattern.Enclosures?.length || pattern.StoredGroups?.length || pattern.layers.some((layer) => layer.group || layer.EnclosureToken || layer.LinkedInstance))
    fail("Pattern folders, linked instances, and group enclosures are not yet represented by the native vector document; import was stopped without dropping them.");
  if (pattern.backgroundSource) fail("Linked or gradient Pattern backgrounds are not yet supported by the native import.");
  if (pattern.repeat !== "straight" || pattern.tileAxes !== "xy" || pattern.mapping !== "uv" || pattern.rotation !== 0)
    fail("Native Pattern import currently requires straight XY tiling, UV mapping, and zero document rotation.");
  const instructions = [], segments = [], layers = [];
  for (const [index, layer] of pattern.layers.entries()) {
    const rotation = finite(layer.rotation) * Math.PI / 180;
    const halfX = Math.abs(Math.cos(rotation)) * layer.width / 2 + Math.abs(Math.sin(rotation)) * layer.height / 2;
    const halfY = Math.abs(Math.sin(rotation)) * layer.width / 2 + Math.abs(Math.cos(rotation)) * layer.height / 2;
    if (layer.visible && (layer.x - halfX < 0 || layer.x + halfX > 512 || layer.y - halfY < 0 || layer.y + halfY > 512))
      fail(`Layer “${layer.name}” crosses a Pattern tile edge; native periodic wrapping is not implemented yet.`);
    if (["image", "svg", "text"].includes(layer.kind))
      fail(`Layer “${layer.name}” uses ${layer.kind} content, which is not yet supported by native geometry import; import was stopped without omitting it.`);
    if (layer.mask?.enabled) fail(`Layer “${layer.name}” has an artwork mask that the native vector format cannot yet represent; import was stopped without dropping it.`);
    if (layer.fillSource || layer.strokeSource || layer.fillGradient || layer.strokeGradient)
      fail(`Layer “${layer.name}” uses linked or gradient paint; only literal solid colours are supported in this import phase.`);
    if (layer.strokeLinejoin && layer.strokeLinejoin !== "round") fail(`Layer “${layer.name}” uses ${layer.strokeLinejoin} stroke joins; native preview currently supports round joins only.`);
    if (layer.strokeLinecap && layer.strokeLinecap !== "round") fail(`Layer “${layer.name}” uses ${layer.strokeLinecap} stroke caps; native preview currently supports round caps only.`);
    const contours = contoursFor(layer);
    const convertedSegments = cubicSegments(contours, layer);
    if (segments.length + convertedSegments.length > 4000) fail("Pattern exceeds the native 4,000-segment limit.");
    const segmentStart = segments.length;
    segments.push(...convertedSegments);
    const fill = layer.paint ? layer.fillEnabled !== false : !(layer.kind === "path" && layer.strokeWidth);
    const strokeWidth = Math.max(0, finite(layer.strokeWidth, 0));
    const fillColour = parseColour(layer.color || "#ffffff", `Layer “${layer.name}” fill`);
    const strokeColour = parseColour(layer.stroke || layer.color || "#ffffff", `Layer “${layer.name}” stroke`);
    const sameColour = fillColour.every((value, channel) => Math.abs(value - strokeColour[channel]) < 1e-7);
    const roots = [];
    const make = (colour, filled, width) => {
      const root = instructions.length;
      instructions.push(instruction({ layer, root, segmentStart, segmentCount: convertedSegments.length,
        colour, filled, strokeWidth: width, fillRule: layer.fillRule }));
      roots.push(root);
    };
    if (fill && strokeWidth && !sameColour) {
      make(fillColour, true, 0);
      make(strokeColour, false, strokeWidth);
    } else if (fill || strokeWidth) make(fill ? fillColour : strokeColour, fill, strokeWidth);
    else fail(`Layer “${layer.name}” has neither a fill nor a stroke.`);
    let root = roots.at(-1);
    if (roots.length === 2) {
      root = instructions.length;
      instructions.push({ topology: [28, roots[0], roots[1], 0], bounds: [0, 0, 0, 0],
        transform: [0, 1, 1, 0], colour: [1, 1, 1, 1], colour_end: [1, 1, 1, 1], paint: [0, 0, 0, 0], root });
    }
    const shape = layerStructure(layer);
    layers.push(nativeLayer(layer, index + 1, root, null, shape));
  }
  if (instructions.length > 128) fail("Pattern exceeds the native 128-instruction limit.");
  return { format: "frontier.vector_artwork", version: 1,
    name: String(pattern.name || "Imported Pattern").slice(0, 80), stamp: false,
    token_clock: layers.length, instructions, segments, layers, enclosures: [] };
}

function tomlString(value) { return `"${String(value).replaceAll("\\", "\\\\").replaceAll('"', '\\"').replaceAll("\n", "\\n").replaceAll("\r", "\\r")}"`; }
function tomlValue(value) {
  if (typeof value === "string") return tomlString(value);
  if (typeof value === "boolean") return value ? "true" : "false";
  if (typeof value === "number") return Number.isFinite(value) ? String(value) : "0";
  if (Array.isArray(value)) return `[${value.map(tomlValue).join(", ")}]`;
  if (value && typeof value === "object") return `{ ${Object.entries(value).map(([key, item]) => `${key} = ${tomlValue(item)}`).join(", ")} }`;
  return "0";
}
export function serializeNativeToml(document) {
  const lines = [
    `format = ${tomlString(document.format)}`,
    `version = ${document.version}`,
    `name = ${tomlString(document.name)}`,
    `stamp = ${document.stamp}`,
    `token_clock = ${document.token_clock}`,
    `instructions = ${tomlValue(document.instructions.map(({ root, ...item }) => item))}`,
    `segments = ${tomlValue(document.segments)}`,
    `layers = ${tomlValue(document.layers)}`,
    `enclosures = []`,
    "",
  ];
  return lines.join("\n");
}

async function main() {
  const [inputFile, outputFile] = process.argv.slice(2);
  if (!inputFile || !outputFile || process.argv.length !== 4) {
    console.error("Usage: node Automation/ConvertWebPattern.mjs <pattern.json> <native.vector.toml>");
    process.exitCode = 2;
    return;
  }
  const input = JSON.parse(fs.readFileSync(inputFile, "utf8"));
  const result = serializeNativeToml(convertPatternJson(input));
  fs.mkdirSync(path.dirname(path.resolve(outputFile)), { recursive: true });
  fs.writeFileSync(outputFile, result, "utf8");
  console.log(`Converted ${input.layers?.length ?? 0} Pattern layers to ${outputFile}`);
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  try { await main(); }
  catch (error) { console.error(`Pattern import failed: ${error.message}`); process.exitCode = 1; }
}
