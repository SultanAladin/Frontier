/*============================================================================================================================================
                                                      EDITORWORKSPACE.JS
============================================================================================================================================*/
// 🧩 World-forge editor wiring: SDF authoring, heightmap sculpting, adaptive ocean tiers, camera navigation and diagnostics

import { ComputeTerrainReport, ConstructOverlayLines, ConstructTerrainClipmap, ResolveTerrainWindows } from './ClipmapSurface.js';
import { ConstructExtractionSpecification, ConvertSdfToPolygonSurface, EncodePolygonSurfaceAsWavefront, ComputeEulerCharacteristic } from './SdfSurfaceExtraction.js';
import { ConstructCrustConfiguration, ConstructPreset, ConstructPrimitive, CrustSignedDistanceField, PrimitiveCategory } from './SdfCrustField.js';
import { ConstructWaterConfiguration, ComputeAdaptivityReport, ConstructSpectralWaveSet, ResolveWindows } from './WaterClipmapLayout.js';
import { ConstructCameraConfiguration, UnrealCameraRig } from './UnrealCameraRig.js';
import { CrustHeightfield, SculptTool } from './CrustHeightfield.js';
import { GpuShallowWater } from './GpuShallowWater.js';
import { CreateWebGL2Context } from './GlResources.js';
import { ViewportRenderer } from './ViewportRenderer.js';
import { DecodeProject, EncodeHeightfieldPng, EncodeProject } from './ProjectCodec.js';

const $ = Identifier => document.getElementById(Identifier);
const $$ = Selector => Array.from(document.querySelectorAll(Selector));

const CategoryColours = {
    Continent: '#8fb45a', Gulf: '#4a9ccb', IslandArc: '#d8b25a',
    Ridge: '#c07a52', Crater: '#9a76c7', CityPad: '#e39a3b'
};

const Workspace = {
    CrustConfiguration: ConstructCrustConfiguration(),
    WaterConfiguration: ConstructWaterConfiguration(),
    Spectral: ConstructSpectralWaveSet(),
    TerrainClipmap: ConstructTerrainClipmap(),
    Camera: new UnrealCameraRig(ConstructCameraConfiguration()),
    Field: null,
    Heightfield: null,
    Renderer: null,
    Water: null,
    Capabilities: null,
    Mode: 'Flat',
    ViewMode: 'beauty',
    Tool: SculptTool.Raise,
    BrushRadius: 6000,
    BrushStrength: 260,
    BrushTarget: 65,
    SelectedPrimitive: null,
    LastBrush: null,
    Surface: null,
    Busy: false,
    SolverRunning: true,
    RainEnabled: false,
    ShowGrid: false,
    ShowBounds: false,
    ShowSurface: false,
    ShowWireframe: false,
    SunAngle: 34,
    Haze: 1,
    Planet: { Yaw: 0.55, Pitch: 0.48, Distance: 1.9e7 },
    FrameIndex: 0,
    Fps: 60,
    LastFrameTime: 0,
    LastHudTime: 0,
    LastStatsTime: 0,
    LastMinimapTime: 0,
    Pointer: { Look: false, Pan: false, Sculpt: false, Orbit: false, LastX: 0, LastY: 0, LastStrokeTime: 0 },
    RebuildToken: 0
};

function FormatCount(Amount)
{
    if (Amount >= 1e12) return `${(Amount / 1e12).toFixed(2)} T`;
    if (Amount >= 1e9) return `${(Amount / 1e9).toFixed(2)} B`;
    if (Amount >= 1e6) return `${(Amount / 1e6).toFixed(2)} M`;
    if (Amount >= 1e3) return `${(Amount / 1e3).toFixed(1)} K`;
    return Amount.toFixed(0);
}

function FormatDistance(Metres)
{
    if (Math.abs(Metres) >= 1000) return `${(Metres / 1000).toFixed(1)} km`;
    return `${Metres.toFixed(1)} m`;
}

function NormalizeVector(Vector)
{
    const Length = Math.hypot(Vector[0], Vector[1], Vector[2]) || 1;
    return [Vector[0] / Length, Vector[1] / Length, Vector[2] / Length];
}

