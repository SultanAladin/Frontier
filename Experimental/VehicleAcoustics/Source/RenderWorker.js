//============================================================================================================================================
//                                                        RENDERWORKER.JS
//============================================================================================================================================
// 🧩 The audio render thread. Owns the vehicle model, advances it in fixed 128-sample blocks, writes finished stereo into the shared ring, and
//    reports telemetry back for the picture and the panels. It never reads the frame loop and the frame loop never blocks on it.

// 📝 Threading contract:
//    · This worker is the only writer of the sample ring and the only owner of the synthesiser state.
//    · The main thread is the only writer of the control block (car, pedals, position). Control arrives as messages, so the audio cannot
//      be stalled by a slow message — the worker keeps rendering with the last known pedals if the page freezes.
//    · Telemetry leaves as a postMessage every 16 blocks (~21 ms at 48 kHz). A 12 Hz picture therefore reads telemetry at the same rate it
//      draws, while the audio keeps running at the full sample rate regardless.

import { CreateVehicleVoiceBundle, RenderVehicleVoiceBlock, ConfigureVoiceInput, ClearVoiceEventInput,
         ConfigureVehicleSelection, ConfigureVehicleVoiceBundle, ComposeVoiceControls, ComposeVoiceReport,
         AdvanceEnergyStore, QueryFrequencyCeiling } from "./VehicleVoice.js";
import { CreateExhaustVoice, RenderExhaustBlock } from "./ExhaustVoice.js";
import { RenderInductionBlock } from "./InductionVoice.js";
import { RenderMechanicalBlock } from "./MechanicalVoice.js";
import { ResolveDrivetrainTelemetry, CreateDrivetrainCondition } from "./VehicleDrivetrain.js";
import { WriteRingBlock, QueryRingFill, RingLayout } from "./AudioKernel.js";

const RenderBlock = 128;
const TelemetryInterval = 16;
const TargetFillBlocks = 6;
const MaximumBlocksPerTick = 48;

let Bundle = null;
let Control = null;
let SamplePlane = null;
let Capacity = RingLayout.SampleCapacity;
let LeftBlock = new Float32Array(RenderBlock);
let RightBlock = new Float32Array(RenderBlock);
let Running = false;
let BlockCounter = 0;
let SampleRate = 48000;
let Input = { Throttle: 0.0, Brake: 0.0, Steer: 0.0, Handbrake: false, AutoShift: true,
              ShiftUp: false, ShiftDown: false, SelectReverse: false, SelectNeutral: false };
let Timer = null;
let MessageTransport = false;
let HoldPoint = null;

function PostTelemetry(Force = false)
{
    if (!Bundle) return;
    if (!Force && BlockCounter % TelemetryInterval !== 0) return;
    const T = Bundle.Telemetry;
    self.postMessage({
        Type: "telemetry",
        SampleRate,
        Fill: Control ? QueryRingFill(Control, Capacity) : 0,
        Underrun: Control ? Atomics.load(Control, RingLayout.UnderrunWord) : 0,
        Overrun: Control ? Atomics.load(Control, RingLayout.OverrunWord) : 0,
        Blocks: BlockCounter,
        Peak: Bundle.PeakObserved,
        Meter: Bundle.MeterLevel,
        Energy: Bundle.EnergyState,
        Vehicle: { Identifier: Bundle.Identifier, Name: Bundle.DisplayName, Position: Bundle.Position,
                   Cylinders: Bundle.Profile.Combustion.CylinderCount, Layout: Bundle.Profile.Combustion.CrankLayout,
                   Firing: Bundle.Profile.Firing.EventCount, Even: Bundle.Profile.Firing.EvenFiring,
                   GasSpeed: Bundle.Profile.GasSoundSpeed, Modes: Bundle.Profile.ExhaustModes,
                   Induction: Bundle.Profile.Induction.Topology, GearCount: Bundle.Profile.GearCount,
                   Electric: Bundle.Profile.Electric, Redline: Bundle.Profile.Combustion.RedlineRPM },
        State: {
            RPM: T.EngineRPM, Speed: T.SpeedKilometresPerHour, Gear: T.GearIndex, Throttle: T.Throttle,
            Brake: T.Brake, Boost: T.BoostBar, BoostFraction: T.BoostFraction, Turbine: T.TurbineSpeed,
            Blower: T.BlowerPressure, BlowOff: T.BlowOffStrength, Slip: T.WheelSlipRatio, Scrub: T.TyreScrub,
            Lash: T.DrivelineLash, Fan: T.CoolingFanSpeed, Bypass: Bundle.ExhaustControl.Bypass,
            Misfire: Bundle.ExhaustControl.Misfire, Overrun: T.OverrunFuelFraction, Limiter: T.LimiterCut,
            WheelRPM: T.WheelRPM, FiringHz: T.FiringFrequencyHz, Clutch: T.ClutchEngagement,
            ShiftCut: T.ShiftCut, Lateral: T.LateralAcceleration, Longitudinal: T.LongitudinalAcceleration,
            Distance: T.Distance, Lockup: T.LockupState, TyreTemp: T.TyreTemperature
        }
    });
}

