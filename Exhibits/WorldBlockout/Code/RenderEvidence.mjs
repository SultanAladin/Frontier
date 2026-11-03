/*==========================================================================================================================================
                                                  RENDEREVIDENCE.MJS
==========================================================================================================================================*/
// 🧩 Renders the blockout evidence: the planet map, the sculpt timeline, the city pads, the ocean resolution map, a
//    cross-section through the solve and the triangle budget. Every figure is the same SVG the page draws, rasterised with
//    resvg in a jsdom document, so a screenshot and the editor cannot disagree.
'use strict';

import { createRequire } from 'node:module';
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const Require = createRequire(import.meta.url);
const Here    = dirname(fileURLToPath(import.meta.url));
const Root    = resolve(Here, '../../..');
const Prototype = resolve(Root, 'Experimental/WorldBlockout');

const Terrain = Require(resolve(Prototype, 'TerrainField.js'));
const OceanField = Require(resolve(Prototype, 'OceanField.js'));
const OceanMesh = Require(resolve(Prototype, 'OceanMesh.js'));
const Scenes = Require(resolve(Prototype, 'Scenes.js'));
const Raster = Require(resolve(Prototype, 'Raster.js'));

let Resvg = null, Jsdom = null;
try
{
    ({ Resvg } = Require('@resvg/resvg-js'));
    Jsdom = Require('jsdom');
}
catch
{
    console.log('BLOCKED: resvg or jsdom is not installed.');
    console.log('Install them outside the repository, then re-run with NODE_PATH pointing at them:');
    console.log('  npm install --prefix /tmp/vehicle-checks jsdom @resvg/resvg-js');
    console.log('  NODE_PATH=/tmp/vehicle-checks/node_modules node Exhibits/WorldBlockout/Code/RenderEvidence.mjs');
    process.exit(2);
}

const ResultsFolder = resolve(Here, '../Results');
mkdirSync(ResultsFolder, { recursive: true });

const PAPER = '#f2ede2';
const ACCENT = '#e8a84a';
const COOL = '#6fc7a8';
const DEEP = '#7aa7e0';

