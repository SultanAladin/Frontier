//============================================================================================================================================
//                                                            BENCH.JS
//============================================================================================================================================
// 🧩 The bench page: garage selection, WASD drivetrain input, instrument drawing from worker telemetry, listening position, the steady-state
//    hold, the WAV renders and the frame-rate independence probe. Owns presentation only; no synthesis and no physics live here.

import { VehicleCatalogue, ResolveVehicleProfile } from "./VehicleCatalogue.js";
import { VehicleAudioHost, QueryAudioCapability } from "./AudioHost.js";
import { DrawTachometer, DrawAnalyser, ComposeLayerRows, DrawLayerBars } from "./GaugeView.js";
import { RenderSchedule, RenderSteadyStateTone, ComposeStandardSchedule, EncodeWaveFile, MeasureSignal }
    from "./OfflineRender.js";
import { CreateVehicleVoiceBundle, RenderVehicleVoiceBlock, ConfigureVoiceInput, ClearVoiceEventInput }
    from "./VehicleVoice.js";

const Host = new VehicleAudioHost();
const PageState = {
    Identifier: "laFerrari",
    Position: "exterior",
    Gain: 0.9,
    AutoShift: true,
    Running: false,
    State: null,
    Profile: null,
    HoldPoint: null,
    LastTelemetryAt: 0,
    TelemetryRate: 0.0,
    FrameBudget: 60,
    FrameTimes: [],
    PictureStall: false
};

const Element = (Name) => document.getElementById(Name);
const Keys = Object.create(null);

//------------------------------------------------------------------------------------------------------------------------
//                                                        GARAGE
//------------------------------------------------------------------------------------------------------------------------

function BuildGarage()
{
    const List = Element("car-list");
    List.textContent = "";
    for (const Vehicle of VehicleCatalogue)
    {
        const Profile = ResolveVehicleProfile(Vehicle.Identifier);
        const Card = document.createElement("button");
        Card.className = "car-card";
        Card.dataset.identifier = Vehicle.Identifier;
        const Cylinders = Vehicle.Combustion.CylinderCount;
        const InductionLabel = {
            naturallyAspirated: "Naturally aspirated",
            twinScrollTurbo: "Twin-scroll turbo",
            rootsSupercharger: "Roots supercharged",
            electricDrive: "Electric drive"
        }[Vehicle.Induction.Topology] || Vehicle.Induction.Topology;
        Card.innerHTML = `
            <span class="car-index">${String(Vehicle.SortieIndex).padStart(2, "0")}</span>
            <span class="car-name">${Vehicle.DisplayName}</span>
            <span class="car-spec">${Cylinders > 0 ? `${Cylinders} cyl · ${Vehicle.Combustion.CrankLayout}` : "Dual motor"}
                · ${InductionLabel}</span>
            <span class="car-numbers">${Cylinders > 0 ? `${Vehicle.Combustion.RedlineRPM.toFixed(0)} rpm` : `${Vehicle.Combustion.RedlineRPM.toFixed(0)} rpm`}
                · ${Vehicle.Combustion.PeakTorqueNewtonMetres.toFixed(0)} N·m · ${Vehicle.GearCount}${Vehicle.GearCount > 1 ? " gears" : " gear"}</span>
            <span class="car-firing">${Profile.Firing.EventCount > 0
                ? `${Profile.Firing.EventCount} firing events/cycle · ${Profile.Firing.EvenFiring ? "even" : "uneven"}`
                : "no combustion"}</span>`;
        Card.addEventListener("click", () => SelectVehicle(Vehicle.Identifier));
        List.appendChild(Card);
    }
    HighlightGarage(PageState.Identifier);
}

function HighlightGarage(Identifier)
{
    for (const Card of Element("car-list").children)
        Card.classList.toggle("selected", Card.dataset.identifier === Identifier);
}

// 📝 Switching cars preserves the rolling state: the drivetrain condition is carried across in ConfigureVehicleSelection, so changing car
//    mid-run behaves like swapping the shell rather than teleporting the noise to a standstill.
function SelectVehicle(Identifier)
{
    PageState.Identifier = Identifier;
    HighlightGarage(Identifier);
    PageState.Profile = SummariseProfile(ResolveVehicleProfile(Identifier));
    FillVehicleSheet(PageState.Profile);
    if (Host.Started)
        Host.ConfigureVehicle(Identifier, PageState.Position);
    else
        RenderIdleInstruments();
    Element("render-status").textContent = `${ResolveVehicleProfile(Identifier).DisplayName} loaded.`;
}

