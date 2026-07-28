@echo off
REM ============================================================================
REM  SceneDirectory\Build.bat - build SceneDirectory.exe, a STANDALONE validation
REM  window reproducing Outliner.html: a self-contained scene tree with rows,
REM  twisties, real SVG classification icons, filter chips, rename + context/add
REM  menus.
REM
REM  Native Vulkan host. Ported from the old Win32 + D3D11 backend so it can reuse
REM  the Vulkan-only SvgIconRegistry (the same icon store the CAD outliner uses)
REM  and draw the real IconGallery SVGs at full colour instead of procedural line
REM  art. The outliner panel is app-local (its own RecordEntry tree in namespace
REM  SceneDirectoryValidation), NOT the thin shared OutlinerPanel.
REM
REM  The Vulkan host + ImGui interface live in Graphics.lib; the window + surface +
REM  relay live in Platform.lib; the theme + icon registry + packs + vendored ImGui
REM  core/Vulkan backend live in EngineContext.lib. So this app rebuilds the shared
REM  pillar libs first via their own Build.bat, compiles ONLY its own units, and
REM  links EngineContext + Graphics + Platform + thorvg + vulkan-1.lib + OS libs.
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=SceneDirectory"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "LIBDIR=%ROOT%\Build\lib"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "OUTPUT=%OUTDIR%\%NAME%.exe"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OBJ%"    mkdir "%OBJ%"

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
REM  Platform (window + surface + relay), Graphics (Vulkan host + ImGui interface),
REM  EngineContext (theme + icon registry/packs + vendored ImGui core/Vulkan backend).
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
set "DEFINES=/DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DFRONTIER_DEVELOPMENT_PROFILE"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Compile this app's own units every time (they are the files under edit) -
set "OBJRSP=%OBJ%\link_objs.rsp"
if exist "%OBJRSP%" del /Q "%OBJRSP%"
for %%U in (SceneDirectoryPanel SceneDirectoryHost) do (
    echo [compile] %%U.cpp
    cl %CXXFLAGS% %DEFINES% %INCLUDES% "%APPDIR%\%%U.cpp" /Fo"%OBJ%\%%U.obj" /Fd"%OBJ%\%NAME%.pdb"
    if errorlevel 1 (
        echo [%NAME%] COMPILE FAILED
        goto :fail
    )
    echo "%OBJ%\%%U.obj">>"%OBJRSP%"
)

REM --- Link the shared libs + thorvg + Vulkan / system libs (no GLFW) ----------
REM  thorvg.lib is the vendored static SVG rasterizer that SvgRasterizer.obj (inside
REM  EngineContext.lib) calls into for the icon glyphs.
set "THORVGLIB=%ROOT%\ExternalPackages\thorvg\lib\thorvg.lib"
set "LINKLIBS="%LIBDIR%\EngineContext.lib" "%LIBDIR%\Graphics.lib" "%LIBDIR%\Platform.lib" "%THORVGLIB%""
set "SYSLIBS="%VULKAN%\Lib\vulkan-1.lib" user32.lib gdi32.lib shell32.lib dwmapi.lib"

echo [%NAME%] linking -^> %OUTPUT%
link /nologo /DEBUG /SUBSYSTEM:CONSOLE @"%OBJRSP%" %LINKLIBS% %SYSLIBS% /OUT:"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] LINK FAILED
    goto :fail
)

REM --- Stage the UI-scale config next to the exe so the theme resolver finds it
robocopy "%ROOT%\EngineContent\Config" "%OUTDIR%\EngineContent\Config" InterfaceScale.config /NFL /NDL /NJH /NJS /NP >nul
if %ERRORLEVEL% GEQ 8 goto :fail

echo [%NAME%] OK -^> %OUTPUT%
endlocal & exit /b 0

:fail
echo [%NAME%] BUILD FAILED
endlocal & exit /b 1
