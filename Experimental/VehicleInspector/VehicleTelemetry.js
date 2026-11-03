/*==========================================================================================================================================
                                                       VEHICLETELEMETRY.JS
==========================================================================================================================================*/
// 🧩 The runtime telemetry dock: a read-only live view of the vehicle a driver watches while the car is on track, with a
//    switch back to the authoring inspector. It is a UI component parented to the vehicle record, like the tyres are
//    parented to the vehicle — it is not a scene object and it never writes to the record.
'use strict';

const VehicleTelemetry = (() =>
{
    const C       = InspectorControls;
    const Catalog = VehicleCatalogue;

    //------------------------------------------------------------------------------------------------------------------------
    //                                                      THE CIRCUIT
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The prototype drives one synthetic lap: straights joined by corners of a fixed radius. The lap is a table of speed
    //    limits; braking and traction passes make it a lap a car can actually follow. Nothing here is a tyre model.
    const LapLength = 4320;          // [m]   - One lap, start line to start line
    const SampleCount = 432;         // [-]   - 10 m between samples

    const CornerTable = [
        [0.00, 0.235, 0],       // main straight
        [0.235, 0.275, 46],     // hairpin, tightest point of the lap
        [0.275, 0.395, 0],      // short straight
        [0.395, 0.455, 78],     // medium right
        [0.455, 0.505, 165],    // fast left
        [0.505, 0.640, 0],      // back straight
        [0.640, 0.700, 62],     // slow left
        [0.700, 0.760, 210],    // kink
        [0.760, 0.845, 0],      // third straight
        [0.845, 0.900, 40],     // final hairpin
        [0.900, 1.000, 0]       // run to the line
    ];

    const Segments = CornerTable.map(([Start, End, Radius], Index) => ({
        Start, End, Radius,
        Side: Radius === 0 ? 0 : (Index % 2 === 0 ? 1 : -1)
    }));

    const Gravity = 9.81;

    function ResolveGearRatios(Specification)
    {
        return Catalog.ResolveGearRatios(Specification);
    }

    // 📝 The gear a driver would be in: the lowest gear that does not ask the engine for more than the limiter.
    function ResolveBestGear(Specification, Speed)
    {
        const Ratios = ResolveGearRatios(Specification);
        let Gear = 0;
        while (Gear < Ratios.length - 1 &&
               Catalog.ResolveEngineSpeed(Specification.Drivetrain, Math.max(Speed, 4), Ratios[Gear]) > Specification.Engine.Redline)
            ++Gear;
        return Gear;
    }

    function ResolveCorner(Sample)
    {
        const Fraction = (Sample % SampleCount) / SampleCount;
        return Segments.find(Segment => Fraction >= Segment.Start && Fraction < Segment.End) || Segments[0];
    }

    function ResolveRadius(Sample)
    {
        const Radius = ResolveCorner(Sample).Radius;
        return Radius === 0 ? Infinity : Radius;
    }

    function ResolveSide(Sample)
    {
        return ResolveCorner(Sample).Side;
    }

    // 📝 The lap table: corner speed from the friction circle, a backward pass for braking, a forward pass for traction.
    //    The car follows this table with a short lag, which is what makes the gauges move instead of snapping.
    function ConfigureTrack(Specification, Profiles)
    {
        const Drivetrain = Specification.Drivetrain;
        const Mass = Specification.Identity.KerbMass;
        const Grip = Drivetrain.TyreFriction * Gravity;
        const Distance = LapLength / SampleCount;
        const Limits = new Float64Array(SampleCount);

        // 📝 The straight is limited by the car's own top speed, so a slower record does not get a 430 km/h straight.
        const Straight = Math.min(120, Catalog.EstimateTopSpeed(Specification, Profiles) / 3.6 + 1);
        for (let Sample = 0; Sample < SampleCount; ++Sample)
        {
            const Radius = ResolveRadius(Sample);
            Limits[Sample] = Radius === Infinity ? Straight : Math.min(Straight, Math.sqrt(Grip * Radius));
        }

        // 📝 The lap is a loop, so both passes wrap and the traction pass is run twice round: the start line inherits the
        //    speed the last corner allows, which is what a real out-lap does too.
        const BrakeGrip = Grip * 0.94;
        for (let Step = 0; Step < SampleCount; ++Step)
        {
            const Sample = SampleCount - 1 - Step;
            const Next = (Sample + 1) % SampleCount;
            Limits[Sample] = Math.min(Limits[Sample], Math.sqrt(Limits[Next] * Limits[Next] + 2 * BrakeGrip * Distance));
        }
        for (let Lap = 0; Lap < 2; ++Lap)
        {
            for (let Sample = 0; Sample < SampleCount; ++Sample)
            {
                // 📝 The gear comes from the speed the car carries into the sample, not from the sample's own limit: on a
                //    straight the limit is top gear's business only once the car has actually got there.
                const Previous = Limits[(Sample + SampleCount - 1) % SampleCount];
                const Speed = Math.max(4, Previous * 3.6);
                const Gear = ResolveBestGear(Specification, Speed);
                const Ratio = ResolveGearRatios(Specification)[Gear];
                const Force = Math.min(Catalog.ResolveWheelForce(Specification, Profiles, Speed, Ratio),
                                       Catalog.ResolveTractionLimit(Specification, Speed, 0))
                            - Catalog.ResolveResistance(Specification, Speed);
                const Acceleration = Math.max(0, Force / Mass);
                Limits[Sample] = Math.min(Limits[Sample], Math.sqrt(Previous * Previous + 2 * Acceleration * Distance));
            }
        }

        // 📝 The limit table is not sorted; it is read by sample index, so it is kept as a plain array for the trace card.
        return { Length: LapLength, Limits: Array.from(Limits), SampleCount, Distance };
    }

    function SampleTrack(Track, Distance)
    {
        const Position = ((Distance % Track.Length) + Track.Length) % Track.Length / Track.Distance;
        const Low = Math.floor(Position) % Track.SampleCount;
        const High = (Low + 1) % Track.SampleCount;
        const Blend = Position - Math.floor(Position);
        return Track.Limits[Low] * (1 - Blend) + Track.Limits[High] * Blend;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       THE SESSION
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Fuel load and reserve are authored on the record; the tank itself is Identity.FuelCapacity, so there is one place
    //    that knows how big the tank is.
    function ResolveFuelLoad(Specification)
    {
        const Capacity = Math.max(1, Specification.Identity.FuelCapacity);
        const Load = Specification.Fuel && typeof Specification.Fuel.Load === 'number' ? Specification.Fuel.Load : Capacity * 0.9;
        return Math.max(0, Math.min(Capacity, Load));
    }

    function ResolveFuelReserve(Specification)
    {
        return Specification.Fuel && typeof Specification.Fuel.Reserve === 'number' ? Specification.Fuel.Reserve : 12;
    }

    function Create(Specification, Profiles, Tyres)
    {
        const Capacity = Math.max(1, Specification.Identity.FuelCapacity);
        return {
            Specification, Profiles, Tyres,
            Track: ConfigureTrack(Specification, Profiles),
            isRunning: true,
            isDry: false,
            Elapsed: 0,               // [s]  - Since the session started
            Lap: 1,
            LapTime: 0,               // [s]
            LapDistance: 0,           // [m]
            TotalDistance: 0,         // [m]
            Speed: 24,                // [m/s]
            PreviousSpeed: 24,        // [m/s]
            Gear: 1,
            Rpm: Specification.Engine.IdleSpeed,
            Throttle: 0.35,
            Brake: 0,
            Steering: 0,              // [°]
            LatG: 0,
            LongG: 0,
            Fuel: ResolveFuelLoad(Specification),
            Capacity,
            BurnTotal: 0,             // [L]  - Burned this session
            LapBurn: 0,               // [L]  - Burned this lap
            LastLapBurn: 0,           // [L]  - Measured on the previous lap
            Flow: 0,                  // [L/min]
            Coolant: Specification.Thermal.CoolantTarget,
            Oil: Specification.Thermal.OilTarget,
            OilPressure: 3.2,         // [bar]
            Boost: 0,                 // [bar]
            Power: 0,                 // [kW]
            Torque: 0,                // [N·m]
            Ers: 0.62,                // [-]  - State of charge
            Drs: 'Available',
            TcActive: false,
            AbsActive: false,
            BrakeTemperature: [Specification.Thermal.AmbientTemperature, Specification.Thermal.AmbientTemperature],
            Corners: Tyres.map(Tyre => ({
                Id: Tyre.Id, Code: Tyre.Code, Compound: Tyre.Compound, Enclosed: Tyre.Enclosed,
                ColdPressure: Tyre.Pressure, Pressure: Tyre.Pressure,
                Surface: Tyre.SurfaceTemperature, Core: Tyre.CoreTemperature,
                Wear: Tyre.Wear, BrakeTemperature: Specification.Thermal.AmbientTemperature
            })),
            History: [],              // [[seconds, km/h]]
            LapHistory: [],           // [[lap, litres]]
            Events: []
        };
    }

    function AddEvent(Session, Channel, Message, Detail, Tone)
    {
        Session.Events.push({ Channel, Message, Detail, Tone });
        if (Session.Events.length > 24) Session.Events.shift();
    }

    function ResolveLoadFraction(Session, Specification)
    {
        return Math.max(0, Math.min(1, Session.Throttle * 0.85 + Session.Rpm / Math.max(1, Specification.Engine.Redline) * 0.15));
    }

    // 📝 One fixed step: follow the lap table, then let every temperature, pressure and level catch up with the load that
    //    step produced. The whole update is deterministic, so a check can advance it and compare values exactly.
    function Advance(Session, Elapsed)
    {
        const Step = Math.max(0, Math.min(0.25, Elapsed));
        if (Step === 0) return Session;
        const Specification = Session.Specification;
        const Profiles = Session.Profiles;
        const Drivetrain = Specification.Drivetrain;
        const Mass = Specification.Identity.KerbMass;
        const Grip = Drivetrain.TyreFriction * Gravity;

        if (!Session.isRunning) return Session;

        // ① road position, the speed the table asks for, and what the car can actually do about it
        const Target = SampleTrack(Session.Track, Session.LapDistance + 18);
        const Previous = Session.Speed;
        const RoadSpeed = Math.max(4, Session.Speed * 3.6);
        const Ratios = ResolveGearRatios(Specification);
        const DemandGear = ResolveBestGear(Specification, RoadSpeed);
        const WheelForce = Catalog.ResolveWheelForce(Specification, Profiles, RoadSpeed, Ratios[DemandGear]);
        const Traction = Catalog.ResolveTractionLimit(Specification, RoadSpeed, Session.LongG * Gravity);
        const Resistance = Catalog.ResolveResistance(Specification, RoadSpeed);
        const DriveAcceleration = Math.max(0, (Math.min(WheelForce, Traction) - Resistance) / Mass);
        const BrakeAcceleration = Grip * 0.94;
        const Error = Target - Session.Speed;
        const Acceleration = Error >= 0 ? Math.min(DriveAcceleration, Error * 3.2) : Math.max(-BrakeAcceleration, Error * 3.2);
        Session.PreviousSpeed = Previous;
        Session.Speed = Math.max(0, Session.Speed + Acceleration * Step);

        const Speed = Session.Speed;
        Session.LongG = (Speed - Previous) / Step / Gravity;
        const SampleIndex = Math.floor(((Session.LapDistance % LapLength) / LapLength) * SampleCount) % SampleCount;
        const Radius = ResolveRadius(SampleIndex);
        Session.LatG = Radius === Infinity ? 0 : (Speed * Speed / Radius) / Gravity * ResolveSide(SampleIndex);
        Session.Steering = Radius === Infinity ? 0 : Math.round(Math.atan((Specification.Identity.Wheelbase / 1000) / Radius) * 57.2958 * 9)
            * ResolveSide(SampleIndex);
        // 📝 Throttle is the share of the available wheel force this step needs; braking is the share of the brake limit.
        const Required = Math.max(0, Acceleration * Mass + Resistance);
        const Demand = Math.min(1, Required / Math.max(1, Math.min(WheelForce, Traction)));
        Session.Throttle += (Demand - Session.Throttle) * Math.min(1, Step * 9);
        const Wanted = Acceleration < -0.05 ? Math.min(1, -Acceleration / BrakeAcceleration) : 0;
        Session.Brake += (Wanted - Session.Brake) * Math.min(1, Step * 12);
        if (Session.isDry) { Session.Throttle = 0; Session.Brake = Math.max(Session.Brake, 0.25); }

        // ② gear and engine speed
        const SpeedKmh = Speed * 3.6;
        const Gear = ResolveBestGear(Specification, SpeedKmh);
        if (Gear !== Session.Gear)
        {
            AddEvent(Session, 'TELEMETRY', `Gear ${Gear + 1}`, `${Session.Gear + 1} → ${Gear + 1} at ${SpeedKmh.toFixed(0)} km/h`, 'live');
            Session.Gear = Gear;
        }
        Session.Rpm = Math.max(Specification.Engine.IdleSpeed,
            Catalog.ResolveEngineSpeed(Drivetrain, Math.max(SpeedKmh, 2), Ratios[Gear]));
        Session.Torque = Catalog.ResolveTorque(Specification, Profiles, Session.Rpm);
        Session.Power = Session.Torque * Session.Rpm * 2 * Math.PI / 60 / 1000 * Session.Throttle;
        Session.Boost = Specification.Engine.TurboBoost * Session.Throttle * Math.min(1, Session.Rpm / 3200);

        // ③ fuel: full-load burn is authored on the record, part load follows the load fraction
        const Load = ResolveLoadFraction(Session, Specification);
        const KilogramPerHour = Session.isDry ? 0 : 4 + (Specification.Engine.FuelBurn - 4) * Math.pow(Load, 1.25);
        const LitrePerHour = KilogramPerHour / 0.745;
        Session.Flow = LitrePerHour / 60;
        const Burned = LitrePerHour / 3600 * Step;
        Session.Fuel = Math.max(0, Session.Fuel - Burned);
        Session.BurnTotal += Burned;
        Session.LapBurn += Burned;
        if (Session.Fuel <= 0 && !Session.isDry)
        {
            Session.isDry = true;
            AddEvent(Session, 'TELEMETRY', 'Tank empty', 'the model keeps rolling; the inspector is where the tank is filled', 'danger');
        }

        // ④ temperatures and pressures catch up with the load (first order, no tyre thermal model)
        const LoadPercent = Load * 100;
        const CoolantCurve = Profiles.find(Profile => Profile.Key === 'CoolantTemperature');
        const OilCurve = Profiles.find(Profile => Profile.Key === 'OilTemperature');
        const CoolantTarget = CoolantCurve
            ? Catalog.EvaluateCurve(CoolantCurve, LoadPercent, Specification, Profiles)
            : Specification.Thermal.CoolantTarget;
        const Emitted = CoolantTarget;
        const OilTarget = OilCurve ? Catalog.EvaluateCurve(OilCurve, Session.Rpm, Specification, Profiles) : Specification.Thermal.OilTarget;
        Session.Coolant += (Emitted - Session.Coolant) * Math.min(1, Step / 16);
        Session.Oil += (OilTarget - Session.Oil) * Math.min(1, Step / 22);
        Session.OilPressure = 1.15 + Session.Rpm / Math.max(1, Specification.Engine.Redline) * 3.4 + Session.Throttle * 0.6;
        Session.Ers = Math.max(0, Math.min(1, Session.Ers + (Session.Brake > 0.2 ? Step * 0.012 : -Step * 0.004)));

        // ⑤ a corner at a time: surface follows the authored curve at the slip the corner produces, core follows surface
        const Slip = Math.min(12, Math.abs(Session.LatG) * 4.2);
        const SurfaceCurve = Profiles.find(Profile => Profile.Key === 'TyreSurfaceTemperature');
        const SurfaceTarget = SurfaceCurve ? Catalog.EvaluateCurve(SurfaceCurve, Slip, Specification, Profiles)
                                           : 88 + Slip * 3;
        for (const Corner of Session.Corners)
        {
            const Share = Corner.Id.endsWith('Left') ? 1.04 : 1;
            Corner.Surface += (SurfaceTarget * Share - Corner.Surface) * Math.min(1, Step / 7);
            Corner.Core += (Corner.Surface - 13 - Corner.Core) * Math.min(1, Step / 34);
            Corner.Pressure = Corner.ColdPressure + (Corner.Surface - 88) * 0.055;
            const Compound = Catalog.ResolveCompound(Corner.Compound);
            Corner.Wear = Math.min(100, Corner.Wear + Step * 0.012 * Session.LatG * Session.LatG * Compound.WearRate + Step * 0.0004);
        }

        // ⑥ brakes: heat in with pedal and speed, out with airflow
        const Ambient = Specification.Thermal.AmbientTemperature;
        const FrontShare = Drivetrain.BrakeBalance / 100;
        const Heat = Session.Brake * Speed * 0.42;
        Session.BrakeTemperature[0] += (Heat * FrontShare - (Session.BrakeTemperature[0] - Ambient) * (0.0045 + Speed * 0.00042)) * Step;
        Session.BrakeTemperature[1] += (Heat * (1 - FrontShare) - (Session.BrakeTemperature[1] - Ambient) * (0.0055 + Speed * 0.0005)) * Step;
        Session.Corners.forEach(Corner =>
        {
            const Front = Corner.Id.includes('Front');
            const Axle = Session.BrakeTemperature[Front ? 0 : 1];
            Corner.BrakeTemperature += (Axle * (Front ? 1 : 0.82) - Corner.BrakeTemperature) * Math.min(1, Step / 3);
        });

        // ⑦ aids and the state of the wing
        Session.TcActive = Specification.Aids.TractionControl && Session.Throttle > 0.75 && Session.Gear <= 1 && SpeedKmh < 90;
        Session.AbsActive = Specification.Aids.AntiLockBrakes && Session.Brake > 0.72 && SpeedKmh > 40;
        const OnStraight = Radius === Infinity;
        Session.Drs = Specification.Aids.DrsEnabled ? (OnStraight && SpeedKmh > 210 ? 'Open' : 'Available') : 'Locked';

        // ⑧ distance, the lap, and the traces
        Session.LapDistance += Speed * Step;
        Session.TotalDistance += Speed * Step;
        Session.LapTime += Step;
        Session.Elapsed += Step;
        Session.History.push([Number(Session.Elapsed.toFixed(2)), Number(SpeedKmh.toFixed(2))]);
        while (Session.History.length && Session.History[0][0] < Session.Elapsed - 20) Session.History.shift();

        if (Session.LapDistance >= LapLength)
        {
            Session.LapDistance -= LapLength;
            Session.LastLapBurn = Session.LapBurn;
            Session.LapBurn = 0;
            Session.LapHistory.push([Session.Lap, Number(Session.Fuel.toFixed(2))]);
            AddEvent(Session, 'TELEMETRY', `Lap ${Session.Lap} complete`,
                     `${FormatClock(Session.LapTime)} · ${Session.Fuel.toFixed(1)} L left`, 'live');
            Session.Lap += 1;
            Session.LapTime = 0;
        }
        return Session;
    }

    function FormatClock(Seconds)
    {
        const Whole = Math.max(0, Math.floor(Seconds));
        const Minutes = Math.floor(Whole / 60);
        const Rest = Whole % 60;
        const Tenths = Math.floor((Seconds - Whole) * 10);
        return `${String(Minutes).padStart(2, '0')}:${String(Rest).padStart(2, '0')}.${Tenths}`;
    }

    function ResolveLapSeconds(Session)
    {
        const Limits = Session.Track.Limits;
        const Mean = Limits.reduce((Total, Limit) => Total + Limit, 0) / Math.max(1, Limits.length);
        return Session.Track.Length / Math.max(6, Mean);
    }

    function ResolveBurnPerLap(Session)
    {
        if (Session.LastLapBurn > 0.05) return Session.LastLapBurn;
        // 📝 Before the first lap is complete the estimate is the average burn this session has measured, over the lap
        //    the table describes. It uses the average, not the instant, so a lift off the throttle does not double the range.
        const Seconds = Math.max(1, Session.Elapsed);
        const Average = Session.Elapsed > 5 ? Session.BurnTotal / Seconds : Session.Flow / 60;
        return Math.max(0.2, Average * ResolveLapSeconds(Session));
    }

    function ResolveLapsOfFuel(Session)
    {
        return Session.Fuel / ResolveBurnPerLap(Session);
    }

    function Snapshot(Session)
    {
        return {
            isRunning: Session.isRunning, isDry: Session.isDry,
            Elapsed: Session.Elapsed, Lap: Session.Lap, LapTime: Session.LapTime,
            Speed: Session.Speed * 3.6, Gear: Session.Gear + 1, Rpm: Session.Rpm,
            Throttle: Session.Throttle * 100, Brake: Session.Brake * 100,
            Steering: Session.Steering, LatG: Session.LatG, LongG: Session.LongG,
            Fuel: Session.Fuel, Capacity: Session.Capacity, Flow: Session.Flow,
            LapsOfFuel: ResolveLapsOfFuel(Session), BurnPerLap: ResolveBurnPerLap(Session),
            Coolant: Session.Coolant, Oil: Session.Oil, OilPressure: Session.OilPressure,
            Boost: Session.Boost, Power: Session.Power, Torque: Session.Torque,
            Ers: Session.Ers, Drs: Session.Drs, TcActive: Session.TcActive, AbsActive: Session.AbsActive,
            Corners: Session.Corners.map(Corner => ({ ...Corner })),
            BrakeTemperature: Session.BrakeTemperature.slice(),
            History: Session.History.slice()
        };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                        THE DOCK
    //------------------------------------------------------------------------------------------------------------------------

    let ActiveSession = null;
    let ActiveTimer = null;
    let AttachedNode = null;

    function Detach()
    {
        if (ActiveTimer) { clearInterval(ActiveTimer); ActiveTimer = null; }
        if (ActiveSession) ActiveSession.isRunning = false;
        AttachedNode = null;
    }

    function FormatVolume(Quantity, Context, Digits)
    {
        return Catalog.FormatQuantity(Quantity, Digits, 'Volume', Context.UnitSystem());
    }

    function FormatSpeed(Quantity, Context, Digits)
    {
        return Catalog.FormatQuantity(Quantity, Digits, 'Speed', Context.UnitSystem());
    }

    function ConstructMetricRow(Parent, ClassName)
    {
        const Metric = C.Construct('div', ClassName);
        Parent.append(Metric);
        return Metric;
    }

    function ConstructShiftStrip(Parent)
    {
        const Strip = C.Construct('div', 'shift-strip');
        const Leds = [];
        for (let Index = 0; Index < 12; ++Index)
        {
            const Led = C.Construct('span', 'shift-led');
            Leds.push(Led);
            Strip.append(Led);
        }
        Parent.append(Strip);
        return Leds;
    }

    function ConstructPedalRow(Parent, Label, ClassName)
    {
        const Fill = C.Construct('div', 'pedal-fill ' + ClassName);
        const Value = C.Construct('span', 'pedal-value', '0 %');
        Parent.append(C.Construct('div', 'pedal-row', [
            C.Construct('span', 'pedal-label', Label),
            C.Construct('div', 'pedal-track', [Fill]),
            Value
        ]));
        return { Fill, Value };
    }

    // 📝 The dock is what the driver sees: no editing, no record writes, and one way back to the authoring inspector.
    function Construct(Parent, Context)
    {
        Detach();
        const Specification = Context.Specification;
        const Profiles = Context.Profiles;
        const Tyres = Context.Tyres;
        const Session = ActiveSession && ActiveSession.Specification === Specification
            ? ActiveSession
            : Create(Specification, Profiles, Tyres);
        ActiveSession = Session;
        Session.isRunning = true;

        const Node = C.Construct('div', 'telemetry');
        Parent.append(Node);
        AttachedNode = Node;

        // ① the header: who is talking, and how to leave
        const PauseButton = C.Construct('button', 'telemetry-pause', [C.ConstructGlyph('Pause'), C.Construct('span', null, 'Pause')]);
        PauseButton.type = 'button';
        const OpenButton = C.Construct('button', 'pill-button accent small',
            [C.ConstructGlyph('Sliders'), C.Construct('span', null, 'Vehicle inspector')]);
        OpenButton.type = 'button';
        OpenButton.addEventListener('click', () => Context.OnMode('Inspector'));
        const LiveDot = C.Construct('i');
        const LiveName = C.Construct('span', 'telemetry-name', `${Specification.Identity.Name} · live`);
        const LiveTag = C.Construct('span', 'telemetry-tag', 'DEBUG · READ-ONLY');
        Node.append(C.Construct('div', 'telemetry-head', [
            C.Construct('div', 'telemetry-identity', [LiveDot, LiveName, LiveTag]),
            C.Construct('span', 'spacer'),
            PauseButton
        ]));
        Node.append(C.Construct('div', 'telemetry-note',
            'Attached to the vehicle at runtime for debugging. It reads the record and never writes to it; the scene object '
            + 'does not change.'));

        // ② speed, gear and the engine strip
        const SpeedMetric = C.Construct('div', 'metric-big');
        const SpeedUnit = C.Construct('div', 'telemetry-unit', 'km/h');
        const GearMetric = C.Construct('div', 'telemetry-gear-value');
        const RpmValue = C.Construct('span', 'telemetry-rpm-value', '0');
        const SpeedCell = C.Construct('div', 'telemetry-speed', [SpeedMetric, SpeedUnit]);
        const GearCell = C.Construct('div', 'telemetry-gear', [GearMetric, C.Construct('div', 'telemetry-unit', 'gear')]);
        const RpmRow = C.Construct('div', 'telemetry-rpm-row', [
            C.Construct('span', 'telemetry-rpm-label', 'rpm'), RpmValue,
            C.Construct('span', 'telemetry-rpm-limit', 'limiter 7800')
        ]);
        SpeedCell.dataset.quantity = 'Speed';
        GearCell.dataset.quantity = 'Ratio';
        RpmRow.dataset.quantity = 'Rpm';
        const Hero = C.Construct('section', 'telemetry-hero', [
            C.Construct('div', 'telemetry-hero-top', [SpeedCell, GearCell]), RpmRow
        ]);
        Node.append(Hero);
        const Leds = ConstructShiftStrip(Hero);
        const Throttle = ConstructPedalRow(Hero, 'Throttle', 'throttle');
        const Brake = ConstructPedalRow(Hero, 'Brake', 'brake');
        const SteeringValue = C.Construct('b', null, '0°');
        const LatValue = C.Construct('b', null, '0.00 g');
        const LongValue = C.Construct('b', null, '0.00 g');
        Hero.append(C.Construct('div', 'telemetry-gforce', [
            C.Construct('div', 'gforce-cell', [C.Construct('span', null, 'Steering'), SteeringValue]),
            C.Construct('div', 'gforce-cell', [C.Construct('span', null, 'Lateral'), LatValue]),
            C.Construct('div', 'gforce-cell', [C.Construct('span', null, 'Longitudinal'), LongValue])
        ]));

        // ③ fuel — the number a driver asks about first
        const FuelCard = C.SectionCard(Node, { Id: 'TelemetryFuel', Title: 'Fuel', Glyph: 'Gauge', Colour: '#8fc7b8',
                                               Status: () => Session.isDry ? 'dry' : `${Session.Fuel.toFixed(1)} L` });
        const FuelLevel = C.Construct('div', 'telemetry-fuel-level');
        const FuelShare = C.Construct('div', 'telemetry-fuel-share');
        const FuelFill = C.Construct('div', 'telemetry-fuel-fill');
        const Reserve = C.Construct('div', 'telemetry-fuel-reserve');
        const FuelBar = C.Construct('div', 'telemetry-fuel-bar', [FuelFill, Reserve]);
        const FuelLaps = C.Construct('div', 'telemetry-fuel-laps');
        const FuelFoot = C.Construct('div', 'telemetry-fuel-foot');
        FuelLevel.dataset.quantity = 'Volume';
        FuelShare.dataset.quantity = 'Volume';
        FuelLaps.dataset.quantity = 'Volume';
        FuelFoot.dataset.quantity = 'Volume';
        FuelCard.Inner.append(
            C.Construct('div', 'telemetry-fuel-head', [FuelLevel, C.Construct('span', 'spacer'), FuelShare]),
            FuelBar,
            FuelLaps,
            FuelFoot
        );

        // ④ engine, in the same readout tiles the inspector uses
        const EngineCard = C.SectionCard(Node, { Id: 'TelemetryEngine', Title: 'Engine and fluids', Glyph: 'Zap', Colour: '#e8a84a',
                                                Status: () => `${Session.Power.toFixed(0)} kW` });
        const EngineGrid = C.Construct('div', 'tile-grid four');
        EngineCard.Inner.append(EngineGrid);
        C.ReadoutTile(EngineGrid, { Label: 'Coolant', Quantity: 'Temperature', Glyph: 'Thermometer',
            Resolve: () => { const Reading = Catalog.FormatQuantity(Session.Coolant, 0, 'Temperature', Context.UnitSystem());
                             return [Reading.Text, Reading.Unit, Session.Coolant > 112 ? 'danger' : Session.Coolant > 104 ? 'warm' : 'live']; } });
        C.ReadoutTile(EngineGrid, { Label: 'Oil', Quantity: 'Temperature', Glyph: 'Thermometer',
            Resolve: () => { const Reading = Catalog.FormatQuantity(Session.Oil, 0, 'Temperature', Context.UnitSystem());
                             return [Reading.Text, Reading.Unit, Session.Oil > 126 ? 'danger' : Session.Oil > 118 ? 'warm' : null]; } });
        C.ReadoutTile(EngineGrid, { Label: 'Oil pressure', Quantity: 'Pressure', Glyph: 'Activity',
            Resolve: () => [Session.OilPressure.toFixed(1), 'bar', Session.OilPressure < 1.4 ? 'danger' : null] });
        C.ReadoutTile(EngineGrid, { Label: 'Boost', Quantity: 'Boost', Glyph: 'Wind',
            Resolve: () => [Session.Boost.toFixed(2), 'bar', null] });
        C.ReadoutTile(EngineGrid, { Label: 'Power', Quantity: 'Power', Glyph: 'Gauge',
            Resolve: () => { const Reading = Catalog.FormatQuantity(Session.Power, 0, 'Power', Context.UnitSystem());
                             return [Reading.Text, Reading.Unit, null]; } });
        C.ReadoutTile(EngineGrid, { Label: 'Torque', Quantity: 'Torque', Glyph: 'Zap',
            Resolve: () => { const Reading = Catalog.FormatQuantity(Session.Torque, 0, 'Torque', Context.UnitSystem());
                             return [Reading.Text, Reading.Unit, null]; } });

        // ⑤ tyres and brakes, one card per corner, read-only
        const TyreCard = C.SectionCard(Node, { Id: 'TelemetryTyres', Title: 'Tyres and brakes', Glyph: 'Tyre', Colour: '#8fc7b8',
                                               Status: () => `${Session.Corners.filter(Corner => Corner.Enclosed).length} enclosed` });
        const TyreGrid = C.Construct('div', 'instrument-grid two');
        TyreCard.Inner.append(TyreGrid);
        for (const Corner of Session.Corners)
        {
            const Copy = Corner;
            C.InstrumentCard(TyreGrid, { Label: Copy.Code, Quantity: 'Pressure', Resolve: () => ({
                Name: `${Copy.Code}`,
                Text: Catalog.FormatQuantity(Copy.Pressure, 0, 'Pressure', Context.UnitSystem()).Text,
                Unit: Catalog.ResolveUnitSymbol('Pressure', Context.UnitSystem()),
                Colour: Catalog.ResolveCompound(Copy.Compound).Colour,
                ValueTone: Copy.Enclosed ? C.TemperatureTone(Copy.Surface, Catalog.ResolveCompound(Copy.Compound).WindowLow,
                                                             Catalog.ResolveCompound(Copy.Compound).WindowHigh) : null,
                Status: `${Catalog.FormatQuantity(Copy.Surface, 0, 'Temperature', Context.UnitSystem()).Text} `
                      + `${Catalog.ResolveUnitSymbol('Temperature', Context.UnitSystem())} · ${Copy.Wear.toFixed(0)} % wear`,
                Tone: Copy.Enclosed ? C.TemperatureTone(Copy.Surface, Catalog.ResolveCompound(Copy.Compound).WindowLow,
                                                        Catalog.ResolveCompound(Copy.Compound).WindowHigh) : 'muted'
            }) });
        }

        // ⑥ what the driver does not see in a mirror: aids, wing, hybrid
        const AidRow = C.Construct('div', 'telemetry-aids');
        TyreCard.Inner.append(AidRow);
        const AidChips = [
            ['TC', () => Session.TcActive ? 'TC · working' : 'TC · 4', () => Session.TcActive ? 'on' : 'idle'],
            ['ABS', () => Session.AbsActive ? 'ABS · working' : 'ABS · 3', () => Session.AbsActive ? 'warm' : 'idle'],
            ['DRS', () => `DRS · ${Session.Drs}`, () => Session.Drs === 'Open' ? 'on' : Session.Drs === 'Available' ? 'idle' : 'off'],
            ['ERS', () => `ERS · ${(Session.Ers * 100).toFixed(0)} %`, () => Session.Ers < 0.2 ? 'off' : 'on'],
            ['Bias', () => `BIAS · ${Specification.Drivetrain.BrakeBalance} % F`, () => 'idle']
        ].map(([Key, Text, Tone]) =>
        {
            const Chip = C.Construct('span', 'aid-chip', Key);
            AidRow.append(Chip);
            return { Chip, Text, Tone };
        });
        const BrakeRow = C.Construct('div', 'telemetry-brake-row');
        TyreCard.Inner.append(BrakeRow);

        // ⑦ the last twenty seconds, drawn with the same chart the inspector uses
        const TraceCard = C.SectionCard(Node, { Id: 'TelemetryTrace', Title: 'Speed · last 20 s', Glyph: 'Activity', Colour: '#9fb4d6',
                                                Status: () => `${(Session.Speed * 3.6).toFixed(0)} km/h` });
        const TracePoints = [];
        TraceCard.Inner.append(C.Construct('div', 'telemetry-trace-note',
            'The prototype drives a synthetic lap: straights joined by corners, with the authored torque curve and the '
            + 'authored tyre temperatures. These are not measured values.'));
        // 📝 The window follows the car: the vertical scale is set from the fastest sample in the last twenty seconds.
        const TraceAxis = { Label: 'Speed', Unit: 'Speed', Minimum: 0, Maximum: 300, MajorStep: 75 };
        C.Register(() =>
        {
            TracePoints.length = 0;
            let Peak = 100;
            for (const [Second, Value] of Session.History)
            {
                TracePoints.push([Second - Math.max(0, Session.Elapsed - 20), Value]);
                Peak = Math.max(Peak, Value);
            }
            TraceAxis.Maximum = Math.min(400, Math.max(140, Math.ceil((Peak + 24) / 80) * 80));
            TraceAxis.MajorStep = TraceAxis.Maximum / 4;
        });
        C.SeriesChart(TraceCard.Inner, {
            Height: 104, UnitSystem: Context.UnitSystem,
            XAxis: { Label: 'Seconds', Unit: 'Seconds', Minimum: 0, Maximum: 20, MajorStep: 5 },
            Series: [{ Id: 'SpeedTrace', Points: TracePoints, Axis: TraceAxis, Colour: '#8fc7b8', Area: true, Width: 1.5 }],
            Caption: false
        });

        // ⑧ the switch back, repeated where the driver's hand already is
        Node.append(C.Construct('div', 'telemetry-foot', [
            C.Construct('span', 'telemetry-foot-note', 'Read-only while driving. Editing lives in the inspector · `c` cycles the text colour.'),
            OpenButton
        ]));

        // ⑨ one refresher paints the dock; the interval only advances the model
        C.Register(() =>
        {
            const Reading = FormatSpeed(Session.Speed * 3.6, Context, 0);
            C.ConstructMetric(SpeedMetric, Reading.Text, null);
            SpeedUnit.textContent = Reading.Unit;
            C.ConstructMetric(GearMetric, String(Session.Gear + 1));
            RpmValue.textContent = Session.Rpm.toFixed(0);
            Hero.classList.toggle('dry', Session.isDry);
            const Fraction = Math.max(0, Math.min(1, Session.Rpm / Math.max(1, Specification.Engine.Redline)));
            // 📝 The strip starts at half the limiter and fills to the red line: idle lights nothing.
            Leds.forEach((Led, Index) =>
            {
                const Threshold = 0.55 + 0.45 * (Index + 1) / Leds.length;
                Led.className = 'shift-led'
                    + (Fraction >= Threshold ? (Fraction >= 0.95 ? ' hot' : Fraction >= 0.86 ? ' warm' : ' lit') : '');
            });
            Throttle.Fill.style.width = `${(Session.Throttle * 100).toFixed(0)}%`;
            Throttle.Value.textContent = `${(Session.Throttle * 100).toFixed(0)} %`;
            Brake.Fill.style.width = `${(Session.Brake * 100).toFixed(0)}%`;
            Brake.Value.textContent = `${(Session.Brake * 100).toFixed(0)} %`;
            SteeringValue.textContent = `${Session.Steering > 0 ? '+' : ''}${Session.Steering.toFixed(0)}°`;
            LatValue.textContent = `${Session.LatG.toFixed(2)} g`;
            LongValue.textContent = `${Session.LongG >= 0 ? '+' : ''}${Session.LongG.toFixed(2)} g`;

            const Level = FormatVolume(Session.Fuel, Context, 1);
            const Share = Session.Fuel / Math.max(1e-6, Session.Capacity) * 100;
            FuelLevel.textContent = '';
            C.ConstructMetric(FuelLevel, Level.Text, Level.Unit);
            FuelShare.textContent = `${Share.toFixed(0)} % of ${FormatVolume(Session.Capacity, Context, 0).Text} `
                + `${FormatVolume(Session.Capacity, Context, 0).Unit}`;
            FuelFill.style.width = `${Math.max(0, Share).toFixed(1)}%`;
            const ReserveShare = ResolveFuelReserve(Specification);
            Reserve.style.left = `${ReserveShare}%`;
            const Laps = ResolveLapsOfFuel(Session);
            const LapBurn = FormatVolume(ResolveBurnPerLap(Session), Context, 2);
            const Tone = Session.isDry || Share <= ReserveShare ? 'danger' : Share <= ReserveShare * 2.4 ? 'warm' : 'live';
            FuelCard.Card.classList.remove('tone-live', 'tone-warm', 'tone-danger');
            FuelCard.Card.classList.add('tone-' + Tone);
            // 📝 Range is only printed once a lap has been measured: before that the dock says so instead of guessing.
            FuelLaps.textContent = '';
            if (Session.LastLapBurn > 0.05)
                FuelLaps.append(C.Construct('b', null, Laps >= 10 ? Laps.toFixed(0) : Laps.toFixed(1)), document.createTextNode(' laps of fuel'));
            else
                FuelLaps.append(C.Construct('b', null, '—'), document.createTextNode(' measuring the first lap'));
            const Measured = Session.LastLapBurn > 0.05;
            FuelFoot.textContent = Measured
                ? `${LapBurn.Text} ${LapBurn.Unit}/lap · ${Session.Flow.toFixed(2)} L/min · reserve at ${ReserveShare} %`
                : `${Session.Flow.toFixed(2)} L/min now · reserve at ${ReserveShare} % · burn is measured after a lap`;

            AidChips.forEach(Chip =>
            {
                Chip.Chip.textContent = Chip.Text();
                Chip.Chip.className = 'aid-chip ' + Chip.Tone();
            });
            BrakeRow.textContent = '';
            ['Front', 'Rear'].forEach((Axle, Index) =>
            {
                const Reading = Catalog.FormatQuantity(Session.BrakeTemperature[Index], 0, 'Temperature', Context.UnitSystem());
                BrakeRow.append(C.Construct('div', 'brake-cell', [
                    C.Construct('span', null, `${Axle} discs`),
                    C.Construct('b', null, `${Reading.Text} ${Reading.Unit}`)
                ]));
            });
            PauseButton.className = 'telemetry-pause' + (Session.isRunning ? '' : ' paused');
            PauseButton.lastChild.textContent = Session.isRunning ? 'Pause' : 'Resume';
            if (PauseButton.dataset.running !== String(Session.isRunning))
            {
                PauseButton.dataset.running = String(Session.isRunning);
                PauseButton.firstChild.replaceWith(C.ConstructGlyph(Session.isRunning ? 'Pause' : 'Play'));
            }
            Node.classList.toggle('paused', !Session.isRunning);
            LiveTag.textContent = Session.isRunning ? 'DEBUG · READ-ONLY' : 'PAUSED · READ-ONLY';
        });

        PauseButton.addEventListener('click', () =>
        {
            Session.isRunning = !Session.isRunning;
            if (Context.OnEvent) Context.OnEvent({ Channel: 'TELEMETRY', Message: Session.isRunning ? 'Telemetry resumed' : 'Telemetry paused',
                                                    Detail: 'the dock stops sampling; the record is untouched', Tone: 'warm' });
            C.Broadcast();
        });

        const Rate = Math.max(1, (Specification.Telemetry && Specification.Telemetry.SampleRate) || 30);
        ActiveTimer = setInterval(() =>
        {
            if (!Session.isRunning) return;
            Advance(Session, 1 / Rate);
            if (Session.Events.length)
            {
                for (const Event of Session.Events.splice(0)) if (Context.OnEvent) Context.OnEvent(Event);
            }
            C.Broadcast();
        }, Math.round(1000 / Rate));

        C.Broadcast();
        return Session;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       EXPORTS
    //------------------------------------------------------------------------------------------------------------------------

    return {
        LapLength, ConfigureTrack, SampleTrack, Create, Advance, Snapshot, Construct, Detach, FormatClock,
        ResolveLapsOfFuel, ResolveBurnPerLap, Active: () => ActiveSession
    };
})();

if (typeof module !== 'undefined' && module.exports) module.exports = VehicleTelemetry;
