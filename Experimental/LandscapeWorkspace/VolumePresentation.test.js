/*==========================================================================================================================================
                                                       VOLUMEPRESENTATION.TEST.JS
==========================================================================================================================================*/
// 🧩 GPU-only ownership, bounded submission, truthful failure, no replacement renderer and current-revision image deadlines.

import { test, beforeEach, afterEach } from 'node:test';
import Assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { CreateVolumePresentation, InspectVolumePresentation } from './VolumePresentation.js';

const OriginalGlobals = new Map(['Worker', 'document', 'devicePixelRatio'].map(Name => [Name, globalThis[Name]]));
let Threads, Presentations;
const Camera = { Yaw: -.35, Pitch: .7, Orbit: 1000, Target: [40, 30, -70] };
const Delay = Milliseconds => new Promise(Resolve => setTimeout(Resolve, Milliseconds));

beforeEach(() =>
{
    Threads = [];
    Presentations = [];
    globalThis.document = {
        hidden: false, addEventListener() {}, removeEventListener() {},
        createElement() { throw Error('No replacement canvas is permitted.'); }
    };
    globalThis.devicePixelRatio = 2;
    globalThis.Worker = class
    {
        constructor(URL, Options) { this.URL = URL; this.Options = Options; this.Requests = []; Threads.push(this); }
        postMessage(Request) { this.Requests.push(structuredClone(Request)); }
        terminate() { this.Terminated = true; }
        Broadcast(Reply) { this.onmessage({ data: Reply }); }
    };
});

afterEach(() =>
{
    for (const Presentation of Presentations) Presentation.Dispose();
    for (const [Name, Content] of OriginalGlobals)
    {
        if (Content === undefined) delete globalThis[Name];
        else globalThis[Name] = Content;
    }
});

function Initialize(Options = {}, Offscreen = true)
{
    const Canvas = {
        dataset: {}, clientWidth: 1000, clientHeight: 800, Transfers: 0,
        getContext() { throw Error('No UI-thread graphics or pixel renderer is permitted.'); }
    };
    if (Offscreen) Canvas.transferControlToOffscreen = () => { Canvas.Transfers++; return { Width: 300, Height: 150 }; };
    const Presentation = CreateVolumePresentation(Canvas, Options);
    Presentations.push(Presentation);
    return { Canvas, Presentation, Thread: Threads.at(-1) };
}

function Complete(Thread, Revision = 1)
{
    Thread.Broadcast({ Category: 'status', Status: 'ready', Message: 'GPU surface ready.', Telemetry: {
        renderMode: 'webgl2', renderStatus: 'ready', volumeRevision: String(Revision), completedRevision: String(Revision),
        completedQuality: '48', terrainRenders: '1', graphicsContexts: '1', renderTechnique: 'xyz-surface'
    } });
}

test('canvas transfers once, only one view waits for acknowledgement, and no UI graphics are requested', () =>
{
    const { Canvas, Presentation, Thread } = Initialize();
    Assert.equal(Canvas.Transfers, 1);
    Assert.equal(Threads.length, 1);
    Assert.equal(Thread.Options.type, 'module');
    Assert.equal(Canvas.dataset.renderExecution, 'thread');
    for (let Index = 0; Index < 100; Index++) Presentation.Rasterize({ ...Camera, Orbit: 500 + Index }, Index, 1, true);
    Assert.equal(Thread.Requests.filter(Request => Request.Category === 'view').length, 1);
    Thread.Broadcast({ Category: 'view-complete' });
    Presentation.Rasterize(Camera, 100, 1, true);
    const Views = Thread.Requests.filter(Request => Request.Category === 'view');
    Assert.equal(Views.length, 2);
    Assert.deepEqual(Views[1].Camera, Camera);
    Assert.deepEqual(Views[1].Display, { Width: 1000, Height: 800, PixelRatio: 2, Visible: true });
});

