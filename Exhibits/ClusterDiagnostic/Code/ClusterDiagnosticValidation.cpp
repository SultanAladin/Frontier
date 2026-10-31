/*==========================================================================================================================================
                                         CLUSTER DIAGNOSTIC & VEHICLE VALIDATION
==========================================================================================================================================*/
// End-to-end validation of:
//  1. 9 watertight ProceduralShapes generators + binary FCT1 ControlVehicle{Body,Rim,Tyre}.facet assembly
//  2. Locked-border Garland-Heckbert QEM ClusterHierarchy DAG, curved-glass IOR boost, & Karis group-atomic cut
//  3. Two-phase opaque-only Hi-Z culling (cabin interior behind curved Class-7 glass survives)
//  4. 3-tier virtualized materials (FullStrata 64B / FilteredToksvig 24B / CompactProxy 16B) & 64 KB RAM/VRAM page telemetry
//  5. ThemeCardPalette 1:1 ThemeStructure binding (zero hardcoded green)
//  6. Editor/Development ONLY (#if FRONTIER_EDITOR) Vulkan 1.3 DiagnosticRaster + Shipping exclusion proof

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/GeometricRaster/ClusterHierarchy.h"
#include "Engine/GeometricRaster/DiagnosticRaster.h"
#include "Engine/GeometricRaster/ProceduralShapes.h"
#include "Engine/GeometricRaster/ThemeCardPalette.h"

#include <stb_image_write.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace Frontier
{
bool VerifyDiagnosticRasterExcludedInShipping() noexcept;
}

namespace
{

using namespace Frontier;

void RequireGate(bool Condition, const char* Expr, const char* Detail, int Line)
{
    if (!Condition)
    {
        std::cerr << "[ClusterDiagnosticValidation] FAILED at line " << Line << ": " << Detail
                  << " (" << Expr << ")\n";
        std::exit(1);
    }
}

#define REQUIRE_CD(Cond, Msg) RequireGate((Cond), #Cond, (Msg), __LINE__)

} // namespace

