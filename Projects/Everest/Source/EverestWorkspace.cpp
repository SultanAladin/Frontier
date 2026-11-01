/*==========================================================================================================================================
                                                        EVERESTWORKSPACE.CPP
==========================================================================================================================================*/
// 🧩 Project Everest workspace implementation: EverestApplication runtime + GTX-first Option A Vulkan 1.3 ReSTIR live viewport,
//    WASD + RMB flight rig, #if FRONTIER_EDITOR PreviewRaster & IlluminationTelemetryJournal, and ThemeStructure UI.

#include "EverestWorkspace.h"

#include "Engine/Diagnostics/LogCategories.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Geometry/GeometryProcessing/FacetTierBuild.h"
#include "Engine/Geometry/PrimitiveGeometryGenerator.h"
#include "Engine/Scene/EngineConfigurations.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/LogStreamPanelRegistration.h"
#include "Engine/Interface/NativeInspectors.h"
#include "Engine/Interface/SceneDirectoryPanelRegistration.h"
#include "Engine/Interface/SceneInspectorPanel.h"
#include "Engine/Scene/SceneDirectory.h"
#include <imgui.h>
#include <imgui_internal.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace Everest
{

namespace
{

constexpr float kPi = 3.14159265358979323846f;

uint32_t EnsureProjectLogCategory(std::string_view CategoryName)
{
    return Frontier::LogCategories().Register(CategoryName, Frontier::LogColour::Mint, true);
}

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

float MeasureLabelWidth(ImFont* Font, float Size, const char* Text) noexcept
{
    if (Font == nullptr || Text == nullptr || Text[0] == '\0')
    {
        return 0.0f;
    }
    return Font->CalcTextSizeA(Size, 1.0e9f, 0.0f, Text).x;
}

#endif

std::array<float, 3> KelvinToLinearTint(float Kelvin) noexcept
{
    const float T = std::clamp(Kelvin, 1800.0f, 12000.0f) * 0.01f;
    float       R = 1.0f;
    float       G = 1.0f;
    float       B = 1.0f;
    if (T <= 66.0f)
    {
        R = 1.0f;
        G = std::clamp((99.4708f * std::log(std::max(1.0f, T)) - 161.1196f) / 255.0f, 0.05f, 1.0f);
        B = (T <= 19.0f)
                ? 0.0f
                : std::clamp((138.5177f * std::log(std::max(1.0f, T - 10.0f)) - 305.0448f) / 255.0f, 0.0f, 1.0f);
    }
    else
    {
        R = std::clamp((329.6987f * std::pow(T - 60.0f, -0.1332f)) / 255.0f, 0.2f, 1.0f);
        G = std::clamp((288.1222f * std::pow(T - 60.0f, -0.0755f)) / 255.0f, 0.2f, 1.0f);
        B = 1.0f;
    }
    return {R * R, G * G, B * B};
}

std::array<float, 16> MakeScaleTranslateColumnMatrix(const float Pos[3], const float Scale[3]) noexcept
{
    const float Sx = std::max(0.05f, Scale[0]);
    const float Sy = std::max(0.05f, Scale[1]);
    const float Sz = std::max(0.05f, Scale[2]);
    return {
        Sx,     0.0f,   0.0f,   0.0f,
        0.0f,   Sy,     0.0f,   0.0f,
        0.0f,   0.0f,   Sz,     0.0f,
        Pos[0], Pos[1], Pos[2], 1.0f,
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                      1. CONSTRUCTION, LIFECYCLE & ASSET RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

EverestWorkspace::EverestWorkspace(Frontier::ControlCentrePanel& ControlCentre,
                                   std::filesystem::path         ProjectRootPath)
    : Centre(ControlCentre)
    , InspectorPanel(OutlinerPanel)
    , ProjectRoot(std::move(ProjectRootPath))
{
    InitializeWorld();
    FlightRig.ResetToDefaultHeroPose();

    OutlinerPanel.ConfigureConstructibleTypes(QuerySceneWorld().EnumerateRegisteredTypes());
    OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());

    if (Frontier::SceneRecordBase* Solar = QuerySceneWorld().FindFirstByType("Solar"))
    {
        OutlinerPanel.Select(Solar->QueryToken());
        OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());
    }

    Dockspace.ConfigureDockspaceKey("EverestWorkspaceDockspace");
    Dockspace.ConfigureWorkspaceInsets(50.0f, 0.0f);

    if (!Dockspace.Register(Frontier::MakeSceneDirectoryPanelSpecification(OutlinerPanel)) ||
        !Dockspace.Register({"Viewport", "Scene Viewport", [this]() { ConstructViewportSurface(); }, true}) ||
        !Dockspace.Register(Frontier::MakeLogStreamPanelSpecification(LogPanel)) ||
        !Dockspace.Register(Frontier::MakeSceneInspectorPanelSpecification(InspectorPanel)))
    {
        throw std::runtime_error("EverestWorkspace dockspace registration refused.");
    }

    (void)Dockspace.ConfigureStackedPanels("Viewport", "LogStream", 520.0f);
    (void)Dockspace.ConfigureColumns({
        {"Outliner", 0.200f},
        {"LogStream", 0.545f},
        {"Inspector", 0.255f},
    });

    ImGui::GetIO().IniFilename = nullptr;
    ObservedLayoutRevision     = Centre.QueryLayoutResetRevision();
}

#else

EverestWorkspace::EverestWorkspace(std::filesystem::path ProjectRootPath)
    : ProjectRoot(std::move(ProjectRootPath))
{
    InitializeWorld();
    FlightRig.ResetToDefaultHeroPose();
}

#endif

EverestWorkspace::~EverestWorkspace()
{
#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
    if (HeadlessTexture && ImGui::GetCurrentContext() != nullptr)
    {
        ImGui::UnregisterUserTexture(HeadlessTexture.get());
        auto& Textures = ImGui::GetPlatformIO().Textures;
        for (int I = Textures.Size - 1; I >= 0; --I)
        {
            if (Textures[I] == HeadlessTexture.get())
            {
                Textures.erase(Textures.Data + I);
            }
        }
    }
    HeadlessTexture.reset();
#endif
    DetachVulkanPresentation();
}

void EverestWorkspace::InitializeWorld()
{
    const uint32_t ProjectCategory = EnsureProjectLogCategory("Everest");
    (void)EnsureProjectLogCategory("Physics");

    const std::filesystem::path ProjectSpacePath = ProjectRoot / "Everest.projectspace";
    if (std::filesystem::exists(ProjectSpacePath))
    {
        if (Application.Initialize(ProjectSpacePath, &Diagnostic))
        {
            ActiveLevelPath = QuerySceneWorld().QueryLoadedLevelPath();
#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
            Frontier::SceneDirectory::Instance().ConfigureActiveSceneHeader(
                ActiveLevelPath.stem().string(), "Frontier LevelSpace");
#endif
            Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log,
                                     "EverestWorkspace", "Loaded '%s' (%zu scene records).",
                                     ActiveLevelPath.filename().string().c_str(),
                                     QuerySceneWorld().QueryRecordCount());
        }
        else
        {
            Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Error, Frontier::LogVerbosity::Log,
                                     "EverestWorkspace", "Project startup failed: %s", Diagnostic.c_str());
        }
    }
    else
    {
        ActiveLevelPath = ProjectRoot / "Content" / "Levels" / "Default.levelspace";
        RegisterEverestSimulationTypes(QuerySceneWorld());
        if (std::filesystem::exists(ActiveLevelPath) && QuerySceneWorld().LoadLevelSpace(ActiveLevelPath, &Diagnostic))
        {
#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
            Frontier::SceneDirectory::Instance().ConfigureActiveSceneHeader(
                ActiveLevelPath.stem().string(), "Frontier LevelSpace");
#endif
        }
        else
        {
            Diagnostic = "Project container missing: " + ProjectSpacePath.string();
            Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Warning, Frontier::LogVerbosity::Log,
                                     "EverestWorkspace", "Initialized empty SceneWorld (%s).", Diagnostic.c_str());
        }
    }
}

std::filesystem::path EverestWorkspace::ResolveShaderArchiveDirectory() const
{
    std::error_code ErrorCode;
    const std::filesystem::path Candidates[] = {
        ProjectRoot / "Content" / "ShaderArchives" / "Environment",
        std::filesystem::path("Content/ShaderArchives/Environment"),
        std::filesystem::path("ShaderArchives/Environment"),
        ProjectRoot.parent_path().parent_path() / "_ClaudeScratch" / "build-linux" / "ShaderArchives" / "Environment",
        std::filesystem::path("_ClaudeScratch/build-linux/ShaderArchives/Environment"),
    };
    for (const auto& Path : Candidates)
    {
        if (std::filesystem::exists(Path / "SolveSurfaceRadiosity.spv", ErrorCode) ||
            std::filesystem::exists(Path / "TracePrimarySurface.spv", ErrorCode))
        {
            return Path;
        }
    }
    return ProjectRoot / "Content" / "ShaderArchives" / "Environment";
}

std::filesystem::path EverestWorkspace::ResolveVehicleArchiveDirectory() const
{
    std::error_code ErrorCode;
    const std::filesystem::path Candidates[] = {
        ProjectRoot / "Content" / "EnvironmentArchives" / "ControlVehicle",
        std::filesystem::path("Content/EnvironmentArchives/ControlVehicle"),
        ProjectRoot.parent_path().parent_path() / "EngineContent" / "EnvironmentArchives" / "ControlVehicle",
        std::filesystem::path("EngineContent/EnvironmentArchives/ControlVehicle"),
    };
    for (const auto& Path : Candidates)
    {
        if (std::filesystem::exists(Path / "ControlVehicle.facet", ErrorCode))
        {
            return Path;
        }
    }
    return ProjectRoot / "Content" / "EnvironmentArchives" / "ControlVehicle";
}

