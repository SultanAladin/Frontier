@echo off
REM ============================================================================
REM  SketchModelViewport\Build.bat - build SketchModelViewport.exe: a standalone
REM  Vulkan validation host for the sketch-model matcap viewport. Opens one native
REM  Vulkan window + ImGui interface, brings up the SvgIconRegistry against the
REM  host, registers both icon tiers, builds one caller-owned perspective viewport
REM  (grid + axis + the SpatialCompass overlay) and drives ConstructViewportPanel
REM  each frame until the window closes.
REM
REM  The viewport panel + theme + icons all live in EngineContext.lib; the Vulkan
REM  host + ImGui interface live in Graphics.lib; the window + surface + relay live
REM  in Platform.lib. So this app rebuilds the shared pillar libs first via their
REM  own Build.bat, then compiles ONLY its own units and links EngineContext +
REM  Graphics + Platform plus vulkan-1.lib + the OS libs. Phase 1 runs the GPU analytic
REM  ground grid, so it STAGES the two AnalyticGroundPlane .spv modules into
REM  %OUTDIR%\Shaders (the runtime resolves them via the relative "Shaders" dir). It
REM  writes its OWN exe under Binaries\Validation and never touches the others.
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=SketchModelViewport"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "LIBDIR=%ROOT%\Build\lib"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "OUTPUT=%OUTDIR%\%NAME%.exe"
set "DEFINE=FRONTIER_SKETCH_MODEL_VIEWPORT_VALIDATION"

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
REM  Platform (window + surface + relay), Graphics (Vulkan host + ImGui interface),
REM  EngineContext (viewport panel/theme/icons + vendored ImGui core).
call "%ROOT%\Internal\Platform\Build.bat"
if errorlevel 1 goto :fail
call "%ROOT%\Internal\Graphics\Build.bat"
if errorlevel 1 goto :fail
call "%ROOT%\Internal\EngineContext\Build.bat"
if errorlevel 1 goto :fail
REM  AuthoringParametric (the standalone Workplane construction-plane model the viewport overlay draws).
call "%ROOT%\Internal\Authoring\ParametricAuthoring\Build.bat"
if errorlevel 1 goto :fail
if not exist "%LIBDIR%\AuthoringParametric.lib" (
    echo [%NAME%] AuthoringParametric.lib missing after build - aborting.
    goto :fail
)
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
REM  The two embedded validation surfaces (SceneDirectoryInspector card on Tab, ConstructionCatalogue
REM  console on right-click) are compiled IN PLACE from the sibling app folders, so their headers must be
REM  on the include path. Their own Host.cpp files carry main() and are skipped in the compile loop below.
set "SDIDIR=%APPDIR%\..\SceneDirectoryInspectorValidation"
set "CCDIR=%APPDIR%\..\ConstructionCatalogueValidation"
REM  The standalone Workplane construction-plane model the viewport overlay draws (AuthoringParametric.lib).
set "APDIR=%ROOT%\Internal\Authoring\ParametricAuthoring"
REM  mapbox::earcut — 2D triangulation with holes, the fill path for a boolean-result Profile (header-only).
set "EARCUT=%ROOT%\ExternalPackages\earcut"
REM  The matcap solid pass (ParametricSketchSolidSequence.h) reaches PolygonCluster.h, which includes its neighbours UNROOTED
REM  ("LinearAlgebra_Float64.h"), so those two folders must be include roots exactly as Internal\Graphics\Build.bat sets them.
set "MATHROOT=%ROOT%\Internal\EngineContext\Math"
set "GEOMROOT=%ROOT%\Internal\Authoring\Geometry\Modeling"
set "INCLUDES=/I"%ROOT%\Internal" /I"%IMGUI%" /I"%IMGUI%\backends" /I"%VULKAN%\Include" /I"%SDIDIR%" /I"%CCDIR%" /I"%APDIR%" /I"%EARCUT%" /I"%MATHROOT%" /I"%GEOMROOT%""
REM  FRONTIER_DEVELOPMENT_PROFILE keeps Trace/Notice diagnostics AND turns on the
REM  Vulkan validation layer by default. Swap to FRONTIER_SHIPPING_PROFILE for lean builds.
set "DEFINES=/DUNICODE /D_UNICODE /D%DEFINE% /DFRONTIER_DEVELOPMENT_PROFILE"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Compile this app's own units --------------------------------------------
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

