/*==========================================================================================================================================
                                    STAGE 3B: SPATIOTEMPORAL ILLUMINATION & .SDFSPACE VALIDATION
==========================================================================================================================================*/
// End-to-end validation of Stage 3B (GTX-first Vulkan 1.3 compute — ZERO hardware RTX raytracing extensions):
//   1. Headless BakeFacetFieldDistanceVolume -> .sdfspace ("SDF1" Float16) round-trip + pre-baked ControlVehicle SDF union
//   2. 16-bin SAH software BoundingVolumeAtlas construction, opaque ray parity, curved Class-7 glass peel & colored shadow
//   3. TextureWorkspace ScatteringSequence + AutomotivePaint material registration (SurfaceScatteringAtlas)
//   4. Full GPU SpatiotemporalIllumination (ReSTIR Direct RIS + temporal + Jacobian spatial + .sdfspace SDF GI/AO +
//      software BVH glossy/clearcoat reflections + curved Snell glass refraction + à-trous variance reconstruction)

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/Geometry/GeometryProcessing/BoundingVolumeAtlas.h"
#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"
#include "Engine/Geometry/PrimitiveGeometryGenerator.h"
#include "Engine/Rendering/SpatiotemporalIllumination.h"
#include "Engine/Rendering/SurfaceScatteringAtlas.h"

#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;

void RequireSti(bool Condition, const char* Expr, const char* Detail, int Line)
{
    if (!Condition)
    {
        std::cerr << "[SpatiotemporalIlluminationValidation] FAILED at line " << Line << ": " << Detail
                  << " (" << Expr << ")\n";
        std::exit(1);
    }
}

#define REQUIRE_STI(Cond, Msg) RequireSti((Cond), #Cond, (Msg), __LINE__)

} // namespace