std::filesystem::path EverestWorkspace::ResolveMaterialRootDirectory() const
{
    std::error_code ErrorCode;
    const std::filesystem::path StagedRoot = ProjectRoot / "Content";
    if (std::filesystem::exists(StagedRoot / "MaterialArchives" / "TextureStudio", ErrorCode))
    {
        return StagedRoot;
    }
    if (std::filesystem::exists("Content/MaterialArchives/TextureStudio", ErrorCode))
    {
        return std::filesystem::path("Content");
    }
    const auto RepoEngineContent = ProjectRoot.parent_path().parent_path() / "EngineContent";
    if (std::filesystem::exists(RepoEngineContent / "MaterialArchives" / "TextureStudio", ErrorCode))
    {
        return RepoEngineContent;
    }
    return std::filesystem::path("EngineContent");
}

//------------------------------------------------------------------------------------------------------------------------
//                                  2. TWO-LEVEL SCENE ASSEMBLY & GPU SYNCHRONIZATION
//------------------------------------------------------------------------------------------------------------------------

bool EverestWorkspace::EnsureSceneGeometryBuilt()
{
    if (GeometryBuilt)
    {
        return true;
    }

    Materials.RegisterControlVehicleScatteringPresets();
    const auto MaterialRoot = ResolveMaterialRootDirectory();
    Materials.LoadProvingGroundAuthoredMaterials(MaterialRoot.string());

    // Emissive Class 20 studio softbox material
    Frontier::SurfaceScatteringEntry Softbox{};
    Softbox.ClassificationId                     = 20u;
    Softbox.Scattering.Configuration             = {1u, 0u, 0u, 0u};
    Softbox.Scattering.Strata[0].AlbedoRoughness = {0.92f, 0.94f, 0.98f, 0.35f};
    Softbox.Scattering.Strata[0].Optics          = {0.0f, 1.50f, 1.0f, 0.0f};
    Softbox.EmissiveRadianceRgb                  = {9.5f, 9.8f, 10.6f};
    Materials.RegisterScatteringEntry(Softbox);

    Frontier::FacetField              ProvingScene;
    Frontier::ControlVehiclePlacement VehiclePlace{};
    VehiclePlace.ArchiveDirectory = ResolveVehicleArchiveDirectory().string();
    if (!Frontier::AssembleControlVehicle(ProvingScene, VehiclePlace, &Diagnostic))
    {
        Frontier::PrimitivePlacement FallbackPlace{};
        FallbackPlace.TranslationMeters = {0.0, 0.65, 0.0};
        FallbackPlace.ClassificationId  = 6u;
        Frontier::AppendProceduralBox(ProvingScene, {0.95, 0.55, 2.05}, FallbackPlace);
    }

    (void)Frontier::ComposeControlVehicleDistanceVolume(VehicleDistanceField, VehiclePlace, {68u, 38u, 92u}, true, nullptr);

    // Proving-ground asphalt deck + board-form concrete arch tunnel + overhead studio softbox luminaire bars
    {
        Frontier::PrimitivePlacement FloorPlace{};
        FloorPlace.TranslationMeters = {0.0, 0.0, 0.0};
        FloorPlace.ClassificationId  = 0u;
        FloorPlace.UvScale           = 0.35;
        Frontier::AppendProceduralGroundGrid(ProvingScene, 18.0, 18.0, 18u, 18u, FloorPlace);

        Frontier::PrimitivePlacement ArchPlace{};
        ArchPlace.TranslationMeters = {0.0, 0.0, -5.2};
        ArchPlace.ClassificationId  = 1u;
        Frontier::AppendProceduralArchTunnel(ProvingScene, 4.4, 0.45, 4.2, 20u, ArchPlace);

        Frontier::PrimitivePlacement LeftBar{};
        LeftBar.TranslationMeters = {-1.85, 3.62, 0.25};
        LeftBar.ClassificationId  = 20u;
        Frontier::AppendProceduralBox(ProvingScene, {0.26, 0.05, 2.35}, LeftBar);

        Frontier::PrimitivePlacement RightBar{};
        RightBar.TranslationMeters = {1.85, 3.62, 0.25};
        RightBar.ClassificationId  = 20u;
        Frontier::AppendProceduralBox(ProvingScene, {0.26, 0.05, 2.35}, RightBar);
    }

#if FRONTIER_EDITOR
    Frontier::FacetField PreviewField = ProvingScene;
    {
        Frontier::PrimitivePlacement BoxPlace{};
        BoxPlace.TranslationMeters = {-3.0, 1.0, 0.0};
        BoxPlace.ClassificationId  = 3u;
        Frontier::AppendProceduralBox(PreviewField, {0.5, 0.5, 0.5}, BoxPlace);

        Frontier::PrimitivePlacement CylPlace{};
        CylPlace.TranslationMeters = {3.0, 1.2, 0.0};
        CylPlace.ClassificationId  = 5u;
        Frontier::AppendProceduralCylinder(PreviewField, 0.45, 1.2, 20u, CylPlace);

        Frontier::PrimitivePlacement SphPlace{};
        SphPlace.TranslationMeters = {0.0, 1.8, -2.5};
        SphPlace.ClassificationId  = 6u;
        Frontier::AppendProceduralGeodesicSphere(PreviewField, 0.42, 2u, SphPlace);
    }
    ClusterRefinement.RegisterControlVehicleMaterials();
    ClusterRefinement.BuildFromFacetField(PreviewField);
#endif

    Frontier::FacetField UnitBoxField;
    Frontier::AppendProceduralBox(UnitBoxField, {0.5, 0.5, 0.5});
    Frontier::FacetField UnitCylField;
    Frontier::AppendProceduralCylinder(UnitCylField, 0.45, 1.2, 24u);
    Frontier::FacetField UnitSphField;
    Frontier::AppendProceduralGeodesicSphere(UnitSphField, 0.42, 2u);

    TwoLevelAtlas.Clear();
    ProvingGroundArchiveId = TwoLevelAtlas.RegisterBottomArchive(ProvingScene);
    UnitBoxArchiveId       = TwoLevelAtlas.RegisterBottomArchive(UnitBoxField);
    UnitCylinderArchiveId  = TwoLevelAtlas.RegisterBottomArchive(UnitCylField);
    UnitSphereArchiveId    = TwoLevelAtlas.RegisterBottomArchive(UnitSphField);

    Frontier::InstancePlacementSpec RootInst{};
    RootInst.BottomArchiveIndex = ProvingGroundArchiveId;
    TwoLevelAtlas.BuildInstanceScene(std::span(&RootInst, 1u));

    // Capture 6-axis surface radiosity patches for proving-ground walls, arch, deck, and pillars
    const std::array<Frontier::SurfaceCardBuildInstance, 4> CardBoxes{{
        {{-4.85f, 0.0f, -7.3f}, {-4.20f, 3.80f, -3.1f}, {0.52f, 0.50f, 0.48f}, {0.0f, 0.0f, 0.0f}},
        {{ 4.20f, 0.0f, -7.3f}, { 4.85f, 3.80f, -3.1f}, {0.52f, 0.50f, 0.48f}, {0.0f, 0.0f, 0.0f}},
        {{-12.0f, -0.25f, -12.0f}, {12.0f, 0.0f, 12.0f}, {0.26f, 0.27f, 0.29f}, {0.0f, 0.0f, 0.0f}},
        {{-3.50f, 0.50f, -0.50f}, {-2.50f, 1.50f, 0.50f}, {0.78f, 0.42f, 0.24f}, {0.0f, 0.0f, 0.0f}},
    }};
    RadiosityCards.BuildPatchesFromInstances(CardBoxes);

    std::vector<Frontier::PackedRayTriangle> WorldTris;
    TwoLevelAtlas.ExtractWorldTriangles(WorldTris);
    LuminousFacets.BuildFromWorldTriangles(WorldTris, Materials);

    Frontier::RangeClipmapBuildSpec ClipSpec{};
    ClipSpec.AnchorWorldMeters = FlightRig.OriginMeters();
    ClipSpec.ActiveRangeCount  = 4u;
    ClipSpec.AxisResolution    = 28u;
    ClipSpec.RangeHalfExtentsM = {8.0f, 24.0f, 72.0f, 192.0f};
    std::vector<Frontier::PlacedDistanceVolumeSpec> PlacedVols;
    if (!VehicleDistanceField.Distances.empty())
    {
        PlacedVols.push_back({&VehicleDistanceField, {0.0f, 0.0f, 0.0f}, 1.0f});
    }
    const std::array<Frontier::AnalyticSolidDistanceSpec, 3> Solids{{
        {2u, {0.0f, 0.0f, 0.0f}, {100.0f, 0.1f, 100.0f}},
        {0u, {-3.0f, 1.0f, 0.0f}, {0.5f, 0.5f, 0.5f}},
        {1u, {0.0f, 1.8f, -2.5f}, {0.42f, 0.42f, 0.42f}},
    }};
    RangeClipmap.BuildClipmap(ClipSpec, PlacedVols, Solids);
    ClipmapAnchorPos_    = FlightRig.OriginMeters();
    ClipmapCascadeZeroM_ = ClipSpec.RangeHalfExtentsM[0];

    GeometryBuilt = true;
    return true;
}

