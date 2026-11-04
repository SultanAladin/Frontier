//============================================================================================================================================
//                                                          VEHICLECATALOGUE.JS
//============================================================================================================================================
// 🧩 The nine authored vehicles as combustion, induction, drivetrain and radiation sheets — one shared schema, one resolver, no
//    per-vehicle synthesis code. Every number is a physical quantity with its unit (m, s, Hz, bar, N·m, kg, m²) so the voicing stage
//    can derive frequencies from geometry instead of recalling them.

// 📝 Provenance: cycle topology, cylinder count, crank layout, idle/limiter and compression are manufacturer-published and are the
//    quantities the timbre actually depends on. Pipe lengths, tailpipe resonances, spool constants and bypass schedules are engineering
//    estimates bounded by what is known about each car (top-exit short stacks on the 918, a Roots blower on the Demon, an odd-fire V10
//    in the Viper, flat-plane cranks in the 918 and Agera, a single-speed front axle and two-speed rear in the Taycan). They are marked
//    `Estimate` where they are not published, and nothing here claims to be a measured impulse response of the real car.
// 💡 The point of the sheet is that the *mechanism* is modelled: a flat-plane V8 peaks on its 4th exhaust order because two firings per
//    bank land 180° apart, an odd-fire V10 cannot be periodic at half-crank order, and a Roots blower is a displacement pump whose
//    pulse rate is rotor lobes × drive ratio, not a turbine blade count. Those are derived below, not typed in.

//------------------------------------------------------------------------------------------------------------------------
//                                                    FIRING TOPOLOGY TABLES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Crankpin arrangements that decide whether the two banks can fire evenly. A cross-plane V8 fires 90/180/270/90 per bank; a
//    flat-plane V8 fires 180/180 and therefore sounds like two inline-fours at even spacing — the "exotic V8" timbre.
// 📝 Every layout states how its cylinders are numbered to banks and the order in which they fire. The global event spacing of a healthy engine
//    is always uniform — 720°/N — so unevenness can only come from the per-bank intervals, which is exactly where an exhaust's character lives.
//    `BankOfCylinder` is indexed by cylinder number minus one; `EventIntervals` is only present for a layout that genuinely cannot be uniform.
export const CrankLayouts = Object.freeze({
    Inline:         { BankCount: 1, AngleBetweenBanks: 0.0,  BankOfCylinder: [0, 0, 0, 0, 0, 0], Uniform: true },
    V60:            { BankCount: 2, AngleBetweenBanks: 60.0, BankOfCylinder: [0, 1, 0, 1, 0, 1], Uniform: true },
    V65:            { BankCount: 2, AngleBetweenBanks: 65.0, BankOfCylinder: [0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1], Uniform: true },
    V72:            { BankCount: 2, AngleBetweenBanks: 72.0, BankOfCylinder: [0, 1, 0, 1, 0, 1, 0, 1, 0, 1], Uniform: true },
    V90:            { BankCount: 2, AngleBetweenBanks: 90.0, BankOfCylinder: [0, 1, 0, 1, 0, 1, 0, 1], Uniform: true },
    // 📝 Cross-plane: cylinders 1-2 and 6-8 on the left bank, order 1-5-4-8-6-3-7-2, so each bank receives two pulses back to back.
    V90CrossPlane:  { BankCount: 2, AngleBetweenBanks: 90.0, BankOfCylinder: [0, 0, 1, 1, 1, 0, 1, 0],
                      FiringOrder: [1, 5, 4, 8, 6, 3, 7, 2], Uniform: true },
    // 📝 Flat-plane: cylinders 1-4 on one bank, 5-8 on the other, order 1-5-3-7-4-8-2-6, so the banks alternate and every pulse is isolated.
    V90FlatPlane:   { BankCount: 2, AngleBetweenBanks: 90.0, BankOfCylinder: [0, 0, 0, 0, 1, 1, 1, 1],
                      FiringOrder: [1, 5, 3, 7, 4, 8, 2, 6], Uniform: true },
    V90OddFire:     { BankCount: 2, AngleBetweenBanks: 90.0, BankOfCylinder: [0, 1, 0, 1, 0, 1, 0, 1, 0, 1],
                      Uniform: false, EventIntervals: [90, 54, 90, 54, 90, 54, 90, 54, 90, 54] },
    FlatSix:        { BankCount: 2, AngleBetweenBanks: 180.0, BankOfCylinder: [0, 1, 0, 1, 0, 1], Uniform: true },
    Electric:       { BankCount: 0, AngleBetweenBanks: 0.0,  BankOfCylinder: [], Uniform: true }
});

// 📝 Induction category decides which blower model the voicing stage runs; it is not a label on a sample.
export const InductionTopologies = Object.freeze({
    NaturallyAspirated: "naturallyAspirated",
    TwinScrollTurbo:    "twinScrollTurbo",
    RootsSupercharger:  "rootsSupercharger",
    ElectricDrive:      "electricDrive"
});

//------------------------------------------------------------------------------------------------------------------------
//                                                  SHARED BASELINE SHEET
//------------------------------------------------------------------------------------------------------------------------

