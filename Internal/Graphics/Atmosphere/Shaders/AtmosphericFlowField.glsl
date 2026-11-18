// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//                                                  ATMOSPHERIC FLOW FIELD 🧩
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// GPU mirror of AtmosphericFlowField.cpp, shared by every flow-driven field (VolumetricCloudField.glsl, VolumetricFogField.glsl) so clouds and
// fog are moved by ONE wind / force field. Line-for-line port of the C++ namesakes:
//   - std140 FlowFieldBlock (560 B) mirror of FlowFieldUniformBlock
//   - C2 quintic vector potential, finite-difference curl (divergence-free), the six force-field operators, Ekman-veered synoptic wind
//   - classical RK4 backward characteristic and the dual-phase bounded-age advection (triangular cross-fade)
//
// The including shader picks the binding:  #define FLOW_FIELD_BINDING n  (default 1).

#ifndef ATMOSPHERIC_FLOW_FIELD_GLSL
#define ATMOSPHERIC_FLOW_FIELD_GLSL

#ifndef FLOW_FIELD_BINDING
#define FLOW_FIELD_BINDING 1
#endif

const float FlowTau                  = 6.28318530717958647692;
const uint  MaximumFlowSourceSlots   = 8u;
const float AdvectionPeriodSeconds   = 40.0;   // mirrors ResolveFlowDisplacement (caps eye-wall differential rotation at ~1 rad)

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// UNIFORM BLOCK (std140, mirror AtmosphericFlowField.h::FlowFieldUniformBlock 560 B)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

struct FlowSourceEntry
{
    vec4 CentreOuterRadius;    // xyz world centre [m] (+Z up); w outer radius [m]
    vec4 AxisInnerRadius;      // xyz unit axis; w inner eye radius [m]
    vec4 KinematicsPack;       // x peak speed [m/s]; y radial inflow fraction; z vertical lift [m/s]; w vertical half-span [m]
    vec4 ModulationCategory;   // x condensation gain; y wavelength [m]; z category index; w mask bits (bit0 active, bit1 project)
};

layout(std140, binding = FLOW_FIELD_BINDING) uniform FlowFieldBlock
{
    vec4            SynopticVelocityAltitude;   // xyz reference velocity [m/s] (wind-gated); w reference altitude [m]
    vec4            ShearVeerCurlPack;          // x shear exponent; y Ekman veer [rad]; z curl speed [m/s]; w curl wavelength [m]
    vec4            ControlClockPack;           // x force coupling; y sky-veil coupling; z sequence seconds [s]; w active source count
    FlowSourceEntry Sources[MaximumFlowSourceSlots];
} FlowField;

const uint CategoryDirectionalJet     = 0u;
const uint CategoryCyclonicSwirl      = 1u;
const uint CategoryRadialOutflow      = 2u;
const uint CategoryBuoyantUpdraft     = 3u;
const uint CategoryIncompressibleCurl = 4u;
const uint CategoryAtmosphericWave    = 5u;

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// DIVERGENCE-FREE CURL TURBULENCE
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

float EvaluateQuinticFade(float Parameter)
{
    float Clamped = clamp(Parameter, 0.0, 1.0);
    return Clamped * Clamped * Clamped * (Clamped * (Clamped * 6.0 - 15.0) + 10.0);
}

float HashLatticeUint(ivec3 Cell, uint ChannelSeed)
{
    uint Mixed = uint(Cell.x) * 0x27D4EB2Du ^ uint(Cell.y) * 0x165667B1u ^ uint(Cell.z) * 0x9E3779B1u ^ ChannelSeed * 0x85EBCA6Bu;
    Mixed ^= Mixed >> 15u;
    Mixed *= 0x2C1B3C6Du;
    Mixed ^= Mixed >> 12u;
    Mixed *= 0x297A2D39u;
    Mixed ^= Mixed >> 15u;
    return float(Mixed >> 8u) * (2.0 / 16777216.0) - 1.0;
}