test('Mask suspension and resume reuse exactly one thread and transferred canvas', () =>
{
    const { Canvas, Presentation, Thread } = Initialize();
    Presentation.Suspend();
    Presentation.Rasterize(Camera, 1, 1, true);
    Assert.equal(Thread.Requests.some(Request => Request.Category === 'view'), false);
    Presentation.Resume();
    Presentation.Rasterize(Camera, 2, 1, true);
    Assert.equal(Thread.Requests.filter(Request => Request.Category === 'view').length, 1);
    Assert.equal(Canvas.Transfers, 1);
    Assert.equal(Threads.length, 1);
});

test('upload messages preserve independent snapshots and carry monotonically increasing document revisions', () =>
{
    const { Presentation, Thread } = Initialize();
    const Layers = [{ Name: 'Original mask', Landforms: [{ Type: 'arch', Amount: 140 }] }];
    Presentation.Upload(Layers);
    Layers[0].Landforms[0].Amount = 200;
    Presentation.Upload(Layers);
    const Uploads = Thread.Requests.filter(Request => Request.Category === 'upload');
    Assert.equal(Uploads[0].Layers[0].Landforms[0].Amount, 140);
    Assert.equal(Uploads[1].Layers[0].Landforms[0].Amount, 200);
    Assert.deepEqual(Uploads.map(Request => Request.Revision), [1, 2]);
});

test('small GPU program readiness resolves initialization without pretending a scene image is complete', async () =>
{
    const { Canvas, Presentation, Thread } = Initialize();
    Thread.Broadcast({ Category: 'status', Status: 'geometry', Message: 'Preparing geometry.', Telemetry: { renderMode: 'webgl2' } });
    await Presentation.Ready;
    Assert.equal(Canvas.dataset.renderStatus, 'geometry');
    Assert.equal(Canvas.dataset.completedRevision, undefined);
});

test('current image telemetry and pixel inspections use the existing graphics thread', async () =>
{
    const { Canvas, Presentation, Thread } = Initialize();
    Presentation.Upload([]);
    Complete(Thread);
    Assert.equal(Canvas.dataset.completedRevision, '1');
    const Pending = Presentation.Inspect('pixels');
    const Request = Thread.Requests.at(-1);
    Thread.Broadcast({ Category: 'inspection', Token: Request.Token, Result: { Width: 64, Height: 48, Pixels: [2, 3, 4, 255] } });
    Assert.deepEqual(await Pending, { Width: 64, Height: 48, Pixels: [2, 3, 4, 255] });
    Assert.equal(Threads.length, 1);
});

test('a re-imported module shares transfer-once presentation ownership through its global symbol', async () =>
{
    const { Canvas, Presentation } = Initialize();
    const Reloaded = await import('./VolumePresentation.js?gpu-ownership-check');
    Assert.equal(Reloaded.CreateVolumePresentation(Canvas), Presentation);
    Assert.equal(Canvas.Transfers, 1);
    Assert.equal(Threads.length, 1);
});

test('native context loss is fatal until explicit restart and never starts another renderer', async () =>
{
    const Notifications = [];
    const { Canvas, Presentation, Thread } = Initialize({ Status: (...Reply) => Notifications.push(Reply) });
    Presentation.Upload([]);
    Complete(Thread);
    const Pending = Assert.rejects(Presentation.Inspect('pixels'), /GPU context lost/);
    Thread.Broadcast({ Category: 'status', Status: 'lost', Message: 'GPU context lost.', Telemetry: { renderMode: 'webgl2' } });
    await Pending;
    Assert.equal(Thread.Terminated, true);
    Assert.equal(Canvas.dataset.renderStatus, 'lost');
    Assert.equal(Canvas.dataset.renderMode, 'webgl2');
    Assert.equal(Canvas.dataset.previewStopped, 'true');
    const Before = Thread.Requests.length;
    Presentation.Resume(); Presentation.Upload([]); Presentation.Rasterize(Camera, 9, 1, true);
    Assert.equal(Thread.Requests.length, Before);
    Assert.equal(Threads.length, 1);
    Assert.equal(typeof Presentation.ActivateSoftware, 'undefined');
    Assert.equal(Notifications.at(-1)[0], 'lost');
});

