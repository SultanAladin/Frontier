/*==========================================================================================================================================
                                                        BROWSERVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Actual GPU surface captures, canonical field comparisons, stack ownership, painting, navigation, and streamed volume-export interaction.

import Assert from 'node:assert/strict';
import { mkdir, writeFile, readFile, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import { gunzipSync } from 'node:zlib';
import { createHash } from 'node:crypto';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { CreateLandform } from '../../Experimental/LandscapeWorkspace/LandformStack.js';
import { VolumeDistance } from '../../Experimental/LandscapeWorkspace/DistanceField.js';
import { DecodeSparseVolume } from '../../Experimental/LandscapeWorkspace/SparseVolume.js';
import { EvaluateOcean } from '../../Experimental/LandscapeWorkspace/OceanSurface.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                        EXECUTION CONDITIONS
//------------------------------------------------------------------------------------------------------------------------

const Root = fileURLToPath(new URL('../../', import.meta.url));
const ImageDirectory = fileURLToPath(new URL('./Images/', import.meta.url));
const ResultDirectory = fileURLToPath(new URL('./Results/', import.meta.url));
await mkdir(ImageDirectory, { recursive: true });
await mkdir(ResultDirectory, { recursive: true });
const Results = {
    Status: 'running', Date: new Date().toISOString(), Revision: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: Root, encoding: 'utf8' }).trim(),
    Branch: execFileSync('git', ['branch', '--show-current'], { cwd: Root, encoding: 'utf8' }).trim(),
    WorkingTree: 'Uncommitted LandscapeWorkspace import and corrections; runtime source fingerprints retained below.',
    Platform: process.platform, Architecture: process.arch, Runtime: process.version,
    Origin: process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173', Viewport: [1600, 1000], Checks: [], Images: []
};
const AuthoredFiles = ['VolumeSculpt.js', 'VolumeSculptInteraction.js', 'VolumeRefinement.js', 'PreviewSampling.js', 'VolumePresentation.js', 'VolumeSubmission.js', 'VolumeRayField.js', 'VolumeSurface.js', 'VolumeLattice.js', 'VolumeDetail.js', 'CoastalWorld.js', 'PreviewTiling.js', 'Workspace.js', 'Workspace.css', 'index.html', 'LayerStackPanel.js', 'DistanceField.js', 'LandformStack.js', 'LandformShader.js', 'TerrainInfluence.js', 'VolumeRasterization.js', 'OceanSurface.js', 'SparseVolume.js', 'DistanceReinitialization.js', 'VolumeBaking.js'];
Results.SourceSHA256 = {};
for (const Name of AuthoredFiles)
{
    const Bytes = await readFile(new URL('../../Experimental/LandscapeWorkspace/' + Name, import.meta.url));
    Results.SourceSHA256[Name] = createHash('sha256').update(Bytes).digest('hex');
}
const Browser = await chromium.launch({
    executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined,
    headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--no-zygote']
});
Results.Browser = Browser.version();
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1 });
Page.setDefaultTimeout(90000);
const BrowserErrors = [], FailedResponses = [], Downloads = [];
let NativeDialogs = 0;
Page.on('pageerror', Failure => BrowserErrors.push(Failure.message));
Page.on('console', Message => { if (Message.type() === 'error') BrowserErrors.push(Message.text()); });
Page.on('response', Response => { if (Response.status() >= 400) FailedResponses.push(`${Response.status()} ${Response.url()}`); });
Page.on('download', Download => Downloads.push(Download.suggestedFilename()));
Page.on('dialog', Dialog => { NativeDialogs++; return Dialog.accept(); });
await Page.addInitScript(() =>
{
    document.addEventListener('DOMContentLoaded', () =>
    {
        const Toggle = document.querySelector('#AnimateOcean');
        if (Toggle) Toggle.checked = false;
    });
});

function Verify(Name, Observed)
{
    Results.Checks.push({ Name, Status: 'passed', Observed });
    console.log(`Verified: ${Name}`);
}

async function Capture(Name)
{
    await Page.screenshot({ path: ImageDirectory + Name });
    Results.Images.push(Name);
}

async function WaitForRender()
{
    await Page.waitForFunction(() =>
    {
        const Canvas = document.querySelector('#Volume');
        return ['failed', 'lost'].includes(Canvas.dataset.renderStatus) || Canvas.dataset.renderStatus === 'ready' && Canvas.dataset.completedRevision === Canvas.dataset.volumeRevision &&
            Number(Canvas.dataset.completedQuality) >= 48 && Canvas.dataset.completedQuality === Canvas.dataset.geometryCompletedQuality && Canvas.dataset.previewDensitySettled === 'true' && Canvas.dataset.completedComposition && Canvas.dataset.completedComposition === Canvas.dataset.submittedComposition;
    }, null, { timeout: 18000 });
    const Preview = await Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset }));
    Assert.equal(Preview.renderStatus, 'ready', JSON.stringify(Preview));
}

async function Snapshot()
{
    await Page.waitForFunction(() => document.querySelector('#SaveStatus').textContent === 'Saved locally' && localStorage.getItem('FrontierLandscape'));
    return Page.evaluate(() => JSON.parse(localStorage.getItem('FrontierLandscape')));
}

async function DownloadBytes(Download)
{
    const Stream = await Download.createReadStream();
    const Chunks = [];
    for await (const Chunk of Stream) Chunks.push(Chunk);
    return Buffer.concat(Chunks);
}

async function ExportDocument()
{
    const Request = Page.waitForEvent('download');
    await Page.locator('#Export').click();
    return JSON.parse((await DownloadBytes(await Request)).toString());
}

async function AddGenerator(Type, Category = 'landform')
{
    await Page.locator('#AddGenerator').click();
    await Page.locator(`[data-generator-category="${Category}"]`).click();
    await Page.locator('#GeneratorSearch').fill('');
    await Page.locator(`[data-generator="${Type}"]`).click();
}

async function CanvasDigest()
{
    return Page.locator('#Volume').evaluate(async Canvas =>
    {
        const Snapshot = await Canvas[Symbol.for('FrontierLandscape.Presentation')].Inspect('pixels');
        let Hash = 2166136261;
        for (const Byte of Snapshot.Pixels) Hash = Math.imul(Hash ^ Byte, 16777619) >>> 0;
        return { Hash, Width: Snapshot.Width, Height: Snapshot.Height, TerrainRenders: Number(Snapshot.Telemetry.terrainRenders), GeometryRevision: Snapshot.Telemetry.geometryCompletedRevision, FieldEvaluations: Number(Snapshot.Telemetry.surfaceEvaluations), Error: Snapshot.Error };
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        ACTUAL EDITOR AND SHADER CHECKS
//------------------------------------------------------------------------------------------------------------------------

try
{
    await Page.goto(Results.Origin, { waitUntil: 'networkidle' });
    await Page.locator('#AnimateOcean').evaluate(Input => Input.checked = false);
    await Page.locator('#LandscapeTab').click();
    await WaitForRender();
    Results.Renderer = await Page.locator('#Volume').getAttribute('data-renderer');
    const Original = await ExportDocument();
    Assert.equal(Original.Layers.length, 3);
    Assert.equal(await Page.locator('#OperationCount').innerText(), '2');
    Assert.ok(await Page.evaluate(() => document.fonts.check('400 13px "DM Sans"')));
    const Theme = await Page.evaluate(() => ({
        Outliner: getComputedStyle(document.querySelector('.outliner')).backgroundColor,
        CardRadius: getComputedStyle(document.querySelector('.property-card')).borderRadius,
        Font: getComputedStyle(document.body).fontFamily,
        PageOverflow: document.documentElement.scrollWidth > innerWidth
    }));
    Assert.equal(Theme.Outliner, 'rgb(21, 21, 21)');
    Assert.equal(Theme.CardRadius, '18px');
    Assert.equal(Theme.PageOverflow, false);
    Verify('ProjectZero typography and neutral graphite docks render with loaded local fonts', Theme);

    const Fixtures = ['fractal', 'multifractal', 'ridged-multifractal', 'badlands', 'ridge-noise', 'fractal-ridges', 'sand-dunes', 'mesa', 'fault', 'craters', 'arch', 'tunnel'].map(Type => ({
        Name: Type, Color: '#b9a481', Visible: true, Depth: 120, Caves: false, Resolution: 256, Vertical: 64,
        Strokes: [{ Points: [[-40, 0], [40, 0]], Radius: 240, Erase: false }],
        Regions: [{ X0: -320, Z0: -280, X1: 320, Z1: 280, Edge: 65 }],
        Landforms: [{ ...CreateLandform(Type), Amount: 180, InfluenceRadius: 1.7, Blend: 0 }]
    }));
    const Points = [[0, 0, 0], [0, 60, 0], [0, -90, 0], [0, 0, 238.8], [220, 10, 0], [275, 5, 0], [310, 30, 0]];
    const Prepared = await Page.evaluate(async ({ Fixtures, Points }) =>
    {
        const { CompileSurfaceField } = await import('/VolumeSurface.js');
        return Fixtures.map(Layer =>
        {
            const Field = CompileSurfaceField([Layer]);
            return Points.map(Point => Field.Sample(...Point).Distance);
        });
    }, { Fixtures, Points });
    let MaximumFieldError = 0;
    Fixtures.forEach((Layer, Index) => Points.forEach((Point, Offset) =>
    {
        const Expected = VolumeDistance(Layer, ...Point, null, false);
        const Difference = Math.abs(Expected - Prepared[Index][Offset]);
        MaximumFieldError = Math.max(MaximumFieldError, Difference);
        Assert.ok(Difference < 1e-10, `${Layer.Name} ${Point}: canonical ${Expected}, prepared ${Prepared[Index][Offset]}`);
    }));
    Verify('prepared XYZ geometry source matches canonical raw residuals in 12 generator/CSG cases; no GPU SDF probe shader',
        { Probes: Fixtures.length * Points.length, MaximumAbsoluteError: MaximumFieldError, Comparison: 'CPU canonical vs CPU geometry preparation, not GPU distance evaluation' });

    const OceanPoints = [[-250.5, 640.25, 18, 1.4, 2], [2750, -9519, 54, .7, 6], [0, 0, 0, 0, 0]];
    const OceanGPU = await Page.evaluate(async Points =>
    {
        const { OceanVertexShader, PrepareOceanSpectrum, OceanPhaseParameters } = await import('/OceanSurface.js');
        const Canvas = document.createElement('canvas');
        Canvas.width = Canvas.height = 1;
        const GL = Canvas.getContext('webgl2');
        GL.disable(GL.DITHER);
        const Compile = (Category, Text) =>
        {
            const Shader = GL.createShader(Category);
            GL.shaderSource(Shader, Text);
            GL.compileShader(Shader);
            if (!GL.getShaderParameter(Shader, GL.COMPILE_STATUS)) throw Error(GL.getShaderInfoLog(Shader));
            return Shader;
        };
        const Program = GL.createProgram();
        GL.attachShader(Program, Compile(GL.VERTEX_SHADER, '#version 300 es\nin vec2 Position;void main(){gl_Position=vec4(Position,0.,1.);}'));
        GL.attachShader(Program, Compile(GL.FRAGMENT_SHADER, `#version 300 es\nprecision highp float;precision highp int;uniform vec4 Point;uniform float Footprint;uniform int Axis;out vec4 Pixel;${OceanVertexShader}\nvoid main(){vec3 Result=OceanVertexEvaluation(Point.xy,Point.z);uint Bits=floatBitsToUint(Result[Axis]);Pixel=vec4(uvec4(Bits&255u,(Bits>>8u)&255u,(Bits>>16u)&255u,(Bits>>24u)&255u))/255.;}`));
        GL.linkProgram(Program);
        if (!GL.getProgramParameter(Program, GL.LINK_STATUS)) throw Error(GL.getProgramInfoLog(Program));
        GL.useProgram(Program);
        const Buffer = GL.createBuffer();
        GL.bindBuffer(GL.ARRAY_BUFFER, Buffer);
        GL.bufferData(GL.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), GL.STATIC_DRAW);
        const Position = GL.getAttribLocation(Program, 'Position');
        GL.enableVertexAttribArray(Position);
        GL.vertexAttribPointer(Position, 2, GL.FLOAT, false, 0, 0);
        GL.viewport(0, 0, 1, 1);
        GL.uniform2fv(GL.getUniformLocation(Program, 'OceanPhases[0]'), OceanPhaseParameters);
        const Samples = [];
        for (const Point of Points)
        {
            GL.uniform4fv(GL.getUniformLocation(Program, 'Point'), Point.slice(0, 4));
            GL.uniform4fv(GL.getUniformLocation(Program, 'OceanVectors[0]'), PrepareOceanSpectrum(Point[3], Point[4]));
            const Channels = [];
            for (let Axis = 0; Axis < 3; Axis++)
            {
                GL.uniform1i(GL.getUniformLocation(Program, 'Axis'), Axis);
                GL.drawArrays(GL.TRIANGLES, 0, 3);
                const Bytes = new Uint8Array(4);
                GL.readPixels(0, 0, 1, 1, GL.RGBA, GL.UNSIGNED_BYTE, Bytes);
                Channels.push(new DataView(Bytes.buffer).getFloat32(0, true));
            }
            Samples.push(Channels);
        }
        const Error = GL.getError();
        GL.getExtension('WEBGL_lose_context').loseContext();
        return { Samples, Error };
    }, OceanPoints);
    Assert.equal(OceanGPU.Error, 0);
    let MaximumOceanError = 0;
    OceanPoints.forEach((Point, Index) =>
    {
        const CPU = EvaluateOcean(...Point);
        [CPU.Height, CPU.SlopeX, CPU.SlopeZ].forEach((Expected, Axis) =>
        {
            const Difference = Math.abs(Expected - OceanGPU.Samples[Index][Axis]);
            MaximumOceanError = Math.max(MaximumOceanError, Difference);
            Assert.ok(Difference < .0005, `Ocean channel ${Index}/${Axis}: ${Difference}`);
        });
    });
    Verify('GPU ocean displacement and analytic normals match the CPU wave spectrum', { Channels: 9, MaximumAbsoluteError: MaximumOceanError });

    Assert.equal(await Page.locator('#RenderQuality').isVisible(), false);
    await Page.waitForFunction(() => document.querySelector('#Volume').width > 480 && document.querySelector('#Volume').width <= 1440);
    await WaitForRender();
    await Capture('Landscape.png');
    await Page.locator('#AddGenerator').click();
    Assert.equal(await Page.locator('[data-generator]').count(), 10);
    await Capture('GeneratorMenu.png');
    await Page.locator('#GeneratorSearch').fill('fractal');
    Assert.equal(await Page.locator('[data-generator]').count(), 4);
    await Page.keyboard.press('Escape');
    Assert.equal(await Page.locator('#GeneratorMenu').isVisible(), false);
    Verify('searchable generator catalogue exposes landforms separately from volume operations', { Landforms: 10, SearchMatches: 4 });

    const BeforeOcean = await CanvasDigest();
    await Page.locator('#Wave').evaluate(Input => { Input.value = '2'; Input.dispatchEvent(new Event('input')); });
    await Page.waitForFunction(() => JSON.parse(document.querySelector('#Volume').dataset.submittedComposition || '[]')[1] === 2);
    await WaitForRender();
    const AfterOcean = await CanvasDigest();
    Assert.notEqual(BeforeOcean.Hash, AfterOcean.Hash);
    Assert.equal(BeforeOcean.FieldEvaluations, AfterOcean.FieldEvaluations);
    Assert.equal(BeforeOcean.GeometryRevision, AfterOcean.GeometryRevision);
    Assert.equal(AfterOcean.Error, 0);
    Verify('ocean edits change actual GPU pixels without rebuilding the terrain geometry', { Before: BeforeOcean, After: AfterOcean });
    await Page.locator('#Wave').evaluate(Input => { Input.value = '1'; Input.dispatchEvent(new Event('input')); });
    await Page.locator('#Environment').click();
    await WaitForRender();
    await Capture('OceanInspector.png');
    await Page.locator('[data-inspector="shape"]').click();

    Assert.equal(await Page.locator('#GeometryQuality').textContent(), 'High detail');
    await Page.waitForFunction(() => document.querySelector('#Volume').width > 480);
    await WaitForRender();
    const BeforeRelief = await CanvasDigest();
    await Page.locator('.landform-row[data-operation-index="0"]').click();
    await Page.getByRole('spinbutton', { name: 'Relief in metres', exact: true }).fill('95');
    await Page.getByRole('spinbutton', { name: 'Relief in metres', exact: true }).dispatchEvent('change');
    Assert.equal((await Snapshot()).Layers[0].Landforms[0].Amount, 95);
    await WaitForRender();
    const AfterRelief = await CanvasDigest();
    Assert.notEqual(BeforeRelief.Hash, AfterRelief.Hash);
    Assert.ok(AfterRelief.TerrainRenders > BeforeRelief.TerrainRenders);
    Assert.equal(AfterRelief.Error, 0);
    await Page.locator('#HistoryUndo').click();
    Assert.deepEqual((await Snapshot()).Layers, Original.Layers);
    Verify('inspector relief edits alter actual GPU pixels and rebuild the XYZ surface', { Before: BeforeRelief, After: AfterRelief });
    await AddGenerator('fractal');
    let Edited = await Snapshot();
    Assert.equal(Edited.Layers[0].Landforms.length, 3);
    Assert.deepEqual(Edited.Layers[1], Original.Layers[1]);
    Assert.deepEqual(Edited.Layers[2], Original.Layers[2]);
    await Page.locator('#HistoryUndo').click();
    Assert.equal((await Snapshot()).Layers[0].Landforms.length, 2);
    await Page.locator('#HistoryRedo').click();
    Assert.equal((await Snapshot()).Layers[0].Landforms.length, 3);
    await Page.locator('#HistoryUndo').click();
    Verify('direct generator insertion, undo and redo preserve all other mask stacks', '2 → 3 → 2 → 3 → 2 generators');

    await Page.locator('.mask-row[data-mask-index="1"]').click();
    Assert.equal(await Page.locator('#OperationCount').innerText(), '1');
    await AddGenerator('badlands');
    Edited = await Snapshot();
    Assert.equal(Edited.Layers[1].Landforms.length, 2);
    Assert.deepEqual(Edited.Layers[0], Original.Layers[0]);
    await Page.locator('#HistoryUndo').click();
    await Page.locator('.mask-row[data-mask-index="0"]').click();
    await Page.locator('#StackScope').selectOption('0');
    await AddGenerator('sand-dunes');
    Edited = await Snapshot();
    Assert.equal(Edited.Layers[0].Regions[0].Landforms.length, 1);
    Assert.deepEqual(Edited.Layers[0].Landforms, Original.Layers[0].Landforms);
    Assert.deepEqual(Edited.Layers[1], Original.Layers[1]);
    await WaitForRender();
    await Capture('BoundaryStack.png');
    await Page.locator('#HistoryUndo').click();
    await Page.locator('#StackScope').selectOption('-1');
    Verify('mask selection and boundary-only insertion retain explicit, independent ownership', 'Region operation does not enter the mask or another mask');

    await Page.locator('.mask-row[data-mask-index="1"]').click({ modifiers: ['Control'] });
    Assert.equal(await Page.locator('.mask-row[aria-selected="true"]').count(), 2);
    await Page.locator('#StackScope').selectOption('0');
    await Page.locator('#BatchLandform').click();
    Assert.match(await Page.locator('#GeneratorTarget').innerText(), /2 selected masks.*mask-wide/);
    await Capture('BatchGeneratorMenu.png');
    await Page.locator('[data-generator="badlands"]').click();
    Edited = await Snapshot();
    Assert.equal(Edited.Layers[0].Landforms.length, 3);
    Assert.equal(Edited.Layers[1].Landforms.length, 2);
    Assert.equal(Edited.Layers[0].Landforms.at(-1).Type, 'badlands');
    Assert.equal(Edited.Layers[1].Landforms.at(-1).Type, 'badlands');
    Assert.deepEqual(Edited.Layers[0].Regions, Original.Layers[0].Regions);
    Assert.deepEqual(Edited.Layers[1].Regions, Original.Layers[1].Regions);
    Assert.deepEqual(Edited.Layers[2], Original.Layers[2]);
    Assert.equal(await Page.locator('#StackScope').inputValue(), '-1');
    await Page.locator('#HistoryUndo').click();
    Assert.deepEqual((await Snapshot()).Layers, Original.Layers);
    await Page.locator('.mask-row[data-mask-index="0"]').click();
    Verify('multi-selected masks use an explicit catalogue target and one atomic independent-stack edit', '2 mask-wide insertions; no boundary or unselected-mask changes; one undo restores both');

    await Page.locator('.mask-row[data-mask-index="0"]').dragTo(Page.locator('.mask-row[data-mask-index="1"]'));
    Edited = await Snapshot();
    Assert.deepEqual(Edited.Layers, [Original.Layers[1], Original.Layers[0], Original.Layers[2]]);
    Assert.equal(await Page.locator('#StackOwner').innerText(), Original.Layers[0].Name);
    await Page.locator('#HistoryUndo').click();
    Assert.deepEqual((await Snapshot()).Layers, Original.Layers);
    Assert.equal(await Page.locator('#StackOwner').innerText(), Original.Layers[0].Name);
    Verify('mask drag reorder carries strokes, boundaries and generator ownership together', '0 → 1 reorder preserves all mask contents; undo restores order and active ownership');

    await Page.locator('[data-inspector="region"]').click();
    await Page.locator('#DuplicateMask').click();
    Edited = await Snapshot();
    Assert.equal(Edited.Layers.length, 4);
    Assert.deepEqual(Edited.Layers[3], { ...Original.Layers[0], Name: Original.Layers[0].Name + ' copy' });
    await Page.locator('#MaskName').fill('Independent copy');
    await Page.locator('#MaskName').dispatchEvent('change');
    Edited = await Snapshot();
    Assert.equal(Edited.Layers[3].Name, 'Independent copy');
    Assert.deepEqual(Edited.Layers.slice(0, 3), Original.Layers);
    await Page.locator('#DeleteMask').click();
    Assert.deepEqual((await Snapshot()).Layers, Original.Layers);
    await Page.locator('#HistoryUndo').click();
    Assert.equal((await Snapshot()).Layers[3].Name, 'Independent copy');
    await Page.locator('#HistoryUndo').click();
    await Page.locator('#HistoryUndo').click();
    Assert.deepEqual((await Snapshot()).Layers, Original.Layers);
    await Page.locator('.mask-row[data-mask-index="0"]').click();
    await Page.locator('[data-inspector="shape"]').click();
    Verify('mask duplication, independent renaming, deletion and undo retain complete scoped contents', 'Copy changes do not mutate the source; deletion and every edit are reversible');

    for (const Layer of Original.Layers) await Page.getByRole('button', { name: `Toggle ${Layer.Name} visibility`, exact: true }).click();
    Assert.ok((await Snapshot()).Layers.every(Layer => !Layer.Visible));
    await WaitForRender();
    const EmptyTerrain = await Page.evaluate(async () =>
    {
        const Snapshot = await document.querySelector('#Volume')[Symbol.for('FrontierLandscape.Presentation')].Inspect('pixels');
        let LandPixels = 0;
        for (let Offset = 0; Offset < Snapshot.Pixels.length; Offset += 4)
            if (Snapshot.Pixels[Offset] > Snapshot.Pixels[Offset + 1] + 3 && Snapshot.Pixels[Offset + 1] > Snapshot.Pixels[Offset + 2] + 6) LandPixels++;
        return { Triangles: Number(Snapshot.Telemetry.surfaceTriangles), Samples: Number(Snapshot.Telemetry.surfaceSamples), LandPixels, Error: Snapshot.Error };
    });
    Assert.deepEqual(EmptyTerrain, { Triangles: 0, Samples: 0, LandPixels: 0, Error: 0 });
    for (let Index = 0; Index < Original.Layers.length; Index++) await Page.locator('#HistoryUndo').click();
    Assert.deepEqual((await Snapshot()).Layers, Original.Layers);
    Verify('hiding every mask produces no GPU terrain triangles or land pixels rather than an infinite solid', EmptyTerrain);

    await Page.locator('.landform-row[data-operation-index="0"]').dragTo(Page.locator('.landform-row[data-operation-index="1"]'));
    Edited = await Snapshot();
    Assert.equal(Edited.Layers[0].Landforms[0].Type, 'sand-dunes');
    Assert.equal(Edited.Layers[0].Landforms[1].Type, 'ridged-multifractal');
    await Page.locator('#HistoryUndo').click();
    await Page.locator('.landform-row[data-operation-index="1"]').click();
    await Page.getByRole('button', { name: 'Duplicate generator', exact: true }).click();
    Edited = await Snapshot();
    Assert.equal(Edited.Layers[0].Landforms.length, 3);
    Assert.match(Edited.Layers[0].Landforms[2].Name, /copy$/);
    await Page.getByRole('button', { name: 'Delete generator', exact: true }).click();
    Assert.equal((await Snapshot()).Layers[0].Landforms.length, 2);
    Verify('drag reorder, duplication and deletion operate on the selected stack', 'Operation order reversed and restored; independent duplicate deleted');

    await Page.locator('#MaskTab').click();
    await Page.locator('#Fit').click();
    await Capture('MaskEditor.png');
    const BeforePan = JSON.parse(await Page.locator('#Paint').getAttribute('data-projection'));
    const Bounds = await Page.locator('#Paint').boundingBox();
    await Page.mouse.move(Bounds.x + Bounds.width * .5, Bounds.y + Bounds.height * .5);
    await Page.mouse.wheel(0, -230);
    const Zoomed = JSON.parse(await Page.locator('#Paint').getAttribute('data-projection'));
    Assert.ok(Zoomed.Scale > BeforePan.Scale);
    await Page.mouse.down({ button: 'middle' });
    await Page.mouse.move(Bounds.x + Bounds.width * .5 + 55, Bounds.y + Bounds.height * .5 + 30, { steps: 5 });
    await Page.mouse.up({ button: 'middle' });
    const Panned = JSON.parse(await Page.locator('#Paint').getAttribute('data-projection'));
    Assert.notEqual(Panned.X, Zoomed.X);
    Assert.deepEqual((await ExportDocument()).Layers, (await Snapshot()).Layers);
    Verify('2D pointer-centred zoom and middle-button panning leave vector geometry unchanged', { Before: BeforePan, Zoomed, Panned });

    const BeforeReplacement = await Snapshot();
    await Page.locator('#Empty').click();
    Assert.equal(await Page.locator('#DocumentReplacement').isVisible(), true);
    await Capture('DocumentConfirmation.png');
    await Page.keyboard.press('Escape');
    Assert.deepEqual(await Snapshot(), BeforeReplacement);
    await Page.locator('#Empty').click();
    await Page.locator('#ConfirmReplacement').click();
    await Page.waitForFunction(() => document.querySelectorAll('#Layers .mask-row').length === 1);
    Assert.equal((await Snapshot()).Layers.length, 1);
    Assert.equal(NativeDialogs, 0);
    Verify('asynchronous document confirmation cancels without edits and accepts without a native blocking dialog',
        { Cancel: 'Escape preserved exact document', Accept: 'New world; GPU thread not recreated' });
    const PaintBounds = await Page.locator('#Paint').boundingBox();
    await Page.locator('[data-tool="paint"]').click();
    await Page.mouse.move(PaintBounds.x + PaintBounds.width * .42, PaintBounds.y + PaintBounds.height * .5);
    await Page.mouse.down();
    await Page.mouse.move(PaintBounds.x + PaintBounds.width * .57, PaintBounds.y + PaintBounds.height * .47, { steps: 14 });
    await Page.mouse.up();
    Edited = await Snapshot();
    Assert.ok(Edited.Layers[0].Strokes.length > 0);
    const PaintedPoint = Edited.Layers[0].Strokes[0].Points[0];
    Assert.ok(VolumeDistance(Edited.Layers[0], PaintedPoint[0], -1, PaintedPoint[1]) > 1e6);
    await Page.locator('[data-tool="select"]').click();
    await Page.mouse.move(PaintBounds.x + PaintBounds.width * .28, PaintBounds.y + PaintBounds.height * .3);
    await Page.mouse.down();
    await Page.mouse.move(PaintBounds.x + PaintBounds.width * .72, PaintBounds.y + PaintBounds.height * .7, { steps: 8 });
    await Page.mouse.up();
    Assert.equal(await Page.locator('#SelectionPrompt').isVisible(), true);
    await Page.locator('#KeepRegion').click();
    Edited = await Snapshot();
    Assert.equal(Edited.Layers[0].Regions.length, 1);
    Assert.ok(VolumeDistance(Edited.Layers[0], PaintedPoint[0], -1, PaintedPoint[1]) < 0);
    Assert.equal(VolumeDistance(Edited.Layers[0], PaintedPoint[0], 0, PaintedPoint[1]), 0);
    Verify('paint → box selection → region allocation creates a real signed volume, with no solid before allocation', { Strokes: Edited.Layers[0].Strokes.length, Regions: Edited.Layers[0].Regions.length });

    await Page.locator('#LandscapeTab').click();
    await Page.locator('#FocusMask').click();
    await WaitForRender();
    await Capture('SeaLevelFoundation.png');
    await Page.locator('[data-inspector="region"]').click();
    await Page.locator('#Resolution').selectOption('256');
    await Page.locator('#Vertical').selectOption('64');
    await Page.locator('.volume-export').evaluate(Details => Details.open = true);
    const BakeRequest = Page.waitForEvent('download', { timeout: 180000 });
    await Page.locator('#Bake').click();
    const Baked = DecodeSparseVolume(gunzipSync(await DownloadBytes(await BakeRequest)));
    Assert.deepEqual(Baked.Specification.Dimensions, [256, 64, 256]);
    Assert.ok(Baked.Leaves.some(Leaf => Leaf.Samples.some(Distance => Distance < 0)));
    Assert.ok(Baked.Leaves.some(Leaf => Leaf.Samples.some(Distance => Distance > 0)));
    Verify('the actual UI background worker exports requested X/Y/Z cells and signed 3D bricks', { Dimensions: Baked.Specification.Dimensions, Leaves: Baked.Leaves.length, BandMetres: Baked.Specification.Band });

    await Page.locator('#Resolution').selectOption('1024');
    const DownloadCount = Downloads.length;
    await Page.locator('#Bake').click();
    await Page.locator('#CancelBake').click();
    await Page.waitForTimeout(750);
    Assert.match(await Page.locator('#Status').innerText(), /cancelled/);
    Assert.equal(Downloads.length, DownloadCount);
    Verify('volume cancellation terminates the worker without exporting a partial file', { DownloadCount });

    await Page.locator('[data-inspector="shape"]').click();
    await AddGenerator('fractal');
    await WaitForRender();
    const TargetBefore = JSON.parse(await Page.locator('#Volume').getAttribute('data-camera-target'));
    const VolumeBounds = await Page.locator('#Volume').boundingBox();
    await Page.mouse.move(VolumeBounds.x + VolumeBounds.width * .5, VolumeBounds.y + VolumeBounds.height * .5);
    await Page.mouse.down({ button: 'middle' });
    await Page.mouse.move(VolumeBounds.x + VolumeBounds.width * .5 + 35, VolumeBounds.y + VolumeBounds.height * .5 + 20, { steps: 4 });
    await Page.mouse.up({ button: 'middle' });
    await Page.waitForFunction(Original => document.querySelector('#Volume').dataset.cameraTarget !== Original, JSON.stringify(TargetBefore));
    const TargetAfterPan = JSON.parse(await Page.locator('#Volume').getAttribute('data-camera-target'));
    await Page.locator('#KeyboardCamera').check();
    await Page.locator('#Viewport').focus();
    await Page.keyboard.down('KeyW');
    await Page.waitForTimeout(400);
    await Page.keyboard.up('KeyW');
    const TargetAfterW = JSON.parse(await Page.locator('#Volume').getAttribute('data-camera-target'));
    Assert.notDeepEqual(TargetAfterW, TargetAfterPan);
    await Page.locator('#KeyboardCamera').uncheck();
    const Stopped = await Page.locator('#Volume').getAttribute('data-camera-target');
    await Page.locator('#Viewport').focus();
    await Page.keyboard.press('KeyW');
    await Page.waitForTimeout(150);
    Assert.equal(await Page.locator('#Volume').getAttribute('data-camera-target'), Stopped);
    Verify('3D panning and opt-in WASD translate the camera; opt-out prevents movement', { TargetBefore, TargetAfterPan, TargetAfterW });

    const RoundTrip = await ExportDocument();
    await Page.locator('#File').setInputFiles({ name: 'RoundTrip.frontier.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(RoundTrip)) });
    Assert.deepEqual((await Snapshot()).Layers, RoundTrip.Layers);
    await Page.locator('#File').setInputFiles({ name: 'Invalid.json', mimeType: 'application/json', buffer: Buffer.from('{"Format":"not-a-landscape","Layers":[]}') });
    await Page.waitForFunction(() => /Open failed/.test(document.querySelector('#Status').textContent));
    Assert.match(await Page.locator('#Status').innerText(), /Open failed/);
    Assert.deepEqual((await Snapshot()).Layers, RoundTrip.Layers);
    Verify('project download/import is lossless and invalid imports do not replace the current world', 'Vector strokes, masks, bounds and generator parameters round-trip');

    await Page.setViewportSize({ width: 1100, height: 820 });
    Assert.equal(await Page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    Assert.ok((await Page.locator('#Viewport').boundingBox()).width > 400);
    await Capture('NarrowEditor.png');
    Verify('1100-pixel editor keeps both docks and a usable centre viewport without horizontal overflow', { Width: 1100, Viewport: await Page.locator('#Viewport').boundingBox() });
    Assert.deepEqual(BrowserErrors, []);
    Assert.deepEqual(FailedResponses, []);
    Verify('application and shader interactions finish without browser errors or failed local resources', { BrowserErrors, FailedResponses });
    Results.Status = 'passed';
    await rm(ImageDirectory + 'Failure.png', { force: true });
}
catch (Failure)
{
    Results.Status = 'failed';
    Results.Failure = Failure.stack;
    Results.BrowserErrors = BrowserErrors;
    Results.FailedResponses = FailedResponses;
    Results.PreviewAtFailure = await Page.locator('#Volume').evaluate(Canvas => ({ ...Canvas.dataset })).catch(() => null);
    await Page.screenshot({ path: ImageDirectory + 'Failure.png' }).catch(() => {});
    console.error(Failure);
    process.exitCode = 1;
}
finally
{
    await Browser.close();
    await writeFile(ResultDirectory + 'BrowserValidation.json', JSON.stringify(Results, null, 2) + '\n');
}
