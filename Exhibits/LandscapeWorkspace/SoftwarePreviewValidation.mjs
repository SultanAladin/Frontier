/*==========================================================================================================================================
                                                      SOFTWAREPREVIEWVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Native no-WebGL terrain presentation, responsive navigation/editing, retained exports and genuine volumetric software rendering.

import Assert from 'node:assert/strict';
import { mkdir, readFile, writeFile, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { gunzipSync } from 'node:zlib';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { CreateLandform } from '../../Experimental/LandscapeWorkspace/LandformStack.js';
import { DecodeSparseVolume, ReadVolumeCell } from '../../Experimental/LandscapeWorkspace/SparseVolume.js';
import { CompileVolumeRayField, IntersectVolumeRay } from '../../Experimental/LandscapeWorkspace/VolumeRayField.js';

const Directory = fileURLToPath(new URL('./', import.meta.url));
const Label = process.env.LANDSCAPE_RESULT || 'SoftwarePreviewValidation';
await mkdir(Directory + 'Results', { recursive: true });
await mkdir(Directory + 'Images', { recursive: true });
const Arguments = ['--no-sandbox', '--disable-dev-shm-usage', '--disable-gpu', '--disable-software-rasterizer', '--no-zygote'];
const Results = {
    Status: 'running', Date: new Date().toISOString(), Runtime: process.version, Platform: process.platform,
    Revision: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
    Branch: execFileSync('git', ['branch', '--show-current'], { encoding: 'utf8' }).trim(),
    Origin: process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173', Arguments, Checks: [], SourceSHA256: {},
    Note: 'Actual native Chromium with both hardware GPU and software WebGL disabled. No context-getter injection for the primary case. Extra API-absence cases explicitly shadow only the named API. CPU ray intersections of the authored XYZ SDF, not a heightmap. Linux browser evidence is not a Windows/physical-device certification. Uncommitted application repair.'
};
for (const Name of ['Workspace.js', 'VolumePresentation.js', 'VolumeSubmission.js', 'VolumeRasterization.js', 'VolumeRayField.js', 'VolumeScalarRasterization.js', 'DistanceField.js', 'LandformStack.js', 'OceanSurface.js', 'TerrainNoise.js', 'Workspace.css', 'index.html'])
    Results.SourceSHA256[Name] = createHash('sha256').update(await readFile(new URL('../../Experimental/LandscapeWorkspace/' + Name, import.meta.url))).digest('hex');
const Browser = await chromium.launch({ executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined, headless: true, args: Arguments });
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 2 });
Page.setDefaultTimeout(30000);
const Errors = [], FailedResources = [], Threads = [];
Page.on('pageerror', Failure => Errors.push(Failure.message));
Page.on('console', Message => { if (Message.type() === 'error') Errors.push(Message.text()); });
Page.on('response', Response => { if (Response.status() >= 400) FailedResources.push(Response.status() + ' ' + Response.url()); });
Page.on('worker', Execution => { if (/VolumeSubmission/.test(Execution.url())) Threads.push(Execution.url()); });
await Page.addInitScript(() =>
{
    sessionStorage.setItem('FrontierLandscapePreviewWidth', '480');
    let Last = performance.now();
    window.SoftwareTiming = { MaximumHeartbeatMilliseconds: 0, Stalls: [] };
    window.ResetSoftwareTiming = () => { Last = performance.now(); window.SoftwareTiming = { MaximumHeartbeatMilliseconds: 0, Stalls: [] }; };
    setInterval(() =>
    {
        const Now = performance.now(), Elapsed = Now - Last;
        window.SoftwareTiming.MaximumHeartbeatMilliseconds = Math.max(window.SoftwareTiming.MaximumHeartbeatMilliseconds, Elapsed);
        if (Elapsed > 250) window.SoftwareTiming.Stalls.push(Elapsed);
        Last = Now;
    }, 10);
});

function Verify(Name, Observed)
{
    Results.Checks.push({ Name, Status: 'passed', Observed });
    console.log('Verified: ' + Name);
}

async function Inspect(Category = 'status', Target = Page)
{
    return Target.locator('#Volume').evaluate(async (Canvas, Category) =>
    {
        const Snapshot = await Canvas[Symbol.for('FrontierLandscape.Presentation')].Inspect(Category);
        if (!Snapshot.Pixels) return Snapshot;
        let Hash = 2166136261, Land = 0;
        for (const Byte of Snapshot.Pixels) Hash = Math.imul(Hash ^ Byte, 16777619) >>> 0;
        for (let Index = 0; Index < Snapshot.Pixels.length; Index += 4)
            if (Snapshot.Pixels[Index] > Snapshot.Pixels[Index + 1] + 3 && Snapshot.Pixels[Index + 1] > Snapshot.Pixels[Index + 2] + 6) Land++;
        delete Snapshot.Pixels;
        return { ...Snapshot, Hash, LandPixels: Land };
    }, Category);
}

async function AwaitImage(Width = 160, Target = Page)
{
    await Target.waitForFunction(Width =>
    {
        const Canvas = document.querySelector('#Volume'), Overlay = document.querySelector('.software-volume');
        return Canvas.dataset.renderMode === 'software' && Number(Canvas.dataset.bufferWidth) === Width &&
            Canvas.dataset.completedRevision === Canvas.dataset.volumeRevision && Number(Canvas.dataset.terrainRenders) > 0 &&
            Overlay && !Overlay.hidden && Overlay.width === Width;
    }, Width);
}

async function AwaitStable()
{
    await Page.waitForFunction(() =>
    {
        const Canvas = document.querySelector('#Volume');
        return Canvas.dataset.completedRevision === Canvas.dataset.volumeRevision &&
            Canvas.dataset.completedComposition && Canvas.dataset.completedComposition === Canvas.dataset.submittedComposition;
    });
}

async function Snapshot()
{
    await Page.waitForFunction(() => document.querySelector('#SaveStatus').textContent === 'Saved locally');
    return Page.evaluate(() => JSON.parse(localStorage.getItem('FrontierLandscape')));
}

async function ExportDocument()
{
    const Request = Page.waitForEvent('download');
    await Page.locator('#Export').click();
    const Download = await Request, Chunks = [];
    for await (const Chunk of await Download.createReadStream()) Chunks.push(Chunk);
    return JSON.parse(Buffer.concat(Chunks).toString());
}

try
{
    await Page.goto(Results.Origin);
    Results.Browser = Browser.version();
    Results.Native = await Page.evaluate(async () =>
    {
        const Main = {};
        for (const Name of ['webgl', 'webgl2', '2d']) Main[Name] = Boolean(document.createElement('canvas').getContext(Name));
        const Address = URL.createObjectURL(new Blob([`onmessage = () => postMessage({ WebGL: Boolean(new OffscreenCanvas(1, 1).getContext('webgl')), WebGL2: Boolean(new OffscreenCanvas(1, 1).getContext('webgl2')), Canvas2D: Boolean(new OffscreenCanvas(1, 1).getContext('2d')) });`], { type: 'text/javascript' }));
        const Execution = new Worker(Address);
        const Thread = await new Promise(Resolve => { Execution.onmessage = Event => Resolve(Event.data); Execution.postMessage(null); });
        Execution.terminate();
        URL.revokeObjectURL(Address);
        return { Main, Thread };
    });
    Assert.deepEqual(Results.Native.Main, { webgl: false, webgl2: false, '2d': true });
    Assert.deepEqual(Results.Native.Thread, { WebGL: false, WebGL2: false, Canvas2D: true });
    Assert.equal(Threads.length, 0);
    Verify('native main-thread and worker WebGL 1/2 are unavailable; Canvas2D remains available', Results.Native);
    await Page.locator('[data-inspector="region"]').click();
    await Page.locator('#MaskName').fill('Preserved software coast');
    await Page.locator('#MaskName').dispatchEvent('change');
    const Original = await Snapshot();
    await Page.evaluate(() => window.ResetSoftwareTiming());
    const Started = performance.now();
    await Page.locator('#LandscapeTab').click();
    await Page.waitForFunction(() => Number(document.querySelector('#Volume').dataset.softwareImages) > 0);
    Results.FirstVisibleMilliseconds = performance.now() - Started;
    const First = await Inspect('pixels');
    Assert.ok(First.LandPixels > First.Width * First.Height * .01);
    Assert.equal(First.graphicsContexts, '0');
    Assert.equal(First.renderExecution, 'thread');
    Verify('Landscape automatically presents actual terrain without any WebGL context', { Milliseconds: Results.FirstVisibleMilliseconds, Width: First.Width, Height: First.Height, LandPixels: First.LandPixels, Renderer: First.renderer });
    await AwaitImage();
    Results.RefinedVisibleMilliseconds = performance.now() - Started;
    Assert.equal(await Page.locator('#SoftwarePreviewBadge').isVisible(), true);
    Assert.equal(await Page.locator('#PreviewNotice').isVisible(), false);
    Assert.match(await Page.locator('#RenderReadout').innerText(), /Software SDF/);
    Assert.equal(await Page.locator('#RenderAPI').innerText(), 'SOFTWARE SDF');
    Verify('the low-cost first image refines automatically and the editor labels software rendering honestly', { Milliseconds: Results.RefinedVisibleMilliseconds, Width: 160, PreviewNoticeHidden: true });
    const InputStarted = performance.now();
    await Page.locator('[data-inspector="ocean"]').click();
    Results.InspectorInputMilliseconds = performance.now() - InputStarted;
    Assert.ok(Results.InspectorInputMilliseconds < 1000);
    const Ocean = await Inspect('pixels');
    await Page.waitForFunction(Previous => Number(document.querySelector('#Volume').dataset.softwareImages) > Previous + 1, Number(Ocean.softwareImages));
    const NextOcean = await Inspect('pixels');
    Assert.notEqual(NextOcean.Hash, Ocean.Hash);
    Assert.equal(NextOcean.terrainRenders, Ocean.terrainRenders);
    Verify('procedural ocean animation changes visible pixels while reusing terrain intersections', { BeforeHash: Ocean.Hash, AfterHash: NextOcean.Hash, TerrainRenders: NextOcean.terrainRenders, InspectorInputMilliseconds: Results.InspectorInputMilliseconds });
    await Page.locator('#AnimateOcean').uncheck();
    await AwaitStable();
    const Stored = await Page.evaluate(() => ({ GPU: sessionStorage.getItem('FrontierLandscapePreviewWidth'), Software: sessionStorage.getItem('FrontierLandscapeSoftwareWidth') }));
    for (const Width of ['64', '96', '160'])
    {
        await Page.locator('#RenderQuality').selectOption(Width);
        await AwaitImage(Number(Width));
        await AwaitStable();
    }
    Assert.equal(await Page.evaluate(() => sessionStorage.getItem('FrontierLandscapePreviewWidth')), '480');
    Assert.equal(await Page.evaluate(() => sessionStorage.getItem('FrontierLandscapeSoftwareWidth')), '160');
    const Choices = await Page.locator('#RenderQuality').evaluate(Select => [...Select.options].filter(Option => !Option.disabled).map(Option => Option.value));
    Assert.deepEqual(Choices, ['0', '64', '96', '160']);
    Verify('software quality has bounded 64/96/160 choices and never overwrites the GPU preference', { Before: Stored, EnabledWidths: Choices, SoftwareWidth: '160', GPUWidth: '480' });
    const ActualPixels = await Page.locator('.software-volume').evaluate(Canvas =>
    {
        const Pixels = Canvas.getContext('2d').getImageData(0, 0, Canvas.width, Canvas.height).data;
        let Hash = 2166136261;
        for (const Byte of Pixels) Hash = Math.imul(Hash ^ Byte, 16777619) >>> 0;
        return { Hash, Width: Canvas.width, Height: Canvas.height, PointerEvents: getComputedStyle(Canvas).pointerEvents };
    });
    const Stable = await Inspect('pixels');
    Assert.equal(ActualPixels.Hash, Stable.Hash);
    Assert.equal(ActualPixels.PointerEvents, 'none');
    Verify('the actual Canvas2D viewport contains the worker pixels and leaves camera input on the original canvas', ActualPixels);
    const Orbit = await Page.locator('#Volume').getAttribute('data-completed-orbit');
    await Page.locator('#Volume').hover();
    await Page.mouse.wheel(0, -180);
    await Page.waitForFunction(Before => document.querySelector('#Volume').dataset.completedOrbit !== Before, Orbit);
    await AwaitImage();
    const Bounds = await Page.locator('#Volume').boundingBox();
    const BeforeTarget = await Page.locator('#Volume').getAttribute('data-completed-target');
    await Page.mouse.move(Bounds.x + Bounds.width * .5, Bounds.y + Bounds.height * .5);
    await Page.mouse.down({ button: 'middle' });
    await Page.mouse.move(Bounds.x + Bounds.width * .5 + 40, Bounds.y + Bounds.height * .5 + 20, { steps: 4 });
    await Page.mouse.up({ button: 'middle' });
    await Page.waitForFunction(Before => document.querySelector('#Volume').dataset.completedTarget !== Before, BeforeTarget);
    const Yaw = await Page.locator('#Volume').getAttribute('data-camera-yaw');
    await Page.mouse.move(Bounds.x + Bounds.width * .5, Bounds.y + Bounds.height * .5);
    await Page.mouse.down();
    await Page.mouse.move(Bounds.x + Bounds.width * .5 + 36, Bounds.y + Bounds.height * .5 + 10, { steps: 4 });
    await Page.mouse.up();
    await Page.waitForFunction(Before => document.querySelector('#Volume').dataset.cameraYaw !== Before, Yaw);
    await Page.waitForFunction(() =>
    {
        const Canvas = document.querySelector('#Volume');
        return Canvas.dataset.completedComposition && JSON.parse(JSON.parse(Canvas.dataset.completedComposition)[0])[3].Yaw === Number(Canvas.dataset.cameraYaw);
    });
    await AwaitImage();
    await AwaitStable();
    Verify('zoom, middle-button pan and orbit all complete newly rendered software camera views', { BeforeOrbit: Orbit, BeforeTarget, BeforeYaw: Yaw, After: await Inspect() });
    await Page.locator('[data-inspector="shape"]').click();
    const BeforeEdit = await Inspect('pixels');
    await Page.getByRole('spinbutton', { name: 'Relief in metres', exact: true }).fill('95');
    await Page.getByRole('spinbutton', { name: 'Relief in metres', exact: true }).dispatchEvent('change');
    await Page.waitForFunction(Before => Number(document.querySelector('#Volume').dataset.volumeRevision) > Before, Number(BeforeEdit.volumeRevision));
    await AwaitImage();
    await AwaitStable();
    const Edited = await Inspect('pixels');
    Assert.notEqual(Edited.Hash, BeforeEdit.Hash);
    const EditedDocument = await Snapshot();
    const Changes = EditedDocument.Layers.flatMap((Layer, LayerIndex) => Layer.Landforms.map((Operation, OperationIndex) => ({ LayerIndex, OperationIndex, Operation })))
        .filter(Entry => JSON.stringify(Entry.Operation) !== JSON.stringify(Original.Layers[Entry.LayerIndex].Landforms[Entry.OperationIndex]));
    Assert.equal(Changes.length, 1);
    Assert.equal(Changes[0].LayerIndex, 0);
    Assert.equal(Changes[0].Operation.Amount, 95);
    const BeforeUndo = Number(await Page.locator('#Volume').getAttribute('data-volume-revision'));
    await Page.locator('#HistoryUndo').click();
    await Page.waitForFunction(Before => Number(document.querySelector('#Volume').dataset.volumeRevision) > Before, BeforeUndo);
    await AwaitImage();
    await AwaitStable();
    Assert.deepEqual(await Snapshot(), Original);
    Verify('an inspector relief edit visibly changes SDF terrain and undo restores the exact authored document', { BeforeHash: BeforeEdit.Hash, EditedHash: Edited.Hash, BeforeRevision: BeforeEdit.volumeRevision, EditedRevision: Edited.volumeRevision, DocumentRestored: true });
    await Page.locator('#MaskTab').click();
    const Paused = await Inspect();
    Assert.equal(Paused.Suspended, true);
    Assert.equal(await Page.locator('.software-volume').isVisible(), false);
    const Samples = await Page.locator('#StrokeCount').innerText();
    const Paint = await Page.locator('#Paint').boundingBox();
    await Page.mouse.move(Paint.x + Paint.width * .5, Paint.y + Paint.height * .55);
    await Page.mouse.down();
    await Page.mouse.move(Paint.x + Paint.width * .5 + 45, Paint.y + Paint.height * .55 + 15, { steps: 5 });
    await Page.mouse.up();
    await Page.waitForFunction(Before => document.querySelector('#StrokeCount').textContent !== Before, Samples);
    const Painted = await Snapshot();
    await Page.waitForTimeout(300);
    const StillPaused = await Inspect();
    Assert.equal(StillPaused.softwareRays, Paused.softwareRays);
    Assert.equal(StillPaused.softwareImages, Paused.softwareImages);
    Assert.equal(StillPaused.softwareUploads, Paused.softwareUploads);
    Assert.notDeepEqual(Painted, Original);
    Verify('Mask hides the overlay and suspends all software work while vector painting remains usable', { Rays: Paused.softwareRays, Images: Paused.softwareImages, Uploads: Paused.softwareUploads, Painted: true });
    await Page.locator('#LandscapeTab').click();
    await Page.waitForFunction(Before => Number(document.querySelector('#Volume').dataset.softwareUploads) > Before, Number(Paused.softwareUploads));
    await AwaitImage();
    await AwaitStable();
    Assert.deepEqual(await ExportDocument(), Painted);
    const BeforePaintUndo = Number(await Page.locator('#Volume').getAttribute('data-volume-revision'));
    await Page.locator('#HistoryUndo').click();
    await Page.waitForFunction(Before => Number(document.querySelector('#Volume').dataset.volumeRevision) > Before, BeforePaintUndo);
    await AwaitImage();
    await AwaitStable();
    Assert.deepEqual(await ExportDocument(), Original);
    for (let Index = 0; Index < 4; Index++) { await Page.locator('#MaskTab').click(); await Page.locator('#LandscapeTab').click(); }
    Assert.equal(Threads.length, 1);
    Verify('returning to Landscape includes new paint; save/undo round-trip masks and stacks without replacing the worker', { GraphicsThreads: Threads.length, PaintedExportPreserved: true, OriginalExportRestored: true });
    Results.Responsiveness = await Page.evaluate(() => window.SoftwareTiming);
    Assert.ok(Results.Responsiveness.MaximumHeartbeatMilliseconds < 1000, JSON.stringify(Results.Responsiveness));
    Verify('the editor remains responsive during CPU rendering, navigation and authoring', Results.Responsiveness);
    await Page.locator('[data-inspector="shape"]').click();
    await Page.screenshot({ path: Directory + 'Images/' + Label + '.png' });
    const Arch = {
        Format: 'FrontierLandscape/1', Layers: [{
            Name: 'Software volumetric arch', Color: '#a9b5af', Visible: true, Depth: 120, Caves: false,
            Resolution: 1024, Vertical: 128, DistanceMode: 'metric',
            Strokes: [{ Points: [[0, 0]], Radius: 150, Erase: false }],
            Regions: [{ X0: -180, Z0: -180, X1: 180, Z1: 180, Edge: 0 }],
            Landforms: [{ ...CreateLandform('arch'), Amount: 140, Blend: 0 }]
        }]
    };
    const Field = CompileVolumeRayField(Arch.Layers);
    const Occupancy = [-50, 40, 116].map(Y => Field.Evaluate(0, Y, 0, true)[0]);
    Assert.ok(Occupancy[0] < 0 && Occupancy[1] > 0 && Occupancy[2] < 0);
    Assert.equal(IntersectVolumeRay(Field, [0, 40, 400], [0, 0, -1]).Found, false);
    const BeforeArch = Number(await Page.locator('#Volume').getAttribute('data-volume-revision'));
    await Page.locator('#File').setInputFiles({ name: 'SoftwareArch.frontier.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(Arch)) });
    await Page.waitForFunction(() => JSON.parse(localStorage.getItem('FrontierLandscape')).Layers[0].Name === 'Software volumetric arch');
    await Page.waitForFunction(Before => Number(document.querySelector('#Volume').dataset.volumeRevision) > Before, BeforeArch);
    await AwaitImage();
    await AwaitStable();
    const ArchBounds = await Page.locator('#Volume').boundingBox();
    await Page.mouse.move(ArchBounds.x + ArchBounds.width * .5, ArchBounds.y + ArchBounds.height * .5);
    await Page.mouse.down();
    await Page.mouse.move(ArchBounds.x + ArchBounds.width * .5 - 36, ArchBounds.y + ArchBounds.height * .5 - 80, { steps: 5 });
    await Page.mouse.up();
    await Page.waitForFunction(() =>
    {
        const Canvas = document.querySelector('#Volume');
        return Canvas.dataset.completedComposition && JSON.parse(JSON.parse(Canvas.dataset.completedComposition)[0])[3].Yaw === Number(Canvas.dataset.cameraYaw);
    });
    await AwaitImage();
    await AwaitStable();
    const ArchImage = await Inspect('pixels');
    Assert.ok(Number(ArchImage.terrainRenders) > Number(Edited.terrainRenders));
    Assert.deepEqual(await ExportDocument(), Arch);
    await Page.screenshot({ path: Directory + 'Images/' + Label + 'Arch.png' });
    Verify('the same software renderer displays a genuine 3D arch with solid-air-solid occupancy and a ray-visible opening', { XZ: [0, 0], Y: [-50, 40, 116], Distances: Occupancy, OpeningRayMissedSolid: true, ExportPreserved: true, Image: ArchImage });
    await Page.locator('[data-inspector="region"]').click();
    await Page.locator('#Resolution').selectOption('256');
    await Page.locator('#Vertical').selectOption('64');
    await Page.locator('.volume-export').evaluate(Details => Details.open = true);
    const DownloadRequest = Page.waitForEvent('download', { timeout: 180000 });
    await Page.locator('#Bake').click();
    const Download = await DownloadRequest, Chunks = [];
    for await (const Chunk of await Download.createReadStream()) Chunks.push(Chunk);
    const Archive = Buffer.concat(Chunks), Volume = DecodeSparseVolume(gunzipSync(Archive));
    Assert.deepEqual(Volume.Specification.Dimensions, [256, 64, 256]);
    Assert.equal(Volume.Specification.Format, 'FrontierSparseTSDF/2');
    Assert.equal(Volume.Specification.Redistance.Converged, true);
    Assert.equal(Volume.Specification.Redistance.SignChanges, 0);
    const BakedSamples = [-50, 40, 116].map(Y =>
    {
        const Cell = [0, Y, 0].map((Coordinate, Axis) => Math.floor((Coordinate - Volume.Specification.Origin[Axis]) / Volume.Specification.Pitch[Axis]));
        return { Cell, Distance: ReadVolumeCell(Volume, ...Cell) };
    });
    Assert.ok(BakedSamples[0].Distance < 0 && BakedSamples[1].Distance > 0 && BakedSamples[2].Distance < 0);
    Assert.equal(await Page.locator('#Volume').getAttribute('data-render-mode'), 'software');
    Verify('the actual software-mode UI exports a converged 256×64×256 metric volume with solid-air-solid samples', { Dimensions: Volume.Specification.Dimensions, Format: Volume.Specification.Format, Redistance: Volume.Specification.Redistance, Samples: BakedSamples, CompressedBytes: Archive.length });
    Assert.deepEqual(Errors, []);
    Assert.deepEqual(FailedResources, []);
    for (const API of ['OffscreenCanvas', 'Worker'])
    {
        const Extra = await Browser.newPage({ viewport: { width: 1200, height: 820 } });
        const ExtraErrors = [];
        Extra.on('pageerror', Failure => ExtraErrors.push(Failure.message));
        await Extra.addInitScript(API =>
        {
            if (API === 'OffscreenCanvas') Object.defineProperty(HTMLCanvasElement.prototype, 'transferControlToOffscreen', { value: undefined });
            else Object.defineProperty(window, 'Worker', { value: undefined });
        }, API);
        await Extra.goto(Results.Origin);
        await Extra.locator('#LandscapeTab').click();
        await AwaitImage(160, Extra);
        const Image = await Inspect('pixels', Extra);
        Assert.equal(Image.renderExecution, API === 'Worker' ? 'main-sliced' : 'thread');
        Assert.ok(Image.LandPixels > Image.Width * Image.Height * .01);
        await Extra.locator('#MaskTab').click();
        Assert.equal((await Inspect('status', Extra)).Suspended, true);
        Assert.equal(await Extra.locator('#Paint').isVisible(), true);
        Assert.deepEqual(ExtraErrors, []);
        Verify('actual terrain remains usable when the ' + API + ' API is additionally unavailable', { ExplicitAPIAbsence: API, RenderExecution: Image.renderExecution, LandPixels: Image.LandPixels, PageErrors: ExtraErrors });
        await Extra.close();
    }
    Results.Status = 'passed';
    await rm(Directory + 'Images/' + Label + 'Failure.png', { force: true });
}
catch (Failure)
{
    Results.Status = 'failed';
    Results.Failure = Failure.stack;
    Results.Errors = Errors;
    Results.Canvas = await Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset })).catch(() => null);
    await Page.screenshot({ path: Directory + 'Images/' + Label + 'Failure.png' }).catch(() => {});
    console.error(Failure);
    process.exitCode = 1;
}
finally
{
    await Browser.close();
    await writeFile(Directory + 'Results/' + Label + '.json', JSON.stringify(Results, null, 2) + '\n');
}
