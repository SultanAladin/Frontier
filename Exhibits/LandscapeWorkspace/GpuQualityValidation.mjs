/*==========================================================================================================================================
                                                        GPUQUALITYVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Native-resolution refinement and actual GPU ocean-uniform invariance across dolly distances; no driver or source overrides.

import Assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { CreateCoastalWorld } from '../../Experimental/LandscapeWorkspace/CoastalWorld.js';
import {
    GraphicsArguments, ConstructGpuReport, ObserveGpuBrowser, WaitForGpuImage, ReadGpuPixels,
    SaveAuthoringDocument, VerifyGpuCheck
} from './GpuValidationEnvironment.mjs';

const Origin = process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173';
const Prefix = process.env.LANDSCAPE_REPORT_PREFIX || 'GpuQualityValidation';
const Report = await ConstructGpuReport(Origin);
const Results = new URL('./Results/', import.meta.url), Images = new URL('./Images/', import.meta.url);
await mkdir(Results, { recursive: true });
await mkdir(Images, { recursive: true });
const Browser = await chromium.launch({
    executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined, headless: true, args: GraphicsArguments
});
const Verify = (Name, Observed) => VerifyGpuCheck(Report, Name, Observed);
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1 });
const Errors = [];
Page.on('pageerror', Failure => Errors.push(Failure.message));
Page.on('console', Message => { if (Message.type() === 'error') Errors.push(Message.text()); });
await ObserveGpuBrowser(Page);

async function InspectOcean()
{
    return Page.evaluate(() => document.querySelector('#Volume')[Symbol.for('FrontierLandscape.Presentation')].Inspect('ocean'));
}

async function Capture(Suffix)
{
    const Name = Prefix + Suffix + '.png';
    await Page.screenshot({ path: new URL(Name, Images).pathname });
    Report.Images.push(Name);
}

try
{
    await Page.goto(Origin, { waitUntil: 'networkidle' });
    await Page.locator('#LandscapeTab').click();
    await WaitForGpuImage(Page);
    await Page.waitForFunction(() =>
    {
        const D = document.querySelector('#Volume').dataset;
        return ['failed', 'lost'].includes(D.renderStatus) || D.previewDensitySettled === 'true' && D.completedComposition === D.submittedComposition;
    }, null, { polling: 50, timeout: 18000 });
    const Refined = await WaitForGpuImage(Page);
    const Pixels = await ReadGpuPixels(Page);
    Assert.equal(Refined.previewWidth, '1440');
    Assert.equal(Number(Refined.completedQuality), 96);
    Assert.ok(Number(Refined.surfaceTriangles) > 96798 && Number(Refined.surfaceTriangles) <= 120000);
    Assert.ok(JSON.parse(Refined.displayedSurfaceRefinement).ProjectedVertices > 30000);
    Assert.ok(Pixels.Width * Pixels.Height > 360000);
    Assert.ok(Pixels.Width * Pixels.Height <= 1500000);
    Assert.ok(Pixels.Width >= 800);
    Assert.equal(Pixels.Error, 0);
    Assert.deepEqual((await SaveAuthoringDocument(Page)).Layers, CreateCoastalWorld());
    await Capture('');
    Verify('always-high mode displays an actually field-refined genuine XYZ surface without changing the authored coastal document',
        { Width: Pixels.Width, Height: Pixels.Height, Triangles: Refined.surfaceTriangles, Samples: Refined.surfaceSamples,
            PitchMetres: Refined.surfacePitch, DensityBudget: Refined.previewPixelBudget, GpuMilliseconds: Refined.gpuImageMilliseconds,
            FenceMilliseconds: Refined.lastFenceMilliseconds, LandPixels: Pixels.LandPixels });

    const Original = await InspectOcean();
    Assert.equal(Original.Error, 0);
    Assert.equal(Original.Extent, 8000);
    Assert.equal(Original.Spectrum.length, 96);
    Assert.equal(Original.Phases.length, 48);
    const Zooms = [];
    for (const Delta of [-220, 440, -220])
    {
        const Previous = await Page.locator('#Volume').getAttribute('data-completed-composition');
        await Page.locator('#Volume').evaluate((Canvas, Delta) => Canvas.dispatchEvent(new WheelEvent('wheel',
            { deltaY: Delta, bubbles: true, cancelable: true })), Delta);
        await Page.waitForFunction(Previous => document.querySelector('#Volume').dataset.completedComposition !== Previous,
            Previous, { polling: 50 });
        const Current = await WaitForGpuImage(Page);
        const Ocean = await InspectOcean();
        Assert.deepEqual(Ocean, Original, 'camera distance must not retune the live GPU wave spectrum, time, phase or grid scale');
        Assert.equal(Current.geometryCompletedRevision, Refined.geometryCompletedRevision);
        Assert.equal(Current.surfaceTriangles, Refined.surfaceTriangles);
        Zooms.push({ Orbit: JSON.parse(Current.completedComposition)[0].Orbit, Ocean, Width: Current.bufferWidth,
            Height: Current.bufferHeight, GpuMilliseconds: Current.gpuImageMilliseconds });
    }
    Verify('actual linked-GPU wave amplitudes, wavelengths, phases, time and world grid remain identical through three camera dolly distances', Zooms);
    await Capture('Water');

    const Observation = await Page.evaluate(() => ({
        Threads: window.GpuValidation.Threads.filter(Thread => Thread.Graphics).length,
        CpuPixelCopies: window.GpuValidation.CpuPixelCopies, MainThreadGraphicsContexts: window.GpuValidation.MainThreadGraphicsContexts
    }));
    Assert.deepEqual(Observation, { Threads: 1, CpuPixelCopies: 0, MainThreadGraphicsContexts: 0 });
    Assert.deepEqual(Errors, []);
    Verify('quality refinement and zoom retain one GPU thread, no CPU pixel copies, no UI-thread GL and no browser errors', { Observation, Errors });
    Report.Status = 'passed';
}
catch (Failure)
{
    Report.Status = 'failed';
    Report.Failure = Failure.stack;
    Report.PreviewAtFailure = await Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset })).catch(() => null);
    await Capture('Failure').catch(() => {});
    process.exitCode = 1;
    console.error(Failure);
}
finally
{
    await Browser.close();
    await writeFile(new URL(Prefix + '.json', Results), JSON.stringify(Report, null, 2) + '\n');
    console.log(JSON.stringify({ Status: Report.Status, Checks: Report.Checks.length }, null, 2));
}
