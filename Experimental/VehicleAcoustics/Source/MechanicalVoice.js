//============================================================================================================================================
//                                                         MECHANICALVOICE.JS
//============================================================================================================================================
// 🧩 The secondary layer: gear and differential mesh whine, tyre scrub and squeal, wind buffeting, brake judder, driveline lash clunk,
//    injector tick, fuel pump, cooling fan and road noise. These are the sounds that tell a player the car is real when the engine is quiet,
//    and about half the perceived realism of a drivetrain at part throttle.

// 📝 Every mechanism is speed-locked to a real rotating member: a gear whine is tooth count × shaft revolutions, a tyre squeal is a stick-slip
//    limit cycle whose frequency rises with slip velocity, wind is stagnation pressure on a real drag area. None of it is a looped sample,
//    so nothing here stops when the frame rate drops — the renderer requests samples and these oscillators advance per sample.

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const TwoPi                    = 6.283185307179586;
const AirDensity               = 1.225;      // [kg/m³]
const WindReferenceSpeed       = 22.0;       // [m/s] - speed at which 1.0 exponent-normalised wind level is defined
const WindPressureExponent     = 1.9;        // [-]   - aero noise grows slightly faster than stagnation pressure
const SquealCentreHz           = 1180.0;     // [Hz]  - tread band lateral mode that squeals on asphalt
const SquealBandwidthHz        = 420.0;      // [Hz]  - spread of the stick-slip limit cycle
const BrakeSquealHz            = 2150.0;     // [Hz]  - disc/pad squeal mode
const RotorOrderCount          = 6;          // [count] - brake rotor vane orders that carry load
const InjectorTickPerRevolution = 0.5;       // [-]   - one tick per two crank revolutions per injector bank
const FanBladeCount            = 7;          // [count]
const WindFilterSections       = 3;
const RoadNoiseSections        = 3;

//------------------------------------------------------------------------------------------------------------------------
//                                                      FILTER ELEMENTS
//------------------------------------------------------------------------------------------------------------------------

function CreateResonator()
{
    return { IntegratorOne: 0.0, IntegratorTwo: 0.0, A1: 0.0, A2: 0.0, A3: 0.0, Damping: 1.0 };
}

