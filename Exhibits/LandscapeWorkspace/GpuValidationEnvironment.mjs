/*==========================================================================================================================================
                                                       GPUVALIDATIONENVIRONMENT.MJS
==========================================================================================================================================*/
// 🧩 Reproducible GPU-only browser observations, unchanged runtime deadlines, real pixel digests and source fingerprints.

import Assert from 'node:assert/strict';
import { readFile, readdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';

export const GraphicsArguments = [
    '--no-sandbox', '--disable-dev-shm-usage', '--use-gl=angle', '--use-angle=swiftshader',
    '--enable-unsafe-swiftshader', '--no-zygote'
];

export async function ConstructGpuReport(Origin)
{
    const SourceSHA256 = {};
    const Directory = new URL('../../Experimental/LandscapeWorkspace/', import.meta.url);
    for (const Name of await readdir(Directory))
    {
        if (!/\.(?:js|css|html|json)$/.test(Name) || /\.test\.js$/.test(Name) || Name === 'package-lock.json') continue;
        SourceSHA256[Name] = createHash('sha256').update(await readFile(new URL(Name, Directory))).digest('hex');
    }
    return {
        Status: 'running', Date: new Date().toISOString(), Runtime: process.version, Platform: process.platform,
        Revision: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
        Branch: execFileSync('git', ['branch', '--show-current'], { encoding: 'utf8' }).trim(),
        Origin, SourceSHA256, Checks: [], Images: [],
        Conditions: {
            WorkingTree: 'Uncommitted GPU-only Landscape repair.',
            ApplicationSourceInterception: false, DeadlineOverride: false,
            Graphics: 'Chromium / ANGLE SwiftShader, a software graphics driver used only by the lab browser. Not physical-GPU or Windows verification.',
            Startup: 'First graphics context in each page. No assertion that operating-system or driver caches are cold.',
            Scope: 'GPU API surface rasterization, not CPU pixel rendering. Geometry is sampled off-thread from the genuine XYZ authoring field.'
        }
    };
}

export async function ObserveGpuBrowser(Page, Mode = 'normal')
{
    await Page.addInitScript(Mode =>
    {
        window.GpuValidation = {
            Threads: [], Images: [], MainThreadGraphicsContexts: 0, MainThreadGraphicsDraws: 0,
            CpuPixelCopies: 0, Heartbeats: [], SlowCanvasCalls: [], Phase: 'startup', Entered: 0
        };
        const Observations = window.GpuValidation;
        const NativeContext = HTMLCanvasElement.prototype.getContext;
        HTMLCanvasElement.prototype.getContext = function(Category, ...Arguments)
        {
            const Start = performance.now();
            const Result = NativeContext.call(this, Category, ...Arguments);
            if (Category.includes('webgl')) Observations.MainThreadGraphicsContexts++;
            if (performance.now() - Start > 20)
                Observations.SlowCanvasCalls.push({ Category, Milliseconds: performance.now() - Start, Phase: Observations.Phase });
            return Result;
        };
        for (const Name of ['drawArrays', 'drawElements'])
        {
            const Native = WebGL2RenderingContext.prototype[Name];
            WebGL2RenderingContext.prototype[Name] = function(...Arguments)
            { Observations.MainThreadGraphicsDraws++; return Native.apply(this, Arguments); };
        }
        const NativePixels = CanvasRenderingContext2D.prototype.putImageData;
        CanvasRenderingContext2D.prototype.putImageData = function(...Arguments)
        { Observations.CpuPixelCopies++; return NativePixels.apply(this, Arguments); };
        const NativeThread = window.Worker;
        window.Worker = class extends NativeThread
        {
            constructor(Address, Options)
            {
                const Graphics = String(Address).includes('VolumeSubmission');
                let SilentAddress = null;
                if (Graphics && Mode === 'silent')
                    SilentAddress = URL.createObjectURL(new Blob(['self.onmessage = () => {};'], { type: 'text/javascript' }));
                super(SilentAddress || Address, Options);
                this.Observation = { Address: String(Address), Graphics, Requests: {}, PendingViews: 0, MaximumPendingViews: 0, Terminated: false };
                this.SilentAddress = SilentAddress;
                Observations.Threads.push(this.Observation);
                this.addEventListener('message', Event =>
                {
                    if (Event.data.Category === 'view-complete') this.Observation.PendingViews--;
                    const Telemetry = Event.data.Telemetry;
                    if (!Graphics || !Telemetry?.completedComposition) return;
                    const Last = Observations.Images.at(-1);
                    if (Last?.Composition === Telemetry.completedComposition) return;
                    Observations.Images.push({
                        At: performance.now(), Composition: Telemetry.completedComposition,
                        Revision: Telemetry.completedRevision, Quality: Telemetry.completedQuality,
                        Width: Telemetry.bufferWidth, Height: Telemetry.bufferHeight,
                        FenceMilliseconds: Telemetry.lastFenceMilliseconds
                    });
                });
            }
            postMessage(Request, ...Arguments)
            {
                const Observation = this.Observation;
                Observation.Requests[Request.Category] = (Observation.Requests[Request.Category] || 0) + 1;
                if (Request.Category === 'view')
                {
                    Observation.PendingViews++;
                    Observation.MaximumPendingViews = Math.max(Observation.MaximumPendingViews, Observation.PendingViews);
                }
                return super.postMessage(Request, ...Arguments);
            }
            terminate()
            {
                this.Observation.Terminated = true;
                if (this.SilentAddress) URL.revokeObjectURL(this.SilentAddress);
                return super.terminate();
            }
        };
        if (Mode === 'missing-worker') window.Worker = undefined;
        if (Mode === 'missing-offscreen') HTMLCanvasElement.prototype.transferControlToOffscreen = undefined;
        if (Mode === 'no-animation') window.requestAnimationFrame = () => 0;
        document.addEventListener('DOMContentLoaded', () => { document.querySelector('#AnimateOcean').checked = false; });
        document.addEventListener('click', Event =>
        {
            if (Event.target.closest('#LandscapeTab')) Observations.Entered = performance.now();
        }, true);
        let Previous = performance.now();
        setInterval(() =>
        {
            const Now = performance.now();
            Observations.Heartbeats.push({ Milliseconds: Now - Previous, Phase: Observations.Phase, At: Now });
            Previous = Now;
        }, 10);
    }, Mode);
}

export async function WaitForGpuImage(Page, Quality = 48)
{
    await Page.waitForFunction(Quality =>
    {
        const D = document.querySelector('#Volume').dataset;
        return ['failed', 'lost'].includes(D.renderStatus) || D.renderStatus === 'ready' &&
            D.completedRevision === D.volumeRevision && Number(D.completedQuality) >= Quality &&
            (Quality < 48 || D.completedQuality === D.geometryCompletedQuality && D.previewDensitySettled === 'true' &&
                D.geometryPreparationEpoch === D.geometryCompletedEpoch && D.completedPreparationEpoch === D.geometryCompletedEpoch) &&
            D.completedComposition && D.completedComposition === D.submittedComposition;
    }, Quality, { polling: 50, timeout: 18000 });
    const Result = await Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset }));
    Assert.equal(Result.renderStatus, 'ready', Result.renderMessage);
    Assert.equal(Result.renderMode, 'webgl2');
    Assert.equal(Result.renderTechnique, 'xyz-surface');
    return Result;
}

