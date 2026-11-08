/*==========================================================================================================================================
                                                             PREVIEWSAMPLING.JS
==========================================================================================================================================*/
// 🧩 Honest completed-image sampling descriptions and camera distances for bounded authored-pitch working areas.

export const PreviewSamplingRevision = 'sculpt-high-2';
export const MinimumWorkingOrbit = .5;

//------------------------------------------------------------------------------------------------------------------------
//                                                       WORKING CAMERA DISTANCE
//------------------------------------------------------------------------------------------------------------------------

export function ResolveWorkingOrbit(Orbit, Pitch)
{
    if (!Array.isArray(Pitch) || Pitch.length !== 3 || Pitch.some(Spacing => !Number.isFinite(Spacing) || Spacing <= 0))
        throw Error('A working camera requires positive XYZ voxel spacing.');
    return Math.max(MinimumWorkingOrbit, Math.min(Orbit, Math.min(Pitch[0], Pitch[2]) * 64 * .55));
}

export function FormatSamplingPitch(Pitch)
{
    return Pitch.map(Spacing => Spacing >= 1 ? Spacing.toFixed(3) : Spacing.toPrecision(3)).join(' × ');
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       COMPLETED GPU IMAGE DESCRIPTION
//------------------------------------------------------------------------------------------------------------------------

export function DescribePreviewSampling(Telemetry, Selection = null, WorkingEnabled = false)
{
    let Working = null, Refinement = null;
    try { Working = JSON.parse(Telemetry.displayedWorkingLattice || 'null'); Refinement = JSON.parse(Telemetry.displayedSurfaceRefinement || 'null'); }
    catch { /* 📝 Incomplete inspection text is not evidence that authored detail is displayed. */ }
    const Completed = Number(Telemetry.terrainRenders) > 0 && Number.isFinite(Number(Telemetry.displayedSurfacePitch));
    const Stopped = ['failed', 'lost'].includes(Telemetry.renderStatus);
    const BudgetLimited = Telemetry.surfaceDetailLimited === 'triangle-budget';
    const Resident = Completed && Working?.Resident === true;
    const Pending = Completed && (Telemetry.completedRevision !== Telemetry.volumeRevision ||
        Telemetry.geometryPreparationEpoch !== Telemetry.geometryCompletedEpoch ||
        Telemetry.completedPreparationEpoch !== Telemetry.geometryCompletedEpoch || WorkingEnabled !== Resident);
    const MatchesSelection = Resident && Selection && Working.LayerIndex === Selection.LayerIndex &&
        Working.RegionIndex === Selection.RegionIndex && JSON.stringify(Working.Dimensions) === JSON.stringify(Selection.Dimensions);
    const Triangles = Number(Telemetry.displayedSurfaceTriangles || 0).toLocaleString('en-US');
    const Pitch = Resident ? Working.Pitch : Completed ? [Number(Telemetry.displayedSurfacePitch)] : null;
    let Heading = 'Coarse overview · GPU surface', Badge = 'COARSE OVERVIEW';
    let Actual = Completed ? `Displayed geometry: ${FormatSamplingPitch(Pitch)} m spacing · ${Triangles} triangles.` :
        'No completed GPU image yet. Target counts are not a displayed surface.';
    let Guidance = 'Target XYZ counts do not change this whole-scene overview. View authored detail opens one bounded local patch.';
    if (Completed && Refinement)
    {
        Heading = 'High detail · adaptive XYZ mesh';
        Badge = 'HIGH DETAIL';
        Actual = `Displayed adaptive geometry: ${Triangles} triangles · ${Number(Refinement.ProjectedVertices).toLocaleString('en-US')} field-projected vertices.`;
        Guidance = 'High geometry stays enabled. The shared-edge mesh is resampled from the 3D field within 120,000 triangles; it is not a dense whole-region lattice. Sculpt opens native-pitch detail.';
    }
    if (Resident)
    {
        Heading = 'Local working detail · GPU surface';
        Badge = 'WORKING PATCH';
        Actual = `Displayed patch: ${FormatSamplingPitch(Pitch)} m/cell · ${Working.Cells.join(' × ')} cells.`;
        Guidance = 'Only this local patch uses the displayed spacing. Surroundings remain coarser; the boundary has a four-cell transition rim.';
    }
    if (!Completed) { Heading = 'Preparing GPU surface'; Badge = 'PREPARING'; }
    let Notice = Pending ? 'Updating requested detail; these measurements describe the last completed image.' : '';
    if (BudgetLimited) Notice = 'Detail reached its triangle budget; the last completed GPU surface is retained.';
    else if (!Pending && Resident && Working.Limited) Notice = 'Working coverage was reduced to the budget; its reported pitch is unchanged.';
    if (Stopped) { Heading = 'GPU preview stopped'; Badge = 'GPU STOPPED'; Notice = 'No software rendering fallback is running.'; }
    return {
        Heading, Badge, Actual, Guidance, Notice, Pitch, Working: Resident ? Working : null,
        Mode: !Completed ? 'preparing' : Resident ? 'working' : Refinement ? 'high' : 'overview',
        MatchesTarget: Boolean(MatchesSelection && !Pending && !Stopped && Working.Pitch.every((Spacing, Axis) =>
            Math.abs(Spacing - Selection.Pitch[Axis]) <= Math.max(1e-12, Selection.Pitch[Axis] * 1e-9))),
        Target: Selection ? `${Selection.Dimensions.join(' × ')} XYZ · ${FormatSamplingPitch(Selection.Pitch)} m/cell` : 'No region selected',
        Image: Completed ? `${Telemetry.displayedWidth} × ${Telemetry.displayedHeight} image pixels` : 'No completed image',
        Renderer: Telemetry.renderer || 'Driver not reported yet',
        Execution: Telemetry.renderMode === 'webgl2' ? 'Browser-side WebGL2 surface rasterization' : 'GPU initialization pending',
        Revisions: `Document ${Telemetry.volumeRevision || '—'} · displayed ${Telemetry.completedRevision || '—'} · preparation ${Telemetry.completedPreparationEpoch || '—'}`,
        Revision: PreviewSamplingRevision
    };
}
