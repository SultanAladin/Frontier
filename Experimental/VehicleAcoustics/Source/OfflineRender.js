//============================================================================================================================================
//                                                          OFFLINERENDER.JS
//============================================================================================================================================
// 🧩 Deterministic offline render of any car against a scripted drivetrain schedule. Used by the validation suite to measure the model and to
//    produce the WAV auditions, and by the live page's "render to file" control. No AudioContext, no browser, no wall clock.

import { CreateVehicleVoiceBundle, RenderVehicleVoiceBlock, ConfigureVoiceInput, ClearVoiceEventInput,
         ComposeVoiceReport, ComposeVoiceControls, ListeningPositions, ConfigureVehicleVoiceBundle } from "./VehicleVoice.js";
import { CreateExhaustVoice, RenderExhaustBlock } from "./ExhaustVoice.js";
import { RenderInductionBlock } from "./InductionVoice.js";
import { RenderMechanicalBlock } from "./MechanicalVoice.js";
import { ResolveDrivetrainTelemetry, CreateDrivetrainCondition } from "./VehicleDrivetrain.js";
import { VehicleCatalogue, ResolveVehicleProfile } from "./VehicleCatalogue.js";

// 📝 A script step is { Seconds, Throttle, Brake, Steer, Handbrake, ShiftUp, ShiftDown, SelectReverse, SelectNeutral, AutoShift }.
//    Steps are applied at the start of the block that follows their time, so the schedule is expressed in seconds and is independent of
//    the block size used to render it — a 128-sample block and a 2048-sample block fire the same events.
export function RenderSchedule(Identifier, Schedule, Options = {})
{
    const SampleRate = Options.SampleRate || 48000;
    const BlockSize = Options.BlockSize || 128;
    const Position = Options.Position || ListeningPositions.Exterior;
    const Gain = Options.Gain === undefined ? 1.0 : Options.Gain;
    const Duration = Math.max(0.1, Options.Duration || Schedule.reduce((Sum, Step) => Sum + (Step.Seconds || 0.0), 0.0));
    const TotalSamples = Math.round(Duration * SampleRate);
    const Bundle = CreateVehicleVoiceBundle(Identifier, SampleRate, { Position, Gain });
    const Left = new Float32Array(TotalSamples);
    const Right = new Float32Array(TotalSamples);
    const Telemetry = [];
    let LeftBlock = new Float32Array(BlockSize);
    let RightBlock = new Float32Array(BlockSize);
    let Cursor = 0;
    let StepIndex = 0;
    let StepStart = 0.0;
    const Active = { Throttle: 0.0, Brake: 0.0, Steer: 0.0, Handbrake: false, AutoShift: true,
                     ShiftUp: false, ShiftDown: false, SelectReverse: false, SelectNeutral: false };

    while (Cursor < TotalSamples)
    {
        const Count = Math.min(BlockSize, TotalSamples - Cursor);
        if (Count !== LeftBlock.length)
        {
            LeftBlock = new Float32Array(Count);
            RightBlock = new Float32Array(Count);
        }
        const Now = Cursor / SampleRate;
        while (StepIndex < Schedule.length && Now >= StepStart)
        {
            const Step = Schedule[StepIndex];
            StepStart += Step.Seconds || 0.0;
            Active.Throttle = Step.Throttle !== undefined ? Step.Throttle : Active.Throttle;
            Active.Brake = Step.Brake !== undefined ? Step.Brake : Active.Brake;
            Active.Steer = Step.Steer !== undefined ? Step.Steer : Active.Steer;
            Active.Handbrake = Step.Handbrake !== undefined ? Step.Handbrake : Active.Handbrake;
            Active.AutoShift = Step.AutoShift !== undefined ? Step.AutoShift : Active.AutoShift;
            Active.ShiftUp = !!Step.ShiftUp;
            Active.ShiftDown = !!Step.ShiftDown;
            Active.SelectReverse = !!Step.SelectReverse;
            Active.SelectNeutral = !!Step.SelectNeutral;
            ++StepIndex;
        }

        ConfigureVoiceInput(Bundle, Active);
        RenderVehicleVoiceBlock(Bundle, LeftBlock, RightBlock, Count);
        ClearVoiceEventInput(Bundle);
        Active.ShiftUp = false;
        Active.ShiftDown = false;
        Active.SelectReverse = false;
        Active.SelectNeutral = false;

        Left.set(LeftBlock.subarray(0, Count), Cursor);
        Right.set(RightBlock.subarray(0, Count), Cursor);
        Telemetry.push({ Time: (Cursor + Count) / SampleRate, Speed: Bundle.Telemetry.SpeedKilometresPerHour,
                         RPM: Bundle.Telemetry.EngineRPM, Gear: Bundle.Telemetry.GearIndex,
                         Boost: Bundle.Telemetry.BoostBar, Slip: Bundle.Telemetry.WheelSlipRatio });
        Cursor += Count;
    }

    return { Left, Right, SampleRate, Telemetry, Bundle, Report: ComposeVoiceReport(Bundle) };
}