// 📝 One render tick: fill to the target queue depth, then yield. Filling in this worker rather than inside the device callback means a slow
//    block cannot cause a click, and the queue depth absorbs any scheduling hiccup from the picture thread.
function RenderTick()
{
    if (!Running || !Bundle) return;
    let Guard = 0;
    const TargetSamples = TargetFillBlocks * RenderBlock;
    while (Control && QueryRingFill(Control, Capacity) < TargetSamples && Guard < MaximumBlocksPerTick)
    {
        ++Guard;
        if (HoldPoint)
        {
            RenderHeldPoint();
        }
        else
        {
            ConfigureVoiceInput(Bundle, Input);
            RenderVehicleVoiceBlock(Bundle, LeftBlock, RightBlock, RenderBlock);
            ClearVoiceEventInput(Bundle);
            Input.ShiftUp = false;
            Input.ShiftDown = false;
            Input.SelectReverse = false;
            Input.SelectNeutral = false;
        }
        if (MessageTransport)
            self.postMessage({ Type: "audio", Left: LeftBlock.slice(0), Right: RightBlock.slice(0), Count: RenderBlock });
        else
            WriteRingBlock(Control, SamplePlane, Capacity, LeftBlock, RightBlock, RenderBlock);
        AdvanceEnergyStore(Bundle, RenderBlock / SampleRate);
        ++BlockCounter;
        PostTelemetry();
    }
}

// 📝 A held operating point: the synthesiser is driven at a fixed rpm and load while the drivetrain is paused. Used to judge a car's timbre
//    without the smear of a sweeping rev, and it is the same path the offline steady render uses.
function RenderHeldPoint()
{
    const Condition = Bundle.Condition;
    Condition.EngineAngularSpeed = Math.max(0.0, HoldPoint.RPM) * Math.PI / 30.0;
    Condition.ThrottlePlate = HoldPoint.Throttle;
    Condition.EngineLoad = 0.3 + 0.65 * HoldPoint.Throttle;
    Condition.ExhaustMassFlow = Condition.EngineLoad;
    Condition.GearWhineLoad = HoldPoint.Throttle;
    Condition.Boost = Bundle.Profile.Induction.WastegateMaxBoostBar * HoldPoint.Throttle;
    Condition.BlowerPressure = Bundle.Profile.Induction.Topology === "rootsSupercharger" ? HoldPoint.Throttle : 0.0;
    Condition.TurbineSpeed = Bundle.Profile.Induction.Topology === "twinScrollTurbo" ? HoldPoint.Throttle : 0.0;
    Condition.WheelSpeed = (HoldPoint.RPM * Math.PI / 30.0) /
                           Math.max(0.1, Bundle.Profile.Drivetrain.GearRatios[0] * Bundle.Profile.Drivetrain.FinalDriveRatio);
    Condition.Speed = Condition.WheelSpeed * Bundle.Profile.Chassis.WheelRadiusMetres;
    Bundle.Telemetry = ResolveDrivetrainTelemetry(Condition, Bundle.Profile);
    ComposeVoiceControls(Bundle, Bundle.Telemetry);
    LeftBlock.fill(0.0);
    RightBlock.fill(0.0);
    RenderExhaustBlock(Bundle.Exhaust, Bundle.ExhaustControl, LeftBlock, RightBlock, RenderBlock);
    RenderInductionBlock(Bundle.Induction, Bundle.InductionControl, LeftBlock, RightBlock, RenderBlock);
    RenderMechanicalBlock(Bundle.Mechanical, Bundle.MechanicalControl, LeftBlock, RightBlock, RenderBlock);
    const Trim = Math.tanh(Bundle.TargetGain);
    for (let Index = 0; Index < RenderBlock; ++Index)
    {
        LeftBlock[Index] = Math.tanh(LeftBlock[Index] * 1.15) * 0.94;
        RightBlock[Index] = Math.tanh(RightBlock[Index] * 1.15) * 0.94;
    }
    void Trim;
}