REM --- Compile the two embedded surfaces' units, in place from the sibling folders --------------
REM  Every .cpp EXCEPT each folder's own *Host.cpp (those carry main()). The data model, glyph pack,
REM  panel, console bridge, content profile, and generated catalogue table all link into this exe.
for /R "%SDIDIR%" %%F in (*.cpp) do (
    set "UNIT=%%~nF"
    echo !UNIT! | findstr /I /C:"ValidationHost" >nul
    if errorlevel 1 (
        echo [compile] !UNIT!
        cl %CXXFLAGS% %DEFINES% %INCLUDES% "%%F" /Fo"%OBJ%\!UNIT!.obj" /Fd"%OBJ%\%NAME%.pdb"
        if errorlevel 1 (
            echo [%NAME%] COMPILE FAILED
            goto :fail
        )
        echo "%OBJ%\!UNIT!.obj">>"%OBJRSP%"
    )
)
for /R "%CCDIR%" %%F in (*.cpp) do (
    set "UNIT=%%~nF"
    echo !UNIT! | findstr /I /C:"ValidationHost" >nul
    if errorlevel 1 (
        echo [compile] !UNIT!
        cl %CXXFLAGS% %DEFINES% %INCLUDES% "%%F" /Fo"%OBJ%\!UNIT!.obj" /Fd"%OBJ%\%NAME%.pdb"
        if errorlevel 1 (
            echo [%NAME%] COMPILE FAILED
            goto :fail
        )
        echo "%OBJ%\!UNIT!.obj">>"%OBJRSP%"
    )
)

REM --- Link the shared libs + thorvg + Vulkan / system libs (no GLFW) ----------
REM  thorvg.lib is the vendored static SVG rasterizer SvgRasterizer.obj (inside
REM  EngineContext.lib) calls into for the icon glyphs.
set "THORVGLIB=%ROOT%\ExternalPackages\thorvg\lib\thorvg.lib"
set "LINKLIBS="%LIBDIR%\EngineContext.lib" "%LIBDIR%\Graphics.lib" "%LIBDIR%\Platform.lib" "%LIBDIR%\AuthoringParametric.lib" "%THORVGLIB%""
set "SYSLIBS="%VULKAN%\Lib\vulkan-1.lib" user32.lib gdi32.lib shell32.lib dwmapi.lib"

echo [%NAME%] linking -^> %OUTPUT%
link /nologo /DEBUG /SUBSYSTEM:CONSOLE @"%OBJRSP%" %LINKLIBS% %SYSLIBS% /OUT:"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] LINK FAILED
    goto :fail
)
REM --- Stage the analytic-grid SPIR-V next to the exe (runtime resolves relative "Shaders") ---
set "SHADERSRC=%ROOT%\Internal\Graphics\Grid\Shaders"
set "SHADEROUT=%OUTDIR%\Shaders"
if not exist "%SHADEROUT%" mkdir "%SHADEROUT%"
copy /Y "%SHADERSRC%\AnalyticGroundPlane.vert.spv" "%SHADEROUT%" >nul
copy /Y "%SHADERSRC%\AnalyticGroundPlane.frag.spv" "%SHADEROUT%" >nul
if not exist "%SHADEROUT%\AnalyticGroundPlane.vert.spv" (
    echo [%NAME%] WARNING: grid vertex SPIR-V not staged - the grid pass will be unavailable at runtime.
)
if not exist "%SHADEROUT%\AnalyticGroundPlane.frag.spv" (
    echo [%NAME%] WARNING: grid fragment SPIR-V not staged - the grid pass will be unavailable at runtime.
)

REM --- Stage the matcap SPIR-V + the chrome matcap disc for the EXTRUDE solid pass ------------
REM  The extruded prisms / thin walls render through ParametricSketchSolidSequence, which loads these two
REM  modules from the relative "Shaders" dir and the matcap PNG from a path under the exe's EngineContent.
REM  A missing PNG or module disables the whole pass (the app falls back to the pure-2D sketch), so both
REM  are staged here and a miss is reported loudly rather than showing up as "extrude renders nothing".
set "SURFACESHADERSRC=%ROOT%\Internal\Graphics\Render\Surface\Shaders"
copy /Y "%SURFACESHADERSRC%\ParametricSketchMatcap.vert.spv" "%SHADEROUT%" >nul
copy /Y "%SURFACESHADERSRC%\ParametricSketchMatcap.frag.spv" "%SHADEROUT%" >nul
if not exist "%SHADEROUT%\ParametricSketchMatcap.vert.spv" (
    echo [%NAME%] WARNING: matcap vertex SPIR-V not staged - extruded bodies will not render.
)
if not exist "%SHADEROUT%\ParametricSketchMatcap.frag.spv" (
    echo [%NAME%] WARNING: matcap fragment SPIR-V not staged - extruded bodies will not render.
)
robocopy "%ROOT%\EngineContent\ReferenceMaterials\Matcaps" "%OUTDIR%\EngineContent\ReferenceMaterials\Matcaps" Matcap-Chrome.png /NFL /NDL /NJH /NJS /NP >nul
if %ERRORLEVEL% GEQ 8 goto :fail
if not exist "%OUTDIR%\EngineContent\ReferenceMaterials\Matcaps\Matcap-Chrome.png" (
    echo [%NAME%] WARNING: chrome matcap not staged - extruded bodies will not render.
)

REM --- Stage the UI-scale config next to the exe so the theme resolver finds it ---------------
robocopy "%ROOT%\EngineContent\Config" "%OUTDIR%\EngineContent\Config" InterfaceScale.config /NFL /NDL /NJH /NJS /NP >nul
if %ERRORLEVEL% GEQ 8 goto :fail

echo [%NAME%] OK -^> %OUTPUT%
endlocal & exit /b 0

:fail
echo [%NAME%] BUILD FAILED
endlocal & exit /b 1