// 📝 Steady-state tone: the acoustic model driven at a fixed operating point WITHOUT the drivetrain advancing. This is the correct way to
//    measure the voice itself — order content, spectral centroid, crest factor — because a moving car changes rpm during the window and
//    smears every harmonic. Used by the validation suite and by the page's "hold an rpm" audition.
export function RenderSteadyStateTone(Identifier, Point, Seconds, SampleRate = 48000)
{
    const Bundle = CreateVehicleVoiceBundle(Identifier, SampleRate, { Position: ListeningPositions.Exterior,
                                                                      Gain: Point.Gain === undefined ? 1.0 : Point.Gain });
    const Condition = CreateDrivetrainCondition(Bundle.Profile, Point.GearIndex || 1);
    Condition.EngineAngularSpeed = Math.max(0.0, Point.RPM) * Math.PI / 30.0;
    Condition.ThrottlePlate = Point.Throttle === undefined ? 1.0 : Point.Throttle;
    Condition.EngineLoad = Point.Load === undefined ? 0.95 : Point.Load;
    Condition.ExhaustMassFlow = Point.Load === undefined ? 0.9 : Point.Load;
    Condition.OverrunFuelFraction = Point.Overrun || 0.0;
    Condition.Boost = Point.Boost || 0.0;
    Condition.BlowerPressure = Point.BlowerPressure || 0.0;
    Condition.TurbineSpeed = Point.TurbineSpeed || 0.0;
    Condition.GearWhineLoad = Point.Throttle === undefined ? 1.0 : Point.Throttle;
    Condition.WheelSpeed = (Point.RPM * Math.PI / 30.0) /
                           Math.max(0.1, Bundle.Profile.Drivetrain.GearRatios[Math.max(0, (Point.GearIndex || 1) - 1)] *
                                          Bundle.Profile.Drivetrain.FinalDriveRatio);
    Condition.Speed = Condition.WheelSpeed * Bundle.Profile.Chassis.WheelRadiusMetres;
    Bundle.Telemetry = ResolveDrivetrainTelemetry(Condition, Bundle.Profile);
    ComposeVoiceControls(Bundle, Bundle.Telemetry);

    const TotalSamples = Math.round(Seconds * SampleRate);
    const Left = new Float32Array(TotalSamples);
    const Right = new Float32Array(TotalSamples);
    const Block = 256;
    const LeftBlock = new Float32Array(Block);
    const RightBlock = new Float32Array(Block);
    for (let Cursor = 0; Cursor < TotalSamples; Cursor += Block)
    {
        const Count = Math.min(Block, TotalSamples - Cursor);
        LeftBlock.fill(0.0);
        RightBlock.fill(0.0);
        RenderExhaustBlock(Bundle.Exhaust, Bundle.ExhaustControl, LeftBlock, RightBlock, Count);
        RenderInductionBlock(Bundle.Induction, Bundle.InductionControl, LeftBlock, RightBlock, Count);
        RenderMechanicalBlock(Bundle.Mechanical, Bundle.MechanicalControl, LeftBlock, RightBlock, Count);
        const Gain = Point.Gain === undefined ? 1.0 : Point.Gain;
        for (let Index = 0; Index < Count; ++Index)
        {
            Left[Cursor + Index] = Math.tanh(LeftBlock[Index] * Gain) * 0.98;
            Right[Cursor + Index] = Math.tanh(RightBlock[Index] * Gain) * 0.98;
        }
    }
    const FiringFrequency = Bundle.Profile.Electric ? 0.0
        : Point.RPM / 60.0 * Math.max(1, Bundle.Profile.Combustion.CylinderCount) /
          Math.max(1.0, Bundle.Profile.Combustion.CycleRevolutions || 2.0);
    return { Left, Right, SampleRate, Bundle, FiringFrequency, Report: ComposeVoiceReport(Bundle) };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MEASUREMENTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Peak, RMS, crest factor and a Third-octave band summary. The band summary is the reviewer's evidence: a V12 and a V8 must differ in
//    where their energy sits, not only in level.
export function MeasureSignal(Samples, SampleRate)
{
    let Peak = 0.0;
    let EnergySum = 0.0;
    let ZeroCrossings = 0;
    let Previous = 0.0;
    for (let Index = 0; Index < Samples.length; ++Index)
    {
        const Magnitude = Math.abs(Samples[Index]);
        if (Magnitude > Peak) Peak = Magnitude;
        EnergySum += Samples[Index] * Samples[Index];
        if ((Samples[Index] >= 0.0) !== (Previous >= 0.0)) ++ZeroCrossings;
        Previous = Samples[Index];
    }
    const Rms = Math.sqrt(EnergySum / Math.max(1, Samples.length));
    return {
        Peak,
        Rms,
        PeakDecibels: 20.0 * Math.log10(Math.max(1.0e-9, Peak)),
        RmsDecibels: 20.0 * Math.log10(Math.max(1.0e-9, Rms)),
        CrestFactor: Rms > 1.0e-9 ? Peak / Rms : 0.0,
        ZeroCrossingRate: ZeroCrossings / Math.max(1, Samples.length) * SampleRate,
        NonFinite: Number.isFinite(Peak) && Number.isFinite(Rms) ? 0 : 1
    };
}

// 📝 Goertzel at a list of frequencies: cheap spectral evidence without a full FFT dependency. Used to assert that the firing order really
//    appears where the catalogue says it should.
export function MeasureBandMagnitude(Samples, SampleRate, Frequency)
{
    const Omega = 2.0 * Math.PI * Frequency / SampleRate;
    const Coefficient = 2.0 * Math.cos(Omega);
    let First = 0.0, Second = 0.0;
    for (let Index = 0; Index < Samples.length; ++Index)
    {
        const Value = Samples[Index] + Coefficient * First - Second;
        Second = First;
        First = Value;
    }
    const Real = First - Second * Math.cos(Omega);
    const Imaginary = Second * Math.sin(Omega);
    return Math.sqrt(Real * Real + Imaginary * Imaginary) / Math.max(1, Samples.length);
}

// 📝 Dominant harmonic of a window: scans the firing-order ladder and reports which order carries the most energy. This is how the
//    validation proves the flat-plane V8 peaks on a different order than the cross-plane V8.
export function MeasureDominantOrder(Samples, SampleRate, FiringFrequency, OrderCount = 8)
{
    let BestOrder = 0.0;
    let BestMagnitude = 0.0;
    const Ladder = [];
    for (let OrderIndex = 1; OrderIndex <= OrderCount; ++OrderIndex)
    {
        const Frequency = FiringFrequency * OrderIndex;
        if (Frequency > SampleRate * 0.45) break;
        const Magnitude = MeasureBandMagnitude(Samples, SampleRate, Frequency);
        Ladder.push({ Order: OrderIndex, Frequency, Magnitude });
        if (Magnitude > BestMagnitude)
        {
            BestMagnitude = Magnitude;
            BestOrder = OrderIndex;
        }
    }
    return { BestOrder, BestMagnitude, Ladder };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        WAV OUTPUT
//------------------------------------------------------------------------------------------------------------------------

// 📝 16-bit PCM little-endian stereo. Written by the Node tool; the browser tool builds a Blob from the same byte layout.
export function EncodeWaveFile(Left, Right, SampleRate)
{
    const Frames = Math.min(Left.length, Right.length);
    const DataBytes = Frames * 4;
    const Buffer = new ArrayBuffer(44 + DataBytes);
    const View = new DataView(Buffer);
    const WriteText = (Offset, Text) =>
    {
        for (let Index = 0; Index < Text.length; ++Index)
            View.setUint8(Offset + Index, Text.charCodeAt(Index));
    };
    WriteText(0, "RIFF");
    View.setUint32(4, 36 + DataBytes, true);
    WriteText(8, "WAVE");
    WriteText(12, "fmt ");
    View.setUint32(16, 16, true);
    View.setUint16(20, 1, true);
    View.setUint16(22, 2, true);
    View.setUint32(24, SampleRate, true);
    View.setUint32(28, SampleRate * 4, true);
    View.setUint16(32, 4, true);
    View.setUint16(34, 16, true);
    WriteText(36, "data");
    View.setUint32(40, DataBytes, true);
    let Offset = 44;
    for (let Index = 0; Index < Frames; ++Index)
    {
        const L = Math.max(-1.0, Math.min(1.0, Left[Index]));
        const R = Math.max(-1.0, Math.min(1.0, Right[Index]));
        View.setInt16(Offset, Math.round(L * 32767.0), true);
        View.setInt16(Offset + 2, Math.round(R * 32767.0), true);
        Offset += 4;
    }
    return new Uint8Array(Buffer);
}

// 📝 The standard audition schedule every car is rendered with: idle, a blipped throttle, a launch, a full-throttle pull through the box,
//    a lift-and-overrun, a brake, then back to idle.
export function ComposeStandardSchedule()
{
    return [
        { Seconds: 1.20, Throttle: 0.00, AutoShift: true },
        { Seconds: 0.50, Throttle: 0.55 },
        { Seconds: 0.40, Throttle: 0.00 },
        { Seconds: 0.60, Throttle: 0.00, Brake: 0.35 },
        { Seconds: 0.70, Throttle: 1.00, Brake: 0.00 },
        { Seconds: 4.20, Throttle: 1.00 },
        { Seconds: 1.10, Throttle: 0.00 },
        { Seconds: 1.20, Throttle: 0.00, Brake: 0.55 },
        { Seconds: 1.40, Throttle: 0.00, Brake: 0.00 }
    ];
}

// 📝 Spectral centroid and band energy from the Goertzel bank: a coarse but dependency-free spectral summary. Bands are chosen to match what
//    a reviewer hears as "body", "bark", "rasp" and "hiss".
export function MeasureSpectralSummary(Samples, SampleRate)
{
    const Bands = [[40, 120], [120, 300], [300, 800], [800, 2000], [2000, 5000], [5000, 12000]];
    const Energy = Bands.map((Band) => 0.0);
    for (let BandIndex = 0; BandIndex < Bands.length; ++BandIndex)
    {
        const [Low, High] = Bands[BandIndex];
        let Sum = 0.0;
        let Count = 0;
        for (let Frequency = Low; Frequency <= High; Frequency *= 1.12)
        {
            const Magnitude = MeasureBandMagnitude(Samples, SampleRate, Frequency);
            Sum += Magnitude * Magnitude;
            Count += 1;
        }
        Energy[BandIndex] = Math.sqrt(Sum / Math.max(1, Count));
    }
    const Total = Energy.reduce((Accumulator, Entry) => Accumulator + Entry, 0.0);
    let Centroid = 0.0;
    for (let BandIndex = 0; BandIndex < Bands.length; ++BandIndex)
        Centroid += 0.5 * (Bands[BandIndex][0] + Bands[BandIndex][1]) * (Energy[BandIndex] / Math.max(1.0e-9, Total));
    return { BandEnergy: Energy, Balance: Energy.map((Entry) => Entry / Math.max(1.0e-9, Total)), CentroidHz: Centroid };
}

export function ListVehicleIdentifiers()
{
    return VehicleCatalogue.map((Vehicle) => ({ Identifier: Vehicle.Identifier, DisplayName: Vehicle.DisplayName }));
}

// 📝 The browser cannot run node:test, so it runs the same assertions from this module and prints the report on the bench. The Node suite
//    (Tools/VehicleAcoustics.test.mjs) covers the identical checks in CI form.
export async function RunBrowserValidations()
{
    const { ComposeValidationReport } = await import("./ValidationSuite.js");
    return ComposeValidationReport();
}

export { ResolveVehicleProfile };
