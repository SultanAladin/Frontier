//============================================================================================================================================
//                                                          EXHAUSTVOICE.JS
//============================================================================================================================================
// 🧩 Exhaust radiation from first principles: one pressure pulse per firing event, injected into a bank of quarter-wave pipe resonances derived
//    from the car's own primary/collector/tailpipe lengths and gas temperature, then nonlinearly radiated through the tailpipe and bypass.
//    Nothing is sampled and nothing is keyed to the drawn frame rate — the renderer asks for a block of samples and gets a block.

// 📝 Why this is not a sample player and not an FPS-driven graph: the pulse TRIGGER is crank angle, integrated from RPM per audio sample.
//    A 12 Hz host update therefore changes RPM 12 times a second, while the pulses between those updates keep arriving at the exact angle
//    the engine would have reached. That is the whole trick, and it is why the sound does not stutter or vanish when the picture drops.

// 📝 Control contract. `Control` is a plain object owned by the caller and reused every render call; the voice never allocates, never
//    allocates a closure, never reads a clock, and never calls Math.random. Smoothing happens per sample inside the voice, so a control
//    jump arriving once every 80 ms is still continuous in the audio.

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const TwoPi                     = 6.283185307179586;
const DegreesPerRevolution      = 360.0;
const PiOverSample              = Math.PI;
const MaximumPulseAccumulator   = 6.0;      // [-] - safety clamp on the pulse integrator
const ResonanceDampingFloor     = 0.35;     // [-] - Q never exceeds 1/(2·floor) so a mode cannot ring forever
const CrackleCeiling            = 3.0;      // [count/sample] - rate accumulator guard
const PartialCeiling            = 10;      // [count] - harmonics of the firing frequency carried before the shed
const UnevenPartialCeiling      = 3;       // [count] - non-integer partials for an odd-fire layout
const LoadRoughnessExponent     = 0.38;    // [-]   - how much load sharpens the pulse train

//------------------------------------------------------------------------------------------------------------------------
//                                                   DETERMINISTIC NOISE
//------------------------------------------------------------------------------------------------------------------------

