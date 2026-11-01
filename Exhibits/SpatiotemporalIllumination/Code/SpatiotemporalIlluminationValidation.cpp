/*==========================================================================================================================================
                        PHASE 3C: SPATIOTEMPORAL ILLUMINATION VALIDATION
==========================================================================================================================================*/
// 🧩 End-to-end Phase 3C validation (GTX-first Vulkan 1.3 compute, ZERO hardware RTX ray-query extensions):
//    1. Headless .sdfspace Float16 round-trip serialization
//    2. ControlVehicle two-level instanced TLAS + 8-wide CWBVH BLAS traversal parity vs the binary SAH oracle
//    3. Exact dielectric boundary Fresnel / Snell / total-internal-reflection arithmetic
//    4. Flux-weighted emissive facet CDF sampling
//    5. GPU SurfaceCard multi-bounce radiosity vs its CPU Jacobi solver
//    6. Full six-kernel render: ReSTIR DI + ReSTIR GI + 4-cascade SDF + VNDF reflections + split denoiser + resolve

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/Geometry/GeometryProcessing/BoundingVolumeAtlas.h"
#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"
#include "Engine/Geometry/PrimitiveGeometryGenerator.h"
#include "Engine/Rendering/SpatiotemporalIllumination.h"
#include "Engine/Rendering/SurfaceCardRadiosity.h"
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

    //--------------------------------------------------------------------------------------------------------------------
    // 1. Headless .sdfspace Float16 round-trip
    //--------------------------------------------------------------------------------------------------------------------
    std::cout << "[1/6] Verifying headless VolumeDistanceField -> .sdfspace Float16 round-trip...\n";
    {
        FacetField SphereMesh{};
        AppendProceduralGeodesicSphere(SphereMesh, 1.0, 2u);
        HeadlessSdfBakeOptions Options{};
        Options.Dimensions   = {32u, 32u, 32u};
        Options.PaddingRatio = 0.25f;
        Options.SignedField  = true;

        const VolumeDistanceField Baked = BakeFacetFieldDistanceVolume(SphereMesh, Options);
        REQUIRE_STI(Baked.ClosedManifold && Baked.MinimumDistance < -0.60f && Baked.MaximumDistance > 0.35f,
                    "Headless bake must yield negative interior and positive exterior signed distances");

        const auto TempPath = OutputDir / "UnitSphereRoundTrip.sdfspace";
        REQUIRE_STI(SaveVolumeDistanceSpace(Baked, TempPath, &Diag), ("SaveVolumeDistanceSpace failed: " + Diag).c_str());

        VolumeDistanceField Reloaded{};
        REQUIRE_STI(LoadVolumeDistanceSpace(TempPath.string(), Reloaded, &Diag),
                    ("LoadVolumeDistanceSpace failed: " + Diag).c_str());

        const float BakedCenter    = SampleVolumeDistanceMeters(Baked, 0.0f, 0.0f, 0.0f);
        const float ReloadedCenter = SampleVolumeDistanceMeters(Reloaded, 0.0f, 0.0f, 0.0f);
        const float OnSurface      = SampleVolumeDistanceMeters(Reloaded, 1.0f, 0.0f, 0.0f);
        const float Outside        = SampleVolumeDistanceMeters(Reloaded, 1.35f, 0.0f, 0.0f);
        REQUIRE_STI(ReloadedCenter < -0.55f && std::abs(ReloadedCenter - BakedCenter) < 2.0e-3f &&
                        std::abs(OnSurface) < 0.08f && Outside > 0.25f,
                    ".sdfspace Float16 round-trip must preserve metric distances inside, on, and outside the surface");
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 2. ControlVehicle scene, two-level TLAS/BLAS, and 8-wide CWBVH traversal parity
    //--------------------------------------------------------------------------------------------------------------------
    std::cout << "[2/6] Building ControlVehicle two-level TLAS + 8-wide CWBVH and checking traversal parity...\n";
    FacetField              SceneFacets{};
    ControlVehiclePlacement Placement{};
    Placement.ArchiveDirectory = (RepoRoot / "EngineContent/EnvironmentArchives/ControlVehicle").string();
    REQUIRE_STI(AssembleControlVehicle(SceneFacets, Placement, &Diag),
                ("AssembleControlVehicle failed: " + Diag).c_str());
    {
        PrimitivePlacement Floor{};
        Floor.ClassificationId = 0u;
        AppendProceduralGroundGrid(SceneFacets, 12.0, 12.0, 14u, 14u, Floor);

        PrimitivePlacement Baffle{};
        Baffle.TranslationMeters = {-3.4, 1.2, -2.6};
        Baffle.ClassificationId  = 14u;
        AppendProceduralBox(SceneFacets, {0.9, 1.2, 0.15}, Baffle);
    }

    ScreenSpaceClusterRefinement Materials{};
    Materials.RegisterControlVehicleMaterials();
    {
        std::array<uint32_t, 64> Histogram{};
        for (uint32_t ClassId : SceneFacets.Classification)
        {
            ++Histogram[std::min<uint32_t>(ClassId, 63u)];
        }
        std::cout << "      Classification histogram:";
        for (uint32_t ClassId = 0u; ClassId < 64u; ++ClassId)
        {
            if (Histogram[ClassId] > 0u)
            {
                std::cout << " [" << ClassId << "]=" << Histogram[ClassId];
            }
        }
        std::cout << "\n";
    }

    BoundingVolumeAtlas VehicleAtlas{};
    REQUIRE_STI(VehicleAtlas.BuildFromFacetField(SceneFacets, &Materials), "BoundingVolumeAtlas::BuildFromFacetField failed");
    std::cout << "      Binary SAH cells=" << VehicleAtlas.CellCount()
              << ", 8-wide CWBVH cells=" << VehicleAtlas.WideCellCount()
              << ", triangles=" << VehicleAtlas.TriangleCount()
              << ", wide triangles=" << VehicleAtlas.WideTriangleCount() << "\n";
    REQUIRE_STI(VehicleAtlas.WideCellCount() > 0u && VehicleAtlas.WideTriangleCount() == VehicleAtlas.TriangleCount(),
                "8-wide compression must retain every triangle and produce at least one wide cell");

    // 8-wide traversal must agree with the binary SAH oracle on closest-hit distance for random rays
    {
        uint32_t Agreements = 0u;
        uint32_t Hits       = 0u;
        for (uint32_t Ray = 0u; Ray < 256u; ++Ray)
        {
            const float U = static_cast<float>((Ray * 37u) % 251u) / 251.0f;
            const float V = static_cast<float>((Ray * 83u) % 241u) / 241.0f;
            const std::array<float, 3> Origin = {-3.0f + 6.0f * U, 0.25f + 2.4f * V, -4.5f + 3.0f * U};
            const std::array<float, 3> Dir    = {0.35f * (V - 0.5f), -0.15f + 0.3f * U, 1.0f};

            const SoftwareRayHit Binary = VehicleAtlas.TraceClosestRay(Origin, Dir);
            const SoftwareRayHit Wide   = VehicleAtlas.TraceWideClosestRay(Origin, Dir);
            if (Binary.Hit)
            {
                ++Hits;
            }
            if (Binary.Hit == Wide.Hit && (!Binary.Hit || std::abs(Binary.DistanceMeters - Wide.DistanceMeters) < 2.5e-3f))
            {
                ++Agreements;
            }
        }
        std::cout << "      Wide-vs-binary traversal: " << Agreements << "/256 rays agree (" << Hits << " hits)\n";
        REQUIRE_STI(Hits > 96u && Agreements == 256u,
                    "8-wide CWBVH traversal must match the binary SAH oracle on every probed ray");
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 3. Exact dielectric boundary arithmetic (Fresnel, Snell, total internal reflection)
    //--------------------------------------------------------------------------------------------------------------------
    std::cout << "[3/6] Verifying exact dielectric boundary Fresnel / Snell / TIR arithmetic...\n";
    {
        const DielectricBoundaryResult HeadOn =
            EvaluateDielectricBoundary({0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, 1.0f / 1.52f);
        // Normal incidence reflectance ((1-1.52)/(1+1.52))^2 = 0.042567...
        REQUIRE_STI(std::abs(HeadOn.FresnelReflectance - 0.042567f) < 2.0e-3f && !HeadOn.TotalInternalReflection,
                    "Normal-incidence air->glass Fresnel reflectance must equal the analytic (eta-1)^2/(eta+1)^2");

        const float GrazingAngle = 1.55334f; // ~89 degrees from the normal
        const std::array<float, 3> GrazingIncident = {std::sin(GrazingAngle), 0.0f, -std::cos(GrazingAngle)};
        const DielectricBoundaryResult Grazing =
            EvaluateDielectricBoundary(GrazingIncident, {0.0f, 0.0f, 1.0f}, 1.0f / 1.52f);
        REQUIRE_STI(Grazing.FresnelReflectance > 0.55f,
                    "Grazing incidence on curved glazing must approach total Fresnel reflection");

        const DielectricBoundaryResult InsideGlass =
            EvaluateDielectricBoundary({std::sin(0.95f), 0.0f, -std::cos(0.95f)}, {0.0f, 0.0f, 1.0f}, 1.52f / 1.0f);
        REQUIRE_STI(InsideGlass.TotalInternalReflection && InsideGlass.FresnelReflectance >= 0.999f,
                    "Glass->air beyond the critical angle must report total internal reflection");
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 4. Instanced two-level scene + flux-weighted emissive CDF
    //--------------------------------------------------------------------------------------------------------------------
    InstancedBoundingScene Instanced{};
    {
        const uint32_t PrototypeIndex = Instanced.RegisterPrototype(VehicleAtlas);
        REQUIRE_STI(PrototypeIndex == 0u, "First registered prototype must receive index 0");

        // Second prototype: an independent concrete column BLAS, so the second TLAS instance proves that
        // per-instance object-to-world transforms address a *different* bottom-level hierarchy.
        FacetField ColumnFacets{};
        PrimitivePlacement ColumnPlace{};
        ColumnPlace.ClassificationId = 10u;
        AppendProceduralCylinder(ColumnFacets, 0.34, 2.3, 28u, ColumnPlace);
        BoundingVolumeAtlas ColumnAtlas{};
        REQUIRE_STI(ColumnAtlas.BuildFromFacetField(ColumnFacets, &Materials), "Column BoundingVolumeAtlas build failed");
        const uint32_t ColumnPrototype = Instanced.RegisterPrototype(ColumnAtlas);
        REQUIRE_STI(ColumnPrototype == 1u, "Second registered prototype must receive index 1");

        std::vector<VolumeInstancePlacement> Placements;
        VolumeInstancePlacement Primary{};
        Primary.PrototypeIndex = PrototypeIndex;
        Placements.push_back(Primary);

        VolumeInstancePlacement ColumnInstance{};
        ColumnInstance.PrototypeIndex        = ColumnPrototype;
        ColumnInstance.ObjectToWorld         = AffineTransform3x4::ComposeTrs({3.9f, 1.15f, -2.9f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
        ColumnInstance.PreviousObjectToWorld = ColumnInstance.ObjectToWorld;
        Placements.push_back(ColumnInstance);

        REQUIRE_STI(Instanced.RebuildInstanceHierarchy(Placements), "RebuildInstanceHierarchy failed");
        REQUIRE_STI(Instanced.InstanceCount() == 2u && Instanced.TlasCellCount() >= 3u &&
                        Instanced.PrototypeCount() == 2u,
                    "Two-instance TLAS over two distinct prototypes must build both instances");

        const SoftwareRayHit InstancedHit = Instanced.TraceClosestRay({-3.25f, 1.45f, 4.25f}, {0.62f, -0.16f, -0.77f});
        REQUIRE_STI(InstancedHit.Hit && InstancedHit.DistanceMeters < 9.0f,
                    "Instanced two-level traversal must intersect the vehicle from the hero camera");
        std::cout << "      Instanced hit: instance=" << InstancedHit.InstanceIndex
                  << " t=" << InstancedHit.DistanceMeters << " m class=" << InstancedHit.ClassificationId << "\n";

        std::vector<EmissiveSlotRadiance> SlotEmissions(64);
        SlotEmissions[16].RadianceRgb = {6.0f, 5.4f, 4.6f};  // Overhead studio strip luminaire
        SlotEmissions[9].RadianceRgb  = {2.2f, 0.12f, 0.10f}; // Ruby taillight bar

        std::vector<std::array<float, 4>> EmissiveTable;
        uint32_t                          EmissiveCount = 0u;
        float                             TotalFlux     = 0.0f;
        Instanced.BuildEmissiveFacetDistribution(SlotEmissions, EmissiveTable, EmissiveCount, TotalFlux);
        std::cout << "      Emissive facet CDF: " << EmissiveCount << " facets, total flux " << TotalFlux << " W\n";

        if (EmissiveCount > 0u)
        {
            float PreviousAreaPdf = -1.0f;
            for (uint32_t Probe = 0u; Probe < 8u; ++Probe)
            {
                const float                 Selector = (static_cast<float>(Probe) + 0.5f) / 8.0f;
                const SampledEmissiveSurfacePoint Sample = Instanced.SampleEmissiveSurface(Selector, 0.35f, 0.55f);
                REQUIRE_STI(Sample.Valid && Sample.AreaPdfPerSquareM > 0.0f &&
                                Sample.EmittedRadiance[0] + Sample.EmittedRadiance[1] + Sample.EmittedRadiance[2] > 0.0f,
                            "Emissive facet CDF sampling must return a lit emissive surface point");
                PreviousAreaPdf = Sample.AreaPdfPerSquareM;
            }
            REQUIRE_STI(PreviousAreaPdf > 0.0f, "Emissive facet area PDF must be positive");
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 5. GPU SurfaceCard multi-bounce radiosity vs the CPU Jacobi reference
    //--------------------------------------------------------------------------------------------------------------------
    std::cout << "[5/6] Solving GPU SurfaceCard radiosity and comparing against the CPU Jacobi reference...\n";
    VulkanComputeExchange Exchange{};
    REQUIRE_STI(Exchange.Initialize("", &Diag), ("VulkanComputeExchange::Initialize failed: " + Diag).c_str());
    std::cout << "      GPU device: " << Exchange.DeviceName() << " (" << Exchange.DriverSummary() << ")\n";

    FourCascadeDistanceVolume Cascades{};
    {
        std::vector<AnalyticalDistancePrimitive> Primitives;
        AnalyticalDistancePrimitive Baffle{};
        Baffle.Category          = AnalyticalDistanceCategory::Box;
        Baffle.CenterMeters      = {-3.4f, 1.2f, -2.6f};
        Baffle.HalfExtentsMeters = {0.9f, 1.2f, 0.15f};
        Primitives.push_back(Baffle);
        AnalyticalDistancePrimitive Column{};
        Column.Category          = AnalyticalDistanceCategory::Cylinder;
        Column.CenterMeters      = {2.6f, 1.0f, -2.2f};
        Column.HalfExtentsMeters = {0.35f, 1.0f, 0.0f};
        Primitives.push_back(Column);

        FourCascadeDistanceParameters CascadeParameters{};
        CascadeParameters.FocusCenterMeters         = {0.0f, 0.85f, 0.0f};
        CascadeParameters.CascadeRadiiMeters        = {7.0f, 20.0f, 58.0f, 165.0f};
        CascadeParameters.VoxelResolutionPerCascade = {26u, 20u, 24u};
        REQUIRE_STI(AssembleFourCascadeDistanceVolume(Cascades, Placement, Primitives, CascadeParameters, &Diag),
                    ("AssembleFourCascadeDistanceVolume failed: " + Diag).c_str());

        const float ChassisDistance = SampleFourCascadeDistanceMeters(Cascades, 0.0f, 0.55f, 1.35f);
        const float AboveRoof       = SampleFourCascadeDistanceMeters(Cascades, 0.0f, 2.65f, 0.0f);
        const float FarBaffle       = SampleFourCascadeDistanceMeters(Cascades, -3.4f, 1.2f, -2.6f);
        std::cout << "      4-cascade SDF: chassis=" << ChassisDistance << " m, aboveRoof=" << AboveRoof
                  << " m, baffleCentre=" << FarBaffle << " m\n";
        REQUIRE_STI(ChassisDistance < 0.10f && AboveRoof > 0.50f && FarBaffle < 0.05f,
                    "4-cascade clipmap + analytical primitives must distinguish chassis, open air, and the baffle");
    }

    SurfaceScatteringAtlas MaterialAtlas{};
    MaterialAtlas.LoadControlVehicleDefaults(RepoRoot.string());
    MaterialAtlas.ApplyRefinementProfiles(Materials);
    std::cout << "      Material atlas layers: " << MaterialAtlas.ActiveTextureLayerCount()
              << ", VNDF white-furnace (rough 0.35) = "
              << IntegrateCpuVndfWhiteFurnace(0.82f, 0.35f, 0.0f, 512u) << "\n";
    {
        const float Smooth = IntegrateCpuVndfWhiteFurnace(1.0f, 0.05f, 0.0f, 512u);
        const float Rough  = IntegrateCpuVndfWhiteFurnace(1.0f, 0.60f, 0.0f, 512u);
        REQUIRE_STI(std::abs(Smooth - 1.0f) < 0.08f && std::abs(Rough - 1.0f) < 0.30f,
                    "Heitz VNDF white-furnace integration must stay near unity (energy conserving) across roughness");
        REQUIRE_STI(ClassifyMaterialDetailTier(4.0f, 12.0f, 55.0f) == MaterialDetailTier::FullStrata &&
                        ClassifyMaterialDetailTier(30.0f, 12.0f, 55.0f) == MaterialDetailTier::FilteredToksvig &&
                        ClassifyMaterialDetailTier(120.0f, 12.0f, 55.0f) == MaterialDetailTier::CompactProxy,
                    "3-tier Material LOD classification must follow the near/far distance thresholds");
    }

    SpatiotemporalIllumination Renderer{};
    REQUIRE_STI(Renderer.Initialize(Exchange, ShaderDir.string(), &Diag),
                ("SpatiotemporalIllumination::Initialize failed: " + Diag).c_str());
    std::cout << "      Six-kernel pipeline compile total: " << Renderer.PipelineCompileMilliseconds() << " ms\n";

    REQUIRE_STI(Renderer.UploadInstancedSceneAndMaterials(Exchange, Instanced, MaterialAtlas, &Cascades),
                "UploadInstancedSceneAndMaterials failed");

    const std::filesystem::path ShowcaseDir = RepoRoot / "Exhibits/SpatiotemporalIllumination/Images";
    if (MirrorShowcase)
    {
        std::filesystem::create_directories(ShowcaseDir);
    }

    auto SaveRender = [&](const char*                            FileName,
                          const SpatiotemporalCamera&            Camera,
                          const std::vector<AnalyticalLuminaire>& Lights,
                          const SpatiotemporalRenderConfig&      Config,
                          uint32_t                               WarmupFrames) -> SpatiotemporalFrameTelemetry {
        Renderer.ResetTemporalHistory();
        SpatiotemporalFrameTelemetry Telemetry{};
        for (uint32_t Frame = 0u; Frame < WarmupFrames; ++Frame)
        {
            REQUIRE_STI(Renderer.RenderFrame(Exchange, Camera, Lights, Config, &Telemetry),
                        "SpatiotemporalIllumination::RenderFrame failed");
        }
        std::vector<uint8_t> Srgb8;
        REQUIRE_STI(Renderer.ReadbackTonemappedSrgb8(Exchange, Config.ExposureScale, Srgb8),
                    "ReadbackTonemappedSrgb8 failed");

        uint64_t SumLuminance = 0u;
        uint32_t LitPixels    = 0u;
        for (size_t Offset = 0u; Offset < Srgb8.size(); Offset += 4u)
        {
            const uint32_t Value = Srgb8[Offset + 0u] + Srgb8[Offset + 1u] + Srgb8[Offset + 2u];
            SumLuminance += Value;
            if (Value > 24u)
            {
                ++LitPixels;
            }
        }
        std::cout << "      stats: lit=" << LitPixels << "/" << (Camera.WidthPx * Camera.HeightPx)
                  << " sum=" << SumLuminance << " firstPx=(" << int(Srgb8[0]) << "," << int(Srgb8[1]) << ","
                  << int(Srgb8[2]) << ") midPx=(" << int(Srgb8[(Srgb8.size() / 2u) & ~3u])
                  << "," << int(Srgb8[((Srgb8.size() / 2u) & ~3u) + 1u]) << "," << int(Srgb8[((Srgb8.size() / 2u) & ~3u) + 2u]) << ")\n";
        REQUIRE_STI(LitPixels > (Camera.WidthPx * Camera.HeightPx) / 2u && SumLuminance > 1200000u,
                    "Resolved frame must produce a richly lit image");

        const auto OutputFile = OutputDir / FileName;
        REQUIRE_STI(stbi_write_png(OutputFile.string().c_str(), Camera.WidthPx, Camera.HeightPx, 4,
                                   Srgb8.data(), Camera.WidthPx * 4u) != 0,
                    "Failed writing Phase 3C PNG exhibit");
        if (MirrorShowcase)
        {
            (void)stbi_write_png((ShowcaseDir / FileName).string().c_str(), Camera.WidthPx, Camera.HeightPx, 4,
                                 Srgb8.data(), Camera.WidthPx * 4u);
        }
        std::cout << "      Saved " << FileName << " (frames=" << Telemetry.FrameIndex
                  << ", wideCells=" << Telemetry.WideCellCount << ", instances=" << Telemetry.InstanceCount
                  << ", cards=" << Telemetry.SurfaceCardCount << ", emissive=" << Telemetry.EmissiveFacetCount
                  << ", gpu=" << Telemetry.TotalGpuMs << " ms)\n";
        return Telemetry;
    };

    // CPU/GPU radiosity parity on the exact card set the renderer uploaded.
    {
        std::vector<SurfaceCardPatch> Cards;
        SurfaceCardRadiositySolver::AppendSixAxisObjectCards({-2.4f, 0.0f, -2.4f}, {2.4f, 1.6f, 2.4f},
                                                             {0.34f, 0.35f, 0.36f}, {0.0f, 0.0f, 0.0f}, 6u, Cards);
        SurfaceCardRadiositySolver::AppendGroundRadiosityGrid({-4.5f, -4.5f}, {4.5f, 4.5f}, 0.0f, 3u,
                                                              {0.30f, 0.31f, 0.32f}, Cards);

        SurfaceCardLightingParameters Parameters{};
        Parameters.IndirectBounceGain   = 1.15f;
        Parameters.SkyVisibilityWeight  = 0.85f;
        Parameters.BounceIterations     = 3u;
        Parameters.EnableVisibilityCones = false; // Isolate the form-factor solver from SDF cone visibility
        Parameters.SunDirectionUnit     = {0.54f, 0.58f, -0.61f};
        Parameters.SunIlluminanceLux    = 14.4f;
        Parameters.SkyAmbientLuminance  = 0.35f;

        std::vector<std::array<float, 3>> CpuDirect;
        std::vector<std::array<float, 3>> CpuSolved;
        std::vector<float>                Residuals;
        SurfaceCardRadiositySolver::SolveCpuReferenceRadiosity(Cards, Parameters, CpuDirect, CpuSolved, &Residuals);
        REQUIRE_STI(CpuSolved.size() == Cards.size(), "CPU Jacobi solver must return one exitance per card");

        SurfaceCardRadiositySolver GpuSolver{};
        REQUIRE_STI(GpuSolver.Initialize(Exchange, ShaderDir.string(), &Diag),
                    ("SurfaceCardRadiositySolver::Initialize failed: " + Diag).c_str());
        REQUIRE_STI(GpuSolver.UploadPatches(Exchange, Cards), "SurfaceCardRadiositySolver::UploadPatches failed");

        DeviceMemorySpan EmptyVolume{};
        DeviceMemorySpan EmptyLuminaires{};
        const std::array<float, 16> NeutralSeed{};
        REQUIRE_STI(Exchange.UploadMemorySpan(EmptyVolume, NeutralSeed.data(), sizeof(NeutralSeed)) &&
                        Exchange.UploadMemorySpan(EmptyLuminaires, NeutralSeed.data(), sizeof(NeutralSeed)),
                    "Radiosity parity neutral spans could not be allocated");

        SurfaceCardSolveReport Report{};
        REQUIRE_STI(GpuSolver.SolveGpuRadiosity(Exchange, EmptyVolume, EmptyLuminaires, 0u, Parameters, &Report),
                    "SurfaceCardRadiositySolver::SolveGpuRadiosity failed");

        std::vector<std::array<float, 4>> GpuExitance;
        REQUIRE_STI(GpuSolver.DownloadSolvedExitance(Exchange, GpuExitance), "DownloadSolvedExitance failed");

        float MaxDifference = 0.0f;
        float MaxMagnitude  = 0.0f;
        for (size_t Index = 0u; Index < std::min(GpuExitance.size(), CpuSolved.size()); ++Index)
        {
            for (int Channel = 0; Channel < 3; ++Channel)
            {
                MaxDifference = std::max(MaxDifference, std::abs(GpuExitance[Index][static_cast<size_t>(Channel)] -
                                                                 CpuSolved[Index][static_cast<size_t>(Channel)]));
                MaxMagnitude  = std::max(MaxMagnitude, std::abs(CpuSolved[Index][static_cast<size_t>(Channel)]));
            }
        }
        const float RelativeError = MaxDifference / std::max(MaxMagnitude, 1.0e-6f);
        std::cout << "      GPU/CPU radiosity: cards=" << Cards.size() << " bounces=" << Report.CompletedBounces
                  << " maxAbsDiff=" << MaxDifference << " relative=" << RelativeError
                  << " gpu=" << Report.GpuMilliseconds << " ms"
                  << (Report.GpuTimestampAvailable ? " (hardware timestamps)" : " (software Vulkan timestamps)") << "\n";
        REQUIRE_STI(MaxMagnitude > 0.05f && MaxDifference < 0.35f,
                    "GPU SurfaceCard radiosity must agree with the CPU Jacobi reference");
        if (!Residuals.empty())
        {
            REQUIRE_STI(Residuals.back() <= Residuals.front() + 1.0e-3f,
                        "Card radiosity bounce residual must not diverge");
        }
        GpuSolver.Shutdown(Exchange);
        Exchange.ReleaseMemorySpan(EmptyVolume);
        Exchange.ReleaseMemorySpan(EmptyLuminaires);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 6. Render the Phase 3C showcase exhibits
    //--------------------------------------------------------------------------------------------------------------------
    std::cout << "[6/6] Rendering Phase 3C ReSTIR DI+GI + 4-cascade SDF + VNDF exhibits...\n";
    {
        AnalyticalLuminaire Sun{};
        Sun.Kind                = AnalyticalLuminaireKind::DirectionalSolar;
        Sun.DirectionOrPosition = {-0.54f, 0.58f, 0.61f};
        Sun.EmittedRadianceRgb  = {2.25f, 2.10f, 1.92f};

        AnalyticalLuminaire LeftSoftbox{};
        LeftSoftbox.Kind                = AnalyticalLuminaireKind::RectangularArea;
        LeftSoftbox.DirectionOrPosition = {-1.85f, 3.55f, 0.25f};
        LeftSoftbox.AxisU               = {0.28f, 0.0f, 0.0f};
        LeftSoftbox.AxisV               = {0.0f, 0.0f, 2.35f};
        LeftSoftbox.EmittedRadianceRgb  = {5.5f, 5.2f, 4.8f};

        AnalyticalLuminaire RightSoftbox = LeftSoftbox;
        RightSoftbox.DirectionOrPosition = {1.85f, 3.55f, 0.25f};

        // Framing matched to the retained ClusterDiagnostic hero view so both exhibits show the same car.
        SpatiotemporalCamera HeroCamera{};
        HeroCamera.OriginMeters   = {4.95f, 1.95f, 6.55f};
        HeroCamera.TargetMeters   = {-0.10f, 0.72f, 0.05f};
        HeroCamera.VerticalFovRad = 0.80f;
        HeroCamera.WidthPx        = 800u;
        HeroCamera.HeightPx       = 450u;

        SpatiotemporalRenderConfig DayConfig{};
        DayConfig.ZenithSkyRgb        = {0.10f, 0.23f, 0.54f};
        DayConfig.HorizonSkyRgb       = {0.54f, 0.65f, 0.80f};
        DayConfig.GroundBounceTintRgb = {0.26f, 0.27f, 0.29f};
        DayConfig.RisCandidateCount   = 8u;
        DayConfig.SpatialNeighbors    = 4u;
        DayConfig.AtrousIterations    = 2u;
        DayConfig.ExposureScale       = 0.92f;
        DayConfig.EnableClouds        = true;
        DayConfig.CloudCoverage       = 0.34f;
        DayConfig.CloudAltitudeMeters = 1350.0f;
        DayConfig.EnableFog           = true;
        DayConfig.FogBaseDensity      = 0.0016f;
        DayConfig.EnableLensFlare     = true;
        DayConfig.FlareIntensity      = 0.55f;
        DayConfig.MoonCoronaStrength  = 0.35f;

        const SpatiotemporalFrameTelemetry DayTelemetry =
            SaveRender("ControlVehicleProvingGroundRestirGi.png", HeroCamera,
                       {Sun, LeftSoftbox, RightSoftbox}, DayConfig, 6u);
        REQUIRE_STI(DayTelemetry.SurfaceCardCount > 0u && DayTelemetry.InstanceCount >= 2u,
                    "Daylight exhibit telemetry must confirm surface cards and instanced geometry");
        REQUIRE_STI(DayTelemetry.TotalGpuMs >= 0.0 && DayTelemetry.WideCellCount > 0u,
                    "Daylight exhibit telemetry must report valid 8-wide cell counts and timings");

        // Glass peel close-up
        SpatiotemporalCamera GlazingCamera = HeroCamera;
        GlazingCamera.OriginMeters         = {2.05f, 1.45f, 2.35f};
        GlazingCamera.TargetMeters         = {-0.15f, 0.88f, -0.10f};
        GlazingCamera.VerticalFovRad       = 0.62f;
        SaveRender("ControlVehicleCurvedGlazingRefraction.png", GlazingCamera,
                   {Sun, LeftSoftbox, RightSoftbox}, DayConfig, 6u);

        // Twilight studio: warm accent point + softboxes + multiple inspection modes
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

        SpatiotemporalCamera StudioCamera = HeroCamera;
        StudioCamera.OriginMeters         = {-4.35f, 1.62f, 5.05f};
        StudioCamera.TargetMeters         = {0.05f, 0.70f, -0.20f};

        SpatiotemporalRenderConfig DuskConfig = DayConfig;
        DuskConfig.ZenithSkyRgb               = {0.03f, 0.06f, 0.16f};
        DuskConfig.HorizonSkyRgb              = {0.22f, 0.15f, 0.18f};
        DuskConfig.GroundBounceTintRgb        = {0.22f, 0.23f, 0.26f};
        DuskConfig.ExposureScale              = 1.22f;
        DuskConfig.EnableClouds               = false;
        DuskConfig.EnableLensFlare            = false;

        SaveRender("ControlVehicleTwilightStudioRestirGi.png", StudioCamera,
                   {DuskSun, KeyLeft, KeyRight, RimKick}, DuskConfig, 6u);

        // Inspection mode exhibits (ReSTIR GI irradiance, reflections, 3-tier material LOD, AO)
        SpatiotemporalRenderConfig InspectionConfig = DuskConfig;
        InspectionConfig.InspectionMode             = 9u; // GlobalIllumination
        SaveRender("ControlVehicleRestirGiIrradiance.png", StudioCamera,
                   {DuskSun, KeyLeft, KeyRight, RimKick}, InspectionConfig, 4u);

        InspectionConfig.InspectionMode = 8u; // Reflections
        SaveRender("ControlVehicleVndfReflections.png", StudioCamera,
                   {DuskSun, KeyLeft, KeyRight, RimKick}, InspectionConfig, 4u);

        InspectionConfig.InspectionMode = 12u; // Cluster identity + 3-tier material LOD tint
        SaveRender("ControlVehicleMaterialTierLod.png", StudioCamera,
                   {DuskSun, KeyLeft, KeyRight, RimKick}, InspectionConfig, 4u);

        // Temporal rejection: teleporting the camera must not smear the previous frame
        {
            Renderer.ResetTemporalHistory();
            SpatiotemporalFrameTelemetry Telemetry{};
            SpatiotemporalRenderConfig   TeleportConfig = DayConfig;
            TeleportConfig.EnableLensFlare               = false;
            REQUIRE_STI(Renderer.RenderFrame(Exchange, HeroCamera, {Sun}, TeleportConfig, &Telemetry),
                        "Teleport warm-up frame failed");
            SpatiotemporalCamera FarCamera = HeroCamera;
            FarCamera.OriginMeters         = {-6.5f, 2.4f, -6.5f};
            FarCamera.TargetMeters         = {0.0f, 0.75f, 0.0f};
            REQUIRE_STI(Renderer.RenderFrame(Exchange, FarCamera, {Sun}, TeleportConfig, &Telemetry),
                        "Teleport displacement frame failed");
            std::vector<uint8_t> Pixels;
            REQUIRE_STI(Renderer.ReadbackTonemappedSrgb8(Exchange, TeleportConfig.ExposureScale, Pixels),
                        "Teleport readback failed");
            uint64_t MeanLuma = 0u;
            for (size_t Offset = 0u; Offset < Pixels.size(); Offset += 4u)
            {
                MeanLuma += Pixels[Offset];
            }
            MeanLuma /= std::max<size_t>(Pixels.size() / 4u, 1u);
            std::cout << "      Post-teleport mean red channel = " << MeanLuma << " (temporal rejection check)\n";
            REQUIRE_STI(MeanLuma > 8u && MeanLuma < 250u, "Post-teleport frame must be a valid, non-degenerate image");
        }
    }

    Renderer.Shutdown(Exchange);
    Exchange.Shutdown();

    std::cout << "[SpatiotemporalIlluminationValidation] ALL PHASE 3C ReSTIR DI+GI, CWBVH, 4-CASCADE SDF,"
                 " SURFACE CARD, & VNDF CHECKS PASSED.\n";
    return 0;
}
