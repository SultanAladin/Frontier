/*==========================================================================================================================================
                                                               WORKSPACE.JS
==========================================================================================================================================*/
// 🧩 Infinite vector painting, independent terrain stacks, bounded volume exports, and persistent editor interaction.

import { SegmentCount, ValidateDocument, SampleSparseBricks } from './DistanceField.js';
import { CreateVolumePresentation } from './VolumePresentation.js';
import { InvalidateLandforms, LayerVerticalBounds } from './LandformStack.js';
import { StackGlyph, InitializeGlyphs } from './StackGlyphs.js';
import { InitializeLayerStackPanel } from './LayerStackPanel.js';
import { CreateTerrainWorld } from './TerrainExamples.js';
import { InitializeCameraNavigation } from './CameraNavigation.js';
import { EvaluateOcean } from './OceanSurface.js';
import { CreateLayer, CreateCoastalWorld } from './CoastalWorld.js';
import { ConstructVolumeLattice, ResolveLatticeDimensions } from './VolumeLattice.js';
import { InitializeVolumeSculpting } from './VolumeSculptInteraction.js';
import { DescribePreviewSampling, ResolveWorkingOrbit, MinimumWorkingOrbit, PreviewSamplingRevision } from './PreviewSampling.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                        DOCUMENT AND HISTORY
//------------------------------------------------------------------------------------------------------------------------

const Find = Selector => document.querySelector(Selector);
const Paint = Find('#Paint'), Volume = Find('#Volume'), Ink = Paint.getContext('2d');

let Layers = CreateCoastalWorld();
let Active = 0, Tool = 'paint', Workspace = 'mask', InspectorSection = 'shape';
let DraggedMask = -1;
let SelectedLayers = new Set([0]), Projection = { X: 0, Z: 0, Scale: .65 };
let Selection = null, Gesture = null, BrushPosition = null, Space = false;
let History = [], Future = [], Rasterization = null, SaveTimer = null;
let OceanTime = 18, PreviousTime = null, LastOceanProfile = -1, LastAnimationTime = performance.now();
let Baking = null, BakingChunks = [];
let VolumeDirty = true, PreparationTimer = null;
let WorkingEnabled = false, WorkingSelection = '', WorkingSelectionEpoch = 0, SelectedLattice = null;
let Sculpting = null, DocumentEditRevision = 0, AutomaticWorking = false;
const Camera = { Yaw: -.35, Pitch: .72, Orbit: 1500, Target: [0, 0, 0] };
const Layer = () => Layers[Active];
const Report = Message => Find('#Status').textContent = Message;
const Document = () => ({ Format: 'FrontierLandscape/1', Layers });

try
{
    const Stored = localStorage.getItem('FrontierLandscape');
    if (Stored) Layers = ValidateDocument(JSON.parse(Stored)).Layers;
}
catch (Failure) { Report(`Local document could not be opened: ${Failure.message}`); }

function CaptureDocumentEdit()
{
    return JSON.stringify({ Layers, Active, SelectedMasks: [...SelectedLayers], StackSelection: Panel.RetrieveSelection() });
}

function RestoreDocumentEdit(Serialized)
{
    const Snapshot = JSON.parse(Serialized);
    ReplaceLayers(Snapshot.Layers);
    Active = Snapshot.Active;
    SelectedLayers = new Set(Snapshot.SelectedMasks);
    Panel.RestoreSelection(Snapshot.StackSelection);
}

function Remember()
{
    History.push(CaptureDocumentEdit());
    if (History.length > 60) History.shift();
    Future = [];
}

function ReplaceLayers(Replacement)
{
    Layers = Replacement;
    Active = Math.max(0, Math.min(Active, Layers.length - 1));
    SelectedLayers = new Set([...SelectedLayers].filter(Index => Index < Layers.length));
}

function ScheduleSave()
{
    Find('#SaveStatus').textContent = 'Saving…';
    clearTimeout(SaveTimer);
    SaveTimer = setTimeout(() =>
    {
        try
        {
            localStorage.setItem('FrontierLandscape', JSON.stringify(Document()));
            Find('#SaveStatus').textContent = 'Saved locally';
        }
        catch
        {
            Find('#SaveStatus').textContent = 'Not saved';
            Report('Local storage is unavailable or full. Save project to keep your work.');
        }
    }, 300);
}

