#============================================================================================================================================
#                                                           CMAKELISTS.TXT
#============================================================================================================================================
# 🧩 Root ISO C++20 build configuration enforcing Editor, Development, and Shipping profiles across Windows, Linux, and Android.

cmake_minimum_required(VERSION 3.21)
project(Frontier LANGUAGES C CXX)

include(CTest)

#------------------------------------------------------------------------------------------------------------------------
#                                                LANGUAGE & PROFILE POLICY
#------------------------------------------------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FRONTIER_PROFILE "Development" CACHE STRING "Active Frontier build profile (Editor, Development, Shipping)")
set_property(CACHE FRONTIER_PROFILE PROPERTY STRINGS Editor Development Shipping)

if(FRONTIER_PROFILE STREQUAL "Editor")
    set(FRONTIER_PROFILE_EDITOR_DEF      1)
    set(FRONTIER_PROFILE_DEVELOPMENT_DEF 0)
    set(FRONTIER_PROFILE_SHIPPING_DEF    0)
elseif(FRONTIER_PROFILE STREQUAL "Development")
    set(FRONTIER_PROFILE_EDITOR_DEF      0)
    set(FRONTIER_PROFILE_DEVELOPMENT_DEF 1)
    set(FRONTIER_PROFILE_SHIPPING_DEF    0)
elseif(FRONTIER_PROFILE STREQUAL "Shipping")
    set(FRONTIER_PROFILE_EDITOR_DEF      0)
    set(FRONTIER_PROFILE_DEVELOPMENT_DEF 0)
    set(FRONTIER_PROFILE_SHIPPING_DEF    1)
else()
    message(FATAL_ERROR "Invalid FRONTIER_PROFILE='${FRONTIER_PROFILE}'. Choose Editor, Development, or Shipping.")
endif()

message(STATUS "Frontier active profile: ${FRONTIER_PROFILE}")

#------------------------------------------------------------------------------------------------------------------------
#                                          PINNED DEPENDENCY & PATCH VERIFICATION
#------------------------------------------------------------------------------------------------------------------------

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Automation/PrepareDependencies.py" --verify
    RESULT_VARIABLE FRONTIER_DEP_STATUS
)
if(NOT FRONTIER_DEP_STATUS EQUAL 0)
    message(FATAL_ERROR "Dependencies or ImGui patches unverified. Run: python3 Automation/PrepareDependencies.py")
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                             COMPILER WARNING & PROFILE FLAGS
#------------------------------------------------------------------------------------------------------------------------

add_library(FrontierCompileSettings INTERFACE)
target_compile_features(FrontierCompileSettings INTERFACE cxx_std_20)
target_compile_definitions(FrontierCompileSettings INTERFACE
    FRONTIER_PROFILE_EDITOR=${FRONTIER_PROFILE_EDITOR_DEF}
    FRONTIER_PROFILE_DEVELOPMENT=${FRONTIER_PROFILE_DEVELOPMENT_DEF}
    FRONTIER_PROFILE_SHIPPING=${FRONTIER_PROFILE_SHIPPING_DEF}
)

if(MSVC)
    target_compile_options(FrontierCompileSettings INTERFACE /utf-8 /W4 /permissive-)
    if(FRONTIER_PROFILE STREQUAL "Shipping")
        target_compile_options(FrontierCompileSettings INTERFACE /O2 /DNDEBUG)
    else()
        target_compile_options(FrontierCompileSettings INTERFACE /Zi)
    endif()
else()
    target_compile_options(FrontierCompileSettings INTERFACE -Wall -Wextra -Wpedantic)
    if(FRONTIER_PROFILE STREQUAL "Shipping")
        target_compile_options(FrontierCompileSettings INTERFACE -O3 -DNDEBUG)
    else()
        target_compile_options(FrontierCompileSettings INTERFACE -O1 -g)
    endif()
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                            PATCHED IMGUI (EXCLUDED IN SHIPPING)
#------------------------------------------------------------------------------------------------------------------------

if(NOT FRONTIER_PROFILE STREQUAL "Shipping")
    set(IMGUI_ROOT "${CMAKE_SOURCE_DIR}/ExternalPackages/imgui")
    add_library(FrontierImGui STATIC
        "${IMGUI_ROOT}/imgui.cpp"
        "${IMGUI_ROOT}/imgui_draw.cpp"
        "${IMGUI_ROOT}/imgui_tables.cpp"
        "${IMGUI_ROOT}/imgui_widgets.cpp"
    )
    target_include_directories(FrontierImGui PUBLIC "${IMGUI_ROOT}")
    target_compile_definitions(FrontierImGui PUBLIC "ImDrawIdx=unsigned int")
    if(NOT MSVC)
        target_compile_options(FrontierImGui PRIVATE -w)
    endif()
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                               ENGINE RUNTIME LIBRARY
#------------------------------------------------------------------------------------------------------------------------

add_library(FrontierEngine STATIC
    Engine/Input/InputExchange.cpp
    Engine/Input/InputExtension.cpp
    Engine/Windowing/PlatformWindowStub.cpp
)

target_include_directories(FrontierEngine PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/volk"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include"
)
target_link_libraries(FrontierEngine PUBLIC FrontierCompileSettings)

#------------------------------------------------------------------------------------------------------------------------
#                                         VALIDATION EXHIBITS (CTEST TARGETS)
#------------------------------------------------------------------------------------------------------------------------

if(BUILD_TESTING)
    add_executable(InputValidation Exhibits/InputValidation.cpp)
    target_link_libraries(InputValidation PRIVATE FrontierEngine)
    if(FRONTIER_PROFILE STREQUAL "Editor")
        target_link_libraries(InputValidation PRIVATE FrontierImGui)
    endif()
    add_test(NAME InputValidation COMMAND InputValidation)
endif()
