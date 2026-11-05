/*==========================================================================================================================================
                                                               DRIVETRAIN.CPP
==========================================================================================================================================*/
// 🧩 Frontier/PhysicalDynamics/Vehicle/Drivetrain.cpp

#include "Drivetrain.h"

namespace Frontier::Vehicle
{

namespace {
inline float Clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline float Sign(float x) noexcept { return (x > 0.0f) ? 1.0f : ((x < 0.0f) ? -1.0f : 0.0f); }
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              DEFAULT SPEC SHEETS (GRIT preset-1 GTR)
//------------------------------------------------------------------------------------------------------------------------
EngineParameters EngineParameters::DefaultGTR()
{
    EngineParameters E;
    // rpm → Nm, peak 520 @ 5800 (18-point curve, as in GRIT EngineSpecifications.h)
    const float T[18][2] = {
        {800,234},{1200,300},{1600,352},{2000,392},{2400,428},{2800,460},{3200,486},{3600,504},
        {4000,514},{4400,518},{4800,520},{5200,520},{5800,520},{6200,506},{6600,486},{7000,462},
        {7600,448},{8500,442}};
    for (auto& p : T) E.TorqueCurve.Add(p[0], p[1]);

    const float F[5][2]  = {{800,0.04f},{2000,0.10f},{4000,0.22f},{6000,0.35f},{8500,0.48f}}; // exhaust mass flow kg/s
    for (auto& p : F) E.ExhaustFlowCurve.Add(p[0], p[1]);
    const float G[5][2]  = {{800,673.0f},{2000,873.0f},{4000,1023.0f},{6000,1123.0f},{8500,1173.0f}}; // exhaust temp K
    for (auto& p : G) E.ExhaustTempCurve.Add(p[0], p[1]);

    E.IdleRPM = 700.0f; E.RedlineRPM = 7200.0f;
    E.MaxRPMIncrease = 1800.0f; E.MaxRPMDecrease = 2600.0f;
    E.EngineBrakingCoeff = 0.08f; E.EngineBrakingQuadratic = 0.00012f;
    E.EngineInertia = 0.38f; E.InvEngineInertia = 2.6315789f;
    return E;
}

TurbochargerParameters TurbochargerParameters::DefaultGTR()
{
    TurbochargerParameters T;
    const float B[6][2] = {{1000,0.0f},{20000,0.2f},{50000,0.8f},{90000,1.2f},{120000,1.4f},{150000,1.3f}};
    for (auto& p : B) T.BoostPressureCurve.Add(p[0], p[1]);
    const float M[4][2] = {{0.0f,1.0f},{0.5f,1.3f},{1.0f,1.7f},{1.2f,1.85f}};
    for (auto& p : M) T.TorqueMultiplierCurve.Add(p[0], p[1]);

    T.TurboInertia = 0.02f; T.InvTurboInertia = 50.0f;
    T.TurboFrictionCoeff = 0.002f; T.TurboFrictionQuadratic = 0.00005f;
    T.WastegateMaxBoost = 1.2f; T.BovPressureReleaseRate = 10.0f;
    T.TurbineEfficiency = 0.65f; T.CompressorEfficiency = 0.70f; T.MechanicalEfficiency = 0.98f;
    return T;
}

// GRIT SuperchargerSpecifications preset-1 (twin-screw). Curves are in CHARGER rpm (= engine rpm × 3.2).
SuperchargerParameters SuperchargerParameters::DefaultTwinScrew()
{
    SuperchargerParameters S;
    S.DriveType = SuperchargerDriveType::TwinScrew;
    const float B[6][2] = {{2000,0.0f},{5000,0.15f},{10000,0.38f},{15000,0.62f},{20000,0.82f},{24000,0.95f}};
    for (auto& p : B) S.BoostPressureCurve.Add(p[0], p[1]);
    const float M[4][2] = {{0.0f,1.0f},{0.4f,1.25f},{0.8f,1.55f},{1.2f,1.75f}};
    for (auto& p : M) S.TorqueMultiplierCurve.Add(p[0], p[1]);
    const float D[6][2] = {{2000,5.0f},{5000,10.0f},{10000,24.0f},{15000,45.0f},{20000,68.0f},{24000,82.0f}};
    for (auto& p : D) S.ParasiticDragCurve.Add(p[0], p[1]);

    S.ChargerInertia = 0.015f; S.ChargerFrictionCoeff = 0.003f; S.ChargerFrictionQuadratic = 0.00008f;
    S.DriveRatio = 3.2f; S.MaxBoost_Pump = 0.8f; S.MaxBoost_Race = 1.2f;
    S.BypassValveRate = 8.0f; S.CompressorEfficiency = 0.72f; S.ParasiticLossFactor = 0.08f;
    return S;
}

//------------------------------------------------------------------------------------------------------------------------
//   Belt-driven supercharger. Boost tracks the crank instantly (no spool): charger rpm = engine rpm × DriveRatio, boost
//   from the curve, capped at the tune ceiling by the recirculation (bypass) valve. Off-throttle the bypass vents boost
//   so the blower recirculates instead of compressing against a closed throttle — that also drops the parasitic drag to
//   its ParasiticLossFactor floor. Returns the torque multiplier; writes the parasitic crank load.
//------------------------------------------------------------------------------------------------------------------------
float Drivetrain::StepSupercharger(float engineRPM, float throttle, float dt, float& parasiticDrag_Nm) noexcept
{
    const float thr        = Clampf(throttle, 0.0f, 1.0f);
    const float chargerRPM = Super.ChargerRPM(engineRPM);
    const float maxBoost   = RaceTune ? Super.MaxBoost_Race : Super.MaxBoost_Pump;

    // Raw displacement boost from the curve, capped at the tune ceiling, gated by throttle (bypass recirculates the rest).
    const float rawBoost    = Super.BoostPressureCurve.Sample(chargerRPM);
    const float targetBoost = std::min(rawBoost, maxBoost) * thr;

    // Recirculation valve slews the delivered boost toward the target at BypassValveRate [Bar/s] (first-order, rate-limited).
    const float dBoostMax = Super.BypassValveRate * dt;
    SuperBoostBar += Clampf(targetBoost - SuperBoostBar, -dBoostMax, dBoostMax);
    SuperBoostBar  = Clampf(SuperBoostBar, 0.0f, maxBoost);

    // Parasitic crank drag: full curve figure while compressing under throttle; falls to the residual floor when bypassed.
    const float dragFull = Super.ParasiticDragCurve.Sample(chargerRPM);
    parasiticDrag_Nm     = dragFull * (Super.ParasiticLossFactor + (1.0f - Super.ParasiticLossFactor) * thr);

    return Super.TorqueMultiplierCurve.Sample(SuperBoostBar);
}

//------------------------------------------------------------------------------------------------------------------------
Drivetrain::Drivetrain()
{
    Engine = EngineParameters::DefaultGTR();
    Turbo  = TurbochargerParameters::DefaultGTR();
    EngineOmega = Engine.IdleRPM * kRpmToRad;
}

void Drivetrain::Reset(float engineRPM) noexcept
{
    EngineOmega = engineRPM * kRpmToRad;
    TurboOmegaRad = 0.0f;
    BoostBar = 0.0f;
    BovPosition = 0.0f;
    WastegatePosition = 0.0f;
    SuperBoostBar = 0.0f;
    ManifoldPressure_Bar = 1.01325f;
    IdleServo            = 0.0f;
    LockupEngagement     = 0.0f;
    MotorTorqueSlew_Nm   = 0.0f;
    BatteryCharge        = Battery.InitialCharge;
    BatteryTemperature_K = Battery.AmbientTemperature_K;
}

//------------------------------------------------------------------------------------------------------------------------
//   Net engine torque at a given ω: torque-curve × boost multiplier × throttle − engine braking.
//------------------------------------------------------------------------------------------------------------------------
float Drivetrain::NetEngineTorque(float omega, float throttle, float boostMult, float clutchLoad) const noexcept
{
    // Faithful to GRIT EvalNetTorque: drive = SampleTorque(rpm)·boost·throttle; resistance = friction (unblended)
    // + pumping·(1−throttle); net = drive − resistance − load.
    const float thr = Clampf(throttle, 0.0f, 1.0f);
    const float rpm = omega * kRadToRpm;
    const float wot = Engine.TorqueCurve.Sample(rpm);
    const float drive = wot * boostMult * thr;
    const float frictionTq = Engine.EngineBrakingCoeff * omega * FrictionScale;
    // 📝 With the intake on, the caller passes the air ratio as the throttle (vacuum meters the air) and the pumping
    //    loss is the real throttling work Δp·Vd/4π — the quadratic law is negligible at idle vacuum and would run the
    //    idle away. Off, this is the source's law bit for bit.
    const float pumpingTq = Intake.Enabled
        ? std::max(0.0f, UpstreamPressure_Bar - ManifoldPressure_Bar) * 1.0e5f * (std::max(0.5f, Intake.Displacement_L) * 0.001f) / 12.5663706f
        : Engine.EngineBrakingQuadratic * omega * omega * FrictionScale;
    const float resistance = Intake.Enabled ? frictionTq + pumpingTq : frictionTq + pumpingTq * (1.0f - thr);
    return drive - resistance - clutchLoad;
}

//------------------------------------------------------------------------------------------------------------------------
//   Turbo spool — faithful port of GRIT SolvePowertrain STEP 1–7 (VehicleSolver.cpp):
//   isentropic turbine expansion vs isentropic compressor load, net shaft power balance with speed-dependent friction,
//   shaft-speed integration, first-order boost lag (FInterpTo), blow-off valve, and wastegate regulation.
//
//   The shaft ODE is stiff (large InvTurboInertia); GRIT relies on a high-rate physics loop, so we sub-step the shaft
//   integration internally here for stability under the caller's larger dt (Phase-0's physics thread supplies the rate).
//------------------------------------------------------------------------------------------------------------------------
void Drivetrain::SpoolTurbo(float engineRPM, float throttle, float dt) noexcept
{
    constexpr float Cp_exhaust = 1150.0f, Gamma_exhaust = 1.33f;
    constexpr float AmbientPressure_Pa = 101325.0f;
    constexpr float Cp_air = 1005.0f, Gamma_air = 1.4f, T_ambient = 293.15f;

    const float MaxFlowRate = Engine.ExhaustFlowCurve.Sample(engineRPM);   // kg/s
    const float MaxExhaustTemp = Engine.ExhaustTempCurve.Sample(engineRPM);// K
    const float thr = Clampf(throttle, 0.0f, 1.0f);
    const float CurrentFlowRate = MaxFlowRate * thr;
    const float CurrentExhaustTemp = 573.0f + (MaxExhaustTemp - 573.0f) * thr;  // FMath::Lerp

    // STEP 1 — Turbine power (isentropic expansion, variable efficiency).
    const float ExhaustPressure_Pa = AmbientPressure_Pa * (1.3f + 0.8f * thr);
    const float TurbineExpansionRatio = ExhaustPressure_Pa / AmbientPressure_Pa;
    const float Cp_exhaust_actual = Cp_exhaust + 0.15f * (CurrentExhaustTemp - 773.0f);
    const float IsentropicWork_turbine = Cp_exhaust_actual * CurrentExhaustTemp
        * (1.0f - std::pow(1.0f / TurbineExpansionRatio, (Gamma_exhaust - 1.0f) / Gamma_exhaust));

    // Sub-stepped shaft dynamics.
    const int subSteps = 16;
    const float h = dt / subSteps;
    for (int s = 0; s < subSteps; ++s)
    {
        const float turboRPM = TurboOmegaRad * kRadToRpm;

        const float BladeSpeedRatio = (TurboOmegaRad * 0.025f) / std::sqrt(std::max(CurrentExhaustTemp, 300.0f));
        const float TurbineEfficiency_actual = Turbo.TurbineEfficiency
            * Clampf(0.85f + 0.15f * BladeSpeedRatio, 0.70f, 1.0f);
        const float TurbinePower = CurrentFlowRate * IsentropicWork_turbine * TurbineEfficiency_actual;

        // STEP 2 — Compressor power (isentropic compression, variable efficiency).
        const float TargetBoostPressure = Turbo.BoostPressureCurve.Sample(turboRPM);
        const float CompressorPressureRatio = 1.0f + TargetBoostPressure;
        const float IsentropicWork_compressor = Cp_air * T_ambient
            * (std::pow(CompressorPressureRatio, (Gamma_air - 1.0f) / Gamma_air) - 1.0f);
        const float CorrectedSpeed = turboRPM / std::sqrt(T_ambient / 288.15f);
        const float SpeedFactor = Clampf(CorrectedSpeed / 90000.0f, 0.3f, 1.2f);
        const float SpeedPenalty = (SpeedFactor < 0.7f) ? (1.0f - 0.12f * (0.7f - SpeedFactor)) : 1.0f;
        const float PressureRatioFactor = Clampf(CompressorPressureRatio / 2.0f, 0.5f, 1.5f);
        const float PressurePenalty = std::fabs(PressureRatioFactor - 1.0f) * 0.08f;
        float CompressorEfficiency_actual = Turbo.CompressorEfficiency * SpeedPenalty * (1.0f - PressurePenalty);
        const float HeatTransferPenalty = (SpeedFactor < 0.6f) ? 0.06f * (1.0f - SpeedFactor / 0.6f) : 0.0f;
        CompressorEfficiency_actual = std::max(CompressorEfficiency_actual - HeatTransferPenalty, 0.50f);
        const float CompressorPower = (CurrentFlowRate * IsentropicWork_compressor)
                                    / std::max(CompressorEfficiency_actual, 0.01f);

        // STEP 2.5 — internal heat transfer.
        const float TurbineHousingTemp = CurrentExhaustTemp * 0.85f;
        const float CompressorHousingTemp = T_ambient + TargetBoostPressure * 15.0f;
        const float InternalHeatTransfer = 45.0f * (TurbineHousingTemp - CompressorHousingTemp);

        // STEP 3 — net shaft power.
        const float NetPower_beforeFriction = (TurbinePower - CompressorPower) * Turbo.MechanicalEfficiency;
        const float TurboFriction = -(Turbo.TurboFrictionCoeff * TurboOmegaRad
                                    + Turbo.TurboFrictionQuadratic * TurboOmegaRad * TurboOmegaRad);
        const float FrictionPower = TurboFriction * TurboOmegaRad;
        const float NetPower = NetPower_beforeFriction + FrictionPower - InternalHeatTransfer * 0.1f;

        // STEP 4 — shaft acceleration.
        const float DrivingTorque = NetPower / std::max(TurboOmegaRad, 1.0f);
        const float TurboAlpha = DrivingTorque * Turbo.InvTurboInertia;
        TurboOmegaRad = std::max(TurboOmegaRad + TurboAlpha * h, 0.0f);
    }

    const float turboRPM = TurboOmegaRad * kRadToRpm;
    const float CorrectedSpeed = turboRPM / std::sqrt(T_ambient / 288.15f);
    const float SpeedFactor = Clampf(CorrectedSpeed / 90000.0f, 0.3f, 1.2f);

    // STEP 5 — compressor surge protection (drives BovPosition).
    const float MassFlowRatio = CurrentFlowRate / std::max(MaxFlowRate, 0.01f);
    const float CompressorPressureRatio = 1.0f + Turbo.BoostPressureCurve.Sample(turboRPM);
    const float SurgeMarginPressureRatio = 1.3f + 0.4f * MassFlowRatio;
    if (CompressorPressureRatio > SurgeMarginPressureRatio && MassFlowRatio < 0.15f)
        BovPosition = std::min(BovPosition + dt * 5.0f, 1.0f);

    // STEP 6 — boost first-order lag (FMath::FInterpTo) with BOV relief.
    const float PotentialBoost = Turbo.BoostPressureCurve.Sample(turboRPM);
    const float BoostTimeConstant = 2.5f - 1.5f * SpeedFactor;
    const float interpAlpha = Clampf(dt * (1.0f / std::max(BoostTimeConstant, 0.1f)), 0.0f, 1.0f);
    BoostBar += (PotentialBoost - BoostBar) * interpAlpha;   // FInterpTo

    if (thr < 0.1f && BoostBar > 0.15f)
    {
        const float BovReleaseRate = Turbo.BovPressureReleaseRate * BovPosition;
        BoostBar = std::max(0.0f, BoostBar - BovReleaseRate * dt);
        BovPosition = std::min(BovPosition + dt * 3.0f, 1.0f);
    }
    else
    {
        BovPosition = std::max(BovPosition - dt * 8.0f, 0.0f);
    }

    // STEP 7 — wastegate regulation.
    const float MaxBoostTarget = Turbo.WastegateMaxBoost;
    if (BoostBar > MaxBoostTarget)
    {
        WastegatePosition = Clampf((BoostBar - MaxBoostTarget) * 2.0f, 0.0f, 1.0f);
        BoostBar = MaxBoostTarget;   // overboost clamp
    }
    else
    {
        WastegatePosition = std::max(WastegatePosition - dt * 5.0f, 0.0f);
    }
    BoostBar = Clampf(BoostBar, 0.0f, Turbo.WastegateMaxBoost);
}

//------------------------------------------------------------------------------------------------------------------------
//   Intake manifold filling (Heywood mean-value law): the throttle meters air into the plenum through a compressible
//   orifice (choked below the critical ratio), the engine gulps it by speed-density, and the air ratio ρ_m/ρ_up scales
//   the torque — vacuum, not the pedal, meters the air at part throttle. Returns the ratio for NetEngineTorque.
//   The idle governor is a PI loop on the bypass trim holding 30 rpm above the hard idle floor (a droop above stall).
//------------------------------------------------------------------------------------------------------------------------
float Drivetrain::StepIntake(float upstream_Bar, float engineRPM, float throttle, float dt) noexcept
{
    constexpr float GasConstant = 287.0f, Gamma = 1.4f, AmbientT = 293.15f;
    const float pedal = Clampf(throttle, 0.0f, 1.0f);
    float idleErr = 0.0f;
    if (pedal < 0.05f)
    {
        idleErr = (Engine.IdleRPM + 30.0f) - engineRPM;
        float next = IdleServo + idleErr * 3.0e-6f;
        if (next < -Intake.IdleThrottle && idleErr < 0.0f) next = -Intake.IdleThrottle;
        else if (next > 0.12f && idleErr > 0.0f)           next = 0.12f;
        IdleServo = Clampf(next, -Intake.IdleThrottle, 0.12f);
    }
    else if (IdleServo > 0.0f) IdleServo = std::max(0.0f, IdleServo - dt * 0.5f);
    else                       IdleServo = std::min(0.0f, IdleServo + dt * 0.5f);
    const float opening = Clampf(pedal + Intake.IdleThrottle + IdleServo + idleErr * 2.0e-4f, 0.0f, 1.0f);

    const float upstream = std::max(0.2f, upstream_Bar);
    const float bore     = std::max(0.01f, Intake.ThrottleBore_mm) * 0.001f;
    const float area     = opening * 0.78539816f * bore * bore;
    const float volume   = std::max(0.5f, Intake.PlenumVolume_L) * 0.001f;
    const float veShape  = (engineRPM - Intake.VolumetricPeakRPM) / 4000.0f;
    const float ve       = std::max(0.5f * Intake.VolumetricEfficiency,
                                    Intake.VolumetricEfficiency * (1.0f - Intake.VolumetricFalloff * veShape * veShape));
    const float gulpPerDensity = ve * std::max(0.5f, Intake.Displacement_L) * 0.001f * (std::max(0.0f, engineRPM) / 120.0f);
    const float upstreamDensity = upstream * 1.0e5f / (GasConstant * AmbientT);
    // The fill time constant (~7 ms) is near the step, so sub-step the density integration.
    float density = ManifoldPressure_Bar * 1.0e5f / (GasConstant * AmbientT);
    const int   subSteps = 4;
    const float h = dt / static_cast<float>(subSteps);
    for (int s = 0; s < subSteps; ++s)
    {
        const float ratio = Clampf(density / std::max(1.0e-4f, upstreamDensity), 0.0f, 1.0f);
        const float psi = ratio <= 0.528f ? 0.685f   // choked: √γ·(2/(γ+1))^((γ+1)/(2(γ−1)))
            : std::sqrt(std::max(0.0f, (2.0f * Gamma / (Gamma - 1.0f)) *
                                 (std::pow(ratio, 2.0f / Gamma) - std::pow(ratio, (Gamma + 1.0f) / Gamma))));
        const float throttleFlow = Intake.DischargeCoefficient * area * (upstream * 1.0e5f) / std::sqrt(GasConstant * AmbientT) * psi;
        density = std::max(0.0f, density + (throttleFlow - gulpPerDensity * density) * h / volume);
    }
    ManifoldPressure_Bar = Clampf(density * GasConstant * AmbientT * 1.0e-5f, 0.05f, upstream * 1.02f);
    return Clampf(ManifoldPressure_Bar / upstream, 0.0f, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//   Torque converter (quasi-static): the pump loads the engine by the K-factor law, fading to nothing as the turbine
//   catches the pump (a fluid coupling at ν = 1 transmits nothing); the turbine torque is the pump torque times the
//   ratio, 1.9 at stall falling to 1 at the coupling point. The lockup clutch is a direct plate in parallel, slewed.
//------------------------------------------------------------------------------------------------------------------------
void Drivetrain::StepConverter(float pumpRPM, float turbineRPM, bool lockupAllowed, float dt, float& pumpTorque_Nm, float& turbineTorque_Nm) noexcept
{
    const float pump = std::max(0.0f, pumpRPM);
    const float turb = std::max(0.0f, turbineRPM);
    const float nu = Clampf(turb / std::max(pump, 1.0f), 0.0f, 1.0f);
    const float fade = Clampf((nu - 0.85f) / 0.15f, 0.0f, 1.0f);
    const float throughput = 1.0f - fade * fade * (3.0f - 2.0f * fade);
    const float K = std::max(10.0f, Clutch.ConverterKFactor);
    const float pumpTq = (pump / K) * (pump / K) * throughput;
    const float couple = Clampf((Clutch.ConverterCouplingPoint - nu) / std::max(0.05f, Clutch.ConverterCouplingPoint), 0.0f, 1.0f);
    const float ratio = 1.0f + (std::max(1.0f, Clutch.ConverterStallRatio) - 1.0f) * couple;
    const float target = lockupAllowed ? 1.0f : 0.0f;
    LockupEngagement = Clampf(LockupEngagement + Clampf(target - LockupEngagement, -Clutch.LockupRate * dt, Clutch.LockupRate * dt), 0.0f, 1.0f);
    const float capacity = Clutch.MaxTorqueCapacity * LockupEngagement;
    // 📝 The lockup plate works over twice the breakaway slip band: grabbing with a saturated plate shock-excites the
    //    driveline loop (G6 chatter); the wider band slips controllably through the grab and still holds cruise slip small.
    const float k = capacity / std::max(Clutch.BreakawaySlipRPM * kRpmToRad * 2.0f, 1e-3f);
    const float direct = Clampf((pump - turb) * kRpmToRad * k, -capacity, capacity);
    pumpTorque_Nm    = pumpTq * (1.0f - LockupEngagement) + direct;
    turbineTorque_Nm = pumpTq * ratio * (1.0f - LockupEngagement) + direct;
}

//------------------------------------------------------------------------------------------------------------------------
//   Electric drive: a permanent-magnet motor on a single-speed reduction with torque/power/overspeed envelope,
//   drivability slew and copper/iron/friction losses, against a battery (open-circuit voltage in charge, series
//   resistance, current limits that clip torque, coulomb counting, thermal derate). Recuperation takes the pedal's
//   braking demand up to the motor and pack limits and fades out near rest, where friction finishes the stop.
//------------------------------------------------------------------------------------------------------------------------
void Drivetrain::StepElectric(const DrivetrainInputs& In, float dt, DrivetrainOutputs& Out) noexcept
{
    const float direction = In.GearIndex < Trans.NeutralIndex ? -1.0f : 1.0f;
    const float motorRPM  = In.DrivenWheelRPM * Motor.ReductionRatio * direction;
    const float motorW    = motorRPM * kRpmToRad;
    const float throttle  = Clampf(In.Throttle, 0.0f, 1.0f);
    const float brake     = Clampf(In.Brake, 0.0f, 1.0f);

    const float regenGate = Clampf((In.VehicleSpeed - 0.5f) / 1.5f, 0.0f, 1.0f);
    const float regenDemand = In.RegenDemand_Nm >= 0.0f ? In.RegenDemand_Nm : brake * Motor.RegenerativeTorque_Nm;
    float demand = direction * throttle * Motor.PeakTorque_Nm - Sign(motorRPM) * regenDemand * regenGate;
    if (BatteryCharge <= 0.0f && demand * motorW > 0.0f) demand = std::min(demand, 0.0f);
    if (BatteryCharge >= 1.0f && demand * motorW < 0.0f) demand = std::max(demand, 0.0f);

    const float slewMax = std::max(1.0f, Motor.TorqueRateLimit_NmPerS) * dt;
    MotorTorqueSlew_Nm += Clampf(demand - MotorTorqueSlew_Nm, -slewMax, slewMax);
    const float heatDerate = Clampf((Battery.DerateEnd_K - BatteryTemperature_K) /
                                    std::max(1.0f, Battery.DerateEnd_K - Battery.DerateStart_K), 0.0f, 1.0f);
    float torqueCap = Motor.PeakTorque_Nm * heatDerate;
    const float powerCap = Motor.PeakPower_W * heatDerate;
    if (std::fabs(motorW) > 1.0e-3f) torqueCap = std::min(torqueCap, powerCap / std::fabs(motorW));
    if (std::fabs(motorRPM) > Motor.MaxRPM) torqueCap = 0.0f;
    float shaftTorque = Clampf(MotorTorqueSlew_Nm, -torqueCap, torqueCap);

    // Battery: terminal power → current (quadratic in R) → the current limits scale torque back.
    const float ocv = Battery.OpenCircuitEmpty_V + Clampf(BatteryCharge, 0.0f, 1.0f) * (Battery.OpenCircuitFull_V - Battery.OpenCircuitEmpty_V);
    const float R = std::max(1.0e-4f, Battery.InternalResistance_Ohm);
    const auto SolveCurrent = [&](float termP)
    {
        const float disc = std::max(0.0f, ocv * ocv - 4.0f * R * termP);
        return (ocv - std::sqrt(disc)) / (2.0f * R);
    };
    const auto LossPower = [&](float shaftTq)
    {
        return Motor.CopperLoss * shaftTq * shaftTq + Motor.IronLoss * motorW * motorW + Motor.FrictionLoss * std::fabs(motorW);
    };
    float current = SolveCurrent(shaftTorque * motorW + LossPower(shaftTorque));
    if (current > Battery.MaxDischargeCurrent_A && current > 0.0f)
    {
        shaftTorque *= Battery.MaxDischargeCurrent_A / current;
        current = SolveCurrent(shaftTorque * motorW + LossPower(shaftTorque));
    }
    else if (-current > Battery.MaxChargeCurrent_A && current < 0.0f)
    {
        shaftTorque *= Battery.MaxChargeCurrent_A / -current;
        current = SolveCurrent(shaftTorque * motorW + LossPower(shaftTorque));
    }
    const float terminalV = ocv - current * R;
    const float terminalP = terminalV * current;
    const float ampHours = std::max(1.0f, Battery.Capacity_kWh * 1000.0f / std::max(50.0f, Battery.NominalVoltage_V));
    BatteryCharge = Clampf(BatteryCharge - current * dt / (ampHours * 3600.0f), 0.0f, 1.0f);
    BatteryTemperature_K += (current * current * R - Battery.CoolingConductance_WK * (BatteryTemperature_K - Battery.AmbientTemperature_K)) * dt /
                            std::max(1000.0f, Battery.ThermalCapacity_JK);

    const float wheelTorque = shaftTorque * Motor.ReductionRatio * Motor.DrivetrainEff * direction;
    float outL = 0.0f, outR = 0.0f;
    Differentiate(wheelTorque, In.LeftWheelRPM, In.RightWheelRPM, outL, outR);
    const float regenWheel = std::min(0.0f, shaftTorque) * Motor.ReductionRatio * Motor.DrivetrainEff;
    float regenL = 0.0f, regenR = 0.0f;
    Differentiate(regenWheel, In.LeftWheelRPM, In.RightWheelRPM, regenL, regenR);

    Out.EngineRPM = motorRPM;
    Out.EngineTorque_Nm = shaftTorque;
    Out.MotorRPM = motorRPM;
    Out.MotorTorque_Nm = shaftTorque;
    Out.BatteryCharge = BatteryCharge;
    Out.BatteryVoltage_V = terminalV;
    Out.BatteryPower_W = terminalP;
    Out.ClutchTorque_Nm = shaftTorque;
    Out.ClutchLocked = true;
    Out.GearboxOutputTorque_Nm = wheelTorque;
    Out.LeftDriveTorque_Nm = outL;
    Out.RightDriveTorque_Nm = outR;
    Out.LeftRegenTorque_Nm = regenL;
    Out.RightRegenTorque_Nm = regenR;
}

//------------------------------------------------------------------------------------------------------------------------
//   Differential torque split.
//------------------------------------------------------------------------------------------------------------------------
void Drivetrain::Differentiate(float driveTorque, float leftRPM, float rightRPM, float& outL, float& outR) const noexcept
{
    const float half = driveTorque * 0.5f;
    switch (Diff.Mode)
    {
    case DifferentialMode::Open:
    default:
        outL = half; outR = half;
        break;

    case DifferentialMode::Locked:
        // Spool: torque follows the slower wheel demand — bias fully toward the wheel with grip (slower spin).
        // With no speed constraint here, approximate by equal split plus full anti-slip transfer.
        outL = half; outR = half;
        {
            const float dw = leftRPM - rightRPM;
            const float transfer = Clampf(dw / 100.0f, -1.0f, 1.0f) * std::fabs(half);
            outL -= transfer; outR += transfer;
        }
        break;

    case DifferentialMode::LimitedSlip:
    {
        // Clutch-pack LSD: transfer torque from the faster (slipping) wheel to the slower one, up to LockingFactor,
        // plus a constant preload. (Van Berkel 2014 driveline coupling.)
        const float dw = leftRPM - rightRPM;               // + → left faster
        const float bias = Clampf(dw / 200.0f, -1.0f, 1.0f) * Diff.LockingFactor * std::fabs(half)
                         + Diff.Preload_Nm * Sign(dw);
        outL = half - bias; outR = half + bias;
        break;
    }

    case DifferentialMode::TorqueVectoring:
    {
        // Active bias by command (+ → right). (Walker 2011.)
        const float b = Clampf(Diff.VectoringBias, -1.0f, 1.0f);
        outL = driveTorque * (0.5f - 0.5f * b);
        outR = driveTorque * (0.5f + 0.5f * b);
        break;
    }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//   One integration step.
//------------------------------------------------------------------------------------------------------------------------
DrivetrainOutputs Drivetrain::Step(const DrivetrainInputs& In) noexcept
{
    DrivetrainOutputs Out;
    const float dt = std::max(In.dt, 1e-5f);
    // 📝 Beyond Project Drive: without fuel there is no combustion (the throttle only opens the pumping loss); cold oil
    //    thickens the friction. Both are identities by default (fuelled, scale 1).
    const float throttle = In.Fuelled ? Clampf(In.Throttle, 0.0f, 1.0f) : 0.0f;
    FrictionScale = In.FrictionScale;
    const float engineRPM0 = EngineOmega * kRadToRpm;
    // 📝 The electric drive replaces the whole combustion pipeline (motor + battery, full outputs).
    if (Propulsion == PropulsionType::Electric)
    {
        StepElectric(In, dt, Out);
        return Out;
    }

    // 1) Forced induction (uses last-step engine rpm). Turbo spools off the exhaust; the supercharger tracks the crank
    //    instantly and taxes it with parasitic drag; NA has neither. boostMult scales the engine torque curve.
    float boostMult      = 1.0f;
    float parasiticDrag  = 0.0f;
    float reportBoostBar = 0.0f;
    float reportForcedRPM= 0.0f;
    if (Induction == InductionType::Turbocharged)
    {
        SpoolTurbo(engineRPM0, throttle, dt);
        boostMult      = Turbo.TorqueMultiplierCurve.Sample(BoostBar);
        reportBoostBar = BoostBar;
        reportForcedRPM= TurboOmegaRad * kRadToRpm;
    }
    else if (Induction == InductionType::Supercharged)
    {
        boostMult      = StepSupercharger(engineRPM0, throttle, dt, parasiticDrag);
        reportBoostBar = SuperBoostBar;
        reportForcedRPM= Super.ChargerRPM(engineRPM0);
    }
    // 📝 Beyond Project Drive: the charge air's temperature after the compressor and the intercooler; with DensityCorrection the
    //    boost's torque gain thins with warmer charge (ρ ∝ 1 / T at the same boost).
    float chargeTemperature = 0.0f;
    if (Charge.Enabled)
    {
        const float efficiency = Induction == InductionType::Supercharged ? Super.CompressorEfficiency : Turbo.CompressorEfficiency;
        chargeTemperature = Induction == InductionType::NaturallyAspirated ? Charge.AmbientTemperature_K
                                                                         : ResolveChargeTemperature(Charge, reportBoostBar, efficiency);
        if (Charge.DensityCorrection && boostMult > 1.0f && chargeTemperature > 1.0f)
            boostMult = 1.0f + (boostMult - 1.0f) * (Charge.ReferenceTemperature_K / chargeTemperature);
    }

    // 📝 With the intake on, the plenum fills here and the air ratio replaces the pedal inside NetEngineTorque
    //    (vacuum meters the air; the pumping loss follows Δp·Vd/4π). Off, airRatio is the pedal exactly.
    float airRatio = throttle;
    if (Intake.Enabled)
    {
        UpstreamPressure_Bar = Charge.AmbientPressure_Bar + (Induction == InductionType::NaturallyAspirated ? 0.0f : reportBoostBar);
        airRatio = StepIntake(UpstreamPressure_Bar, engineRPM0, throttle, dt);
    }

    // 2) Clutch: compute transmitted torque from slip between engine and gearbox input.
    const float gearRatio = Trans.RatioAt(In.GearIndex);
    const bool  inNeutral = (In.GearIndex == Trans.NeutralIndex) || (gearRatio == 0.0f);
    const float gearboxInputRPM = inNeutral ? engineRPM0
                                            : In.DrivenWheelRPM * std::fabs(gearRatio) * Trans.FinalDriveRatio;
    const float slipRPM = engineRPM0 - gearboxInputRPM;
    float clutchTorque = 0.0f;      // what reaches the gearbox (turbine torque on the converter)
    float crankLoad    = 0.0f;      // what loads the crank (both DCT plates, the pump on the converter)
    bool  locked       = false;
    float coupling01    = 0.0f;   // how rigidly the engine is coupled to the gearbox (drives the reflected inertia)
    float converterSlip = 0.0f;
    float dualA = 0.0f, dualB = 0.0f, dualPreRatio = 0.0f;
    bool  dualBlend = false;
    if (Clutch.Category == ClutchCategory::SinglePlate && In.ClutchEngagement < 0.0f && In.HandoffBlend < 0.0f)
    {
        // Capacity scales with engagement (assume fully engaged when not shifting → 1.0).
        const float engagement = inNeutral ? 0.0f : 1.0f;
        const float capacity = Clutch.MaxTorqueCapacity * engagement;
        const float k = capacity / std::max(Clutch.BreakawaySlipRPM * kRpmToRad, 1e-3f);   // Nm per (rad/s) slip
        clutchTorque = Clampf(slipRPM * kRpmToRad * k, -capacity, capacity);
        crankLoad = clutchTorque;
        locked = (engagement > Clutch.LockupEngagement) && (std::fabs(slipRPM) < Clutch.LockupSlipRPM);
        coupling01 = engagement;
    }
    else if (Clutch.Category == ClutchCategory::TorqueConverter)
    {
        float pumpTq = 0.0f, turbTq = 0.0f;
        if (!inNeutral)
        {
            StepConverter(engineRPM0, gearboxInputRPM, In.LockupAllowed, dt, pumpTq, turbTq);
            converterSlip = engineRPM0 - gearboxInputRPM;
        }
        else LockupEngagement = 0.0f;
        clutchTorque = turbTq;
        crankLoad    = pumpTq;
        locked = (LockupEngagement > Clutch.LockupEngagement) && (std::fabs(converterSlip) < Clutch.LockupSlipRPM);
        coupling01 = LockupEngagement;
    }
    else
    {
        // 📝 Staged single plate (the gearbox servo's engagement) or dual-clutch overlap. Each DCT clutch's PRESSURE
        //    ramps with its blend share (that is what the mechatronic meters); the transmitted torque is whatever each
        //    plate holds at its slip, so the engine is never double-loaded through the handoff. The offgoing plate can
        //    only push the driving way (an opening plate cannot drag the engine back).
        const float engagementCmd = Clampf(In.ClutchEngagement < 0.0f ? 1.0f : In.ClutchEngagement, 0.0f, 1.0f) * (inNeutral ? 0.0f : 1.0f);
        const float capacity = Clutch.MaxTorqueCapacity * engagementCmd;
        const float k = capacity / std::max(Clutch.BreakawaySlipRPM * kRpmToRad, 1e-3f);
        clutchTorque = Clampf(slipRPM * kRpmToRad * k, -capacity, capacity);
        crankLoad = clutchTorque;
        locked = (engagementCmd > Clutch.LockupEngagement) && (std::fabs(slipRPM) < Clutch.LockupSlipRPM);
        coupling01 = engagementCmd;
        if (Clutch.Category == ClutchCategory::DualClutch && !inNeutral && In.HandoffBlend >= 0.0f && In.HandoffBlend < 1.0f && In.PreselectGearIndex >= 0)
        {
            dualPreRatio = Trans.RatioAt(In.PreselectGearIndex);
            const float h = Clampf(In.HandoffBlend, 0.0f, 1.0f);
            const float slipA = engineRPM0 - In.DrivenWheelRPM * std::fabs(gearRatio) * Trans.FinalDriveRatio;
            const float slipB = engineRPM0 - In.DrivenWheelRPM * std::fabs(dualPreRatio) * Trans.FinalDriveRatio;
            const float driving = throttle >= 0.05f ? 1.0f : -1.0f;
            const float capA = Clutch.MaxTorqueCapacity * engagementCmd * (1.0f - h);
            const float capB = Clutch.MaxTorqueCapacity * engagementCmd * h;
            const float rawA = Clampf(slipA * kRpmToRad * capA / std::max(Clutch.BreakawaySlipRPM * kRpmToRad, 1e-3f), -capA, capA);
            dualA = driving > 0.0f ? std::max(0.0f, rawA) : std::min(0.0f, rawA);
            dualB = Clampf(slipB * kRpmToRad * capB / std::max(Clutch.BreakawaySlipRPM * kRpmToRad, 1e-3f), -capB, capB);
            clutchTorque = dualA + dualB;
            crankLoad    = dualA + dualB;
            locked       = false;
            dualBlend    = true;
        }
    }

    // 3) Engine dynamics — RK4 on ω with the clutch reaction + supercharger parasitic drag as load.
    const float clutchLoad = crankLoad + parasiticDrag;   // torque pulled off the crank (clutch + blower drag)
    auto f = [&](float w) noexcept { return NetEngineTorque(w, airRatio, boostMult, clutchLoad) * Engine.InvEngineInertia; };
    const float w0 = EngineOmega;
    const float k1 = f(w0);
    const float k2 = f(w0 + 0.5f * dt * k1);
    const float k3 = f(w0 + 0.5f * dt * k2);
    const float k4 = f(w0 + dt * k3);
    float wNew = w0 + (dt / 6.0f) * (k1 + 2.0f * k2 + 2.0f * k3 + k4);

    // Rev-rate limiting (GRIT MaxRPMIncrease / MaxRPMDecrease).
    float rpmNew = wNew * kRadToRpm;
    const float dRpmMax = Engine.MaxRPMIncrease * dt;
    const float dRpmMin = -Engine.MaxRPMDecrease * dt;
    rpmNew = engineRPM0 + Clampf(rpmNew - engineRPM0, dRpmMin, dRpmMax);
    // Idle floor + redline ceiling.
    rpmNew = Clampf(rpmNew, Engine.IdleRPM, Engine.RedlineRPM);
    EngineOmega = rpmNew * kRpmToRad;

    // 4) Transmission → gearbox output torque (the DCT mixes both plates' ratios through the handoff).
    const float gearboxOutTorque = inNeutral ? 0.0f
        : dualBlend ? (dualA * gearRatio + dualB * dualPreRatio) * Trans.FinalDriveRatio * Trans.DriveEfficiency
                    : clutchTorque * gearRatio * Trans.FinalDriveRatio * Trans.DriveEfficiency;

    // 5) Differential split.
    float outL = 0.0f, outR = 0.0f;
    Differentiate(gearboxOutTorque, In.LeftWheelRPM, In.RightWheelRPM, outL, outR);

    Out.EngineRPM = rpmNew;
    Out.EngineTorque_Nm = NetEngineTorque(EngineOmega, airRatio, boostMult, 0.0f);
    Out.TurboRPM = reportForcedRPM;
    Out.BoostPressure_Bar = reportBoostBar;
    Out.BoostMultiplier = boostMult;
    Out.ParasiticDrag_Nm = parasiticDrag;
    Out.ClutchTorque_Nm = clutchTorque;
    Out.ClutchLocked = locked;
    Out.ClutchEngagement = coupling01;
    Out.GearboxOutputTorque_Nm = gearboxOutTorque;
    Out.LeftDriveTorque_Nm = outL;
    Out.RightDriveTorque_Nm = outR;
    Out.ChargeTemperature_K = chargeTemperature;
    Out.ManifoldPressure_Bar = Intake.Enabled ? ManifoldPressure_Bar : 0.0f;
    Out.ConverterSlipRPM = Clutch.Category == ClutchCategory::TorqueConverter ? converterSlip : 0.0f;
    return Out;
}

} // namespace Frontier::Vehicle
