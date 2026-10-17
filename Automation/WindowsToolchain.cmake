# Cross-compile Windows x64 with the pinned Zig distribution installed from PyPI.
# Native Windows users can keep using MSVC and do not need this file.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" -c
    "import pathlib, ziglang; print(pathlib.Path(ziglang.__file__).parent / 'zig')"
    OUTPUT_VARIABLE FRONTIER_ZIG OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE ZIG_STATUS)
if(NOT ZIG_STATUS EQUAL 0 OR NOT EXISTS "${FRONTIER_ZIG}")
    message(FATAL_ERROR "Install the cross-toolchain: python -m pip install ziglang==0.13.0")
endif()
execute_process(COMMAND "${FRONTIER_ZIG}" version OUTPUT_VARIABLE ZIG_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT ZIG_VERSION STREQUAL "0.13.0")
    message(FATAL_ERROR "This cross-toolchain is pinned to ziglang==0.13.0")
endif()
set(CMAKE_CXX_COMPILER "${FRONTIER_ZIG}")
set(CMAKE_CXX_COMPILER_ARG1 "c++")
set(CMAKE_CXX_FLAGS_INIT "-target x86_64-windows-gnu -static")
# CMake's platform modules reset archive templates; use executable adapters instead.
set(FRONTIER_ZIG_TOOLS "${CMAKE_CURRENT_BINARY_DIR}/zig-tools")
file(MAKE_DIRECTORY "${FRONTIER_ZIG_TOOLS}")
foreach(TOOL ar ranlib)
    file(WRITE "${FRONTIER_ZIG_TOOLS}/${TOOL}" "#!/bin/sh\nexec '${FRONTIER_ZIG}' ${TOOL} \"$@\"\n")
    file(CHMOD "${FRONTIER_ZIG_TOOLS}/${TOOL}"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()
set(CMAKE_AR "${FRONTIER_ZIG_TOOLS}/ar" CACHE FILEPATH "Zig archiver" FORCE)
set(CMAKE_RANLIB "${FRONTIER_ZIG_TOOLS}/ranlib" CACHE FILEPATH "Zig archive index" FORCE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(FRONTIER_D3DCOMPILER d3dcompiler_47 CACHE STRING "Windows shader compiler import library")
