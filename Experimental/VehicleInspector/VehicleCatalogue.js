/*==========================================================================================================================================
                                                        VEHICLECATALOGUE.JS
==========================================================================================================================================*/
// 🧩 Vehicle and tyre specifications, unit conversion, curve evaluation, performance estimates and the TOML record.
'use strict';

const VehicleCatalogue = (() =>
{
    //------------------------------------------------------------------------------------------------------------------------
    //                                                   UNIT DEFINITIONS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Canonical quantities are the numbers the C++ specification stores (kg, mm, m/s², N·m, kW, kPa, °C, L, N).
    //    Every readout converts once, so the imperial view is a display concern and never a second source of truth.
    const UnitDefinitions = {
        Mass:        { Metric: { Symbol: 'kg',   Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'lb',      Scale: 2.20462262,   Offset: 0 } },
        Length:      { Metric: { Symbol: 'mm',   Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'in',      Scale: 0.0393700787, Offset: 0 } },
        Distance:    { Metric: { Symbol: 'm',    Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'ft',      Scale: 3.280839895,  Offset: 0 } },
        Speed:       { Metric: { Symbol: 'km/h', Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'mph',     Scale: 0.621371192,  Offset: 0 } },
        Torque:      { Metric: { Symbol: 'Nm',   Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'lb·ft',   Scale: 0.737562149,  Offset: 0 } },
        Power:       { Metric: { Symbol: 'kW',   Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'hp',      Scale: 1.34102209,   Offset: 0 } },
        Pressure:    { Metric: { Symbol: 'kPa',  Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'psi',     Scale: 0.145037738,  Offset: 0 } },
        Temperature: { Metric: { Symbol: '°C',   Scale: 1,            Offset: 0 }, Imperial: { Symbol: '°F',      Scale: 1.8,          Offset: 32 } },
        Volume:      { Metric: { Symbol: 'L',    Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'gal',     Scale: 0.264172052,  Offset: 0 } },
        Force:       { Metric: { Symbol: 'N',    Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'lbf',     Scale: 0.224808943,  Offset: 0 } },
        Area:        { Metric: { Symbol: 'm²',   Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'ft²',     Scale: 10.7639104,   Offset: 0 } },
        SpecificPower:{ Metric: { Symbol: 'kW/t', Scale: 1,           Offset: 0 }, Imperial: { Symbol: 'hp/ton',  Scale: 1.34102209,   Offset: 0 } },
        Rpm:         { Metric: { Symbol: 'rpm',  Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'rpm',     Scale: 1,            Offset: 0 } },
        Percent:     { Metric: { Symbol: '%',    Scale: 1,            Offset: 0 }, Imperial: { Symbol: '%',       Scale: 1,            Offset: 0 } },
        Degree:      { Metric: { Symbol: '°',    Scale: 1,            Offset: 0 }, Imperial: { Symbol: '°',       Scale: 1,            Offset: 0 } },
        Ratio:       { Metric: { Symbol: '×',    Scale: 1,            Offset: 0 }, Imperial: { Symbol: '×',       Scale: 1,            Offset: 0 } },
        Seconds:     { Metric: { Symbol: 's',    Scale: 1,            Offset: 0 }, Imperial: { Symbol: 's',       Scale: 1,            Offset: 0 } },
        Litres:         { Metric: { Symbol: 'L',     Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'L',        Scale: 1,             Offset: 0 } },
        Flow:           { Metric: { Symbol: 'kg/h',  Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'lb/h',     Scale: 2.20462262,    Offset: 0 } },
        Boost:          { Metric: { Symbol: 'bar',   Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'psi',      Scale: 14.5037738,    Offset: 0 } },
        Inertia:        { Metric: { Symbol: 'kg·m²', Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'slug·ft²', Scale: 23.7303604,    Offset: 0 } },
        Stiffness:      { Metric: { Symbol: 'kN/m',  Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'lbf/in',   Scale: 5.71014716,    Offset: 0 } },
        Damping:        { Metric: { Symbol: 'N·s/m', Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'lbf·s/in', Scale: 0.00571014716, Offset: 0 } },
        Modulus:        { Metric: { Symbol: 'MPa',   Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'psi',      Scale: 145.037738,    Offset: 0 } },
        Frequency:      { Metric: { Symbol: 'Hz',    Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'Hz',       Scale: 1,             Offset: 0 } },
        Milliseconds:   { Metric: { Symbol: 'ms',    Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'ms',       Scale: 1,             Offset: 0 } },
        DisplacementCc: { Metric: { Symbol: 'cm³',   Scale: 1,            Offset: 0 }, Imperial: { Symbol: 'in³',      Scale: 0.0610237441,  Offset: 0 } }
    };

    function ResolveUnitDefinition(UnitKey, System)
    {
        const Definition = UnitDefinitions[UnitKey] || UnitDefinitions.Ratio;
        return Definition[System === 'Imperial' ? 'Imperial' : 'Metric'];
    }

    // 📝 Display quantity = canonical × Scale + Offset; the temperature offset is the only affine case.
    function ConvertQuantity(Quantity, UnitKey, System)
    {
        const Definition = ResolveUnitDefinition(UnitKey, System);
        return Quantity * Definition.Scale + Definition.Offset;
    }

    function DecodeQuantity(Quantity, UnitKey, System)
    {
        const Definition = ResolveUnitDefinition(UnitKey, System);
        return (Quantity - Definition.Offset) / Definition.Scale;
    }

    function ResolveUnitSymbol(UnitKey, System)
    {
        return ResolveUnitDefinition(UnitKey, System).Symbol;
    }

    // 📝 The dashboard reference's metric: whole part bright, decimals dimmed, the unit small beside it.
    function FormatQuantity(Quantity, Digits, UnitKey, System)
    {
        const Display = ConvertQuantity(Quantity, UnitKey, System);
        return { Text: Number(Display).toFixed(Digits), Unit: ResolveUnitSymbol(UnitKey, System), Quantity: Display };
    }

    function FormatCount(Count)
    {
        if (Count >= 1e6) return (Count / 1e6).toFixed(1) + 'M';
        if (Count >= 1e4) return (Count / 1e3).toFixed(0) + 'k';
        if (Count >= 1e3) return (Count / 1e3).toFixed(1) + 'k';
        return String(Math.round(Count));
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     CURVE PROFILES
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Each profile names its own axes with a unit key, so one editor draws torque, temperature and pressure curves and
    //    every readout converts units without special cases. Points are [X, Y] in canonical units.
    function ConfigureCurveProfiles()
    {
        return [
            {
                Key: 'EngineTorque', Title: 'Engine torque', Glyph: 'Zap', Colour: '#e8a84a', Editable: true,
                X: { Label: 'Engine speed', Unit: 'Rpm', Minimum: 800, Maximum: 8000, MajorStep: 1000 },
                Y: { Label: 'Torque', Unit: 'Torque', Minimum: 0, Maximum: 800, MajorStep: 200 },
                Interpolation: 'Smooth',
                Points: [[950, 300], [1500, 430], [2500, 560], [3500, 620], [4500, 640], [5500, 610], [6500, 545], [7800, 410]]
            },
            {
                Key: 'EnginePower', Title: 'Engine power', Glyph: 'Gauge', Colour: '#9fb4d6', Editable: false, DerivedFrom: 'EngineTorque',
                X: { Label: 'Engine speed', Unit: 'Rpm', Minimum: 800, Maximum: 8000, MajorStep: 1000 },
                Y: { Label: 'Power', Unit: 'Power', Minimum: 0, Maximum: 500, MajorStep: 100 },
                Derivation: 'TorqueBySpeed',
                Points: []
            },
            {
                Key: 'BrakeTorque', Title: 'Brake torque', Glyph: 'Aperture', Colour: '#d95656', Editable: true,
                X: { Label: 'Road speed', Unit: 'Speed', Minimum: 0, Maximum: 320, MajorStep: 80 },
                Y: { Label: 'Brake torque', Unit: 'Torque', Minimum: 0, Maximum: 2800, MajorStep: 500 },
                Interpolation: 'Linear',
                Points: [[0, 2150], [60, 2400], [120, 2350], [200, 2050], [320, 1650]]
            },
            {
                Key: 'CoolantTemperature', Title: 'Coolant temperature', Glyph: 'Thermometer', Colour: '#e8a84a', Editable: true,
                X: { Label: 'Engine load', Unit: 'Percent', Minimum: 0, Maximum: 100, MajorStep: 25 },
                Y: { Label: 'Coolant', Unit: 'Temperature', Minimum: 40, Maximum: 140, MajorStep: 20 },
                Interpolation: 'Smooth',
                Points: [[0, 88], [20, 90], [40, 93], [60, 97], [80, 103], [100, 112]]
            },
            {
                Key: 'OilTemperature', Title: 'Oil temperature', Glyph: 'Thermometer', Colour: '#d95656', Editable: true,
                X: { Label: 'Engine speed', Unit: 'Rpm', Minimum: 800, Maximum: 8000, MajorStep: 1000 },
                Y: { Label: 'Oil', Unit: 'Temperature', Minimum: 60, Maximum: 160, MajorStep: 20 },
                Interpolation: 'Smooth',
                Points: [[950, 96], [2000, 101], [3500, 106], [5000, 111], [6500, 118], [7800, 126]]
            },
            {
                Key: 'TyreSurfaceTemperature', Title: 'Tyre surface temperature', Glyph: 'Tyre', Colour: '#8fc7b8', Editable: true,
                X: { Label: 'Slip angle', Unit: 'Degree', Minimum: 0, Maximum: 12, MajorStep: 2 },
                Y: { Label: 'Surface', Unit: 'Temperature', Minimum: 40, Maximum: 160, MajorStep: 20 },
                Interpolation: 'Smooth',
                Points: [[0, 62], [2, 78], [4, 95], [6, 108], [8, 116], [10, 120], [12, 118]]
            }
        ];
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       CURVE MATH
    //------------------------------------------------------------------------------------------------------------------------

    function SortCurvePoints(Points)
    {
        return Points.map(Point => [Point[0], Point[1]]).sort((Left, Right) => Left[0] - Right[0]);
    }

    function ResolveCurvePoints(Profile, Specification, Profiles)
    {
        if (Profile.DerivedFrom && Profile.Derivation === 'TorqueBySpeed')
        {
            const Source = (Profiles || []).find(Candidate => Candidate.Key === Profile.DerivedFrom);
            if (!Source) return [];
            const Samples = 96;
            const Output = [];
            for (let Index = 0; Index <= Samples; ++Index)
            {
                const Speed = Profile.X.Minimum + (Profile.X.Maximum - Profile.X.Minimum) * Index / Samples;
                Output.push([Speed, EvaluateCurve(Source, Speed, Specification, Profiles) * Speed * (2 * Math.PI / 60) / 1000]);
            }
            return Output;
        }
        return SortCurvePoints(Profile.Points);
    }

    // 📝 Fritsch–Carlson monotone tangents: the curve passes through every authored point and never overshoots between them,
    //    so a hand-placed point cannot produce a torque spike the engine does not have.
    function ResolveMonotoneTangents(Points)
    {
        const Count = Points.length;
        if (Count < 2) return new Array(Count).fill(0);
        const Slopes = [];
        for (let Index = 0; Index < Count - 1; ++Index)
            Slopes.push((Points[Index + 1][1] - Points[Index][1]) / Math.max(1e-9, Points[Index + 1][0] - Points[Index][0]));
        const Tangents = new Array(Count).fill(0);
        Tangents[0] = Slopes[0];
        Tangents[Count - 1] = Slopes[Count - 2];
        for (let Index = 1; Index < Count - 1; ++Index)
        {
            if (Slopes[Index - 1] * Slopes[Index] <= 0) { Tangents[Index] = 0; continue; }
            const WeightLeft  = 2 * (Points[Index + 1][0] - Points[Index][0]) + (Points[Index][0] - Points[Index - 1][0]);
            const WeightRight = (Points[Index + 1][0] - Points[Index][0]) + 2 * (Points[Index][0] - Points[Index - 1][0]);
            Tangents[Index] = (WeightLeft + WeightRight) / (WeightLeft / Slopes[Index - 1] + WeightRight / Slopes[Index]);
        }
        return Tangents;
    }

    function EvaluateCurveSamples(Points, Interpolation, X)
    {
        if (!Points || Points.length === 0) return 0;
        if (Points.length === 1) return Points[0][1];
        if (X <= Points[0][0]) return Points[0][1];
        if (X >= Points[Points.length - 1][0]) return Points[Points.length - 1][1];
        let Upper = 1;
        while (Upper < Points.length - 1 && Points[Upper][0] < X) ++Upper;
        const Lower  = Upper - 1;
        const Span   = Math.max(1e-9, Points[Upper][0] - Points[Lower][0]);
        const Share  = (X - Points[Lower][0]) / Span;
        if (Interpolation !== 'Smooth') return Points[Lower][1] + Share * (Points[Upper][1] - Points[Lower][1]);
        const Start  = ResolveTangents(Points);
        const Share2 = Share * Share, Share3 = Share2 * Share;
        const Basis0 = 2 * Share3 - 3 * Share2 + 1, Basis1 = Share3 - 2 * Share2 + Share;
        const Basis2 = -2 * Share3 + 3 * Share2, Basis3 = Share3 - Share2;
        return Basis0 * Points[Lower][1] + Basis1 * Span * Start[Lower] + Basis2 * Points[Upper][1] + Basis3 * Span * Start[Upper];
    }

    // 📝 Tangents depend on the whole point list, so they are retained per list identity and dropped when a point moves.
    const TangentTable = new WeakMap();

    function ResolveTangents(Points)
    {
        let Tangents = TangentTable.get(Points);
        if (!Tangents)
        {
            Tangents = ResolveMonotoneTangents(Points);
            TangentTable.set(Points, Tangents);
        }
        return Tangents;
    }

    function EvaluateCurve(Profile, X, Specification, Profiles)
    {
        const Points = ResolveCurvePoints(Profile, Specification, Profiles);
        return EvaluateCurveSamples(Points, Profile.Interpolation, X);
    }

    // 📝 A moved or removed point changes the tangents of its neighbours; the retained entry must go with it.
    function InvalidateCurvePointTangents(Profile)
    {
        TangentTable.delete(Profile.Points);
    }

    function ResolveCurvePeak(Profile, Specification, Profiles)
    {
        const Points = ResolveCurvePoints(Profile, Specification, Profiles);
        let Peak = { X: Profile.X.Minimum, Y: -Infinity };
        for (const Point of Points) if (Point[1] > Peak.Y) Peak = { X: Point[0], Y: Point[1] };
        return Peak;
    }

    // 📝 A coarse readout at a named speed, used by the hero card and the thermal gauges.
    function EvaluateCurveAt(Profile, X, Specification, Profiles)
    {
        return EvaluateCurve(Profile, X, Specification, Profiles);
    }

    function ResolvePointMinimumGap(Profile)
    {
        return (Profile.X.Maximum - Profile.X.Minimum) * 0.01;
    }

    function InsertCurvePoint(Profile, X, Y)
    {
        const MinimumGap = ResolvePointMinimumGap(Profile);
        const ClampedX = Math.min(Profile.X.Maximum, Math.max(Profile.X.Minimum, X));
        const ClampedY = Math.min(Profile.Y.Maximum, Math.max(Profile.Y.Minimum, Y));
        const Points = Profile.Points;
        let Index = Points.findIndex(Point => Point[0] > ClampedX);
        if (Index < 0) Index = Points.length;
        // 📝 Refuse a point that would land on a neighbour; the caller reports the refusal instead of silently moving it.
        if (Index > 0 && ClampedX - Points[Index - 1][0] < MinimumGap) return -1;
        if (Index < Points.length && Points[Index][0] - ClampedX < MinimumGap) return -1;
        Points.splice(Index, 0, [ClampedX, ClampedY]);
        InvalidateCurvePointTangents(Profile);
        return Index;
    }

    function RemoveCurvePoint(Profile, Index)
    {
        if (Profile.Points.length <= 2) return false;
        if (Index < 0 || Index >= Profile.Points.length) return false;
        Profile.Points.splice(Index, 1);
        InvalidateCurvePointTangents(Profile);
        return true;
    }

    function RelocateCurvePoint(Profile, Index, X, Y)
    {
        const Points = Profile.Points;
        if (Index < 0 || Index >= Points.length) return Index;
        const MinimumGap = ResolvePointMinimumGap(Profile);
        const LowerBound = Index === 0 ? Profile.X.Minimum : Points[Index - 1][0] + MinimumGap;
        const UpperBound = Index === Points.length - 1 ? Profile.X.Maximum : Points[Index + 1][0] - MinimumGap;
        Points[Index] = [
            Math.min(UpperBound, Math.max(LowerBound, X)),
            Math.min(Profile.Y.Maximum, Math.max(Profile.Y.Minimum, Y))
        ];
        InvalidateCurvePointTangents(Profile);
        return Index;
    }

    function EncodeCurveCsv(Profile, System)
    {
        const XSymbol = ResolveUnitSymbol(Profile.X.Unit, System || 'Metric');
        const YSymbol = ResolveUnitSymbol(Profile.Y.Unit, System || 'Metric');
        const Rows = [`${Profile.Key.toLowerCase()},${XSymbol},${YSymbol}`];
        for (const Point of Profile.Points)
            Rows.push(`${Profile.Key.toLowerCase()},${ConvertQuantity(Point[0], Profile.X.Unit, System).toFixed(1)},`
                + `${ConvertQuantity(Point[1], Profile.Y.Unit, System).toFixed(2)}`);
        return Rows.join('\n');
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  VEHICLE SPECIFICATION
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 VehicleSpecification is the TOML-backed model the plan names for `VehicleSpecification.js`. Values here are the
    //    prototype's authored defaults; the C++ record reads the same keys (see EncodeVehicleRecord).
    function ConfigureVehicleSpecification()
    {
        return {
            Identity: {
                Name: 'GT-04', Maker: 'Frontier Works', Classification: 'GT3',
                KerbMass: 1284,              // [kg]   - Dry mass without driver or fuel
                Wheelbase: 2720,             // [mm]   - Front axle to rear axle
                TrackFront: 1610,            // [mm]   - Front wheel centre to centre
                TrackRear: 1585,             // [mm]   - Rear wheel centre to centre
                CentreOfGravityHeight: 412,  // [mm]   - Above the road surface
                FuelCapacity: 68,            // [L]    - Tank capacity
                RideHeight: 118              // [mm]   - Floor to road at rest
            },
            Drivetrain: {
                Layout: 'Rwd',               // [-]    - Fwd | Rwd | All
                GearCount: 6,                // [-]    - Forward gears
                GearRatios: [3.62, 2.19, 1.55, 1.19, 1.00, 0.82],
                FinalDrive: 3.42,            // [×]    - Crown wheel ratio
                DrivelineEfficiency: 0.91,   // [-]    - Crank to contact patch
                WheelRadius: 342,            // [mm]   - Loaded radius
                TyreFriction: 1.42,          // [-]    - Peak longitudinal μ
                RearMassShare: 54,           // [%]    - Static rear axle share
                RollingResistance: 0.013,    // [-]    - Crr
                DragCoefficient: 0.34,       // [-]    - Cd
                FrontalArea: 2.05,           // [m²]   - A
                DownforceAt200: 640,         // [N]    - Total downforce at 200 km/h
                BrakeTorque: 2400,           // [N·m]  - Total at full pedal
                BrakeBalance: 62,            // [%]    - Front share
                AirDensity: 1.225            // [kg/m³]- Sea level, 15 °C
            },
            Engine: {
                Displacement: 3.0,           // [L]    - Swept volume
                IdleSpeed: 950,              // [rpm]  - Idle
                Redline: 7800,               // [rpm]  - Rev limiter
                RatedTorque: 640,            // [N·m]  - Rated peak
                EngineInertia: 0.21,         // [kg·m²]- Rotating assembly
                TurboBoost: 1.1,             // [bar]  - Gauge pressure
                FuelBurn: 62,                // [kg/h] - At full load
                ThermostatOpening: 84        // [°C]   - Coolant opens
            },
            Thermal: {
                CoolantTarget: 92,           // [°C]   - Regulated coolant
                OilTarget: 108,              // [°C]   - Regulated oil
                BrakeTarget: 540,            // [°C]   - Disc target
                AmbientTemperature: 24,      // [°C]   - Air
                CoolingCapacity: 58          // [kW]   - Heat rejected at target
            },
            Fuel: {
                Load: 61.2,                  // [L]    - In the tank when the session starts
                Reserve: 12                  // [%]    - Reserve warning at this share of the tank
            },
            Telemetry: {
                SampleRate: 30               // [Hz]   - Debug dock only; never part of the driving solver
            },
            Aids: {
                TractionControl: true, StabilityControl: true, AntiLockBrakes: true, LaunchControl: false,
                DrsEnabled: false, RevLimiter: true, Telemetry: true
            },
            Induction: {
                Mode: 'Supercharged',            // [-]    - NaturallyAspirated | Turbocharged | Supercharged
                RaceTune: false,                 // [-]    - +33 % peak boost & aggressive spark calibration
                CompressorType: 'TwinScrew',     // [-]    - Roots | TwinScrew | Centrifugal
                DriveRatio: 2.15,                // [×]    - Pulley speed ratio ω_sc / ω_crank
                DisplacementCc: 1850,            // [cm³]  - Positive-displacement volume per compressor rev
                SuperchargerMaxBoost: 0.80,      // [bar]  - Peak manifold boost from compressor
                PeakParasiticTorque: 34,         // [N·m]  - Crankshaft parasitic drag at redline WOT
                BypassParasiticPercent: 9,       // [%]    - Residual parasitic drag with bypass valve open
                BypassThrottlePercent: 12,       // [%]    - Throttle threshold closing the bypass valve
                TurboSpoolUpRate: 3.6,           // [1/s]  - Turbine spool-up rate
                TurboSpoolDownRate: 6.5,         // [1/s]  - Blow-off decay rate
                MinExhaustRpm: 1900,             // [rpm]  - Exhaust mass-flow threshold
                FullBoostRpm: 3600,              // [rpm]  - Full turbine boost engine speed
                TorqueGainPerBar: 0.58           // [×]    - Crankshaft torque gain per bar of boost
            },
            Differential: {
                Type: 'ClutchPack15Way',         // [-]    - Open | Locked | ClutchPack15Way | Torsen
                PreloadTorque: 85,               // [N·m]  - Static clutch-pack preload torque
                PowerLockPercent: 52,            // [%]    - Drive-side cam lock ratio
                CoastLockPercent: 28,            // [%]    - Overrun/braking cam lock ratio
                TorsenBiasRatio: 2.8,            // [×]    - Helical torque-bias ratio (TBR)
                FrontTorqueSplitPercent: 35,     // [%]    - Centre-diff front share when Layout = All
                ShiftDurationMs: 65,             // [ms]   - Dual-clutch / dog-box shift cut duration
                ClutchCapacity: 920              // [N·m]  - Peak clutch plate torque capacity
            },
            Suspension: {
                FrontSpringRate: 48,             // [kN/m] - Front linear coilover spring stiffness
                RearSpringRate: 54,              // [kN/m] - Rear linear coilover spring stiffness
                ProgressiveFactor: 0.28,         // [×]    - Progressive cubic spring hardening coefficient
                FrontRestLength: 340,            // [mm]   - Unloaded front strut length
                RearRestLength: 350,             // [mm]   - Unloaded rear strut length
                MinStrutLength: 230,             // [mm]   - Full jounce bump-stop limit
                MaxStrutLength: 440,             // [mm]   - Full droop rebound limit
                FrontBumpDamping: 4200,          // [N·s/m]- Front low-speed compression damping
                FrontReboundDamping: 6800,       // [N·s/m]- Front low-speed rebound damping
                RearBumpDamping: 4500,           // [N·s/m]- Rear low-speed compression damping
                RearReboundDamping: 7200,        // [N·s/m]- Rear low-speed rebound damping
                HighSpeedDampingRatio: 0.42,     // [×]    - Digressive blow-off slope above knee speed
                DamperKneeSpeed: 0.16,           // [m/s]  - Digressive damper knee velocity
                BumpStopStiffness: 185,          // [kN/m] - Polyurethane jounce bumper stiffness
                FrontStaticCamber: -2.1,         // [°]    - Static front camber angle
                RearStaticCamber: -1.6,          // [°]    - Static rear camber angle
                CamberGainPerMetre: -4.2,        // [°]    - Dynamic camber gain per 100 mm compression
                FrontAntiRollRate: 24,           // [kN/m] - Front anti-roll bar stiffness
                RearAntiRollRate: 19             // [kN/m] - Rear anti-roll bar stiffness
            },
            Steering: {
                MaxInnerAngle: 33,               // [°]    - Maximum inner front wheel steer lock
                SteeringRatio: 13.8,             // [×]    - Steering wheel to road-wheel ratio
                AckermannPercent: 78,            // [%]    - 0 % parallel, 100 % pure Ackermann
                SpeedSensitiveFloorPercent: 42,  // [%]    - High-speed authority floor
                SpeedSensitiveHalfSpeed: 105,    // [km/h] - Speed where authority reaches midpoint
                MaxSteerRate: 240,               // [°/s]  - Rack rate limit
                MechanicalTrail: 28,             // [mm]   - Caster mechanical trail for SAT feedback
                EscYawGain: 0.85,                // [×]    - Electronic stability yaw-rate gain
                EscMaxBrakePercent: 55,          // [%]    - Peak single-corner ESC brake intervention
                TcsSlipThresholdPercent: 11,     // [%]    - Traction control target longitudinal slip
                AbsSlipThresholdPercent: 14      // [%]    - Anti-lock brake target longitudinal slip
            },
            SoftBody: {
                Formulation: 'Fem3D',            // [-]    - Spring | Xpbd | Fem3D
                SlipMode: 'GritImplicitNewton',  // [-]    - GritImplicitNewton | RelaxationLength | QuasiStaticNewton
                CircumferentialNodes: 24,        // [-]    - Carcass circumferential node count
                LateralRings: 3,                 // [-]    - Tread lateral ring count (inner/crown/outer)
                VerticalStiffness: 215,          // [kN/m] - Reference carcass radial stiffness
                PressureStiffnessGain: 1.00,     // [×]    - Pneumatic stiffness scaling dK/dP
                RadialCompliance: 18,            // [µm/N] - XPBD radial spoke compliance (×1e-6 m/N)
                ShearCompliance: 34,             // [µm/N] - XPBD circumferential belt compliance (×1e-6 m/N)
                BendingCompliance: 95,           // [µm/N] - XPBD sidewall bending compliance (×1e-6 m/N)
                PneumaticVolumeWeight: 82,       // [%]    - Enclosed pneumatic cavity volume preservation
                XpbdSubsteps: 4,                 // [-]    - XPBD substeps per physics tick
                XpbdIterations: 4,               // [-]    - XPBD Gauss-Seidel constraint iterations
                MaxContactBulgePercent: 22,      // [%]    - Sidewall Poisson bulge ratio under load
                FemSubsteps: 10,                 // [-]    - Explicit symplectic Euler Hex8 FEM substeps
                FemYoungsModulusMpa: 2.6,        // [MPa]  - Neo-Hookean rubber carcass Young's modulus
                FemPoissonRatio: 0.43,           // [×]    - Nearly-incompressible rubber Poisson ratio
                FemNodeMassKg: 0.14,             // [kg]   - Lumped mass per Hex8 finite-element vertex
                FemDampingRate: 14               // [Hz]   - Rayleigh mass-proportional damping rate (1/s)
            },
            AeroActive: {
                CenterOfPressureFrontPercent: 46,// [%]    - Static aerodynamic front balance
                PitchSensitivity: 1.15,          // [×]    - Forward CoP migration per rad nose-down pitch
                DrsDragReductionPercent: 26,     // [%]    - Rear-wing drag reduction with DRS open
                DrsDownforceLossPercent: 34,     // [%]    - Rear-wing downforce loss with DRS open
                DrsActivationSpeed: 130,         // [km/h] - Minimum speed permitting DRS deployment
                AirbrakeDragAddition: 0.24,      // [×]    - Additional Cd under heavy braking
                AirbrakeDownforceGain: 380,      // [N]    - Rear airbrake downforce addition at 200 km/h
                GroundEffectOptimalHeight: 68,   // [mm]   - Venturi floor peak suction ride height
                GroundEffectPeakGain: 1.38,      // [×]    - Venturi floor peak downforce multiplier
                GroundEffectChokeHeight: 28,     // [mm]   - Ride height below which diffuser stalls
                HandbrakeTorque: 1850,           // [N·m]  - Mechanical rear axle handbrake torque
                BrakeFadeStart: 560,             // [°C]   - Rotor temperature where pad fade begins
                MaxBrakeFadePercent: 42,         // [%]    - Worst-case brake torque loss at peak fade
                LineLockFrontOnly: false         // [-]    - Holds front callipers only for burnout staging
            },
            Fluids: {
                OilGrade: '5W-40',               // [-]    - 0W-20 | 5W-30 | 5W-40 | 10W-60
                CoolantHeatCapacityKjK: 24.5,    // [×]    - Lumped block + coolant heat capacity [kJ/K]
                RadiatorAirflowGain: 0.62,       // [×]    - Speed-proportional ram-air heat rejection
                RadiatorFanThreshold: 96,        // [°C]   - Electric radiator fan cut-in temperature
                OverheatDerateStart: 116,        // [°C]   - Coolant temperature triggering limp torque cut
                SpecificFuelBurnGkwh: 248        // [×]    - Brake-specific fuel consumption [g/kWh]
            },
            Simulation: {
                OuterTickHz: 60,                 // [Hz]   - PhysicsExecutionThread outer fixed rate
                SubstepsPerOuterTick: 4,         // [-]    - Inner Jolt + tyre substeps per outer tick
                MaximumCatchUpSteps: 6,          // [-]    - Spiral-of-death frame clamp
                StationarityHold: true,          // [-]    - Coupled Coulomb-cone slope hold at rest
                ForceZoneCoupling: true          // [-]    - Couples world wind/vortex/impulse zones
            }
        };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    TYRE SPECIFICATION
    //------------------------------------------------------------------------------------------------------------------------

    const CompoundCatalogue = {
        Soft:         { Name: 'Soft',         Symbol: 'S', Colour: '#e05b5b', GripScale: 1.06, WindowLow: 78, WindowHigh: 104, WearRate: 1.35 },
        Medium:       { Name: 'Medium',       Symbol: 'M', Colour: '#e8c24a', GripScale: 1.00, WindowLow: 68, WindowHigh: 96,  WearRate: 1.00 },
        Hard:         { Name: 'Hard',         Symbol: 'H', Colour: '#dcdcdc', GripScale: 0.94, WindowLow: 58, WindowHigh: 88,  WearRate: 0.72 },
        Intermediate: { Name: 'Intermediate', Symbol: 'I', Colour: '#4cbf6f', GripScale: 0.84, WindowLow: 40, WindowHigh: 70,  WearRate: 0.90 },
        Wet:          { Name: 'Wet',          Symbol: 'W', Colour: '#4a90e2', GripScale: 0.72, WindowLow: 30, WindowHigh: 60,  WearRate: 0.85 }
    };

    // 📝 Four corners, each a record the vehicle encloses. Detaching clears Enclosed and moves the record to the pit lane;
    //    fitting a set replaces the profile and re-encloses it, which is the pit-stop edit the owner asked for.
    function ConfigureTyreRecords()
    {
        const Corners = [
            ['TyreFrontLeft', 'FL', 'Front left', 'Medium', 'SetA'],
            ['TyreFrontRight', 'FR', 'Front right', 'Medium', 'SetA'],
            ['TyreRearLeft', 'RL', 'Rear left', 'Medium', 'SetA'],
            ['TyreRearRight', 'RR', 'Rear right', 'Medium', 'SetA']
        ];
        return Corners.map(([Id, Code, Position, Compound, SetId]) => ({
            Id, Code, Position, Enclosed: true,
            SetId,                       // [-]    - Fitted set
            Compound,                    // [-]    - CompoundCatalogue key
            Pressure: Code[0] === 'F' ? 186 : 174,  // [kPa] - Cold inflation
            SurfaceTemperature: 88,      // [°C]   - Tread surface
            CoreTemperature: 74,         // [°C]   - Carcass
            Wear: 18,                    // [%]    - Share of usable life consumed
            StintLaps: 12,               // [-]    - Laps on this tyre
            TreadDepth: 6.4,             // [mm]   - Groove depth
            Radius: 342,                 // [mm]   - Loaded radius
            Width: 305,                  // [mm]   - Section width
            CarcassPressure: 240,        // [kPa]  - Inflation at temperature
            ContactPatchLoad: 3.1,       // [kN]   - Standing load
            BeltTemperature: 81,         // [°C]   - 3-node intermediate carcass belt temperature
            GasTemperature: 76,          // [°C]   - Internal pneumatic cavity gas temperature (PV=mRT)
            PacejkaLongitudinalShape: 1.65, // [×] - MF6.1 longitudinal shape factor PCx1
            PacejkaLateralShape: 1.32,   // [×]    - MF6.1 lateral shape factor PCy1
            LoadSensitivity: 0.14,       // [×]    - Friction decay per kN of normal load
            Punctured: false             // [-]    - Carcass blowout / rapid deflation state
        }));
    }

    const TyreSetCatalogue = {
        SetA: { Name: 'Set A · Medium slicks', Compound: 'Medium', Laps: 0, Scuffed: false },
        SetB: { Name: 'Set B · Soft slicks',   Compound: 'Soft',   Laps: 0, Scuffed: false },
        SetC: { Name: 'Set C · Full wets',     Compound: 'Wet',    Laps: 0, Scuffed: false }
    };

    function ResolveCompound(Key)
    {
        return CompoundCatalogue[Key] || CompoundCatalogue.Medium;
    }

    // 📝 Window position decides the coloured dot on the gauge: below the window is cold, above is overheating.
    function ResolveTemperatureWindow(CompoundKey, Temperature)
    {
        const Compound = ResolveCompound(CompoundKey);
        if (Temperature < Compound.WindowLow) return 'Cold';
        if (Temperature > Compound.WindowHigh) return 'Hot';
        return 'Optimal';
    }

    function ResolveWearStatus(Wear)
    {
        if (Wear >= 85) return 'Worn';
        if (Wear >= 55) return 'Degraded';
        return 'Fresh';
    }

    function FitTyreSet(Tyres, SetId, Compound)
    {
        for (const Tyre of Tyres)
        {
            Tyre.SetId = SetId;
            Tyre.Compound = Compound;
            Tyre.Wear = 0;
            Tyre.StintLaps = 0;
            Tyre.TreadDepth = Compound === 'Wet' || Compound === 'Intermediate' ? 8.4 : 5.6;
            Tyre.Pressure = Tyre.Code[0] === 'F' ? 186 : 174;
            Tyre.SurfaceTemperature = 62;
            Tyre.CoreTemperature = 48;
            Tyre.CarcassPressure = 236;
            Tyre.Enclosed = true;
        }
        return Tyres;
    }

    function DetachTyre(Tyre)
    {
        Tyre.Enclosed = false;
        return Tyre;
    }

    function AttachTyre(Tyre)
    {
        Tyre.Enclosed = true;
        return Tyre;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  PERFORMANCE ESTIMATES
    //------------------------------------------------------------------------------------------------------------------------

    function ResolveTorque(Specification, Profiles, Speed)
    {
        const TorqueProfile = (Profiles || []).find(Profile => Profile.Key === 'EngineTorque');
        if (!TorqueProfile) return 0;
        return Math.max(0, EvaluateCurve(TorqueProfile, Speed, Specification, Profiles));
    }

    function ResolveAeroDownforce(Drivetrain, Speed)
    {
        return Drivetrain.DownforceAt200 * Math.pow(Speed / 200, 2);
    }

    // 📝 Engine speed from road speed in one gear: ω_wheel = 𝑣 / r, then rpm = ω_wheel × ratio × final × 60 / 2π.
    function ResolveEngineSpeed(Drivetrain, Speed, GearRatio)
    {
        const Radius = Drivetrain.WheelRadius / 1000;
        return (Speed / 3.6) / Radius * GearRatio * Drivetrain.FinalDrive * 60 / (2 * Math.PI);
    }

    function ResolveWheelForce(Specification, Profiles, Speed, GearRatio)
    {
        const Drivetrain = Specification.Drivetrain;
        const Radius = Drivetrain.WheelRadius / 1000;
        const SpeedValue = ResolveEngineSpeed(Drivetrain, Speed, GearRatio);
        if (SpeedValue > Specification.Engine.Redline) return 0;
        const EngineSpeed = Math.max(Specification.Engine.IdleSpeed, SpeedValue);
        const Torque = ResolveTorque(Specification, Profiles, EngineSpeed);
        return Torque * GearRatio * Drivetrain.FinalDrive * Drivetrain.DrivelineEfficiency / Radius;
    }

    function ResolveTractionLimit(Specification, Speed, LongitudinalAcceleration)
    {
        const Drivetrain = Specification.Drivetrain;
        const Mass = Specification.Identity.KerbMass;
        const Wheelbase = Specification.Identity.Wheelbase / 1000;
        const Height = Specification.Identity.CentreOfGravityHeight / 1000;
        const Gravity = 9.81;
        const Downforce = ResolveAeroDownforce(Drivetrain, Speed);
        const Transfer = Mass * LongitudinalAcceleration * Height / Math.max(1e-6, Wheelbase);
        let Share;
        if (Drivetrain.Layout === 'Rwd') Share = Drivetrain.RearMassShare / 100;
        else if (Drivetrain.Layout === 'Fwd') Share = 1 - Drivetrain.RearMassShare / 100;
        else Share = 1;
        const Normal = Math.max(0, (Mass * Gravity + Downforce) * Share + (Drivetrain.Layout === 'Fwd' ? -Transfer : Transfer));
        return Drivetrain.TyreFriction * Normal;
    }

    function ResolveResistance(Specification, Speed)
    {
        const Drivetrain = Specification.Drivetrain;
        const Mass = Specification.Identity.KerbMass;
        const AirSpeed = Speed / 3.6;
        const Drag = 0.5 * Drivetrain.AirDensity * Drivetrain.DragCoefficient * Drivetrain.FrontalArea * AirSpeed * AirSpeed;
        const Rolling = Drivetrain.RollingResistance * Mass * 9.81;
        return Drag + Rolling;
    }

    function ResolveGearRatios(Specification)
    {
        return Specification.Drivetrain.GearRatios.slice(0, Math.max(1, Math.min(Specification.Drivetrain.GearCount,
            Specification.Drivetrain.GearRatios.length)));
    }

    function EstimateTopSpeed(Specification, Profiles)
    {
        const Ratios = ResolveGearRatios(Specification);
        let Speed = 0;
        for (let Candidate = 20; Candidate <= 460; Candidate += 0.5)
        {
            let Available = 0;
            for (const Ratio of Ratios) Available = Math.max(Available, ResolveWheelForce(Specification, Profiles, Candidate, Ratio));
            const Limit = ResolveTractionLimit(Specification, Candidate, 0);
            if (Math.min(Available, Limit) < ResolveResistance(Specification, Candidate)) break;
            Speed = Candidate;
        }
        return Speed;
    }

    // 📝 Fixed-step launch: the gear is the lowest whose engine speed stays under the limiter, wheel force is capped by the
    //    traction circle with weight transfer, and the trace is what the performance card draws. It is a prototype estimate.
    function EstimateZeroToHundred(Specification, Profiles, TargetSpeed)
    {
        const Target = TargetSpeed || 100;
        const Ratios = ResolveGearRatios(Specification);
        const Mass = Specification.Identity.KerbMass;
        const Step = 0.005;
        const Trace = [[0, 0]];
        let Speed = 0, Elapsed = 0, Acceleration = 0, Gear = 0, Distance = 0;
        let ShiftCount = 0;
        while (Elapsed < 60 && Speed < Target)
        {
            while (Gear < Ratios.length - 1 && ResolveEngineSpeed(Specification.Drivetrain, Math.max(Speed, 1), Ratios[Gear]) > Specification.Engine.Redline)
            {
                ++Gear;
                ++ShiftCount;
            }
            const WheelForce = ResolveWheelForce(Specification, Profiles, Math.max(Speed, 2), Ratios[Gear]);
            const Traction = ResolveTractionLimit(Specification, Speed, Acceleration);
            const Force = Math.min(WheelForce, Traction) - ResolveResistance(Specification, Speed);
            Acceleration = Force / Mass;
            if (Acceleration <= 0 && Speed > 1) break;
            Speed = Math.max(0, Speed + Acceleration * 3.6 * Step);
            Distance += Speed / 3.6 * Step;
            Elapsed += Step;
            if (Trace.length === 0 || Elapsed - Trace[Trace.length - 1][0] >= 0.02) Trace.push([Number(Elapsed.toFixed(3)), Speed]);
        }
        // 📝 The trace ends on the exact moment the target speed is reached, so the chart's marker sits on the line.
        Trace.push([Number(Elapsed.toFixed(3)), Number(Math.min(Speed, Math.max(Speed, Target)).toFixed(2))]);
        return { Seconds: Number(Elapsed.toFixed(2)), Trace, Distance: Number(Distance.toFixed(1)), ShiftCount, Reached: Speed >= Target };
    }

    function EstimateBrakingDistance(Specification, FromSpeed)
    {
        const Mass = Specification.Identity.KerbMass;
        const Downforce = ResolveAeroDownforce(Specification.Drivetrain, FromSpeed);
        const Friction = Specification.Drivetrain.TyreFriction;
        const Deceleration = Friction * (Mass * 9.81 + Downforce) / Mass;
        const Speed = FromSpeed / 3.6;
        return Number((Speed * Speed / (2 * Math.max(0.1, Deceleration))).toFixed(1));
    }

    function EstimatePerformance(Specification, Profiles, Tyres)
    {
        const Torque = (Profiles || []).find(Profile => Profile.Key === 'EngineTorque');
        const Power  = (Profiles || []).find(Profile => Profile.Key === 'EnginePower');
        const TorquePeak = Torque ? ResolveCurvePeak(Torque, Specification, Profiles) : { X: 0, Y: 0 };
        const PowerPeak  = Power ? ResolveCurvePeak(Power, Specification, Profiles) : { X: 0, Y: 0 };
        const Launch = EstimateZeroToHundred(Specification, Profiles, 100);
        const Compound = Tyres && Tyres.length ? ResolveCompound(Tyres[0].Compound) : ResolveCompound('Medium');
        return {
            PeakTorque: TorquePeak.Y, PeakTorqueSpeed: TorquePeak.X,
            PeakPower: PowerPeak.Y, PeakPowerSpeed: PowerPeak.X,
            ZeroToHundred: Launch.Seconds, LaunchTrace: Launch.Trace, LaunchDistance: Launch.Distance,
            ZeroToHundredReached: Launch.Reached, ShiftCount: Launch.ShiftCount,
            TopSpeed: EstimateTopSpeed(Specification, Profiles),
            PowerToWeight: PowerPeak.Y / (Specification.Identity.KerbMass / 1000),
            BrakingDistance: EstimateBrakingDistance(Specification, 100),
            DownforceAt200: Specification.Drivetrain.DownforceAt200,
            SpecificTorque: TorquePeak.Y / Math.max(0.1, Specification.Engine.Displacement),
            GripScale: Compound.GripScale
        };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     SCENE RECORDS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The records the environment already owns. The vehicle arrives beside them, which is why the outliner keeps its
    //    existing groups and adds one Vehicle group; nothing else in the scene gains geometry.
    const EnvironmentRows = [
        { Id: 'sun', Name: 'Sun', Glyph: 'Sun', Colour: '#e8c27a', Meta: '38° · 98 klx' },
        { Id: 'atmosphere', Name: 'Atmosphere', Glyph: 'Atmosphere', Colour: '#8fb4e8', Meta: 'Mie 2.3 · ozone 1.0' },
        { Id: 'clouds', Name: 'Clouds', Glyph: 'Cloud', Colour: '#b9c6d6', Meta: '42 % cover · 1750 m' },
        { Id: 'stars', Name: 'Stars', Glyph: 'Star', Colour: '#c9b8f0', Meta: '5000 stars · ×1.15' }
    ];

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     TOML RECORD
    //------------------------------------------------------------------------------------------------------------------------

    function EncodeCurvePoints(Points)
    {
        return '[' + Points.map(Point => `[${Number(Point[0].toFixed(2))}, ${Number(Point[1].toFixed(2))}]`).join(', ') + ']';
    }

    function EncodeVehicleRecord(Specification, Profiles, Tyres, Detached)
    {
        const Identity = Specification.Identity, Drivetrain = Specification.Drivetrain;
        const Engine = Specification.Engine, Thermal = Specification.Thermal, Aids = Specification.Aids;
        const Fuel = Specification.Fuel, Telemetry = Specification.Telemetry;
        const Lines = [];
        const Propose = '   # proposed: new record';
        Lines.push('[vehicle]' + Propose);
        Lines.push(`identity = "${Identity.Name}"`);
        Lines.push(`maker = "${Identity.Maker}"`);
        Lines.push(`classification = "${Identity.Classification}"`);
        Lines.push(`kerb_mass_kg = ${Identity.KerbMass}`);
        Lines.push(`wheelbase_mm = ${Identity.Wheelbase}`);
        Lines.push(`track_front_mm = ${Identity.TrackFront}`);
        Lines.push(`track_rear_mm = ${Identity.TrackRear}`);
        Lines.push(`centre_of_gravity_height_mm = ${Identity.CentreOfGravityHeight}`);
        Lines.push(`fuel_capacity_l = ${Identity.FuelCapacity}`);
        Lines.push(`ride_height_mm = ${Identity.RideHeight}`);
        Lines.push('');
        Lines.push('[vehicle.drivetrain]');
        Lines.push(`layout = "${Drivetrain.Layout}"`);
        Lines.push(`gear_count = ${Drivetrain.GearCount}`);
        Lines.push(`gear_ratios = [${Drivetrain.GearRatios.map(Ratio => Ratio.toFixed(2)).join(', ')}]`);
        Lines.push(`final_drive = ${Drivetrain.FinalDrive.toFixed(2)}`);
        Lines.push(`driveline_efficiency = ${Drivetrain.DrivelineEfficiency.toFixed(3)}`);
        Lines.push(`wheel_radius_mm = ${Drivetrain.WheelRadius}`);
        Lines.push(`tyre_friction = ${Drivetrain.TyreFriction.toFixed(2)}`);
        Lines.push(`rear_mass_share_percent = ${Drivetrain.RearMassShare}`);
        Lines.push(`rolling_resistance = ${Drivetrain.RollingResistance.toFixed(3)}`);
        Lines.push(`drag_coefficient = ${Drivetrain.DragCoefficient.toFixed(3)}`);
        Lines.push(`frontal_area_m2 = ${Drivetrain.FrontalArea.toFixed(2)}`);
        Lines.push(`downforce_at_200_kmh_n = ${Drivetrain.DownforceAt200}`);
        Lines.push(`brake_torque_nm = ${Drivetrain.BrakeTorque}`);
        Lines.push(`brake_balance_percent = ${Drivetrain.BrakeBalance}`);
        Lines.push('');
        Lines.push('[vehicle.engine]');
        Lines.push(`displacement_l = ${Engine.Displacement.toFixed(1)}`);
        Lines.push(`idle_speed_rpm = ${Engine.IdleSpeed}`);
        Lines.push(`redline_rpm = ${Engine.Redline}`);
        Lines.push(`rated_torque_nm = ${Engine.RatedTorque}`);
        Lines.push(`engine_inertia_kgm2 = ${Engine.EngineInertia.toFixed(2)}`);
        Lines.push(`turbo_boost_bar = ${Engine.TurboBoost.toFixed(1)}`);
        Lines.push(`fuel_burn_kg_h = ${Engine.FuelBurn}`);
        Lines.push(`thermostat_opening_c = ${Engine.ThermostatOpening}`);
        Lines.push('');
        Lines.push('[vehicle.thermal]');
        Lines.push(`coolant_target_c = ${Thermal.CoolantTarget}`);
        Lines.push(`oil_target_c = ${Thermal.OilTarget}`);
        Lines.push(`brake_target_c = ${Thermal.BrakeTarget}`);
        Lines.push(`ambient_c = ${Thermal.AmbientTemperature}`);
        Lines.push(`cooling_capacity_kw = ${Thermal.CoolingCapacity}`);
        Lines.push('');
        // 📝 The live dock reads these two blocks; it is debug instrumentation, so it is written with the record.
        Lines.push('[vehicle.fuel]');
        Lines.push(`load_l = ${Fuel.Load.toFixed(1)}`);
        Lines.push(`reserve_percent = ${Fuel.Reserve}`);
        Lines.push('');
        Lines.push('[vehicle.telemetry]');
        Lines.push(`sample_rate_hz = ${Telemetry.SampleRate}`);
        Lines.push('');
        Lines.push('[vehicle.aids]');
        for (const [Key, Setting] of Object.entries(Aids))
            Lines.push(`${Key.replace(/([a-z0-9])([A-Z])/g, '$1_$2').toLowerCase()} = ${Setting}`);
        if (Specification.Induction)
        {
            const Ind = Specification.Induction;
            Lines.push('');
            Lines.push('[vehicle.induction]');
            Lines.push(`mode = "${Ind.Mode}"`);
            Lines.push(`race_tune = ${Ind.RaceTune}`);
            Lines.push(`compressor_type = "${Ind.CompressorType}"`);
            Lines.push(`drive_ratio = ${Ind.DriveRatio.toFixed(2)}`);
            Lines.push(`displacement_cc = ${Ind.DisplacementCc}`);
            Lines.push(`supercharger_max_boost_bar = ${Ind.SuperchargerMaxBoost.toFixed(2)}`);
            Lines.push(`peak_parasitic_torque_nm = ${Ind.PeakParasiticTorque}`);
            Lines.push(`bypass_parasitic_percent = ${Ind.BypassParasiticPercent}`);
            Lines.push(`bypass_throttle_percent = ${Ind.BypassThrottlePercent}`);
            Lines.push(`turbo_spool_up_rate = ${Ind.TurboSpoolUpRate.toFixed(2)}`);
            Lines.push(`turbo_spool_down_rate = ${Ind.TurboSpoolDownRate.toFixed(2)}`);
            Lines.push(`torque_gain_per_bar = ${Ind.TorqueGainPerBar.toFixed(2)}`);
        }
        if (Specification.Differential)
        {
            const Diff = Specification.Differential;
            Lines.push('');
            Lines.push('[vehicle.differential]');
            Lines.push(`type = "${Diff.Type}"`);
            Lines.push(`preload_torque_nm = ${Diff.PreloadTorque}`);
            Lines.push(`power_lock_percent = ${Diff.PowerLockPercent}`);
            Lines.push(`coast_lock_percent = ${Diff.CoastLockPercent}`);
            Lines.push(`torsen_bias_ratio = ${Diff.TorsenBiasRatio.toFixed(2)}`);
            Lines.push(`front_torque_split_percent = ${Diff.FrontTorqueSplitPercent}`);
            Lines.push(`shift_duration_ms = ${Diff.ShiftDurationMs}`);
            Lines.push(`clutch_capacity_nm = ${Diff.ClutchCapacity}`);
        }
        if (Specification.Suspension)
        {
            const Susp = Specification.Suspension;
            Lines.push('');
            Lines.push('[vehicle.suspension]');
            Lines.push(`front_spring_kn_m = ${Susp.FrontSpringRate}`);
            Lines.push(`rear_spring_kn_m = ${Susp.RearSpringRate}`);
            Lines.push(`progressive_factor = ${Susp.ProgressiveFactor.toFixed(2)}`);
            Lines.push(`front_rest_length_mm = ${Susp.FrontRestLength}`);
            Lines.push(`rear_rest_length_mm = ${Susp.RearRestLength}`);
            Lines.push(`front_bump_damping_ns_m = ${Susp.FrontBumpDamping}`);
            Lines.push(`front_rebound_damping_ns_m = ${Susp.FrontReboundDamping}`);
            Lines.push(`rear_bump_damping_ns_m = ${Susp.RearBumpDamping}`);
            Lines.push(`rear_rebound_damping_ns_m = ${Susp.RearReboundDamping}`);
            Lines.push(`high_speed_damping_ratio = ${Susp.HighSpeedDampingRatio.toFixed(2)}`);
            Lines.push(`damper_knee_speed_mps = ${Susp.DamperKneeSpeed.toFixed(2)}`);
            Lines.push(`front_anti_roll_kn_m = ${Susp.FrontAntiRollRate}`);
            Lines.push(`rear_anti_roll_kn_m = ${Susp.RearAntiRollRate}`);
            Lines.push(`front_static_camber_deg = ${Susp.FrontStaticCamber.toFixed(2)}`);
            Lines.push(`rear_static_camber_deg = ${Susp.RearStaticCamber.toFixed(2)}`);
        }
        if (Specification.Steering)
        {
            const Steer = Specification.Steering;
            Lines.push('');
            Lines.push('[vehicle.steering]');
            Lines.push(`max_inner_angle_deg = ${Steer.MaxInnerAngle}`);
            Lines.push(`steering_ratio = ${Steer.SteeringRatio.toFixed(2)}`);
            Lines.push(`ackermann_percent = ${Steer.AckermannPercent}`);
            Lines.push(`speed_sensitive_floor_percent = ${Steer.SpeedSensitiveFloorPercent}`);
            Lines.push(`esc_yaw_gain = ${Steer.EscYawGain.toFixed(2)}`);
            Lines.push(`tcs_slip_threshold_percent = ${Steer.TcsSlipThresholdPercent}`);
            Lines.push(`abs_slip_threshold_percent = ${Steer.AbsSlipThresholdPercent}`);
        }
        if (Specification.SoftBody)
        {
            const Sb = Specification.SoftBody;
            Lines.push('');
            Lines.push('[vehicle.soft_body]');
            Lines.push(`formulation = "${Sb.Formulation}"`);
            Lines.push(`slip_mode = "${Sb.SlipMode}"`);
            Lines.push(`circumferential_nodes = ${Sb.CircumferentialNodes}`);
            Lines.push(`lateral_rings = ${Sb.LateralRings}`);
            Lines.push(`vertical_stiffness_kn_m = ${Sb.VerticalStiffness}`);
            Lines.push(`xpbd_substeps = ${Sb.XpbdSubsteps}`);
            Lines.push(`xpbd_iterations = ${Sb.XpbdIterations}`);
            Lines.push(`fem_substeps = ${Sb.FemSubsteps}`);
            Lines.push(`fem_youngs_modulus_mpa = ${Sb.FemYoungsModulusMpa.toFixed(2)}`);
            Lines.push(`fem_poisson_ratio = ${Sb.FemPoissonRatio.toFixed(3)}`);
        }
        if (Specification.AeroActive)
        {
            const Aero = Specification.AeroActive;
            Lines.push('');
            Lines.push('[vehicle.aero_active]');
            Lines.push(`cop_front_percent = ${Aero.CenterOfPressureFrontPercent}`);
            Lines.push(`drs_drag_reduction_percent = ${Aero.DrsDragReductionPercent}`);
            Lines.push(`airbrake_drag_addition = ${Aero.AirbrakeDragAddition.toFixed(2)}`);
            Lines.push(`ground_effect_optimal_height_mm = ${Aero.GroundEffectOptimalHeight}`);
            Lines.push(`ground_effect_peak_gain = ${Aero.GroundEffectPeakGain.toFixed(2)}`);
            Lines.push(`line_lock_front_only = ${Aero.LineLockFrontOnly}`);
        }
        if (Specification.Fluids)
        {
            const Fl = Specification.Fluids;
            Lines.push('');
            Lines.push('[vehicle.fluids]');
            Lines.push(`oil_grade = "${Fl.OilGrade}"`);
            Lines.push(`radiator_fan_threshold_c = ${Fl.RadiatorFanThreshold}`);
            Lines.push(`overheat_derate_start_c = ${Fl.OverheatDerateStart}`);
            Lines.push(`specific_fuel_burn_g_kwh = ${Fl.SpecificFuelBurnGkwh}`);
        }
        if (Specification.Simulation)
        {
            const Sim = Specification.Simulation;
            Lines.push('');
            Lines.push('[vehicle.simulation]');
            Lines.push(`outer_tick_hz = ${Sim.OuterTickHz}`);
            Lines.push(`substeps_per_outer_tick = ${Sim.SubstepsPerOuterTick}`);
            Lines.push(`maximum_catch_up_steps = ${Sim.MaximumCatchUpSteps}`);
            Lines.push(`stationarity_hold = ${Sim.StationarityHold}`);
            Lines.push(`force_zone_coupling = ${Sim.ForceZoneCoupling}`);
        }
        Lines.push('');
        Lines.push('[vehicle.curves]');
        for (const Profile of Profiles)
        {
            if (!Profile.Editable) continue;
            const Key = Profile.Key.replace(/([a-z0-9])([A-Z])/g, '$1_$2').toLowerCase();
            Lines.push(`${Key}_points = ${EncodeCurvePoints(Profile.Points)}`);
            Lines.push(`${Key}_interpolation = "${Profile.Interpolation}"`);
        }
        // 📝 Detached tyres keep their record but lose the enclosure; the pit lane writes them back on the next set.
        for (const Tyre of Tyres)
        {
            const Key = Tyre.Id.replace(/([a-z0-9])([A-Z])/g, '$1_$2').toLowerCase();
            Lines.push('');
            Lines.push(`[vehicle.tyres.${Key}]`);
            Lines.push(`compound = "${Tyre.Compound}"`);
            Lines.push(`set = "${Tyre.SetId}"`);
            Lines.push(`pressure_kpa = ${Tyre.Pressure}`);
            Lines.push(`surface_temperature_c = ${Tyre.SurfaceTemperature}`);
            Lines.push(`core_temperature_c = ${Tyre.CoreTemperature}`);
            Lines.push(`wear_percent = ${Tyre.Wear}`);
            Lines.push(`stint_laps = ${Tyre.StintLaps}`);
            Lines.push(`tread_depth_mm = ${Tyre.TreadDepth.toFixed(1)}`);
            Lines.push(`radius_mm = ${Tyre.Radius}`);
            Lines.push(`width_mm = ${Tyre.Width}`);
            Lines.push(`belt_temperature_c = ${Tyre.BeltTemperature}`);
            Lines.push(`gas_temperature_c = ${Tyre.GasTemperature}`);
            Lines.push(`pacejka_cx = ${Tyre.PacejkaLongitudinalShape.toFixed(2)}`);
            Lines.push(`pacejka_cy = ${Tyre.PacejkaLateralShape.toFixed(2)}`);
            Lines.push(`punctured = ${Tyre.Punctured}`);
            Lines.push(`enclosed = ${Tyre.Enclosed}`);
        }
        if (Detached && Detached.length)
        {
            Lines.push('');
            Lines.push('[vehicle.pit_lane]');
            Lines.push(`detached = [${Detached.map(Tyre => `"${Tyre.Id}"`).join(', ')}]`);
        }
        return Lines;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       EXPORTS
    //------------------------------------------------------------------------------------------------------------------------

    return {
        UnitDefinitions, CompoundCatalogue, TyreSetCatalogue, EnvironmentRows,
        ResolveUnitDefinition, ConvertQuantity, DecodeQuantity, ResolveUnitSymbol, FormatQuantity, FormatCount,
        ConfigureCurveProfiles, SortCurvePoints, ResolveCurvePoints, ResolveMonotoneTangents, ResolveTangents, EvaluateCurveSamples, EvaluateCurve,
        EvaluateCurveAt, ResolveCurvePeak, ResolvePointMinimumGap, InsertCurvePoint, RemoveCurvePoint, RelocateCurvePoint,
        InvalidateCurvePointTangents, EncodeCurveCsv,
        ConfigureVehicleSpecification, ConfigureTyreRecords, ResolveCompound, ResolveTemperatureWindow, ResolveWearStatus,
        FitTyreSet, AttachTyre, DetachTyre,
        ResolveTorque, ResolveAeroDownforce, ResolveEngineSpeed, ResolveWheelForce, ResolveTractionLimit, ResolveResistance,
        ResolveGearRatios,
        EstimateTopSpeed, EstimateZeroToHundred, EstimateBrakingDistance, EstimatePerformance,
        EncodeVehicleRecord, EncodeCurvePoints
    };
})();

if (typeof module !== 'undefined' && module.exports) module.exports = VehicleCatalogue;
