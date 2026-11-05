# Frontier engine — session & game mechanics (standalone slice).
#
# The session and software presenter are self-contained; no Vulkan or GLFW is required for their tests.
# Define FRONTIER_WITH_GLFW and link glfw to enable live-window input.
#
#   cmake -S Frontier -B /tmp/frontier-build && cmake --build /tmp/frontier-build
#   ctest --test-dir /tmp/frontier-build --output-on-failure

cmake_minimum_required(VERSION 3.16)
project(FrontierSession CXX)

add_library(FrontierSession STATIC
    Engine/Input/InputEncoder.cpp
    Engine/Input/InputDriver.cpp
    Engine/Input/SyntheticDriver.cpp
    Engine/Scene/DeployableUnit.cpp
    Engine/Scene/FacetArchive.cpp
    Engine/Session/SpawnPolicy.cpp
    Engine/Session/SessionConfiguration.cpp
    Engine/Session/SessionPolicy.cpp
    Engine/Session/SessionSpace.cpp
    Engine/Session/TransferConfiguration.cpp
    Engine/Session/LevelTransition.cpp
    Engine/Session/LobbySession.cpp
    ../Projects/Everest/Source/Session/SessionPolicy.cpp)
target_include_directories(FrontierSession PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(FrontierSession PUBLIC cxx_std_17)

add_executable(SessionValidation ../Validation/SessionValidation.cpp)
target_link_libraries(SessionValidation PRIVATE FrontierSession)

add_library(FrontierGame STATIC Game/SoftwarePresentation.cpp)
target_link_libraries(FrontierGame PUBLIC FrontierSession)

add_executable(LobbySession Game/LobbySessionMain.cpp)
target_link_libraries(LobbySession PRIVATE FrontierGame FrontierSession)

# Host-side HMI declarations and software presentation validation, not the native Vulkan renderer.
find_package(Threads REQUIRED)
add_library(FrontierHmiDeclarations STATIC
    Engine/Rendering/HmiViewportUnits.cpp
    Engine/Rendering/IlluminationUnits.cpp
    Engine/Rendering/EnvironmentUnits.cpp
    Engine/Rendering/SubmissionOrder.cpp
    Engine/Diagnostics/LogCategories.cpp
    Engine/Diagnostics/LogRingStore.cpp
    Engine/Diagnostics/LogStream.cpp)
target_include_directories(FrontierHmiDeclarations PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(FrontierHmiDeclarations PUBLIC cxx_std_20)
target_link_libraries(FrontierHmiDeclarations PUBLIC Threads::Threads)

add_executable(Phase3Validation ../Validation/Phase3Validation.cpp)
target_compile_features(Phase3Validation PRIVATE cxx_std_20)
target_link_libraries(Phase3Validation PRIVATE FrontierGame FrontierHmiDeclarations)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME SessionValidation COMMAND SessionValidation)
    add_test(NAME Phase3Validation COMMAND Phase3Validation)
    set_tests_properties(SessionValidation Phase3Validation PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif()
