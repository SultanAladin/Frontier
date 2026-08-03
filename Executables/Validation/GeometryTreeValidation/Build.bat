@echo off
REM ============================================================================
REM  GeometryTreeValidation\Build.bat - build + run GeometryTreeValidation.exe: the
REM  exit gate for the CPU surface-area-heuristic tree build (the BLAS of the
REM  two-level acceleration structure).
REM
REM  CPU ONLY. Unlike VolumeBoundsValidation this gate needs no device, no
REM  surface and no shaders - the builder it judges is pure C++ over local-space
REM  geometry - so it links neither vulkan-1.lib nor Platform.lib, and it does
REM  NOT run ShaderPlan.
REM
REM  Pass --stress to add the endurance suite (40 randomized meshes). The default
REM  run is the correctness suite only.
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=GeometryTreeValidation"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "OUTPUT=%OUTDIR%\%NAME%.exe"

REM  Forwarded to the exe: --stress selects the endurance suite.
set "RUNARGS=%*"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OBJ%" mkdir "%OBJ%"

REM --- Activate MSVC if cl.exe is not already visible --------------------------
where cl >nul 2>nul
if errorlevel 1 (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    if not exist "!VCVARS!" (
        echo [%NAME%] could not find vcvars64.bat - edit the VCVARS fallback.
        endlocal ^& exit /b 1
    )
    call "!VCVARS!" >nul
)

REM --- Include roots ----------------------------------------------------------
REM  Internal alone: GeometryTreeBuild.h includes nothing but <cstdint> and
REM  <vector>, which is what makes this gate independent of the Graphics pillar.
set "INCLUDES=/I"%ROOT%\Internal""
set "DEFINES=/DUNICODE /D_UNICODE"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Compile the builder + this app's entry unit -----------------------------
REM  GeometryTreeBuild.cpp is compiled DIRECTLY rather than linked from
REM  Graphics.lib, so this gate stays runnable without the whole pillar.
set "OBJRSP=%OBJ%\link_objs.rsp"
if exist "%OBJRSP%" del /Q "%OBJRSP%"

echo [compile] GeometryTreeBuild
cl %CXXFLAGS% %DEFINES% %INCLUDES% "%ROOT%\Internal\Graphics\Acceleration\GeometryTreeBuild.cpp" /Fo"%OBJ%\GeometryTreeBuild.obj" /Fd"%OBJ%\%NAME%.pdb"
if errorlevel 1 (
    echo [%NAME%] COMPILE FAILED
    goto :fail
)
echo "%OBJ%\GeometryTreeBuild.obj">>"%OBJRSP%"

for /R "%APPDIR%" %%F in (*.cpp) do (
    set "UNIT=%%~nF"
    echo [compile] !UNIT!
    cl %CXXFLAGS% %DEFINES% %INCLUDES% "%%F" /Fo"%OBJ%\!UNIT!.obj" /Fd"%OBJ%\%NAME%.pdb"
    if errorlevel 1 (
        echo [%NAME%] COMPILE FAILED
        goto :fail
    )
    echo "%OBJ%\!UNIT!.obj">>"%OBJRSP%"
)

REM --- Link -------------------------------------------------------------------
echo [%NAME%] linking -^> %OUTPUT%
link /nologo /DEBUG /SUBSYSTEM:CONSOLE @"%OBJRSP%" /OUT:"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] LINK FAILED
    goto :fail
)
echo [%NAME%] OK -^> %OUTPUT%

REM --- Run the checks ---------------------------------------------------------
echo [%NAME%] running checks
pushd "%ROOT%"
"%OUTPUT%" %RUNARGS%
set "CHECK_RESULT=%ERRORLEVEL%"
popd
if not "%CHECK_RESULT%"=="0" (
    echo [%NAME%] CHECKS FAILED
    goto :fail
)

echo [%NAME%] DONE
endlocal & exit /b 0

:fail
echo [%NAME%] BUILD FAILED
endlocal & exit /b 1
