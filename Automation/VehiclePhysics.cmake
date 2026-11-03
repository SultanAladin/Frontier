#============================================================================================================================================
#                                                          VEHICLEPHYSICS.CMAKE
#============================================================================================================================================
# 🧩 JoltPhysics v5.2.0 static library, unified GRIT + ProjectDrive + 3-Soft-Body-Tyre vehicle dynamics library, and CTest validations.

set(FRONTIER_JOLT_ROOT "${CMAKE_SOURCE_DIR}/ExternalPackages/jolt")
file(GLOB_RECURSE FRONTIER_JOLT_SOURCES CONFIGURE_DEPENDS "${FRONTIER_JOLT_ROOT}/Jolt/*.cpp")

add_library(FrontierJoltPhysics STATIC ${FRONTIER_JOLT_SOURCES})
target_include_directories(FrontierJoltPhysics PUBLIC "${FRONTIER_JOLT_ROOT}")
target_compile_features(FrontierJoltPhysics PUBLIC cxx_std_17)
target_compile_definitions(FrontierJoltPhysics PUBLIC JPH_NO_DEBUG JPH_DISABLE_CUSTOM_ALLOCATOR)
if(MSVC)
    target_compile_options(FrontierJoltPhysics PRIVATE /utf-8 /W0)
else()
    target_compile_options(FrontierJoltPhysics PRIVATE -w -fno-strict-aliasing)
endif()
target_link_libraries(FrontierJoltPhysics PUBLIC Threads::Threads)

add_library(FrontierVehiclePhysics STATIC
    Frontier/Engine/PhysicalDynamics/RigidBodySolver.cpp
    Frontier/Engine/PhysicalDynamics/JoltAdapter.cpp
    Frontier/Engine/PhysicalDynamics/PhysicsExecutionThread.cpp
    Frontier/Engine/PhysicalDynamics/Vehicle/PacejkaMagicFormula.cpp
    Frontier/Engine/PhysicalDynamics/Vehicle/TyreSlipDynamics.cpp
    Frontier/Engine/PhysicalDynamics/Vehicle/TyreSoftBody.cpp
    Frontier/Engine/PhysicalDynamics/Vehicle/Drivetrain.cpp
    Frontier/Engine/PhysicalDynamics/Vehicle/Aerodynamics.cpp
    Frontier/Engine/PhysicalDynamics/Vehicle/VehicleGeometry.cpp
    Frontier/Engine/PhysicalDynamics/Vehicle/VehicleSolver.cpp
    Frontier/Engine/PhysicalDynamics/Vehicle/VehicleJoltRig.cpp)

target_include_directories(FrontierVehiclePhysics PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/Frontier")
target_compile_features(FrontierVehiclePhysics PUBLIC cxx_std_20)
target_link_libraries(FrontierVehiclePhysics PRIVATE FrontierJoltPhysics PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(FrontierVehiclePhysics PRIVATE /utf-8 /W4)
else()
    target_compile_options(FrontierVehiclePhysics PRIVATE -Wall -Wextra -Werror)
endif()

if(TARGET FrontierSceneViewport)
    add_library(FrontierVehicleScenePublisher STATIC
        Frontier/Engine/PhysicalDynamics/Vehicle/VehicleScenePublisher.cpp)
    target_include_directories(FrontierVehicleScenePublisher PUBLIC
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/Frontier"
        "${CMAKE_SOURCE_DIR}/Frontier/Engine")
    target_compile_features(FrontierVehicleScenePublisher PUBLIC cxx_std_20)
    target_link_libraries(FrontierVehicleScenePublisher PUBLIC FrontierVehiclePhysics FrontierSceneViewport)
    if(MSVC)
        target_compile_options(FrontierVehicleScenePublisher PRIVATE /utf-8 /W4)
    else()
        target_compile_options(FrontierVehicleScenePublisher PRIVATE -Wall -Wextra -Werror)
    endif()
endif()

if(EXISTS "${FRONTIER_JOLT_ROOT}/LICENSE")
    install(FILES "${FRONTIER_JOLT_ROOT}/LICENSE" DESTINATION Notices RENAME JoltPhysics-LICENSE.txt)
endif()

if(BUILD_TESTING)
    add_executable(VehiclePhysicsValidation
        Exhibits/VehiclePhysics/Code/VehiclePhysicsValidation.cpp)
    target_link_libraries(VehiclePhysicsValidation PRIVATE FrontierVehiclePhysics)
    if(MSVC)
        target_compile_options(VehiclePhysicsValidation PRIVATE /utf-8 /W4)
    else()
        target_compile_options(VehiclePhysicsValidation PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME VehiclePhysicsValidation COMMAND VehiclePhysicsValidation "${CMAKE_BINARY_DIR}/vehicle-physics-telemetry")

    if(TARGET FrontierVehicleScenePublisher)
        add_executable(VehicleTyreAnimationCapture
            Exhibits/VehiclePhysics/Code/VehicleTyreAnimationCapture.cpp
            Engine/Render/StbImageWriteImplementation.cpp)
        target_include_directories(VehicleTyreAnimationCapture SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
        target_link_libraries(VehicleTyreAnimationCapture PRIVATE FrontierVehicleScenePublisher)
        if(MSVC)
            target_compile_options(VehicleTyreAnimationCapture PRIVATE /utf-8 /W4)
        else()
            target_compile_options(VehicleTyreAnimationCapture PRIVATE -Wall -Wextra -Werror)
        endif()
    endif()
endif()
