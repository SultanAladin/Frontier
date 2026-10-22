cmake_minimum_required(VERSION 3.21)
project(FrontierUIFoundation LANGUAGES C CXX)

find_package(Git QUIET)
set(FRONTIER_BUILD_COMMIT "local")
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=8 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE FRONTIER_BUILD_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()

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
# TextureWorkspace (copied from the working Texture Studio system): same ImGui panels, same Control Centre/notch.
target_compile_definitions(FrontierImGui PUBLIC "ImDrawIdx=unsigned int")
add_library(FrontierTextFiles STATIC Engine/Interface/TextFileReplacement.cpp)
target_include_directories(FrontierTextFiles PUBLIC "${CMAKE_SOURCE_DIR}")
add_library(FrontierVectorAuthoring STATIC
    Engine/ContentInterchange/VectorAuthoringDocument.cpp
    Engine/ContentInterchange/VectorAuthoringEditing.cpp
    Engine/ContentInterchange/VectorAuthoringArrayPlacement.cpp
    Engine/ContentInterchange/VectorAuthoringProperties.cpp
    Engine/ContentInterchange/VectorAuthoringSelectionEditing.cpp
    Engine/ContentInterchange/CubicPathEditing.cpp
    Engine/Render/VectorSurfaceCompilation.cpp
    Engine/ContentInterchange/SurfaceMapEncoding.cpp)
target_include_directories(FrontierVectorAuthoring PUBLIC "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include")
add_library(FrontierMaterialLibrary STATIC Engine/ContentInterchange/MaterialLibrary.cpp)
target_include_directories(FrontierMaterialLibrary PUBLIC "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include")
target_link_libraries(FrontierMaterialLibrary PUBLIC FrontierSurfaceRasterization FrontierTextFiles)
add_library(FrontierTextureStudioUI STATIC UI/TextureStudio/MaterialLibraryPanel.cpp UI/TextureStudio/PatternWorkspacePanel.cpp
    UI/TextureStudio/PatternThumbnailRaster.cpp UI/TextureStudio/VectorMaskControls.cpp Engine/Interface/VectorAuthoringControls.cpp)
target_link_libraries(FrontierTextureStudioUI PUBLIC FrontierMaterialLibrary FrontierInterface FrontierVectorAuthoring)
target_compile_definitions(FrontierTextureStudioUI PUBLIC FRONTIER_EDITOR=1)
add_library(FrontierInterface STATIC
    Engine/Interface/IconArt.cpp Engine/Interface/IconPresentation.cpp
    Engine/Interface/PixelSpace.cpp Engine/Interface/GlyphSpace.cpp
    Engine/Interface/ControlKit.cpp Engine/Interface/TextEntryState.cpp
    Engine/Interface/ThemeStructure.cpp Engine/Interface/VectorCodec.cpp
    Engine/Interface/ContentDirectory.cpp Engine/Interface/FontCodec.cpp
    Engine/Interface/TypefaceRegistry.cpp Engine/Interface/AppearanceInspector.cpp
    Engine/Interface/AppearanceCodec.cpp Engine/Interface/MotionIntegrator.cpp
    Engine/Interface/NotchMotion.cpp Engine/Interface/LogStreamChannel.cpp
    Engine/Interface/PanelExpansionTab.cpp Engine/Interface/ConfigurationInspector.cpp
    Engine/Interface/ConfigurationStructure.cpp Engine/Interface/InputPreferencesCodec.cpp
    Engine/Interface/MonitorPlacement.cpp Engine/Interface/PanelDockspaceMonitor.cpp
    Engine/Interface/PanelDockspace.cpp Engine/Interface/PanelDockspaceLayout.cpp
    Engine/Interface/NotificationSequence.cpp Engine/Interface/ConfirmationDialogue.cpp
    Engine/ContentInterchange/ProjectSpaceCodec.cpp)
target_link_libraries(FrontierInterface PUBLIC FrontierTextFiles)
add_library(FrontierLifecycle STATIC Engine/Lifecycle/StartupSequenceTrace.cpp)
target_include_directories(FrontierLifecycle PUBLIC "${CMAKE_SOURCE_DIR}")
target_include_directories(FrontierInterface PUBLIC "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include")
target_link_libraries(FrontierInterface PUBLIC FrontierImGui thorvg_static)
add_library(FrontierEditorUI STATIC UI/Editor/FoundationPanel.cpp UI/Editor/ControlCentrePanel.cpp
    UI/Editor/ContentBrowserPanel.cpp UI/Editor/LogStreamPanel.cpp)
target_link_libraries(FrontierEditorUI PUBLIC FrontierInterface FrontierLifecycle)
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
    # TextureWorkspace: the copied Texture Studio application (Material Library + Pattern panels,
    # Control Centre/notch from FrontierEditorUI), Vulkan presentation as on the source system.
    add_executable(TextureWorkspace Experimental/TextureStudio/Native/WindowsTextureStudio.cpp
        Engine/Interface/WindowsApplication.cpp Engine/Interface/VulkanPresentation.cpp
        "${IMGUI_ROOT}/backends/imgui_impl_glfw.cpp" "${IMGUI_ROOT}/backends/imgui_impl_vulkan.cpp")
    target_include_directories(TextureWorkspace PRIVATE "${IMGUI_ROOT}/backends"
        "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include")
    target_compile_definitions(TextureWorkspace PRIVATE FRONTIER_UI_VULKAN=1 VK_NO_PROTOTYPES
        FRONTIER_BUILD_COMMIT=\"${FRONTIER_BUILD_COMMIT}\")
    target_link_libraries(TextureWorkspace PRIVATE FrontierEditorUI FrontierTextureStudioUI glfw)
    add_custom_command(TARGET TextureWorkspace POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/EngineContent/MaterialArchives/TextureStudio"
            "$<TARGET_FILE_DIR:TextureWorkspace>/EngineContent/MaterialArchives/TextureStudio"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/EngineContent/PatternArchives/TextureStudio"
            "$<TARGET_FILE_DIR:TextureWorkspace>/EngineContent/PatternArchives/TextureStudio")
    install(TARGETS TextureWorkspace RUNTIME DESTINATION .)
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
include(Automation/SurfaceRasterization.cmake)
include(Automation/VectorCoverage.cmake)
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
    # Virtualized-geometry kernel (Engine/Geometry): stdlib-only, builds on every host.
    add_library(FrontierGeometry STATIC Engine/Geometry/FacetTierBuild.cpp)
    target_include_directories(FrontierGeometry PUBLIC "${CMAKE_SOURCE_DIR}")
    add_executable(FacetTierValidation Exhibits/VirtualizedGeometry/Code/FacetTierValidation.cpp)
    target_link_libraries(FacetTierValidation PRIVATE FrontierGeometry)
    add_test(NAME FacetTierValidation COMMAND FacetTierValidation)
    # Log stream panel stack: recorded startup + structured channel filtering (host-buildable).
    add_executable(LogStreamPanelValidation Exhibits/LogStreamPanel/Code/LogStreamPanelValidation.cpp)
    target_link_libraries(LogStreamPanelValidation PRIVATE FrontierInterface FrontierLifecycle)
    target_compile_definitions(LogStreamPanelValidation PRIVATE FRONTIER_EDITOR=1)
    add_test(NAME LogStreamPanelValidation COMMAND LogStreamPanelValidation
        "${CMAKE_BINARY_DIR}/log-stream-panel.ppm")
endif()