export async function ReadGpuPixels(Page)
{
    return Page.evaluate(async () =>
    {
        const Snapshot = await document.querySelector('#Volume')[Symbol.for('FrontierLandscape.Presentation')].Inspect('pixels');
        let Hash = 2166136261, LandPixels = 0;
        for (const Byte of Snapshot.Pixels) Hash = Math.imul(Hash ^ Byte, 16777619) >>> 0;
        for (let Index = 0; Index < Snapshot.Pixels.length; Index += 4)
            if (Snapshot.Pixels[Index] > Snapshot.Pixels[Index + 1] + 3 && Snapshot.Pixels[Index + 1] > Snapshot.Pixels[Index + 2] + 6) LandPixels++;
        return { Hash, LandPixels, Width: Snapshot.Width, Height: Snapshot.Height, Error: Snapshot.Error, Telemetry: Snapshot.Telemetry };
    });
}

export async function SaveAuthoringDocument(Page)
{
    const Pending = Page.waitForEvent('download');
    await Page.locator('#Export').click();
    const Download = await Pending, Chunks = [];
    for await (const Chunk of await Download.createReadStream()) Chunks.push(Chunk);
    return JSON.parse(Buffer.concat(Chunks).toString('utf8'));
}

export function VerifyGpuCheck(Report, Name, Observed)
{
    Report.Checks.push({ Name, Status: 'passed', Observed });
    console.log('Verified: ' + Name);
}
