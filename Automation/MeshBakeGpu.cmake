#============================================================================================================================================
#                                                           MESHBAKEGPU.CMAKE
#============================================================================================================================================
# 🧩 Build the GPU mesh-bake compute unit, compile+authenticate its shader, and wire the CPU math-mirror
# regression test that proves the kernel's algorithm matches Engine/ContentInterchange/BakeMesh.cpp's trusted
# RunMeshBake reference. MeshBakeGpuMirrorValidation needs no Vulkan driver at all and always runs under
# BUILD_TESTING; a live GPU dispatch/readback test belongs alongside VectorSurfaceValidation's pattern once this
# ships on a machine with an actual Vulkan driver (this sandbox has none - see Engine/Render/MeshBakeGpu.h).

add_library(FrontierMeshBakeGpu STATIC Engine/Render/MeshBakeGpu.cpp Engine/Render/MeshBakeGpuMirror.cpp)
target_include_directories(FrontierMeshBakeGpu PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(FrontierMeshBakeGpu PUBLIC FrontierSurfaceRasterization FrontierBakeMesh)

set(MESH_BAKE_ARCHIVE "${FRONTIER_SURFACE_SHADER_DIRECTORY}/MeshBakeCompute.spv")
if(FRONTIER_SLANGC)
    add_custom_command(OUTPUT "${MESH_BAKE_ARCHIVE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
        COMMAND "${FRONTIER_SLANGC}" "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/MeshBakeCompute.slang"
            -entry MeshBakeCompute -stage compute -target spirv -profile spirv_1_3 -o "${MESH_BAKE_ARCHIVE}"
        DEPENDS Engine/Render/Shader/MeshBakeCompute.slang VERBATIM)
elseif(FRONTIER_SLANG_WASM)
    find_program(FRONTIER_NODE NAMES node)
    if(FRONTIER_NODE)
        add_custom_command(OUTPUT "${MESH_BAKE_ARCHIVE}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}/MeshBakeCompilation.json"
            COMMAND "${FRONTIER_NODE}" "${CMAKE_SOURCE_DIR}/Automation/CompileMeshBakeCompute.mjs"
                "${FRONTIER_SLANG_WASM}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            DEPENDS Automation/CompileMeshBakeCompute.mjs Engine/Render/Shader/MeshBakeCompute.slang "${FRONTIER_SLANG_WASM}" VERBATIM)
    endif()
endif()
if(FRONTIER_SLANGC OR (FRONTIER_SLANG_WASM AND FRONTIER_NODE))
    add_custom_target(FrontierMeshBakeShader ALL DEPENDS "${MESH_BAKE_ARCHIVE}")
    if(TARGET TextureStudio)
        add_dependencies(TextureStudio FrontierMeshBakeShader)
        install(FILES "${MESH_BAKE_ARCHIVE}" DESTINATION EngineContent/ShaderArchives/Surface)
    endif()
endif()

if(BUILD_TESTING)
    # 📝 Pure CPU, no Vulkan driver required - compares Engine/Render/MeshBakeGpuMirror.cpp (a structural
    # transcription of the compute shader) against RunMeshBake texel for texel across every mesh map.
    add_executable(MeshBakeGpuMirrorValidation Exhibits/Renderer/Code/MeshBakeGpuMirrorValidation.cpp)
    target_link_libraries(MeshBakeGpuMirrorValidation PRIVATE FrontierMeshBakeGpu)
    add_test(NAME MeshBakeGpuMirrorValidation COMMAND MeshBakeGpuMirrorValidation)
endif()
