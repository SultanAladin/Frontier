cmake_minimum_required(VERSION 3.21)
project(FrontierUIFoundation LANGUAGES C CXX)

# No scene renderer target or sources are included in this selective port.
option(FRONTIER_HOST_VERIFICATION "Build local verification tools on a non-Windows editing host" OFF)
if(NOT WIN32 AND NOT FRONTIER_HOST_VERIFICATION)
    message(FATAL_ERROR "Windows is the delivery target. Use host verification only for local checks.")
endif()
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Automation/PrepareUI.py" --verify
    RESULT_VARIABLE DEPENDENCY_STATUS)
if(NOT DEPENDENCY_STATUS EQUAL 0)
    message(FATAL_ERROR "Run python Automation/PrepareUI.py before configuring; configure never downloads.")
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Automation/PrepareFonts.py" --verify
    RESULT_VARIABLE FONT_STATUS)
if(NOT FONT_STATUS EQUAL 0)
    message(FATAL_ERROR "Prepare fonts first: pip install -r Automation/FontRequirements.txt; python Automation/PrepareFonts.py")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8 /W4)
endif()
include(Automation/ThorVGSoftware.cmake)
set(IMGUI_ROOT "${CMAKE_SOURCE_DIR}/ExternalPackages/imgui")
add_library(FrontierImGui STATIC
    "${IMGUI_ROOT}/imgui.cpp" "${IMGUI_ROOT}/imgui_draw.cpp"
    "${IMGUI_ROOT}/imgui_tables.cpp" "${IMGUI_ROOT}/imgui_widgets.cpp")
target_include_directories(FrontierImGui PUBLIC "${IMGUI_ROOT}")
add_library(FrontierInterface STATIC
    Engine/Interface/IconArt.cpp Engine/Interface/IconPresentation.cpp
    Engine/Interface/PixelSpace.cpp Engine/Interface/GlyphSpace.cpp
    Engine/Interface/ControlKit.cpp Engine/Interface/TextEntryState.cpp
    Engine/Interface/ThemeStructure.cpp Engine/Interface/VectorCodec.cpp
    Engine/Interface/ContentDirectory.cpp Engine/Interface/FontCodec.cpp
    Engine/Interface/TypefaceRegistry.cpp Engine/Interface/AppearanceInspector.cpp
    Engine/Interface/AppearanceCodec.cpp Engine/Interface/MotionIntegrator.cpp
    Engine/Interface/NotchMotion.cpp)
target_include_directories(FrontierInterface PUBLIC "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include")
target_link_libraries(FrontierInterface PUBLIC FrontierImGui thorvg_static)
add_library(FrontierEditorUI STATIC UI/Editor/FoundationPanel.cpp UI/Editor/ControlCentrePanel.cpp)
target_link_libraries(FrontierEditorUI PUBLIC FrontierInterface)
target_compile_definitions(FrontierEditorUI PUBLIC FRONTIER_EDITOR=1)

if(WIN32)
    # GLFW owns windows/input; DX11 is only this ImGui exhibit's display adapter, not the primary renderer.
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(ExternalPackages/glfw EXCLUDE_FROM_ALL)
    add_executable(FrontierUI Exhibits/UIFoundation/Code/WindowsUI.cpp
        "${IMGUI_ROOT}/backends/imgui_impl_glfw.cpp" "${IMGUI_ROOT}/backends/imgui_impl_dx11.cpp")
    target_include_directories(FrontierUI PRIVATE "${IMGUI_ROOT}/backends")
    target_compile_definitions(FrontierUI PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(NOT DEFINED FRONTIER_D3DCOMPILER)
        set(FRONTIER_D3DCOMPILER d3dcompiler)
    endif()
    target_link_libraries(FrontierUI PRIVATE FrontierEditorUI glfw d3d11 dxgi ${FRONTIER_D3DCOMPILER} dwmapi)
    add_custom_command(TARGET FrontierUI POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_SOURCE_DIR}/EngineContent" "$<TARGET_FILE_DIR:FrontierUI>/EngineContent")
    install(TARGETS FrontierUI RUNTIME DESTINATION .)
    install(DIRECTORY EngineContent DESTINATION .)
    install(FILES "${IMGUI_ROOT}/LICENSE.txt" DESTINATION Notices RENAME ImGui-LICENSE.txt)
    install(FILES ExternalPackages/glfw/LICENSE.md DESTINATION Notices RENAME GLFW-LICENSE.md)
    install(FILES ExternalPackages/tomlpp/LICENSE DESTINATION Notices RENAME TomlPlusPlus-LICENSE.txt)
    install(FILES ExternalPackages/thorvg/LICENSE DESTINATION Notices RENAME ThorVG-LICENSE.txt)
    install(FILES Engine/Interface/Lucide.NOTICE Documentation/SourceNotices.md DESTINATION Notices)
endif()

include(CTest)
if(BUILD_TESTING)
    foreach(EDITOR_MODE 0 1)
        add_executable(ActivationValidation${EDITOR_MODE} Exhibits/UIFoundation/Code/ActivationValidation.cpp)
        target_include_directories(ActivationValidation${EDITOR_MODE} PRIVATE "${CMAKE_SOURCE_DIR}")
        target_compile_definitions(ActivationValidation${EDITOR_MODE} PRIVATE FRONTIER_EDITOR=${EDITOR_MODE})
        add_test(NAME ActivationValidation${EDITOR_MODE} COMMAND ActivationValidation${EDITOR_MODE})
    endforeach()
    add_test(NAME FontCatalogueValidation COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/Exhibits/ControlCentre/Code/FontCatalogueValidation.py")
    add_executable(NotchValidation Exhibits/ControlCentre/Code/NotchValidation.cpp)
    target_link_libraries(NotchValidation PRIVATE FrontierInterface)
    add_test(NAME NotchValidation COMMAND NotchValidation)
    add_executable(ControlCentreValidation Exhibits/ControlCentre/Code/ControlCentreValidation.cpp)
    target_link_libraries(ControlCentreValidation PRIVATE FrontierEditorUI)
    add_test(NAME ControlCentreValidation COMMAND ControlCentreValidation
        "${CMAKE_SOURCE_DIR}/EngineContent/FontArchives" "${CMAKE_BINARY_DIR}/control-centre-evidence")
    add_executable(InterfaceValidation Exhibits/UIFoundation/Code/InterfaceValidation.cpp)
    target_link_libraries(InterfaceValidation PRIVATE FrontierEditorUI)
    add_test(NAME InterfaceValidation COMMAND InterfaceValidation
        "${CMAKE_SOURCE_DIR}/EngineContent/Icons" "${CMAKE_BINARY_DIR}/evidence")
endif()
