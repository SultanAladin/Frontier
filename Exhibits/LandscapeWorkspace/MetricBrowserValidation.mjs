/*==========================================================================================================================================
                                                     METRICBROWSERVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Actual metric bake, immutable snapshot, recovered metre-scale distances, legacy mode and background cancellation.

import Assert from 'node:assert/strict';
import { mkdir, writeFile, readFile, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { gunzipSync } from 'node:zlib';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { CreateLandform } from '../../Experimental/LandscapeWorkspace/LandformStack.js';
import { VolumeDistance } from '../../Experimental/LandscapeWorkspace/DistanceField.js';
import { DecodeSparseVolume, ReadVolumeCell } from '../../Experimental/LandscapeWorkspace/SparseVolume.js';

const Directory = fileURLToPath(new URL('./', import.meta.url));
await mkdir(Directory + 'Results', { recursive: true });
await mkdir(Directory + 'Images', { recursive: true });
const Fixture = { Format: 'FrontierLandscape/1', Layers: [{
    Name: 'Metric coastal field', Color: '#b9a481', Visible: true, Depth: 80, Caves: false,
    Resolution: 256, Vertical: 64, DistanceMode: 'bound',
    Strokes: [{ Points: [[-35, 0], [35, 0]], Radius: 110, Erase: false }],
    Regions: [{ X0: -160, Z0: -130, X1: 160, Z1: 130, Edge: 55 }],
    Landforms: [{ ...CreateLandform('fractal'), Amount: 90, Scale: 1, InfluenceRadius: .55, InfluenceFalloff: 1, Blend: 0 }]
}] };
const Results = {
    Status: 'running', Date: new Date().toISOString(), Fixture, Runtime: process.version, Platform: process.platform,
    Revision: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
    Branch: execFileSync('git', ['branch', '--show-current'], { encoding: 'utf8' }).trim(),
    Checks: [], SourceSHA256: {}
};
for (const Name of ['VolumeSculpt.js', 'VolumeSculptInteraction.js', 'VolumeRefinement.js', 'PreviewSampling.js', 'VolumePresentation.js', 'VolumeSubmission.js', 'VolumeRayField.js', 'VolumeSurface.js', 'VolumeLattice.js', 'VolumeDetail.js', 'CoastalWorld.js', 'VolumeRasterization.js', 'PreviewTiling.js', 'SparseVolume.js', 'DistanceReinitialization.js', 'VolumeBaking.js', 'DistanceField.js', 'Workspace.js', 'Workspace.css', 'index.html', 'LayerStackPanel.js'])
{
    Results.SourceSHA256[Name] = createHash('sha256').update(await readFile(new URL('../../Experimental/LandscapeWorkspace/' + Name, import.meta.url))).digest('hex');
}
const Browser = await chromium.launch({
    executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined,
    headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--no-zygote']
});
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 } });
Page.setDefaultTimeout(180000);
const Errors = [], Downloads = [];
Page.on('pageerror', Failure => Errors.push(Failure.message));
Page.on('console', Message => { if (Message.type() === 'error') Errors.push(Message.text()); });
Page.on('download', Download => Downloads.push(Download.suggestedFilename()));
await Page.addInitScript(Fixture =>
{
    localStorage.setItem('FrontierLandscape', JSON.stringify(Fixture));
    window.MetricProtocol = [];
    const SamplingConstructor = window.Worker;
    window.Worker = class extends SamplingConstructor
    {
        constructor(...Arguments)
        {
            super(...Arguments);
            if (!String(Arguments[0]).includes('VolumeBaking')) return;
            window.MetricSampling = this;
            this.addEventListener('message', Event => window.MetricProtocol.push(Event.data.Category));
        }
    };
    document.addEventListener('DOMContentLoaded', () => document.querySelector('#AnimateOcean').checked = false);
}, Fixture);

async function DownloadVolume(Download)
{
    const Chunks = [];
    for await (const Chunk of await Download.createReadStream()) Chunks.push(Chunk);
    return DecodeSparseVolume(gunzipSync(Buffer.concat(Chunks)));
}

function Verify(Name, Observed)
{
    Results.Checks.push({ Name, Status: 'passed', Observed });
    console.log('Verified: ' + Name);
}

try
{
    await Page.goto(process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173');
    await Page.locator('#LandscapeTab').click();
    await Page.waitForFunction(() =>
    {
        const Canvas = document.querySelector('#Volume');
        return Canvas.dataset.renderStatus === 'ready' && Canvas.dataset.completedRevision && Canvas.dataset.completedRevision === Canvas.dataset.volumeRevision && Number(Canvas.dataset.completedQuality) >= 48 && Canvas.dataset.completedQuality === Canvas.dataset.geometryCompletedQuality && Canvas.dataset.previewDensitySettled === 'true' && Canvas.dataset.completedComposition === Canvas.dataset.submittedComposition;
    });
    await Page.locator('[data-inspector="region"]').click();
    await Page.locator('.volume-export').evaluate(Details => Details.open = true);
    Assert.equal(await Page.locator('#DistanceMode').inputValue(), 'bound');
    const Revision = await Page.locator('#Volume').getAttribute('data-volume-revision');
    await Page.locator('#DistanceMode').selectOption('metric');
    await Page.waitForFunction(() => document.querySelector('#SaveStatus').textContent === 'Saved locally');
    Assert.equal(await Page.locator('#Volume').getAttribute('data-volume-revision'), Revision);
    Verify('distance-mode editing persists without changing or rerasterizing analytic geometry', { Revision });

    const DownloadRequest = Page.waitForEvent('download', { timeout: 600000 });
    await Page.locator('#Bake').click();
    await Page.waitForFunction(() => /Metric sweep/.test(document.querySelector('#BakeRead').textContent));
    Assert.equal(await Page.locator('#CancelBake').isVisible(), true);
    await Page.locator('#DistanceMode').selectOption('bound');
    await Page.locator('#MaskName').fill('Edited while sampling');
    await Page.locator('#MaskName').dispatchEvent('change');
    const Volume = await DownloadVolume(await DownloadRequest);
    Assert.equal(Volume.Specification.Format, 'FrontierSparseTSDF/2');
    Assert.deepEqual(Volume.Specification.Dimensions, [256, 64, 256]);
    Assert.equal(Volume.Specification.Layer.DistanceMode, 'metric');
    Assert.equal(Volume.Specification.Layer.Name, 'Metric coastal field');
    Assert.equal(Volume.Specification.Redistance.Converged, true);
    Assert.equal(Volume.Specification.Redistance.SignChanges, 0);
    Assert.match(await Page.locator('#BakeDistanceRead').innerText(), /Metric TSDF/);
    Assert.match(await Page.locator('#BakeSnapshotRead').innerText(), /Metric coastal field/);
    const Live = await Page.evaluate(() => JSON.parse(localStorage.getItem('FrontierLandscape')));
    Assert.equal(Live.Layers[0].DistanceMode, 'bound');
    Assert.equal(Live.Layers[0].Name, 'Edited while sampling');
    Verify('the actual UI exports a converged metric volume from its immutable captured snapshot', Volume.Specification.Redistance);

    const { Origin, Pitch, Dimensions, Band } = Volume.Specification;
    const Queries = [0.7, -2.2].map(Y =>
    {
        const Cell = [110, Y, 0].map((Coordinate, Axis) => Math.floor((Coordinate - Origin[Axis]) / Pitch[Axis]));
        const World = Cell.map((Coordinate, Axis) => Origin[Axis] + (Coordinate + .5) * Pitch[Axis]);
        const Distance = ReadVolumeCell(Volume, ...Cell);
        const RawBound = VolumeDistance(Volume.Specification.Layer, ...World, Volume.Specification.Region);
        Assert.ok(Math.abs(World[0]) > 75 && Math.abs(World[1]) < Band);
        Assert.ok(Math.abs(Distance - World[1]) < .0001, JSON.stringify({ World, Distance }));
        Assert.ok(Math.abs(Distance) > Math.abs(RawBound) * 4);
        return { Cell, World, RawBound, MetricDistance: Distance, ExpectedPlaneDistance: World[1] };
    });
    Verify('flat radial-exterior samples recover metre-scale distance instead of a compressed conservative bound', Queries);
    Assert.equal(await Page.locator('#RenderQuality').isVisible(), false);
    await Page.waitForFunction(() => document.querySelector('#Volume').width > 480 && document.querySelector('#Volume').width <= 1440 &&
        document.querySelector('#Volume').dataset.completedComposition === document.querySelector('#Volume').dataset.submittedComposition);
    await Page.locator('#BakeResult').scrollIntoViewIfNeeded();
    await Page.screenshot({ path: Directory + 'Images/MetricBake.png' });

    const CancelCount = Downloads.length;
    await Page.locator('#Resolution').selectOption('256');
    await Page.locator('#DistanceMode').selectOption('metric');
    await Page.locator('#Bake').click();
    await Page.waitForFunction(() => /Metric sweep/.test(document.querySelector('#BakeRead').textContent));
    const CancellationPhase = await Page.locator('#BakeRead').innerText();
    await Page.evaluate(() =>
    {
        window.ExpiredBakeMessage = window.MetricSampling.onmessage;
        window.ExpiredBakeError = window.MetricSampling.onerror;
    });
    await Page.locator('#CancelBake').click();
    await Page.evaluate(() =>
    {
        window.ExpiredBakeMessage({ data: { Category: 'complete' } });
        window.ExpiredBakeError({ message: 'expired export' });
    });
    await Page.waitForTimeout(500);
    Assert.equal(Downloads.length, CancelCount);
    Assert.match(await Page.locator('#Status').innerText(), /cancelled/);
    Verify('cancellation during actual 3D sweeping terminates computation and discards intermediate output', { PhaseAtCancellation: CancellationPhase, Downloads: Downloads.length, ExpiredCompletionAndErrorCallbacks: 'ignored' });

    await Page.locator('#Resolution').selectOption('256');
    await Page.locator('#DistanceMode').selectOption('bound');
    const LegacyRequest = Page.waitForEvent('download', { timeout: 600000 });
    await Page.locator('#Bake').click();
    const Legacy = await DownloadVolume(await LegacyRequest);
    Assert.equal(Legacy.Specification.Format, 'FrontierSparseTSDF/1');
    Assert.equal(Legacy.Specification.Redistance, undefined);
    Assert.match(await Page.locator('#BakeDistanceRead').innerText(), /Conservative bound/);
    Assert.equal(await Page.locator('#BakeMetricDetails').isVisible(), false);
    Verify('legacy conservative exports keep their original format and are never labelled metric', { Format: Legacy.Specification.Format });
    // 📝 Fault injection changes only the test HTTP response, never production source or the retained fingerprints.
    await Page.route('**/DistanceReinitialization.js*', async Route =>
    {
        const Response = await Route.fetch();
        const Text = await Response.text();
        Assert.ok(Text.includes('Options.MaximumCycles ?? 6'));
        await Route.fulfill({ response: Response, body: Text.replace('Options.MaximumCycles ?? 6', 'Options.MaximumCycles ?? 1') });
    });
    await Page.locator('#DistanceMode').selectOption('metric');
    await Page.evaluate(() => window.MetricProtocol = []);
    const FailureDownloadCount = Downloads.length;
    await Page.locator('#Bake').click();
    await Page.waitForFunction(() => /did not converge/.test(document.querySelector('#Status').textContent));
    Assert.equal(Downloads.length, FailureDownloadCount);
    Assert.equal(await Page.locator('#Bake').isEnabled(), true);
    const Protocol = await Page.evaluate(() => window.MetricProtocol);
    Assert.equal(Protocol.at(-1), 'failure');
    Assert.equal(Protocol.includes('chunk'), false);
    Assert.equal(Protocol.includes('complete'), false);
    await Page.unroute('**/DistanceReinitialization.js*');
    Verify('nonconvergence produces no archive bytes and restores editable bake controls', {
        Injection: 'HTTP-only maximum-cycle limit reduced from six to one',
        Message: await Page.locator('#Status').innerText(), ProtocolCategories: [...new Set(Protocol)]
    });
    Assert.deepEqual(Errors, []);
    Assert.equal(await Page.locator('#Volume').evaluate(async Canvas => (await Canvas[Symbol.for('FrontierLandscape.Presentation')].Inspect('pixels')).Error), 0);
    Verify('metric and legacy UI operations finish without browser or WebGL errors', Errors);
    const Figure = await Browser.newPage({ viewport: { width: 1440, height: 1200 } });
    await Figure.goto(new URL('./Results/MetricDistance.html', import.meta.url).href);
    await Figure.evaluate(() => document.fonts.ready);
    await Figure.screenshot({ path: Directory + 'Images/MetricDistance.png', fullPage: true });
    await Figure.close();
    Results.Status = 'passed';
    await rm(Directory + 'Images/MetricFailure.png', { force: true });
    Results.Browser = Browser.version();
    Results.Renderer = await Page.locator('#Volume').getAttribute('data-renderer');
}
catch (Failure)
{
    Results.Status = 'failed';
    Results.Failure = Failure.stack;
    Results.Errors = Errors;
    await Page.screenshot({ path: Directory + 'Images/MetricFailure.png' }).catch(() => {});
    console.error(Failure);
    process.exitCode = 1;
}
finally
{
    await Browser.close();
    await writeFile(Directory + 'Results/MetricBrowserValidation.json', JSON.stringify(Results, null, 2) + '\n');
}
