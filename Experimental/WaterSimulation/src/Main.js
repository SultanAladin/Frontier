/*====================================================================================================================================
                                                            MAIN.JS
====================================================================================================================================*/
// 🧩 Boot sequence, frame loop, keyboard commands, capture, and runtime validation reporting

import { GraphicsDevice } from './GraphicsDevice.js';
import { LoadShaderLibrary } from './ShaderLibrary.js';
import { WaterScenario } from './WaterScenario.js';
import { ControlPanelAssembly } from './ControlPanelAssembly.js';
import { DefaultSettings, QualityProfiles } from './ScenarioSettings.js';

const ViewSurface = document.getElementById('ViewSurface');
const BootPlate = document.getElementById('BootPlate');
const BootLine = document.getElementById('BootLine');
const NoticePlate = document.getElementById('NoticePlate');
const ModelReadout = document.getElementById('ModelReadout');
const FrameReadout = document.getElementById('FrameReadout');
const SolveReadout = document.getElementById('SolveReadout');
const GridReadout = document.getElementById('GridReadout');
const ParticleReadout = document.getElementById('ParticleReadout');
const SeaReadout = document.getElementById('SeaReadout');
const ValidationPlate = document.getElementById('ValidationPlate');
const ValidationBody = document.getElementById('ValidationBody');
const PanelGrip = document.getElementById('PanelGrip');
const ControlPanel = document.getElementById('ControlPanel');
const PanelScroll = document.getElementById('PanelScroll');

const Settings = { ...DefaultSettings };
let Scenario = null;
let Panel = null;
let FrameTimeAverage = 16.7;
let ValidationMessages = [];

function ShowNotice(Title, Detail)
{
    NoticePlate.hidden = false;
    NoticePlate.textContent = '';
    const Heading = document.createElement('strong');
    Heading.textContent = Title;
    NoticePlate.append(Heading, document.createTextNode(Detail));
}

function ResizeCanvas()
{
    // 📝 Render scale trades canvas resolution for frame time on weaker GPUs
    const Ratio = Math.min(window.devicePixelRatio || 1, 1.5) * (Settings.RenderScale ?? 1.0);
    const Width = Math.max(2, Math.floor(window.innerWidth * Ratio));
    const Height = Math.max(2, Math.floor(window.innerHeight * Ratio));

    if (ViewSurface.width !== Width || ViewSurface.height !== Height)
    {
        ViewSurface.width = Width;
        ViewSurface.height = Height;
    }
}

function Report(Label, Detail)
{
    ValidationMessages.push({ Label: Label, Detail: Detail });
    RenderValidation();
}

function RenderValidation()
{
    ValidationBody.textContent = '';

    for (const Entry of ValidationMessages)
    {
        const Row = document.createElement('div');
        Row.className = 'ValidationLine';
        const Label = document.createElement('span');
        Label.textContent = Entry.Label;
        const Detail = document.createElement('b');
        Detail.textContent = Entry.Detail;
        Row.append(Label, Detail);
        ValidationBody.append(Row);
    }
}

function SelectModel(Index)
{
    Settings.SimulationCategory = Math.min(2, Math.max(0, Index | 0));
    Scenario.SpectralSignature = '';
    Scenario.GerstnerSignature = '';
    Panel.Synchronize();
    UpdateModelReadout();
}

function UpdateModelReadout()
{
    const Names = ['Spectral FFT ocean', 'Gerstner crest sum', 'Global grid wave solve'];
    ModelReadout.textContent = Names[Settings.SimulationCategory] ?? Names[0];
}

async function Boot()
{
    ResizeCanvas();

    const Device = new GraphicsDevice(ViewSurface);
    BootLine.textContent = 'Assembling shader library…';

    const Programs = await LoadShaderLibrary(Device, (FileName) =>
    {
        BootLine.textContent = `Preparing ${FileName}`;
    });

    BootLine.textContent = 'Baking shoreline atlas…';
    Scenario = new WaterScenario(ViewSurface, Device, Programs, Settings, Report);

    Panel = new ControlPanelAssembly(PanelScroll, Settings, (Key, Value) =>
    {
        Settings[Key] = Value;

        if (Key === 'QualityCategory' || Key === 'SimulationCategory')
        {
            Scenario.SpectralSignature = '';
            Scenario.GerstnerSignature = '';
        }

        Panel.Synchronize();
        UpdateModelReadout();
    },
    [
        { Label: 'Capture frame (PNG)', Run: () => CaptureFrame() },
        { Label: 'Re-run runtime checks', Run: () => RunValidation() }
    ]);

    ControlPanelGlobals();

    UpdateModelReadout();
    Panel.Synchronize();

    Report('WebGL2', 'available');
    Report('Float render targets', Device.FloatRenderable ? 'EXT_color_buffer_float' : 'missing');
    Report('Float filtering', Device.FloatFilterable ? 'OES_texture_float_linear' : 'nearest fallback');
    Report('Programs compiled', String(Programs.size));

    BootPlate.classList.add('Hidden');
    ValidationPlate.hidden = false;

    RunValidation();
    Loop(Device);
}

function ControlPanelGlobals()
{
    PanelGrip.addEventListener('click', () => ControlPanel.classList.toggle('Collapsed'));
}