// 📝 C2-continuous quintic-interpolated scalar potential component (smooth first and second derivatives for a clean curl).
float EvaluateSmoothPotential(vec3 Coord, uint ChannelSeed)
{
    vec3  Floored = floor(Coord);
    ivec3 Cell    = ivec3(Floored);
    vec3  Weight  = vec3(EvaluateQuinticFade(Coord.x - Floored.x), EvaluateQuinticFade(Coord.y - Floored.y), EvaluateQuinticFade(Coord.z - Floored.z));

    float Corner000 = HashLatticeUint(Cell + ivec3(0, 0, 0), ChannelSeed);
    float Corner100 = HashLatticeUint(Cell + ivec3(1, 0, 0), ChannelSeed);
    float Corner010 = HashLatticeUint(Cell + ivec3(0, 1, 0), ChannelSeed);
    float Corner110 = HashLatticeUint(Cell + ivec3(1, 1, 0), ChannelSeed);
    float Corner001 = HashLatticeUint(Cell + ivec3(0, 0, 1), ChannelSeed);
    float Corner101 = HashLatticeUint(Cell + ivec3(1, 0, 1), ChannelSeed);
    float Corner011 = HashLatticeUint(Cell + ivec3(0, 1, 1), ChannelSeed);
    float Corner111 = HashLatticeUint(Cell + ivec3(1, 1, 1), ChannelSeed);

    float SliceLow  = mix(mix(Corner000, Corner100, Weight.x), mix(Corner010, Corner110, Weight.x), Weight.y);
    float SliceHigh = mix(mix(Corner001, Corner101, Weight.x), mix(Corner011, Corner111, Weight.x), Weight.y);
    return mix(SliceLow, SliceHigh, Weight.z);
}

// 📝 Two-octave domain-rotated vector potential ψ(p) preventing axis-aligned streamlines.
vec3 EvaluateVectorPotential(vec3 Scaled)
{
    vec3 Rotated = vec3( 0.80 * Scaled.x - 0.48 * Scaled.y + 0.36 * Scaled.z,
                         0.48 * Scaled.x + 0.86 * Scaled.y + 0.16 * Scaled.z,
                        -0.36 * Scaled.x + 0.04 * Scaled.y + 0.93 * Scaled.z);
    vec3 Fine    = Rotated * 2.07 + vec3(11.3, -6.7, 4.9);
    return vec3(EvaluateSmoothPotential(Scaled, 1u) + 0.45 * EvaluateSmoothPotential(Fine, 4u),
                EvaluateSmoothPotential(Scaled, 2u) + 0.45 * EvaluateSmoothPotential(Fine, 5u),
                EvaluateSmoothPotential(Scaled, 3u) + 0.45 * EvaluateSmoothPotential(Fine, 6u));
}

// 📝 Central finite-difference curl ∇ × ψ (divergence vanishes identically), world metres (+Z up).
vec3 EvaluateDivergenceFreeCurl3D(vec3 WorldPositionMetres, float WavelengthMetres, float SequenceSeconds)
{
    float InverseScale = 1.0 / max(80.0, WavelengthMetres);
    float TimeDrift    = SequenceSeconds * 0.0045;
    vec3  Sample       = WorldPositionMetres * InverseScale + vec3(TimeDrift, -TimeDrift * 0.71, TimeDrift * 0.37);

    const float Step           = 0.035;
    const float InverseTwoStep = 1.0 / (2.0 * Step);
    vec3 PlusX  = EvaluateVectorPotential(Sample + vec3(Step, 0.0, 0.0));
    vec3 MinusX = EvaluateVectorPotential(Sample - vec3(Step, 0.0, 0.0));
    vec3 PlusY  = EvaluateVectorPotential(Sample + vec3(0.0, Step, 0.0));
    vec3 MinusY = EvaluateVectorPotential(Sample - vec3(0.0, Step, 0.0));
    vec3 PlusZ  = EvaluateVectorPotential(Sample + vec3(0.0, 0.0, Step));
    vec3 MinusZ = EvaluateVectorPotential(Sample - vec3(0.0, 0.0, Step));

    float PotentialZAlongY = (PlusY.z - MinusY.z) * InverseTwoStep;
    float PotentialYAlongZ = (PlusZ.y - MinusZ.y) * InverseTwoStep;
    float PotentialXAlongZ = (PlusZ.x - MinusZ.x) * InverseTwoStep;
    float PotentialZAlongX = (PlusX.z - MinusX.z) * InverseTwoStep;
    float PotentialYAlongX = (PlusX.y - MinusX.y) * InverseTwoStep;
    float PotentialXAlongY = (PlusY.x - MinusY.x) * InverseTwoStep;
    return vec3(PotentialZAlongY - PotentialYAlongZ, PotentialXAlongZ - PotentialZAlongX, PotentialYAlongX - PotentialXAlongY) * 0.55;
}

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// FORCE-FIELD OPERATORS + SYNOPTIC WIND
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