bool EverestWorkspace::InitializeRendererOnExchange()
{
    if (!Exchange.Ready() || !EnsureSceneGeometryBuilt())
    {
        return false;
    }

    const auto ShaderDir = ResolveShaderArchiveDirectory();
    if (!Illumination.Initialize(Exchange, ShaderDir.string(), &Diagnostic))
    {
        return false;
    }
    if (!Illumination.UploadTwoLevelScene(Exchange,
                                          TwoLevelAtlas,
                                          Materials,
                                          &RangeClipmap,
                                          &RadiosityCards,
                                          &LuminousFacets))
    {
        Diagnostic = "Failed to upload Everest two-level GPU scene.";
        return false;
    }

#if FRONTIER_EDITOR
    FastPreview.Initialize(Exchange, ShaderDir.string(), nullptr);
    if (!VehicleDistanceField.Distances.empty())
    {
        FastPreview.AttachDistanceVolume(Exchange, &VehicleDistanceField);
    }

    if (!TelemetryJournal.IsOpen())
    {
        TelemetryJournal.OpenSession(ProjectRoot / "Saved" / "Logs", Exchange, Illumination);
        Frontier::SpatiotemporalFrameTelemetry UploadSummary{};
        UploadSummary.BottomArchiveCount    = TwoLevelAtlas.BottomArchiveCount();
        UploadSummary.TopInstanceCount      = TwoLevelAtlas.InstanceCount();
        UploadSummary.BvhCellCount          = TwoLevelAtlas.TopCellCount();
        UploadSummary.OctetBlockCount       = TwoLevelAtlas.TotalOctetBlockCount();
        UploadSummary.BvhTriangleCount      = TwoLevelAtlas.TotalTriangleCount();
        UploadSummary.SurfacePatchCount     = RadiosityCards.PatchCount();
        UploadSummary.LuminousFacetCount    = LuminousFacets.FacetCount();
        UploadSummary.AuthoredTextureLayers = Materials.AuthoredTextureLayerCount();
        UploadSummary.RangeClipmapActive    = !RangeClipmap.Empty();
        TelemetryJournal.RecordSceneUploadSummary(UploadSummary, Exchange.QueryAllocatedDeviceBytes());
    }
#endif

    const uint32_t ProjectCategory = EnsureProjectLogCategory("Everest");
    Frontier::WriteLogFormat(ProjectCategory,
                             Frontier::LogSeverity::Info,
                             Frontier::LogVerbosity::Log,
                             "EverestWorkspace",
                             "Vulkan 1.3 GTX ReSTIR initialized on '%s' (%u triangles, %u 8-wide CWBVH blocks, %u cards).",
                             Exchange.DeviceName().c_str(),
                             TwoLevelAtlas.TotalTriangleCount(),
                             TwoLevelAtlas.TotalOctetBlockCount(),
                             RadiosityCards.PatchCount());

    RendererReady = true;
    return true;
}

void EverestWorkspace::SyncDynamicWorldToRenderer(const Frontier::SpatiotemporalCamera&       Camera,
                                                  std::vector<Frontier::AnalyticalLuminaire>& OutLuminaires,
                                                  Frontier::SpatiotemporalRenderConfig&       OutConfig)
{
    const auto& World = QuerySceneWorld();

    std::vector<Frontier::InstancePlacementSpec> Instances;
    Instances.reserve(8u);
    {
        Frontier::InstancePlacementSpec ProvingRoot{};
        ProvingRoot.BottomArchiveIndex = ProvingGroundArchiveId;
        Instances.push_back(ProvingRoot);
    }

    OutLuminaires.clear();

    float SunElevDeg   = 32.0f;
    float SunAzimDeg   = 138.0f;
    float SunKilolux   = 112.0f;
    float SunKelvin    = 5778.0f;
    float MieTurbidity = 2.4f;
    bool  CloudsOn     = true;
    float CloudCov     = 0.42f;
    float CloudDens    = 0.045f;
    float CloudBaseM   = 1500.0f;
    bool  FogOn        = true;
    float FogDensity   = 0.004f;
    float FogFalloff   = 0.06f;
    bool  MoonsOn      = true;
    float MoonOrbitDeg = 38.0f;
    bool  CardsOn      = true;
    float CardBounce   = 1.0f;
    float CardSkyWt    = 0.35f;
    bool  SdfEnabled   = true;
    float CascadeZeroM = 8.0f;

    std::vector<Frontier::AnalyticalLuminaire> DynamicEmitters;

    for (const auto& Entry : World.QueryRecords())
    {
        if (!Entry)
        {
            continue;
        }
        const std::string& Type = Entry->QueryTypeIdentifier();

        if (Type == "ThermalBody")
        {
            const auto* Rec   = static_cast<const Frontier::ThermalBodyRecord*>(Entry.get());
            const auto& Spat  = Rec->QuerySpatial();
            const float TempK = Rec->QueryThermal().TemperatureKelvin;
            const float Heat  = std::clamp((TempK - 540.0f) / 720.0f, 0.0f, 1.6f);

            Frontier::InstancePlacementSpec Inst{};
            Inst.BottomArchiveIndex     = UnitBoxArchiveId;
            Inst.ObjectToWorldColumn    = MakeScaleTranslateColumnMatrix(Spat.PositionMetres, Spat.Scale);
            Inst.ClassificationOverride = 17u;
            Inst.OverrideOpticsEnabled  = true;
            Inst.AlbedoMultiplier       = {0.94f, 0.58f, 0.36f};
            Inst.SelfRadianceScale      = Heat * 0.65f;
            Instances.push_back(Inst);

            if (Heat > 0.05f)
            {
                Frontier::AnalyticalLuminaire ThermalGlow{};
                ThermalGlow.Kind                = Frontier::AnalyticalLuminaireKind::OmniPoint;
                ThermalGlow.DirectionOrPosition = {Spat.PositionMetres[0],
                                                   Spat.PositionMetres[1] + 0.55f,
                                                   Spat.PositionMetres[2]};
                ThermalGlow.AxisU               = {0.32f, 0.0f, 0.0f};
                ThermalGlow.EmittedRadianceRgb  = {1.45f * Heat, 0.48f * Heat, 0.12f * Heat};
                DynamicEmitters.push_back(ThermalGlow);
            }
        }
        else if (Type == "ChemicalVessel")
        {
            const auto* Rec  = static_cast<const Frontier::ChemicalVesselRecord*>(Entry.get());
            const auto& Spat = Rec->QuerySpatial();
            const float Conv = std::clamp(1.0f - Rec->QueryChemical().AmountMoles / 2.5f, 0.0f, 1.0f);

            Frontier::InstancePlacementSpec Inst{};
            Inst.BottomArchiveIndex     = UnitCylinderArchiveId;
            Inst.ObjectToWorldColumn    = MakeScaleTranslateColumnMatrix(Spat.PositionMetres, Spat.Scale);
            Inst.ClassificationOverride = 18u;
            Inst.OverrideOpticsEnabled  = true;
            Inst.AlbedoMultiplier       = {0.76f + 0.18f * Conv, 0.90f + 0.08f * Conv, 0.98f};
            Instances.push_back(Inst);
        }
        else if (Type == "HarmonicOscillator")
        {
            const auto* Rec    = static_cast<const HarmonicOscillatorRecord*>(Entry.get());
            const auto& Spat   = Rec->QuerySpatial();
            const float Pos[3] = {
                Spat.PositionMetres[0],
                Spat.PositionMetres[1] + Rec->QueryOscillator().CurrentDisplacementM,
                Spat.PositionMetres[2],
            };

            Frontier::InstancePlacementSpec Inst{};
            Inst.BottomArchiveIndex     = UnitSphereArchiveId;
            Inst.ObjectToWorldColumn    = MakeScaleTranslateColumnMatrix(Pos, Spat.Scale);
            Inst.ClassificationOverride = 19u;
            Inst.OverrideOpticsEnabled  = true;
            Inst.AlbedoMultiplier       = {1.0f, 1.0f, 1.0f};
            Instances.push_back(Inst);
        }
        else if (Type == "RigidBody")
        {
            const auto* Rec = static_cast<const Frontier::RigidBodyRecord*>(Entry.get());
            if (Rec->QueryPhysics().DynamicBody)
            {
                Frontier::InstancePlacementSpec Inst{};
                Inst.BottomArchiveIndex     = UnitBoxArchiveId;
                Inst.ObjectToWorldColumn    = MakeScaleTranslateColumnMatrix(Rec->QuerySpatial().PositionMetres,
                                                                             Rec->QuerySpatial().Scale);
                Inst.ClassificationOverride = 0u;
                Inst.OverrideOpticsEnabled  = true;
                Inst.AlbedoMultiplier       = {0.92f, 0.68f, 0.28f};
                Instances.push_back(Inst);
            }
        }
        else if (Type == "Solar")
        {
            const auto& C = static_cast<const Frontier::SolarRecord*>(Entry.get())->QueryConfiguration();
            SunElevDeg    = C.ElevationDegrees;
            SunAzimDeg    = C.AzimuthDegrees;
            SunKilolux    = C.IlluminanceKilolux;
            SunKelvin     = C.KelvinTemperature;
        }
        else if (Type == "Atmosphere")
        {
            MieTurbidity = static_cast<const Frontier::AtmosphereRecord*>(Entry.get())->QueryConfiguration().MieTurbidity;
        }
        else if (Type == "Clouds")
        {
            const auto& C = static_cast<const Frontier::CloudRecord*>(Entry.get())->QueryConfiguration();
            CloudsOn      = C.Enabled;
            CloudCov      = C.Coverage;
            CloudDens     = C.ExtinctionDensity * 0.06f;
            CloudBaseM    = C.BaseAltitudeMetres;
        }
        else if (Type == "Fog")
        {
            const auto& C = static_cast<const Frontier::FogRecord*>(Entry.get())->QueryConfiguration();
            FogOn         = C.Enabled;
            FogDensity    = C.BaseDensity * 0.35f;
            FogFalloff    = C.HeightFalloff;
        }
        else if (Type == "Moons")
        {
            const auto& C = static_cast<const Frontier::MoonRecord*>(Entry.get())->QueryConfiguration();
            MoonsOn       = C.Enabled;
            MoonOrbitDeg  = C.PrimaryOrbitDegrees;
        }
        else if (Type == "SurfaceCards")
        {
            const auto& C = static_cast<const Frontier::SurfaceCardRecord*>(Entry.get())->QueryConfiguration();
            CardsOn       = C.Enabled;
            CardBounce    = C.IndirectBounceGain;
            CardSkyWt     = C.SkyVisibilityWeight;
        }
        else if (Type == "DistanceField")
        {
            const auto& C = static_cast<const Frontier::DistanceFieldRecord*>(Entry.get())->QueryConfiguration();
            SdfEnabled    = C.Enabled;
            CascadeZeroM  = std::clamp(C.CascadeZeroRadiusMetres, 4.0f, 24.0f);
        }
        else if (Type == "RenderPipeline")
        {
            const auto& C                  = static_cast<const Frontier::RenderPipelineRecord*>(Entry.get())->QueryConfiguration();
            OutConfig.InspectionMode       = static_cast<uint32_t>(std::clamp(C.InspectionModeIndex, 0, 14));
            OutConfig.TemporalHistoryLimit = static_cast<uint32_t>(std::clamp(C.RestirTemporalHistory, 1.0f, 32.0f));
            OutConfig.SpatialNeighbors     = static_cast<uint32_t>(std::clamp(C.RestirSpatialNeighbors, 0.0f, 6.0f));
            OutConfig.ExposureScale        = std::pow(2.0f, std::clamp(C.ExposureCompensationEv, -4.0f, 4.0f));
            OutConfig.EnableIndirectGi     = C.GlobalIllumination;
            OutConfig.EnableReflections    = C.ScreenSpaceReflections;
            if (C.QualityTier == 0)
            {
                OutConfig.RisCandidateCount = 4u;
                OutConfig.SdfIndirectRays   = C.GlobalIllumination ? 1u : 0u;
                OutConfig.AtrousIterations  = 1u;
            }
            else if (C.QualityTier == 1)
            {
                OutConfig.RisCandidateCount = 6u;
                OutConfig.SdfIndirectRays   = C.GlobalIllumination ? 2u : 0u;
                OutConfig.AtrousIterations  = 2u;
            }
            else
            {
                OutConfig.RisCandidateCount = 8u;
                OutConfig.SdfIndirectRays   = C.GlobalIllumination ? 4u : 0u;
                OutConfig.AtrousIterations  = 2u;
            }
        }
    }

    if (Instances.size() == TwoLevelAtlas.InstanceCount())
    {
        TwoLevelAtlas.RefitTopLevelInstances(Instances);
    }
    else
    {
        TwoLevelAtlas.BuildInstanceScene(Instances);
    }
    Illumination.UpdateDynamicTopLevelInstances(Exchange, TwoLevelAtlas);

    const float CamDx = Camera.OriginMeters[0] - ClipmapAnchorPos_[0];
    const float CamDy = Camera.OriginMeters[1] - ClipmapAnchorPos_[1];
    const float CamDz = Camera.OriginMeters[2] - ClipmapAnchorPos_[2];
    if (SdfEnabled &&
        ((CamDx * CamDx + CamDy * CamDy + CamDz * CamDz) > 16.0f || std::abs(CascadeZeroM - ClipmapCascadeZeroM_) > 0.25f))
    {
        Frontier::RangeClipmapBuildSpec ClipSpec{};
        ClipSpec.AnchorWorldMeters = Camera.OriginMeters;
        ClipSpec.ActiveRangeCount  = 4u;
        ClipSpec.AxisResolution    = 28u;
        ClipSpec.RangeHalfExtentsM = {CascadeZeroM, CascadeZeroM * 3.0f, CascadeZeroM * 9.0f, CascadeZeroM * 24.0f};
        std::vector<Frontier::PlacedDistanceVolumeSpec> PlacedVols;
        if (!VehicleDistanceField.Distances.empty())
        {
            PlacedVols.push_back({&VehicleDistanceField, {0.0f, 0.0f, 0.0f}, 1.0f});
        }
        const std::array<Frontier::AnalyticSolidDistanceSpec, 3> Solids{{
            {2u, {0.0f, 0.0f, 0.0f}, {100.0f, 0.1f, 100.0f}},
            {0u, {-3.0f, 1.0f, 0.0f}, {0.5f, 0.5f, 0.5f}},
            {1u, {0.0f, 1.8f, -2.5f}, {0.42f, 0.42f, 0.42f}},
        }};
        RangeClipmap.BuildClipmap(ClipSpec, PlacedVols, Solids);
        Illumination.UpdateRangeClipmap(Exchange, RangeClipmap);
        ClipmapAnchorPos_    = Camera.OriginMeters;
        ClipmapCascadeZeroM_ = CascadeZeroM;
    }

    // 1. Directional Solar Luminaire
    const float ElevRad    = SunElevDeg * (kPi / 180.0f);
    const float AzimRad    = SunAzimDeg * (kPi / 180.0f);
    const float Sy         = std::sin(ElevRad);
    const float CosE       = std::cos(ElevRad);
    const float Sx         = CosE * std::cos(AzimRad);
    const float Sz         = CosE * std::sin(AzimRad);
    const auto  KelvinRgb  = KelvinToLinearTint(SunKelvin);
    const float DayFactor  = std::clamp((Sy + 0.08f) / 0.35f, 0.0f, 1.0f);
    const float CloudAtten = CloudsOn ? (1.0f - 0.45f * std::clamp(CloudCov, 0.0f, 1.0f)) : 1.0f;
    const float SunEnergy    = (SunKilolux / 44.0f) * DayFactor * CloudAtten;
    const float StudioWeight = std::clamp(1.0f - DayFactor * 0.92f, 0.08f, 1.0f);

    {
        Frontier::AnalyticalLuminaire SunLum{};
        SunLum.Kind                = Frontier::AnalyticalLuminaireKind::DirectionalSolar;
        SunLum.DirectionOrPosition = {Sx, Sy, Sz};
        SunLum.AxisU               = {0.00465f, 0.0f, 0.0f};
        SunLum.EmittedRadianceRgb  = {KelvinRgb[0] * SunEnergy, KelvinRgb[1] * SunEnergy, KelvinRgb[2] * SunEnergy};
        OutLuminaires.push_back(SunLum);
    }

    // 2. Overhead Rectangular Studio Softbox Luminaires (automatically brighten at twilight/night)
    {
        Frontier::AnalyticalLuminaire LeftBar{};
        LeftBar.Kind                = Frontier::AnalyticalLuminaireKind::RectangularArea;
        LeftBar.DirectionOrPosition = {-1.85f, 3.45f, -2.4f};
        LeftBar.AxisU               = {0.21f, 0.0f, 0.0f};
        LeftBar.AxisV               = {0.0f, 0.0f, 2.30f};
        LeftBar.EmittedRadianceRgb  = {3.6f * StudioWeight, 3.8f * StudioWeight, 4.4f * StudioWeight};
        OutLuminaires.push_back(LeftBar);

        Frontier::AnalyticalLuminaire RightBar = LeftBar;
        RightBar.DirectionOrPosition[0]        = 1.85f;
        OutLuminaires.push_back(RightBar);
    }

    for (const auto& DynLum : DynamicEmitters)
    {
        OutLuminaires.push_back(DynLum);
    }

    // 3. Coupled PlanetaryEnsemble Sky, Clouds, Lunar & Fog Parameters
    const float LunarFloor         = MoonsOn ? (0.045f + 0.025f * std::sin(MoonOrbitDeg * (kPi / 180.0f))) : 0.015f;
    const float SkyEnergy          = std::max(LunarFloor, DayFactor * (0.85f + 0.12f * MieTurbidity));
    OutConfig.ZenithSkyRgb         = {0.13f * SkyEnergy, 0.29f * SkyEnergy, 0.66f * SkyEnergy};
    const float SunsetWarm         = std::clamp(1.0f - std::abs(Sy) * 3.0f, 0.0f, 1.0f) * DayFactor;
    OutConfig.HorizonSkyRgb        = {
        (0.46f + 0.48f * SunsetWarm) * SkyEnergy,
        (0.56f + 0.14f * SunsetWarm) * SkyEnergy,
        (0.72f - 0.18f * SunsetWarm) * SkyEnergy,
    };

    OutConfig.SurfaceCardGain                 = CardsOn ? std::clamp(CardBounce, 0.0f, 3.0f) : 0.0f;
    OutConfig.SurfaceCardSkyWeight            = std::clamp(CardSkyWt, 0.0f, 1.0f);
    OutConfig.Environment.CloudsEnabled       = CloudsOn;
    OutConfig.Environment.CloudCoverage       = std::clamp(CloudCov, 0.0f, 1.0f);
    OutConfig.Environment.CloudDensity        = std::clamp(CloudDens, 0.005f, 0.25f);
    OutConfig.Environment.CloudBottomMeters   = std::max(250.0f, CloudBaseM);
    OutConfig.Environment.LunarEnabled        = MoonsOn;
    const float MoonRad                       = MoonOrbitDeg * (kPi / 180.0f);
    OutConfig.Environment.PrimaryMoonDir      = {std::cos(MoonRad) * 0.78f, 0.58f, std::sin(MoonRad) * 0.78f};
    OutConfig.Environment.FogEnabled          = FogOn;
    OutConfig.Environment.FogDensity          = std::clamp(FogDensity, 0.0f, 0.06f);
    OutConfig.Environment.FogHeightFalloff    = std::max(0.02f, FogFalloff);
    OutConfig.Environment.FogInscatteringRgb  = OutConfig.HorizonSkyRgb;
}

