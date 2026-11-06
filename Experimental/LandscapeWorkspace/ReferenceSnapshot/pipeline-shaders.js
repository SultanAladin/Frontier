/**
 * Composable GPU Stack Shaders (WebGL2 GLSL ES 3.00, ANGLE D3D11 HLSL Strict Compliant).
 *
 * Operates directly on `GpuErosionEngine`'s native `terrainTex` (`RGBA32F`:
 *   .r = bedrock height, .g = coarse talus, .b = fine sand/alluvium, .a = water depth)
 * plus `vecDispTex` (`RGBA32F`: 3D lateral overhang vector displacement) and `SdfCaveSystem`.
 *
 * Iterative physical simulation passes (hydraulic rain/pipe flow, thermal talus, cryosphere
 * snow/glaciers, aeolian wind) run through `erosion-shaders.js`'s strictly mass-conserving
 * GPU shaders — eliminating any possibility of iterative mass blowup or terrain stretching.
 */

import { COMMON_GEOLOGY_GLSL } from './erosion-shaders.js';

const GLSL_PRECISION_HEADER = `precision highp float;
precision highp int;
precision highp sampler2D;`;

const PIPELINE_MASK_GLSL = `
float safeSmoothstep(float e0, float e1, float x) {
  float lo = min(e0, e1);
  float hi = max(e0 + 1e-4, e1);
  float t = clamp((x - lo) / (hi - lo), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

// Evaluates a composable Black-and-White Layer Mask in [0, 1].
// uMaskCfg1 = (enabled, mode, minVal, maxVal)
// uMaskCfg2 = (feather, invert, noiseScale, seed)
// mode: 0=Height, 1=Slope, 2=Flow, 3=Concavity, 4=RadialCenter, 5=ProceduralNoise
float evalLayerMask(
  vec2 uv,
  vec2 worldXZ,
  float totalH,
  float maxRelief,
  float slopeDeg,
  float flowVal,
  float concavity,
  float worldSpan,
  vec4 uMaskCfg1,
  vec4 uMaskCfg2
) {
  if (uMaskCfg1.x < 0.5) return 1.0;

  int mode = int(uMaskCfg1.y + 0.5);
  float lo = uMaskCfg1.z;
  float hi = max(lo + 0.005, uMaskCfg1.w);
  float feath = max(0.015, uMaskCfg2.x);

  float metric = 0.0;
  if (mode == 0) {
    metric = clamp(totalH / max(20.0, maxRelief), 0.0, 1.2);
  } else if (mode == 1) {
    metric = clamp(slopeDeg / 75.0, 0.0, 1.0);
  } else if (mode == 2) {
    metric = clamp(flowVal, 0.0, 1.0);
  } else if (mode == 3) {
    metric = clamp(0.5 + concavity * 0.25, 0.0, 1.0);
  } else if (mode == 4) {
    metric = clamp(length(worldXZ) / (worldSpan * 0.52), 0.0, 1.2);
  } else {
    vec2 np = (worldXZ / max(1.0, worldSpan)) * max(0.4, uMaskCfg2.z) * 3.2 + vec2(uMaskCfg2.w * 0.013, uMaskCfg2.w * 0.029);
    metric = fbm2D(np, 4, 0.52);
  }

  float rampIn = safeSmoothstep(lo - feath, lo + feath * 0.35, metric);
  float rampOut = 1.0 - safeSmoothstep(hi - feath * 0.35, hi + feath, metric);
  float m = clamp(rampIn * rampOut, 0.0, 1.0);
  return uMaskCfg2.y > 0.5 ? (1.0 - m) : m;
}
`;

// ============================================================================
// 1. UNIFIED LANDFORM / DISPLACEMENT / SCULPT / SPLINE STACK PASS
// ============================================================================

export const STACK_OP_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

in vec2 vUv;
layout(location = 0) out vec4 outTerrain; // (bedrock, coarseSed, fineSed, water)
layout(location = 1) out vec4 outVecDisp; // (dispX, dispY, dispZ, 0)

uniform sampler2D uTerrainTex;
uniform sampler2D uVecDispTex;
uniform sampler2D uSculptTex;
uniform sampler2D uVelocityFieldTex;

uniform vec4 uDomain;    // (worldSpan, gridRes, maxRelief, opKind)
uniform vec4 uParam0;
uniform vec4 uParam1;
uniform vec4 uParam2;
uniform vec4 uParam3;
uniform vec4 uMaskCfg1;
uniform vec4 uMaskCfg2;
uniform vec4 uSplinePts[4]; // up to 8 (x, z) points packed as 4 vec4s
uniform vec4 uSplineElev[2]; // up to 8 control-point elevations (m); unset points are pre-filled from the start->end grade
uniform float uSplineHasY;   // 1 when any control point carries an authored elevation (true 3D spline)
uniform int uSplineCount;

${COMMON_GEOLOGY_GLSL}
${PIPELINE_MASK_GLSL}

void evalSplineDist(vec2 xz, out float minDist, out float alongT) {
  minDist = 1e6;
  alongT = 0.0;
  int n = clamp(uSplineCount, 0, 8);
  if (n < 2) return;

  vec2 pts[8];
  pts[0] = uSplinePts[0].xy; pts[1] = uSplinePts[0].zw;
  pts[2] = uSplinePts[1].xy; pts[3] = uSplinePts[1].zw;
  pts[4] = uSplinePts[2].xy; pts[5] = uSplinePts[2].zw;
  pts[6] = uSplinePts[3].xy; pts[7] = uSplinePts[3].zw;

  float invSegs = 1.0 / float(max(1, n - 1));
  for (int i = 0; i < 7; i++) {
    if (i >= n - 1) break;
    vec2 a = pts[i];
    vec2 b = pts[i + 1];
    vec2 ab = b - a;
    float len2 = max(1e-4, dot(ab, ab));
    float t = clamp(dot(xz - a, ab) / len2, 0.0, 1.0);
    vec2 proj = a + ab * t;
    float d = length(xz - proj);
    if (d < minDist) {
      minDist = d;
      alongT = (float(i) + t) * invSegs;
    }
  }
}

