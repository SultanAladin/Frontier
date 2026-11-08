/*==========================================================================================================================================
                                                        GPUDIALOGVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Native blocking-dialog diagnostic versus actual asynchronous new-world, paint, allocation and GPU presentation.

import Assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { GraphicsArguments, ConstructGpuReport, ObserveGpuBrowser, WaitForGpuImage, ReadGpuPixels } from './GpuValidationEnvironment.mjs';

const Origin = process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173';
const Report = await ConstructGpuReport(Origin);
const Prefix = process.env.LANDSCAPE_REPORT_PREFIX || 'GpuDialogValidation';
const EditorOnly = process.env.LANDSCAPE_DIALOG_MODE === 'editor';
const Results = new URL('./Results/', import.meta.url), Images = new URL('./Images/', import.meta.url);
await mkdir(Results, { recursive: true });
await mkdir(Images, { recursive: true });
Report.Conditions.Scope = EditorOnly ? 'Isolated actual HTML confirmation, new-world, painting, allocation and GPU resume. No native dialog injected; no source or deadline changes.' : 'Diagnostic adds a real synchronous native dialog in one fresh page only. Neither application sources nor graphics APIs/deadlines are replaced. The actual application uses HTML dialog in both pages. Native-dialog behaviour is browser-specific, not a device diagnosis.';

async function ValidateDecision(NativeDialog)
{
    const Browser = await chromium.launch({
        executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined, headless: true, args: GraphicsArguments
    });
    try
    {
        const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 } });
        let NativeDialogs = 0;
        Page.on('dialog', async Dialog => { NativeDialogs++; await Dialog.accept(); });
        await ObserveGpuBrowser(Page);
        await Page.goto(Origin, { waitUntil: 'networkidle' });
        await Page.locator('#LandscapeTab').click();
        await WaitForGpuImage(Page);
        await Page.locator('#MaskTab').click();
        await Page.locator('#Empty').click();
        await Page.locator('#ConfirmReplacement').click();
        await Page.waitForFunction(() => document.querySelectorAll('#Layers .mask-row').length === 1, null, { polling: 50 });
        if (NativeDialog) await Page.evaluate(() => confirm('Native blocking-dialog diagnostic; no document or graphics API changes.'));
        const Bounds = await Page.locator('#Paint').boundingBox();
        await Page.locator('[data-tool="paint"]').click();
        await Page.mouse.move(Bounds.x + Bounds.width * .42, Bounds.y + Bounds.height * .5);
        await Page.mouse.down();
        await Page.mouse.move(Bounds.x + Bounds.width * .57, Bounds.y + Bounds.height * .47, { steps: 14 });
        await Page.mouse.up();
        await Page.locator('[data-tool="select"]').click();
        await Page.mouse.move(Bounds.x + Bounds.width * .28, Bounds.y + Bounds.height * .3);
        await Page.mouse.down();
        await Page.mouse.move(Bounds.x + Bounds.width * .72, Bounds.y + Bounds.height * .7, { steps: 8 });
        await Page.mouse.up();
        await Page.locator('#KeepRegion').click();
        await Page.locator('#LandscapeTab').click();
        await Page.locator('#FocusMask').click();
        await Page.waitForFunction(() =>
        {
            const D = document.querySelector('#Volume').dataset;
            return ['failed', 'lost'].includes(D.renderStatus) || D.renderStatus === 'ready' &&
                D.completedRevision === D.volumeRevision && Number(D.completedQuality) >= 48 && D.completedQuality === D.geometryCompletedQuality && D.previewDensitySettled === 'true' && D.completedComposition === D.submittedComposition;
        }, null, { polling: 50, timeout: 18000 });
        const Dataset = await Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset }));
        const Observation = await Page.evaluate(() => ({
            Threads: window.GpuValidation.Threads, CpuPixelCopies: window.GpuValidation.CpuPixelCopies,
            MainThreadGraphicsContexts: window.GpuValidation.MainThreadGraphicsContexts
        }));
        const Image = Prefix + (NativeDialog ? '-Native.png' : '-Editor.png');
        await Page.screenshot({ path: new URL(Image, Images).pathname });
        Report.Images.push(Image);
        const Decision = { NativeDialogIntroducedByValidation: NativeDialog, NativeDialogs, Dataset, Observation, Pixels: null };
        if (NativeDialog) Report.NativeDialogDiagnostic = Decision;
        else Report.EditorDecision = Decision;
        let Pixels = null;
        if (!NativeDialog)
        {
            Assert.equal(Dataset.renderStatus, 'ready', Dataset.renderMessage);
            Assert.ok(Number(Dataset.completedQuality) >= 48);
            Pixels = await ReadGpuPixels(Page);
            Assert.equal(Pixels.Error, 0);
            Assert.ok(Pixels.LandPixels > 300);
            Assert.equal(NativeDialogs, 0);
            Assert.equal(Observation.Threads.length, 1);
            Assert.equal(Observation.Threads[0].Terminated, false);
            Assert.equal(Observation.CpuPixelCopies, 0);
            Assert.equal(Observation.MainThreadGraphicsContexts, 0);
        }
        Decision.Pixels = Pixels && { Width: Pixels.Width, Height: Pixels.Height, LandPixels: Pixels.LandPixels, Hash: Pixels.Hash };
        return Decision;
    }
    finally { await Browser.close(); }
}

try
{
    if (!EditorOnly)
    {
        Report.NativeDialogDiagnostic = await ValidateDecision(true);
        Report.NativeDialogResult = Report.NativeDialogDiagnostic.Dataset.renderStatus === 'failed' ? 'reproduced' : 'not reproduced on this browser run';
    }
    else Report.NativeDialogResult = 'not run (isolated editor confirmation)';
    Report.EditorDecision = await ValidateDecision(false);
    Report.Status = 'passed';
    console.log(JSON.stringify({ Status: Report.Status, NativeDialogResult: Report.NativeDialogResult,
        Editor: Report.EditorDecision.Dataset.renderStatus }, null, 2));
}
catch (Failure)
{
    Report.Status = 'failed';
    Report.Failure = Failure.stack;
    process.exitCode = 1;
    console.error(Failure);
}
finally { await writeFile(new URL(Prefix + '.json', Results), JSON.stringify(Report, null, 2) + '\n'); }
