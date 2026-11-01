#============================================================================================================================================
#                                                     ENVIRONMENTRENDERING.CMAKE
#============================================================================================================================================
# 🧩 Multi-instance PlanetaryAtmosphere + 8-Trigger ForceField + GeometryProcessing (QEM Cluster Refinement, Headless .sdfspace
#    VolumeDistanceArchive, Software SAH BoundingVolumeAtlas, Two-Level 8-Wide OctetVolumeAtlas, 4-Cascade RangeDistanceAtlas) +
#    (#if FRONTIER_EDITOR) PreviewRaster + Option A GTX-First SpatiotemporalIllumination (GPU SurfaceCard Radiosity, LuminousFacet CDF,
#    Motion-Reprojected ReSTIR DI & GI, Heitz VNDF Reflections, Curved Dielectric Glazing, & Split Diffuse/Specular À-Trous).

set(FRONTIER_ENV_RENDERING_SOURCES
    Engine/ContentInterchange/VolumeDistanceField.cpp
    Frontier/Engine/DeviceExchange/VulkanComputeExchange.cpp
    Frontier/Engine/Geometry/PrimitiveGeometryGenerator.cpp
    Frontier/Engine/Geometry/GeometryProcessing/FacetTierBuild.cpp
    Frontier/Engine/Geometry/GeometryProcessing/QuadricTierBuild.cpp
    Frontier/Engine/Geometry/GeometryProcessing/ScreenSpaceClusterRefinement.cpp
    Frontier/Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.cpp
    Frontier/Engine/Geometry/GeometryProcessing/BoundingVolumeAtlas.cpp
    Frontier/Engine/Diagnostics/IlluminationTelemetryJournal.cpp
    Frontier/Engine/Geometry/GeometryProcessing/OctetVolumeAtlas.cpp
    Frontier/Engine/Geometry/GeometryProcessing/RangeDistanceAtlas.cpp
    Frontier/Engine/GeometricRaster/VisibilityProjection.cpp
    Frontier/Engine/GeometricRaster/ViewportFlightRig.cpp
    Frontier/Engine/GeometricRaster/PreviewRaster.cpp
    Frontier/Engine/PhysicalDynamics/ForceField.cpp
    Frontier/Engine/PlanetaryAtmosphere/AtmosphereScattering.cpp
    Frontier/Engine/PlanetaryAtmosphere/AstronomicalEphemeris.cpp
    Frontier/Engine/PlanetaryAtmosphere/AtmosphereProjection.cpp
    Frontier/Engine/PlanetaryAtmosphere/PlanetaryProjection.cpp
    Frontier/Engine/PlanetaryAtmosphere/CloudSpace.cpp
    Frontier/Engine/PlanetaryAtmosphere/VolumetricClouds.cpp
    Frontier/Engine/PlanetaryAtmosphere/FogSpace.cpp
    Frontier/Engine/PlanetaryAtmosphere/OpticalLensFlare.cpp
    Frontier/Engine/PlanetaryAtmosphere/PlanetaryEnsemble.cpp
    Frontier/Engine/PlanetaryAtmosphere/WeatherEffects.cpp
    Frontier/Engine/Rendering/EnvironmentUnits.cpp
    Frontier/Engine/Rendering/SubmissionOrder.cpp
    Frontier/Engine/Rendering/TransientTargetAtlas.cpp
    Frontier/Engine/Rendering/BarrierDerivation.cpp
    Frontier/Engine/Rendering/SurfaceRadiosityAtlas.cpp
    Frontier/Engine/Rendering/LuminousFacetAtlas.cpp
    Frontier/Engine/Rendering/SurfaceScatteringAtlas.cpp
    Frontier/Engine/Rendering/SpatiotemporalIllumination.cpp)