//------------------------------------------------------------------------------------------------------------------------
//                          3. LIVE SWAPCHAIN ATTACHMENT, GPU STREAMING & SIMULATION STEP
//------------------------------------------------------------------------------------------------------------------------

bool EverestWorkspace::AttachVulkanPresentation(const Frontier::VulkanRasterAccess&  Access,
                                                std::function<uint64_t(VkImageView)> DescribeTextureView,
                                                std::function<void(uint64_t)>        ReleaseTextureDescriptor)
{
    DetachVulkanPresentation();
    if (Access.Device == VK_NULL_HANDLE || Access.Queue == VK_NULL_HANDLE || !DescribeTextureView)
    {
        return false;
    }

    Frontier::BorrowedVulkanDeviceSpec Handles{};
    Handles.Device         = Access.Device;
    Handles.PhysicalDevice = Access.PhysicalDevice;
    Handles.Queue          = Access.Queue;
    Handles.QueueFamily    = Access.QueueFamily;
    Handles.ResolveDevice  = Access.ResolveDevice;
    Handles.QueryMemory    = Access.QueryMemory;
    Handles.QueryLimits    = Access.QueryLimits;
    const auto PipelineArchPath = (ProjectRoot / "Saved" / "PipelineArchive" / "ComputePipelines.bin").string();

    if (!Exchange.AttachBorrowedDevice(Handles, PipelineArchPath, &Diagnostic))
    {
        return false;
    }

    DescribeViewFn_      = std::move(DescribeTextureView);
    ReleaseDescriptorFn_ = std::move(ReleaseTextureDescriptor);
    BorrowedPresentation = true;

    if (!InitializeRendererOnExchange())
    {
        DetachVulkanPresentation();
        return false;
    }

    ActiveViewportSlot_ = 0u;
    if (!Exchange.AllocateViewportImage2D(DesiredRenderWidth_, DesiredRenderHeight_, ViewportSlots_[0]))
    {
        DetachVulkanPresentation();
        return false;
    }
    Exchange.PrepareViewportImageForSampling(ViewportSlots_[0]);
    ViewportDescriptors_[0] = DescribeViewFn_(ViewportSlots_[0].View2D);
    return true;
}