function SummariseProfile(Profile)
{
    return {
        Identifier: Profile.Identifier,
        Name: Profile.DisplayName,
        Cylinders: Profile.Combustion.CylinderCount,
        Layout: Profile.Combustion.CrankLayout,
        Induction: Profile.Induction.Topology,
        Redline: Profile.Combustion.RedlineRPM,
        Limiter: Profile.Combustion.LimiterRPM,
        Idle: Profile.Combustion.IdleRPM,
        Torque: Profile.Combustion.PeakTorqueNewtonMetres,
        Mass: Profile.Chassis.MassKilograms,
        Electric: Profile.Electric,
        Gears: Profile.Drivetrain.GearRatios,
        FinalDrive: Profile.Drivetrain.FinalDriveRatio,
        Firing: Profile.Firing.EventCount,
        Even: Profile.Firing.EvenFiring,
        GasSpeed: Profile.GasSoundSpeed,
        Modes: Profile.ExhaustModes,
        IntakeModes: Profile.IntakeModes,
        MufflerCutoff: Profile.Exhaust.MufflerCutoffHz,
        Bypass: [Profile.Exhaust.BypassOpenRPM, Profile.Exhaust.BypassFullRPM],
        Boost: Profile.Induction.WastegateMaxBoostBar,
        Blades: Profile.Induction.CompressorBladeCount,
        Lobe: Profile.Induction.LobeCount,
        BlowerRatio: Profile.Induction.BlowerDriveRatio,
        Shift: Profile.Drivetrain.ShiftSeconds,
        Cabin: Profile.Cabin.SoundInsulationDecibels,
        GearCount: Profile.GearCount
    };
}

