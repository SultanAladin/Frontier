/*==========================================================================================================================================
                                                          PREVIEWVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Actual cold Landscape entry, UI responsiveness, animated terrain/ocean, bounded dispatches and genuine context-loss recovery.

import Assert from 'node:assert/strict';
import { ConfigureGraphicsDeadline } from './GraphicsDeadlineConfiguration.mjs';
import { mkdir, readFile, writeFile, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';

const Directory = fileURLToPath(new URL('./', import.meta.url));
await mkdir(Directory + 'Results', { recursive: true });
await mkdir(Directory + 'Images', { recursive: true });
const Results = {
    Status: 'running', Date: new Date().toISOString(), Runtime: process.version, Platform: process.platform,
    Revision: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
    Branch: execFileSync('git', ['branch', '--show-current'], { encoding: 'utf8' }).trim(),
    Origin: process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173', Checks: [], SourceSHA256: {},
    Note: 'The responsiveness interval starts at first Landscape entry, after the separately recorded Mask-only startup. Captures follow this interval. Uncommitted freeze repair; actual application and WEBGL_lose_context, not synthetic DOM events. Linux Chromium / software GPU, not Windows hardware. Cold means the first WebGL context in this page; the driver shader cache may be warm.'
};
for (const Name of ['Workspace.js', 'VolumeRasterization.js', 'VolumePresentation.js', 'VolumeSubmission.js', 'VolumeRayField.js', 'VolumeScalarRasterization.js', 'PreviewTiling.js', 'Workspace.css', 'index.html'])
    Results.SourceSHA256[Name] = createHash('sha256').update(await readFile(new URL('../../Experimental/LandscapeWorkspace/' + Name, import.meta.url))).digest('hex');
const BrowserOptions = {
    executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined, headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--no-zygote']
};
const Browser = await chromium.launch(BrowserOptions);
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 2 });
Page.setDefaultTimeout(90000);
await ConfigureGraphicsDeadline(Page, Results);
const Errors = [], FailedResources = [], GraphicsThreads = [];
Page.on('pageerror', Failure => Errors.push(Failure.message));
Page.on('console', Message => { if (Message.type() === 'error') Errors.push(Message.text()); });
Page.on('response', Response => { if (Response.status() >= 400) FailedResources.push(Response.status() + ' ' + Response.url()); });
Page.on('worker', Execution => { if (/VolumeSubmission/.test(Execution.url())) GraphicsThreads.push(Execution.url()); });
await Page.addInitScript(() =>
{
    window.PreviewPhase = 'startup';
    window.PreviewTelemetry = { MainThreadContexts: 0, MainThreadDraws: 0, MaximumHeartbeatMilliseconds: 0, HeartbeatStalls: [], SlowCanvasCalls: [] };
    const Original = HTMLCanvasElement.prototype.getContext, Contexts = new WeakSet();
    HTMLCanvasElement.prototype.getContext = function(...Arguments)
    {
        const Started = performance.now();
        const Context = Original.apply(this, Arguments);
        if (performance.now() - Started > 20) window.PreviewTelemetry.SlowCanvasCalls.push({ Name: 'getContext', Context: Arguments[0], Elapsed: performance.now() - Started, Phase: window.PreviewPhase });
        if (Arguments[0] === 'webgl2' && Context && !Contexts.has(this)) { Contexts.add(this); window.PreviewTelemetry.MainThreadContexts++; }
        return Context;
    };
    const Submit = WebGL2RenderingContext.prototype.drawArrays;
    WebGL2RenderingContext.prototype.drawArrays = function(...Arguments)
    {
        window.PreviewTelemetry.MainThreadDraws++;
        return Submit.apply(this, Arguments);
    };
    for (const Name of ['drawImage', 'getImageData', 'fill', 'stroke', 'clearRect', 'fillRect', 'fillText'])
    {
        const Native = CanvasRenderingContext2D.prototype[Name];
        CanvasRenderingContext2D.prototype[Name] = function(...Arguments)
        {
            const Started = performance.now();
            const Result = Native.apply(this, Arguments);
            if (performance.now() - Started > 20) window.PreviewTelemetry.SlowCanvasCalls.push({ Name, Elapsed: performance.now() - Started, Phase: window.PreviewPhase });
            return Result;
        };
    }
    let Last = performance.now();
    window.ResetPreviewTiming = () =>
    {
        Last = performance.now();
        window.PreviewTelemetry.MaximumHeartbeatMilliseconds = 0;
        window.PreviewTelemetry.HeartbeatStalls = [];
    };
    setInterval(() =>
    {
        const Now = performance.now(), Elapsed = Now - Last;
        window.PreviewTelemetry.MaximumHeartbeatMilliseconds = Math.max(window.PreviewTelemetry.MaximumHeartbeatMilliseconds, Elapsed);
        if (Elapsed > 250) window.PreviewTelemetry.HeartbeatStalls.push({ Elapsed, Phase: window.PreviewPhase, Timestamp: Now });
        Last = Now;
    }, 10);
});

async function Verify(Name, Observed)
{
    await Page.evaluate(Name => window.PreviewPhase = Name, Name).catch(() => {});
    Results.Checks.push({ Name, Status: 'passed', Observed });
    console.log('Verified: ' + Name);
}

async function InspectFor(Target, Category = 'pixels')
{
    return Target.evaluate(async Category =>
    {
        const { InspectVolumePresentation } = await import('/VolumePresentation.js');
        const Snapshot = await InspectVolumePresentation(document.querySelector('#Volume'), Category);
        if (!Snapshot.Pixels) return Snapshot;
        let Land = 0, Hash = 2166136261;
        for (let Index = 0; Index < Snapshot.Pixels.length; Index++) Hash = Math.imul(Hash ^ Snapshot.Pixels[Index], 16777619) >>> 0;
        for (let Index = 0; Index < Snapshot.Pixels.length; Index += 4)
            if (Snapshot.Pixels[Index] > Snapshot.Pixels[Index + 1] + 3 && Snapshot.Pixels[Index + 1] > Snapshot.Pixels[Index + 2] + 6) Land++;
        delete Snapshot.Pixels;
        return { ...Snapshot, Hash, LandPixels: Land };
    }, Category);
}

async function Inspect(Category = 'status') { return InspectFor(Page, Category); }

async function Switch(Selector)
{
    const Started = performance.now();
    await Page.locator(Selector).click({ timeout: 2000 });
    return performance.now() - Started;
}

async function FirstTerrain(AfterRenders = 0, AfterEpoch = 0)
{
    await Page.waitForFunction(({ AfterRenders, AfterEpoch }) =>
    {
        const Canvas = document.querySelector('#Volume');
        return Number(Canvas.dataset.terrainRenders) > AfterRenders && Number(Canvas.dataset.initializationEpoch) > AfterEpoch &&
            Canvas.dataset.completedRevision && Canvas.dataset.completedRevision === Canvas.dataset.volumeRevision;
    }, { AfterRenders, AfterEpoch });
}

async function Lose()
{
    const Before = await Inspect();
    await Inspect('lose');
    await Page.waitForFunction(() => document.querySelector('#Volume').dataset.renderStatus === 'lost');
    Assert.equal((await Inspect()).contextLossPrevented, 'true');
    Assert.equal(await Page.locator('#Volume').getAttribute('data-completed-revision'), null);
    return Before;
}

let UnavailableBrowser;
try
{
    await Page.goto(Results.Origin);
    Assert.equal(await Page.locator('#Paint').isVisible(), true);
    Assert.equal(GraphicsThreads.length, 0);
    Assert.equal(await Page.evaluate(() => window.PreviewTelemetry.MainThreadContexts), 0);
    await Verify('Mask startup creates no WebGL context or hidden 3D workload', { GraphicsThreads: GraphicsThreads.length });
    await Page.evaluate(() => { document.querySelector('#LandscapeTab').click(); document.querySelector('#MaskTab').click(); });
    await Page.waitForTimeout(100);
    Assert.equal(GraphicsThreads.length, 0);
    await Verify('returning immediately to Mask cancels deferred graphics creation', { GraphicsThreads: GraphicsThreads.length });
    await Page.locator('[data-inspector="region"]').click();
    await Page.locator('#MaskName').fill('Preserved freeze-test coast');
    await Page.locator('#MaskName').dispatchEvent('change');
    await Page.waitForFunction(() => document.querySelector('#SaveStatus').textContent === 'Saved locally');
    const DocumentBefore = await Page.evaluate(() => localStorage.getItem('FrontierLandscape'));
    Results.BeforeGraphics = await Page.evaluate(() => ({ BrowserMilliseconds: performance.now(), Telemetry: window.PreviewTelemetry }));
    await Page.evaluate(() => { window.PreviewPhase = 'cold Landscape entry'; window.ResetPreviewTiming(); });
    const Started = performance.now();
    const LandscapeClickMilliseconds = await Switch('#LandscapeTab');
    await Page.waitForFunction(() => Number(document.querySelector('#Volume').dataset.maximumTerrainTilePixels) > 0);
    const InputStarted = performance.now();
    await Page.locator('[data-inspector="ocean"]').click({ timeout: 2000 });
    const InspectorInputMilliseconds = performance.now() - InputStarted;
    await FirstTerrain();
    Results.ColdFirstVisibleTerrainSeconds = (performance.now() - Started) / 1000;
    const Appearance = await Inspect('pixels');
    Assert.ok(Appearance.LandPixels > Appearance.Width * Appearance.Height * .01, 'the preview must contain actual terrain, not only sky/ocean');
    Assert.equal(Appearance.Error, 0);
    Assert.equal(Appearance.renderExecution, 'thread');
    Assert.equal(Appearance.graphicsContexts, '1');
    Assert.ok(Number(Appearance.maximumTerrainTilePixels) <= 256);
    Assert.ok(LandscapeClickMilliseconds < 1000 && InspectorInputMilliseconds < 1000);
    await Verify('cold high-DPI Landscape presents real land while accepting input during GPU work', {
        ColdFirstVisibleTerrainSeconds: Results.ColdFirstVisibleTerrainSeconds, LandscapeClickMilliseconds, InspectorInputMilliseconds, Appearance
    });
    await Page.waitForFunction(() =>
    {
        const Canvas = document.querySelector('#Volume');
        return Canvas.width === 256 && Canvas.dataset.renderStatus === 'ready' && Canvas.dataset.completedRevision === Canvas.dataset.volumeRevision;
    });
    const OceanImage = await Inspect('pixels');
    const OceanComposition = await Page.locator('#Volume').getAttribute('data-completed-composition');
    await Page.waitForFunction(Previous => document.querySelector('#Volume').dataset.completedComposition !== Previous, OceanComposition);
    const NextImage = await Inspect('pixels');
    Assert.equal(NextImage.Width, OceanImage.Width);
    Assert.equal(NextImage.Height, OceanImage.Height);
    Assert.equal(NextImage.terrainRenders, OceanImage.terrainRenders);
    Assert.notEqual(NextImage.Hash, OceanImage.Hash);
    await Verify('the procedural ocean changes the presented pixels without rerendering stable terrain', { BeforeHash: OceanImage.Hash, AfterHash: NextImage.Hash, Width: NextImage.Width, TerrainRenders: NextImage.terrainRenders });
    const MaskClickMilliseconds = await Switch('#MaskTab');
    const Paused = await Inspect();
    await Page.waitForTimeout(300);
    const StillPaused = await Inspect();
    Assert.equal(Paused.Suspended, true);
    Assert.equal(StillPaused.gpuDraws, Paused.gpuDraws);
    Assert.equal(StillPaused.gpuUploads, Paused.gpuUploads);
    Assert.ok(MaskClickMilliseconds < 1000);
    await Verify('Mask suspends the existing renderer without further GPU draws or uploads', { MaskClickMilliseconds, Draws: Paused.gpuDraws, Uploads: Paused.gpuUploads });
    await Switch('#LandscapeTab');
    const Orbit = await Page.locator('#Volume').getAttribute('data-completed-orbit');
    await Page.locator('#Volume').hover();
    await Page.mouse.wheel(0, -180);
    await Page.waitForFunction(Previous => document.querySelector('#Volume').dataset.completedOrbit !== Previous, Orbit);
    await Verify('3D camera zoom completes a new perspective', { PreviousOrbit: Orbit, CompletedOrbit: await Page.locator('#Volume').getAttribute('data-completed-orbit') });
    const BeforePan = await Page.locator('#Volume').getAttribute('data-completed-target');
    const Bounds = await Page.locator('#Volume').boundingBox();
    await Page.mouse.move(Bounds.x + Bounds.width * .5, Bounds.y + Bounds.height * .5);
    await Page.mouse.down({ button: 'middle' });
    await Page.mouse.move(Bounds.x + Bounds.width * .5 + 45, Bounds.y + Bounds.height * .5 + 25, { steps: 4 });
    await Page.mouse.up({ button: 'middle' });
    await Page.waitForFunction(Previous => document.querySelector('#Volume').dataset.completedTarget !== Previous, BeforePan);
    await Verify('3D middle-button pan completes a newly rendered camera target', { PreviousTarget: BeforePan, CompletedTarget: await Page.locator('#Volume').getAttribute('data-completed-target') });
    const BeforeLoss = await Lose();
    Assert.equal(await Page.locator('#PreviewNotice').isVisible(), true);
    await Switch('#MaskTab');
    Assert.equal(await Page.evaluate(() => localStorage.getItem('FrontierLandscape')), DocumentBefore);
    await Inspect('restore');
    await Page.waitForFunction(() => document.querySelector('#Volume').dataset.renderStatus === 'suspended');
    const Deferred = await Inspect();
    Assert.equal(Deferred.ContextLost, false);
    Assert.equal(Deferred.gpuDraws, (await Inspect()).gpuDraws);
    await Switch('#LandscapeTab');
    await FirstTerrain(Number(BeforeLoss.terrainRenders), Number(BeforeLoss.initializationEpoch));
    Assert.equal(await Page.evaluate(() => localStorage.getItem('FrontierLandscape')), DocumentBefore);
    await Verify('real context loss is prevented, restoration in Mask stays suspended, Landscape rebuilds fresh resources', { BeforeEpoch: BeforeLoss.initializationEpoch, Recovered: await Inspect(), DocumentPreserved: true });
    await Page.locator('[data-inspector="ocean"]').click();
    await Page.locator('#AnimateOcean').uncheck();
    await Page.locator('#RenderQuality').selectOption('256');
    await Page.waitForFunction(() =>
    {
        const Canvas = document.querySelector('#Volume');
        return Canvas.width === 256 && Canvas.dataset.completedComposition === Canvas.dataset.submittedComposition;
    });
    for (let Index = 0; Index < 8; Index++) { await Switch('#MaskTab'); await Switch('#LandscapeTab'); }
    const Reused = await Inspect();
    Assert.equal(GraphicsThreads.length, 1);
    Assert.equal(Reused.graphicsContexts, '1');
    await Verify('eight Mask/Landscape cycles reuse the same rendering thread and WebGL context', { GraphicsThreads, Contexts: Reused.graphicsContexts });
    const SecondLoss = await Lose();
    await Inspect('restore');
    await FirstTerrain(Number(SecondLoss.terrainRenders), Number(SecondLoss.initializationEpoch));
    const InteractiveTelemetry = await Page.evaluate(() => window.PreviewTelemetry);
    Assert.equal(InteractiveTelemetry.MainThreadContexts, 0);
    Assert.equal(InteractiveTelemetry.MainThreadDraws, 0);
    Assert.ok(InteractiveTelemetry.MaximumHeartbeatMilliseconds < 1000, JSON.stringify(InteractiveTelemetry));
    await Verify('all WebGL calls stay off the UI thread through rendering, navigation and recovery', InteractiveTelemetry);
    // Captures intentionally follow the responsiveness measurement: CDP screenshot readback may stall a software GPU.
    await Page.waitForFunction(() =>
    {
        const Canvas = document.querySelector('#Volume');
        return Canvas.width === 256 && Canvas.dataset.completedComposition === Canvas.dataset.submittedComposition;
    });
    await Page.screenshot({ path: Directory + 'Images/ResponsiveLandscape.png' });
    await Lose();
    await Inspect('restore');
    await Page.waitForFunction(() =>
    {
        const Canvas = document.querySelector('#Volume');
        return Canvas.dataset.renderMode === 'software' && Number(Canvas.dataset.softwareImages) > 0 &&
            Canvas.dataset.completedRevision === Canvas.dataset.volumeRevision;
    });
    Assert.match(await Page.locator('#Volume').getAttribute('data-software-reason'), /reset repeatedly/);
    Assert.ok((await Inspect('pixels')).LandPixels > 0);
    const Stopped = await Inspect();
    await Page.waitForTimeout(300);
    Assert.equal((await Inspect()).gpuDraws, Stopped.gpuDraws);
    Assert.equal(await Page.evaluate(() => localStorage.getItem('FrontierLandscape')), DocumentBefore);
    await Page.screenshot({ path: Directory + 'Images/GraphicsRecovery.png' });
    await Switch('#MaskTab');
    Assert.equal(await Page.locator('#Paint').isVisible(), true);
    const DownloadRequest = Page.waitForEvent('download');
    await Page.locator('#Export').click();
    const Download = await DownloadRequest;
    const Chunks = [];
    for await (const Chunk of await Download.createReadStream()) Chunks.push(Chunk);
    Assert.deepEqual(JSON.parse(Buffer.concat(Chunks).toString()), JSON.parse(DocumentBefore));
    await Verify('third real graphics reset stops GPU retries and automatically presents software terrain while preserving Save project', { ContextLosses: Stopped.contextLosses, DocumentPreserved: true, Export: Download.suggestedFilename() });
    Assert.deepEqual(Errors, []);
    Assert.deepEqual(FailedResources, []);
    Results.Browser = Browser.version();
    Results.Renderer = Appearance.renderer;
    await Browser.close();

    UnavailableBrowser = await chromium.launch({ ...BrowserOptions, args: ['--no-sandbox', '--disable-dev-shm-usage', '--disable-gpu', '--disable-software-rasterizer', '--no-zygote'] });
    const Unavailable = await UnavailableBrowser.newPage({ viewport: { width: 1200, height: 820 } });
    const UnavailableErrors = [];
    Unavailable.on('pageerror', Failure => UnavailableErrors.push(Failure.message));
    // Native no-WebGL launch plus explicitly absent OffscreenCanvas: this still has to display terrain.
    await Unavailable.addInitScript(() => Object.defineProperty(HTMLCanvasElement.prototype, 'transferControlToOffscreen', { value: undefined }));
    await Unavailable.goto(Results.Origin);
    await Unavailable.locator('#LandscapeTab').click();
    await Unavailable.waitForFunction(() => Number(document.querySelector('#Volume').dataset.softwareImages) > 0);
    const UnavailableImage = await InspectFor(Unavailable);
    Assert.ok(UnavailableImage.LandPixels > 0);
    Assert.equal(UnavailableImage.renderMode, 'software');
    await Unavailable.locator('#MaskTab').click();
    Assert.equal(await Unavailable.locator('#Paint').isVisible(), true);
    Assert.equal(await Unavailable.locator('#Export').isEnabled(), true);
    Assert.deepEqual(UnavailableErrors, []);
    await Verify('native disabled WebGL without OffscreenCanvas presents software terrain and preserves Mask authoring', { WebGLDisabled: true, OffscreenDisabled: true, PageErrors: UnavailableErrors });
    // Inject context-allocation failure into the actual graphics thread, rather than pretending a window-only browser flag disables it.
    const ThreadFailure = await UnavailableBrowser.newPage({ viewport: { width: 1200, height: 820 } });
    const ThreadErrors = [];
    ThreadFailure.on('pageerror', Failure => ThreadErrors.push(Failure.message));
    await ThreadFailure.route('**/VolumeSubmission.js*', async Route =>
    {
        const Response = await Route.fetch();
        const Prefix = "const NativeContext = OffscreenCanvas.prototype.getContext; OffscreenCanvas.prototype.getContext = function(Category, ...Options) { return Category === 'webgl2' ? null : NativeContext.call(this, Category, ...Options); };\n";
        await Route.fulfill({ response: Response, body: Prefix + await Response.text() });
    });
    await ThreadFailure.goto(Results.Origin);
    await ThreadFailure.locator('#LandscapeTab').click();
    await ThreadFailure.waitForFunction(() => Number(document.querySelector('#Volume').dataset.softwareImages) > 0);
    const ThreadImage = await InspectFor(ThreadFailure);
    Assert.ok(ThreadImage.LandPixels > 0);
    Assert.equal(ThreadImage.renderMode, 'software');
    Assert.equal(await ThreadFailure.locator('#Volume').getAttribute('data-render-execution'), 'thread');
    await ThreadFailure.locator('#MaskTab').click();
    Assert.equal(await ThreadFailure.locator('#Paint').isVisible(), true);
    Assert.deepEqual(ThreadErrors, []);
    await Verify('injected graphics-thread allocation failure automatically presents real software terrain', { SimulatedContextAllocationFailure: true, PageErrors: ThreadErrors });
    Results.Status = 'passed';
    await rm(Directory + 'Images/PreviewFailure.png', { force: true });
}
catch (Failure)
{
    Results.Status = 'failed';
    Results.Failure = Failure.stack;
    Results.Errors = Errors;
    Results.Telemetry = await Page.evaluate(() => window.PreviewTelemetry).catch(() => null);
    Results.Canvas = await Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset })).catch(() => null);
    await Page.screenshot({ path: Directory + 'Images/PreviewFailure.png' }).catch(() => {});
    console.error(Failure);
    process.exitCode = 1;
}
finally
{
    await Browser.close();
    await UnavailableBrowser?.close();
    await writeFile(Directory + 'Results/PreviewValidation.json', JSON.stringify(Results, null, 2) + '\n');
}