function ConfigureResonator(Resonator, FrequencyHz, QualityFactor, SampleRate)
{
    const NyquistCeiling = SampleRate * 0.46;
    const Frequency = Math.min(Math.max(FrequencyHz, 6.0), NyquistCeiling);
    const G = Math.tan(Math.PI * Frequency / SampleRate);
    const K = 1.0 / Math.max(0.30, QualityFactor);
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

function AdvanceRandom(Sequence)
{
    let X = Sequence ^ 0x9e3779b9;
    X ^= X << 13; X >>>= 0;
    X ^= X >>> 17;
    X ^= X << 5;  X >>>= 0;
    return { Sequence: X, Sample: (X / 4294967296.0) * 2.0 - 1.0 };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       VOICE CREATION
//------------------------------------------------------------------------------------------------------------------------

export function CreateMechanicalVoice(Profile, SampleRate)
{
    const Voice = {
        SampleRate,
        Profile,
        RandomSequence: 0x6c078965,
        Smooth: 1.0 - Math.exp(-1.0 / (0.020 * SampleRate)),

        SpeedMemory: 0.0,
        WheelRPMMemory: 0.0,
        EngineRPMMemory: 0.0,
        ThrottleMemory: 0.0,
        ScrubMemory: 0.0,
        SlipMemory: 0.0,
        BrakeMemory: 0.0,
        SteerMemory: 0.0,
        FanMemory: 0.0,
        GearMemory: 0.0,
        DifferentialMemory: 0.0,
        LashMemory: 0.0,

        GearPhase: new Float64Array(3),
        GearResonators: [CreateResonator(), CreateResonator(), CreateResonator()],
        DifferentialPhase: new Float64Array(2),
        DifferentialResonators: [CreateResonator(), CreateResonator()],

        SquealOscillator: 0.0,
        SquealResonator: CreateResonator(),
        SquealNoise: [CreateResonator(), CreateResonator()],
        SquealEnvelope: 0.0,
        SquealFrequency: SquealCentreHz,

        WindNoise: [],
        WindLowPass: { Memory: 0.0, Coefficient: 0.02 },

        RoadNoise: [],
        RoadResonator: CreateResonator(),

        BrakeResonators: [],
        BrakePhase: new Float64Array(RotorOrderCount),
        BrakeEnvelope: 0.0,

        FanPhase: new Float64Array(FanBladeCount),
        FanNoise: [CreateResonator()],

        InjectorPhase: 0.0,
        InjectorFilter: CreateResonator(),

        PumpPhase: 0.0,
        PumpFilter: CreateResonator(),

        ClunkPhase: 0.0,
        ClunkEnvelope: 0.0,
        ClunkResonator: CreateResonator(),
        ClunkLowResonator: CreateResonator(),

        PreviousLongitudinal: 0.0,
        PreviousGearIndex: 0
    };

    const K = Profile.Chassis;
    const AirSpeed = Profile.AirSoundSpeed;
    const SpeedOfSound = 343.0;

    ConfigureResonator(Voice.SquealResonator, SquealCentreHz, SquealCentreHz / SquealBandwidthHz, SampleRate);
    ConfigureResonator(Voice.SquealNoise[0], 2400.0, 2.2, SampleRate);
    ConfigureResonator(Voice.SquealNoise[1], 3600.0, 1.8, SampleRate);
    ConfigureResonator(Voice.RoadResonator, 92.0, 1.6, SampleRate);
    ConfigureResonator(Voice.InjectorFilter, 2400.0, 3.4, SampleRate);
    ConfigureResonator(Voice.PumpFilter, Math.max(40.0, Profile.Tones.FuelPumpHz), 7.0, SampleRate);
    ConfigureResonator(Voice.ClunkResonator, 340.0, 4.5, SampleRate);
    ConfigureResonator(Voice.ClunkLowResonator, 88.0, 2.4, SampleRate);
    ConfigureResonator(Voice.FanNoise[0], 620.0, 1.4, SampleRate);

    for (let Index = 0; Index < RotorOrderCount; ++Index)
        Voice.BrakeResonators.push(CreateResonator());
    for (let Index = 0; Index < RotorOrderCount; ++Index)
        ConfigureResonator(Voice.BrakeResonators[Index], BrakeSquealHz * (1.0 + Index * 0.22), 12.0 - Index * 1.4, SampleRate);

    for (let Index = 0; Index < Voice.GearResonators.length; ++Index)
        ConfigureResonator(Voice.GearResonators[Index], 640.0 * (Index + 1), 6.0 - Index * 1.2, SampleRate);
    for (let Index = 0; Index < Voice.DifferentialResonators.length; ++Index)
        ConfigureResonator(Voice.DifferentialResonators[Index], 420.0 * (Index + 1), 5.0 - Index * 1.0, SampleRate);

    for (let Section = 0; Section < WindFilterSections; ++Section)
        Voice.WindNoise.push(CreateBandSection([180.0, 720.0, 2100.0][Section], [0.5, 0.7, 0.9][Section], SampleRate));
    for (let Section = 0; Section < RoadNoiseSections; ++Section)
        Voice.RoadNoise.push(CreateBandSection([70.0, 210.0, 620.0][Section], [0.9, 0.7, 0.5][Section], SampleRate));

    ConfigureResonator(Voice.PumpFilter, Math.max(40.0, Profile.Tones.FuelPumpHz), 7.0, SampleRate);
    void K; void AirSpeed; void SpeedOfSound;
    return Voice;
}

function CreateBandSection(Frequency, Width, SampleRate)
{
    const Resonator = CreateResonator();
    ConfigureResonator(Resonator, Frequency, Math.max(0.4, Frequency / Math.max(40.0, Width * Frequency)), SampleRate);
    return Resonator;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     BLOCK RENDER
//------------------------------------------------------------------------------------------------------------------------

// 📝 Returns the summed secondary layer into the provided arrays. `Squeal` and `Clunk` are surfaced separately through the returned metrics
//    so the host can drive smoke particles or a camera shake from the same physics that made the noise.
export function RenderMechanicalBlock(Voice, Control, Left, Right, SampleCount)
{
    const Profile = Voice.Profile;
    const D = Profile.Drivetrain;
    const K = Profile.Chassis;
    const Tones = Profile.Tones;
    const Cabin = Profile.Cabin;
    const SampleRate = Voice.SampleRate;
    const Smooth = Voice.Smooth;

    const TargetSpeed = Math.max(0.0, Control.Speed || 0.0);
    const TargetWheelRPM = Math.max(0.0, Control.WheelRPM || 0.0);
    const TargetRPM = Math.max(0.0, Control.RPM || 0.0);
    const TargetThrottle = Math.max(0.0, Math.min(1.0, Control.Throttle || 0.0));
    const TargetScrub = Math.max(0.0, Math.min(1.0, Control.TyreScrub || 0.0));
    const TargetSlip = Math.max(0.0, Math.min(1.5, Control.WheelSlipRatio || 0.0));
    const TargetBrake = Math.max(0.0, Math.min(1.0, Control.Brake || 0.0));
    const TargetSteer = Math.max(-1.0, Math.min(1.0, Control.Steer || 0.0));
    const TargetFan = Math.max(0.0, Math.min(1.0, Control.Fan || 0.0));
    const TargetGearLoad = Math.max(0.0, Math.min(1.0, Control.GearWhineLoad || 0.0));
    const TargetDifferentialLoad = Math.max(0.0, Math.min(1.0, Control.DifferentialLoad || 0.0));
    const TargetLash = Math.max(0.0, Math.min(1.0, Control.DrivelineLash || 0.0));
    const TargetGearIndex = Control.GearIndex || 0;
    const InCabin = Control.Cabin === true;

    const WheelRevolutionsPerSecond = TargetSpeed / Math.max(0.12, K.WheelRadiusMetres);
    const GearRatio = D.GearRatios[Math.max(0, Math.min(D.GearRatios.length - 1, (TargetGearIndex > 0 ? TargetGearIndex : 1) - 1))];
    const GearboxInputHz = TargetWheelRPM / 60.0 * Math.max(0.1, Math.abs(GearRatio));

    // 📝 A gear whine is the tooth-mesh frequency times its harmonics: tooth count × revolutions per second. A race dog box is straight-cut
    //    and has twice the whine energy of a helical road box at the same load — the audible difference between the GT3 and the Mustang.
    const MeshBaseHz = Math.max(12.0, Math.abs(GearboxInputHz) * Math.max(6.0, D.GearMeshOrder));
    const WhineGain = Math.pow(10.0, (D.GearWhineDecibels - 12.0) / 20.0) * (D.SequentialDogBox ? 1.85 : 1.0);
    const DifferentialBaseHz = Math.max(8.0, WheelRevolutionsPerSecond * Math.max(4.0, D.DifferentialRatioOrder));

    // 📝 Wind: stagnation pressure ½ρv² over the frontal area, normalised to a reference speed. Above roughly 30 m/s it dominates.
    const WindPressure = 0.5 * AirDensity * TargetSpeed * TargetSpeed * K.DragArea;
    const WindReference = 0.5 * AirDensity * WindReferenceSpeed * WindReferenceSpeed * 0.7;
    const WindLevel = Math.pow(Math.max(0.0, WindPressure / WindReference), WindPressureExponent * 0.5);
    const RoadLevel = Math.min(2.2, TargetSpeed / 30.0) * (0.65 + 0.35 * K.RollingResistance * 60.0);

    const Insulated = InCabin ? Math.pow(10.0, -Cabin.SoundInsulationDecibels / 20.0) : 1.0;
    const CabinPass = 1.0 - 0.82 * Math.min(1.0, Math.max(0.0, InCabin ? 1.0 : 0.0)) * Math.min(1.0, Cabin.InsulationFrequencyKnee / 800.0);

    let PreviousLongitudinal = Voice.PreviousLongitudinal;

    for (let Index = 0; Index < SampleCount; ++Index)
    {
        Voice.SpeedMemory += (TargetSpeed - Voice.SpeedMemory) * Smooth;
        Voice.WheelRPMMemory += (TargetWheelRPM - Voice.WheelRPMMemory) * Smooth;
        Voice.EngineRPMMemory += (TargetRPM - Voice.EngineRPMMemory) * Smooth;
        Voice.ThrottleMemory += (TargetThrottle - Voice.ThrottleMemory) * Smooth;
        Voice.ScrubMemory += (TargetScrub - Voice.ScrubMemory) * Smooth;
        Voice.SlipMemory += (TargetSlip - Voice.SlipMemory) * Smooth;
        Voice.BrakeMemory += (TargetBrake - Voice.BrakeMemory) * Smooth;
        Voice.SteerMemory += (TargetSteer - Voice.SteerMemory) * Smooth;
        Voice.FanMemory += (TargetFan - Voice.FanMemory) * Smooth;
        Voice.GearMemory += (TargetGearLoad - Voice.GearMemory) * Smooth;
        Voice.DifferentialMemory += (TargetDifferentialLoad - Voice.DifferentialMemory) * Smooth;
        Voice.LashMemory += (TargetLash - Voice.LashMemory) * Smooth;

        let Contribution = 0.0;

        // ① Gearbox mesh whine. Three orders, because a real gear pair radiates its fundamental plus the tooth-pass sidebands.
        if (Voice.GearMemory > 0.004 && Math.abs(Voice.WheelRPMMemory) > 2.0)
        {
            let Mesh = 0.0;
            for (let Order = 0; Order < 3; ++Order)
            {
                const Frequency = MeshBaseHz * (Order + 1);
                if (Frequency > SampleRate * 0.45) break;
                Voice.GearPhase[Order] += TwoPi * Frequency / SampleRate;
                if (Voice.GearPhase[Order] > TwoPi) Voice.GearPhase[Order] -= TwoPi;
                Mesh += Math.sin(Voice.GearPhase[Order]) / Math.pow(Order + 1, 0.85);
            }
            const Shaped = EvaluateResonator(Voice.GearResonators[0], Mesh);
            Contribution += Shaped * Voice.GearMemory * 0.020 * WhineGain;
        }

        // ② Differential crown-wheel whine, two orders, present whenever the car is rolling even off throttle.
        if (Math.abs(Voice.WheelRPMMemory) > 4.0)
        {
            let Differential = 0.0;
            for (let Order = 0; Order < 2; ++Order)
            {
                const Frequency = DifferentialBaseHz * (Order + 1);
                if (Frequency > SampleRate * 0.45) break;
                Voice.DifferentialPhase[Order] += TwoPi * Frequency / SampleRate;
                if (Voice.DifferentialPhase[Order] > TwoPi) Voice.DifferentialPhase[Order] -= TwoPi;
                Differential += Math.sin(Voice.DifferentialPhase[Order]) / (Order + 1);
            }
            const Shaped = EvaluateResonator(Voice.DifferentialResonators[0], Differential);
            Contribution += Shaped * (0.006 + 0.012 * Voice.DifferentialMemory) * (D.SequentialDogBox ? 1.4 : 0.8);
        }

        // ③ Tyre scrub and squeal. Stick-slip is a limit cycle: the tread band springs against the road, releases, and rings at a lateral
        //    mode. Slip velocity raises both the frequency and the amplitude — a real squeal climbs in pitch as the car washes out.
        const SlipAmount = Math.max(Voice.ScrubMemory * 0.7, Math.min(1.0, Voice.SlipMemory * 0.9));
        if (SlipAmount > 0.02 && Math.abs(Voice.SpeedMemory) > 1.5)
        {
            const Frequency = SquealCentreHz + SquealBandwidthHz * SlipAmount * 1.35;
            if (Math.abs(Frequency - Voice.SquealFrequency) > 1.0)
            {
                Voice.SquealFrequency = Frequency;
                ConfigureResonator(Voice.SquealResonator, Frequency, Math.max(0.9, Frequency / (SquealBandwidthHz * 0.9)), SampleRate);
            }
            const Draw = AdvanceRandom(Voice.RandomSequence);
            Voice.RandomSequence = Draw.Sequence;
            const Excitation = Draw.Sample * SlipAmount + Math.sin(Voice.SquealOscillator) * 0.35;
            Voice.SquealOscillator += TwoPi * Frequency / SampleRate;
            if (Voice.SquealOscillator > TwoPi) Voice.SquealOscillator -= TwoPi;
            Voice.SquealEnvelope += (SlipAmount - Voice.SquealEnvelope) * 0.0009;
            const Ring = EvaluateResonator(Voice.SquealResonator, Excitation);
            const Hiss = EvaluateResonator(Voice.SquealNoise[0], Draw.Sample) +
                         EvaluateResonator(Voice.SquealNoise[1], Draw.Sample) * 0.6;
            Contribution += Ring * Voice.SquealEnvelope * 0.115 + Hiss * Voice.SquealEnvelope * 0.030;
        }
        else
        {
            Voice.SquealEnvelope *= 0.9995;
        }

        // ④ Wind. Broadband with a low-passed body, amplitude from real stagnation pressure. The pan and the level both rise with speed.
        if (WindLevel > 0.001)
        {
            const Draw = AdvanceRandom(Voice.RandomSequence);
            Voice.RandomSequence = Draw.Sequence;
            let Wind = 0.0;
            for (let Section = 0; Section < Voice.WindNoise.length; ++Section)
                Wind += EvaluateResonator(Voice.WindNoise[Section], Draw.Sample) * [1.0, 0.6, 0.35][Section];
            Voice.WindLowPass.Memory += 0.06 * (Wind - Voice.WindLowPass.Memory);
            Contribution += (Wind * 0.05 + Voice.WindLowPass.Memory * 0.12) * WindLevel * 0.30;
        }

        // ⑤ Road noise: tread block impact on coarse asphalt, rises with speed but saturates.
        if (RoadLevel > 0.001)
        {
            const Draw = AdvanceRandom(Voice.RandomSequence);
            Voice.RandomSequence = Draw.Sequence;
            let Road = 0.0;
            for (let Section = 0; Section < Voice.RoadNoise.length; ++Section)
                Road += EvaluateResonator(Voice.RoadNoise[Section], Draw.Sample) * [1.0, 0.55, 0.3][Section];
            const Body = EvaluateResonator(Voice.RoadResonator, Road);
            Contribution += (Road * 0.014 + Body * 0.030) * Math.min(1.4, RoadLevel);
        }

        // ⑥ Brake judder and squeal: rotor vane orders excited at low speed under light-to-medium pedal.
        if (Voice.BrakeMemory > 0.03 && Math.abs(Voice.SpeedMemory) > 0.9 && Math.abs(Voice.SpeedMemory) < 26.0)
        {
            const RotorHz = Math.max(1.5, WheelRevolutionsPerSecond * 1.0);
            const LightLoad = 1.0 - Math.abs(Voice.BrakeMemory - 0.45) * 1.55;
            const SquealState = Math.max(0.0, LightLoad) * Math.min(1.0, Math.abs(Voice.SpeedMemory) / 8.0);
            Voice.BrakeEnvelope += (SquealState - Voice.BrakeEnvelope) * 0.0016;
            if (Voice.BrakeEnvelope > 0.004)
            {
                let Squeal = 0.0;
                for (let Order = 0; Order < RotorOrderCount; ++Order)
                {
                    Voice.BrakePhase[Order] += TwoPi * Math.max(1.0, RotorHz * (Order + 1)) / SampleRate;
                    if (Voice.BrakePhase[Order] > TwoPi) Voice.BrakePhase[Order] -= TwoPi;
                    Squeal += EvaluateResonator(Voice.BrakeResonators[Order], Math.sin(Voice.BrakePhase[Order])) /
                              Math.pow(Order + 1, 0.7);
                }
                Contribution += Squeal * Voice.BrakeEnvelope * 0.010;
            }
        }
        else
        {
            Voice.BrakeEnvelope *= 0.999;
        }

        // ⑦ Cooling fan: blade-pass tone plus broadband wash, gated by coolant temperature.
        if (Voice.FanMemory > 0.01)
        {
            const FanHz = 42.0 + 38.0 * Voice.FanMemory;
            let Fan = 0.0;
            for (let Blade = 0; Blade < FanBladeCount; ++Blade)
            {
                Voice.FanPhase[Blade] += TwoPi * FanHz * (Blade + 1) / SampleRate;
                if (Voice.FanPhase[Blade] > TwoPi) Voice.FanPhase[Blade] -= TwoPi;
                Fan += Math.sin(Voice.FanPhase[Blade]) / Math.pow(Blade + 1, 1.05);
            }
            const Draw = AdvanceRandom(Voice.RandomSequence);
            Voice.RandomSequence = Draw.Sequence;
            const Wash = EvaluateResonator(Voice.FanNoise[0], Draw.Sample);
            Contribution += (Fan * 0.0055 + Wash * 0.012) * Voice.FanMemory;
        }

        // ⑧ Injector tick: a solenoid slamming open, one click per two revolutions per injector, high-passed by the block.
        if (Tones.InjectorClicks && Voice.EngineRPMMemory > 200.0 && Voice.ThrottleMemory > 0.01)
        {
            Voice.InjectorPhase += Math.max(1.0, Voice.EngineRPMMemory / 60.0 * InjectorTickPerRevolution) / SampleRate;
            if (Voice.InjectorPhase >= 1.0)
            {
                Voice.InjectorPhase -= 1.0;
                const Draw = AdvanceRandom(Voice.RandomSequence);
                Voice.RandomSequence = Draw.Sequence;
                const Tick = EvaluateResonator(Voice.InjectorFilter, Draw.Sample) * Math.pow(10.0, Tones.InjectorDecibels / 20.0);
                Contribution += Tick * 2.4 * Voice.ThrottleMemory;
            }
        }

        // ⑨ Fuel pump: a gearotor whine at a fixed speed, always present while the ignition is on.
        if (Tones.FuelPumpDecibels > -60.0 && Profile.Electric !== true)
        {
            Voice.PumpPhase += TwoPi * Math.max(20.0, Tones.FuelPumpHz) / SampleRate;
            if (Voice.PumpPhase > TwoPi) Voice.PumpPhase -= TwoPi;
            const Pump = EvaluateResonator(Voice.PumpFilter, Math.sin(Voice.PumpPhase) + Math.sin(Voice.PumpPhase * 2.0) * 0.35);
            Contribution += Pump * Math.pow(10.0, Tones.FuelPumpDecibels / 20.0) * 1.6;
        }

        // ⑩ Driveline lash: a clunk when the torque reverses through the lash, plus a take-up thump under launch. Two resonant bodies
        //    (prop shaft tube and the diff nose) make it read as a mechanical event rather than a click.
        if (Voice.LashMemory > 0.12 && Voice.ClunkEnvelope < 0.35)
        {
            const Draw = AdvanceRandom(Voice.RandomSequence);
            Voice.RandomSequence = Draw.Sequence;
            Voice.ClunkEnvelope = Math.min(1.0, 0.45 + Voice.LashMemory * 0.75);
            Voice.ClunkOscillator = Draw.Sample;
        }
        if (Voice.ClunkEnvelope > 0.002)
        {
            const Draw = AdvanceRandom(Voice.RandomSequence);
            Voice.RandomSequence = Draw.Sequence;
            const Tube = EvaluateResonator(Voice.ClunkResonator, Draw.Sample);
            const Nose = EvaluateResonator(Voice.ClunkLowResonator, Draw.Sample);
            Contribution += (Tube * 0.085 + Nose * 0.060) * Voice.ClunkEnvelope;
            Voice.ClunkEnvelope *= Math.exp(-1.0 / (0.028 * SampleRate));
        }

        // ⑪ Gear engagement: a soft click whenever the selected gear changes.
        if (TargetGearIndex !== Voice.PreviousGearIndex)
        {
            Voice.ClunkEnvelope = Math.min(1.0, Voice.ClunkEnvelope + 0.30);
            Voice.PreviousGearIndex = TargetGearIndex;
        }

        const CabinFiltered = Contribution * (InCabin ? Insulated * (0.35 + 0.65 * CabinPass) : 1.0);
        Left[Index] += CabinFiltered * 0.96;
        Right[Index] += CabinFiltered;

        PreviousLongitudinal = Voice.SquealEnvelope;
    }

    Voice.PreviousLongitudinal = PreviousLongitudinal;
    return Voice;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   DISCRETE EVENT HOOKS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Discrete events are queued by the host at the fixed-step rate, not per frame: a 60 Hz picture and a 12 Hz picture schedule the
//    identical clunk at the identical sample, because the schedule is in seconds, not in frames.
export function TriggerMechanicalEvent(Voice, EventName, Strength)
{
    const Gain = Math.max(0.0, Math.min(2.0, Strength));
    if (EventName === "lash" || EventName === "shift")
        Voice.ClunkEnvelope = Math.min(1.4, Voice.ClunkEnvelope + 0.55 * Gain);
    if (EventName === "squeal")
        Voice.SquealEnvelope = Math.min(1.6, Voice.SquealEnvelope + 0.35 * Gain);
    return Voice;
}
