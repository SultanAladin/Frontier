/*==========================================================================================================================================
                                                          OCEANSURFACE.TEST.JS
==========================================================================================================================================*/
// 🧩 Spectral diversity, analytic slope accuracy, absent coordinate wrapping, and distant-wave antialiasing regressions.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { OceanSpectrum, EvaluateOcean, OceanShader, OceanVertexShader, OceanPhaseParameters, PrepareOceanSpectrum, OceanDetailShader } from './OceanSurface.js';

test('ocean contains 24 distinct gravity-wave components with dispersed directions', () =>
{
    Assert.equal(OceanSpectrum.length, 24);
    Assert.equal(new Set(OceanSpectrum.map(Wave => Wave.κ)).size, 24);
    Assert.equal(new Set(OceanSpectrum.map(Wave => Wave.Direction.join(','))).size, 24);
    for (const Wave of OceanSpectrum)
    {
        Assert.ok(Math.abs(Math.hypot(...Wave.Direction) - 1) < 1e-12);
        Assert.ok(Wave.Amplitude > 0 && Wave.κ > 0);
        Assert.ok(Math.abs(Wave.ω ** 2 - 9.81 * Wave.κ) < 1e-12);
    }
});

test('analytic ocean slopes agree with the actual warped surface derivatives', () =>
{
    const ε = .001;
    for (let Index = 0; Index < 100; Index++)
    {
        const X = Math.sin(Index * .7) * 3000, Z = Math.cos(Index * .37) * 2000, Seconds = Index * .43;
        const Evaluation = EvaluateOcean(X, Z, Seconds, 1.3, 2);
        const SlopeX = (EvaluateOcean(X + ε, Z, Seconds, 1.3, 2).Height - EvaluateOcean(X - ε, Z, Seconds, 1.3, 2).Height) / (2 * ε);
        const SlopeZ = (EvaluateOcean(X, Z + ε, Seconds, 1.3, 2).Height - EvaluateOcean(X, Z - ε, Seconds, 1.3, 2).Height) / (2 * ε);
        Assert.ok(Math.abs(Evaluation.SlopeX - SlopeX) < 1e-7);
        Assert.ok(Math.abs(Evaluation.SlopeZ - SlopeZ) < 1e-7);
    }
});

test('world-space ocean does not repeat at the previous visually plausible texture-tile offsets', () =>
{
    for (const Offset of [64, 128, 256, 512, 1024, 2048, 4096])
    for (const Axis of ['X', 'Z'])
    {
        let SquaredDifference = 0;
        for (let Index = 0; Index < 80; Index++)
        {
            const X = Index * 73.391 - 3000, Z = Math.cos(Index * .37) * 900;
            const Original = EvaluateOcean(X, Z, 17).Height;
            const Translated = EvaluateOcean(X + (Axis === 'X' ? Offset : 0), Z + (Axis === 'Z' ? Offset : 0), 17).Height;
            SquaredDifference += (Original - Translated) ** 2;
        }
        Assert.ok(Math.sqrt(SquaredDifference / 80) > .15, `${Axis}/${Offset} repeated`);
    }
    Assert.doesNotMatch(OceanShader, /texture\s*\(|texture2D|fract\s*\(|mod\s*\(/);
});

test('pixel footprints suppress unresolved short-wave slopes, not just their reflected highlights', () =>
{
    let NearEnergy = 0, FarEnergy = 0;
    for (let Index = 0; Index < 500; Index++)
    {
        const Near = EvaluateOcean(Index * 17.19, Index * 8.47, 11, 1, 0);
        const Far = EvaluateOcean(Index * 17.19, Index * 8.47, 11, 1, 30);
        NearEnergy += Near.SlopeX ** 2 + Near.SlopeZ ** 2;
        FarEnergy += Far.SlopeX ** 2 + Far.SlopeZ ** 2;
    }
    Assert.ok(FarEnergy < NearEnergy * .12, `${FarEnergy}/${NearEnergy}`);
});

test('zero amplitude is flat, amplitude is linear, and animation does not reset the world coordinates', () =>
{
    Assert.deepEqual(EvaluateOcean(-12345, 7733, 40, 0), { Height: 0, SlopeX: 0, SlopeZ: 0 });
    for (const Coordinate of [0, -273.1, 1e6])
    {
        const Unit = EvaluateOcean(Coordinate, Coordinate * .71, 9);
        const Double = EvaluateOcean(Coordinate, Coordinate * .71, 9, 2);
        Assert.equal(Double.Height, Unit.Height * 2);
        Assert.equal(Double.SlopeX, Unit.SlopeX * 2);
        Assert.equal(Double.SlopeZ, Unit.SlopeZ * 2);
        Assert.ok(Object.values(Unit).every(Number.isFinite));
        Assert.notEqual(Unit.Height, EvaluateOcean(Coordinate, Coordinate * .71, 10).Height);
    }
});

test('compact GPU spectrum has 24 finite filtered packets and immutable frequency/phase pairs', () =>
{
    Assert.equal(OceanPhaseParameters.length, 48);
    const Near = PrepareOceanSpectrum(1, 0), Far = PrepareOceanSpectrum(1, 30), Flat = PrepareOceanSpectrum(0, 0);
    Assert.equal(Near.length, 96);
    for (let Index = 0; Index < 24; Index++)
    {
        Assert.ok(Near.subarray(Index * 4, Index * 4 + 4).every(Number.isFinite));
        Assert.equal(Flat[Index * 4 + 3], 0);
        Assert.ok(Far[Index * 4 + 3] <= Near[Index * 4 + 3]);
        Assert.ok(Math.abs(OceanPhaseParameters[Index * 2] - OceanSpectrum[Index].ω) < 1e-6);
    }
    Assert.ok(OceanVertexShader.length < 1500, 'do not expand the full spectrum into a large driver program');
    Assert.doesNotMatch(OceanVertexShader, /texture\s*\(|texture2D|fract\s*\(|mod\s*\(/);
});

test('live wave displacement and world-grid scale never depend on camera orbit or pixel footprint', () =>
{
    const Source = readFileSync(new URL('./VolumeRasterization.js', import.meta.url), 'utf8');
    Assert.match(Source, /PrepareOceanSpectrum\(LatestView\.Wave, 0\)/);
    Assert.match(Source, /OceanExtent'\), OceanWorldExtent/);
    Assert.doesNotMatch(Source, /OceanExtent = .*Orbit|PrepareOceanSpectrum\([^;]*OceanExtent/);
    Assert.match(OceanDetailShader, /OceanDetailSlope/);
    Assert.doesNotMatch(OceanDetailShader, /for\s*\(|while\s*\(|OceanVertexEvaluation/);
    Assert.ok(OceanDetailShader.length < 4000, 'four resolved slope bands, not a 24-wave pixel loop');
});
