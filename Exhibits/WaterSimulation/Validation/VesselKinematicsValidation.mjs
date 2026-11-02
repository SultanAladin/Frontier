/*====================================================================================================================================
                                                VESSEL KINEMATICS VALIDATION.MJS
====================================================================================================================================*/
// 🧩 Verifies the vessel points along its course, stays afloat on the height atlas, and emits into the grids

import path from 'path';
import { fileURLToPath, pathToFileURL } from 'url';

const Here = path.dirname(fileURLToPath(import.meta.url));
const SourceRoot = path.resolve(Here, '../../../Experimental/WaterSimulation/src');
const Module = (Name) => pathToFileURL(path.join(SourceRoot, Name)).href;

const { VesselBody } = await import(Module('VesselBody.js'));
const { Mat4FromPose } = await import(Module('LinearAlgebra.js'));
const { TerrainHeight } = await import(Module('TerrainShape.js'));

const Report = [];
let Failures = 0;

function Check(Label, Passed, Detail)
{
    if (!Passed) Failures++;
    Report.push({ Label: Label, Result: Passed ? 'PASS' : 'FAIL', Detail: Detail });
}

// Synthetic sea state: two crossed swells plus the real seabed clamp
const Sampler = (X, Z) =>
{
    const Surface = 0.55 * Math.sin(X * 0.11 + 1.3) * Math.cos(Z * 0.09) + 0.28 * Math.sin(Z * 0.23 + X * 0.05);
    return Math.max(Surface, TerrainHeight(X, Z) + 0.05);
};

const Settings = { VesselThrottle: 0.6, RippleEnabled: true, FoamEnabled: true };
const Vessel = new VesselBody({ CourseCenter: [0, -6], CourseRadius: 36 });

const RippleCalls = [];
const FoamCalls = [];
const RippleField = { QueueSplat: (X, Z, Strength, Size) => RippleCalls.push([X, Z, Strength, Size]) };
const FoamField = { QueueSplat: (X, Z, Strength, Size) => FoamCalls.push([X, Z, Strength, Size]) };

let MaximumBowError = 0.0;
let MaximumHeaveError = 0.0;
let SampleCount = 0;
let Finite = true;

for (let Index = 0; Index < 900; Index++)
{
    Vessel.Update(1.0 / 60.0, Sampler, Settings, Index / 60.0);
    Vessel.QueueWake(RippleField, FoamField, Settings, 1.0 / 60.0);

    if (!Vessel.Transform.every((Value) => Number.isFinite(Value)))
    {
        Finite = false;
    }

    if (Index > 240 && Index % 30 === 0)
    {
        // Bow world position from the transform must lead the stern along the direction of travel
        const BowOffset = [-Vessel.Transform[8] * 2.9, -Vessel.Transform[10] * 2.9];
        const SternOffset = [Vessel.Transform[8] * 3.1, Vessel.Transform[10] * 3.1];
        const Motion = [Vessel.Position[0] - BowOffset[0] * 0.0, 0];

        const Delta = SampleStep(Vessel, Settings, Sampler, Index);
        const Travel = [Delta[0], Delta[1]];
        const BowDirection = Normalize([BowOffset[0], BowOffset[1]]);
        const Alignment = BowDirection[0] * Travel[0] + BowDirection[1] * Travel[1];

        MaximumBowError = Math.max(MaximumBowError, Math.abs(1.0 - Alignment));

        const Surface = Sampler(Vessel.Position[0], Vessel.Position[2]);
        MaximumHeaveError = Math.max(MaximumHeaveError, Math.abs(Vessel.Position[1] - (Surface + 0.16)));
        SampleCount++;
    }
}

function SampleStep(Current, CurrentSettings, CurrentSampler, Index)
{
    const Start = [Current.Position[0], Current.Position[2]];
    Current.Update(1.0 / 60.0, CurrentSampler, CurrentSettings, Index / 60.0 + 0.0167);
    const End = [Current.Position[0], Current.Position[2]];
    return Normalize([End[0] - Start[0], End[1] - Start[1]]);
}

function Normalize(Vector)
{
    const Length = Math.hypot(Vector[0], Vector[1]) || 1.0;
    return [Vector[0] / Length, Vector[1] / Length];
}

Check('Vessel transform stays finite over 900 frames', Finite, '16 matrix elements checked every frame');
Check('Bow leads along the direction of travel', MaximumBowError < 0.14,
      `max |1 - bow·travel| = ${MaximumBowError.toFixed(3)} over ${SampleCount} samples`);
Check('Heave tracks the sampled surface', MaximumHeaveError < 0.32,
      `max |heave - (surface + 0.16)| = ${MaximumHeaveError.toFixed(3)} m`);
Check('Wake writes into the ripple grid', RippleCalls.length > 200, `${RippleCalls.length} ripple splats queued`);
Check('Wake writes into the foam grid', FoamCalls.length > 100, `${FoamCalls.length} foam splats queued`);

const Diagnostics = Vessel.EmitterDiagnostics();
const BowTerrain = TerrainHeight(Diagnostics.Bow[0], Diagnostics.Bow[2]);
const SternTerrain = TerrainHeight(Diagnostics.Stern[0], Diagnostics.Stern[2]);
Check('Vessel stays in navigable water', BowTerrain < 0.0 && SternTerrain < 0.0,
      `bow depth ${(-BowTerrain).toFixed(1)} m, stern depth ${(-SternTerrain).toFixed(1)} m`);

const Pose = Mat4FromPose(new Float32Array(16), [3.0, 1.0, -2.0], 0.7, 0.1, -0.05, 1.0);
Check('Pose matrix translation matches the requested position',
      Math.abs(Pose[12] - 3.0) < 1e-6 && Math.abs(Pose[13] - 1.0) < 1e-6 && Math.abs(Pose[14] - -2.0) < 1e-6,
      `translation (${Pose[12].toFixed(2)}, ${Pose[13].toFixed(2)}, ${Pose[14].toFixed(2)})`);

console.log('--- vessel kinematics validation ---');

for (const Entry of Report)
{
    console.log(`${Entry.Result.padEnd(5)} ${Entry.Label}`);
    console.log(`      ${Entry.Detail}`);
}

console.log('course radius        :', Vessel.CourseRadius.toFixed(1), 'm, speed', Vessel.Speed.toFixed(2), 'm/s');
console.log('result:', Failures === 0 ? 'PASS' : `${Failures} FAILURES`);
process.exitCode = Failures === 0 ? 0 : 1;
