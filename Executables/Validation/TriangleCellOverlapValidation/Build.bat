@echo off
REM ============================================================================
REM  TriangleCellOverlapValidation\Build.bat - build + run the exe: the exit gate
REM  for the exact triangle-cell overlap predicate. Proves the 13-axis SAT and
REM  the mesh sweep over it are unit-correct IN ISOLATION - no Vulkan, no pillar
REM  lib. Compiles only the three sources it needs directly:
REM    - TriangleCellOverlapValidationEntry.cpp  (entry: the PASS/FAIL checks)
REM    - TriangleCellOverlap.cpp                 (the predicate under test)
REM    - ToroidalClipmapField.cpp                (the lattice it voxelizes into)
REM  Runs on a successful link; a failing check returns non-zero so this surfaces
REM  a regression.
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=TriangleCellOverlapValidation"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "OUTPUT=%OUTDIR%\%NAME%.exe"

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
REM  Internal : the one pillar-rooted root, matching EngineContext\Build.bat. The
REM             sources include "EngineContext/SpatialAcceleration/..." and pull
REM             "EngineContext/Math/LinearAlgebra_Float32.h" through the header.
set "INCLUDES=/I"%ROOT%\Internal""
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3"

REM --- The three sources this tool compiles directly ---------------------------
set "SRC_ENTRY=%APPDIR%\TriangleCellOverlapValidationEntry.cpp"
set "SRC_OVERLAP=%ROOT%\Internal\EngineContext\SpatialAcceleration\TriangleCellOverlap.cpp"
set "SRC_SPINE=%ROOT%\Internal\EngineContext\SpatialAcceleration\ToroidalClipmapField.cpp"

set "OBJRSP=%OBJ%\link_objs.rsp"
if exist "%OBJRSP%" del /Q "%OBJRSP%"
for %%F in ("%SRC_ENTRY%" "%SRC_OVERLAP%" "%SRC_SPINE%") do (
    set "UNIT=%%~nF"
    echo [compile] !UNIT!
    cl %CXXFLAGS% %INCLUDES% "%%~F" /Fo"%OBJ%\!UNIT!.obj" /Fd"%OBJ%\%NAME%.pdb"
    if errorlevel 1 (
        echo [%NAME%] COMPILE FAILED
        goto :fail
    )
    echo "%OBJ%\!UNIT!.obj">>"%OBJRSP%"
)

echo [%NAME%] linking -^> %OUTPUT%
link /nologo /DEBUG /SUBSYSTEM:CONSOLE @"%OBJRSP%" /OUT:"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] LINK FAILED
    goto :fail
)
echo [%NAME%] OK -^> %OUTPUT%

REM --- Run the checks ---------------------------------------------------------
echo [%NAME%] running checks
"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] CHECKS FAILED
    goto :fail
)

echo [%NAME%] DONE
endlocal & exit /b 0

:fail
echo [%NAME%] BUILD FAILED
endlocal & exit /b 1
