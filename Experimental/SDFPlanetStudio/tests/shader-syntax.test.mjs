/**
 * Shader syntax gate.
 *
 * The GPU is where a typo in a GLSL string finally shows up, and a failed program
 * renders nothing — the most common cause of a black viewport that still has working
 * UI. There is no GPU here, so the shaders are parsed with a real GLSL ES parser
 * instead: that catches syntax and declaration errors (missing semicolons, unbalanced
 * braces, bad `uniform` blocks, malformed function signatures) before they reach a
 * browser.
 *
 * It cannot catch semantic misuse the driver would reject (type mismatches, using an
 * undeclared identifier), so it is a floor, not a proof.
 */

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { parser } from '@shaderfrog/glsl-parser';

const source = await readFile(new URL('../src/app/macro-viewport.js', import.meta.url), 'utf8');

/** Pull `const NAME = /* glsl *\/ \`...\`;` blocks out of the module. */
function extractShaders(code) {
  const shaders = [];
  const pattern = /const\s+([A-Z_]+)\s*=\s*\/\*\s*glsl\s*\*\/\s*`([\s\S]*?)`;/g;
  let match = pattern.exec(code);
  while (match) {
    shaders.push({ name: match[1], body: match[2] });
    match = pattern.exec(code);
  }
  return shaders;
}

const shaders = extractShaders(source);

test('all shaders are found in the viewport module', () => {
  const names = shaders.map((shader) => shader.name).sort();
  assert.deepEqual(names, [
    'SKY_FRAGMENT',
    'SKY_VERTEX',
    'TERRAIN_FRAGMENT',
    'TERRAIN_VERTEX',
    'WATER_FRAGMENT',
    'WATER_VERTEX',
  ]);
});

for (const shader of shaders) {
  test(`${shader.name} parses as GLSL`, () => {
    assert.ok(shader.body.length > 40, `${shader.name} is suspiciously short`);
    assert.doesNotThrow(
      () => parser.parse(shader.body, { quiet: true }),
      `${shader.name} failed to parse`,
    );
  });

  test(`${shader.name} has balanced braces and no stray backticks`, () => {
    const opens = (shader.body.match(/\{/g) ?? []).length;
    const closes = (shader.body.match(/\}/g) ?? []).length;
    assert.equal(opens, closes, `${shader.name}: ${opens} '{' vs ${closes} '}'`);
    assert.ok(!shader.body.includes('`'), `${shader.name} contains a backtick, breaking the template literal`);
  });
}

test('the water program declares every uniform the app sets', () => {
  // A uniform may live in the vertex or the fragment stage; three links both.
  const water = {
    body: shaders
      .filter((shader) => shader.name.startsWith('WATER_'))
      .map((shader) => shader.body)
      .join('\n'),
  };
  const declared = new Set(
    [...water.body.matchAll(/uniform\s+\w+\s+(\w+)\s*(\[\s*\d+\s*\])?\s*;/g)].map((match) => match[1]),
  );

  for (const name of [
    'uWaveA',
    'uWaveB',
    'uWaveCount',
    'uTime',
    'uSunDirection',
    'uCameraPosition',
    'uSkyTop',
    'uSkyHorizon',
    'uAirDensity',
    'uFoamWidth',
    'uFoamIntensity',
    'uCrestFoam',
    'uAbsorption',
    'uScattering',
    'uRipple',
    'uPixelAngle',
    'uSeaRadius',
  ]) {
    assert.ok(declared.has(name), `WATER_FRAGMENT does not declare ${name}`);
  }
});

test('the terrain program declares the uniforms the app writes each frame', () => {
  const terrain = {
    body: shaders
      .filter((shader) => shader.name.startsWith('TERRAIN_'))
      .map((shader) => shader.body)
      .join('\n'),
  };
  const declared = new Set(
    [...terrain.body.matchAll(/uniform\s+\w+\s+(\w+)\s*;/g)].map((match) => match[1]),
  );
  for (const name of ['uSunDirection', 'uSkyTop', 'uSkyHorizon', 'uCameraPosition', 'uSeaLevel', 'uSnowLine', 'uHazeDistance', 'uAirDensity', 'uTime']) {
    assert.ok(declared.has(name), `TERRAIN_FRAGMENT does not declare ${name}`);
  }
});

test('no stage references a uniform it does not declare', () => {
  // GLSL requires the declaration in the stage that uses it. A uniform named only in
  // the vertex stage and used in the fragment stage is a compile error, and a compile
  // error is a black viewport.
  for (const shader of shaders) {
    const declared = new Set(
      [...shader.body.matchAll(/uniform\s+\w+\s+(\w+)\s*(\[[^\]]*\])?\s*;/g)].map((match) => match[1]),
    );
    const used = new Set([...shader.body.matchAll(/\b(u[A-Z]\w*)\b/g)].map((match) => match[1]));
    const undeclared = [...used].filter((name) => !declared.has(name)).sort();
    assert.deepEqual(undeclared, [], `${shader.name} uses undeclared uniforms`);
  }
});
