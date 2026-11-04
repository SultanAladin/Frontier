//============================================================================================================================================
//                                                          VEHICLEVOICE.JS
//============================================================================================================================================
// 🧩 The composite voice: drivetrain condition in, stereo block out. Owns the layer stack (exhaust, induction, mechanical), the listening
//    position acoustics (cabin, exterior, pass-by) and the output safety. This is the only file the audio host and the offline renderer call.

// 📝 Frame-rate independence, stated exactly: nothing here is parameterised by frame time. The host hands a block duration derived from the
//    SAMPLE COUNT, the drivetrain is advanced in fixed sub-steps no greater than 2.5 ms, and the engines integrate crank angle per audio
//    sample. A 10 fps picture, a 240 fps picture and an offline 48 kHz render with no picture at all produce the same waveform, because the
//    only clock involved is the sample clock.

// 📝 Layer gain budget. Measured with Validation/OfflineRender.mjs at wide-open throttle on the redline for every car; the constants below are
//    the normalisation that keeps peak below unity before the safety limiter, so the limiter is a guard and not the sound.
const ExhaustLayerGain        = 0.92;
const InductionLayerGain      = 0.80;
const MechanicalLayerGain     = 0.85;
const CabinLayerGain          = 1.00;
const ExteriorLayerGain       = 1.00;
const PassByLayerGain         = 0.55;
const OutputHeadroom          = 0.82;
const TailpipeDelaySamples    = 6;
const MasterSmoothingSeconds  = 0.050;

import { ResolveVehicleProfile, FindVehicle, ValidateVehicleProfile } from "./VehicleCatalogue.js";
import { CreateDrivetrainCondition, AdvanceDrivetrain, ResolveDrivetrainTelemetry, ResolveGearRatio,
         PrepareDrivetrainProfile } from "./VehicleDrivetrain.js";
import { CreateExhaustVoice, ConfigureExhaustVoice, RenderExhaustBlock, TriggerExhaustEvent } from "./ExhaustVoice.js";
import { CreateInductionVoice, RenderInductionBlock } from "./InductionVoice.js";
import { CreateMechanicalVoice, RenderMechanicalBlock, TriggerMechanicalEvent } from "./MechanicalVoice.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                     LISTENING POSITIONS
//------------------------------------------------------------------------------------------------------------------------

export const ListeningPositions = Object.freeze({
    Cabin:    "cabin",
    Exterior: "exterior",
    PassBy:   "passBy"
});

//------------------------------------------------------------------------------------------------------------------------
//                                                      FILTER PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

function CreateOnePole()
{
    return { Memory: 0.0, Low: 0.0 };
}

function ConfigureOnePole(Pole, CutoffHz, SampleRate)
{
    const Frequency = Math.min(Math.max(CutoffHz, 0.5), SampleRate * 0.47);
    Pole.Coefficient = 1.0 - Math.exp(-6.283185307179586 * Frequency / SampleRate);
    return Pole;
}

function EvaluateOnePole(Pole, Input)
{
    Pole.Memory += Pole.Coefficient * (Input - Pole.Memory);
    if (!Number.isFinite(Pole.Memory)) Pole.Memory = 0.0;
    return Pole.Memory;
}

function CreateResonator()
{
    return { IntegratorOne: 0.0, IntegratorTwo: 0.0, A1: 0.0, A2: 0.0, A3: 0.0, Damping: 1.0 };
}

function ConfigureResonator(Resonator, FrequencyHz, QualityFactor, SampleRate)
{
    const Frequency = Math.min(Math.max(FrequencyHz, 8.0), SampleRate * 0.46);
    const G = Math.tan(Math.PI * Frequency / SampleRate);
    const K = 1.0 / Math.max(0.3, QualityFactor);
    const A1 = 1.0 / (1.0 + G * (G + K));
    Resonator.A1 = A1;
    Resonator.A2 = G * A1;
    Resonator.A3 = G * Resonator.A2;
    Resonator.Damping = K;
    return Resonator;
}

