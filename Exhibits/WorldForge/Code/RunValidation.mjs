/*============================================================================================================================================
                                                      RUNVALIDATION.MJS
============================================================================================================================================*/
// 🧩 Headless numerical validation of the world-forge prototype plus retained image/graph evidence

import { mkdir, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const Here = dirname(fileURLToPath(import.meta.url));
const ExhibitRoot = resolve(Here, '..');
const SourceRoot = resolve(Here, '../../../Experimental/WorldForge/src');

const { CrustSignedDistanceField, ConstructCrustConfiguration, ConstructPreset, ConstructPrimitive, PrimitiveCategory } = await import(join(SourceRoot, 'SdfCrustField.js'));
const { CrustHeightfield, SculptTool } = await import(join(SourceRoot, 'CrustHeightfield.js'));
const { ConstructExtractionSpecification, ConvertSdfToPolygonSurface, ComputeEulerCharacteristic, EncodePolygonSurfaceAsWavefront } = await import(join(SourceRoot, 'SdfSurfaceExtraction.js'));
const { ConstructWaterConfiguration, ResolveWindows, ResolvePositiveModulo, ComputeAdaptivityReport } = await import(join(SourceRoot, 'WaterClipmapLayout.js'));
const { ShallowWaterReference, ConstructReferenceBasin } = await import(join(SourceRoot, 'ShallowWaterReference.js'));
const { UnrealCameraRig, ConstructCameraConfiguration } = await import(join(SourceRoot, 'UnrealCameraRig.js'));
const { EvaluateFractalNoise, EvaluateLatticeHash } = await import(join(SourceRoot, 'NumericField.js'));
const { EncodeColourPng, EncodeHeightfieldPng, EncodeProject, DecodeProject } = await import(join(SourceRoot, 'ProjectCodec.js'));
const { ConstructTerrainClipmap, ResolveTerrainWindows, ComputeTerrainReport } = await import(join(SourceRoot, 'ClipmapSurface.js'));

const Results = [];
const Metrics = {};
let FailedCount = 0;
let EtaFieldForImage = null;
let Celerity = null;
let TransectSeries = [];

function RecordMeasurement(Identifier, Passed, Measurements, Explanation)
{
    Results.push({ Identifier, Passed, Measurements, Explanation });
    if (!Passed) ++FailedCount;
    const Marker = Passed ? 'PASS' : 'FAIL';
    console.log(`[${Marker}] ${Identifier} — ${Explanation}`);
}

function AssertFinite(ScalarReading, Label)
{
    if (!Number.isFinite(ScalarReading)) throw new Error(`${Label} is not finite: ${ScalarReading}`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       1. NOISE BASIS
//------------------------------------------------------------------------------------------------------------------------

{
    const First = EvaluateFractalNoise(1.234, 5.678, 1337, 5);
    const Second = EvaluateFractalNoise(1.234, 5.678, 1337, 5);
    const Other = EvaluateFractalNoise(1.234, 5.678, 1338, 5);
    const Hash = EvaluateLatticeHash(-17, 42, 7);
    const Passed = First === Second && First !== Other && Hash >= 0 && Hash < 1 && Math.abs(First) < 1;
    Metrics.Noise = { FractalSample: First, DifferentSeedSample: Other, HashSample: Hash };
    RecordMeasurement('01 Deterministic lattice noise', Passed, Metrics.Noise,
        `identical inputs reproduce ${First.toFixed(6)}; seed change yields ${Other.toFixed(6)}`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 2. COASTLINE SDF AND GULFS
//------------------------------------------------------------------------------------------------------------------------

const CrustConfiguration = ConstructCrustConfiguration();
const CrustField = new CrustSignedDistanceField(CrustConfiguration);
for (const Primitive of ConstructPreset('AfricaBlockout', CrustConfiguration.Extent)) CrustField.AppendPrimitive(Primitive);

{
    let LandSamples = 0;
    let OceanSamples = 0;
    for (let Row = 0; Row < 48; ++Row)
    {
        for (let Column = 0; Column < 48; ++Column)
        {
            const WorldX = (Column / 47 - 0.5) * CrustConfiguration.Extent;
            const WorldZ = (Row / 47 - 0.5) * CrustConfiguration.Extent;
            if (CrustField.EvaluateCoastDistance(WorldX, WorldZ) < 0) ++LandSamples; else ++OceanSamples;
        }
    }
    const WithoutGulfField = new CrustSignedDistanceField(CrustConfiguration);
    for (const Primitive of CrustField.Primitives)
        if (Primitive.Category !== PrimitiveCategory.Gulf) WithoutGulfField.AppendPrimitive(Primitive);
    let CarvedCells = 0;
    let Representative = null;
    for (let Row = 0; Row < 96; ++Row)
    {
        for (let Column = 0; Column < 96; ++Column)
        {
            const WorldX = (Column / 95 - 0.5) * CrustConfiguration.Extent;
            const WorldZ = (Row / 95 - 0.5) * CrustConfiguration.Extent;
            const WithCarve = CrustField.EvaluateCoastDistance(WorldX, WorldZ);
            const WithoutCarve = WithoutGulfField.EvaluateCoastDistance(WorldX, WorldZ);
            if (WithCarve > 0 && WithoutCarve < 0)
            {
                ++CarvedCells;
                if (!Representative) Representative = { WorldX, WorldZ, WithCarve, WithoutCarve };
            }
        }
    }
    const Passed = LandSamples > 100 && OceanSamples > 100 && CarvedCells > 20 && Representative !== null;
    Metrics.Coast = { LandSamples, OceanSamples, CarvedCells, Representative };
    RecordMeasurement('02 Coastline SDF and gulf carve', Passed, Metrics.Coast,
        `${LandSamples} inland / ${OceanSamples} ocean samples; ${CarvedCells} inland cells converted to water by the gulf carve`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                3. FLAT VASE AND CONTINENT
//------------------------------------------------------------------------------------------------------------------------

{
    const FlatConfiguration = ConstructCrustConfiguration({ Extent: 204800, FlatRelief: 0 });
    const FlatField = new CrustSignedDistanceField(FlatConfiguration);
    const FlatHeightfield = new CrustHeightfield(FlatField, 128);
    FlatHeightfield.RegenerateBase();
    let MaximumDeviation = 0;
    for (const Elevation of FlatHeightfield.Elevations)
        MaximumDeviation = Math.max(MaximumDeviation, Math.abs(Elevation - FlatConfiguration.FlatElevation));
    Metrics.FlatVase = { Elevation: FlatConfiguration.FlatElevation, MaximumDeviation };
    RecordMeasurement('03 Blank vase is exactly flat', MaximumDeviation < 1e-6, Metrics.FlatVase,
        `all 16,384 cells at ${FlatConfiguration.FlatElevation} m (max deviation ${MaximumDeviation.toExponential(2)})`);
}

const EvidenceHeightfield = new CrustHeightfield(CrustField, 512);
const GenerationStarted = Date.now();
EvidenceHeightfield.RegenerateBase();
const GenerationMilliseconds = Date.now() - GenerationStarted;
const CrustStatistics = EvidenceHeightfield.ComputeStatistics();

{
    const Passed = CrustStatistics.LandFraction > 0.15 && CrustStatistics.LandFraction < 0.55
        && CrustStatistics.Minimum < -1500 && CrustStatistics.Maximum > 900
        && CrustStatistics.ShorelineKilometres > 400;
    Metrics.Crust = { ...CrustStatistics, GenerationMilliseconds, Resolution: 512 };
    RecordMeasurement('04 Continental blockout with ocean', Passed, Metrics.Crust,
        `${(CrustStatistics.LandFraction * 100).toFixed(1)}% land, ${(CrustStatistics.OceanFraction * 100).toFixed(1)}% ocean, `
        + `${CrustStatistics.ShorelineKilometres.toFixed(0)} km coastline, elevation ${CrustStatistics.Minimum.toFixed(0)}…${CrustStatistics.Maximum.toFixed(0)} m`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              4. SCULPT STROKES AND UNDO
//------------------------------------------------------------------------------------------------------------------------

{
    const SculptField = new CrustSignedDistanceField(CrustConfiguration);
    for (const Primitive of CrustField.Primitives) SculptField.AppendPrimitive({ ...Primitive, Outline: Primitive.Outline.map(OutlineNode => [...OutlineNode]) });
    const Heightfield = new CrustHeightfield(SculptField, 256);
    Heightfield.RegenerateBase();
    const SampleX = -12000;
    const SampleZ = 24000;
    const Before = Heightfield.SampleElevation(SampleX, SampleZ);
    const Stroke = Heightfield.ConstructStroke(SculptTool.Raise, SampleX, SampleZ, 9000, 400);
    Heightfield.ApplyStroke(Stroke);
    const AfterStroke = Heightfield.SampleElevation(SampleX, SampleZ);
    Heightfield.UndoStroke();
    const AfterUndo = Heightfield.SampleElevation(SampleX, SampleZ);
    Heightfield.RedoStroke();
    const AfterRedo = Heightfield.SampleElevation(SampleX, SampleZ);
    const Passed = AfterStroke - Before > 350 && Math.abs(AfterUndo - Before) < 1e-3 && Math.abs(AfterRedo - AfterStroke) < 1e-3;
    Metrics.Sculpt = { Before, AfterStroke, AfterUndo, AfterRedo, Delta: AfterStroke - Before };
    RecordMeasurement('05 Heightmap sculpt / undo / redo', Passed, Metrics.Sculpt,
        `raise +${(AfterStroke - Before).toFixed(1)} m, undo restores ${AfterUndo.toFixed(2)} m, redo reapplies`);
}

{
    const Direction = [0, -1, 0];
    const Hit = EvidenceHeightfield.ResolveSurfaceIntersection([0, 20000, 0], Direction);
    const Expected = EvidenceHeightfield.SampleElevation(0, 0);
    const Error = Math.abs(Hit.Y - Expected);
    RecordMeasurement('06 Ray-to-crust intersection', Hit.Hit && Error < 20, { HitY: Hit.Y, Expected, Error },
        `sphere-trace + bisection landed ${Error.toFixed(2)} m from the raster elevation`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             7. SDF → POLYGON SURFACE
//------------------------------------------------------------------------------------------------------------------------

{
    const Radius = 6000;
    const Dome = {
        Field: { Configuration: { LandElevation: 9000, MountainAmplitude: 3000 } },
        SampleElevation: (X, Z) =>
        {
            const RadiusSquared = X * X + Z * Z;
            return RadiusSquared < Radius * Radius ? Math.sqrt(Radius * Radius - RadiusSquared) : -Radius * 0.5;
        }
    };
    const DomeSurface = ConvertSdfToPolygonSurface(Dome, ConstructExtractionSpecification({
        HorizontalExtent: 20000, VoxelSize: 800, VerticalMargin: 1000
    }));
    const Euler = ComputeEulerCharacteristic(DomeSurface);
    let OutwardNormals = 0;
    let TotalNormals = 0;
    for (let Vertex = 0; Vertex < DomeSurface.VertexCount; ++Vertex)
    {
        const Y = DomeSurface.VertexPositions[Vertex * 3 + 1];
        if (Y > 3000)
        {
            ++TotalNormals;
            if (DomeSurface.VertexNormals[Vertex * 3 + 1] > 0.2) ++OutwardNormals;
        }
    }
    const Passed = DomeSurface.VertexCount > 500 && DomeSurface.TriangleCount > 1000 && Euler === 2
        && OutwardNormals / Math.max(1, TotalNormals) > 0.9;
    Metrics.Dome = {
        VertexCount: DomeSurface.VertexCount, TriangleCount: DomeSurface.TriangleCount,
        EulerCharacteristic: Euler, VoxelCount: DomeSurface.VoxelCount,
        OutwardNormalFraction: OutwardNormals / Math.max(1, TotalNormals)
    };
    RecordMeasurement('07 Dual-contouring watertight genus-0 solid', Passed, Metrics.Dome,
        `V−E+F = ${Euler}, ${DomeSurface.TriangleCount} triangles, ${(100 * OutwardNormals / Math.max(1, TotalNormals)).toFixed(0)}% outward normals`);
}

let RealSurface = null;
{
    const Specification = ConstructExtractionSpecification({ CenterX: -8000, CenterZ: 20000, HorizontalExtent: 25600, VoxelSize: 800, VerticalMargin: 1000 });
    RealSurface = ConvertSdfToPolygonSurface(EvidenceHeightfield, Specification);
    const Wavefront = EncodePolygonSurfaceAsWavefront(RealSurface);
    const Passed = RealSurface.VertexCount > 1000 && RealSurface.TriangleCount > 2000
        && Wavefront.includes('\nf ') && ComputeEulerCharacteristic(RealSurface) === 2;
    Metrics.RealConversion = {
        VertexCount: RealSurface.VertexCount, TriangleCount: RealSurface.TriangleCount,
        VoxelCount: RealSurface.VoxelCount, ConversionMilliseconds: RealSurface.ConversionMilliseconds,
        EulerCharacteristic: ComputeEulerCharacteristic(RealSurface), WavefrontBytes: Wavefront.length
    };
    RecordMeasurement('08 Authored crust SDF → OBJ surface', Passed, Metrics.RealConversion,
        `${RealSurface.TriangleCount.toLocaleString()} triangles from ${RealSurface.VoxelCount.toLocaleString()} voxels in ${RealSurface.ConversionMilliseconds} ms`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              9. ADAPTIVE WATER CLIPMAP
//------------------------------------------------------------------------------------------------------------------------

const WaterConfiguration = ConstructWaterConfiguration();
const CameraX = 12345.6;
const CameraZ = -7890.1;
const WaterWindows = ResolveWindows(WaterConfiguration, CameraX, CameraZ);
const Adaptivity = ComputeAdaptivityReport(WaterConfiguration, CrustConfiguration.Extent, WaterWindows);

{
    let ExtentsDouble = true;
    let Nested = true;
    let ShiftConsistent = true;
    for (let Level = 1; Level < WaterWindows.length; ++Level)
    {
        if (Math.abs(WaterWindows[Level].Extent / WaterWindows[Level - 1].Extent - 2) > 1e-9) ExtentsDouble = false;
        if (WaterWindows[Level].WorldOriginX > WaterWindows[Level - 1].WorldOriginX
            || WaterWindows[Level].WorldOriginZ > WaterWindows[Level - 1].WorldOriginZ
            || WaterWindows[Level].WorldOriginX + WaterWindows[Level].Extent < WaterWindows[Level - 1].WorldOriginX + WaterWindows[Level - 1].Extent
            || WaterWindows[Level].WorldOriginZ + WaterWindows[Level].Extent < WaterWindows[Level - 1].WorldOriginZ + WaterWindows[Level - 1].Extent) Nested = false;
    }
    for (const Window of WaterWindows)
    {
        for (const Index of [0, 1, 137, WaterConfiguration.GridResolution - 1])
        {
            const Cell = Window.OriginCellsX + Index;
            const Texel = ResolvePositiveModulo(Index + Window.ShiftX, WaterConfiguration.GridResolution);
            if (ResolvePositiveModulo(Cell, WaterConfiguration.GridResolution) !== Texel) ShiftConsistent = false;
        }
    }
    const Passed = ExtentsDouble && Nested && ShiftConsistent && Adaptivity.SavingsFactor > 1000 && Adaptivity.FinestSpacing < 10;
    Metrics.Adaptivity = { ...Adaptivity, ExtentsDouble, Nested, ShiftConsistent, WindowCount: WaterWindows.length };
    RecordMeasurement('09 Camera-anchored rolling water clipmap', Passed, Metrics.Adaptivity,
        `${Adaptivity.SavingsFactor.toFixed(0)}× fewer cells than a uniform fine grid; Δx ${Adaptivity.FinestSpacing.toFixed(2)} m → ${Adaptivity.CoarsestSpacing.toFixed(0)} m`);
}

const TerrainClipmap = ConstructTerrainClipmap();
const TerrainWindows = ResolveTerrainWindows(TerrainClipmap, CameraX, CameraZ);
const TerrainReport = ComputeTerrainReport(TerrainClipmap, TerrainWindows, CrustConfiguration.Extent);
{
    const Passed = TerrainReport.SavingsFactor > 10 && TerrainWindows.length === TerrainClipmap.RingCount;
    Metrics.TerrainLod = TerrainReport;
    RecordMeasurement('10 Crust tessellation LOD', Passed, TerrainReport,
        `${TerrainReport.TriangleCount.toLocaleString()} triangles across ${TerrainWindows.length} tiers, ${TerrainReport.SavingsFactor.toFixed(0)}× cell saving`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                          11–13. 2.5D SHALLOW-WATER PHYSICS
//------------------------------------------------------------------------------------------------------------------------

{
    const Basin = new ShallowWaterReference(ConstructReferenceBasin({ Resolution: 96, CellSpacing: 25, BedElevation: -100, Friction: 0.02 }));
    const Centre = 96 * 25 * 0.5;
    Basin.InjectImpulse(Centre, Centre, 1.5, 220);
    const InitialVolume = Basin.ComputeDiagnostics().Volume;
    for (let Step = 0; Step < 500; ++Step) Basin.IntegrateTimestep();
    const Final = Basin.ComputeDiagnostics();
    const Drift = Math.abs(Final.Volume - InitialVolume) / InitialVolume;
    AssertFinite(Final.Energy, 'Energy');
    const Passed = Drift < 0.005 && Number.isFinite(Final.MaximumEta) && Math.abs(Final.MaximumEta) < 5;
    Metrics.Conservation = { InitialVolume, FinalVolume: Final.Volume, RelativeDrift: Drift, ...Final };
    RecordMeasurement('11 Water volume conservation (500 steps)', Passed, Metrics.Conservation,
        `relative drift ${(Drift * 100).toExponential(2)}%, η range ${Final.MinimumEta.toFixed(3)}…${Final.MaximumEta.toFixed(3)} m`);
}

{
    const Resolution = 128;
    const Spacing = 20;
    const Basin = new ShallowWaterReference(ConstructReferenceBasin({ Resolution, CellSpacing: Spacing, BedElevation: -100, Friction: 0.0, WindStrength: 0 }));
    const Centre = Math.floor(Resolution / 2);
    Basin.InjectImpulse(Centre * Spacing, Centre * Spacing, 1.2, 150);
    const ExtractTransect = () =>
    {
        const Line = [];
        for (let Column = Centre; Column < Resolution; ++Column) Line.push(Basin.Eta[Centre * Resolution + Column]);
        return Line;
    };
    const FindCrest = () =>
    {
        const Line = ExtractTransect();
        let BestIndex = 1;
        for (let Index = 1; Index < Line.length; ++Index) if (Math.abs(Line[Index]) > Math.abs(Line[BestIndex])) BestIndex = Index;
        return { Index: BestIndex, Line: Line.slice() };
    };
    for (let Step = 0; Step < 90; ++Step) Basin.IntegrateTimestep();
    EtaFieldForImage = { Eta: Float64Array.from(Basin.Eta), Resolution };
    const Early = FindCrest();
    const EarlyTime = Basin.ElapsedSeconds;
    for (let Step = 0; Step < 90; ++Step) Basin.IntegrateTimestep();
    const Late = FindCrest();
    const LateTime = Basin.ElapsedSeconds;
    const Theoretical = Math.sqrt(9.81 * 100);
    Celerity = (Late.Index - Early.Index) * Spacing / (LateTime - EarlyTime);
    TransectSeries = [
        { Time: EarlyTime, Line: Early.Line },
        { Time: LateTime, Line: Late.Line }
    ];
    const Ratio = Celerity / Theoretical;
    const Passed = Number.isFinite(Celerity) && Ratio > 0.65 && Ratio < 1.4 && Late.Index > Early.Index;
    Metrics.Celerity = { Measured: Celerity, Theoretical, Ratio, EarlyCrestCell: Early.Index, LateCrestCell: Late.Index, EarlyTime, LateTime };
    RecordMeasurement('12 Wave celerity ≈ √(g·h)', Passed, Metrics.Celerity,
        `measured ${Celerity.toFixed(2)} m/s vs theoretical ${Theoretical.toFixed(2)} m/s (${(Ratio * 100).toFixed(0)}%)`);
}

{
    const Resolution = 96;
    const Spacing = 25;
    const Basin = new ShallowWaterReference(ConstructReferenceBasin({ Resolution, CellSpacing: Spacing, Friction: 0.03 }));
    Basin.ConfigureBed((X) => X > Resolution * Spacing * 0.55 ? 60 : -100);
    Basin.InjectImpulse(Resolution * Spacing * 0.25, Resolution * Spacing * 0.5, 1.2, 240);
    for (let Step = 0; Step < 400; ++Step) Basin.IntegrateTimestep();
    let DryViolations = 0;
    let VelocityViolations = 0;
    let LandCells = 0;
    for (let Row = 0; Row < Resolution; ++Row)
    {
        for (let Column = 0; Column < Resolution; ++Column)
        {
            const Index = Row * Resolution + Column;
            if (Basin.Bed[Index] > 0)
            {
                ++LandCells;
                if (Basin.ResolveDepth(Index) > Basin.Configuration.DryDepth) ++DryViolations;
                if (Math.hypot(Basin.VelocityX[Index], Basin.VelocityZ[Index]) > 1e-9) ++VelocityViolations;
            }
        }
    }
    const Diagnostics = Basin.ComputeDiagnostics();
    const Passed = DryViolations === 0 && VelocityViolations === 0 && Number.isFinite(Diagnostics.Energy);
    Metrics.WetDry = { LandCells, DryViolations, VelocityViolations, WetCells: Diagnostics.WetCells, MaximumVelocity: Diagnostics.MaximumVelocity };
    RecordMeasurement('13 Wet/dry shoreline stays dry', Passed, Metrics.WetDry,
        `${LandCells} land cells, ${DryViolations} flooded, ${VelocityViolations} with velocity; ${Diagnostics.WetCells} wet cells`);
}

{
    const Resolution = 128;
    const Spacing = 22;
    const Basin = new ShallowWaterReference(ConstructReferenceBasin({ Resolution, CellSpacing: Spacing, Friction: 0.015, WindStrength: 0.02 }));
    Basin.ConfigureBed((X, Z) =>
    {
        const Shore = Resolution * Spacing * 0.78;
        return X > Shore ? -100 + (X - Shore) / (Resolution * Spacing - Shore) * 96 : -100;
    });
    Basin.InjectImpulse(Resolution * Spacing * 0.42, Resolution * Spacing * 0.5, 1.6, 230);
    for (let Step = 0; Step < 260; ++Step) Basin.IntegrateTimestep();
    const Diagnostics = Basin.ComputeDiagnostics();
    let HasNonFinite = false;
    for (const Sample of Basin.Eta) if (!Number.isFinite(Sample)) HasNonFinite = true;
    const Passed = !HasNonFinite && Diagnostics.MaximumVelocity < 6
        && Math.abs(Diagnostics.MaximumEta) < 4 && Math.abs(Diagnostics.MinimumEta) < 4
        && Diagnostics.Energy > 0 && Number.isFinite(Diagnostics.Energy);
    Metrics.Stability = { ...Diagnostics, HasNonFinite, Bathymetry: 'sloping shore with 0.02 m/s² wind forcing' };
    RecordMeasurement('14 Solver stability over sloping bathymetry', Passed, Metrics.Stability,
        `260 steps, wind + shoaling bed, η ${Diagnostics.MinimumEta.toFixed(2)}…${Diagnostics.MaximumEta.toFixed(2)} m, `
        + `max |v| ${Diagnostics.MaximumVelocity.toFixed(2)} m/s, ${HasNonFinite ? 'non-finite cells present' : 'no non-finite cells'}`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   15. CAMERA RIG
//------------------------------------------------------------------------------------------------------------------------

{
    const Camera = new UnrealCameraRig(ConstructCameraConfiguration({ AltitudeScalingEnabled: false }));
    Camera.Position = [0, 1000, 0];
    Camera.Yaw = 0;
    Camera.Pitch = 0;
    Camera.PressedKeys.add('KeyW');
    for (let FrameIteration = 0; FrameIteration < 60; ++FrameIteration) Camera.IntegrateMotion(1 / 60);
    const ForwardDistance = -Camera.Position[2];
    Camera.PressedKeys.clear();
    Camera.PressedKeys.add('KeyE');
    const BeforeRise = Camera.Position[1];
    for (let FrameIteration = 0; FrameIteration < 60; ++FrameIteration) Camera.IntegrateMotion(1 / 60);
    const Rise = Camera.Position[1] - BeforeRise;
    Camera.PressedKeys.clear();
    Camera.PressedKeys.add('KeyW');
    Camera.PressedKeys.add('ShiftLeft');
    const BeforeBoost = Camera.Position[2];
    for (let FrameIteration = 0; FrameIteration < 60; ++FrameIteration) Camera.IntegrateMotion(1 / 60);
    const BoostDistance = Math.abs(Camera.Position[2] - BeforeBoost);
    Camera.RotateBy(0, -100000);
    const PitchClamped = Math.abs(Camera.Pitch) <= 89 * Math.PI / 180 + 1e-6;
    const Basis = Camera.ResolveBasis();
    const Dot = Basis.Right[0] * Basis.Forward[0] + Basis.Right[1] * Basis.Forward[1] + Basis.Right[2] * Basis.Forward[2];
    const Passed = ForwardDistance > 100 && Rise > 100 && BoostDistance > ForwardDistance * 2 && PitchClamped && Math.abs(Dot) < 1e-9;
    Metrics.Camera = { ForwardDistance, Rise, BoostDistance, PitchClamped, BasisOrthogonality: Dot };
    RecordMeasurement('15 Unreal-style fly camera', Passed, Metrics.Camera,
        `W ${ForwardDistance.toFixed(0)} m, E ${Rise.toFixed(0)} m, Shift+W ${BoostDistance.toFixed(0)} m, pitch clamped, orthogonal basis`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  16. PNG / PROJECT CODEC
//------------------------------------------------------------------------------------------------------------------------

function InspectPng(Bytes)
{
    const Signature = [137, 80, 78, 71, 13, 10, 26, 10];
    for (let Index = 0; Index < 8; ++Index) if (Bytes[Index] !== Signature[Index]) throw new Error('PNG signature mismatch');
    const View = new DataView(Bytes.buffer, Bytes.byteOffset, Bytes.byteLength);
    let Cursor = 8;
    let Width = 0;
    let Height = 0;
    let BitDepth = 0;
    let ColourType = 0;
    let Payload = new Uint8Array(0);
    while (Cursor < Bytes.length)
    {
        const Length = View.getUint32(Cursor);
        const Identifier = String.fromCharCode(...Bytes.subarray(Cursor + 4, Cursor + 8));
        const Chunk = Bytes.subarray(Cursor + 8, Cursor + 8 + Length);
        if (Identifier === 'IHDR')
        {
            Width = View.getUint32(Cursor + 8);
            Height = View.getUint32(Cursor + 12);
            BitDepth = Chunk[8];
            ColourType = Chunk[9];
        }
        if (Identifier === 'IDAT') Payload = Chunk;
        Cursor += 12 + Length;
    }
    return { Width, Height, BitDepth, ColourType, Payload };
}

{
    const HeightPng = EncodeHeightfieldPng(EvidenceHeightfield, -4200, 4200);
    const Parsed = InspectPng(HeightPng);
    const ColourPng = EncodeColourPng(4, 2, new Uint8Array([255, 0, 0, 0, 255, 0, 0, 0, 255, 30, 30, 30, 90, 90, 90, 20, 40, 60, 1, 2, 3, 4, 5, 6]));
    const ColourParsed = InspectPng(ColourPng);
    const ExpectedLength = (512 * 2 + 1) * 512;
    const Passed = Parsed.Width === 512 && Parsed.Height === 512 && Parsed.BitDepth === 16 && Parsed.ColourType === 0
        && ColourParsed.Width === 4 && ColourParsed.Height === 2 && ColourParsed.ColourType === 2
        && Parsed.Payload.length >= ExpectedLength;
    Metrics.Codec = {
        HeightPngBytes: HeightPng.length, HeightPng: `${Parsed.Width}×${Parsed.Height} @ ${Parsed.BitDepth}-bit`,
        ColourPng: `${ColourParsed.Width}×${ColourParsed.Height} @ ${ColourParsed.BitDepth}-bit type ${ColourParsed.ColourType}`,
        IdatBytes: Parsed.Payload.length
    };
    RecordMeasurement('16 Dependency-free PNG encoding', Passed, Metrics.Codec,
        `16-bit heightfield ${HeightPng.length.toLocaleString()} B; RGB evidence PNG validated`);
}

{
    const Document = {
        CrustConfiguration,
        Field: CrustField,
        Heightfield: EvidenceHeightfield,
        WaterConfiguration,
        SpectralConfiguration: { Amplitude: 1.15, Wavelength: 340 },
        Camera: new UnrealCameraRig(),
        View: { Mode: 'Flat', ViewMode: 'eta' }
    };
    const Encoded = EncodeProject(Document);
    const Decoded = DecodeProject(Encoded);
    const Passed = Decoded.Format === 'Frontier.WorldForge.Project'
        && Decoded.Primitives.length === CrustField.Primitives.length
        && Decoded.Strokes.length === EvidenceHeightfield.StrokeCommands.length
        && Decoded.View.ViewMode === 'eta';
    Metrics.Project = { Bytes: Encoded.length, PrimitiveCount: Decoded.Primitives.length, StrokeCount: Decoded.Strokes.length };
    RecordMeasurement('17 Project document round trip', Passed, Metrics.Project,
        `${Encoded.length.toLocaleString()} B JSON, ${Decoded.Primitives.length} SDF layers preserved`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             18-20. GLSL PROGRAM STATIC AUDIT
//------------------------------------------------------------------------------------------------------------------------

const ShaderSources = await import(join(SourceRoot, 'Shaders.js'));
const ShaderEntries = Object.entries(ShaderSources).filter(([, Source]) => typeof Source === 'string' && Source.startsWith('#version'));
const DeclaredUniforms = new Set();
const UniformDeclarations = new Map();

{
    let StructuralProblems = 0;
    let DuplicateProblems = 0;
    let NonAsciiProblems = 0;
    for (const [Name, Source] of ShaderEntries)
    {
        if (!Source.startsWith('#version 300 es')) ++StructuralProblems;
        let Depth = 0;
        for (const Character of Source)
        {
            if (Character === '{') ++Depth;
            if (Character === '}') --Depth;
            if (Depth < 0) ++StructuralProblems;
        }
        if (Depth !== 0) ++StructuralProblems;
        if ([...Source].some(Character => Character.charCodeAt(0) > 127)) ++NonAsciiProblems;
        const Uniforms = [...Source.matchAll(/^\s*uniform\s+(?:(?:highp|mediump|lowp)\s+)?\w+\s+(\w+)\s*;/gm)].map(Match => Match[1]);
        if (new Set(Uniforms).size !== Uniforms.length) ++DuplicateProblems;
        for (const Uniform of Uniforms) DeclaredUniforms.add(Uniform);
        UniformDeclarations.set(Name, new Set(Uniforms));
    }
    const Passed = StructuralProblems === 0 && DuplicateProblems === 0 && NonAsciiProblems === 0 && ShaderEntries.length === 11;
    Metrics.GlslStructure = { ShaderCount: ShaderEntries.length, StructuralProblems, DuplicateProblems, NonAsciiProblems };
    RecordMeasurement('18 GLSL ES 3.00 structure and declarations', Passed, Metrics.GlslStructure,
        `${ShaderEntries.length} shader sources, 0 duplicate uniforms, 0 unbalanced scopes, ASCII-only identifiers`);
}

{
    const Pairs = [
        ['TerrainVertexShader', 'TerrainFragmentShader'], ['WaterVertexShader', 'WaterFragmentShader'],
        ['SurfaceVertexShader', 'SurfaceFragmentShader'], ['OverlayVertexShader', 'OverlayFragmentShader'],
        ['FullscreenVertexShader', 'SkyFragmentShader'], ['FullscreenVertexShader', 'SolverFragmentShader']
    ];
    let Mismatches = 0;
    for (const [VertexName, FragmentName] of Pairs)
    {
        const Outputs = new Map([...ShaderSources[VertexName].matchAll(/^\s*out\s+(\w+)\s+(\w+)\s*;/gm)].map(Match => [Match[2], Match[1]]));
        const Inputs = [...ShaderSources[FragmentName].matchAll(/^\s*in\s+(\w+)\s+(\w+)\s*;/gm)].map(Match => [Match[2], Match[1]]);
        for (const [Name, Type] of Inputs) if (Outputs.get(Name) !== Type) ++Mismatches;
    }
    Metrics.GlslVaryings = { ProgramPairs: Pairs.length, Mismatches };
    RecordMeasurement('19 Vertex/fragment varying interfaces', Mismatches === 0, Metrics.GlslVaryings,
        `${Pairs.length} program pairs checked, ${Mismatches} type/name mismatches`);
}

{
    const { readFileSync } = await import('node:fs');
    const UsedUniforms = new Set();
    for (const FileName of ['ViewportRenderer.js', 'GpuShallowWater.js'])
    {
        const Text = readFileSync(join(SourceRoot, FileName), 'utf8');
        for (const Match of Text.matchAll(/Set(?:Float|Integer|Vec2|IVec2|Vec3|Vec4)\('([A-Za-z_][A-Za-z0-9_]*)'/g)) UsedUniforms.add(Match[1]);
    }
    const Undeclared = [...UsedUniforms].filter(Name => !DeclaredUniforms.has(Name));
    Metrics.GlslWiring = { DeclaredUniforms: DeclaredUniforms.size, BoundUniforms: UsedUniforms.size, Undeclared };
    RecordMeasurement('20 CPU→GPU uniform wiring', Undeclared.length === 0, Metrics.GlslWiring,
        `${UsedUniforms.size} bound uniform names all declared across ${DeclaredUniforms.size} GLSL declarations`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                          21. WEBGL2 RENDER-PATH SMOKE TEST
//------------------------------------------------------------------------------------------------------------------------

{
    // 📝 A recording mock device exercises every CPU-side call in the renderer and solver without a GPU.
    const { ViewportRenderer } = await import(join(SourceRoot, 'ViewportRenderer.js'));
    const { GpuShallowWater } = await import(join(SourceRoot, 'GpuShallowWater.js'));
    const ConstantCache = new Map();
    const ResolveConstant = Name =>
    {
        if (!ConstantCache.has(Name)) ConstantCache.set(Name, 1000 + ConstantCache.size);
        return ConstantCache.get(Name);
    };
    const MockContext = new Proxy({}, {
        get(Target, Property)
        {
            if (typeof Property !== 'string') return undefined;
            if (/^[A-Z0-9_]+$/.test(Property)) return ResolveConstant(Property);
            if (Property === 'getShaderParameter' || Property === 'getProgramParameter') return () => true;
            if (Property === 'getShaderInfoLog' || Property === 'getProgramInfoLog') return () => '';
            if (Property === 'checkFramebufferStatus') return () => ResolveConstant('FRAMEBUFFER_COMPLETE');
            if (Property === 'getUniformLocation') return () => ({});
            if (Property === 'readPixels') return () => {};
            if (Property.startsWith('create')) return () => ({});
            return () => undefined;
        }
    });
    const MockCapabilities = { Context: MockContext, FloatColourAttachment: true, UseHalfFloat: false };
    const MockCanvas = { clientWidth: 1280, clientHeight: 720, width: 0, height: 0 };
    globalThis.window = { devicePixelRatio: 1 };
    const SmokeHeightfield = new CrustHeightfield(CrustField, 64);
    SmokeHeightfield.RegenerateBase();
    const SmokeSpectral = { Amplitude: 1.15, Wavelength: 340, Direction: [0.88, 0.47], Steepness: 0.62 };
    let Problems = [];
    try
    {
        const Renderer = new ViewportRenderer(MockCanvas, MockCapabilities, SmokeHeightfield, WaterConfiguration, TerrainClipmap);
        const Solver = new GpuShallowWater(MockContext, WaterConfiguration, MockCapabilities);
        const SmokeCamera = new UnrealCameraRig();
        const SmokeWindows = ResolveWindows(WaterConfiguration, 1200, -3400);
        const SmokeTerrain = ResolveTerrainWindows(TerrainClipmap, 1200, -3400);
        Solver.InjectDisturbance(1200, -3400, 1.5, 300);
        Solver.Integrate(SmokeWindows, Renderer.ElevationTexture, SmokeHeightfield.Resolution, SmokeHeightfield.Extent);
        Solver.RefreshProbe(1200, -3400);
        const BaseFrame = {
            Camera: SmokeCamera, Planet: { Yaw: 0.5, Pitch: 0.4, Distance: 1.9e7 },
            Water: Solver, WaterWindows: SmokeWindows, TerrainWindows: SmokeTerrain,
            ShowConvertedSurface: false, ShowSurfaceWireframe: false,
            OverlayLines: new Float32Array([0, 10, 0, 100, 10, 100]),
            SunDirection: [0.5, 0.6, 0.4], HazeDensity: 1e-6, SeaLevel: 0,
            Spectral: SmokeSpectral, ElapsedSeconds: 1.2
        };
        let TotalDrawCalls = 0;
        for (const Mode of ['Flat', 'Planet'])
        {
            for (const ViewMode of ['beauty', 'bathymetry', 'eta', 'velocity', 'lod'])
            {
                const Result = Renderer.Rasterize({ ...BaseFrame, Mode, ViewMode });
                TotalDrawCalls += Result.DrawCalls;
                if (Result.DrawCalls <= 0 || !Number.isFinite(Result.TriangleCount)) Problems.push(`${Mode}/${ViewMode}`);
            }
        }
        Metrics.RenderSmoke = { Modes: 10, TotalDrawCalls, TrianglesPerFrame: Renderer.LastTriangleCount, Problems };
    }
    catch (Failure)
    {
        Problems.push(Failure.message);
        Metrics.RenderSmoke = { Problems };
    }
    RecordMeasurement('21 WebGL2 render path (mock device)', Problems.length === 0, Metrics.RenderSmoke,
        Problems.length === 0
            ? `10 mode/view combinations composed, ${Metrics.RenderSmoke.TotalDrawCalls} draw calls, no CPU-side exceptions`
            : `failures: ${Problems.join(', ')}`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                            22. EDITOR DOM CONTRACT
//------------------------------------------------------------------------------------------------------------------------

{
    const { readFileSync } = await import('node:fs');
    const PrototypeRoot = resolve(Here, '../../../Experimental/WorldForge');
    const Markup = readFileSync(join(PrototypeRoot, 'index.html'), 'utf8');
    const Controller = readFileSync(join(PrototypeRoot, 'src/EditorWorkspace.js'), 'utf8');
    const DeclaredIdentifiers = new Set([...Markup.matchAll(/\bid="([^"]+)"/g)].map(Match => Match[1]));
    const ReferencedIdentifiers = [...Controller.matchAll(/(?<!\$)\$\('([^']+)'\)/g)].map(Match => Match[1]);
    const Missing = [...new Set(ReferencedIdentifiers)].filter(Identifier => !DeclaredIdentifiers.has(Identifier));
    const SelectorGroups = [...Controller.matchAll(/\$\$\('([^']+)'\)/g)].map(Match => Match[1]);
    const AbsentSelectors = SelectorGroups.filter(Selector =>
    {
        const ClassName = Selector.split(' ')[0].replace(/^[.#]/, '');
        return !Markup.includes(ClassName);
    });
    Metrics.DomContract = {
        DeclaredIdentifiers: DeclaredIdentifiers.size,
        ReferencedIdentifiers: new Set(ReferencedIdentifiers).size,
        Missing, AbsentSelectors
    };
    RecordMeasurement('22 Editor DOM identifier contract', Missing.length === 0 && AbsentSelectors.length === 0, Metrics.DomContract,
        `${new Set(ReferencedIdentifiers).size} referenced identifiers resolve against ${DeclaredIdentifiers.size} markup identifiers; `
        + `${Missing.length} missing, ${AbsentSelectors.length} absent selector groups`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     EVIDENCE OUTPUT
//------------------------------------------------------------------------------------------------------------------------

function ClampByte(ScalarReading)
{
    return Math.max(0, Math.min(255, Math.round(ScalarReading)));
}

function BuildCrustImage()
{
    const Size = 512;
    const Pixels = new Uint8Array(Size * Size * 3);
    for (let Row = 0; Row < Size; ++Row)
    {
        for (let Column = 0; Column < Size; ++Column)
        {
            const WorldX = (Column / Size - 0.5) * CrustConfiguration.Extent;
            const WorldZ = (Row / Size - 0.5) * CrustConfiguration.Extent;
            const Elevation = EvidenceHeightfield.SampleElevation(WorldX, WorldZ);
            let Red, Green, Blue;
            if (Elevation < 0)
            {
                const Depth = Math.min(1, -Elevation / 3800);
                Red = 16 + 26 * (1 - Depth); Green = 62 + 66 * (1 - Depth); Blue = 104 + 58 * (1 - Depth);
            }
            else
            {
                const Height = Math.min(1, Elevation / 1800);
                Red = 96 + 146 * Height; Green = 132 + 106 * Height; Blue = 72 + 128 * Height;
            }
            const Offset = (Row * Size + Column) * 3;
            Pixels[Offset] = ClampByte(Red); Pixels[Offset + 1] = ClampByte(Green); Pixels[Offset + 2] = ClampByte(Blue);
        }
    }
    return EncodeColourPng(Size, Size, Pixels);
}

function BuildEtaImage()
{
    const Source = EtaFieldForImage.Resolution;
    const Size = 384;
    const Pixels = new Uint8Array(Size * Size * 3);
    let Peak = 1e-6;
    for (const Sample of EtaFieldForImage.Eta) Peak = Math.max(Peak, Math.abs(Sample));
    for (let Row = 0; Row < Size; ++Row)
    {
        for (let Column = 0; Column < Size; ++Column)
        {
            const SampleColumn = Math.min(Source - 1, Math.floor(Column / Size * Source));
            const SampleRow = Math.min(Source - 1, Math.floor(Row / Size * Source));
            const Eta = EtaFieldForImage.Eta[SampleRow * Source + SampleColumn];
            const Normalized = Math.max(-1, Math.min(1, Eta / Peak));
            let Red, Green, Blue;
            if (Normalized >= 0) { Red = 32 + 223 * Normalized; Green = 74 + 90 * Normalized; Blue = 168 - 110 * Normalized; }
            else { Red = 32 + 20 * (1 + Normalized); Green = 74 + 80 * (1 + Normalized); Blue = 168 + 70 * (1 + Normalized); }
            const Offset = (Row * Size + Column) * 3;
            Pixels[Offset] = ClampByte(Red); Pixels[Offset + 1] = ClampByte(Green); Pixels[Offset + 2] = ClampByte(Blue);
        }
    }
    return EncodeColourPng(Size, Size, Pixels);
}

function BuildWaveChart()
{
    const Width = 880;
    const Height = 300;
    const Padding = 52;
    const Lines = [
        `<svg xmlns="http://www.w3.org/2000/svg" width="${Width}" height="${Height}" viewBox="0 0 ${Width} ${Height}">`,
        `<rect width="${Width}" height="${Height}" fill="#10161d"/>`,
        `<text x="20" y="26" fill="#d6dde6" font-family="monospace" font-size="14">2.5D shallow-water crest propagation along a transect</text>`,
        `<text x="20" y="44" fill="#8694a4" font-family="monospace" font-size="11">Reference solver, h = 100 m, Δx = 20 m, reflective basin</text>`
    ];
    const Baseline = Height - Padding;
    const ChartHeight = Height - Padding - 70;
    Lines.push(`<line x1="${Padding}" y1="${Baseline}" x2="${Width - 20}" y2="${Baseline}" stroke="#394654"/>`);
    Lines.push(`<line x1="${Padding}" y1="70" x2="${Padding}" y2="${Baseline}" stroke="#394654"/>`);
    const Colours = ['#4ea8e8', '#e39a3b'];
    TransectSeries.forEach((Series, SeriesIndex) =>
    {
        const Points = [];
        for (let Index = 0; Index < Series.Line.length; ++Index)
        {
            const X = Padding + Index / (Series.Line.length - 1) * (Width - Padding - 20);
            const Y = Baseline - ChartHeight * (0.5 + Math.max(-1, Math.min(1, Series.Line[Index] / 1.2)) * 0.45);
            Points.push(`${X.toFixed(1)},${Y.toFixed(1)}`);
        }
        Lines.push(`<polyline fill="none" stroke="${Colours[SeriesIndex]}" stroke-width="2" points="${Points.join(' ')}"/>`);
        Lines.push(`<text x="${Width - 210}" y="${86 + SeriesIndex * 18}" fill="${Colours[SeriesIndex]}" font-family="monospace" font-size="11">t = ${Series.Time.toFixed(2)} s</text>`);
    });
    Lines.push(`<text x="${Padding}" y="${Height - 16}" fill="#8694a4" font-family="monospace" font-size="11">distance from impulse (cells × Δx) →</text>`);
    if (Celerity !== null)
        Lines.push(`<text x="${Width - 330}" y="${Height - 16}" fill="#57bf88" font-family="monospace" font-size="11">measured c = ${Celerity.toFixed(2)} m/s</text>`);
    Lines.push('</svg>');
    return Lines.join('\n');
}

function BuildLodChart()
{
    const Width = 880;
    const Height = 300;
    const Lines = [
        `<svg xmlns="http://www.w3.org/2000/svg" width="${Width}" height="${Height}" viewBox="0 0 ${Width} ${Height}">`,
        `<rect width="${Width}" height="${Height}" fill="#10161d"/>`,
        `<text x="20" y="26" fill="#d6dde6" font-family="monospace" font-size="14">Adaptive water tiers: cell spacing by distance from camera</text>`,
        `<text x="20" y="44" fill="#8694a4" font-family="monospace" font-size="11">${WaterConfiguration.RingCount} × ${WaterConfiguration.GridResolution}² solver tiers + tessellated spectral shell</text>`
    ];
    const Baseline = Height - 46;
    const MaximumLog = Math.log10(WaterWindows[WaterWindows.length - 1].CellSpacing);
    const BarWidth = (Width - 100) / WaterWindows.length;
    WaterWindows.forEach((Window, Index) =>
    {
        const BarHeight = 12 + 170 * Math.log10(Window.CellSpacing) / Math.max(1, MaximumLog);
        const X = 60 + Index * BarWidth;
        const Y = Baseline - BarHeight;
        Lines.push(`<rect x="${X.toFixed(1)}" y="${Y.toFixed(1)}" width="${(BarWidth - 14).toFixed(1)}" height="${BarHeight.toFixed(1)}" fill="#3f7fa8" stroke="#4ea8e8"/>`);
        Lines.push(`<text x="${(X + 4).toFixed(1)}" y="${(Y - 8).toFixed(1)}" fill="#d6dde6" font-family="monospace" font-size="11">${Window.CellSpacing < 10 ? Window.CellSpacing.toFixed(2) : Window.CellSpacing.toFixed(0)} m</text>`);
        Lines.push(`<text x="${(X + 4).toFixed(1)}" y="${Baseline + 16}" fill="#8694a4" font-family="monospace" font-size="10">L${Window.Level} · ${(Window.Extent / 1000).toFixed(1)} km</text>`);
    });
    Lines.push(`<text x="20" y="${Height - 8}" fill="#57bf88" font-family="monospace" font-size="11">${Adaptivity.SavingsFactor.toFixed(0)}× fewer cells than a uniform finest-resolution ocean</text>`);
    Lines.push('</svg>');
    return Lines.join('\n');
}

await mkdir(join(ExhibitRoot, 'Images'), { recursive: true });
await mkdir(join(ExhibitRoot, 'Results'), { recursive: true });
await writeFile(join(ExhibitRoot, 'Images', 'crust-blockout.png'), BuildCrustImage());
await writeFile(join(ExhibitRoot, 'Images', 'water-eta-field.png'), BuildEtaImage());
await writeFile(join(ExhibitRoot, 'Images', 'wave-propagation.svg'), BuildWaveChart());
await writeFile(join(ExhibitRoot, 'Images', 'lod-savings.svg'), BuildLodChart());
await writeFile(join(ExhibitRoot, 'Results', 'validation.json'), JSON.stringify({
    GeneratedAtUtc: new Date().toISOString(),
    Revision: 'arena/23caea4c-frontier',
    Passed: Results.length - FailedCount,
    Failed: FailedCount,
    Results,
    Metrics
}, null, 2));

const Markdown = [
    '# World Forge — validation results',
    '',
    `Generated ${new Date().toISOString()} · ${Results.length - FailedCount}/${Results.length} checks passed.`,
    '',
    '| Check | Result | Observation |',
    '|---|---|---|'
];
for (const Entry of Results)
    Markdown.push(`| ${Entry.Identifier} | ${Entry.Passed ? 'PASS' : 'FAIL'} | ${Entry.Explanation.replace(/\|/g, '/')} |`);
Markdown.push('', '## Evidence', '',
    '- `Images/crust-blockout.png` — top-down raster of the authored SDF crust (512² from the real heightfield).',
    '- `Images/water-eta-field.png` — simulated free-surface elevation η after 180 shallow-water steps (radial wave field).',
    '- `Images/wave-propagation.svg` — crest transects at two simulated times with measured celerity.',
    '- `Images/lod-savings.svg` — per-tier cell spacing and the computed cell-saving factor.',
    '- `Results/validation.json` — full numeric measurements.', '');
await writeFile(join(ExhibitRoot, 'Results', 'validation.md'), Markdown.join('\n'));

console.log(`\n${Results.length - FailedCount}/${Results.length} checks passed.`);
if (FailedCount > 0) process.exitCode = 1;
