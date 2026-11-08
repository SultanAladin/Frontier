/*==========================================================================================================================================
                                                      PREVIEWDEADLINEVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Recovery from a real WebGL-capable but deliberately blocked graphics thread, manual CPU selection and silent replacement threads.

import Assert from 'node:assert/strict';
import { mkdir, readFile, writeFile, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { GraphicsImageTimeoutMilliseconds, SoftwareThreadTimeoutMilliseconds } from '../../Experimental/LandscapeWorkspace/VolumePresentation.js';

const Directory = fileURLToPath(new URL('./', import.meta.url));
const Label = process.env.LANDSCAPE_RESULT || 'PreviewDeadlineValidation';
const Results = {
    Status: 'running', Date: new Date().toISOString(), Runtime: process.version, Platform: process.platform,
    Revision: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
    Branch: execFileSync('git', ['branch', '--show-current'], { encoding: 'utf8' }).trim(),
    Origin: process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173', Checks: [], SourceSHA256: {},
    Deadlines: { GraphicsImageTimeoutMilliseconds, SoftwareThreadTimeoutMilliseconds },
    Note: 'Real Chromium/SwiftShader WebGL2 allocation followed by an explicitly injected infinite loop in only the GPU worker. This reproduces no-error/no-image startup, not the physical cause on the user device. The software replacement has no transferred GL surface. An extra case explicitly silences that replacement to exercise local recovery. Uncommitted browser-prototype edits, not Windows hardware certification.'
};
await mkdir(Directory + 'Results', { recursive: true });
await mkdir(Directory + 'Images', { recursive: true });
for (const Name of ['Workspace.js', 'VolumePresentation.js', 'VolumeSubmission.js', 'VolumeRasterization.js', 'VolumeRayField.js', 'VolumeScalarRasterization.js', 'LandformStack.js', 'Workspace.css', 'index.html'])
    Results.SourceSHA256[Name] = createHash('sha256').update(await readFile(new URL('../../Experimental/LandscapeWorkspace/' + Name, import.meta.url))).digest('hex');
const Browser = await chromium.launch({
    executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined, headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--no-zygote']
});
Results.Browser = Browser.version();
const Errors = [], Pages = [];

function Verify(Name, Observed)
{
    Results.Checks.push({ Name, Status: 'passed', Observed });
    console.log('Verified: ' + Name);
}

async function Initialize(SilenceSoftware = false, AnimationCallbacks = true)
{
    const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1 });
    Pages.push(Page);
    Page.setDefaultTimeout(35000);
    // 📝 Test polling must not itself depend on the animation API deliberately suppressed below.
    const AwaitBrowserCondition = Page.waitForFunction.bind(Page);
    Page.waitForFunction = (Predicate, Argument, Options = {}) => AwaitBrowserCondition(Predicate, Argument, { polling: 50, ...Options });
    Page.on('pageerror', Failure => Errors.push(Failure.message));
    Page.on('console', Message => { if (Message.type() === 'error') Errors.push(Message.text()); });
    const Threads = [];
    Page.on('worker', Execution => { if (/VolumeSubmission/.test(Execution.url())) Threads.push(Execution.url()); });
    await Page.route(/VolumeSubmission[^/]*\.js(?:\?.*)?$/, async Route =>
    {
        const Response = await Route.fetch();
        const Injection = `
const NativeReceive = self.onmessage;
self.onmessage = Event =>
{
    if (Event.data.Category === 'initialize')
    {
        if (Event.data.Surface)
        {
            const GL = Event.data.Surface.getContext('webgl2');
            self.postMessage({ Category: 'telemetry', Telemetry: { renderExecution: 'thread', renderMode: 'webgl2', renderStatus: 'compiling', validationNativeWebGL: String(Boolean(GL)) } });
            while (true) {}
        }
        if (${JSON.stringify(SilenceSoftware)}) { self.onmessage = () => {}; return; }
    }
    NativeReceive(Event);
};
`;
        await Route.fulfill({ response: Response, body: await Response.text() + Injection });
    });
    if (!AnimationCallbacks) await Page.addInitScript(() => Object.defineProperty(window, 'requestAnimationFrame', { value: () => 0 }));
    await Page.goto(Results.Origin);
    if (AnimationCallbacks)
    {
        await Page.locator('[data-inspector="region"]').click();
        await Page.locator('#MaskName').fill('Retained stalled-preview coast');
        await Page.locator('#MaskName').dispatchEvent('change');
    }
    else await Page.evaluate(() =>
    {
        document.querySelector('[data-inspector="region"]').click();
        document.querySelector('#MaskName').value = 'Retained timer-driven coast';
        document.querySelector('#MaskName').dispatchEvent(new Event('change', { bubbles: true }));
    });
    await Snapshot(Page);
    if (AnimationCallbacks) await Page.locator('#LandscapeTab').click();
    else await Page.evaluate(() => document.querySelector('#LandscapeTab').click());
    await Page.waitForFunction(() => document.querySelector('#Volume').dataset.validationNativeWebGL === 'true');
    Assert.equal(await Page.locator('#PreviewSoftware').isVisible(), true);
    return { Page, Threads };
}

async function Snapshot(Page)
{
    await Page.waitForFunction(() => document.querySelector('#SaveStatus').textContent === 'Saved locally');
    return Page.evaluate(() => JSON.parse(localStorage.getItem('FrontierLandscape')));
}

async function AwaitImage(Page)
{
    await Page.waitForFunction(() =>
    {
        const Canvas = document.querySelector('#Volume');
        return Number(Canvas.dataset.softwareImages) > 0 && Canvas.dataset.completedRevision === Canvas.dataset.volumeRevision &&
            document.querySelector('.software-volume') && !document.querySelector('.software-volume').hidden;
    });
    return Page.locator('#Volume').evaluate(async Canvas =>
    {
        const Image = await Canvas[Symbol.for('FrontierLandscape.Presentation')].Inspect('pixels');
        let Land = 0, Hash = 2166136261;
        for (const Byte of Image.Pixels) Hash = Math.imul(Hash ^ Byte, 16777619) >>> 0;
        for (let Index = 0; Index < Image.Pixels.length; Index += 4)
            if (Image.Pixels[Index] > Image.Pixels[Index + 1] + 3 && Image.Pixels[Index + 1] > Image.Pixels[Index + 2] + 6) Land++;
        delete Image.Pixels;
        return { ...Image, Hash, LandPixels: Land };
    });
}

async function ExportDocument(Page)
{
    const Request = Page.waitForEvent('download');
    await Page.locator('#Export').click();
    const Download = await Request, Chunks = [];
    for await (const Chunk of await Download.createReadStream()) Chunks.push(Chunk);
    return JSON.parse(Buffer.concat(Chunks).toString());
}

try
{
    const Automatic = await Initialize();
    const Page = Automatic.Page;
    await Page.locator('#AddGenerator').click();
    await Page.locator('[data-generator="multifractal"]').click();
    const Authored = await Snapshot(Page);
    Assert.equal(Authored.Layers[0].Landforms.length, 3);
    Assert.equal(Authored.Layers[0].Landforms.at(-1).Type, 'multifractal');
    const Before = await Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset }));
    const Started = performance.now();
    Verify('a genuine WebGL2 context can exist while the preview worker remains blocked with no image or failure', { NativeWebGL2: Before.validationNativeWebGL, Simulation: 'GPU worker infinite loop after actual context allocation', Waiting: Before.renderStatus, AuthoredGeneratorCount: 3 });
    const First = await AwaitImage(Page);
    const Elapsed = performance.now() - Started;
    Assert.ok(Elapsed < GraphicsImageTimeoutMilliseconds + 5000);
    Assert.equal(First.renderMode, 'software');
    Assert.equal(First.renderExecution, 'thread');
    Assert.match(First.softwareReason, /did not present an image/);
    Assert.ok(First.LandPixels > First.Width * First.Height * .01);
    Assert.equal(Automatic.Threads.length, 2);
    Assert.equal(await Page.locator('#PreviewNotice').isVisible(), false);
    Assert.deepEqual(await ExportDocument(Page), Authored);
    Verify('the visible-image deadline replaces the blocked GPU with a fresh CPU worker and actual terrain', { ElapsedMilliseconds: Elapsed, DeadlineMilliseconds: GraphicsImageTimeoutMilliseconds, Threads: Automatic.Threads.length, FirstImage: First, ExactDocumentRetained: true });
    await Page.locator('[data-inspector="ocean"]').click();
    await Page.locator('#AnimateOcean').uncheck();
    await Page.waitForFunction(() =>
    {
        const Canvas = document.querySelector('#Volume');
        return Number(Canvas.dataset.bufferWidth) === 160 && Canvas.dataset.completedComposition === Canvas.dataset.submittedComposition;
    });
    await Page.locator('[data-inspector="shape"]').click();
    const Orbit = await Page.locator('#Volume').getAttribute('data-completed-orbit');
    await Page.locator('#Volume').hover();
    await Page.mouse.wheel(0, -140);
    await Page.waitForFunction(Before => document.querySelector('#Volume').dataset.completedOrbit !== Before, Orbit);
    const BeforeEdit = Number(await Page.locator('#Volume').getAttribute('data-volume-revision'));
    await Page.getByRole('spinbutton', { name: 'Relief in metres', exact: true }).fill('75');
    await Page.getByRole('spinbutton', { name: 'Relief in metres', exact: true }).dispatchEvent('change');
    await Page.waitForFunction(Before => Number(document.querySelector('#Volume').dataset.volumeRevision) > Before, BeforeEdit);
    await AwaitImage(Page);
    Assert.equal((await Snapshot(Page)).Layers[0].Landforms.at(-1).Amount, 75);
    Verify('the recovered preview still accepts camera navigation and edits to the third multifractal generator', { PreviousOrbit: Orbit, CompletedOrbit: await Page.locator('#Volume').getAttribute('data-completed-orbit'), Relief: 75 });
    await Page.waitForFunction(() => Number(document.querySelector('#Volume').dataset.bufferWidth) === 160);
    await Page.screenshot({ path: Directory + 'Images/' + Label + '.png' });
    await Page.close();

    const Manual = await Initialize();
    const ManualDocument = await Snapshot(Manual.Page);
    const ManualStarted = performance.now();
    await Manual.Page.locator('#PreviewSoftware').click();
    const ManualImage = await AwaitImage(Manual.Page);
    const ManualElapsed = performance.now() - ManualStarted;
    Assert.ok(ManualElapsed < 5000);
    Assert.equal(ManualImage.renderMode, 'software');
    Assert.equal(await Manual.Page.locator('#Volume').getAttribute('data-software-transitions'), '1');
    Assert.equal(Manual.Threads.length, 2);
    Assert.ok(ManualImage.LandPixels > 0);
    Assert.deepEqual(await ExportDocument(Manual.Page), ManualDocument);
    Verify('Use software renderer bypasses the GPU deadline without reload or document loss', { ElapsedMilliseconds: ManualElapsed, Threads: Manual.Threads.length, Image: ManualImage, ExactDocumentRetained: true });
    await Manual.Page.close();

    const Starved = await Initialize(false, false);
    const StarvedStarted = performance.now();
    const StarvedImage = await AwaitImage(Starved.Page);
    const StarvedElapsed = performance.now() - StarvedStarted;
    Assert.ok(StarvedImage.LandPixels > 0);
    Assert.equal(await Starved.Page.locator('#Volume').getAttribute('data-view-schedule'), 'timer');
    const StarvedOrbit = await Starved.Page.locator('#Volume').getAttribute('data-completed-orbit');
    await Starved.Page.evaluate(() => document.querySelector('#Volume').dispatchEvent(new WheelEvent('wheel', { deltaY: -140, bubbles: true, cancelable: true })));
    await Starved.Page.waitForFunction(Before => document.querySelector('#Volume').dataset.completedOrbit !== Before, StarvedOrbit);
    Verify('startup, software recovery and camera resubmission still work with every animation callback suppressed', { ExplicitAnimationSuppression: true, SyntheticWheelInput: true, ViewSchedule: 'timer', ElapsedMilliseconds: StarvedElapsed, Image: StarvedImage, PreviousOrbit: StarvedOrbit, CompletedOrbit: await Starved.Page.locator('#Volume').getAttribute('data-completed-orbit') });
    await Starved.Page.close();

    const Silent = await Initialize(true);
    const SilentStarted = performance.now();
    const LocalImage = await AwaitImage(Silent.Page);
    const LocalElapsed = performance.now() - SilentStarted;
    Assert.equal(LocalImage.renderExecution, 'main-sliced');
    Assert.ok(LocalImage.LandPixels > 0);
    Assert.equal(Silent.Threads.length, 2);
    Assert.ok(LocalElapsed < GraphicsImageTimeoutMilliseconds + SoftwareThreadTimeoutMilliseconds + 5000);
    await Silent.Page.locator('#MaskTab').click();
    Assert.equal(await Silent.Page.locator('#Paint').isVisible(), true);
    Verify('a silent CPU replacement also times out and local yielding CPU rendering still presents terrain', { ElapsedMilliseconds: LocalElapsed, GraphicsDeadline: GraphicsImageTimeoutMilliseconds, SoftwareHandshakeDeadline: SoftwareThreadTimeoutMilliseconds, Image: LocalImage, MaskUsable: true });
    await Silent.Page.close();
    Assert.deepEqual(Errors, []);
    Verify('all deadline and manual recovery cases finish without uncaught application errors', { Errors });
    Results.Status = 'passed';
    await rm(Directory + 'Images/' + Label + 'Failure.png', { force: true });
}
catch (Failure)
{
    Results.Status = 'failed';
    Results.Failure = Failure.stack;
    Results.Errors = Errors;
    const Current = Pages.find(Page => !Page.isClosed());
    if (Current)
    {
        Results.Canvas = await Current.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset })).catch(() => null);
        await Current.screenshot({ path: Directory + 'Images/' + Label + 'Failure.png' }).catch(() => {});
    }
    console.error(Failure);
    process.exitCode = 1;
}
finally
{
    await Browser.close();
    await writeFile(Directory + 'Results/' + Label + '.json', JSON.stringify(Results, null, 2) + '\n');
}