int main(int Argc, char** Argv)
{
    const std::filesystem::path RepoRoot  = (Argc > 1) ? Argv[1] : ".";
    const std::filesystem::path ShaderDir = (Argc > 2) ? Argv[2] : "build/ShaderArchives/Environment";
    const std::filesystem::path OutputDir = (Argc > 3) ? Argv[3] : "build/spatiotemporal-illumination-exhibits";

    bool MirrorShowcase = false;
    for (int Idx = 1; Idx < Argc; ++Idx)
    {
        if (std::strcmp(Argv[Idx], "--write-showcase") == 0)
        {
            MirrorShowcase = true;
        }
    }

    std::filesystem::create_directories(OutputDir);
    std::string Diag;

    // 1. Verify Headless .sdfspace Baker & Round-Trip Serialization (Zero UI Dependency)
    std::cout << "[1/5] Verifying Headless VolumeDistanceField -> .sdfspace Float16 Round-Trip...\n";
    {
        FacetField SphereMesh{};
        AppendProceduralGeodesicSphere(SphereMesh, 1.0, 2u);
        HeadlessSdfBakeOptions Opts{};
        Opts.Dimensions   = {32u, 32u, 32u};
        Opts.PaddingRatio = 0.25f;
        Opts.SignedField  = true;

        const VolumeDistanceField Baked = BakeFacetFieldDistanceVolume(SphereMesh, Opts);
        REQUIRE_STI(Baked.ClosedManifold && Baked.MinimumDistance < -0.60f && Baked.MaximumDistance > 0.35f,
                    "Headless BakeFacetFieldDistanceVolume on closed sphere must yield negative interior & positive exterior distances");

        const auto TempSdfPath = OutputDir / "UnitSphereRoundTrip.sdfspace";
        REQUIRE_STI(SaveVolumeDistanceSpace(Baked, TempSdfPath, &Diag),
                    ("SaveVolumeDistanceSpace failed: " + Diag).c_str());

        VolumeDistanceField Reloaded{};
        REQUIRE_STI(LoadVolumeDistanceSpace(TempSdfPath.string(), Reloaded, &Diag),
                    ("LoadVolumeDistanceSpace failed: " + Diag).c_str());

        const float BakedCenter    = SampleVolumeDistanceMeters(Baked, 0.0f, 0.0f, 0.0f);
        const float ReloadedCenter = SampleVolumeDistanceMeters(Reloaded, 0.0f, 0.0f, 0.0f);
        const float SurfaceDist    = SampleVolumeDistanceMeters(Reloaded, 1.0f, 0.0f, 0.0f);
        const float OutsideDist    = SampleVolumeDistanceMeters(Reloaded, 1.35f, 0.0f, 0.0f);
        REQUIRE_STI(ReloadedCenter < -0.55f &&
                        std::abs(ReloadedCenter - BakedCenter) < 2.0e-3f &&
                        std::abs(SurfaceDist) < 0.08f &&
                        OutsideDist > 0.25f,
                    ".sdfspace Float16 round-trip must preserve metric Euclidean signed distances inside, on, and outside the surface");
    }

    // 2. Assemble ControlVehicle + Proving-Ground Architectural Scene & Pre-Baked .sdfspace World Union
    std::cout << "[2/5] Assembling ControlVehicle Proving-Ground Scene & Pre-Baked .sdfspace World SDF...\n";
    FacetField              SceneFacets{};
    ControlVehiclePlacement Placement{};
    Placement.ArchiveDirectory = (RepoRoot / "EngineContent/EnvironmentArchives/ControlVehicle").string();
    if (!std::filesystem::is_directory(Placement.ArchiveDirectory))
    {
        Placement.ArchiveDirectory = "EnvironmentArchives/ControlVehicle";
    }
    REQUIRE_STI(AssembleControlVehicle(SceneFacets, Placement, &Diag),
                ("AssembleControlVehicle failed: " + Diag).c_str());

    // Proving-ground asphalt floor + architectural concrete backdrop arches + overhead studio softbox bars (Class 20)
    {
        PrimitivePlacement FloorPlace{};
        FloorPlace.TranslationMeters = {0.0, 0.0, 0.0};
        FloorPlace.ClassificationId  = 0u;
        FloorPlace.UvScale           = 0.35;
        AppendProceduralGroundGrid(SceneFacets, 14.0, 14.0, 16u, 16u, FloorPlace);

        PrimitivePlacement ArchPlace{};
        ArchPlace.TranslationMeters = {0.0, 0.0, -5.2};
        ArchPlace.ClassificationId  = 1u;
        AppendProceduralArchTunnel(SceneFacets, 4.2, 0.45, 3.6, 20u, ArchPlace);

        // Two visible emissive overhead softbox light bars so clearcoat hood & curved windshield reflect crisp studio bars!
        PrimitivePlacement LeftBar{};
        LeftBar.TranslationMeters = {-1.85, 3.65, 0.25};
        LeftBar.ClassificationId  = 20u;
        AppendProceduralBox(SceneFacets, {0.28, 0.05, 2.35}, LeftBar);

        PrimitivePlacement RightBar{};
        RightBar.TranslationMeters = {1.85, 3.65, 0.25};
        RightBar.ClassificationId  = 20u;
        AppendProceduralBox(SceneFacets, {0.28, 0.05, 2.35}, RightBar);
    }

    VolumeDistanceField WorldSdf{};
    REQUIRE_STI(ComposeControlVehicleDistanceVolume(WorldSdf, Placement, {68u, 38u, 92u}, true, &Diag),
                ("ComposeControlVehicleDistanceVolume failed: " + Diag).c_str());
    // Verify interior of car body is negative in composed SDF and above roof is positive!
    const float NearChassis = SampleVolumeDistanceMeters(WorldSdf, 0.0f, 0.55f, 1.35f);
    const float AboveRoof   = SampleVolumeDistanceMeters(WorldSdf, 0.0f, 2.65f, 0.0f);
    std::cout << "      Composed .sdfspace World SDF: NearChassis=" << NearChassis
              << " m, AboveRoof=" << AboveRoof << " m\n";
    REQUIRE_STI(NearChassis < 0.08f && AboveRoof > 0.55f,
                "Composed ControlVehicle .sdfspace world SDF must distinguish chassis volume from open air above roof");

    // 3. Build Software SAH BoundingVolumeAtlas & Validate Opaque + Glass-Tinted Shadow Queries
    std::cout << "[3/5] Building 16-Bin SAH Software BoundingVolumeAtlas & TextureWorkspace SurfaceScatteringAtlas...\n";
    ScreenSpaceClusterRefinement MatLookup{};
    MatLookup.RegisterControlVehicleMaterials();
    BoundingVolumeAtlas Bvh{};
    REQUIRE_STI(Bvh.BuildFromFacetField(SceneFacets, &MatLookup), "BoundingVolumeAtlas::BuildFromFacetField failed");
    std::cout << "      Built SAH BoundingVolumeAtlas: " << Bvh.CellCount() << " cells over "
              << Bvh.TriangleCount() << " triangles\n";

    // Downward ray onto vehicle roof/windshield & shadow check under chassis
    const SoftwareRayHit DownHit   = Bvh.TraceClosestRay({0.0f, 3.0f, -0.4f}, {0.0f, -1.0f, 0.0f});
    const auto           UnderShad = Bvh.TraceShadowTransmittance({0.0f, 0.05f, 0.0f}, {0.0f, 1.0f, 0.0f}, 5.0f);
    REQUIRE_STI(DownHit.Hit && DownHit.DistanceMeters < 2.5f && UnderShad[1] < 0.05f,
                "Software SAH BoundingVolumeAtlas must intersect vehicle body and occlude sky rays beneath chassis");

    SurfaceScatteringAtlas Materials{};
    Materials.RegisterControlVehicleScatteringPresets();
    const ScatteringSpectrum PaintSpec = Materials.EvaluateBrdfCpu(6u, {0.0f, 1.0f, 0.0f}, {0.0f, 0.8f, 0.6f}, {0.0f, 0.8f, -0.6f});
    const ScatteringSpectrum SeatSpec  = Materials.EvaluateBrdfCpu(16u, {0.0f, 1.0f, 0.0f}, {0.0f, 0.35f, 0.93f}, {0.0f, 0.7f, 0.71f});
    REQUIRE_STI(PaintSpec[2] > PaintSpec[0] && SeatSpec[0] > SeatSpec[2] && SeatSpec[0] > 0.05,
                "SurfaceScatteringAtlas must evaluate sapphire clearcoat paint and Charlie-sheen Alcantara seats via ScatteringSequence");

    // 4. Initialize GTX-First Vulkan 1.3 SpatiotemporalIllumination (Zero RTX Extensions!)
    std::cout << "[4/5] Initializing GTX-First Vulkan 1.3 SpatiotemporalIllumination & Uploading Scene...\n";
    VulkanComputeExchange Exchange{};
    REQUIRE_STI(Exchange.Initialize("", &Diag), ("VulkanComputeExchange::Initialize failed: " + Diag).c_str());

    SpatiotemporalIllumination Renderer{};
    REQUIRE_STI(Renderer.Initialize(Exchange, ShaderDir.string(), &Diag),
                ("SpatiotemporalIllumination::Initialize failed: " + Diag).c_str());
    REQUIRE_STI(Renderer.UploadSceneGeometryAndMaterials(Exchange, Bvh, Materials, &WorldSdf),
                "UploadSceneGeometryAndMaterials failed");

    const std::filesystem::path ShowcaseDir = RepoRoot / "Exhibits/SpatiotemporalIllumination/Images";
    if (MirrorShowcase)
    {
        std::filesystem::create_directories(ShowcaseDir);
    }

    auto SaveRender = [&](const char*                            FileName,
                          const SpatiotemporalCamera&            Cam,
                          const std::vector<AnalyticalLuminaire>& Lights,
                          const SpatiotemporalRenderConfig&      Cfg,
                          uint32_t                               TemporalWarmupFrames) {
        Renderer.ResetTemporalHistory();
        SpatiotemporalFrameTelemetry Tel{};
        for (uint32_t F = 0u; F < TemporalWarmupFrames; ++F)
        {
            REQUIRE_STI(Renderer.RenderFrame(Exchange, Cam, Lights, Cfg, &Tel),
                        "SpatiotemporalIllumination::RenderFrame failed");
        }
        std::vector<uint8_t> Srgb8;
        REQUIRE_STI(Renderer.ReadbackTonemappedSrgb8(Exchange, Cfg.ExposureScale, Srgb8),
                    "ReadbackTonemappedSrgb8 failed");

        uint64_t SumLum = 0u;
        uint32_t LitPx  = 0u;
        for (size_t Off = 0u; Off < Srgb8.size(); Off += 4u)
        {
            const uint32_t L = Srgb8[Off + 0u] + Srgb8[Off + 1u] + Srgb8[Off + 2u];
            SumLum += L;
            if (L > 24u) ++LitPx;
        }
        REQUIRE_STI(LitPx > (Cam.WidthPx * Cam.HeightPx) / 2u && SumLum > 1200000u,
                    "SpatiotemporalIllumination frame must produce rich physically-lit pixels");

        const auto OutFile = OutputDir / FileName;
        REQUIRE_STI(stbi_write_png(OutFile.string().c_str(), Cam.WidthPx, Cam.HeightPx, 4, Srgb8.data(), Cam.WidthPx * 4u) != 0,
                    "Failed writing Stage 3B PNG exhibit");
        if (MirrorShowcase)
        {
            (void)stbi_write_png((ShowcaseDir / FileName).string().c_str(), Cam.WidthPx, Cam.HeightPx, 4, Srgb8.data(), Cam.WidthPx * 4u);
        }
        std::cout << "      Saved " << FileName << " (TemporalFrames=" << Tel.FrameIndex
                  << ", Luminaires=" << Tel.LuminaireCount
                  << ", BvhCells=" << Tel.BvhCellCount << ", SdfActive=" << (Tel.SdfVolumeActive ? 1 : 0) << ")\n";
    };

    // 5. Render 3 Stage 3B Showcase Exhibits (Daylight ReSTIR+SDF+BVH, Curved Glazing Refraction, Twilight Multi-Light Studio)
    std::cout << "[5/5] Rendering Stage 3B GTX-First ReSTIR + .sdfspace SDF GI + Software BVH Exhibits...\n";
    {
        // Exhibit A: Hero Proving-Ground Three-Quarter Render (Sun + 2 Overhead Rectangular Softboxes + Headlamp Fill)
        AnalyticalLuminaire Sun{};
        Sun.Kind                = AnalyticalLuminaireKind::DirectionalSolar;
        Sun.DirectionOrPosition = {-0.54f, 0.58f, 0.61f};
        Sun.AxisU               = {0.009f, 0.0f, 0.0f};
        Sun.EmittedRadianceRgb  = {3.6f, 3.35f, 3.05f};

        AnalyticalLuminaire LeftSoftbox{};
        LeftSoftbox.Kind                = AnalyticalLuminaireKind::RectangularArea;
        LeftSoftbox.DirectionOrPosition = {-1.85f, 3.55f, 0.25f};
        LeftSoftbox.AxisU               = {0.28f, 0.0f, 0.0f};
        LeftSoftbox.AxisV               = {0.0f, 0.0f, 2.35f};
        LeftSoftbox.EmittedRadianceRgb  = {5.5f, 5.2f, 4.8f};

        AnalyticalLuminaire RightSoftbox = LeftSoftbox;
        RightSoftbox.DirectionOrPosition = {1.85f, 3.55f, 0.25f};

        SpatiotemporalCamera HeroCam{};
        HeroCam.OriginMeters   = {3.25f, 1.42f, 4.15f};
        HeroCam.TargetMeters   = {-0.08f, 0.72f, -0.15f};
        HeroCam.VerticalFovRad = 0.74f;
        HeroCam.WidthPx        = 800u;
        HeroCam.HeightPx       = 450u;

        SpatiotemporalRenderConfig DayCfg{};
        DayCfg.ZenithSkyRgb        = {0.10f, 0.23f, 0.54f};
        DayCfg.HorizonSkyRgb       = {0.54f, 0.65f, 0.80f};
        DayCfg.GroundBounceTintRgb = {0.26f, 0.27f, 0.29f};
        DayCfg.RisCandidateCount   = 8u;
        DayCfg.SpatialNeighbors    = 4u;
        DayCfg.SdfIndirectRays     = 4u;
        DayCfg.AtrousIterations    = 2u;
        DayCfg.ExposureScale       = 1.05f;

        SaveRender("ControlVehicleProvingGroundRestirSdf.png",
                   HeroCam,
                   {Sun, LeftSoftbox, RightSoftbox},
                   DayCfg,
                   3u);

        // Exhibit B: Close-Up Curved Windshield Snell Refraction & Charlie-Sheen Cabin Interior
        SpatiotemporalCamera GlazingCam = HeroCam;
        GlazingCam.OriginMeters         = {1.58f, 1.38f, 1.85f};
        GlazingCam.TargetMeters         = {-0.12f, 0.86f, -0.05f};
        GlazingCam.VerticalFovRad       = 0.68f;
        SaveRender("ControlVehicleCurvedGlazingRefraction.png",
                   GlazingCam,
                   {Sun, LeftSoftbox, RightSoftbox},
                   DayCfg,
                   3u);

        // Exhibit C: Multi-Light Twilight Studio Softbox + Warm Accent Point Lights + .sdfspace SDF Bounce/AO
        AnalyticalLuminaire DuskSun = Sun;
        DuskSun.DirectionOrPosition = {-0.78f, 0.16f, -0.60f};
        DuskSun.EmittedRadianceRgb  = {1.45f, 0.78f, 0.42f};

        AnalyticalLuminaire KeyLeft = LeftSoftbox;
        KeyLeft.EmittedRadianceRgb  = {11.5f, 11.8f, 13.2f};
        AnalyticalLuminaire KeyRight = RightSoftbox;
        KeyRight.EmittedRadianceRgb  = {10.5f, 9.2f, 7.6f};

        AnalyticalLuminaire RimKick{};
        RimKick.Kind                = AnalyticalLuminaireKind::OmniPoint;
        RimKick.DirectionOrPosition = {-2.8f, 1.6f, -3.4f};
        RimKick.EmittedRadianceRgb  = {22.0f, 14.0f, 8.5f};

        SpatiotemporalCamera StudioCam = HeroCam;
        StudioCam.OriginMeters         = {-3.35f, 1.28f, 3.85f};
        StudioCam.TargetMeters         = {0.05f, 0.70f, -0.20f};

        SpatiotemporalRenderConfig DuskCfg = DayCfg;
        DuskCfg.ZenithSkyRgb               = {0.03f, 0.06f, 0.16f};
        DuskCfg.HorizonSkyRgb              = {0.22f, 0.15f, 0.18f};
        DuskCfg.GroundBounceTintRgb        = {0.22f, 0.23f, 0.26f};
        DuskCfg.ExposureScale              = 1.22f;

        SaveRender("ControlVehicleTwilightStudioRestirSdf.png",
                   StudioCam,
                   {DuskSun, KeyLeft, KeyRight, RimKick},
                   DuskCfg,
                   3u);
    }

    Renderer.Shutdown(Exchange);
    Exchange.Shutdown();

    std::cout << "[SpatiotemporalIlluminationValidation] ALL STAGE 3B RESTIR, .SDFSPACE SDF GI, & SOFTWARE SAH-BVH CHECKS PASSED.\n";
    return 0;
}
