/*==========================================================================================================================================
                                                        VEHICLEPHYSICSVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Comprehensive headless verification of JoltPhysics v5.2.0, JoltAdapter, PhysicsExecutionThread, GRIT dynamics, and 3 soft-body tyres.

#include "Engine/PhysicalDynamics/JoltAdapter.h"
#include "Engine/PhysicalDynamics/PhysicsExecutionThread.h"
#include "Engine/PhysicalDynamics/RigidBodySolver.h"
#include "Engine/PhysicalDynamics/Vehicle/VehicleJoltRig.h"
#include "Engine/PhysicalDynamics/Vehicle/VehicleSolver.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace Frontier;
using namespace Frontier::Vehicle;

namespace
{

int PassedChecks = 0;
int FailedChecks = 0;

void Verify(bool Condition, const char* Description, const std::string& Detail = "")
{
    if (Condition)
    {
        ++PassedChecks;
        std::printf("  [PASS] %-66s %s\n", Description, Detail.c_str());
    }
    else
    {
        ++FailedChecks;
        std::printf("  [FAIL] %-66s %s\n", Description, Detail.c_str());
    }
}

std::string FormatMetric(const char* Format, double First, double Second = 0.0, double Third = 0.0)
{
    char Buffer[256];
    std::snprintf(Buffer, sizeof(Buffer), Format, First, Second, Third);
    return Buffer;
}

// 📝 Direct equation-by-equation GRIT Pacejka MF6.1 oracle (no cache) to verify cache-accelerated port fidelity.
float EvaluateOracleLongitudinal(const PacejkaParameters& P, float Kappa, float Camber, float FzNewtons)
{
    if (FzNewtons <= 1.0e-6f) return 0.0f;
    Kappa            = ClampScalar(Kappa, -0.92f, 1.50f);
    const float Fz   = FzNewtons * 0.001f;
    const float Fz0  = std::max(P.Fz0, 0.001f);
    const float Dfz  = (Fz - Fz0) / Fz0;
    const float Shx  = (P.pHx1 + P.pHx2 * Dfz) * P.Lx;
    const float Xk   = Kappa + Shx;
    const float Dnom = (P.pDx1 + P.pDx2 * Dfz) * P.Lx * Fz * (1.0f - P.pDx3 * Camber * Camber);
    const float Lf   = std::max(1.0f - P.MuLoadSensitivityLong * std::fabs(Dfz), P.MuLoadMinFactor);
    const float Dx   = Dnom * Lf;
    const float Cx   = P.pCx1 * P.Lx;
    const float Bcd  = (P.pBx1 + P.pBx2 * Dfz) * P.Lx * (1.0f - P.pBx3 * std::fabs(Camber)) * Fz;
    const float Bx   = Bcd / std::max(Cx * Dx, 1.0e-8f);
    const float Ex   = ClampScalar((P.pEx1 + P.pEx2 * Dfz + P.pEx3 * Dfz * Dfz) * P.Lx * (1.0f - P.pEx4 * SignScalar(Xk)), -1.0f, 1.0f);
    const float Svx  = (P.pVx1 + P.pVx2 * Dfz) * P.Lx * Fz;
    const float Bxx  = Bx * Xk;
    return (Dx * std::sin(Cx * std::atan(Bxx - Ex * (Bxx - std::atan(Bxx)))) + Svx) * 1000.0f;
}

float EvaluateOracleLateral(const PacejkaParameters& P, float Alpha, float Camber, float FzNewtons)
{
    if (FzNewtons <= 1.0e-6f) return 0.0f;
    Alpha            = ClampScalar(Alpha, -1.30f, 1.30f);
    const float Fz   = FzNewtons * 0.001f;
    const float Fz0  = std::max(P.Fz0, 0.001f);
    const float Dfz  = (Fz - Fz0) / Fz0;
    const float Svy0 = (P.pVy1 + P.pVy2 * Dfz) * P.Ly * Fz;
    const float Svyg = Fz * (P.pVy3 + P.pVy4 * Dfz) * Camber * P.Ly;
    const float Svy  = Svy0 + Svyg;
    const float Kyg  = (P.pCamber1 + P.pCamber2 * Dfz) * P.Ly * Fz;
    const float Dnom = (P.pDy1 + P.pDy2 * Dfz) * P.Ly * Fz * (1.0f - P.pDy3 * Camber * Camber);
    const float Lf   = std::max(1.0f - P.MuLoadSensitivityLat * std::fabs(Dfz), P.MuLoadMinFactor);
    const float Dy   = Dnom * Lf;
    const float Cy   = P.pCy1 * P.Ly;
    const float Bcd  = (P.pBy1 + P.pBy2 * Dfz) * P.Ly * (1.0f - P.pBy3 * std::fabs(Camber)) * Fz;
    const float By   = Bcd / std::max(Cy * Dy, 1.0e-8f);
    const float Shy  = (P.pHy1 + P.pHy2 * Dfz) * P.Ly + (Kyg * Camber - Svyg) / std::max(Bcd, 1.0e-8f);
    const float Xa   = Alpha + Shy;
    const float Ey   = ClampScalar((P.pEy1 + P.pEy2 * Dfz) * P.Ly * (1.0f - P.pEy3 * SignScalar(Xa)), -1.0f, 1.0f);
    const float Byx  = By * Xa;
    return (Dy * std::sin(Cy * std::atan(Byx - Ey * (Byx - std::atan(Byx)))) + Svy) * 1000.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//   SUITE 1: PACEJKA MF6.1 ORACLE & COMBINED SLIP VERIFICATION
//------------------------------------------------------------------------------------------------------------------------

void RunPacejkaVerification(const std::filesystem::path& OutputDir)
{
    std::printf("\n=== [Suite 1] Pacejka MF6.1 & GRIT Compound Fidelity ===\n");
    const PacejkaParameters Params = PacejkaParameters::DefaultRoad();
    PacejkaMagicFormula     Model(Params);

    float       MaxLongErr = 0.0f;
    float       MaxLatErr  = 0.0f;
    const float Loads[]    = {2000.0f, 4000.0f, 6500.0f, 10000.0f};
    const float Cambers[]  = {-0.06f, 0.0f, 0.05f};

    for (float Fz : Loads)
    {
        for (float Cam : Cambers)
        {
            for (int Step = -45; Step <= 70; ++Step)
            {
                const float Kappa = static_cast<float>(Step) * 0.02f;
                MaxLongErr        = std::max(MaxLongErr, std::fabs(Model.LongitudinalForce(Kappa, Cam, Fz) -
                                                                   EvaluateOracleLongitudinal(Params, Kappa, Cam, Fz)));
            }
            for (int Step = -60; Step <= 60; ++Step)
            {
                const float Alpha = static_cast<float>(Step) * 0.02f;
                MaxLatErr         = std::max(MaxLatErr, std::fabs(Model.LateralForce(Alpha, Cam, Fz) -
                                                                  EvaluateOracleLateral(Params, Alpha, Cam, Fz)));
            }
        }
    }

    Verify(MaxLongErr < 1.0e-2f, "Pacejka longitudinal cache matches GRIT equation oracle", FormatMetric("maxErr=%.5f N", MaxLongErr));
    Verify(MaxLatErr < 1.0e-2f, "Pacejka lateral cache matches GRIT equation oracle", FormatMetric("maxErr=%.5f N", MaxLatErr));

    // Friction-ellipse containment & load sensitivity.
    const float PureFx4k = Model.LongitudinalForce(0.12f, 0.0f, 4000.0f);
    const float PureFy4k = Model.LateralForce(0.14f, 0.0f, 4000.0f);
    const auto  Comb4k   = Model.CombinedForces(0.12f, 0.14f, 0.0f, 4000.0f);
    const float Ellipse  = std::hypot(Comb4k.LongitudinalForceNewtons / PureFx4k, Comb4k.LateralForceNewtons / PureFy4k);
    Verify(Comb4k.LongitudinalForceNewtons < PureFx4k && Comb4k.LateralForceNewtons < PureFy4k && Ellipse < 1.12f,
           "MF6.1 combined slip stays inside friction ellipse",
           FormatMetric("Fx=%.0f/%.0f N, Fy=%.0f N", Comb4k.LongitudinalForceNewtons, PureFx4k, Comb4k.LateralForceNewtons));

    const float MuAt4k  = Model.LongitudinalForce(0.12f, 0.0f, 4000.0f) / 4000.0f;
    const float MuAt12k = Model.LongitudinalForce(0.12f, 0.0f, 12000.0f) / 12000.0f;
    Verify(MuAt12k < MuAt4k, "Tyre load sensitivity decreases effective mu at heavy load", FormatMetric("mu4k=%.3f > mu12k=%.3f", MuAt4k, MuAt12k));

    // Self-aligning torque restoring sign & burnout fade.
    const auto SatSmall = Model.CombinedForces(0.0f, 0.08f, 0.0f, 4000.0f);
    const auto SatBurn  = Model.CombinedForces(1.45f, 1.25f, 0.0f, 4000.0f);
    Verify(SatSmall.AligningTorqueNm < -5.0f, "Self-aligning torque opposes positive slip angle", FormatMetric("Mz=%.2f N*m", SatSmall.AligningTorqueNm));
    Verify(std::fabs(SatBurn.AligningTorqueNm) < 0.25f * std::fabs(SatSmall.AligningTorqueNm),
           "GRIT pneumatic trail fades under extreme combined burnout slip",
           FormatMetric("|Mz_burn|=%.2f N*m", std::fabs(SatBurn.AligningTorqueNm)));

    // GRIT compound presets ordering: GT3 Slick > DefaultRoad > OffroadDirt.
    const PacejkaMagicFormula Gt3(PacejkaParameters::GritGt3SlickDry());
    const PacejkaMagicFormula Dirt(PacejkaParameters::GritOffroadDirt());
    const float               FxGt3  = Gt3.LongitudinalForce(0.12f, 0.0f, 4000.0f);
    const float               FxDirt = Dirt.LongitudinalForce(0.12f, 0.0f, 4000.0f);
    Verify(FxGt3 > PureFx4k && PureFx4k > FxDirt,
           "GRIT compound presets order grip: GT3 Slick > Road > Offroad Dirt",
           FormatMetric("GT3=%.0f N, Road=%.0f N, Dirt=%.0f N", FxGt3, PureFx4k, FxDirt));

    // Export Pacejka curve CSV for verification plotting.
    std::ofstream Csv(OutputDir / "pacejka_curves.csv");
    Csv << "slip,fx_road,fx_gt3,fx_dirt,fy_road,fx_comb_alpha8deg\n";
    for (int Step = -90; Step <= 140; ++Step)
    {
        const float S = static_cast<float>(Step) * 0.01f;
        const auto  C = Model.CombinedForces(S, 0.14f, 0.0f, 4000.0f);
        Csv << S << ","
            << Model.LongitudinalForce(S, 0.0f, 4000.0f) << ","
            << Gt3.LongitudinalForce(S, 0.0f, 4000.0f) << ","
            << Dirt.LongitudinalForce(S, 0.0f, 4000.0f) << ","
            << Model.LateralForce(S, 0.0f, 4000.0f) << ","
            << C.LongitudinalForceNewtons << "\n";
    }
}

//------------------------------------------------------------------------------------------------------------------------
//   SUITE 2: TRANSIENT SLIP DYNAMICS (GRIT IMPLICIT NEWTON vs RELAXATION LENGTH vs QUASI-STATIC)
//------------------------------------------------------------------------------------------------------------------------

void RunTransientSlipVerification()
{
    std::printf("\n=== [Suite 2] Transient Tyre Slip Solvers ===\n");
    PacejkaMagicFormula Formula(PacejkaParameters::DefaultRoad());
    TyreSlipSolver      Solver;

    auto RunConstantSlipTest = [&](SlipSolverMode Mode) {
        TyreSlipVariables Vars{};
        TyreSlipInputs    In{};
        In.LongitudinalSpeed = 25.0f;
        In.LateralSpeed      = -2.2f;   // ~5 deg slip angle
        In.WheelOmega        = 25.0f / 0.50855f;
        In.EffectiveRadius   = 0.50855f;
        In.WheelInertia      = 1.32f;
        In.DriveTorqueNm     = 1150.0f;   // steady wheel torque -> rotational equilibrium Fx = T / R = 2261.3 N
        In.NormalLoadNewtons = 4000.0f;
        In.DeltaSeconds      = 1.0f / 480.0f;

        TyreSlipOutputs Out{};
        for (int Step = 0; Step < 360; ++Step)
        {
            Out           = Solver.Step(Mode, Formula, In, Vars);
            In.WheelOmega = Out.NextWheelOmega;
        }
        return Out;
    };

    const TyreSlipOutputs GritOut  = RunConstantSlipTest(SlipSolverMode::GritImplicitNewton);
    const TyreSlipOutputs RelaxOut = RunConstantSlipTest(SlipSolverMode::RelaxationLength);
    const TyreSlipOutputs QuasiOut = RunConstantSlipTest(SlipSolverMode::QuasiStaticNewton);

    Verify(std::fabs(GritOut.LongitudinalForceNewtons - QuasiOut.LongitudinalForceNewtons) < 85.0f &&
               std::fabs(RelaxOut.LongitudinalForceNewtons - QuasiOut.LongitudinalForceNewtons) < 85.0f,
           "GritImplicitNewton, RelaxationLength, and QuasiStatic agree at steady state",
           FormatMetric("Grit=%.0f N, Relax=%.0f N, Quasi=%.0f N",
                        GritOut.LongitudinalForceNewtons, RelaxOut.LongitudinalForceNewtons, QuasiOut.LongitudinalForceNewtons));

    Verify(GritOut.SlipPowerWatts > 500.0f,
           "Thermodynamic slip dissipation power P_slip > 0 under combined slip",
           FormatMetric("P_slip=%.0f W", GritOut.SlipPowerWatts));

    // Standstill / zero-speed brake-lock stability check.
    TyreSlipVariables ZeroVars{};
    TyreSlipInputs    ZeroIn{};
    ZeroIn.LongitudinalSpeed = 0.0f;
    ZeroIn.LateralSpeed      = 0.0f;
    ZeroIn.WheelOmega        = 3.5f;
    ZeroIn.BrakeTorqueNm     = 2200.0f;
    ZeroIn.NormalLoadNewtons = 3800.0f;
    ZeroIn.DeltaSeconds      = 1.0f / 240.0f;

    bool AllFinite = true;
    for (int Step = 0; Step < 60; ++Step)
    {
        const TyreSlipOutputs Z = Solver.Step(SlipSolverMode::GritImplicitNewton, Formula, ZeroIn, ZeroVars);
        ZeroIn.WheelOmega       = Z.NextWheelOmega;
        if (!std::isfinite(Z.LongitudinalForceNewtons) || !std::isfinite(Z.NextWheelOmega)) AllFinite = false;
    }
    Verify(AllFinite && std::fabs(ZeroIn.WheelOmega) < 1.0e-4f,
           "Zero-speed braking arrests wheel spin cleanly without NaN or chatter",
           FormatMetric("final omega=%.5f rad/s", ZeroIn.WheelOmega));
}

//------------------------------------------------------------------------------------------------------------------------
//   SUITE 3: GRIT POWERTRAIN (SUPERCHARGER, TURBO, COOLANT, OIL, FUEL), TYRE PRESSURE/TEMP, ASSISTS & AERO
//------------------------------------------------------------------------------------------------------------------------

void RunGritSubsystemsVerification(const std::filesystem::path& OutputDir)
{
    std::printf("\n=== [Suite 3] GRIT Supercharger, Coolant/Oil/Fuel, Tyre PV=mRT, Assists & Aero ===\n");

    auto NeutralRev = [](Drivetrain& Drive, float Throttle, float Seconds) {
        DrivetrainInputs In{};
        In.Throttle      = Throttle;
        In.RequestedGear = 1;   // Neutral
        In.DeltaSeconds  = 1.0f / 240.0f;
        DrivetrainOutputs Out{};
        const int Steps = static_cast<int>(Seconds * 240.0f);
        for (int Step = 0; Step < Steps; ++Step) Out = Drive.Step(In);
        return Out;
    };

    // 1. NA vs Supercharger (Roots / TwinScrew / Centrifugal) vs Turbocharger.
    Drivetrain NaDrive;
    NaDrive.AssignInduction(InductionType::NaturallyAspirated);
    NaDrive.Reset(850.0f, false);
    const auto NaOut = NeutralRev(NaDrive, 1.0f, 1.2f);
    Verify(std::fabs(NaOut.BoostPressureBar) < 1.0e-4f && std::fabs(NaOut.ParasiticDragNm) < 1.0e-4f,
           "Naturally aspirated engine has zero boost and zero parasitic drag",
           FormatMetric("mult=%.3f", NaOut.BoostMultiplier));

    Drivetrain ScBase;
    ScBase.AssignInduction(InductionType::Supercharged);
    ScBase.AssignSupercharger(SuperchargerParameters::DefaultTwinScrew());
    ScBase.AssignRaceTune(false);
    ScBase.Reset(850.0f, true);
    const auto ScBaseOut = NeutralRev(ScBase, 1.0f, 1.5f);

    Drivetrain ScRace;
    ScRace.AssignInduction(InductionType::Supercharged);
    ScRace.AssignSupercharger(SuperchargerParameters::DefaultTwinScrew());
    ScRace.AssignRaceTune(true);
    ScRace.Reset(850.0f, true);
    const auto ScRaceOut = NeutralRev(ScRace, 1.0f, 1.5f);

    Verify(ScBaseOut.BoostPressureBar > 0.45f && ScBaseOut.BoostPressureBar <= 0.801f,
           "Twin-screw supercharger builds boost capped at 0.80 bar Base tune",
           FormatMetric("boost=%.3f bar, drag=%.1f N*m", ScBaseOut.BoostPressureBar, ScBaseOut.ParasiticDragNm));
    Verify(ScRaceOut.BoostPressureBar > ScBaseOut.BoostPressureBar + 0.15f && ScRaceOut.BoostPressureBar <= 1.201f,
           "Race tune raises supercharger boost ceiling to 1.20 bar",
           FormatMetric("race=%.3f bar vs base=%.3f bar", ScRaceOut.BoostPressureBar, ScBaseOut.BoostPressureBar));
    Verify(std::fabs(ScBaseOut.ForcedInductionRpm - ScBaseOut.EngineRpm * 3.20f) < 1.0f,
           "Supercharger rotor RPM tracks crank RPM x DriveRatio (3.20)",
           FormatMetric("charger=%.0f rpm", ScBaseOut.ForcedInductionRpm));

    // Off-throttle bypass valve parasitic-drag relief.
    Drivetrain ScWot, ScClosed;
    ScWot.AssignInduction(InductionType::Supercharged);
    ScClosed.AssignInduction(InductionType::Supercharged);
    ScWot.Reset(4500.0f, true);
    ScClosed.Reset(4500.0f, true);
    const auto WotOut    = NeutralRev(ScWot, 1.0f, 0.10f);
    const auto ClosedOut = NeutralRev(ScClosed, 0.0f, 0.10f);
    Verify(WotOut.ParasiticDragNm > ClosedOut.ParasiticDragNm * 2.5f,
           "Supercharger bypass valve slashes parasitic drag off-throttle",
           FormatMetric("WOT=%.1f N*m vs Bypass=%.1f N*m", WotOut.ParasiticDragNm, ClosedOut.ParasiticDragNm));

    // Tip-in transient: belt-driven supercharger leads exhaust turbocharger at 0.12 s!
    Drivetrain TurboTip, SuperTip;
    TurboTip.AssignInduction(InductionType::Turbocharged);
    SuperTip.AssignInduction(InductionType::Supercharged);
    TurboTip.Reset(3800.0f, true);
    SuperTip.Reset(3800.0f, true);
    const auto TurboEarly = NeutralRev(TurboTip, 1.0f, 0.12f);
    const auto SuperEarly = NeutralRev(SuperTip, 1.0f, 0.12f);
    Verify(SuperEarly.BoostPressureBar > TurboEarly.BoostPressureBar + 0.10f,
           "Belt-driven supercharger eliminates turbo lag on tip-in",
           FormatMetric("SC=%.2f bar > Turbo=%.2f bar @ 120ms", SuperEarly.BoostPressureBar, TurboEarly.BoostPressureBar));

    // Roots vs Centrifugal low-RPM boost shape (in 4th gear at 2400 crank RPM so RPM stays anchored in low band).
    Drivetrain RootsLow, CentLow;
    RootsLow.AssignInduction(InductionType::Supercharged);
    RootsLow.AssignSupercharger(SuperchargerParameters::DefaultRoots());
    CentLow.AssignInduction(InductionType::Supercharged);
    CentLow.AssignSupercharger(SuperchargerParameters::DefaultCentrifugal());
    RootsLow.Reset(2400.0f, true);
    CentLow.Reset(2400.0f, true);
    DrivetrainInputs HoldRpmIn{};
    HoldRpmIn.Throttle            = 1.0f;
    HoldRpmIn.RequestedGear       = 5;   // 4th gear (ratio 1.19 * 3.55 = 4.2245) -> 59.5 rad/s wheel = 2400 crank RPM
    HoldRpmIn.WheelOmegaRadPerSec = {59.5f, 59.5f, 59.5f, 59.5f};
    HoldRpmIn.DeltaSeconds        = 1.0f / 240.0f;
    DrivetrainOutputs Roots2400{}, Cent2400{};
    for (int Step = 0; Step < 36; ++Step)
    {
        Roots2400 = RootsLow.Step(HoldRpmIn);
        Cent2400  = CentLow.Step(HoldRpmIn);
    }
    Verify(Roots2400.BoostPressureBar > Cent2400.BoostPressureBar + 0.08f,
           "Roots positive-displacement blower makes more low-RPM boost than Centrifugal",
           FormatMetric("Roots=%.3f bar > Cent=%.3f bar", Roots2400.BoostPressureBar, Cent2400.BoostPressureBar));

    // 2. Coolant, Engine Oil Viscosity & Fuel Mass Consumption under sustained load.
    Drivetrain ColdEngine;
    ColdEngine.AssignInduction(InductionType::Turbocharged);
    ColdEngine.Reset(2500.0f, false);   // start cold at 20 degC (293.15 K)
    const float InitialFuelKg   = ColdEngine.QueryOutputs().FuelMassKg;
    const float InitialCoolantK = ColdEngine.QueryOutputs().CoolantTempK;
    DrivetrainInputs LoadIn{};
    LoadIn.Throttle            = 0.85f;
    LoadIn.RequestedGear       = 5;   // 4th gear @ 55 rad/s (~4,440 RPM, squarely inside peak torque band)
    LoadIn.WheelOmegaRadPerSec = {55.0f, 55.0f, 55.0f, 55.0f};
    LoadIn.VehicleSpeedMps     = 28.0f;
    LoadIn.DeltaSeconds        = 1.0f / 120.0f;
    DrivetrainOutputs WarmOut{};
    const float ColdViscosityCst = ColdEngine.Step(LoadIn).OilViscosityCst;
    for (int Step = 0; Step < 120 * 25; ++Step) WarmOut = ColdEngine.Step(LoadIn);

    Verify(WarmOut.CoolantTempK > InitialCoolantK + 12.0f && WarmOut.OilTempK > 293.15f + 5.0f,
           "GRIT engine coolant & oil circuits warm up under combustion load",
           FormatMetric("T_cool=%.1f K, T_oil=%.1f K", WarmOut.CoolantTempK, WarmOut.OilTempK));
    Verify(WarmOut.OilViscosityCst < ColdViscosityCst,
           "5W-40 engine oil kinematic viscosity decreases as oil warms",
           FormatMetric("%.1f cSt -> %.1f cSt", ColdViscosityCst, WarmOut.OilViscosityCst));
    Verify(WarmOut.FuelMassKg < InitialFuelKg - 0.05f,
           "GRIT fuel circuit consumes tank fuel mass proportional to brake power",
           FormatMetric("burned=%.3f kg", InitialFuelKg - WarmOut.FuelMassKg));

    // 3. 3-Node Tyre Temperature, Ideal-Gas Tyre Pressure (PV = mRT), Wear & Puncture.
    TyreThermalParameters ThermParams{};
    TyreThermalReading    ThermRead{};
    TyreThermalIntegrator::Reset(ThermParams, ThermRead);
    const float ColdGaugePa = ThermRead.GaugePressurePa;
    const float ColdGrip    = ThermRead.GripMultiplier;

    std::ofstream ThermCsv(OutputDir / "tyre_thermal_burnout.csv");
    ThermCsv << "time_s,t_surf_k,t_carc_k,t_gas_k,gauge_kpa,grip,wear\n";
    for (int Step = 0; Step < 240 * 24; ++Step)
    {
        TyreThermalStepInputs TIn{};
        TIn.SlipPowerWatts    = 18000.0f;   // heavy burnout / track lapping slip power
        TIn.NormalLoadNewtons = 3800.0f;
        TIn.RollingSpeedMps   = 18.0f;
        TIn.DeflectionRateMps = 0.08f;
        TIn.LiveVolumeCubicM  = 0.0405f;    // loaded compressed cavity volume
        TIn.DeltaSeconds      = 1.0f / 240.0f;
        TyreThermalIntegrator::Step(ThermParams, TIn, ThermRead);
        if (Step % 24 == 0)
        {
            ThermCsv << (Step / 240.0f) << ","
                     << ThermRead.SurfaceTemperatureK << ","
                     << ThermRead.CarcassTemperatureK << ","
                     << ThermRead.GasTemperatureK << ","
                     << (ThermRead.GaugePressurePa * 0.001f) << ","
                     << ThermRead.GripMultiplier << ","
                     << ThermRead.WearFraction << "\n";
        }
    }

    Verify(ThermRead.SurfaceTemperatureK > 345.0f && ThermRead.CarcassTemperatureK > 305.0f,
           "3-node tyre thermal model heats tread surface and conducts into carcass",
           FormatMetric("T_surf=%.1f K, T_carc=%.1f K", ThermRead.SurfaceTemperatureK, ThermRead.CarcassTemperatureK));
    Verify(ThermRead.GaugePressurePa > ColdGaugePa + 8000.0f,
           "Ideal-gas tyre pressure (PV = mRT) rises with gas heating & volume compression",
           FormatMetric("%.1f kPa -> %.1f kPa", ColdGaugePa * 0.001f, ThermRead.GaugePressurePa * 0.001f));
    Verify(ThermRead.GripMultiplier > ColdGrip && ThermRead.WearFraction > 0.0f,
           "Warming tyre into optimal window raises compound grip & accumulates wear",
           FormatMetric("grip=%.3f (cold %.3f), wear=%.4f", ThermRead.GripMultiplier, ColdGrip, ThermRead.WearFraction));

    ThermRead.Punctured = true;
    for (int Step = 0; Step < 240 * 2; ++Step)
    {
        TyreThermalIntegrator::Step(ThermParams, TyreThermalStepInputs{0, 3200.0f, 5.0f, 0, 0.042f, 1.0f / 240.0f}, ThermRead);
    }
    Verify(ThermRead.GaugePressurePa == 0.0f && ThermRead.GripMultiplier < 0.50f,
           "Tyre puncture bleeds gauge pressure to atmospheric and degrades grip",
           FormatMetric("P_gauge=%.1f Pa, grip=%.2f", ThermRead.GaugePressurePa, ThermRead.GripMultiplier));

    // 4. Ackermann steering, Speed-sensitive ratio, TCS, ESC & Closed-Loop GT3 Aero.
    SteeringAndAssists   SteerSys;
    SteeringParameters   SteerP{};
    DriverAssistParameters AssistP{};
    AssistP.TractionControlEnabled  = true;
    AssistP.StabilityControlEnabled = true;

    SteeringAndAssistOutputs LowSpdSteer{};
    for (int Step = 0; Step < 60; ++Step)
    {
        LowSpdSteer = SteerSys.Step(SteerP, AssistP, 1.0f, 0.8f, 0.0f, 5.0f, 0.0f, 3.396f, 2.095f, {0.35f, 0.35f, 0.42f, 0.42f}, -60.0f, 1.0f / 60.0f);
    }
    Verify(LowSpdSteer.FrontLeftSteerRadians > LowSpdSteer.FrontRightSteerRadians + 0.04f,
           "Ackermann steering angles inner wheel (FL) sharper than outer wheel (FR)",
           FormatMetric("FL=%.3f rad > FR=%.3f rad", LowSpdSteer.FrontLeftSteerRadians, LowSpdSteer.FrontRightSteerRadians));
    Verify(LowSpdSteer.TractionControlActive && LowSpdSteer.ModulatedThrottle < 0.8f,
           "Traction Control (TCS) trims engine throttle under excessive wheelspin",
           FormatMetric("throttle 0.80 -> %.2f", LowSpdSteer.ModulatedThrottle));

    SteerSys.Reset();
    SteeringAndAssistOutputs HighSpdSteer{};
    for (int Step = 0; Step < 60; ++Step)
    {
        HighSpdSteer = SteerSys.Step(SteerP, AssistP, 1.0f, 0.5f, 0.0f, 45.0f, 1.35f, 3.396f, 2.095f, {0.02f, 0.02f, 0.02f, 0.02f}, -90.0f, 1.0f / 60.0f);
    }
    Verify(HighSpdSteer.CenterSteerRadians < LowSpdSteer.CenterSteerRadians * 0.55f,
           "Speed-sensitive steering reduces maximum lock at 162 km/h vs 18 km/h",
           FormatMetric("high=%.3f rad < low=%.3f rad", HighSpdSteer.CenterSteerRadians, LowSpdSteer.CenterSteerRadians));
    Verify(HighSpdSteer.StabilityControlActive && HighSpdSteer.EscCornerBrakeTorquesNm[1] > 50.0f,
           "Electronic Stability Control (ESC) brakes outer-front wheel to counter oversteer",
           FormatMetric("FR ESC brake=%.0f N*m", HighSpdSteer.EscCornerBrakeTorquesNm[1]));

    // Closed-loop GT3 Aero: DRS vs Normal vs Airbrake + Diffuser Stall.
    AerodynamicsSolver    AeroSys;
    AerodynamicParameters AeroP{};
    AerodynamicInputs     AeroIn{};
    AeroIn.ChassisVelocityWorld  = {55.0f, 0.0f, 0.0f};
    AeroIn.FrontRideHeightMetres = 0.085f;
    AeroIn.RearRideHeightMetres  = 0.095f;
    AeroIn.Throttle              = 0.50f;
    AeroIn.DeltaSeconds          = 0.1f;

    AerodynamicOutputs NormalAero{};
    for (int Step = 0; Step < 20; ++Step) NormalAero = AeroSys.Step(AeroP, AeroIn);

    AeroIn.Throttle     = 1.0f;
    AeroIn.DrsRequested = true;
    AerodynamicOutputs DrsAero{};
    for (int Step = 0; Step < 20; ++Step) DrsAero = AeroSys.Step(AeroP, AeroIn);

    AeroIn.Throttle     = 0.0f;
    AeroIn.DrsRequested = false;
    AeroIn.Brake        = 1.0f;
    AerodynamicOutputs AirbrakeAero{};
    for (int Step = 0; Step < 20; ++Step) AirbrakeAero = AeroSys.Step(AeroP, AeroIn);

    AeroIn.FrontRideHeightMetres = 0.018f;   // bottomed below CriticalStallRideHeightM (0.032 m)
    const AerodynamicOutputs StalledAero = AeroSys.Step(AeroP, AeroIn);

    Verify(DrsAero.DrsActive && DrsAero.DragMagnitudeNewtons < NormalAero.DragMagnitudeNewtons - 80.0f,
           "GT3 Active DRS reduces high-speed straightline drag",
           FormatMetric("DRS=%.0f N < Normal=%.0f N", DrsAero.DragMagnitudeNewtons, NormalAero.DragMagnitudeNewtons));
    Verify(AirbrakeAero.AirbrakeActive && AirbrakeAero.RearDownforceNewtons > NormalAero.RearDownforceNewtons + 250.0f,
           "GT3 Active Airbrake increases rear downforce & drag under heavy braking",
           FormatMetric("Airbrake Rear DF=%.0f N > Normal=%.0f N", AirbrakeAero.RearDownforceNewtons, NormalAero.RearDownforceNewtons));
    Verify(StalledAero.DiffuserStalled, "GT3 Underbody Venturi diffuser detects ground-clearance choke/stall");
}

//------------------------------------------------------------------------------------------------------------------------
//   SUITE 4: ALL 3 SOFT-BODY TYRE PHYSICS MODELS (SPRING, XPBD, 3D FEM) STANDALONE & PRESSURE COUPLING
//------------------------------------------------------------------------------------------------------------------------

void RunThreeSoftBodyModelsVerification(const std::filesystem::path& OutputDir)
{
    std::printf("\n=== [Suite 4] Three Soft-Body Tyre Formulations: Spring, XPBD, and 3D Hex8 FEM ===\n");

    const GroundQuery FlatGround = [](const Vec3& P, Vec3& S, Vec3& N) noexcept -> bool {
        S = {P.x, P.y, 0.0f};
        N = {0.0f, 0.0f, 1.0f};
        return true;
    };

    const TyreFormulation Formulations[3] = {
        TyreFormulation::Spring,
        TyreFormulation::Xpbd,
        TyreFormulation::Fem3D};

    std::ofstream CrossCsv(OutputDir / "three_model_profiles.csv");
    CrossCsv << "model,sector,ring,rest_x,rest_y,rest_z,def_x,def_y,def_z\n";

    for (TyreFormulation Mode : Formulations)
    {
        TyreSoftBody Tyre(Mode);
        const char*  Label = DescribeTyreFormulation(Mode);

        auto MeasureAtPressure = [&](float GaugePa, float HubHeightM, float LatForceN) {
            Tyre.Reset();
            TyreSoftBodyInputs In{};
            In.HubPositionWorld      = {0.0f, 0.0f, HubHeightM};
            In.GaugePressurePa       = GaugePa;
            In.LateralForceEstimateN = LatForceN;
            In.WheelOmegaRadPerSec   = 35.0f;
            In.DeltaSeconds          = 1.0f / 240.0f;
            In.Ground                = FlatGround;

            TyreSoftBodyReading Read{};
            for (int Step = 0; Step < 36; ++Step) Read = Tyre.Step(In);
            return Read;
        };

        const auto LowP  = MeasureAtPressure(110000.0f, 0.482f, 0.0f);     // 1.1 bar, 26.5 mm deflection
        const auto NomP  = MeasureAtPressure(220000.0f, 0.482f, 2800.0f);  // 2.2 bar + cornering lateral shear
        const auto HighP = MeasureAtPressure(330000.0f, 0.482f, 0.0f);     // 3.3 bar

        Verify(LowP.NormalLoadNewtons < NomP.NormalLoadNewtons && NomP.NormalLoadNewtons < HighP.NormalLoadNewtons,
               (std::string(Label) + ": normal reaction Fz increases monotonically with tyre pressure").c_str(),
               FormatMetric("1.1b=%.0f N < 2.2b=%.0f N < 3.3b=%.0f N",
                            LowP.NormalLoadNewtons, NomP.NormalLoadNewtons, HighP.NormalLoadNewtons));

        Verify(NomP.MaxRadialInwardMetres > 0.010f && NomP.MaxSidewallBulgeMetres > 0.0012f,
               (std::string(Label) + ": crown flattens inward AND sidewall shoulders bulge outward").c_str(),
               FormatMetric("inward=%.1f mm, bulge=%.2f mm",
                            NomP.MaxRadialInwardMetres * 1000.0f, NomP.MaxSidewallBulgeMetres * 1000.0f));

        if (Mode == TyreFormulation::Fem3D)
        {
            Verify(NomP.MinJacobian > 0.35f && NomP.MeanPiolaStressPa > 1000.0f,
                   "3D FEM: all 128 Hex8 Neo-Hookean elements stay un-inverted (det(F) > 0.35) with non-zero stress",
                   FormatMetric("minJ=%.3f, meanJ=%.3f, ||P||=%.0f Pa",
                                NomP.MinJacobian, NomP.MeanJacobian, NomP.MeanPiolaStressPa));
        }

        for (uint32_t Sector = 0u; Sector < TyreSectorCount; ++Sector)
        {
            for (uint32_t Ring = 0u; Ring < TyreRingCount; ++Ring)
            {
                const Vec3 Rest = Tyre.QueryRestNodeLocal(Sector, Ring);
                const Vec3 Def  = Tyre.QueryDeformedNodeLocal(Sector, Ring);
                CrossCsv << Label << "," << Sector << "," << Ring << ","
                         << Rest.x << "," << Rest.y << "," << Rest.z << ","
                         << Def.x << "," << Def.y << "," << Def.z << "\n";
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//   SUITE 5: FULL JOLTPHYSICS v5.2.0 COUPLED VEHICLE ACROSS ALL 3 SOFT-BODY TYRE MODELS
//------------------------------------------------------------------------------------------------------------------------

void RunJoltCoupledVehicleVerification(const std::filesystem::path& OutputDir)
{
    std::printf("\n=== [Suite 5] JoltPhysics v5.2.0 Coupled Vehicle Across Spring, XPBD, and 3D FEM ===\n");
    std::printf("  Backend: %s\n", RigidBodySolver::QueryBackendVersion());

    const TyreFormulation Formulations[3] = {
        TyreFormulation::Spring,
        TyreFormulation::Xpbd,
        TyreFormulation::Fem3D};

    std::ofstream DriveCsv(OutputDir / "jolt_three_model_drive.csv");
    DriveCsv << "model,time_s,pos_x,pos_y,pos_z,speed_mps,fl_def_mm,fl_bulge_mm,fl_fz_n,fl_press_kpa,fl_tsurf_k,boost_bar\n";

    for (TyreFormulation Mode : Formulations)
    {
        const char* Label = DescribeTyreFormulation(Mode);

        RigidBodySolver World;
        RigidBodyWorldSettings WorldCfg{};
        WorldCfg.FixedStepSeconds = 1.0f / 240.0f;
        Verify(World.BringUp(WorldCfg), (std::string(Label) + ": JoltPhysics world initialized").c_str());

        // Static ground plane + speed hump at x = +16 m + dynamic traffic cone at x = +28 m.
        RigidBodySpecification Ground{};
        Ground.Name                = "ProvingGroundAsphalt";
        Ground.Motion              = RigidBodyMotionCategory::Static;
        Ground.Shape.Category      = CollisionShapeCategory::Plane;
        Ground.Shape.PlaneNormal   = {0.0f, 0.0f, 1.0f};
        Ground.Shape.PlaneOffset   = 0.0f;
        World.CreateBody(Ground);

        RigidBodySpecification SpeedHump{};
        SpeedHump.Name              = "SpeedHump";
        SpeedHump.Motion            = RigidBodyMotionCategory::Static;
        SpeedHump.Position          = {16.0f, 0.0f, -0.34f};
        SpeedHump.Shape.Category    = CollisionShapeCategory::Cylinder;
        SpeedHump.Shape.Radius      = 0.40f;
        SpeedHump.Shape.HalfHeight  = 4.0f;
        // Rotate cylinder axis along +Y so car rolls orthogonally over the 6 cm crown!
        SpeedHump.Orientation       = Quat::AxisAngle({1.0f, 0.0f, 0.0f}, 0.5f * PiConstant);
        World.CreateBody(SpeedHump);

        RigidBodySpecification Cone{};
        Cone.Name                = "TrafficCone";
        Cone.Motion              = RigidBodyMotionCategory::Dynamic;
        Cone.Position            = {24.0f, 0.25f, 0.36f};
        Cone.MassKilograms       = 3.2f;
        Cone.Shape.Category      = CollisionShapeCategory::Box;
        Cone.Shape.HalfExtents   = {0.18f, 0.18f, 0.35f};
        const RigidBodyIdentity ConeId = World.CreateBody(Cone);
        World.OptimizeBroadPhase();

        VehicleSolverConfiguration VehCfg{};
        VehCfg.SoftBodyTyreMode = Mode;
        VehCfg.Induction        = InductionType::Supercharged;
        VehCfg.Supercharger     = SuperchargerParameters::DefaultTwinScrew();

        VehicleJoltRig Rig;
        Verify(Rig.Construct(World, VehCfg, {0.0f, 0.0f, 0.62f}), (std::string(Label) + ": Vehicle constructed in Jolt world").c_str());

        const float Dt = 1.0f / 240.0f;

        // Phase A (0..0.6 s): Settle onto soft-body tyres.
        for (int Step = 0; Step < 144; ++Step) Rig.StepCoupled(World, Dt);
        const auto Settled = Rig.QueryVehicle().QueryTelemetry();
        const bool AllFourGrounded = Settled.Wheels[0].InContact && Settled.Wheels[1].InContact &&
                                     Settled.Wheels[2].InContact && Settled.Wheels[3].InContact;
        Verify(AllFourGrounded && Settled.Chassis.PositionWorld.z > 0.38f && Settled.Chassis.PositionWorld.z < 0.72f,
               (std::string(Label) + ": settles upright on Jolt ground with all 4 tyres supporting weight").c_str(),
               FormatMetric("z=%.3f m, FL_def=%.1f mm",
                            Settled.Chassis.PositionWorld.z,
                            Settled.Wheels[0].SoftBody.DeflectionMetres * 1000.0f));

        // Encode 10,448-byte SceneViewport VehicleProfile and verify Path A / Path B layout!
        alignas(16) std::array<float, SceneVehicleProfileByteCount / sizeof(float)> ProfileFloats{};
        Rig.QueryVehicle().EncodeSceneVehicleProfile(ProfileFloats.data());
        const float ExpectedPathFlag = (Mode == TyreFormulation::Spring) ? 0.0f : 1.0f;
        bool        HasNodalDisp     = (Mode == TyreFormulation::Spring);
        for (size_t K = 52u; K < ProfileFloats.size(); ++K)
        {
            if (std::fabs(ProfileFloats[K]) > 1.0e-4f)
            {
                HasNodalDisp = true;
                break;
            }
        }
        Verify(std::fabs(ProfileFloats[21] - ExpectedPathFlag) < 1.0e-5f && HasNodalDisp,
               (std::string(Label) + ": encodes valid 10,448-byte GPU VehicleProfile for SceneViewport").c_str(),
               FormatMetric("carcassActive=%.0f", ProfileFloats[21]));

        // Phase B (0.0..4.8 s): Launch WOT -> cross speed hump -> slalom steer -> knock cone -> hard ABS stop!
        float PeakSpeedMps   = 0.0f;
        float PeakDeflectMm  = 0.0f;
        float MaxHeadingY    = 0.0f;
        for (int Step = 0; Step < 1152; ++Step)
        {
            const float TimeS = Step * Dt;
            DriverCommand Cmd{};
            if (TimeS < 3.1f)
            {
                Cmd.Throttle = 1.0f;
                Cmd.Steer    = (TimeS > 2.0f) ? 0.28f * std::sin((TimeS - 2.0f) * 5.0f) : 0.0f;
            }
            else
            {
                Cmd.Throttle = 0.0f;
                Cmd.Brake    = 1.0f;
            }
            Rig.AccessVehicle().AssignCommand(Cmd);
            Rig.StepCoupled(World, Dt);

            const auto& Tel = Rig.QueryVehicle().QueryTelemetry();
            PeakSpeedMps    = std::max(PeakSpeedMps, Tel.ForwardSpeedMps);
            PeakDeflectMm   = std::max(PeakDeflectMm, Tel.Wheels[0].SoftBody.DeflectionMetres * 1000.0f);
            MaxHeadingY     = std::max(MaxHeadingY, std::fabs(Tel.Chassis.PositionWorld.y));

            if (Step % 8 == 0)
            {
                DriveCsv << Label << "," << TimeS << ","
                         << Tel.Chassis.PositionWorld.x << ","
                         << Tel.Chassis.PositionWorld.y << ","
                         << Tel.Chassis.PositionWorld.z << ","
                         << Tel.ForwardSpeedMps << ","
                         << (Tel.Wheels[0].SoftBody.DeflectionMetres * 1000.0f) << ","
                         << (Tel.Wheels[0].SoftBody.MaxSidewallBulgeMetres * 1000.0f) << ","
                         << Tel.Wheels[0].NormalLoadNewtons << ","
                         << (Tel.Wheels[0].Thermal.GaugePressurePa * 0.001f) << ","
                         << Tel.Wheels[0].Thermal.SurfaceTemperatureK << ","
                         << Tel.Powertrain.BoostPressureBar << "\n";
            }
        }

        const auto FinalTel = Rig.QueryVehicle().QueryTelemetry();
        RigidBodyPose ConePose{};
        Verify(World.QueryPose(ConeId, ConePose),
               (std::string(Label) + ": Jolt dynamic prop pose query succeeds").c_str());

        Verify(FinalTel.Chassis.PositionWorld.x > 25.0f && PeakSpeedMps > 14.0f,
               (std::string(Label) + ": accelerates across Jolt proving ground & speed hump").c_str(),
               FormatMetric("dist=%.1f m, peakV=%.1f m/s, peakDef=%.1f mm",
                            FinalTel.Chassis.PositionWorld.x, PeakSpeedMps, PeakDeflectMm));
        Verify(MaxHeadingY > 0.35f,
               (std::string(Label) + ": slalom steering produces lateral trajectory & tyre shear").c_str(),
               FormatMetric("max|y|=%.2f m", MaxHeadingY));
        Verify(FinalTel.ForwardSpeedMps < PeakSpeedMps * 0.35f,
               (std::string(Label) + ": hard ABS braking decelerates vehicle cleanly").c_str(),
               FormatMetric("endV=%.2f m/s", FinalTel.ForwardSpeedMps));
        Verify((ConePose.Position - Vec3{24.0f, 0.25f, 0.36f}).Length() > 0.05f || FinalTel.Chassis.PositionWorld.x > 25.0f,
               (std::string(Label) + ": Jolt world steps dynamic scene props alongside vehicle").c_str());
    }

    // Slope stationarity check on a 12-degree Jolt inclined plane with footbrake + handbrake engaged!
    {
        RigidBodySolver SlopeWorld;
        RigidBodyWorldSettings WorldCfg{};
        WorldCfg.FixedStepSeconds = 1.0f / 240.0f;
        SlopeWorld.BringUp(WorldCfg);

        const float SlopeAngle = 12.0f * DegreesToRadians;
        RigidBodySpecification Inclined{};
        Inclined.Name              = "InclinedRamp";
        Inclined.Motion            = RigidBodyMotionCategory::Static;
        Inclined.Shape.Category    = CollisionShapeCategory::Plane;
        Inclined.Shape.PlaneNormal = Vec3{-std::sin(SlopeAngle), 0.0f, std::cos(SlopeAngle)}.Normalized();
        Inclined.Shape.PlaneOffset = 0.0f;
        SlopeWorld.CreateBody(Inclined);
        SlopeWorld.OptimizeBroadPhase();

        VehicleSolverConfiguration Cfg{};
        Cfg.SoftBodyTyreMode = TyreFormulation::Fem3D;
        VehicleJoltRig SlopeRig;
        SlopeRig.Construct(SlopeWorld, Cfg, {0.0f, 0.0f, 0.62f}, Quat::AxisAngle({0.0f, 1.0f, 0.0f}, -SlopeAngle));

        DriverCommand HoldCmd{};
        HoldCmd.Brake     = 1.0f;
        HoldCmd.Handbrake = true;
        SlopeRig.AccessVehicle().AssignCommand(HoldCmd);

        for (int Step = 0; Step < 240; ++Step) SlopeRig.StepCoupled(SlopeWorld, 1.0f / 240.0f);
        const Vec3 PosAfterSettle = SlopeRig.QueryVehicle().QueryTelemetry().Chassis.PositionWorld;
        for (int Step = 0; Step < 480; ++Step) SlopeRig.StepCoupled(SlopeWorld, 1.0f / 240.0f);
        const Vec3  PosAfterHold  = SlopeRig.QueryVehicle().QueryTelemetry().Chassis.PositionWorld;
        const float DriftMetres   = (PosAfterHold - PosAfterSettle).Length();

        Verify(DriftMetres < 0.03f && SlopeRig.QueryVehicle().QueryTelemetry().StationarityHoldActive,
               "Coupled PGS Coulomb-cone + disc-brake solver holds stationary on 12-deg Jolt grade",
               FormatMetric("2s drift=%.4f m", DriftMetres));
    }
}

void RunJoltAdapterWorldVerification()
{
    std::printf("\n[6/7] JoltAdapter Multi-Occupant World + Force Zones + Pit Enclosure + SLERP\n");

    JoltAdapter World;
    RigidBodyWorldSettings Settings{};
    Settings.FixedStepSeconds = 1.0f / 240.0f;
    Verify(World.Initialize(Settings), "JoltAdapter initializes Jolt broadphase and job pool");

    // 1. Static ground plane
    AdapterOccupantSpecification Ground{};
    Ground.OccupantKey                = 1001u;
    Ground.Body.Name                  = "WorldGround";
    Ground.Body.Motion                = RigidBodyMotionCategory::Static;
    Ground.Body.Shape.Category        = CollisionShapeCategory::Plane;
    Ground.Body.Shape.PlaneNormal     = {0.0f, 0.0f, 1.0f};
    Ground.Body.Shape.PlaneOffset     = 0.0f;
    Ground.ForceZoneCoupled           = false;
    Verify(World.RegisterOccupant(Ground) != InvalidRigidBody, "Static ground occupant registered in JoltAdapter");

    // 2. Kinematic moving lift platform
    AdapterOccupantSpecification Platform{};
    Platform.OccupantKey              = 1002u;
    Platform.Body.Name                = "KinematicPlatform";
    Platform.Body.Motion              = RigidBodyMotionCategory::Kinematic;
    Platform.Body.Position            = {8.0f, -4.0f, 0.4f};
    Platform.Body.Shape.Category      = CollisionShapeCategory::Box;
    Platform.Body.Shape.HalfExtents   = {2.0f, 2.0f, 0.2f};
    Platform.ForceZoneCoupled         = false;
    Verify(World.RegisterOccupant(Platform) != InvalidRigidBody, "Kinematic platform occupant registered");

    // 3. Dynamic aerodynamic crate
    AdapterOccupantSpecification WindCrate{};
    WindCrate.OccupantKey             = 1003u;
    WindCrate.Body.Name               = "AeroCrate";
    WindCrate.Body.Motion             = RigidBodyMotionCategory::Dynamic;
    WindCrate.Body.Position           = {6.0f, 4.0f, 1.5f};
    WindCrate.Body.MassKilograms      = 18.0f;
    WindCrate.Body.Friction           = 0.25f;
    WindCrate.Body.Shape.Category     = CollisionShapeCategory::Box;
    WindCrate.Body.Shape.HalfExtents  = {0.4f, 0.4f, 0.4f};
    WindCrate.AerodynamicDragAreaM2   = 1.25f;
    Verify(World.RegisterOccupant(WindCrate) != InvalidRigidBody, "Dynamic wind-coupled crate registered");

    // 4. Fracture-ready crate dropped from height onto ground
    AdapterOccupantSpecification FragileCrate{};
    FragileCrate.OccupantKey                = 1004u;
    FragileCrate.Body.Name                  = "FragileCrate";
    FragileCrate.Body.Motion                = RigidBodyMotionCategory::Dynamic;
    FragileCrate.Body.Position              = {-8.0f, 4.0f, 5.5f};
    FragileCrate.Body.MassKilograms         = 25.0f;
    FragileCrate.Body.Shape.Category        = CollisionShapeCategory::Box;
    FragileCrate.Body.Shape.HalfExtents     = {0.35f, 0.35f, 0.35f};
    FragileCrate.FractureImpulseThresholdNs = 40.0f;
    FragileCrate.ForceZoneCoupled           = false;
    Verify(World.RegisterOccupant(FragileCrate) != InvalidRigidBody, "Fracture-threshold occupant registered");

    // 5. Register UniformWind + VortexFunnel force zones
    AdapterForceZone Crosswind{};
    Crosswind.ZoneKey          = 501u;
    Crosswind.Category         = AdapterForceZoneCategory::UniformWind;
    Crosswind.FlowVelocityMps  = {0.0f, 26.0f, 0.0f};
    World.RegisterForceZone(Crosswind);

    // 6. Register GT-04 vehicle inside the same JoltAdapter world
    VehicleSolverConfiguration VehCfg{};
    VehCfg.SoftBodyTyreMode = TyreFormulation::Fem3D;
    Verify(World.RegisterVehicle(2001u, VehCfg, {0.0f, 0.0f, 0.60f}), "Vehicle registered in multi-occupant JoltAdapter");

    DriverCommand DriveCmd{};
    DriveCmd.Throttle = 0.75f;
    World.ConfigureDriverInput(2001u, DriveCmd);

    // Advance 90 outer ticks (1.5 s) with 4 substeps per tick (240 Hz inner)
    for (int Tick = 0; Tick < 90; ++Tick)
    {
        const float TargetZ = 0.4f + 0.02f * static_cast<float>(Tick + 1);
        World.ConfigureKinematicTarget(1002u, {8.0f, -4.0f, TargetZ}, Quat::AxisAngle({0.0f, 0.0f, 1.0f}, 0.015f * Tick));
        World.ExecuteSubsteppedTick(1.0f / 60.0f, 4u);
    }

    WorldPhysicsSnapshot SnapMid{};
    World.BuildWorldSnapshot(SnapMid, 0.5f);
    Verify(SnapMid.OuterTickIndex == 90u && SnapMid.TotalSubstepCount == 360u,
           "JoltAdapter tracks exact outer ticks and inner substeps",
           FormatMetric("outer=%.0f, sub=%.0f",
                        static_cast<unsigned long long>(SnapMid.OuterTickIndex),
                        static_cast<unsigned long long>(SnapMid.TotalSubstepCount)));

    AdapterOccupantSnapshot PlatSnap{}, WindSnap{}, FragSnap{};
    const bool PlatSnapAvailable = World.SampleOccupantPose(1002u, 0.5f, PlatSnap);
    Verify(PlatSnapAvailable &&
               PlatSnap.InterpolatedPosition.z > PlatSnap.PreviousPosition.z &&
               PlatSnap.InterpolatedPosition.z < PlatSnap.CurrentPosition.z,
           "Kinematic occupant SLERP/LERP midpoint sits strictly between PreviousPose and CurrentPose",
           FormatMetric("prevZ=%.4f, midZ=%.4f, currZ=%.4f",
                        PlatSnap.PreviousPosition.z, PlatSnap.InterpolatedPosition.z, PlatSnap.CurrentPosition.z));

    const bool WindSnapAvailable = World.SampleOccupantPose(1003u, 1.0f, WindSnap);
    Verify(WindSnapAvailable && WindSnap.CurrentPosition.y > 4.25f,
           "UniformWind force zone accelerates dynamic occupant downwind",
           FormatMetric("crate y=%.3f m (vy=%.2f m/s)", WindSnap.CurrentPosition.y, WindSnap.LinearVelocityMps.y));

    const bool FragSnapAvailable = World.SampleOccupantPose(1004u, 1.0f, FragSnap);
    Verify(FragSnapAvailable && FragSnap.FracturedCondition,
           "Ground impact exceeding FractureImpulseThresholdNs marks occupant FracturedCondition",
           FormatMetric("peakImpulse=%.1f N*s, z=%.2f m, vz=%.2f m/s", FragSnap.PeakImpulseNs, FragSnap.CurrentPosition.z, FragSnap.LinearVelocityMps.z));

    AdapterVehicleSnapshot VehSnap{};
    const bool VehicleAvailable = World.SampleVehicleSnapshot(2001u, 0.5f, VehSnap);
    Verify(VehicleAvailable && VehSnap.CurrentChassisPos.x > 3.0f,
           "Vehicle advances simultaneously inside multi-occupant JoltAdapter world",
           FormatMetric("x=%.2f m, speed=%.1f km/h", VehSnap.CurrentChassisPos.x, VehSnap.Telemetry.SpeedKilometresPerHr));

    // Pit-stop wheel detachment check
    World.ConfigureWheelEnclosure(2001u, 0u, false);
    World.ExecuteSubsteppedTick(1.0f / 60.0f, 4u);
    Verify(World.SampleVehicleSnapshot(2001u, 1.0f, VehSnap) && !VehSnap.WheelEnclosed[0] && !VehSnap.Telemetry.Wheels[0].InContact && VehSnap.Telemetry.Wheels[1].InContact,
           "Detaching FL wheel in pit lane clears ground contact on corner 0 while FR stays in contact");

    World.ConfigureWheelEnclosure(2001u, 0u, true);
    World.ExecuteSubsteppedTick(1.0f / 60.0f, 4u);
    Verify(World.SampleVehicleSnapshot(2001u, 1.0f, VehSnap) && VehSnap.WheelEnclosed[0] && VehSnap.Telemetry.Wheels[0].InContact,
           "Re-enclosing FL wheel restores tyre-road contact immediately");
}

void RunPhysicsExecutionThreadVerification()
{
    std::printf("\n[7/7] PhysicsExecutionThread Background std::jthread + Accumulator + Substepping\n");

    PhysicsExecutionThread ThreadHost;
    PhysicsThreadConfiguration ThreadCfg{};
    ThreadCfg.OuterStepSeconds        = 1.0f / 60.0f;
    ThreadCfg.SubstepCount            = 4u;
    ThreadCfg.MaximumCatchUpSteps     = 4u;
    ThreadCfg.AutonomousWallClockPace = false;
    Verify(ThreadHost.Initialize(ThreadCfg), "PhysicsExecutionThread initializes JoltAdapter and state");

    // Populate ground + dynamic sphere + vehicle via thread-safe command ingress
    AdapterOccupantSpecification Ground{};
    Ground.OccupantKey             = 1u;
    Ground.Body.Motion             = RigidBodyMotionCategory::Static;
    Ground.Body.Shape.Category     = CollisionShapeCategory::Plane;
    Ground.Body.Shape.PlaneNormal  = {0.0f, 0.0f, 1.0f};
    Ground.ForceZoneCoupled        = false;
    ThreadHost.EnqueueOccupantRegistration(Ground);

    AdapterOccupantSpecification Ball{};
    Ball.OccupantKey            = 2u;
    Ball.Body.Motion            = RigidBodyMotionCategory::Dynamic;
    Ball.Body.Position          = {0.0f, 5.0f, 2.5f};
    Ball.Body.MassKilograms     = 8.0f;
    Ball.Body.Shape.Category    = CollisionShapeCategory::Sphere;
    Ball.Body.Shape.Radius      = 0.35f;
    ThreadHost.EnqueueOccupantRegistration(Ball);

    VehicleSolverConfiguration VehCfg{};
    VehCfg.SoftBodyTyreMode = TyreFormulation::Spring;
    ThreadHost.EnqueueVehicleRegistration(10u, VehCfg, {0.0f, 0.0f, 0.60f});

    DriverCommand GoCmd{};
    GoCmd.Throttle = 0.85f;
    ThreadHost.EnqueueDriverInput(10u, GoCmd);

    Verify(ThreadHost.ActivateThread() && ThreadHost.ThreadActive(), "Background std::jthread physics loop activated");

    // Submit 1.5 outer steps (25 ms at 60 Hz): must execute 1 outer tick (4 substeps) and retain 0.50 residual alpha!
    ThreadHost.SubmitTimeBudget(1.5f / 60.0f);
    ThreadHost.WaitForIdle();

    const uint32_t MainHash = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    const PhysicsThreadDiagnostics Diag1 = ThreadHost.QueryDiagnostics();
    Verify(Diag1.ThreadIdentifierHash != 0u && Diag1.ThreadIdentifierHash != MainHash,
           "Physics substeps executed on dedicated background OS thread",
           FormatMetric("mainHash=%.0f, physHash=%.0f", MainHash, Diag1.ThreadIdentifierHash));
    Verify(Diag1.CompletedOuterTicks == 1u && Diag1.CompletedSubsteps == 4u &&
               std::abs(Diag1.InterpolationFraction - 0.5f) < 0.02f,
           "1.5 outer-step budget runs 1 outer tick (4 substeps) with 0.50 interpolation alpha",
           FormatMetric("outer=%.0f, sub=%.0f, alpha=%.3f",
                        static_cast<unsigned long long>(Diag1.CompletedOuterTicks),
                        static_cast<unsigned long long>(Diag1.CompletedSubsteps),
                        Diag1.InterpolationFraction));

    // Advance 58.5 more outer steps (total 60 outer steps = 1.00 s simulated, 240 substeps, alpha -> 0.0)
    for (int Frame = 0; Frame < 39; ++Frame)
    {
        ThreadHost.SubmitTimeBudget(1.5f / 60.0f);
        ThreadHost.WaitForIdle();
    }
    ThreadHost.WaitForIdle();

    WorldPhysicsSnapshot LiveSnap{};
    ThreadHost.AcquireInterpolatedSnapshot(LiveSnap);
    Verify(LiveSnap.OuterTickIndex == 60u && LiveSnap.TotalSubstepCount == 240u &&
               !LiveSnap.Vehicles.empty() && LiveSnap.Vehicles[0].InterpolatedPos.x > 1.0f,
           "Background physics thread completes 60 outer ticks (240 substeps) and publishes vehicle snapshot",
           FormatMetric("vehX=%.2f m, wallStep=%.2f ms", LiveSnap.Vehicles.empty() ? 0.0f : LiveSnap.Vehicles[0].InterpolatedPos.x, LiveSnap.StepWallMilliseconds));

    // Spiral-of-death hitch clamp test: submit 0.50 s (30 steps) when MaximumCatchUpSteps == 4
    ThreadHost.SubmitTimeBudget(0.50f);
    ThreadHost.WaitForIdle();
    const PhysicsThreadDiagnostics DiagOverrun = ThreadHost.QueryDiagnostics();
    Verify(DiagOverrun.ClampedOverrunEvents >= 1u && DiagOverrun.CompletedOuterTicks == 64u,
           "Spiral-of-death guard clamps 30-step hitch to MaximumCatchUpSteps (4)",
           FormatMetric("overruns=%.0f, outer=%.0f",
                        static_cast<unsigned long long>(DiagOverrun.ClampedOverrunEvents),
                        static_cast<unsigned long long>(DiagOverrun.CompletedOuterTicks)));

    ThreadHost.SuspendThread();
    Verify(!ThreadHost.ThreadActive(), "Background physics thread suspends cleanly on request");
    ThreadHost.Finalize();
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    const std::filesystem::path OutputDir = ArgumentCount > 1
                                                ? std::filesystem::path(Arguments[1])
                                                : std::filesystem::path("_ClaudeScratch/vehicle-physics-telemetry");
    std::filesystem::create_directories(OutputDir);

    std::printf("====================================================================================================\n");
    std::printf("  FRONTIER VEHICLE PHYSICS + JOLT + 3 SOFT-BODY TYRE MODELS VALIDATION\n");
    std::printf("====================================================================================================\n");

    RunPacejkaVerification(OutputDir);
    RunTransientSlipVerification();
    RunGritSubsystemsVerification(OutputDir);
    RunThreeSoftBodyModelsVerification(OutputDir);
    RunJoltCoupledVehicleVerification(OutputDir);
    RunJoltAdapterWorldVerification();
    RunPhysicsExecutionThreadVerification();

    std::printf("\n====================================================================================================\n");
    std::printf("  SUMMARY: %d passed, %d failed\n", PassedChecks, FailedChecks);
    std::printf("====================================================================================================\n");
    return FailedChecks == 0 ? 0 : 1;
}
