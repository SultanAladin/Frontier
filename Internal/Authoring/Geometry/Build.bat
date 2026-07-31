@echo off
REM ============================================================================
REM  Internal\Authoring\Geometry\Build.bat - compile the Authoring GEOMETRY
REM  subtree to one static lib (AuthoringGeometry.lib).
REM
REM  ADAPTIVE + SKIP: globs its own .cpp recursively (nothing hand-listed), then
REM  asks the shared planner (Automation\BuildPlan.ps1) which units are stale. A
REM  unit recompiles only when its .obj is missing, its .cpp changed, or a header
REM  it actually included (per cl /sourceDependencies) changed; the .lib is
REM  re-archived only when an object changed. An unchanged tree does nothing.
REM
REM  Scope is the CPU-only geometry foundation the renderer and tools reuse: the
REM  authoring PolygonCluster + VertexField + descriptor, face triangulation, the
REM  display derivation (ConstructRenderVertexStream), adjacency / picking /
REM  selection / UV, the asset Interchange decoders (glTF / OBJ / FBX), and the
REM  WorkspaceDocument scene format. It bare-includes LinearAlgebra_Float64.h
REM  (EngineContext\Math) and the vendored decoder + toml headers by root name,
REM  so those dirs are on the include path. NO Vulkan, NO ImGui - this subtree is
REM  device-independent (the GPU Baking / Painting units live OUTSIDE Geometry\
REM  and are not built here).
REM
REM  Requires an MSVC environment (cl.exe on PATH). Run this through the
REM  PowerShell tool, never Bash (it shells to BuildPlan.ps1).
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "EXTDIR=%~dp0"
if "%EXTDIR:~-1%"=="\" set "EXTDIR=%EXTDIR:~0,-1%"
for %%I in ("%EXTDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=AuthoringGeometry"
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
REM  Pillar headers resolve pillar-rooted (/I Internal). PolygonCluster / VertexField
REM  bare-include LinearAlgebra_Float64.h (EngineContext\Math) and their Modeling
REM  siblings; the Interchange decoders include cgltf.h / fast_obj.h / ufbx.h and the
REM  WorkspaceDocument format includes toml.hpp - all vendored, added as include roots.
set "MATHROOT=%ROOT%\Internal\EngineContext\Math"
REM  The ported Geometry units cross-include their siblings by BARE name (no path), so
REM  every subdir that owns such a header goes on the include path as its own root.
set "G=%ROOT%\Internal\Authoring\Geometry"
set "GEOMROOTS=/I"%G%\Modeling" /I"%G%\Modeling\Display" /I"%G%\Modeling\Primitives" /I"%G%\Modeling\Fixtures" /I"%G%\Adjacency" /I"%G%\Selection" /I"%G%\UV" /I"%G%\Picking" /I"%G%\Interchange""
set "CGLTF=%ROOT%\ExternalPackages\cgltf"
set "FASTOBJ=%ROOT%\ExternalPackages\fast_obj"
set "UFBX=%ROOT%\ExternalPackages\ufbx"
set "TOMLPP=%ROOT%\ExternalPackages\tomlpp"
set "EARCUT=%ROOT%\ExternalPackages\earcut"
set "INCLUDES=/I"%ROOT%\Internal" /I"%MATHROOT%" %GEOMROOTS% /I"%CGLTF%" /I"%FASTOBJ%" /I"%UFBX%" /I"%TOMLPP%" /I"%EARCUT%""
REM  toml++ compiled no-throw (parse errors arrive in the parse_result, never thrown).
set "DEFINES=/DUNICODE /D_UNICODE /DFRONTIER_AUTHORING_GEOMETRY /DTOML_EXCEPTIONS=0"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Collect this subtree's own sources (recursive, no manual list) ---------
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

REM  An unchanged unit keeps its existing .obj, so it joins the archive list untouched.
if /I "%KIND%"=="SKIP" (
    echo "%OBJF%">>"%OBJRSP%"
    echo [SKIP] %UNIT% : %ARG%
    goto :eof
)

if defined COMPILE_FAILED goto :eof
echo [compile] %UNIT%
cl %CXXFLAGS% %DEFINES% %INCLUDES% "%ARG%" /Fo"%OBJF%" /Fd"%OBJ%\%NAME%.pdb" /sourceDependencies "%OBJ%\%UNIT%.json"
if errorlevel 1 (
    echo [compile failed] %UNIT%
    set "COMPILE_FAILED=1"
    goto :eof
)

REM  Recorded ONLY after cl succeeded. Appending before the compile (the previous shape) meant a FAILED
REM  unit still contributed its name to the archive response file, so `lib` was asked for an .obj that was
REM  never written and answered with `LNK1181: cannot open input file` - which reads as a link/archive
REM  problem and sends you hunting through the wrong layer. The real compile error is what should surface.
echo "%OBJF%">>"%OBJRSP%"
goto :eof