vec2 splineControlPoint(int idx) {
  if (idx <= 0) return uSplinePts[0].xy;
  if (idx == 1) return uSplinePts[0].zw;
  if (idx == 2) return uSplinePts[1].xy;
  if (idx == 3) return uSplinePts[1].zw;
  if (idx == 4) return uSplinePts[2].xy;
  if (idx == 5) return uSplinePts[2].zw;
  if (idx == 6) return uSplinePts[3].xy;
  return uSplinePts[3].zw;
}

vec2 catmullRomSplinePoint(vec2 p0, vec2 p1, vec2 p2, vec2 p3, float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  return 0.5 * ((2.0 * p1) + (-p0 + p2) * t +
    (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
    (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

float splinePointElev(int idx) {
  if (idx <= 0) return uSplineElev[0].x;
  if (idx == 1) return uSplineElev[0].y;
  if (idx == 2) return uSplineElev[0].z;
  if (idx == 3) return uSplineElev[0].w;
  if (idx == 4) return uSplineElev[1].x;
  if (idx == 5) return uSplineElev[1].y;
  if (idx == 6) return uSplineElev[1].z;
  return uSplineElev[1].w;
}

float catmullRom1D(float p0, float p1, float p2, float p3, float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  return 0.5 * ((2.0 * p1) + (-p0 + p2) * t +
    (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
    (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

// Longitudinal elevation of the spline at alongT. When no control point
// carries an authored elevation this returns the caller's original
// start->end grade unchanged; with authored 3D points it smoothly
// interpolates the control heights, so dragging a point up or down in the
// viewport re-grades the river / road / canyon (or ridge crest) locally.
float splineGradeAt(float alongT, float fallbackGrade) {
  if (uSplineHasY < 0.5) return fallbackGrade;
  int n = clamp(uSplineCount, 2, 8);
  float segF = clamp(alongT, 0.0, 1.0) * float(n - 1);
  int i = min(int(segF), n - 2);
  float t = segF - float(i);
  return catmullRom1D(
    splinePointElev(max(i - 1, 0)),
    splinePointElev(i),
    splinePointElev(min(i + 1, n - 1)),
    splinePointElev(min(i + 2, n - 1)),
    t
  );
}

// Smooth Catmull-Rom distance field for the newer Spline Landform layer.
// The legacy evalSplineDist above remains untouched for spline-feature parity.
void evalSmoothSplineDist(vec2 xz, out float minDist, out float alongT) {
  minDist = 1e6;
  alongT = 0.0;
  int n = clamp(uSplineCount, 0, 8);
  if (n < 2) return;
  int segCount = n - 1;
  float invSegs = 1.0 / float(max(1, segCount));
  for (int i = 0; i < 7; i++) {
    if (i >= segCount) break;
    vec2 p0 = splineControlPoint(max(i - 1, 0));
    vec2 p1 = splineControlPoint(i);
    vec2 p2 = splineControlPoint(min(i + 1, n - 1));
    vec2 p3 = splineControlPoint(min(i + 2, n - 1));
    for (int j = 0; j < 5; j++) {
      float t0 = float(j) * 0.2;
      float t1 = float(j + 1) * 0.2;
      vec2 a = catmullRomSplinePoint(p0, p1, p2, p3, t0);
      vec2 b = catmullRomSplinePoint(p0, p1, p2, p3, t1);
      vec2 ab = b - a;
      float len2 = max(1e-4, dot(ab, ab));
      float localT = clamp(dot(xz - a, ab) / len2, 0.0, 1.0);
      vec2 proj = a + ab * localT;
      float d = length(xz - proj);
      if (d < minDist) {
        minDist = d;
        alongT = (float(i) + (float(j) + localT) * 0.2) * invSegs;
      }
    }
  }
}

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  int maxI = int(uDomain.y) - 1;
  float span = uDomain.x;
  float res = uDomain.y;
  float cellSize = span / res;
  float maxRelief = max(40.0, uDomain.z);
  int opKind = int(uDomain.w + 0.5);

  vec4 tC = texelFetch(uTerrainTex, c, 0);
  vec4 vd = texelFetch(uVecDispTex, c, 0);
  vec4 velD = texelFetch(uVelocityFieldTex, c, 0);

  vec4 tR = texelFetch(uTerrainTex, ivec2(min(maxI, c.x + 1), c.y), 0);
  vec4 tL = texelFetch(uTerrainTex, ivec2(max(0, c.x - 1), c.y), 0);
  vec4 tU = texelFetch(uTerrainTex, ivec2(c.x, min(maxI, c.y + 1)), 0);
  vec4 tD = texelFetch(uTerrainTex, ivec2(c.x, max(0, c.y - 1)), 0);

  float hC = tC.r + tC.g + tC.b;
  float hR = tR.r + tR.g + tR.b;
  float hL = tL.r + tL.g + tL.b;
  float hU = tU.r + tU.g + tU.b;
  float hD = tD.r + tD.g + tD.b;

  vec2 grad = vec2(hR - hL, hU - hD) / (2.0 * cellSize);
  float slopeTan = length(grad);
  float slopeDeg = atan(slopeTan) * 57.2957795;
  float concavity = (hR + hL + hU + hD) * 0.25 - hC;
  float flowVal = clamp(velD.w * 2.4 + tC.a * 2.5, 0.0, 1.0);

  vec2 worldXZ = (vUv - 0.5) * span;
  float maskW = evalLayerMask(vUv, worldXZ, hC, maxRelief, slopeDeg, flowVal, concavity, span, uMaskCfg1, uMaskCfg2);

  if (opKind == 1) {
    // 1. PROCEDURAL LANDFORM MODIFIER (Ridges / Mesa / Escarpment / Dunes / Craters)
    int gen = int(uParam0.x + 0.5);
    int mode = int(uParam0.y + 0.5);
    float amp = uParam0.z;
    float sc = uParam0.w;
    float sharp = uParam1.x;
    float azRad = uParam1.y * 0.01745329;
    float opacity = uParam1.z * maskW;
    float seed = uParam2.x;
    vec2 sv = vec2(fract(seed * 0.1031) * 37.0, fract(seed * 0.1733) * 53.0);

    vec2 dir = vec2(cos(azRad), sin(azRad));
    vec2 perp = vec2(-dir.y, dir.x);
    vec2 np = (worldXZ / (span * 0.5)) * sc;
    vec2 warp = vec2(fbm2D(np * 1.4 + sv, 3, 0.5), fbm2D(np * 1.4 + sv + 11.3, 3, 0.5)) - 0.5;
    vec2 wp = np + warp * 0.42;

    float shape = 0.0;
    float addFine = 0.0;
    if (gen == 0) {
      float r = ridgedFbm2D(wp * 1.6 + sv, 6, 0.58);
      shape = pow(max(1e-4, r), mix(1.8, 0.72, sharp));
    } else if (gen == 1) {
      float m = fbm2D(wp * 1.25 + sv, 5, 0.5);
      shape = safeSmoothstep(0.44 - (1.0 - sharp) * 0.18, 0.52 + (1.0 - sharp) * 0.18, m);
    } else if (gen == 2) {
      float faultCoord = dot(worldXZ / (span * 0.5), perp) + warp.x * 0.35;
      shape = safeSmoothstep(-0.22 * (1.15 - sharp), 0.22 * (1.15 - sharp), faultCoord);
    } else if (gen == 3) {
      float ph = fract(dot(worldXZ / span, dir) * sc * 3.8 + warp.y * 0.55);
      float stoss = safeSmoothstep(0.0, 0.72, ph);
      float lee = 1.0 - safeSmoothstep(0.72, 0.96, ph);
      shape = stoss * lee;
      addFine = shape * amp * 0.26 * opacity;
    } else {
      vec2 cell = floor(wp * 1.4);
      vec2 f = fract(wp * 1.4) - 0.5;
      vec2 j = (hash22(cell + sv) - 0.5) * 0.45;
      float d = length(f - j) * 2.1;
      float bowl = -(1.0 - safeSmoothstep(0.0, 0.68, d)) * (1.0 - d * d * 0.85);
      float rim = exp(-pow(max(0.0, d - 0.72) * 5.2, 2.0)) * 0.36;
      shape = bowl + rim;
    }

    float delta = shape * amp;
    float targetH = tC.r;
    if (mode == 0) targetH = tC.r + delta;
    else if (mode == 1) targetH = max(1.0, tC.r - abs(delta));
    else if (mode == 2) targetH = max(tC.r, delta + 12.0);
    else if (mode == 3) targetH = min(tC.r, max(2.0, amp));
    else targetH = delta + 10.0;

    tC.r = max(1.0, mix(tC.r, targetH, clamp(opacity, 0.0, 1.0)));
    tC.b = max(0.0, tC.b + addFine);
    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 2) {
    // 2. STRATIGRAPHIC BEDS GEOMETRIC BENCHING & CAPROCK OVERHANG
    float bedCount = max(2.0, uParam0.x);
    float terraceStr = uParam0.y;
    float dipRad = uParam0.w * 0.01745329;
    float strikeRad = uParam1.x * 0.01745329;
    float ledgeOverhang = uParam1.z;
    float opacity = uParam1.w * maskW;
    float seed = uParam2.x;

    if ((terraceStr <= 0.001 && ledgeOverhang <= 0.001) || opacity <= 0.001) {
      outTerrain = tC;
      outVecDisp = vd;
      return;
    }

    vec2 dipDir = vec2(-sin(strikeRad), cos(strikeRad));
    float warp = (fbm2D(worldXZ * 0.009 + seed * 0.013, 3, 0.5) - 0.5) * 14.0;
    float stratMeters = tC.r + dot(worldXZ, dipDir) * tan(dipRad) + warp;
    float stratCoord = stratMeters / max(30.0, maxRelief);

    float ph = stratCoord * bedCount;
    float idx = floor(ph);
    float f = fract(ph);
    float bedHard = hash12(vec2(idx, seed * 0.071));
    float stepF = mix(f, safeSmoothstep(0.18, 0.82, f), clamp(terraceStr * (0.45 + 0.55 * bedHard), 0.0, 0.96));
    float newStrat = (idx + stepF) / bedCount;
    float steppedH = newStrat * max(30.0, maxRelief) - dot(worldXZ, dipDir) * tan(dipRad) - warp;

    // Authored measured section: bench inside the actual variable-thickness
    // bed at this stratigraphic height. Hard beds retain ledges; soft beds
    // round into slopes. Outside the section, bottom/top beds clamp rather
    // than repeating the sequence.
    if (uParam2.y > 0.5) {
      // Use COMMON_GEOLOGY_GLSL's exact stratigraphic coordinate here, rather
      // than this op's legacy benching coordinate, so the geometric ledge is
      // carved inside the same bed that supplies hardness/colour/chemistry.
      vec2 commonStrike = vec2(sin(strikeRad), cos(strikeRad));
      float acrossCommon = dot(worldXZ, commonStrike);
      float warpCommon = (fbm2D(worldXZ / max(maxRelief, 1.0) * 2.2 + seed, 3, 0.55) - 0.5) * 14.0;
      float stratCommon = tC.r + acrossCommon * tan(dipRad) + warpCommon;
      int authoredIdx = strataBedIndexAt(stratCommon);
      if (authoredIdx >= 0) {
        float localM = stratCommon - uStrataColumnMeta.z;
        float bottomM = strataBedBottomM(authoredIdx);
        float thicknessM = strataBedThicknessM(authoredIdx);
        f = clamp((localM - bottomM) / max(thicknessM, 0.05), 0.0, 1.0);
        bedHard = uStrataBedsA[authoredIdx].y;
        stepF = mix(f, safeSmoothstep(0.18, 0.82, f), clamp(terraceStr * (0.35 + 0.65 * bedHard), 0.0, 0.96));
        float newStratMeters = uStrataColumnMeta.z + bottomM + stepF * thicknessM;
        steppedH = newStratMeters - acrossCommon * tan(dipRad) - warpCommon;
      }
    }

    float slopeGate = safeSmoothstep(8.0, 34.0, slopeDeg);
    tC.r = max(1.0, mix(tC.r, steppedH, clamp(opacity * terraceStr * (0.35 + 0.65 * slopeGate), 0.0, 1.0)));

    if (ledgeOverhang > 0.005 && slopeTan > 0.25) {
      vec2 outDir = -grad / max(1e-4, slopeTan);
      float capPulse = sin(f * 6.2831853) * smoothstep(0.55, 0.92, bedHard);
      float pushM = capPulse * ledgeOverhang * 4.2 * safeSmoothstep(22.0, 56.0, slopeDeg) * opacity;
      vd.xz += outDir * pushM;
      vd.y += max(0.0, capPulse) * ledgeOverhang * 0.9 * opacity;
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 3) {
    // 3. ROCK STACKS / BOULDER TORS / HOODOOS (Slate-style tiered columns + 3D overhang/lean)
    float stackH = uParam0.x;
    float spacing = max(24.0, uParam0.y);
    float baseRad = max(4.0, uParam0.z);
    float taper = uParam0.w;
    float tiers = max(2.0, uParam1.x);
    float lean = uParam1.y;
    float leanAz = uParam1.z * 0.01745329;
    float capOverhang = uParam1.w;
    float opacity = uParam2.x * maskW;
    float seed = uParam2.y;

    vec2 cellUv = (worldXZ + span * 0.5) / spacing;
    vec2 cellId = floor(cellUv);
    vec2 local = (fract(cellUv) - 0.5) * spacing;

    float bestCol = 0.0;
    vec2 bestOffset = vec2(0.0);
    float bestTierFrac = 0.0;

    for (int j = -1; j <= 1; j++) {
      for (int i = -1; i <= 1; i++) {
        vec2 cid = cellId + vec2(float(i), float(j));
        vec3 rnd = hash32(cid + vec2(seed * 0.031, seed * 0.077));
        if (rnd.z < 0.28) continue;
        vec2 center = (vec2(float(i), float(j)) + (rnd.xy - 0.5) * 0.58) * spacing;
        vec2 dVec = local - center;
        float ang = atan(dVec.y, dVec.x);
        float facet = 1.0 + 0.16 * cos(ang * 5.0 + rnd.x * 6.28) + 0.09 * sin(ang * 3.0 + rnd.y * 6.28);
        float rEff = baseRad * (0.68 + 0.55 * rnd.z) * facet;
        float rNorm = length(dVec) / max(1.0, rEff);
        if (rNorm < 1.18) {
          float profile = pow(max(1e-4, 1.0 - safeSmoothstep(0.0, 1.05, rNorm)), mix(0.45, 1.35, taper));
          float tierQuant = (floor(profile * tiers) + safeSmoothstep(0.12, 0.65, fract(profile * tiers))) / tiers;
          float colH = stackH * (0.55 + 0.60 * rnd.x) * mix(profile, tierQuant, 0.72);
          if (colH > bestCol) {
            bestCol = colH;
            bestOffset = dVec;
            bestTierFrac = clamp(colH / max(1.0, stackH), 0.0, 1.0);
          }
        }
      }
    }

    if (bestCol > 0.05) {
      tC.r += bestCol * opacity;
      vec2 leanDir = vec2(cos(leanAz), sin(leanAz));
      vec2 radDir = length(bestOffset) > 0.05 ? normalize(bestOffset) : vec2(0.0);
      float capRim = sin(bestTierFrac * tiers * 6.2831853) * 0.5 + 0.5;
      float overhangM = capOverhang * 4.6 * bestTierFrac * capRim;
      vd.xz += (leanDir * (lean * bestTierFrac * 6.5) + radDir * overhangM) * opacity;
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 4) {
    // 4. RUGGED CLIFF 3D PUSH-PULL DISPLACEMENT
    float dispAmp = uParam0.x;
    float featScale = uParam0.y;
    float slopeMinDeg = uParam0.z;
    float ledgeStr = uParam0.w;
    float overhangBias = uParam1.x;
    float vertCrease = uParam1.y;
    float opacity = uParam1.z * maskW;
    float seed = uParam1.w;

    float cliffMask = safeSmoothstep(slopeMinDeg - 4.0, slopeMinDeg + 14.0, slopeDeg) * opacity;
    if (cliffMask > 0.002 && dispAmp > 0.01) {
      vec2 outDir = slopeTan > 1e-4 ? (-grad / slopeTan) : vec2(0.0);
      vec2 np = (worldXZ / 85.0) * featScale + vec2(seed * 0.019, seed * 0.041);
      float buttress = ridgedFbm2D(np, 4, 0.56) * 2.0 - 1.0;
      float chute = pow(max(1e-4, abs(sin(dot(worldXZ, vec2(-outDir.y, outDir.x)) * 0.065 * featScale + buttress * 1.4))), 1.6);
      float ledges = sin((tC.r / 9.5) * featScale * 2.2 + buttress * 1.8);

      float heightPush = (buttress * 0.55 - (1.0 - chute) * vertCrease * 0.48 + ledges * ledgeStr * 0.35) * dispAmp;
      tC.r = max(1.0, tC.r + heightPush * cliffMask * 0.55);

      float lateralPush = (overhangBias * 0.65 + ledges * ledgeStr * 0.55 + buttress * 0.40) * dispAmp * 0.62 * cliffMask;
      vd.xz += outDir * lateralPush;
      vd.y += max(0.0, ledges) * ledgeStr * dispAmp * 0.14 * cliffMask;
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 5) {
    // 5. NON-DESTRUCTIVE GPU SCULPT LAYER
    // uParam0=(opacity, blendMode, noiseAmount, noiseScale)
    // uParam1=(targetHeight, seed, noiseHeight, multiplyStrength)
    // Blend 0 preserves the original additive sculpt path exactly when noise=0.
    float opacity = uParam0.x * maskW;
    int blendMode = int(uParam0.y + 0.5);
    float noiseAmount = clamp(uParam0.z, 0.0, 1.0);
    float noiseScale = max(0.05, uParam0.w);
    float targetHeight = uParam1.x;
    float seed = uParam1.y;
    float noiseHeight = uParam1.z;
    float multiplyStrength = clamp(uParam1.w, 0.0, 2.0);
    vec4 sc = texelFetch(uSculptTex, c, 0);
    float avgBed = (tR.r + tL.r + tU.r + tD.r) * 0.25;
    float smoothed = mix(tC.r, avgBed, clamp(sc.g, 0.0, 0.92));
    float footprint = clamp(max(sc.b, max(clamp(abs(sc.r) / 18.0, 0.0, 1.0), sc.g)), 0.0, 1.0);
    vec2 sv = vec2(fract(seed * 0.1031) * 41.0, fract(seed * 0.1733) * 57.0);
    float ridgeNoise = ridgedFbm2D(worldXZ * (0.012 * noiseScale) + sv, 4, 0.56);
    float broadNoise = fbm2D(worldXZ * (0.006 * noiseScale) + sv + 17.3, 4, 0.52);
    float noisyDelta = sc.r * (1.0 + (ridgeNoise - 0.5) * 1.6 * noiseAmount)
      + (broadNoise - 0.5) * noiseHeight * noiseAmount * footprint;
    float candidate = smoothed + noisyDelta;
    float blendW = clamp(opacity, 0.0, 1.0);
    if (blendMode == 1) {
      float normalized = clamp(noisyDelta / 45.0, -1.0, 1.0);
      float heightScale = max(0.05, 1.0 + normalized * multiplyStrength);
      candidate = smoothed * heightScale + (broadNoise - 0.5) * noiseHeight * 0.35 * noiseAmount * footprint;
      blendW = clamp(opacity * footprint, 0.0, 1.0);
    } else if (blendMode == 2) {
      candidate = targetHeight + noisyDelta;
      blendW = clamp(opacity * footprint, 0.0, 1.0);
    } else if (blendMode == 3) {
      float sculptSurface = targetHeight + noisyDelta;
      candidate = sc.r >= 0.0 ? max(smoothed, sculptSurface) : min(smoothed, sculptSurface);
      blendW = clamp(opacity * footprint, 0.0, 1.0);
    }
    tC.r = max(1.0, mix(tC.r, candidate, blendW));
    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 6) {
    // 6. SPATIAL SPLINE FEATURE (0=Canyon Gorge Cut, 1=Graded Road, 2=Hydrostatic Lake Basin)
    int mode = int(uParam0.x + 0.5);
    float width = max(6.0, uParam0.y);
    float depth = uParam0.z;
    float steep = uParam0.w;
    float meander = uParam1.x;
    float waterFill = uParam1.y;
    float bankFall = max(0.1, uParam1.z);
    float opacity = uParam1.w * maskW;

    float dSpline;
    float alongT;
    evalSplineDist(worldXZ, dSpline, alongT);

    float wallWarp = (fbm2D(worldXZ * 0.032, 3, 0.52) - 0.5) * width * 0.55 * meander;
    float dWarped = max(0.0, dSpline + wallWarp);
    float halfW = width * 0.5;

    if (mode == 0) {
      // Canyon Gorge Cut + optional river channel water
      float inner = 1.0 - safeSmoothstep(halfW * (0.25 + 0.55 * steep), halfW * (1.0 + bankFall), dWarped);
      float cut = depth * pow(max(1e-4, inner), mix(1.6, 0.65, steep));
      tC.r = max(1.5, tC.r - cut * opacity);
      if (waterFill > 0.01 && dWarped < halfW * 0.72) {
        float wDepth = waterFill * depth * 0.22 * (1.0 - safeSmoothstep(0.0, halfW * 0.72, dWarped));
        tC.a = max(tC.a, wDepth * opacity);
      }
    } else if (mode == 1) {
      // Graded Mountain Road / Bench Terrace
      float roadMask = 1.0 - safeSmoothstep(halfW * 0.65, halfW * (1.0 + bankFall), dWarped);
      float gradeElev = splineGradeAt(alongT, mix(maxRelief * 0.24, maxRelief * 0.56, alongT));
      tC.r = max(1.5, mix(tC.r, gradeElev, roadMask * opacity));
      tC.g *= (1.0 - roadMask * 0.85 * opacity);
    } else {
      // Hydrostatic Lake Basin Excavation
      float basinMask = 1.0 - safeSmoothstep(halfW * 0.45, halfW * (1.15 + bankFall), dWarped);
      float targetFloor = max(2.0, tC.r - depth * basinMask);
      tC.r = mix(tC.r, targetFloor, opacity);
      if (waterFill > 0.01 && basinMask > 0.05) {
        float waterSurf = targetFloor + depth * waterFill * 0.65;
        tC.a = max(tC.a, max(0.0, waterSurf - (tC.r + tC.g + tC.b)) * basinMask * opacity);
      }
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }


  if (opKind == 7) {
    // 7. PARAMETERIZED SPLINE LANDFORM
    // Modes: 0=Mountain Ridge Add, 1=Canyon Subtract, 2=River Channel,
    //        3=Road Cut/Grade, 4=SDF Tunnel (CPU SDF only; no height op)
    // uParam0=(mode,width,amount,falloff)
    // uParam1=(sharpness,noiseAmount,noiseScale,waterDepth)
    // uParam2=(startElevation,endElevation,seed,opacity)
    // uParam3=(sediment,flattenStrength,bankHeight,reserved)
    int mode = int(uParam0.x + 0.5);
    if (mode == 4) {
      outTerrain = tC;
      outVecDisp = vd;
      return;
    }
    float width = max(4.0, uParam0.y);
    float amount = max(0.0, uParam0.z);
    float falloff = clamp(uParam0.w, 0.0, 1.0);
    float sharpness = clamp(uParam1.x, 0.0, 1.0);
    float noiseAmount = clamp(uParam1.y, 0.0, 1.0);
    float noiseScale = max(0.05, uParam1.z);
    float waterDepth = max(0.0, uParam1.w);
    float startElevation = uParam2.x;
    float endElevation = uParam2.y;
    float seed = uParam2.z;
    float opacity = clamp(uParam2.w * maskW, 0.0, 1.0);
    float sediment = clamp(uParam3.x, 0.0, 1.0);
    float flattenStrength = clamp(uParam3.y, 0.0, 1.0);
    float bankHeight = max(0.0, uParam3.z);

    float dSpline;
    float alongT;
    evalSmoothSplineDist(worldXZ, dSpline, alongT);
    vec2 sv = vec2(fract(seed * 0.1031) * 43.0, fract(seed * 0.1733) * 59.0);
    float wallWarp = (fbm2D(worldXZ * (0.018 * noiseScale) + sv, 3, 0.52) - 0.5)
      * width * 0.48 * noiseAmount;
    float d = max(0.0, dSpline + wallWarp);
    float halfW = width * 0.5;
    float core = 1.0 - safeSmoothstep(
      halfW * (0.18 + 0.62 * sharpness),
      halfW * (0.75 + 0.85 * falloff),
      d
    );
    float profile = pow(max(1e-4, core), mix(1.7, 0.62, sharpness));
    float bankBand = exp(-pow(max(0.0, d - halfW * 0.72) / (halfW * 0.42 + 1.0), 2.0))
      * safeSmoothstep(halfW * 0.34, halfW * 0.72, d) * (1.0 - profile * 0.45);
    float ridgeNoise = ridgedFbm2D(worldXZ * (0.011 * noiseScale) + sv, 5, 0.56);
    float broadNoise = fbm2D(worldXZ * (0.0055 * noiseScale) + sv + 23.7, 4, 0.52);
    float noiseMod = clamp(1.0 + (ridgeNoise - 0.5) * 1.7 * noiseAmount, 0.18, 1.9);
    float grade = splineGradeAt(alongT, mix(startElevation, endElevation, alongT));

    if (mode == 0) {
      // Additive mountain ridge: broad noisy uplift plus a sharper crest.
      // With authored 3D point elevations the crest is instead pulled toward
      // the spline's height profile (raise-only, so it still reads as an
      // added mountain rather than a levelling pass).
      float lift = amount * profile * noiseMod;
      float crest = pow(profile, 3.0) * amount * 0.18 * noiseMod;
      if (uSplineHasY > 0.5) {
        float crestTarget = grade + crest + (ridgeNoise - 0.5) * amount * 0.16 * noiseAmount;
        float raised = mix(tC.r, crestTarget, pow(profile, 1.25));
        tC.r = max(1.0, mix(tC.r, max(tC.r, raised), opacity) + bankBand * bankHeight * 0.35 * opacity);
      } else {
        tC.r = max(1.0, tC.r + (lift + crest + bankBand * bankHeight * 0.35) * opacity);
      }
      tC.b = max(0.0, tC.b - lift * 0.025 * sediment * opacity);
      tC.g = max(0.0, tC.g + profile * (1.0 - profile) * sediment * 0.18 * opacity);
    } else if (mode == 1) {
      // Subtractive canyon. The optional longitudinal grade can guide the
      // floor downstream, but min() guarantees this mode never fills terrain.
      float noisyGrade = max(2.0, grade + (broadNoise - 0.5) * amount * 0.10 * noiseAmount);
      float cutH = max(2.0, tC.r - amount * profile * noiseMod);
      float guidedH = mix(cutH, min(cutH, noisyGrade), pow(profile, 1.4) * flattenStrength);
      tC.r = max(1.5, mix(tC.r, guidedH, opacity) + bankBand * bankHeight * opacity);
      tC.g = max(0.0, tC.g * (1.0 - profile * 0.72 * opacity));
      tC.b = max(0.0, tC.b * (1.0 - profile * 0.45 * opacity) + profile * sediment * 0.22 * opacity);
    } else if (mode == 2) {
      // Directed river: carve toward a start->end water/bed grade so the
      // channel has a continuous downstream direction chosen by the author.
      float channelHalf = halfW * 0.48;
      float channel = 1.0 - safeSmoothstep(channelHalf * 0.35, channelHalf * (1.0 + 0.70 * falloff), d);
      float noisyGrade = grade + (broadNoise - 0.5) * 0.8 * noiseAmount;
      float carved = max(1.5, tC.r - amount * pow(profile, 1.25) * noiseMod);
      float gradeBlend = flattenStrength * max(pow(profile, 1.15), pow(channel, 0.65));
      float target = mix(carved, noisyGrade, gradeBlend);
      tC.r = max(1.5, mix(tC.r, target, opacity) + bankBand * bankHeight * opacity);
      tC.g = max(0.0, tC.g * (1.0 - channel * 0.65 * opacity));
      tC.b = max(0.0, tC.b + channel * sediment * 0.20 * opacity);
      float totalH = tC.r + tC.g + tC.b;
      tC.a = max(tC.a, max(0.0, noisyGrade + waterDepth - totalH) * channel * opacity);
    } else if (mode == 3) {
      // Road corridor: cut or fill toward the authored longitudinal grade,
      // capped by Amount so a road can bench through a hillside without
      // flattening the entire mountain. Tunnels use mode 4 / the SDF volume.
      float roadHalf = halfW * 0.62;
      float roadMask = 1.0 - safeSmoothstep(roadHalf * 0.55, roadHalf * (1.0 + 0.65 * falloff), d);
      float roadGrade = grade + (broadNoise - 0.5) * 0.7 * noiseAmount;
      float limitedDelta = clamp(roadGrade - tC.r, -amount, amount);
      float target = tC.r + limitedDelta;
      tC.r = max(1.5, mix(tC.r, target, roadMask * flattenStrength * opacity)
        + bankBand * bankHeight * 0.45 * opacity);
      tC.g = max(0.0, tC.g * (1.0 - roadMask * 0.80 * opacity));
      tC.b = max(0.0, tC.b * (1.0 - roadMask * 0.55 * opacity) + roadMask * sediment * 0.10 * opacity);
      tC.a = max(0.0, tC.a * (1.0 - roadMask * 0.90 * opacity));
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  outTerrain = tC;
  outVecDisp = vd;
}
`;

// ============================================================================
// 2. NON-DESTRUCTIVE GPU SCULPT BRUSH STAMP
// ============================================================================

export const SCULPT_STAMP_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

in vec2 vUv;
layout(location = 0) out vec4 outSculpt; // (deltaH, smoothWeight, paintedFootprint, 0)

uniform sampler2D uSculptTex;
uniform vec4 uBrushPosRad; // (worldX, worldZ, radius, worldSpan)
uniform vec4 uBrushOpStr;  // (opCode: 0=add, 1=remove, 2=smooth, strength, 0, 0)

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  vec4 cur = texelFetch(uSculptTex, c, 0);
  vec2 worldXZ = (vUv - 0.5) * uBrushPosRad.w;
  float r = max(2.0, uBrushPosRad.z);
  float d = length(worldXZ - uBrushPosRad.xy);
  if (d >= r) {
    outSculpt = cur;
    return;
  }
  float t = 1.0 - d / r;
  float falloff = t * t * (3.0 - 2.0 * t);
  int op = int(uBrushOpStr.x + 0.5);
  float str = uBrushOpStr.y;

  cur.b = clamp(max(cur.b, falloff), 0.0, 1.0);
  if (op == 0) {
    cur.r = clamp(cur.r + falloff * str * 2.2, -160.0, 160.0);
  } else if (op == 1) {
    cur.r = clamp(cur.r - falloff * str * 2.2, -160.0, 160.0);
  } else {
    cur.r = mix(cur.r, 0.0, falloff * str * 0.25);
    cur.g = clamp(cur.g + falloff * str * 0.22, 0.0, 0.95);
  }
  outSculpt = cur;
}
`;

// ============================================================================
// 3. B&W SPATIAL MASK BAKE & MASKED SIMULATION BLEND SHADERS
// ============================================================================

export const STACK_MASK_BAKE_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

in vec2 vUv;
layout(location = 0) out vec4 outMask;

uniform sampler2D uTerrainTex;
uniform sampler2D uVelocityFieldTex;
uniform vec4 uBakeDomain; // (worldSpan, gridRes, maxRelief, 0)
uniform vec4 uMaskCfg1;
uniform vec4 uMaskCfg2;

${COMMON_GEOLOGY_GLSL}
${PIPELINE_MASK_GLSL}

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  int maxI = int(uBakeDomain.y) - 1;
  float span = uBakeDomain.x;
  float cellSize = span / uBakeDomain.y;

  vec4 tC = texelFetch(uTerrainTex, c, 0);
  vec4 tR = texelFetch(uTerrainTex, ivec2(min(maxI, c.x + 1), c.y), 0);
  vec4 tL = texelFetch(uTerrainTex, ivec2(max(0, c.x - 1), c.y), 0);
  vec4 tU = texelFetch(uTerrainTex, ivec2(c.x, min(maxI, c.y + 1)), 0);
  vec4 tD = texelFetch(uTerrainTex, ivec2(c.x, max(0, c.y - 1)), 0);
  vec4 velD = texelFetch(uVelocityFieldTex, c, 0);

  float hC = tC.r + tC.g + tC.b;
  float hR = tR.r + tR.g + tR.b;
  float hL = tL.r + tL.g + tL.b;
  float hU = tU.r + tU.g + tU.b;
  float hD = tD.r + tD.g + tD.b;

  vec2 grad = vec2(hR - hL, hU - hD) / (2.0 * cellSize);
  float slopeDeg = atan(length(grad)) * 57.2957795;
  float concavity = (hR + hL + hU + hD) * 0.25 - hC;
  float flowVal = clamp(velD.w * 2.4 + tC.a * 2.5, 0.0, 1.0);
  vec2 worldXZ = (vUv - 0.5) * span;

  float m = evalLayerMask(vUv, worldXZ, hC, uBakeDomain.z, slopeDeg, flowVal, concavity, span, uMaskCfg1, uMaskCfg2);
  outMask = vec4(m, m, m, 1.0);
}
`;

export const STACK_MASK_BLEND_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

layout(location = 0) out vec4 outTex;

uniform sampler2D uPrevTex;
uniform sampler2D uNextTex;
uniform sampler2D uMaskTex;

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  vec4 prevV = texelFetch(uPrevTex, c, 0);
  vec4 nextV = texelFetch(uNextTex, c, 0);
  float m = clamp(texelFetch(uMaskTex, c, 0).r, 0.0, 1.0);
  outTex = mix(prevV, nextV, m);
}
`;

export const STACK_COPY_VEC_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
layout(location = 0) out vec4 outVec;
uniform sampler2D uSrcTex;
void main() {
  outVec = texelFetch(uSrcTex, ivec2(gl_FragCoord.xy), 0);
}
`;

// ============================================================================
// 4. 3D VOLUMETRIC SDF CAVE & CLIFF-UNDERCUT MESH SHADER
// ============================================================================

export const SDF_CAVE_VERT = `#version 300 es
${GLSL_PRECISION_HEADER}

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aAttribs; // (ao, strataPhase, hardness, moisture)

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vAttribs;

void main() {
  vWorldPos = aPos;
  vNormal = aNormal;
  vAttribs = aAttribs;
  gl_Position = uViewProj * vec4(aPos, 1.0);
}
`;

export const SDF_CAVE_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vAttribs;

layout(location = 0) out vec4 outColor;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform vec4 uRenderDomain; // (worldSpan, gridRes, 1.0, maxRelief)
uniform ivec2 uShadeConfig; // (materialTheme, viewMode)

${COMMON_GEOLOGY_GLSL}

void main() {
  vec3 N = safeNorm3(vNormal, vec3(0.0, 1.0, 0.0));
  vec3 L = safeNorm3(uSunDir, vec3(0.4, 0.8, 0.4));
  vec3 V = safeNorm3(uCameraPos - vWorldPos, vec3(0.0, 1.0, 0.0));

  float ao = clamp(vAttribs.x, 0.10, 1.0);
  float strata = vAttribs.y;
  float hardness = vAttribs.z;
  int theme = uShadeConfig.x;
  int viewMode = uShadeConfig.y;

  vec3 rockDark = vec3(0.14, 0.13, 0.12);
  vec3 rockLight = vec3(0.36, 0.33, 0.30);
  if (theme == 0 || theme == 3) {
    rockDark = vec3(0.28, 0.12, 0.08);
    rockLight = vec3(0.62, 0.34, 0.21);
  } else if (theme == 1 || theme == 4 || theme == 8) {
    rockDark = vec3(0.09, 0.095, 0.105);
    rockLight = vec3(0.24, 0.25, 0.27);
  } else if (theme == 5 || theme == 6 || theme == 9) {
    rockDark = vec3(0.30, 0.22, 0.15);
    rockLight = vec3(0.64, 0.50, 0.36);
  }

  float bedBand = 0.5 + 0.5 * sin(strata * 6.2831853 * 2.0);
  vec3 albedo = mix(rockDark, rockLight, clamp(hardness * 0.65 + bedBand * 0.35, 0.0, 1.0));
  if (viewMode == 4) albedo = vec3(0.28, 0.27, 0.26);
  else if (viewMode == 2) albedo = mix(vec3(0.07, 0.22, 0.36), vec3(0.72, 0.44, 0.12), hardness);

  float portalBounce = clamp(ao * 1.15 - 0.12, 0.04, 1.0);
  float ndotl = max(0.0, dot(N, L)) * portalBounce * 0.65;
  vec3 ambient = mix(vec3(0.04, 0.045, 0.055), vec3(0.18, 0.22, 0.28), 0.5 + 0.5 * N.y) * ao;
  float rim = pow(max(1e-4, 1.0 - max(0.0, dot(N, V))), 3.0) * ao * 0.12;

  vec3 lit = albedo * (vec3(2.2, 2.05, 1.88) * ndotl + ambient) + vec3(0.22, 0.26, 0.32) * rim;
  vec3 x = max(vec3(0.0), lit);
  vec3 mapped = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
  outColor = vec4(pow(max(vec3(1e-6), clamp(mapped, 0.0, 1.0)), vec3(1.0 / 2.2)), 1.0);
}
`;
