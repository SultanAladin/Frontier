//============================================================================================================================================
//                                                        INDUCTIONVOICE.JS
//============================================================================================================================================
// 🧩 Everything the engine breathes through or drives electrically: compressor whistle and spool whoosh, blow-off discharge, Roots rotor
//    mesh whine, naturally-aspirated intake honk from runner resonance, and the permanent-magnet drive whine of an electric car.

// 📝 These are separate mechanisms, not one "turbo sound". Blade passage frequency is a shaft speed times a blade count; a Roots whine is a
//    lobe count times a drive ratio; intake honk is a quarter-wave resonance of the runner at the local air temperature; an inverter whine
//    is a switching frequency. Deriving each one is what makes the nine cars distinguishable while the pedal is moving.

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const TwoPi                  = 6.283185307179586;
const ElectricMotorPolePairs = 8.0;        // [-]   - typical PMSM design for a traction motor of this class
const InverterSwitchingHz    = 8000.0;     // [Hz]  - automotive IGBT/SiC carrier
const CompressorShaftMaxHz   = 3400.0;     // [rev/s] - ~204 000 rpm, upper shaft speed of a modern turbocharger
const BlowerCaseResonanceHz  = 2600.0;     // [Hz]  - die-cast case/rotor housing mode that makes the whine piercing
const BlowerHarmonicCeiling  = 11;         // [count] - harmonics carried before the case resonance takes over
const CompressorFoldCount    = 4;          // [count] - audible sub-harmonics of blade passage frequency
const NoiseFilterSections    = 2;

//------------------------------------------------------------------------------------------------------------------------
//                                                       VOICE CREATION
//------------------------------------------------------------------------------------------------------------------------

