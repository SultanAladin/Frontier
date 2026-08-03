@echo off
REM ============================================================================
REM  SimulationSandbox\Build.bat - build SimulationSandbox.exe: a standalone launcher for the single Simulation workspace. A native Vulkan
REM  window + ImGui interface that clears to black and drives the shared interior
REM  WorkspacePanelDock every frame (its own host window + partition tree + tab
REM  strip). No renderer passes of its own, so NO shaders are staged.
REM
REM  ImGui core + the Win32/Vulkan backends live in EngineContext.lib (its
REM  Build.bat globs the vendored ImGui tree). So this app rebuilds the shared
REM  pillar libs first via their own Build.bat, then compiles ONLY its lifecycle
REM  unit and links EngineContext + Graphics + Platform plus vulkan-1.lib + the OS
REM  libs. No GLFW: the window is the native PlatformWindow and ImGui's OS half is
REM  the Win32 backend fed by the app-installed message relay (dwmapi at link time).
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..") do set "ROOT=%%~fI"

set "NAME=SimulationSandbox"
set "OUTDIR=%ROOT%\Binaries\%NAME%"
set "LIBDIR=%ROOT%\Build\lib"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "OUTPUT=%OUTDIR%\%NAME%.exe"
set "DEFINE=FRONTIER_SIMULATIONSANDBOX"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OBJ%" mkdir "%OBJ%"

REM --- Activate MSVC if cl.exe is not already visible --------------------------
where cl >nul 2>nul
if errorlevel 1 (
    set "VCVARS="
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
    if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    if not exist "!VCVARS!" (
        echo [%NAME%] could not find vcvars64.bat - edit the VCVARS fallback.
        endlocal ^& exit /b 1
    )
    call "!VCVARS!" >nul
)

REM --- Ensure the shared pillar libs exist + are current ----------------------
REM  Platform (window + surface), Graphics (Vulkan host + ImGui interface),
REM  EngineContext (Interface + Theme + the vendored ImGui core the pillar carries).
call "%ROOT%\Internal\Platform\Build.bat"
if errorlevel 1 goto :fail
call "%ROOT%\Internal\Graphics\Build.bat"
if errorlevel 1 goto :fail
call "%ROOT%\Internal\EngineContext\Build.bat"
if errorlevel 1 goto :fail
if not exist "%LIBDIR%\EngineContext.lib" (
    echo [%NAME%] EngineContext.lib missing after build - aborting.
    goto :fail
)
if not exist "%LIBDIR%\Graphics.lib" (
    echo [%NAME%] Graphics.lib missing after build - aborting.
    goto :fail
)
if not exist "%LIBDIR%\Platform.lib" (
    echo [%NAME%] Platform.lib missing after build - aborting.
    goto :fail
)

REM --- Include roots (pillar-rooted headers + ImGui + Vulkan; no GLFW) ---------
set "IMGUI=%ROOT%\ExternalPackages\imgui"
set "VULKAN=%VULKAN_SDK%"
if not defined VULKAN set "VULKAN=C:\VulkanSDK\1.4.335.0"
set "INCLUDES=/I"%ROOT%\Internal" /I"%IMGUI%" /I"%IMGUI%\backends" /I"%VULKAN%\Include""
REM  FRONTIER_DEVELOPMENT_PROFILE keeps Trace/Notice diagnostics AND turns on the
REM  Vulkan validation layer by default. Swap to FRONTIER_SHIPPING_PROFILE for lean builds.
set "DEFINES=/DUNICODE /D_UNICODE /D%DEFINE% /DFRONTIER_DEVELOPMENT_PROFILE"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Compile this app's lifecycle unit(s) -----------------------------------
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

REM --- Link the shared libs + thorvg + Vulkan / system libs (no GLFW) ----------
REM  thorvg.lib is the vendored static SVG rasterizer SvgRasterizer.obj (inside
REM  EngineContext.lib) calls into for the icon glyphs. EngineContext.lib compiles
REM  green without it - it only needs the header - so the omission surfaces ONLY
REM  at link time, in whichever exe drags SvgRasterizer.obj in. Same seam every
REM  other icon-registry exe already carries (SketchOutliner, SceneDirectory, ...).
set "THORVGLIB=%ROOT%\ExternalPackages\thorvg\lib\thorvg.lib"
set "LINKLIBS="%LIBDIR%\EngineContext.lib" "%LIBDIR%\Graphics.lib" "%LIBDIR%\Platform.lib" "%THORVGLIB%""
set "SYSLIBS="%VULKAN%\Lib\vulkan-1.lib" user32.lib gdi32.lib shell32.lib dwmapi.lib"

echo [%NAME%] linking -^> %OUTPUT%
link /nologo /DEBUG /SUBSYSTEM:CONSOLE @"%OBJRSP%" %LINKLIBS% %SYSLIBS% /OUT:"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] LINK FAILED
    goto :fail
)
echo [%NAME%] OK -^> %OUTPUT%
endlocal & exit /b 0

:fail
echo [%NAME%] BUILD FAILED
endlocal & exit /b 1
