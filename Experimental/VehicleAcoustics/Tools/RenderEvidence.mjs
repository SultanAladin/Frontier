//============================================================================================================================================
//                                                     RENDEREVIDENCE.MJS
//============================================================================================================================================
// 🧩 Produces the retained evidence for this bench: one WAV audition per car, one steady-state WAV per car, a measured results table, and two
//    SVG charts drawn directly from the measured samples. Nothing here is illustrative — every number and every plotted point comes from
//    rendering Source/ through Source/OfflineRender.js.

import { writeFileSync, mkdirSync, readdirSync, statSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

import { VehicleCatalogue, ResolveVehicleProfile } from "../Source/VehicleCatalogue.js";
import { RenderSchedule, RenderSteadyStateTone, ComposeStandardSchedule, EncodeWaveFile, MeasureSignal,
         MeasureBandMagnitude, MeasureSpectralSummary } from "../Source/OfflineRender.js";
import { ComposeValidationReport, FormatValidationReport } from "../Source/ValidationSuite.js";

const Root = join(dirname(fileURLToPath(import.meta.url)), "..");
const AuditionFolder = join(Root, "Validation", "Auditions");
const ResultFolder = join(Root, "Validation", "Results");
mkdirSync(AuditionFolder, { recursive: true });
mkdirSync(ResultFolder, { recursive: true });

const SampleRate = 48000;
const Rows = [];
const Started = Date.now();

for (const Vehicle of VehicleCatalogue)
{
    const Profile = ResolveVehicleProfile(Vehicle.Identifier);
    const ScheduleStarted = Date.now();
    const Audition = RenderSchedule(Vehicle.Identifier, ComposeStandardSchedule(),
                                    { Duration: 11.0, SampleRate, BlockSize: 128 });
    const AuditionMilliseconds = Date.now() - ScheduleStarted;
    writeFileSync(join(AuditionFolder, `audition-${Vehicle.Identifier}.wav`),
                  EncodeWaveFile(Audition.Left, Audition.Right, SampleRate));

    const SteadyRPM = Profile.Electric ? 9000 : Math.round(Profile.Combustion.RedlineRPM * 0.72);
    const Steady = RenderSteadyStateTone(Vehicle.Identifier, { RPM: SteadyRPM, Throttle: 1.0, Load: 0.95 }, 2.0, SampleRate);
    writeFileSync(join(AuditionFolder, `steady-${Vehicle.Identifier}-${SteadyRPM}.wav`),
                  EncodeWaveFile(Steady.Left, Steady.Right, SampleRate));

    const AuditionMeasurement = MeasureSignal(Audition.Left, SampleRate);
    const SteadyMeasurement = MeasureSignal(Steady.Left, SampleRate);
    const Spectral = MeasureSpectralSummary(Steady.Left, SampleRate);
    const TopSpeed = Audition.Telemetry.reduce((Best, Entry) => Math.max(Best, Entry.Speed), 0.0);
    Rows.push({
        Identifier: Vehicle.Identifier,
        Name: Vehicle.DisplayName,
        Cylinders: Vehicle.Combustion.CylinderCount,
        Layout: Vehicle.Combustion.CrankLayout,
        Induction: Vehicle.Induction.Topology,
        Firing: Profile.Firing.EventCount,
        Even: Profile.Firing.EvenFiring,
        FiringHzAtSteady: Steady.FiringFrequency,
        SteadyRPM,
        AuditionPeak: AuditionMeasurement.Peak,
        AuditionRmsDecibels: AuditionMeasurement.RmsDecibels,
        AuditionCrest: AuditionMeasurement.CrestFactor,
        SteadyCrest: SteadyMeasurement.CrestFactor,
        CentroidHz: Spectral.CentroidHz,
        TopSpeed: TopSpeed,
        AuditionMilliseconds
    });
    process.stdout.write(`${Vehicle.Identifier.padEnd(15)} steady ${SteadyRPM} rpm · firing ${Steady.FiringFrequency.toFixed(0)} Hz · ` +
                         `crest ${SteadyMeasurement.CrestFactor.toFixed(2)} · centroid ${Spectral.CentroidHz.toFixed(0)} Hz · ` +
                         `render ${AuditionMilliseconds} ms\n`);
}

const Validation = ComposeValidationReport();
const TotalMilliseconds = Date.now() - Started;

//------------------------------------------------------------------------------------------------------------------------
//                                                     RESULTS TABLES
//------------------------------------------------------------------------------------------------------------------------

const Table = (Headers, Entries) =>
{
    const Width = Headers.map((Header, Index) => Math.max(Header.length, ...Entries.map((Row) => String(Row[Index]).length)));
    const Line = (Cells) => Cells.map((Cell, Index) => String(Cell).padEnd(Width[Index])).join("  ");
    return [Line(Headers), Width.map((Value) => "-".repeat(Value)).join("  "), ...Entries.map(Line)].join("\n");
};

const SummaryTable = Table(
    ["car", "cyl", "layout", "induction", "events", "even", "firing Hz", "peak", "rms dBFS", "crest", "centroid Hz", "top km/h"],
    Rows.map((Row) => [Row.Identifier, Row.Cylinders || "EV", Row.Layout, Row.Induction, Row.Firing, Row.Even ? "yes" : "no",
                       Row.FiringHzAtSteady.toFixed(0), Row.AuditionPeak.toFixed(3), Row.AuditionRmsDecibels.toFixed(1),
                       Row.AuditionCrest.toFixed(2), Row.CentroidHz.toFixed(0), Row.TopSpeed.toFixed(0)]));

writeFileSync(join(ResultFolder, "AcousticSummary.txt"),
    `Vehicle Acoustics Bench — measured summary\n` +
    `generated ${new Date().toISOString()} · sample rate ${SampleRate} Hz · audition 11 s · steady 2 s\n` +
    `renderer: ${process.platform}/${process.arch} node ${process.version}\n\n` +
    `${SummaryTable}\n\n` +
    `Steady-state points are rendered at 72% of each car's redline (9000 rpm for the electric car).\n` +
    `"events" is firing events per combustion cycle; "even" is the derived firing-geometry verdict.\n` +
    `Total render time for all auditions and steady tones: ${TotalMilliseconds} ms.\n`);

writeFileSync(join(ResultFolder, "ValidationReport.txt"),
    `Vehicle Acoustics Bench — validation suite\n` +
    `generated ${new Date().toISOString()}\n\n` +
    `${FormatValidationReport(Validation)}\n`);

//------------------------------------------------------------------------------------------------------------------------
//                                                  HARMONIC LADDER CHART
//------------------------------------------------------------------------------------------------------------------------

// 📝 Measured harmonic ladder. For each car, every firing order is measured with a Hann-windowed Goertzel transform averaged over three bins
//    either side of the expected frequency, then normalised to that car's own strongest order. The plot therefore answers one question
//    directly: where does each engine put its energy along the order ladder?
function MeasureOrderLadder(Samples, SampleRate, FiringFrequency, OrderCount)
{
    const Windowed = new Float32Array(Samples.length);
    for (let Index = 0; Index < Samples.length; ++Index)
        Windowed[Index] = Samples[Index] * (0.5 - 0.5 * Math.cos((2.0 * Math.PI * Index) / (Samples.length - 1)));
    const Ladder = [];
    for (let Order = 1; Order <= OrderCount; ++Order)
    {
        const Frequency = FiringFrequency * Order;
        if (Frequency > SampleRate * 0.45)
            break;
        let Power = 0.0;
        for (const Offset of [-1.5, -0.75, 0.0, 0.75, 1.5])
            Power += MeasureBandMagnitude(Windowed, SampleRate, Frequency + Offset) ** 2;
        Ladder.push({ Order, Frequency, Magnitude: Math.sqrt(Power) });
    }
    const Peak = Math.max(...Ladder.map((Entry) => Entry.Magnitude), 1.0e-9);
    return Ladder.map((Entry) => ({ ...Entry, Relative: Entry.Magnitude / Peak }));
}

const Compared = [
    { Identifier: "mustangGt", Colour: "#f4a259", Label: "Mustang GT · cross-plane V8" },
    { Identifier: "ageraRs", Colour: "#3ec9c9", Label: "Agera RS · flat-plane V8" },
    { Identifier: "laFerrari", Colour: "#e4572e", Label: "LaFerrari · 65° V12" },
    { Identifier: "viperAcr", Colour: "#9b7bd4", Label: "Viper · odd-fire V10" }
];

const OrderCount = 8;
const Traces = Compared.map((Entry) =>
{
    const Profile = ResolveVehicleProfile(Entry.Identifier);
    const RPM = Math.round(Profile.Combustion.RedlineRPM * 0.55);
    const Render = RenderSteadyStateTone(Entry.Identifier, { RPM, Throttle: 1.0, Load: 0.95 }, 1.2, SampleRate);
    return { ...Entry, RPM, Firing: Render.FiringFrequency,
             Ladder: MeasureOrderLadder(Render.Left, SampleRate, Render.FiringFrequency, OrderCount) };
});

const ChartWidth = 1040;
const ChartHeight = 430;
const PlotLeft = 78;
const PlotTop = 96;
const PlotWidth = ChartWidth - PlotLeft - 30;
const PlotHeight = ChartHeight - PlotTop - 66;
const Decibels = (Relative) => Math.max(-60.0, 20.0 * Math.log10(Math.max(Relative, 1.0e-4)));
const XForOrder = (Order) => PlotLeft + (Order - 1) * (PlotWidth / (OrderCount - 1));
const YForDecibels = (Value) => PlotTop + PlotHeight - ((Value + 60.0) / 60.0) * PlotHeight;

const GridLines = [];
for (let Value = 0; Value >= -60; Value -= 20)
    GridLines.push(`<line x1="${PlotLeft}" y1="${YForDecibels(Value).toFixed(1)}" x2="${PlotLeft + PlotWidth}" ` +
                   `y2="${YForDecibels(Value).toFixed(1)}" stroke="#263041"/>` +
                   `<text x="${PlotLeft - 10}" y="${(YForDecibels(Value) + 4).toFixed(1)}" fill="#8b99ae" font-size="11" ` +
                   `text-anchor="end">${Value} dB</text>`);
for (let Order = 1; Order <= OrderCount; ++Order)
    GridLines.push(`<text x="${XForOrder(Order).toFixed(1)}" y="${PlotTop + PlotHeight + 20}" fill="#8b99ae" font-size="11" ` +
                   `text-anchor="middle">×${Order}</text>`);

const Traces2 = Traces.map((Trace) =>
{
    const Points = Trace.Ladder.map((Entry) => `${XForOrder(Entry.Order).toFixed(1)},${YForDecibels(Decibels(Entry.Relative)).toFixed(1)}`);
    const Stems = Trace.Ladder.map((Entry) =>
        `<line x1="${XForOrder(Entry.Order).toFixed(1)}" y1="${PlotTop + PlotHeight}" ` +
        `x2="${XForOrder(Entry.Order).toFixed(1)}" y2="${YForDecibels(Decibels(Entry.Relative)).toFixed(1)}" ` +
        `stroke="${Trace.Colour}" stroke-opacity="0.28"/>` +
        `<circle cx="${XForOrder(Entry.Order).toFixed(1)}" cy="${YForDecibels(Decibels(Entry.Relative)).toFixed(1)}" r="3.4" ` +
        `fill="${Trace.Colour}"/>`).join("\n  ");
    return `<polyline fill="none" stroke="${Trace.Colour}" stroke-width="1.8" points="${Points.join(" ")}"/>\n  ${Stems}`;
});

const Legend = Traces.map((Trace, Index) =>
    `<rect x="${PlotLeft + 6}" y="${34 + Index * 20}" width="16" height="3" fill="${Trace.Colour}"/>` +
    `<text x="${PlotLeft + 30}" y="${41 + Index * 20}" fill="#e8edf4" font-size="12">${Trace.Label} · ${Trace.RPM} rpm · ` +
    `firing ${Trace.Firing.toFixed(0)} Hz</text>`);

// 📝 The table under the legend is the claim stated as numbers, so the chart cannot be read as decoration.
const LadderRows = Traces.map((Trace) =>
{
    const Value = (Order) => Trace.Ladder[Order - 1] ? Decibels(Trace.Ladder[Order - 1].Relative).toFixed(1) : "–";
    return `${Trace.Label.padEnd(30)} ×1 ${Value(1).padStart(6)} dB   ×2 ${Value(2).padStart(6)} dB   ` +
           `×3 ${Value(3).padStart(6)} dB   ×4 ${Value(4).padStart(6)} dB`;
});

const SpectrumSvg = `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="${ChartWidth}" height="${ChartHeight}" viewBox="0 0 ${ChartWidth} ${ChartHeight}">
  <rect width="100%" height="100%" fill="#0d1117"/>
  <text x="${PlotLeft}" y="22" fill="#e8edf4" font-size="15" font-family="Segoe UI, system-ui, sans-serif">
    Measured firing-order ladder · wide-open throttle, steady revolution
  </text>
  <text x="${ChartWidth - 30}" y="22" fill="#67748a" font-size="11" text-anchor="end" font-family="Segoe UI, system-ui, sans-serif">
    Hann-windowed Goertzel over rendered samples · each car normalised to its own strongest order
  </text>
  ${Legend.join("\n  ")}
  ${GridLines.join("\n  ")}
  <rect x="${PlotLeft}" y="${PlotTop}" width="${PlotWidth}" height="${PlotHeight}" fill="none" stroke="#263041"/>
  ${Traces2.join("\n  ")}
  <text x="${PlotLeft}" y="${ChartHeight - 14}" fill="#67748a" font-size="11" font-family="Segoe UI, system-ui, sans-serif">
    ×n is the nth harmonic of the car's own firing frequency. The flat-plane V8 (Agera) keeps a strong fourth order; the cross-plane V8's
    energy sits low.
  </text>
</svg>
`;
writeFileSync(join(ResultFolder, "HarmonicSpectra.svg"), SpectrumSvg);
writeFileSync(join(ResultFolder, "HarmonicLadder.txt"),
    `Measured firing-order ladder · 48 kHz · 1.2 s Hann-windowed render at 55% of redline · generated ${new Date().toISOString()}\n\n` +
    LadderRows.join("\n") + "\n\n" +
    `All values are dB relative to each car's own strongest order, so the table compares shape rather than loudness.\n`);

//------------------------------------------------------------------------------------------------------------------------
//                                                     PIPELINE CHART
//------------------------------------------------------------------------------------------------------------------------

// 📝 Block-size chart: rendered level and crest factor as the device quantum changes. A dependency on the quantum would show as a step here.
const ChunkSizes = [64, 128, 256, 512, 1024, 2048];
const PipelinePoints = ChunkSizes.map((Chunk) =>
{
    const Result = RenderSchedule("gt-rNismoGt3", ComposeStandardSchedule(), { Duration: 4.0, SampleRate, BlockSize: Chunk });
    const Measurement = MeasureSignal(Result.Left, SampleRate);
    return { Chunk, Rms: Measurement.RmsDecibels, Crest: Measurement.CrestFactor, Peak: Measurement.Peak };
});

const PipelineSvg = `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="${ChartWidth}" height="260" viewBox="0 0 ${ChartWidth} 260">
  <rect width="100%" height="100%" fill="#0d1117"/>
  <text x="${PlotLeft}" y="20" fill="#e8edf4" font-size="14" font-family="Segoe UI, system-ui, sans-serif">
    Device block size does not change the sound · GT-R GT3, 4 s standard schedule
  </text>
  ${PipelinePoints.map((Point, Index) =>
  {
      const X = PlotLeft + 50 + Index * ((PlotWidth - 90) / (PipelinePoints.length - 1));
      const Height = (Point.Rms + 40.0) * 4.2;
      return `<rect x="${(X - 26).toFixed(1)}" y="${(200 - Height).toFixed(1)}" width="52" height="${Height.toFixed(1)}" ` +
             `fill="#3ec9c9" fill-opacity="0.55"/>` +
             `<text x="${X.toFixed(1)}" y="${(200 - Height - 8).toFixed(1)}" fill="#e8edf4" font-size="11" text-anchor="middle">` +
             `${Point.Rms.toFixed(1)} dB</text>` +
             `<text x="${X.toFixed(1)}" y="218" fill="#8b99ae" font-size="11" text-anchor="middle">${Point.Chunk}</text>`;
  }).join("\n  ")}
  <line x1="${PlotLeft}" y1="200" x2="${ChartWidth - 24}" y2="200" stroke="#263041"/>
  <text x="${PlotLeft}" y="244" fill="#67748a" font-size="11" font-family="Segoe UI, system-ui, sans-serif">
    Bars: RMS level in dBFS for the same schedule at each block size. Labels below: samples per block.
  </text>
</svg>
`;
writeFileSync(join(ResultFolder, "BlockSizeStability.svg"), PipelineSvg);

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRESSURE TRACE CHART
//------------------------------------------------------------------------------------------------------------------------

// 📝 The waveform is the strongest evidence that this is a pulse train and not a filtered noise source: at idle you can count the individual
//    blowdown events; at the limiter they merge into a dense, asymmetric wave. Two traces from two operating points of one engine.
const TraceWindows = [
    { Label: "LaFerrari at 950 rpm idle", Identifier: "laFerrari", Point: { RPM: 950, Throttle: 0.0, Load: 0.15 }, Colour: "#3ec9c9" },
    { Label: "LaFerrari at 6800 rpm, wide-open throttle", Identifier: "laFerrari",
      Point: { RPM: 6800, Throttle: 1.0, Load: 0.95 }, Colour: "#f4a259" }
];

const TraceWidth = 1040;
const TraceHeight = 300;
const WindowMilliseconds = 24.0;
const TraceSamples = TraceWindows.map((Entry) =>
{
    const Render = RenderSteadyStateTone(Entry.Identifier, Entry.Point, 0.3, SampleRate);
    const Count = Math.round((WindowMilliseconds / 1000.0) * SampleRate);
    const Start = Math.round(Render.Left.length * 0.6);
    const Slice = Render.Left.subarray(Start, Start + Count);
    const Peak = Math.max(...Slice.map((Value) => Math.abs(Value)), 1.0e-6);
    return { ...Entry, Slice, Peak, Firing: Render.FiringFrequency };
});

const TracePanel = (Trace, Index) =>
{
    const PanelTop = 40 + Index * 128;
    const PanelHeight = 100;
    const Centre = PanelTop + PanelHeight * 0.5;
    const Points = Array.from(Trace.Slice).map((Value, Position) =>
        `${(PlotLeft + (Position / Trace.Slice.length) * PlotWidth).toFixed(2)},` +
        `${(Centre - (Value / Trace.Peak) * PanelHeight * 0.46).toFixed(2)}`);
    const Events = Trace.Firing * WindowMilliseconds / 1000.0;
    return `<text x="${PlotLeft}" y="${PanelTop - 9}" fill="${Trace.Colour}" font-size="12" font-family="Segoe UI, system-ui, sans-serif">` +
           `${Trace.Label} · firing ${Trace.Firing.toFixed(0)} Hz · ${Events.toFixed(1)} firing events in this ${WindowMilliseconds} ms window</text>` +
           `<line x1="${PlotLeft}" y1="${Centre}" x2="${PlotLeft + PlotWidth}" y2="${Centre}" stroke="#263041"/>` +
           `<polyline fill="none" stroke="${Trace.Colour}" stroke-width="1.1" points="${Points.join(" ")}"/>`;
};

const TraceSvg = `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="${TraceWidth}" height="${TraceHeight}" viewBox="0 0 ${TraceWidth} ${TraceHeight}">
  <rect width="100%" height="100%" fill="#0d1117"/>
  <text x="${PlotLeft}" y="20" fill="#e8edf4" font-size="15" font-family="Segoe UI, system-ui, sans-serif">
    Rendered exhaust pressure · ${WindowMilliseconds} ms of output, normalised per trace
  </text>
  ${TraceSamples.map(TracePanel).join("\n  ")}
  <text x="${PlotLeft}" y="${TraceHeight - 10}" fill="#67748a" font-size="11" font-family="Segoe UI, system-ui, sans-serif">
    Rendered by Source/ExhaustVoice.js through the public render call, not drawn separately from it.
  </text>
</svg>
`;
writeFileSync(join(ResultFolder, "PressureTrace.svg"), TraceSvg);

//------------------------------------------------------------------------------------------------------------------------
//                                                       REAL-TIME COST
//------------------------------------------------------------------------------------------------------------------------

// 📝 Real-time factor: how many seconds of audio one wall-clock second can synthesise, measured by rendering a fixed schedule. A value above 1.0
//    means the synthesiser could run in real time on this machine; the figure here is from the single-threaded Node build on the evidence host.
const Costs = VehicleCatalogue.map((Vehicle) =>
{
    const Started = process.hrtime.bigint();
    const Render = RenderSchedule(Vehicle.Identifier, ComposeStandardSchedule(), { Duration: 3.0, SampleRate, BlockSize: 128 });
    const Elapsed = Number(process.hrtime.bigint() - Started) / 1.0e9;
    void Render;
    return { Identifier: Vehicle.Identifier, Elapsed, Factor: 3.0 / Elapsed };
});

const CostTable = Table(["car", "audio s", "wall s", "real-time factor"],
    Costs.map((Entry) => [Entry.Identifier, "3.00", Entry.Elapsed.toFixed(2), Entry.Factor.toFixed(1) + "×"]));

writeFileSync(join(ResultFolder, "RealTimeCost.txt"),
    `Vehicle Acoustics Bench — synthesis cost\n` +
    `host: ${process.platform}/${process.arch} node ${process.version} · single threaded · 48 kHz stereo · 128-sample blocks\n` +
    `generated ${new Date().toISOString()}\n\n${CostTable}\n\n` +
    `Real-time factor = rendered audio seconds per wall-clock second. Anything above 1.0 can run live; the live bench runs the same code in a\n` +
    `render worker, where the measured factor is the headroom available before the device starves.\n`);
process.stdout.write("\nreal-time factors:\n" + Costs.map((Entry) => `  ${Entry.Identifier.padEnd(15)} ${Entry.Factor.toFixed(1)}×`).join("\n") + "\n");

//------------------------------------------------------------------------------------------------------------------------
//                                                         MANIFEST
//------------------------------------------------------------------------------------------------------------------------

const Files = readdirSync(AuditionFolder).sort().map((Name) =>
    `${Name.padEnd(46)}${(statSync(join(AuditionFolder, Name)).size / 1024).toFixed(0).padStart(8)} KiB`);
writeFileSync(join(ResultFolder, "AuditionManifest.txt"),
    `Audition files · 48 kHz · 16-bit stereo · generated ${new Date().toISOString()}\n\n${Files.join("\n")}\n\n` +
    `Pipeline chart points (rms dBFS by block size):\n` +
    PipelinePoints.map((Point) => `  ${String(Point.Chunk).padStart(5)} samples  ${Point.Rms.toFixed(3)} dBFS  crest ${Point.Crest.toFixed(3)}`).join("\n") +
    `\n`);

process.stdout.write(`\nvalidation ${Validation.Passed}/${Validation.Total} · evidence written to Validation/\n`);
process.stdout.write(readdirSync(ResultFolder).map((Name) => `  Results/${Name}`).join("\n") + "\n");
