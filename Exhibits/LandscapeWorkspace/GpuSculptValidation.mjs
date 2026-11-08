/*==========================================================================================================================================
                                                            GPUSCULPTVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Actual always-high geometry, atomic XYZ clay/carve interaction, persistence/export, automatic close detail and protected layer ownership.

import Assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { gunzipSync } from 'node:zlib';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { CreateCoastalWorld } from '../../Experimental/LandscapeWorkspace/CoastalWorld.js';
import { VolumeDistance } from '../../Experimental/LandscapeWorkspace/DistanceField.js';
import { DecodeSparseVolume, ReadVolumeCell } from '../../Experimental/LandscapeWorkspace/SparseVolume.js';
import {
    GraphicsArguments, ConstructGpuReport, ObserveGpuBrowser, WaitForGpuImage, ReadGpuPixels, SaveAuthoringDocument, VerifyGpuCheck
} from './GpuValidationEnvironment.mjs';

const Origin = process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173';
const Prefix = process.env.LANDSCAPE_REPORT_PREFIX || 'GpuSculpt';
const Report = await ConstructGpuReport(Origin), Results = new URL('./Results/', import.meta.url), Images = new URL('./Images/', import.meta.url);
await mkdir(Results, { recursive: true }); await mkdir(Images, { recursive: true });
const Browser = await chromium.launch({ executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined, headless: true, args: GraphicsArguments });
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1 });
Page.setDefaultTimeout(18000);
const Errors = [];
Page.on('pageerror', Failure => Errors.push(Failure.message));
Page.on('console', Message => { if (Message.type() === 'error') Errors.push(Message.text()); });
await ObserveGpuBrowser(Page);
const Verify = (Name, Observed) => VerifyGpuCheck(Report, Name, Observed);
const Read = () => Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset }));
async function Capture(Suffix)
{
    const Name = Prefix + Suffix + '.png'; await Page.screenshot({ path: new URL(Name, Images).pathname }); Report.Images.push(Name);
}
async function Working(AfterRevision = -1)
{
    await Page.waitForFunction(After =>
    {
        const D = document.querySelector('#Volume').dataset;
        return ['failed', 'lost'].includes(D.renderStatus) || Number(D.completedRevision) > After && D.completedDetailKey &&
            D.completedDetailKey === D.requestedDetailKey && D.completedRevision === D.volumeRevision &&
            D.geometryCompletedEpoch === D.geometryPreparationEpoch && D.completedPreparationEpoch === D.geometryCompletedEpoch &&
            D.completedComposition === D.submittedComposition && D.previewDensitySettled === 'true';
    }, AfterRevision, { polling: 50 });
    const D = await Read(); Assert.equal(D.renderStatus, 'ready', D.renderMessage); return D;
}
async function Import(Document)
{
    await Page.locator('#File').setInputFiles({ name: 'SculptFixture.frontier.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(Document)) });
    await Page.waitForFunction(() => document.querySelector('#SaveStatus').textContent === 'Saved locally');
    await WaitForGpuImage(Page);
}
async function Stroke(Mode, Radius, Strength, Start = [.48, .64], End = [.56, .64])
{
    await Page.locator('#SculptMode').selectOption(Mode);
    for (const [Name, Value] of [['Radius', Radius], ['Strength', Strength]])
        await Page.locator('#Sculpt' + Name).evaluate((Control, Value) => { Control.value = String(Value); Control.dispatchEvent(new Event('input')); }, Value);
    const Previous = Number((await Read()).volumeRevision), Bounds = await Page.locator('#Volume').boundingBox();
    await Page.mouse.move(Bounds.x + Bounds.width * Start[0], Bounds.y + Bounds.height * Start[1]);
    await Page.mouse.down();
    await Page.mouse.move(Bounds.x + Bounds.width * End[0], Bounds.y + Bounds.height * End[1], { steps: 12 });
    await Page.mouse.up();
    await Page.waitForFunction(() => document.querySelector('#Volume').dataset.sculptPending === 'false');
    const Doc = await SaveAuthoringDocument(Page);
    Assert.ok(Doc.Layers[0].Regions[0].SculptStrokes?.length, await Page.locator('#Status').textContent());
    await Working(Previous); return Doc;
}

try
{
    await Page.goto(Origin, { waitUntil: 'networkidle' }); await Page.locator('#LandscapeTab').click();
    const High = await WaitForGpuImage(Page), Refinement = JSON.parse(High.displayedSurfaceRefinement);
    Assert.equal(High.completedQuality, '96'); Assert.equal(High.previewWidth, '1440');
    Assert.ok(Number(High.displayedSurfaceTriangles) > 96798 && Number(High.displayedSurfaceTriangles) <= 120000);
    Assert.ok(Number(High.surfaceSamples) <= 1100000);
    Assert.equal(Refinement.Method, 'projected-adaptive-xyz'); Assert.ok(Refinement.ProjectedVertices > 30000 && Refinement.SplitEdges > 10000);
    Assert.equal(await Page.locator('#RenderQuality').isVisible(), false);
    Assert.equal(await Page.locator('#GeometryQuality').textContent(), 'High detail');
    Assert.deepEqual((await SaveAuthoringDocument(Page)).Layers, CreateCoastalWorld());
    const HighPixels = await ReadGpuPixels(Page); Assert.equal(HighPixels.Error, 0); Assert.ok(HighPixels.LandPixels > 500);
    await Capture('High');
    Verify('high geometry is the default real field-projected mesh, with no low-resolution selector or 16→48→64 image replacement',
        { Telemetry: High, Refinement, Pixels: HighPixels });

    const Layer = { Name: 'Sculpt owner', Color: '#b9a481', Visible: true, Depth: 24, Caves: false, Resolution: 512, Vertical: 128,
        DistanceMode: 'bound', Landforms: [], Strokes: [{ Radius: 28, Points: [[0, 0]], Erase: false }],
        Regions: [{ X0: -30, X1: 30, Z0: -30, Z1: 30, Edge: 0, Resolution: [128, 128, 128] }] };
    const Other = structuredClone(Layer); Other.Name = 'Protected neighbour'; Other.Color = '#94a8ae';
    Other.Strokes[0].Points = [[120, 0]]; Other.Regions[0].X0 += 120; Other.Regions[0].X1 += 120;
    const Fixture = { Format: 'FrontierLandscape/1', Layers: [Layer, Other] };
    await Import(Fixture); await Page.locator('#SculptTool').click();
    const Before = await Working(), Camera = JSON.parse(Before.completedComposition)[0], BeforePixels = await ReadGpuPixels(Page);
    Assert.equal(await Page.locator('#SculptTool').getAttribute('aria-pressed'), 'true');
    await Capture('BeforeClay');
    const Clay = await Stroke('add', 6, .6), ClayPixels = await ReadGpuPixels(Page);
    Assert.notEqual(ClayPixels.Hash, BeforePixels.Hash); Assert.equal(ClayPixels.Error, 0);
    Assert.deepEqual(Clay.Layers[1], Fixture.Layers[1]);
    Assert.equal(Clay.Layers[0].Regions[0].SculptStrokes[0].Mode, 'add');
    Assert.deepEqual(JSON.parse((await Read()).completedComposition)[0], Camera);
    await Capture('Clay');
    Verify('left-drag clay creates persistent region-owned XYZ geometry and different actual GPU pixels at the exact same camera; other layer unchanged',
        { Camera, BeforeHash: BeforePixels.Hash, ClayHash: ClayPixels.Hash, Strokes: Clay.Layers[0].Regions[0].SculptStrokes });

    const Carved = await Stroke('remove', 3, 1, [.5, .64], [.55, .64]), CarvedPixels = await ReadGpuPixels(Page);
    Assert.notEqual(CarvedPixels.Hash, ClayPixels.Hash);
    Assert.deepEqual(Carved.Layers[1], Fixture.Layers[1]);
    Assert.ok(Carved.Layers[0].Regions[0].SculptStrokes.some(Part => Part.Mode === 'remove'));
    await Capture('Carve');
    Verify('carve is a true 3D signed-field subtraction, saved in the region rather than a painted overlay or height texture',
        { ClayHash: ClayPixels.Hash, CarveHash: CarvedPixels.Hash, Strokes: Carved.Layers[0].Regions[0].SculptStrokes });

    let Revision = Number((await Read()).volumeRevision);
    await Page.locator('#HistoryUndo').click(); await Working(Revision);
    Assert.deepEqual(await SaveAuthoringDocument(Page), Clay);
    Revision = Number((await Read()).volumeRevision);
    await Page.locator('#HistoryUndo').click(); await Working(Revision);
    Assert.deepEqual(await SaveAuthoringDocument(Page), Fixture);
    Revision = Number((await Read()).volumeRevision);
    await Page.locator('#HistoryRedo').click(); await Working(Revision);
    Assert.deepEqual(await SaveAuthoringDocument(Page), Clay);
    Verify('each released drag is one atomic undoable stroke; two undos and one redo restore the exact expected documents', { AtomicStrokes: 2 });

    await Import(Clay); await Working();
    Assert.deepEqual(await SaveAuthoringDocument(Page), Clay);
    const SavedCamera = JSON.parse((await Read()).completedComposition)[0];
    await Page.locator('#MaskTab').click(); await Page.locator('#LandscapeTab').click(); await Working();
    Assert.deepEqual(await SaveAuthoringDocument(Page), Clay);
    Assert.deepEqual(JSON.parse((await Read()).completedComposition)[0], SavedCamera);
    Verify('project download/import preserve sculpt geometry and target; Mask/resume preserve the loaded document and camera',
        { RegionTarget: Clay.Layers[0].Regions[0].Resolution, Strokes: Clay.Layers[0].Regions[0].SculptStrokes.length });

    await Page.locator('#SculptTool').click();
    // 📝 Import/resume may leave the brush enabled; normalize through its visible pressed state without changing application source.
    if (await Page.locator('#SculptTool').getAttribute('aria-pressed') !== 'true') await Page.locator('#SculptTool').click();
    await Working();
    const Bounds = await Page.locator('#Volume').boundingBox();
    await Page.mouse.move(Bounds.x + Bounds.width * .5, Bounds.y + Bounds.height * .6); await Page.mouse.down();
    await Page.locator('#Volume').dispatchEvent('pointercancel'); await Page.mouse.up();
    await Page.waitForFunction(() => document.querySelector('#Volume').dataset.sculptPending === 'false');
    Assert.deepEqual(await SaveAuthoringDocument(Page), Clay);
    await Page.keyboard.down('Alt'); await Page.mouse.move(Bounds.x + Bounds.width * .6, Bounds.y + Bounds.height * .7);
    await Page.mouse.down(); await Page.mouse.move(Bounds.x + Bounds.width * .6 + 12, Bounds.y + Bounds.height * .7 + 4); await Page.mouse.up(); await Page.keyboard.up('Alt');
    Assert.deepEqual(await SaveAuthoringDocument(Page), Clay);
    Verify('cancelled pointers and Alt-orbit never commit accidental sculpt geometry', { DocumentUnchanged: true });

    await Page.locator('#WorkingOverview').click();
    await Page.waitForFunction(() => document.querySelector('#Volume').dataset.displayedSamplingMode === 'high' && !document.querySelector('#Volume').dataset.completedDetailKey);
    await WaitForGpuImage(Page);
    await Page.locator('#Volume').dispatchEvent('wheel', { deltaY: -4000, bubbles: true });
    await Page.waitForFunction(() => document.querySelector('#Volume').dataset.automaticWorking === 'true');
    const Automatic = await Working();
    Assert.ok(JSON.parse(Automatic.completedComposition)[0].Orbit < 25);
    Assert.deepEqual(JSON.parse(Automatic.displayedWorkingLattice).Factors, [1, 1, 1]);
    Assert.deepEqual(await SaveAuthoringDocument(Page), Clay);
    Verify('zooming in automatically requests native XYZ detail without Work here or another high-resolution option', Automatic);

    const BakingDocument = structuredClone(Clay);
    BakingDocument.Layers[0].Regions[0].Resolution = [32, 32, 32];
    await Import(BakingDocument);
    await Page.locator('[data-inspector="region"]').click();
    Assert.equal(await Page.locator('#Resolution').inputValue(), '32');
    Assert.equal(await Page.locator('#Vertical').inputValue(), '32');
    await Page.locator('.volume-export').evaluate(Details => Details.open = true);
    const DownloadPromise = Page.waitForEvent('download', { timeout: 60000 }); await Page.locator('#Bake').click();
    const Download = await DownloadPromise, Chunks = [];
    for await (const Chunk of await Download.createReadStream()) Chunks.push(Chunk);
    const Volume = DecodeSparseVolume(gunzipSync(Buffer.concat(Chunks))), Specification = Volume.Specification;
    Assert.deepEqual(Specification.Dimensions, [32, 32, 32]);
    Assert.deepEqual(Specification.Region.SculptStrokes, Clay.Layers[0].Regions[0].SculptStrokes);
    let AddedCells = 0;
    for (let Z = 0; Z < 32; Z++) for (let Y = 0; Y < 32; Y++) for (let X = 0; X < 32; X++)
    {
        const Point = [X, Y, Z].map((Coordinate, Axis) => Specification.Origin[Axis] + (Coordinate + .5) * Specification.Pitch[Axis]);
        const Actual = ReadVolumeCell(Volume, X, Y, Z);
        if (Actual < 0 && VolumeDistance(Fixture.Layers[0], ...Point, null, false) > 0) AddedCells++;
    }
    Assert.ok(AddedCells > 0);
    Verify('actual UI sparse-volume bake/download/decode contains sculpted added solid cells and retains the exact XYZ brush provenance',
        { Dimensions: Specification.Dimensions, AddedCells, CompressedBytes: Buffer.concat(Chunks).length });

    await Import(Clay);
    await Page.locator('#SculptTool').click();
    if (await Page.locator('#SculptTool').getAttribute('aria-pressed') !== 'true') await Page.locator('#SculptTool').click();
    await Working();
    await Page.locator('#Volume').dispatchEvent('wheel', { deltaY: 900, bubbles: true });
    await Page.keyboard.down('Alt');
    await Page.mouse.move(Bounds.x + Bounds.width * .5, Bounds.y + Bounds.height * .72);
    await Page.mouse.down();
    await Page.mouse.move(Bounds.x + Bounds.width * .5, Bounds.y + Bounds.height * .32, { steps: 15 });
    await Page.mouse.up(); await Page.keyboard.up('Alt');
    await Page.waitForFunction(() => JSON.parse(document.querySelector('#Volume').dataset.completedComposition)[0].Pitch < -.8);
    await Working();
    Assert.ok(JSON.parse((await Read()).completedComposition)[0].Pitch < -.8);
    await Page.locator('#SculptTool').click();
    await Page.locator('#SculptTool').click();
    await Page.waitForFunction(() => document.querySelector('#SculptEnable').textContent === 'Finish sculpting' &&
        JSON.parse(document.querySelector('#Volume').dataset.completedComposition)[0].Target[1] < -23);
    await Working();
    const UndersideBefore = await ReadGpuPixels(Page), UndersideCamera = JSON.parse((await Read()).completedComposition)[0];
    const UndersideDocument = await Stroke('remove', 2, 1, [.48, .5], [.55, .5]);
    const UndersideStroke = UndersideDocument.Layers[0].Regions[0].SculptStrokes.at(-1), UndersideAfter = await ReadGpuPixels(Page);
    Assert.ok(UndersideStroke.Points.every(Point => Point[1] < -23));
    Assert.notEqual(UndersideAfter.Hash, UndersideBefore.Hash);
    Assert.deepEqual(UndersideDocument.Layers[1], Fixture.Layers[1]);
    Assert.deepEqual(JSON.parse((await Read()).completedComposition)[0], UndersideCamera);
    await Capture('Underside');
    Verify('actual below-ground camera, canonical underside picking and left-drag carve alter negative-Y geometry, not a heightmap',
        { Camera: UndersideCamera, Stroke: UndersideStroke, BeforeHash: UndersideBefore.Hash, AfterHash: UndersideAfter.Hash });

    const Ownership = await Page.evaluate(() => ({ Threads: window.GpuValidation.Threads.filter(Thread => Thread.Graphics).length,
        MainThreadGraphicsContexts: window.GpuValidation.MainThreadGraphicsContexts, CpuPixelCopies: window.GpuValidation.CpuPixelCopies }));
    Assert.deepEqual(Ownership, { Threads: 1, MainThreadGraphicsContexts: 0, CpuPixelCopies: 0 }); Assert.deepEqual(Errors, []);
    Verify('high mesh refinement, brush picking, sculpt edits and exports retain one graphics owner and no software-pixel fallback', { Ownership, Errors });
    Report.Status = 'passed';
}
catch (Failure)
{
    Report.Status = 'failed'; Report.Failure = Failure.stack; Report.TelemetryAtFailure = await Read().catch(() => null);
    Report.EditorStatus = await Page.locator('#Status').textContent().catch(() => null); await Capture('Failure').catch(() => {});
    console.error(Failure); process.exitCode = 1;
}
finally
{
    await Browser.close(); await writeFile(new URL(Prefix + '.json', Results), JSON.stringify(Report, null, 2) + '\n');
    console.log(JSON.stringify({ Status: Report.Status, Checks: Report.Checks.length }));
}
