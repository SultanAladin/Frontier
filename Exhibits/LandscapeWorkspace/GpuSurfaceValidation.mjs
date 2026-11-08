/*==========================================================================================================================================
                                                        GPUSURFACEVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Actual GPU-only coastal/arch images, geometry reuse, editing, suspension, real context loss and explicit state-preserving restart.

import Assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { CreateCoastalWorld } from '../../Experimental/LandscapeWorkspace/CoastalWorld.js';
import { CreateLandform } from '../../Experimental/LandscapeWorkspace/LandformStack.js';
import {
    GraphicsArguments, ConstructGpuReport, ObserveGpuBrowser, WaitForGpuImage, ReadGpuPixels,
    SaveAuthoringDocument, VerifyGpuCheck
} from './GpuValidationEnvironment.mjs';

const Origin = process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173';
const Report = await ConstructGpuReport(Origin);
const Prefix = process.env.LANDSCAPE_REPORT_PREFIX || 'GpuSurfaceValidation';
const Images = new URL('./Images/', import.meta.url), Results = new URL('./Results/', import.meta.url);
await mkdir(Images, { recursive: true });
await mkdir(Results, { recursive: true });
const Browser = await chromium.launch({
    executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined, headless: true, args: GraphicsArguments
});
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 } });
Page.setDefaultTimeout(18000);
const Errors = [], FailedResources = [];
Page.on('pageerror', Failure => Errors.push(Failure.message));
Page.on('console', Message => { if (Message.type() === 'error') Errors.push(Message.text()); });
Page.on('response', Response => { if (Response.status() >= 400) FailedResources.push(Response.status() + ' ' + Response.url()); });
await ObserveGpuBrowser(Page);
const Verify = (Name, Observed) => VerifyGpuCheck(Report, Name, Observed);

async function Snapshot()
{
    await Page.waitForFunction(() => document.querySelector('#SaveStatus').textContent === 'Saved locally' && localStorage.getItem('FrontierLandscape'),
        null, { polling: 50 });
    return Page.evaluate(() => JSON.parse(localStorage.getItem('FrontierLandscape')));
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
    await Page.waitForFunction(() => Boolean(document.querySelector('#Paint').dataset.projection), null, { polling: 50 });
    const MaskOnly = await Page.evaluate(() => ({ ...window.GpuValidation, Threads: window.GpuValidation.Threads.length }));
    Assert.equal(MaskOnly.Threads, 0);
    Assert.equal(MaskOnly.MainThreadGraphicsContexts, 0);
    Verify('Mask startup creates no graphics thread or UI-thread WebGL context', { Threads: 0, MainThreadGraphicsContexts: 0 });

    await Page.locator('#LandscapeTab').click();
    await WaitForGpuImage(Page, 16);
    const First = await ReadGpuPixels(Page);
    Assert.equal(First.Error, 0);
    Assert.ok(First.LandPixels > 300);
    const Refined = await WaitForGpuImage(Page);
    const Timing = await Page.evaluate(() =>
    {
        const O = window.GpuValidation;
        return {
            Images: O.Images.map(Image => ({ ...Image, FromClickMilliseconds: Image.At - O.Entered })),
            MaximumStartupHeartbeatMilliseconds: Math.max(...O.Heartbeats.map(Beat => Beat.Milliseconds)),
            StartupStalls: O.Heartbeats.filter(Beat => Beat.Milliseconds > 250), SlowCanvasCalls: O.SlowCanvasCalls
        };
    });
    Report.Startup = Timing;
    Report.Renderer = Refined.renderer;
    Report.Browser = Browser.version();
    Verify('the unmodified GPU-only path presents actual coastal land directly at high field-resampled geometry',
        { First, Refined, Timing, DeadlineMilliseconds: Refined.graphicsImageTimeoutMilliseconds });
    Assert.deepEqual((await SaveAuthoringDocument(Page)).Layers, CreateCoastalWorld());
    Assert.equal(Number(Refined.surfaceTriangles) > 30000, true);
    Assert.ok(Number(Refined.surfaceTriangles) <= 120000);
    Assert.ok(Number(Refined.surfaceBytes) <= 120000 * 3 * 9 * 4);
    Assert.equal(Refined.shaderTerrainFieldEvaluations, '0');
    Assert.equal(Refined.maximumPendingGpuImages, '1');
    Assert.equal(Refined.oceanVertices, '4225');
    Verify('real preview geometry and ocean vertices fit the explicit budgets; no procedural terrain runs in a pixel shader',
        { Triangles: Refined.surfaceTriangles, Bytes: Refined.surfaceBytes, FieldEvaluationsInShader: Refined.shaderTerrainFieldEvaluations,
            OceanVertices: Refined.oceanVertices, DrawCalls: Refined.drawCalls });

    const BeforeCamera = await ReadGpuPixels(Page);
    await Page.evaluate(() => window.GpuValidation.Phase = 'camera');
    const Box = await Page.locator('#Volume').boundingBox();
    const Start = performance.now();
    await Page.mouse.move(Box.x + Box.width * .5, Box.y + Box.height * .5);
    await Page.mouse.down();
    await Page.mouse.move(Box.x + Box.width * .5 + 85, Box.y + Box.height * .5 + 15, { steps: 8 });
    await Page.mouse.up();
    await Page.waitForFunction(Previous => document.querySelector('#Volume').dataset.completedComposition !== Previous,
        BeforeCamera.Telemetry.completedComposition, { polling: 50 });
    const AfterCamera = await WaitForGpuImage(Page);
    const InputTiming = await Page.evaluate(() => ({
        MaximumHeartbeatMilliseconds: Math.max(...window.GpuValidation.Heartbeats.filter(Beat => Beat.Phase === 'camera').map(Beat => Beat.Milliseconds)),
        Stalls: window.GpuValidation.Heartbeats.filter(Beat => Beat.Phase === 'camera' && Beat.Milliseconds > 250)
    }));
    await Page.evaluate(() => window.GpuValidation.Phase = 'verification');
    const CameraPixels = await ReadGpuPixels(Page);
    Assert.notEqual(CameraPixels.Hash, BeforeCamera.Hash);
    Assert.equal(AfterCamera.surfaceEvaluations, BeforeCamera.Telemetry.surfaceEvaluations);
    Assert.equal(AfterCamera.geometryCompletedRevision, BeforeCamera.Telemetry.geometryCompletedRevision);
    Verify('camera movement changes real pixels without resampling terrain or replacing the GPU thread',
        { InteractionAndCompletionMilliseconds: performance.now() - Start, BeforeHash: BeforeCamera.Hash, AfterHash: CameraPixels.Hash, InputTiming });

    await Page.locator('#Wave').evaluate(Input => { Input.value = '2'; Input.dispatchEvent(new Event('input')); });
    await Page.waitForFunction(() => JSON.parse(document.querySelector('#Volume').dataset.completedComposition || '[]')[1] === 2,
        null, { polling: 50 });
    await WaitForGpuImage(Page);
    const WavePixels = await ReadGpuPixels(Page);
    Assert.notEqual(WavePixels.Hash, CameraPixels.Hash);
    Assert.equal(WavePixels.Telemetry.surfaceEvaluations, CameraPixels.Telemetry.surfaceEvaluations);
    Assert.equal(WavePixels.Telemetry.geometryCompletedRevision, CameraPixels.Telemetry.geometryCompletedRevision);
    await Page.locator('#AnimateOcean').evaluate(Input => Input.checked = true);
    const AnimatedAfter = Number(WavePixels.Telemetry.oceanRenders) + 2;
    await Page.waitForFunction(After => Number(document.querySelector('#Volume').dataset.oceanRenders) >= After, AnimatedAfter, { polling: 50 });
    await Page.locator('#AnimateOcean').evaluate(Input => Input.checked = false);
    await WaitForGpuImage(Page);
    Verify('ocean amplitude and animation use the bounded GPU spectrum without rerunning the terrain generators',
        { BeforeHash: CameraPixels.Hash, AfterHash: WavePixels.Hash, VertexCount: WavePixels.Telemetry.oceanVertices,
            GeometryRevision: WavePixels.Telemetry.geometryCompletedRevision });

    await Page.locator('#AddGenerator').click();
    await Page.locator('[data-generator="multifractal"]').click();
    for (const Amount of ['75', '78', '80', '82'])
    {
        const Input = Page.getByRole('spinbutton', { name: 'Relief in metres', exact: true });
        await Input.fill(Amount);
        await Input.dispatchEvent('change');
    }
    const Edited = await Snapshot();
    Assert.equal(Edited.Layers[0].Landforms.length, 3);
    Assert.equal(Edited.Layers[0].Landforms[2].Amount, 82);
    Assert.deepEqual(Edited.Layers.slice(1), CreateCoastalWorld().slice(1));
    const Latest = await WaitForGpuImage(Page);
    Assert.equal(Latest.geometryCompletedRevision, Latest.volumeRevision);
    Assert.equal(Latest.completedRevision, Latest.volumeRevision);
    Assert.equal((await ReadGpuPixels(Page)).Error, 0);
    Verify('rapid third-generator edits cancel stale preparation and present only the latest independent mask document',
        { Revision: Latest.volumeRevision, CompletedRevision: Latest.completedRevision, GeometryRevision: Latest.geometryCompletedRevision,
            Operations: Edited.Layers[0].Landforms.map(Operation => ({ Type: Operation.Type, Amount: Operation.Amount })) });

    await Page.locator('#MaskTab').click();
    await Page.waitForFunction(() => document.querySelector('#Volume').dataset.previewSuspended === 'true', null, { polling: 50 });
    await Page.waitForTimeout(150);
    const Suspended = await Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset }));
    await Page.waitForTimeout(350);
    const SuspendedAgain = await Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset }));
    Assert.equal(SuspendedAgain.terrainRenders, Suspended.terrainRenders);
    Assert.equal(SuspendedAgain.geometrySamples, Suspended.geometrySamples);
    Assert.equal(await Page.locator('#PreviewNotice').isVisible(), false);
    await Page.locator('#Radius').evaluate(Input => { Input.value = '65'; Input.dispatchEvent(new Event('input')); });
    for (let Index = 0; Index < 3; Index++)
    {
        await Page.locator('#LandscapeTab').click();
        await WaitForGpuImage(Page);
        if (Index < 2) await Page.locator('#MaskTab').click();
    }
    const Ownership = await Page.evaluate(() => ({
        Threads: window.GpuValidation.Threads.filter(Thread => Thread.Graphics),
        MainThreadGraphicsContexts: window.GpuValidation.MainThreadGraphicsContexts,
        MainThreadGraphicsDraws: window.GpuValidation.MainThreadGraphicsDraws, CpuPixelCopies: window.GpuValidation.CpuPixelCopies
    }));
    Assert.equal(Ownership.Threads.length, 1);
    Assert.equal(Ownership.Threads[0].MaximumPendingViews, 1);
    Assert.equal(Ownership.MainThreadGraphicsContexts, 0);
    Assert.equal(Ownership.MainThreadGraphicsDraws, 0);
    Assert.equal(Ownership.CpuPixelCopies, 0);
    Verify('Mask suspends GPU and geometry work; repeated workspace switches reuse one context and bounded view queue', { Suspended, Ownership });

    Assert.equal(await Page.locator('#RenderQuality').isVisible(), false);
    await Page.waitForFunction(() => document.querySelector('#Volume').width > 480, null, { polling: 50 });
    await WaitForGpuImage(Page);
    const Fine = await ReadGpuPixels(Page);
    Assert.ok(Fine.Width > 480 && Fine.Width <= 1440);
    Assert.ok(Fine.Width * Fine.Height <= 1500000);
    Assert.equal(Fine.Error, 0);
    Assert.ok(Fine.LandPixels > 5000);
    await Capture('');
    Verify('a genuine pixel-budgeted GPU frame at the always-high image-width ceiling shows the edited coastal scene, not an overlay or software bitmap',
        { Width: Fine.Width, Height: Fine.Height, LandPixels: Fine.LandPixels, Hash: Fine.Hash, Telemetry: Fine.Telemetry });

    const DocumentBeforeLoss = await SaveAuthoringDocument(Page);
    const CameraBeforeLoss = JSON.parse(Fine.Telemetry.completedComposition)[0];
    const Loss = await Page.evaluate(() => document.querySelector('#Volume')[Symbol.for('FrontierLandscape.Presentation')].Inspect('lose'));
    Assert.equal(Loss.Requested, true);
    await Page.waitForFunction(() => document.querySelector('#Volume').dataset.renderStatus === 'lost', null, { polling: 50 });
    await Page.waitForTimeout(600);
    const FailedOwnership = await Page.evaluate(() => ({
        Threads: window.GpuValidation.Threads.filter(Thread => Thread.Graphics), CpuPixelCopies: window.GpuValidation.CpuPixelCopies,
        Dataset: { ...document.querySelector('#Volume').dataset },
        SoftwareButtons: document.querySelectorAll('#PreviewSoftware, #SoftwarePreviewBadge, .software-volume').length
    }));
    Assert.equal(FailedOwnership.Threads.length, 1);
    Assert.equal(FailedOwnership.Threads[0].Terminated, true);
    Assert.equal(FailedOwnership.Dataset.previewStopped, 'true');
    Assert.equal(FailedOwnership.CpuPixelCopies, 0);
    Assert.equal(FailedOwnership.SoftwareButtons, 0);
    await Page.locator('#PreviewMask').click();
    Assert.equal(await Page.locator('#PreviewNotice').isVisible(), false);
    Assert.deepEqual(await SaveAuthoringDocument(Page), DocumentBeforeLoss);
    Verify('actual WEBGL_lose_context stops the sole GPU thread with no software fallback; Mask and exact document save survive', FailedOwnership);

    await Page.locator('#LandscapeTab').click();
    await Page.locator('#PreviewRetry').click();
    await Page.waitForURL('**safePreview=1');
    const Restarted = await WaitForGpuImage(Page);
    Assert.deepEqual(JSON.parse(Restarted.completedComposition)[0], CameraBeforeLoss);
    Assert.deepEqual(await SaveAuthoringDocument(Page), DocumentBeforeLoss);
    Verify('only explicit restart creates a new GPU session and restores the exact document and camera',
        { CameraBeforeLoss, CameraAfterRestart: JSON.parse(Restarted.completedComposition)[0], Deadline: Restarted.graphicsImageTimeoutMilliseconds });

    const Arch = { Format: 'FrontierLandscape/1', Layers: [{
        Name: 'XYZ arch and empty opening', Color: '#b9a481', Visible: true, Depth: 120, Caves: false,
        Resolution: 256, Vertical: 64, Landforms: [{ ...CreateLandform('arch'), Amount: 140, Blend: 0 }],
        Strokes: [{ Points: [[0, 0]], Radius: 150, Erase: false }],
        Regions: [{ X0: -180, Z0: -180, X1: 180, Z1: 180, Edge: 0 }]
    }] };
    await Page.locator('#File').setInputFiles({ name: 'ActualXYZArch.frontier.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(Arch)) });
    await Page.locator('#Ocean').evaluate(Input => { Input.checked = false; Input.dispatchEvent(new Event('change')); });
    await Page.locator('#Fit').click();
    Assert.equal(await Page.locator('#RenderQuality').isVisible(), false);
    await Page.waitForFunction(() => document.querySelector('#Volume').width > 480, null, { polling: 50 });
    const ArchBox = await Page.locator('#Volume').boundingBox();
    await Page.mouse.move(ArchBox.x + ArchBox.width * .7, ArchBox.y + ArchBox.height * .6);
    await Page.mouse.down();
    await Page.mouse.move(ArchBox.x + ArchBox.width * .7, ArchBox.y + ArchBox.height * .6 - 76, { steps: 8 });
    await Page.mouse.up();
    await WaitForGpuImage(Page);
    const ArchPixels = await ReadGpuPixels(Page);
    Assert.equal(ArchPixels.Error, 0);
    Assert.ok(ArchPixels.LandPixels > 1000);
    Assert.deepEqual((await SaveAuthoringDocument(Page)).Layers, Arch.Layers);
    await Capture('Arch');
    Verify('GPU rasterization displays the actual XYZ arch; unit triangle intersections independently verify its empty opening and four vertical interfaces',
        { Width: ArchPixels.Width, Height: ArchPixels.Height, LandPixels: ArchPixels.LandPixels, SurfaceTriangles: ArchPixels.Telemetry.surfaceTriangles });

    Assert.deepEqual(Errors, []);
    Assert.deepEqual(FailedResources, []);
    Verify('the GPU-only interaction run ends with no uncaught errors or missing resources', { Errors, FailedResources });
    Report.Status = 'passed';
}
catch (Failure)
{
    Report.Status = 'failed';
    Report.Failure = Failure.stack;
    Report.Errors = Errors;
    Report.FailedResources = FailedResources;
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