struct FlowVelocitySample
{
    vec3  VelocityMetresPerSec;
    float CondensationDelta;
    float SwirlIntensity;
};

// 📝 One force-field operator with C2-smooth radial and vertical envelopes (mirror of AccumulateFlowSource).
void AccumulateFlowSource(FlowSourceEntry Source, vec3 PositionMetres, float SequenceSeconds, float MasterGain, inout FlowVelocitySample Sample)
{
    uint MaskBits = uint(Source.ModulationCategory.w + 0.5);
    if ((MaskBits & 1u) == 0u || MasterGain <= 1.0e-5)
        return;

    float OuterRadius    = max(50.0, Source.CentreOuterRadius.w);
    float InnerRadius    = clamp(Source.AxisInnerRadius.w, 15.0, OuterRadius * 0.72);
    float VerticalSpan   = max(100.0, Source.KinematicsPack.w);
    float PeakSpeed      = Source.KinematicsPack.x;
    float InflowFraction = Source.KinematicsPack.y;
    float LiftSpeed      = Source.KinematicsPack.z;
    float Condensation   = Source.ModulationCategory.x;
    float WaveLength     = max(150.0, Source.ModulationCategory.y);
    uint  Category       = uint(Source.ModulationCategory.z + 0.5);
    vec3  PrimaryAxis    = Source.AxisInnerRadius.xyz;

    vec3  Offset         = PositionMetres - Source.CentreOuterRadius.xyz;
    float RadialSquared  = Offset.x * Offset.x + Offset.y * Offset.y;
    float RadialDistance = sqrt(RadialSquared);
    if (RadialDistance >= OuterRadius)
        return;

    float EffectiveSpan    = ((MaskBits & 2u) != 0u) ? VerticalSpan * 3.5 : VerticalSpan;
    float VerticalFraction = abs(Offset.z) / EffectiveSpan;
    if (VerticalFraction >= 1.0)
        return;

    float Envelope      = (1.0 - EvaluateQuinticFade(RadialDistance / OuterRadius)) * (1.0 - EvaluateQuinticFade(VerticalFraction)) * MasterGain;
    float InverseRadial = (RadialDistance > 1.0e-3) ? 1.0 / RadialDistance : 0.0;
    vec2  Radial        = Offset.xy * InverseRadial;
    vec2  Tangent       = vec2(-Radial.y, Radial.x);

    if (Category == CategoryDirectionalJet)
    {
        vec3  JetAxis    = normalize(PrimaryAxis);
        float AlongJet   = Offset.x * JetAxis.x + Offset.y * JetAxis.y;
        vec2  Cross      = Offset.xy - JetAxis.xy * AlongJet;
        float CrossWidth = max(80.0, InnerRadius * 1.45);
        float Corridor   = exp(-dot(Cross, Cross) / (CrossWidth * CrossWidth));
        float Speed      = PeakSpeed * Envelope * (0.28 + 0.72 * Corridor);
        Sample.VelocityMetresPerSec += vec3(JetAxis.xy * Speed, JetAxis.z * Speed + LiftSpeed * Envelope * Corridor * 0.35);
        Sample.CondensationDelta    += Condensation * Envelope * (Corridor * 0.55 - 0.12);
        Sample.SwirlIntensity       += abs(Speed) / CrossWidth;
    }
    else if (Category == CategoryCyclonicSwirl)
    {
        float SpinSign          = (PrimaryAxis.z < 0.0) ? -1.0 : 1.0;
        float EyeRatio          = RadialDistance / InnerRadius;
        float RankineCurve      = (2.0 * EyeRatio) / (1.0 + EyeRatio * EyeRatio);
        float SwirlSpeed        = PeakSpeed * RankineCurve * Envelope * SpinSign;
        float InflowMask        = clamp((EyeRatio - 0.55) / 0.85, 0.0, 1.0);
        float InflowSpeed       = -abs(PeakSpeed) * InflowFraction * InflowMask * Envelope;
        float EyeInterior       = exp(-(EyeRatio * EyeRatio) * 1.45);
        float EyewallDifference = EyeRatio - 1.18;
        float EyewallRing       = exp(-(EyewallDifference * EyewallDifference) / 0.34);
        float Azimuth           = atan(Offset.y, Offset.x);
        float SpiralPhase       = 2.0 * Azimuth + 2.6 * SpinSign * log(max(0.22, EyeRatio)) - SpinSign * SequenceSeconds * 0.012;
        float SpiralBand        = cos(SpiralPhase) * clamp((EyeRatio - 0.75) / 0.80, 0.0, 1.0);
        Sample.VelocityMetresPerSec += vec3(Tangent * SwirlSpeed + Radial * InflowSpeed, LiftSpeed * Envelope * (EyewallRing - 1.25 * EyeInterior));
        Sample.CondensationDelta    += Condensation * Envelope * (0.85 * EyewallRing + 0.38 * SpiralBand - 1.65 * EyeInterior);
        Sample.SwirlIntensity       += abs(SwirlSpeed) / InnerRadius;
    }
    else if (Category == CategoryRadialOutflow)
    {
        float EyeRatio    = RadialDistance / InnerRadius;
        float OutflowRamp = (1.85 * EyeRatio) / (0.65 + EyeRatio * EyeRatio);
        float RadialSpeed = PeakSpeed * OutflowRamp * Envelope;
        float ClearedEye  = exp(-(EyeRatio * EyeRatio) * 0.88);
        float RingFront   = RadialDistance / OuterRadius - 0.56;
        float ArcusRing   = exp(-(RingFront * RingFront) / 0.045);
        Sample.VelocityMetresPerSec += vec3(Radial * RadialSpeed, LiftSpeed * Envelope * (ArcusRing * 0.9 - ClearedEye * 1.1));
        Sample.CondensationDelta    += Condensation * Envelope * (0.95 * ArcusRing - 1.55 * ClearedEye);
        Sample.SwirlIntensity       += abs(RadialSpeed) / OuterRadius;
    }
    else if (Category == CategoryBuoyantUpdraft)
    {
        float PlumeAxial  = exp(-RadialSquared / (InnerRadius * InnerRadius * 1.35));
        float HeightSign  = clamp(Offset.z / VerticalSpan, -1.0, 1.0);
        float DivergeRate = PeakSpeed * InflowFraction * HeightSign * Envelope;
        float AscentSpeed = LiftSpeed * PlumeAxial * Envelope;
        Sample.VelocityMetresPerSec += vec3(Radial * DivergeRate, AscentSpeed);
        Sample.CondensationDelta    += Condensation * PlumeAxial * Envelope;
        Sample.SwirlIntensity       += abs(AscentSpeed) / InnerRadius;
    }
    else if (Category == CategoryIncompressibleCurl)
    {
        vec3  EddyVelocity = EvaluateDivergenceFreeCurl3D(PositionMetres, WaveLength, SequenceSeconds);
        float Amplitude    = PeakSpeed * Envelope;
        Sample.VelocityMetresPerSec += EddyVelocity * Amplitude * vec3(1.0, 1.0, 0.45);
        Sample.SwirlIntensity       += abs(Amplitude) / WaveLength;
    }
    else if (Category == CategoryAtmosphericWave)
    {
        vec3  WaveAxis   = normalize(PrimaryAxis);
        float WaveNumber = FlowTau / max(250.0, WaveLength);
        float AlongAxis  = Offset.x * WaveAxis.x + Offset.y * WaveAxis.y;
        float Crest      = sin(AlongAxis * WaveNumber - SequenceSeconds * (PeakSpeed * WaveNumber));
        Sample.VelocityMetresPerSec += vec3(WaveAxis.xy * PeakSpeed * 0.45 * Envelope, LiftSpeed * Crest * Envelope);
        Sample.CondensationDelta    += Condensation * Crest * Envelope;
        Sample.SwirlIntensity       += abs(LiftSpeed * WaveNumber * Envelope);
    }
}