function Write(FileName, Svg, Width)
{
    const Png = new Resvg(Svg, { fitTo: { mode: 'width', value: Width }, font: { loadSystemFonts: true } })
        .render().asPng();
    writeFileSync(resolve(ResultsFolder, FileName), Png);
    console.log(`${FileName.padEnd(24)} ${String(Math.round(Png.length / 1024)).padStart(5)} kB`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE WORLD
//------------------------------------------------------------------------------------------------------------------------

function Sculpt(Field, Steps)
{
    for (const Step of Steps)
    {
        if (Step.Stamp) Terrain.StampContinent(Field, Step.Stamp);
        if (Step.Carve) Terrain.CarveGulf(Field, Step.Carve);
        if (Step.Isles) Terrain.StampArchipelago(Field, Step.Isles);
        if (Step.Pad) Terrain.PlaceCityPad(Field, Step.Pad);
    }
    return Field;
}

const Continents = Terrain.ContinentOutlines();
const Gulfs = Terrain.GulfOutlines();

const Terra = Sculpt(Terrain.CreateField(Terrain.ConfigurePlanet()), [
    { Stamp: { Name: 'Africa', Polygon: Continents.Africa, Plateau: 680 } },
    { Stamp: { Name: 'Eurasia', Polygon: Continents.Eurasia, Plateau: 780 } },
    { Stamp: { Name: 'SouthAmerica', Polygon: Continents.SouthAmerica, Plateau: 700 } },
    { Stamp: { Name: 'Australia', Polygon: Continents.Australia, Plateau: 520 } },
    { Isles: { Name: 'Sunda', Lon: 112, Lat: -4, Count: 20, Radius: 1500 } },
    { Carve: { Name: 'PersianGulf', Polygon: Gulfs.PersianGulf, RampKm: 200 } },
    { Carve: { Name: 'RedSea', Polygon: Gulfs.RedSea, RampKm: 120 } },
    { Carve: { Name: 'GulfOfGuinea', Polygon: Gulfs.GulfOfGuinea, RampKm: 150 } },
    { Pad: { Name: 'Kinshasa', Lon: 15.3, Lat: -4.3, RadiusKm: 240 } },
    { Pad: { Name: 'Punjab', Lon: 74.0, Lat: 31.0, RadiusKm: 180 } },
    { Pad: { Name: 'Queensland', Lon: 145.0, Lat: -22.0, RadiusKm: 160 } }
]);
Terra.Cursor = { Lon: 15.3, Lat: -4.3 };

const Coverage = Terrain.ResolveCoverage(Terra);
console.log(`planet: ${(Coverage.LandShare * 100).toFixed(1)}% land, peak ${Math.round(Coverage.Peak)} m, `
    + `${Terra.Cities.length} pads, ${Terra.Heights.length} cells`);

Write('WorldMap.png', Scenes.RenderPlanetMap(Terra, { Width: 960, Height: 480, Cursor: true }), 960);
Write('CityPads.png', Scenes.RenderCityPads(Terra, { Width: 720, Height: 220 }), 720);

// 📝 The timeline is the owner's own workflow: flat, then a continent, then the gulfs and a pad. Three maps, one row.
// 📝 Stage one is the owner's first instruction — sculpt the flat ground — so it is a real brush stroke, not an empty field.
const Shelf = Terrain.CreateField(Terrain.ConfigurePlanet());
Terrain.ApplyBrush(Shelf, { Tool: 'Raise', Lon: 18, Lat: 4, RadiusKm: 2800, Strength: 1 });
Terrain.ApplyBrush(Shelf, { Tool: 'Raise', Lon: 18, Lat: 4, RadiusKm: 2800, Strength: 1 });
Terrain.ApplyBrush(Shelf, { Tool: 'Raise', Lon: 18, Lat: 4, RadiusKm: 2800, Strength: 1 });
Terrain.ApplyBrush(Shelf, { Tool: 'Plateau', Lon: 18, Lat: 4, RadiusKm: 2600, Strength: 1 });
const Stages = [
    { Label: '① sculpt a flat shelf', Field: Shelf },
    {
        Label: '② stamp a continent',
        Field: Sculpt(Terrain.CreateField(Terrain.ConfigurePlanet()), [
            { Stamp: { Name: 'Africa', Polygon: Continents.Africa, Plateau: 680 } }
        ])
    },
    {
        Label: '③ carve the gulfs, then the pad',
        Field: Sculpt(Terrain.CreateField(Terrain.ConfigurePlanet()), [
            { Stamp: { Name: 'Africa', Polygon: Continents.Africa, Plateau: 680 } },
            { Carve: { Name: 'RedSea', Polygon: Gulfs.RedSea, RampKm: 120 } },
            { Carve: { Name: 'GulfOfGuinea', Polygon: Gulfs.GulfOfGuinea, RampKm: 150 } },
            { Pad: { Name: 'Kinshasa', Lon: 15.3, Lat: -4.3, RadiusKm: 240 } }
        ])
    }
];
const PanelWidth = 460, PanelHeight = 230, Gap = 18;
const Timeline = [];
Timeline.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${Stages.length * PanelWidth + (Stages.length - 1) * Gap}" `
    + `height="${PanelHeight + 34}" viewBox="0 0 ${Stages.length * PanelWidth + (Stages.length - 1) * Gap} ${PanelHeight + 34}" `
    + `font-family="'Inter',system-ui,sans-serif">`);
Timeline.push(`<rect width="100%" height="100%" fill="#0b1218"/>`);
Stages.forEach((Stage, Index) =>
{
    const X = Index * (PanelWidth + Gap);
    const Map = Scenes.RenderPlanetMap(Stage.Field, { Width: PanelWidth, Height: PanelHeight, Cursor: false })
        .replace('<svg ', '<svg x="0" y="0" ')
        .replace(/^<svg([^>]*)>/, `<svg$1 style="overflow:hidden">`);
    Timeline.push(`<g transform="translate(${X}, 0)">`);
    Timeline.push(Map.replace(/width="\d+" height="\d+"/, `width="${PanelWidth}" height="${PanelHeight}"`));
    Timeline.push(`<text x="8" y="${PanelHeight + 22}" fill="${PAPER}" font-size="12" fill-opacity="0.85">`
        + `${Stage.Label}</text></g>`);
});
Timeline.push('</svg>');
Write('SculptTimeline.png', Timeline.join(''), Stages.length * PanelWidth + (Stages.length - 1) * Gap);

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE OCEAN
//------------------------------------------------------------------------------------------------------------------------

const Ocean = OceanField.CreateOcean(Terra);

// 📝 Put the patch where there is water to solve: walk west from the Gulf of Guinea shore until the shelf is a few hundred
//    metres deep, so the section shows a coast on one side and open water on the other.
let Anchor = { Lon: 8.2, Lat: 4 };
for (let Lon = 9.5; Lon > -20; Lon -= 0.1)
{
    const Depth = -Terra.Sample.Distance(Lon, 4);
    if (Depth > 120 && Depth < 500) { Anchor = { Lon, Lat: 4 }; break; }
    Anchor = { Lon, Lat: 4 };
}
const Patch = OceanField.CreatePatch(Ocean, { FocusLon: Anchor.Lon, FocusLat: Anchor.Lat, Cells: 112, ExtentKm: 1.6 });
Ocean.Patch = Patch;
OceanField.SettlePatch(Ocean, Patch, 240);
OceanField.AddProbe(Patch, { Name: 'Surf', Lon: Anchor.Lon + 0.0016, Lat: Anchor.Lat });
OceanField.AddProbe(Patch, { Name: 'Shelf', Lon: Anchor.Lon - 0.0012, Lat: Anchor.Lat });

const Plan = OceanMesh.CreatePlan(Ocean, { FocusLon: Anchor.Lon, FocusLat: Anchor.Lat, CameraHeightKm: 2,
    MaxTriangles: 120000, PixelBudget: 2.5 });
const Mesh = OceanMesh.BuildMesh(Ocean, Plan);
const Summary = OceanMesh.Summarize(Plan, Mesh);
console.log(`ocean: ${Summary.Triangles} triangles (${Summary.NearTriangles} near / ${Summary.MidTriangles} mid / `
    + `${Summary.FarTriangles} far), ${Summary.Ratio.toFixed(0)}× cheaper than uniform, worst ${Summary.WorstErrorPx.toFixed(1)} px`);

Write('OceanResolution.png', Scenes.RenderOceanPlan(Ocean, Plan, Mesh,
    { Width: 700, Height: 700, SpanKm: Plan.Settings.AdaptiveKm }), 700);

// 📝 Six seconds apart: the solve is running, so the cross-section has to look different in each frame.
for (let Frame = 0; Frame < 180; ++Frame) OceanField.StepPatch(Ocean, Patch, 1 / 30);
Write('OceanSection.png', Scenes.RenderSurfaceSlice(Ocean, { Width: 760, Height: 300 }), 760);

//------------------------------------------------------------------------------------------------------------------------
//                                                  TRIANGLE BUDGET
//------------------------------------------------------------------------------------------------------------------------

function RenderBudget(Summary, Width, Height)
{
    // 📝 Two scales, because one bar of five million against three bars of sixty thousand is a chart that says nothing about
    //    the tiers. The uniform bar is drawn against its own width; the tiers against the adaptive total.
    const Rows = [
        { Label: 'uniform grid at the near cell', Value: Summary.UniformTriangles, Colour: '#3a4a5a', Scale: Summary.UniformTriangles },
        { Label: 'adaptive · total', Value: Summary.Triangles, Colour: '#96603a', Scale: Math.max(Summary.UniformTriangles, Summary.Triangles) },
        { Label: 'adaptive · near', Value: Summary.NearTriangles, Colour: COOL, Scale: Summary.Triangles },
        { Label: 'adaptive · mid', Value: Summary.MidTriangles, Colour: DEEP, Scale: Summary.Triangles },
        { Label: 'adaptive · far fan', Value: Summary.FarTriangles, Colour: ACCENT, Scale: Summary.Triangles }
    ];
    const Left = 260, Right = Width - 130, Span = Right - Left;
    const Lines = [];
    Lines.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${Width}" height="${Height}" `
        + `viewBox="0 0 ${Width} ${Height}" font-family="'Inter',system-ui,sans-serif">`);
    Lines.push(`<rect width="${Width}" height="${Height}" fill="#0b1218"/>`);
    Lines.push(`<text x="18" y="26" fill="${PAPER}" font-size="13">Where the triangles went — `
        + `${Summary.Triangles.toLocaleString('en-GB')} against ${Math.round(Summary.UniformTriangles).toLocaleString('en-GB')} `
        + `for a uniform grid</text>`);
    Rows.forEach((Row, Index) =>
    {
        const Y = 46 + Index * 34;
        const Length = Math.max(1.5, (Row.Value / Row.Scale) * Span);
        Lines.push(`<text x="${Left - 12}" y="${Y + 14}" text-anchor="end" fill="${PAPER}" font-size="11.5" `
            + `fill-opacity="0.82">${Row.Label}</text>`);
        Lines.push(`<rect x="${Left}" y="${Y}" width="${Length.toFixed(1)}" height="20" rx="4" fill="${Row.Colour}"/>`);
        Lines.push(`<text x="${Left + Length + 8}" y="${Y + 14}" fill="${Row.Colour}" font-size="11.5" `
            + `font-family="'JetBrains Mono',monospace">${Row.Value.toLocaleString('en-GB')}</text>`);
        if (Index === 1)
        {
            Lines.push(`<line x1="${Left}" y1="${Y + 26}" x2="${Right}" y2="${Y + 26}" stroke="#2a3944"/>`);
            Lines.push(`<text x="${Left}" y="${Y + 24}" fill="${PAPER}" font-size="9.5" fill-opacity="0.45">`
                + `the three rows below are drawn against the adaptive total, not the uniform grid</text>`);
        }
    });
    Lines.push(`<text x="18" y="${Height - 26}" fill="${PAPER}" font-size="11" fill-opacity="0.65">`
        + `budget ${Summary.Budget.toLocaleString('en-GB')} · ${Summary.Tiles.toLocaleString('en-GB')} tiles · `
        + `cells ${(Summary.MinCellKm * 1000).toFixed(0)} m near to ${(Summary.MaxCellKm * 1000).toFixed(0)} m out · `
        + `worst screen error ${Summary.WorstErrorPx.toFixed(1)} px</text>`);
    Lines.push(`<text x="18" y="${Height - 10}" fill="${COOL}" font-size="11">`
        + `${Summary.Ratio.toFixed(0)}× fewer triangles than a uniform grid at the same near cell size</text>`);
    Lines.push('</svg>');
    return Lines.join('');
}