function EvaluateResonator(Resonator, Input)
{
    const V3 = Input - Resonator.IntegratorTwo;
    const V1 = Resonator.A1 * Resonator.IntegratorOne + Resonator.A2 * V3;
    const V2 = Resonator.IntegratorTwo + Resonator.A2 * Resonator.IntegratorOne + Resonator.A3 * V3;
    Resonator.IntegratorOne = 2.0 * V1 - Resonator.IntegratorOne;
    Resonator.IntegratorTwo = 2.0 * V2 - Resonator.IntegratorTwo;
    if (!Number.isFinite(Resonator.IntegratorOne) || !Number.isFinite(Resonator.IntegratorTwo))
    {
        Resonator.IntegratorOne = 0.0;
        Resonator.IntegratorTwo = 0.0;
        return 0.0;
    }
    return V2;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       BUNDLE CREATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 A bundle is one car in one listening position at one sample rate. Rebuilding on a car change is cheap (milliseconds) and keeps the
//    render path free of branches on "which car am I". The shared physics condition survives a swap so the drivetrain keeps rolling.
export function CreateVehicleVoiceBundle(Identifier, SampleRate, Options = {})
{
    const Profile = PrepareDrivetrainProfile(ResolveVehicleProfile(Identifier));
    const Position = Options.Position || ListeningPositions.Exterior;
    const Bundle = {
        Identifier: Profile.Identifier,
        DisplayName: Profile.DisplayName,
        Profile,
        SampleRate,
        Position,
        Exhaust: CreateExhaustVoice(Profile, SampleRate),
        Induction: CreateInductionVoice(Profile, SampleRate),
        Mechanical: CreateMechanicalVoice(Profile, SampleRate),
        EnergyState: 0.86,
        ExhaustControl: { RPM: 0.0, Load: 0.0, Throttle: 0.0, BoostFraction: 0.0, Bypass: 0.0, Misfire: 0.0,
                          IgnitionCut: 0.0, OverrunFuel: 0.0, MassFlow: 0.0 },
        InductionControl: { RPM: 0.0, WheelRPM: 0.0, BoostFraction: 0.0, TurbineSpeed: 0.0, BlowerPressure: 0.0,
                            Load: 0.0, Throttle: 0.0, BlowOff: 0.0, MotorFraction: 0.0 },
        MechanicalControl: { Speed: 0.0, WheelRPM: 0.0, RPM: 0.0, Throttle: 0.0, TyreScrub: 0.0, WheelSlipRatio: 0.0,
                             Brake: 0.0, Steer: 0.0, Fan: 0.0, GearWhineLoad: 0.0, DifferentialLoad: 0.0,
                             DrivelineLash: 0.0, GearIndex: 0.0, Cabin: false },
        Stereo: { DelayLine: new Float32Array(TailpipeDelaySamples * 2), Cursor: 0, Delay: 2, LeftGain: 1.0, RightGain: 0.94 },
        CabinFilters: {
            Glass: [CreateOnePole(), CreateOnePole()], GlassSecond: [CreateOnePole(), CreateOnePole()],
            Boom: [CreateResonator(), CreateResonator()],
            ExteriorAir: [CreateOnePole(), CreateOnePole()],
            PassByAir: [CreateOnePole(), CreateOnePole()], PassBySecond: [CreateOnePole(), CreateOnePole()]
        },
        MasterGain: 1.0,
        TargetGain: Options.Gain !== undefined ? Options.Gain : 1.0,
        OutputGain: 1.0,
        GainCoefficient: 1.0 - Math.exp(-1.0 / (MasterSmoothingSeconds * SampleRate)),
        PeakObserved: 0.0,
        MeterLevel: 0.0,
        Layers: { ExhaustPeak: 0.0, InductionPeak: 0.0, MechanicalPeak: 0.0 },
        Scratch: { ExhaustLeft: new Float32Array(0), ExhaustRight: new Float32Array(0),
                   MixLeft: new Float32Array(0), MixRight: new Float32Array(0) },
        Condition: null,
        Input: { Throttle: 0.0, Brake: 0.0, Steer: 0.0, Handbrake: false, AutoShift: true, LaunchControl: false,
                 ShiftUp: false, ShiftDown: false, SelectReverse: false, SelectNeutral: false },
        Telemetry: null,
        Validation: ValidateVehicleProfile(Profile),
        BlockCount: 0
    };
    ConfigureVehicleVoiceBundle(Bundle, SampleRate, Position);
    Bundle.Condition = CreateDrivetrainCondition(Profile, Options.StartGear !== undefined ? Options.StartGear : 1);
    Bundle.Telemetry = ResolveDrivetrainTelemetry(Bundle.Condition, Profile);
    return Bundle;
}

export function ConfigureVehicleVoiceBundle(Bundle, SampleRate, Position)
{
    const Profile = Bundle.Profile;
    const Cabin = Profile.Cabin;
    Bundle.SampleRate = SampleRate;
    Bundle.Position = Position || Bundle.Position;
    ConfigureExhaustVoice(Bundle.Exhaust, SampleRate);

    // 📝 Cabin: glass and trim are a two-pole acoustic low-pass at the insulation knee with a first-order body boom added, not a volume
    //    knob. That is why a 9000 rpm V12 inside the car is a distant saw rather than a quiet version of the outside sound.
    for (let Channel = 0; Channel < 2; ++Channel)
    {
        ConfigureOnePole(Bundle.CabinFilters.Glass[Channel], Math.max(120.0, Cabin.InsulationFrequencyKnee * 1.55), SampleRate);
        ConfigureOnePole(Bundle.CabinFilters.GlassSecond[Channel], Math.max(90.0, Cabin.InsulationFrequencyKnee * 0.95), SampleRate);
        ConfigureResonator(Bundle.CabinFilters.Boom[Channel], Math.max(40.0, Cabin.CabinResonanceHz), 2.6, SampleRate);

        // 📝 Exterior: a measurement microphone beside the car still sits in air; a gentle roll-off above 12 kHz keeps digital fizz out.
        ConfigureOnePole(Bundle.CabinFilters.ExteriorAir[Channel], Math.min(15000.0, SampleRate * 0.40), SampleRate);

        // 📝 Pass-by: the car is 7-15 m away behind a barrier of air, so it loses the top two octaves and most of the level. Distance is
        //    applied by the host as a gain; this is the air path.
        ConfigureOnePole(Bundle.CabinFilters.PassByAir[Channel], 2400.0, SampleRate);
        ConfigureOnePole(Bundle.CabinFilters.PassBySecond[Channel], 5200.0, SampleRate);
    }
    CreateOnePole();

    Bundle.Stereo.Delay = Math.max(1, Math.min(TailpipeDelaySamples - 1, Math.round(SampleRate * 0.00007 * Profile.Exhaust.TailpipeLength * 10.0)));
    return Bundle;
}

// 📝 Swapping car keeps the physics: the player changes vehicle while rolling and the road speed, gear and wheel rotation carry over.
export function ConfigureVehicleSelection(Bundle, Identifier, SampleRate, Position)
{
    const PreviousCondition = Bundle.Condition;
    const PreviousTelemetry = Bundle.Telemetry;
    const Replacement = CreateVehicleVoiceBundle(Identifier, SampleRate, { Position, Gain: Bundle.TargetGain });
    if (PreviousCondition)
    {
        PreviousCondition.Speed = Math.max(0.0, PreviousCondition.Speed);
        Replacement.Condition.Speed = PreviousCondition.Speed;
        Replacement.Condition.Distance = PreviousCondition.Distance;
        Replacement.Condition.WheelSpeed = PreviousCondition.WheelSpeed;
        Replacement.Condition.GearIndex = Math.max(1, Math.min(Replacement.Profile.Drivetrain.GearRatios.length,
                                                          PreviousCondition.GearIndex));
        Replacement.Condition.EngineAngularSpeed = PreviousCondition.EngineAngularSpeed;
        Replacement.Condition.ThrottlePlate = PreviousCondition.ThrottlePlate;
        Replacement.Condition.BrakePedal = PreviousCondition.BrakePedal;
        Replacement.Condition.SteerAngle = PreviousCondition.SteerAngle;
    }
    Replacement.Telemetry = PreviousTelemetry || Replacement.Telemetry;
    return Replacement;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                TELEMETRY TO VOICE CONTROLS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The single mapping from physical state to voice control. Everything here is a derived physical quantity, never a mix decision:
//    the bypass valve angle is an rpm window, the misfire depth is a cut fraction, blow-off is a pressure-dump trigger.
export function ComposeVoiceControls(Bundle, Telemetry)
{
    const Profile = Bundle.Profile;
    const E = Profile.Exhaust;
    const D = Profile.Drivetrain;
    const C = Profile.Combustion;

    // 📝 Exhaust bypass: the valve opens across its authored rpm window; it also cracks open on a fast throttle lift, which is why an
    //    overrun pop arrives with the throttle, not with the rpm.
    const OpenWindow = Math.max(1.0, E.BypassFullRPM - E.BypassOpenRPM);
    const BypassFromSpeed = Math.max(0.0, Math.min(1.0, (Telemetry.EngineRPM - E.BypassOpenRPM) / OpenWindow));
    const LiftCrack = Math.max(0.0, Math.min(0.35, (1.0 - Telemetry.Throttle) * Telemetry.OverrunFuelFraction * 0.5));
    const Bypass = Math.max(BypassFromSpeed, LiftCrack);

    // 📝 Ignition cut: a limiter cuts hard; a gearshift torque cut is as audible as the shift is fast. A 40 ms dual-clutch cut is a bang;
    //    a 420 ms manual clutch-to-clutch cut is a soft pause, which is exactly the difference the player hears between the GT3 and the
    //    Mustang.
    const ShiftSharpness = D.ShiftSeconds > 0.0 ? Math.max(0.0, Math.min(1.0, 0.075 / D.ShiftSeconds)) : 0.0;
    const GearCut = Telemetry.ShiftCut > 0.0 ? Telemetry.ShiftCut * ShiftSharpness : 0.0;
    const IgnitionCut = Math.max(Telemetry.LimiterCut * 0.85, GearCut);

    Bundle.ExhaustControl.RPM = Telemetry.EngineRPM;
    Bundle.ExhaustControl.Load = Telemetry.EngineLoad;
    Bundle.ExhaustControl.Throttle = Telemetry.Throttle;
    Bundle.ExhaustControl.BoostFraction = Telemetry.BoostFraction;
    Bundle.ExhaustControl.Bypass = Profile.Electric ? 0.0 : Bypass;
    Bundle.ExhaustControl.Misfire = Profile.Electric ? 0.0 : GearCut * 0.55;
    Bundle.ExhaustControl.IgnitionCut = Profile.Electric ? 0.0 : IgnitionCut;
    Bundle.ExhaustControl.OverrunFuel = Profile.Electric ? 0.0 : Telemetry.OverrunFuelFraction;
    Bundle.ExhaustControl.MassFlow = Telemetry.ExhaustMassFlow;

    // 📝 Electric drive fraction: torque over available torque at the present speed, which is what an inverter current maps to.
    const AvailableTorque = Math.max(1.0, C.PeakTorqueNewtonMetres);
    const MotorFraction = Profile.Electric
        ? Math.max(0.0, Math.min(1.3, Math.abs(Telemetry.Throttle) * 0.85 + Math.abs(Telemetry.LongitudinalAcceleration) * 0.02))
        : 0.0;

    Bundle.InductionControl.RPM = Telemetry.EngineRPM;
    Bundle.InductionControl.WheelRPM = Telemetry.WheelRPM;
    Bundle.InductionControl.BoostFraction = Telemetry.BoostFraction;
    Bundle.InductionControl.TurbineSpeed = Telemetry.TurbineSpeed;
    Bundle.InductionControl.BlowerPressure = Telemetry.BlowerPressure;
    Bundle.InductionControl.Load = Telemetry.EngineLoad;
    Bundle.InductionControl.Throttle = Telemetry.Throttle;
    Bundle.InductionControl.BlowOff = Telemetry.BlowOffStrength;
    Bundle.InductionControl.MotorFraction = MotorFraction;
    void AvailableTorque;

    Bundle.MechanicalControl.Speed = Math.abs(Telemetry.SpeedMetresPerSecond);
    Bundle.MechanicalControl.WheelRPM = Telemetry.WheelRPM;
    Bundle.MechanicalControl.RPM = Telemetry.EngineRPM;
    Bundle.MechanicalControl.Throttle = Telemetry.Throttle;
    Bundle.MechanicalControl.TyreScrub = Telemetry.TyreScrub;
    Bundle.MechanicalControl.WheelSlipRatio = Telemetry.WheelSlipRatio;
    Bundle.MechanicalControl.Brake = Telemetry.Brake;
    Bundle.MechanicalControl.Steer = Telemetry.Steer;
    Bundle.MechanicalControl.Fan = Telemetry.CoolingFanSpeed;
    Bundle.MechanicalControl.GearWhineLoad = Telemetry.GearWhineLoad;
    Bundle.MechanicalControl.DifferentialLoad = Telemetry.DifferentialLoad;
    Bundle.MechanicalControl.DrivelineLash = Telemetry.DrivelineLash;
    Bundle.MechanicalControl.GearIndex = Telemetry.GearIndex;
    Bundle.MechanicalControl.Cabin = Bundle.Position === ListeningPositions.Cabin;
    return Bundle;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDER CALL
//------------------------------------------------------------------------------------------------------------------------

// 📝 RenderVehicleVoiceBlock is what the AudioWorklet calls. It advances the drivetrain by the block's duration, maps the resulting
//    telemetry into the three voices, sums their stereo output, applies the listening position and returns. Left/Right are overwritten.
export function RenderVehicleVoiceBlock(Bundle, Left, Right, SampleCount)
{
    if (SampleCount > Bundle.Scratch.ExhaustLeft.length)
    {
        Bundle.Scratch.ExhaustLeft = new Float32Array(SampleCount);
        Bundle.Scratch.ExhaustRight = new Float32Array(SampleCount);
    }
    const ExhaustLeft = Bundle.Scratch.ExhaustLeft;
    const ExhaustRight = Bundle.Scratch.ExhaustRight;
    const LocalLeft = Bundle.Scratch.MixLeft && Bundle.Scratch.MixLeft.length >= SampleCount ? Bundle.Scratch.MixLeft
                                                                                             : (Bundle.Scratch.MixLeft = new Float32Array(SampleCount));
    const LocalRight = Bundle.Scratch.MixRight && Bundle.Scratch.MixRight.length >= SampleCount ? Bundle.Scratch.MixRight
                                                                                               : (Bundle.Scratch.MixRight = new Float32Array(SampleCount));

    const BlockSeconds = SampleCount / Bundle.SampleRate;
    AdvanceDrivetrain(Bundle.Condition, Bundle.Profile, Bundle.Input, BlockSeconds);
    Bundle.Telemetry = ResolveDrivetrainTelemetry(Bundle.Condition, Bundle.Profile);
    ComposeVoiceControls(Bundle, Bundle.Telemetry);

    const Telemetry = Bundle.Telemetry;
    const ExhaustControl = Bundle.ExhaustControl;
    ExhaustControl.Misfire = Math.max(ExhaustControl.Misfire, ExhaustControl.IgnitionCut * 0.4);

    for (let Index = 0; Index < SampleCount; ++Index)
    {
        ExhaustLeft[Index] = 0.0;
        ExhaustRight[Index] = 0.0;
        LocalLeft[Index] = 0.0;
        LocalRight[Index] = 0.0;
    }

    RenderExhaustBlock(Bundle.Exhaust, Bundle.ExhaustControl, ExhaustLeft, ExhaustRight, SampleCount);
    RenderInductionBlock(Bundle.Induction, Bundle.InductionControl, LocalLeft, LocalRight, SampleCount);
    RenderMechanicalBlock(Bundle.Mechanical, Bundle.MechanicalControl, LocalLeft, LocalRight, SampleCount);

    // 📝 Exhaust stereo: two tailpipes of unequal length, so one channel lags by a handful of samples and carries slightly less level.
    //    On a single-outlet car both channels are the same pipe, so the delay collapses to zero and the image is centred.
    const TwinOutlet = Bundle.Profile.Firing.BankCount >= 2;
    const Delay = TwinOutlet ? Bundle.Stereo.Delay : 0;
    const DelayLine = Bundle.Stereo.DelayLine;
    let Cursor = Bundle.Stereo.Cursor;
    const LeftGain = TwinOutlet ? Bundle.Stereo.LeftGain : 1.0;
    const RightGain = TwinOutlet ? Bundle.Stereo.RightGain : 1.0;

    for (let Index = 0; Index < SampleCount; ++Index)
    {
        const Delayed = DelayLine[Cursor * 2];
        DelayLine[Cursor * 2] = ExhaustLeft[Index];
        const DelayRead = Delay > 0 ? Delayed : ExhaustLeft[Index];
        Cursor = (Cursor + 1) % Math.max(1, DelayLine.length / 2);

        ExhaustLeft[Index] = (ExhaustLeft[Index] * LeftGain + DelayRead * 0.35) * ExhaustLayerGain;
        ExhaustRight[Index] = (DelayRead * RightGain) * ExhaustLayerGain;
    }
    Bundle.Stereo.Cursor = Cursor;

    const Position = Bundle.Position;
    const Cabin = Position === ListeningPositions.Cabin;
    const PassBy = Position === ListeningPositions.PassBy;
    const CabinGain = Math.pow(10.0, -Bundle.Profile.Cabin.SoundInsulationDecibels / 20.0);
    const CabinMakeup = Math.min(14.0, 1.0 / Math.max(0.02, CabinGain * 1.9)) * 0.85;
    // 📝 An electric car has no exhaust to carry the level, so its tyre, wind and drive layers are mixed up to the level a combustion car's
    //    exhaust occupies. This is a mix-bus decision, not a physical claim: the layers themselves remain at their measured proportions.
    const InductionMakeup = Bundle.Profile.Electric ? 3.4 : 1.0;
    const MechanicalMakeup = Bundle.Profile.Electric ? 2.6 : 1.0;
    const GlassLeft = Bundle.CabinFilters.Glass[0], GlassSecondLeft = Bundle.CabinFilters.GlassSecond[0];
    const BoomLeft = Bundle.CabinFilters.Boom[0];
    const GlassRight = Bundle.CabinFilters.Glass[1], GlassSecondRight = Bundle.CabinFilters.GlassSecond[1];
    const BoomRight = Bundle.CabinFilters.Boom[1];
    const AirLeft = Bundle.CabinFilters.ExteriorAir[0], AirRight = Bundle.CabinFilters.ExteriorAir[1];
    const PassLeft = Bundle.CabinFilters.PassByAir[0], PassSecondLeft = Bundle.CabinFilters.PassBySecond[0];
    const PassRight = Bundle.CabinFilters.PassByAir[1], PassSecondRight = Bundle.CabinFilters.PassBySecond[1];

    for (let Index = 0; Index < SampleCount; ++Index)
    {
        // 📝 Layer stack. Exhaust is the wide left/right pair; induction and mechanical are narrower and arrive pre-summed.
        const SecondaryLeft = LocalLeft[Index];
        const SecondaryRight = LocalRight[Index];
        let Sample = ExhaustLeft[Index] + SecondaryLeft * InductionLayerGain * InductionMakeup * 0.90 +
                     SecondaryRight * InductionLayerGain * InductionMakeup * 0.10;
        let Mirror = ExhaustRight[Index] + SecondaryRight * InductionLayerGain * InductionMakeup * 0.90 +
                     SecondaryLeft * InductionLayerGain * InductionMakeup * 0.10;
        Sample += SecondaryLeft * MechanicalLayerGain * MechanicalMakeup * 0.85 +
                  SecondaryRight * MechanicalLayerGain * MechanicalMakeup * 0.15;
        Mirror += SecondaryRight * MechanicalLayerGain * MechanicalMakeup * 0.85 +
                  SecondaryLeft * MechanicalLayerGain * MechanicalMakeup * 0.15;

        if (Cabin)
        {
            const Glass = EvaluateOnePole(GlassLeft, Sample);
            const GlassTwo = EvaluateOnePole(GlassSecondLeft, Glass);
            const Boom = EvaluateResonator(BoomLeft, GlassTwo) * 0.16;
            Sample = (GlassTwo + Boom) * CabinMakeup * CabinLayerGain;
            const GlassR = EvaluateOnePole(GlassRight, Mirror);
            const GlassTwoR = EvaluateOnePole(GlassSecondRight, GlassR);
            const BoomR = EvaluateResonator(BoomRight, GlassTwoR) * 0.16;
            Mirror = (GlassTwoR + BoomR) * CabinMakeup * CabinLayerGain;
        }
        else if (PassBy)
        {
            const Air = EvaluateOnePole(PassLeft, Sample);
            Sample = EvaluateOnePole(PassSecondLeft, Air) * PassByLayerGain;
            const AirR = EvaluateOnePole(PassRight, Mirror);
            Mirror = EvaluateOnePole(PassSecondRight, AirR) * PassByLayerGain;
        }
        else
        {
            Sample = EvaluateOnePole(AirLeft, Sample) * ExteriorLayerGain;
            Mirror = EvaluateOnePole(AirRight, Mirror) * ExteriorLayerGain;
        }

        Bundle.OutputGain += (Bundle.TargetGain - Bundle.OutputGain) * Bundle.GainCoefficient;
        Sample *= Bundle.OutputGain * OutputHeadroom;
        Mirror *= Bundle.OutputGain * OutputHeadroom;

        // 📝 Safety: a hard limiter would audibly crush a V12 at the limiter, so the curve is a soft knee that is transparent below 0.7
        //    and asymptotic to 1.0 above it. The offline renders assert the peak never exceeds 0.999.
        const ShapedLeft = SoftLimit(Sample);
        const ShapedRight = SoftLimit(Mirror);
        Left[Index] = ShapedLeft;
        Right[Index] = ShapedRight;

        const Magnitude = Math.abs(ShapedLeft) > Math.abs(ShapedRight) ? Math.abs(ShapedLeft) : Math.abs(ShapedRight);
        if (Magnitude > Bundle.PeakObserved) Bundle.PeakObserved = Magnitude;
        Bundle.MeterLevel += (Magnitude - Bundle.MeterLevel) * 0.02;
    }

    Bundle.Layers.ExhaustPeak = Bundle.Exhaust.PeakEstimate;
    Bundle.Layers.InductionPeak = 0.0;
    Bundle.BlockCount += 1;
    return Bundle;
}

// 📝 tanh knee with unity slope at the origin, scaled so the asymptote is 1.0. Cheap, monotone, no overshoot, no lookahead latency.
function SoftLimit(Argument)
{
    if (Argument > 4.0) return 0.9999;
    if (Argument < -4.0) return -0.9999;
    return Math.tanh(Argument) * 0.9995;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INPUT AND ENERGY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Event inputs are latched by the host and cleared here after one render, so a keypress shorter than a block still lands.
export function ConfigureVoiceInput(Bundle, Input)
{
    const Target = Bundle.Input;
    Target.Throttle = Math.max(0.0, Math.min(1.0, Input.Throttle || 0.0));
    Target.Brake = Math.max(0.0, Math.min(1.0, Input.Brake || 0.0));
    Target.Steer = Math.max(-1.0, Math.min(1.0, Input.Steer || 0.0));
    Target.Handbrake = !!Input.Handbrake;
    Target.AutoShift = Input.AutoShift === undefined ? Target.AutoShift : !!Input.AutoShift;
    Target.LaunchControl = !!Input.LaunchControl;
    Target.ShiftUp = !!Input.ShiftUp || Target.ShiftUp;
    Target.ShiftDown = !!Input.ShiftDown || Target.ShiftDown;
    Target.SelectReverse = !!Input.SelectReverse || Target.SelectReverse;
    Target.SelectNeutral = !!Input.SelectNeutral || Target.SelectNeutral;
    return Bundle;
}

export function ClearVoiceEventInput(Bundle)
{
    Bundle.Input.ShiftUp = false;
    Bundle.Input.ShiftDown = false;
    Bundle.Input.SelectReverse = false;
    Bundle.Input.SelectNeutral = false;
    return Bundle;
}

export function ResetVehicleVoiceBundle(Bundle)
{
    Bundle.Condition = CreateDrivetrainCondition(Bundle.Profile, 1);
    Bundle.Telemetry = ResolveDrivetrainTelemetry(Bundle.Condition, Bundle.Profile);
    Bundle.PeakObserved = 0.0;
    Bundle.MeterLevel = 0.0;
    Bundle.BlockCount = 0;
    return Bundle;
}

// 📝 Battery state of charge for the electric car: consumed from traction work, recovered from regenerative braking. Exposed because a
//    Taycan that never runs out is not a simulation, and the regen whine depends on it.
export function AdvanceEnergyStore(Bundle, DeltaSeconds)
{
    const Condition = Bundle.Condition;
    if (!Bundle.Profile.Electric)
        return 1.0;
    const CapacityJoules = 93.4 * 3.6e6 * 0.94;         // [J] - 93.4 kWh usable, 94% efficiency round trip
    const Force = Bundle.Profile.Chassis.MassKilograms * Condition.LongitudinalAcceleration;
    const Power = Force * Condition.Speed;
    Bundle.EnergyState = Bundle.EnergyState === undefined ? 0.86 : Bundle.EnergyState;
    Bundle.EnergyState = Math.max(0.02, Math.min(1.0, Bundle.EnergyState - Power * DeltaSeconds / CapacityJoules));
    return Bundle.EnergyState;
}

// 📝 Frequency ceiling report: the highest firing order the device can represent for this car at this rpm. The UI shows it, and the
//    validation asserts no engine is asked to reproduce an alias. It is a query, not a limiter — clamping would change timbre.
export function QueryFrequencyCeiling(Bundle)
{
    const Profile = Bundle.Profile;
    const Nyquist = Bundle.SampleRate * 0.5;
    const CylinderFactor = Math.max(1, Profile.Combustion.CylinderCount) / Math.max(1.0, Profile.Combustion.CycleRevolutions || 2.0);
    const FiringAtRedline = Profile.Combustion.RedlineRPM / 60.0 * CylinderFactor;
    return Object.freeze({
        NyquistHz: Nyquist,
        FiringAtRedlineHz: FiringAtRedline,
        HighestRepresentableOrder: Nyquist / Math.max(1.0, FiringAtRedline),
        AudibleOrders: FiringAtRedline * 6.0 < Nyquist ? 6 : Math.max(1, Math.floor(Nyquist / Math.max(1.0, FiringAtRedline)))
    });
}

// 📝 A short text block for the inspector and for the offline reports: the numbers a reviewer needs to judge whether the model is doing
//    what it claims, without reading the source.
export function ComposeVoiceReport(Bundle)
{
    const Profile = Bundle.Profile;
    const T = Bundle.Telemetry;
    const Lines = [];
    Lines.push(`${Profile.DisplayName} — ${Profile.Combustion.CylinderCount > 0 ? Profile.Combustion.CylinderCount + " cylinder " +
               Profile.Combustion.CrankLayout : "electric drive"}`);
    Lines.push(`sample rate ${Bundle.SampleRate} Hz · position ${Bundle.Position} · bundles ${Bundle.BlockCount}`);
    Lines.push(`firing ${Profile.Firing.EventCount} events/cycle · cycle ${Profile.Firing.CycleRevolutions.toFixed(1)} rev · ` +
               `even ${Profile.Firing.EvenFiring ? "yes" : "no"}`);
    Lines.push(`gas sound speed ${Profile.GasSoundSpeed.toFixed(1)} m/s · exhaust modes ` +
               Profile.ExhaustModes.map((Value) => Value.toFixed(0)).join(" / ") + " Hz");
    Lines.push(`rpm ${T.EngineRPM.toFixed(0)} · ${T.SpeedKilometresPerHour.toFixed(1)} km/h · gear ${T.GearIndex} · ` +
               `boost ${T.BoostBar.toFixed(2)} bar`);
    Lines.push(`firing rate ${T.FiringFrequencyHz.toFixed(1)} Hz · slip ${T.WheelSlipRatio.toFixed(2)} · ` +
               `scrub ${T.TyreScrub.toFixed(2)} · gear whine load ${T.GearWhineLoad.toFixed(2)}`);
    Lines.push(`peak ${Bundle.PeakObserved.toFixed(4)} · meter ${Bundle.MeterLevel.toFixed(4)}`);
    if (Bundle.Validation.Findings.length > 0)
        Lines.push(`validation findings: ${Bundle.Validation.Findings.join("; ")}`);
    return Lines.join("\n");
}

export { FindVehicle, ResolveGearRatio };
