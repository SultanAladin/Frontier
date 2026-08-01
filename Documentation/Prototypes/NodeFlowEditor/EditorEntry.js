/*====================================================================================================================================
                                                        EDITORENTRY.JS
====================================================================================================================================*/
// 🧩 Editor assembly — plane pan/zoom, entry dragging, port linking, chrome wiring, viewport feed

import { ComposeGlyph } from './Presentation/GlyphOutlines.js';
import { TerrainTokens, CompactEntryCondition } from './Graph/CatalogueSpecifications.js';
import { RasterizeEntry, EscapeMarkup } from './Presentation/EntryRasterization.js';
import { FormulateLinkPath } from './Graph/LinkRouting.js';
import
{
    RevealCatalogueMenu, ConcealCatalogueMenu, CatalogueMenuRevealedCondition
} from './Presentation/CatalogueMenu.js';
import
{
    InitializeTerrainViewport, ReconfigureTerrain, ReconfigureShading,
    ReconfigureCameraMode, ReconfigureResolution, ReconfigureEnvironment
} from './Terrain/TerrainViewport.js';
import
{
    RecordStore, RetrieveEntry, RetrieveChosenEntry, RetrievePort,
    ConstructOriginEntry, ConstructCatalogueEntry,
    AttachInboundPort, AttachOutboundPort, DetachInboundPort, DetachOutboundPort,
    ReclaimEntry, IntegrateLink, ReclaimLink, AdmissibleLinkCondition, ProvokeRebuild
} from './Graph/GraphStore.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const MinimumPlaneScale = 0.2;    // [-]  - Furthest zoom-out
const MaximumPlaneScale = 2.5;    // [-]  - Closest zoom-in
const ZoomIncrement     = 1.12;   // [-]  - Multiplier per wheel notch
const DragThreshold     = 3;      // [px] - Pointer travel before a press becomes a drag
const GlideResponse     = 0.22;   // [-]  - Fraction of remaining distance closed per frame
const GlideEpsilon      = 0.0005; // [-]  - Residual below which the glide is considered settled

//------------------------------------------------------------------------------------------------------------------------
//                                                    ELEMENT RETRIEVAL
//------------------------------------------------------------------------------------------------------------------------

const GraphRegion    = document.getElementById('GraphRegion');
const PanSurface     = document.getElementById('PanSurface');
const TransformPlane = document.getElementById('TransformPlane');
const EdgeCanvas     = document.getElementById('EdgeCanvas');
const EntryPlane     = document.getElementById('EntryPlane');
const SelectionBand  = document.getElementById('SelectionBand');
const ViewportRegion = document.getElementById('ViewportRegion');
const ProgressAnchor = document.getElementById('ProgressAnchor');
const ControlAnchor  = document.getElementById('ControlAnchor');

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL STATE
//------------------------------------------------------------------------------------------------------------------------

let DraftLinkPath  = null;   // In-flight link being dragged from a port
let LinkInFlight   = null;   // { Entry, Port, Direction, Classification }
let EntryDragState = null;
let PlanePanState  = null;
let BandState      = null;
let GlideTarget    = null;   // { Scale, OffsetX, OffsetY } the plane is easing toward
let GlideFrame     = null;   // rAF token for the glide in flight

//------------------------------------------------------------------------------------------------------------------------
//                                                  COORDINATE TRANSFORM
//------------------------------------------------------------------------------------------------------------------------

// Project a client-space point into graph-plane space.
function ProjectToPlane(ClientX, ClientY)
{
    const SurfaceBounds = PanSurface.getBoundingClientRect();
    return {
        PlaneX: (ClientX - SurfaceBounds.left - RecordStore.PlaneOffsetX) / RecordStore.PlaneScale,
        PlaneY: (ClientY - SurfaceBounds.top  - RecordStore.PlaneOffsetY) / RecordStore.PlaneScale
    };
}

function AlignPlaneTransform()
{
    TransformPlane.style.transform =
        `translate(${RecordStore.PlaneOffsetX}px, ${RecordStore.PlaneOffsetY}px) scale(${RecordStore.PlaneScale})`;

    // 📝 The dot grid lives on the un-transformed surface, so its spacing and phase are tracked by hand
    //    to stay locked to plane space through pan and zoom.
    const GridSpacing = 32 * RecordStore.PlaneScale;
    PanSurface.style.backgroundSize = `${GridSpacing}px ${GridSpacing}px`;
    PanSurface.style.backgroundPosition =
        `${RecordStore.PlaneOffsetX}px ${RecordStore.PlaneOffsetY}px`;
}