// 📝 Instantaneous Eulerian velocity: Ekman-veered power-law synoptic wind + curl turbulence + all active force-field operators.
FlowVelocitySample EvaluateAtmosphericFlow(vec3 WorldPositionMetres, float SequenceSeconds)
{
    FlowVelocitySample Result = FlowVelocitySample(vec3(0.0), 0.0, 0.0);

    float Altitude          = max(10.0, WorldPositionMetres.z);
    float ReferenceAltitude = max(50.0, FlowField.SynopticVelocityAltitude.w);
    float SpeedProfile      = clamp(pow(Altitude / ReferenceAltitude, FlowField.ShearVeerCurlPack.x), 0.25, 2.40);
    float VeerAngle         = clamp((Altitude - ReferenceAltitude) / 6000.0, -0.5, 1.2) * FlowField.ShearVeerCurlPack.y;
    float CosineVeer        = cos(VeerAngle);
    float SineVeer          = sin(VeerAngle);
    vec3  Reference         = FlowField.SynopticVelocityAltitude.xyz;
    Result.VelocityMetresPerSec += vec3((Reference.x * CosineVeer - Reference.y * SineVeer) * SpeedProfile,
                                        (Reference.x * SineVeer + Reference.y * CosineVeer) * SpeedProfile,
                                        Reference.z);
    if (FlowField.ShearVeerCurlPack.z > 1.0e-4)
    {
        vec3 Curl = EvaluateDivergenceFreeCurl3D(WorldPositionMetres, FlowField.ShearVeerCurlPack.w, SequenceSeconds);
        Result.VelocityMetresPerSec += Curl * FlowField.ShearVeerCurlPack.z * vec3(1.0, 1.0, 0.30);
    }

    uint ActiveSlots = min(uint(FlowField.ControlClockPack.w + 0.5), MaximumFlowSourceSlots);
    for (uint Index = 0u; Index < ActiveSlots; ++Index)
        AccumulateFlowSource(FlowField.Sources[Index], WorldPositionMetres, SequenceSeconds, FlowField.ControlClockPack.x, Result);
    return Result;
}

