@echo off
REM ============================================================================
REM  Internal\Authoring\ParametricAuthoring\Build.bat - compile the parametric
REM  authoring subtree to one static lib (AuthoringParametric.lib).
REM
REM  ADAPTIVE + SKIP: globs its own .cpp recursively (nothing hand-listed), then
REM  asks the shared planner (Automation\BuildPlan.ps1) which units are stale. A
REM  unit recompiles only when its .obj is missing, its .cpp changed, or a header
REM  it actually included (per cl /sourceDependencies) changed; the .lib is
REM  re-archived only when an object changed. An unchanged tree does nothing.
REM
REM  Scope is the CPU-only parametric-sketch model: the analytic shape store +
REM  factory + flatten (ParametricSketchShapeStore), the constraint solver, and
REM  the Operations subtree (Boolean / Fillet / Loft / Transform). It bridges the
REM  sketch to the GPU via the Register/Retrieve body + image slots the renderer
REM  reads. NO Vulkan - device-independent - but it DOES include imgui.h (for
REM  ImTextureID + ImVec2 on the bridge structs), so the ImGui root is on the path.
REM  The Boolean / Transform ops call Clipper2 and Loft calls earcut, so those
REM  vendored trees are include roots; Clipper2's three .cpp fold into this lib.
REM
REM  Requires an MSVC environment (cl.exe on PATH). Run this through the
REM  PowerShell tool, never Bash (it shells to BuildPlan.ps1).
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "EXTDIR=%~dp0"
if "%EXTDIR:~-1%"=="\" set "EXTDIR=%EXTDIR:~0,-1%"
for %%I in ("%EXTDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=AuthoringParametric"
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
REM  Pillar headers resolve pillar-rooted (/I Internal). The subtree includes its
REM  own headers by BARE name (siblings + the ParametricSketchShapeStore.h at the
REM  subtree root), so the ParametricAuthoring root goes on the path as its own
REM  root. imgui.h is needed for the bridge structs; clipper2\include for the
REM  Boolean / Transform ops; earcut for Loft.
set "IMGUI=%ROOT%\ExternalPackages\imgui"
set "CLIPPER=%ROOT%\ExternalPackages\clipper2\include"
set "CLIPPERSRC=%ROOT%\ExternalPackages\clipper2\src"
set "EARCUT=%ROOT%\ExternalPackages\earcut"
set "INCLUDES=/I"%ROOT%\Internal" /I"%EXTDIR%" /I"%IMGUI%" /I"%CLIPPER%" /I"%EARCUT%""
set "DEFINES=/DUNICODE /D_UNICODE /DFRONTIER_AUTHORING_PARAMETRIC"
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

REM --- Vendored Clipper2 (built once, missing-obj gate only) -------------------
REM  The polygon-clipping engine the Boolean / Transform (offset) ops call. Never
REM  edited; the planner recompiles them only when their .obj is absent. They fold
REM  into this lib so the parametric ops resolve Clipper2 symbols without a
REM  separate lib.
set "VENDOR_SRCS=%CLIPPERSRC%\clipper.engine.cpp;%CLIPPERSRC%\clipper.offset.cpp;%CLIPPERSRC%\clipper.rectclip.cpp"

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

echo "%OBJF%">>"%OBJRSP%"
goto :eof