int main(int Argc, char** Argv)
{
    const std::filesystem::path RepoRoot  = (Argc > 1) ? Argv[1] : ".";
    const std::filesystem::path ShaderDir = (Argc > 2) ? Argv[2] : "build/ShaderArchives/Environment";
    const std::filesystem::path OutputDir = (Argc > 3) ? Argv[3] : "build/cluster-diagnostic-exhibits";

    bool MirrorShowcase = false;
    for (int Idx = 1; Idx < Argc; ++Idx)
    {
        if (std::strcmp(Argv[Idx], "--write-showcase") == 0)
        {
            MirrorShowcase = true;
        }
    }

    // 1. Shipping Exclusion Proof
    std::cout << "[1/5] Verifying Shipping Build Exclusion (FRONTIER_EDITOR == 0 TU)...\n";
    REQUIRE_CD(VerifyDiagnosticRasterExcludedInShipping(),
               "Shipping profile translation unit must compile with FRONTIER_EDITOR == 0");

    // 2. Procedural 3D Shape Generators + Binary FCT1 ControlVehicle Assembly
    std::cout << "[2/5] Verifying 9 Watertight Procedural Shapes & Binary FCT1 ControlVehicle Assembly...\n";
    {
        FacetField BoxF{}, SphF{}, GeoF{}, CylF{}, CapF{}, TorF{}, ConF{}, TunF{}, GrdF{};
        AppendProceduralBox(BoxF, {0.5, 0.5, 0.5});
        AppendProceduralUvSphere(SphF, 1.0, 24u, 16u);
        AppendProceduralGeodesicSphere(GeoF, 1.0, 2u);
        AppendProceduralCylinder(CylF, 0.6, 1.8, 24u);
        AppendProceduralCapsule(CapF, 0.45, 1.2, 20u, 8u);
        AppendProceduralTorus(TorF, 1.2, 0.35, 24u, 16u);
        AppendProceduralCone(ConF, 0.8, 1.6, 24u);
        AppendProceduralArchTunnel(TunF, 2.2, 0.4, 4.0, 16u);
        AppendProceduralGroundGrid(GrdF, 5.0, 5.0, 8u, 8u);

        REQUIRE_CD(BoxF.Classification.size() == 12u && SphF.Classification.size() > 400u && GeoF.Classification.size() == 320u &&
                       CylF.Classification.size() == 96u && CapF.Classification.size() > 400u && TorF.Classification.size() == 768u &&
                       ConF.Classification.size() == 48u && TunF.Classification.size() > 100u && GrdF.Classification.size() == 128u,
                   "All 9 procedural 3D shape builders must emit valid triangulated facet fields");
    }

    std::string             Diag;
    FacetField              VehicleScene{};
    ControlVehiclePlacement Placement{};
    Placement.ArchiveDirectory = (RepoRoot / "EngineContent/EnvironmentArchives/ControlVehicle").string();
    if (!std::filesystem::is_directory(Placement.ArchiveDirectory))
    {
        Placement.ArchiveDirectory = "EnvironmentArchives/ControlVehicle";
    }
    REQUIRE_CD(AssembleControlVehicle(VehicleScene, Placement, &Diag),
               ("AssembleControlVehicle failed: " + Diag).c_str());
    const uint32_t VehicleTriCount = static_cast<uint32_t>(VehicleScene.Classification.size());
    std::cout << "      ControlVehicle assembled: " << VehicleTriCount << " triangles\n";
    REQUIRE_CD(VehicleTriCount > 14000u,
               "ControlVehicle must assemble body + 4x rims + 4x tyres + interior cabin (>14,000 triangles)");

    // Add proving-grounds asphalt plinth beneath tyres (ClassificationId = 0)
    ShapePlacement PlinthPlace{};
    PlinthPlace.TranslationMeters = {0.0, 0.0, 0.0};
    PlinthPlace.ClassificationId  = 0u;
    PlinthPlace.UvScale           = 0.35;
    AppendProceduralGroundGrid(VehicleScene, 9.5, 9.5, 12u, 12u, PlinthPlace);

    // 3. Build Locked-Border QEM ClusterHierarchy + Verify Curved-Glass IOR Boost, Cut, Two-Phase Hi-Z, & 3-Tier Materials
    std::cout << "[3/5] Building QEM ClusterHierarchy DAG, 64 KB Pages, & Virtualized Material Tiers...\n";
    ClusterHierarchy Hierarchy{};
    Hierarchy.RegisterControlVehicleMaterials();
    TierBuildSpecification BuildSpec{};
    BuildSpec.TriangleLimit      = 96u;
    BuildSpec.CoarseningFraction = 0.52;
    BuildSpec.MaximumTiers       = 5u;
    REQUIRE_CD(Hierarchy.BuildFromFacetField(VehicleScene, BuildSpec), "ClusterHierarchy::BuildFromFacetField failed");

    std::cout << "      Built " << Hierarchy.TierSequence().Tiers.size() << " LOD tiers, "
              << Hierarchy.ClusterRecords().size() << " total clusters across "
              << Hierarchy.StreamingPages().size() << " x 64 KB pages\n";
    REQUIRE_CD(Hierarchy.TierSequence().Tiers.size() >= 3u && Hierarchy.StreamingPages().size() >= 2u,
               "ClusterHierarchy must construct multi-tier QEM DAG packed into 64 KB pages");

    float MaxGlassBoost = 1.0f;
    for (const auto& Rec : Hierarchy.ClusterRecords())
    {
        if (Rec.TransmissiveGlass)
        {
            MaxGlassBoost = std::max(MaxGlassBoost, Rec.OpticalDeviationBoost);
        }
    }
    std::cout << "      MaxCurvedGlassOpticalBoost=" << MaxGlassBoost << "x\n";
    REQUIRE_CD(MaxGlassBoost > 1.65f,
               "Curved automotive laminated glazing (Class 7/12, IOR 1.52/1.58) must receive refractive optical error boost");

    // Verify Karis DAG cut monotonic reduction with camera distance + Two-Phase Hi-Z cabin survival behind glass
    ClusterCameraView NearCam{};
    NearCam.CameraOrigin     = {3.20f, 1.48f, 4.20f};
    NearCam.CameraTarget     = {-0.10f, 0.72f, 0.05f};
    NearCam.ViewportWidthPx  = 800u;
    NearCam.ViewportHeightPx = 450u;

    ClusterCameraView MidCam  = NearCam;
    MidCam.CameraOrigin       = {12.0f, 5.2f, 16.0f};
    ClusterCameraView FarCam  = NearCam;
    FarCam.CameraOrigin       = {68.0f, 26.0f, 88.0f};

    const ClusterRuntimeFrameReport NearCut = Hierarchy.ExecuteCutAndTwoPhaseHiZ(NearCam);
    const ClusterRuntimeFrameReport MidCut  = Hierarchy.ExecuteCutAndTwoPhaseHiZ(MidCam);
    const ClusterRuntimeFrameReport FarCut  = Hierarchy.ExecuteCutAndTwoPhaseHiZ(FarCam);

    std::cout << "      DAG Cut Active Triangles: Near(4.2m)=" << NearCut.ActiveTrianglesAfterCull
              << " -> Mid(20m)=" << MidCut.ActiveTrianglesAfterCull
              << " -> Far(110m)=" << FarCut.ActiveTrianglesAfterCull
              << " | OpaqueBehindGlassSurvived=" << NearCut.OpaqueBehindGlassSurvived << "\n";

    REQUIRE_CD(NearCut.ActiveTrianglesAfterCull > FarCut.ActiveTrianglesAfterCull &&
                   FarCut.ActiveTrianglesAfterCull > 0u,
               "QEM Cluster-DAG cut must reduce active triangles at far camera distances");
    REQUIRE_CD(NearCut.OpaqueBehindGlassSurvived > 0u,
               "Two-Phase Opaque-Only Hi-Z must NOT occlude interior cabin seats behind transmissive Class-7 glass");
    REQUIRE_CD(NearCut.MaterialTierClusterCounts[0] > 0u &&
                   (FarCut.MaterialTierClusterCounts[1] + FarCut.MaterialTierClusterCounts[2]) > 0u,
               "3-tier material virtualization must select FullStrata (64B) near and Toksvig/CompactProxy far");

    // Verify 64 KB Page Budget & RAM/VRAM Telemetry
    const ClusterMemoryTelemetry UnconstrainedMem = Hierarchy.QueryMemoryTelemetry(NearCut, 64u * 1024u * 1024u);
    Hierarchy.EnforcePageResidencyBudget(128u * 1024u, FarCam, 2u); // Tight 2-page budget
    const ClusterMemoryTelemetry TightMem         = Hierarchy.QueryMemoryTelemetry(NearCut, 128u * 1024u);
    REQUIRE_CD(UnconstrainedMem.TotalHostRamBytes > 128u * 1024u &&
                   TightMem.PinnedRootPages >= 1u &&
                   TightMem.ResidentVramBytes <= UnconstrainedMem.TotalPagedVramBytes,
               "ClusterMemoryTelemetry must track RAM/VRAM 64 KB page bytes while keeping root tier pinned");
    // Restore full residency for GPU diagnostic renders
    Hierarchy.EnforcePageResidencyBudget(64u * 1024u * 1024u, NearCam, 3u);

    // 4. Verify ThemeCardPalette 1:1 ThemeStructure Binding (Zero Hardcoded Green)
    std::cout << "[4/5] Verifying ThemeCardPalette 1:1 ThemeStructure Binding (Zero Hardcoded Green)...\n";
    {
        ThemeStructure         Theme{}; // Default: OLED + Blue (#3B82F6) + GeneralSans
        const ThemeCardPalette DefaultCard = ResolveThemeCardPalette(Theme);
        REQUIRE_CD(DefaultCard.WorkspaceBackground.Red == 0.0f &&
                       DefaultCard.WorkspaceBackground.Green == 0.0f &&
                       DefaultCard.AccentPrimary.Blue > DefaultCard.AccentPrimary.Green &&
                       DefaultCard.FontFamilyAssetStem == "GeneralSans",
                   "Default ThemeCardPalette must bind to OLED black + Blue (#3B82F6) accent + GeneralSans with zero green tint");

        Theme.ConfigureTheme(ThemeCategory::Sepia);
        Theme.ConfigureAccent(AccentCategory::Amber);
        Theme.ConfigureFontFamily(FontFamilyCategory::JetBrainsMono);
        const ThemeCardPalette SepiaCard = ResolveThemeCardPalette(Theme);
        REQUIRE_CD(SepiaCard.WorkspaceBackground.Red > 0.85f &&
                       SepiaCard.ForegroundPrimary.Red < 0.45f &&
                       SepiaCard.AccentPrimary.Red > 0.90f &&
                       SepiaCard.FontFamilyAssetStem == "JetBrainsMono",
                   "Reconfiguring ThemeStructure must dynamically update card background, foreground, accent, and font");
    }

    // 5. Execute Editor/Development Vulkan 1.3 DiagnosticRaster & Export 4 Showcase PNGs
    std::cout << "[5/5] Executing Vulkan 1.3 DiagnosticRaster (ShadedPreview / ClusterID / Wireframe / GlassPeel)...\n";
    VulkanComputeExchange Exchange{};
    REQUIRE_CD(Exchange.Initialize("", &Diag), ("VulkanComputeExchange::Initialize failed: " + Diag).c_str());

    DiagnosticRaster Raster{};
    REQUIRE_CD(Raster.Initialize(Exchange, ShaderDir.string(), &Diag),
               ("DiagnosticRaster::Initialize failed: " + Diag).c_str());

    DiagnosticAtmosphereBackdrop Sky{};
    Sky.SolarDirectionUnit = {-0.52f, 0.68f, 0.51f};
    Sky.ZenithSkyRgb       = {0.11f, 0.25f, 0.58f};
    Sky.HorizonSkyRgb      = {0.54f, 0.66f, 0.82f};
    Sky.ExposureScale      = 1.08f;

    std::filesystem::create_directories(OutputDir);
    const std::filesystem::path ShowcaseDir = RepoRoot / "Exhibits/ClusterDiagnostic/Images";
    if (MirrorShowcase)
    {
        std::filesystem::create_directories(ShowcaseDir);
    }

    const auto RenderAndSave = [&](DiagnosticViewMode Mode, const char* FileName) {
        DiagnosticFrameExecution Exec{};
        REQUIRE_CD(Raster.RenderView(Exchange, Hierarchy, NearCam, Mode, Sky, &Exec),
                   "DiagnosticRaster::RenderView failed");
        std::vector<uint8_t> SrgbBytes;
        REQUIRE_CD(Raster.ReadbackTonemappedSrgb8(Exchange, Sky.ExposureScale, SrgbBytes),
                   "DiagnosticRaster::ReadbackTonemappedSrgb8 failed");

        uint64_t SumLum        = 0u;
        uint32_t NonZeroPixels = 0u;
        for (size_t Off = 0u; Off < SrgbBytes.size(); Off += 4u)
        {
            const uint32_t L = SrgbBytes[Off + 0u] + SrgbBytes[Off + 1u] + SrgbBytes[Off + 2u];
            SumLum += L;
            if (L > 18u) ++NonZeroPixels;
        }
        REQUIRE_CD(NonZeroPixels > (NearCam.ViewportWidthPx * NearCam.ViewportHeightPx) / 2u && SumLum > 1000000u,
                   "Rendered DiagnosticRaster view must produce valid lit/diagnostic pixels");

        const auto OutFile = OutputDir / FileName;
        REQUIRE_CD(stbi_write_png(OutFile.string().c_str(),
                                  static_cast<int>(NearCam.ViewportWidthPx),
                                  static_cast<int>(NearCam.ViewportHeightPx),
                                  4,
                                  SrgbBytes.data(),
                                  static_cast<int>(NearCam.ViewportWidthPx * 4u)) != 0,
                   "Failed writing PNG exhibit");
        if (MirrorShowcase)
        {
            (void)stbi_write_png((ShowcaseDir / FileName).string().c_str(),
                                 static_cast<int>(NearCam.ViewportWidthPx),
                                 static_cast<int>(NearCam.ViewportHeightPx),
                                 4,
                                 SrgbBytes.data(),
                                 static_cast<int>(NearCam.ViewportWidthPx * 4u));
        }
        std::cout << "      Saved " << FileName << " (ActiveClusters=" << Exec.UploadedClusterCount
                  << ", ActiveTris=" << Exec.UploadedTriangleCount << ")\n";
    };

    RenderAndSave(DiagnosticViewMode::ShadedPreview,   "ControlVehicleShadedPreview.png");
    RenderAndSave(DiagnosticViewMode::ClusterIdentity, "ControlVehicleClusterIdentity.png");
    RenderAndSave(DiagnosticViewMode::ShadedWireframe, "ControlVehicleHierarchyAndWireframe.png");
    RenderAndSave(DiagnosticViewMode::GlassLayerPeel,  "ControlVehicleGlassLayerPeel.png");

    Raster.Shutdown(Exchange);
    Exchange.Shutdown();

    std::cout << "[ClusterDiagnosticValidation] ALL CLUSTER-DAG, MATERIAL TIER, THEME CARD & GPU DIAGNOSTIC CHECKS PASSED.\n";
    return 0;
}