struct FlowAdvectedSample
{
    vec3  SourcePositionMetres;
    float CondensationDelta;
};

// 📝 Classical RK4 backward streamline solve dX/ds = -u(X, t - s), s in [0, SpanSeconds] (mirror of TraceFlowCharacteristicBackward).
//    The C++ MaximumAdvectionSpanSeconds clamp (600 s) is omitted: the only caller, ResolveFlowDisplacement, passes phase ages < 40 s.
FlowAdvectedSample TraceFlowCharacteristicBackward(vec3 WorldPositionMetres, float SpanSeconds, uint SubstepCount)
{
    FlowVelocitySample Instant = EvaluateAtmosphericFlow(WorldPositionMetres, SpanSeconds);
    FlowAdvectedSample Advected = FlowAdvectedSample(WorldPositionMetres, Instant.CondensationDelta);
    if (abs(SpanSeconds) <= 1.0e-4)
        return Advected;

    uint  Steps                   = clamp(SubstepCount, 1u, 16u);
    float StepSeconds             = SpanSeconds / float(Steps);
    vec3  ParcelCoord             = WorldPositionMetres;
    float TimeCursor              = SpanSeconds;
    float CondensationAccumulated = 0.5 * Instant.CondensationDelta;
    for (uint StepIndex = 0u; StepIndex < Steps; ++StepIndex)
    {
        FlowVelocitySample SlopeFirst  = EvaluateAtmosphericFlow(ParcelCoord, TimeCursor);
        FlowVelocitySample SlopeSecond = EvaluateAtmosphericFlow(ParcelCoord - SlopeFirst.VelocityMetresPerSec * (0.5 * StepSeconds), TimeCursor - 0.5 * StepSeconds);
        FlowVelocitySample SlopeThird  = EvaluateAtmosphericFlow(ParcelCoord - SlopeSecond.VelocityMetresPerSec * (0.5 * StepSeconds), TimeCursor - 0.5 * StepSeconds);
        FlowVelocitySample SlopeFourth = EvaluateAtmosphericFlow(ParcelCoord - SlopeThird.VelocityMetresPerSec * StepSeconds, TimeCursor - StepSeconds);
        vec3 WeightedVelocity = (SlopeFirst.VelocityMetresPerSec + 2.0 * SlopeSecond.VelocityMetresPerSec
                               + 2.0 * SlopeThird.VelocityMetresPerSec + SlopeFourth.VelocityMetresPerSec) * (1.0 / 6.0);
        ParcelCoord             -= WeightedVelocity * StepSeconds;
        TimeCursor              -= StepSeconds;
        CondensationAccumulated += SlopeSecond.CondensationDelta * (0.5 / float(Steps));
    }
    float RampIn = clamp(abs(SpanSeconds) / 45.0, 0.25, 1.0);
    Advected.SourcePositionMetres = ParcelCoord;
    Advected.CondensationDelta    = clamp((0.65 * Instant.CondensationDelta + 0.35 * CondensationAccumulated) * RampIn, -1.5, 1.5);
    return Advected;
}

