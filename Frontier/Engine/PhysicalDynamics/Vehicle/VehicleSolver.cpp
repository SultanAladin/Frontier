/*==========================================================================================================================================
                                                             VEHICLESOLVER.CPP
==========================================================================================================================================*/
// 🧩 Implementation of the 4-corner vehicle solver, coupled Coulomb-cone slope stationarity, 3 soft-body tyre models, and GPU profile output.

#include "Engine/PhysicalDynamics/Vehicle/VehicleSolver.h"

#include <cstring>

namespace Frontier::Vehicle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    BUILD & RESET
//------------------------------------------------------------------------------------------------------------------------

void VehicleSolver::Build(const VehicleSolverConfiguration& Configuration, const WorldHooks& SuppliedHooks,
                          const ChassisKinematicReading& InitialChassis) noexcept
{
    Config        = Configuration;
    Hooks         = SuppliedHooks;
    ActiveCommand = DriverCommand{};

    Config.TyreSoftBody.OuterRadiusMetres       = Config.Geometry.WheelOuterRadiusMetres;
    Config.TyreSoftBody.RimRadiusMetres         = Config.Geometry.WheelRimRadiusMetres;
    Config.TyreSoftBody.ContactTreadWidthMetres = Config.Geometry.WheelTreadWidthMetres;
    Config.TyreSoftBody.ReferencePressurePa     = Config.TyreThermal.ColdGaugePressurePa;

    MagicFormula.AssignParameters(Config.TyreCompound);

    Powertrain.AssignInduction(Config.Induction);
    Powertrain.AssignRaceTune(Config.RaceTune);
    Powertrain.AssignEngine(Config.Engine);
    Powertrain.AssignTurbocharger(Config.Turbocharger);
    Powertrain.AssignSupercharger(Config.Supercharger);
    Powertrain.AssignThermalAndFuel(Config.EngineFluids);
    Powertrain.AssignTransmission(Config.Transmission);
    Powertrain.AssignDifferential(Config.Differential);
    Powertrain.Reset(Config.Engine.IdleRpm, true);

    Brakes.Reset(Config.Brakes);
    Steering.Reset();
    Aero.Reset();

    for (size_t Idx = 0; Idx < 4; ++Idx)
    {
        CornerRuntime& Corner      = Corners[Idx];
        const StrutSpecification& S= Idx < 2 ? Config.FrontStrut : Config.RearStrut;
        Corner.SoftBody.Configure(Config.SoftBodyTyreMode, Config.TyreSoftBody);
        SlipSolver.Reset(Corner.Slip);
        TyreThermalIntegrator::Reset(Config.TyreThermal, Corner.Thermal);
        Corner.StrutLengthMetres   = S.RestLengthMetres;
        Corner.WheelOmegaRadPerSec = 0.0f;
        Corner.SpinAngleRadians    = 0.0f;
        Corner.PrevLateralForceN   = 0.0f;
        Corner.PrevLongForceN      = 0.0f;
    }

    Telemetry         = VehicleTelemetryReading{};
    Telemetry.Chassis = InitialChassis;
}

void VehicleSolver::Reconfigure(const VehicleSolverConfiguration& Configuration) noexcept
{
    const bool ModeChanged      = (Config.SoftBodyTyreMode != Configuration.SoftBodyTyreMode);
    Config                      = Configuration;
    Config.TyreSoftBody.OuterRadiusMetres       = Config.Geometry.WheelOuterRadiusMetres;
    Config.TyreSoftBody.RimRadiusMetres         = Config.Geometry.WheelRimRadiusMetres;
    Config.TyreSoftBody.ContactTreadWidthMetres = Config.Geometry.WheelTreadWidthMetres;
    Config.TyreSoftBody.ReferencePressurePa     = Config.TyreThermal.ColdGaugePressurePa;

    MagicFormula.AssignParameters(Config.TyreCompound);
    Powertrain.AssignInduction(Config.Induction);
    Powertrain.AssignRaceTune(Config.RaceTune);
    Powertrain.AssignEngine(Config.Engine);
    Powertrain.AssignTurbocharger(Config.Turbocharger);
    Powertrain.AssignSupercharger(Config.Supercharger);
    Powertrain.AssignThermalAndFuel(Config.EngineFluids);
    Powertrain.AssignTransmission(Config.Transmission);
    Powertrain.AssignDifferential(Config.Differential);

    if (ModeChanged)
    {
        for (CornerRuntime& Corner : Corners)
        {
            Corner.SoftBody.Configure(Config.SoftBodyTyreMode, Config.TyreSoftBody);
        }
    }
}

