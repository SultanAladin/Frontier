/*==========================================================================================================================================
                                                       PRODUCTIONVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Built-asset hosting, packaged fonts, actual WebGL drawing, and the bundled compressed-volume sampling thread.

import Assert from 'node:assert/strict';
import { mkdir, writeFile, readFile, readdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { gunzipSync } from 'node:zlib';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { DecodeSparseVolume } from '../../Experimental/LandscapeWorkspace/SparseVolume.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                        EXECUTION CONDITIONS
//------------------------------------------------------------------------------------------------------------------------

const ResultDirectory = fileURLToPath(new URL('./Results/', import.meta.url));
await mkdir(ResultDirectory, { recursive: true });
const Results = {
    Status: 'running', Date: new Date().toISOString(), Runtime: process.version,
    Origin: process.env.LANDSCAPE_BUILD_URL || 'http://127.0.0.1:4173',
    Note: 'Production build of the local uncommitted LandscapeWorkspace correction.',
    Checks: [], Assets: []
};
const Output = new URL('../../Experimental/LandscapeWorkspace/dist/', import.meta.url);
for (const Name of ['index.html', ...(await readdir(new URL('assets/', Output))).map(Name => 'assets/' + Name)])
{
    const Bytes = await readFile(new URL(Name, Output));
    Results.Assets.push({ Path: Name, Bytes: Bytes.length, SHA256: createHash('sha256').update(Bytes).digest('hex') });
}
const Browser = await chromium.launch({
    executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined,
    headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--no-zygote']
});
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 } });
Page.setDefaultTimeout(120000);
const BrowserErrors = [], FailedResponses = [], ExecutionScripts = [];
Page.on('pageerror', Failure => BrowserErrors.push(Failure.message));
Page.on('console', Message => { if (Message.type() === 'error') BrowserErrors.push(Message.text()); });
Page.on('response', Response => { if (Response.status() >= 400) FailedResponses.push(`${Response.status()} ${Response.url()}`); });
Page.on('worker', Execution => ExecutionScripts.push(Execution.url()));
await Page.addInitScript(() =>
{
    document.addEventListener('DOMContentLoaded', () => document.querySelector('#AnimateOcean').checked = false);
});

async function WaitForRender()
{
    await Page.waitForFunction(() =>
    {
        const Canvas = document.querySelector('#Volume');
        return Canvas.dataset.renderStatus === 'ready' && Canvas.dataset.completedRevision === Canvas.dataset.volumeRevision &&
            Number(Canvas.dataset.completedQuality) >= 48 && Canvas.dataset.completedQuality === Canvas.dataset.geometryCompletedQuality && Canvas.dataset.previewDensitySettled === 'true' && Canvas.dataset.completedComposition && Canvas.dataset.completedComposition === Canvas.dataset.submittedComposition;
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        BUILT APPLICATION
//------------------------------------------------------------------------------------------------------------------------

try
{
    await Page.goto(Results.Origin);
    await Page.locator('#LandscapeTab').click();
    await WaitForRender();
    Assert.equal(await Page.locator('.mask-row').count(), 3);
    await Page.evaluate(() => document.fonts.ready);
    Assert.ok(await Page.evaluate(() => document.fonts.check('13px "DM Sans"')));
    const InitialGL = await Page.locator('#Volume').evaluate(async Canvas => (await Canvas[Symbol.for('FrontierLandscape.Presentation')].Inspect('pixels')).Error);
    const RenderMode = await Page.locator('#Volume').getAttribute('data-render-mode');
    Assert.equal(RenderMode, 'webgl2');
    const PresentationScripts = ExecutionScripts.filter(URL => /\/assets\/VolumeSubmission-[^/]+\.js$/.test(URL));
    Assert.equal(PresentationScripts.length, 1);
    Assert.equal(InitialGL, 0);
    await Page.locator('#AddGenerator').click();
    await Page.locator('[data-generator="badlands"]').click();
    Assert.equal(await Page.locator('#OperationCount').innerText(), '3');
    await Page.locator('#HistoryUndo').click();
    Assert.equal(await Page.locator('#OperationCount').innerText(), '2');
    Results.Checks.push({ Name: 'Built UI, packaged local fonts, actual rendering, generator insertion and undo', Status: 'passed', RenderMode, PresentationScripts, DefaultImageDeadlineMilliseconds: Number(await Page.locator('#Volume').getAttribute('data-graphics-image-timeout-milliseconds')) });

    const Fixture = {
        Format: 'FrontierLandscape/1', Layers: [{
            Name: 'Production export', Color: '#a9b5af', Visible: true, Depth: 120, Caves: false,
            Resolution: 256, Vertical: 64, DistanceMode: 'metric', Landforms: [],
            Strokes: [{ Points: [[-20, 0], [20, 0]], Radius: 60, Erase: false }],
            Regions: [{ X0: -90, Z0: -70, X1: 90, Z1: 70, Edge: 45 }]
        }]
    };
    await Page.locator('#File').setInputFiles({ name: 'Production.frontier.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(Fixture)) });
    await Page.locator('[data-inspector="region"]').click();
    await Page.locator('.volume-export').evaluate(Details => Details.open = true);
    const DownloadRequest = Page.waitForEvent('download');
    await Page.locator('#Bake').click();
    const Download = await DownloadRequest;
    const Stream = await Download.createReadStream();
    const Chunks = [];
    for await (const Chunk of Stream) Chunks.push(Chunk);
    const Volume = DecodeSparseVolume(gunzipSync(Buffer.concat(Chunks)));
    Assert.deepEqual(Volume.Specification.Dimensions, [256, 64, 256]);
    Assert.equal(Volume.Specification.Format, 'FrontierSparseTSDF/2');
    Assert.equal(Volume.Specification.Redistance.Converged, true);
    Assert.equal(Volume.Specification.Redistance.SignChanges, 0);
    Assert.ok(Volume.Leaves.some(Leaf => Leaf.Samples.some(Distance => Distance < 0)));
    Assert.ok(Volume.Leaves.some(Leaf => Leaf.Samples.some(Distance => Distance > 0)));
    const SamplingScripts = ExecutionScripts.filter(URL => /\/assets\/VolumeBaking-[^/]+\.js$/.test(URL));
    Assert.equal(SamplingScripts.length, 1);
    Assert.equal(ExecutionScripts.length, 2);
    Assert.match(SamplingScripts[0], /\/assets\/VolumeBaking-[^/]+\.js$/);
    Results.Checks.push({ Name: 'Bundled sampling script produces a converged metric 256 × 64 × 256 gzip volume', Status: 'passed', Dimensions: Volume.Specification.Dimensions, Redistance: Volume.Specification.Redistance, SamplingScripts });
    await WaitForRender();
    Assert.equal(await Page.locator('#Volume').evaluate(async Canvas => (await Canvas[Symbol.for('FrontierLandscape.Presentation')].Inspect('pixels')).Error), 0);
    Assert.deepEqual(BrowserErrors, []);
    Assert.deepEqual(FailedResponses, []);
    Results.Checks.push({ Name: 'Built application finishes with no console/page/WebGL errors or failed resources', Status: 'passed', BrowserErrors, FailedResponses });
    Results.Browser = Browser.version();
    Results.Renderer = await Page.locator('#Volume').getAttribute('data-renderer');
    Results.Status = 'passed';
    console.log(JSON.stringify({ Status: Results.Status, Checks: Results.Checks }, null, 2));
}
catch (Failure)
{
    Results.Status = 'failed';
    Results.Failure = Failure.stack;
    Results.BrowserErrors = BrowserErrors;
    Results.FailedResponses = FailedResponses;
    process.exitCode = 1;
    console.error(Failure);
}
finally
{
    await Browser.close();
    await writeFile(ResultDirectory + 'ProductionValidation.json', JSON.stringify(Results, null, 2) + '\n');
}