test('worker crash rejects pending inspection, stops submissions and preserves actual failure instead of CPU recovery', async () =>
{
    const { Canvas, Presentation, Thread } = Initialize();
    const Pending = Assert.rejects(Presentation.Inspect(), /GPU thread failed/);
    Thread.onerror({ preventDefault() {}, message: 'GPU thread failed.' });
    await Pending;
    await Assert.rejects(Presentation.Ready, /GPU thread failed/);
    Assert.equal(Canvas.dataset.renderStatus, 'failed');
    Assert.equal(Thread.Terminated, true);
    Assert.equal(Threads.length, 1);
});

test('missing OffscreenCanvas fails without creating a worker, replacement canvas, or main-thread context', async () =>
{
    const { Canvas, Presentation } = Initialize({}, false);
    await Assert.rejects(Presentation.Ready, /OffscreenCanvas/);
    Assert.equal(Canvas.Transfers, 0);
    Assert.equal(Threads.length, 0);
    Assert.equal(Canvas.dataset.previewStopped, 'true');
});

test('missing Worker fails before transferring the canvas; software rendering is not an alternative', async () =>
{
    globalThis.Worker = undefined;
    const { Canvas, Presentation } = Initialize();
    await Assert.rejects(Presentation.Ready, /GPU threads/);
    Assert.equal(Canvas.Transfers, 0);
    Assert.equal(Threads.length, 0);
    Assert.equal(Canvas.dataset.renderStatus, 'failed');
});

test('a silent first-image deadline stops the sole GPU thread, with no retry or local rendering', async () =>
{
    const { Canvas, Presentation, Thread } = Initialize({ GraphicsImageTimeoutMilliseconds: 5, GraphicsReplyTimeoutMilliseconds: 1000 });
    Presentation.Upload([]);
    Presentation.Rasterize(Camera, 0, 1, true);
    await Delay(160);
    Assert.equal(Canvas.dataset.renderStatus, 'failed');
    Assert.match(Canvas.dataset.renderMessage, /time budget.*no software fallback/);
    Assert.equal(Thread.Terminated, true);
    Assert.equal(Threads.length, 1);
});

test('a silent worker after a completed image also stops without a replacement renderer', async () =>
{
    const { Canvas, Presentation, Thread } = Initialize({ GraphicsReplyTimeoutMilliseconds: 5 });
    Presentation.Upload([]);
    Complete(Thread);
    Presentation.Rasterize(Camera, 0, 1, true);
    await Delay(160);
    Assert.equal(Canvas.dataset.renderStatus, 'failed');
    Assert.match(Canvas.dataset.renderMessage, /stopped responding/);
    Assert.equal(Thread.Terminated, true);
    Assert.equal(Threads.length, 1);
});

test('Mask and hidden-page time do not consume the visible first-image budget', async () =>
{
    const { Canvas, Presentation } = Initialize({ GraphicsImageTimeoutMilliseconds: 5 });
    Presentation.Upload([]);
    Presentation.Rasterize(Camera, 0, 1, true);
    Presentation.Suspend();
    await Delay(160);
    Assert.equal(Canvas.dataset.previewStopped, undefined);
    globalThis.document.hidden = true;
    Presentation.Resume();
    await Delay(160);
    Assert.equal(Canvas.dataset.previewStopped, undefined);
    globalThis.document.hidden = false;
    Presentation.Resume();
    await Delay(160);
    Assert.equal(Canvas.dataset.previewStopped, 'true');
});