void EverestWorkspace::DetachVulkanPresentation() noexcept
{
#if FRONTIER_EDITOR
    if (TelemetryJournal.IsOpen())
    {
        TelemetryJournal.CloseSession(Exchange.Ready() ? &Exchange : nullptr);
    }
#endif

    if (Exchange.Ready())
    {
        Exchange.WaitDeviceIdle();
        for (size_t i = 0; i < ViewportSlots_.size(); ++i)
        {
            if (ViewportDescriptors_[i] != 0u && ReleaseDescriptorFn_)
            {
                ReleaseDescriptorFn_(ViewportDescriptors_[i]);
                ViewportDescriptors_[i] = 0u;
            }
            Exchange.ReleaseViewportImage2D(ViewportSlots_[i]);
        }
#if FRONTIER_EDITOR
        FastPreview.Shutdown(Exchange);
#endif
        Illumination.Shutdown(Exchange);
        Exchange.Shutdown();
    }
    ViewportDescriptors_ = {0u, 0u};
    DescribeViewFn_      = {};
    ReleaseDescriptorFn_ = {};
    RendererReady        = false;
    BorrowedPresentation = false;
}

void EverestWorkspace::ConstructGpuCommands(VkCommandBuffer CommandBuffer,
                                            uint64_t        CompletedSerial,
                                            uint64_t        UpcomingSerial)
{
    (void)CompletedSerial;
    if (!BorrowedPresentation || !RendererReady || CommandBuffer == VK_NULL_HANDLE)
    {
        return;
    }

    auto& Slot = ViewportSlots_[ActiveViewportSlot_];
    if (Slot.Width != DesiredRenderWidth_ || Slot.Height != DesiredRenderHeight_ || Slot.Image == VK_NULL_HANDLE)
    {
        Exchange.WaitDeviceIdle();
        if (ViewportDescriptors_[ActiveViewportSlot_] != 0u && ReleaseDescriptorFn_)
        {
            ReleaseDescriptorFn_(ViewportDescriptors_[ActiveViewportSlot_]);
            ViewportDescriptors_[ActiveViewportSlot_] = 0u;
        }
        Exchange.ReleaseViewportImage2D(Slot);
        if (!Exchange.AllocateViewportImage2D(DesiredRenderWidth_, DesiredRenderHeight_, Slot))
        {
            return;
        }
        Exchange.PrepareViewportImageForSampling(Slot);
        ViewportDescriptors_[ActiveViewportSlot_] = DescribeViewFn_(Slot.View2D);
    }

    Frontier::SpatiotemporalCamera Camera{};
    Camera.OriginMeters   = FlightRig.OriginMeters();
    Camera.TargetMeters   = FlightRig.TargetMeters(5.0f);
    Camera.UpUnit         = {0.0f, 1.0f, 0.0f};
    Camera.VerticalFovRad = FlightRig.VerticalFovRadians();
    Camera.WidthPx        = Slot.Width;
    Camera.HeightPx       = Slot.Height;

    std::vector<Frontier::AnalyticalLuminaire> Luminaires;
    Frontier::SpatiotemporalRenderConfig       Config{};
    SyncDynamicWorldToRenderer(Camera, Luminaires, Config);

#if FRONTIER_EDITOR
    if (UseFastPreviewRaster_)
    {
        Frontier::ClusterCameraView ClusterCam{};
        ClusterCam.CameraOrigin     = {Camera.OriginMeters[0], Camera.OriginMeters[1], Camera.OriginMeters[2]};
        ClusterCam.CameraTarget     = {Camera.TargetMeters[0], Camera.TargetMeters[1], Camera.TargetMeters[2]};
        ClusterCam.CameraUp         = {0.0f, 1.0f, 0.0f};
        ClusterCam.VerticalFovRad   = Camera.VerticalFovRad;
        ClusterCam.ViewportWidthPx  = Slot.Width;
        ClusterCam.ViewportHeightPx = Slot.Height;

        const auto SunDir = Luminaires.empty() ? std::array<float, 3>{0.42f, 0.76f, 0.50f} : Luminaires[0].DirectionOrPosition;
        Frontier::PreviewAtmosphereBackdrop Backdrop{};
        Backdrop.SolarDirectionUnit = {SunDir[0], SunDir[1], SunDir[2]};
        Backdrop.ZenithSkyRgb       = {Config.ZenithSkyRgb[0], Config.ZenithSkyRgb[1], Config.ZenithSkyRgb[2]};
        Backdrop.HorizonSkyRgb      = {Config.HorizonSkyRgb[0], Config.HorizonSkyRgb[1], Config.HorizonSkyRgb[2]};
        Backdrop.ExposureScale      = Config.ExposureScale;
        Backdrop.ShadowMode         = static_cast<Frontier::PreviewShadowMode>(std::clamp(PreviewShadowIndex_, 0, 4));

        Frontier::PreviewFrameExecution Exec{};
        if (FastPreview.RenderView(Exchange,
                                   ClusterRefinement,
                                   ClusterCam,
                                   static_cast<Frontier::PreviewViewMode>(std::clamp(PreviewViewIndex_, 0, 9)),
                                   Backdrop,
                                   &Exec))
        {
            std::vector<uint8_t> PreviewBytes;
            if (FastPreview.ReadbackTonemappedSrgb8(Exchange, Config.ExposureScale, PreviewBytes))
            {
                Exchange.UploadViewportImageRgba8(Slot, PreviewBytes);
            }
            LatestTelemetry                 = {};
            LatestTelemetry.TotalGpuMs      = Exchange.QueryLastDispatchGpuMilliseconds();
            LatestTelemetry.PrimaryGpuMs    = LatestTelemetry.TotalGpuMs;
            LatestTelemetry.CpuSubmissionMs = Exchange.QueryLastDispatchCpuMilliseconds();
        }
    }
    else
#endif
    {
        const uint32_t RingSlot = static_cast<uint32_t>(UpcomingSerial % Frontier::VulkanComputeExchange::ExternalRingCount);
        Illumination.RecordFrameIntoExternalStream(Exchange,
                                                   CommandBuffer,
                                                   RingSlot,
                                                   Camera,
                                                   Luminaires,
                                                   Config,
                                                   Slot,
                                                   &LatestTelemetry);
    }

    if (auto* DiagRec = dynamic_cast<Frontier::DiagnosticsRecord*>(QuerySceneWorld().FindFirstByType("Diagnostics")))
    {
        auto&        Diag    = DiagRec->AccessConfiguration();
        Diag.GpuRayQueryMs   = LatestTelemetry.PrimaryGpuMs + LatestTelemetry.SpatialReflectGpuMs;
        Diag.GpuLightingMs   = LatestTelemetry.RadiosityGpuMs + LatestTelemetry.DirectRisGpuMs + LatestTelemetry.IndirectGiGpuMs;
        Diag.GpuVolumetricMs = LatestTelemetry.AtrousResolveGpuMs;
        const size_t SlotIdx = static_cast<size_t>(Application.QuerySimulatedFrames()) % Diag.FrameHistoryMs.size();
        Diag.FrameHistoryMs[SlotIdx] = Diag.FrameTimeMs;
    }

#if FRONTIER_EDITOR
    if (TelemetryJournal.IsOpen())
    {
        uint32_t QualityTier = 2u;
        if (const auto* PipeRec = dynamic_cast<const Frontier::RenderPipelineRecord*>(
                const_cast<Frontier::SceneWorld&>(QuerySceneWorld()).FindFirstByType("RenderPipeline")))
        {
            QualityTier = static_cast<uint32_t>(std::clamp(PipeRec->QueryConfiguration().QualityTier, 0, 2));
        }

        Frontier::IlluminationFrameJournalSample Sample{};
        Sample.ElapsedSeconds    = ElapsedSeconds;
        Sample.RenderWidthPx     = Slot.Width;
        Sample.RenderHeightPx    = Slot.Height;
        Sample.ResolutionPercent = static_cast<uint32_t>(ResolutionScalePct_);
        Sample.PreviewRasterUsed = UseFastPreviewRaster_;
        Sample.QualityTier       = QualityTier;
        Sample.WallFrameMs       = LastDeltaSeconds * 1000.0f;
        Sample.CameraOrigin      = FlightRig.OriginMeters();
        Sample.CameraYawDeg      = FlightRig.YawDegrees();
        Sample.CameraPitchDeg    = FlightRig.PitchDegrees();
        Sample.DeviceVramBytes   = Exchange.QueryAllocatedDeviceBytes();
        Sample.Config            = Config;
        Sample.Telemetry         = LatestTelemetry;
        TelemetryJournal.RecordFrameSample(Sample);
    }
#endif
}

bool EverestWorkspace::RenderOffscreenFrame(uint32_t WidthPx, uint32_t HeightPx, std::vector<uint8_t>& OutSrgbRgbaBytes)
{
    if (!RendererReady)
    {
        if (!Exchange.Ready())
        {
            const auto PipelineArchPath = (ProjectRoot / "Saved" / "PipelineArchive" / "ComputePipelines.bin").string();
            if (!Exchange.Initialize("", &Diagnostic, PipelineArchPath))
            {
                return false;
            }
        }
        if (!InitializeRendererOnExchange())
        {
            return false;
        }
    }

    Frontier::SpatiotemporalCamera Camera{};
    Camera.OriginMeters   = FlightRig.OriginMeters();
    Camera.TargetMeters   = FlightRig.TargetMeters(5.0f);
    Camera.UpUnit         = {0.0f, 1.0f, 0.0f};
    Camera.VerticalFovRad = FlightRig.VerticalFovRadians();
    Camera.WidthPx        = std::max(64u, WidthPx);
    Camera.HeightPx       = std::max(64u, HeightPx);

    std::vector<Frontier::AnalyticalLuminaire> Luminaires;
    Frontier::SpatiotemporalRenderConfig       Config{};
    SyncDynamicWorldToRenderer(Camera, Luminaires, Config);

    if (!Illumination.RenderFrame(Exchange, Camera, Luminaires, Config, &LatestTelemetry))
    {
        return false;
    }
    return Illumination.ReadbackTonemappedSrgb8(Exchange, Config.ExposureScale, OutSrgbRgbaBytes);
}

