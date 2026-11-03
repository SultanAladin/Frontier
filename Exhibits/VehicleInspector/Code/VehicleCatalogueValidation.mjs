/*==========================================================================================================================================
                                                   VEHICLECATALOGUEVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Node checks for the vehicle catalogue: units, curve editing, performance estimates and the TOML record.
'use strict';

import { createRequire } from 'node:module';
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const Require = createRequire(import.meta.url);
const Here    = dirname(fileURLToPath(import.meta.url));
const Catalogue = Require(resolve(Here, '../../../Experimental/VehicleInspector/VehicleCatalogue.js'));

const Results = [];
let Failures = 0;

function Assert(Name, Condition, Detail)
{
    Results.push({ Name, Passed: !!Condition, Detail });
    if (!Condition) ++Failures;
}

function Near(Left, Right, Tolerance)
{
    return Math.abs(Left - Right) <= Tolerance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        UNITS
//------------------------------------------------------------------------------------------------------------------------

const UnitKeys = ['Mass', 'Length', 'Distance', 'Speed', 'Torque', 'Power', 'Pressure', 'Temperature', 'Volume', 'Force', 'Area',
                  'SpecificPower', 'Rpm', 'Percent', 'Degree', 'Ratio', 'Seconds', 'Litres', 'Flow', 'Boost', 'Inertia'];
for (const System of ['Metric', 'Imperial'])
{
    let WorstError = 0;
    for (const Key of UnitKeys)
        for (const Quantity of [-40, 0, 1, 92, 620, 1284, 8000])
            WorstError = Math.max(WorstError, Math.abs(Catalogue.DecodeQuantity(Catalogue.ConvertQuantity(Quantity, Key, System), Key, System) - Quantity));
    Assert(`unit round trip · ${System}`, WorstError < 1e-9, `largest round-trip error ${WorstError.toExponential(2)}`);
}

const Landmarks = [
    ['Torque 620 Nm → lb·ft', Catalogue.FormatQuantity(620, 1, 'Torque', 'Imperial').Text, '457.3'],
    ['Power 371 kW → hp', Catalogue.FormatQuantity(371, 0, 'Power', 'Imperial').Text, '498'],
    ['Speed 100 km/h → mph', Catalogue.FormatQuantity(100, 0, 'Speed', 'Imperial').Text, '62'],
    ['Mass 1284 kg → lb', Catalogue.FormatQuantity(1284, 0, 'Mass', 'Imperial').Text, '2831'],
    ['Pressure 220 kPa → psi', Catalogue.FormatQuantity(220, 1, 'Pressure', 'Imperial').Text, '31.9'],
    ['Temperature 92 °C → °F', Catalogue.FormatQuantity(92, 1, 'Temperature', 'Imperial').Text, '197.6'],
    ['Temperature 0 °C → °F', Catalogue.FormatQuantity(0, 0, 'Temperature', 'Imperial').Text, '32'],
    ['Length 2720 mm → in', Catalogue.FormatQuantity(2720, 0, 'Length', 'Imperial').Text, '107']
];
for (const [Name, Actual, Expected] of Landmarks) Assert(Name, Actual === Expected, `${Actual} (expected ${Expected})`);

//------------------------------------------------------------------------------------------------------------------------
//                                                  CURVE EVALUATION
//------------------------------------------------------------------------------------------------------------------------

const Profiles = Catalogue.ConfigureCurveProfiles();
const Specification = Catalogue.ConfigureVehicleSpecification();
const Torque = Profiles.find(Profile => Profile.Key === 'EngineTorque');
const Power  = Profiles.find(Profile => Profile.Key === 'EnginePower');

const Linear = { ...Torque, Interpolation: 'Linear', Points: [[1000, 200], [2000, 400]] };
Assert('linear midpoint', Near(Catalogue.EvaluateCurveSamples(Linear.Points, 'Linear', 1500), 300, 1e-9),
    `evaluated ${Catalogue.EvaluateCurveSamples(Linear.Points, 'Linear', 1500)}`);
Assert('linear clamps below the first point', Catalogue.EvaluateCurveSamples(Linear.Points, 'Linear', 200) === 200, 'returns the first Y');
Assert('linear clamps above the last point', Catalogue.EvaluateCurveSamples(Linear.Points, 'Linear', 9000) === 400, 'returns the last Y');

let PassesThroughPoints = true, LargestOvershoot = 0;
const SmoothPoints = Catalogue.SortCurvePoints(Torque.Points);
const Tangents = Catalogue.ResolveMonotoneTangents(SmoothPoints);
for (const [X, Y] of SmoothPoints) if (!Near(Catalogue.EvaluateCurveSamples(SmoothPoints, 'Smooth', X), Y, 1e-6)) PassesThroughPoints = false;
for (let Index = 0; Index < SmoothPoints.length - 1; ++Index)
{
    const Low = Math.min(SmoothPoints[Index][1], SmoothPoints[Index + 1][1]);
    const High = Math.max(SmoothPoints[Index][1], SmoothPoints[Index + 1][1]);
    for (let Step = 0; Step <= 20; ++Step)
    {
        const X = SmoothPoints[Index][0] + (SmoothPoints[Index + 1][0] - SmoothPoints[Index][0]) * Step / 20;
        const Y = Catalogue.EvaluateCurveSamples(SmoothPoints, 'Smooth', X);
        LargestOvershoot = Math.max(LargestOvershoot, Math.max(0, Low - Y), Math.max(0, Y - High));
    }
}
Assert('smooth curve passes through every authored point', PassesThroughPoints, `${SmoothPoints.length} points`);
Assert('smooth curve does not overshoot between points', LargestOvershoot < 1e-6, `largest overshoot ${LargestOvershoot.toExponential(2)}`);
Assert('tangents are finite', Tangents.every(Tangent => Number.isFinite(Tangent)), `${Tangents.length} tangents`);

// 📝 The derived power curve must be torque × ω, not a second authored list.
const Derived = Catalogue.ResolveCurvePoints(Power, Specification, Profiles);
let LargestPowerError = 0;
for (const [Speed, Kilowatts] of Derived)
{
    const Expected = Catalogue.EvaluateCurve(Torque, Speed, Specification, Profiles) * Speed * (2 * Math.PI / 60) / 1000;
    LargestPowerError = Math.max(LargestPowerError, Math.abs(Expected - Kilowatts));
}
Assert('power curve equals torque × engine speed', LargestPowerError < 1e-9, `largest error ${LargestPowerError.toExponential(2)} kW`);
const PeakPower = Catalogue.ResolveCurvePeak(Power, Specification, Profiles);
Assert('peak power is inside the curve range', PeakPower.X > 5000 && PeakPower.X < 7800, `${PeakPower.X.toFixed(0)} rpm · ${PeakPower.Y.toFixed(1)} kW`);

//------------------------------------------------------------------------------------------------------------------------
//                                                     CURVE EDITING
//------------------------------------------------------------------------------------------------------------------------

const Editable = Catalogue.ConfigureCurveProfiles().find(Profile => Profile.Key === 'EngineTorque');
const OriginalCount = Editable.Points.length;
const Inserted = Catalogue.InsertCurvePoint(Editable, 4000, 600);
Assert('insert returns the new index', Inserted === 4, `index ${Inserted}`);
Assert('insert keeps X ordering', Editable.Points.every((Point, Index) => Index === 0 || Point[0] > Editable.Points[Index - 1][0]),
    Editable.Points.map(Point => Point[0]).join(', '));
Assert('insert adds one point', Editable.Points.length === OriginalCount + 1, `${OriginalCount} → ${Editable.Points.length}`);
Assert('insert too close to a neighbour is refused', Catalogue.InsertCurvePoint(Editable, 4010, 600) === -1, 'refused within the minimum gap');
Assert('insert clamps Y to the axis', Catalogue.InsertCurvePoint(Editable, 5200, 9999) >= 0 && Editable.Points.find(Point => Point[0] === 5200)[1] === 800,
    'clamped to the 800 Nm ceiling');

Catalogue.InvalidateCurvePointTangents(Editable);
const RelocatedIndex = Catalogue.RelocateCurvePoint(Editable, 4, 3400, 640);
Assert('relocate clamps X between neighbours', Editable.Points[RelocatedIndex][0] >= Editable.Points[3][0]
    && Editable.Points[RelocatedIndex][0] <= Editable.Points[5][0],
    `X = ${Editable.Points[RelocatedIndex][0].toFixed(1)} between ${Editable.Points[3][0]} and ${Editable.Points[5][0]}`);
const BeforeRelocateCount = Editable.Points.length;
Catalogue.RelocateCurvePoint(Editable, 0, -5000, -200);
Assert('relocate clamps to the axis bounds', Editable.Points[0][0] === Editable.AxisMinimum || Editable.Points[0][0] === Editable.X.Minimum,
    `X = ${Editable.Points[0][0]}, Y = ${Editable.Points[0][1]}`);
Assert('relocate never adds a point', Editable.Points.length === BeforeRelocateCount, `${Editable.Points.length} points`);

while (Editable.Points.length > 2) Catalogue.RemoveCurvePoint(Editable, 0);
Assert('remove refuses the last two points', Catalogue.RemoveCurvePoint(Editable, 0) === false, `${Editable.Points.length} points remain`);
Assert('remove refuses an out-of-range index', Catalogue.RemoveCurvePoint(Editable, 9) === false, 'index 9 refused');

const Csv = Catalogue.EncodeCurveCsv(Torque, 'Metric');
Assert('CSV has one header and one row per point', Csv.split('\n').length === Torque.Points.length + 1, `${Csv.split('\n').length} rows`);
Assert('CSV names its axes in the active units', Csv.split('\n')[0].includes('rpm') && Csv.split('\n')[0].includes('Nm'), Csv.split('\n')[0]);

//------------------------------------------------------------------------------------------------------------------------
//                                                   PERFORMANCE ESTIMATES
//------------------------------------------------------------------------------------------------------------------------

const Tyres = Catalogue.ConfigureTyreRecords();
const Baseline = { Specification: Catalogue.ConfigureVehicleSpecification(), Profiles: Catalogue.ConfigureCurveProfiles(), Tyres };
const Performance = Catalogue.EstimatePerformance(Baseline.Specification, Baseline.Profiles, Baseline.Tyres);
Assert('0–100 km/h is reached', Performance.ZeroToHundredReached, `${Performance.ZeroToHundred.toFixed(2)} s`);
Assert('0–100 km/h is plausible for the record', Performance.ZeroToHundred > 2.4 && Performance.ZeroToHundred < 5.2,
    `${Performance.ZeroToHundred.toFixed(2)} s · ${Performance.LaunchDistance.toFixed(1)} m`);
Assert('top speed is redline and drag limited', Performance.TopSpeed > 250 && Performance.TopSpeed < 380, `${Performance.TopSpeed.toFixed(0)} km/h`);
Assert('braking distance 100–0 is plausible', Performance.BrakingDistance > 24 && Performance.BrakingDistance < 40,
    `${Performance.BrakingDistance.toFixed(1)} m`);
Assert('power to weight matches peak power and mass', Near(Performance.PowerToWeight, Performance.PeakPower / 1.284, 0.6),
    `${Performance.PowerToWeight.toFixed(0)} kW/t`);
Assert('peak torque matches the authored curve', Near(Performance.PeakTorque, 640, 1e-9) && Performance.PeakTorqueSpeed === 4500,
    `${Performance.PeakTorque} Nm at ${Performance.PeakTorqueSpeed} rpm`);
Assert('launch trace is time ordered and starts at rest', Performance.LaunchTrace.every((Point, Index, Trace) =>
    Index === 0 || Point[0] > Trace[Index - 1][0]) && Performance.LaunchTrace[0][0] === 0 && Performance.LaunchTrace[0][1] === 0,
    `${Performance.LaunchTrace.length} samples`);

// 📝 A custom vehicle is the point of the curves: more torque must make it quicker and faster, extra mass must slow it.
const Stronger = { Specification: Catalogue.ConfigureVehicleSpecification(), Profiles: Catalogue.ConfigureCurveProfiles(), Tyres: [] };
for (const Profile of Stronger.Profiles) if (Profile.Key === 'EngineTorque') Profile.Points = Profile.Points.map(([X, Y]) => [X, Y * 1.2]);
const StrongerPerformance = Catalogue.EstimatePerformance(Stronger.Specification, Stronger.Profiles, []);
Assert('more torque lowers the 0–100 time', StrongerPerformance.ZeroToHundred < Performance.ZeroToHundred,
    `${Performance.ZeroToHundred.toFixed(2)} s → ${StrongerPerformance.ZeroToHundred.toFixed(2)} s`);
Assert('more torque raises the top speed', StrongerPerformance.TopSpeed > Performance.TopSpeed,
    `${Performance.TopSpeed.toFixed(0)} → ${StrongerPerformance.TopSpeed.toFixed(0)} km/h`);

const Heavier = { Specification: Catalogue.ConfigureVehicleSpecification(), Profiles: Catalogue.ConfigureCurveProfiles(), Tyres: [] };
Heavier.Specification.Identity.KerbMass = 1584;
const HeavierPerformance = Catalogue.EstimatePerformance(Heavier.Specification, Heavier.Profiles, []);
Assert('more mass raises the 0–100 time', HeavierPerformance.ZeroToHundred > Performance.ZeroToHundred,
    `${Performance.ZeroToHundred.toFixed(2)} s → ${HeavierPerformance.ZeroToHundred.toFixed(2)} s`);
Assert('more mass lowers the power-to-weight ratio', HeavierPerformance.PowerToWeight < Performance.PowerToWeight,
    `${Performance.PowerToWeight.toFixed(0)} → ${HeavierPerformance.PowerToWeight.toFixed(0)} kW/t`);

//------------------------------------------------------------------------------------------------------------------------
//                                                     TYRE RECORDS
//------------------------------------------------------------------------------------------------------------------------

const TyreRecords = Catalogue.ConfigureTyreRecords();
Assert('four tyres, each enclosed at the start', TyreRecords.length === 4 && TyreRecords.every(Tyre => Tyre.Enclosed), '4 enclosed');
Assert('front and rear pressures differ', TyreRecords[0].Pressure !== TyreRecords[2].Pressure,
    `${TyreRecords[0].Pressure} / ${TyreRecords[2].Pressure} kPa`);

Catalogue.DetachTyre(TyreRecords[1]);
Assert('detach clears the enclosure and keeps the record', TyreRecords[1].Enclosed === false && TyreRecords[1].Compound === 'Medium',
    `${TyreRecords[1].Id} detached`);
Assert('the detached tyre leaves the enclosed list', TyreRecords.filter(Tyre => !Tyre.Enclosed).length === 1, '1 detached');
Catalogue.AttachTyre(TyreRecords[1]);
Assert('attach restores the enclosure', TyreRecords[1].Enclosed === true, 'all four enclosed again');

Catalogue.FitTyreSet(TyreRecords, 'SetB', 'Soft');
Assert('fitting a set replaces the compound', TyreRecords.every(Tyre => Tyre.Compound === 'Soft' && Tyre.SetId === 'SetB'), 'Soft · Set B');
Assert('fitting a set resets wear, laps and enclosure', TyreRecords.every(Tyre => Tyre.Wear === 0 && Tyre.StintLaps === 0 && Tyre.Enclosed),
    'wear 0 %, 0 laps, enclosed');
Assert('fitting a set restores the cold pressure', TyreRecords[0].Pressure === 186 && TyreRecords[2].Pressure === 174,
    `${TyreRecords[0].Pressure} / ${TyreRecords[2].Pressure} kPa`);

const Window = Catalogue.ResolveCompound('Medium');
Assert('temperature window classifies below, inside and above', Catalogue.ResolveTemperatureWindow('Medium', Window.WindowLow - 1) === 'Cold'
    && Catalogue.ResolveTemperatureWindow('Medium', Window.WindowLow) === 'Optimal'
    && Catalogue.ResolveTemperatureWindow('Medium', Window.WindowHigh + 1) === 'Hot',
    `window ${Window.WindowLow}–${Window.WindowHigh} °C`);
Assert('wear status escalates', Catalogue.ResolveWearStatus(10) === 'Fresh' && Catalogue.ResolveWearStatus(60) === 'Degraded'
    && Catalogue.ResolveWearStatus(90) === 'Worn', 'Fresh / Degraded / Worn');

//------------------------------------------------------------------------------------------------------------------------
//                                                      TOML RECORD
//------------------------------------------------------------------------------------------------------------------------

const Record = Catalogue.EncodeVehicleRecord(Specification, Profiles, Tyres, [Tyres[2]]).join('\n');
const Required = ['[vehicle]', 'identity = "GT-04"', 'kerb_mass_kg = 1284', '[vehicle.drivetrain]', 'gear_ratios = [3.62',
                  '[vehicle.engine]', 'redline_rpm = 7800', '[vehicle.thermal]', '[vehicle.aids]', '[vehicle.curves]',
                  'engine_torque_points = [[',
                  '[vehicle.tyres.tyre_front_left]', '[vehicle.tyres.tyre_rear_right]', 'enclosed = true', '[vehicle.pit_lane]',
                  'detached = ["TyreRearLeft"]'];
for (const Line of Required) Assert(`record contains ${Line}`, Record.includes(Line), Line);
Assert('the record omits the derived power curve', !Record.includes('engine_power_points'), 'only authored curves are written');
Assert('the record has no NaN or undefined', !/NaN|undefined/.test(Record), 'clean numbers');
Assert('torque points travel as an array of pairs', /engine_torque_points = \[\[\d+(\.\d+)?, \d+(\.\d+)?\](, \[)?/.test(Record),
    Record.split('\n').find(Line => Line.startsWith('engine_torque_points')).slice(0, 76) + '…');

//------------------------------------------------------------------------------------------------------------------------
//                                                       REPORT
//------------------------------------------------------------------------------------------------------------------------

const Passed = Results.length - Failures;
const Lines = [
    '# Vehicle catalogue validation',
    '',
    `Generated ${new Date().toISOString()} · Node ${process.version} · ${Passed}/${Results.length} checks passed.`,
    '',
    '| Check | Result | Observed |',
    '|-------|--------|----------|'
];
for (const Result of Results)
    Lines.push(`| ${Result.Name} | ${Result.Passed ? '✔️ pass' : '🔴 fail'} | ${String(Result.Detail).replace(/\|/g, '\\|')} |`);
Lines.push('', `**${Failures === 0 ? '🟢 all checks passed' : `🔴 ${Failures} check(s) failed`}**`, '');

const ResultsFolder = resolve(Here, '../Results');
mkdirSync(ResultsFolder, { recursive: true });
writeFileSync(resolve(ResultsFolder, 'CatalogueValidation.md'), Lines.join('\n'));

for (const Result of Results)
    if (!Result.Passed) console.log(`FAIL ${Result.Name} — ${Result.Detail}`);
console.log(`${Passed}/${Results.length} checks passed`);
console.log(`report: ${resolve(ResultsFolder, 'CatalogueValidation.md')}`);
process.exit(Failures === 0 ? 0 : 1);
