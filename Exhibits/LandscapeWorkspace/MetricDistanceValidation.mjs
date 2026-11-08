/*==========================================================================================================================================
                                                     METRICDISTANCEVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Independent analytic solids, distance/gradient errors, brick seams, anisotropy and a fixed-domain grid-refinement study.

import Assert from 'node:assert/strict';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { SampleSparseVolume, EncodeSampledVolume, DecodeSparseVolume } from '../../Experimental/LandscapeWorkspace/SparseVolume.js';
import { ReinitializeSparseVolume } from '../../Experimental/LandscapeWorkspace/DistanceReinitialization.js';

const Directory = fileURLToPath(new URL('./Results/', import.meta.url));
await mkdir(Directory, { recursive: true });
const Results = {
    Status: 'running', Date: new Date().toISOString(), Runtime: process.version, Platform: process.platform,
    Revision: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
    Note: 'Local uncommitted metric-export continuation. Small dense arrays below are independent measurement-only readbacks, never terrain allocation.',
    Fixtures: [], Refinement: [], SourceSHA256: {}
};
for (const Name of ['SparseVolume.js', 'DistanceReinitialization.js'])
{
    Results.SourceSHA256[Name] = createHash('sha256').update(await readFile(new URL('../../Experimental/LandscapeWorkspace/' + Name, import.meta.url))).digest('hex');
}

function ConstructSpecification(Dimensions, Pitch, Band = Math.max(...Pitch) * 2)
{
    return {
        Format: 'FrontierSparseTSDF/1', Dimensions, Pitch, Band, BrickWidth: 8,
        Origin: Dimensions.map((Count, Axis) => -Count * Pitch[Axis] / 2)
    };
}

function DenseSamples(Volume)
{
    const [Width, Height, Depth] = Volume.Specification.Dimensions;
    const Samples = new Float32Array(Width * Height * Depth);
    for (const Leaf of Volume.Leaves)
    for (let Z = 0; Z < Leaf.Width; Z++)
    for (let Y = 0; Y < Leaf.Width; Y++)
    for (let X = 0; X < Leaf.Width; X++)
    {
        Samples[((Leaf.Z + Z) * Height + Leaf.Y + Y) * Width + Leaf.X + X] = Leaf.Category === 1 ? Leaf.Samples[0] : Leaf.Samples[(Z * Leaf.Width + Y) * Leaf.Width + X];
    }
    return Samples;
}

function Measure(Specification, Source, Metric, Analytic, Margin)
{
    const { Dimensions, Origin, Pitch, Band } = Specification;
    const [Width, Height, Depth] = Dimensions;
    const Offsets = [1, Width, Width * Height];
    let MaximumErrorCell = null;
    let Count = 0, SourceSum = 0, MetricSum = 0, MaximumError = 0, SeamMaximum = 0, SignChanges = 0;
    let GradientCount = 0, SourceGradient = 0, MetricGradient = 0, SourceGradientError = 0, MetricGradientError = 0;
    let CommonBandCount = 0, CommonBandError = 0;
    for (let Z = Margin; Z < Depth - Margin; Z++)
    for (let Y = Margin; Y < Height - Margin; Y++)
    for (let X = Margin; X < Width - Margin; X++)
    {
        const Index = (Z * Height + Y) * Width + X;
        const Expected = Analytic(Origin[0] + (X + .5) * Pitch[0], Origin[1] + (Y + .5) * Pitch[1], Origin[2] + (Z + .5) * Pitch[2]);
        const Truncated = Math.max(-Band, Math.min(Band, Expected));
        const Error = Math.abs(Metric[Index] - Truncated);
        if (Math.sign(Source[Index]) !== Math.sign(Metric[Index])) SignChanges++;
        Count++;
        SourceSum += Math.abs(Source[Index] - Truncated);
        MetricSum += Error;
        if (Error > MaximumError) { MaximumError = Error; MaximumErrorCell = [X, Y, Z]; }
        if ([X, Y, Z].some(Coordinate => Coordinate % 8 === 0 || Coordinate % 8 === 7)) SeamMaximum = Math.max(SeamMaximum, Error);
        if (Math.abs(Expected) < .8)
        {
            CommonBandCount++;
            CommonBandError += Error;
        }
        if (Math.abs(Expected) >= Band - Math.max(...Pitch) || Offsets.some(Offset => Math.abs(Metric[Index - Offset]) >= Band * .999 || Math.abs(Metric[Index + Offset]) >= Band * .999)) continue;
        const SourceNorm = Math.hypot(...Offsets.map((Offset, Axis) => (Source[Index + Offset] - Source[Index - Offset]) / (2 * Pitch[Axis])));
        const MetricNorm = Math.hypot(...Offsets.map((Offset, Axis) => (Metric[Index + Offset] - Metric[Index - Offset]) / (2 * Pitch[Axis])));
        GradientCount++;
        SourceGradient += SourceNorm;
        MetricGradient += MetricNorm;
        SourceGradientError += Math.abs(SourceNorm - 1);
        MetricGradientError += Math.abs(MetricNorm - 1);
    }
    Assert.ok(Count > 1000 && GradientCount > 100);
    return {
        Count, SignChanges, SourceMeanAbsoluteErrorMetres: SourceSum / Count, MetricMeanAbsoluteErrorMetres: MetricSum / Count,
        MaximumErrorMetres: MaximumError, MaximumErrorCell, BrickSeamMaximumErrorMetres: SeamMaximum,
        GradientCount, SourceMeanGradientMagnitude: SourceGradient / GradientCount, MetricMeanGradientMagnitude: MetricGradient / GradientCount,
        SourceMeanGradientResidual: SourceGradientError / GradientCount, MetricMeanGradientResidual: MetricGradientError / GradientCount,
        CommonBandCount, CommonBandMeanErrorMetres: CommonBandError / CommonBandCount
    };
}

function EvaluateFixture(Name, Specification, Scalar, Analytic, MaximumError, Margin = 2)
{
    const Started = performance.now();
    const Volume = { Specification, Leaves: [] };
    for (const Entry of SampleSparseVolume(Specification, undefined, Scalar)) if (Entry.Leaf) Volume.Leaves.push(Entry.Leaf);
    const Source = DenseSamples(Volume);
    for (const Progress of ReinitializeSparseVolume(Volume, { Exterior: Scalar, Surface: Scalar })) Assert.ok(Progress.Fraction >= 0 && Progress.Fraction <= 1);
    const Parts = [...EncodeSampledVolume(Volume)].map(Entry => Entry.Bytes);
    const Encoded = Buffer.concat(Parts);
    const Decoded = DecodeSparseVolume(Encoded);
    const Metric = DenseSamples(Decoded);
    const Statistics = Measure(Specification, Source, Metric, Analytic, Margin);
    Assert.equal(Statistics.SignChanges, 0);
    Assert.ok(Statistics.MaximumErrorMetres <= MaximumError, Name + ': ' + JSON.stringify(Statistics));
    Assert.ok(Statistics.MetricMeanGradientResidual < Statistics.SourceMeanGradientResidual, Name + ': gradient residual did not improve');
    const Result = {
        Name, Dimensions: Specification.Dimensions, PitchMetres: Specification.Pitch, BandMetres: Specification.Band,
        MaximumAcceptedErrorMetres: MaximumError, ExcludedBoundaryCells: Margin, ...Statistics, Redistance: Volume.Specification.Redistance,
        EncodedBytes: Encoded.length, ElapsedSeconds: (performance.now() - Started) / 1000
    };
    console.log(`${Name}: max ${Statistics.MaximumErrorMetres.toPrecision(5)} m; mean |∇d| ${Statistics.MetricMeanGradientMagnitude.toPrecision(5)}`);
    return { Result, Source, Metric, Specification, Analytic };
}

const Plane = (X, Y) => Y - .173;
const Oblique = (X, Y, Z) => (.3 * X + .8 * Y - .4 * Z) / Math.hypot(.3, .8, .4) - .173;
const Sphere = (X, Y, Z) => Math.hypot(X, Y, Z) - 2.4;
const QuadraticSphere = (X, Y, Z) => (X * X + Y * Y + Z * Z - 2.4 ** 2) * .025;
const Torus = (X, Y, Z) => Math.hypot(Math.hypot(X, Z) - 2.1, Y) - .8;
const Box = (X, Y, Z) =>
{
    const Q = [Math.abs(X) - 4, Math.abs(Y) - 5.4, Math.abs(Z) - 4];
    return Math.hypot(...Q.map(Coordinate => Math.max(Coordinate, 0))) + Math.min(Math.max(...Q), 0);
};

function ConstructChart(Fixture)
{
    const { Source, Metric, Specification, Analytic } = Fixture;
    const { Dimensions: [Width, Height, Depth], Origin, Pitch, Band } = Specification;
    const Y = Math.floor(Height / 2), Z = Math.floor(Depth / 2);
    const Observations = Array.from({ length: Width }, (_, X) =>
    {
        const Position = [X, Y, Z].map((Coordinate, Axis) => Origin[Axis] + (Coordinate + .5) * Pitch[Axis]);
        const Index = (Z * Height + Y) * Width + X;
        return { X: Position[0], Source: Source[Index], Metric: Metric[Index], Expected: Math.max(-Band, Math.min(Band, Analytic(...Position))) };
    });
    const Lines = ['Expected', 'Metric', 'Source'].map((Category, Index) =>
    {
        const Points = Observations.map((Sample, X) => `${70 + X / (Width - 1) * 1010},${178 - Sample[Category] / Band * 105}`).join(' ');
        return `<polyline points="${Points}" fill="none" stroke="${['#e5e5e5', '#8fbab1', '#a99072'][Index]}" stroke-width="${Index === 0 ? 4 : 2}" ${Index === 2 ? 'stroke-dasharray="6 5"' : ''}/>`;
    }).join('');
    return {
        Observations,
        SVG: `<svg viewBox="0 0 1150 340" role="img" aria-label="Measured sphere cross section before and after metric redistancing"><path d="M70 73V283H1080M70 178H1080" fill="none" stroke="#3d3d3d"/>${Lines}<g fill="#989898" font-size="13"><text x="18" y="78">+${Band} m</text><text x="40" y="183">0</text><text x="22" y="287">−${Band} m</text><text x="70" y="314">${Observations[0].X} m</text><text x="1030" y="314">${Observations.at(-1).X} m</text><text x="440" y="335">World X · fixed cell-centre Y/Z slice</text></g></svg>`
    };
}

try
{
    for (const [Name, Specification, Scalar, Analytic, Limit, Margin] of [
        ['Scaled plane', ConstructSpecification([48, 48, 48], [.25, .25, .25]), (X, Y) => Plane(X, Y) * .005, Plane, 2e-6],
        ['Oblique plane, sign-dependent scaling', ConstructSpecification([48, 48, 48], [.25, .25, .25]), (X, Y, Z) => Oblique(X, Y, Z) * (Oblique(X, Y, Z) > 0 ? .2 : .025), Oblique, .002],
        ['Quadratic sphere', ConstructSpecification([48, 48, 48], [.25, .25, .25]), QuadraticSphere, Sphere, .125],
        ['Anisotropic sphere', ConstructSpecification([48, 24, 32], [.25, .6, .35]), QuadraticSphere, Sphere, .3],
        ['Torus', ConstructSpecification([48, 48, 48], [.25, .25, .25]), (X, Y, Z) => Torus(X, Y, Z) * .04, Torus, .15],
        ['Allocated box with analytic exterior', ConstructSpecification([32, 24, 32], [.25, .45, .25]), (X, Y, Z) => Box(X, Y, Z) * .03, Box, .225, 1]
    ])
    {
        const Fixture = EvaluateFixture(Name, Specification, Scalar, Analytic, Limit, Margin);
        Results.Fixtures.push(Fixture.Result);
    }
    let Chart;
    for (const Count of [24, 48, 96])
    {
        const Pitch = 12 / Count;
        const Fixture = EvaluateFixture(`Sphere refinement ${Count}³`, ConstructSpecification([Count, Count, Count], [Pitch, Pitch, Pitch], 1), QuadraticSphere, Sphere, Pitch * .75);
        Results.Refinement.push(Fixture.Result);
        if (Count === 48) Chart = ConstructChart(Fixture);
    }
    for (let Index = 1; Index < Results.Refinement.length; Index++) Assert.ok(
        Results.Refinement[Index].CommonBandMeanErrorMetres < Results.Refinement[Index - 1].CommonBandMeanErrorMetres,
        'Mean distance error must decrease when pitch halves on the same domain and physical band');
    Results.ChartObservations = Chart.Observations;
    Results.Status = 'passed';
    const Font = (await readFile(new URL('../../Experimental/SlateReference/B/Exhibits/IlluminationHost/Style/Fonts/dm-sans-latin-400-normal.woff2', import.meta.url))).toString('base64');
    const Rows = Results.Fixtures.map(Fixture => `<tr><td>${Fixture.Name}</td><td>${Fixture.SourceMeanAbsoluteErrorMetres.toFixed(5)}</td><td>${Fixture.MetricMeanAbsoluteErrorMetres.toFixed(5)}</td><td>${Fixture.MaximumErrorMetres.toFixed(5)}</td><td>${Fixture.MetricMeanGradientMagnitude.toFixed(4)}</td><td>${Fixture.SignChanges}</td></tr>`).join('');
    const Refinement = Results.Refinement.map(Fixture => `<tr><td>${Fixture.Dimensions[0]}³</td><td>${Fixture.PitchMetres[0]}</td><td>${Fixture.CommonBandMeanErrorMetres.toFixed(6)} m</td></tr>`).join('');
    await writeFile(Directory + 'MetricDistance.html', `<!doctype html><html lang="en"><meta charset="utf-8"><title>Frontier · Metric distance validation</title><style>@font-face{font-family:DM;src:url(data:font/woff2;base64,${Font})}*{box-sizing:border-box}body{margin:0;background:#101010;color:#e5e5e5;font:15px DM,sans-serif;padding:50px 64px}main{max-width:1200px;margin:auto}header{display:flex;justify-content:space-between;align-items:center}h1{font-size:32px;font-weight:400;letter-spacing:-1px;margin:8px 0 12px}h2{font-size:18px;font-weight:400;margin:0}.eyebrow{font-size:11px;letter-spacing:2px;color:#999}.pill{border:1px solid #444;border-radius:20px;padding:8px 14px;font-size:12px}.card{background:linear-gradient(#252525,#202020);border:1px solid #343434;border-radius:18px;padding:24px;margin-top:24px}.note{color:#aaa;font-size:13px;line-height:1.6}table{border-collapse:collapse;width:100%;margin-top:20px;font-size:13px}td,th{text-align:left;padding:12px 10px;border-bottom:1px solid #383838}th{font-size:11px;color:#aaa;font-weight:400}.legend{font-size:12px;display:flex;gap:26px;margin-top:18px}.legend span:before{content:'━';margin-right:10px}.reference{color:#e5e5e5}.metric{color:#8fbab1}.source{color:#a99072}svg{width:100%;margin-top:8px}.lower{display:grid;grid-template-columns:1fr 1fr;gap:24px}small{color:#999}</style><main><header><div><div class="eyebrow">FRONTIER / VOLUMETRIC DISTANCE</div><h1>Metric redistancing · measured, not relabelled</h1><div class="note">Independent analytic solids · sparse anisotropic 3D fast sweeping · actual codec round trips</div></div><div class="pill">${Results.Fixtures.length} fixtures · zero sign changes</div></header><section class="card"><h2>Quadratic sphere · actual sampled cross section</h2><div class="legend"><span class="reference">Analytic distance, truncated</span><span class="metric">Reinitialized metric field</span><span class="source">Original conservative bound</span></div>${Chart.SVG}<div class="note">Values come from the retained numeric run. The displayed slice uses actual cell-centre Y/Z, not an idealized equator.</div></section><section class="card"><h2>Expected versus observed · distances in metres</h2><table><tr><th>Independent shape</th><th>Source mean error</th><th>Metric mean error</th><th>Metric max error</th><th>Mean |∇d|</th><th>Sign changes</th></tr>${Rows}</table><div class="note">Two boundary cells excluded (one for the allocated box). Gradient samples exclude the saturated band and its immediate neighbours; finite-difference normals and distance remain first-order grid approximations.</div></section><div class="lower"><section class="card"><h2>Grid refinement · fixed 12 m domain, 1 m band</h2><table><tr><th>Resolution</th><th>Pitch / m</th><th>Mean error in |d| &lt; 0.8 m</th></tr>${Refinement}</table></section><section class="card"><h2>What this establishes</h2><p class="note">True 3D anisotropic propagation, metric scale, interface refinement, brick-seam continuity and voxel-sign preservation. Accuracy improves with resolution on the retained sphere.</p><p class="note">Not exact Euclidean distance, exact subcell topology, erosion, native-editor integration or a physical-GPU benchmark.</p><small>Node ${Results.Runtime} · ${Results.Platform} · ${Results.Date.slice(0, 10)}<br>Local uncommitted continuation · source SHA256 recorded in JSON</small></section></div></main></html>\n`);
}
catch (Failure)
{
    Results.Status = 'failed';
    Results.Failure = Failure.stack;
    console.error(Failure);
    process.exitCode = 1;
}
finally
{
    await writeFile(Directory + 'MetricDistanceValidation.json', JSON.stringify(Results, null, 2) + '\n');
}