void EverestWorkspace::IntegrateSimulation(double DeltaSeconds)
{
    const float ClampedDt = static_cast<float>(std::clamp(DeltaSeconds, 0.0, 0.25));
    LastDeltaSeconds      = std::max(1.0e-4f, ClampedDt);
    ElapsedSeconds += static_cast<double>(ClampedDt);

    Application.StepFrame(ClampedDt);

    if (auto* DiagRecord = dynamic_cast<Frontier::DiagnosticsRecord*>(QuerySceneWorld().FindFirstByType("Diagnostics")))
    {
        if (ClampedDt > 1.0e-5f)
        {
            auto&       Diag     = DiagRecord->AccessConfiguration();
            const float Instant  = ClampedDt * 1000.0f;
            Diag.FrameTimeMs     = Diag.FrameTimeMs * 0.9f + Instant * 0.1f;
            Diag.FramesPerSecond = 1000.0f / std::max(0.25f, Diag.FrameTimeMs);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                  4. EDITOR INTERFACE, RIBBON & INTERACTIVE FLIGHT
//------------------------------------------------------------------------------------------------------------------------

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

bool EverestWorkspace::ConfigurePlatform() noexcept
{
    Dockspace.ConfigureDetachedWindows(Centre.QueryDetachedWindows());
    return Dockspace.ConfigurePlatform();
}

void EverestWorkspace::ServiceDynamicSceneRequests()
{
    const uint32_t ProjectCategory = EnsureProjectLogCategory("Everest");
    bool           Mutated         = false;

    while (const auto RequestedType = OutlinerPanel.ConsumeConstructRequest())
    {
        Frontier::RecordToken ParentToken{};
        if (*RequestedType == "OpticalFlare")
        {
            if (Frontier::SceneRecordBase* Solar = QuerySceneWorld().FindFirstByType("Solar"))
            {
                ParentToken = Solar->QueryToken();
            }
        }
        const Frontier::RecordToken Created = QuerySceneWorld().ConstructRecord(*RequestedType, {}, ParentToken);
        if (Created.IsValid())
        {
            OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());
            OutlinerPanel.Select(Created);
            Mutated = true;
            Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log,
                                     "EverestWorkspace", "Added record '%s' (type '%s', token #%llu).",
                                     Frontier::SceneDirectory::Instance().QueryName(Created).c_str(),
                                     RequestedType->c_str(),
                                     static_cast<unsigned long long>(Created.Value));
        }
    }

    while (const auto DeleteToken = OutlinerPanel.ConsumeDeleteRequest())
    {
        const std::string OldName = Frontier::SceneDirectory::Instance().QueryName(*DeleteToken);
        if (QuerySceneWorld().DestroyRecord(*DeleteToken))
        {
            Mutated = true;
            Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log,
                                     "EverestWorkspace", "Removed record '%s' (token #%llu).",
                                     OldName.c_str(),
                                     static_cast<unsigned long long>(DeleteToken->Value));
        }
    }

    if (Mutated)
    {
        OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());
    }
}

void EverestWorkspace::RefreshHeadlessViewportTexture(uint32_t TargetW, uint32_t TargetH)
{
    const auto& IO = ImGui::GetIO();
    if (!(IO.BackendFlags & ImGuiBackendFlags_RendererHasTextures))
    {
        return;
    }

    if (!HeadlessVulkanChecked)
    {
        HeadlessVulkanChecked = true;
        const auto PipelineArchPath = (ProjectRoot / "Saved" / "PipelineArchive" / "ComputePipelines.bin").string();
        HeadlessVulkanReady = Exchange.Initialize("", &Diagnostic, PipelineArchPath) && InitializeRendererOnExchange();
    }
    if (!HeadlessVulkanReady)
    {
        return;
    }

    uint64_t Sig = 14695981039346656037ull;
    auto MixFloat = [&](float V) {
        uint32_t Bits = 0u;
        std::memcpy(&Bits, &V, sizeof(uint32_t));
        Sig = (Sig ^ Bits) * 1099511628211ull;
    };
    const auto CamPos = FlightRig.OriginMeters();
    MixFloat(CamPos[0]);
    MixFloat(CamPos[1]);
    MixFloat(CamPos[2]);
    MixFloat(FlightRig.YawRadians());
    MixFloat(FlightRig.PitchRadians());
    for (const auto& Entry : QuerySceneWorld().QueryRecords())
    {
        if (!Entry)
        {
            continue;
        }
        const std::string& Type = Entry->QueryTypeIdentifier();
        if (Type == "Solar")
        {
            const auto& C = static_cast<const Frontier::SolarRecord*>(Entry.get())->QueryConfiguration();
            MixFloat(C.ElevationDegrees);
            MixFloat(C.AzimuthDegrees);
            MixFloat(C.IlluminanceKilolux);
        }
        else if (Type == "ThermalBody")
        {
            MixFloat(static_cast<const Frontier::ThermalBodyRecord*>(Entry.get())->QueryThermal().TemperatureKelvin);
        }
        else if (Type == "RenderPipeline")
        {
            MixFloat(static_cast<float>(
                static_cast<const Frontier::RenderPipelineRecord*>(Entry.get())->QueryConfiguration().InspectionModeIndex));
        }
    }

    const uint32_t RenderW   = std::clamp(TargetW, 320u, 640u);
    const uint32_t RenderH   = std::clamp(TargetH, 180u, 360u);
    const bool     NeedAlloc = (!HeadlessTexture || HeadlessTexture->Width != static_cast<int>(RenderW) ||
                            HeadlessTexture->Height != static_cast<int>(RenderH));
    if (!NeedAlloc && Sig == HeadlessSceneSignature)
    {
        return;
    }

    std::vector<uint8_t> Pixels;
    const int            WarmupPasses = NeedAlloc ? 2 : 1;
    for (int p = 0; p < WarmupPasses; ++p)
    {
        if (!RenderOffscreenFrame(RenderW, RenderH, Pixels))
        {
            return;
        }
    }

    if (NeedAlloc)
    {
        if (HeadlessTexture)
        {
            ImGui::UnregisterUserTexture(HeadlessTexture.get());
            auto& Textures = ImGui::GetPlatformIO().Textures;
            for (int I = Textures.Size - 1; I >= 0; --I)
            {
                if (Textures[I] == HeadlessTexture.get())
                {
                    Textures.erase(Textures.Data + I);
                }
            }
        }
        HeadlessTexture = std::make_unique<ImTextureData>();
        HeadlessTexture->Create(ImTextureFormat_RGBA32, static_cast<int>(RenderW), static_cast<int>(RenderH));
        HeadlessTexture->UseColors = true;
        std::memcpy(HeadlessTexture->GetPixels(), Pixels.data(), Pixels.size());
        ImGui::RegisterUserTexture(HeadlessTexture.get());
    }
    else
    {
        std::memcpy(HeadlessTexture->GetPixels(), Pixels.data(), Pixels.size());
        HeadlessTexture->SetStatus(ImTextureStatus_WantUpdates);
    }
    HeadlessSceneSignature = Sig;
}

void EverestWorkspace::ConstructInterface(double DeltaSeconds)
{
    IntegrateSimulation(DeltaSeconds);

    if (ObservedLayoutRevision != Centre.QueryLayoutResetRevision())
    {
        ObservedLayoutRevision = Centre.QueryLayoutResetRevision();
        Dockspace.ResetLayout();
        (void)Dockspace.ConfigureStackedPanels("Viewport", "LogStream", 520.0f);
        (void)Dockspace.ConfigureColumns({
            {"Outliner", 0.200f},
            {"LogStream", 0.545f},
            {"Inspector", 0.255f},
        });
    }

    ServiceDynamicSceneRequests();
    OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());
    LogPanel.Synchronize(Frontier::LogStore(), Frontier::LogCategories(), DeltaSeconds);

    const float Scale = Centre.QueryAppearance().QueryApplied().InterfaceScale / 100.0f;
    ConstructTopToolbar(Scale);
    Dockspace.ConstructInterface(Scale);
    Frontier::InspectorRuntime::Instance().ConstructActiveEditor();
}

