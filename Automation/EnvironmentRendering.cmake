#============================================================================================================================================
#                                                     ENVIRONMENTRENDERING.CMAKE
#============================================================================================================================================
# 🧩 Multi-instance, portal-ready PlanetaryAtmosphere + GeometricRaster + DeviceExchange C++20 archive, Slang SPIR-V shaders, and CTest suite.

add_library(FrontierEnvironmentRendering STATIC
    Frontier/Engine/DeviceExchange/VulkanComputeExchange.cpp
    Frontier/Engine/GeometricRaster/VisibilityProjection.cpp
    Frontier/Engine/PlanetaryAtmosphere/AtmosphereScattering.cpp
    Frontier/Engine/PlanetaryAtmosphere/AstronomicalEphemeris.cpp
    Frontier/Engine/PlanetaryAtmosphere/SkyProjection.cpp
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
    Frontier/Engine/Rendering/BarrierDerivation.cpp)

target_include_directories(FrontierEnvironmentRendering PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/Frontier"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include")
target_include_directories(FrontierEnvironmentRendering PRIVATE
    "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
target_compile_definitions(FrontierEnvironmentRendering PUBLIC VK_NO_PROTOTYPES)
target_link_libraries(FrontierEnvironmentRendering PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)

find_program(FRONTIER_ENV_SLANGC
    NAMES slangc
    PATHS "${CMAKE_SOURCE_DIR}/ExternalPackages/VulkanCompute/Slang/slangtorch/bin")

set(FRONTIER_ENV_SHADER_SRC "${CMAKE_SOURCE_DIR}/Frontier/Engine/Shaders")
set(FRONTIER_ENV_SHADER_OUT "${CMAKE_BINARY_DIR}/ShaderArchives/Environment")
set(FRONTIER_ENV_COMMON_SLANG
    "${FRONTIER_ENV_SHADER_SRC}/PlanetaryRecords.slang"
    "${FRONTIER_ENV_SHADER_SRC}/AtmosphereScattering.slang"
    "${FRONTIER_ENV_SHADER_SRC}/CloudSynthesis.slang")

# Module:EntryPoint:OutputStem:ExtraDefineFlag
set(FRONTIER_ENV_SHADER_SPECS
    "AtmosphereScattering:BakeTransmittanceLutMain:BakeTransmittanceLut:-DATMOSPHERE_LUT_KERNEL=1"
    "AtmosphereScattering:BakeMultiScatterLutMain:BakeMultiScatterLut:-DATMOSPHERE_LUT_KERNEL=1"
    "SkyProjection:BakeSkyViewLutMain:BakeSkyViewLut:"
    "VisibilityProjection:RasterizeVisibilityProjectionMain:RasterizeVisibilityProjection:"
    "PlanetaryProjection:RasterizePlanetaryProjectionMain:RasterizePlanetaryProjection:"
    "CloudShadowProjection:ProjectCloudShadowMain:ProjectCloudShadow:"
    "VolumetricClouds:TraceVolumetricCloudsMain:TraceVolumetricClouds:"
    "VolumetricClouds:ReconstructVolumetricCloudsMain:ReconstructVolumetricClouds:"
    "FogSpace:IntegrateFogSpaceMain:IntegrateFogSpace:"
    "OpticalLensFlare:EvaluateFlareVisibilityMain:EvaluateFlareVisibility:"
    "OpticalLensFlare:CompositeFlareApertureMain:CompositeFlareAperture:")

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
    set_tests_properties(EnvironmentRealmValidation PROPERTIES RUN_SERIAL TRUE)
endif()
