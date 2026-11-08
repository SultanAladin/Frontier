/*==========================================================================================================================================
                                                          VOLUMESUBMISSION.JS
==========================================================================================================================================*/
// 🧩 Dedicated-thread XYZ geometry, bounded region working bricks and GPU-only presentation. No CPU pixel rendering or automatic retries.

import { CreateVolumeRasterization } from './VolumeRasterization.js';
import { CompileSurfaceField, ConstructShoreDistance, SurfaceCapacityExceeded } from './VolumeSurface.js';
import { ConstructWorkingSurface, PlanWorkingSurface } from './VolumeDetail.js';
import { CompileVolumeRayField, ProjectVolumeCamera, ProjectVolumeRay, IntersectVolumeRay } from './VolumeRayField.js';
import { ConstructVolumeLattice } from './VolumeLattice.js';
import { ConstructHighSurface } from './VolumeRefinement.js';

let Surface = null, Rasterization = null, TelemetryTimer = null, Suspended = false, Stopped = false;
let LatestLayers = null, Revision = 0, Generation = 0, PreparationPending = false;
let Field = null, FieldRevision = -1, LatestView = null, DetailTimer = null, DetailSignature = '', FocusSignature = '', FocusPoint = null;
let Display = { Width: 0, Height: 0, PixelRatio: 1, Visible: false };

function Broadcast(Category, Content = {})
{
    self.postMessage({ Category, ...Content, Telemetry: Surface ? { ...Surface.dataset } : {} });
}

function GraphicsStatus(Status, Message)
{
    Broadcast('status', { Status, Message });
    if (['failed', 'lost'].includes(Status))
    {
        Stopped = true;
        ++Generation;
        clearTimeout(DetailTimer);
        clearInterval(TelemetryTimer);
    }
}