void EverestWorkspace::ConstructTopToolbar(float Scale)
{
    const Frontier::InspectorPalette Palette = Frontier::InspectorPalette::Resolve();
    const Frontier::EditorTypefaces  Faces   = Frontier::EditorTypefaces::Resolve();
    const ImGuiViewport*             MainVp  = ImGui::GetMainViewport();
    const ImVec2                     BarPos  = MainVp->Pos;
    const ImVec2                     BarSize(MainVp->Size.x, 50.0f);

    ImGui::SetNextWindowPos(BarPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(BarSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(Palette.ColumnBg));
    if (ImGui::Begin("##EverestTopToolbar", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus))
    {
        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        DrawList->AddLine(ImVec2(BarPos.x, BarPos.y + BarSize.y - 1.0f),
                          ImVec2(BarPos.x + BarSize.x, BarPos.y + BarSize.y - 1.0f),
                          Palette.EdgeBorder, 1.0f);

        float       CursorX = BarPos.x + 14.0f;
        const float BtnY    = BarPos.y + 10.0f;
        const float BtnH    = 30.0f;

        if (EvaluateReturnToBrowser)
        {
            const float BackW = 104.0f;
            ImGui::SetCursorScreenPos(ImVec2(CursorX, BtnY));
            if (ImGui::InvisibleButton("##ReturnProjectsBtn", ImVec2(BackW, BtnH)))
            {
                EvaluateReturnToBrowser();
            }
            const bool Hover = ImGui::IsItemHovered();
            DrawList->AddRectFilled(ImVec2(CursorX, BtnY), ImVec2(CursorX + BackW, BtnY + BtnH),
                                    Hover ? Palette.SegmentedActiveBg : Palette.CountCardBg, 8.0f);
            DrawList->AddRect(ImVec2(CursorX, BtnY), ImVec2(CursorX + BackW, BtnY + BtnH), Palette.CountCardBorder, 8.0f);
            Frontier::ConstructEditorGlyph(DrawList, ImVec2(CursorX + 15.0f, BtnY + BtnH * 0.5f), 4.5f,
                                           Palette.TextSecondary, "ChevronRight", 1.4f);
            DrawList->AddText(Faces.Medium, 11.5f * Scale, ImVec2(CursorX + 26.0f, BtnY + 8.5f),
                              Palette.TextPrimary, "Projects");
            CursorX += BackW + 14.0f;
        }

        Frontier::ConstructEditorGlyph(DrawList, ImVec2(CursorX + 8.0f, BarPos.y + 25.0f), 7.0f,
                                       Palette.ThemeAccent, "Layers", 1.5f);
        DrawList->AddText(Faces.SemiBold, 13.5f, ImVec2(CursorX + 22.0f, BarPos.y + 17.0f),
                          Palette.TextPrimary, "Everest");
        CursorX += 22.0f + MeasureLabelWidth(Faces.SemiBold, 13.5f, "Everest") + 12.0f;

        const std::string LevelFilename = ActiveLevelPath.empty() ? "Default.levelspace" : ActiveLevelPath.filename().string();
        char              LevelBadge[128]{};
        std::snprintf(LevelBadge, sizeof(LevelBadge), "%s  ·  %zu records",
                      LevelFilename.c_str(), QuerySceneWorld().QueryRecordCount());
        const float BadgeW = MeasureLabelWidth(Faces.Monospace, 10.5f, LevelBadge) + 18.0f;
        DrawList->AddRectFilled(ImVec2(CursorX, BtnY + 3.0f), ImVec2(CursorX + BadgeW, BtnY + BtnH - 3.0f),
                                Palette.InsetFieldBg, 6.0f);
        DrawList->AddRect(ImVec2(CursorX, BtnY + 3.0f), ImVec2(CursorX + BadgeW, BtnY + BtnH - 3.0f),
                          Palette.InsetFieldBorder, 6.0f);
        DrawList->AddText(Faces.Monospace, 10.5f, ImVec2(CursorX + 9.0f, BtnY + 9.0f),
                          Palette.TextSecondary, LevelBadge);

        float RightX = BarPos.x + BarSize.x - 14.0f;

        {
            const float SettingsW = 148.0f;
            RightX -= SettingsW;
            ImGui::SetCursorScreenPos(ImVec2(RightX, BtnY));
            if (ImGui::InvisibleButton("##EverestOpenAppearanceBtn", ImVec2(SettingsW, BtnH)))
            {
                Centre.ActivateSettingsPage(Frontier::ControlCentrePageCategory::Appearance);
            }
            const bool Hover = ImGui::IsItemHovered();
            DrawList->AddRectFilled(ImVec2(RightX, BtnY), ImVec2(RightX + SettingsW, BtnY + BtnH),
                                    Hover ? Palette.SegmentedActiveBg : Palette.CountCardBg, 8.0f);
            DrawList->AddRect(ImVec2(RightX, BtnY), ImVec2(RightX + SettingsW, BtnY + BtnH),
                              Palette.CountCardBorder, 8.0f);
            Frontier::ConstructEditorGlyph(DrawList, ImVec2(RightX + 16.0f, BtnY + BtnH * 0.5f), 6.0f,
                                           Palette.ThemeAccent, "Sliders", 1.4f);
            DrawList->AddText(Faces.Medium, 11.0f, ImVec2(RightX + 29.0f, BtnY + 9.0f),
                              Palette.TextPrimary, "Control Centre");
            RightX -= 10.0f;
        }

        {
            const float SaveW = 132.0f;
            RightX -= SaveW;
            ImGui::SetCursorScreenPos(ImVec2(RightX, BtnY));
            if (ImGui::InvisibleButton("##EverestSaveLevelBtn", ImVec2(SaveW, BtnH)))
            {
                std::string    SaveDiag;
                const uint32_t ProjectCategory = EnsureProjectLogCategory("Everest");
                const std::filesystem::path TargetPath = ActiveLevelPath.empty()
                                                             ? (ProjectRoot / "Content" / "Levels" / "Default.levelspace")
                                                             : ActiveLevelPath;
                if (QuerySceneWorld().SaveLevelSpace(TargetPath, &SaveDiag))
                {
                    ActiveLevelPath = TargetPath;
                    Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log,
                                             "EverestWorkspace", "Saved '%s' (%zu records).",
                                             TargetPath.filename().string().c_str(),
                                             QuerySceneWorld().QueryRecordCount());
                }
                else
                {
                    Diagnostic = SaveDiag;
                    Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Error, Frontier::LogVerbosity::Log,
                                             "EverestWorkspace", "Save failed: %s", SaveDiag.c_str());
                }
            }
            const bool Hover = ImGui::IsItemHovered();
            DrawList->AddRectFilled(ImVec2(RightX, BtnY), ImVec2(RightX + SaveW, BtnY + BtnH),
                                    Hover ? Palette.SegmentedActiveBg : Palette.CountCardBg, 8.0f);
            DrawList->AddRect(ImVec2(RightX, BtnY), ImVec2(RightX + SaveW, BtnY + BtnH),
                              Palette.CountCardBorder, 8.0f);
            DrawList->AddText(Faces.Medium, 11.0f, ImVec2(RightX + 14.0f, BtnY + 9.0f),
                              Palette.TextSecondary, "Save .levelspace");
            RightX -= 10.0f;
        }

        {
            const char* TierText = UseFastPreviewRaster_
                                       ? "TIER: FAST PREVIEW (Cluster-DAG + Hybrid Shadow)"
                                       : "TIER: GTX COMPUTE (CWBVH + 4-Cascade SDF + ReSTIR)";
            const ImU32 TierTint = UseFastPreviewRaster_ ? Palette.WarmAmber : Palette.LiveGreen;
            const float PillW    = MeasureLabelWidth(Faces.Monospace, 10.0f, TierText) + 26.0f;
            RightX -= PillW;
            DrawList->AddRectFilled(ImVec2(RightX, BtnY + 3.0f), ImVec2(RightX + PillW, BtnY + BtnH - 3.0f),
                                    Frontier::ModulateSpectrumAlpha(TierTint, 0.14f), 6.0f);
            DrawList->AddRect(ImVec2(RightX, BtnY + 3.0f), ImVec2(RightX + PillW, BtnY + BtnH - 3.0f),
                              Frontier::ModulateSpectrumAlpha(TierTint, 0.45f), 6.0f);
            DrawList->AddCircleFilled(ImVec2(RightX + 10.0f, BtnY + BtnH * 0.5f), 3.0f, TierTint);
            DrawList->AddText(Faces.Monospace, 10.0f, ImVec2(RightX + 18.0f, BtnY + 9.0f), TierTint, TierText);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void EverestWorkspace::ConstructViewportRibbon(const ImVec2& BarMin, const ImVec2& BarMax, float Scale)
{
    const Frontier::InspectorPalette Palette = Frontier::InspectorPalette::Resolve();
    ImDrawList*                      Draw    = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(BarMin, BarMax, Palette.ColumnBg);
    Draw->AddLine(ImVec2(BarMin.x, BarMax.y - 1.0f),
                  ImVec2(BarMax.x, BarMax.y - 1.0f),
                  Palette.EdgeBorder,
                  1.0f);

    auto* PipeRec  = dynamic_cast<Frontier::RenderPipelineRecord*>(QuerySceneWorld().FindFirstByType("RenderPipeline"));
    auto* SolarRec = dynamic_cast<Frontier::SolarRecord*>(QuerySceneWorld().FindFirstByType("Solar"));

    ImGui::SetCursorScreenPos(ImVec2(BarMin.x + 8.0f * Scale, BarMin.y + 4.0f * Scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f * Scale, 3.0f * Scale));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(Palette.InsetFieldBg));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::ColorConvertU32ToFloat4(Palette.SegmentedActiveBg));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(Palette.ColumnBg));
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(Palette.CountCardBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(Palette.SegmentedActiveBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(Palette.SegmentedActiveBg));

    // 1. Renderer mode toggle (ReSTIR GTX vs Editor-only Fast Preview)
    {
        const bool RestirActive = !UseFastPreviewRaster_;
        if (RestirActive)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(Palette.SegmentedActiveBg));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Palette.ThemeAccent));
        }
        if (ImGui::Button("ReSTIR GTX"))
        {
            UseFastPreviewRaster_ = false;
        }
        if (RestirActive)
        {
            ImGui::PopStyleColor(2);
        }

        ImGui::SameLine(0.0f, 4.0f * Scale);
        if (UseFastPreviewRaster_)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(Palette.SegmentedActiveBg));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Palette.ThemeAccent));
        }
        if (ImGui::Button("Preview"))
        {
            UseFastPreviewRaster_ = true;
        }
        if (UseFastPreviewRaster_)
        {
            ImGui::PopStyleColor(2);
        }
    }

    // 2. View / Inspection channel selector
    ImGui::SameLine(0.0f, 8.0f * Scale);
    ImGui::SetNextItemWidth(134.0f * Scale);
    if (!UseFastPreviewRaster_ && PipeRec != nullptr)
    {
        auto& Cfg  = PipeRec->AccessConfiguration();
        int   Mode = std::clamp(Cfg.InspectionModeIndex, 0, 14);
        if (ImGui::Combo("##InspectCombo", &Mode, Frontier::RenderInspectionModes::ModeNames, 15))
        {
            Cfg.InspectionModeIndex = Mode;
            Cfg.AdvanceRevision();
        }
    }
    else
    {
        static const char* const kPreviewViews[] = {
            "Shaded Lit",
            "Wireframe",
            "Cluster IDs",
            "Page Slots",
            "LOD Tier Heat",
            "Pixel Error",
            "Material Tier",
            "Normals",
            "UV Mip Grid",
            "Glass Peel",
        };
        ImGui::Combo("##PreviewCombo", &PreviewViewIndex_, kPreviewViews, 10);
    }

    // 3. Quality tier / Shadow mode + Resolution scale
    if (!UseFastPreviewRaster_ && PipeRec != nullptr)
    {
        ImGui::SameLine(0.0f, 6.0f * Scale);
        ImGui::SetNextItemWidth(82.0f * Scale);
        static const char* const kQualityNames[] = {"Low", "Medium", "Ultra"};
        auto&                    Cfg             = PipeRec->AccessConfiguration();
        int                      QTier           = std::clamp(Cfg.QualityTier, 0, 2);
        if (ImGui::Combo("##QualityCombo", &QTier, kQualityNames, 3))
        {
            Cfg.QualityTier = QTier;
            Cfg.AdvanceRevision();
        }
    }
    else
    {
        ImGui::SameLine(0.0f, 6.0f * Scale);
        ImGui::SetNextItemWidth(104.0f * Scale);
        static const char* const kShadowNames[] = {"No Shadow", "Contact", "Ortho PCF", "SDF Cone", "Hybrid SDF"};
        ImGui::Combo("##ShadowCombo", &PreviewShadowIndex_, kShadowNames, 5);
    }

    ImGui::SameLine(0.0f, 6.0f * Scale);
    ImGui::SetNextItemWidth(68.0f * Scale);
    static const char* const kScaleLabels[] = {"50%", "75%", "100%"};
    int                      ScaleIdx       = (ResolutionScalePct_ <= 55) ? 0 : ((ResolutionScalePct_ <= 85) ? 1 : 2);
    if (ImGui::Combo("##ScaleCombo", &ScaleIdx, kScaleLabels, 3))
    {
        ResolutionScalePct_ = (ScaleIdx == 0) ? 50 : ((ScaleIdx == 1) ? 75 : 100);
    }

    // 4. Quick Sun Elevation slider & Reset Cam button
    if (SolarRec != nullptr && (BarMax.x - ImGui::GetCursorScreenPos().x) > 200.0f * Scale)
    {
        ImGui::SameLine(0.0f, 8.0f * Scale);
        ImGui::SetNextItemWidth(92.0f * Scale);
        auto& SolarCfg = SolarRec->AccessConfiguration();
        if (ImGui::SliderFloat("##SunEl", &SolarCfg.ElevationDegrees, -8.0f, 85.0f, "Sun %.0f°"))
        {
            SolarCfg.AdvanceRevision();
        }
    }

    if ((BarMax.x - ImGui::GetCursorScreenPos().x) > 84.0f * Scale)
    {
        ImGui::SameLine(0.0f, 6.0f * Scale);
        if (ImGui::Button("Reset Cam"))
        {
            FlightRig.ResetToDefaultHeroPose();
            Illumination.ResetTemporalHistory();
        }
    }

    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(2);
}