// 📝 Every vehicle overrides only what differs. The baseline is a mid-size rear-drive V8 so a missing override still yields a
//    coherent, stable voice rather than silence.
export const VehicleAcousticBaseline = Object.freeze({
    Identifier: "baselineV8",
    DisplayName: "Baseline V8",
    SortieIndex: 0,

    Combustion: {
        CylinderCount: 8,
        CrankLayout: "V90CrossPlane",
        CycleRevolutions: 2.0,              // [rev] - four-stroke cycle length
        IdleRPM: 750.0,                     // [rpm] - closed-throttle governed idle
        RedlineRPM: 6800.0,                  // [rpm] - last point of the torque sheet
        LimiterRPM: 7000.0,                  // [rpm] - ignition cut ceiling
        StaticCompressionRatio: 11.0,       // [-]   - geometric compression
        CamOverlapDegrees: 18.0,            // [°]   - valve overlap at 0.050 in lift; drives the low-order burble
        IntakeValveDiameter: 0.0508,        // [m]   - 2.00 in
        ExhaustValveDiameter: 0.0394,       // [m]   - 1.55 in
        PeakTorqueNewtonMetres: 550.0,      // [N·m]
        TorquePeakRPM: 4600.0,              // [rpm]
        EngineInertiaKilogramMetres: 0.32,  // [kg·m²] - rotating assembly
        EngineBrakingLosses: 0.075,         // [-]   - pumping + friction fraction of indicated torque
        LowOrderCharacter: 0.35,            // [-]   - fraction of half-crank-order unevenness retained (0 even, 1 fully odd-fire)
        IdleLope: 0.20,                     // [-]   - idle speed wander depth, cam-driven
        Estimate: true
    },

    Exhaust: {
        PrimaryLength: 0.55,                // [m] - port to collector
        CollectorLength: 1.80,              // [m] - collector to muffler entry
        TailpipeLength: 0.45,               // [m] - muffler to atmosphere
        TailpipeDiameter: 0.060,            // [m] - outlet bore
        MufflerTransmission: 0.30,          // [0-1] - fraction of acoustic power passed at low frequency
        MufflerCutoffHz: 240.0,             // [Hz]  - the muffler's low-pass knee
        BypassOpenRPM: 3800.0,              // [rpm] - active valve start of travel
        BypassFullRPM: 5200.0,              // [rpm] - fully open
        BypassGainDecibels: 10.0,           // [dB]  - extra radiated power fully open
        BoundaryLossPerSecond: 380.0,       // [1/s] - wall/radiation damping of the pulse tail
        RadiationEfficiency: 0.020,         // [0-1] - dipole radiation factor at the open end
        BackfireEnabled: false,             // [bool]- overrun crackle authored for this car
        BackfireThreshold: 0.55,            // [-]   - unburnt fraction needed to ignite in the tailpipe
        TailpipeTemperatureKelvin: 850.0,   // [K]   - sets the local speed of sound
        Estimate: true
    },

    Induction: {
        Topology: "naturallyAspirated",
        RunnerLength: 0.24,                 // [m] - plenum to valve
        PlenumVolumeLitres: 4.2,            // [L]
        AirboxVolumeLitres: 6.0,            // [L]
        FilterRestriction: 0.92,            // [0-1] - fraction of pressure retained
        CompressorBladeCount: 0.0,          // [-]   - zero for naturally aspirated
        CompressorDriveRatio: 0.0,          // [-]   - turbine shaft revolutions per crank revolution
        WastegateMaxBoostBar: 0.0,          // [bar] - gauge
        SpoolTimeConstantSeconds: 0.0,      // [s]   - first-order shaft lag
        BlowOffEnabled: false,
        LobeCount: 0,                       // [-]   - Roots rotor lobes, zero unless supercharged
        BlowerDriveRatio: 0.0,              // [-]   - rotor revolutions per crank revolution
        BlowerMeshOrder: 0.0,               // [-]   - rotor pair mesh harmonic
        BlowerHissDecibels: 0.0,
        Estimate: true
    },

    Drivetrain: {
        GearRatios: [3.24, 2.10, 1.55, 1.20, 0.96, 0.79],   // [-] - forward gears only, descending
        FinalDriveRatio: 3.55,              // [-]
        DriveEfficiency: 0.92,              // [0-1]
        ShiftSeconds: 0.35,                 // [s] - torque interruption, clutch-to-clutch or manual
        SequentialDogBox: false,            // [bool] - straight-cut + dog engagement (race sequential)
        GearMeshOrder: 18.0,                // [-]   - tooth count at the meshing pair
        GearWhineDecibels: 4.0,             // [dB]  - at full torque
        DifferentialRatioOrder: 9.0,        // [-]   - crown wheel teeth / pinion teeth
        DrivelineLashDegrees: 2.5,          // [°]   - total lash seen at the wheels
        LaunchRPM: 2200.0,                  // [rpm] - clutch slip target from rest
        ClutchTorqueCapacity: 900.0,        // [N·m]
        FrontTorqueFraction: 0.0,           // [0-1] - all-wheel drive share
        ReverseRatio: -2.90,                // [-]
        ReductionStages: 1,                 // [count] - gear pairs from motor/engine to the wheel
        TorqueConverterStallRPM: 0.0,       // [rpm] - zero for a clutch
        Estimate: true
    },

    Chassis: {
        MassKilograms: 1650.0,              // [kg]
        CentreOfMassHeight: 0.45,           // [m]  - load transfer arm
        DrivenStaticFraction: 0.50,         // [-]  - static share of weight on the driven axle
        DragArea: 0.72,                     // [m²] Cd·A
        RollingResistance: 0.014,           // [-]
        WheelRadiusMetres: 0.360,           // [m]
        BrakeTorqueCapacity: 5200.0,        // [N·m] per axle pair at full pedal
        TyreGripCoefficient: 1.05,          // [-]
        TyreLongitudinalStiffness: 14.0,    // [1] slip stiffness, force per unit slip
        TyreRelaxationLength: 0.24,         // [m] - contact patch relaxation
        OnCentreSteerRatio: 15.0,           // [-]
        Estimate: true
    },

    Tones: {
        AlternatorOrder: 2.60,              // [-] - pulley ratio, order of crank speed
        AlternatorDecibels: -26.0,
        InjectorClicks: true,
        InjectorDecibels: -30.0,
        FuelPumpHz: 128.0,
        FuelPumpDecibels: -34.0,
        CoolantFanStartKelvin: 373.0,
        CoolantFanDecibels: -28.0
    },

    Cabin: {
        SoundInsulationDecibels: 24.0,      // [dB] - exterior to occupant at mid band
        InsulationFrequencyKnee: 420.0,     // [Hz]  - above this, glass and trim absorb more
        OccupantDistanceMetres: 1.90,       // [m]   - tailpipe to ear path
        CabinResonanceHz: 86.0,             // [Hz]  - first cabin mode
        Estimate: true
    }
});

