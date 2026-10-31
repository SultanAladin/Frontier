#============================================================================================================================================
#                                                     ENVIRONMENTRENDERING.CMAKE
#============================================================================================================================================
# 🧩 Multi-instance, portal-ready Environment & Celestial C++20 rendering archive, Slang SPIR-V compute shaders, and Vulkan CTest validation.

add_library(FrontierEnvironmentRendering STATIC
    Frontier/Engine/Rendering/AtmosphereModel.cpp
    Frontier/Engine/Rendering/SolarSkyEnvironment.cpp
    Frontier/Engine/Rendering/CelestialModel.cpp
    Frontier/Engine/Rendering/CelestialDomeModel.cpp
    Frontier/Engine/Rendering/CloudField.cpp
    Frontier/Engine/Rendering/FogMedium.cpp
    Frontier/Engine/Rendering/MediumRadiancePayload.cpp
    Frontier/Engine/Rendering/EnvironmentUnits.cpp
    Frontier/Engine/Rendering/SubmissionOrder.cpp
    Frontier/Engine/Rendering/TransientTargetAtlas.cpp
    Frontier/Engine/Rendering/BarrierDerivation.cpp
    Frontier/Engine/Rendering/WeatherEffects.cpp
    Frontier/Engine/Rendering/EnvironmentRealmEnsemble.cpp
    Frontier/Engine/Rendering/VulkanComputeSubstrate.cpp
    Frontier/Engine/Rendering/AtmosphereLutSubmission.cpp
    Frontier/Engine/Rendering/CelestialDomeRasterization.cpp
    Frontier/Engine/Rendering/VolumetricCloudSequence.cpp
    Frontier/Engine/Rendering/VolumetricFogSubmission.cpp
    Frontier/Engine/Rendering/OpticalLensFlareSequence.cpp
    Frontier/Engine/Rendering/EnvironmentSubmissionSequence.cpp)

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

set(FRONTIER_ENV_SHADER_SRC "${CMAKE_SOURCE_DIR}/Frontier/Engine/Rendering/Shaders")
set(FRONTIER_ENV_SHADER_OUT "${CMAKE_BINARY_DIR}/ShaderArchives/Environment")
set(FRONTIER_ENV_COMMON_SLANG
    "${FRONTIER_ENV_SHADER_SRC}/EnvironmentEnsemble.slang"
    "${FRONTIER_ENV_SHADER_SRC}/SolarSkyEnvironment.slang")

set(FRONTIER_ENV_SHADER_SPECS
    "AtmosphereLut:BakeTransmittanceLutMain:BakeTransmittanceLut"
    "AtmosphereLut:BakeMultiScatterLutMain:BakeMultiScatterLut"
    "AtmosphereLut:BakeSkyViewLutMain:BakeSkyViewLut"
    "EnvironmentSceneGbuffer:RasterizeRealmGbufferMain:RasterizeRealmGbuffer"
    "CelestialDome:RasterizeCelestialDomeMain:RasterizeCelestialDome"
    "VolumetricClouds:ProjectCloudShadowMain:ProjectCloudShadow"
    "VolumetricClouds:TraceVolumetricCloudsMain:TraceVolumetricClouds"
    "VolumetricClouds:ReconstructVolumetricCloudsMain:ReconstructVolumetricClouds"
    "VolumetricFog:IntegrateVolumetricFogMain:IntegrateVolumetricFog"
    "OpticalLensFlare:EvaluateFlareVisibilityMain:EvaluateFlareVisibility"
    "OpticalLensFlare:CompositeFlareApertureMain:CompositeFlareAperture")

set(FRONTIER_ENV_SPV_OUTPUTS)
if(FRONTIER_ENV_SLANGC)
    foreach(SPEC IN LISTS FRONTIER_ENV_SHADER_SPECS)
        string(REPLACE ":" ";" SPEC_PARTS "${SPEC}")
        list(GET SPEC_PARTS 0 SLANG_MODULE)
        list(GET SPEC_PARTS 1 ENTRY_NAME)
        list(GET SPEC_PARTS 2 OUT_STEM)
        set(SPV_PATH "${FRONTIER_ENV_SHADER_OUT}/${OUT_STEM}.spv")
        list(APPEND FRONTIER_ENV_SPV_OUTPUTS "${SPV_PATH}")
        add_custom_command(
            OUTPUT "${SPV_PATH}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${FRONTIER_ENV_SHADER_OUT}"
            COMMAND "${FRONTIER_ENV_SLANGC}" "${FRONTIER_ENV_SHADER_SRC}/${SLANG_MODULE}.slang"
                -I "${FRONTIER_ENV_SHADER_SRC}"
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
endif()
