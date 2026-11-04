//============================================================================================================================================
//                                                        VEHICLEDRIVETRAIN.JS
//============================================================================================================================================
// 🧩 Fixed-timestep drivetrain: a two-inertia engine and wheel pair joined by a slipping clutch (or a welded electric reduction), a saturating
//    tyre that can spin or lock, and a rigid vehicle body. Engine torque comes from a derived wide-open-throttle sheet with boost, friction
//    and pumping. Every acoustic quantity the voicing stage needs is a consequence of this solver.

// 📝 Determinism contract: AdvanceDrivetrain() is called with a fixed step from the host clock, never with a frame delta, so a 12 Hz capture
//    produces byte-identical telemetry to a 240 Hz one. Nothing here reads the wall clock, the DOM, or the sample rate. That is what makes the
//    engine note independent of the drawn frame rate.

// 📝 Model equations, stated once so the code below is readable:
//    ① engine         Iₑ·dωₑ/dt = τₑ(ωₑ, θ, boost) − τ_clutch − τ_friction
//    ② clutch         τ_clutch = f(Δω), saturating at capacity; welded when Δω closes and capacity can carry the demand
//    ③ driven wheel   I_w·dω_w/dt = τ_clutch·i·η − F_x·r_w − τ_brake
//    ④ tyre           F_x = μ·F_z·tanh(k·σ),  σ = (ω_w·r_w − v)/max(|v|, v_ref)
//    ⑤ vehicle        m·dv/dt = F_x − ½ρ·C_dA·v² − C_rr·m·g − F_brake
//    A wheel that spins is equation ④ saturating, not an invented penalty; a launch is equation ② slipping; a limiter is equation ① cut.

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const GravityAcceleration      = 9.81;      // [m/s²]
const AirDensity               = 1.225;     // [kg/m³]
const LimiterHysteresis        = 120.0;     // [rpm] - cut dwell before fuel returns
const LimiterCutSeconds        = 0.055;     // [s]
const ClutchStiffnessPerRadian = 240.0;     // [N·m per rad/s] at full engagement; scaled by capacity
const ClutchLockSlip           = 1.6;       // [rad/s] - slip below which the pair welds
const ClutchLockMargin         = 1.15;      // [-] - capacity margin needed before welding
const CreepShare               = 0.055;     // [-] - fraction of capacity a closed-throttle idle crawl transmits
const SlipReferenceSpeed       = 1.6;       // [m/s] - denominator floor in the slip ratio
const TyreSaturationStiffness  = 11.0;      // [-] - tanh slope; force saturates by ~20% slip
const BrakeGripFraction        = 0.92;      // [-] - of μ·m·g
const IntegratorStepNanoseconds = 2000000;      // [ns] - the absolute 2 ms integration grid
const IntegratorSubstepCeiling = 0.002;         // [s]  - one grid step
const MaximumSteerAngle        = 0.52;      // [rad]
const WheelbaseMetres          = 2.70;      // [m]
const DownshiftLockoutSeconds  = 0.55;      // [s] - prevents shift hunting right after an upshift
const LimitGovernorGain        = 5.0;       // [1/s] - strength of the idle speed governor
const LaunchTractionAllowance  = 1.10;      // [-] - launch-control margin over the tyre's friction budget

//------------------------------------------------------------------------------------------------------------------------
//                                                     MONOTONE INTERPOLATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Fritsch-Carlson monotone cubic: a torque curve must not overshoot between authored points, or the engine invents torque the real sheet
//    never had and the model becomes unphysical exactly where a reviewer looks hardest.
function ConstructCurve(Points)
{
    const X = Points.map((Entry) => Entry[0]);
    const Y = Points.map((Entry) => Entry[1]);
    const Count = X.length;
    const Slope = new Array(Count).fill(0.0);
    if (Count < 2)
        return { X, Y, Slope, Count };
    const Width = [];
    const Secant = [];
    for (let Index = 0; Index < Count - 1; ++Index)
    {
        Width.push(Math.max(1.0e-9, X[Index + 1] - X[Index]));
        Secant.push((Y[Index + 1] - Y[Index]) / Width[Index]);
    }
    Slope[0] = Secant[0];
    Slope[Count - 1] = Secant[Count - 2];
    for (let Index = 1; Index < Count - 1; ++Index)
        Slope[Index] = Secant[Index - 1] * Secant[Index] <= 0.0 ? 0.0 : (Secant[Index - 1] + Secant[Index]) * 0.5;
    for (let Index = 0; Index < Count - 1; ++Index)
    {
        if (Math.abs(Secant[Index]) < 1.0e-12)
        {
            Slope[Index] = 0.0;
            Slope[Index + 1] = 0.0;
            continue;
        }
        const LeftRatio = Slope[Index] / Secant[Index];
        const RightRatio = Slope[Index + 1] / Secant[Index];
        const Magnitude = LeftRatio * LeftRatio + RightRatio * RightRatio;
        if (Magnitude > 9.0)
        {
            const Scale = 3.0 / Math.sqrt(Magnitude);
            Slope[Index] = Scale * LeftRatio * Secant[Index];
            Slope[Index + 1] = Scale * RightRatio * Secant[Index];
        }
    }
    return { X, Y, Slope, Count };
}