// 📝 xorshift32 rather than Math.random: the same controls produce the same audio on every run, which is what makes the offline
//    renders in Validation/ byte-comparable and the tests meaningful.
export function AdvanceRandomSequence(Sequence)
{
    let X = Sequence ^ 0x9e3779b9;
    X ^= X << 13; X >>>= 0;
    X ^= X >>> 17;
    X ^= X << 5;  X >>>= 0;
    Sequence = X;
    return { Sequence, Sample: (X / 4294967296.0) * 2.0 - 1.0 };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FILTER PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Zavalishin's topology-preserving state-variable filter: unconditionally stable under coefficient changes, which matters because the
//    centre frequency tracks firing order at 9000 rpm and would otherwise pop through a direct-form IIR.
export function CreateBandFilter()
{
    return { IntegratorOne: 0.0, IntegratorTwo: 0.0, A1: 0.0, A2: 0.0, A3: 0.0, Damping: 1.0 };
}

export function ConfigureBandFilter(Filter, CentreHz, QualityFactor, SampleRate)
{
    const NyquistCeiling = SampleRate * 0.46;
    const Frequency = Math.min(Math.max(CentreHz, 8.0), NyquistCeiling);
    const G = Math.tan(PiOverSample * Frequency / SampleRate);
    const K = 1.0 / Math.max(ResonanceDampingFloor, QualityFactor);
    const A1 = 1.0 / (1.0 + G * (G + K));
    Filter.A1 = A1;
    Filter.A2 = G * A1;
    Filter.A3 = G * Filter.A2;
    Filter.Damping = K;
    return Filter;
}

export function EvaluateBandFilter(Filter, Input, BandPassGain)
{
    const V3 = Input - Filter.IntegratorTwo;
    const V1 = Filter.A1 * Filter.IntegratorOne + Filter.A2 * V3;
    const V2 = Filter.IntegratorTwo + Filter.A2 * Filter.IntegratorOne + Filter.A3 * V3;
    Filter.IntegratorOne = 2.0 * V1 - Filter.IntegratorOne;
    Filter.IntegratorTwo = 2.0 * V2 - Filter.IntegratorTwo;
    if (!Number.isFinite(Filter.IntegratorOne) || !Number.isFinite(Filter.IntegratorTwo))
    {
        Filter.IntegratorOne = 0.0;
        Filter.IntegratorTwo = 0.0;
        return 0.0;
    }
    return V2 + BandPassGain * Filter.Damping * V1;
}

export function CreateOnePole()
{
    return { Memory: 0.0, Coefficient: 1.0 };
}

export function ConfigureOnePole(Pole, CutoffHz, SampleRate)
{
    const Frequency = Math.min(Math.max(CutoffHz, 0.5), SampleRate * 0.47);
    Pole.Coefficient = 1.0 - Math.exp(-TwoPi * Frequency / SampleRate);
    return Pole;
}

export function EvaluateOnePole(Pole, Input)
{
    Pole.Memory += Pole.Coefficient * (Input - Pole.Memory);
    if (!Number.isFinite(Pole.Memory)) Pole.Memory = 0.0;
    return Pole.Memory;
}

// 📝 A second-order low-pass at the soffit of a Butterworth response: two cascaded one-poles at the knee. Used for the muffler and for the
//    cabin's glass/trim absorption, where the exact slope is less important than a stable, cheap roll-off.
export function CreateLowPassPair()
{
    return { First: CreateOnePole(), Second: CreateOnePole() };
}

export function ConfigureLowPassPair(Pair, CutoffHz, SampleRate)
{
    ConfigureOnePole(Pair.First, CutoffHz, SampleRate);
    ConfigureOnePole(Pair.Second, CutoffHz * 1.22, SampleRate);
    return Pair;
}

export function EvaluateLowPassPair(Pair, Input)
{
    return EvaluateOnePole(Pair.Second, EvaluateOnePole(Pair.First, Input));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PULSE-TRAIN HARMONICS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The exhaust of a four-stroke is a periodic pulse train: one blowdown per cylinder, spaced by the firing geometry. A periodic pulse has an
//    exact Fourier series, so the radiated pressure is synthesised as that series instead of by filtering a click and hoping. The catalogue
//    supplies the relative amplitudes through the firing topology:
//      · order 1 is always present — it is the repetition of the whole cycle.
//      · a cross-plane V8 fires two cylinders 90°/270° apart inside each bank, so its energy concentrates in orders 1 and 2 (the burble);
//      · a flat-plane V8 fires its banks alternately at even spacing, so order 4 dominates (the "exotic V8" scream);
//      · a V12's 60° spacing puts strong energy high in the series, which is why it wails rather than thumps;
//      · an odd-fire V10 is not periodic at half-cylinder order at all, so non-integer partials carry its unevenness.
export function DerivePartialAmplitudes(Profile, PartialCount)
{
    const C = Profile.Combustion;
    const Even = Profile.Firing.EvenFiring;
    const Oddness = Math.max(0.0, Math.min(1.0, C.LowOrderCharacter));
    const Cylinders = Math.max(1, C.CylinderCount);
    const Overlap = Math.max(0.0, Math.min(1.0, C.CamOverlapDegrees / 40.0));
    const FlatPlane = C.CrankLayout === "V90FlatPlane";
    const Amplitudes = new Float64Array(PartialCount);
    for (let Order = 1; Order <= PartialCount; ++Order)
    {
        let Topology = 0.30;
        if (Order === 1) Topology = 1.00;
        else if (Order === 2) Topology = Even ? 0.62 : 0.88 + 0.15 * Oddness;
        else if (Order === 3) Topology = 0.50 + 0.22 * Overlap;
        else if (Order === 4) Topology = FlatPlane ? 1.35 : 0.34;
        else if (Order === 5 || Order === 6) Topology = FlatPlane ? 0.55 : 0.30;
        else if (Order <= 8) Topology = Cylinders >= 10 ? 0.34 : 0.22;
        else Topology = 0.16;
        // 📝 A twelve-cylinder's series is dense because it repeats six times per revolution; the per-partial level falls so the total
        //    radiated power stays a function of load rather than of cylinder count.
        const Density = Math.pow(Cylinders / 8.0, -0.20);
        Amplitudes[Order - 1] = Topology * Math.pow(Order, -0.62) * Density;
    }
    return Amplitudes;
}

// 📝 Non-integer partials: an odd-fire engine cannot be described by whole multiples of the cylinder order. The Viper's 90° V10 gives a
//    0.5-order and 1.5-order component that no amount of whole-harmonic synthesis reproduces, and it is the sound everyone recognises.
export function DeriveUnevenPartials(Profile)
{
    const Oddness = Math.max(0.0, Math.min(1.0, Profile.Combustion.LowOrderCharacter));
    if (Oddness < 0.02 || Profile.Firing.EvenFiring)
        return Object.freeze({ Orders: Object.freeze([]), Amplitudes: Object.freeze([]) });
    return Object.freeze({
        Orders: Object.freeze([0.5, 1.5, 2.5]),
        Amplitudes: Object.freeze([0.85 * Oddness, 0.48 * Oddness, 0.26 * Oddness])
    });
}

export function OrderForWeightIndex(Index)
{
    return [0.5, 1.0, 2.0, 3.0, 4.0, 0.25][Index];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       VOICE CREATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Every buffer is allocated here, once. RenderExhaustBlock writes into caller-provided Float32Arrays and allocates nothing.
export function CreateExhaustVoice(Profile, SampleRate)
{
    const ModeCount = 4;
    const Resonators = [];
    const ModeGains = new Float64Array(ModeCount);
    const ModeFrequencies = new Float64Array(ModeCount);
    const ModeDamping = new Float64Array(ModeCount);

    const GasSpeed = Profile.GasSoundSpeed;
    const PrimaryLength = Math.max(0.02, Profile.Exhaust.PrimaryLength + Profile.Exhaust.CollectorLength * 0.42);
    const CollectorLength = Math.max(0.03, Profile.Exhaust.CollectorLength);
    const TailpipeLength = Math.max(0.02, Profile.Exhaust.TailpipeLength);
    const ModeSets = [
        GasSpeed / (4.0 * PrimaryLength),
        GasSpeed / (2.0 * PrimaryLength),
        GasSpeed / (4.0 * CollectorLength),
        GasSpeed / (4.0 * TailpipeLength)
    ];
    for (let Index = 0; Index < ModeCount; ++Index)
    {
        ModeFrequencies[Index] = ModeSets[Index];
        Resonators.push(CreateBandFilter());
        // 📝 Q from the wall loss: a mode's ring time is 1/λ, so Q = π·f/λ. Long, hot, open systems ring longer.
        ModeDamping[Index] = Math.max(1.2, Math.min(28.0, PiOverSample * ModeSets[Index] /
                                   Math.max(60.0, Profile.Exhaust.BoundaryLossPerSecond) * 60.0));
        ModeGains[Index] = [1.00, 0.42, 0.34, 0.22][Index];
    }

    const Speaker = ModeSets.map((Frequency) => Frequency);
    for (let Index = 0; Index < ModeCount; ++Index)
        ConfigureBandFilter(Resonators[Index], Speaker[Index], ModeDamping[Index], SampleRate);

    return {
        SampleRate,
        Profile,
        PartialAmplitudes: DerivePartialAmplitudes(Profile, PartialCeiling),
        UnevenPartials: DeriveUnevenPartials(Profile),
        FiringPhase: 0.0,
        PhaseCursor: 0,                       // [°] - position inside the combustion cycle
        EventCursor: 0,                       // [idx] - next firing event to fire
        PulseAccumulator: 0.0,                // [-] - integrator carrying the pulse tail
        PulseDecay: Math.exp(-Math.max(40.0, Profile.Exhaust.BoundaryLossPerSecond) / SampleRate),
        PulseShaped: CreateOnePole(),
        DirectPath: CreateLowPassPair(),
        MufflerPath: CreateLowPassPair(),
        BodyFilter: CreateBandFilter(),
        GrowlFilter: CreateBandFilter(),
        ScreamFilter: CreateBandFilter(),
        SubFilter: CreateOnePole(),
        ClipMemory: 0.0,
        Resonators,
        ModeFrequencies,
        ModeDamping,
        ModeGains,
        IntegralGains: new Float64Array(PartialCeiling),
        RandomSequence: 0x51ed270b,
        CrackleAccumulator: 0.0,
        CrackleEnvelope: 0.0,
        CrackleDecay: Math.exp(-1.0 / (0.011 * SampleRate)),
        CrackleTone: CreateBandFilter(),
        PopAccumulator: 0.0,
        PopEnvelope: 0.0,
        BypassMemory: 0.0,
        LoadMemory: 0.0,
        ThrottleMemory: 0.0,
        RpmMemory: Profile.Combustion.IdleRPM,
        BoostMemory: 0.0,
        MisfireMemory: 0.0,
        CutMemory: 0.0,
        LadderUpdateInterval: Math.max(8, Math.round(SampleRate * 0.0016)),
        LadderCounter: 0,
        LadderFrequency: new Float64Array(3),
        SmoothingCoefficient: 1.0 - Math.exp(-1.0 / (0.018 * SampleRate)),
        FastSmoothingCoefficient: 1.0 - Math.exp(-1.0 / (0.004 * SampleRate)),
        OutputGain: 0.0,
        OutputGainReady: false,
        PeakEstimate: 0.0
    };
}

export function ConfigureExhaustVoice(Voice, SampleRate)
{
    const E = Voice.Profile.Exhaust;
    ConfigureLowPassPair(Voice.DirectPath, Math.max(120.0, 900.0 + E.MufflerCutoffHz * 2.0), SampleRate);
    ConfigureLowPassPair(Voice.MufflerPath, Math.max(90.0, E.MufflerCutoffHz), SampleRate);
    ConfigureOnePole(Voice.PulseShaped, 260.0, SampleRate);
    ConfigureOnePole(Voice.SubFilter, 78.0, SampleRate);
    ConfigureBandFilter(Voice.CrackleTone, 620.0, 0.9, SampleRate);
    for (let Index = 0; Index < Voice.Resonators.length; ++Index)
        ConfigureBandFilter(Voice.Resonators[Index], Voice.ModeFrequencies[Index], Voice.ModeDamping[Index], SampleRate);
    return Voice;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PER-SAMPLE PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Soft saturation with asymmetry: a pressure wave that leaves an open pipe is not a symmetric tanh. The positive lobe of a blowdown
//    is sharper than the negative, so the shaping function is biased — that asymmetry is where even harmonics (the "hard" V8 second
//    order) come from, and it is why a symmetric clipper sounds electronic.
function ShapePressure(Argument, Asymmetry)
{
    const Bias = Asymmetry * 0.22;
    const Shifted = Argument + Bias;
    const Limited = Shifted > 3.0 ? 3.0 : (Shifted < -3.0 ? -3.0 : Shifted);
    return Math.tanh(Limited) - Math.tanh(Bias);
}

// 📝 Firing event injection. A cylinder's exhaust valve cracks open and dumps a blowdown pulse; that pulse is one spoken sample in the
//    accumulator, and the accumulator's decay is the pipe's wall loss. Pulse strength comes from the firing geometry, so an uneven
//    V10 really is uneven here rather than being an amplitude envelope over an even train.
function InjectFiringEvent(Voice, Strength, NoiseSample)
{
    const Jitter = 1.0 + NoiseSample * 0.045;
    Voice.PulseAccumulator = Math.min(MaximumPulseAccumulator, Voice.PulseAccumulator + Strength * Jitter);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     BLOCK RENDER
//------------------------------------------------------------------------------------------------------------------------

// 📝 RenderExhaustBlock: writes `Left` and `Right` in place. Both arrays must be at least SampleCount long and are ADDED to, so the
//    composite voice can stack intake, mechanical and tyre layers on the same buffers without intermediate copies.
export function RenderExhaustBlock(Voice, Control, Left, Right, SampleCount)
{
    const Profile = Voice.Profile;
    const E = Profile.Exhaust;
    const C = Profile.Combustion;
    const SampleRate = Voice.SampleRate;
    const Phases = Profile.Firing.Phases;
    const Strengths = Profile.Firing.Strength;
    const EventCount = Profile.Firing.EventCount;
    const CycleDegrees = DegreesPerRevolution * (C.CycleRevolutions || 2.0);
    const Electric = Profile.Electric === true;

    const Float = Voice.SmoothingCoefficient;
    const Fast = Voice.FastSmoothingCoefficient;
    const PartialAmplitudes = Voice.PartialAmplitudes;
    const UnevenOrders = Voice.UnevenPartials.Orders;
    const UnevenAmplitudes = Voice.UnevenPartials.Amplitudes;
    const Redline = Math.max(1.0, C.RedlineRPM);
    const Idle = Math.max(1.0, C.IdleRPM);

    // 📝 Coupling gains: the direct path is the un-muffled impulse (always present, carries the attack), the muffler path is what a
    //    closed valve passes, and the bypass adds the rest. Radiation efficiency scales the whole radiator, as it does physically.
    const Radiation = Math.max(0.02, Math.min(1.0, E.RadiationEfficiency * 34.0)) * 0.42;
    const MufflerGain = Math.max(0.02, E.MufflerTransmission);
    const BypassCeiling = Math.max(0.0, E.BypassGainDecibels) * 0.055;
    const Asymmetry = 0.25 + 0.55 * Math.max(0.0, Math.min(1.0, C.LowOrderCharacter));

    let Cursor = Voice.EventCursor;
    let Phase = Voice.PhaseCursor;
    let Accumulator = Voice.PulseAccumulator;
    let CrackleAccumulator = Voice.CrackleAccumulator;
    let CrackleEnvelope = Voice.CrackleEnvelope;
    let PopAccumulator = Voice.PopAccumulator;
    let PopEnvelope = Voice.PopEnvelope;
    let Random = Voice.RandomSequence;
    let Rpm = Voice.RpmMemory;
    let Load = Voice.LoadMemory;
    let Throttle = Voice.ThrottleMemory;
    let Boost = Voice.BoostMemory;
    let Bypass = Voice.BypassMemory;
    let Misfire = Voice.MisfireMemory;
    let Cut = Voice.CutMemory;
    let Peak = Voice.PeakEstimate;
    let FiringPhase = Voice.FiringPhase;

    const TargetRPM = Math.max(0.0, Control.RPM || 0.0);
    const TargetLoad = Math.max(0.0, Math.min(1.35, Control.Load || 0.0));
    const TargetThrottle = Math.max(0.0, Math.min(1.0, Control.Throttle || 0.0));
    const TargetBoost = Math.max(0.0, Math.min(1.5, Control.BoostFraction || 0.0));
    const TargetBypass = Math.max(0.0, Math.min(1.0, Control.Bypass || 0.0));
    const TargetMisfire = Math.max(0.0, Math.min(1.0, Control.Misfire || 0.0));
    const TargetCut = Math.max(0.0, Math.min(1.0, Control.IgnitionCut || 0.0));
    const Overrun = Math.max(0.0, Math.min(1.0, Control.OverrunFuel || 0.0));
    const MassFlow = Math.max(0.0, Math.min(1.6, Control.MassFlow !== undefined ? Control.MassFlow : TargetLoad));

    // 📝 Partial gain correction: a V12 at 9500 rpm and a V8 at 700 rpm must not differ only in pitch; the number of firing events per
    //    second scales the radiated pressure, so the per-pulse gain comes down as the event rate goes up. This is why a high-revving
    //    engine screams rather than simply becoming a higher-pitched version of itself.
    const EventRate = Math.max(1.0, TargetRPM / 60.0 * Math.max(1, C.CylinderCount) / Math.max(1.0, C.CycleRevolutions || 2.0));
    const FiringHz = Math.max(4.0, Rpm / 60.0 * Math.max(1, C.CylinderCount) / Math.max(1.0, C.CycleRevolutions || 2.0));
    const EventGain = Math.min(1.8, Math.pow(420.0 / EventRate, 0.28));

    const CrackleRateCeiling = E.BackfireEnabled ? 42.0 : 0.0;      // [count/s] at full overrun

    for (let Index = 0; Index < SampleCount; ++Index)
    {
        // ① Control smoothing. Everything below reads the smoothed quantities, never the raw inputs.
        Rpm += (TargetRPM - Rpm) * Float;
        Load += (TargetLoad - Load) * Float;
        Throttle += (TargetThrottle - Throttle) * (TargetThrottle > Throttle ? Fast : Float);
        Boost += (TargetBoost - Boost) * Float;
        Bypass += (TargetBypass - Bypass) * Float;
        Misfire += (TargetMisfire - Misfire) * Float;
        Cut += (TargetCut - Cut) * Fast;

        // ② Crank-angle integration. This is the one line that makes the engine frame-rate independent: angle per AUDIO sample.
        const DegreesPerSample = Rpm * 6.0 / SampleRate;
        Phase += DegreesPerSample;
        if (Phase >= CycleDegrees)
        {
            Phase -= CycleDegrees;
            Cursor = 0;
        }

        // ③ Fire every event whose crank angle has been reached inside this sample.
        while (Cursor < EventCount && Phases[Cursor] <= Phase)
        {
            const NoiseResult = AdvanceRandomSequence(Random);
            Random = NoiseResult.Sequence;
            const Strength = Strengths[Cursor] * (0.55 + 0.45 * Math.min(1.25, Load)) * EventGain;
            InjectFiringEvent(Voice, Strength, NoiseResult.Sample);
            ++Cursor;
        }

        // ④ Ignition cut and gear-cut misfire punch air/fuel straight into the tailpipe, which is why both are loud.
        if (Misfire > 0.02 || Cut > 0.02)
        {
            const Ignition = Math.max(Misfire, Cut);
            Voice.PulseAccumulator += Ignition * 0.55;
        }

        // ⑤ Overrun crackle: unburnt charge ignites in the hot pipe at a rate set by residual fuel and mass flow.
        if (Overrun > 0.04 && CrackleRateCeiling > 0.0)
        {
            const Rate = Overrun * CrackleRateCeiling * (0.35 + 0.65 * Math.min(1.4, MassFlow));
            CrackleAccumulator += Rate / SampleRate;
            if (CrackleAccumulator >= 1.0)
            {
                CrackleAccumulator -= 1.0;
                const Roll = AdvanceRandomSequence(Random);
                Random = Roll.Sequence;
                if (Roll.Sample > -0.15)
                {
                    CrackleEnvelope = Math.min(2.2, 0.55 + Math.abs(Roll.Sample) * 1.1);
                    Accumulator = Math.min(MaximumPulseAccumulator, Accumulator + 0.35 * CrackleEnvelope);
                }
            }
        }

        // ⑥ Pulse decay and shaping. The accumulator is the pipe pressure; the one-pole subtraction removes its DC part so the
        //    resonators see a transient, not a bias.
        Accumulator *= Voice.PulseDecay;
        const Shaped = EvaluateOnePole(Voice.PulseShaped, Accumulator);
        const Excitation = (Accumulator - Shaped) * 3.4;
        Voice.PulseAccumulator = Accumulator;

        CrackleEnvelope *= Voice.CrackleDecay;
        PopEnvelope *= 0.9992;

        // ⑦ Noise excitation: crackle and pop are injected INTO the pipe, so they carry the pipe's resonances rather than sitting on top.
        let Noise = 0.0;
        if (CrackleEnvelope > 0.004 || PopEnvelope > 0.004)
        {
            const Draw = AdvanceRandomSequence(Random);
            Random = Draw.Sequence;
            Noise = Draw.Sample * (CrackleEnvelope * 0.65 + PopEnvelope * 0.9);
            Noise = EvaluateBandFilter(Voice.CrackleTone, Noise, 1.0);
        }
        const PipeInput = Excitation + Noise;

        // ⑧ Pipe radiance. The resonators are fed the pulse transient, so the pipe's own quarter-wave modes colour the tail of each pulse
        //    — the hollow body of the note, and the reason a short open stack (918) and a long muffled system (Mustang) differ.
        let Resonant = 0.0;
        for (let Mode = 0; Mode < Voice.Resonators.length; ++Mode)
            Resonant += EvaluateBandFilter(Voice.Resonators[Mode], PipeInput, 0.0) * Voice.ModeGains[Mode];

        // ⑨ Firing fundamental phase. This is the pulse train's fundamental, integrated from crank angle, so the series below is exactly the
        //    harmonic content of a real pulse train at this rpm — not an oscillator retuned from a frame-rate clock.
        const CycleFractionAdvance = DegreesPerSample / CycleDegrees;
        FiringPhase += TwoPi * Math.max(1, C.CylinderCount) * CycleFractionAdvance;
        if (FiringPhase > TwoPi) FiringPhase -= TwoPi * Math.floor(FiringPhase / TwoPi);

        // ⑩ Band-limited harmonic series. Partials above 0.45·fs are dropped rather than aliased, so a V12 at 9500 rpm is filtered down to
        //    what the device can carry instead of folding back into a false whistle.
        const LoadSharpen = Math.max(0.0, Math.min(1.0, Load));
        // 📝 Spectral tilt: closed-throttle exhaust is a dark thump; wide-open it gains the upper partials that make it bark. The exponent is
        //    strictly decreasing with load, so opening the throttle always brightens the note and never inverts it.
        const Tilt = LoadRoughnessExponent + (0.62 - LoadRoughnessExponent) * LoadSharpen;
        let Tone = 0.0;
        for (let Order = 1; Order <= PartialCeiling; ++Order)
        {
            const Frequency = FiringHz * Order;
            if (Frequency > SampleRate * 0.45) break;
            Tone += PartialAmplitudes[Order - 1] * Math.pow(Order, -Tilt) * Math.sin(FiringPhase * Order);
        }
        for (let Index = 0; Index < UnevenOrders.length; ++Index)
            Tone += UnevenAmplitudes[Index] * Math.sin(FiringPhase * UnevenOrders[Index]);

        // ⑪ Amplitude: radiated pressure follows mass flow, and the pipe adds its resonance on top. The pulse accumulator still shapes the
        //    attack so the note beats with the firing instead of being a static organ tone.
        const Chuff = 0.82 + 0.18 * Math.min(1.0, Accumulator);
        const ToneLevel = (0.28 + 0.72 * LoadSharpen) * Chuff * EventGain * 0.34;
        const BodySignal = Tone * ToneLevel;

        // ⑫ Sub-harmonic boom: the lowest radiation lobe, felt more than heard, present on big-displacement engines.
        const SubSignal = EvaluateOnePole(Voice.SubFilter, PipeInput) * 2.4;

        // ⑬ Radiation path. Muffled and bypassed paths carry the series with their own filters; the bypass opening is the loudest single
        //    event in the whole model and is authored per car in decibels. The direct path carries the un-muffled attack.
        const Direct = EvaluateLowPassPair(Voice.DirectPath, PipeInput) * 0.22;
        const Muffled = EvaluateLowPassPair(Voice.MufflerPath, BodySignal + Resonant * 0.28) * MufflerGain * 1.60;
        const Open = BodySignal + Resonant * 0.35 + SubSignal * 0.22;
        const BypassGate = (0.26 + Bypass * (0.90 + BypassCeiling)) * (0.30 + 0.70 * LoadSharpen);
        const Bypassed = Open * BypassGate;

        // ⑭ Nonlinear radiation through the open end, then the pipe's own high-frequency roll-off. Asymmetry is what puts even harmonics and
        //    the "hard" V8 second order into an otherwise smooth series.
        const PreShaped = Direct + Muffled + Bypassed;
        const Drive = 0.75 + 0.55 * LoadSharpen + 0.90 * Overrun + 1.60 * Math.max(Misfire, Cut);
        const Radiated = ShapePressure(PreShaped * Drive, Asymmetry) * Radiation;

        // ⑮ Electric drive has no exhaust; the block contributes exact zeros so the composite voice never branches per sample.
        const Contribution = Electric ? 0.0 : Radiated;

        Left[Index] += Contribution;
        Right[Index] += Contribution;

        const Magnitude = Contribution < 0.0 ? -Contribution : Contribution;
        if (Magnitude > Peak) Peak = Magnitude;
        PopAccumulator += 0.0;
    }

    Voice.PhaseCursor = Phase;
    Voice.EventCursor = Cursor;
    Voice.PulseAccumulator = Accumulator;
    Voice.CrackleAccumulator = CrackleAccumulator;
    Voice.CrackleEnvelope = CrackleEnvelope;
    Voice.PopAccumulator = PopAccumulator;
    Voice.PopEnvelope = PopEnvelope;
    Voice.RandomSequence = Random;
    Voice.RpmMemory = Rpm;
    Voice.LoadMemory = Load;
    Voice.ThrottleMemory = Throttle;
    Voice.BoostMemory = Boost;
    Voice.BypassMemory = Bypass;
    Voice.MisfireMemory = Misfire;
    Voice.CutMemory = Cut;
    Voice.PeakEstimate = Peak;
    Voice.FiringPhase = FiringPhase;
    return Voice;
}

// 📝 The host raises this when the throttle closes against boost; the voice converts it into a tailpipe event on the next render so the
//    overrun pop and the blow-off can be scheduled from the same instant.
export function TriggerExhaustEvent(Voice, Strength)
{
    Voice.PopEnvelope = Math.min(2.0, Voice.PopEnvelope + Math.max(0.0, Math.min(1.0, Strength)) * 1.4);
    Voice.PulseAccumulator = Math.min(MaximumPulseAccumulator, Voice.PulseAccumulator + 0.5 * Strength);
    return Voice;
}