export function CreateInductionVoice(Profile, SampleRate)
{
    const Voice = {
        SampleRate,
        Profile,
        Electric: Profile.Electric === true,
        RandomSequence: 0x2545f491,
        ShaftMemory: 0.0,
        BoostMemory: 0.0,
        ThrottleMemory: 0.0,
        BlowerMemory: 0.0,
        BlowOffMemory: 0.0,
        SpoolMemory: 0.0,

        // Compressor
        WhistlePhase: new Float64Array(CompressorFoldCount),
        WhistleGain: new Float64Array(CompressorFoldCount),
        WhistleFilter: CreateResonator(),
        SpoolFilter: CreateNoiseSection(),
        SpoolLowPass: { Memory: 0.0, Coefficient: 0.01 },
        SurgeFilter: CreateResonator(),
        BlowOffFilter: CreateNoiseSection(),
        BlowOffResonator: CreateResonator(),
        BlowOffEnvelope: 0.0,

        // Roots blower
        BlowerPhase: new Float64Array(BlowerHarmonicCeiling),
        BlowerCaseFilter: CreateResonator(),
        BlowerHissFilter: CreateNoiseSection(),

        // Naturally aspirated intake
        IntakeResonators: [],
        IntakeGains: [],
        IntakeNoise: CreateNoiseSection(),
        IntakeHonkPhase: 0.0,
        IntakeFilter: CreateResonator(),

        // Electric drive
        MotorPhase: 0.0,
        InverterPhase: 0.0,
        MotorResonator: CreateResonator(),
        InverterResonator: CreateResonator(),
        ReductionResonator: CreateResonator(),
        MotorGainMemory: 0.0
    };

    const AirSpeed = Profile.AirSoundSpeed;
    const Runner = Math.max(0.02, Profile.Induction.RunnerLength);
    const ModeCount = 3;
    for (let Index = 0; Index < ModeCount; ++Index)
    {
        Voice.IntakeResonators.push(CreateResonator());
        // 📝 Quarter-wave modes of the intake runner: odd multiples of c/4L. The lowest one is the note an engine makes when it is
        //    breathing hardest, which is why it appears at wide throttle, not at idle.
        Voice.IntakeGains.push([1.0, 0.48, 0.26][Index]);
        ConfigureResonator(Voice.IntakeResonators[Index], (2 * Index + 1) * AirSpeed / (4.0 * Runner), 5.0 - Index * 1.2, SampleRate);
    }

    ConfigureResonator(Voice.WhistleFilter, 3200.0, 14.0, SampleRate);
    ConfigureResonator(Voice.SurgeFilter, 46.0, 3.0, SampleRate);
    ConfigureResonator(Voice.BlowOffResonator, 1750.0, 2.6, SampleRate);
    ConfigureResonator(Voice.BlowerCaseFilter, BlowerCaseResonanceHz, 9.0, SampleRate);
    ConfigureResonator(Voice.IntakeFilter, Math.max(90.0, AirSpeed / (4.0 * Math.max(0.05, Runner))), 3.0, SampleRate);
    ConfigureResonator(Voice.MotorResonator, 860.0, 6.0, SampleRate);
    ConfigureResonator(Voice.InverterResonator, InverterSwitchingHz, 11.0, SampleRate);
    ConfigureResonator(Voice.ReductionResonator, 2400.0, 5.0, SampleRate);

    for (let Section = 0; Section < NoiseFilterSections; ++Section)
    {
        ConfigureNoiseSection(Voice.SpoolFilter, [140.0, 620.0][Section], [0.7, 0.45][Section], SampleRate, Section);
        ConfigureNoiseSection(Voice.BlowOffFilter, [900.0, 2600.0][Section], [1.1, 0.9][Section], SampleRate, Section);
        ConfigureNoiseSection(Voice.BlowerHissFilter, [1800.0, 5200.0][Section], [0.5, 0.4][Section], SampleRate, Section);
        ConfigureNoiseSection(Voice.IntakeNoise, [220.0, 900.0][Section], [0.55, 0.35][Section], SampleRate, Section);
    }

    Voice.SmoothingCoefficient = 1.0 - Math.exp(-1.0 / (0.015 * SampleRate));
    return Voice;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      FILTER ELEMENTS
//------------------------------------------------------------------------------------------------------------------------

export function CreateResonator()
{
    return { IntegratorOne: 0.0, IntegratorTwo: 0.0, A1: 0.0, A2: 0.0, A3: 0.0, Damping: 1.0 };
}

// 📝 Q is passed in directly here: a case resonance or a pipe mode is a physical Q, not a design filter order.
export function ConfigureResonator(Resonator, FrequencyHz, QualityFactor, SampleRate)
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

export function EvaluateResonator(Resonator, Input)
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

function CreateNoiseSection()
{
    const Sections = [];
    for (let Index = 0; Index < NoiseFilterSections; ++Index)
        Sections.push(CreateResonator());
    return Sections;
}

function ConfigureNoiseSection(Sections, Frequency, QualityFactor, SampleRate, Index)
{
    ConfigureResonator(Sections[Index], Frequency, QualityFactor, SampleRate);
}

function EvaluateNoiseSection(Sections, Input)
{
    let Output = Input;
    for (let Index = 0; Index < Sections.length; ++Index)
        Output = EvaluateResonator(Sections[Index], Output);
    return Output;
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
//                                                     BLOCK RENDER
//------------------------------------------------------------------------------------------------------------------------

// 📝 Adds into Left/Right. The turbine, blower and intake sit on the engine side of the firewall, which is asymmetric in a left-hand-drive
//    car; the pan below is that small offset, and it is why an induction layer is narrower than the exhaust.
export function RenderInductionBlock(Voice, Control, Left, Right, SampleCount)
{
    const Profile = Voice.Profile;
    const I = Profile.Induction;
    const C = Profile.Combustion;
    const SampleRate = Voice.SampleRate;
    const Smooth = Voice.SmoothingCoefficient;

    const TargetBoost = Math.max(0.0, Math.min(1.4, Control.BoostFraction || 0.0));
    const TargetShaft = Math.max(0.0, Math.min(1.2, Control.TurbineSpeed || 0.0));
    const TargetBlower = Math.max(0.0, Math.min(1.2, Control.BlowerPressure || 0.0));
    const TargetLoad = Math.max(0.0, Math.min(1.35, Control.Load || 0.0));
    const TargetThrottle = Math.max(0.0, Math.min(1.0, Control.Throttle || 0.0));
    const TargetBlowOff = Math.max(0.0, Math.min(1.0, Control.BlowOff || 0.0));
    const TargetMotor = Math.max(0.0, Math.min(1.3, Control.MotorFraction || 0.0));
    const TargetRPM = Math.max(0.0, Control.RPM || 0.0);
    const TargetWheelRPM = Math.max(0.0, Control.WheelRPM || 0.0);

    const Electric = Voice.Electric;
    const Turbo = I.Topology === "twinScrollTurbo";
    const Blower = I.Topology === "rootsSupercharger";
    const NaturallyAspirated = I.Topology === "naturallyAspirated";

    // 📝 Blade passage frequency, in Hz: shaft revolutions per second × blades. The audible component is whichever subdivision lands
    //    below Nyquist, which is why the whistle sweeps up through the rev range and then settles.
    const BladeCount = Math.max(1.0, I.CompressorBladeCount);
    const ShaftHz = TargetShaft * CompressorShaftMaxHz;
    const BladePassageHz = ShaftHz * BladeCount;

    const BlowerRevHz = TargetRPM / 60.0 * Math.max(0.1, I.BlowerDriveRatio);
    const BlowerMeshHz = BlowerRevHz * Math.max(1.0, I.LobeCount);

    const WheelHz = TargetWheelRPM / 60.0;
    const MotorHz = WheelHz * 15.0 * Math.max(1.0, Profile.Drivetrain.GearRatios[0]);
    const ElectricalHz = MotorHz * ElectricMotorPolePairs;

    const Idle = Math.max(1.0, C.IdleRPM);
    const Redline = Math.max(1.0, C.RedlineRPM);

    if (Turbo && TargetBlowOff > 0.85 && Voice.BlowOffMemory < 0.5)
        Voice.BlowOffEnvelope = Math.min(1.4, 0.85 + TargetBoost * 0.5);

    for (let Index = 0; Index < SampleCount; ++Index)
    {
        Voice.BoostMemory += (TargetBoost - Voice.BoostMemory) * Smooth;
        Voice.ShaftMemory += (TargetShaft - Voice.ShaftMemory) * Smooth;
        Voice.BlowerMemory += (TargetBlower - Voice.BlowerMemory) * Smooth;
        Voice.ThrottleMemory += (TargetThrottle - Voice.ThrottleMemory) * Smooth;
        Voice.BlowOffMemory += (TargetBlowOff - Voice.BlowOffMemory) * Smooth;
        Voice.MotorGainMemory += (TargetMotor - Voice.MotorGainMemory) * Smooth;

        let Contribution = 0.0;

        // ① Compressor whistle and spool. Folded sub-harmonics keep an audible pitch through the whole shaft-speed range.
        if (Turbo && Voice.ShaftMemory > 0.012)
        {
            const Shaft = Voice.ShaftMemory;
            const Folds = [1.0, 0.5, 0.25, 0.125];
            let Whistle = 0.0;
            for (let Fold = 0; Fold < CompressorFoldCount; ++Fold)
            {
                const Frequency = Math.min(SampleRate * 0.45, BladePassageHz * Folds[Fold]);
                if (Frequency < 40.0) continue;
                Voice.WhistlePhase[Fold] += TwoPi * Frequency / SampleRate;
                if (Voice.WhistlePhase[Fold] > TwoPi) Voice.WhistlePhase[Fold] -= TwoPi;
                const Weight = [1.0, 0.55, 0.34, 0.20][Fold];
                Whistle += Math.sin(Voice.WhistlePhase[Fold]) * Weight;
            }
            const ShaftTone = EvaluateResonator(Voice.WhistleFilter, Whistle);
            const SpoolDraw = AdvanceRandom(Voice.RandomSequence);
            Voice.RandomSequence = SpoolDraw.Sequence;
            const SpoolNoise = EvaluateNoiseSection(Voice.SpoolFilter, SpoolDraw.Sample) *
                               Math.pow(Shaft, 1.6) * (0.55 + 0.45 * Voice.ThrottleMemory);
            const Surge = EvaluateResonator(Voice.SurgeFilter, SpoolDraw.Sample) * Shaft * 0.35;
            Contribution += ShaftTone * Shaft * (0.020 + 0.055 * Voice.BoostMemory) + SpoolNoise * 0.16 + Surge;
        }

        // ② Blow-off discharge: a burst of compressed air through a horn. Rate is set by boost, gate by the envelope.
        if (Turbo && Voice.BlowOffEnvelope > 0.003)
        {
            const Draw = AdvanceRandom(Voice.RandomSequence);
            Voice.RandomSequence = Draw.Sequence;
            const Body = EvaluateNoiseSection(Voice.BlowOffFilter, Draw.Sample);
            const Ring = EvaluateResonator(Voice.BlowOffResonator, Draw.Sample);
            Contribution += (Body * 0.42 + Ring * 0.30) * Voice.BlowOffEnvelope;
            Voice.BlowOffEnvelope *= Math.exp(-1.0 / (0.055 * SampleRate));
        }

        // ③ Roots blower: lobe count × rotor speed is the mesh tone; the die-cast case resonates it into a whine.
        if (Blower && Voice.BlowerMemory > 0.01)
        {
            let Mesh = 0.0;
            for (let Harmonic = 0; Harmonic < BlowerHarmonicCeiling; ++Harmonic)
            {
                const Frequency = BlowerMeshHz * (Harmonic + 1);
                if (Frequency > SampleRate * 0.45) break;
                Voice.BlowerPhase[Harmonic] += TwoPi * Frequency / SampleRate;
                if (Voice.BlowerPhase[Harmonic] > TwoPi) Voice.BlowerPhase[Harmonic] -= TwoPi;
                Mesh += Math.sin(Voice.BlowerPhase[Harmonic]) / Math.pow(Harmonic + 1, 0.55);
            }
            const Case = EvaluateResonator(Voice.BlowerCaseFilter, Mesh);
            const HissDraw = AdvanceRandom(Voice.RandomSequence);
            Voice.RandomSequence = HissDraw.Sequence;
            const Hiss = EvaluateNoiseSection(Voice.BlowerHissFilter, HissDraw.Sample);
            const WhineGain = Math.pow(Voice.BlowerMemory, 1.15) * (0.030 + 0.055 * Voice.ThrottleMemory);
            Contribution += Case * WhineGain * (I.BlowerHissDecibels * 0.16) + Hiss * Voice.BlowerMemory * 0.020;
        }

        // ④ Naturally aspirated intake honk: the runner's quarter-wave mode excited by intake pulses. It appears with throttle and
        //    load, and on a V12 at 9000 rpm it is the sound people describe as "induction scream".
        if (NaturallyAspirated)
        {
            const IntakePulseHz = TargetRPM / 60.0 * Math.max(1, C.CylinderCount) / Math.max(1.0, C.CycleRevolutions || 2.0) * 0.5;
            Voice.IntakeHonkPhase += TwoPi * Math.max(1.0, IntakePulseHz) / SampleRate;
            if (Voice.IntakeHonkPhase > TwoPi) Voice.IntakeHonkPhase -= TwoPi;
            const Pulse = Math.sin(Voice.IntakeHonkPhase);
            let Honk = 0.0;
            for (let Index = 0; Index < Voice.IntakeResonators.length; ++Index)
                Honk += EvaluateResonator(Voice.IntakeResonators[Index], Pulse) * Voice.IntakeGains[Index];
            const Draw = AdvanceRandom(Voice.RandomSequence);
            Voice.RandomSequence = Draw.Sequence;
            const Air = EvaluateNoiseSection(Voice.IntakeNoise, Draw.Sample);
            const Opening = Math.pow(Voice.ThrottleMemory, 1.35) * (0.30 + 0.70 * Math.min(1.0, TargetLoad));
            const SpeedBand = Math.min(1.0, Math.max(0.0, (TargetRPM - Idle) / Redline));
            Contribution += (Honk * 0.045 * Opening + Air * 0.055 * Opening * (0.4 + 0.6 * SpeedBand));
        }

        // ⑤ Electric drive. Four layers: motor magnetic whine, reduction gear mesh, inverter carrier, and a torque-ripple growl.
        if (Electric)
        {
            const MotorLevel = Voice.MotorGainMemory;
            if (MotorLevel > 0.002)
            {
                Voice.MotorPhase += TwoPi * Math.min(SampleRate * 0.45, Math.max(8.0, ElectricalHz)) / SampleRate;
                if (Voice.MotorPhase > TwoPi) Voice.MotorPhase -= TwoPi;
                Voice.InverterPhase += TwoPi * InverterSwitchingHz / SampleRate;
                if (Voice.InverterPhase > TwoPi) Voice.InverterPhase -= TwoPi;

                const MotorFundamental = Math.sin(Voice.MotorPhase);
                const MotorSecond = Math.sin(Voice.MotorPhase * 2.0 + 0.4) * 0.32;
                const MotorThird = Math.sin(Voice.MotorPhase * 3.0 + 1.1) * 0.14;
                const Electrical = EvaluateResonator(Voice.MotorResonator, MotorFundamental + MotorSecond + MotorThird);

                const MeshPhase = TwoPi * Math.min(SampleRate * 0.45, Math.max(6.0, MotorHz * Math.max(1.0, Profile.Drivetrain.GearMeshOrder) *
                                                         0.045)) / SampleRate;
                const Mesh = EvaluateResonator(Voice.ReductionResonator, Math.sin(MeshPhase));
                const Carrier = Math.sin(Voice.InverterPhase) * (1.0 - 0.6 * MotorLevel);
                const CarrierResonant = EvaluateResonator(Voice.InverterResonator, Carrier + MotorFundamental * 0.5);

                Contribution += (Electrical * 0.030 + Mesh * 0.016 + CarrierResonant * 0.008) * MotorLevel;
            }
        }

        Left[Index] += Contribution * 0.92;
        Right[Index] += Contribution;
    }

    return Voice;
}