function Failed(Failure)
{
    if (Stopped) return;
    Stopped = true;
    ++Generation;
    clearTimeout(DetailTimer);
    clearInterval(TelemetryTimer);
    Rasterization?.Suspend();
    if (Surface)
    {
        Surface.dataset.renderStatus = 'failed';
        Surface.dataset.renderMessage = Failure.message;
    }
    Broadcast('status', { Status: 'failed', Message: Failure.message + ' No software fallback is started.' });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ONE-RAY SURFACE SELECTION
//------------------------------------------------------------------------------------------------------------------------

function RetrieveField()
{
    if (FieldRevision !== Revision) { Field = CompileSurfaceField(LatestLayers); FieldRevision = Revision; }
    return Field;
}

function PickSurface(Query)
{
    if (!LatestLayers || !LatestView) throw Error('Wait for the first landscape view before selecting a working point.');
    const Canonical = RetrieveField();
    const Selection = Canonical.Entries.find(Entry => Entry.LayerIndex === Query.LayerIndex &&
        Entry.Layer.Regions.indexOf(Entry.Region) === Query.RegionIndex);
    if (Query.SelectionOnly && !Selection) return { Found: false, Revision };
    const PickingField = Query.SelectionOnly ? CompileVolumeRayField(LatestLayers,
        { LayerIndex: Query.LayerIndex, RegionIndex: Query.RegionIndex }) : Canonical;
    const Camera = Query.Camera || LatestView.Camera, Projection = ProjectVolumeCamera(Camera);
    const Ray = ProjectVolumeRay(Projection, (Query.X ?? .5) * Display.Width - .5, (Query.Y ?? .5) * Display.Height - .5, Display.Width, Display.Height);
    let Hit = IntersectVolumeRay(PickingField, Projection.Eye, Ray, Math.max(64, Display.Height));
    if (!Hit.Found && Query.CentreFallback && Selection)
    {
        const Point = [(Selection.Lower[0] + Selection.Upper[0]) / 2, Selection.Upper[1] + 1, (Selection.Lower[2] + Selection.Upper[2]) / 2];
        Hit = IntersectVolumeRay(PickingField, Point, [0, -1, 0], Math.max(64, Display.Height));
        if (!Hit.Found)
        {
            const Stroke = Selection.Layer.Strokes.find(Entry => !Entry.Erase && Entry.Points.some(([X, Z]) =>
                X > Selection.Lower[0] && X < Selection.Upper[0] && Z > Selection.Lower[2] && Z < Selection.Upper[2]));
            const Inside = Stroke?.Points.find(([X, Z]) => X > Selection.Lower[0] && X < Selection.Upper[0] && Z > Selection.Lower[2] && Z < Selection.Upper[2]);
            if (Inside) Hit = IntersectVolumeRay(PickingField, [Inside[0], Point[1], Inside[1]], [0, -1, 0], Math.max(64, Display.Height));
        }
    }
    if (!Hit.Found) return { ...Hit, Revision };
    const Entry = Query.SelectionOnly && Selection ? Selection : Canonical.Entries.filter(Part => Part.LayerIndex === Hit.LayerIndex)
        .sort((First, Second) => Canonical.SampleEntry(First, ...Hit.Position) - Canonical.SampleEntry(Second, ...Hit.Position))[0];
    if (!Entry) return { Found: false, Revision };
    const RegionIndex = Entry.Layer.Regions.indexOf(Entry.Region);
    return { ...Hit, LayerIndex: Entry.LayerIndex, RegionIndex, Revision,
        Lattice: ConstructVolumeLattice(Entry.Layer, Entry.Region) };
}

function ResolveWorkingRequest()
{
    if (!LatestView?.Detail?.Enabled) return null;
    const Detail = LatestView.Detail;
    const Signature = JSON.stringify([Revision, Detail.LayerIndex, Detail.RegionIndex, Detail.Point]);
    if (Signature !== FocusSignature)
    {
        const Hit = PickSurface({ SelectionOnly: true, LayerIndex: Detail.LayerIndex, RegionIndex: Detail.RegionIndex });
        FocusPoint = Hit.Found ? Hit.Position : Detail.Point;
        FocusSignature = Signature;
    }
    return { LayerIndex: Detail.LayerIndex, RegionIndex: Detail.RegionIndex, Point: FocusPoint,
        Distance: LatestView.Camera.Orbit, PixelHeight: Display.Height, Force: true };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ALWAYS-HIGH AND WORKING DETAIL
//------------------------------------------------------------------------------------------------------------------------

async function PrepareSurface(DetailOnly = false, WorkingRequest = null)
{
    if (!LatestLayers || Suspended || Stopped) return;
    PreparationPending = false;
    const Epoch = ++Generation, SceneRevision = Revision;
    const Current = () => !Stopped && !Suspended && Epoch === Generation;
    Surface.dataset.geometryPreparationEpoch = String(Epoch);
    const Upload = Prepared => { Prepared.PreparationEpoch = Epoch; Rasterization.UploadSurface(Prepared, SceneRevision); };
    try
    {
        const Canonical = RetrieveField();
        const Progress = Part =>
        {
            if (!Current()) return;
            Surface.dataset.geometrySamples = String(Part.Completed);
            Surface.dataset.geometrySampleBudget = String(Part.Samples);
            Broadcast('progress', { Fraction: Math.min(.95, Part.Completed / Math.max(1, Part.Samples)), Revision: SceneRevision });
        };
        const Work = WorkingRequest || ResolveWorkingRequest();
        let CompletedQuality = 96, CompletedKey = '';
        if (Work)
        {
            const Plan = PlanWorkingSurface(Canonical, Work);
            Surface.dataset.requestedDetailKey = Plan.Key;
            const Detailed = await ConstructWorkingSurface(Canonical, { ...Work, Key: Plan.Key }, { Current, Progress });
            if (!Current()) return;
            Upload(Detailed);
            CompletedQuality = Detailed.Quality;
            CompletedKey = Plan.Key;
            Surface.dataset.surfaceDetailLimited = Detailed.Working && !Detailed.Working.Resident ? 'triangle-budget' :
                Detailed.Working?.Limited ? 'working-coverage-budget' : 'false';
        }
        else
        {
            Surface.dataset.requestedDetailKey = '';
            const Detailed = await ConstructHighSurface(Canonical, { Current, Progress, Camera: LatestView?.Camera });
            if (!Current()) return;
            Upload(Detailed);
            Surface.dataset.surfaceDetailLimited = Detailed.Refinement.Limited ? 'adaptive-budget' : 'false';
        }
        if (!DetailOnly)
        {
            const Shore = await ConstructShoreDistance(Canonical, { Current });
            if (!Current()) return;
            Rasterization.UploadShore(Shore, SceneRevision);
        }
        Surface.dataset.geometryCompletedEpoch = String(Epoch);
        Surface.dataset.geometryCompletedQuality = String(CompletedQuality);
        Surface.dataset.geometryCompletedRevision = String(SceneRevision);
        Surface.dataset.geometryCompletedDetailKey = CompletedKey;
        Broadcast('telemetry');
    }
    catch (Failure)
    {
        if (Failure instanceof SurfaceCapacityExceeded && Current())
        {
            Surface.dataset.surfaceDetailLimited = 'triangle-budget';
            Broadcast('status', { Status: 'refining', Message: 'Working detail reached its triangle budget. The last GPU surface is retained; reduce complexity or choose a smaller region.' });
        }
        else if (Failure.name !== 'AbortError' && Current()) Failed(Failure);
    }
}

function QueueWorkingDetail()
{
    if (Suspended || Stopped || !LatestLayers || LatestView.Interacting ||
        Surface.dataset.geometryCompletedRevision !== String(Revision)) return;
    const Signature = JSON.stringify([Revision, LatestView.Detail?.Enabled || false,
        LatestView.Detail?.LayerIndex, LatestView.Detail?.RegionIndex, LatestView.Detail?.Point]);
    if (Signature === DetailSignature) return;
    DetailSignature = Signature;
    clearTimeout(DetailTimer);
    if (!LatestView.Detail?.Enabled && !Surface.dataset.geometryCompletedDetailKey && !Surface.dataset.requestedDetailKey) return;
    DetailTimer = setTimeout(() =>
    {
        if (Suspended || Stopped) return;
        try
        {
            const Request = ResolveWorkingRequest();
            const Key = Request ? PlanWorkingSurface(RetrieveField(), Request).Key : '';
            if (Surface.dataset.geometryCompletedRevision === String(Revision) && Surface.dataset.geometryCompletedDetailKey === Key &&
                Surface.dataset.requestedDetailKey === Key && Surface.dataset.geometryPreparationEpoch === Surface.dataset.geometryCompletedEpoch) return;
            Surface.dataset.requestedDetailKey = Key;
            Broadcast('status', { Status: 'refining', Message: 'Preparing a bounded XYZ working brick at this region’s authored spacing.' });
            void PrepareSurface(true, Request);
        }
        catch (Failure) { Failed(Failure); }
    }, 180);
}

self.onmessage = async Event =>
{
    const Request = Event.data;
    try
    {
        if (Stopped && Request.Category !== 'dispose') throw Error('GPU preview is stopped; explicit restart is required.');
        if (Request.Category === 'initialize')
        {
            if (Rasterization) throw Error('The landscape already owns a rendering thread.');
            if (!Request.Surface) throw Error('OffscreenCanvas is required for the GPU-only preview.');
            Surface = Request.Surface;
            Surface.dataset = { renderExecution: 'thread' };
            Rasterization = CreateVolumeRasterization(Surface, {
                Display: () => Display, Status: GraphicsStatus,
                Progress: Fraction => Broadcast('progress', { Fraction, Revision })
            });
            TelemetryTimer = setInterval(() => Broadcast('telemetry'), 100);
        }
        else if (Request.Category === 'upload')
        {
            ++Generation;
            clearTimeout(DetailTimer);
            DetailSignature = '';
            Revision = Request.Revision;
            LatestLayers = Request.Layers;
            Surface.dataset.volumeRevision = String(Revision);
            Surface.dataset.geometrySamples = '0';
            PreparationPending = true;
            Broadcast('status', { Status: Number(Surface.dataset.terrainRenders) ? 'refining' : 'geometry',
                Message: 'Preparing XYZ surface geometry off-thread. Camera images are GPU rasterized.' });
            void PrepareSurface();
        }
        else if (Request.Category === 'view')
        {
            Display = Request.Display;
            LatestView = Request;
            Surface.dataset.previewWidth = Request.PreviewWidth;
            Rasterization.Rasterize(Request.Camera, Request.Seconds, Request.Wave, Request.Ocean, Request.OceanTime, Request.Interacting);
            QueueWorkingDetail();
        }
        else if (Request.Category === 'suspend')
        {
            Suspended = true;
            ++Generation;
            clearTimeout(DetailTimer);
            DetailSignature = '';
            PreparationPending = Boolean(LatestLayers && (Surface.dataset.geometryCompletedRevision !== String(Revision) ||
                (Surface.dataset.requestedDetailKey || '') !== (Surface.dataset.geometryCompletedDetailKey || '')));
            Rasterization?.Suspend();
            Broadcast('telemetry');
        }
        else if (Request.Category === 'resume')
        {
            Suspended = false;
            Rasterization?.Resume();
            if (PreparationPending) void PrepareSurface();
            Broadcast('telemetry');
        }
        else if (Request.Category === 'inspect')
        {
            const Result = Request.Query?.Category === 'pick' ? PickSurface(Request.Query) : await Rasterization.Inspect(Request.Query);
            const Transfer = Result.Pixels ? [Result.Pixels.buffer] : [];
            self.postMessage({ Category: 'inspection', Token: Request.Token, Result }, Transfer);
        }
        else if (Request.Category === 'dispose')
        {
            Stopped = true;
            ++Generation;
            clearTimeout(DetailTimer);
            clearInterval(TelemetryTimer);
            Rasterization?.Dispose();
            self.close();
        }
    }
    catch (Failure)
    {
        if (Request.Category === 'inspect') self.postMessage({ Category: 'inspection', Token: Request.Token, Failure: Failure.message });
        else Failed(Failure);
    }
    finally
    {
        if (Request.Category === 'view') self.postMessage({ Category: 'view-complete' });
    }
};