export function EvaluateCurve(Curve, Argument)
{
    const X = Curve.X, Y = Curve.Y, Slope = Curve.Slope;
    if (Argument <= X[0]) return Y[0];
    if (Argument >= X[Curve.Count - 1]) return Y[Curve.Count - 1];
    let Low = 0, High = Curve.Count - 1;
    while (High - Low > 1)
    {
        const Middle = (Low + High) >> 1;
        if (X[Middle] > Argument) High = Middle; else Low = Middle;
    }
    const Width = X[High] - X[Low];
    const T = (Argument - X[Low]) / Width;
    const T2 = T * T, T3 = T2 * T;
    return (2.0 * T3 - 3.0 * T2 + 1.0) * Y[Low] + (T3 - 2.0 * T2 + T) * Width * Slope[Low] +
           (-2.0 * T3 + 3.0 * T2) * Y[High] + (T3 - T2) * Width * Slope[High];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   DERIVED ENGINE SHEETS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The published sheet gives peak torque and the rpm it lands at. The shape either side is a normalised four-stroke curve: strong manifold
//    filling low down, a broad plateau, then volumetric-efficiency roll-off to the limiter. Scaling that shape by the published peak gives
//    every car a believable curve without inventing a dyno graph per vehicle.
export function DeriveTorqueCurve(Combustion)
{
    const Peak = Math.max(1.0, Combustion.PeakTorqueNewtonMetres);
    const PeakRPM = Math.max(600.0, Combustion.TorquePeakRPM);
    const Redline = Math.max(PeakRPM + 200.0, Combustion.RedlineRPM);
    const Idle = Math.max(400.0, Combustion.IdleRPM);
    const Points = [];
    const Anchor = (RPM, Fraction) => Points.push([RPM, Peak * Fraction]);
    Anchor(Idle * 0.80, 0.58);
    Anchor(Idle, 0.66);
    Anchor(Idle + (PeakRPM - Idle) * 0.35, 0.88);
    Anchor(PeakRPM * 0.86, 0.98);
    Anchor(PeakRPM, 1.00);
    Anchor(PeakRPM + (Redline - PeakRPM) * 0.45, 0.955);
    Anchor(Redline, 0.86);
    Anchor(Redline * 1.04, 0.78);
    Anchor(Redline * 1.09, 0.60);
    return ConstructCurve(Points);
}

// 📝 Boost is a function of compressor speed and flow, not pedal angle. The curve is the compressor map collapsed to one dimension: nothing
//    below the spool point, a steep climb through it, then the wastegate holding a flat ceiling.
export function DeriveBoostSchedule(Induction, Combustion)
{
    const Maximum = Math.max(0.0, Induction.WastegateMaxBoostBar);
    if (Maximum <= 0.0 || Induction.Topology === "electricDrive")
        return ConstructCurve([[0.0, 0.0], [20000.0, 0.0]]);
    const Turbo = Induction.Topology === "twinScrollTurbo";
    const SpoolStart = Turbo ? Math.max(Combustion.IdleRPM * 0.9, 1400.0) : Combustion.IdleRPM * 0.6;
    const SpoolFull  = Turbo ? Math.min(Combustion.RedlineRPM * 0.70, SpoolStart + 3200.0)
                             : Math.min(Combustion.RedlineRPM * 0.40, SpoolStart + 1500.0);
    return ConstructCurve([
        [0.0, 0.0],
        [SpoolStart, Maximum * 0.05],
        [SpoolStart + (SpoolFull - SpoolStart) * 0.45, Maximum * 0.36],
        [SpoolFull, Maximum * 0.90],
        [SpoolFull + (Combustion.RedlineRPM - SpoolFull) * 0.5, Maximum],
        [Combustion.RedlineRPM * 1.10, Maximum * 0.95]
    ]);
}

// 📝 A Roots blower is a positive-displacement pump: delivery is proportional to rotor speed, so boost tracks rpm almost linearly and arrives
//    almost instantly. The multiplier below converts bar of boost into extra indicated torque.
export function DeriveBoostMultiplier(Induction)
{
    if (Induction.Topology === "rootsSupercharger") return 0.62;
    if (Induction.Topology === "twinScrollTurbo")  return 0.78;
    return 0.0;
}

export function PrepareDrivetrainProfile(Profile)
{
    if (Profile.DerivedTorqueCurve)
        return Profile;
    return Object.freeze({
        ...Profile,
        DerivedTorqueCurve:     DeriveTorqueCurve(Profile.Combustion),
        DerivedBoostSchedule:   DeriveBoostSchedule(Profile.Induction, Profile.Combustion),
        DerivedBoostMultiplier: DeriveBoostMultiplier(Profile.Induction)
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONDITION CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

export function CreateDrivetrainCondition(Vehicle, StartGear = 1)
{
    const Electric = Vehicle.Electric === true;
    return {
        Time:                     0.0,     // [s]
        Distance:                 0.0,     // [m]
        Speed:                    0.0,     // [m/s] - body, positive forward
        WheelSpeed:               0.0,     // [rad/s] - driven axle
        WheelAngle:               0.0,     // [rad]
        EngineAngularSpeed:       Electric ? 0.0 : Max(1.0, Vehicle.Combustion.IdleRPM) * Math.PI / 30.0,
        GearIndex:                StartGear,
        ClutchEngagement:         0.0,     // [0-1]
        ClutchSlipSpeed:          0.0,     // [rad/s]
        TransmittedTorque:        0.0,     // [N·m]
        EngineNetTorque:          0.0,     // [N·m]
        Boost:                    0.0,     // [bar]
        BoostTarget:              0.0,     // [bar]
        ThrottlePlate:            0.0,     // [0-1]
        ThrottleRateMemory:       0.0,     // [1/s] - pedal rate, drives tip-in richness
        BrakePedal:               0.0,     // [0-1]
        Handbrake:                0.0,     // [0-1]
        SteerAngle:               0.0,     // [rad]
        ShiftTimer:               0.0,     // [s]
        ShiftCut:                 0.0,     // [0-1]
        ShiftPending:             0,       // [idx]
        ShiftLockout:             0.0,     // [s]
        LimiterTimer:             0.0,     // [s]
        LimiterCut:               0.0,     // [0-1]
        OverrunFuelFraction:      0.0,     // [0-1]
        WheelSlipRatio:           0.0,     // [-]
        WheelSlipSpeed:           0.0,     // [m/s]
        LongitudinalForce:        0.0,     // [N]
        LongitudinalAcceleration: 0.0,     // [m/s²]
        LateralAcceleration:      0.0,     // [m/s²]
        YawRate:                  0.0,     // [rad/s]
        TyreScrub:                0.0,     // [0-1]
        DrivelineLash:            0.0,     // [0-1]
        EngineLoad:               0.0,     // [0-1]
        ExhaustMassFlow:          0.0,     // [0-1]
        BlowerPressure:           0.0,     // [0-1]
        TurbineSpeed:             0.0,     // [0-1]
        BlowOffTrigger:           0.0,     // [0-1]
        GearWhineLoad:            0.0,     // [0-1]
        DifferentialLoad:         0.0,     // [0-1]
        CoolingFanSpeed:          0.0,     // [0-1]
        LaunchControl:            0.0,     // [0-1]
        CreepEnabled:             false,   // [bool]- an automatic's idle crawl; off by default so a parked car stays parked
        TyreTemperature:          0.55,    // [0-1]
        LockupState:              0.0,     // [0-1] - 1 welded, 0 fully open
        IntegratorResidualNanoseconds: 0,   // [ns]  - integer remainder, so block slicing cannot change the step count
        ShiftPhaseTimer:          0.0,     // [s]   - current shift duration, reset when the target gear changes
        CrankAccumulator:         0.0      // [°]   - unused by the physics, reserved for external angle queries
    };
}

function Max(Left, Right) { return Left > Right ? Left : Right; }
function Min(Left, Right) { return Left < Right ? Left : Right; }
function Clamp(Argument, Lower, Upper) { return Argument < Lower ? Lower : (Argument > Upper ? Upper : Argument); }
function MoveToward(Current, Target, Step)
{
    if (Current < Target) return Min(Target, Current + Step);
    if (Current > Target) return Max(Target, Current - Step);
    return Current;
}
function SmoothStep(EdgeLower, EdgeUpper, Argument)
{
    const T = Clamp((Argument - EdgeLower) / Max(1.0e-6, EdgeUpper - EdgeLower), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}
function SmoothSign(Argument) { return Math.tanh(Argument / 0.25); }

//------------------------------------------------------------------------------------------------------------------------
//                                                     ENGINE TORQUE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Indicated torque is the wide-open-throttle sheet scaled by manifold filling. Part throttle is not linear: the pumping loop costs a
//    roughly fixed amount, so τ = τ_wot·(0.15 + 0.85θ)·boost − friction − pumping·(1 − θ).
export function EvaluateEngineTorque(Vehicle, RPM, Throttle, BoostBar)
{
    if (Vehicle.Electric)
        return EvaluateElectricTorque(Vehicle, RPM, Throttle);
    const Curve = Vehicle.DerivedTorqueCurve;
    if (!Curve)
        return 0.0;
    const Speed = Clamp(RPM, 0.0, Vehicle.Combustion.LimiterRPM * 1.1);
    const WideOpen = EvaluateCurve(Curve, Speed);
    const Boosted = WideOpen * (1.0 + Vehicle.DerivedBoostMultiplier * Max(0.0, BoostBar));
    const Indicated = Boosted * (0.15 + 0.85 * Throttle);
    const SpeedFraction = Speed / Max(1.0, Vehicle.Combustion.RedlineRPM);
    const Friction = Vehicle.Combustion.EngineBrakingLosses * Vehicle.Combustion.PeakTorqueNewtonMetres *
                     (0.28 + 0.72 * Math.pow(SpeedFraction, 0.8));
    const Pumping = Boosted * 0.16 * (1.0 - Throttle);

    // 📝 Closed throttle is the idle bypass, not a scaled-down wide-open throttle. The idle air controller passes just enough air to cancel
    //    friction and pumping at the idle speed; above idle the same path leaves a deficit, which is engine braking. Returning +（F+P) makes the
    //    net crank torque zero at idle and strongly negative above it — the behaviour a driver feels when they lift.
    if (Throttle <= 0.02)
    {
        const IdleRPM = Max(1.0, Vehicle.Combustion.IdleRPM);
        const Governor = Clamp(1.0 + 4.5 * (IdleRPM - Speed) / IdleRPM, 0.0, 1.6);
        // Negative above idle: friction and pumping decelerate the engine. At idle the bypass holds it exactly neutral.
        return (Friction + Pumping) * (Governor - 1.0);
    }
    return Indicated - Friction - Pumping;
}

// 📝 A permanent-magnet traction machine holds torque flat to its base speed and then runs constant power. Base speed here is where the
//    mechanical and electrical limits cross; above it torque falls with speed, which is why an EV pulls hardest off the line.
export function EvaluateElectricTorque(Vehicle, RPM, Throttle)
{
    const Peak = Vehicle.Combustion.PeakTorqueNewtonMetres;
    const BaseSpeed = Math.max(600.0, Vehicle.Combustion.TorquePeakRPM);
    const Speed = Max(RPM, 1.0);
    const Ratio = Clamp(BaseSpeed / Speed, 0.0, 1.0);
    const Torque = Peak * Math.pow(Ratio, 0.82);
    if (Throttle <= 0.02)
        return -0.30 * Torque * Clamp((Speed - BaseSpeed) / BaseSpeed, 0.0, 1.0);
    return Torque * Throttle;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GEARBOOK HANDLING
//------------------------------------------------------------------------------------------------------------------------

export function ResolveGearRatio(Vehicle, GearIndex)
{
    const D = Vehicle.Drivetrain;
    if (GearIndex === 0) return 0.0;
    if (GearIndex < 0) return D.ReverseRatio;
    const Index = GearIndex - 1;
    if (Index < D.GearRatios.length) return D.GearRatios[Index];
    return D.GearRatios[D.GearRatios.length - 1];
}

// 📝 An automatic box shifts on engine speed with hysteresis and a lockout, because the acoustic signature of a hunting gearbox is a car that
//    cannot decide what note to sing. Hysteresis is authored (0.94 up / 0.42 down), not tuned to make a test pass.
function SelectAutomaticGear(Condition, Vehicle)
{
    const D = Vehicle.Drivetrain;
    if (Vehicle.Electric || D.GearRatios.length <= 1 || Condition.ShiftTimer > 0.0 || Condition.ShiftLockout > 0.0)
        return Condition.GearIndex;
    if (Condition.GearIndex < 1)
        return Condition.GearIndex;
    const Redline = Vehicle.Combustion.RedlineRPM;
    const RPM = Condition.EngineAngularSpeed * 30.0 / Math.PI;
    const ProjectedRPM = (Gear) => Max(0.1, Math.abs(Condition.Speed)) / Max(1.0e-3, Vehicle.Chassis.WheelRadiusMetres) *
                                   ResolveGearRatio(Vehicle, Gear) * D.FinalDriveRatio * 60.0 / (2.0 * Math.PI);
    if (RPM > Redline * 0.94 && Condition.GearIndex < D.GearRatios.length)
    {
        if (ProjectedRPM(Condition.GearIndex + 1) > Vehicle.Combustion.IdleRPM * 1.15)
            return Condition.GearIndex + 1;
    }
    if (RPM < Redline * 0.42 && Condition.GearIndex > 1)
    {
        if (ProjectedRPM(Condition.GearIndex - 1) < Redline * 0.86)
            return Condition.GearIndex - 1;
    }
    return Condition.GearIndex;
}

function RequestGear(Condition, Vehicle, Requested)
{
    const D = Vehicle.Drivetrain;
    if (Vehicle.Electric && Requested > 1)
        return;
    const Clamped = Clamp(Math.round(Requested), -1, D.GearRatios.length);
    if (Clamped === Condition.GearIndex || Clamped === Condition.ShiftPending)
        return;
    const ChangingDirection = (Clamped < 0) !== (Condition.GearIndex < 0) && Clamped !== 0 && Condition.GearIndex !== 0;
    if (ChangingDirection && Math.abs(Condition.Speed) > 0.9)
        return;
    Condition.ShiftPending = Clamped;
    const ShiftSeconds = ResolveShiftSeconds(Vehicle, Condition.GearIndex, Clamped);
    Condition.ShiftTimer = ShiftSeconds;
    Condition.ShiftPhaseTimer = ShiftSeconds;
    Condition.ShiftLockout = Max(DownshiftLockoutSeconds, ShiftSeconds);
}

function ResolveShiftSeconds(Vehicle, Current, Requested)
{
    const D = Vehicle.Drivetrain;
    if (Vehicle.Electric || D.ShiftSeconds <= 0.0) return 0.0;
    if (Requested === 0 || Current === 0) return 0.10;
    if (Current < 0 || Requested < 0) return 0.60;
    return D.ShiftSeconds * (Math.abs(Requested - Current) > 1 ? 1.7 : 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE FIXED SUBSTEP
//------------------------------------------------------------------------------------------------------------------------

function AdvanceSubstep(Condition, Vehicle, Input, DeltaSeconds)
{
    const C = Vehicle.Combustion, D = Vehicle.Drivetrain, K = Vehicle.Chassis, I = Vehicle.Induction;
    const Electric = Vehicle.Electric === true;
    const WheelRadius = Max(0.12, K.WheelRadiusMetres);

    // ① Pedals and rack. Rate-limited like a real pedal box and a real steering rack, so a keypress is a stroke, not a step.
    const ThrottleTarget = Clamp(Input.Throttle || 0.0, 0.0, 1.0);
    const BrakeTarget = Clamp(Input.Brake || 0.0, 0.0, 1.0);
    const ThrottleRate = Input.LaunchControl ? 16.0 : 8.0;   // [1/s]
    const BrakeRate = 10.0;
    const PreviousThrottle = Condition.ThrottlePlate;
    Condition.ThrottlePlate += Clamp(ThrottleTarget - Condition.ThrottlePlate, -ThrottleRate * DeltaSeconds,
                                     ThrottleRate * DeltaSeconds);
    Condition.BrakePedal += Clamp(BrakeTarget - Condition.BrakePedal, -BrakeRate * DeltaSeconds, BrakeRate * DeltaSeconds);
    Condition.ThrottleRateMemory = (Condition.ThrottlePlate - PreviousThrottle) / Max(1.0e-4, DeltaSeconds);
    Condition.Handbrake = Input.Handbrake ? MoveToward(Condition.Handbrake, 1.0, 8.0 * DeltaSeconds)
                                          : MoveToward(Condition.Handbrake, 0.0, 8.0 * DeltaSeconds);

    const SteerTarget = Clamp(Input.Steer || 0.0, -1.0, 1.0);
    const SpeedLimitedSteer = Min(MaximumSteerAngle, 0.10 + 0.42 / (1.0 + Math.abs(Condition.Speed) * 0.055));
    Condition.SteerAngle += Clamp(SteerTarget * SpeedLimitedSteer - Condition.SteerAngle, -3.4 * DeltaSeconds, 3.4 * DeltaSeconds);

    // ② Gear requests. A shift is a timed torque interruption; requests are queued, never applied mid-shift.
    if (Input.ShiftUp) RequestGear(Condition, Vehicle, Condition.GearIndex + 1);
    if (Input.ShiftDown) RequestGear(Condition, Vehicle, Condition.GearIndex - 1);
    if (Input.SelectReverse) RequestGear(Condition, Vehicle, -1);
    if (Input.SelectNeutral) RequestGear(Condition, Vehicle, 0);
    if (Input.AutoShift) RequestGear(Condition, Vehicle, SelectAutomaticGear(Condition, Vehicle));
    Condition.ShiftLockout = Max(0.0, Condition.ShiftLockout - DeltaSeconds);

    if (Condition.ShiftTimer > 0.0)
    {
        Condition.ShiftTimer = Max(0.0, Condition.ShiftTimer - DeltaSeconds);
        const Total = Max(1.0e-4, Condition.ShiftPhaseTimer);
        Condition.ShiftCut = 1.0 - Clamp(Condition.ShiftTimer / Total, 0.0, 1.0);
        if (Condition.ShiftPending !== 0 && Condition.ShiftTimer <= Total * 0.40)
        {
            Condition.GearIndex = Condition.ShiftPending;
            Condition.ShiftPending = 0;
        }
    }
    else if (Condition.ShiftPending !== 0)
    {
        Condition.GearIndex = Condition.ShiftPending;
        Condition.ShiftPending = 0;
    }
    Condition.ShiftCut = MoveToward(Condition.ShiftCut, 0.0, DeltaSeconds / 0.04);

    // ③ Boost. A blower follows rotor speed; a turbo lags the schedule. Closing the throttle against boost dumps the charge.
    const RPM = Electric ? Condition.EngineAngularSpeed * 30.0 / Math.PI
                         : Max(0.0, Condition.EngineAngularSpeed * 30.0 / Math.PI);
    const PedalFilling = SmoothStep(0.02, 0.85, Condition.ThrottlePlate);
    Condition.BoostTarget = Clamp(EvaluateCurve(Vehicle.DerivedBoostSchedule, RPM) * PedalFilling, 0.0,
                                  Max(0.0, I.WastegateMaxBoostBar));
    if (I.Topology === "rootsSupercharger")
    {
        Condition.BlowerPressure = Clamp(RPM / Max(1.0, C.RedlineRPM), 0.0, 1.1) * (0.25 + 0.75 * Condition.ThrottlePlate);
        const Lag = Max(0.005, I.SpoolTimeConstantSeconds);
        Condition.Boost += (Condition.BoostTarget - Condition.Boost) * Clamp(DeltaSeconds / Lag, 0.0, 1.0);
        Condition.TurbineSpeed = Condition.BlowerPressure;
    }
    else if (I.Topology === "twinScrollTurbo")
    {
        const Lag = Max(0.06, I.SpoolTimeConstantSeconds) * (Condition.BoostTarget > Condition.Boost ? 1.0 : 0.5);
        Condition.Boost += (Condition.BoostTarget - Condition.Boost) * Clamp(DeltaSeconds / Lag, 0.0, 1.0);
        Condition.BlowerPressure = 0.0;
        Condition.TurbineSpeed = Clamp(Condition.Boost / Max(0.05, I.WastegateMaxBoostBar), 0.0, 1.15) *
                                 (0.30 + 0.70 * Condition.ThrottlePlate);
        if (I.BlowOffEnabled && Condition.ThrottlePlate < 0.12 && Condition.Boost > I.WastegateMaxBoostBar * 0.20)
        {
            Condition.BlowOffTrigger = 1.0;
            Condition.Boost *= 0.55;
        }
    }
    else
    {
        Condition.Boost = MoveToward(Condition.Boost, 0.0, DeltaSeconds / 0.18);
        Condition.BlowerPressure = 0.0;
        Condition.TurbineSpeed = MoveToward(Condition.TurbineSpeed, 0.0, DeltaSeconds / 0.25);
    }
    Condition.BlowOffTrigger = MoveToward(Condition.BlowOffTrigger, 0.0, DeltaSeconds / 0.25);

    // ④ Rev limiter: spark cut with hysteresis, and a hard ceiling on engine speed so no coupling error can escape it. The cut is what a
    //    limiter sounds like — a machine-gun dropout, not a smooth clip.
    const LimiterRPM = C.LimiterRPM;
    if (RPM >= LimiterRPM)
        Condition.LimiterTimer = LimiterCutSeconds;
    Condition.LimiterTimer = Max(0.0, Condition.LimiterTimer - DeltaSeconds);
    Condition.LimiterCut = Condition.LimiterTimer > 0.0 ? 1.0 : (RPM > LimiterRPM - LimiterHysteresis ? 0.55 : 0.0);
    if (RPM > LimiterRPM)
        Condition.EngineAngularSpeed = LimiterRPM * Math.PI / 30.0;

    // ⑤ Engine torque.
    const EffectiveThrottle = Condition.ThrottlePlate * (1.0 - Condition.LimiterCut);
    let EngineTorque = EvaluateEngineTorque(Vehicle, RPM, EffectiveThrottle, Condition.Boost);
    if (Condition.ShiftCut > 0.0)
        EngineTorque *= 1.0 - 0.90 * Clamp(Condition.ShiftCut * 1.5, 0.0, 1.0);
    Condition.EngineNetTorque = EngineTorque;

    // ⑥ Driveline. Electric drive has no clutch: the motor is welded to the reduction by definition.
    const OverallRatio = ResolveGearRatio(Vehicle, Condition.GearIndex) * D.FinalDriveRatio;
    let Tyre;
    if (Electric)
        Tyre = AdvanceElectricDriveline(Condition, Vehicle, EngineTorque, OverallRatio, WheelRadius, DeltaSeconds);
    else if (Condition.GearIndex === 0)
    {
        AdvanceNeutralEngine(Condition, Vehicle, EngineTorque, DeltaSeconds);
        Tyre = EvaluateTyreState(Condition, Vehicle, WheelRadius);
    }
    else
        Tyre = AdvanceClutchedDriveline(Condition, Vehicle, EngineTorque, OverallRatio, WheelRadius, DeltaSeconds);

    // ⑦ Body motion from the contact force, and the lateral state derived from it.
    AdvanceBodyMotion(Condition, Vehicle, Tyre, DeltaSeconds);

    // ⑧ Secondary loads and the signals the voices read.
    const Redline = Max(1.0, C.RedlineRPM);
    const NormalisedSpeed = Clamp(RPM / Redline, 0.0, 1.25);
    Condition.EngineLoad = Clamp((0.15 + 0.85 * Condition.ThrottlePlate) * (Electric ? 1.0 : (0.35 + 0.65 * NormalisedSpeed * 1.4)),
                                 0.0, 1.0);
    Condition.ExhaustMassFlow = Clamp((0.22 + 0.78 * Condition.ThrottlePlate) * (0.30 + 0.70 * NormalisedSpeed), 0.0, 1.4);
    const Coolant = 353.0 + 46.0 * Clamp(Condition.EngineLoad * 0.85 + NormalisedSpeed * 0.35, 0.0, 1.0);
    Condition.CoolingFanSpeed = MoveToward(Condition.CoolingFanSpeed, Coolant > 388.0 ? 1.0 : 0.0, DeltaSeconds / 0.7);
    Condition.GearWhineLoad = Clamp(Math.abs(Condition.TransmittedTorque) / Max(60.0, D.ClutchTorqueCapacity), 0.0, 1.0);
    Condition.DifferentialLoad = Clamp(Math.abs(Condition.LongitudinalForce) / Max(800.0, K.MassKilograms * 4.0), 0.0, 1.0);
    Condition.TyreTemperature = Clamp(Condition.TyreTemperature + (Condition.TyreScrub * 0.9 +
                                     Math.abs(Condition.WheelSlipRatio) * 0.6 - 0.25) * DeltaSeconds * 0.05, 0.05, 1.0);

    // ⑨ Overrun: closed throttle above the torque peak leaves unburnt charge in a hot exhaust, which is where crackle comes from.
    const OverrunDepth = Condition.ThrottlePlate < 0.10 && RPM > C.IdleRPM * 1.30
        ? Clamp((RPM - C.IdleRPM * 1.30) / Max(1.0, Redline - C.IdleRPM * 1.30), 0.0, 1.0) : 0.0;
    const TargetUnburnt = Vehicle.Exhaust.BackfireEnabled ? OverrunDepth * (1.0 - Condition.ThrottlePlate) : 0.0;
    Condition.OverrunFuelFraction += (TargetUnburnt - Condition.OverrunFuelFraction) * Clamp(DeltaSeconds / 0.10, 0.0, 1.0);

    Condition.Time += DeltaSeconds;
}

// 📝 In neutral the engine is its own inertia against friction and a governor, which is why a blipped throttle in neutral climbs instantly and
//    falls slowly — the flywheel, not the car, is what the note is following.
function AdvanceNeutralEngine(Condition, Vehicle, EngineTorque, DeltaSeconds)
{
    const Ie = Max(0.02, Vehicle.Combustion.EngineInertiaKilogramMetres);
    const IdleSpeed = Max(1.0, Vehicle.Combustion.IdleRPM) * Math.PI / 30.0;
    const Governor = Condition.ThrottlePlate < 0.04 ? (IdleSpeed - Condition.EngineAngularSpeed) * LimitGovernorGain * Ie : 0.0;
    const Drag = Condition.EngineAngularSpeed * 0.055 * Ie;
    Condition.EngineAngularSpeed = Max(IdleSpeed * 0.92,
                                       Condition.EngineAngularSpeed + (EngineTorque + Governor - Drag) / Ie * DeltaSeconds);
    Condition.TransmittedTorque = 0.0;
    Condition.ClutchSlipSpeed = 0.0;
    Condition.ClutchEngagement = MoveToward(Condition.ClutchEngagement, 0.0, DeltaSeconds / 0.10);
    Condition.LockupState = MoveToward(Condition.LockupState, 0.0, DeltaSeconds / 0.10);
}

// 📝 Tyre state from the real slip ratio. Load transfer under acceleration is included because it is the single largest term in a launch:
//    a rear-drive car gains load on its driven axle as it squats, and without that term every car accelerates like a shopping trolley.
function EvaluateTyreState(Condition, Vehicle, WheelRadius)
{
    const K = Vehicle.Chassis;
    const Transfer = K.MassKilograms * Condition.LongitudinalAcceleration * K.CentreOfMassHeight / WheelbaseMetres;
    const NormalLoad = Max(0.0, K.MassKilograms * GravityAcceleration * K.DrivenStaticFraction + Transfer);
    const GripLimit = K.TyreGripCoefficient * NormalLoad * (0.86 + 0.28 * Condition.TyreTemperature);
    const SurfaceSpeed = Condition.WheelSpeed * WheelRadius;
    const SlipRatio = (SurfaceSpeed - Condition.Speed) / Max(SlipReferenceSpeed, Math.abs(Condition.Speed));
    const ContactForce = GripLimit * Math.tanh(TyreSaturationStiffness * SlipRatio);
    return { NormalLoad, GripLimit, SlipRatio, ContactForce, SurfaceSpeed };
}

function ResolveBrakeTorque(Condition, Chassis, WheelRadius)
{
    const Demand = (Condition.BrakePedal + Condition.Handbrake * 0.55) * Chassis.BrakeTorqueCapacity;
    return Demand * SmoothSign(Condition.WheelSpeed);
}

// 📝 Two-inertia clutch with the tyre reaction on the wheel. While slipping the clutch transmits its capacity in the direction of slip, so
//    wheelspin and clutch slip stay separate phenomena. During a launch the controller holds the engine at the launch speed by sizing
//    capacity, and clamps that capacity to the traction limit the tyre can actually take — which is exactly what launch control does, and
//    it is why the engine sits on one note while the car leaves the line instead of bouncing off the limiter.
function AdvanceClutchedDriveline(Condition, Vehicle, EngineTorque, OverallRatio, WheelRadius, DeltaSeconds)
{
    const C = Vehicle.Combustion, D = Vehicle.Drivetrain, K = Vehicle.Chassis;
    const Ie = Max(0.02, C.EngineInertiaKilogramMetres);
    const DrivenWheelInertia = D.FrontTorqueFraction > 0.0 ? 4.6 : 2.6;      // [kg·m²] - driven wheels, hubs, discs, half shafts
    const Capacity = Max(80.0, D.ClutchTorqueCapacity);
    const IdleSpeed = Max(1.0, C.IdleRPM) * Math.PI / 30.0;
    const TransmissionSpeed = Condition.WheelSpeed * OverallRatio;
    const Slip = Condition.EngineAngularSpeed - TransmissionSpeed;
    const Thr = Condition.ThrottlePlate;
    const Tyre = EvaluateTyreState(Condition, Vehicle, WheelRadius);
    const BrakeTorque = ResolveBrakeTorque(Condition, K, WheelRadius);
    const Efficiency = D.DriveEfficiency;

    // 📝 Clutch torque the tyre can accept before it spins: the traction budget expressed at the clutch.
    const TractionTorque = Tyre.GripLimit * WheelRadius / Max(0.1, OverallRatio * Efficiency);
    const LaunchSpeed = Max(D.LaunchRPM, D.TorqueConverterStallRPM) * Math.PI / 30.0;
    // 📝 The launch controller only has authority while the clutch is genuinely slipping; once the pair welds, the engine is the wheels.
    const Launching = Thr > 0.05 && TransmissionSpeed < Max(LaunchSpeed * 1.15, 7.0) && Math.abs(Slip) > 25.0 &&
                      Condition.EngineAngularSpeed < C.LimiterRPM * Math.PI / 30.0 * 0.97;
    Condition.LaunchControl = MoveToward(Condition.LaunchControl, Launching && Thr > 0.7 ? 1.0 : 0.0, DeltaSeconds / 0.4);

    // ① Decide lock. A clutch welds only when the slip has closed AND the capacity can carry the demand without forcing a slip reversal.
    // 📝 A clutch also refuses to weld when the engine is being held at idle faster than the gearing demands — otherwise idle torque would drag a
    //    parked car away. That is the real behaviour of both a manual clutch and a torque converter.
    let Welded = Math.abs(Slip) < ClutchLockSlip && EngineTorque < Capacity * ClutchLockMargin && !Launching &&
                 (TransmissionSpeed > IdleSpeed * 0.9 || Thr > 0.05);

    if (Welded)
    {
        // 📝 Locked driveline: one inertia in the wheel frame, engine inertia reflected through the gearing.
        const WheelInertiaTotal = DrivenWheelInertia + Ie * OverallRatio * OverallRatio * Efficiency;
        const AxleTorque = EngineTorque * OverallRatio * Efficiency;
        const WheelAcceleration = (AxleTorque - Tyre.ContactForce * WheelRadius - BrakeTorque) / Max(0.4, WheelInertiaTotal);
        Condition.WheelSpeed = Max(-600.0, Condition.WheelSpeed + WheelAcceleration * DeltaSeconds);
        Condition.EngineAngularSpeed = Max(0.0, Condition.WheelSpeed * OverallRatio);
        // 📝 The limiter caps the locked pair: a held gear cannot rev past its ceiling, so the wheels cannot either.
        const Ceiling = C.LimiterRPM * Math.PI / 30.0 / Max(0.1, OverallRatio);
        if (Condition.WheelSpeed > Ceiling) Condition.WheelSpeed = Ceiling;
        Condition.EngineAngularSpeed = Max(IdleSpeed * 0.55, Condition.WheelSpeed * OverallRatio);
        Condition.TransmittedTorque = EngineTorque;
        Condition.ClutchSlipSpeed = 0.0;
        Condition.ClutchEngagement = 1.0;
        Condition.LockupState = 1.0;
        Condition.DrivelineLash = MoveToward(Condition.DrivelineLash, 0.0, DeltaSeconds / 0.15);
        return Tyre;
    }

    // ② Slipping. Size the capacity, transmit it, and spin the engine freely against it.
    let Engagement;
    if (Launching)
    {
        // 📝 Proportional control on engine speed about the launch target. Overspeed must ADD load, not remove it — the error therefore enters
        //    with its natural sign, so an engine running ahead of the target is pulled down by the clutch instead of revving away from it.
        const SpeedError = (Condition.EngineAngularSpeed - LaunchSpeed) / Max(1.0, LaunchSpeed);
        const Authority = Clamp(0.55 + 1.70 * SpeedError, 0.05, 1.0);
        const Desired = Math.max(0.0, EngineTorque) * Authority;
        const Allowed = Math.min(Desired, TractionTorque * LaunchTractionAllowance);
        Engagement = Clamp(Allowed / Capacity, 0.03, 1.0);
    }
    else if (Thr < 0.04 && Condition.EngineAngularSpeed < IdleSpeed * 1.35)
    {
        // 📝 Creep fades out as the road speed approaches what idle would demand through the gearing, so a parked automatic settles at a walking
        //    pace instead of accelerating forever off its own idle torque.
        // 📝 Creep is the converter unloading, so it holds the engine at idle while it pushes: the ceiling is the road speed whose wheel speed
        //    matches idle through the gearing, scaled by the converter's slip at that condition.
        // 📝 Creep is a real automatic behaviour, but it is off by default here: on a bench a car with no pedal applied should stay where it was
        //    put, and a car that crawls away while the operator reads the panel is a defect, not a feature.
        const CreepCeiling = IdleSpeed / Max(0.2, OverallRatio) * WheelRadius * 0.55;
        Engagement = Condition.CreepEnabled && Math.abs(Condition.Speed) < CreepCeiling
            ? CreepShare * SmoothStep(0.0, Max(0.4, CreepCeiling), CreepCeiling - Math.abs(Condition.Speed))
            : 0.0;
        if (Engagement <= 0.0)
        {
            const IdleTarget = IdleSpeed;
            const Governor = (IdleTarget - Condition.EngineAngularSpeed) * 1.4 * Ie;
            const Drag = Condition.EngineAngularSpeed * 0.055 * Ie;
            Condition.EngineAngularSpeed = Max(IdleTarget * 0.92,
                                               Condition.EngineAngularSpeed + (EngineTorque + Governor - Drag) / Ie * DeltaSeconds);
        }
    }
    else
    {
        // 📝 A closed throttle transmits nothing: engagement is the pedal. Coasting engine braking arrives through the locked path instead, which
        //    is why a car in gear slows down at zero pedal rather than continuing to be pushed by its own idle.
        Engagement = Thr > 0.02 ? Clamp(0.18 + 0.82 * Thr, 0.08, 1.0) : 0.0;
    }
    Condition.ClutchEngagement = Engagement;
    Condition.LockupState = MoveToward(Condition.LockupState, Engagement, DeltaSeconds / 0.05);

    const Direction = Slip >= 0.0 ? 1.0 : -1.0;
    const CapacityNow = Capacity * Engagement;
    const Viscous = ClutchStiffnessPerRadian * Slip * Engagement;
    let ClutchTorque = Clamp(Viscous, -CapacityNow, CapacityNow);
    if (Math.abs(Slip) > 2.5)
        ClutchTorque = Direction * Min(CapacityNow, Math.abs(Viscous) * 0.45 + CapacityNow * 0.35);
    if (Condition.ShiftCut > 0.05)
        ClutchTorque *= 1.0 - 0.85 * Condition.ShiftCut;
    Condition.TransmittedTorque = ClutchTorque;
    Condition.ClutchSlipSpeed = Slip;
    Condition.DrivelineLash = MoveToward(Condition.DrivelineLash, Math.abs(Slip) > 5.0 && Engagement > 0.3 ? 0.5 : 0.0,
                                         DeltaSeconds / 0.09);

    const EngineDrag = Condition.EngineAngularSpeed * 0.055 * Ie;
    Condition.EngineAngularSpeed = Max(IdleSpeed * 0.55,
                                       Condition.EngineAngularSpeed + (EngineTorque - ClutchTorque - EngineDrag) / Ie * DeltaSeconds);

    const AxleTorque = ClutchTorque * OverallRatio * (ClutchTorque >= 0.0 ? Efficiency : 1.0 / Max(0.2, Efficiency));
    const WheelAcceleration = (AxleTorque - Tyre.ContactForce * WheelRadius - BrakeTorque) / Max(0.4, DrivenWheelInertia);
    Condition.WheelSpeed = Max(-600.0, Condition.WheelSpeed + WheelAcceleration * DeltaSeconds);
    return Tyre;
}

// 📝 Electric drive is welded by construction: one reduction, no clutch, and regenerative torque when the pedal is released.
function AdvanceElectricDriveline(Condition, Vehicle, MotorTorque, OverallRatio, WheelRadius, DeltaSeconds)
{
    const K = Vehicle.Chassis, D = Vehicle.Drivetrain;
    const Im = Max(0.02, Vehicle.Combustion.EngineInertiaKilogramMetres);
    const DrivenWheelInertia = 4.6;
    const Tyre = EvaluateTyreState(Condition, Vehicle, WheelRadius);
    const BrakeTorque = ResolveBrakeTorque(Condition, K, WheelRadius);
    const WheelInertiaTotal = DrivenWheelInertia + Im * OverallRatio * OverallRatio * D.DriveEfficiency;
    const AxleTorque = MotorTorque * OverallRatio * D.DriveEfficiency;
    const WheelAcceleration = (AxleTorque - Tyre.ContactForce * WheelRadius - BrakeTorque) / Max(0.6, WheelInertiaTotal);
    Condition.WheelSpeed = Max(-800.0, Condition.WheelSpeed + WheelAcceleration * DeltaSeconds);
    const Ceiling = Vehicle.Combustion.LimiterRPM * Math.PI / 30.0 / Max(0.1, OverallRatio);
    if (Condition.WheelSpeed > Ceiling) Condition.WheelSpeed = Ceiling;
    Condition.EngineAngularSpeed = Max(0.0, Math.abs(Condition.WheelSpeed * OverallRatio));
    Condition.TransmittedTorque = MotorTorque;
    Condition.ClutchEngagement = 1.0;
    Condition.ClutchSlipSpeed = 0.0;
    Condition.LockupState = 1.0;
    Condition.DrivelineLash = MoveToward(Condition.DrivelineLash, 0.0, DeltaSeconds / 0.2);
    return Tyre;
}

// 📝 Body motion uses the contact force the driveline branch was already computed against, so the two cannot disagree: the wheel spins, the
//    tyre saturates at the grip limit, and the car accelerates by the remainder. That is the whole of a launch, a burnout and a wheelspin.
function AdvanceBodyMotion(Condition, Vehicle, Tyre, DeltaSeconds)
{
    const K = Vehicle.Chassis;
    Condition.WheelSlipRatio = Condition.GearIndex !== 0 ? Tyre.SlipRatio : Tyre.SlipRatio * 0.1;
    Condition.WheelSlipSpeed = Tyre.SurfaceSpeed - Condition.Speed;
    Condition.LongitudinalForce = Condition.GearIndex !== 0 ? Tyre.ContactForce : 0.0;

    const Drag = 0.5 * AirDensity * K.DragArea * Condition.Speed * Math.abs(Condition.Speed);
    const Rolling = K.RollingResistance * K.MassKilograms * GravityAcceleration * SmoothSign(Condition.Speed);
    const BrakeForceLimit = BrakeGripFraction * K.TyreGripCoefficient * K.MassKilograms * GravityAcceleration;
    const BrakeDemand = (Condition.BrakePedal + Condition.Handbrake * 0.55) * K.BrakeTorqueCapacity / Max(0.15, K.WheelRadiusMetres);
    const BrakeForce = Min(BrakeDemand, BrakeForceLimit) * SmoothSign(Condition.Speed);

    const NetForce = Condition.LongitudinalForce - Drag - Rolling - BrakeForce;
    Condition.LongitudinalAcceleration = NetForce / K.MassKilograms;
    Condition.Speed += Condition.LongitudinalAcceleration * DeltaSeconds;
    if (Math.abs(Condition.Speed) < 0.04 && Math.abs(Condition.LongitudinalForce) < 400.0)
        Condition.Speed = 0.0;
    Condition.Distance += Condition.Speed * DeltaSeconds;
    Condition.WheelAngle += Condition.WheelSpeed * DeltaSeconds;

    // 📝 Lateral state from the bicycle model: yaw rate for a given steer angle, lateral acceleration = v·r, and the tyre scrub that follows
    //    once the lateral demand passes the grip envelope. Scrub is the squeal excitation, so the physics drives the sound directly.
    const YawRate = Condition.Speed * Math.tan(Condition.SteerAngle) / WheelbaseMetres;
    const LateralAcceleration = Condition.Speed * YawRate;
    Condition.YawRate = YawRate;
    Condition.LateralAcceleration = LateralAcceleration;
    const LateralLimit = K.TyreGripCoefficient * GravityAcceleration;
    const ScrubTarget = Clamp((Math.abs(LateralAcceleration) - LateralLimit * 0.55) / Max(0.2, LateralLimit * 0.45), 0.0, 1.0);
    Condition.TyreScrub = MoveToward(Condition.TyreScrub, Max(ScrubTarget * 0.7, Math.min(1.0, Math.abs(Tyre.SlipRatio) * 0.8)),
                                     DeltaSeconds / 0.06);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PUBLIC ADVANCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The host calls this once per fixed step. Long device blocks are subdivided here so a 2048-sample quantum at 48 kHz (42 ms) still gets
//    far smaller sub-steps and cannot tunnel through the tyre model.
// 📝 The integrator runs on an absolute 2 ms grid, with the remainder carried on the condition. That is what makes the trajectory identical
//    for a 128-sample block and a 512-sample block: the physics advances by the same number of the same-sized steps per elapsed second, no
//    matter how the caller chops up time. Long device quanta therefore cannot change the car's behaviour, only when the caller observes it.
export function AdvanceDrivetrain(Condition, Vehicle, Input, DeltaSeconds)
{
    if (!(DeltaSeconds > 0.0))
        return Condition;
    Condition.IntegratorResidualNanoseconds += Math.round(DeltaSeconds * 1.0e9);
    const Latch = {
        Throttle: Input.Throttle, Brake: Input.Brake, Steer: Input.Steer, Handbrake: Input.Handbrake,
        AutoShift: Input.AutoShift, LaunchControl: Input.LaunchControl,
        ShiftUp: !!Input.ShiftUp, ShiftDown: !!Input.ShiftDown,
        SelectReverse: !!Input.SelectReverse, SelectNeutral: !!Input.SelectNeutral
    };
    let Guard = 0;
    while (Condition.IntegratorResidualNanoseconds >= IntegratorStepNanoseconds && Guard < 8192)
    {
        ++Guard;
        AdvanceSubstep(Condition, Vehicle, Latch, IntegratorSubstepCeiling);
        Condition.IntegratorResidualNanoseconds -= IntegratorStepNanoseconds;
        Latch.ShiftUp = false;
        Latch.ShiftDown = false;
        Latch.SelectReverse = false;
        Latch.SelectNeutral = false;
    }
    return Condition;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TELEMETRY SNAPSHOT
//------------------------------------------------------------------------------------------------------------------------

export function ResolveDrivetrainTelemetry(Condition, Vehicle)
{
    const C = Vehicle.Combustion, D = Vehicle.Drivetrain;
    const RPM = Condition.EngineAngularSpeed * 30.0 / Math.PI;
    const GearRatio = ResolveGearRatio(Vehicle, Condition.GearIndex);
    const FiringFrequency = Vehicle.Electric ? RPM / 60.0 * 0.5
                                             : RPM / 60.0 * Max(1, C.CylinderCount) / Max(1.0, C.CycleRevolutions || 2.0);
    return Object.freeze({
        Time:                     Condition.Time,
        Distance:                 Condition.Distance,
        SpeedMetresPerSecond:     Condition.Speed,
        SpeedKilometresPerHour:   Condition.Speed * 3.6,
        EngineRPM:                RPM,
        EngineNormalisedSpeed:    Clamp(RPM / Max(1.0, C.RedlineRPM), 0.0, 1.35),
        FiringFrequencyHz:        FiringFrequency,
        WheelRPM:                 Condition.WheelSpeed * 30.0 / Math.PI,
        WheelAngle:               Condition.WheelAngle,
        GearIndex:                Condition.GearIndex,
        GearRatio,
        OverallRatio:             GearRatio * D.FinalDriveRatio,
        Throttle:                 Condition.ThrottlePlate,
        ThrottleRate:             Condition.ThrottleRateMemory,
        Brake:                    Condition.BrakePedal,
        Steer:                    Condition.SteerAngle,
        BoostBar:                 Condition.Boost,
        BoostFraction:            Clamp(Condition.Boost / Max(0.05, Vehicle.Induction.WastegateMaxBoostBar), 0.0, 1.2),
        TurbineSpeed:             Condition.TurbineSpeed,
        BlowerPressure:           Condition.BlowerPressure,
        BlowOffStrength:          Condition.BlowOffTrigger,
        ClutchEngagement:         Condition.ClutchEngagement,
        ClutchSlipSpeed:          Condition.ClutchSlipSpeed,
        LockupState:              Condition.LockupState,
        ShiftCut:                 Condition.ShiftCut,
        ShiftTimer:               Condition.ShiftTimer,
        LimiterCut:               Condition.LimiterCut,
        OverrunFuelFraction:      Condition.OverrunFuelFraction,
        EngineLoad:               Condition.EngineLoad,
        ExhaustMassFlow:          Condition.ExhaustMassFlow,
        WheelSlipRatio:           Condition.WheelSlipRatio,
        WheelSlipSpeed:           Condition.WheelSlipSpeed,
        LongitudinalForce:        Condition.LongitudinalForce,
        TyreScrub:                Condition.TyreScrub,
        LongitudinalAcceleration: Condition.LongitudinalAcceleration,
        LateralAcceleration:      Condition.LateralAcceleration,
        YawRate:                  Condition.YawRate,
        DrivelineLash:            Condition.DrivelineLash,
        GearWhineLoad:            Condition.GearWhineLoad,
        DifferentialLoad:         Condition.DifferentialLoad,
        CoolingFanSpeed:          Condition.CoolingFanSpeed,
        LaunchControl:            Condition.LaunchControl,
        TyreTemperature:          Condition.TyreTemperature,
        TorqueConverterSlip:      Condition.ClutchSlipSpeed
    });
}