function Changed(GeometryChanged = true)
{
    if (GeometryChanged) { DocumentEditRevision++; Sculpting?.Cancel(); InvalidateLandforms(); VolumeDirty = true; }
    Refresh();
    Draw();
    if (Rasterization && GeometryChanged && Workspace === 'landscape')
    {
        try { Rasterization.Upload(Layers); VolumeDirty = false; }
        catch (Failure) { Report(Failure.message); }
    }
    ScheduleSave();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        MASK DISPLAY AND NAVIGATION
//------------------------------------------------------------------------------------------------------------------------

function PaintStroke(Context, Stroke, Colour)
{
    Context.globalCompositeOperation = Stroke.Erase ? 'destination-out' : 'source-over';
    Context.fillStyle = Colour;
    Context.strokeStyle = Colour;
    Context.lineWidth = Stroke.Radius * 2;
    Context.lineCap = 'round';
    Context.lineJoin = 'round';
    Context.beginPath();
    Context.moveTo(...Stroke.Points[0]);
    for (const Point of Stroke.Points) Context.lineTo(...Point);
    Context.stroke();
    Context.beginPath();
    Context.arc(...Stroke.Points[0], Stroke.Radius, 0, Math.PI * 2);
    Context.fill();
}

function MaskThumbnail(Entry)
{
    const Canvas = document.createElement('canvas');
    Canvas.width = 96;
    Canvas.height = 88;
    if (!Entry.Strokes.length) return Canvas;
    const X = Entry.Strokes.flatMap(Stroke => Stroke.Points.flatMap(Point => [Point[0] - Stroke.Radius, Point[0] + Stroke.Radius]));
    const Z = Entry.Strokes.flatMap(Stroke => Stroke.Points.flatMap(Point => [Point[1] - Stroke.Radius, Point[1] + Stroke.Radius]));
    const MinimumX = Math.min(...X), MaximumX = Math.max(...X), MinimumZ = Math.min(...Z), MaximumZ = Math.max(...Z);
    const Scale = Math.min(82 / (MaximumX - MinimumX), 74 / (MaximumZ - MinimumZ));
    const Context = Canvas.getContext('2d');
    Context.setTransform(Scale, 0, 0, Scale, 48 - (MinimumX + MaximumX) * .5 * Scale, 44 - (MinimumZ + MaximumZ) * .5 * Scale);
    for (const Stroke of Entry.Strokes) PaintStroke(Context, Stroke, Entry.Color);
    return Canvas;
}

function SelectMask(Index, Additive = false)
{
    Active = Index;
    if (!Additive) SelectedLayers = new Set([Index]);
    else if (SelectedLayers.has(Index)) SelectedLayers.delete(Index);
    else SelectedLayers.add(Index);
    Selection = null;
    Panel.CloseMenu();
    Refresh();
    Draw();
    Report(`${Layer().Name} active · ${SelectedLayers.size} mask${SelectedLayers.size === 1 ? '' : 's'} selected.`);
}

function ReorderMasks(From, To)
{
    if (From < 0 || To < 0 || To >= Layers.length || From === To) return;
    Remember();
    const [Moved] = Layers.splice(From, 1);
    Layers.splice(To, 0, Moved);
    Active = To;
    SelectedLayers = new Set([Active]);
    Selection = null;
    Panel.ResetSelection();
    Changed();
    Report(`${Moved.Name} moved to mask position ${To + 1}. Its strokes, boundaries and generator stacks move together.`);
}

function RenderMasks()
{
    Find('#Layers').replaceChildren();
    Layers.forEach((Entry, Index) =>
    {
        const Row = document.createElement('div');
        Row.className = `mask-row ${Index === Active ? 'selected' : ''} ${SelectedLayers.has(Index) ? 'batch-selected' : ''} ${Entry.Visible ? '' : 'muted'}`;
        Row.dataset.maskIndex = Index;
        Row.tabIndex = 0;
        Row.draggable = true;
        Row.setAttribute('role', 'option');
        Row.setAttribute('aria-label', Entry.Name);
        Row.setAttribute('aria-selected', String(SelectedLayers.has(Index)));
        const Thumbnail = document.createElement('span');
        Thumbnail.className = 'mask-thumbnail';
        Thumbnail.append(MaskThumbnail(Entry));
        const Copy = document.createElement('span');
        Copy.className = 'mask-copy';
        const Title = document.createElement('strong'), Detail = document.createElement('small');
        Title.textContent = Entry.Name;
        Detail.textContent = `${Entry.Regions.length} region${Entry.Regions.length === 1 ? '' : 's'} · ${(Entry.Landforms || []).length} generators`;
        Copy.append(Title, Detail);
        const Eye = document.createElement('button');
        Eye.className = 'icon-button';
        Eye.title = `Toggle ${Entry.Name} visibility`;
        Eye.setAttribute('aria-label', Eye.title);
        Eye.setAttribute('aria-pressed', String(Entry.Visible));
        Eye.append(StackGlyph(Entry.Visible ? 'eye' : 'eye-off'));
        Eye.onclick = Event =>
        {
            Event.stopPropagation();
            Remember();
            Entry.Visible = !Entry.Visible;
            Changed();
        };
        Row.append(Thumbnail, Copy, Eye);
        if (SelectedLayers.size > 1 && SelectedLayers.has(Index))
        {
            const Mark = document.createElement('span');
            Mark.className = 'batch-mark';
            Row.append(Mark);
        }
        Row.ondragstart = Event =>
        {
            DraggedMask = Index;
            Event.dataTransfer.effectAllowed = 'move';
            Event.dataTransfer.setData('application/frontier-mask', String(Index));
        };
        Row.ondragover = Event =>
        {
            if (DraggedMask < 0 || DraggedMask === Index) return;
            Event.preventDefault();
            Row.classList.add('drop-target');
        };
        Row.ondragleave = () => Row.classList.remove('drop-target');
        Row.ondrop = Event =>
        {
            if (DraggedMask < 0) return;
            Event.preventDefault();
            const From = DraggedMask;
            DraggedMask = -1;
            ReorderMasks(From, Index);
        };
        Row.ondragend = () =>
        {
            DraggedMask = -1;
            document.querySelectorAll('.mask-row.drop-target').forEach(Entry => Entry.classList.remove('drop-target'));
        };
        Row.onclick = Event => SelectMask(Index, Event.ctrlKey || Event.metaKey);
        Row.onkeydown = Event =>
        {
            if (Event.target === Row && Event.altKey && ['ArrowUp', 'ArrowDown'].includes(Event.key))
            {
                Event.preventDefault();
                ReorderMasks(Index, Index + (Event.key === 'ArrowUp' ? -1 : 1));
                Find(`.mask-row[data-mask-index="${Active}"]`).focus();
                return;
            }
            if (Event.target !== Row || !['Enter', ' '].includes(Event.key)) return;
            Event.preventDefault();
            SelectMask(Index, Event.ctrlKey || Event.metaKey);
        };
        Find('#Layers').append(Row);
    });
    Find('#MaskLayerCount').textContent = Layers.length;
    Find('#DuplicateMask').disabled = Layers.length >= 16;
    Find('#DeleteMask').disabled = Layers.length <= 1;
}

function World(Event)
{
    const Bounds = Paint.getBoundingClientRect();
    return [(Event.clientX - Bounds.left - Bounds.width / 2) / Projection.Scale + Projection.X,
        (Event.clientY - Bounds.top - Bounds.height / 2) / Projection.Scale + Projection.Z];
}

function Draw()
{
    if (Workspace !== 'mask') return;
    const Ratio = Math.min(devicePixelRatio, 2), Width = Paint.clientWidth, Height = Paint.clientHeight;
    Paint.width = Math.max(1, Math.floor(Width * Ratio));
    Paint.height = Math.max(1, Math.floor(Height * Ratio));
    Ink.setTransform(Ratio, 0, 0, Ratio, 0, 0);
    Ink.fillStyle = '#1a2022';
    Ink.fillRect(0, 0, Width, Height);
    const Grid = 100 * Math.pow(10, Math.floor(Math.log10(1 / Projection.Scale)));
    const Left = Projection.X - Width / 2 / Projection.Scale, Top = Projection.Z - Height / 2 / Projection.Scale;
    Ink.strokeStyle = '#2b3234';
    Ink.lineWidth = .7;
    Ink.beginPath();
    for (let X = Math.floor(Left / Grid) * Grid; X < Left + Width / Projection.Scale; X += Grid)
    {
        const Pixel = (X - Projection.X) * Projection.Scale + Width / 2;
        Ink.moveTo(Pixel, 0);
        Ink.lineTo(Pixel, Height);
    }
    for (let Z = Math.floor(Top / Grid) * Grid; Z < Top + Height / Projection.Scale; Z += Grid)
    {
        const Pixel = (Z - Projection.Z) * Projection.Scale + Height / 2;
        Ink.moveTo(0, Pixel);
        Ink.lineTo(Width, Pixel);
    }
    Ink.stroke();
    const Surface = document.createElement('canvas');
    Surface.width = Paint.width;
    Surface.height = Paint.height;
    const Brush = Surface.getContext('2d');
    for (const Entry of Layers)
    {
        if (!Entry.Visible) continue;
        Brush.setTransform(1, 0, 0, 1, 0, 0);
        Brush.clearRect(0, 0, Surface.width, Surface.height);
        Brush.setTransform(Ratio * Projection.Scale, 0, 0, Ratio * Projection.Scale,
            Ratio * (Width / 2 - Projection.X * Projection.Scale), Ratio * (Height / 2 - Projection.Z * Projection.Scale));
        for (const Stroke of Entry.Strokes) PaintStroke(Brush, Stroke, Entry.Color);
        Ink.globalAlpha = Entry === Layer() ? .84 : .38;
        Ink.drawImage(Surface, 0, 0, Width, Height);
    }
    Ink.globalAlpha = 1;
    Ink.setTransform(Ratio * Projection.Scale, 0, 0, Ratio * Projection.Scale,
        Ratio * (Width / 2 - Projection.X * Projection.Scale), Ratio * (Height / 2 - Projection.Z * Projection.Scale));
    Layers.forEach((Entry, Index) =>
    {
        if (!Entry.Visible) return;
        Entry.Regions.forEach((Region, Offset) =>
        {
            Ink.strokeStyle = Index === Active ? '#becacb' : '#5e7175';
            Ink.lineWidth = 1 / Projection.Scale;
            Ink.setLineDash([5 / Projection.Scale, 5 / Projection.Scale]);
            Ink.strokeRect(Region.X0, Region.Z0, Region.X1 - Region.X0, Region.Z1 - Region.Z0);
            Ink.fillStyle = Index === Active ? '#a6b5b7' : '#60777b';
            Ink.font = `${9 / Projection.Scale}px "DM Sans", sans-serif`;
            Ink.fillText(`${Entry.Name.toUpperCase()} / REGION ${String(Offset + 1).padStart(2, '0')}`, Region.X0, Region.Z0 - 11 / Projection.Scale);
        });
    });
    if (Selection)
    {
        Ink.setLineDash([]);
        Ink.strokeStyle = '#d5e2e3';
        Ink.fillStyle = '#cddfe414';
        Ink.fillRect(Selection.X0, Selection.Z0, Selection.X1 - Selection.X0, Selection.Z1 - Selection.Z0);
        Ink.strokeRect(Selection.X0, Selection.Z0, Selection.X1 - Selection.X0, Selection.Z1 - Selection.Z0);
    }
    Ink.setLineDash([]);
    if (BrushPosition && ['paint', 'erase'].includes(Tool))
    {
        Ink.lineWidth = 1 / Projection.Scale;
        Ink.strokeStyle = Tool === 'erase' ? '#f3c2bbaa' : '#dde5e7bb';
        Ink.beginPath();
        Ink.arc(...BrushPosition, Number(Find('#Radius').value), 0, Math.PI * 2);
        Ink.stroke();
    }
    Paint.dataset.projection = JSON.stringify(Projection);
    Find('#ScaleRead').textContent = `${Math.round(70 / Projection.Scale)} m`;
    Find('#RenderReadout').textContent = `Vector canvas · ${Math.round(Projection.Scale * 100)}%`;
}

function SelectTool(Next)
{
    Tool = Next;
    document.querySelectorAll('[data-tool]').forEach(Control => Control.classList.toggle('active', Control.dataset.tool === Next));
    Paint.style.cursor = Next === 'pan' ? 'grab' : Next === 'select' ? 'crosshair' : 'none';
    Draw();
}

function Fit(ActiveOnly = false)
{
    const Sources = ActiveOnly ? [Layer()] : Layers.filter(Entry => Entry.Visible);
    const Regions = Sources.flatMap(Entry => Entry.Regions);
    if (!Regions.length)
    {
        Projection = { X: 0, Z: 0, Scale: .65 };
        Camera.Target = [0, 0, 0];
        Camera.Orbit = 1000;
        Draw();
        return;
    }
    const X0 = Math.min(...Regions.map(Region => Region.X0)), X1 = Math.max(...Regions.map(Region => Region.X1));
    const Z0 = Math.min(...Regions.map(Region => Region.Z0)), Z1 = Math.max(...Regions.map(Region => Region.Z1));
    const Width = Find('#Viewport').clientWidth, Height = Find('#Viewport').clientHeight;
    Projection = { X: (X0 + X1) / 2, Z: (Z0 + Z1) / 2, Scale: Math.max(.0008, Math.min((Width - 130) / (X1 - X0), (Height - 160) / (Z1 - Z0))) };
    const Top = Math.max(...Sources.flatMap(Entry => Entry.Regions.map(Region => LayerVerticalBounds(Entry, Region)[1])));
    Camera.Target = [Projection.X, Math.max(0, Top) * .12, Projection.Z];
    Camera.Orbit = Math.max(X1 - X0, Z1 - Z0) * 1.23 * Math.max(.9, Height / Math.max(Width, 1) * .9);
    Draw();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        INSPECTOR AND WORKSPACES
//------------------------------------------------------------------------------------------------------------------------

function ActivateInspector(Section)
{
    InspectorSection = Section;
    document.querySelectorAll('[data-inspector]').forEach(Control =>
    {
        Control.classList.toggle('active', Control.dataset.inspector === Section);
        Control.setAttribute('aria-selected', String(Control.dataset.inspector === Section));
    });
    Find('#ShapeInspector').hidden = Section !== 'shape';
    Find('#RegionInspector').hidden = Section !== 'region';
    Find('#OceanPanel').hidden = Section !== 'ocean';
}

function RenderCoastProfile()
{
    const Region = Layer().Regions[Panel.RetrieveBoundary()];
    const Width = Region?.Edge || 0;
    Find('#CoastProfile').innerHTML = `<svg viewBox="0 0 220 65" aria-label="Coastline feather profile"><path d="M8 49H212" stroke="#666" stroke-width=".6"/><path d="M15 49 ${Width > 0 ? 'C55 49 58 18 130 18' : 'L55 49 55 18 130 18'} H210" stroke="#b7c4c4" stroke-width="1.2" fill="none"/><text x="150" y="60" fill="#717171" font-size="7" font-family="DM Sans">SEA LEVEL · 0 m</text></svg>`;
}

function RenderRegions()
{
    Find('#MaskName').value = Layer().Name;
    Find('#MaskColour').value = Layer().Color;
    Find('#RegionCount').textContent = Layer().Regions.length;
    Find('#RegionList').replaceChildren();
    Layer().Regions.forEach((Region, Index) =>
    {
        const Row = document.createElement('div');
        Row.className = `region-row ${Index === Panel.RetrieveBoundary() ? 'selected' : ''}`;
        const Caption = document.createElement('button');
        Caption.textContent = `Region ${String(Index + 1).padStart(2, '0')}`;
        Caption.setAttribute('aria-pressed', String(Index === Panel.RetrieveBoundary()));
        const Detail = document.createElement('small');
        Detail.textContent = `${Math.round(Region.X1 - Region.X0)} × ${Math.round(Region.Z1 - Region.Z0)} m · ${ResolveLatticeDimensions(Layer(), Region).join(' × ')} XYZ`;
        Caption.append(Detail);
        Caption.onclick = () => Panel.SelectBoundary(Index);
        const Remove = document.createElement('button');
        Remove.className = 'icon-button';
        Remove.append(StackGlyph('close'));
        Remove.title = `Remove region ${Index + 1}`;
        Remove.setAttribute('aria-label', Remove.title);
        Remove.onclick = () =>
        {
            Remember();
            Layer().Regions.splice(Index, 1);
            Panel.ResetBoundary();
            Changed();
        };
        Row.append(Caption, Remove);
        Find('#RegionList').append(Row);
    });
    if (!Layer().Regions.length)
    {
        const Caption = document.createElement('p');
        Caption.className = 'card-note';
        Caption.textContent = 'No volume allocated. In the mask editor, drag a region with R, then choose Add to mask.';
        Find('#RegionList').append(Caption);
    }
    const Region = Layer().Regions[Panel.RetrieveBoundary()];
    Find('#EdgeWidth').disabled = !Region;
    Find('#EdgeWidth').value = Region?.Edge || 0;
    Find('#EdgeRead').textContent = `${Region?.Edge || 0} m`;
    const Dimensions = ResolveLatticeDimensions(Layer(), Region);
    for (const [Selector, Axis] of [['#Resolution', 0], ['#Vertical', 1], ['#ResolutionZ', 2]])
    {
        const Control = Find(Selector);
        if (![...Control.options].some(Option => Number(Option.value) === Dimensions[Axis])) Control.add(new Option(String(Dimensions[Axis])));
        Control.value = Dimensions[Axis];
        Control.disabled = !Region;
    }
    const SelectionKey = `${Active}:${Panel.RetrieveBoundary()}`;
    if (SelectionKey !== WorkingSelection)
    {
        WorkingEnabled = false;
        WorkingSelectionEpoch++;
        Find('#LinkHorizontal').checked = Dimensions[0] === Dimensions[2];
    }
    if (Dimensions[0] !== Dimensions[2]) Find('#LinkHorizontal').checked = false;
    Find('#LinkHorizontal').disabled = !Region;
    WorkingSelection = SelectionKey;
    if (!Region || !Layer().Visible) WorkingEnabled = false;
    Find('#WorkHere').disabled = !Region || !Layer().Visible || Workspace !== 'landscape';
    Find('#WorkAtResolution').disabled = Find('#WorkHere').disabled;
    SelectedLattice = Region ? { ...ConstructVolumeLattice(Layer(), Region), LayerIndex: Active, RegionIndex: Panel.RetrieveBoundary() } : null;
    Find('#DistanceMode').value = Layer().DistanceMode || 'bound';
    Find('#DistanceModeNote').textContent = Layer().DistanceMode === 'metric' ?
        'Reinitialize the sampled 3D field in metres. First-order anisotropic fast sweeping preserves cell signs; subcell geometry remains approximate.' :
        'Legacy conservative signed bound, not metric distance. Faster streaming export without reinitialization.';
    Find('#Depth').value = Layer().Depth;
    Find('#DepthRead').textContent = `${Layer().Depth} m`;
    Find('#Caves').checked = Layer().Caves;
    Find('#VolumeNote').textContent = Region ? `Region ${Panel.RetrieveBoundary() + 1}: ${Dimensions.join(' × ')} cells in X × Y × Z order. Y is vertical; X and Z are the horizontal plane.` : 'Allocate a region to choose its XYZ resolution.';
    Find('#LatticePitch').textContent = Region ? `Target voxel spacing: ${SelectedLattice.Pitch.map(Spacing => Spacing.toFixed(3)).join(' × ')} m. This is NOT the current whole-scene overview spacing.` : '';
    Find('#BakeRegion').replaceChildren();
    Layer().Regions.forEach((Entry, Index) => Find('#BakeRegion').add(new Option(`Region ${String(Index + 1).padStart(2, '0')}`, String(Index))));
    Find('#BakeRegion').value = String(Panel.RetrieveBoundary());
    Find('#Bake').disabled = !!Baking || !Region;
    Find('#PreviewBake').disabled = !Region;
    RenderCoastProfile();
    RenderSamplingReadout();
    Sculpting?.Refresh();
}

function Refresh()
{
    RenderMasks();
    RenderRegions();
    Panel.Render();
    Find('#HistoryUndo').disabled = !History.length;
    Find('#HistoryRedo').disabled = !Future.length;
    Find('#StrokeCount').textContent = `${SegmentCount(Layers)} / 512 vector samples`;
    Find('#SelectionPrompt').hidden = !Selection || Workspace !== 'mask';
    document.querySelectorAll('input[type="range"]').forEach(Input => Input.style.setProperty('--progress', `${(Input.value - Input.min) / (Input.max - Input.min) * 100}%`));
}

function PreviewStatus(Category, Message = '')
{
    Volume.dataset.renderStatus = Category;
    Volume.dataset.renderMessage = Message;
    Find('#RenderAPI').textContent = 'WEBGL 2 · GPU';
    const Notice = Find('#PreviewNotice');
    Notice.hidden = Workspace !== 'landscape' || ['ready', 'refining'].includes(Category);
    Notice.dataset.category = Category;
    Find('#PreviewHeading').textContent = ['lost', 'failed'].includes(Category) ? 'GPU preview stopped' :
        Category === 'geometry' ? 'Preparing XYZ surface' : 'Preparing GPU preview';
    Find('#PreviewMessage').textContent = Message || 'Preparing bounded 3D geometry. The GPU rasterizes camera images; there is no software rendering fallback.';
    Find('#PreviewRetry').hidden = !['lost', 'failed'].includes(Category);
    Find('#PreviewMask').hidden = Category === 'ready';
    if (['lost', 'failed'].includes(Category)) Report(Message);
}

function PrepareLandscape()
{
    PreparationTimer = null;
    if (Workspace !== 'landscape') return;
    try
    {
        if (VolumeDirty || !Rasterization) ValidateDocument(Document());
        if (!Rasterization) Rasterization = CreateVolumePresentation(Volume, {
            Status: PreviewStatus,
            Progress: Fraction =>
            {
                if (['geometry', 'refining'].includes(Volume.dataset.renderStatus)) Find('#PreviewMessage').textContent = `Preparing XYZ geometry · ${(Fraction * 100).toFixed(0)}%. Camera images use the GPU. Mask and save remain available.`;
            }
        });
        Rasterization.Resume();
        if (VolumeDirty) { Rasterization.Upload(Layers); VolumeDirty = false; }
        SubmitLandscapeView(performance.now());
    }
    catch (Failure)
    {
        PreviewStatus('failed', Failure.message);
    }
}

function SwitchWorkspace(Next)
{
    clearTimeout(PreparationTimer);
    PreparationTimer = null;
    if (Next === 'mask') { Sculpting?.Disable(); Rasterization?.Suspend(); }
    Workspace = Next;
    Panel.CloseMenu();
    Navigation.Clear();
    Paint.hidden = Next !== 'mask';
    Volume.hidden = Next === 'mask';
    Find('#PreviewNotice').hidden = Next === 'mask' || ['ready', 'refining'].includes(Volume.dataset.renderStatus);
    Find('#TerrainStackPanel').hidden = Next === 'mask';
    Find('#MaskInstructions').hidden = Next !== 'mask';
    Find('#BrushPanel').hidden = Next !== 'mask';
    Find('#LandformPanel').hidden = Next === 'mask';
    Find('#PaintTools').hidden = Next !== 'mask';
    Find('#LandscapeTools').hidden = Next === 'mask';
    Find('#CameraNavigation').hidden = Next === 'mask';
    Find('#MaskTab').classList.toggle('active', Next === 'mask');
    Find('#LandscapeTab').classList.toggle('active', Next === 'landscape');
    Find('#ViewTitle').textContent = Next === 'mask' ? 'Mask editor' : 'Landscape';
    Find('#ViewSubtitle').textContent = Next === 'mask' ? 'Top orthographic' : 'Perspective';
    Find('#ViewportGlyph').replaceChildren(StackGlyph(Next === 'mask' ? 'mask' : 'mountain'));
    Find('#ModeLabel').textContent = Next === 'mask' ? 'TOP ORTHOGRAPHIC' : 'PERSPECTIVE';
    Find('#FieldBadge').textContent = Next === 'mask' ? 'VECTOR CANVAS' : 'SDF · GPU SURFACE';
    Find('#CanvasHint').textContent = Next === 'mask' ? 'B paint · E erase · R region · Space / MMB pan · Scroll to zoom' : 'Double-click to work · Drag to orbit · MMB / Shift-drag to pan · Scroll to zoom · F focus';
    if (Next === 'mask' && InspectorSection === 'ocean') ActivateInspector('shape');
    Refresh();
    Draw();
    Report(Next === 'mask' ? 'Paint a world-space mask, box-select its bounds with R, then add the region to the mask.' : 'Volumetric SDF · generators apply only to their owning mask or region.');
    if (Next === 'landscape')
    {
        if (!Rasterization) PreviewStatus('compiling', 'Preparing the 3D landscape. The mask editor remains available.');
        PreparationTimer = setTimeout(PrepareLandscape, 0);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        POINTER AUTHORING
//------------------------------------------------------------------------------------------------------------------------

Paint.onpointerdown = Event =>
{
    if (Event.button > 2) return;
    Paint.setPointerCapture(Event.pointerId);
    const Point = World(Event), Mode = Space || Event.button === 1 || Event.button === 2 ? 'pan' : Tool;
    Gesture = { Mode, Point, Client: [Event.clientX, Event.clientY] };
    if (Mode === 'paint' || Mode === 'erase')
    {
        if (SegmentCount(Layers) >= 512)
        {
            Gesture = null;
            Report('This analytic GPU prototype supports 512 vector samples. Split the document or simplify strokes.');
            return;
        }
        Remember();
        Layer().Strokes.push({ Points: [Point], Radius: Number(Find('#Radius').value), Erase: Mode === 'erase' });
    }
    if (Mode === 'select') Selection = { X0: Point[0], Z0: Point[1], X1: Point[0], Z1: Point[1] };
    Event.preventDefault();
    Draw();
};

Paint.onpointermove = Event =>
{
    const Point = World(Event);
    BrushPosition = Point;
    Find('#Coordinates').textContent = `X ${Math.round(Point[0])}  ·  Z ${Math.round(Point[1])}`;
    if (Gesture?.Mode === 'pan')
    {
        Projection.X -= (Event.clientX - Gesture.Client[0]) / Projection.Scale;
        Projection.Z -= (Event.clientY - Gesture.Client[1]) / Projection.Scale;
        Gesture.Client = [Event.clientX, Event.clientY];
        Paint.style.cursor = 'grabbing';
    }
    else if (Gesture?.Mode === 'select')
    {
        Selection = { X0: Math.min(Point[0], Gesture.Point[0]), Z0: Math.min(Point[1], Gesture.Point[1]), X1: Math.max(Point[0], Gesture.Point[0]), Z1: Math.max(Point[1], Gesture.Point[1]) };
    }
    else if (Gesture)
    {
        const Stroke = Layer().Strokes.at(-1), Previous = Stroke.Points.at(-1);
        if (Math.hypot(Point[0] - Previous[0], Point[1] - Previous[1]) > Math.max(1, Stroke.Radius * .12) && SegmentCount(Layers) < 512) Stroke.Points.push(Point);
    }
    Draw();
};

function FinishGesture()
{
    if (!Gesture) return;
    const Authored = ['paint', 'erase'].includes(Gesture.Mode);
    if (Selection && (Selection.X1 - Selection.X0 < 2 || Selection.Z1 - Selection.Z0 < 2)) Selection = null;
    Gesture = null;
    SelectTool(Tool);
    if (Authored) Changed();
    else { Refresh(); Draw(); }
}

Paint.onpointerup = FinishGesture;
Paint.onpointercancel = FinishGesture;
Paint.onpointerleave = () => { BrushPosition = null; Draw(); };
Paint.oncontextmenu = Event => Event.preventDefault();
Paint.addEventListener('wheel', Event =>
{
    Event.preventDefault();
    const Before = World(Event);
    Projection.Scale = Math.max(.0008, Math.min(100, Projection.Scale * Math.exp(-Event.deltaY * .001)));
    const After = World(Event);
    Projection.X += Before[0] - After[0];
    Projection.Z += Before[1] - After[1];
    BrushPosition = After;
    Draw();
}, { passive: false });

Volume.onpointerdown = Event =>
{
    Volume.setPointerCapture(Event.pointerId);
    const Navigate = Event.altKey || Event.shiftKey || Space || Event.button === 1 || Event.button === 2 || Navigation.RetrieveMode() === 'pan';
    if (Sculpting?.PointerDown(Event, Navigate)) return;
    Gesture = { Client: [Event.clientX, Event.clientY], Mode: Event.shiftKey || Space || Event.button === 1 || Event.button === 2 || Navigation.RetrieveMode() === 'pan' ? 'pan3d' : 'orbit' };
    Event.preventDefault();
};
Volume.onpointermove = Event =>
{
    if (!Gesture) { Sculpting?.PointerMove(Event); return; }
    const ΔX = Event.clientX - Gesture.Client[0], ΔY = Event.clientY - Gesture.Client[1];
    if (Gesture.Mode === 'pan3d')
    {
        Camera.Target[0] -= (ΔX * Math.cos(Camera.Yaw) + ΔY * Math.sin(Camera.Yaw)) * Camera.Orbit * .001;
        Camera.Target[2] += (ΔX * Math.sin(Camera.Yaw) - ΔY * Math.cos(Camera.Yaw)) * Camera.Orbit * .001;
    }
    else
    {
        Camera.Yaw -= ΔX * .007;
        Camera.Pitch = Math.max(-1.50, Math.min(1.50, Camera.Pitch + ΔY * .006));
    }
    Gesture.Client = [Event.clientX, Event.clientY];
};
Volume.onpointerup = Event => { Gesture = null; Sculpting?.PointerUp(Event); };
Volume.onpointercancel = () => { Gesture = null; Sculpting?.Cancel(); };
Volume.oncontextmenu = Event => Event.preventDefault();
Volume.addEventListener('wheel', Event =>
{
    Event.preventDefault();
    Camera.Orbit = Math.max(MinimumWorkingOrbit, Math.min(1e7, Camera.Orbit * Math.exp(Event.deltaY * .001)));
}, { passive: false });

//------------------------------------------------------------------------------------------------------------------------
//                                                        WORKING SURFACE SELECTION
//------------------------------------------------------------------------------------------------------------------------

function RenderSamplingReadout()
{
    const Description = DescribePreviewSampling(Volume.dataset, SelectedLattice, WorkingEnabled || AutomaticWorking);
    const Text = (Selector, Caption) => { if (Find(Selector).textContent !== Caption) Find(Selector).textContent = Caption; };
    for (const [Suffix, Caption] of Object.entries({ Heading: Description.Heading, Actual: Description.Actual,
        Guidance: Description.Guidance, Notice: Description.Notice, Target: Description.Target, Image: Description.Image,
        Execution: Description.Execution, Renderer: Description.Renderer, Revisions: Description.Revisions, Revision: Description.Revision }))
        Text('#Sampling' + Suffix, Caption);
    Find('#SamplingNotice').hidden = !Description.Notice;
    if (Workspace === 'landscape')
    {
        Text('#FieldBadge', Description.Badge);
        Find('#FieldBadge').title = Description.Actual + ' ' + Description.Guidance;
    }
    Find('#Volume').dataset.displayedSamplingMode = Description.Mode;
    Find('#Volume').dataset.displayedSamplingMatchesTarget = String(Description.MatchesTarget);
    document.documentElement.dataset.previewSamplingRevision = PreviewSamplingRevision;
}

function RenderWorkingReadout()
{
    RenderSamplingReadout();
    Find('#WorkingReadout').hidden = Workspace !== 'landscape' || !(WorkingEnabled || AutomaticWorking);
    if (!(WorkingEnabled || AutomaticWorking)) return;
    const Region = Layer().Regions[Panel.RetrieveBoundary()];
    const Dimensions = ResolveLatticeDimensions(Layer(), Region);
    const Specification = JSON.parse(Volume.dataset.displayedWorkingLattice || 'null');
    const Current = Specification && Specification.LayerIndex === Active && Specification.RegionIndex === Panel.RetrieveBoundary() &&
        JSON.stringify(Specification.Dimensions) === JSON.stringify(Dimensions) &&
        Volume.dataset.completedDetailKey === Volume.dataset.requestedDetailKey && Boolean(Volume.dataset.completedDetailKey) &&
        Volume.dataset.completedRevision === Volume.dataset.volumeRevision &&
        Volume.dataset.completedPreparationEpoch === Volume.dataset.geometryCompletedEpoch;
    const Text = (Selector, Caption) => { if (Find(Selector).textContent !== Caption) Find(Selector).textContent = Caption; };
    Text('#WorkingTitle', `Region ${Panel.RetrieveBoundary() + 1} · ${Dimensions.join(' × ')} XYZ · ${Volume.dataset.surfaceDetailLimited === 'triangle-budget' ? 'detail budget reached' : Current ? 'working surface' : 'preparing detail'}`);
    if (!Current || !Specification.Resident)
    {
        Text('#WorkingPitch', Volume.dataset.surfaceDetailLimited === 'triangle-budget' ? 'Triangle budget reached; the last GPU surface is retained.' : 'Preparing a bounded brick; the last GPU image remains available.');
        Text('#WorkingCoverage', 'Target counts are not a claim that the whole landscape is resident.');
        return;
    }
    Text('#WorkingPitch', `${Specification.Pitch.map(Spacing => Spacing.toFixed(3)).join(' × ')} m/cell · ${Specification.Cells.join(' × ')} resident cells`);
    Text('#WorkingCoverage', `${Specification.Maximum.map((Coordinate, Axis) => (Coordinate - Specification.Minimum[Axis]).toFixed(1)).join(' × ')} m working area · 4-cell transition rim${Specification.Limited ? ' · coverage reduced to budget' : ''}`);
}

async function FocusWorkingSurface(Event = null)
{
    if (Workspace !== 'landscape' || !Rasterization) return;
    const Epoch = ++WorkingSelectionEpoch, Revision = Volume.dataset.volumeRevision;
    try
    {
        await Rasterization.Ready;
        const Bounds = Volume.getBoundingClientRect();
        const Hit = await Rasterization.Inspect({ Category: 'pick', Camera: structuredClone(Camera),
            X: Event ? (Event.clientX - Bounds.left) / Bounds.width : .5,
            Y: Event ? (Event.clientY - Bounds.top) / Bounds.height : .5,
            SelectionOnly: !Event, CentreFallback: !Event, LayerIndex: Active, RegionIndex: Panel.RetrieveBoundary() });
        if (Epoch !== WorkingSelectionEpoch || Workspace !== 'landscape' || Revision !== Volume.dataset.volumeRevision || String(Hit.Revision) !== Revision) return;
        if (!Hit.Found) { Report('No landscape surface at that point. Double-click a visible surface, or select a painted region and choose Work here.'); return; }
        SelectMask(Hit.LayerIndex);
        Panel.SelectBoundary(Hit.RegionIndex);
        WorkingEnabled = true;
        Camera.Target = [...Hit.Position];
        Camera.Orbit = ResolveWorkingOrbit(Camera.Orbit, Hit.Lattice.Pitch);
        RenderWorkingReadout();
        SubmitLandscapeView(performance.now());
        Report('Working at this region’s XYZ voxel spacing. Edit generators and operations; pan to move the working area, or double-click another surface. Choose 3D sculpt to add clay or carve this region.');
    }
    catch (Failure) { Report(`Working-point selection failed: ${Failure.message}`); }
}

Find('#WorkHere').onclick = () => void FocusWorkingSurface();
Find('#WorkAtResolution').onclick = () => void FocusWorkingSurface();
Volume.ondblclick = Event => { Event.preventDefault(); if (!Sculpting?.Enabled()) void FocusWorkingSurface(Event); };
Find('#WorkingOverview').onclick = () =>
{
    Sculpting?.Disable();
    WorkingEnabled = false;
    WorkingSelectionEpoch++;
    Fit();
    RenderWorkingReadout();
    SubmitLandscapeView(performance.now());
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        DOCUMENT CONTROLS
//------------------------------------------------------------------------------------------------------------------------

Find('#KeepRegion').onclick = () =>
{
    if (!Selection) return;
    if (Layer().Regions.length >= 16) { Report('Maximum 16 regions per mask.'); return; }
    Remember();
    Layer().Regions.push({ ...Selection, Edge: 60 });
    Selection = null;
    Panel.SelectBoundary(Layer().Regions.length - 1);
    Changed();
    Report('Region allocated. Open Landscape and add generators to this mask or boundary.');
};
Find('#CancelSelection').onclick = () => { Selection = null; Refresh(); Draw(); };
Find('#AddLayer').onclick = () =>
{
    if (Layers.length >= 16) { Report('Maximum 16 masks in this prototype.'); return; }
    Remember();
    Layers.push(CreateLayer(Layers.length));
    Active = Layers.length - 1;
    SelectedLayers = new Set([Active]);
    Selection = null;
    Changed();
    SwitchWorkspace('mask');
    Fit(true);
    ActivateInspector('shape');
    Report('New mask. Paint, then drag its region bounds with R. No SDF exists until a region is added.');
};
Find('#DuplicateMask').onclick = () =>
{
    const Copy = structuredClone(Layer());
    Copy.Name = `${Copy.Name.slice(0, 90)} copy`;
    const Edited = [...Layers, Copy];
    try
    {
        ValidateDocument({ Format: 'FrontierLandscape/1', Layers: Edited });
    }
    catch (Failure)
    {
        Report(Failure.message);
        return;
    }
    Remember();
    Layers = Edited;
    Active = Layers.length - 1;
    SelectedLayers = new Set([Active]);
    Selection = null;
    Panel.ResetSelection();
    Changed();
    Report('Independent mask copy created, including its strokes, boundaries and scoped generators.');
};
Find('#DeleteMask').onclick = () =>
{
    if (Layers.length <= 1) return;
    const Name = Layer().Name;
    Remember();
    Layers.splice(Active, 1);
    Active = Math.min(Active, Layers.length - 1);
    SelectedLayers = new Set([Active]);
    Selection = null;
    Panel.ResetSelection();
    Changed();
    Report(`${Name} deleted. Undo restores the complete mask and its generator stacks.`);
};
// 📝 Browser-native confirm pauses execution/compositing and can strand an OffscreenCanvas fence. Keep document decisions asynchronous.
function ConfirmDocumentReplacement(Heading, Message, Action)
{
    const Dialog = Find('#DocumentReplacement');
    if (Dialog.open) return Promise.resolve(false);
    Navigation.Clear();
    Find('#ReplacementHeading').textContent = Heading;
    Find('#ReplacementMessage').textContent = Message;
    Find('#ConfirmReplacement').textContent = Action;
    Dialog.returnValue = '';
    Find('#Empty').disabled = Find('#LoadTerrainExamples').disabled = true;
    return new Promise(Resolve =>
    {
        Dialog.addEventListener('close', () =>
        {
            Find('#Empty').disabled = Find('#LoadTerrainExamples').disabled = false;
            Resolve(Dialog.returnValue === 'replace');
        }, { once: true });
        Dialog.showModal();
    });
}

Find('#Empty').onclick = async () =>
{
    if (!await ConfirmDocumentReplacement('Start a new world?', 'Undo restores the current document.', 'New world')) return;
    Remember();
    Layers = [CreateLayer(0)];
    Active = 0;
    SelectedLayers = new Set([0]);
    Panel.ResetSelection();
    Selection = null;
    Changed();
    SwitchWorkspace('mask');
    ActivateInspector('shape');
    Fit();
};
Find('#HistoryUndo').onclick = () =>
{
    if (!History.length) return;
    Future.push(CaptureDocumentEdit());
    RestoreDocumentEdit(History.pop());
    Selection = null;
    Changed();
    Report('Document change undone.');
};
Find('#HistoryRedo').onclick = () =>
{
    if (!Future.length) return;
    History.push(CaptureDocumentEdit());
    RestoreDocumentEdit(Future.pop());
    Selection = null;
    Changed();
    Report('Document change restored.');
};
Find('#MaskTab').onclick = () => SwitchWorkspace('mask');
Find('#LandscapeTab').onclick = Find('#Build').onclick = () => SwitchWorkspace('landscape');
Find('#EditMask').onclick = () => { SwitchWorkspace('mask'); ActivateInspector('shape'); Refresh(); };
Find('#FocusMask').onclick = () => Fit(true);
Find('#Fit').onclick = () => Fit();
Find('#RenderQuality').onchange = Event =>
{
    Event.target.value = '1440';
    Volume.dataset.previewWidth = '1440';
};
Find('#PreviewMask').onclick = () => SwitchWorkspace('mask');
Find('#PreviewRetry').onclick = () =>
{
    try
    {
        localStorage.setItem('FrontierLandscape', JSON.stringify(Document()));
        sessionStorage.setItem('FrontierLandscapePreviewWidth', '1440');
        sessionStorage.setItem('FrontierLandscapePreviewCamera', JSON.stringify(Camera));
    }
    catch (Failure) { Report(`Save project before restarting: ${Failure.message}`); return; }
    const Address = new URL(location.href);
    Address.searchParams.set('safePreview', '1');
    location.replace(Address.href);
};
Volume.dataset.previewWidth = '1440';
Find('#RenderQuality').value = '1440';
document.querySelectorAll('[data-tool]').forEach(Control => Control.onclick = () => SelectTool(Control.dataset.tool));
document.querySelectorAll('[data-inspector]').forEach(Control => Control.onclick = () => { ActivateInspector(Control.dataset.inspector); Panel.Render(); });
Find('#Environment').onclick = () => { ActivateInspector('ocean'); Panel.Render(); };

for (const [Selector, Property] of [['#Depth', 'Depth'], ['#DistanceMode', 'DistanceMode'], ['#Caves', 'Caves'], ['#MaskName', 'Name'], ['#MaskColour', 'Color']])
{
    Find(Selector).onchange = Event =>
    {
        const Setting = Property === 'Caves' ? Event.target.checked : ['Name', 'Color', 'DistanceMode'].includes(Property) ? Event.target.value.trim() : Number(Event.target.value);
        Panel.Modify(() => Layer()[Property] = Setting, !['Name', 'DistanceMode'].includes(Property));
    };
}
Find('#Depth').oninput = Event => Find('#DepthRead').textContent = `${Event.target.value} m`;
Find('#EdgeWidth').oninput = Event => Find('#EdgeRead').textContent = `${Event.target.value} m`;
for (const [Selector, Axis] of [['#Resolution', 0], ['#Vertical', 1], ['#ResolutionZ', 2]])
{
    Find(Selector).onchange = Event =>
    {
        const Region = Layer().Regions[Panel.RetrieveBoundary()];
        if (!Region) return;
        const Dimensions = ResolveLatticeDimensions(Layer(), Region);
        Dimensions[Axis] = Number(Event.target.value);
        if (Axis !== 1 && Find('#LinkHorizontal').checked) Dimensions[0] = Dimensions[2] = Dimensions[Axis];
        Panel.Modify(() => Region.Resolution = Dimensions);
    };
}

Find('#LinkHorizontal').onchange = Event =>
{
    if (!Event.target.checked) return;
    const Region = Layer().Regions[Panel.RetrieveBoundary()];
    if (!Region) return;
    const Dimensions = ResolveLatticeDimensions(Layer(), Region);
    Dimensions[2] = Dimensions[0];
    Panel.Modify(() => Region.Resolution = Dimensions);
};

Find('#EdgeWidth').onchange = Event => Panel.Modify(() => Layer().Regions[Panel.RetrieveBoundary()].Edge = Number(Event.target.value));
Find('#Radius').oninput = Event => { Find('#RadiusRead').textContent = Event.target.value; Draw(); };
Find('#BakeRegion').onchange = Event => Panel.SelectBoundary(Number(Event.target.value));
Find('#Wave').oninput = Event => Find('#WaveRead').textContent = Number(Event.target.value).toFixed(2);
Find('#Ocean').onchange = () => Find('#ViewportOcean').classList.toggle('active', Find('#Ocean').checked);
Find('#ViewportOcean').onclick = () =>
{
    Find('#Ocean').checked = !Find('#Ocean').checked;
    Find('#Ocean').dispatchEvent(new Event('change'));
};
Find('#LoadTerrainExamples').onclick = async () =>
{
    if (!await ConfirmDocumentReplacement('Load applied examples?',
        'Replace this world with eight editable generator examples. Undo restores your document.', 'Load examples')) return;
    Remember();
    Layers = CreateTerrainWorld();
    Active = 0;
    SelectedLayers = new Set([0]);
    Panel.ResetSelection();
    Selection = null;
    Changed();
    SwitchWorkspace('landscape');
    ActivateInspector('shape');
    Fit(true);
    Report('Eight editable, already-applied landform examples. Select a mask and press F to focus it.');
};

function Download(Filename, Contents)
{
    const URL = window.URL.createObjectURL(new Blob([JSON.stringify(Contents)], { type: 'application/json' }));
    const Anchor = document.createElement('a');
    Anchor.href = URL;
    Anchor.download = Filename;
    Anchor.click();
    setTimeout(() => window.URL.revokeObjectURL(URL), 1000);
}
Find('#Export').onclick = () => Download('CoastalStudy.frontier.json', Document());
Find('#Import').onclick = () => Find('#File').click();
Find('#File').onchange = async Event =>
{
    try
    {
        const File = Event.target.files[0];
        if (!File) return;
        if (File.size > 5e6) throw Error('Document exceeds 5 MB.');
        const Imported = ValidateDocument(JSON.parse(await File.text()));
        Remember();
        Layers = Imported.Layers;
        Active = 0;
        SelectedLayers = new Set([0]);
        Panel.ResetSelection();
        Selection = null;
        Changed();
        Fit();
        Report('Vector document opened. Masks and generator stacks restored.');
    }
    catch (Failure) { Report(`Open failed: ${Failure.message}`); }
    Event.target.value = '';
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        VOLUMETRIC EXPORT
//------------------------------------------------------------------------------------------------------------------------

Find('#PreviewBake').onclick = async () =>
{
    if (!Layer().Regions.length) return;
    const Snapshot = structuredClone(Layer());
    const RegionIndex = Panel.RetrieveBoundary();
    const Control = Find('#PreviewBake');
    Control.disabled = true;
    Report('Sampling the selected 64³ signed volume…');
    await new Promise(Resolve => setTimeout(Resolve, 30));
    try
    {
        const Result = SampleSparseBricks(Snapshot, Snapshot.Regions[RegionIndex], 64, 64);
        Download('Landscape.sdf-preview.json', Result);
        Report(`Exported ${Result.Bricks.length} occupied 8³ SDF bricks. This is a 64³ preview, not the requested full resolution.`);
    }
    catch (Failure) { Report(`Preview sampling failed: ${Failure.message}`); }
    finally { Control.disabled = false; }
};

function FinishBaking()
{
    if (Baking) Baking.terminate();
    Baking = null;
    BakingChunks = [];
    Find('#Bake').disabled = !Layer().Regions.length;
    Find('#BakeProgress').hidden = true;
}
Find('#CancelBake').onclick = () => { FinishBaking(); Report('Volume bake cancelled. No partial export was saved.'); };
Find('#Bake').onclick = () =>
{
    if (!Layer().Visible || !Layer().Strokes.length || !Layer().Regions.length) { Report('Choose a painted region on a visible mask.'); return; }
    if (!window.CompressionStream) { Report('This browser does not support gzip streaming.'); return; }
    FinishBaking();
    const Snapshot = structuredClone(Layer()), RegionIndex = Panel.RetrieveBoundary();
    Baking = new Worker(new URL('./VolumeBaking.js', import.meta.url), { type: 'module' });
    Find('#Bake').disabled = true;
    Find('#BakeProgress').hidden = false;
    Find('#BakeMeter').value = 0;
    Find('#BakeRead').textContent = 'Preparing signed volume…';
    Find('#BakeResult').hidden = true;
    Report(`Baking ${Snapshot.Name}, region ${RegionIndex + 1}. Editing continues independently of this snapshot.`);
    const Sampling = Baking;
    Sampling.onmessage = Event =>
    {
        if (Baking !== Sampling) return;
        const Message = Event.data;
        if (Message.Category === 'chunk') BakingChunks.push(Message.Chunk);
        if (Message.Category === 'progress')
        {
            const Progress = Message.Progress;
            Find('#BakeMeter').value = Progress.Completion ?? Progress.Fraction;
            if (Progress.Phase === 'seeding') Find('#BakeRead').textContent = `Seeding 3D interfaces · ${(Progress.Fraction * 100).toFixed(0)}% · ${Progress.SeedCells.toLocaleString()} fixed cells`;
            else if (Progress.Phase === 'sweeping') Find('#BakeRead').textContent = `Metric sweep ${Progress.Cycle}/${Progress.MaximumCycles} · direction ${Progress.Direction}/8 · Δ ${Progress.MaximumChange.toPrecision(3)} m`;
            else if (Progress.Phase === 'redistanced') Find('#BakeRead').textContent = 'Metric field converged. Encoding sparse volume…';
            else Find('#BakeRead').textContent = `${Progress.Phase === 'encoding' ? 'Encoding' : 'Sampling'} ${(Progress.Fraction * 100).toFixed(1)}% · ${Progress.Bricks.toLocaleString()} bricks · ${(Progress.Bytes / 1048576).toFixed(1)} MiB`;
        }
        if (Message.Category === 'failure') { FinishBaking(); Report(`Bake failed: ${Message.Message}`); }
        if (Message.Category === 'complete')
        {
            const BlobURL = URL.createObjectURL(new Blob(BakingChunks, { type: 'application/gzip' }));
            const Anchor = document.createElement('a');
            Anchor.href = BlobURL;
            Anchor.download = 'Landscape.sdf.gz';
            Anchor.click();
            setTimeout(() => URL.revokeObjectURL(BlobURL), 10000);
            const Metric = Message.Redistance;
            Find('#BakeResult').hidden = false;
            Find('#BakeDistanceRead').textContent = Metric ? 'Metric TSDF' : 'Conservative bound';
            Find('#BakeSnapshotRead').textContent = `Exported snapshot: ${Snapshot.Name} · Region ${RegionIndex + 1}`;
            Find('#BakeDimensionsRead').textContent = `${Message.Dimensions.join(' × ')} cells · ${(Message.CompressedBytes / 1048576).toFixed(2)} MiB gzip`;
            Find('#BakeMetricDetails').hidden = !Metric;
            if (Metric)
            {
                Find('#SweepCyclesRead').textContent = Metric.Cycles;
                Find('#SignChangesRead').textContent = Metric.SignChanges;
                Find('#ConvergenceRead').textContent = `Converged · max change ${Metric.MaximumChange.toPrecision(3)} m ≤ ${Metric.Tolerance.toPrecision(3)} m tolerance. ${Metric.SeedCells.toLocaleString()} fixed interface cells. First-order grid approximation, not exact analytic distance.`;
            }
            const Summary = `Baked ${Metric ? 'metric TSDF' : 'signed bound'} ${Message.Dimensions.join(' × ')} cells · ${Message.Progress.Bricks.toLocaleString()} sampled bricks · ${(Message.CompressedBytes / 1048576).toFixed(2)} MiB gzip.`;
            FinishBaking();
            Report(Summary);
        }
    };
    Sampling.onerror = Event =>
    {
        if (Baking !== Sampling) return;
        FinishBaking();
        Report(`Background bake failed: ${Event.message}`);
    };
    Sampling.postMessage({ Layer: Snapshot, RegionIndex });
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        KEYBOARD AND ANIMATION
//------------------------------------------------------------------------------------------------------------------------

window.addEventListener('keydown', Event =>
{
    if (Find('#DocumentReplacement').open) return;
    if (Event.target.closest('input,select,textarea,[contenteditable="true"]')) return;
    if (Event.code === 'Space') { Space = true; Event.preventDefault(); }
    if ((Event.ctrlKey || Event.metaKey) && Event.key.toLowerCase() === 'z')
    {
        Event.preventDefault();
        Find(Event.shiftKey ? '#HistoryRedo' : '#HistoryUndo').click();
        return;
    }
    if (Event.shiftKey && Event.code === 'KeyA' && Workspace === 'landscape') { Event.preventDefault(); Panel.OpenMenu(); return; }
    if (Event.key === '1') SwitchWorkspace('mask');
    if (Event.key === '2') SwitchWorkspace('landscape');
    if (Workspace === 'mask')
    {
        const Tools = { b: 'paint', e: 'erase', r: 'select', h: 'pan' };
        if (Tools[Event.key.toLowerCase()]) SelectTool(Tools[Event.key.toLowerCase()]);
    }
    if (Event.key.toLowerCase() === 'f') Fit(!Event.shiftKey);
    if (Workspace === 'landscape' && Event.key.toLowerCase() === 'b') { Event.preventDefault(); void Sculpting.Toggle(); }
    if (Event.key === 'Escape') { Sculpting?.Disable(); Selection = null; Refresh(); Draw(); }
});
window.addEventListener('keyup', Event => { if (Event.code === 'Space') Space = false; });
window.addEventListener('blur', () => { Space = false; FinishGesture(); });

function RefreshOceanProfile(Seconds)
{
    if (InspectorSection !== 'ocean' || Seconds - LastOceanProfile < .2) return;
    LastOceanProfile = Seconds;
    const Points = Array.from({ length: 100 }, (_, Index) =>
    {
        const Elevation = EvaluateOcean(Index * 4, 40, OceanTime, Number(Find('#Wave').value)).Height;
        return `${Index * 3},${50 - Elevation * 16}`;
    });
    Find('#OceanProfile').innerHTML = `<svg viewBox="0 0 297 100" aria-label="Current procedural ocean cross-section"><path d="M0 50H297" stroke="#414a4c" stroke-width=".5"/><path d="M${Points.join(' L')} L297 100H0Z" fill="#44575c35"/><path d="M${Points.join(' L')}" stroke="#9baeb1" stroke-width="1" fill="none"/></svg>`;
}

function SubmitLandscapeView(Milliseconds)
{
    if (Workspace !== 'landscape' || !Rasterization) return;
    AutomaticWorking = Boolean(SelectedLattice && Layer().Visible && Camera.Orbit < Math.min(SelectedLattice.Pitch[0], SelectedLattice.Pitch[2]) * 64 * .85);
    Volume.dataset.automaticWorking = String(AutomaticWorking && !WorkingEnabled);
    try { Rasterization.Rasterize(Camera, Milliseconds / 1000, Number(Find('#Wave').value), Find('#Ocean').checked, OceanTime, Boolean(Gesture),
        { Enabled: WorkingEnabled || AutomaticWorking, LayerIndex: Active, RegionIndex: Panel.RetrieveBoundary(), Point: Camera.Target }); }
    catch (Failure) { Rasterization.Failed(Failure); }
    Find('#ScaleRead').textContent = `${Camera.Orbit >= 120 ? Math.round(Camera.Orbit / 12 / 10) * 10 : (Camera.Orbit / 12).toPrecision(2)} m`;
    const PreviewReadout = ['lost', 'failed'].includes(Volume.dataset.renderStatus) ? 'GPU stopped' :
        Volume.dataset.renderStatus === 'compiling' ? 'preparing GPU' : Volume.dataset.renderStatus === 'geometry' ?
            'preparing XYZ geometry' : `${Volume.dataset.displayedWidth || Volume.width} × ${Volume.dataset.displayedHeight || Volume.height} · ${Number(Volume.dataset.displayedSurfaceTriangles || 0).toLocaleString()} triangles`;
    Find('#RenderReadout').textContent = `WebGL2 · ${PreviewReadout}`;
    Find('#Coordinates').textContent = `TARGET  X ${Camera.Target[0].toFixed(0)} · Z ${Camera.Target[2].toFixed(0)}`;
    RenderWorkingReadout();
}

function EvaluateWorkspaceView(Milliseconds)
{
    const Seconds = Milliseconds / 1000;
    const Δτ = PreviousTime === null ? 0 : Math.max(0, Math.min(.1, Seconds - PreviousTime));
    PreviousTime = Seconds;
    if (Find('#AnimateOcean').checked) OceanTime += Δτ;
    Navigation.Step(Milliseconds);
    SubmitLandscapeView(Milliseconds);
    RefreshOceanProfile(Seconds);
}

function Animate(Milliseconds)
{
    LastAnimationTime = performance.now();
    Volume.dataset.viewSchedule = 'animation';
    EvaluateWorkspaceView(Milliseconds);
    requestAnimationFrame(Animate);
}

// 📝 A graphics/compositor stall can also stop animation callbacks. Startup and fallback must not depend on that clock.
setInterval(() =>
{
    if (Workspace !== 'landscape' || document.hidden || performance.now() - LastAnimationTime < 300) return;
    Volume.dataset.viewSchedule = 'timer';
    EvaluateWorkspaceView(performance.now());
}, 200);

InitializeGlyphs();
const Navigation = InitializeCameraNavigation({ Canvas: Volume, Camera, RetrieveWorkspace: () => Find('#DocumentReplacement').open ? 'confirmation' : Workspace });
const Panel = InitializeLayerStackPanel({
    RetrieveLayers: () => Layers, RetrieveActive: () => Active, RetrieveSelected: () => SelectedLayers,
    RetrieveWorkspace: () => Workspace, RetrieveInspector: () => InspectorSection,
    ReplaceLayers, Remember, Changed, Refresh, Report, ActivateInspector
});
Sculpting = InitializeVolumeSculpting({
    Canvas: Volume, Camera, RetrieveWorkspace: () => Workspace, RetrieveRevision: () => DocumentEditRevision,
    RetrieveSelection: () => ({ Layer: Layer(), Region: Layer().Regions[Panel.RetrieveBoundary()], LayerIndex: Active, RegionIndex: Panel.RetrieveBoundary() }),
    Inspect: Query => Rasterization.Inspect(Query), EnterDetail: () => FocusWorkingSurface(),
    ShowInspector: () => ActivateInspector('shape'), Report,
    FinalizeStroke: (Stroke, Parts) =>
    {
        try
        {
            const Candidate = structuredClone(Document());
            const Region = Candidate.Layers[Stroke.LayerIndex].Regions[Stroke.RegionIndex];
            Region.SculptStrokes = [...(Region.SculptStrokes || []), ...Parts];
            ValidateDocument(Candidate);
            Remember();
            Stroke.Region.SculptStrokes = [...(Stroke.Region.SculptStrokes || []), ...Parts];
            Changed();
            Report(`Saved ${Stroke.Mode === 'add' ? 'clay' : 'carve'} stroke in ${Stroke.Layer.Name}, region ${Stroke.RegionIndex + 1}. Ctrl+Z undoes the complete stroke. Other layers unchanged.`);
        }
        catch (Failure) { Report(`Sculpt stroke not applied: ${Failure.message}`); }
    }
});
new ResizeObserver(() => Draw()).observe(Find('#Viewport'));
Refresh();
SelectTool('paint');
ActivateInspector('shape');
SwitchWorkspace(new URLSearchParams(location.search).get('safePreview') === '1' ? 'landscape' : 'mask');
Fit();
if (new URLSearchParams(location.search).get('safePreview') === '1')
{
    try
    {
        const Saved = JSON.parse(sessionStorage.getItem('FrontierLandscapePreviewCamera'));
        if (Saved && [Saved.Yaw, Saved.Pitch, Saved.Orbit].every(Number.isFinite) && Saved.Orbit > 0 &&
            Array.isArray(Saved.Target) && Saved.Target.length === 3 && Saved.Target.every(Number.isFinite))
        {
            Camera.Yaw = Saved.Yaw; Camera.Pitch = Saved.Pitch; Camera.Orbit = Saved.Orbit; Camera.Target = Saved.Target;
        }
    }
    catch {}
}
requestAnimationFrame(Animate);

window.addEventListener('pagehide', () => Rasterization?.Suspend());
window.addEventListener('pageshow', () => { if (Workspace === 'landscape') Rasterization?.Resume(); });
document.addEventListener('visibilitychange', () =>
{
    if (document.hidden) Rasterization?.Suspend();
    else if (Workspace === 'landscape') Rasterization?.Resume();
});
