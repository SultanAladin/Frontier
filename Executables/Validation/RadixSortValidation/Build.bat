@echo off
REM ============================================================================
REM  RadixSortValidation\Build.bat - build + run RadixSortValidation.exe: the
REM  exit gate for the GPU radix sort. Unlike the CPU-only validation tools this
REM  one needs a REAL DEVICE - it runs the twenty compute dispatches and judges
REM  the readback against std::stable_sort - so it links Graphics.lib (the sort
REM  submission + Vulkan host) and EngineContext.lib (the diagnostics the host
REM  reports through), plus vulkan-1.lib.
REM
REM  It creates NO WINDOW: InitializeVulkanHost never creates or queries a
REM  surface (it only enables the swapchain DEVICE extension and picks a queue
REM  family on the graphics bit), so the entry passes a zero extension count and
REM  gets a headless compute device. That is what lets the stress mode run
REM  unattended, and it is why Platform.lib is NOT linked here.
REM
REM  Shaders are resolved from the SOURCE tree (Internal\Graphics\Acceleration\
REM  Shaders) rather than staged beside the exe, so the exe must be launched from
REM  the repo root - which is what this script does. ShaderPlan runs first so a
REM  .comp edit cannot ship a stale .spv.
REM
REM  Pass --stress to add the endurance suite (repetition + randomized counts +
REM  at-capacity runs). Slow; the default run is the correctness suite only.
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=RadixSortValidation"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "LIBDIR=%ROOT%\Build\lib"
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

REM --- Ensure the pillar libs exist + are current ------------------------------
REM  Graphics carries RadixSortSubmission + VulkanHost; EngineContext carries the
REM  diagnostics the host reports through. Authoring\Geometry is pulled in because
REM  Graphics' BufferAllocation reaches PolygonCluster.h.
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
REM  Internal for the pillar-rooted headers, Vulkan for vulkan.h. MATHROOT/GEOMROOT
REM  mirror the Graphics pillar: headers reached through this entry bare-include
REM  their siblings. No ImGui root - this gate touches no UI.
set "VULKAN=%VULKAN_SDK%"
if not defined VULKAN set "VULKAN=C:\VulkanSDK\1.4.335.0"
set "MATHROOT=%ROOT%\Internal\EngineContext\Math"
set "GEOMROOT=%ROOT%\Internal\Authoring\Geometry\Modeling"
set "INCLUDES=/I"%ROOT%\Internal" /I"%MATHROOT%" /I"%GEOMROOT%" /I"%VULKAN%\Include""

REM  FRONTIER_DEVELOPMENT_PROFILE keeps the diagnostics AND turns on the Vulkan
REM  validation layer - which is exactly what this gate wants, since a descriptor
REM  or barrier mistake should surface as a validation message, not as a wrong
REM  answer to chase through the shaders.
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
REM  No Platform.lib (headless, no window) and no GLFW. AuthoringGeometry is
REM  needed because Graphics.lib references it.
set "LINKLIBS="%LIBDIR%\Graphics.lib" "%LIBDIR%\EngineContext.lib" "%LIBDIR%\AuthoringGeometry.lib""
set "SYSLIBS="%VULKAN%\Lib\vulkan-1.lib" user32.lib gdi32.lib shell32.lib"

echo [%NAME%] linking -^> %OUTPUT%
link /nologo /DEBUG /SUBSYSTEM:CONSOLE @"%OBJRSP%" %LINKLIBS% %SYSLIBS% /OUT:"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] LINK FAILED
    goto :fail
)
echo [%NAME%] OK -^> %OUTPUT%

REM --- Compile GLSL -> SPIR-V -------------------------------------------------
REM  The four radix .comp must be current before the exe reads them, or the gate
REM  validates a stale binary against fresh source and reports a fault in the
REM  wrong layer. ShaderPlan recompiles only what is stale.
echo [%NAME%] compiling shaders
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\Automation\ShaderPlan.ps1" -Quiet
if errorlevel 1 (
    echo [%NAME%] SHADER COMPILE FAILED
    goto :fail
)

REM --- Run the checks ---------------------------------------------------------
REM  Launched from the REPO ROOT, because the entry resolves the .spv by the
REM  relative path Internal\Graphics\Acceleration\Shaders - reading the source
REM  tree directly rather than a staged copy, so there is no staging step that
REM  could go stale.
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
