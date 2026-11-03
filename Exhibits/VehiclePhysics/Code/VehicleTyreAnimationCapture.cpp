/*==========================================================================================================================================
                                                      VEHICLETYREANIMATIONCAPTURE.CPP
==========================================================================================================================================*/
// 🧩 Simulates the JoltPhysics-coupled ControlVehicle across all 3 soft-body tyre models (3D FEM, Spring, XPBD), renders real Vulkan
//    SceneViewport frames (VehicleVisual.slang Path A & Path B), and exports 32x5 carcass meshes + telemetry for animation assembly.

#include "Engine/PhysicalDynamics/RigidBodySolver.h"
#include "Engine/PhysicalDynamics/Vehicle/VehicleJoltRig.h"
#include "Engine/PhysicalDynamics/Vehicle/VehicleScenePublisher.h"
#include "Engine/Rendering/SceneViewport/SceneViewport.h"

#include "stb_image_write.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::Vehicle;
using namespace Frontier::GlobalIllumination;

namespace
{

struct CapturedFrameRecord
{
    float                               TimeSeconds       = 0.0f;
    Vec3                                ChassisPos        {};
    Quat                                ChassisRot        {};
    float                               SpeedMps          = 0.0f;
    float                               BoostBar          = 0.0f;
    float                               SteerWheelRad     = 0.0f;
    WheelCornerTelemetry                FrontLeft         {};
    WheelCornerTelemetry                FrontRight        {};
    std::array<Vec3, TyreNodesPerWheel> DeformedNodesLocal{};
    std::array<Vec3, TyreNodesPerWheel> RestNodesLocal    {};
};

SceneEnvironment ConstructDaylightEnvironment()
{
    SceneEnvironment Env{};
    Env.Solar.ElevationDegrees      = 34.0f;
    Env.Solar.AzimuthDegrees        = 138.0f;
    Env.Solar.IlluminanceKilolux    = 98.0f;
    Env.Solar.KelvinTemperature     = 5778.0f;
    Env.Atmosphere.RayleighScattering = 1.0f;
    Env.Atmosphere.MieTurbidity       = 2.1f;
    Env.Clouds.Coverage             = 0.30f;
    Env.Fog.BaseDensity             = 0.004f;
    return Env;
}

std::vector<CapturedFrameRecord> SimulateManoeuvre(TyreFormulation Mode, uint32_t FrameCount, float FrameIntervalSec)
{
    std::vector<CapturedFrameRecord> Frames;
    Frames.reserve(FrameCount);

    RigidBodySolver World;
    RigidBodyWorldSettings WorldCfg{};
    WorldCfg.FixedStepSeconds = 1.0f / 240.0f;
    World.BringUp(WorldCfg);

    RigidBodySpecification Ground{};
    Ground.Name              = "ProvingGroundPlane";
    Ground.Motion            = RigidBodyMotionCategory::Static;
    Ground.Shape.Category    = CollisionShapeCategory::Plane;
    Ground.Shape.PlaneNormal = {0.0f, 0.0f, 1.0f};
    Ground.Shape.PlaneOffset = 0.0f;
    World.CreateBody(Ground);

    // Place a pronounced rounded speed-bump ridge across x = -31.5 m so the tyres visibly squash & bulge rolling over it!
    RigidBodySpecification Bump{};
    Bump.Name             = "TestRidge";
    Bump.Motion           = RigidBodyMotionCategory::Static;
    Bump.Position         = {-31.5f, 0.0f, -0.325f};
    Bump.Shape.Category   = CollisionShapeCategory::Cylinder;
    Bump.Shape.Radius     = 0.40f;
    Bump.Shape.HalfHeight = 8.0f;
    Bump.Orientation      = Quat::AxisAngle({1.0f, 0.0f, 0.0f}, 0.5f * PiConstant);
    World.CreateBody(Bump);
    World.OptimizeBroadPhase();

    VehicleSolverConfiguration VehCfg{};
    VehCfg.SoftBodyTyreMode = Mode;
    VehCfg.Induction        = InductionType::Supercharged;
    VehCfg.Supercharger     = SuperchargerParameters::DefaultTwinScrew();
    VehCfg.RaceTune         = true;

    VehicleJoltRig Rig;
    Rig.Construct(World, VehCfg, {-40.0f, 0.0f, 0.56f});

    const float Dt          = 1.0f / 240.0f;
    const int   SettleSteps = 120;
    for (int Step = 0; Step < SettleSteps; ++Step) Rig.StepCoupled(World, Dt);

    const int SubstepsPerFrame = std::max(1, static_cast<int>( std::lround(FrameIntervalSec / Dt)));
    for (uint32_t FrameIdx = 0u; FrameIdx < FrameCount; ++FrameIdx)
    {
        const float TimeS = static_cast<float>(FrameIdx) * FrameIntervalSec;
        for (int Sub = 0; Sub < SubstepsPerFrame; ++Sub)
        {
            const float SubTime = TimeS + static_cast<float>(Sub) * Dt;
            DriverCommand Cmd{};
            if (SubTime < 1.35f)
            {
                // Full supercharged launch + roll across the speed-bump ridge!
                Cmd.Throttle = 1.0f;
                Cmd.Steer    = 0.0f;
            }
            else if (SubTime < 2.95f)
            {
                // High-load S-bend slalom cornering (loads outside front tyre & shears tread laterally)!
                Cmd.Throttle = 0.82f;
                Cmd.Steer    = 0.72f * std::sin((SubTime - 1.35f) * 3.927f);
            }
            else
            {
                // Hard emergency ABS stop (dives nose onto front tyres, spiking Fz & sidewall bulge)!
                Cmd.Throttle = 0.0f;
                Cmd.Brake    = 1.0f;
                Cmd.Steer    = -0.18f;
            }
            Rig.AccessVehicle().AssignCommand(Cmd);
            Rig.StepCoupled(World, Dt);
        }

        const VehicleTelemetryReading& Tel  = Rig.QueryVehicle().QueryTelemetry();
        const TyreSoftBody&            Tyre = Rig.QueryVehicle().QueryCornerSoftBody(0u);

        CapturedFrameRecord Rec{};
        Rec.TimeSeconds   = TimeS + FrameIntervalSec;
        Rec.ChassisPos    = Tel.Chassis.PositionWorld;
        Rec.ChassisRot    = Tel.Chassis.OrientationWorld;
        Rec.SpeedMps      = Tel.ForwardSpeedMps;
        Rec.BoostBar      = Tel.Powertrain.BoostPressureBar;
        Rec.SteerWheelRad = Tel.SteeringWheelRadians;
        Rec.FrontLeft     = Tel.Wheels[0];
        Rec.FrontRight    = Tel.Wheels[1];

        for (uint32_t Sector = 0u; Sector < TyreSectorCount; ++Sector)
        {
            for (uint32_t Ring = 0u; Ring < TyreRingCount; ++Ring)
            {
                const uint32_t Idx          = Sector * TyreRingCount + Ring;
                Rec.RestNodesLocal[Idx]     = Tyre.QueryRestNodeLocal(Sector, Ring);
                Rec.DeformedNodesLocal[Idx] = Tyre.QueryDeformedNodeLocal(Sector, Ring);
            }
        }
        Frames.push_back(Rec);
    }
    return Frames;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    const std::filesystem::path RepoRoot  = ArgumentCount > 1 ? std::filesystem::path(Arguments[1]) : std::filesystem::current_path();
    const std::filesystem::path ShaderDir = ArgumentCount > 2 ? std::filesystem::path(Arguments[2])
                                                              : (RepoRoot / "EngineContent/ShaderArchives");
    const std::filesystem::path OutDir    = ArgumentCount > 3 ? std::filesystem::path(Arguments[3])
                                                              : (RepoRoot / "_ClaudeScratch/vehicle-animation-raw");
    std::filesystem::create_directories(OutDir);

    constexpr uint32_t FrameCount       = 36u;
    constexpr float    FrameIntervalSec = 0.11f;   // 36 * 0.11s = 3.96s full manoeuvre

    const TyreFormulation Modes[3]      = {TyreFormulation::Fem3D, TyreFormulation::Spring, TyreFormulation::Xpbd};
    const char*           ModeSlugs[3]  = {"fem3d", "spring", "xpbd"};

    std::printf("[1/3] Simulating JoltPhysics + Vehicle across 3D FEM, Spring, and XPBD (%u frames @ 240 Hz)...\n", FrameCount);
    std::array<std::vector<CapturedFrameRecord>, 3> AllRuns{};

    for (size_t M = 0; M < 3; ++M)
    {
        AllRuns[M] = SimulateManoeuvre(Modes[M], FrameCount, FrameIntervalSec);

        // Write per-frame telemetry CSV and binary 32x5 node positions for Python showcase renderer.
        std::ofstream Csv(OutDir / (std::string("telemetry_") + ModeSlugs[M] + ".csv"));
        Csv << "frame,time_s,pos_x,pos_y,pos_z,speed_mps,boost_bar,steer_rad,"
               "fl_def_mm,fl_bulge_mm,fl_lat_mm,fl_fz_n,fl_fx_n,fl_fy_n,fl_kappa,fl_alpha_deg,"
               "fl_press_kpa,fl_tsurf_k,fl_spin_rad,fl_contact_rad,fl_mean_j,fl_piola_kpa\n";

        std::ofstream Bin(OutDir / (std::string("nodes_") + ModeSlugs[M] + ".bin"), std::ios::binary);
        for (uint32_t F = 0u; F < FrameCount; ++F)
        {
            const CapturedFrameRecord& R = AllRuns[M][F];
            Csv << F << "," << R.TimeSeconds << ","
                << R.ChassisPos.x << "," << R.ChassisPos.y << "," << R.ChassisPos.z << ","
                << R.SpeedMps << "," << R.BoostBar << "," << R.SteerWheelRad << ","
                << (R.FrontLeft.SoftBody.DeflectionMetres * 1000.0f) << ","
                << (R.FrontLeft.SoftBody.MaxSidewallBulgeMetres * 1000.0f) << ","
                << (R.FrontLeft.SoftBody.LateralDistortionMetres * 1000.0f) << ","
                << R.FrontLeft.NormalLoadNewtons << ","
                << R.FrontLeft.LongitudinalForceN << ","
                << R.FrontLeft.LateralForceN << ","
                << R.FrontLeft.SlipRatio << ","
                << (R.FrontLeft.SlipAngleRadians * RadiansToDegrees) << ","
                << (R.FrontLeft.Thermal.GaugePressurePa * 0.001f) << ","
                << R.FrontLeft.Thermal.SurfaceTemperatureK << ","
                << R.FrontLeft.SpinAngleRadians << ","
                << R.FrontLeft.SoftBody.ContactAngleRadians << ","
                << R.FrontLeft.SoftBody.MeanJacobian << ","
                << (R.FrontLeft.SoftBody.MeanPiolaStressPa * 0.001f) << "\n";

            for (uint32_t Idx = 0u; Idx < TyreNodesPerWheel; ++Idx)
            {
                const float Pair[6] = {
                    R.RestNodesLocal[Idx].x,     R.RestNodesLocal[Idx].y,     R.RestNodesLocal[Idx].z,
                    R.DeformedNodesLocal[Idx].x, R.DeformedNodesLocal[Idx].y, R.DeformedNodesLocal[Idx].z};
                Bin.write(reinterpret_cast<const char*>(Pair), sizeof(Pair));
            }
        }
    }

    std::printf("[2/3] Bringing up Vulkan SceneViewport (IncludeVehicle = true) for live 3D rendering...\n");
    SceneViewportConfiguration ViewCfg{};
    ViewCfg.GlobalIlluminationShaders = ShaderDir / "GlobalIllumination";
    ViewCfg.EnvironmentShaders        = ShaderDir / "Environment";
    ViewCfg.SceneArchive              = RepoRoot / "EngineContent/EnvironmentArchives/ProvingGround";
    ViewCfg.CelestialMaps             = RepoRoot / "EngineContent/EnvironmentArchives/CelestialMaps";
    ViewCfg.Quality                   = "low";
    ViewCfg.RayTracing                = false;
    ViewCfg.IncludeVehicle            = true;
    ViewCfg.TraceScale                = 2;

    const std::array<uint32_t, 2> ChaseExtent{640u, 360u};
    SceneViewport                 Viewport;
    std::string                   Diagnostic;
    if (!Viewport.Construct(nullptr, ViewCfg, ChaseExtent, &Diagnostic))
    {
        std::fprintf(stderr, "SceneViewport bring-up failed: %s\n", Diagnostic.c_str());
        return 1;
    }
    std::printf("  Adapter: %s\n", Viewport.QueryAdapter().c_str());

    const SceneEnvironment Env = ConstructDaylightEnvironment();
    ViewportRendering      FastRender{};
    FastRender.AnalyticIllumination = true;   // crisp, deterministic, zero-noise 36-frame animation + macro wheel views!
    FastRender.ExposureCompensation = 0.25;

    // Re-run a live coupled rig to feed PublishVehicleToViewport directly at every animation frame (3D FEM hero run),
    // PLUS render macro close-up wheel-arch comparisons for ALL THREE models (3D FEM, Spring, XPBD) under peak load!
    {
        RigidBodySolver World;
        RigidBodyWorldSettings WorldCfg{};
        WorldCfg.FixedStepSeconds = 1.0f / 240.0f;
        World.BringUp(WorldCfg);

        RigidBodySpecification Ground{};
        Ground.Motion            = RigidBodyMotionCategory::Static;
        Ground.Shape.Category    = CollisionShapeCategory::Plane;
        Ground.Shape.PlaneNormal = {0.0f, 0.0f, 1.0f};
        World.CreateBody(Ground);

        RigidBodySpecification Bump{};
        Bump.Motion           = RigidBodyMotionCategory::Static;
        Bump.Position         = {-31.5f, 0.0f, -0.325f};
        Bump.Shape.Category   = CollisionShapeCategory::Cylinder;
        Bump.Shape.Radius     = 0.40f;
        Bump.Shape.HalfHeight = 8.0f;
        Bump.Orientation      = Quat::AxisAngle({1.0f, 0.0f, 0.0f}, 0.5f * PiConstant);
        World.CreateBody(Bump);
        World.OptimizeBroadPhase();

        VehicleSolverConfiguration VehCfg{};
        VehCfg.SoftBodyTyreMode = TyreFormulation::Fem3D;
        VehCfg.Induction        = InductionType::Supercharged;
        VehCfg.Supercharger     = SuperchargerParameters::DefaultTwinScrew();
        VehCfg.RaceTune         = true;

        VehicleJoltRig Rig;
        Rig.Construct(World, VehCfg, {-40.0f, 0.0f, 0.56f});
        const float Dt = 1.0f / 240.0f;
        for (int Step = 0; Step < 120; ++Step) Rig.StepCoupled(World, Dt);

        const int SubstepsPerFrame = std::max(1, static_cast<int>(std::lround(FrameIntervalSec / Dt)));
        std::vector<uint8_t> Pixels;

        for (uint32_t F = 0u; F < FrameCount; ++F)
        {
            const float TimeS = static_cast<float>(F) * FrameIntervalSec;
            for (int Sub = 0; Sub < SubstepsPerFrame; ++Sub)
            {
                const float SubTime = TimeS + static_cast<float>(Sub) * Dt;
                DriverCommand Cmd{};
                if (SubTime < 1.35f)
                {
                    Cmd.Throttle = 1.0f;
                }
                else if (SubTime < 2.95f)
                {
                    Cmd.Throttle = 0.82f;
                    Cmd.Steer    = 0.72f * std::sin((SubTime - 1.35f) * 3.927f);
                }
                else
                {
                    Cmd.Brake = 1.0f;
                    Cmd.Steer = -0.18f;
                }
                Rig.AccessVehicle().AssignCommand(Cmd);
                Rig.StepCoupled(World, Dt);
            }

            PublishVehicleToViewport(Viewport, Rig.QueryVehicle());
            const auto& Tel   = Rig.QueryVehicle().QueryTelemetry();
            const Vec3  CarP  = Tel.Chassis.PositionWorld;
            const Vec3  Fwd   = Tel.Chassis.OrientationWorld.Rotate({1.0f, 0.0f, 0.0f});
            const Vec3  Left  = Tel.Chassis.OrientationWorld.Rotate({0.0f, 1.0f, 0.0f});

            // Dynamic low three-quarter front-left chase camera showcasing body pitch/roll, steering, and front-left tyre!
            ViewportCamera ChaseCam{};
            const Vec3     Eye = CarP + Fwd * 3.65f + Left * 2.55f + Vec3{0.0f, 0.0f, 1.05f};
            const Vec3     Tgt = CarP + Fwd * 0.45f + Vec3{0.0f, 0.0f, 0.38f};
            ChaseCam.Position            = {Eye.x, Eye.y, Eye.z};
            ChaseCam.Target              = {Tgt.x, Tgt.y, Tgt.z};
            ChaseCam.VerticalFieldOfView = 0.78;

            if (!Viewport.Refresh(ChaseCam, Env, FastRender, FrameIntervalSec, ChaseExtent, &Diagnostic) ||
                !Viewport.ReadPresented(Pixels))
            {
                std::fprintf(stderr, "Chase frame %u failed: %s\n", F, Diagnostic.c_str());
                return 1;
            }
            char Name[64];
            std::snprintf(Name, sizeof(Name), "vulkan_chase_%02u.png", F);
            stbi_write_png((OutDir / Name).string().c_str(),
                           static_cast<int>(ChaseExtent[0]),
                           static_cast<int>(ChaseExtent[1]),
                           4,
                           Pixels.data(),
                           static_cast<int>(ChaseExtent[0] * 4u));
        }
    }

    // [3/3] Render Vulkan close-up macro wheel-contact renders for all 3 soft-body models (3D FEM, Spring, XPBD)
    //       under heavy loaded compression + ReSTIR GI beauty shot!
    std::printf("[3/3] Rendering Vulkan close-up wheel deformation & ReSTIR GI beauty captures across all 3 models...\n");
    for (size_t M = 0; M < 3; ++M)
    {
        VehicleSolverConfiguration Cfg{};
        Cfg.SoftBodyTyreMode                = Modes[M];
        Cfg.TyreThermal.ColdGaugePressurePa = 165000.0f;   // 1.65 bar track pressure to accentuate visible contact bulge

        // Static heavily-loaded front-left corner (simulating 1.1G braking + cornering load transfer: hub compressed 34 mm)
        ChassisKinematicReading Pose{};
        Pose.PositionWorld    = {-36.0f, 0.0f, 0.505f};
        Pose.OrientationWorld = Quat::AxisAngle({0.0f, 1.0f, 0.0f}, 0.028f);   // slight brake dive

        VehicleSolver StaticSolver;
        VehicleSolver::WorldHooks Hooks{};
        Hooks.ReadChassis = [&Pose]() { return Pose; };
        Hooks.Ground      = [](const Vec3& P, Vec3& S, Vec3& N) {
            S = {P.x, P.y, 0.0f};
            N = {0.0f, 0.0f, 1.0f};
            return true;
        };
        StaticSolver.Build(Cfg, Hooks, Pose);
        DriverCommand Cmd{};
        Cmd.Steer = 0.28f;
        StaticSolver.AssignCommand(Cmd);
        for (int Step = 0; Step < 60; ++Step) StaticSolver.Step(1.0f / 240.0f);

        PublishVehicleToViewport(Viewport, StaticSolver);
        const Vec3 WheelHub = StaticSolver.QueryTelemetry().Wheels[0].HubPositionWorld;

        ViewportCamera MacroCam{};
        MacroCam.Position            = {WheelHub.x + 1.05, WheelHub.y + 1.12, 0.36};
        MacroCam.Target              = {WheelHub.x + 0.02, WheelHub.y - 0.04, 0.24};
        MacroCam.VerticalFieldOfView = 0.58;

        std::vector<uint8_t> Pixels;
        if (!Viewport.Refresh(MacroCam, Env, FastRender, 1.0 / 60.0, ChaseExtent, &Diagnostic) ||
            !Viewport.ReadPresented(Pixels))
        {
            std::fprintf(stderr, "Macro wheel render failed (%s): %s\n", ModeSlugs[M], Diagnostic.c_str());
            return 1;
        }
        const std::filesystem::path MacroPath = OutDir / (std::string("vulkan_wheel_macro_") + ModeSlugs[M] + ".png");
        stbi_write_png(MacroPath.string().c_str(),
                       static_cast<int>(ChaseExtent[0]),
                       static_cast<int>(ChaseExtent[1]),
                       4,
                       Pixels.data(),
                       static_cast<int>(ChaseExtent[0] * 4u));
    }

    // Also render one full ReSTIR Global Illumination + PlanetaryAtmosphere sky beauty frame of the vehicle!
    {
        ViewportRendering GiRender{};
        GiRender.AnalyticIllumination = false;
        GiRender.GlobalIllumination   = true;
        GiRender.TemporalAntiAliasing = true;
        GiRender.ExposureCompensation = 0.15;

        ViewportCamera BeautyCam{};
        BeautyCam.Position            = {-32.2, 3.3, 1.42};
        BeautyCam.Target              = {-36.2, 0.1, 0.58};
        BeautyCam.VerticalFieldOfView = 0.82;

        for (int Warm = 0; Warm < 6; ++Warm)
        {
            Viewport.Refresh(BeautyCam, Env, GiRender, 1.0 / 60.0, ChaseExtent, &Diagnostic);
        }
        std::vector<uint8_t> Pixels;
        if (Viewport.ReadPresented(Pixels))
        {
            stbi_write_png((OutDir / "vulkan_restir_vehicle_beauty.png").string().c_str(),
                           static_cast<int>(ChaseExtent[0]),
                           static_cast<int>(ChaseExtent[1]),
                           4,
                           Pixels.data(),
                           static_cast<int>(ChaseExtent[0] * 4u));
        }
    }

    std::printf("Capture complete -> %s\n", OutDir.string().c_str());
    return 0;
}