void VehicleSolver::AssignSoftBodyFormulation(TyreFormulation Formulation) noexcept
{
    Config.SoftBodyTyreMode = Formulation;
    for (CornerRuntime& Corner : Corners)
    {
        Corner.SoftBody.Configure(Formulation, Config.TyreSoftBody);
    }
}

void VehicleSolver::PunctureWheel(uint32_t WheelIndex) noexcept
{
    if (WheelIndex < 4u)
    {
        Corners[WheelIndex].Thermal.Punctured = true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PHYSICS STEP
//------------------------------------------------------------------------------------------------------------------------

void VehicleSolver::Step(float DeltaSeconds) noexcept
{
    const float Dt = ClampScalar(DeltaSeconds, 1.0e-5f, 1.0f / 30.0f);
    const ChassisKinematicReading Chassis = Hooks.ReadChassis ? Hooks.ReadChassis() : Telemetry.Chassis;
    Telemetry.Chassis                     = Chassis;

    const Vec3 ForwardWorld = Chassis.OrientationWorld.Rotate({1.0f, 0.0f, 0.0f});
    const Vec3 LeftWorld    = Chassis.OrientationWorld.Rotate({0.0f, 1.0f, 0.0f});
    const Vec3 UpWorld      = Chassis.OrientationWorld.Rotate({0.0f, 0.0f, 1.0f});

    const float ForwardSpeed = Dot(Chassis.LinearVelocityWorld, ForwardWorld);
    const float LateralSpeed = Dot(Chassis.LinearVelocityWorld, LeftWorld);
    const float BodyYawRate  = Dot(Chassis.AngularVelocityWorld, UpWorld);

    Telemetry.ForwardSpeedMps      = ForwardSpeed;
    Telemetry.LateralSpeedMps      = LateralSpeed;
    Telemetry.SpeedKilometresPerHr = std::sqrt(ForwardSpeed * ForwardSpeed + LateralSpeed * LateralSpeed) * 3.6f;
    Telemetry.YawRateRadPerSec     = BodyYawRate;

    //--------------------------------------------------------------------------------------------------------------------
    //   1. STEERING, ACKERMANN GEOMETRY & ACTIVE DRIVER ASSISTS (TCS / ESC / LAUNCH CONTROL)
    //--------------------------------------------------------------------------------------------------------------------
    std::array<float, 4> PrevSlipRatios{};
    std::array<float, 4> PrevWheelOmegas{};
    for (size_t Idx = 0; Idx < 4; ++Idx)
    {
        PrevSlipRatios[Idx]  = Corners[Idx].Slip.SlipRatio;
        PrevWheelOmegas[Idx] = Corners[Idx].WheelOmegaRadPerSec;
    }

    const float PrevFrontSatNm = Telemetry.Wheels[0].AligningTorqueNm + Telemetry.Wheels[1].AligningTorqueNm;
    const SteeringAndAssistOutputs SteerAssist = Steering.Step(
        Config.Steering,
        Config.Assists,
        ActiveCommand.Steer,
        ActiveCommand.Throttle,
        ActiveCommand.Brake,
        ForwardSpeed,
        BodyYawRate,
        Config.Geometry.WheelbaseMetres(),
        Config.Geometry.FrontTrackMetres(),
        PrevSlipRatios,
        PrevFrontSatNm,
        Dt);

    Telemetry.SteeringWheelRadians   = SteerAssist.SteeringWheelRadians;
    Telemetry.SteeringColumnTorqueNm = SteerAssist.SteeringColumnTorqueNm;
    Telemetry.TractionControlActive  = SteerAssist.TractionControlActive;
    Telemetry.StabilityControlActive = SteerAssist.StabilityControlActive;

    const std::array<float, 4> SteerAngles = {
        SteerAssist.FrontLeftSteerRadians,
        SteerAssist.FrontRightSteerRadians,
        0.0f,
        0.0f};

    //--------------------------------------------------------------------------------------------------------------------
    //   2. AERODYNAMICS (CLOSED-LOOP RIDE HEIGHTS + DRS / AIRBRAKE + DOWNFORCE & DRAG)
    //--------------------------------------------------------------------------------------------------------------------
    float FrontRideHeight = 0.11f;
    float RearRideHeight  = 0.12f;
    if (Hooks.Ground)
    {
        Vec3 SurfF{}, NormF{}, SurfR{}, NormR{};
        const Vec3 NoseWorld = Chassis.PositionWorld + ForwardWorld * 1.95f;
        const Vec3 TailWorld = Chassis.PositionWorld - ForwardWorld * 1.90f;
        if (Hooks.Ground(NoseWorld, SurfF, NormF))
        {
            FrontRideHeight = std::max(0.01f, Dot(NoseWorld - UpWorld * Config.Geometry.ChassisHalfExtentsMetres.z - SurfF, NormF));
        }
        if (Hooks.Ground(TailWorld, SurfR, NormR))
        {
            RearRideHeight = std::max(0.01f, Dot(TailWorld - UpWorld * Config.Geometry.ChassisHalfExtentsMetres.z - SurfR, NormR));
        }
    }

    AerodynamicInputs AeroIn{};
    AeroIn.ChassisVelocityWorld  = Chassis.LinearVelocityWorld;
    AeroIn.AmbientWindWorld      = Hooks.AmbientWindWorld;
    AeroIn.ChassisOrientation    = Chassis.OrientationWorld;
    AeroIn.FrontRideHeightMetres = FrontRideHeight;
    AeroIn.RearRideHeightMetres  = RearRideHeight;
    AeroIn.Throttle              = SteerAssist.ModulatedThrottle;
    AeroIn.Brake                 = ActiveCommand.Brake;
    AeroIn.SteerRadians          = SteerAssist.CenterSteerRadians;
    AeroIn.DrsRequested          = ActiveCommand.DrsRequested;
    AeroIn.DeltaSeconds          = Dt;
    Telemetry.Aero               = Aero.Step(Config.Aerodynamics, AeroIn);

    if (Hooks.ApplyForceAtPoint && Telemetry.Aero.DragMagnitudeNewtons > 0.0f)
    {
        Hooks.ApplyForceAtPoint(Telemetry.Aero.DragForceWorld, Chassis.PositionWorld + UpWorld * 0.12f);
        const Vec3 FrontAeroPt = Chassis.PositionWorld + ForwardWorld * (0.5f * Config.Geometry.WheelbaseMetres());
        const Vec3 RearAeroPt  = Chassis.PositionWorld - ForwardWorld * (0.5f * Config.Geometry.WheelbaseMetres());
        Hooks.ApplyForceAtPoint(-UpWorld * Telemetry.Aero.FrontDownforceNewtons, FrontAeroPt);
        Hooks.ApplyForceAtPoint(-UpWorld * Telemetry.Aero.RearDownforceNewtons, RearAeroPt);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //   3. POWERTRAIN (NA / TURBO / SUPERCHARGER + COOLANT + OIL + FUEL + AMT + DIFFERENTIALS)
    //--------------------------------------------------------------------------------------------------------------------
    DrivetrainInputs DriveIn{};
    DriveIn.Throttle            = SteerAssist.ModulatedThrottle;
    DriveIn.RequestedGear       = ActiveCommand.RequestedGear;
    DriveIn.WheelOmegaRadPerSec = PrevWheelOmegas;
    DriveIn.VehicleSpeedMps     = ForwardSpeed;
    DriveIn.DeltaSeconds        = Dt;
    Telemetry.Powertrain        = Powertrain.Step(DriveIn);

    //--------------------------------------------------------------------------------------------------------------------
    //   4. HYDRAULIC BRAKING ASSEMBLY + ROTOR THERMALS + 4-CHANNEL ABS + HANDBRAKE + LINE-LOCK
    //--------------------------------------------------------------------------------------------------------------------
    Brakes.Step(
        Config.Brakes,
        ActiveCommand.Brake,
        ActiveCommand.Handbrake,
        ActiveCommand.LineLock,
        ForwardSpeed,
        PrevSlipRatios,
        PrevWheelOmegas,
        SteerAssist.EscCornerBrakeTorquesNm,
        Dt);
    const auto& BrakeReadings = Brakes.QueryCorners();

    //--------------------------------------------------------------------------------------------------------------------
    //   5. SUSPENSION KINEMATICS + TWO-WAY SOFT-BODY TYRE PHYSICS (SPRING / XPBD / 3D FEM)
    //--------------------------------------------------------------------------------------------------------------------
    std::array<Vec3, 4>  HardpointsWorld{};
    std::array<Vec3, 4>  HardpointVelWorld{};
    std::array<float, 4> NewStrutLengths{};
    std::array<float, 4> StrutRatesMps{};

    for (size_t Idx = 0; Idx < 4; ++Idx)
    {
        const StrutSpecification& Spec     = Idx < 2 ? Config.FrontStrut : Config.RearStrut;
        const Vec3                RelArm   = Chassis.OrientationWorld.Rotate(Config.Geometry.StrutTopHardpointsBody[Idx]);
        const Vec3                TopWorld = Chassis.PositionWorld + RelArm;
        const Vec3                TopVel   = Chassis.LinearVelocityWorld + Cross(Chassis.AngularVelocityWorld, RelArm);
        HardpointsWorld[Idx]               = TopWorld;
        HardpointVelWorld[Idx]             = TopVel;

        // Solve equilibrium wheel hub position along the strut axis against the ground so suspension + soft-body tyre share the load!
        float CandidateLength = Spec.RestLengthMetres;
        if (!Corners[Idx].EnclosedCondition)
        {
            CandidateLength = Spec.MaxLengthMetres;
        }
        else if (Hooks.Ground)
        {
            const Vec3 ProbePoint = TopWorld - UpWorld * Spec.RestLengthMetres;
            Vec3       Surf{}, Norm{};
            if (Hooks.Ground(ProbePoint, Surf, Norm))
            {
                const Vec3  UnitNorm       = Norm.Normalized();
                const float GroundHubDist  = Dot(TopWorld - (Surf + UnitNorm * Config.Geometry.WheelOuterRadiusMetres), UpWorld);
                // Partition total geometric jounce between coilover strut and tyre radial stiffness:
                // k_strut / (k_strut + k_tyre) compresses the tyre; k_tyre / (k_strut + k_tyre) compresses the strut!
                const float TyreK          = std::max(Config.TyreSoftBody.VerticalStiffnessNpm, 50000.0f);
                const float StrutShare     = TyreK / (TyreK + Spec.SpringRateNpm);
                const float TotalJounce    = Spec.RestLengthMetres - GroundHubDist;
                if (TotalJounce > 0.0f)
                {
                    CandidateLength = ClampScalar(
                        Spec.RestLengthMetres - TotalJounce * StrutShare,
                        Spec.MinLengthMetres,
                        Spec.MaxLengthMetres);
                }
            }
        }

        StrutRatesMps[Idx]   = (Corners[Idx].StrutLengthMetres - CandidateLength) / Dt;
        NewStrutLengths[Idx] = CandidateLength;
    }

    // Evaluate all 4 struts (with opposite-side compression for anti-roll bars) + step all 4 soft-body tyres!
    const float QuarterMass = 0.25f * Config.Geometry.ChassisMassKg;
    std::array<StrutReading, 4>        StrutReadings{};
    std::array<TyreSoftBodyReading, 4> SoftReadings{};
    std::array<Quat, 4>                CarrierOrientations{};

    for (size_t Idx = 0; Idx < 4; ++Idx)
    {
        const bool                Front        = Idx < 2;
        const bool                LeftSide     = (Idx % 2) == 0;
        const size_t              OppositeIdx  = LeftSide ? (Idx + 1) : (Idx - 1);
        const StrutSpecification& Spec         = Front ? Config.FrontStrut : Config.RearStrut;
        const StrutSpecification& OppSpec      = Front ? Config.FrontStrut : Config.RearStrut;
        const float               OppComp      = OppSpec.RestLengthMetres - NewStrutLengths[OppositeIdx];
        const float               ArbStiff     = Front ? Config.AntiRoll.FrontStiffnessNpm : Config.AntiRoll.RearStiffnessNpm;

        Corners[Idx].StrutLengthMetres = NewStrutLengths[Idx];
        StrutReadings[Idx]             = SuspensionKinematics::EvaluateCorner(
            Spec,
            NewStrutLengths[Idx],
            StrutRatesMps[Idx],
            OppComp,
            ArbStiff,
            QuarterMass,
            Dt,
            LeftSide);

        const Quat SteerQuat  = Quat::AxisAngle({0.0f, 0.0f, 1.0f}, SteerAngles[Idx]);
        const Quat CamberQuat = Quat::AxisAngle({1.0f, 0.0f, 0.0f}, StrutReadings[Idx].DynamicCamberRad);
        CarrierOrientations[Idx] = (Chassis.OrientationWorld * SteerQuat * CamberQuat).Normalized();

        const Vec3 HubWorld = HardpointsWorld[Idx] - UpWorld * NewStrutLengths[Idx];
        const Vec3 HubVel   = HardpointVelWorld[Idx] + UpWorld * StrutRatesMps[Idx];

        TyreSoftBodyInputs SoftIn{};
        SoftIn.HubPositionWorld      = HubWorld;
        SoftIn.HubOrientationWorld   = CarrierOrientations[Idx];
        SoftIn.HubVelocityWorld      = HubVel;
        SoftIn.SpinAngleRadians      = Corners[Idx].SpinAngleRadians;
        SoftIn.WheelOmegaRadPerSec   = Corners[Idx].WheelOmegaRadPerSec;
        SoftIn.GaugePressurePa       = Corners[Idx].Thermal.GaugePressurePa;
        SoftIn.LateralForceEstimateN = Corners[Idx].PrevLateralForceN;
        SoftIn.LongitudinalForceEstN = Corners[Idx].PrevLongForceN;
        SoftIn.DeltaSeconds          = Dt;
        SoftIn.Ground                = Corners[Idx].EnclosedCondition ? Hooks.Ground : GroundQuery{};

        SoftReadings[Idx] = Corners[Idx].SoftBody.Step(SoftIn);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //   6. TRANSIENT TYRE SLIP + 3-NODE THERMODYNAMICS + IDEAL-GAS TYRE PRESSURE
    //--------------------------------------------------------------------------------------------------------------------
    std::array<Vec3, 4> WheelForwardDirs{};
    std::array<Vec3, 4> WheelLeftDirs{};
    std::array<float, 4> EffectiveNormalLoads{};

    for (size_t Idx = 0; Idx < 4; ++Idx)
    {
        CornerRuntime&              Corner = Corners[Idx];
        const TyreSoftBodyReading&  Soft   = SoftReadings[Idx];
        const StrutReading&         Strut  = StrutReadings[Idx];
        const CornerBrakeReading&   Brake  = BrakeReadings[Idx];
        WheelCornerTelemetry&       Tel    = Telemetry.Wheels[Idx];

        const Vec3 ContactNormal = Soft.InContact ? Soft.ContactNormalWorld : UpWorld;
        Vec3       WheelFwd      = CarrierOrientations[Idx].Rotate({1.0f, 0.0f, 0.0f});
        WheelFwd                 = (WheelFwd - ContactNormal * Dot(WheelFwd, ContactNormal)).Normalized();
        if (WheelFwd.LengthSquared() < 0.25f) WheelFwd = ForwardWorld;
        const Vec3 WheelLeft     = Cross(ContactNormal, WheelFwd).Normalized();
        WheelForwardDirs[Idx]    = WheelFwd;
        WheelLeftDirs[Idx]       = WheelLeft;

        // Combine soft-body carcass reaction + suspension strut support so tyre load tracks weight transfer & aero!
        const float AeroCornerLoad = Idx < 2 ? (0.5f * Telemetry.Aero.FrontDownforceNewtons) : (0.5f * Telemetry.Aero.RearDownforceNewtons);
        const float CombinedFz     = Soft.InContact
                                         ? std::max(0.0f, 0.50f * Soft.NormalLoadNewtons + 0.50f * Strut.TotalStrutForceN + AeroCornerLoad)
                                         : 0.0f;
        EffectiveNormalLoads[Idx]  = CombinedFz;

        const Vec3  PatchPoint     = Soft.InContact
                                         ? Soft.PatchCentreWorld
                                         : (HardpointsWorld[Idx] - UpWorld * (Corner.StrutLengthMetres + Config.Geometry.WheelOuterRadiusMetres));
        const Vec3  PatchVel       = Chassis.LinearVelocityWorld + Cross(Chassis.AngularVelocityWorld, PatchPoint - Chassis.PositionWorld);
        const float VxWheel        = Dot(PatchVel, WheelFwd);
        const float VyWheel        = Dot(PatchVel, WheelLeft);

        TyreSlipInputs SlipIn{};
        SlipIn.LongitudinalSpeed = VxWheel;
        SlipIn.LateralSpeed      = VyWheel;
        SlipIn.WheelOmega        = Corner.WheelOmegaRadPerSec;
        SlipIn.EffectiveRadius   = Soft.LoadedRadiusMetres;
        SlipIn.WheelInertia      = Config.Geometry.WheelInertiaKgm2;
        SlipIn.DriveTorqueNm     = Telemetry.Powertrain.WheelDriveTorqueNm[Idx];
        SlipIn.BrakeTorqueNm     = Brake.AppliedTorqueNm;
        SlipIn.NormalLoadNewtons = CombinedFz;
        SlipIn.CamberRadians     = Strut.DynamicCamberRad;
        SlipIn.GripMultiplier    = Corner.Thermal.GripMultiplier;
        SlipIn.DeltaSeconds      = Dt;

        const TyreSlipOutputs SlipOut = SlipSolver.Step(Config.SlipMode, MagicFormula, SlipIn, Corner.Slip);
        Corner.WheelOmegaRadPerSec    = SlipOut.NextWheelOmega;
        Corner.SpinAngleRadians       = WrapAngleRadians(Corner.SpinAngleRadians + Corner.WheelOmegaRadPerSec * Dt);
        Corner.PrevLongForceN         = SlipOut.LongitudinalForceNewtons;
        Corner.PrevLateralForceN      = SlipOut.LateralForceNewtons;

        // Step 3-node tyre thermodynamics + ideal-gas pressure using live soft-body cavity volume!
        TyreThermalStepInputs ThermIn{};
        ThermIn.SlipPowerWatts    = SlipOut.SlipPowerWatts;
        ThermIn.NormalLoadNewtons = CombinedFz;
        ThermIn.RollingSpeedMps   = std::sqrt(VxWheel * VxWheel + VyWheel * VyWheel);
        ThermIn.DeflectionRateMps = Soft.DeflectionRateMps;
        ThermIn.LiveVolumeCubicM  = Soft.EnclosedVolumeM3;
        ThermIn.DeltaSeconds      = Dt;
        TyreThermalIntegrator::Step(Config.TyreThermal, ThermIn, Corner.Thermal);

        const Vec3 HubWorld             = HardpointsWorld[Idx] - UpWorld * Corner.StrutLengthMetres;
        Tel.HubPositionWorld            = HubWorld;
        Tel.CarrierOrientationWorld     = CarrierOrientations[Idx];
        Tel.WheelOrientationWorld       = (CarrierOrientations[Idx] * Quat::AxisAngle({0.0f, 1.0f, 0.0f}, Corner.SpinAngleRadians)).Normalized();
        Tel.ContactPointWorld           = PatchPoint;
        Tel.ContactNormalWorld          = ContactNormal;
        Tel.InContact                   = Soft.InContact;
        Tel.WheelLocked                 = SlipOut.WheelLocked;
        Tel.AbsActive                   = Brake.AbsActive;
        Tel.Shredded                    = Corner.Thermal.WearFraction >= 0.995f || (Corner.Thermal.Punctured && Soft.RimBottomed);
        Tel.SteerAngleRadians           = SteerAngles[Idx];
        Tel.SpinAngleRadians            = Corner.SpinAngleRadians;
        Tel.WheelOmegaRadPerSec         = Corner.WheelOmegaRadPerSec;
        Tel.StrutCompressionMetres      = Strut.CompressionMetres;
        Tel.StrutForceNewtons           = Strut.TotalStrutForceN;
        Tel.NormalLoadNewtons           = CombinedFz;
        Tel.LongitudinalForceN          = SlipOut.LongitudinalForceNewtons;
        Tel.LateralForceN               = SlipOut.LateralForceNewtons;
        Tel.AligningTorqueNm            = SlipOut.AligningTorqueNm;
        Tel.SlipRatio                   = SlipOut.SlipRatio;
        Tel.SlipAngleRadians            = SlipOut.SlipAngleRadians;
        Tel.SlipPowerWatts              = SlipOut.SlipPowerWatts;
        Tel.BrakeTorqueNm               = Brake.AppliedTorqueNm;
        Tel.BrakeRotorTempK             = Brake.RotorTempK;
        Tel.SoftBody                    = Soft;
        Tel.Thermal                     = Corner.Thermal;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //   7. COUPLED MULTI-CONTACT PGS COULOMB CONE & DISC-BRAKE STATIONARITY SOLVER (SLOPE HOLD)
    //--------------------------------------------------------------------------------------------------------------------
    Telemetry.StationarityHoldActive = false;
    const float HorizontalSpeed      = std::sqrt(ForwardSpeed * ForwardSpeed + LateralSpeed * LateralSpeed);
    const bool  BrakedLowSpeed       = (ActiveCommand.Throttle < 0.03f) &&
                                 (ActiveCommand.Brake > 0.05f || ActiveCommand.Handbrake) &&
                                 (HorizontalSpeed < 2.2f);

    if (BrakedLowSpeed)
    {
        uint32_t GroundedCount = 0u;
        Vec3     AvgNormal{};
        for (size_t Idx = 0; Idx < 4; ++Idx)
        {
            if (Telemetry.Wheels[Idx].InContact && EffectiveNormalLoads[Idx] > 20.0f)
            {
                ++GroundedCount;
                AvgNormal += Telemetry.Wheels[Idx].ContactNormalWorld;
            }
        }

        if (GroundedCount > 0u)
        {
            Telemetry.StationarityHoldActive = true;
            AvgNormal                        = AvgNormal.Normalized();

            // Gravity tangential downslope force + velocity arrest impulse across Dt:
            const Vec3 GravityWorld{0.0f, 0.0f, -StandardGravity * Config.Geometry.ChassisMassKg};
            const Vec3 GravityTangent = GravityWorld - AvgNormal * Dot(GravityWorld, AvgNormal);
            const Vec3 ArrestImpulse  = -(Chassis.LinearVelocityWorld - AvgNormal * Dot(Chassis.LinearVelocityWorld, AvgNormal)) *
                                       (Config.Geometry.ChassisMassKg / std::max(Dt * 2.5f, 0.008f));
            const Vec3 RequiredInPlane = ArrestImpulse - GravityTangent;
            const Vec3 PerWheelTarget  = RequiredInPlane * (1.0f / static_cast<float>(GroundedCount));

            const float HoldBlend      = ClampScalar(1.0f - HorizontalSpeed / 2.2f, 0.0f, 1.0f);
            for (size_t Idx = 0; Idx < 4; ++Idx)
            {
                WheelCornerTelemetry& Tel = Telemetry.Wheels[Idx];
                if (!Tel.InContact || EffectiveNormalLoads[Idx] <= 20.0f) continue;

                const float StaticMu     = 1.18f * Corners[Idx].Thermal.GripMultiplier;
                const float ConeRadiusN  = StaticMu * EffectiveNormalLoads[Idx];
                const float CaliperCapN  = Tel.BrakeTorqueNm / std::max(Tel.SoftBody.LoadedRadiusMetres, 0.1f);

                const float HoldFx       = ClampScalar(Dot(PerWheelTarget, WheelForwardDirs[Idx]), -CaliperCapN, CaliperCapN);
                const float HoldFy       = ClampScalar(Dot(PerWheelTarget, WheelLeftDirs[Idx]), -ConeRadiusN, ConeRadiusN);

                // Project onto Coulomb friction cone ||(Fx, Fy)|| <= ConeRadiusN.
                float BlendedFx          = LerpScalar(Tel.LongitudinalForceN, HoldFx, HoldBlend);
                float BlendedFy          = LerpScalar(Tel.LateralForceN, HoldFy, HoldBlend);
                const float Mag          = std::sqrt(BlendedFx * BlendedFx + BlendedFy * BlendedFy);
                if (Mag > ConeRadiusN && Mag > 1.0e-5f)
                {
                    const float Scale = ConeRadiusN / Mag;
                    BlendedFx *= Scale;
                    BlendedFy *= Scale;
                }
                Tel.LongitudinalForceN = BlendedFx;
                Tel.LateralForceN      = BlendedFy;
                if (HoldBlend > 0.65f && std::fabs(HoldFx) < CaliperCapN)
                {
                    Corners[Idx].WheelOmegaRadPerSec = 0.0f;
                    Tel.WheelOmegaRadPerSec          = 0.0f;
                    Tel.WheelLocked                  = true;
                }
            }
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //   8. APPLY CORNER REACTIONS TO SPRUNG CHASSIS RIGID BODY
    //--------------------------------------------------------------------------------------------------------------------
    if (Hooks.ApplyForceAtPoint)
    {
        for (size_t Idx = 0; Idx < 4; ++Idx)
        {
            const WheelCornerTelemetry& Tel = Telemetry.Wheels[Idx];
            if (!Tel.InContact) continue;

            // Vertical support acts through the suspension strut + soft-body normal reaction; in-plane tyre forces act
            // near the roll-centre / contact patch height to produce realistic pitch squat, brake dive, and cornering roll!
            const Vec3 SupportForce = Tel.ContactNormalWorld * (0.5f * Tel.StrutForceNewtons + 0.5f * Tel.SoftBody.NormalLoadNewtons);
            const Vec3 InPlaneForce = WheelForwardDirs[Idx] * Tel.LongitudinalForceN + WheelLeftDirs[Idx] * Tel.LateralForceN;
            const Vec3 ForcePoint   = LerpScalar(0.0f, 1.0f, 0.55f) * Tel.ContactPointWorld +
                                    LerpScalar(0.0f, 1.0f, 0.45f) * HardpointsWorld[Idx];

            Hooks.ApplyForceAtPoint(SupportForce, HardpointsWorld[Idx]);
            Hooks.ApplyForceAtPoint(InPlaneForce, ForcePoint);
        }
    }

    if (Hooks.ApplyTorque)
    {
        float TotalAligningTorque = 0.0f;
        for (const WheelCornerTelemetry& Tel : Telemetry.Wheels)
        {
            if (Tel.InContact) TotalAligningTorque += Tel.AligningTorqueNm;
        }
        Hooks.ApplyTorque(UpWorld * (TotalAligningTorque * 0.25f));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                      SCENEVIEWPORT VEHICLEPROFILE ENCODING
//------------------------------------------------------------------------------------------------------------------------

void VehicleSolver::EncodeSceneVehicleProfile(void* Destination10448Bytes) const noexcept
{
    if (Destination10448Bytes == nullptr) return;
    std::memset(Destination10448Bytes, 0, SceneVehicleProfileByteCount);

    float* Out = static_cast<float*>(Destination10448Bytes);

    // 0..4: Cabin, Column, Axis, CabinFrame, CabinShift (20 floats = 80 bytes).
    Out[0]  = Telemetry.SteeringWheelRadians;
    Out[1]  = 1.0f;   // see-through cabin glass enabled
    Out[2]  = 0.0f;
    Out[3]  = ActiveCommand.HeadlightsOn ? 1.0f : 0.0f;

    Out[4]  = Config.Geometry.SteeringColumnPivotBody.x;
    Out[5]  = Config.Geometry.SteeringColumnPivotBody.y;
    Out[6]  = Config.Geometry.SteeringColumnPivotBody.z;
    Out[7]  = 0.0f;

    Out[8]  = Config.Geometry.SteeringColumnAxisBody.x;
    Out[9]  = Config.Geometry.SteeringColumnAxisBody.y;
    Out[10] = Config.Geometry.SteeringColumnAxisBody.z;
    Out[11] = 0.0f;

    Out[12] = Config.Geometry.CabinFrameAnchor.x;
    Out[13] = Config.Geometry.CabinFrameAnchor.y;
    Out[14] = Config.Geometry.CabinFrameAnchor.z;
    Out[15] = 1.0f;

    Out[16] = Config.Geometry.CabinTabletCenter.x;
    Out[17] = Config.Geometry.CabinTabletCenter.y;
    Out[18] = Config.Geometry.CabinTabletCenter.z;
    Out[19] = 1.0f;

    // 5..8: Wheels[4] (x = shredded, y = carcassActive: 0 for Spring Path B, 1 for XPBD / 3D FEM Path A).
    const float CarcassActiveFlag = (Config.SoftBodyTyreMode == TyreFormulation::Spring) ? 0.0f : 1.0f;
    for (size_t Wheel = 0; Wheel < 4; ++Wheel)
    {
        const size_t Base = 20u + Wheel * 4u;
        Out[Base + 0u]    = Telemetry.Wheels[Wheel].Shredded ? 1.0f : 0.0f;
        Out[Base + 1u]    = CarcassActiveFlag;
        Out[Base + 2u]    = static_cast<float>(Config.SoftBodyTyreMode);
        Out[Base + 3u]    = Telemetry.Wheels[Wheel].Thermal.SurfaceTemperatureK;
    }

    // 9..12: Deformation[4] (x = ContactAngle, y = Deflection, z = GaugePressure, w = LateralDistortion).
    for (size_t Wheel = 0; Wheel < 4; ++Wheel)
    {
        const WheelCornerTelemetry& Tel  = Telemetry.Wheels[Wheel];
        const size_t                Base = 36u + Wheel * 4u;
        Out[Base + 0u]                   = Tel.SoftBody.ContactAngleRadians;
        Out[Base + 1u]                   = Tel.SoftBody.DeflectionMetres;
        Out[Base + 2u]                   = Tel.Thermal.GaugePressurePa;
        Out[Base + 3u]                   = Tel.SoftBody.LateralDistortionMetres;
    }

    // 13..652: CarcassDisplacements[640] (4 wheels * 160 float4 wheel-local nodal displacements u = x - X).
    for (size_t Wheel = 0; Wheel < 4; ++Wheel)
    {
        Corners[Wheel].SoftBody.WriteCarcassDisplacements(Out + 52u + Wheel * (TyreNodesPerWheel * 4u));
    }
}

} // namespace Frontier::Vehicle
