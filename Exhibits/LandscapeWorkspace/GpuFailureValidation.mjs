/*==========================================================================================================================================
                                                        GPUFAILUREVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Native no-WebGL and explicit API/silence faults fail closed without CPU rendering; authoring/save and no-RAF GPU views survive.

import Assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { CreateCoastalWorld } from '../../Experimental/LandscapeWorkspace/CoastalWorld.js';
import {
    GraphicsArguments, ConstructGpuReport, ObserveGpuBrowser, WaitForGpuImage, ReadGpuPixels,
    SaveAuthoringDocument, VerifyGpuCheck
} from './GpuValidationEnvironment.mjs';

const Origin = process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173';
const Report = await ConstructGpuReport(Origin);
const Prefix = process.env.LANDSCAPE_REPORT_PREFIX || 'GpuFailureValidation';
const Results = new URL('./Results/', import.meta.url), Images = new URL('./Images/', import.meta.url);
await mkdir(Results, { recursive: true });
await mkdir(Images, { recursive: true });
const Verify = (Name, Observed) => VerifyGpuCheck(Report, Name, Observed);

async function ValidateFailure(Mode, NativeNoGraphics = false)
{
    const Browser = await chromium.launch({
        executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined, headless: true,
        args: NativeNoGraphics ? ['--no-sandbox', '--disable-dev-shm-usage', '--disable-gpu', '--disable-software-rasterizer', '--no-zygote'] : GraphicsArguments
    });
    try
    {
        const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 } });
        const Errors = [];
        Page.on('pageerror', Failure => Errors.push(Failure.message));
        await ObserveGpuBrowser(Page, Mode);
        await Page.goto(Origin, { waitUntil: 'networkidle' });
        const Started = performance.now();
        await Page.locator('#LandscapeTab').click();
        await Page.waitForFunction(() => document.querySelector('#Volume').dataset.renderStatus === 'failed', null, { polling: 50, timeout: 15000 });
        const ElapsedMilliseconds = performance.now() - Started;
        await Page.waitForTimeout(250);
        const Observation = await Page.evaluate(() => ({
            Dataset: { ...document.querySelector('#Volume').dataset }, Threads: window.GpuValidation.Threads,
            CpuPixelCopies: window.GpuValidation.CpuPixelCopies,
            MainThreadGraphicsContexts: window.GpuValidation.MainThreadGraphicsContexts,
            SoftwareControls: document.querySelectorAll('#PreviewSoftware, #SoftwarePreviewBadge, .software-volume').length,
            Heading: document.querySelector('#PreviewHeading').textContent
        }));
        Assert.equal(Observation.Dataset.previewStopped, 'true');
        Assert.notEqual(Observation.Dataset.renderMode, 'software');
        Assert.equal(Observation.CpuPixelCopies, 0);
        Assert.equal(Observation.MainThreadGraphicsContexts, 0);
        Assert.equal(Observation.SoftwareControls, 0);
        Assert.equal(Observation.Heading, 'GPU preview stopped');
        const GraphicsThreads = Observation.Threads.filter(Thread => Thread.Graphics);
        Assert.equal(GraphicsThreads.length, ['missing-worker', 'missing-offscreen'].includes(Mode) ? 0 : 1);
        Assert.ok(GraphicsThreads.every(Thread => Thread.Terminated));
        const Image = Prefix + '-' + Mode + '.png';
        if (NativeNoGraphics)
        {
            await Page.screenshot({ path: new URL(Image, Images).pathname });
            Report.Images.push(Image);
        }
        await Page.locator('#PreviewMask').click();
        Assert.equal(await Page.locator('#PreviewNotice').isVisible(), false);
        Assert.equal(await Page.locator('#Paint').isVisible(), true);
        await Page.locator('#Radius').evaluate(Input => { Input.value = '72'; Input.dispatchEvent(new Event('input')); });
        Assert.deepEqual((await SaveAuthoringDocument(Page)).Layers, CreateCoastalWorld());
        Assert.deepEqual(Errors, []);
        return { Mode, NativeNoGraphics, ElapsedMilliseconds, Observation, ExactDocumentSaved: true, Errors,
            Injection: Mode === 'silent' ? 'A deliberately silent replacement thread; no actual GPU hang was induced.' :
                Mode === 'normal' ? 'Native browser graphics disabled by command-line flags; no fake context getters.' : 'Explicit browser API absence.' };
    }
    finally { await Browser.close(); }
}

try
{
    Verify('native missing WebGL stops promptly, never renders CPU pixels, and preserves Mask and exact save', await ValidateFailure('normal', true));
    Verify('missing OffscreenCanvas does not create a UI-thread GPU or CPU renderer', await ValidateFailure('missing-offscreen'));
    Verify('missing Worker stops before transferring or rendering the canvas', await ValidateFailure('missing-worker'));
    Verify('a silent graphics protocol hits the unchanged production deadline and terminates without starting another renderer', await ValidateFailure('silent'));

    const Browser = await chromium.launch({
        executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined, headless: true, args: GraphicsArguments
    });
    try
    {
        const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 } });
        await ObserveGpuBrowser(Page, 'no-animation');
        await Page.goto(Origin, { waitUntil: 'networkidle' });
        await Page.locator('#LandscapeTab').evaluate(Button => Button.click());
        await WaitForGpuImage(Page);
        const First = await ReadGpuPixels(Page);
        Assert.ok(First.LandPixels > 300);
        Assert.equal(First.Telemetry.viewSchedule, undefined);
        const BeforeComposition = First.Telemetry.completedComposition;
        await Page.locator('#Volume').evaluate(Canvas => Canvas.dispatchEvent(new WheelEvent('wheel', { deltaY: -120, bubbles: true, cancelable: true })));
        await Page.waitForFunction(Before => document.querySelector('#Volume').dataset.completedComposition !== Before, BeforeComposition,
            { polling: 50, timeout: 12000 });
        await WaitForGpuImage(Page);
        const Second = await ReadGpuPixels(Page);
        Assert.notEqual(Second.Hash, First.Hash);
        const Scheduling = await Page.locator('#Volume').getAttribute('data-view-schedule');
        Assert.equal(Scheduling, 'timer');
        const Observation = await Page.evaluate(() => ({
            Threads: window.GpuValidation.Threads.length, CpuPixelCopies: window.GpuValidation.CpuPixelCopies,
            MainThreadGraphicsContexts: window.GpuValidation.MainThreadGraphicsContexts
        }));
        Assert.deepEqual(Observation, { Threads: 1, CpuPixelCopies: 0, MainThreadGraphicsContexts: 0 });
        Verify('with every window animation callback suppressed, direct submission and the timer still produce GPU images and camera updates',
            { First: { Width: First.Width, Height: First.Height, Hash: First.Hash, LandPixels: First.LandPixels },
                Second: { Width: Second.Width, Height: Second.Height, Hash: Second.Hash }, Scheduling, Observation });
    }
    finally { await Browser.close(); }
    Report.Status = 'passed';
}
catch (Failure)
{
    Report.Status = 'failed';
    Report.Failure = Failure.stack;
    process.exitCode = 1;
    console.error(Failure);
}
finally
{
    await writeFile(new URL(Prefix + '.json', Results), JSON.stringify(Report, null, 2) + '\n');
    console.log(JSON.stringify({ Status: Report.Status, Checks: Report.Checks.length }, null, 2));
}