function CaptureFrame()
{
    ViewSurface.toBlob((Blob) =>
    {
        const Link = document.createElement('a');
        Link.download = `frontier-water-${Date.now()}.png`;
        Link.href = URL.createObjectURL(Blob);
        Link.click();
        setTimeout(() => URL.revokeObjectURL(Link.href), 4000);
    }, 'image/png');
}

function RunValidation()
{
    const Gl = Scenario.Device.Gl;
    const ErrorCode = Gl.getError();
    Report('GL error queue', ErrorCode === 0 ? 'clean' : `0x${ErrorCode.toString(16)}`);

    const Probe = Scenario.HeightAtlas.Target;
    Scenario.HeightAtlas.RefreshWindow(Scenario.Vessel.Position[0], Scenario.Vessel.Position[2]);
    const Region = Scenario.HeightAtlas.WindowArray;
    let FiniteCount = 0;
    let MaximumHeight = 0.0;

    for (let Index = 0; Index < Region.length; Index += 4)
    {
        if (Number.isFinite(Region[Index]))
        {
            FiniteCount++;
            MaximumHeight = Math.max(MaximumHeight, Math.abs(Region[Index]));
        }
    }

    const SampleCount = Region.length / 4;
    Report('Atlas samples finite', `${FiniteCount} / ${SampleCount}`);
    Report('Atlas peak height', `${MaximumHeight.toFixed(2)} m`);
    Report('Atlas texture', `${Probe.Width}² rgba32f`);
    Report('Surface grid vertices', String(Scenario.OceanMesh.IndexCount / 3) + ' triangles');
    Report('Spray capacity', String(Scenario.Spray.ActiveCount));
}

function Loop(Device)
{
    let PreviousTime = performance.now();

    const Step = (Now) =>
    {
        const Elapsed = Math.min(0.05, Math.max(0.0001, (Now - PreviousTime) / 1000.0));
        PreviousTime = Now;
        FrameTimeAverage = FrameTimeAverage * 0.9 + Elapsed * 1000.0 * 0.1;

        try
        {
            ResizeCanvas();
            Scenario.Resize();
            Scenario.Frame(Elapsed);
        }
        catch (Failure)
        {
            ShowNotice('Frame failure', `${Failure.message}\n\n${Failure.stack ?? ''}`);
            return;
        }

        UpdateTelemetry();
        requestAnimationFrame(Step);
    };

    requestAnimationFrame(Step);
    window.addEventListener('resize', () => ResizeCanvas());
    BindKeyboard();
}

function UpdateTelemetry()
{
    FrameReadout.textContent = `${(1000.0 / FrameTimeAverage).toFixed(0)} fps · ${FrameTimeAverage.toFixed(1)} ms`;
    const Quality = QualityProfiles[Settings.QualityCategory] ?? QualityProfiles[2];
    const ModelNames = ['FFT 3-cascade', 'Gerstner ×16', 'Grid 512²'];
    SolveReadout.textContent = `${ModelNames[Settings.SimulationCategory]} · ${Quality.Label}`;
    GridReadout.textContent = 'radial 190 × 384';
    ParticleReadout.textContent = SeriesCount(Scenario.Spray.ActiveCount);
    SeaReadout.textContent = `${Settings.WaveHeight.toFixed(2)} m · ${Settings.WindSpeed.toFixed(1)} m/s`;
}

function SeriesCount(Value)
{
    if (Value >= 1000)
    {
        return `${(Value / 1000).toFixed(1)} k`;
    }

    return String(Value);
}

function BindKeyboard()
{
    window.addEventListener('keydown', (Event) =>
    {
        if (Event.target instanceof HTMLInputElement)
        {
            return;
        }

        switch (Event.key)
        {
            case '1':
            case '2':
            case '3':
                SelectModel(Number.parseInt(Event.key, 10) - 1);
                break;
            case 'g':
            case 'G':
                Settings.GridOverlay = !Settings.GridOverlay;
                Panel.Synchronize();
                break;
            case 'f':
            case 'F':
                Settings.FoamEnabled = !Settings.FoamEnabled;
                Panel.Synchronize();
                break;
            case 'p':
            case 'P':
                Settings.SprayEnabled = !Settings.SprayEnabled;
                Panel.Synchronize();
                break;
            case 'b':
            case 'B':
                Settings.VesselEnabled = !Settings.VesselEnabled;
                Panel.Synchronize();
                break;
            case 'h':
            case 'H':
                ControlPanel.classList.toggle('Collapsed');
                break;
            case 'v':
            case 'V':
                ValidationPlate.hidden = !ValidationPlate.hidden;
                break;
            case 'c':
            case 'C':
                CaptureFrame();
                break;
            case ' ':
                Settings.SimulationPaused = !Settings.SimulationPaused;
                Panel.Synchronize();
                Event.preventDefault();
                break;
            default:
                break;
        }
    });
}

ViewSurface.addEventListener('webglcontextlost', () =>
{
    ShowNotice('WebGL context lost', 'Reload the page to recreate the simulation.');
});


Boot().catch((Failure) =>
{
    ShowNotice('Water simulation could not start', `${Failure.message}\n\n${Failure.stack ?? ''}`);
    BootLine.textContent = 'Startup failed — see the message above.';
});
