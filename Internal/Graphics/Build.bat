@echo off
REM ============================================================================
REM  Internal\Graphics\Build.bat - compile this ONE pillar to a static lib.
REM
REM  ADAPTIVE + SKIP: globs its own .cpp recursively (nothing hand-listed), then
REM  asks the shared planner (Automation\BuildPlan.ps1) which units are stale. A
REM  unit recompiles only when its .obj is missing, its .cpp changed, or a header
REM  it actually included (per cl /sourceDependencies) changed; the .lib is
REM  re-archived only when an object changed. An unchanged tree does nothing.
REM
REM  Graphics owns the render coordinator: the Vulkan host + ImGui swapchain
REM  interface (VulkanHost, VulkanImguiInterface, WindowSubstrate), the ground
REM  grid, and the Hillaire sky. It includes imgui.h + the Vulkan ImGui backend
REM  header + stb_image.h by include-root name, its own headers by "Graphics/..."
REM  and cross-pillar headers by "EngineContext/..." / "Platform/..." - so /I
REM  Internal is on the path. It carries NO vendored ImGui objects (those fold
REM  into EngineContext.lib); it links nothing itself. No GLFW.
REM
REM  Requires an MSVC environment (cl.exe on PATH). Run this through the
REM  PowerShell tool, never Bash (it shells to BuildPlan.ps1).
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "EXTDIR=%~dp0"
if "%EXTDIR:~-1%"=="\" set "EXTDIR=%EXTDIR:~0,-1%"
for %%I in ("%EXTDIR%\..\..") do set "ROOT=%%~fI"

set "NAME=Graphics"
set "LIBDIR=%ROOT%\Build\lib"
set "OBJ=%ROOT%\Build\obj\%NAME%"
set "OUTPUT=%LIBDIR%\%NAME%.lib"
set "PLANNER=%ROOT%\Automation\BuildPlan.ps1"

if not exist "%LIBDIR%" mkdir "%LIBDIR%"
if not exist "%OBJ%" mkdir "%OBJ%"

where cl >nul 2>nul
if errorlevel 1 (
    echo [%NAME%] cl.exe not on PATH - run vcvars64.bat first ^(or use the root Build.bat^).
    endlocal & exit /b 1
)

REM --- Include roots + compiler configuration ---------------------------------
REM  Pillar headers resolve pillar-rooted (/I Internal); the vendored ImGui the
REM  render bridge is written against needs its root + backends dir; Vulkan SDK
REM  headers for the device layer.
set "IMGUI=%ROOT%\ExternalPackages\imgui"
set "STB=%ROOT%\ExternalPackages\stb"
set "VULKAN=%VULKAN_SDK%"
if not defined VULKAN set "VULKAN=C:\VulkanSDK\1.4.335.0"
REM  BufferAllocation reaches into Authoring's PolygonCluster.h, which (like the
REM  EngineContext Math headers) bare-includes its siblings - LinearAlgebra_Float64.h
REM  (EngineContext\Math) and VertexField.h / PolygonDescriptor.h (Authoring\Geometry\
REM  Modeling). Those two dirs go on the path as include roots so the bare names resolve
REM  from a Graphics .cpp that is not their sibling.
set "MATHROOT=%ROOT%\Internal\EngineContext\Math"
set "GEOMROOT=%ROOT%\Internal\Authoring\Geometry\Modeling"
set "INCLUDES=/I"%ROOT%\Internal" /I"%IMGUI%" /I"%IMGUI%\backends" /I"%STB%" /I"%MATHROOT%" /I"%GEOMROOT%" /I"%VULKAN%\Include""
set "DEFINES=/DUNICODE /D_UNICODE /DFRONTIER_GRAPHICS"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Collect this pillar's own sources (recursive, no manual list) ----------
REM  Written one-per-line to a file the planner reads via SRC_LIST_FILE, NOT
REM  concatenated into OWN_SRCS: a long path list overflows cmd's 8191-char
REM  environment-variable cap, which silently truncates the tail units so they
REM  never compile. A file has no length cap.
set "SRC_LIST_FILE=%OBJ%\own_srcs.txt"
if exist "%SRC_LIST_FILE%" del /Q "%SRC_LIST_FILE%"
set "HAVE_SRCS="
for /R "%EXTDIR%" %%F in (*.cpp) do (
    echo %%F>>"%SRC_LIST_FILE%"
    set "HAVE_SRCS=1"
)
if not defined HAVE_SRCS (
    echo [%NAME%] no sources yet - skeleton only, nothing to compile.
    endlocal & exit /b 0
)

REM --- Ask the planner what is stale, compile only those ----------------------
set "VENDOR_SRCS="
set "HEADER_ROOTS=%EXTDIR%"
set "OBJRSP=%OBJ%\lib_objs.rsp"
if exist "%OBJRSP%" del /Q "%OBJRSP%"
set "LINK_NEEDED="
set "COMPILE_FAILED="

for /f "usebackq tokens=1,2,* delims=|" %%a in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%PLANNER%"`) do (
    if /I "%%a"=="LINK" (
        if /I "%%b"=="YES" set "LINK_NEEDED=1"
    ) else (
        call :Plan "%%a" "%%b" "%%c"
    )
)

if defined COMPILE_FAILED (
    echo [%NAME%] COMPILE FAILED
    endlocal & exit /b 1
)

if not defined LINK_NEEDED (
    echo [%NAME%] up to date -^> %OUTPUT%
    endlocal & exit /b 0
)

REM --- Archive (object list via @response file, no 8191-char cap) --------------
echo [%NAME%] archiving -^> %OUTPUT%
lib /nologo @"%OBJRSP%" /OUT:"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] ARCHIVE FAILED
    endlocal & exit /b 1
)
echo [%NAME%] OK -^> %OUTPUT%
endlocal & exit /b 0

REM ============================================================================
REM  :Plan  KIND  NAME  SRC-OR-REASON
REM  Records this unit's obj in the archive response file, then compiles it when
REM  the planner asked for COMPILE (SKIP just keeps the existing obj).
REM ============================================================================
:Plan
set "KIND=%~1"
set "UNIT=%~2"
set "ARG=%~3"
set "OBJF=%OBJ%\%UNIT%.obj"
echo "%OBJF%">>"%OBJRSP%"

if /I "%KIND%"=="SKIP" (
    echo [SKIP] %UNIT% : %ARG%
    goto :eof
)

if defined COMPILE_FAILED goto :eof
echo [compile] %UNIT%
cl %CXXFLAGS% %DEFINES% %INCLUDES% "%ARG%" /Fo"%OBJF%" /Fd"%OBJ%\%NAME%.pdb" /sourceDependencies "%OBJ%\%UNIT%.json"
if errorlevel 1 (
    echo [compile failed] %UNIT%
    set "COMPILE_FAILED=1"
)
goto :eof