test('old image completion cannot satisfy a newer edit or overwrite its revision', async () =>
{
    const { Canvas, Presentation, Thread } = Initialize({ GraphicsImageTimeoutMilliseconds: 5, GraphicsReplyTimeoutMilliseconds: 1000 });
    Presentation.Upload([]);
    Complete(Thread);
    Presentation.Upload([{ Name: 'Edited scene' }]);
    Thread.Broadcast({ Category: 'status', Status: 'geometry', Message: 'New scene.', Telemetry: { volumeRevision: '2', renderStatus: 'geometry' } });
    Complete(Thread, 1);
    Assert.equal(Canvas.dataset.volumeRevision, '2');
    Assert.equal(Canvas.dataset.renderStatus, 'geometry');
    Presentation.Rasterize(Camera, 1, 1, true);
    await Delay(160);
    Assert.equal(Canvas.dataset.previewStopped, 'true');
});

test('a genuine current image cancels its first-image deadline', async () =>
{
    const { Canvas, Presentation, Thread } = Initialize({ GraphicsImageTimeoutMilliseconds: 5, GraphicsReplyTimeoutMilliseconds: 1000 });
    Presentation.Upload([]);
    Presentation.Rasterize(Camera, 1, 1, true);
    Complete(Thread);
    Thread.Broadcast({ Category: 'view-complete' });
    await Delay(160);
    Assert.equal(Canvas.dataset.renderStatus, 'ready');
    Assert.equal(Canvas.dataset.previewStopped, undefined);
});

test('termination ignores late telemetry and disposal rejects inspections and removes ownership', async () =>
{
    const { Canvas, Presentation, Thread } = Initialize();
    const Pending = Assert.rejects(Presentation.Inspect(), /disposed/);
    Presentation.Dispose();
    await Pending;
    Complete(Thread);
    Assert.equal(Canvas.dataset.completedRevision, undefined);
    Assert.equal(Thread.Terminated, true);
    await Assert.rejects(InspectVolumePresentation(Canvas), /Open Landscape/);
});

test('live presentation and submission contain no scalar renderer, software activation, pixel copies or replacement canvas', () =>
{
    for (const Name of ['VolumePresentation.js', 'VolumeSubmission.js'])
    {
        const Source = readFileSync(new URL(Name, import.meta.url), 'utf8');
        Assert.doesNotMatch(Source, /CreateVolumeScalarRasterization|ActivateSoftware|putImageData|ImageData|LocalSurface|createElement/);
        Assert.doesNotMatch(Source, /getContext\s*\(/);
    }
});

test('late worker telemetry cannot revert a newer UI resolution or navigation preference', () =>
{
    const { Canvas, Thread } = Initialize();
    Object.assign(Canvas.dataset, { previewWidth: '900', viewSchedule: 'timer', cameraTarget: '[40,30,-70]', cameraYaw: '-.35' });
    Thread.Broadcast({ Category: 'telemetry', Telemetry: {
        previewWidth: '256', viewSchedule: 'animation', cameraTarget: '[0,0,0]', cameraYaw: '0', bufferWidth: '256'
    } });
    Assert.equal(Canvas.dataset.previewWidth, '900');
    Assert.equal(Canvas.dataset.viewSchedule, 'timer');
    Assert.equal(Canvas.dataset.cameraTarget, '[40,30,-70]');
    Assert.equal(Canvas.dataset.cameraYaw, '-.35');
    Assert.equal(Canvas.dataset.bufferWidth, '256');
});

test('document replacement uses asynchronous editor decisions rather than blocking browser-native dialogs', () =>
{
    const Source = readFileSync(new URL('./Workspace.js', import.meta.url), 'utf8');
    const Markup = readFileSync(new URL('./index.html', import.meta.url), 'utf8');
    Assert.doesNotMatch(Source, /\b(?:confirm|alert|prompt)\s*\(/);
    Assert.match(Source, /await ConfirmDocumentReplacement/g);
    Assert.match(Source, /Dialog\.showModal\(\)/);
    Assert.match(Markup, /<dialog id="DocumentReplacement"/);
    Assert.match(Markup, /<form method="dialog">/);
    Assert.match(Markup, /id="CancelReplacement"[^>]*autofocus/);
});
