/*==========================================================================================================================================
                                                       GPUPREVIEWSAMPLINGVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Reproduce the user's 2048-target coarse overview, inspect completed GPU sampling, and verify usable local working zoom.

import Assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { ConstructVolumeLattice } from '../../Experimental/LandscapeWorkspace/VolumeLattice.js';
import { PreviewSamplingRevision } from '../../Experimental/LandscapeWorkspace/PreviewSampling.js';
import {
    GraphicsArguments, ConstructGpuReport, ObserveGpuBrowser, WaitForGpuImage, ReadGpuPixels,
    SaveAuthoringDocument, VerifyGpuCheck
} from './GpuValidationEnvironment.mjs';

const Origin = process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173';
const Prefix = process.env.LANDSCAPE_REPORT_PREFIX || 'GpuPreviewSampling';
const Report = await ConstructGpuReport(Origin);
Report.Conditions.Scope = 'Always-high field-projected geometry and truthful native working pitch, image dimensions and diagnostics.';
const Results = new URL('./Results/', import.meta.url), Images = new URL('./Images/', import.meta.url);
await mkdir(Results, { recursive: true });
await mkdir(Images, { recursive: true });
const Browser = await chromium.launch({ executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined, headless: true, args: GraphicsArguments });
const Page = await Browser.newPage({ viewport: { width: 1050, height: 720 }, deviceScaleFactor: 1 });
Page.setDefaultTimeout(18000);
const Errors = [];
Page.on('pageerror', Failure => Errors.push(Failure.message));
Page.on('console', Message => { if (Message.type() === 'error') Errors.push(Message.text()); });
await ObserveGpuBrowser(Page);
const Verify = (Name, Observed) => VerifyGpuCheck(Report, Name, Observed);
const Read = () => Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset }));

async function Capture(Suffix)
{
    const Name = Prefix + Suffix + '.png';
    await Page.screenshot({ path: new URL(Name, Images).pathname });
    Report.Images.push(Name);
}

async function WaitForWorking()
{
    await Page.waitForFunction(() =>
    {
        const D = document.querySelector('#Volume').dataset;
        return ['failed', 'lost'].includes(D.renderStatus) || D.completedDetailKey &&
            D.completedDetailKey === D.requestedDetailKey && D.completedPreparationEpoch === D.geometryPreparationEpoch &&
            D.completedRevision === D.volumeRevision && D.completedComposition === D.submittedComposition &&
            D.previewDensitySettled === 'true' && D.displayedSamplingMatchesTarget === 'true';
    }, null, { polling: 50 });
    const D = await Read();
    Assert.equal(D.renderStatus, 'ready', D.renderMessage);
    Assert.ok(Number(D.displayedSurfaceTriangles) <= 120000);
    Assert.ok(Number(D.surfaceSamples) <= 1100000);
    Assert.equal(D.maximumPendingGpuImages, '1');
    return D;
}

try
{
    await Page.goto(Origin, { waitUntil: 'networkidle' });
    await Page.locator('#LandscapeTab').click();
    await WaitForGpuImage(Page);
    await Page.locator('[data-inspector="region"]').click();
    await Page.locator('#Resolution').selectOption('2048');
    await Page.locator('#Vertical').selectOption('512');
    let Overview = await WaitForGpuImage(Page);
    await Page.waitForFunction(() => document.querySelector('#Volume').dataset.displayedSamplingMode === 'high');
    Assert.equal(Overview.completedQuality, '96');
    Assert.equal(Overview.completedDetailKey, '');
    Assert.equal(Overview.displayedWorkingLattice, 'null');
    Assert.equal(JSON.parse(Overview.displayedSurfaceRefinement).Method, 'projected-adaptive-xyz');
    Assert.ok(Number(Overview.displayedSurfaceTriangles) > 96798 && Number(Overview.displayedSurfaceTriangles) <= 120000);
    Assert.match(await Page.locator('#SamplingHeading').textContent(), /High detail/);
    Assert.match(await Page.locator('#SamplingActual').textContent(), /Displayed adaptive geometry/);
    Assert.match(await Page.locator('#SamplingTarget').textContent(), /2048 × 512 × 2048/);
    Assert.equal(await Page.locator('#WorkingReadout').isVisible(), false);
    await Page.locator('#SamplingActual').scrollIntoViewIfNeeded();
    await Capture('Overview');
    Verify('2048 × 512 × 2048 keeps always-high field-refined geometry, not a falsely native full-island lattice', Overview);

    Assert.equal(await Page.locator('#RenderQuality').isVisible(), false);
    Assert.equal(await Page.locator('#RenderQuality').inputValue(), '1440');
    Overview = await WaitForGpuImage(Page);
    Assert.equal(JSON.parse(Overview.displayedSurfaceRefinement).Method, 'projected-adaptive-xyz');
    Assert.ok(Number(Overview.displayedSurfaceTriangles) > 96798 && Number(Overview.displayedSurfaceTriangles) <= 120000);
    Assert.equal(await Page.locator('#RenderQuality').getAttribute('aria-label'), 'Image pixel width');
    const Pixels = await ReadGpuPixels(Page);
    Assert.equal(Number(Overview.displayedWidth), Pixels.Width);
    Assert.equal(Number(Overview.displayedHeight), Pixels.Height);
    Assert.equal(Pixels.Error, 0);
    Assert.ok(Pixels.LandPixels > 500);
    Verify('1440 is an image-width ceiling, independently measured from actual GPU geometry spacing and completed framebuffer dimensions', Pixels);

    await Page.locator('#PreviewDiagnostics').evaluate(Details => Details.open = true);
    await Page.locator('#SamplingRenderer').scrollIntoViewIfNeeded();
    Assert.equal(await Page.locator('#SamplingRenderer').textContent(), Overview.renderer);
    Assert.equal(await Page.locator('#SamplingRevision').textContent(), PreviewSamplingRevision);
    Assert.match(await Page.locator('#SamplingExecution').textContent(), /Browser-side WebGL2/);
    Assert.match(await Page.locator('#SamplingRevisions').textContent(), new RegExp(`displayed ${Overview.completedRevision}`));
    await Capture('Diagnostics');
    Verify('diagnostics report the actual browser driver, completed document revision and readout version rather than guessing cache or hardware',
        { Renderer: Overview.renderer, Revisions: await Page.locator('#SamplingRevisions').textContent(), Revision: PreviewSamplingRevision });

    const Document = await SaveAuthoringDocument(Page);
    const Lattice = ConstructVolumeLattice(Document.Layers[0], Document.Layers[0].Regions[0]);
    await Page.locator('#WorkAtResolution').click();
    const Working = await WaitForWorking();
    const Specification = JSON.parse(Working.displayedWorkingLattice), Camera = JSON.parse(Working.completedComposition)[0];
    Assert.deepEqual(Specification.Dimensions, [2048, 512, 2048]);
    Assert.deepEqual(Specification.Pitch, Lattice.Pitch);
    Assert.equal(Specification.Resident, true);
    Assert.ok(Camera.Orbit < 25 && Camera.Orbit >= .5);
    Assert.equal(await Page.locator('#FieldBadge').textContent(), 'WORKING PATCH');
    Assert.match(await Page.locator('#SamplingActual').textContent(), /Displayed patch:/);
    Assert.match(await Page.locator('#SamplingGuidance').textContent(), /Surroundings remain coarser/);
    await Page.locator('#SamplingActual').scrollIntoViewIfNeeded();
    await Capture('Working');
    const WorkingPixels = await ReadGpuPixels(Page);
    Assert.equal(WorkingPixels.Error, 0);
    Assert.notEqual(WorkingPixels.Hash, Pixels.Hash);
    Verify('the prominent detail action displays a bounded native-pitch 2048-target patch and focuses closer than the former 25-metre floor',
        { Camera, Specification, Pixels: WorkingPixels, Telemetry: Working });

    const BeforeZoom = Camera.Orbit;
    await Page.locator('#Volume').dispatchEvent('wheel', { deltaY: -1000, bubbles: true });
    await Page.waitForFunction(Previous =>
    {
        const D = document.querySelector('#Volume').dataset;
        return D.completedComposition && JSON.parse(D.completedComposition)[0].Orbit < Previous * .8 &&
            D.completedComposition === D.submittedComposition;
    }, BeforeZoom, { polling: 50 });
    const Zoomed = await WaitForWorking(), ZoomCamera = JSON.parse(Zoomed.completedComposition)[0];
    Assert.ok(ZoomCamera.Orbit < BeforeZoom && ZoomCamera.Orbit >= .5);
    Assert.equal(Zoomed.completedPreparationEpoch, Working.completedPreparationEpoch);
    Assert.equal(Zoomed.completedDetailKey, Working.completedDetailKey);
    Assert.notEqual(await Page.locator('#ScaleRead').textContent(), '0 m');
    Assert.deepEqual(await SaveAuthoringDocument(Page), Document);
    Verify('working wheel zoom continues below 25 metres, keeps the resident geometry, reports a nonzero scale and leaves the authored document unchanged',
        { BeforeOrbit: BeforeZoom, AfterOrbit: ZoomCamera.Orbit, Scale: await Page.locator('#ScaleRead').textContent(), PreparationEpoch: Zoomed.completedPreparationEpoch });

    await Page.locator('#WorkingOverview').click();
    await Page.waitForFunction(() =>
    {
        const D = document.querySelector('#Volume').dataset;
        return !D.completedDetailKey && !D.requestedDetailKey && D.displayedSamplingMode === 'high' &&
            D.geometryPreparationEpoch === D.geometryCompletedEpoch && D.completedPreparationEpoch === D.geometryCompletedEpoch;
    }, null, { polling: 50 });
    const Returned = await WaitForGpuImage(Page, 64);
    Assert.equal(Returned.displayedWorkingLattice, 'null');
    Assert.equal(JSON.parse(Returned.displayedSurfaceRefinement).Method, 'projected-adaptive-xyz');
    Assert.equal(Returned.completedDetailKey, '');
    Assert.equal(await Page.locator('#FieldBadge').textContent(), 'HIGH DETAIL');
    const Ownership = await Page.evaluate(() => ({ Threads: window.GpuValidation.Threads.filter(Thread => Thread.Graphics).length,
        CpuPixelCopies: window.GpuValidation.CpuPixelCopies, MainThreadGraphicsContexts: window.GpuValidation.MainThreadGraphicsContexts }));
    Assert.deepEqual(Ownership, { Threads: 1, CpuPixelCopies: 0, MainThreadGraphicsContexts: 0 });
    Assert.deepEqual(Errors, []);
    Verify('returning to the whole-scene view restores high geometry with one graphics owner and no application CPU pixel fallback', { Returned, Ownership, Errors });
    Report.Status = 'passed';
}
catch (Failure)
{
    Report.Status = 'failed';
    Report.Failure = Failure.stack;
    Report.PreviewAtFailure = await Read().catch(() => null);
    Report.EditorStatus = await Page.locator('#Status').textContent().catch(() => null);
    await Capture('Failure').catch(() => {});
    console.error(Failure);
    process.exitCode = 1;
}
finally
{
    await Browser.close();
    await writeFile(new URL(Prefix + '.json', Results), JSON.stringify(Report, null, 2) + '\n');
    console.log(JSON.stringify({ Status: Report.Status, Checks: Report.Checks.length }, null, 2));
}