// Resolve a port's centre in plane space from its rendered dot.
function ResolvePortAnchor(EntryToken, PortToken)
{
    const PortDot = EntryPlane.querySelector(
        `[data-entry="${EntryToken}"][data-port="${PortToken}"]`);
    if (!PortDot) return null;

    const DotBounds     = PortDot.getBoundingClientRect();
    const SurfaceBounds = PanSurface.getBoundingClientRect();

    return {
        PlaneX: (DotBounds.left + DotBounds.width  / 2 - SurfaceBounds.left - RecordStore.PlaneOffsetX) / RecordStore.PlaneScale,
        PlaneY: (DotBounds.top  + DotBounds.height / 2 - SurfaceBounds.top  - RecordStore.PlaneOffsetY) / RecordStore.PlaneScale
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LINK INSCRIPTION
//------------------------------------------------------------------------------------------------------------------------

function InscribeLinks()
{
    while (EdgeCanvas.firstChild) EdgeCanvas.removeChild(EdgeCanvas.firstChild);

    for (const Link of RecordStore.Links)
    {
        const Origin  = ResolvePortAnchor(Link.SourceEntry, Link.SourcePort);
        const Arrival = ResolvePortAnchor(Link.TargetEntry, Link.TargetPort);
        if (!Origin || !Arrival) continue;

        const PathData = FormulateLinkPath(Origin.PlaneX, Origin.PlaneY, Arrival.PlaneX, Arrival.PlaneY);

        // 📝 A transparent wide halo under each curve gives the 2 px stroke a clickable target.
        const Halo = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        Halo.setAttribute('class', 'EdgeHalo');
        Halo.setAttribute('d', PathData);
        Halo.dataset.link = Link.Token;
        EdgeCanvas.appendChild(Halo);

        const Curve = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        Curve.setAttribute('class', 'EdgeCurve');
        Curve.setAttribute('d', PathData);
        Curve.dataset.link = Link.Token;
        EdgeCanvas.appendChild(Curve);
    }

    if (DraftLinkPath) EdgeCanvas.appendChild(DraftLinkPath);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY REFRESH
//------------------------------------------------------------------------------------------------------------------------

function RefreshEntries()
{
    while (EntryPlane.firstChild) EntryPlane.removeChild(EntryPlane.firstChild);
    for (const Entry of RecordStore.Entries) EntryPlane.appendChild(RasterizeEntry(Entry));
    InscribeLinks();
}

// Full refresh of every surface that reads the record store.
function RefreshEditor()
{
    RefreshEntries();
    RefreshProgressPill();
    FeedTerrainViewport();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ENTRY CHOICE
//------------------------------------------------------------------------------------------------------------------------

function ChooseEntry(EntryToken, AdditiveCondition)
{
    for (const Entry of RecordStore.Entries)
    {
        if (Entry.Token === EntryToken)          Entry.Chosen = true;
        else if (!AdditiveCondition)             Entry.Chosen = false;
    }
    RefreshEditor();
}

function DiscardChoice()
{
    for (const Entry of RecordStore.Entries) Entry.Chosen = false;
    RefreshEditor();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  TERRAIN PARAMETER FEED
//------------------------------------------------------------------------------------------------------------------------

// 📝 Resolve one generator parameter: an incoming link from a scalar regulator wins over a typed-in
//    port scalar, which in turn wins over the default.
function ResolveGeneratorParameter(Entry, PortLabel, DefaultMagnitude)
{
    const Port = Entry.InboundPorts.find((Candidate) => Candidate.Label === PortLabel);
    if (!Port) return DefaultMagnitude;

    let RawMagnitude = Entry.PortScalars[Port.Token];

    const ArrivingLink = RecordStore.Links.find(
        (Link) => Link.TargetEntry === Entry.Token && Link.TargetPort === Port.Token);
    if (ArrivingLink)
    {
        const SourceEntry = RetrieveEntry(ArrivingLink.SourceEntry);
        if (SourceEntry && SourceEntry.CatalogueToken && SourceEntry.CatalogueToken.startsWith('input_'))
        {
            RawMagnitude = SourceEntry.PortScalars['val'];
        }
    }

    if (RawMagnitude === undefined || RawMagnitude === '') return DefaultMagnitude;
    const Parsed = parseFloat(RawMagnitude);
    return Number.isFinite(Parsed) ? Parsed : DefaultMagnitude;
}

// 📝 Choosing a scalar regulator that feeds a generator previews the generator, not the bare number —
//    otherwise dragging a slider would blank the viewport.
function ResolvePreviewEntry(ChosenEntry)
{
    if (!ChosenEntry) return null;
    if (TerrainTokens.includes(ChosenEntry.CatalogueToken)) return ChosenEntry;

    if (ChosenEntry.CatalogueToken && ChosenEntry.CatalogueToken.startsWith('input_'))
    {
        const DepartingLink = RecordStore.Links.find((Link) => Link.SourceEntry === ChosenEntry.Token);
        if (DepartingLink)
        {
            const Downstream = RetrieveEntry(DepartingLink.TargetEntry);
            if (Downstream && TerrainTokens.includes(Downstream.CatalogueToken)) return Downstream;
        }
    }
    return ChosenEntry;
}

function FeedTerrainViewport()
{
    if (RecordStore.ViewportSide === 'hidden') return;

    const PreviewEntry = ResolvePreviewEntry(RetrieveChosenEntry());
    const GeneratorToken = (PreviewEntry && TerrainTokens.includes(PreviewEntry.CatalogueToken))
        ? PreviewEntry.CatalogueToken : 'default';

    if (GeneratorToken === 'default')
    {
        // 📝 The readout reports the specification actually handed to the viewport rather than
        //    re-deriving it, so a change to the resolution rules above cannot leave the panel
        //    quietly describing parameters nothing is rendering.
        DispatchedSpecification = {
            GeneratorToken: 'default',
            NoiseScale: 0.1, Amplitude: 2.0, Octaves: 4,
            Persistence: 0.5, Lacunarity: 2.0, FieldSeed: 0
        };
        ReconfigureTerrain(DispatchedSpecification);
        return;
    }

    // Ridged and multifractal generators open at a coarser, taller default than the smooth ones.
    const CoarseCondition = ['gen_multifractal', 'gen_cellular'].includes(GeneratorToken);
    const DefaultScale     = CoarseCondition ? 0.5 : 0.2;
    const DefaultAmplitude = CoarseCondition ? 6.0 : 4.0;

    DispatchedSpecification = {
        GeneratorToken: GeneratorToken,
        NoiseScale:  ResolveGeneratorParameter(PreviewEntry, 'Scale',       DefaultScale),
        Amplitude:   DefaultAmplitude,
        Octaves:     ResolveGeneratorParameter(PreviewEntry, 'Octaves',     4),
        Persistence: ResolveGeneratorParameter(PreviewEntry, 'Persistence', 0.5),
        Lacunarity:  ResolveGeneratorParameter(PreviewEntry, 'Lacunarity',  2.0),
        FieldSeed:   ResolveGeneratorParameter(PreviewEntry, 'Seed',        0)
    };
    DispatchedSpecification.PreviewNaming = PreviewEntry ? PreviewEntry.Title : '—';

    ReconfigureTerrain(DispatchedSpecification);
}

// The specification last handed to the viewport, kept so a re-feed can reuse it verbatim.
let DispatchedSpecification = null;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROGRESS PILL
//------------------------------------------------------------------------------------------------------------------------

function RefreshProgressPill()
{
    const ChosenEntry = RetrieveChosenEntry();
    if (!ChosenEntry) { ProgressAnchor.innerHTML = ''; return; }

    const BuildCondition = ChosenEntry.BuildCondition || 'built';
    let ProgressTally = ChosenEntry.BuildProgress ?? 100;
    let FillClass = '';
    let ConditionNaming = 'Built';

    if (BuildCondition === 'building')   { FillClass = 'Building'; ConditionNaming = 'Building...'; ProgressTally = 45; }
    else if (BuildCondition === 'failed'){ FillClass = 'Failed';   ConditionNaming = 'Failed'; }
    else                                 { ProgressTally = 100; }

    ProgressAnchor.innerHTML =
        `<div class="ProgressPill">`
      +   `<div class="ProgressStack">`
      +     `<div class="ProgressRow">`
      +       `<span class="ProgressNaming">${ConditionNaming}</span>`
      +       `<span class="ProgressTally ${BuildCondition === 'failed' ? 'Failed' : ''}">${ProgressTally}%</span>`
      +     `</div>`
      +     `<div class="ProgressTrack">`
      +       `<div class="ProgressFill ${FillClass}" style="width:${ProgressTally}%"></div>`
      +     `</div>`
      +   `</div>`
      + `</div>`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CANVAS CONTROLS
//------------------------------------------------------------------------------------------------------------------------

function RefreshCanvasControls()
{
    if (!RecordStore.ControlsRevealed) { ControlAnchor.innerHTML = ''; return; }

    const LockGlyph = RecordStore.CanvasImmobilized ? 'Fastened' : 'Unfastened';
    const LockHint  = RecordStore.CanvasImmobilized ? 'Unlock Canvas' : 'Lock Canvas';

    ControlAnchor.innerHTML =
        `<div class="ControlStack">`
      +   `<div class="SideHint HintLeading" data-hint="Zoom In">`
      +     `<button class="ControlAction" data-control="ZoomIn">${ComposeGlyph('Plus', 14)}</button></div>`
      +   `<div class="SideHint HintLeading" data-hint="Zoom Out">`
      +     `<button class="ControlAction" data-control="ZoomOut">${ComposeGlyph('Minus', 14)}</button></div>`
      +   `<div class="SideHint HintLeading" data-hint="Fit to Selected / All">`
      +     `<button class="ControlAction" data-control="Encompass">${ComposeGlyph('Encompass', 14)}</button></div>`
      +   `<div class="ControlRule"></div>`
      +   `<div class="SideHint HintLeading" data-hint="${LockHint}">`
      +     `<button class="ControlAction ${RecordStore.CanvasImmobilized ? 'Engaged' : ''}" `
      +     `data-control="Immobilize">${ComposeGlyph(LockGlyph, 14)}</button></div>`
      + `</div>`;
}

// 📝 Zoom settles toward a target rather than snapping. Offset and scale are eased on the same
//    curve, because easing the scale alone would let the pivot point slide out from under the
//    cursor mid-glide. Nothing animates while a pointer drag is live — a pan must track the hand
//    exactly, so pans write the transform directly and cancel any glide in flight.
function GlideToTransform(TargetScale, TargetOffsetX, TargetOffsetY)
{
    GlideTarget = { Scale: TargetScale, OffsetX: TargetOffsetX, OffsetY: TargetOffsetY };
    if (GlideFrame !== null) return;

    const AdvanceGlide = () =>
    {
        if (!GlideTarget) { GlideFrame = null; return; }

        const ScaleResidual   = GlideTarget.Scale   - RecordStore.PlaneScale;
        const OffsetXResidual = GlideTarget.OffsetX - RecordStore.PlaneOffsetX;
        const OffsetYResidual = GlideTarget.OffsetY - RecordStore.PlaneOffsetY;

        const SettledCondition = Math.abs(ScaleResidual) < GlideEpsilon
                              && Math.abs(OffsetXResidual) < 0.5
                              && Math.abs(OffsetYResidual) < 0.5;

        if (SettledCondition)
        {
            RecordStore.PlaneScale   = GlideTarget.Scale;
            RecordStore.PlaneOffsetX = GlideTarget.OffsetX;
            RecordStore.PlaneOffsetY = GlideTarget.OffsetY;
            GlideTarget = null;
            GlideFrame  = null;
            AlignPlaneTransform();
            InscribeLinks();
            return;
        }

        RecordStore.PlaneScale   += ScaleResidual   * GlideResponse;
        RecordStore.PlaneOffsetX += OffsetXResidual * GlideResponse;
        RecordStore.PlaneOffsetY += OffsetYResidual * GlideResponse;

        AlignPlaneTransform();
        InscribeLinks();

        GlideFrame = requestAnimationFrame(AdvanceGlide);
    };

    GlideFrame = requestAnimationFrame(AdvanceGlide);
}

// Abandon any glide in flight, leaving the plane wherever it currently sits.
function AbandonGlide()
{
    if (GlideFrame !== null) cancelAnimationFrame(GlideFrame);
    GlideFrame  = null;
    GlideTarget = null;
}

function ApplyZoomAboutPoint(TargetScale, PivotClientX, PivotClientY, GlideCondition)
{
    const ClampedScale = Math.max(MinimumPlaneScale, Math.min(MaximumPlaneScale, TargetScale));
    const SurfaceBounds = PanSurface.getBoundingClientRect();

    const PivotSurfaceX = PivotClientX - SurfaceBounds.left;
    const PivotSurfaceY = PivotClientY - SurfaceBounds.top;

    // 📝 Hold the plane point under the pivot fixed across the scale change. The pivot is resolved
    //    against the glide target when one is in flight, so a fast wheel spin compounds toward the
    //    intended scale instead of fighting the partially-eased current one.
    const ReferenceScale   = GlideTarget ? GlideTarget.Scale   : RecordStore.PlaneScale;
    const ReferenceOffsetX = GlideTarget ? GlideTarget.OffsetX : RecordStore.PlaneOffsetX;
    const ReferenceOffsetY = GlideTarget ? GlideTarget.OffsetY : RecordStore.PlaneOffsetY;

    const PlaneX = (PivotSurfaceX - ReferenceOffsetX) / ReferenceScale;
    const PlaneY = (PivotSurfaceY - ReferenceOffsetY) / ReferenceScale;

    const ArrivalOffsetX = PivotSurfaceX - PlaneX * ClampedScale;
    const ArrivalOffsetY = PivotSurfaceY - PlaneY * ClampedScale;

    if (GlideCondition === false)
    {
        AbandonGlide();
        RecordStore.PlaneScale   = ClampedScale;
        RecordStore.PlaneOffsetX = ArrivalOffsetX;
        RecordStore.PlaneOffsetY = ArrivalOffsetY;
        AlignPlaneTransform();
        return;
    }

    GlideToTransform(ClampedScale, ArrivalOffsetX, ArrivalOffsetY);
}

// Frame either the chosen entries or the whole graph.
function EncompassEntries()
{
    const Candidates = RecordStore.Entries.filter((Entry) => Entry.Chosen);
    const Framed = Candidates.length > 0 ? Candidates : RecordStore.Entries;
    if (Framed.length === 0) return;

    let MinimumX = Infinity, MinimumY = Infinity, MaximumX = -Infinity, MaximumY = -Infinity;
    for (const Entry of Framed)
    {
        const Wrapper = EntryPlane.querySelector(`.NodeEntry[data-entry="${Entry.Token}"]`);
        const EntryWidth  = Wrapper ? Wrapper.offsetWidth  : 220;
        const EntryHeight = Wrapper ? Wrapper.offsetHeight : 140;
        MinimumX = Math.min(MinimumX, Entry.PlaneX);
        MinimumY = Math.min(MinimumY, Entry.PlaneY);
        MaximumX = Math.max(MaximumX, Entry.PlaneX + EntryWidth);
        MaximumY = Math.max(MaximumY, Entry.PlaneY + EntryHeight);
    }

    const SurfaceBounds = PanSurface.getBoundingClientRect();
    const Padding = 80;
    const SpanX = Math.max(1, MaximumX - MinimumX);
    const SpanY = Math.max(1, MaximumY - MinimumY);

    const FittedScale = Math.max(MinimumPlaneScale, Math.min(MaximumPlaneScale, 0.7,
        Math.min((SurfaceBounds.width - Padding * 2) / SpanX, (SurfaceBounds.height - Padding * 2) / SpanY)));

    // Framing glides as well, so pressing F reads as the view travelling rather than teleporting.
    GlideToTransform(
        FittedScale,
        SurfaceBounds.width  / 2 - (MinimumX + SpanX / 2) * FittedScale,
        SurfaceBounds.height / 2 - (MinimumY + SpanY / 2) * FittedScale);
}

// Frame the graph with no animation — used at startup, where a glide from a cold transform
// would read as the editor lurching into place on load.
function EncompassEntriesAbruptly()
{
    EncompassEntries();

    // EncompassEntries queues a glide; adopt its target immediately and drop the animation.
    const Target = GlideTarget;
    AbandonGlide();
    if (Target)
    {
        RecordStore.PlaneScale   = Target.Scale;
        RecordStore.PlaneOffsetX = Target.OffsetX;
        RecordStore.PlaneOffsetY = Target.OffsetY;
    }

    AlignPlaneTransform();
    InscribeLinks();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY SPAWNING
//------------------------------------------------------------------------------------------------------------------------

// Spawn a catalogue item and, when a link was in flight, terminate that link on the new entry.
function SpawnCatalogueEntry(CatalogueItem, Category, ScreenX, ScreenY, PendingLink)
{
    const PlanePoint = ProjectToPlane(ScreenX, ScreenY);
    const SpawnedEntry = ConstructCatalogueEntry(CatalogueItem, Category, PlanePoint.PlaneX, PlanePoint.PlaneY);
    RecordStore.Entries.push(SpawnedEntry);

    // 📝 Ports must exist in the DOM before a link can be routed to them, so render first.
    RefreshEntries();

    if (PendingLink)
    {
        if (PendingLink.PortDirection === 'source' && SpawnedEntry.InboundPorts.length > 0)
        {
            IntegrateLink(PendingLink.Entry, PendingLink.Port,
                          SpawnedEntry.Token, SpawnedEntry.InboundPorts[0].Token);
        }
        else if (PendingLink.PortDirection === 'target' && SpawnedEntry.OutboundPorts.length > 0)
        {
            IntegrateLink(SpawnedEntry.Token, SpawnedEntry.OutboundPorts[0].Token,
                          PendingLink.Entry, PendingLink.Port);
        }
    }

    ChooseEntry(SpawnedEntry.Token, false);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PORT LINK DRAGGING
//------------------------------------------------------------------------------------------------------------------------

function CommencePortDrag(PortDot, PointerPress)
{
    if (RecordStore.CanvasImmobilized) return;

    const EntryToken = PortDot.dataset.entry;
    const PortToken  = PortDot.dataset.port;
    const PortDirection = PortDot.dataset.porthit;
    const Port = RetrievePort(EntryToken, PortToken);

    LinkInFlight = {
        Entry: EntryToken,
        Port: PortToken,
        PortDirection: PortDirection,
        PortClassification: (Port && Port.Classification) || 'any'
    };

    DraftLinkPath = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    DraftLinkPath.setAttribute('class', 'EdgeDraft');
    EdgeCanvas.appendChild(DraftLinkPath);

    AdvancePortDrag(PointerPress);
    PanSurface.setPointerCapture(PointerPress.pointerId);
}

function AdvancePortDrag(PointerMotion)
{
    if (!LinkInFlight || !DraftLinkPath) return;

    const Anchor = ResolvePortAnchor(LinkInFlight.Entry, LinkInFlight.Port);
    if (!Anchor) return;
    const Cursor = ProjectToPlane(PointerMotion.clientX, PointerMotion.clientY);

    // Draw in the true direction of flow so the elbow shape matches a settled link.
    const PathData = LinkInFlight.PortDirection === 'source'
        ? FormulateLinkPath(Anchor.PlaneX, Anchor.PlaneY, Cursor.PlaneX, Cursor.PlaneY)
        : FormulateLinkPath(Cursor.PlaneX, Cursor.PlaneY, Anchor.PlaneX, Anchor.PlaneY);
    DraftLinkPath.setAttribute('d', PathData);
}

function ConcludePortDrag(PointerRelease)
{
    if (!LinkInFlight) return;

    const Pending = LinkInFlight;
    LinkInFlight = null;
    if (DraftLinkPath) { DraftLinkPath.remove(); DraftLinkPath = null; }

    // 📝 The pointer is captured by the surface, so hit-test the release point explicitly.
    const ReleaseTarget = document.elementFromPoint(PointerRelease.clientX, PointerRelease.clientY);
    const ArrivalDot = ReleaseTarget && ReleaseTarget.closest('[data-porthit]');

    if (ArrivalDot)
    {
        const ArrivalDirection = ArrivalDot.dataset.porthit;
        if (ArrivalDirection !== Pending.PortDirection)
        {
            const SourceEntry = Pending.PortDirection === 'source' ? Pending.Entry : ArrivalDot.dataset.entry;
            const SourcePort  = Pending.PortDirection === 'source' ? Pending.Port  : ArrivalDot.dataset.port;
            const TargetEntry = Pending.PortDirection === 'source' ? ArrivalDot.dataset.entry : Pending.Entry;
            const TargetPort  = Pending.PortDirection === 'source' ? ArrivalDot.dataset.port  : Pending.Port;
            IntegrateLink(SourceEntry, SourcePort, TargetEntry, TargetPort);
        }
        RefreshEditor();
        return;
    }

    // Released over empty canvas — offer the compatible slice of the catalogue.
    const OverSurface = ReleaseTarget === PanSurface || ReleaseTarget === TransformPlane
                     || (ReleaseTarget && ReleaseTarget.closest('.PanSurface') && !ReleaseTarget.closest('.NodeEntry'));

    if (OverSurface)
    {
        RevealCatalogueMenu(PointerRelease.clientX, PointerRelease.clientY, Pending, SpawnCatalogueEntry);
    }
    RefreshEditor();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY DRAGGING
//------------------------------------------------------------------------------------------------------------------------

function CommenceEntryDrag(Wrapper, PointerPress)
{
    const Entry = RetrieveEntry(Wrapper.dataset.entry);
    if (!Entry || Entry.Immobilized || RecordStore.CanvasImmobilized) return;

    const AdditiveCondition = PointerPress.shiftKey;
    if (!Entry.Chosen) ChooseEntry(Entry.Token, AdditiveCondition);

    const ChosenEntries = RecordStore.Entries.filter((Candidate) => Candidate.Chosen && !Candidate.Immobilized);

    EntryDragState = {
        PointerId: PointerPress.pointerId,
        OriginClientX: PointerPress.clientX,
        OriginClientY: PointerPress.clientY,
        DragEngaged: false,
        Moving: ChosenEntries.map((Candidate) => ({
            Token: Candidate.Token,
            OriginPlaneX: Candidate.PlaneX,
            OriginPlaneY: Candidate.PlaneY
        }))
    };

    PanSurface.setPointerCapture(PointerPress.pointerId);
}

function AdvanceEntryDrag(PointerMotion)
{
    if (!EntryDragState) return;

    const TravelX = PointerMotion.clientX - EntryDragState.OriginClientX;
    const TravelY = PointerMotion.clientY - EntryDragState.OriginClientY;

    if (!EntryDragState.DragEngaged)
    {
        if (Math.hypot(TravelX, TravelY) < DragThreshold) return;
        EntryDragState.DragEngaged = true;
        for (const Moving of EntryDragState.Moving)
        {
            const Wrapper = EntryPlane.querySelector(`.NodeEntry[data-entry="${Moving.Token}"]`);
            if (Wrapper) Wrapper.classList.add('Dragging');
        }
    }

    const PlaneTravelX = TravelX / RecordStore.PlaneScale;
    const PlaneTravelY = TravelY / RecordStore.PlaneScale;

    for (const Moving of EntryDragState.Moving)
    {
        const Entry = RetrieveEntry(Moving.Token);
        if (!Entry) continue;
        Entry.PlaneX = Moving.OriginPlaneX + PlaneTravelX;
        Entry.PlaneY = Moving.OriginPlaneY + PlaneTravelY;

        const Wrapper = EntryPlane.querySelector(`.NodeEntry[data-entry="${Moving.Token}"]`);
        if (Wrapper) Wrapper.style.transform = `translate(${Entry.PlaneX}px, ${Entry.PlaneY}px)`;
    }

    InscribeLinks();
}

function ConcludeEntryDrag()
{
    if (!EntryDragState) return;
    for (const Moving of EntryDragState.Moving)
    {
        const Wrapper = EntryPlane.querySelector(`.NodeEntry[data-entry="${Moving.Token}"]`);
        if (Wrapper) Wrapper.classList.remove('Dragging');
    }
    EntryDragState = null;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SURFACE INTERACTION
//------------------------------------------------------------------------------------------------------------------------

PanSurface.addEventListener('pointerdown', (PointerPress) =>
{
    if (PointerPress.button === 2) return;   // Context menu is raised on contextmenu instead

    const PortDot = PointerPress.target.closest('[data-porthit]');
    if (PortDot)
    {
        PointerPress.preventDefault();
        PointerPress.stopPropagation();
        CommencePortDrag(PortDot, PointerPress);
        return;
    }

    // 📝 Interactive controls inside an entry must not start a drag or steal focus.
    if (PointerPress.target.closest('input, textarea, select, button, [data-action="RadialDial"]')) return;

    const Wrapper = PointerPress.target.closest('.NodeEntry');
    if (Wrapper)
    {
        CommenceEntryDrag(Wrapper, PointerPress);
        return;
    }

    // Middle button, or space held, pans; a plain left drag on empty canvas bands a selection.
    const PanCondition = PointerPress.button === 1 || PointerPress.altKey || RecordStore.CanvasImmobilized;

    if (PanCondition)
    {
        // A pan must track the hand exactly, so any glide is abandoned where it stands.
        AbandonGlide();
        PlanePanState = {
            OriginClientX: PointerPress.clientX,
            OriginClientY: PointerPress.clientY,
            OriginOffsetX: RecordStore.PlaneOffsetX,
            OriginOffsetY: RecordStore.PlaneOffsetY
        };
        PanSurface.classList.add('Grabbing');
        PanSurface.setPointerCapture(PointerPress.pointerId);
        return;
    }

    const SurfaceBounds = PanSurface.getBoundingClientRect();
    BandState = {
        OriginSurfaceX: PointerPress.clientX - SurfaceBounds.left,
        OriginSurfaceY: PointerPress.clientY - SurfaceBounds.top,
        AdditiveCondition: PointerPress.shiftKey
    };
    PanSurface.setPointerCapture(PointerPress.pointerId);
});

PanSurface.addEventListener('pointermove', (PointerMotion) =>
{
    if (LinkInFlight)   { AdvancePortDrag(PointerMotion); return; }
    if (EntryDragState) { AdvanceEntryDrag(PointerMotion); return; }

    if (PlanePanState)
    {
        RecordStore.PlaneOffsetX = PlanePanState.OriginOffsetX + (PointerMotion.clientX - PlanePanState.OriginClientX);
        RecordStore.PlaneOffsetY = PlanePanState.OriginOffsetY + (PointerMotion.clientY - PlanePanState.OriginClientY);
        AlignPlaneTransform();
        return;
    }

    if (BandState)
    {
        const SurfaceBounds = PanSurface.getBoundingClientRect();
        const CurrentX = PointerMotion.clientX - SurfaceBounds.left;
        const CurrentY = PointerMotion.clientY - SurfaceBounds.top;

        const BandLeft   = Math.min(BandState.OriginSurfaceX, CurrentX);
        const BandTop    = Math.min(BandState.OriginSurfaceY, CurrentY);
        const BandWidth  = Math.abs(CurrentX - BandState.OriginSurfaceX);
        const BandHeight = Math.abs(CurrentY - BandState.OriginSurfaceY);

        if (BandWidth > DragThreshold || BandHeight > DragThreshold)
        {
            SelectionBand.style.display = 'block';
            SelectionBand.style.left   = `${BandLeft}px`;
            SelectionBand.style.top    = `${BandTop}px`;
            SelectionBand.style.width  = `${BandWidth}px`;
            SelectionBand.style.height = `${BandHeight}px`;
            BandState.Engaged = true;
        }
    }
});

PanSurface.addEventListener('pointerup', (PointerRelease) =>
{
    if (LinkInFlight) { ConcludePortDrag(PointerRelease); return; }

    if (EntryDragState)
    {
        const DragEngaged = EntryDragState.DragEngaged;
        ConcludeEntryDrag();
        if (!DragEngaged)
        {
            const Wrapper = PointerRelease.target.closest('.NodeEntry');
            if (Wrapper) ChooseEntry(Wrapper.dataset.entry, PointerRelease.shiftKey);
        }
        else
        {
            RefreshEditor();
        }
        return;
    }

    if (PlanePanState) { PlanePanState = null; PanSurface.classList.remove('Grabbing'); return; }

    if (BandState)
    {
        if (BandState.Engaged)
        {
            // Collect every entry whose rendered footprint meets the band.
            const BandBounds = SelectionBand.getBoundingClientRect();
            for (const Entry of RecordStore.Entries)
            {
                const Wrapper = EntryPlane.querySelector(`.NodeEntry[data-entry="${Entry.Token}"]`);
                if (!Wrapper) continue;
                const EntryBounds = Wrapper.getBoundingClientRect();
                const OverlapCondition = EntryBounds.left < BandBounds.right
                                      && EntryBounds.right > BandBounds.left
                                      && EntryBounds.top < BandBounds.bottom
                                      && EntryBounds.bottom > BandBounds.top;
                if (OverlapCondition)              Entry.Chosen = true;
                else if (!BandState.AdditiveCondition) Entry.Chosen = false;
            }
            RefreshEditor();
        }
        else
        {
            DiscardChoice();
            ConcealCatalogueMenu();
        }

        SelectionBand.style.display = 'none';
        BandState = null;
    }
});

PanSurface.addEventListener('wheel', (WheelMotion) =>
{
    if (RecordStore.CanvasImmobilized) return;
    WheelMotion.preventDefault();
    const ZoomFactor = WheelMotion.deltaY < 0 ? ZoomIncrement : 1 / ZoomIncrement;

    // Compound from the glide target so a fast spin accumulates instead of fighting the easing.
    const ReferenceScale = GlideTarget ? GlideTarget.Scale : RecordStore.PlaneScale;
    ApplyZoomAboutPoint(ReferenceScale * ZoomFactor, WheelMotion.clientX, WheelMotion.clientY);
}, { passive: false });

PanSurface.addEventListener('contextmenu', (PointerPress) =>
{
    PointerPress.preventDefault();

    const Wrapper = PointerPress.target.closest('.NodeEntry');
    if (Wrapper)
    {
        RevealEntryMenu(PointerPress.clientX, PointerPress.clientY, Wrapper.dataset.entry);
        return;
    }
    RevealCatalogueMenu(PointerPress.clientX, PointerPress.clientY, null, SpawnCatalogueEntry);
});

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY MENU
//------------------------------------------------------------------------------------------------------------------------

let EntryMenuElement = null;
let EntryMenuScreen  = null;

function ConcealEntryMenu()
{
    if (EntryMenuElement) { EntryMenuElement.remove(); EntryMenuElement = null; }
    if (EntryMenuScreen)  { EntryMenuScreen.remove();  EntryMenuScreen = null; }
}

function RevealEntryMenu(ScreenX, ScreenY, EntryToken)
{
    ConcealEntryMenu();
    ConcealCatalogueMenu();

    const Entry = RetrieveEntry(EntryToken);
    if (!Entry) return;

    EntryMenuScreen = document.createElement('div');
    EntryMenuScreen.className = 'MenuScreen';
    EntryMenuScreen.addEventListener('pointerdown', ConcealEntryMenu);
    EntryMenuScreen.addEventListener('contextmenu', (PointerPress) =>
    {
        PointerPress.preventDefault();
        ConcealEntryMenu();
    });
    document.body.appendChild(EntryMenuScreen);

    EntryMenuElement = document.createElement('div');
    EntryMenuElement.className = 'EntryMenu';
    EntryMenuElement.style.top  = `${Math.min(ScreenY, window.innerHeight - 100)}px`;
    EntryMenuElement.style.left = `${Math.min(ScreenX, window.innerWidth - 180)}px`;
    EntryMenuElement.innerHTML =
        `<button class="EntryMenuAction">`
      + `${Entry.Exported ? 'Unmark for export' : 'Mark for export'}</button>`;

    EntryMenuElement.addEventListener('pointerdown', (PointerPress) => PointerPress.stopPropagation());
    EntryMenuElement.addEventListener('click', () =>
    {
        Entry.Exported = !Entry.Exported;
        ConcealEntryMenu();
        RefreshEditor();
    });

    document.body.appendChild(EntryMenuElement);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ENTRY ACTION ROUTING
//------------------------------------------------------------------------------------------------------------------------

EntryPlane.addEventListener('click', (PointerRelease) =>
{
    const ActionElement = PointerRelease.target.closest('[data-action]');
    if (!ActionElement) return;

    const Wrapper = ActionElement.closest('.NodeEntry');
    if (!Wrapper) return;
    const Entry = RetrieveEntry(Wrapper.dataset.entry);
    if (!Entry) return;

    const ActionNaming = ActionElement.dataset.action;
    if (ActionNaming === 'EntryBody') return;

    PointerRelease.stopPropagation();

    switch (ActionNaming)
    {
        case 'Fold':          Entry.Folded = !Entry.Folded; break;
        case 'Tack':          Entry.Tacked = !Entry.Tacked; break;
        case 'Immobilize':    Entry.Immobilized = !Entry.Immobilized; break;
        case 'Reclaim':       ReclaimEntry(Entry.Token); break;
        case 'AttachInbound': AttachInboundPort(Entry.Token); break;
        case 'AttachOutbound':AttachOutboundPort(Entry.Token); break;
        case 'DetachOutbound':DetachOutboundPort(Entry.Token, ActionElement.dataset.port); break;
        case 'Export':        window.alert(`Exporting ${Entry.Title}...`); return;
        case 'Preview':       ChooseEntry(Entry.Token, false); return;

        case 'Annotate':
            Entry.Composing = true;
            if (Entry.Annotation === undefined) Entry.Annotation = '';
            break;

        case 'AnnotationOpen':
            Entry.Composing = true;
            break;

        case 'Notice':
        {
            const NoticeText = window.prompt('Enter tooltip text for this node:', Entry.Notice || '');
            if (NoticeText === null) return;
            Entry.Notice = NoticeText;
            break;
        }

        case 'PortScalarReveal':
            Entry.RevealedScalars[ActionElement.dataset.port] = true;
            break;

        default: return;
    }

    RefreshEditor();

    // 📝 Re-focus the annotation field after the re-render that revealed it.
    if (ActionNaming === 'Annotate' || ActionNaming === 'AnnotationOpen')
    {
        const Field = EntryPlane.querySelector(
            `.NodeEntry[data-entry="${Entry.Token}"] [data-action="AnnotationField"]`);
        if (Field) { Field.focus(); Field.setSelectionRange(Field.value.length, Field.value.length); }
    }
    if (ActionNaming === 'PortScalarReveal')
    {
        const Field = EntryPlane.querySelector(
            `.NodeEntry[data-entry="${Entry.Token}"] [data-port="${ActionElement.dataset.port}"].PinScalarField`);
        if (Field) Field.focus();
    }
});

// 📝 Scalar edits mutate the store in place and never re-render the entry mid-typing — a re-render
//    would destroy the focused field and drop the caret.
EntryPlane.addEventListener('input', (FieldMotion) =>
{
    const Field = FieldMotion.target.closest('[data-action]');
    if (!Field) return;

    const Wrapper = Field.closest('.NodeEntry');
    const Entry = Wrapper && RetrieveEntry(Wrapper.dataset.entry);
    if (!Entry) return;

    const ActionNaming = Field.dataset.action;

    if (ActionNaming === 'PortScalar')
    {
        Entry.PortScalars[Field.dataset.port] = Field.value;
    }
    else if (ActionNaming === 'RegulatorScalar')
    {
        Entry.PortScalars['val'] = Field.value;
        const Sweep = Wrapper.querySelector('[data-action="RegulatorSweep"]');
        if (Sweep) Sweep.value = parseFloat(Field.value) || 0;
    }
    else if (ActionNaming === 'RegulatorSweep')
    {
        Entry.PortScalars['val'] = Field.value;
        const Scalar = Wrapper.querySelector('[data-action="RegulatorScalar"]');
        if (Scalar) Scalar.value = Field.value;
    }
    else if (ActionNaming === 'AxisScalar')
    {
        Entry.PortScalars[Field.dataset.axis] = Field.value;
    }
    else if (ActionNaming === 'AnnotationField')
    {
        Entry.Annotation = Field.value;
        return;
    }
    else
    {
        return;
    }

    ProvokeRebuild(Entry.Token, () => { RefreshProgressPill(); });
    RefreshProgressPill();
    FeedTerrainViewport();
});

// A scalar field closes on double click, restoring the compact affordance.
EntryPlane.addEventListener('dblclick', (PointerRelease) =>
{
    const Field = PointerRelease.target.closest('[data-action="PortScalar"]');
    if (!Field) return;

    const Wrapper = Field.closest('.NodeEntry');
    const Entry = Wrapper && RetrieveEntry(Wrapper.dataset.entry);
    if (!Entry) return;

    PointerRelease.stopPropagation();
    const PortToken = Field.dataset.port;
    delete Entry.PortScalars[PortToken];
    delete Entry.RevealedScalars[PortToken];
    RefreshEditor();
});

EntryPlane.addEventListener('focusout', (FocusRelease) =>
{
    const Field = FocusRelease.target.closest('[data-action="AnnotationField"]');
    if (!Field) return;

    const Wrapper = Field.closest('.NodeEntry');
    const Entry = Wrapper && RetrieveEntry(Wrapper.dataset.entry);
    if (!Entry) return;

    Entry.Composing = false;
    RefreshEntries();
});

// 📝 Typing inside an entry must not reach the canvas-level delete shortcut.
EntryPlane.addEventListener('keydown', (KeyPress) =>
{
    if (KeyPress.target.closest('input, textarea')) KeyPress.stopPropagation();
});

//------------------------------------------------------------------------------------------------------------------------
//                                                     RADIAL DIAL
//------------------------------------------------------------------------------------------------------------------------

EntryPlane.addEventListener('pointerdown', (PointerPress) =>
{
    const Dial = PointerPress.target.closest('[data-action="RadialDial"]');
    if (!Dial) return;

    PointerPress.preventDefault();
    PointerPress.stopPropagation();

    const Wrapper = Dial.closest('.NodeEntry');
    const Entry = Wrapper && RetrieveEntry(Wrapper.dataset.entry);
    if (!Entry) return;

    const Needle = Dial.querySelector('.DialNeedle');

    const AlignNeedle = (PointerMotion) =>
    {
        const DialBounds = Dial.getBoundingClientRect();
        const CentreX = DialBounds.left + DialBounds.width / 2;
        const CentreY = DialBounds.top + DialBounds.height / 2;
        let DialAngle = Math.atan2(PointerMotion.clientY - CentreY, PointerMotion.clientX - CentreX) * (180 / Math.PI);
        if (DialAngle < 0) DialAngle += 360;

        Entry.PortScalars['val'] = DialAngle.toFixed(1);
        if (Needle) Needle.style.transform = `translate(0,-50%) rotate(${DialAngle}deg)`;

        const Scalar = Wrapper.querySelector('[data-action="RegulatorScalar"]');
        if (Scalar) Scalar.value = Entry.PortScalars['val'];

        ProvokeRebuild(Entry.Token, () => RefreshProgressPill());
        RefreshProgressPill();
    };

    AlignNeedle(PointerPress);
    Dial.setPointerCapture(PointerPress.pointerId);

    const ReleaseDial = () =>
    {
        Dial.removeEventListener('pointermove', AlignNeedle);
        Dial.removeEventListener('pointerup', ReleaseDial);
    };
    Dial.addEventListener('pointermove', AlignNeedle);
    Dial.addEventListener('pointerup', ReleaseDial);
});

//------------------------------------------------------------------------------------------------------------------------
//                                                   LINK SEVERING
//------------------------------------------------------------------------------------------------------------------------

EdgeCanvas.addEventListener('click', (PointerRelease) =>
{
    const Curve = PointerRelease.target.closest('[data-link]');
    if (!Curve) return;
    PointerRelease.stopPropagation();
    ReclaimLink(Curve.dataset.link);
    RefreshEditor();
});

//------------------------------------------------------------------------------------------------------------------------
//                                                  KEYBOARD SHORTCUTS
//------------------------------------------------------------------------------------------------------------------------

window.addEventListener('keydown', (KeyPress) =>
{
    if (KeyPress.target.closest('input, textarea, select')) return;

    if (KeyPress.key === 'Delete' || KeyPress.key === 'Backspace')
    {
        const Doomed = RecordStore.Entries.filter((Entry) => Entry.Chosen && !Entry.Immobilized);
        if (Doomed.length === 0) return;
        KeyPress.preventDefault();
        for (const Entry of Doomed) ReclaimEntry(Entry.Token);
        RefreshEditor();
        return;
    }

    if (KeyPress.key === 'Escape')
    {
        ConcealCatalogueMenu();
        ConcealEntryMenu();
        DiscardChoice();
        return;
    }

    if (KeyPress.key === 'f' || KeyPress.key === 'F') EncompassEntries();
});

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOP BAR WIRING
//------------------------------------------------------------------------------------------------------------------------

const SimulateAction       = document.getElementById('SimulateAction');
const SimulateHint         = document.getElementById('SimulateHint');
const ChromeSettingsAction = document.getElementById('ChromeSettingsAction');
const ChromeSettingsPane   = document.getElementById('ChromeSettingsPane');
const ChromeSettingsShell  = document.getElementById('ChromeSettingsShell');
const ViewportToggle       = document.getElementById('ViewportToggle');
const ControlsToggle       = document.getElementById('ControlsToggle');

function RefreshTopBar()
{
    SimulateAction.innerHTML = ComposeGlyph(RecordStore.SimulationRunning ? 'Suspend' : 'Advance', 18);
    SimulateAction.classList.toggle('Running', RecordStore.SimulationRunning);
    SimulateHint.dataset.hint = RecordStore.SimulationRunning ? 'Pause Simulation' : 'Simulate';

    ChromeSettingsAction.innerHTML = ComposeGlyph('Configure', 16);
}

SimulateAction.addEventListener('click', () =>
{
    RecordStore.SimulationRunning = !RecordStore.SimulationRunning;
    RefreshTopBar();
});

function ConcealChromeSettings()
{
    ChromeSettingsPane.classList.remove('Open');
    ChromeSettingsAction.classList.remove('Chosen');
}

ChromeSettingsAction.addEventListener('click', () =>
{
    const Revealed = ChromeSettingsPane.classList.toggle('Open');
    ChromeSettingsAction.classList.toggle('Chosen', Revealed);
});

// 📝 The card closes on leaving the shell that holds the button and the pane together, so it dismisses
//    as soon as the pointer walks off the settings themselves rather than surviving until the pointer
//    crosses into the other canvas.
ChromeSettingsShell.addEventListener('pointerleave', DismissOnGenuineLeave(ConcealChromeSettings));

ViewportToggle.addEventListener('click', () =>
{
    RecordStore.ViewportSide = RecordStore.ViewportSide === 'hidden' ? 'right' : 'hidden';
    RefreshViewportSide();
});

ControlsToggle.addEventListener('click', () =>
{
    RecordStore.ControlsRevealed = !RecordStore.ControlsRevealed;
    ControlsToggle.querySelector('.ToggleTrack').classList.toggle('Engaged', RecordStore.ControlsRevealed);
    RefreshCanvasControls();
});

function RefreshViewportSide()
{
    const ConcealedCondition = RecordStore.ViewportSide === 'hidden';
    document.body.classList.toggle('ViewportHidden', ConcealedCondition);
    ViewportRegion.classList.toggle('Concealed', ConcealedCondition);
    ViewportToggle.querySelector('.ToggleTrack').classList.toggle('Engaged', !ConcealedCondition);
    if (!ConcealedCondition) FeedTerrainViewport();
    InscribeLinks();
}

ControlAnchor.addEventListener('click', (PointerRelease) =>
{
    const ControlAction = PointerRelease.target.closest('[data-control]');
    if (!ControlAction) return;

    const SurfaceBounds = PanSurface.getBoundingClientRect();
    const CentreX = SurfaceBounds.left + SurfaceBounds.width / 2;
    const CentreY = SurfaceBounds.top + SurfaceBounds.height / 2;

    switch (ControlAction.dataset.control)
    {
        case 'ZoomIn':
            ApplyZoomAboutPoint(
                (GlideTarget ? GlideTarget.Scale : RecordStore.PlaneScale) * ZoomIncrement, CentreX, CentreY);
            break;
        case 'ZoomOut':
            ApplyZoomAboutPoint(
                (GlideTarget ? GlideTarget.Scale : RecordStore.PlaneScale) / ZoomIncrement, CentreX, CentreY);
            break;
        case 'Encompass':  EncompassEntries(); break;
        case 'Immobilize':
            RecordStore.CanvasImmobilized = !RecordStore.CanvasImmobilized;
            PanSurface.classList.toggle('Locked', RecordStore.CanvasImmobilized);
            RefreshCanvasControls();
            break;
    }
});

//------------------------------------------------------------------------------------------------------------------------
//                                                  VIEWPORT CHROME WIRING
//------------------------------------------------------------------------------------------------------------------------

const ViewportSettingsAction = document.getElementById('ViewportSettingsAction');
const ViewportSettingsPane   = document.getElementById('ViewportSettingsPane');
const ViewportSettingsShell  = document.getElementById('ViewportSettingsShell');

ViewportSettingsAction.innerHTML = ComposeGlyph('Configure', 18);
document.getElementById('DisplayGlyph').innerHTML     = ComposeGlyph('Strata', 16);
document.getElementById('EnvironmentGlyph').innerHTML = ComposeGlyph('Sun', 16);

function ConcealViewportSettings()
{
    ViewportSettingsPane.classList.remove('Open');
    ViewportSettingsAction.classList.remove('Engaged');
    ViewportSettingsShell.classList.add('Shut');
}

ViewportSettingsAction.addEventListener('click', () =>
{
    const Revealed = ViewportSettingsPane.classList.toggle('Open');
    ViewportSettingsAction.classList.toggle('Engaged', Revealed);
    ViewportSettingsShell.classList.toggle('Shut', !Revealed);
});

// 📝 Dragging a slider past the pane edge still holds the pointer captive, and dismissing the card
//    underneath the hand would abandon the drag. A held button means the pointer is still committed to
//    the pane, so the leave is ignored until it is released.
function DismissOnGenuineLeave(ConcealAction)
{
    return (PointerExit) =>
    {
        if (PointerExit.buttons !== 0) return;
        ConcealAction();
    };
}

ViewportSettingsShell.addEventListener('pointerleave', DismissOnGenuineLeave(ConcealViewportSettings));

// 📝 A max-height transition needs two real numbers to travel between — `none` is not interpolable, so
//    the open height has to be a measured pixel value. It is measured at fold time rather than seeded
//    once at load: at load the card is shut inside a clamped shell, so scrollHeight comes back
//    clipped, and a value cached then would also go stale as soon as the content reflowed.
for (const FoldCaption of ViewportSettingsPane.querySelectorAll('[data-fold]'))
{
    const Section = FoldCaption.nextElementSibling;
    FoldCaption.querySelector('.FoldChevron').innerHTML = ComposeGlyph('ChevronDown', 14);

    if (!Section || !Section.classList.contains('PaneSection')) continue;

    FoldCaption.addEventListener('click', () =>
    {
        const FoldingCondition = !FoldCaption.classList.contains('Folded');

        if (FoldingCondition)
        {
            // Pin the open height so the collapse eases from a number rather than from `none`, and let
            // that frame land before the 0 — applied in one flush the browser sees no transition to run.
            Section.style.maxHeight = `${Section.scrollHeight}px`;
            requestAnimationFrame(() => FoldCaption.classList.add('Folded'));
            return;
        }

        // 📝 Unfolding runs the other way about: the collapsed rule carries `!important`, so an inline
        //    height set while `Folded` is still on is ignored, then snaps in the instant the class
        //    drops. The class comes off first, pinning 0 inline as the start, and the measured target
        //    goes on the following frame.
        Section.style.maxHeight = '0px';
        FoldCaption.classList.remove('Folded');
        requestAnimationFrame(() =>
        {
            Section.style.maxHeight = `${Section.scrollHeight}px`;
        });
    });

    // Once open, drop the cap so the section can grow with its own content.
    Section.addEventListener('transitionend', (Motion) =>
    {
        if (Motion.propertyName !== 'max-height') return;
        if (!FoldCaption.classList.contains('Folded')) Section.style.maxHeight = 'none';
    });
}

const ShadingActions =
[
    { Identifier: 'ShadeTextured',  Mode: 'textured',  Glyph: 'Raster' },
    { Identifier: 'ShadeLambert',   Mode: 'lambert',   Glyph: 'Circle' },
    { Identifier: 'ShadeFlat',      Mode: 'flat',      Glyph: 'Square' },
    { Identifier: 'ShadeWireframe', Mode: 'wireframe', Glyph: 'Strata' }
];

for (const Specification of ShadingActions)
{
    const ShadingAction = document.getElementById(Specification.Identifier);
    ShadingAction.innerHTML = ComposeGlyph(Specification.Glyph, 18);
    ShadingAction.addEventListener('click', () =>
    {
        for (const Candidate of ShadingActions)
        {
            document.getElementById(Candidate.Identifier)
                .classList.toggle('Chosen', Candidate.Mode === Specification.Mode);
        }
        ReconfigureShading(Specification.Mode);
    });
}

const CameraSolid  = document.getElementById('CameraSolid');
const CameraPlanar  = document.getElementById('CameraPlanar');

CameraSolid.addEventListener('click', () =>
{
    CameraSolid.classList.add('Chosen');
    CameraPlanar.classList.remove('Chosen');
    ReconfigureCameraMode('3D');
});

CameraPlanar.addEventListener('click', () =>
{
    CameraPlanar.classList.add('Chosen');
    CameraSolid.classList.remove('Chosen');
    ReconfigureCameraMode('2D');
});

document.getElementById('ResolutionSelect').addEventListener('change', (SelectionChange) =>
{
    ReconfigureResolution(parseInt(SelectionChange.target.value, 10));
});

// The dial openings, matching the viewport profile these sweeps drive.
const EnvironmentOpenings =
{
    SunElevation: 20,
    SunAzimuth:   45,
    Turbidity:    10,
    FogDensity:   0.005
};

// 📝 Ported from ControlsPreview.html: the track is a plain element dragged by pointer rather than an
//    `input[type=range]`, because the range thumb cannot be styled to the component vocabulary and its
//    fill has no cross-engine form at all. Pointer capture is taken on the track so a drag that leaves
//    the pane still steers the dial until the button is released.
for (const SweepGroup of ViewportSettingsPane.querySelectorAll('[data-sweep]'))
{
    const Dial   = SweepGroup.dataset.sweep;
    const Least  = parseFloat(SweepGroup.dataset.least);
    const Most   = parseFloat(SweepGroup.dataset.most);
    const Step   = parseFloat(SweepGroup.dataset.step);
    const Places = parseInt(SweepGroup.dataset.places, 10);

    const Track   = SweepGroup.querySelector('.Sweep');
    const Fill    = SweepGroup.querySelector('.SweepFill');
    const Knob    = SweepGroup.querySelector('.SweepKnob');
    const Readout = SweepGroup.querySelector('.ValueNumber input');

    Readout.min  = Least;
    Readout.max  = Most;
    Readout.step = Step;

    let Magnitude = EnvironmentOpenings[Dial];

    // Paint the track from the held magnitude. Kept separate from the commit so a typed value and a
    // dragged one settle on exactly the same geometry.
    function InscribeSweep(RefreshReadout)
    {
        const Fraction = (Magnitude - Least) / (Most - Least);
        Fill.style.width = `${Fraction * 100}%`;
        Knob.style.left  = `${Fraction * 100}%`;
        if (RefreshReadout) Readout.value = Magnitude.toFixed(Places);
    }

    function CommitMagnitude(Proposal, RefreshReadout)
    {
        // 📝 Quantized to the step before clamping, so the readout can never show a value the dial
        //    cannot actually hold — a 0.0007 fog density displaying as 0.001 while the viewport ran the
        //    unrounded figure would put the two quietly out of agreement.
        const Quantized = Math.round(Proposal / Step) * Step;
        Magnitude = Math.min(Most, Math.max(Least, Quantized));
        InscribeSweep(RefreshReadout);
        ReconfigureEnvironment({ [Dial]: Magnitude });
    }

    function ResolveFromPointer(ClientX)
    {
        const Bounds   = Track.getBoundingClientRect();
        const Fraction = Math.min(1, Math.max(0, (ClientX - Bounds.left) / Bounds.width));
        CommitMagnitude(Least + Fraction * (Most - Least), true);
    }

    Track.addEventListener('pointerdown', (PointerPress) =>
    {
        PointerPress.preventDefault();
        Track.setPointerCapture(PointerPress.pointerId);
        Track.dataset.hauling = 'true';
        ResolveFromPointer(PointerPress.clientX);
    });

    Track.addEventListener('pointermove', (PointerMotion) =>
    {
        if (Track.dataset.hauling !== 'true') return;
        ResolveFromPointer(PointerMotion.clientX);
    });

    const ConcludeHaul = (PointerRelease) =>
    {
        if (Track.dataset.hauling !== 'true') return;
        delete Track.dataset.hauling;
        Track.releasePointerCapture(PointerRelease.pointerId);
    };

    Track.addEventListener('pointerup', ConcludeHaul);
    Track.addEventListener('pointercancel', ConcludeHaul);

    // 📝 The typed path deliberately does not rewrite the field while it is being edited: reformatting
    //    mid-keystroke would fight the caret, so the readout is left alone until focus departs.
    Readout.addEventListener('input', () =>
    {
        const Typed = parseFloat(Readout.value);
        if (Number.isNaN(Typed)) return;
        CommitMagnitude(Typed, false);
    });

    Readout.addEventListener('change', () => InscribeSweep(true));
    Readout.addEventListener('blur',   () => InscribeSweep(true));

    InscribeSweep(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SPLIT HANDLE
//------------------------------------------------------------------------------------------------------------------------

const SplitHandle = document.getElementById('SplitHandle');

const SplitFloor   = 0.18;   // [-] - Narrowest either side may be squeezed, as a fraction of the window
const SplitCeiling = 0.82;   // [-] - Widest the graph may grow before the viewport is uselessly thin

// 📝 Committed as a percentage rather than a pixel width so the split survives a window resize at the
//    ratio the user chose, instead of the graph keeping its pixels and the viewport absorbing the change.
function ApplySplitFraction(Fraction)
{
    const Clamped = Math.min(SplitCeiling, Math.max(SplitFloor, Fraction));
    document.body.style.setProperty('--split-fraction', `${(Clamped * 100).toFixed(3)}%`);
    InscribeLinks();
}

SplitHandle.addEventListener('pointerdown', (PointerPress) =>
{
    PointerPress.preventDefault();
    SplitHandle.setPointerCapture(PointerPress.pointerId);
    SplitHandle.classList.add('Hauling');
    document.body.classList.add('Hauling');
});

SplitHandle.addEventListener('pointermove', (PointerMotion) =>
{
    if (!SplitHandle.classList.contains('Hauling')) return;
    ApplySplitFraction(PointerMotion.clientX / window.innerWidth);
});

function ConcludeSplitHaul(PointerRelease)
{
    if (!SplitHandle.classList.contains('Hauling')) return;
    SplitHandle.classList.remove('Hauling');
    document.body.classList.remove('Hauling');
    if (PointerRelease) SplitHandle.releasePointerCapture(PointerRelease.pointerId);
}

SplitHandle.addEventListener('pointerup', ConcludeSplitHaul);
SplitHandle.addEventListener('pointercancel', ConcludeSplitHaul);

// A double click restores the even split, which is otherwise fiddly to hit by hand.
SplitHandle.addEventListener('dblclick', () => ApplySplitFraction(0.5));

//------------------------------------------------------------------------------------------------------------------------
//                                                     INITIALIZE
//------------------------------------------------------------------------------------------------------------------------

RecordStore.Entries.push(ConstructOriginEntry());

AlignPlaneTransform();
RefreshTopBar();
RefreshViewportSide();
RefreshCanvasControls();
RefreshEditor();

InitializeTerrainViewport(document.getElementById('TerrainCanvas'));
EncompassEntriesAbruptly();

window.addEventListener('resize', () =>
{
    InscribeLinks();
});
