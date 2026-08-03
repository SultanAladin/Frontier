@echo off
REM ============================================================================
REM  InstanceBoundsValidation\Build.bat - build + run InstanceBoundsValidation.exe:
REM  the exit gate for the TOP-level (TLAS) build's first two dispatches. It needs
REM  a REAL DEVICE - it runs InstanceBoundsReduce.comp and InstanceMortonCode.comp
REM  and reads their results back - so it links Graphics.lib (the submission + the
REM  Vulkan host) and EngineContext.lib (the diagnostics the host reports through),
REM  plus vulkan-1.lib.
REM
REM  It creates NO WINDOW: InitializeVulkanHost never creates or queries a surface,
REM  so the entry passes a zero extension count and gets a headless compute device.
REM  That is why Platform.lib is NOT linked here.
REM
REM  SHADER STEP REQUIRED, unlike GeometryArenaValidation. This gate DISPATCHES, so
REM  a stale .spv would have it validate the previous revision of the shader and
REM  report a pass. ShaderPlan.ps1 glob-discovers every .comp under Internal, so no
REM  table edit is needed for the two new modules.
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=InstanceBoundsValidation"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "LIBDIR=%ROOT%\Build\lib"
set "OUTPUT=%OUTDIR%\%NAME%.exe"

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

REM --- Compile the shaders BEFORE the gate runs -------------------------------
REM  A stale .spv is the silent failure this step exists to prevent: the gate would
REM  exercise the previous revision of the shader and report PASS.
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\Automation\ShaderPlan.ps1"
if errorlevel 1 (
    echo [%NAME%] SHADER COMPILE FAILED
    goto :fail
)

REM --- Ensure the pillar libs exist + are current ------------------------------
call "%ROOT%\Internal\Authoring\Geometry\Build.bat"
if errorlevel 1 goto :fail
call "%ROOT%\Internal\Graphics\Build.bat"
if errorlevel 1 goto :fail
call "%ROOT%\Internal\EngineContext\Build.bat"
if errorlevel 1 goto :fail
if not exist "%LIBDIR%\Graphics.lib" (
    echo [%NAME%] Graphics.lib missing after build - aborting.
    goto :fail
)
if not exist "%LIBDIR%\EngineContext.lib" (
    echo [%NAME%] EngineContext.lib missing after build - aborting.
    goto :fail
)

REM --- Include roots ----------------------------------------------------------
set "VULKAN=%VULKAN_SDK%"
if not defined VULKAN set "VULKAN=C:\VulkanSDK\1.4.335.0"
set "MATHROOT=%ROOT%\Internal\EngineContext\Math"
set "GEOMROOT=%ROOT%\Internal\Authoring\Geometry\Modeling"
set "INCLUDES=/I"%ROOT%\Internal" /I"%MATHROOT%" /I"%GEOMROOT%" /I"%VULKAN%\Include""

REM  CAUTION: FRONTIER_POLYGON_AUTHORING is ABI-AFFECTING - it adds RenderExtension
REM  members, so it MUST match Internal\Graphics\Build.bat's DEFINES even though
REM  this gate never touches RenderExtension, or the struct layout disagrees
REM  across the Graphics.lib boundary.
set "DEFINES=/DUNICODE /D_UNICODE /DFRONTIER_DEVELOPMENT_PROFILE /DFRONTIER_POLYGON_AUTHORING"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Compile this app's own entry unit --------------------------------------
set "OBJRSP=%OBJ%\link_objs.rsp"
if exist "%OBJRSP%" del /Q "%OBJRSP%"
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
set "LINKLIBS="%LIBDIR%\Graphics.lib" "%LIBDIR%\EngineContext.lib" "%LIBDIR%\AuthoringGeometry.lib""
set "SYSLIBS="%VULKAN%\Lib\vulkan-1.lib" user32.lib gdi32.lib shell32.lib"

echo [%NAME%] linking -^> %OUTPUT%
link /nologo /DEBUG /SUBSYSTEM:CONSOLE @"%OBJRSP%" %LINKLIBS% %SYSLIBS% /OUT:"%OUTPUT%"
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