// 📝 Dual-phase bounded-age advection: two characteristics of age ≤ P (offset P/2) cross-faded with triangular weights. Returns the
//    horizontal displacement [km] in xy and the condensation coupling in z (mirror of ResolveFlowDisplacement).
vec3 ResolveFlowDisplacement(vec3 PlanetPositionKm, float AltitudeKm, float SequenceSeconds)
{
    vec3  WorldMetres  = vec3(PlanetPositionKm.x, PlanetPositionKm.z, AltitudeKm) * 1000.0;
    float Elapsed      = max(0.0, SequenceSeconds);
    float PhaseFirst   = mod(Elapsed, AdvectionPeriodSeconds);
    float PhaseSecond  = mod(Elapsed + 0.5 * AdvectionPeriodSeconds, AdvectionPeriodSeconds);
    float WeightFirst  = 1.0 - abs(2.0 * PhaseFirst / AdvectionPeriodSeconds - 1.0);
    float WeightSecond = 1.0 - WeightFirst;
    FlowAdvectedSample First  = TraceFlowCharacteristicBackward(WorldMetres, PhaseFirst, 4u);
    FlowAdvectedSample Second = TraceFlowCharacteristicBackward(WorldMetres, PhaseSecond, 4u);
    vec2  Displacement = ((First.SourcePositionMetres.xy - WorldMetres.xy) * WeightFirst
                        + (Second.SourcePositionMetres.xy - WorldMetres.xy) * WeightSecond) * 0.001;
    float Instant      = EvaluateAtmosphericFlow(WorldMetres, SequenceSeconds).CondensationDelta;
    float Condensation = clamp(0.5 * (First.CondensationDelta + Second.CondensationDelta) + 0.5 * Instant, -1.0, 1.0);
    return vec3(Displacement, Condensation);
}

#endif