void EverestWorkspace::ConstructViewportSurface()
{
    const Frontier::InspectorPalette Palette = Frontier::InspectorPalette::Resolve();
    const Frontier::EditorTypefaces  Faces   = Frontier::EditorTypefaces::Resolve();
    const float                      Scale   = std::max(0.85f, Centre.QueryAppearance().QueryApplied().InterfaceScale / 100.0f);

    const ImVec2 Avail    = ImGui::GetContentRegionAvail();
    const float  TotalW   = std::max(64.0f, Avail.x);
    const float  TotalH   = std::max(96.0f, Avail.y);
    const float  RibbonH  = std::round(32.0f * Scale);
    const ImVec2 PanelMin = ImGui::GetCursorScreenPos();

    ConstructViewportRibbon(PanelMin, ImVec2(PanelMin.x + TotalW, PanelMin.y + RibbonH), Scale);

    const ImVec2 Min(PanelMin.x, PanelMin.y + RibbonH);
    const float  W = TotalW;
    const float  H = std::max(64.0f, TotalH - RibbonH);
    const ImVec2 Max(Min.x + W, Min.y + H);

    const float ScaleFactor = static_cast<float>(ResolutionScalePct_) * 0.01f;
    DesiredRenderWidth_     = std::clamp(((static_cast<uint32_t>(W * ScaleFactor) + 7u) / 8u) * 8u, 256u, 2560u);
    DesiredRenderHeight_    = std::clamp(((static_cast<uint32_t>(H * ScaleFactor) + 7u) / 8u) * 8u, 144u, 1440u);

    ImGui::SetCursorScreenPos(Min);
    ImGui::InvisibleButton("##SceneViewportCanvas",
                           ImVec2(W, H),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool  Hovered = ImGui::IsItemHovered();
    const auto& IO      = ImGui::GetIO();

    if (Hovered || FlightRig.IsCapturingLook())
    {
        const auto&                    InputPrefs = Centre.QueryInput().QueryApplied();
        Frontier::ViewportFlightInputs FlightIn{};
        const bool                     RmbHeld = ImGui::IsMouseDown(ImGuiMouseButton_Right) && (Hovered || FlightRig.IsCapturingLook());
        FlightIn.LookActive                    = RmbHeld;
        FlightIn.ReleaseCapture                = ImGui::IsKeyPressed(ImGuiKey_Escape);
        FlightIn.ResetPose                     = Hovered && ImGui::IsKeyPressed(ImGuiKey_R) && !IO.WantTextInput;
        FlightIn.MouseSensitivity              = InputPrefs.MouseSensitivity;
        FlightIn.InvertPitch                   = InputPrefs.InvertPitch;

        if (RmbHeld || (Hovered && !IO.WantTextInput))
        {
            FlightIn.MoveForward     = ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow);
            FlightIn.MoveBackward    = ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow);
            FlightIn.StrafeLeft      = ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow);
            FlightIn.StrafeRight     = ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow);
            FlightIn.AscendVertical  = ImGui::IsKeyDown(ImGuiKey_E) || ImGui::IsKeyDown(ImGuiKey_Space);
            FlightIn.DescendVertical = ImGui::IsKeyDown(ImGuiKey_Q) || ImGui::IsKeyDown(ImGuiKey_C);
            FlightIn.SprintBoost     = IO.KeyShift;
            FlightIn.PrecisionSlow   = IO.KeyCtrl;
            FlightIn.MouseDeltaX     = IO.MouseDelta.x;
            FlightIn.MouseDeltaY     = IO.MouseDelta.y;
            FlightIn.WheelStep       = Hovered ? IO.MouseWheel : 0.0f;
        }

        FlightRig.IntegrateInputs(FlightIn, static_cast<double>(LastDeltaSeconds));
    }

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    DrawList->PushClipRect(Min, Max, true);

    const uint64_t LiveTexDesc = ViewportDescriptors_[ActiveViewportSlot_];
    if (BorrowedPresentation && LiveTexDesc != 0u)
    {
        DrawList->AddImage(static_cast<ImTextureID>(LiveTexDesc), Min, Max);
    }
    else
    {
        RefreshHeadlessViewportTexture(static_cast<uint32_t>(W * 0.5f), static_cast<uint32_t>(H * 0.5f));
        if (HeadlessTexture)
        {
            DrawList->AddImage(HeadlessTexture->GetTexRef(), Min, Max);
        }
        else
        {
            DrawList->AddRectFilled(Min, Max, Palette.ColumnBg);
        }
    }

    // ThemeStructure-bound viewport HUD overlay card
    {
        const auto CamPos = FlightRig.OriginMeters();
        char       HudLine1[160]{};
        char       HudLine2[192]{};
        std::snprintf(HudLine1,
                      sizeof(HudLine1),
                      "%s  ·  %ux%u (%d%%)  ·  Cam (%.1f, %.1f, %.1f)  %.1f m/s",
                      Exchange.DeviceName().empty() ? "Vulkan 1.3 Compute" : Exchange.DeviceName().c_str(),
                      DesiredRenderWidth_,
                      DesiredRenderHeight_,
                      ResolutionScalePct_,
                      static_cast<double>(CamPos[0]),
                      static_cast<double>(CamPos[1]),
                      static_cast<double>(CamPos[2]),
                      static_cast<double>(FlightRig.BaseSpeedMetersPerSec()));
        std::snprintf(HudLine2,
                      sizeof(HudLine2),
                      "GPU %.2f ms [Rad %.2f · Prim %.2f · DI %.2f · GI %.2f · Refl %.2f · Atr %.2f]  |  RMB+WASD/QE · Shift 3x · R Reset",
                      static_cast<double>(LatestTelemetry.TotalGpuMs),
                      static_cast<double>(LatestTelemetry.RadiosityGpuMs),
                      static_cast<double>(LatestTelemetry.PrimaryGpuMs),
                      static_cast<double>(LatestTelemetry.DirectRisGpuMs),
                      static_cast<double>(LatestTelemetry.IndirectGiGpuMs),
                      static_cast<double>(LatestTelemetry.SpatialReflectGpuMs),
                      static_cast<double>(LatestTelemetry.AtrousResolveGpuMs));

        const float  LineW   = std::max(MeasureLabelWidth(Faces.Monospace, 9.8f, HudLine1),
                                        MeasureLabelWidth(Faces.Monospace, 9.5f, HudLine2));
        const float  CardW   = std::min(W - 24.0f, LineW + 24.0f);
        const float  CardH   = 40.0f;
        const ImVec2 CardMin(Min.x + 12.0f, Max.y - CardH - 10.0f);
        const ImVec2 CardMax(CardMin.x + CardW, CardMin.y + CardH);

        DrawList->AddRectFilled(CardMin, CardMax, Frontier::ModulateSpectrumAlpha(Palette.ColumnBg, 0.84f), 6.0f);
        DrawList->AddRect(CardMin, CardMax, Palette.CountCardBorder, 6.0f);
        DrawList->AddText(Faces.Monospace, 9.8f, ImVec2(CardMin.x + 11.0f, CardMin.y + 6.0f), Palette.TextPrimary, HudLine1);
        DrawList->AddText(Faces.Monospace, 9.5f, ImVec2(CardMin.x + 11.0f, CardMin.y + 21.5f), Palette.TextSecondary, HudLine2);
    }

    DrawList->PopClipRect();
}

#endif // FRONTIER_EDITOR && FRONTIER_WITH_INTERFACE_SDK

} // namespace Everest