add_library(FrontierEnvironmentRendering STATIC ${FRONTIER_ENV_RENDERING_SOURCES})
target_include_directories(FrontierEnvironmentRendering PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/Frontier"
    "${CMAKE_SOURCE_DIR}/Engine/Interface"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include")
target_include_directories(FrontierEnvironmentRendering PRIVATE
    "${CMAKE_SOURCE_DIR}/ExternalPackages/stb"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include")
target_compile_definitions(FrontierEnvironmentRendering PUBLIC VK_NO_PROTOTYPES)
target_link_libraries(FrontierEnvironmentRendering PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)

# Dedicated Shipping variant compiled strictly with FRONTIER_SHIPPING_PROFILE=1 / FRONTIER_EDITOR=0 so Shipping binaries
# contain zero PreviewRaster and zero editor telemetry symbols at the object-file level.
add_library(FrontierEnvironmentRenderingShipping STATIC ${FRONTIER_ENV_RENDERING_SOURCES})
target_include_directories(FrontierEnvironmentRenderingShipping PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/Frontier"
    "${CMAKE_SOURCE_DIR}/Engine/Interface"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include")
target_include_directories(FrontierEnvironmentRenderingShipping PRIVATE
    "${CMAKE_SOURCE_DIR}/ExternalPackages/stb"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include")
target_compile_definitions(FrontierEnvironmentRenderingShipping PUBLIC
    VK_NO_PROTOTYPES
    FRONTIER_SHIPPING_PROFILE=1
    FRONTIER_EDITOR=0)
target_link_libraries(FrontierEnvironmentRenderingShipping PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)

# Shipping-profile compile-time verification target: compiles PreviewRaster.cpp + DiagnosticShippingExclusionCheck.cpp
# strictly under FRONTIER_SHIPPING_PROFILE=1 / FRONTIER_EDITOR=0 so Shipping builds never include debug/preview rasterizers.
add_library(FrontierPreviewShippingCheck STATIC
    Frontier/Engine/Diagnostics/IlluminationTelemetryJournal.cpp
    Frontier/Engine/GeometricRaster/PreviewRaster.cpp
    Exhibits/ClusterDiagnostic/Code/DiagnosticShippingExclusionCheck.cpp)
target_include_directories(FrontierPreviewShippingCheck PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/Frontier"
    "${CMAKE_SOURCE_DIR}/Engine/Interface"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include")
target_compile_definitions(FrontierPreviewShippingCheck PRIVATE
    VK_NO_PROTOTYPES
    FRONTIER_SHIPPING_PROFILE=1
    FRONTIER_EDITOR=0)

# Headless CLI baker converting binary FCT1 .facet geometry into compact IEEE-754 Float16 .sdfspace SDF volumes.
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(FrontierBakeSdfSpace
        Exhibits/SpatiotemporalIllumination/Code/BakeSdfSpaceTool.cpp)
    target_link_libraries(FrontierBakeSdfSpace PRIVATE FrontierEnvironmentRendering)
endif()

find_program(FRONTIER_ENV_SLANGC
    NAMES slangc
    PATHS "${CMAKE_SOURCE_DIR}/ExternalPackages/VulkanCompute/Slang/slangtorch/bin"
    NO_CMAKE_FIND_ROOT_PATH)

set(FRONTIER_ENV_SHADER_SRC "${CMAKE_SOURCE_DIR}/Frontier/Engine/Shaders")
set(FRONTIER_ENV_SHADER_OUT "${CMAKE_BINARY_DIR}/ShaderArchives/Environment")
set(FRONTIER_ENV_COMMON_SLANG
    "${FRONTIER_ENV_SHADER_SRC}/PlanetaryRecords.slang"
    "${FRONTIER_ENV_SHADER_SRC}/ForceField.slang"
    "${FRONTIER_ENV_SHADER_SRC}/AtmosphereScattering.slang"
    "${FRONTIER_ENV_SHADER_SRC}/CloudSynthesis.slang"
    "${FRONTIER_ENV_SHADER_SRC}/VolumeDistanceSampling.slang"
    "${FRONTIER_ENV_SHADER_SRC}/RangeDistanceSampling.slang"
    "${FRONTIER_ENV_SHADER_SRC}/BoundingVolumeSearch.slang"
    "${FRONTIER_ENV_SHADER_SRC}/OctetVolumeSearch.slang"
    "${FRONTIER_ENV_SHADER_SRC}/SurfaceRadiositySampling.slang"
    "${FRONTIER_ENV_SHADER_SRC}/LuminousFacetSampling.slang"
    "${FRONTIER_ENV_SHADER_SRC}/SurfaceScatteringEvaluation.slang")

# Module:EntryPoint:OutputStem:ExtraDefineFlag
set(FRONTIER_ENV_SHADER_SPECS
    "AtmosphereScattering:BakeTransmittanceLutMain:BakeTransmittanceLut:-DATMOSPHERE_LUT_KERNEL=1"
    "AtmosphereScattering:BakeMultiScatterLutMain:BakeMultiScatterLut:-DATMOSPHERE_LUT_KERNEL=1"
    "AtmosphereProjection:BakeAtmosphereProjectionLutMain:BakeAtmosphereProjectionLut:"
    "VisibilityProjection:RasterizeVisibilityProjectionMain:RasterizeVisibilityProjection:"
    "PlanetaryProjection:RasterizePlanetaryProjectionMain:RasterizePlanetaryProjection:"
    "CloudShadowProjection:ProjectCloudShadowMain:ProjectCloudShadow:"
    "VolumetricClouds:TraceVolumetricCloudsMain:TraceVolumetricClouds:"
    "VolumetricClouds:ReconstructVolumetricCloudsMain:ReconstructVolumetricClouds:"
    "FogSpace:IntegrateFogSpaceMain:IntegrateFogSpace:"
    "OpticalLensFlare:EvaluateFlareVisibilityMain:EvaluateFlareVisibility:"
    "OpticalLensFlare:CompositeFlareApertureMain:CompositeFlareAperture:"
    "PreviewRaster:ClearPreviewShadowMain:ClearPreviewShadow:"
    "PreviewRaster:ProjectPreviewShadowMain:ProjectPreviewShadow:"
    "PreviewRaster:BinPreviewTilesMain:BinPreviewTiles:"
    "PreviewRaster:ResolvePreviewViewMain:ResolvePreviewView:"
    "SpatiotemporalIllumination:SolveSurfaceRadiosityMain:SolveSurfaceRadiosity:"
    "SpatiotemporalIllumination:TracePrimarySurfaceMain:TracePrimarySurface:"
    "SpatiotemporalIllumination:SampleDirectReservoirsMain:SampleDirectReservoirs:"
    "SpatiotemporalIllumination:SampleIndirectReservoirsMain:SampleIndirectReservoirs:"
    "SpatiotemporalIllumination:ReuseSpatialReservoirsMain:ReuseSpatialReservoirs:"
    "SpatiotemporalIllumination:FilterSpatiotemporalAtrousMain:FilterSpatiotemporalAtrous:")

set(FRONTIER_ENV_SPV_OUTPUTS)
if(FRONTIER_ENV_SLANGC)
    foreach(SPEC IN LISTS FRONTIER_ENV_SHADER_SPECS)
        string(REPLACE ":" ";" SPEC_PARTS "${SPEC}")
        list(GET SPEC_PARTS 0 SLANG_MODULE)
        list(GET SPEC_PARTS 1 ENTRY_NAME)
        list(GET SPEC_PARTS 2 OUT_STEM)
        list(LENGTH SPEC_PARTS SPEC_LEN)
        set(EXTRA_SLANG_ARGS)
        if(SPEC_LEN GREATER 3)
            list(GET SPEC_PARTS 3 EXTRA_FLAG)
            if(NOT "${EXTRA_FLAG}" STREQUAL "")
                list(APPEND EXTRA_SLANG_ARGS "${EXTRA_FLAG}")
            endif()
        endif()
        set(SPV_PATH "${FRONTIER_ENV_SHADER_OUT}/${OUT_STEM}.spv")
        list(APPEND FRONTIER_ENV_SPV_OUTPUTS "${SPV_PATH}")
        add_custom_command(
            OUTPUT "${SPV_PATH}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${FRONTIER_ENV_SHADER_OUT}"
            COMMAND "${FRONTIER_ENV_SLANGC}" "${FRONTIER_ENV_SHADER_SRC}/${SLANG_MODULE}.slang"
                -I "${FRONTIER_ENV_SHADER_SRC}"
                ${EXTRA_SLANG_ARGS}
                -entry "${ENTRY_NAME}"
                -stage compute
                -target spirv
                -profile spirv_1_3
                -emit-spirv-directly
                -O0
                -o "${SPV_PATH}"
            DEPENDS "${FRONTIER_ENV_SHADER_SRC}/${SLANG_MODULE}.slang" ${FRONTIER_ENV_COMMON_SLANG}
            VERBATIM)
    endforeach()
    add_custom_target(FrontierEnvironmentShaders ALL DEPENDS ${FRONTIER_ENV_SPV_OUTPUTS})
endif()

macro(frontier_register_environment_tests)
    if(BUILD_TESTING)
        add_executable(EnvironmentRealmValidation
            Exhibits/EnvironmentRealm/Code/EnvironmentRealmValidation.cpp
            Engine/Render/StbImageWriteImplementation.cpp)
        target_include_directories(EnvironmentRealmValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
        target_link_libraries(EnvironmentRealmValidation PRIVATE FrontierEnvironmentRendering)
        if(TARGET FrontierEnvironmentShaders)
            add_dependencies(EnvironmentRealmValidation FrontierEnvironmentShaders)
        endif()
        add_test(NAME EnvironmentRealmValidation COMMAND EnvironmentRealmValidation
            "${CMAKE_SOURCE_DIR}"
            "${FRONTIER_ENV_SHADER_OUT}"
            "${CMAKE_BINARY_DIR}/environment-realm-exhibits")
        set_tests_properties(EnvironmentRealmValidation PROPERTIES
            RUN_SERIAL TRUE
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

        add_executable(ForceFieldTriggerValidation
            Exhibits/EnvironmentRealm/Code/ForceFieldTriggerValidation.cpp
            Engine/Render/StbImageWriteImplementation.cpp)
        target_include_directories(ForceFieldTriggerValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
        target_link_libraries(ForceFieldTriggerValidation PRIVATE FrontierEnvironmentRendering)
        if(TARGET FrontierEnvironmentShaders)
            add_dependencies(ForceFieldTriggerValidation FrontierEnvironmentShaders)
        endif()
        add_test(NAME ForceFieldTriggerValidation COMMAND ForceFieldTriggerValidation
            "${CMAKE_SOURCE_DIR}"
            "${FRONTIER_ENV_SHADER_OUT}"
            "${CMAKE_BINARY_DIR}/environment-realm-exhibits")
        set_tests_properties(ForceFieldTriggerValidation PROPERTIES
            RUN_SERIAL TRUE
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

        add_executable(ClusterDiagnosticValidation
            Exhibits/ClusterDiagnostic/Code/ClusterDiagnosticValidation.cpp
            Engine/Render/StbImageWriteImplementation.cpp)
        target_include_directories(ClusterDiagnosticValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
        target_compile_definitions(ClusterDiagnosticValidation PRIVATE FRONTIER_EDITOR_PROFILE=1 FRONTIER_EDITOR=1)
        target_link_libraries(ClusterDiagnosticValidation PRIVATE
            FrontierEnvironmentRendering
            FrontierPreviewShippingCheck
            FrontierInterface)
        if(TARGET FrontierEnvironmentShaders)
            add_dependencies(ClusterDiagnosticValidation FrontierEnvironmentShaders)
        endif()
        add_test(NAME ClusterDiagnosticValidation COMMAND ClusterDiagnosticValidation
            "${CMAKE_SOURCE_DIR}"
            "${FRONTIER_ENV_SHADER_OUT}"
            "${CMAKE_BINARY_DIR}/cluster-diagnostic-exhibits")
        set_tests_properties(ClusterDiagnosticValidation PROPERTIES
            RUN_SERIAL TRUE
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

        add_executable(SpatiotemporalIlluminationValidation
            Exhibits/SpatiotemporalIllumination/Code/SpatiotemporalIlluminationValidation.cpp
            Engine/Render/StbImageWriteImplementation.cpp)
        target_include_directories(SpatiotemporalIlluminationValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
        target_link_libraries(SpatiotemporalIlluminationValidation PRIVATE FrontierEnvironmentRendering)
        if(TARGET FrontierEnvironmentShaders)
            add_dependencies(SpatiotemporalIlluminationValidation FrontierEnvironmentShaders)
        endif()
        add_test(NAME SpatiotemporalIlluminationValidation COMMAND SpatiotemporalIlluminationValidation
            "${CMAKE_SOURCE_DIR}"
            "${FRONTIER_ENV_SHADER_OUT}"
            "${CMAKE_BINARY_DIR}/spatiotemporal-illumination-exhibits")
        set_tests_properties(SpatiotemporalIlluminationValidation PROPERTIES
            RUN_SERIAL TRUE
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

        add_executable(ProjectContentStagingValidation
            Exhibits/ProjectSpace/Code/ProjectContentStagingValidation.cpp)
        target_link_libraries(ProjectContentStagingValidation PRIVATE
            FrontierEnvironmentRendering
            FrontierSceneEditorUI)
        if(TARGET StageContent_Everest)
            add_dependencies(ProjectContentStagingValidation StageContent_Everest)
        endif()
        add_test(NAME ProjectContentStagingValidation COMMAND ProjectContentStagingValidation
            "${CMAKE_SOURCE_DIR}/Projects/Everest"
            "/tmp/frontier-standalone-everest-sandbox")
    endif()
endmacro()