function TriggerDownload(Filename, Bytes, MimeType)
{
    const Blob = new Blob([Bytes], { type: MimeType });
    const Anchor = document.createElement('a');
    Anchor.href = URL.createObjectURL(Blob);
    Anchor.download = Filename;
    document.body.appendChild(Anchor);
    Anchor.click();
    Anchor.remove();
    setTimeout(() => URL.revokeObjectURL(Anchor.href), 4000);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      INITIALIZATION
//------------------------------------------------------------------------------------------------------------------------

async function InitializeWorkspace()
{
    try
    {
        Workspace.Capabilities = CreateWebGL2Context($('gl-canvas'));
        Workspace.Field = new CrustSignedDistanceField(Workspace.CrustConfiguration);
        for (const Primitive of ConstructPreset('AfricaBlockout', Workspace.CrustConfiguration.Extent))
            Workspace.Field.AppendPrimitive(Primitive);
        Workspace.Heightfield = new CrustHeightfield(Workspace.Field, 1024);
        await RebuildCrustField('Compiling continental SDF…');
        Workspace.Renderer = new ViewportRenderer($('gl-canvas'), Workspace.Capabilities, Workspace.Heightfield,
            Workspace.WaterConfiguration, Workspace.TerrainClipmap);
        Workspace.Water = new GpuShallowWater(Workspace.Capabilities.Context, Workspace.WaterConfiguration, Workspace.Capabilities);
        Workspace.Camera.ApplyViewPreset('Continent', Workspace.CrustConfiguration.Extent);
        BindInterface();
        RefreshLayerList();
        RefreshStats();
        DrawMinimap();
        $('status-left').textContent = 'Ready · Africa blockout · 7 water tiers · SDF crust 1024²';
        requestAnimationFrame(FrameLoop);
    }
    catch (Failure)
    {
        const Fatal = $('fatal');
        Fatal.textContent = `World Forge could not start.\n\n${Failure.message}\n\nRequires WebGL2 with float colour attachments (EXT_color_buffer_float).`;
        Fatal.classList.remove('hidden');
        console.error(Failure);
    }
}

async function RebuildCrustField(Label = 'Recompiling crust SDF…')
{
    const Token = ++Workspace.RebuildToken;
    Workspace.Busy = true;
    $('loading-label').textContent = Label;
    $('progress-fill').style.width = '0%';
    $('loading').classList.remove('hidden');
    await Workspace.Heightfield.RegenerateBaseAsync(Progress =>
    {
        if (Token === Workspace.RebuildToken) $('progress-fill').style.width = `${Math.round(Progress * 100)}%`;
    });
    if (Token !== Workspace.RebuildToken) return;
    if (Workspace.Renderer) Workspace.Renderer.RefreshElevation();
    if (Workspace.Water) Workspace.Water.FinalizeFields();
    $('loading').classList.add('hidden');
    Workspace.Busy = false;
    RefreshStats();
    DrawMinimap();
}

function ReconfigureWater()
{
    if (!Workspace.Capabilities) return;
    if (Workspace.Water) Workspace.Water.Reclaim();
    Workspace.Water = new GpuShallowWater(Workspace.Capabilities.Context, Workspace.WaterConfiguration, Workspace.Capabilities);
    RefreshStats();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PRESETS AND LAYERS
//------------------------------------------------------------------------------------------------------------------------

async function ApplyPreset(Identifier)
{
    const Extent = Workspace.CrustConfiguration.Extent;
    Workspace.Field.Primitives = ConstructPreset(Identifier, Extent);
    Workspace.Heightfield.StrokeCommands.length = 0;
    Workspace.Heightfield.UndoCommands.length = 0;
    Workspace.Heightfield.SculptDeltas.fill(0);
    Workspace.SelectedPrimitive = null;
    await RebuildCrustField(`Assembling ${Identifier}…`);
    Workspace.Camera.ApplyViewPreset('Continent', Extent);
    RefreshLayerList();
}

function ResolveTargetPosition()
{
    if (Workspace.LastBrush) return { X: Workspace.LastBrush.X, Z: Workspace.LastBrush.Z };
    const Canvas = $('gl-canvas');
    const Rect = Canvas.getBoundingClientRect();
    const Direction = Workspace.Camera.ResolveRay(0, 0, Rect.width / Rect.height);
    const Hit = Workspace.Heightfield.ResolveSurfaceIntersection(Workspace.Camera.Position, Direction);
    if (Hit.Hit) return { X: Hit.X, Z: Hit.Z };
    return { X: Workspace.Camera.Position[0], Z: Workspace.Camera.Position[2] - 12000 };
}

async function AppendLayer(Category)
{
    const Target = ResolveTargetPosition();
    const Radius = Math.max(1200, Workspace.BrushRadius);
    const Primitive = ConstructPrimitive(Category, { Label: `${Category} ${Workspace.Field.Primitives.length + 1}`, CenterX: Target.X, CenterZ: Target.Z });
    if (Category === PrimitiveCategory.Continent)
    {
        Primitive.Outline = [];
        for (let OutlineNodeIndex = 0; OutlineNodeIndex < 7; ++OutlineNodeIndex)
        {
            const Angle = OutlineNodeIndex / 7 * Math.PI * 2;
            Primitive.Outline.push([
                Math.cos(Angle) * Radius * 0.85,
                Math.sin(Angle) * Radius * 0.62,
                Radius * (0.24 + 0.09 * Math.sin(OutlineNodeIndex * 2.1))
            ]);
        }
        Primitive.WarpAmplitude = Radius * 0.22;
        Primitive.WarpWavelength = Radius * 2.6;
        Primitive.Blend = Radius * 0.3;
    }
    else if (Category === PrimitiveCategory.Gulf)
    {
        Primitive.RadiiX = Radius;
        Primitive.RadiiZ = Radius * 0.65;
        Primitive.Blend = Radius * 0.22;
        Primitive.WarpAmplitude = Radius * 0.14;
    }
    else if (Category === PrimitiveCategory.IslandArc)
    {
        Primitive.RadiiX = Radius * 0.3;
        Primitive.RadiiZ = Radius * 0.62;
        Primitive.Blend = Radius * 0.12;
    }
    else if (Category === PrimitiveCategory.Ridge)
    {
        Primitive.Outline = [[-Radius, -Radius * 0.3, 0], [0, Radius * 0.2, 0], [Radius, -Radius * 0.1, 0]];
        Primitive.Width = Radius * 0.35;
        Primitive.Elevation = 1400;
    }
    else if (Category === PrimitiveCategory.Crater)
    {
        Primitive.RadiiX = Radius * 0.5;
        Primitive.Width = Radius * 0.22;
        Primitive.Elevation = 500;
    }
    else if (Category === PrimitiveCategory.CityPad)
    {
        Primitive.RadiiX = Radius * 0.55;
        Primitive.RadiiZ = Radius * 0.45;
        Primitive.Elevation = Workspace.BrushTarget;
        Primitive.Label = 'City vase / pad';
    }
    Workspace.Field.AppendPrimitive(Primitive);
    Workspace.SelectedPrimitive = Primitive.Identifier;
    Workspace.LastBrush = Target;
    RefreshLayerList();
    await RebuildCrustField(`Integrating ${Category}…`);
}

function RefreshLayerList()
{
    const List = $('layer-list');
    $('layer-count').textContent = String(Workspace.Field.Primitives.length);
    List.innerHTML = '';
    for (const Primitive of Workspace.Field.Primitives)
    {
        const Entry = document.createElement('div');
        Entry.className = `layer-entry${Workspace.SelectedPrimitive === Primitive.Identifier ? ' active' : ''}${Primitive.Enabled ? '' : ' disabled'}`;
        Entry.style.opacity = Primitive.Enabled ? '1' : '0.45';
        Entry.dataset.identifier = String(Primitive.Identifier);
        const Enabled = document.createElement('input');
        Enabled.type = 'checkbox';
        Enabled.checked = Primitive.Enabled;
        Enabled.title = 'Enable / suspend layer';
        Enabled.addEventListener('change', async Event =>
        {
            Event.stopPropagation();
            Primitive.Enabled = Enabled.checked;
            RefreshLayerList();
            await RebuildCrustField('Recompiling crust SDF…');
        });
        const Swatch = document.createElement('span');
        Swatch.className = 'swatch';
        Swatch.style.background = CategoryColours[Primitive.Category] || '#888';
        const Name = document.createElement('span');
        Name.className = 'name';
        Name.textContent = Primitive.Label;
        const Category = document.createElement('span');
        Category.className = 'category';
        Category.textContent = Primitive.Category;
        const Remove = document.createElement('button');
        Remove.className = 'remove';
        Remove.textContent = '✕';
        Remove.title = 'Remove layer';
        Remove.addEventListener('click', async Event =>
        {
            Event.stopPropagation();
            Workspace.Field.RemovePrimitive(Primitive.Identifier);
            if (Workspace.SelectedPrimitive === Primitive.Identifier) Workspace.SelectedPrimitive = null;
            RefreshLayerList();
            await RebuildCrustField('Removing SDF layer…');
        });
        Entry.append(Enabled, Swatch, Name, Category, Remove);
        Entry.addEventListener('click', () =>
        {
            Workspace.SelectedPrimitive = Primitive.Identifier;
            RefreshLayerList();
        });
        List.appendChild(Entry);
    }
    RefreshLayerEditor();
}

let DebounceIdentifier = null;

function RefreshLayerEditor()
{
    const Editor = $('layer-editor');
    const Primitive = Workspace.Field.ResolvePrimitive(Workspace.SelectedPrimitive);
    if (!Primitive) { Editor.classList.add('hidden'); Editor.innerHTML = ''; return; }
    Editor.classList.remove('hidden');
    const Fields = [
        ['Label', 'text', Primitive.Label, 1],
        ['CenterX', 'number', (Primitive.CenterX / 1000).toFixed(2), 1000, 'km'],
        ['CenterZ', 'number', (Primitive.CenterZ / 1000).toFixed(2), 1000, 'km'],
        ['Rotation', 'number', Primitive.Rotation.toFixed(2), 1, 'rad'],
        ['RadiiX', 'number', (Primitive.RadiiX / 1000).toFixed(2), 1000, 'km'],
        ['RadiiZ', 'number', (Primitive.RadiiZ / 1000).toFixed(2), 1000, 'km'],
        ['Width', 'number', (Primitive.Width / 1000).toFixed(2), 1000, 'km'],
        ['Elevation', 'number', Primitive.Elevation.toFixed(0), 1, 'm'],
        ['Blend', 'number', (Primitive.Blend / 1000).toFixed(2), 1000, 'km'],
        ['WarpAmplitude', 'number', (Primitive.WarpAmplitude / 1000).toFixed(2), 1000, 'km'],
        ['Seed', 'number', Primitive.Seed, 1, '-']
    ];
    Editor.innerHTML = `<div class="section-label">Selected · ${Primitive.Category}</div>`;
    for (const [Property, Type, ScalarReading, Scale, Unit] of Fields)
    {
        const Row = document.createElement('div');
        Row.className = 'field-row';
        const Label = document.createElement('label');
        Label.innerHTML = `${Property} ${Unit ? `<span class="unit">${Unit}</span>` : ''}`;
        const Input = document.createElement('input');
        Input.type = Type;
        Input.value = ScalarReading;
        Input.addEventListener('input', () =>
        {
            const Parsed = Type === 'number' ? parseFloat(Input.value) : Input.value;
            if (Type === 'number' && !Number.isFinite(Parsed)) return;
            Primitive[Property] = Type === 'number' ? Parsed * Scale : Parsed;
            clearTimeout(DebounceIdentifier);
            DebounceIdentifier = setTimeout(() => RebuildCrustField('Re-evaluating SDF layer…'), 320);
        });
        Row.append(Label, Input);
        Editor.appendChild(Row);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOOL APPLICATION
//------------------------------------------------------------------------------------------------------------------------

function ResolvePointerRay(Event)
{
    const Canvas = $('gl-canvas');
    const Rect = Canvas.getBoundingClientRect();
    const NdcX = (Event.clientX - Rect.left) / Rect.width * 2 - 1;
    const NdcY = 1 - (Event.clientY - Rect.top) / Rect.height * 2;
    return { Direction: Workspace.Camera.ResolveRay(NdcX, NdcY, Rect.width / Rect.height), Aspect: Rect.width / Rect.height };
}

function ResolveSeaSurfacePoint(Direction)
{
    const Camera = Workspace.Camera;
    if (Math.abs(Direction[1]) < 1e-5) return { X: Camera.Position[0], Z: Camera.Position[2] };
    const Parameter = (Workspace.CrustConfiguration.SeaLevel - Camera.Position[1]) / Direction[1];
    if (Parameter <= 0) return { X: Camera.Position[0], Z: Camera.Position[2] };
    return { X: Camera.Position[0] + Direction[0] * Parameter, Z: Camera.Position[2] + Direction[2] * Parameter };
}

function ApplyToolAtPointer(Event)
{
    if (!Workspace.Heightfield || Workspace.Busy) return;
    const { Direction } = ResolvePointerRay(Event);
    if (Workspace.Tool === 'Drop' || Workspace.Tool === 'Tsunami')
    {
        const Point = ResolveSeaSurfacePoint(Direction);
        const Tsunami = Workspace.Tool === 'Tsunami';
        Workspace.Water.InjectDisturbance(Point.X, Point.Z, Tsunami ? 14 : 2.4, Tsunami ? 4200 : 180);
        Workspace.LastBrush = Point;
        return;
    }
    const Hit = Workspace.Heightfield.ResolveSurfaceIntersection(Workspace.Camera.Position, Direction);
    if (!Hit.Hit) return;
    Workspace.LastBrush = { X: Hit.X, Z: Hit.Z };
    const Stroke = Workspace.Heightfield.ConstructStroke(Workspace.Tool, Hit.X, Hit.Z,
        Workspace.BrushRadius, Workspace.BrushStrength, Workspace.BrushTarget);
    const Region = Workspace.Heightfield.ApplyStroke(Stroke);
    Workspace.Renderer.RefreshElevation(Region);
}

function SelectTool(Tool)
{
    Workspace.Tool = Tool;
    $$('#tool-grid .tool').forEach(Button => Button.classList.toggle('selected', Button.dataset.tool === Tool));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         EXPORT
//------------------------------------------------------------------------------------------------------------------------

function ExportDocument(Kind)
{
    if (Kind === 'project')
    {
        TriggerDownload('WorldForge.project.json', EncodeProject({
            CrustConfiguration: Workspace.CrustConfiguration,
            Field: Workspace.Field,
            Heightfield: Workspace.Heightfield,
            WaterConfiguration: Workspace.WaterConfiguration,
            SpectralConfiguration: Workspace.Spectral,
            Camera: Workspace.Camera,
            View: { Mode: Workspace.Mode, ViewMode: Workspace.ViewMode, SunAngle: Workspace.SunAngle }
        }), 'application/json');
    }
    else if (Kind === 'heightmap')
    {
        const Statistics = Workspace.Heightfield.ComputeStatistics();
        TriggerDownload('WorldForge.heightfield.png', EncodeHeightfieldPng(Workspace.Heightfield,
            Math.min(-4200, Statistics.Minimum - 100), Math.max(4200, Statistics.Maximum + 100)), 'image/png');
    }
    else if (Kind === 'obj')
    {
        if (!Workspace.Surface) { window.alert('Convert the SDF to a polygon surface first.'); return; }
        TriggerDownload('WorldForge.crust.obj', EncodePolygonSurfaceAsWavefront(Workspace.Surface), 'text/plain');
    }
    else if (Kind === 'report')
    {
        const Windows = ResolveWindows(Workspace.WaterConfiguration, Workspace.Camera.Position[0], Workspace.Camera.Position[2]);
        const Adaptivity = ComputeAdaptivityReport(Workspace.WaterConfiguration, Workspace.Heightfield.Extent, Windows);
        const TerrainWindows = ResolveTerrainWindows(Workspace.TerrainClipmap, Workspace.Camera.Position[0], Workspace.Camera.Position[2]);
        TriggerDownload('WorldForge.diagnostics.json', JSON.stringify({
            Crust: Workspace.Heightfield.ComputeStatistics(),
            WaterAdaptivity: Adaptivity,
            Terrain: ComputeTerrainReport(Workspace.TerrainClipmap, TerrainWindows, Workspace.Heightfield.Extent),
            Probe: Workspace.Water.Probe,
            PolygonSurface: Workspace.Surface ? {
                VertexCount: Workspace.Surface.VertexCount,
                TriangleCount: Workspace.Surface.TriangleCount,
                EulerCharacteristic: ComputeEulerCharacteristic(Workspace.Surface)
            } : null,
            Frame: { Fps: Workspace.Fps, DrawCalls: Workspace.Renderer.LastDrawCalls, Triangles: Workspace.Renderer.LastTriangleCount }
        }, null, 2), 'application/json');
    }
    $('export-menu').classList.add('hidden');
}

async function ImportDocument(Text)
{
    const Document = DecodeProject(Text);
    Object.assign(Workspace.CrustConfiguration, Document.Crust);
    Workspace.TerrainClipmap.InnerExtent = Workspace.CrustConfiguration.Extent / 32;
    Workspace.Field.Primitives = Document.Primitives;
    Workspace.Heightfield.StrokeCommands = Document.Strokes;
    if (Document.Water)
    {
        Object.assign(Workspace.WaterConfiguration, Document.Water);
        $('water-rings').value = String(Workspace.WaterConfiguration.RingCount);
        $('water-resolution').value = String(Workspace.WaterConfiguration.GridResolution);
        ReconfigureWater();
    }
    if (Document.Spectral) Object.assign(Workspace.Spectral, Document.Spectral);
    if (Document.Camera)
    {
        Workspace.Camera.Position = Document.Camera.Position;
        Workspace.Camera.Yaw = Document.Camera.Yaw;
        Workspace.Camera.Pitch = Document.Camera.Pitch;
        Workspace.Camera.FlySpeed = Document.Camera.FlySpeed;
    }
    if (Document.View) { Workspace.ViewMode = Document.View.ViewMode || 'beauty'; Workspace.Mode = Document.View.Mode || 'Flat'; }
    Workspace.Heightfield.RecomputeDeltas();
    await RebuildCrustField('Loading project…');
    RefreshLayerList();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERFACE BINDING
//------------------------------------------------------------------------------------------------------------------------

function BindInterface()
{
    const Canvas = $('gl-canvas');

    $$('.mode-tab').forEach(Button => Button.addEventListener('click', () =>
    {
        Workspace.Mode = Button.dataset.mode;
        $$('.mode-tab').forEach(Candidate => Candidate.classList.toggle('selected', Candidate === Button));
        if (Workspace.Mode === 'Planet') Workspace.Planet.Distance = Math.max(Workspace.Planet.Distance, 1.35e7);
    }));
    $$('.view-chip').forEach(Button => Button.addEventListener('click', () =>
    {
        Workspace.ViewMode = Button.dataset.view;
        $$('.view-chip').forEach(Candidate => Candidate.classList.toggle('selected', Candidate === Button));
        $('hud-mode').textContent = Workspace.ViewMode.toUpperCase();
    }));
    $$('.preset').forEach(Button => Button.addEventListener('click', () => ApplyPreset(Button.dataset.preset)));
    $$('.layer-add').forEach(Button => Button.addEventListener('click', () => AppendLayer(Button.dataset.layer)));
    $$('#tool-grid .tool').forEach(Button => Button.addEventListener('click', () => SelectTool(Button.dataset.tool)));
    $$('.view-preset').forEach(Button => Button.addEventListener('click', () =>
    {
        if (Button.dataset.viewpreset === 'Focus')
        {
            const Target = ResolveTargetPosition();
            Workspace.Camera.FocusOn([Target.X, Workspace.Heightfield.SampleElevation(Target.X, Target.Z), Target.Z]);
        }
        else Workspace.Camera.ApplyViewPreset(Button.dataset.viewpreset, Workspace.CrustConfiguration.Extent);
    }));

    $('action-rebuild').addEventListener('click', () => RebuildCrustField());
    $('action-new').addEventListener('click', () => ApplyPreset('BlankVase'));
    $('action-undo').addEventListener('click', () =>
    {
        if (Workspace.Heightfield.UndoStroke()) { Workspace.Renderer.RefreshElevation(); RefreshStats(); }
    });
    $('action-redo').addEventListener('click', () =>
    {
        if (Workspace.Heightfield.RedoStroke()) { Workspace.Renderer.RefreshElevation(); RefreshStats(); }
    });
    $('action-convert').addEventListener('click', ConvertSurface);
    $('action-drop').addEventListener('click', () =>
    {
        const Point = ResolveSeaSurfacePoint(Workspace.Camera.ResolveRay(0, 0, Canvas.clientWidth / Canvas.clientHeight));
        Workspace.Water.InjectDisturbance(Point.X, Point.Z, 2.4, 180);
    });
    $('action-rain').addEventListener('click', Event =>
    {
        Workspace.RainEnabled = !Workspace.RainEnabled;
        Event.currentTarget.classList.toggle('on', Workspace.RainEnabled);
    });
    $('action-solver').addEventListener('click', Event =>
    {
        Workspace.SolverRunning = !Workspace.SolverRunning;
        Event.currentTarget.classList.toggle('on', Workspace.SolverRunning);
        Event.currentTarget.textContent = Workspace.SolverRunning ? 'Solver running' : 'Solver paused';
    });
    $('action-reset-water').addEventListener('click', () => Workspace.Water.FinalizeFields());

    $('action-export').addEventListener('click', () => $('export-menu').classList.toggle('hidden'));
    $$('#export-menu button').forEach(Button => Button.addEventListener('click', () => ExportDocument(Button.dataset.export)));
    $('action-open').addEventListener('click', () => $('file-input').click());
    $('file-input').addEventListener('change', async Event =>
    {
        const File = Event.target.files[0];
        if (File) await ImportDocument(await File.text());
        Event.target.value = '';
    });
    $('action-save').addEventListener('click', () => ExportDocument('project'));

    const BindRange = (Identifier, OutputIdentifier, Apply, Format = ScalarReading => ScalarReading) =>
    {
        const Input = $(Identifier);
        Input.addEventListener('input', () =>
        {
            const ScalarReading = parseFloat(Input.value);
            $(OutputIdentifier).textContent = Format(ScalarReading);
            Apply(ScalarReading);
        });
    };
    BindRange('brush-radius', 'brush-radius-out', ScalarReading => Workspace.BrushRadius = ScalarReading * 1000, ScalarReading => ScalarReading.toFixed(1));
    BindRange('brush-strength', 'brush-strength-out', ScalarReading => Workspace.BrushStrength = ScalarReading, ScalarReading => ScalarReading.toFixed(0));
    BindRange('water-wind', 'water-wind-out', ScalarReading => Workspace.WaterConfiguration.WindStrength = ScalarReading, ScalarReading => ScalarReading.toFixed(2));
    BindRange('water-swell', 'water-swell-out', ScalarReading =>
    {
        Workspace.WaterConfiguration.SwellAmplitude = ScalarReading;
        Workspace.Spectral.Amplitude = Math.max(0.4, ScalarReading * 1.4);
    }, ScalarReading => ScalarReading.toFixed(2));
    BindRange('water-damping', 'water-damping-out', ScalarReading => Workspace.WaterConfiguration.Damping = ScalarReading, ScalarReading => ScalarReading.toFixed(3));
    BindRange('water-rings', 'water-rings-out', ScalarReading =>
    {
        Workspace.WaterConfiguration.RingCount = Math.round(ScalarReading);
        ReconfigureWater();
    }, ScalarReading => ScalarReading.toFixed(0));
    BindRange('sun-angle', 'sun-angle-out', ScalarReading => Workspace.SunAngle = ScalarReading, ScalarReading => `${ScalarReading.toFixed(0)}°`);
    BindRange('haze', 'haze-out', ScalarReading => Workspace.Haze = ScalarReading, ScalarReading => ScalarReading.toFixed(1));

    $('water-resolution').addEventListener('change', Event =>
    {
        Workspace.WaterConfiguration.GridResolution = parseInt(Event.target.value, 10);
        ReconfigureWater();
    });
    $('brush-target').addEventListener('change', Event => Workspace.BrushTarget = parseFloat(Event.target.value));
    $('toggle-grid').addEventListener('change', Event => Workspace.ShowGrid = Event.target.checked);
    $('toggle-bounds').addEventListener('change', Event => Workspace.ShowBounds = Event.target.checked);
    $('toggle-surface').addEventListener('change', Event => Workspace.ShowSurface = Event.target.checked);
    $('toggle-wireframe').addEventListener('change', Event => Workspace.ShowWireframe = Event.target.checked);
    $('camera-speed').addEventListener('input', Event =>
    {
        const ScalarReading = parseFloat(Event.target.value);
        Workspace.Camera.FlySpeed = Math.round(0.26 * ScalarReading * ScalarReading + 4);
        $('camera-speed-out').textContent = String(Workspace.Camera.FlySpeed);
    });

    const WorldFields = [
        ['world-sea', 'SeaLevel', 1], ['world-flat', 'FlatElevation', 1],
        ['world-ocean', 'OceanFloor', 1], ['world-mountain', 'MountainAmplitude', 1],
        ['world-seed', 'Seed', 1]
    ];
    for (const [Identifier, Property, Scale] of WorldFields)
    {
        $(Identifier).addEventListener('change', Event =>
        {
            const ScalarReading = parseFloat(Event.target.value);
            if (!Number.isFinite(ScalarReading)) return;
            Workspace.CrustConfiguration[Property] = ScalarReading * Scale;
            if (Property === 'SeaLevel') Workspace.WaterConfiguration.SeaLevel = ScalarReading;
            RebuildCrustField();
        });
    }
    $('world-extent').addEventListener('change', async Event =>
    {
        const NewExtent = parseFloat(Event.target.value);
        const OldExtent = Workspace.CrustConfiguration.Extent;
        const Ratio = NewExtent / OldExtent;
        Workspace.CrustConfiguration.Extent = NewExtent;
        Workspace.TerrainClipmap.InnerExtent = NewExtent / 32;
        for (const Primitive of Workspace.Field.Primitives)
        {
            Primitive.CenterX *= Ratio; Primitive.CenterZ *= Ratio;
            Primitive.RadiiX *= Ratio; Primitive.RadiiZ *= Ratio;
            Primitive.Width *= Ratio; Primitive.Blend *= Ratio;
            Primitive.WarpAmplitude *= Ratio; Primitive.WarpWavelength *= Ratio;
            Primitive.Outline = Primitive.Outline.map(OutlineNode => [OutlineNode[0] * Ratio, OutlineNode[1] * Ratio, OutlineNode[2] * Ratio]);
        }
        for (const Stroke of Workspace.Heightfield.StrokeCommands)
        {
            Stroke.WorldX *= Ratio; Stroke.WorldZ *= Ratio; Stroke.Radius *= Ratio;
        }
        Workspace.Heightfield.RecomputeDeltas();
        await RebuildCrustField('Rescaling world domain…');
        RefreshLayerList();
    });

    // 📝 RMB look, MMB pan, LMB sculpt/inject; planet mode uses LMB orbit instead.
    Canvas.addEventListener('contextmenu', Event => Event.preventDefault());
    Canvas.addEventListener('pointerdown', Event =>
    {
        Canvas.setPointerCapture(Event.pointerId);
        Workspace.Pointer.LastX = Event.clientX;
        Workspace.Pointer.LastY = Event.clientY;
        if (Workspace.Mode === 'Planet')
        {
            if (Event.button === 0) Workspace.Pointer.Orbit = true;
            return;
        }
        if (Event.button === 2) { Workspace.Pointer.Look = true; Workspace.Camera.LookEnabled = true; }
        else if (Event.button === 1) { Workspace.Pointer.Pan = true; Workspace.Camera.PanEnabled = true; }
        else if (Event.button === 0) { Workspace.Pointer.Sculpt = true; ApplyToolAtPointer(Event); }
    });
    Canvas.addEventListener('pointermove', Event =>
    {
        const DeltaX = Event.clientX - Workspace.Pointer.LastX;
        const DeltaY = Event.clientY - Workspace.Pointer.LastY;
        Workspace.Pointer.LastX = Event.clientX;
        Workspace.Pointer.LastY = Event.clientY;
        if (Workspace.Pointer.Orbit)
        {
            Workspace.Planet.Yaw += DeltaX * 0.005;
            Workspace.Planet.Pitch = Math.max(-1.45, Math.min(1.45, Workspace.Planet.Pitch + DeltaY * 0.005));
        }
        if (Workspace.Pointer.Look) Workspace.Camera.RotateBy(DeltaX, DeltaY);
        if (Workspace.Pointer.Pan)
            Workspace.Camera.PanBy(DeltaX, DeltaY,
                Workspace.Heightfield.SampleElevation(Workspace.Camera.Position[0], Workspace.Camera.Position[2]));
        if (Workspace.Pointer.Sculpt && performance.now() - Workspace.Pointer.LastStrokeTime > 55)
        {
            Workspace.Pointer.LastStrokeTime = performance.now();
            ApplyToolAtPointer(Event);
        }
    });
    const ReleasePointer = () =>
    {
        Workspace.Pointer.Look = false;
        Workspace.Pointer.Pan = false;
        Workspace.Pointer.Sculpt = false;
        Workspace.Pointer.Orbit = false;
        Workspace.Camera.LookEnabled = false;
        Workspace.Camera.PanEnabled = false;
    };
    Canvas.addEventListener('pointerup', ReleasePointer);
    Canvas.addEventListener('pointercancel', ReleasePointer);
    Canvas.addEventListener('pointerleave', ReleasePointer);
    Canvas.addEventListener('wheel', Event =>
    {
        Event.preventDefault();
        if (Workspace.Mode === 'Planet')
        {
            Workspace.Planet.Distance = Math.max(6.6e6, Math.min(4e7, Workspace.Planet.Distance * Math.pow(1.0015, Event.deltaY)));
        }
        else if (Event.ctrlKey)
        {
            Workspace.BrushRadius = Math.max(200, Math.min(60000, Workspace.BrushRadius * Math.pow(1.0015, -Event.deltaY)));
            $('brush-radius').value = String(Workspace.BrushRadius / 1000);
            $('brush-radius-out').textContent = (Workspace.BrushRadius / 1000).toFixed(1);
        }
        else
        {
            const Speed = Workspace.Camera.AdjustSpeed(Event.deltaY);
            $('camera-speed-out').textContent = String(Math.round(Speed));
            $('camera-speed').value = String(Math.max(0, Math.min(100, Math.sqrt(Math.max(0, Speed - 4) / 0.26))));
        }
    }, { passive: false });

    $('minimap').addEventListener('click', Event =>
    {
        const Rectangle = Event.currentTarget.getBoundingClientRect();
        const Extent = Workspace.Heightfield.Extent;
        const WorldX = ((Event.clientX - Rectangle.left) / Rectangle.width - 0.5) * Extent;
        const WorldZ = ((Event.clientY - Rectangle.top) / Rectangle.height - 0.5) * Extent;
        Workspace.Camera.Position[0] = WorldX;
        Workspace.Camera.Position[2] = WorldZ;
        Workspace.Camera.Velocity.fill(0);
    });

    document.addEventListener('keydown', Event =>
    {
        const Tag = Event.target.tagName;
        if (Tag === 'INPUT' || Tag === 'SELECT' || Tag === 'TEXTAREA') return;
        Workspace.Camera.PressedKeys.add(Event.code);
        const Digit = Event.code.match(/^Digit([1-8])$/);
        if (Digit)
        {
            const Tools = ['Raise', 'Lower', 'Smooth', 'Flatten', 'Erode', 'Roughen', 'Drop', 'Tsunami'];
            SelectTool(Tools[parseInt(Digit[1], 10) - 1]);
        }
        if ((Event.ctrlKey || Event.metaKey) && Event.code === 'KeyZ')
        {
            Event.preventDefault();
            if (Workspace.Heightfield.UndoStroke()) Workspace.Renderer.RefreshElevation();
        }
        if ((Event.ctrlKey || Event.metaKey) && Event.code === 'KeyY')
        {
            Event.preventDefault();
            if (Workspace.Heightfield.RedoStroke()) Workspace.Renderer.RefreshElevation();
        }
        if (Event.code === 'KeyF')
        {
            const Target = ResolveTargetPosition();
            Workspace.Camera.FocusOn([Target.X, Workspace.Heightfield.SampleElevation(Target.X, Target.Z), Target.Z]);
        }
        if (Event.code === 'KeyG') { Workspace.ShowGrid = !Workspace.ShowGrid; $('toggle-grid').checked = Workspace.ShowGrid; }
        if (Event.code === 'KeyL') { Workspace.ShowBounds = !Workspace.ShowBounds; $('toggle-bounds').checked = Workspace.ShowBounds; }
        if (Event.code === 'KeyR') Workspace.Water.FinalizeFields();
        if (Event.code === 'KeyP')
        {
            Workspace.Mode = Workspace.Mode === 'Flat' ? 'Planet' : 'Flat';
            $$('.mode-tab').forEach(Button => Button.classList.toggle('selected', Button.dataset.mode === Workspace.Mode));
        }
        if (Event.code === 'KeyV')
        {
            const Modes = ['beauty', 'bathymetry', 'eta', 'velocity', 'lod'];
            Workspace.ViewMode = Modes[(Modes.indexOf(Workspace.ViewMode) + 1) % Modes.length];
            $$('.view-chip').forEach(Button => Button.classList.toggle('selected', Button.dataset.view === Workspace.ViewMode));
            $('hud-mode').textContent = Workspace.ViewMode.toUpperCase();
        }
        if (Event.code === 'Space')
        {
            Event.preventDefault();
            const Point = ResolveSeaSurfacePoint(Workspace.Camera.ResolveRay(0, 0, Canvas.clientWidth / Canvas.clientHeight));
            Workspace.Water.InjectDisturbance(Point.X, Point.Z, 2.4, 180);
        }
    });
    document.addEventListener('keyup', Event => Workspace.Camera.PressedKeys.delete(Event.code));
    window.addEventListener('blur', () => { Workspace.Camera.PressedKeys.clear(); ReleasePointer(); });
    document.addEventListener('click', Event =>
    {
        if (!Event.target.closest('.file-actions')) $('export-menu').classList.add('hidden');
    });
}

async function ConvertSurface()
{
    const Target = ResolveTargetPosition();
    const Specification = ConstructExtractionSpecification({
        CenterX: Target.X,
        CenterZ: Target.Z,
        HorizontalExtent: Math.max(3200, parseFloat($('mesh-extent').value) * 1000),
        VoxelSize: Math.max(100, parseFloat($('mesh-voxel').value))
    });
    $('loading-label').textContent = 'Contouring SDF volume…';
    $('progress-fill').style.width = '35%';
    $('loading').classList.remove('hidden');
    await new Promise(Resolve => setTimeout(Resolve, 40));
    const Surface = ConvertSdfToPolygonSurface(Workspace.Heightfield, Specification);
    Workspace.Surface = Surface;
    Workspace.Renderer.AttachPolygonSurface(Surface);
    Workspace.ShowSurface = true;
    $('toggle-surface').checked = true;
    $('mesh-vertices').textContent = Surface.VertexCount.toLocaleString();
    $('mesh-triangles').textContent = Surface.TriangleCount.toLocaleString();
    $('mesh-voxels').textContent = FormatCount(Surface.VoxelCount);
    $('mesh-time').textContent = `${Surface.ConversionMilliseconds} ms`;
    $('progress-fill').style.width = '100%';
    $('loading').classList.add('hidden');
    $('status-left').textContent = `Converted SDF → ${Surface.TriangleCount.toLocaleString()} triangles · Euler χ ${ComputeEulerCharacteristic(Surface)}`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FRAME COMPOSITION
//------------------------------------------------------------------------------------------------------------------------

function FrameLoop(Now)
{
    requestAnimationFrame(FrameLoop);
    const DeltaSeconds = Workspace.LastFrameTime ? Math.min(0.05, Math.max(0.001, (Now - Workspace.LastFrameTime) / 1000)) : 0.016;
    Workspace.LastFrameTime = Now;
    Workspace.Fps = Workspace.Fps * 0.92 + (1 / DeltaSeconds) * 0.08;
    if (Workspace.Busy || !Workspace.Renderer) return;

    const Camera = Workspace.Camera;
    const PlanetMode = Workspace.Mode === 'Planet';
    if (!PlanetMode)
    {
        const GroundElevation = Workspace.Heightfield.SampleElevation(Camera.Position[0], Camera.Position[2]);
        Camera.IntegrateMotion(DeltaSeconds, GroundElevation);
    }
    const CameraX = PlanetMode ? 0 : Camera.Position[0];
    const CameraZ = PlanetMode ? 0 : Camera.Position[2];
    const WaterWindows = ResolveWindows(Workspace.WaterConfiguration, CameraX, CameraZ);
    const TerrainWindows = ResolveTerrainWindows(Workspace.TerrainClipmap, CameraX, CameraZ);

    if (Workspace.SolverRunning && !PlanetMode)
        Workspace.Water.Integrate(WaterWindows, Workspace.Renderer.ElevationTexture,
            Workspace.Heightfield.Resolution, Workspace.Heightfield.Extent);
    if (Workspace.RainEnabled && !PlanetMode && Workspace.FrameIndex % 9 === 0)
    {
        const Angle = Math.random() * Math.PI * 2;
        const Radial = Math.random() * Workspace.WaterConfiguration.InnerExtent * 0.7;
        Workspace.Water.InjectDisturbance(CameraX + Math.cos(Angle) * Radial, CameraZ + Math.sin(Angle) * Radial,
            0.4 + Math.random() * 0.7, 90 + Math.random() * 140);
    }
    if (!PlanetMode && Workspace.FrameIndex % 12 === 0) Workspace.Water.RefreshProbe(CameraX, CameraZ);

    const SunRadians = Workspace.SunAngle * Math.PI / 180;
    const SunDirection = NormalizeVector([0.64 * Math.cos(SunRadians), Math.sin(SunRadians), 0.42 * Math.cos(SunRadians)]);
    const OverlayLines = ConstructOverlayLines(Workspace.Heightfield, TerrainWindows, WaterWindows, {
        Brush: Workspace.LastBrush && !PlanetMode ? { ...Workspace.LastBrush, Radius: Workspace.BrushRadius } : null,
        ShowSimulationGrid: Workspace.ShowGrid && !PlanetMode,
        ShowLodBounds: Workspace.ShowBounds && !PlanetMode,
        SeaLevel: Workspace.CrustConfiguration.SeaLevel
    });

    Workspace.Renderer.Rasterize({
        Camera,
        Mode: Workspace.Mode,
        Planet: Workspace.Planet,
        Water: Workspace.Water,
        WaterWindows,
        TerrainWindows,
        ViewMode: Workspace.ViewMode,
        ShowConvertedSurface: Workspace.ShowSurface,
        ShowSurfaceWireframe: Workspace.ShowWireframe,
        OverlayLines,
        SunDirection,
        HazeDensity: Workspace.Haze / (PlanetMode ? Workspace.Renderer.PlanetRadius * 20 : Workspace.Heightfield.Extent * 0.9),
        SeaLevel: Workspace.CrustConfiguration.SeaLevel,
        Spectral: Workspace.Spectral,
        ElapsedSeconds: Workspace.Water.ElapsedSeconds
    });

    ++Workspace.FrameIndex;
    if (Now - Workspace.LastHudTime > 110) { RefreshHud(WaterWindows); Workspace.LastHudTime = Now; }
    if (Now - Workspace.LastStatsTime > 520) { RefreshStats(); Workspace.LastStatsTime = Now; }
    if (Now - Workspace.LastMinimapTime > 320) { DrawMinimap(); Workspace.LastMinimapTime = Now; }
}

function RefreshHud(WaterWindows)
{
    const Camera = Workspace.Camera;
    const Altitude = Camera.Position[1] - Workspace.Heightfield.SampleElevation(Camera.Position[0], Camera.Position[2]);
    $('hud-coords').textContent = `X ${(Camera.Position[0] / 1000).toFixed(1)} km · Z ${(Camera.Position[2] / 1000).toFixed(1)} km · ALT ${FormatDistance(Altitude)}`;
    $('hud-scale').textContent = `Δx ${WaterWindows[0].CellSpacing.toFixed(2)} m · ${WaterWindows.length} tiers · ${Math.round(Camera.FlySpeed)} m/s`;
    $('hud-mode').textContent = Workspace.ViewMode.toUpperCase();
    $('compass-needle').style.transform = `rotate(${-Camera.Yaw * 180 / Math.PI}deg)`;
    $('compass-label').textContent = Math.abs(Camera.Yaw) < 0.35 ? 'N' : Camera.Yaw > 0 ? 'NE' : 'NW';
    $('minimap-scale').textContent = `${FormatDistance(Workspace.Heightfield.Extent)}`;
}

function RefreshStats()
{
    if (!Workspace.Heightfield || !Workspace.Water) return;
    const Camera = Workspace.Camera;
    const Windows = ResolveWindows(Workspace.WaterConfiguration, Camera.Position[0], Camera.Position[2]);
    const Adaptivity = ComputeAdaptivityReport(Workspace.WaterConfiguration, Workspace.Heightfield.Extent, Windows);
    $('savings-factor').textContent = `${FormatCount(Adaptivity.SavingsFactor)}×`;
    $('savings-fill').style.width = `${Math.min(100, 12 + 88 * Math.min(1, Math.log10(Adaptivity.SavingsFactor + 1) / 7))}%`;
    $('uniform-cells').textContent = FormatCount(Adaptivity.UniformCells);
    $('adaptive-cells').textContent = FormatCount(Adaptivity.AdaptiveCells);
    const Body = $('ring-table').querySelector('tbody');
    Body.innerHTML = Windows.map(Window => `<tr><td>L${Window.Level}</td><td>${FormatDistance(Window.Extent)}</td><td>${Window.CellSpacing < 10 ? Window.CellSpacing.toFixed(2) : Window.CellSpacing.toFixed(0)} m</td><td>${Window.DeltaTime.toFixed(3)} s</td><td>${FormatCount(Window.CellCount)}</td></tr>`).join('');
    const Probe = Workspace.Water.Probe;
    $('probe-eta').textContent = `${Probe.Eta.toFixed(3)} m`;
    $('probe-velocity').textContent = `${Math.hypot(Probe.VelocityX, Probe.VelocityZ).toFixed(3)} m/s`;
    $('probe-depth').textContent = `${Probe.Depth.toFixed(1)} m`;
    $('probe-time').textContent = `${Workspace.Water.ElapsedSeconds.toFixed(2)} s`;
    const Statistics = Workspace.Heightfield.ComputeStatistics();
    $('stat-land').textContent = `${(Statistics.LandFraction * 100).toFixed(1)} / ${(Statistics.OceanFraction * 100).toFixed(1)}%`;
    $('stat-fps').textContent = Workspace.Fps.toFixed(0);
    $('stat-calls').textContent = String(Workspace.Renderer.LastDrawCalls);
    $('stat-tris').textContent = FormatCount(Workspace.Renderer.LastTriangleCount);
    $('status-right').textContent = `${Workspace.Heightfield.Resolution}² crust · ${Windows.length} × ${Workspace.WaterConfiguration.GridResolution}² SWE cells · ${Workspace.Capabilities.UseHalfFloat ? 'RGBA16F' : 'RGBA32F'} · ${Workspace.Mode}`;
}

function DrawMinimap()
{
    const Canvas = $('minimap');
    const Context = Canvas.getContext('2d');
    const Size = Canvas.width;
    const Extent = Workspace.Heightfield.Extent;
    const Image = Context.createImageData(Size, Size);
    const SeaLevel = Workspace.CrustConfiguration.SeaLevel;
    for (let Row = 0; Row < Size; ++Row)
    {
        for (let Column = 0; Column < Size; ++Column)
        {
            const WorldX = (Column / Size - 0.5) * Extent;
            const WorldZ = (Row / Size - 0.5) * Extent;
            const Elevation = Workspace.Heightfield.SampleElevation(WorldX, WorldZ);
            let Red, Green, Blue;
            if (Elevation < SeaLevel)
            {
                const Depth = Math.min(1, (SeaLevel - Elevation) / 3600);
                Red = 12 + 24 * (1 - Depth); Green = 54 + 44 * (1 - Depth); Blue = 86 + 40 * (1 - Depth);
            }
            else
            {
                const Height = Math.min(1, Elevation / 3200);
                Red = 92 + 140 * Height; Green = 126 + 108 * Height; Blue = 70 + 110 * Height;
            }
            const Offset = (Row * Size + Column) * 4;
            Image.data[Offset] = Red; Image.data[Offset + 1] = Green; Image.data[Offset + 2] = Blue; Image.data[Offset + 3] = 255;
        }
    }
    Context.putImageData(Image, 0, 0);
    const Project = (WorldX, WorldZ) => [(WorldX / Extent + 0.5) * Size, (WorldZ / Extent + 0.5) * Size];
    const Windows = ResolveWindows(Workspace.WaterConfiguration, Workspace.Camera.Position[0], Workspace.Camera.Position[2]);
    Context.lineWidth = 1;
    for (const Window of Windows)
    {
        const [X, Y] = Project(Window.WorldOriginX, Window.WorldOriginZ);
        const Side = Window.Extent / Extent * Size;
        Context.strokeStyle = `rgba(227,154,59,${0.72 - Window.Level * 0.07})`;
        Context.strokeRect(X, Y, Side, Side);
    }
    const [CameraX, CameraY] = Project(Workspace.Camera.Position[0], Workspace.Camera.Position[2]);
    Context.fillStyle = '#ffffff';
    Context.strokeStyle = '#10161c';
    Context.lineWidth = 2;
    Context.beginPath();
    Context.moveTo(CameraX, CameraY - 7);
    Context.lineTo(CameraX + 5, CameraY + 5);
    Context.lineTo(CameraX - 5, CameraY + 5);
    Context.closePath();
    Context.fill();
    Context.stroke();
    if (Workspace.LastBrush)
    {
        const [BrushX, BrushY] = Project(Workspace.LastBrush.X, Workspace.LastBrush.Z);
        Context.strokeStyle = '#ffd391';
        Context.lineWidth = 1;
        Context.beginPath();
        Context.arc(BrushX, BrushY, Math.max(2, Workspace.BrushRadius / Extent * Size), 0, Math.PI * 2);
        Context.stroke();
    }
}

InitializeWorkspace();
