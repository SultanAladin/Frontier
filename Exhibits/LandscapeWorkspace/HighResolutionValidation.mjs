/*==========================================================================================================================================
                                                     HIGHRESOLUTIONVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Full 1024 × 128 × 1024 streamed SDF export with a solid-air-solid column that cannot be encoded as a heightmap.

import Assert from 'node:assert/strict';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { gunzipSync } from 'node:zlib';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { CreateLandform } from '../../Experimental/LandscapeWorkspace/LandformStack.js';
import { DecodeSparseVolume, ReadVolumeCell } from '../../Experimental/LandscapeWorkspace/SparseVolume.js';

const ResultDirectory = fileURLToPath(new URL('./Results/', import.meta.url));
const ImageDirectory = fileURLToPath(new URL('./Images/', import.meta.url));
await mkdir(ResultDirectory, { recursive: true });
await mkdir(ImageDirectory, { recursive: true });
const Document = {
    Format: 'FrontierLandscape/1', Layers: [{
        Name: 'Volumetric verification', Color: '#a9b5af', Visible: true, Depth: 120, Caves: false,
        Resolution: 1024, Vertical: 128, DistanceMode: 'metric',
        Strokes: [{ Points: [[0, 0]], Radius: 150, Erase: false }],
        Regions: [{ X0: -180, Z0: -180, X1: 180, Z1: 180, Edge: 0 }],
        Landforms: [{ ...CreateLandform('arch'), Amount: 140, Blend: 0 }]
    }]
};
const Results = {
    Status: 'running', Date: new Date().toISOString(), Platform: process.platform, Runtime: process.version,
    Revision: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: fileURLToPath(new URL('../../', import.meta.url)), encoding: 'utf8' }).trim(),
    Fixture: Document, Note: 'Local uncommitted LandscapeWorkspace correction; analytic preview is separate from the sampled export.'
};
Results.SourceSHA256 = {};
for (const Name of ['VolumeSculpt.js', 'VolumeSculptInteraction.js', 'VolumeRefinement.js', 'PreviewSampling.js', 'VolumePresentation.js', 'VolumeSubmission.js', 'VolumeRayField.js', 'VolumeSurface.js', 'VolumeLattice.js', 'VolumeDetail.js', 'CoastalWorld.js', 'VolumeRasterization.js', 'PreviewTiling.js', 'Workspace.js', 'LayerStackPanel.js', 'DistanceField.js', 'LandformStack.js', 'SparseVolume.js', 'VolumeBaking.js', 'DistanceReinitialization.js'])
{
    const Bytes = await readFile(new URL('../../Experimental/LandscapeWorkspace/' + Name, import.meta.url));
    Results.SourceSHA256[Name] = createHash('sha256').update(Bytes).digest('hex');
}
const Browser = await chromium.launch({
    executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined,
    headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--no-zygote']
});
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 } });
Page.setDefaultTimeout(180000);
await Page.addInitScript(Document =>
{
    localStorage.setItem('FrontierLandscape', JSON.stringify(Document));
    document.addEventListener('DOMContentLoaded', () => document.querySelector('#AnimateOcean').checked = false);
}, Document);
const Errors = [];
Page.on('pageerror', Failure => Errors.push(Failure.message));
try
{
    await Page.goto(process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173');
    await Page.locator('[data-inspector="region"]').click();
    await Page.locator('.volume-export').evaluate(Details => Details.open = true);
    const DownloadRequest = Page.waitForEvent('download', { timeout: 600000 });
    const Started = performance.now();
    await Page.locator('#Bake').click();
    const Download = await DownloadRequest;
    Results.BakeDownloadSeconds = (performance.now() - Started) / 1000;
    const Stream = await Download.createReadStream();
    const Chunks = [];
    for await (const Chunk of Stream) Chunks.push(Chunk);
    const Compressed = Buffer.concat(Chunks);
    const Decoded = gunzipSync(Compressed);
    const Volume = DecodeSparseVolume(Decoded);
    Assert.deepEqual(Volume.Specification.Dimensions, [1024, 128, 1024]);
    Assert.equal(Volume.Specification.Format, 'FrontierSparseTSDF/2');
    Assert.equal(Volume.Specification.Redistance.Converged, true);
    Assert.equal(Volume.Specification.Redistance.SignChanges, 0);
    Results.Redistance = Volume.Specification.Redistance;
    const CoveredCells = Volume.Leaves.reduce((Count, Leaf) => Count + Leaf.Width ** 3, 0);
    Assert.equal(CoveredCells, 1024 * 128 * 1024);
    const WorldColumn = [-50, 40, 116].map(Y =>
    {
        const Position = [0, Y, 0];
        const Cell = Position.map((Coordinate, Axis) => Math.floor((Coordinate - Volume.Specification.Origin[Axis]) / Volume.Specification.Pitch[Axis]));
        return { World: Position, Cell, Distance: ReadVolumeCell(Volume, ...Cell) };
    });
    Results.WorldColumn = WorldColumn;
    Assert.ok(WorldColumn[0].Distance < 0, 'underwater foundation must be solid');
    Assert.ok(WorldColumn[1].Distance > 0, 'the space underneath the span must remain empty');
    Assert.ok(WorldColumn[2].Distance < 0, 'the roof must be solid at the same X/Z coordinate');
    Assert.deepEqual(Errors, []);
    Results.Status = 'passed';
    Results.Dimensions = Volume.Specification.Dimensions;
    Results.CoveredCells = CoveredCells;
    Results.Leaves = Volume.Leaves.length;
    Results.CompressedBytes = Compressed.length;
    Results.UncompressedBytes = Decoded.length;
    Results.BandMetres = Volume.Specification.Band;
    Results.WorldColumn = WorldColumn;
    Results.Browser = Browser.version();
    await Page.locator('#LandscapeTab').click();
    Assert.equal(await Page.locator('#RenderQuality').isVisible(), false);
    await Page.waitForFunction(() =>
    {
        const Canvas = document.querySelector('#Volume');
        return Canvas.width > 480 && Canvas.width <= 1440 && Canvas.dataset.completedComposition === Canvas.dataset.submittedComposition;
    });
    Results.Renderer = await Page.locator('#Volume').getAttribute('data-renderer');
    await Page.screenshot({ path: ImageDirectory + 'VolumeArch1024.png' });
    console.log(JSON.stringify({ Status: Results.Status, Dimensions: Results.Dimensions, CoveredCells, WorldColumn, CompressedBytes: Compressed.length }, null, 2));
}
catch (Failure)
{
    Results.Status = 'failed';
    Results.Failure = Failure.stack;
    Results.BrowserErrors = Errors;
    process.exitCode = 1;
    console.error(Failure);
}
finally
{
    await Browser.close();
    await writeFile(ResultDirectory + 'HighResolutionValidation.json', JSON.stringify(Results, null, 2) + '\n');
}
