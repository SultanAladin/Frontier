cmake_minimum_required(VERSION 3.20)
project(FrontierCompleteRendererExhibit CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(FRONTIER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Frontier" ABSOLUTE)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RENDERING ${FRONTIER_ROOT}/Engine/Rendering)
set(PLANETARY ${FRONTIER_ROOT}/Engine/PlanetaryAtmosphere)
set(DIAGNOSTICS
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogRingStore.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogCategories.cpp
    ${FRONTIER_ROOT}/Engine/Diagnostics/LogStream.cpp)
set(REFERENCE_SOURCES
    ${FRONTIER_ROOT}/Engine/DeviceExchange/VulkanComputeExchange.cpp
    ${RENDERING}/ClusterNesting.cpp
    ${RENDERING}/VisibilityCull.cpp
    ${RENDERING}/DiscrepancySequence.cpp
    ${RENDERING}/ReservoirSampling.cpp
    ${RENDERING}/VarianceFiltering.cpp
    ${RENDERING}/DistanceField.cpp
    ${RENDERING}/SurfaceCards.cpp
    ${PLANETARY}/AtmosphereScattering.cpp
    ${PLANETARY}/CloudSpace.cpp
    ${PLANETARY}/FogSpace.cpp
    ${RENDERING}/ReferenceRenderer.cpp)
add_executable(CompleteRendererCapture CompleteRendererCapture.cpp ${DIAGNOSTICS} ${REFERENCE_SOURCES})
add_executable(CompleteRendererValidation CompleteRendererValidation.cpp ${DIAGNOSTICS} ${REFERENCE_SOURCES}
    ${RENDERING}/IlluminationUnits.cpp
    ${RENDERING}/EnvironmentUnits.cpp
    ${RENDERING}/CompleteFrameUnits.cpp
    ${RENDERING}/InspectionUnits.cpp
    ${RENDERING}/SubmissionOrder.cpp
    ${RENDERING}/TransientTargetAtlas.cpp
    ${RENDERING}/BarrierDerivation.cpp)
target_include_directories(CompleteRendererCapture PRIVATE
    ${FRONTIER_ROOT}
    ${REPO_ROOT}/ExternalPackages/stb
    ${REPO_ROOT}/ExternalPackages/vulkan-headers/include)
target_include_directories(CompleteRendererValidation PRIVATE
    ${FRONTIER_ROOT}
    ${REPO_ROOT}/ExternalPackages/stb
    ${REPO_ROOT}/ExternalPackages/vulkan-headers/include)
target_compile_definitions(CompleteRendererCapture PRIVATE VK_NO_PROTOTYPES)
target_compile_definitions(CompleteRendererValidation PRIVATE VK_NO_PROTOTYPES)
find_package(Threads REQUIRED)
target_link_libraries(CompleteRendererCapture PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
target_link_libraries(CompleteRendererValidation PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
enable_testing()
add_test(NAME CompleteRendererValidation COMMAND CompleteRendererValidation)
