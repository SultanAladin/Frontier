/*==========================================================================================================================================
                                                     GPUREGIONRESOLUTIONVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Actual per-region XYZ settings, authored-pitch GPU working surfaces, same-camera captures, editing, navigation and 96-cell exports.

import Assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { gunzipSync } from 'node:zlib';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { CreateCoastalWorld } from '../../Experimental/LandscapeWorkspace/CoastalWorld.js';
import { CreateLandform } from '../../Experimental/LandscapeWorkspace/LandformStack.js';
import { VolumeDistance } from '../../Experimental/LandscapeWorkspace/DistanceField.js';
import { ConstructVolumeLattice } from '../../Experimental/LandscapeWorkspace/VolumeLattice.js';
import { DecodeSparseVolume } from '../../Experimental/LandscapeWorkspace/SparseVolume.js';
import { ProjectVolumeCamera } from '../../Experimental/LandscapeWorkspace/VolumeRayField.js';
import {
    GraphicsArguments, ConstructGpuReport, ObserveGpuBrowser, WaitForGpuImage, ReadGpuPixels,
    SaveAuthoringDocument, VerifyGpuCheck
} from './GpuValidationEnvironment.mjs';

const Origin = process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173';
const Prefix = process.env.LANDSCAPE_REPORT_PREFIX || 'GpuRegionResolution';
const Report = await ConstructGpuReport(Origin);
const Results = new URL('./Results/', import.meta.url), Images = new URL('./Images/', import.meta.url);
await mkdir(Results, { recursive: true });
await mkdir(Images, { recursive: true });
const Browser = await chromium.launch({ executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined, headless: true, args: GraphicsArguments });
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1 });
Page.setDefaultTimeout(18000);
const Errors = [];
Page.on('pageerror', Failure => Errors.push(Failure.message));
Page.on('console', Message => { if (Message.type() === 'error') Errors.push(Message.text()); });
await ObserveGpuBrowser(Page);
const Verify = (Name, Observed) => VerifyGpuCheck(Report, Name, Observed);

async function Capture(Suffix)
{
    const Name = Prefix + Suffix + '.png';
    await Page.screenshot({ path: new URL(Name, Images).pathname });
    Report.Images.push(Name);
}

async function WaitForWorking(PreviousKey = null)
{
    await Page.waitForFunction(Previous =>
    {
        const D = document.querySelector('#Volume').dataset;
        return ['failed', 'lost'].includes(D.renderStatus) || D.completedDetailKey && D.completedDetailKey !== Previous &&
            D.completedDetailKey === D.requestedDetailKey && D.completedDetailKey === D.geometryCompletedDetailKey &&
            D.geometryCompletedEpoch === D.geometryPreparationEpoch && D.completedPreparationEpoch === D.geometryCompletedEpoch &&
            D.completedRevision === D.volumeRevision && D.completedComposition === D.submittedComposition && D.previewDensitySettled === 'true';
    }, PreviousKey, { polling: 50, timeout: 18000 });
    const D = await Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset }));
    Assert.equal(D.renderStatus, 'ready', D.renderMessage);
    const Working = JSON.parse(D.workingLattice);
    Assert.ok(Working?.Resident, JSON.stringify(Working));
    Assert.equal(D.shaderTerrainFieldEvaluations, '0');
    Assert.ok(Number(D.surfaceTriangles) <= 120000 && Number(D.surfaceSamples) <= 1100000);
    Assert.equal(D.maximumPendingGpuImages, '1');
    return { Telemetry: D, Working };
}

async function Inspect(Query)
{
    return Page.evaluate(Query => document.querySelector('#Volume')[Symbol.for('FrontierLandscape.Presentation')].Inspect(Query), Query);
}

async function Import(Document)
{
    await Page.locator('#File').setInputFiles({ name: 'IndependentRegions.frontier.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(Document)) });
    await Page.waitForFunction(() => document.querySelector('#SaveStatus').textContent === 'Saved locally');
    await WaitForGpuImage(Page);
}

function ProjectPoint(Camera, Position, Width, Height)
{
    const Projection = ProjectVolumeCamera(Camera), Relative = Position.map((Coordinate, Axis) => Coordinate - Projection.Eye[Axis]);
    const Dot = Direction => Relative.reduce((Sum, Coordinate, Axis) => Sum + Coordinate * Direction[Axis], 0);
    const Depth = Dot(Projection.Forward);
    return [.5 + Dot(Projection.Right) * 1.65 * Height / Width / Depth / 2, .5 - Dot(Projection.Up) * 1.65 / Depth / 2];
}

// 📝 Validation-only landmark selection on this terrain fixture; the live application always picks the genuine XYZ field.
function FindCoastalLandmark(Layer)
{
    let Peak = [0, 0, 0];
    for (let X = -420; X <= -150; X += 30)
    for (let Z = -100; Z <= 140; Z += 30)
    {
        if (VolumeDistance(Layer, X, -1, Z, null, false) > 0) continue;
        let Lower = 0, Upper = 250;
        for (let Step = 0; Step < 16; Step++)
        {
            const Middle = (Lower + Upper) / 2;
            if (VolumeDistance(Layer, X, Middle, Z, null, false) < 0) Lower = Middle;
            else Upper = Middle;
        }
        if (Lower > Peak[1]) Peak = [X, Lower, Z];
    }
    return Peak;
}

try
{
    await Page.goto(Origin, { waitUntil: 'networkidle' });
    await Page.locator('#LandscapeTab').click();
    const Overview = await WaitForGpuImage(Page);
    Assert.equal(Overview.completedQuality, '96');
    Assert.ok(Number(Overview.surfaceTriangles) > 96798 && Number(Overview.surfaceTriangles) <= 120000);
    await Page.locator('[data-inspector="region"]').click();
    await Page.locator('#Resolution').selectOption('512');
    await Page.locator('#Vertical').selectOption('96');
    const Authored = await SaveAuthoringDocument(Page);
    Assert.deepEqual(Authored.Layers[0].Regions[0].Resolution, [512, 96, 512]);
    Assert.deepEqual(Authored.Layers.slice(1), CreateCoastalWorld().slice(1));
    Assert.equal(Authored.Layers[0].Resolution, 1024);
    Assert.equal(Authored.Layers[0].Vertical, 128);
    Assert.match(await Page.locator('#VolumeNote').textContent(), /X × Y × Z/);
    const Lattice = ConstructVolumeLattice(Authored.Layers[0], Authored.Layers[0].Regions[0]);
    Verify('UI X/Z linking creates a per-region 512 × 96 × 512 target without changing other landscapes or legacy defaults',
        { Dimensions: Lattice.Dimensions, PitchMetres: Lattice.Pitch, DenseFloat32MiBNotAllocated: Lattice.Cells * 4 / 1048576 });
    const Before = await WaitForGpuImage(Page), Camera = JSON.parse(Before.completedComposition)[0];
    const Bounds = await Page.locator('#Volume').boundingBox(), Landmark = FindCoastalLandmark(Authored.Layers[0]);
    const Screen = ProjectPoint(Camera, Landmark, Bounds.width, Bounds.height);
    Assert.ok(Screen.every(Coordinate => Coordinate > .05 && Coordinate < .95), String(Screen));
    const Pick = await Inspect({ Category: 'pick', X: Screen[0], Y: Screen[1] });
    Assert.ok(Pick.Found && Pick.LayerIndex === 0 && Pick.Position[1] > 50, JSON.stringify(Pick));
    const FocusStart = performance.now();
    await Page.mouse.dblclick(Bounds.x + Screen[0] * Bounds.width, Bounds.y + Screen[1] * Bounds.height, { delay: 60 });
    await Page.waitForFunction(Orbit =>
    {
        const D = document.querySelector('#Volume').dataset;
        return D.completedComposition && JSON.parse(D.completedComposition)[0].Orbit < Orbit * .5 && !D.completedDetailKey;
    }, Camera.Orbit, { polling: 25, timeout: 6000 });
    const CoarseAtWorkCamera = await ReadGpuPixels(Page);
    await Capture('CoarseAtWorkCamera');
    const Native = await WaitForWorking();
    const NativePixels = await ReadGpuPixels(Page), Linked = await Inspect('detail');
    Assert.deepEqual(Native.Working.Pitch, Lattice.Pitch);
    Assert.deepEqual(Native.Working.Factors, [1, 1, 1]);
    Assert.ok(Native.Working.Cells.every(Count => Count <= 64));
    Assert.equal(Linked.Error, 0);
    Assert.ok(Linked.ContextBatches > 0 && Linked.DetailBatches > 0 && Linked.DetailTriangles > 0);
    Assert.equal(NativePixels.Error, 0);
    Assert.notEqual(NativePixels.Hash, CoarseAtWorkCamera.Hash);
    Assert.deepEqual(JSON.parse(NativePixels.Telemetry.completedComposition)[0], JSON.parse(CoarseAtWorkCamera.Telemetry.completedComposition)[0]);
    Assert.deepEqual((await SaveAuthoringDocument(Page)).Layers, Authored.Layers);
    await Capture('Working');
    Verify('double-clicking a real XYZ surface focuses a bounded authored-pitch GPU brick; before/after images use the same camera',
        { Milliseconds: performance.now() - FocusStart, Pick, Working: Native.Working, Linked, CoarseHash: CoarseAtWorkCamera.Hash,
            WorkingHash: NativePixels.Hash, Triangles: Native.Telemetry.surfaceTriangles, Samples: Native.Telemetry.surfaceSamples,
            PreparationMilliseconds: Native.Telemetry.surfacePreparationMilliseconds, MaximumSliceMilliseconds: Native.Telemetry.surfaceMaximumSliceMilliseconds });

    await Page.mouse.move(Bounds.x + Bounds.width * .55, Bounds.y + Bounds.height * .6);
    await Page.mouse.down();
    await Page.mouse.move(Bounds.x + Bounds.width * .55 + 45, Bounds.y + Bounds.height * .6 + 8, { steps: 6 });
    await Page.mouse.up();
    await Page.waitForFunction(Previous => document.querySelector('#Volume').dataset.completedComposition !== Previous,
        Native.Telemetry.completedComposition, { polling: 50 });
    const Rotated = await WaitForWorking();
    Assert.equal(Rotated.Telemetry.completedDetailKey, Native.Telemetry.completedDetailKey);
    Assert.equal(Rotated.Telemetry.surfacePreparationMilliseconds, Native.Telemetry.surfacePreparationMilliseconds);
    const OceanBefore = await Inspect('ocean');
    for (const Delta of [-160, 320, -160])
    {
        const Previous = await Page.locator('#Volume').getAttribute('data-completed-composition');
        await Page.locator('#Volume').evaluate((Canvas, Delta) => Canvas.dispatchEvent(new WheelEvent('wheel', { deltaY: Delta, bubbles: true, cancelable: true })), Delta);
        await Page.waitForFunction(Previous => document.querySelector('#Volume').dataset.completedComposition !== Previous, Previous, { polling: 50 });
        await WaitForWorking();
        Assert.deepEqual(await Inspect('ocean'), OceanBefore);
    }
    Verify('orbit and dolly reuse the working geometry; actual GPU water spectrum, phase, time and world grid stay distance-stable',
        { DetailKey: Rotated.Telemetry.completedDetailKey, PreparationMilliseconds: Rotated.Telemetry.surfacePreparationMilliseconds, Ocean: OceanBefore });

    const PanStart = performance.now();
    await Page.mouse.move(Bounds.x + Bounds.width * .5, Bounds.y + Bounds.height * .5);
    await Page.mouse.down({ button: 'middle' });
    await Page.mouse.move(Bounds.x + Bounds.width * .5 + 90, Bounds.y + Bounds.height * .5 + 28, { steps: 10 });
    await Page.mouse.up({ button: 'middle' });
    const Panned = await WaitForWorking(Native.Telemetry.completedDetailKey);
    Assert.deepEqual(Panned.Working.Pitch, Lattice.Pitch);
    Assert.equal(Panned.Telemetry.workingContextReused, 'true');
    Assert.deepEqual((await SaveAuthoringDocument(Page)).Layers, Authored.Layers);
    Verify('panning moves the resident brick at unchanged authored spacing and reuses bounded context without changing the document',
        { Milliseconds: performance.now() - PanStart, Working: Panned.Working, ContextReused: Panned.Telemetry.workingContextReused,
            Samples: Panned.Telemetry.surfaceSamples, Evaluations: Panned.Telemetry.surfaceEvaluations });

    await Page.locator('[data-inspector="shape"]').click();
    await Page.locator('[data-operation-index="0"]').click();
    const OldRevision = Panned.Telemetry.volumeRevision;
    for (const Amount of ['190', '195', '200'])
        await Page.locator('[data-property="Amount"]').evaluate((Input, Amount) => { Input.value = Amount; Input.dispatchEvent(new Event('change')); }, Amount);
    await Page.waitForFunction(Revision => document.querySelector('#Volume').dataset.volumeRevision !== Revision, OldRevision, { polling: 50 });
    const Edited = await WaitForWorking();
    const EditedDocument = await SaveAuthoringDocument(Page);
    Assert.equal(EditedDocument.Layers[0].Landforms[0].Amount, 200);
    Assert.deepEqual(EditedDocument.Layers.slice(1), Authored.Layers.slice(1));
    Assert.deepEqual(Edited.Working.Dimensions, [512, 96, 512]);
    Assert.equal(Edited.Telemetry.geometryCompletedRevision, Edited.Telemetry.volumeRevision);
    Verify('real procedural edits during working mode cancel stale preparation and complete the latest revision at the region target',
        { Revision: Edited.Telemetry.volumeRevision, GeometryRevision: Edited.Telemetry.geometryCompletedRevision, Working: Edited.Working });

    const CameraBeforeMask = JSON.parse(Edited.Telemetry.completedComposition)[0];
    await Page.locator('#MaskTab').click();
    await Page.waitForFunction(() => document.querySelector('#Volume').dataset.previewSuspended === 'true', null, { polling: 50 });
    await Page.locator('#LandscapeTab').click();
    const Resumed = await WaitForWorking();
    Assert.deepEqual(JSON.parse(Resumed.Telemetry.completedComposition)[0], CameraBeforeMask);
    Assert.deepEqual((await SaveAuthoringDocument(Page)).Layers, EditedDocument.Layers);
    await Page.locator('#WorkingOverview').click();
    await Page.waitForFunction(() => !document.querySelector('#Volume').dataset.completedDetailKey &&
        !document.querySelector('#Volume').dataset.geometryCompletedDetailKey, null, { polling: 50 });
    await WaitForGpuImage(Page);
    Verify('Mask suspension and resume preserve working camera and authoring; explicit overview returns to the high-detail whole-scene surface',
        { Camera: CameraBeforeMask, OverviewQuality: 96 });

    await Page.locator('#WorkHere').click();
    await Page.waitForFunction(() =>
    {
        const D = document.querySelector('#Volume').dataset;
        return D.requestedDetailKey && D.geometryPreparationEpoch !== D.geometryCompletedEpoch;
    }, null, { polling: 20 });
    const CancelledEpoch = Number(await Page.locator('#Volume').getAttribute('data-geometry-preparation-epoch'));
    await Page.locator('#WorkingOverview').click();
    await Page.waitForFunction(Cancelled =>
    {
        const D = document.querySelector('#Volume').dataset;
        return Number(D.geometryPreparationEpoch) > Cancelled && D.geometryCompletedEpoch === D.geometryPreparationEpoch &&
            D.completedPreparationEpoch === D.geometryCompletedEpoch && !D.completedDetailKey && !D.requestedDetailKey;
    }, CancelledEpoch, { polling: 50 });
    Verify('leaving working mode during preparation cancels the pending brick and cannot upload it over the completed overview',
        { CancelledEpoch, CompletedEpoch: await Page.locator('#Volume').getAttribute('data-completed-preparation-epoch') });

    const Fixture = { Format: 'FrontierLandscape/1', Layers: [{ Name: 'XYZ arch regions', Color: '#b9a481', Visible: true,
        Depth: 120, Caves: false, Resolution: 256, Vertical: 64, DistanceMode: 'bound',
        Landforms: [{ ...CreateLandform('arch'), Amount: 140, Blend: 0 }],
        Strokes: [{ Points: [[0, 0], [1180, 0]], Radius: 150, Erase: false }],
        Regions: [{ X0: -180, Z0: -180, X1: 180, Z1: 180, Edge: 0, Resolution: [512, 96, 256] },
            { X0: 1000, Z0: -180, X1: 1360, Z1: 180, Edge: 0, Resolution: [32, 96, 24] }] }] };
    await Import(Fixture);
    await Page.locator('[data-inspector="region"]').click();
    Assert.equal(await Page.locator('#LinkHorizontal').isChecked(), false);
    Assert.equal(await Page.locator('#ResolutionZ').inputValue(), '256');
    await Page.locator('#Resolution').selectOption('1024');
    let Independent = await SaveAuthoringDocument(Page);
    Assert.deepEqual(Independent.Layers[0].Regions[0].Resolution, [1024, 96, 256]);
    Assert.deepEqual(Independent.Layers[0].Regions[1].Resolution, [32, 96, 24]);
    await Page.locator('#Resolution').selectOption('512');
    await WaitForGpuImage(Page);
    await Page.locator('#WorkHere').click();
    const Arch = await WaitForWorking();
    Assert.deepEqual(Arch.Working.Dimensions, [512, 96, 256]);
    Assert.deepEqual(Arch.Working.Pitch, ConstructVolumeLattice(Fixture.Layers[0], Fixture.Layers[0].Regions[0]).Pitch);
    await Page.locator('#ViewportOcean').click();
    await WaitForWorking();
    await Capture('Arch');
    Verify('two regions in one mask keep independent anisotropic targets; Work here also supports the genuine volumetric arch fixture', Arch.Working);

    await Page.locator('.volume-export').evaluate(Details => Details.open = true);
    await Page.locator('#BakeRegion').selectOption('1');
    Assert.equal(await Page.locator('#Resolution').inputValue(), '32');
    Assert.equal(await Page.locator('#Vertical').inputValue(), '96');
    Assert.equal(await Page.locator('#ResolutionZ').inputValue(), '24');
    await Page.locator('.volume-export').evaluate(Details => Details.open = true);
    const PendingDownload = Page.waitForEvent('download', { timeout: 60000 });
    await Page.locator('#Bake').click();
    const Download = await PendingDownload, Chunks = [];
    for await (const Chunk of await Download.createReadStream()) Chunks.push(Chunk);
    const Volume = DecodeSparseVolume(gunzipSync(Buffer.concat(Chunks)));
    Assert.deepEqual(Volume.Specification.Dimensions, [32, 96, 24]);
    Assert.deepEqual(Volume.Specification.Pitch, ConstructVolumeLattice(Fixture.Layers[0], Fixture.Layers[0].Regions[1]).Pitch);
    Independent = await SaveAuthoringDocument(Page);
    Assert.deepEqual(Independent.Layers[0].Regions[0].Resolution, [512, 96, 256]);
    Assert.deepEqual(Independent.Layers[0].Regions[1].Resolution, [32, 96, 24]);
    Verify('actual UI bake/download/decode uses the selected region’s independent XYZ dimensions including 96 vertical cells',
        { Dimensions: Volume.Specification.Dimensions, Pitch: Volume.Specification.Pitch, Leaves: Volume.Leaves.length, CompressedBytes: Buffer.concat(Chunks).length });

    const Ownership = await Page.evaluate(() => ({ Threads: window.GpuValidation.Threads.filter(Thread => Thread.Graphics).length,
        CpuPixelCopies: window.GpuValidation.CpuPixelCopies, MainThreadGraphicsContexts: window.GpuValidation.MainThreadGraphicsContexts }));
    Assert.deepEqual(Ownership, { Threads: 1, CpuPixelCopies: 0, MainThreadGraphicsContexts: 0 });
    Assert.deepEqual(Errors, []);
    Verify('working detail, procedural editing, selection, navigation and export keep one GPU context owner and no CPU pixel fallback', { Ownership, Errors });
    Report.Status = 'passed';
}
catch (Failure)
{
    Report.Status = 'failed';
    Report.Failure = Failure.stack;
    Report.PreviewAtFailure = await Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset })).catch(() => null);
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