Write('OceanBudget.png', RenderBudget(Summary, 760, 290), 760);

//------------------------------------------------------------------------------------------------------------------------
//                                                      SUMMARY
//------------------------------------------------------------------------------------------------------------------------

const Lines = [
    '# World blockout evidence',
    '',
    `Generated ${new Date().toISOString()} · ${Coverage.LandCells} land cells of ${Terra.Heights.length} · `
        + `${Terra.Cities.length} pads · ocean ${Summary.Triangles} triangles`,
    '',
    'Every PNG in this folder is produced by `RenderEvidence.mjs` from the same scene builders the editor page uses.',
    '',
    '| Figure | What it shows |',
    '|--------|----------------|',
    `| \`WorldMap.png\` | Terra-01 after the authored pass: four continents, three carved gulfs, the Sunda isles and ${Terra.Cities.length} city pads |`,
    '| `SculptTimeline.png` | The owner\'s workflow in three frames: flat field, continent stamped, gulfs carved and a pad sunk |',
    '| `CityPads.png` | The placed pads: radius, levelled height, and the ground they sit on |',
    `| \`OceanResolution.png\` | The adaptive ocean plan: ${Summary.Tiles} tiles coloured by cell size, the near footprint, the far fan |`,
    '| `OceanSection.png` | A cross-section through the 2.5D patch: solved surface, spectral field, sea bed, probes |',
    '| `OceanBudget.png` | Where the triangles went, against what a uniform grid would have cost |',
    ''
];
writeFileSync(resolve(ResultsFolder, 'Evidence.md'), Lines.join('\n'));
console.log(`evidence: ${resolve(ResultsFolder, 'Evidence.md')}`);