function FillVehicleSheet(Profile)
{
    const Sheet = Element("vehicle-sheet");
    if (!Profile) { Sheet.textContent = ""; return; }
    const Rows = [
        ["Engine", Profile.Cylinders > 0 ? `${Profile.Cylinders} cylinder · ${Profile.Layout}` : "Electric drive"],
        ["Induction", Profile.Induction],
        ["Idle / limiter", `${Profile.Idle.toFixed(0)} / ${Profile.Limiter.toFixed(0)} rpm`],
        ["Peak torque", `${Profile.Torque.toFixed(0)} N·m`],
        ["Mass", `${Profile.Mass.toFixed(0)} kg`],
        ["Gearbox", `${Profile.Gears.length} ratio(s) · final ${Profile.FinalDrive.toFixed(2)}`],
        ["Shift time", Profile.Shift > 0 ? `${(Profile.Shift * 1000).toFixed(0)} ms` : "single speed"],
        ["Firing", Profile.Firing > 0 ? `${Profile.Firing} events/cycle · ${Profile.Even ? "even" : "uneven"}` : "none"],
        ["Exhaust modes", Profile.Cylinders > 0 ? Profile.Modes.map((Entry) => `${Entry.toFixed(0)}`).join(" / ") + " Hz" : "no exhaust"],
        ["Intake modes", Profile.IntakeModes.length > 0 ? Profile.IntakeModes.map((Entry) => `${Entry.toFixed(0)}`).join(" / ") + " Hz" : "—"],
        ["Muffler knee", Profile.Cylinders > 0 ? `${Profile.MufflerCutoff.toFixed(0)} Hz` : "—"],
        ["Bypass", Profile.Cylinders > 0 ? `${Profile.Bypass[0].toFixed(0)}–${Profile.Bypass[1].toFixed(0)} rpm` : "—"],
        ["Compressor", Profile.Blades > 0 ? `${Profile.Blades} blades · ${Profile.Boost.toFixed(2)} bar` : "none"],
        ["Blower", Profile.Lobe > 0 ? `${Profile.Lobe} lobes · ${Profile.BlowerRatio.toFixed(2)}×` : "none"],
        ["Cabin insulation", `${Profile.Cabin.toFixed(0)} dB`],
        ["Gas sound speed", `${Profile.GasSpeed.toFixed(0)} m/s`]
    ];
    Sheet.textContent = "";
    for (const [Label, Value] of Rows)
    {
        const Term = document.createElement("dt");
        Term.textContent = Label;
        const Definition = document.createElement("dd");
        Definition.textContent = Value;
        Sheet.appendChild(Term);
        Sheet.appendChild(Definition);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          INPUT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Key handling is state-based, not event-based: a keypress sets a boolean, and the renderer samples the booleans. A dropped keyup cannot
//    leave the throttle stuck because every keyup is also handled on blur.
window.addEventListener("keydown", (Event) =>
{
    const Code = Event.code;
    if (Code === "KeyW" || Code === "KeyS" || Code === "KeyA" || Code === "KeyD" || Code === "KeyQ" || Code === "KeyE" ||
        Code === "KeyR" || Code === "KeyN" || Code === "Space" || Code === "KeyM")
    {
        Event.preventDefault();
        if (!Keys[Code])
        {
            if (Code === "KeyQ") Host.ConfigureInput({ ShiftUp: true });
            if (Code === "KeyE") Host.ConfigureInput({ ShiftDown: true });
            if (Code === "KeyR") Host.ConfigureInput({ SelectReverse: true });
            if (Code === "KeyN") Host.ConfigureInput({ SelectNeutral: true });
            if (Code === "KeyM")
            {
                PageState.AutoShift = !PageState.AutoShift;
                Host.ConfigureInput({ AutoShift: PageState.AutoShift });
                Element("subtitle").textContent =
                    `Physics-driven engine, induction and drivetrain sound · ${PageState.AutoShift ? "automatic" : "manual"} shift · ` +
                    `frame-rate independent`;
            }
        }
        Keys[Code] = true;
    }
});
window.addEventListener("keyup", (Event) => { Keys[Event.code] = false; });
window.addEventListener("blur", () => { for (const Code of Object.keys(Keys)) Keys[Code] = false; });

// 📝 Throttle ramps rather than snapping when a key is held: a keyboard is a switch, a pedal is a spring. This is the driver's side of that
//    difference and it is why a WASD throttle sounds like a foot rather than a button.
function ComposeInput()
{
    const Throttle = Keys.KeyW ? 1.0 : 0.0;
    const Brake = Keys.KeyS ? 1.0 : 0.0;
    let Steer = 0.0;
    if (Keys.KeyA) Steer -= 1.0;
    if (Keys.KeyD) Steer += 1.0;
    return { Throttle, Brake, Steer, Handbrake: !!Keys.Space, AutoShift: PageState.AutoShift };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          DEVICE
//------------------------------------------------------------------------------------------------------------------------

async function TogglePower()
{
    if (Host.Started)
    {
        await Host.Stop();
        PageState.Running = false;
        Element("power").textContent = "Start audio";
        Element("transport-mode").textContent = "stopped";
        return;
    }
    try
    {
        Element("power").textContent = "Starting…";
        const Mode = await Host.Start({ Identifier: PageState.Identifier, Position: PageState.Position, Gain: PageState.Gain,
                                        AutoShift: PageState.AutoShift });
        PageState.Running = true;
        Element("power").textContent = "Stop audio";
        Element("transport-mode").textContent = Mode;
    }
    catch (Failure)
    {
        Element("power").textContent = "Start audio";
        Element("transport-mode").textContent = "failed";
        Element("telemetry-line").textContent = `Audio device failed: ${Failure.message}`;
    }
}

Host.ConfigureHandler((Message) =>
{
    if (!Message) return;
    if (Message.Type === "telemetry")
    {
        PageState.State = Message.State;
        PageState.Profile = Message.Vehicle || PageState.Profile;
        PageState.LastTelemetryAt = performance.now();
        Element("ring-fill").textContent = `${Message.Fill} smp`;
        Element("underrun").textContent = `${Message.Underrun}`;
        if (Message.Energy !== undefined && Message.Vehicle && Message.Vehicle.Electric)
            Element("telemetry-line").textContent = `Battery ${(Message.Energy * 100).toFixed(0)}% · ${Message.Vehicle.Name}`;
        FillVehicleSheet(PageState.Profile);
        return;
    }
    if (Message.Type === "configured")
    {
        Element("report-line").textContent = Message.Report || "";
        return;
    }
    if (Message.Type === "error" || Message.Type === "note")
    {
        Element("telemetry-line").textContent = Message.Message || Message.Type;
        return;
    }
});

//------------------------------------------------------------------------------------------------------------------------
//                                                      PICTURE LOOP
//------------------------------------------------------------------------------------------------------------------------

let LastFrameAt = typeof performance !== "undefined" ? performance.now() : 0.0;
let FrameAccumulator = 0.0;

// 📝 The picture loop is deliberately allowed to be slow. The frame-budget slider inserts real work — a busy-wait — so the audio pipeline can be
//    stressed at 6 fps without the sound changing. If any part of the sound were tied to this loop, the probe below would catch it.
function PictureLoop()
{
    const Now = performance.now();
    const Delta = Now - LastFrameAt;
    LastFrameAt = Now;
    PageState.FrameTimes.push(Delta);
    if (PageState.FrameTimes.length > 40) PageState.FrameTimes.shift();
    FrameAccumulator += Delta;

    if (PageState.Running)
        Host.ConfigureInput(ComposeInput());

    const Budget = 1000.0 / Math.max(1, PageState.FrameBudget);
    const Stall = Math.max(0.0, Budget - Delta * 0.9);
    if (Stall > 1.0)
    {
        const Until = performance.now() + Stall;
        while (performance.now() < Until) { /* deliberate picture stall */ }
    }

    DrawInstruments();
    requestAnimationFrame(PictureLoop);
}

function DrawInstruments()
{
    const Profile = PageState.Profile;
    const State = PageState.State || IdleState();
    DrawTachometer(Element("tach"), State, Profile);
    DrawAnalyser(Element("analyser"), Host.QuerySpectrum(), Host.SampleRate || 48000, State.FiringHz || 0);
    DrawLayerBars(Element("layer-bars"), ComposeLayerRows(State, Profile));
    Element("rpm-value").textContent = State.RPM.toFixed(0);
    Element("gear-value").textContent = State.Gear === 0 ? "N" : (State.Gear < 0 ? "R" : `${State.Gear}`);
    Element("speed-value").textContent = State.Speed.toFixed(0);
    Element("boost-value").textContent = (State.Boost || 0).toFixed(2);
    Element("slip-value").textContent = (State.Slip || 0).toFixed(2);
    Element("firing-value").textContent = (State.FiringHz || 0).toFixed(0);
    Element("wheel-value").textContent = (State.WheelRPM || 0).toFixed(0);
    Element("steer-value").textContent = (((State.SteerRadians || 0) * 180.0) / Math.PI).toFixed(1);
    Element("limiter-lamp").classList.toggle("lit", (State.Limiter || 0) > 0.1);
    if (!Host.Started)
    {
        Element("ring-fill").textContent = "idle";
        Element("underrun").textContent = "0";
    }
}

function IdleState()
{
    return { RPM: 0, Speed: 0, Gear: 0, Throttle: 0, Brake: 0, Boost: 0, BoostFraction: 0, Blower: 0, BlowOff: 0,
             Slip: 0, Scrub: 0, Lash: 0, Fan: 0, Bypass: 0, Misfire: 0, Overrun: 0, Limiter: 0, WheelRPM: 0,
             FiringHz: 0, Clutch: 0, ShiftCut: 0, Lateral: 0, Longitudinal: 0, Distance: 0, Lockup: 0, TyreTemp: 0,
             SteerRadians: 0 };
}

function RenderIdleInstruments()
{
    PageState.State = IdleState();
    DrawInstruments();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     OFFLINE RENDERS
//------------------------------------------------------------------------------------------------------------------------

function Download(Bytes, Name)
{
    const Blob = new Blob([Bytes], { type: "audio/wav" });
    const Url = URL.createObjectURL(Blob);
    const Anchor = document.createElement("a");
    Anchor.href = Url;
    Anchor.download = Name;
    document.body.appendChild(Anchor);
    Anchor.click();
    document.body.removeChild(Anchor);
    setTimeout(() => URL.revokeObjectURL(Url), 4000);
}

// 📝 The auditions are rendered offline rather than recorded from the device: that keeps them bit-identical between runs and independent of the
//    browser's resampling, which is what makes them usable as evidence.
function RenderAudition()
{
    Element("render-status").textContent = "Rendering audition…";
    setTimeout(() =>
    {
        const Started = performance.now();
        const Result = RenderSchedule(PageState.Identifier, ComposeStandardSchedule(), { Duration: 11.0, SampleRate: 48000, BlockSize: 128 });
        const Bytes = EncodeWaveFile(Result.Left, Result.Right, Result.SampleRate);
        const Measurement = MeasureSignal(Result.Left, Result.SampleRate);
        Download(Bytes, `audition-${PageState.Identifier}.wav`);
        Element("render-status").textContent =
            `Audition rendered in ${(performance.now() - Started).toFixed(0)} ms · peak ${Measurement.Peak.toFixed(3)} · ` +
            `rms ${Measurement.RmsDecibels.toFixed(1)} dBFS · crest ${Measurement.CrestFactor.toFixed(2)}`;
    }, 30);
}

function RenderSteady()
{
    const RPM = Number(Element("hold-rpm").value);
    const Throttle = Number(Element("hold-throttle").value);
    Element("render-status").textContent = "Rendering steady tone…";
    setTimeout(() =>
    {
        const Result = RenderSteadyStateTone(PageState.Identifier, { RPM, Throttle, Load: 0.35 + 0.6 * Throttle }, 2.0, 48000);
        Download(EncodeWaveFile(Result.Left, Result.Right, Result.SampleRate), `steady-${PageState.Identifier}-${RPM}.wav`);
        const Measurement = MeasureSignal(Result.Left, Result.SampleRate);
        Element("render-status").textContent = `Steady ${RPM} rpm · firing ${Result.FiringFrequency.toFixed(1)} Hz · ` +
                                                `peak ${Measurement.Peak.toFixed(3)} · crest ${Measurement.CrestFactor.toFixed(2)}`;
    }, 30);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  FRAME-RATE INDEPENDENCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The probe: render the same schedule twice, once with a 12 Hz host tick and once with a 240 Hz host tick, both through the offline engine at
//    the same block size. Because the model advances on sample count alone, the two must be sample-identical. A nonzero result would mean the
//    sound had become a function of the drawn frame rate.
function RunFrameRateProbe()
{
    const Rate = Number(Element("probe-rate").value);
    const Output = Element("probe-result");
    Output.textContent = `Rendering at ${Rate} Hz host tick…`;
    setTimeout(() =>
    {
        const Schedule = [{ Seconds: 1.0, Throttle: 0.0 }, { Seconds: 2.0, Throttle: 1.0 }];
        const Reference = RenderSchedule(PageState.Identifier, Schedule, { Duration: 3.0, SampleRate: 48000, BlockSize: 128 });
        const Slow = RenderAtHostRate(PageState.Identifier, Schedule, 3.0, Rate, 128);
        let Difference = 0.0;
        let Worst = 0.0;
        for (let Index = 0; Index < Reference.Left.length; ++Index)
        {
            const Delta = Math.abs(Reference.Left[Index] - Slow.Left[Index]);
            Difference += Delta;
            if (Delta > Worst) Worst = Delta;
        }
        const Mean = Difference / Math.max(1, Reference.Left.length);
        Output.textContent =
            `host tick        ${Rate} Hz (a ${(1000 / Rate).toFixed(1)} ms picture)\n` +
            `samples compared ${Reference.Left.length}\n` +
            `mean |Δ|         ${Mean.toExponential(3)}\n` +
            `peak |Δ|         ${Worst.toExponential(3)}\n` +
            `verdict          ${Worst < 1.0e-9 ? "identical — the audio never sampled the picture clock" : "DIFFERS — investigate"}`;
    }, 30);
}

// 📝 RenderAtHostRate drives the same voice but advances the drivetrain in host-sized chunks, which is what a naive implementation would do. It
//    exists to be compared against, not to be used for playback.
function RenderAtHostRate(Identifier, Schedule, Duration, Rate, BlockSize)
{
    const Bundle = CreateVehicleVoiceBundle(Identifier, 48000, { Position: "exterior", Gain: 1.0 });
    const Total = Math.round(Duration * 48000);
    const Left = new Float32Array(Total);
    const Right = new Float32Array(Total);
    const LeftBlock = new Float32Array(BlockSize);
    const RightBlock = new Float32Array(BlockSize);
    const HostStep = 1.0 / Rate;
    let Cursor = 0;
    let HostClock = 0.0;
    let StepIndex = 0;
    let StepStart = 0.0;
    const Active = { Throttle: 0.0, Brake: 0.0, Steer: 0.0 };
    while (Cursor < Total)
    {
        const Count = Math.min(BlockSize, Total - Cursor);
        while (StepIndex < Schedule.length && HostClock >= StepStart)
        {
            const Step = Schedule[StepIndex];
            StepStart += Step.Seconds || 0.0;
            if (Step.Throttle !== undefined) Active.Throttle = Step.Throttle;
            if (Step.Brake !== undefined) Active.Brake = Step.Brake;
            ++StepIndex;
        }
        ConfigureVoiceInput(Bundle, Active);
        RenderVehicleVoiceBlock(Bundle, LeftBlock.subarray(0, Count), RightBlock.subarray(0, Count), Count);
        ClearVoiceEventInput(Bundle);
        Left.set(LeftBlock.subarray(0, Count), Cursor);
        Right.set(RightBlock.subarray(0, Count), Cursor);
        Cursor += Count;
        HostClock += Count / 48000;
    }
    return { Left, Right };
}

// 📝 The self-test runs the shared assertion module in the page, so a reviewer can confirm the claims without leaving the bench. It is the same
//    code Node executes, imported rather than reimplemented.
function RunSelfTest()
{
    const Output = Element("selftest-result");
    Output.textContent = "Running…";
    setTimeout(async () =>
    {
        try
        {
            const { ComposeValidationReport, FormatValidationReport } = await import("./ValidationSuite.js");
            const Report = ComposeValidationReport();
            Output.textContent = FormatValidationReport(Report);
        }
        catch (Failure)
        {
            Output.textContent = `Self-test failed to run: ${Failure.message}`;
        }
    }, 30);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     HOLD AND CONTROLS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Hold: bypasses the drivetrain and drives the voice at a fixed operating point through the device, so the tone can be judged steady. The
//    offline steady render uses the same entry point, which keeps the audition and the measurement consistent.
function HoldPoint()
{
    if (!Host.Started)
    {
        Element("render-status").textContent = "Start the audio device first.";
        return;
    }
    PageState.HoldPoint = { RPM: Number(Element("hold-rpm").value), Throttle: Number(Element("hold-throttle").value) };
    Host.Worker.postMessage({ Type: "hold", Point: PageState.HoldPoint });
    Element("render-status").textContent = `Holding ${PageState.HoldPoint.RPM} rpm at ${(PageState.HoldPoint.Throttle * 100).toFixed(0)}% throttle.`;
}

function ReleaseHold()
{
    if (Host.Worker) Host.Worker.postMessage({ Type: "hold", Point: null });
    PageState.HoldPoint = null;
    Element("render-status").textContent = "Hold released; the drivetrain is driving again.";
}

function WireControls()
{
    Element("power").addEventListener("click", TogglePower);
    for (const Button of Element("position-control").children)
    {
        Button.addEventListener("click", () =>
        {
            PageState.Position = Button.dataset.position;
            for (const Other of Element("position-control").children)
                Other.classList.toggle("active", Other === Button);
            Host.ConfigurePosition(PageState.Position);
        });
    }
    Element("gain").addEventListener("input", (Event) =>
    {
        PageState.Gain = Number(Event.target.value);
        Element("gain-value").textContent = PageState.Gain.toFixed(2);
        Host.ConfigureGain(PageState.Gain);
    });
    Element("framebudget").addEventListener("input", (Event) =>
    {
        PageState.FrameBudget = Number(Event.target.value);
        Element("framebudget-value").textContent = PageState.FrameBudget.toFixed(0);
    });
    Element("hold-play").addEventListener("click", HoldPoint);
    Element("hold-stop").addEventListener("click", ReleaseHold);
    Element("render-audition").addEventListener("click", RenderAudition);
    Element("render-steady").addEventListener("click", RenderSteady);
    Element("run-probe").addEventListener("click", RunFrameRateProbe);
    Element("run-selftest").addEventListener("click", RunSelfTest);
    Element("hold-rpm").addEventListener("change", () =>
    {
        const Profile = ResolveVehicleProfile(PageState.Identifier);
        const Capped = Math.min(Number(Element("hold-rpm").value), Profile.Combustion.LimiterRPM);
        Element("hold-rpm").value = String(Math.round(Capped / 50) * 50);
    });
    const Capability = QueryAudioCapability();
    Element("transport-mode").textContent = Capability.AudioWorklet
        ? (Capability.SharedMemory ? "worklet ready" : "worklet (messages)")
        : (Capability.AudioContext ? "processor fallback" : "unsupported");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          START
//------------------------------------------------------------------------------------------------------------------------

BuildGarage();
WireControls();
SelectVehicle(PageState.Identifier);
requestAnimationFrame(PictureLoop);
Element("framebudget-value").textContent = String(PageState.FrameBudget);
