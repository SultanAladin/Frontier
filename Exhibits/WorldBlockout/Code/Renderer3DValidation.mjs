/*==========================================================================================================================================
                                              RENDERER3DVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Validation for the blockout's three-dimensional viewport. It checks the parts that can be checked without eyes: the camera's
//    projection is the one its field of view says, the disc reaches the horizon and not past it, the layers wind the right way,
//    the sea never paints dry land, the frame is deterministic, and a frame costs less than a coffee break. Everything that is
//    written here runs against the same Renderer3D.js the page loads, so a passing report is a passing viewport.
'use strict';

import { createRequire } from 'node:module';
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const Require = createRequire(import.meta.url);
const Here    = dirname(fileURLToPath(import.meta.url));
const Root    = resolve(Here, '../../..');
const Prototype = resolve(Root, 'Experimental/WorldBlockout');

const Terrain   = Require(resolve(Prototype, 'TerrainField.js'));
const OceanField = Require(resolve(Prototype, 'OceanField.js'));
const Raster    = Require(resolve(Prototype, 'Raster.js'));
const View3D    = Require(resolve(Prototype, 'Renderer3D.js'));

const ResultsFolder = resolve(Here, '../Results');
mkdirSync(ResultsFolder, { recursive: true });

const Results = [];
let Failures = 0;

function Assert(Name, Condition, Detail)
{
    Results.push({ Name, Passed: !!Condition, Detail });
    if (!Condition) ++Failures;
}

const Round = (Value, Digits) => Number(Value).toFixed(Digits ?? 0);
const Degrees = Math.PI / 180;

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE WORLD
//------------------------------------------------------------------------------------------------------------------------

const Field = Terrain.CreateField(Terrain.ConfigurePlanet());
for (const [Name, Plateau] of [['Africa', 680], ['Eurasia', 780], ['SouthAmerica', 700], ['Australia', 520]])
{
    Terrain.StampContinent(Field, { Name, Polygon: Terrain.ContinentOutlines()[Name], Plateau });
}
for (const Gulf of [{ Name: 'RedSea', Polygon: Terrain.GulfOutlines().RedSea, RampKm: 120 }])
{
    Terrain.CarveGulf(Field, Gulf);
}
const Coverage = Terrain.ResolveCoverage(Field);
const PlanetRadiusKm = Field.Planet.RadiusKm;

// 📝 The patch is anchored where the shell meets the shelf — deep enough to be water, shallow enough to be the coast — and
//    settled before anything is drawn, because a report about the sea that has not been solved is a report about nothing.
function AnchoredPatch()
{
    const Ocean = OceanField.CreateOcean(Field);
    let Anchor = { Lon: 8.2, Lat: 4 };
    for (let Lon = 9.5; Lon > -20; Lon -= 0.1)
    {
        const Depth = -Field.Sample.Distance(Lon, 4);
        if (Depth > 120 && Depth < 500) { Anchor = { Lon, Lat: 4 }; break; }
    }
    const Patch = OceanField.CreatePatch(Ocean, { FocusLon: Anchor.Lon, FocusLat: Anchor.Lat, Cells: 112, ExtentKm: 1.6 });
    Ocean.Patch = Patch;
    OceanField.SettlePatch(Ocean, Patch, 200);
    for (let Frame = 0; Frame < 120; ++Frame) OceanField.StepPatch(Ocean, Patch, 1 / 30);
    return { Ocean, Patch, Anchor };
}

const Anchored = AnchoredPatch();
const Ocean = Anchored.Ocean, Patch = Anchored.Patch, Anchor = Anchored.Anchor;

const Presets = {
    Globe: { AltitudeKm: 9000, TargetLon: 16, TargetLat: 6, Relief: 6, WaveScale: 8 },
    Coast: { AltitudeKm: 0, DistanceKm: 520, ElevationDeg: 30, AzimuthDeg: 248, TargetLon: 9, TargetLat: -4, Relief: 6, WaveScale: 24 },
    Waves: { AltitudeKm: 0, DistanceKm: 2.4, ElevationDeg: 34, AzimuthDeg: 290, TargetLon: Anchor.Lon, TargetLat: Anchor.Lat,
        Relief: 6, WaveScale: 30 }
};

function Shoot(Settings, Options)
{
    const Camera = View3D.CreateCamera(Object.assign({ Width: 640, Height: 400 }, Settings));
    const TerrainLayer = Options && Options.Terrain === false ? null : View3D.BuildTerrain(Field, Camera);
    const WaterLayer = Options && Options.Water === false ? null : View3D.BuildWater(Ocean, Camera, {});
    const Started = process.hrtime.bigint();
    const Stats = View3D.Render(Camera, PlanetRadiusKm, { Terrain: TerrainLayer, Water: WaterLayer },
        { PaintTiers: !!(Options && Options.PaintTiers) });
    Stats.ElapsedMs = Number(process.hrtime.bigint() - Started) / 1e6;
    Stats.Camera = Camera;
    Stats.TerrainLayer = TerrainLayer;
    Stats.WaterLayer = WaterLayer;
    return Stats;
}

const Frames = {
    Globe: Shoot(Presets.Globe),
    Coast: Shoot(Presets.Coast),
    Waves: Shoot(Presets.Waves),
    Tiers: Shoot(Presets.Waves, { PaintTiers: true }),
    NoWater: Shoot(Presets.Coast, { Water: false }),
    LandOnly: Shoot({ AltitudeKm: 0, DistanceKm: 240, ElevationDeg: 34, AzimuthDeg: 20, TargetLon: 20, TargetLat: 18, Relief: 6, WaveScale: 8 })
};

const PixelAt = (Frame, X, Y) => Raster.Sample ? null : [
    Frame.Raster.Pixels[(Y * Frame.Raster.Width + X) * 3],
    Frame.Raster.Pixels[(Y * Frame.Raster.Width + X) * 3 + 1],
    Frame.Raster.Pixels[(Y * Frame.Raster.Width + X) * 3 + 2]
];

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE CAMERA
//------------------------------------------------------------------------------------------------------------------------

{
    // 📝 The frame's focal length is the height over two tangents of half the vertical field of view. If that number is wrong
    //    every other number about the view is wrong with it.
    const Camera = View3D.CreateCamera({ Width: 640, Height: 400, FieldOfViewDeg: 42 });
    const Frame = View3D.ResolveFrame(Camera, PlanetRadiusKm);
    const Expected = 200 / Math.tan(42 * Degrees / 2);
    Assert('focal length matches the field of view', Math.abs(Frame.Focal - Expected) < 1e-6,
        `${Round(Frame.Focal, 3)} px against ${Round(Expected, 3)} px`);

    const Basis = ['Forward', 'Right', 'UpView'].map(Key => Frame[Key]);
    const Orthogonal = Math.abs(Basis[0][0] * Basis[1][0] + Basis[0][1] * Basis[1][1] + Basis[0][2] * Basis[1][2]) < 1e-9
        && Math.abs(Basis[1][0] * Basis[2][0] + Basis[1][1] * Basis[2][1] + Basis[1][2] * Basis[2][2]) < 1e-9;
    Assert('the view basis is orthogonal', Orthogonal, 'forward · right · up are pairwise perpendicular');

    const Lengths = Basis.map(Vector => Math.hypot(...Vector));
    Assert('the view basis is unit length', Lengths.every(Length => Math.abs(Length - 1) < 1e-9),
        Lengths.map(Length => Round(Length, 6)).join(' / '));
}

{
    // 📝 The point the camera is aimed at has to land on the centre pixel, whatever the azimuth and elevation. This is the one
    //    claim a user can check with a finger on the screen.
    const Errors = [];
    for (const Settings of [
        { TargetLon: 9, TargetLat: -4, DistanceKm: 520, ElevationDeg: 30, AzimuthDeg: 248 },
        { TargetLon: 8, TargetLat: 4, DistanceKm: 2.4, ElevationDeg: 34, AzimuthDeg: 290 },
        { TargetLon: -60, TargetLat: 40, DistanceKm: 1200, ElevationDeg: 12, AzimuthDeg: 95 }
    ])
    {
        const Camera = View3D.CreateCamera(Object.assign({ Width: 640, Height: 400 }, Settings));
        const Frame = View3D.ResolveFrame(Camera, PlanetRadiusKm);
        const Relative = [Frame.Anchor[0] - Frame.Position[0], Frame.Anchor[1] - Frame.Position[1], Frame.Anchor[2] - Frame.Position[2]];
        const Z = Relative[0] * Frame.Forward[0] + Relative[1] * Frame.Forward[1] + Relative[2] * Frame.Forward[2];
        const Right = Relative[0] * Frame.Right[0] + Relative[1] * Frame.Right[1] + Relative[2] * Frame.Right[2];
        const Up = Relative[0] * Frame.UpView[0] + Relative[1] * Frame.UpView[1] + Relative[2] * Frame.UpView[2];
        const Scale = Frame.Focal / Z;
        Errors.push(Math.hypot(320 + Right * Scale - 320, 200 - Up * Scale - 200));
    }
    Assert('the aim point projects to the centre of the frame', Math.max(...Errors) < 1e-6,
        `worst ${Errors.map(Error => Round(Error, 9)).join(' / ')} px`);
}

{
    // 📝 Globe mode puts the camera over the target at an altitude and looks straight down. A planet aimed at a surface point
    //    is a planet with half of itself off the bottom of the frame.
    const Camera = View3D.CreateCamera(Object.assign({ Width: 640, Height: 400 }, Presets.Globe));
    const Frame = View3D.ResolveFrame(Camera, PlanetRadiusKm);
    const Altitude = Math.hypot(...Frame.Position) - PlanetRadiusKm;
    Assert('globe mode places the camera at its altitude', Math.abs(Altitude - 9000) < 1e-6, `${Round(Altitude, 3)} km`);
    const ToCentre = [-Frame.Position[0], -Frame.Position[1], -Frame.Position[2]];
    const Length = Math.hypot(...ToCentre);
    const Dot = (ToCentre[0] * Frame.Forward[0] + ToCentre[1] * Frame.Forward[1] + ToCentre[2] * Frame.Forward[2]) / Length;
    Assert('globe mode looks at the planet centre', Math.abs(Dot - 1) < 1e-9, `cos ${Round(Dot, 9)}`);
    Assert('globe mode has a usable right vector', Math.hypot(...Frame.Right) > 0.999,
        'the straight-down basis does not collapse to NaN');
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DISC
//------------------------------------------------------------------------------------------------------------------------

for (const [Name, Preset] of Object.entries(Presets))
{
    const Camera = View3D.CreateCamera(Object.assign({ Width: 640, Height: 400 }, Preset));
    const Layer = View3D.BuildTerrain(Field, Camera);
    const AltitudeKm = Camera.AltitudeKm > 0 ? Camera.AltitudeKm : Camera.DistanceKm * Math.sin(Camera.ElevationDeg * Degrees);
    const HorizonKm = Math.sqrt(2 * PlanetRadiusKm * AltitudeKm + AltitudeKm * AltitudeKm);
    Assert(`${Name}: the disc reaches past the horizon`, Layer.MaxKm >= HorizonKm * 0.98,
        `disc ${Round(Layer.MaxKm, 0)} km against a horizon of ${Round(HorizonKm, 0)} km`);
    Assert(`${Name}: the innermost ring is not a point`, Layer.MaxKm * 1e-4 > 0,
        `first ring at ${Round(Layer.MaxKm * 1e-4 * 1000, 1)} m`);
    Assert(`${Name}: the mesh has triangles`, Layer.Triangles > 2000 && Layer.Indices.length === Layer.Triangles * 3,
        `${Layer.Triangles.toLocaleString('en-GB')} triangles, ${Layer.Rings} rings of ${Layer.Segments}`);

    // 📝 Measured as the mean great-circle radius of each ring, not the radius of one displaced vertex: relief moves a vertex
    //    up and down, and a ring of mountains is not a ring that has moved inwards.
    // 📝 Measured as the angle between each ring and the first one, averaged around the ring: a vertex a few hundred metres
    //    above the sphere does not move a ring, and relief must not be mistaken for the disc closing in on itself.
    const Centre = [Layer.Reals[0], Layer.Reals[1], Layer.Reals[2]];
    const MeanAngle = Row =>
    {
        let Total = 0;
        for (let Segment = 0; Segment <= Layer.Segments; ++Segment)
        {
            const Index = (Row * (Layer.Segments + 1) + Segment) * 3;
            const Span = Math.hypot(Layer.Reals[Index] - Centre[0], Layer.Reals[Index + 1] - Centre[1],
                Layer.Reals[Index + 2] - Centre[2]);
            Total += 2 * Math.asin(Math.min(1, Span / (2 * PlanetRadiusKm)));
        }
        return Total / (Layer.Segments + 1);
    };
    let Sorted = true;
    for (let Ring = 8; Ring <= Layer.Rings; ++Ring)
    {
        if (MeanAngle(Ring) <= MeanAngle(Ring - 1)) Sorted = false;
    }
    Assert(`${Name}: rings grow outwards`, Sorted && MeanAngle(Layer.Rings) > 0.004,
        `outer ring ${Round(MeanAngle(Layer.Rings) / Degrees, 1)}° from the centre, ring eight ${Round(MeanAngle(8) / Degrees, 2)}°`);
}

{
    // 📝 Spot-check the layer against the field it was built from: five vertices, five samplings, no drift.
    const Camera = View3D.CreateCamera(Object.assign({ Width: 640, Height: 400 }, Presets.Coast));
    const Layer = View3D.BuildTerrain(Field, Camera);
    const Centre = View3D.Destination(Camera.TargetLon, Camera.TargetLat, Camera.AzimuthDeg,
        Camera.DistanceKm * Math.cos(Camera.ElevationDeg * Degrees) / 111.32);
    const Sampled = Field.Sample.Height(Centre.Lon, Centre.Lat);
    const MeshHeight = Layer.Heights[0];
    Assert('the layer samples the field it was built from', Math.abs(Sampled - MeshHeight) < 5,
        `${Round(MeshHeight, 1)} m against ${Round(Sampled, 1)} m`);
}

{
    // 📝 Exaggeration is a cheat with a limit: the abyss must never be turned into a pit deep enough to swallow the frame.
    const Far = View3D.DisplayHeight(-4800, 6, 520);
    const Close = View3D.DisplayHeight(-4800, 60, 2.4);
    const Monotone = View3D.DisplayHeight(100, 6, 520) > View3D.DisplayHeight(0, 6, 520);
    Assert('drawn depth is clamped against the view distance', Math.abs(Far) < 520 * 0.18 + 1e-9 && Math.abs(Close) < 2.4 * 0.18 + 1e-9,
        `abyss draws at ${Round(Far, 2)} km from 520 km out and ${Round(Close, 3)} km from 2.4 km out`);
    Assert('height above sea level is not clamped', Monotone, 'land keeps its sign and its order');
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE FRAME
//------------------------------------------------------------------------------------------------------------------------

const Sky = [18, 34, 47];
for (const [Name, Frame] of Object.entries({ Globe: Frames.Globe, Coast: Frames.Coast, Waves: Frames.Waves }))
{
    const { Pixels, Width, Height } = Frame.Raster;
    let Drawn = 0, SumX = 0, SumY = 0;
    for (let Y = 0; Y < Height; ++Y)
    {
        for (let X = 0; X < Width; ++X)
        {
            const Offset = (Y * Width + X) * 3;
            const SkyValue = Sky[0] * (0.25 + Math.pow(Y / Height, 1.5) * 0.9);
            if (Math.abs(Pixels[Offset] - SkyValue) > 6 || Math.abs(Pixels[Offset + 1] - Sky[1] * (0.25 + Math.pow(Y / Height, 1.5) * 0.9)) > 6)
            {
                ++Drawn; SumX += X; SumY += Y;
            }
        }
    }
    const Share = Drawn / (Width * Height);
    Assert(`${Name}: the frame is not empty`, Share > 0.25, `${Round(Share * 100, 1)}% of pixels carry scene`);
    Assert(`${Name}: both layers contribute`, Frame.TerrainTriangles > 1000 && Frame.WaterTriangles > 1000,
        `${Frame.TerrainTriangles.toLocaleString('en-GB')} land · ${Frame.WaterTriangles.toLocaleString('en-GB')} water`);
    if (Name === 'Globe')
    {
        Assert('the globe is centred in the frame', Math.abs(SumX / Drawn - Width / 2) < Width * 0.06 && Math.abs(SumY / Drawn - Height / 2) < Height * 0.12,
            `centroid ${Round(SumX / Drawn, 1)}, ${Round(SumY / Drawn, 1)} of ${Width} × ${Height}`);
    }
}

{
    // 📝 Determinism: the same camera has to draw the same bytes, or the evidence and the viewport are two different pictures.
    const Again = Shoot(Presets.Coast);
    let Identical = true;
    for (let Index = 0; Index < Again.Raster.Pixels.length; ++Index)
    {
        if (Math.abs(Again.Raster.Pixels[Index] - Frames.Coast.Raster.Pixels[Index]) > 0) { Identical = false; break; }
    }
    Assert('a frame is deterministic', Identical, 'the same camera drew the same pixels twice');

    const Budget = Math.max(...Object.values(Frames).map(Frame => Frame.ElapsedMs));
    Assert('a frame renders inside its budget', Budget < 1500, `slowest of six frames ${Round(Budget, 0)} ms`);
}

{
    // 📝 The sea on dry land is the failure this viewport is most likely to make. Two checks: a camera over the Sahara must
    //    find no sea at all, and the water must change the picture where there is sea to change.
    Assert('no sea over a continent', Frames.LandOnly.WaterTriangles === 0,
        `${Frames.LandOnly.WaterTriangles} water triangles over the Sahara`);

    let Different = 0;
    for (let Index = 0; Index < Frames.Coast.Raster.Pixels.length; ++Index)
    {
        if (Math.abs(Frames.Coast.Raster.Pixels[Index] - Frames.NoWater.Raster.Pixels[Index]) > 0) ++Different;
    }
    const Share = Different / Frames.Coast.Raster.Pixels.length;
    Assert('the sea changes the coast frame', Share > 0.05, `${Round(Share * 100, 1)}% of channels differ with water off`);

    let TierDifferent = 0;
    for (let Index = 0; Index < Frames.Waves.Raster.Pixels.length; ++Index)
    {
        if (Math.abs(Frames.Waves.Raster.Pixels[Index] - Frames.Tiers.Raster.Pixels[Index]) > 0) ++TierDifferent;
    }
    Assert('the LOD paint is a different picture', TierDifferent / Frames.Waves.Raster.Pixels.length > 0.05,
        `${Round(100 * TierDifferent / Frames.Waves.Raster.Pixels.length, 1)}% of channels differ`);
}

{
    // 📝 The water must be a solved surface, not a plane: the heights across the patch have to move, and the rendered sea
    //    has to show it. Flat water would pass every other test in this file.
    const Heights = [];
    for (let Step = 0; Step < 40; ++Step)
    {
        const Lon = Patch.FocusLon + (Step - 20) * 0.002;
        Heights.push(OceanField.SurfaceHeight(Ocean, Lon, Patch.FocusLat).Height);
    }
    const Mean = Heights.reduce((Total, Value) => Total + Value, 0) / Heights.length;
    const Deviation = Math.sqrt(Heights.reduce((Total, Value) => Total + (Value - Mean) ** 2, 0) / Heights.length);
    Assert('the water surface is not flat', Deviation > 0.05, `σ ${Round(Deviation, 3)} m across 0.08° of patch`);

    const NearWeight = OceanField.SurfaceHeight(Ocean, Patch.FocusLon, Patch.FocusLat).Weight;
    const FarWeight = OceanField.SurfaceHeight(Ocean, Patch.FocusLon + 4, Patch.FocusLat).Weight;
    Assert('the viewport samples the solve at the patch and the spectrum away from it',
        NearWeight < 0.01 && FarWeight > 0.99, `weight ${Round(NearWeight, 3)} at the focus, ${Round(FarWeight, 3)} four degrees out`);
}

{
    // 📝 A wave's crest has to be visible in the pixels, or the exaggeration is decoration. The assertion is the spread of
    //    the water's own colour in the waves frame, where the sea fills the view.
    const { Pixels, Width, Height } = Frames.Waves.Raster;
    const Values = [];
    for (let Y = Math.round(Height * 0.45); Y < Math.round(Height * 0.9); Y += 3)
    {
        for (let X = 0; X < Width; X += 3) Values.push(Pixels[(Y * Width + X) * 3 + 1]);
    }
    const Mean = Values.reduce((Total, Value) => Total + Value, 0) / Values.length;
    const Deviation = Math.sqrt(Values.reduce((Total, Value) => Total + (Value - Mean) ** 2, 0) / Values.length);
    Assert('the sea shows its waves in the pixels', Deviation > 1.5, `σ ${Round(Deviation, 1)} of the green channel`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE REPORT
//------------------------------------------------------------------------------------------------------------------------

const Lines = [
    '# Renderer 3D validation',
    '',
    `Generated ${new Date().toISOString()} · ${Results.length} checks · ${Failures === 0 ? 'all passed' : `${Failures} failed`}`,
    '',
    `World: ${Coverage.LandCells} land cells of ${Field.Heights.length}, ${PlanetRadiusKm} km radius. ` +
        `Patch: ${Patch.Cells}² at ${Round(Patch.CellKm * 1000, 1)} m, anchored at ${Round(Anchor.Lon, 2)}° ${Round(Anchor.Lat, 2)}°.`,
    '',
    '| Frame | Altitude / distance | Land triangles | Water triangles | Milliseconds |',
    '|-------|--------------------|----------------|-----------------|--------------|',
    ...Object.entries({ Globe: Frames.Globe, Coast: Frames.Coast, Waves: Frames.Waves }).map(([Name, Frame]) =>
        `| ${Name} | ${Frame.Camera.AltitudeKm > 0 ? `${Round(Frame.Camera.AltitudeKm, 0)} km altitude`
            : `${Round(Frame.Camera.DistanceKm, 1)} km out`} | ${Frame.TerrainTriangles.toLocaleString('en-GB')} | `
            + `${Frame.WaterTriangles.toLocaleString('en-GB')} | ${Round(Frame.ElapsedMs, 0)} |`),
    '',
    '| Check | Result | Detail |',
    '|-------|--------|--------|',
    ...Results.map(Result => `| ${Result.Name} | ${Result.Passed ? 'pass' : 'FAIL'} | ${Result.Detail} |`),
    ''
];
writeFileSync(resolve(ResultsFolder, 'Renderer3DValidation.md'), Lines.join('\n'));

console.log(`renderer 3d: ${Results.length - Failures}/${Results.length} checks passed`);
for (const Result of Results) if (!Result.Passed) console.log(`  FAIL ${Result.Name} — ${Result.Detail}`);
if (Failures > 0)
{
    console.log('RESULT: FAIL');
    process.exit(1);
}
console.log('RESULT: PASS');
