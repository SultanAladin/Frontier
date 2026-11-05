import { test } from 'node:test';
import assert from 'node:assert/strict';
import { RayTracingSpace, TlasNodeCapacity } from '../Source/RayTracingSpace.js';
import { OccupantRole } from '../Source/FacetStructure.js';

// ⚡ The static TLAS skip must be invisible: identical leaf inputs → identical node bytes, and any
//    mover (even a 6 mm settle) forces the full rebuild. Fakes mirror UpdateTlas's scene surface only.
function MakeWorld() {
    const Bounds = [
        { Minimum: [0, 0, 0], Maximum: [1, 1, 1] },
        { Minimum: [3, 0, 0], Maximum: [4, 1, 1] },
        { Minimum: [0, 3, 0], Maximum: [1, 4, 2] },
    ];
    const Scene = {
        GroundOccupant: -1,
        Occupants: Bounds.map(() => ({ Roles: OccupantRole.Raster, Inactive: false, Fragment: false })),
        QueryWorldBounds: (Index) => Bounds[Index],
    };
    const Space = Object.create(RayTracingSpace.prototype);
    Space.OccupantBlasRoot = new Int32Array([100, 200, 300]);
    Space.AllNodeBytes = new ArrayBuffer(TlasNodeCapacity * 32);
    Space.AllNodeScalars = new Float32Array(Space.AllNodeBytes);
    Space.AllNodeIntegers = new Uint32Array(Space.AllNodeBytes);
    Space.UsedTlasNodes = 0; Space.ActiveInstances = 0; Space.Exchange = null; Space.TlasSkips = 0;
    for (const Name of ['ScratchOrder', 'ScratchOccIdx']) Space[Name] = new Uint32Array(3);
    for (const Name of ['ScratchMinX','ScratchMinY','ScratchMinZ','ScratchMaxX','ScratchMaxY','ScratchMaxZ','ScratchCentX','ScratchCentY','ScratchCentZ']) Space[Name] = new Float32Array(3);
    return { Bounds, Scene, Space };
}

test('TLAS skip: identical inputs skip the rebuild and keep byte-identical nodes', () => {
    const { Scene, Space } = MakeWorld();
    Space.UpdateTlas(Scene, null);
    const FirstBytes = new Uint8Array(Space.AllNodeBytes.slice(0, Space.UsedTlasNodes * 32));
    const FirstNodes = Space.UsedTlasNodes;
    assert.ok(FirstNodes > 0);
    Space.UpdateTlas(Scene, null);
    assert.equal(Space.TlasSkips, 1, 'second identical frame is skipped');
    assert.deepEqual(new Uint8Array(Space.AllNodeBytes, 0, FirstNodes * 32), FirstBytes, 'nodes unchanged');
    Space.UpdateTlas(Scene, null);
    assert.equal(Space.TlasSkips, 2);
});

test('TLAS skip: a 6 mm mover (the parked-car settle) forces a rebuild', () => {
    const { Bounds, Scene, Space } = MakeWorld();
    Space.UpdateTlas(Scene, null);
    Space.UpdateTlas(Scene, null);
    assert.equal(Space.TlasSkips, 1);
    Bounds[1].Minimum[2] += 0.006; Bounds[1].Maximum[2] += 0.006; // the lobby settle from the TL;DR
    Space.UpdateTlas(Scene, null);
    assert.equal(Space.TlasSkips, 1, 'moved bound rebuilt instead of skipping');
    Space.UpdateTlas(Scene, null);
    assert.equal(Space.TlasSkips, 2, 'static again after the move');
});

test('TLAS skip: a BLAS root change (fracture) forces a rebuild', () => {
    const { Scene, Space } = MakeWorld();
    Space.UpdateTlas(Scene, null);
    Space.OccupantBlasRoot[2] = 999;
    Space.UpdateTlas(Scene, null);
    assert.equal(Space.TlasSkips, 0);
});