function StartTicker()
{
    if (Timer !== null) return;
    // 📝 A worker timer keeps the producer ahead of the device; the device pulls at its own rate. 8 ms is well under the device quantum.
    Timer = setInterval(RenderTick, 8);
}

function StopTicker()
{
    if (Timer !== null) clearInterval(Timer);
    Timer = null;
}

self.onmessage = (Event) =>
{
    const Message = Event.data;
    if (!Message) return;
    switch (Message.Type)
    {
        case "configure":
        {
            SampleRate = Message.SampleRate || 48000;
            Capacity = Message.Capacity || RingLayout.SampleCapacity;
            if (Message.ControlBuffer) Control = new Int32Array(Message.ControlBuffer);
            if (Message.SampleBuffer && Message.SampleBuffer.byteLength >= 8) SamplePlane = new Float32Array(Message.SampleBuffer);
            MessageTransport = Message.MessageMode === true || !Control;
            Bundle = CreateVehicleVoiceBundle(Message.Identifier || "laFerrari", SampleRate,
                                              { Position: Message.Position || "exterior", Gain: Message.Gain || 1.0 });
            Input.AutoShift = Message.AutoShift !== false;
            BlockCounter = 0;
            PostTelemetry(true);
            self.postMessage({ Type: "configured", SampleRate, Report: ComposeVoiceReport(Bundle),
                               Ceiling: QueryFrequencyCeiling(Bundle) });
            StartTicker();
            break;
        }
        case "select":
        {
            if (!Bundle) break;
            Bundle = ConfigureVehicleSelection(Bundle, Message.Identifier, SampleRate, Message.Position || Bundle.Position);
            PostTelemetry(true);
            self.postMessage({ Type: "configured", SampleRate, Report: ComposeVoiceReport(Bundle),
                               Ceiling: QueryFrequencyCeiling(Bundle) });
            break;
        }
        case "position":
        {
            if (!Bundle) break;
            ConfigureVehicleVoiceBundle(Bundle, SampleRate, Message.Position);
            break;
        }
        case "input":
        {
            const Source = Message.Input || {};
            Input.Throttle = Source.Throttle === undefined ? Input.Throttle : Source.Throttle;
            Input.Brake = Source.Brake === undefined ? Input.Brake : Source.Brake;
            Input.Steer = Source.Steer === undefined ? Input.Steer : Source.Steer;
            Input.Handbrake = Source.Handbrake === undefined ? Input.Handbrake : !!Source.Handbrake;
            Input.AutoShift = Source.AutoShift === undefined ? Input.AutoShift : !!Source.AutoShift;
            Input.LaunchControl = !!Source.LaunchControl;
            if (Source.ShiftUp) Input.ShiftUp = true;
            if (Source.ShiftDown) Input.ShiftDown = true;
            if (Source.SelectReverse) Input.SelectReverse = true;
            if (Source.SelectNeutral) Input.SelectNeutral = true;
            break;
        }
        case "gain":
        {
            if (Bundle) Bundle.TargetGain = Math.max(0.0, Math.min(2.0, Message.Gain));
            break;
        }
        case "hold": HoldPoint = Message.Point || null; break;
        case "start": Running = true; StartTicker(); break;
        case "stop": Running = false; StopTicker(); break;
        case "reset":
        {
            if (!Bundle) break;
            Bundle.Condition.Speed = 0.0;
            Bundle.Condition.WheelSpeed = 0.0;
            Bundle.Condition.Distance = 0.0;
            Bundle.Condition.GearIndex = 1;
            Bundle.Condition.EngineAngularSpeed = Bundle.Profile.Combustion.IdleRPM * Math.PI / 30.0;
            Bundle.PeakObserved = 0.0;
            Bundle.MeterLevel = 0.0;
            BlockCounter = 0;
            break;
        }
        default: break;
    }
};