// 📝 Deep merge that keeps authored objects frozen; arrays are replaced wholesale so a gear ratio list never half-inherits.
function MergeSheet(Base, Override)
{
    if (Array.isArray(Base) || Array.isArray(Override))
        return Override === undefined ? Base : Override;
    if (typeof Base !== "object" || Base === null)
        return Override === undefined ? Base : Override;
    if (typeof Override !== "object" || Override === null)
        return Base;
    const Merged = {};
    for (const Key of Object.keys(Base))
        Merged[Key] = MergeSheet(Base[Key], Override[Key]);
    for (const Key of Object.keys(Override))
        if (!(Key in Merged))
            Merged[Key] = Override[Key];
    return Object.freeze(Merged);
}

function ComposeVehicle(Override)
{
    const Merged = MergeSheet(VehicleAcousticBaseline, Override);
    return Object.freeze({ ...Merged, Identifier: Override.Identifier, DisplayName: Override.DisplayName,
                           SortieIndex: Override.SortieIndex });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE NINE VEHICLES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Ordering follows the request. Each sheet is the difference from the baseline, never a copy of it.
export const VehicleCatalogue = Object.freeze([
    ComposeVehicle({
        Identifier: "laFerrari",
        DisplayName: "Ferrari LaFerrari",
        SortieIndex: 1,
        Combustion: {
            CylinderCount: 12, CrankLayout: "V65", IdleRPM: 950.0, RedlineRPM: 9250.0, LimiterRPM: 9500.0,
            StaticCompressionRatio: 13.5, CamOverlapDegrees: 26.0, IntakeValveDiameter: 0.0380, ExhaustValveDiameter: 0.0320,
            PeakTorqueNewtonMetres: 700.0, TorquePeakRPM: 6750.0, EngineInertiaKilogramMetres: 0.24,
            EngineBrakingLosses: 0.085, LowOrderCharacter: 0.10, IdleLope: 0.06
        },
        Exhaust: {
            PrimaryLength: 0.78, CollectorLength: 2.20, TailpipeLength: 0.32, TailpipeDiameter: 0.070,
            MufflerTransmission: 0.22, MufflerCutoffHz: 320.0, BypassOpenRPM: 3900.0, BypassFullRPM: 5600.0,
            BypassGainDecibels: 13.0, BoundaryLossPerSecond: 300.0, RadiationEfficiency: 0.026,
            BackfireEnabled: true, BackfireThreshold: 0.62, TailpipeTemperatureKelvin: 1000.0
        },
        Induction: {
            Topology: InductionTopologies.NaturallyAspirated, RunnerLength: 0.155, PlenumVolumeLitres: 9.5,
            AirboxVolumeLitres: 12.0, FilterRestriction: 0.94
        },
        Drivetrain: {
            GearRatios: [3.08, 2.19, 1.63, 1.29, 1.03, 0.84, 0.69], FinalDriveRatio: 3.36, ShiftSeconds: 0.040,
            GearMeshOrder: 21.0, GearWhineDecibels: 2.0, DifferentialRatioOrder: 8.0, DrivelineLashDegrees: 0.8,
            LaunchRPM: 2600.0, ClutchTorqueCapacity: 980.0, ReverseRatio: -2.60
        },
        Chassis: { MassKilograms: 1585.0, CentreOfMassHeight: 0.38, DrivenStaticFraction: 0.58, DragArea: 0.62, WheelRadiusMetres: 0.350, TyreGripCoefficient: 1.15,
                   TyreLongitudinalStiffness: 17.0, BrakeTorqueCapacity: 7600.0, OnCentreSteerRatio: 13.0 },
        Tones: { AlternatorOrder: 2.85, FuelPumpHz: 142.0 },
        Cabin: { SoundInsulationDecibels: 21.0, CabinResonanceHz: 94.0, OccupantDistanceMetres: 1.15 }
    }),
    ComposeVehicle({
        Identifier: "gt-rNismoGt3",
        DisplayName: "Nissan GT-R Nismo GT3",
        SortieIndex: 2,
        Combustion: {
            CylinderCount: 6, CrankLayout: "V60", IdleRPM: 1100.0, RedlineRPM: 7000.0, LimiterRPM: 7200.0,
            StaticCompressionRatio: 9.0, CamOverlapDegrees: 34.0, IntakeValveDiameter: 0.0385, ExhaustValveDiameter: 0.0330,
            PeakTorqueNewtonMetres: 620.0, TorquePeakRPM: 5200.0, EngineInertiaKilogramMetres: 0.30,
            EngineBrakingLosses: 0.10, LowOrderCharacter: 0.05, IdleLope: 0.10
        },
        Exhaust: {
            PrimaryLength: 0.62, CollectorLength: 1.35, TailpipeLength: 0.22, TailpipeDiameter: 0.089,
            MufflerTransmission: 0.55, MufflerCutoffHz: 420.0, BypassOpenRPM: 2400.0, BypassFullRPM: 3600.0,
            BypassGainDecibels: 6.0, BoundaryLossPerSecond: 250.0, RadiationEfficiency: 0.030,
            BackfireEnabled: true, BackfireThreshold: 0.50, TailpipeTemperatureKelvin: 1050.0
        },
        Induction: {
            Topology: InductionTopologies.TwinScrollTurbo, RunnerLength: 0.185, PlenumVolumeLitres: 6.0,
            AirboxVolumeLitres: 8.0, FilterRestriction: 0.98, CompressorBladeCount: 11, CompressorDriveRatio: 1.0,
            WastegateMaxBoostBar: 1.90, SpoolTimeConstantSeconds: 0.26, BlowOffEnabled: true
        },
        Drivetrain: {
            GearRatios: [3.05, 2.13, 1.63, 1.31, 1.09, 0.94], FinalDriveRatio: 3.89, DriveEfficiency: 0.94,
            ShiftSeconds: 0.075, SequentialDogBox: true, GearMeshOrder: 24.0, GearWhineDecibels: 6.5,
            DifferentialRatioOrder: 11.0, DrivelineLashDegrees: 0.6, LaunchRPM: 3400.0, ClutchTorqueCapacity: 1150.0,
            ReverseRatio: -2.40, FrontTorqueFraction: 0.0
        },
        Chassis: { MassKilograms: 1300.0, CentreOfMassHeight: 0.33, DrivenStaticFraction: 0.54, DragArea: 0.74, WheelRadiusMetres: 0.345, TyreGripCoefficient: 1.45,
                   TyreLongitudinalStiffness: 23.0, BrakeTorqueCapacity: 9200.0, OnCentreSteerRatio: 12.0 },
        Tones: { AlternatorOrder: 2.20, InjectorDecibels: -28.0 },
        Cabin: { SoundInsulationDecibels: 12.0, InsulationFrequencyKnee: 620.0, OccupantDistanceMetres: 1.05,
                 CabinResonanceHz: 108.0 }
    }),
    ComposeVehicle({
        Identifier: "mustangGt",
        DisplayName: "Ford Mustang GT 5.0",
        SortieIndex: 3,
        Combustion: {
            CylinderCount: 8, CrankLayout: "V90CrossPlane", IdleRPM: 720.0, RedlineRPM: 7000.0, LimiterRPM: 7250.0,
            StaticCompressionRatio: 12.0, CamOverlapDegrees: 22.0, IntakeValveDiameter: 0.0373, ExhaustValveDiameter: 0.0335,
            PeakTorqueNewtonMetres: 570.0, TorquePeakRPM: 4600.0, EngineInertiaKilogramMetres: 0.36,
            EngineBrakingLosses: 0.080, LowOrderCharacter: 0.55, IdleLope: 0.30
        },
        Exhaust: {
            PrimaryLength: 0.52, CollectorLength: 1.95, TailpipeLength: 0.50, TailpipeDiameter: 0.064,
            MufflerTransmission: 0.26, MufflerCutoffHz: 210.0, BypassOpenRPM: 3600.0, BypassFullRPM: 5200.0,
            BypassGainDecibels: 11.0, BoundaryLossPerSecond: 400.0, RadiationEfficiency: 0.021,
            BackfireEnabled: true, BackfireThreshold: 0.70, TailpipeTemperatureKelvin: 900.0
        },
        Induction: {
            Topology: InductionTopologies.NaturallyAspirated, RunnerLength: 0.285, PlenumVolumeLitres: 5.4,
            AirboxVolumeLitres: 7.5, FilterRestriction: 0.93
        },
        Drivetrain: {
            GearRatios: [3.24, 2.10, 1.55, 1.20, 0.96, 0.79], FinalDriveRatio: 3.73, ShiftSeconds: 0.42,
            GearMeshOrder: 19.0, GearWhineDecibels: 3.0, DifferentialRatioOrder: 9.0, DrivelineLashDegrees: 3.2,
            LaunchRPM: 2100.0, ClutchTorqueCapacity: 760.0, ReverseRatio: -3.10
        },
        Chassis: { MassKilograms: 1705.0, CentreOfMassHeight: 0.48, DrivenStaticFraction: 0.52, DragArea: 0.80, WheelRadiusMetres: 0.360, TyreGripCoefficient: 1.00,
                   TyreLongitudinalStiffness: 13.5, BrakeTorqueCapacity: 5400.0, OnCentreSteerRatio: 15.5 },
        Tones: { AlternatorOrder: 2.70, FuelPumpHz: 118.0 },
        Cabin: { SoundInsulationDecibels: 25.0, CabinResonanceHz: 78.0, OccupantDistanceMetres: 1.55 }
    }),
    ComposeVehicle({
        Identifier: "ageraRs",
        DisplayName: "Koenigsegg Agera RS",
        SortieIndex: 4,
        Combustion: {
            CylinderCount: 8, CrankLayout: "V90FlatPlane", IdleRPM: 900.0, RedlineRPM: 8250.0, LimiterRPM: 8500.0,
            StaticCompressionRatio: 9.5, CamOverlapDegrees: 30.0, IntakeValveDiameter: 0.0400, ExhaustValveDiameter: 0.0345,
            PeakTorqueNewtonMetres: 1000.0, TorquePeakRPM: 4100.0, EngineInertiaKilogramMetres: 0.30,
            EngineBrakingLosses: 0.090, LowOrderCharacter: 0.05, IdleLope: 0.12
        },
        Exhaust: {
            PrimaryLength: 0.58, CollectorLength: 1.60, TailpipeLength: 0.30, TailpipeDiameter: 0.076,
            MufflerTransmission: 0.24, MufflerCutoffHz: 300.0, BypassOpenRPM: 3100.0, BypassFullRPM: 4600.0,
            BypassGainDecibels: 14.0, BoundaryLossPerSecond: 300.0, RadiationEfficiency: 0.028,
            BackfireEnabled: true, BackfireThreshold: 0.55, TailpipeTemperatureKelvin: 1020.0
        },
        Induction: {
            Topology: InductionTopologies.TwinScrollTurbo, RunnerLength: 0.200, PlenumVolumeLitres: 7.0,
            AirboxVolumeLitres: 9.0, FilterRestriction: 0.97, CompressorBladeCount: 9, CompressorDriveRatio: 1.0,
            WastegateMaxBoostBar: 1.50, SpoolTimeConstantSeconds: 0.20, BlowOffEnabled: true
        },
        Drivetrain: {
            GearRatios: [3.36, 2.31, 1.77, 1.40, 1.14, 0.95, 0.80], FinalDriveRatio: 3.00, ShiftSeconds: 0.045,
            GearMeshOrder: 22.0, GearWhineDecibels: 2.5, DifferentialRatioOrder: 8.0, DrivelineLashDegrees: 0.9,
            LaunchRPM: 3000.0, ClutchTorqueCapacity: 1300.0, ReverseRatio: -2.50
        },
        Chassis: { MassKilograms: 1395.0, CentreOfMassHeight: 0.36, DrivenStaticFraction: 0.60, DragArea: 0.60, WheelRadiusMetres: 0.365, TyreGripCoefficient: 1.20,
                   TyreLongitudinalStiffness: 18.0, BrakeTorqueCapacity: 8200.0, OnCentreSteerRatio: 13.5 },
        Tones: { AlternatorOrder: 2.90, FuelPumpHz: 150.0 },
        Cabin: { SoundInsulationDecibels: 18.0, CabinResonanceHz: 92.0, OccupantDistanceMetres: 1.10 }
    }),
    ComposeVehicle({
        Identifier: "spyder918",
        DisplayName: "Porsche 918 Spyder",
        SortieIndex: 5,
        Combustion: {
            CylinderCount: 8, CrankLayout: "V90FlatPlane", IdleRPM: 1000.0, RedlineRPM: 9150.0, LimiterRPM: 9300.0,
            StaticCompressionRatio: 12.5, CamOverlapDegrees: 24.0, IntakeValveDiameter: 0.0380, ExhaustValveDiameter: 0.0325,
            PeakTorqueNewtonMetres: 540.0, TorquePeakRPM: 6700.0, EngineInertiaKilogramMetres: 0.26,
            EngineBrakingLosses: 0.085, LowOrderCharacter: 0.04, IdleLope: 0.08
        },
        Exhaust: {
            PrimaryLength: 0.34, CollectorLength: 0.88, TailpipeLength: 0.14, TailpipeDiameter: 0.072,
            MufflerTransmission: 0.34, MufflerCutoffHz: 480.0, BypassOpenRPM: 2900.0, BypassFullRPM: 4200.0,
            BypassGainDecibels: 12.0, BoundaryLossPerSecond: 470.0, RadiationEfficiency: 0.031,
            BackfireEnabled: true, BackfireThreshold: 0.58, TailpipeTemperatureKelvin: 980.0
        },
        Induction: {
            Topology: InductionTopologies.NaturallyAspirated, RunnerLength: 0.135, PlenumVolumeLitres: 6.4,
            AirboxVolumeLitres: 11.0, FilterRestriction: 0.95
        },
        Drivetrain: {
            GearRatios: [3.87, 2.30, 1.66, 1.23, 0.97, 0.81, 0.68], FinalDriveRatio: 3.09, ShiftSeconds: 0.035,
            GearMeshOrder: 23.0, GearWhineDecibels: 2.2, DifferentialRatioOrder: 8.0, DrivelineLashDegrees: 0.7,
            LaunchRPM: 3200.0, ClutchTorqueCapacity: 780.0, ReverseRatio: -2.70, FrontTorqueFraction: 0.35
        },
        Chassis: { MassKilograms: 1674.0, CentreOfMassHeight: 0.40, DrivenStaticFraction: 1.00, DragArea: 0.66, WheelRadiusMetres: 0.350, TyreGripCoefficient: 1.18,
                   TyreLongitudinalStiffness: 17.0, BrakeTorqueCapacity: 8000.0, OnCentreSteerRatio: 13.0 },
        Tones: { AlternatorOrder: 2.40, FuelPumpHz: 136.0 },
        Cabin: { SoundInsulationDecibels: 19.0, CabinResonanceHz: 96.0, OccupantDistanceMetres: 1.05 }
    }),
    ComposeVehicle({
        Identifier: "taycanTurbo",
        DisplayName: "Porsche Taycan Turbo",
        SortieIndex: 6,
        Combustion: {
            CylinderCount: 0, CrankLayout: "Electric", CycleRevolutions: 1.0, IdleRPM: 0.0, RedlineRPM: 16000.0,
            LimiterRPM: 16500.0, StaticCompressionRatio: 0.0, CamOverlapDegrees: 0.0, PeakTorqueNewtonMetres: 850.0,
            TorquePeakRPM: 5200.0, EngineInertiaKilogramMetres: 0.06, EngineBrakingLosses: 0.0,
            LowOrderCharacter: 0.0, IdleLope: 0.0
        },
        Exhaust: {
            PrimaryLength: 0.0, CollectorLength: 0.0, TailpipeLength: 0.0, TailpipeDiameter: 0.0,
            MufflerTransmission: 0.0, MufflerCutoffHz: 12000.0, BypassOpenRPM: 0.0, BypassFullRPM: 0.0,
            BypassGainDecibels: 0.0, BoundaryLossPerSecond: 3000.0, RadiationEfficiency: 0.0,
            BackfireEnabled: false, BackfireThreshold: 1.0, TailpipeTemperatureKelvin: 380.0
        },
        Induction: {
            Topology: InductionTopologies.ElectricDrive, RunnerLength: 0.0, PlenumVolumeLitres: 0.0,
            AirboxVolumeLitres: 0.0, FilterRestriction: 1.0, CompressorBladeCount: 0, CompressorDriveRatio: 0.0,
            WastegateMaxBoostBar: 0.0, SpoolTimeConstantSeconds: 0.0, BlowOffEnabled: false
        },
        Drivetrain: {
            GearRatios: [8.60], FinalDriveRatio: 1.0, DriveEfficiency: 0.96, ShiftSeconds: 0.0,
            SequentialDogBox: false, GearMeshOrder: 23.0, GearWhineDecibels: 7.5, DifferentialRatioOrder: 41.0,
            DrivelineLashDegrees: 0.4, LaunchRPM: 0.0, ClutchTorqueCapacity: 1200.0,
            FrontTorqueFraction: 0.38, ReverseRatio: -8.60, ReductionStages: 2
        },
        Chassis: { MassKilograms: 2305.0, CentreOfMassHeight: 0.42, DrivenStaticFraction: 1.00, DragArea: 0.62, WheelRadiusMetres: 0.365, TyreGripCoefficient: 1.10,
                   TyreLongitudinalStiffness: 16.0, BrakeTorqueCapacity: 8600.0, OnCentreSteerRatio: 14.0 },
        Tones: { AlternatorOrder: 0.0, AlternatorDecibels: -80.0, InjectorClicks: false,
                 FuelPumpHz: 0.0, FuelPumpDecibels: -80.0, CoolantFanDecibels: -34.0 },
        Cabin: { SoundInsulationDecibels: 30.0, InsulationFrequencyKnee: 900.0, CabinResonanceHz: 72.0,
                 OccupantDistanceMetres: 0.65 }
    }),
    ComposeVehicle({
        Identifier: "demonSrt",
        DisplayName: "Dodge Challenger SRT Demon",
        SortieIndex: 7,
        Combustion: {
            CylinderCount: 8, CrankLayout: "V90CrossPlane", IdleRPM: 800.0, RedlineRPM: 6500.0, LimiterRPM: 6700.0,
            StaticCompressionRatio: 9.5, CamOverlapDegrees: 30.0, IntakeValveDiameter: 0.0405, ExhaustValveDiameter: 0.0345,
            PeakTorqueNewtonMetres: 1044.0, TorquePeakRPM: 4800.0, EngineInertiaKilogramMetres: 0.42,
            EngineBrakingLosses: 0.070, LowOrderCharacter: 0.62, IdleLope: 0.42
        },
        Exhaust: {
            PrimaryLength: 0.50, CollectorLength: 1.50, TailpipeLength: 0.42, TailpipeDiameter: 0.077,
            MufflerTransmission: 0.32, MufflerCutoffHz: 190.0, BypassOpenRPM: 2600.0, BypassFullRPM: 3800.0,
            BypassGainDecibels: 12.0, BoundaryLossPerSecond: 360.0, RadiationEfficiency: 0.023,
            BackfireEnabled: true, BackfireThreshold: 0.66, TailpipeTemperatureKelvin: 880.0
        },
        Induction: {
            Topology: InductionTopologies.RootsSupercharger, RunnerLength: 0.115, PlenumVolumeLitres: 12.0,
            AirboxVolumeLitres: 14.0, FilterRestriction: 0.90, LobeCount: 4, BlowerDriveRatio: 1.55,
            BlowerMeshOrder: 8.0, BlowerHissDecibels: 9.0, WastegateMaxBoostBar: 0.83,
            SpoolTimeConstantSeconds: 0.020, BlowOffEnabled: false
        },
        Drivetrain: {
            GearRatios: [4.71, 3.14, 2.10, 1.67, 1.29, 1.00, 0.84, 0.67], FinalDriveRatio: 3.09, ShiftSeconds: 0.16,
            GearMeshOrder: 20.0, GearWhineDecibels: 3.5, DifferentialRatioOrder: 10.0, DrivelineLashDegrees: 3.0,
            LaunchRPM: 1800.0, ClutchTorqueCapacity: 1500.0, ReverseRatio: -3.30,
            TorqueConverterStallRPM: 2400.0
        },
        Chassis: { MassKilograms: 1941.0, CentreOfMassHeight: 0.50, DrivenStaticFraction: 0.58, DragArea: 0.86, WheelRadiusMetres: 0.375, TyreGripCoefficient: 1.30,
                   TyreLongitudinalStiffness: 20.0, BrakeTorqueCapacity: 6800.0, OnCentreSteerRatio: 16.0 },
        Tones: { AlternatorOrder: 2.55, FuelPumpHz: 132.0 },
        Cabin: { SoundInsulationDecibels: 20.0, CabinResonanceHz: 74.0, OccupantDistanceMetres: 1.60 }
    }),
    ComposeVehicle({
        Identifier: "viperAcr",
        DisplayName: "Dodge Viper SRT",
        SortieIndex: 8,
        Combustion: {
            CylinderCount: 10, CrankLayout: "V90OddFire", IdleRPM: 700.0, RedlineRPM: 6200.0, LimiterRPM: 6400.0,
            StaticCompressionRatio: 10.2, CamOverlapDegrees: 28.0, IntakeValveDiameter: 0.0410, ExhaustValveDiameter: 0.0355,
            PeakTorqueNewtonMetres: 813.0, TorquePeakRPM: 5000.0, EngineInertiaKilogramMetres: 0.44,
            EngineBrakingLosses: 0.075, LowOrderCharacter: 0.88, IdleLope: 0.50
        },
        Exhaust: {
            PrimaryLength: 0.62, CollectorLength: 2.00, TailpipeLength: 0.38, TailpipeDiameter: 0.070,
            MufflerTransmission: 0.28, MufflerCutoffHz: 200.0, BypassOpenRPM: 4000.0, BypassFullRPM: 5400.0,
            BypassGainDecibels: 10.0, BoundaryLossPerSecond: 350.0, RadiationEfficiency: 0.022,
            BackfireEnabled: true, BackfireThreshold: 0.72, TailpipeTemperatureKelvin: 900.0
        },
        Induction: {
            Topology: InductionTopologies.NaturallyAspirated, RunnerLength: 0.275, PlenumVolumeLitres: 8.0,
            AirboxVolumeLitres: 9.5, FilterRestriction: 0.91
        },
        Drivetrain: {
            GearRatios: [2.26, 1.58, 1.19, 1.00, 0.83, 0.63], FinalDriveRatio: 3.55, ShiftSeconds: 0.40,
            GearMeshOrder: 18.0, GearWhineDecibels: 3.8, DifferentialRatioOrder: 9.0, DrivelineLashDegrees: 3.4,
            LaunchRPM: 2000.0, ClutchTorqueCapacity: 880.0, ReverseRatio: -2.90
        },
        Chassis: { MassKilograms: 1560.0, CentreOfMassHeight: 0.42, DrivenStaticFraction: 0.53, DragArea: 0.75, WheelRadiusMetres: 0.355, TyreGripCoefficient: 1.22,
                   TyreLongitudinalStiffness: 18.0, BrakeTorqueCapacity: 7200.0, OnCentreSteerRatio: 14.0 },
        Tones: { AlternatorOrder: 2.65, FuelPumpHz: 126.0 },
        Cabin: { SoundInsulationDecibels: 17.0, CabinResonanceHz: 70.0, OccupantDistanceMetres: 1.35 }
    }),
    ComposeVehicle({
        Identifier: "challengerScat",
        DisplayName: "Dodge Challenger 392",
        SortieIndex: 9,
        Combustion: {
            CylinderCount: 8, CrankLayout: "V90CrossPlane", IdleRPM: 750.0, RedlineRPM: 6200.0, LimiterRPM: 6400.0,
            StaticCompressionRatio: 10.9, CamOverlapDegrees: 27.0, IntakeValveDiameter: 0.0400, ExhaustValveDiameter: 0.0340,
            PeakTorqueNewtonMetres: 644.0, TorquePeakRPM: 4800.0, EngineInertiaKilogramMetres: 0.40,
            EngineBrakingLosses: 0.072, LowOrderCharacter: 0.58, IdleLope: 0.36
        },
        Exhaust: {
            PrimaryLength: 0.55, CollectorLength: 1.75, TailpipeLength: 0.46, TailpipeDiameter: 0.070,
            MufflerTransmission: 0.30, MufflerCutoffHz: 195.0, BypassOpenRPM: 3400.0, BypassFullRPM: 4800.0,
            BypassGainDecibels: 11.0, BoundaryLossPerSecond: 360.0, RadiationEfficiency: 0.023,
            BackfireEnabled: true, BackfireThreshold: 0.68, TailpipeTemperatureKelvin: 890.0
        },
        Induction: {
            Topology: InductionTopologies.NaturallyAspirated, RunnerLength: 0.250, PlenumVolumeLitres: 8.8,
            AirboxVolumeLitres: 10.0, FilterRestriction: 0.92
        },
        Drivetrain: {
            GearRatios: [4.71, 3.14, 2.10, 1.67, 1.29, 1.00, 0.84, 0.67], FinalDriveRatio: 3.09, ShiftSeconds: 0.18,
            GearMeshOrder: 20.0, GearWhineDecibels: 3.4, DifferentialRatioOrder: 10.0, DrivelineLashDegrees: 3.0,
            LaunchRPM: 1900.0, ClutchTorqueCapacity: 1050.0, ReverseRatio: -3.30,
            TorqueConverterStallRPM: 2000.0
        },
        Chassis: { MassKilograms: 1936.0, CentreOfMassHeight: 0.50, DrivenStaticFraction: 0.55, DragArea: 0.86, WheelRadiusMetres: 0.360, TyreGripCoefficient: 1.12,
                   TyreLongitudinalStiffness: 14.5, BrakeTorqueCapacity: 6600.0, OnCentreSteerRatio: 16.0 },
        Tones: { AlternatorOrder: 2.60, FuelPumpHz: 122.0 },
        Cabin: { SoundInsulationDecibels: 22.0, CabinResonanceHz: 76.0, OccupantDistanceMetres: 1.55 }
    })
]);

//------------------------------------------------------------------------------------------------------------------------
//                                                     FIRING GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

// 📝 The exhaust of a four-stroke is not a smooth tone: it is a sequence of pressure pulses, one per cylinder, spaced by the crank
//    angles at which each pair of exhaust valve events completes. Deriving that sequence from cylinder count + crankpin layout is
//    the difference between nine real engines and nine pitch-shifted copies of one waveform.
//    720°/N for an even engine, e.g. V12 → 60°, V10 → 72°, V8 → 90°, V6 → 120°. The odd-fire Viper cannot be even: its 90° V
//    with offset throws gives 90/54/90/54/... — a nine-times-per-cycle unevenness that is exactly the "American V10" wail.
export function DeriveFiringSequence(Combustion)
{
    const Cylinders = Math.max(0, Math.round(Combustion.CylinderCount));
    if (Cylinders === 0)
        return Object.freeze({ EventCount: 0, Phases: Object.freeze([]), BankOfEvent: Object.freeze([]), Strength: Object.freeze([]),
                               BankGaps: Object.freeze([]), EvenFiring: true, CycleRevolutions: 1.0, BankCount: 0 });

    const CycleRevolutions = Combustion.CycleRevolutions || 2.0;
    const CycleDegrees = 360.0 * CycleRevolutions;
    const Layout = CrankLayouts[Combustion.CrankLayout] || CrankLayouts.V90;
    const Interval = CycleDegrees / Cylinders;
    const FiringOrder = Layout.FiringOrder || Array.from({ length: Cylinders }, (Unused, Index) => Index + 1);
    const BankOfCylinder = Layout.BankOfCylinder;

    const Phases = [];
    const BankOfEvent = [];
    if (Layout.Uniform)
    {
        for (let Index = 0; Index < Cylinders; ++Index)
        {
            Phases.push(Index * Interval);
            const Cylinder = FiringOrder[Index % FiringOrder.length];
            BankOfEvent.push(BankOfCylinder[(Cylinder - 1) % BankOfCylinder.length] || 0);
        }
    }
    else
    {
        const Pattern = Layout.EventIntervals;
        let Angle = 0.0;
        for (let Index = 0; Index < Cylinders; ++Index)
        {
            Phases.push(Angle % CycleDegrees);
            BankOfEvent.push(Index % Math.max(1, Layout.BankCount));
            Angle += Pattern[Index % Pattern.length];
        }
    }

    // 📝 Per-bank intervals. A cross-plane V8 fires 90/180/270/90 inside one bank, so two of its four pulses in each bank arrive back to back and
    //    scavenge each other; a flat-plane V8 fires its banks alternately at 180°, so every pulse is isolated. That difference — not the global
    //    spacing, which is identical for both — is what a listener hears as burble versus scream.
    const BankGaps = [];
    for (let Bank = 0; Bank < Math.max(1, Layout.BankCount); ++Bank)
    {
        const Angles = [];
        for (let Index = 0; Index < Phases.length; ++Index)
            if (BankOfEvent[Index] === Bank)
                Angles.push(Phases[Index]);
        Angles.sort((Left, Right) => Left - Right);
        for (let Index = 0; Index < Angles.length; ++Index)
            BankGaps.push(Index + 1 < Angles.length ? Angles[Index + 1] - Angles[Index]
                                                    : Angles[0] + CycleDegrees - Angles[Index]);
    }

    // 📝 Strength per event: a pulse whose predecessor in the same pipe arrived inside ~1.3 intervals is a merged, stronger event; an isolated
    //    pulse radiates its full pressure. Normalised so total radiated pressure is comparable between layouts.
    const Strength = BankGaps.map((Gap) => (Gap <= 0.0 ? 1.0 : Math.max(0.35, Math.min(1.6, Interval / Gap * 0.85 + 0.5))));
    const StrengthSum = Strength.reduce((Sum, Entry) => Sum + Entry, 0.0) || 1.0;
    const Normalised = Strength.map((Entry) => Entry * Strength.length / StrengthSum);
    const EvenFiring = Layout.Uniform === true && Layout.EventIntervals === undefined;

    return Object.freeze({ EventCount: Cylinders, Phases: Object.freeze(Phases), BankOfEvent: Object.freeze(BankOfEvent),
                           Strength: Object.freeze(Normalised), BankGaps: Object.freeze(BankGaps), EvenFiring, CycleRevolutions,
                           BankCount: Layout.BankCount, Interval });
}

// 📝 Speed of sound in the tailpipe from the gas temperature; a 1000 K exhaust radiates and resonates 1.7× higher than cold air, which
//    is why a hot V12 crackle sits above where a cold estimate would put it.
export function ResolveGasSoundSpeed(Kelvin)
{
    const Ratio = 1.40;                   // [-] - specific heat ratio, burnt gas
    const GasConstant = 287.05;           // [J/(kg·K)]
    return Math.sqrt(Ratio * GasConstant * Math.max(200.0, Kelvin));
}

// 📝 Open-end (radiation) resonance of a pipe of length L closed at the valve and open at the tip: f₁ = c/(4L). The higher modes are
//    odd multiples, which is what the voiced taps reproduce.
export function ResolvePipeModes(LengthMetres, SoundSpeed, ModeCount)
{
    const Modes = [];
    if (LengthMetres <= 1.0e-4)
        return Object.freeze(Modes);
    for (let Mode = 1; Mode <= Math.max(1, ModeCount); ++Mode)
        Modes.push((2 * Mode - 1) * SoundSpeed / (4.0 * LengthMetres));
    return Object.freeze(Modes);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SHEET RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

export function FindVehicle(Identifier)
{
    return VehicleCatalogue.find((Vehicle) => Vehicle.Identifier === Identifier) || VehicleCatalogue[0];
}

export function ResolveVehicleProfile(Identifier)
{
    const Vehicle = FindVehicle(Identifier);
    const Firing = DeriveFiringSequence(Vehicle.Combustion);
    const GasSoundSpeed = ResolveGasSoundSpeed(Vehicle.Exhaust.TailpipeTemperatureKelvin);
    const AirSoundSpeed = ResolveGasSoundSpeed(293.15);
    const ExhaustModes = ResolvePipeModes(Vehicle.Exhaust.PrimaryLength + Vehicle.Exhaust.CollectorLength * 0.5,
                                          GasSoundSpeed, 3);
    const IntakeModes = ResolvePipeModes(Vehicle.Induction.RunnerLength, AirSoundSpeed, 2);
    const Electric = Vehicle.Induction.Topology === InductionTopologies.ElectricDrive;
    const Redline = Math.max(1.0, Vehicle.Combustion.RedlineRPM);
    return Object.freeze({ ...Vehicle, Firing, GasSoundSpeed, AirSoundSpeed, ExhaustModes, IntakeModes, Electric,
                           Redline, GearCount: Vehicle.Drivetrain.GearRatios.length });
}

// 📝 Validation is a returned report, not a throw: the host shows it, the tests assert it, and nothing is silently coerced.
export function ValidateVehicleProfile(Vehicle)
{
    const Findings = [];
    const C = Vehicle.Combustion, E = Vehicle.Exhaust, D = Vehicle.Drivetrain, K = Vehicle.Chassis;
    if (!(C.IdleRPM >= 0.0)) Findings.push("idle below zero");
    if (!(C.RedlineRPM > C.IdleRPM)) Findings.push("redline not above idle");
    if (!(C.LimiterRPM >= C.RedlineRPM)) Findings.push("limiter below redline");
    if (C.CylinderCount > 0 && !(C.StaticCompressionRatio > 0.0)) Findings.push("missing compression ratio");
    if (E.PrimaryLength <= 0.0 && C.CylinderCount > 0) Findings.push("missing exhaust primary length");
    if (D.GearRatios.length === 0) Findings.push("no forward gears");
    for (let Index = 1; Index < D.GearRatios.length; ++Index)
        if (!(D.GearRatios[Index] < D.GearRatios[Index - 1])) { Findings.push("gear ratios not descending"); break; }
    if (!(D.FinalDriveRatio > 0.0)) Findings.push("final drive must be positive");
    if (!(K.MassKilograms > 200.0)) Findings.push("implausible mass");
    if (!(K.WheelRadiusMetres > 0.20 && K.WheelRadiusMetres < 0.55)) Findings.push("implausible wheel radius");
    if (Vehicle.Firing.EventCount > 0 && Vehicle.Firing.Phases.length !== C.CylinderCount)
        Findings.push("firing phase count does not match cylinders");
    if (Vehicle.CrankLayout === "Electric" && Vehicle.Exhaust.PrimaryLength !== 0.0)
        Findings.push("electric vehicle carries an exhaust");
    if (Vehicle.Drivetrain.ShiftSeconds < 0.0) Findings.push("negative shift duration");
    return Object.freeze({ Identifier: Vehicle.Identifier, Findings: Object.freeze(Findings),
                           Accepted: Findings.length === 0 });
}

// 📝 The highest firing order that can be represented at the device rate, so the voicing stage can band-limit rather than alias.
export function ResolveRepresentableOrder(HighestFrequency, SampleRate, RPM)
{
    if (RPM <= 1.0e-6)
        return 0.0;
    const FiringFrequency = RPM / 60.0;   // [Hz] - one pulse per revolution order, scaled per car by cylinder count
    return Math.min(HighestFrequency, SampleRate * 0.5) / FiringFrequency;
}
