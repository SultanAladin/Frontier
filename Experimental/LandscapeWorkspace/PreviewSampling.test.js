/*==========================================================================================================================================
                                                          PREVIEWSAMPLING.TEST.JS
==========================================================================================================================================*/
// 🧩 Completed-image geometry versus authored targets, honest GPU diagnostics and sub-25-metre working focus.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { DescribePreviewSampling, ResolveWorkingOrbit, MinimumWorkingOrbit } from './PreviewSampling.js';

const Target = { LayerIndex: 0, RegionIndex: 0, Dimensions: [2048, 512, 2048], Pitch: [.33935546875, .642578125, .28564453125] };
const Working = { ...Target, Resident: true, Limited: false, Cells: [64, 64, 64] };
function Overview()
{
    return { terrainRenders: '8', displayedSurfacePitch: '10.859375', displayedSurfaceTriangles: '96798', displayedWorkingLattice: 'null',
        displayedWidth: '515', displayedHeight: '543', renderStatus: 'ready', renderMode: 'webgl2', renderer: 'ANGLE lab driver',
        completedRevision: '3', volumeRevision: '3', geometryPreparationEpoch: '6', geometryCompletedEpoch: '6', completedPreparationEpoch: '6' };
}

test('a 2048 target and wide image never label the still-coarse overview as authored detail', () =>
{
    const Result = DescribePreviewSampling(Overview(), Target);
    Assert.equal(Result.Mode, 'overview');
    Assert.equal(Result.Badge, 'COARSE OVERVIEW');
    Assert.equal(Result.MatchesTarget, false);
    Assert.deepEqual(Result.Pitch, [10.859375]);
    Assert.match(Result.Actual, /10\.859 m spacing/);
    Assert.match(Result.Actual, /96,798 triangles/);
    Assert.match(Result.Target, /2048 × 512 × 2048/);
    Assert.match(Result.Guidance, /do not change this whole-scene overview/);
});

test('prepared metadata cannot masquerade as the image whose GPU fence actually completed', () =>
{
    const Reads = { ...Overview(), surfacePitch: '.339', surfaceTriangles: '118000', workingLattice: JSON.stringify(Working) };
    const Result = DescribePreviewSampling(Reads, Target, true);
    Assert.equal(Result.Mode, 'overview');
    Assert.equal(Result.MatchesTarget, false);
    Assert.deepEqual(Result.Pitch, [10.859375]);
    Assert.match(Result.Notice, /last completed image/);
});

test('completed authored detail reports only local resident coverage, not a full landscape lattice', () =>
{
    const Result = DescribePreviewSampling({ ...Overview(), displayedWorkingLattice: JSON.stringify(Working) }, Target, true);
    Assert.equal(Result.Mode, 'working');
    Assert.equal(Result.MatchesTarget, true);
    Assert.deepEqual(Result.Pitch, Target.Pitch);
    Assert.match(Result.Actual, /64 × 64 × 64 cells/);
    Assert.match(Result.Guidance, /Surroundings remain coarser/);
    Assert.match(Result.Guidance, /transition rim/);
    const Other = DescribePreviewSampling({ ...Overview(), displayedWorkingLattice: JSON.stringify(Working) }, { ...Target, RegionIndex: 1 }, true);
    Assert.equal(Other.MatchesTarget, false);
});

test('revision or preparation mismatch identifies retained imagery instead of calling the requested target complete', () =>
{
    for (const Change of [{ volumeRevision: '4' }, { geometryPreparationEpoch: '7' }, { completedPreparationEpoch: '5' }])
    {
        const Result = DescribePreviewSampling({ ...Overview(), displayedWorkingLattice: JSON.stringify(Working), ...Change }, Target, true);
        Assert.equal(Result.MatchesTarget, false);
        Assert.match(Result.Notice, /last completed image/);
    }
});

test('missing completed evidence and malformed inspection text do not invent native detail', () =>
{
    Assert.equal(DescribePreviewSampling({}, Target).Mode, 'preparing');
    Assert.match(DescribePreviewSampling({}, Target).Actual, /No completed GPU image/);
    Assert.equal(DescribePreviewSampling({ ...Overview(), displayedWorkingLattice: '{' }, Target).MatchesTarget, false);
});

test('budget and stopped GPU messages are explicit without pretending the driver identifies an application fallback', () =>
{
    const Limited = DescribePreviewSampling({ ...Overview(), surfaceDetailLimited: 'triangle-budget' }, Target, true);
    Assert.match(Limited.Notice, /triangle budget/);
    Assert.equal(Limited.Mode, 'overview');
    const Stopped = DescribePreviewSampling({ ...Overview(), renderStatus: 'lost', renderer: 'ANGLE SwiftShader' }, Target);
    Assert.equal(Stopped.Badge, 'GPU STOPPED');
    Assert.equal(Stopped.Renderer, 'ANGLE SwiftShader');
    Assert.match(Stopped.Notice, /No software rendering fallback/);
    Assert.equal(Stopped.Execution, 'Browser-side WebGL2 surface rasterization');
});

test('working focus can fill the view with a fine 2048-grid patch rather than stopping at 25 metres', () =>
{
    const Distance = ResolveWorkingOrbit(1000, Target.Pitch);
    Assert.ok(Distance < 25);
    Assert.equal(Distance, Target.Pitch[2] * 64 * .55);
    Assert.equal(ResolveWorkingOrbit(8, Target.Pitch), 8);
    Assert.equal(ResolveWorkingOrbit(1000, [.0001, .0002, .0001]), MinimumWorkingOrbit);
    Assert.equal(ResolveWorkingOrbit(40, [2, 3, 2]), 40);
    Assert.throws(() => ResolveWorkingOrbit(1000, [1, 0, 1]), /positive XYZ/);
});


test('adaptive high geometry is described honestly rather than relabelled as a dense native lattice', () =>
{
    const Result = DescribePreviewSampling({ ...Overview(), displayedSurfaceRefinement: JSON.stringify({
        Method: 'projected-adaptive-xyz', ProjectedVertices: 59980, SplitEdges: 33130 }) }, Target);
    Assert.equal(Result.Mode, 'high');
    Assert.equal(Result.Badge, 'HIGH DETAIL');
    Assert.equal(Result.MatchesTarget, false);
    Assert.match(Result.Actual, /field-projected vertices/);
    Assert.match(Result.Guidance, /not a dense whole-region lattice/);
});
