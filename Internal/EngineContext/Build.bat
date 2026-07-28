@echo off
REM ============================================================================
REM  Internal\EngineContext\Build.bat - compile this ONE pillar to a static lib.
REM
REM  ADAPTIVE + SKIP: globs its own .cpp recursively (nothing hand-listed), then
REM  asks the shared planner (Automation\BuildPlan.ps1) which units are stale. A
REM  unit recompiles only when its .obj is missing, its .cpp changed, or a header
REM  it actually included (per cl /sourceDependencies) changed; the .lib is
REM  re-archived only when an object changed. An unchanged tree does nothing.
REM
REM  EngineContext owns the Interface (Theme, Instrumentation, Components,
REM  WorkspaceHost incl. ImguiPlatformRelay, Workspaces), Navigation, Math, and
REM  MicroUtils. Its UI is written against the vendored ImGui, so this lib carries
REM  the ImGui core + BOTH backends (Win32 + Vulkan) as vendored objects - every
REM  workspace and the shared host link the ImGui symbols out of this one lib. No
REM  GLFW: the OS window is the native PlatformWindow and ImGui's OS half is the
REM  Win32 backend fed by the app-installed message relay (dwmapi at link time).
REM  Headers resolve pillar-rooted (/I Internal) plus the ImGui + Vulkan roots.
REM
REM  Requires an MSVC environment (cl.exe on PATH). Run this through the
REM  PowerShell tool, never Bash (it shells to BuildPlan.ps1).
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "EXTDIR=%~dp0"
if "%EXTDIR:~-1%"=="\" set "EXTDIR=%EXTDIR:~0,-1%"
for %%I in ("%EXTDIR%\..\..") do set "ROOT=%%~fI"

set "NAME=EngineContext"
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
set "IMGUI=%ROOT%\ExternalPackages\imgui"
set "THORVG=%ROOT%\ExternalPackages\thorvg\inc"
set "VULKAN=%VULKAN_SDK%"
if not defined VULKAN set "VULKAN=C:\VulkanSDK\1.4.335.0"
REM  Scene\WorkspaceDocumentRegister.cpp includes the Authoring WorkspaceDocumentEncoder.h, which
REM  bare-includes PolygonCluster.h / VertexField.h / PolygonDescriptor.h (Authoring\Geometry\
REM  Modeling) and LinearAlgebra_Float64.h (EngineContext\Math). Those dirs go on the path as
REM  include roots so the bare names resolve; the register itself pulls no toml (encode/decode
REM  live in AuthoringGeometry.lib), so no tomlpp root is needed here.
set "MATHROOT=%ROOT%\Internal\EngineContext\Math"
set "GEOMROOT=%ROOT%\Internal\Authoring\Geometry\Modeling"
set "INCLUDES=/I"%ROOT%\Internal" /I"%IMGUI%" /I"%IMGUI%\backends" /I"%THORVG%" /I"%MATHROOT%" /I"%GEOMROOT%" /I"%VULKAN%\Include""
REM  TVG_STATIC switches the vendored thorvg.h from its default __declspec(dllimport)
REM  API to plain static linkage, matching the static ExternalPackages\thorvg\lib\thorvg.lib.
REM  Without it SvgRasterizer.obj emits __imp_ dllimport references no static lib can satisfy.
set "DEFINES=/DUNICODE /D_UNICODE /DFRONTIER_ENGINECONTEXT /DTVG_STATIC"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Collect this pillar's own sources (recursive, no manual list) ----------
REM  Written one-per-line to a file the planner reads via SRC_LIST_FILE, NOT
REM  concatenated into OWN_SRCS: this pillar's path list (~80 .cpp) exceeds cmd's
REM  8191-char environment-variable cap, which silently truncates the tail units
REM  so they never compile (ProjectionEvaluator.cpp and friends went missing that
REM  way). A file has no length cap.
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

REM --- Vendored ImGui core + backends (built once, missing-obj gate only) ------
REM  These live in ExternalPackages and are never edited; the planner recompiles
REM  them only when their .obj is absent. They carry the ImGui symbols the shared
REM  host + all workspaces link against, so they belong in this one lib.
set "VENDOR_SRCS=%IMGUI%\imgui.cpp;%IMGUI%\imgui_draw.cpp;%IMGUI%\imgui_tables.cpp;%IMGUI%\imgui_widgets.cpp;%IMGUI%\backends\imgui_impl_win32.cpp;%IMGUI%\backends\imgui_impl_vulkan.cpp"

REM --- Ask the planner what is stale, compile only those ----------------------
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
