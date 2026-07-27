@echo off
REM ============================================================================
REM  RenderExtensionValidation\Build.bat - build RenderExtensionValidation.exe:
REM  a standalone test bed for the modular RenderExtension. Opens one native
REM  Vulkan window, inspects the GPU feature profile, and drives the
REM  clear -> grid -> sky -> present sequence until the window closes.
REM
REM  RenderExtension.cpp lives under Internal\Graphics\RenderExtension and folds
REM  into Graphics.lib automatically (that pillar's Build.bat globs its own .cpp
REM  tree). The camera navigation it drives lives in EngineContext.lib. So this
REM  app rebuilds the shared pillar libs first via their own Build.bat, then
REM  compiles ONLY its entry unit and links EngineContext + Graphics + Platform
REM  plus vulkan-1.lib + the OS libs. No GLFW: the window is the native
REM  PlatformWindow the substrate owns.
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=RenderExtensionValidation"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "LIBDIR=%ROOT%\Build\lib"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "OUTPUT=%OUTDIR%\%NAME%.exe"
set "DEFINE=FRONTIER_RENDER_EXTENSION_VALIDATION"

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
REM  Platform (window + surface), Graphics (Vulkan host + RenderExtension + grid
REM  + sky), EngineContext (camera navigation + ImGui core the pillar carries).
call "%ROOT%\Internal\Platform\Build.bat"
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
if not exist "%LIBDIR%\Platform.lib" (
    echo [%NAME%] Platform.lib missing after build - aborting.
    goto :fail
)

REM --- Include roots (pillar-rooted headers + ImGui + Vulkan; no GLFW) ---------
REM  The RenderExtension header pulls the ImGui + Vulkan backend headers through
REM  its substrate + navigation includes by include-root name, so /I Internal +
REM  the ImGui + Vulkan roots are all on the path.
set "IMGUI=%ROOT%\ExternalPackages\imgui"
set "VULKAN=%VULKAN_SDK%"
if not defined VULKAN set "VULKAN=C:\VulkanSDK\1.4.335.0"
set "INCLUDES=/I"%ROOT%\Internal" /I"%IMGUI%" /I"%IMGUI%\backends" /I"%VULKAN%\Include""
REM  FRONTIER_DEVELOPMENT_PROFILE keeps Trace/Notice diagnostics AND turns on the
REM  Vulkan validation layer by default. Swap to FRONTIER_SHIPPING_PROFILE for lean builds.
set "DEFINES=/DUNICODE /D_UNICODE /D%DEFINE% /DFRONTIER_DEVELOPMENT_PROFILE"
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

REM --- Link the shared libs + Vulkan / system libs (no GLFW) -------------------
set "LINKLIBS="%LIBDIR%\EngineContext.lib" "%LIBDIR%\Graphics.lib" "%LIBDIR%\Platform.lib""
set "SYSLIBS="%VULKAN%\Lib\vulkan-1.lib" user32.lib gdi32.lib shell32.lib dwmapi.lib"

echo [%NAME%] linking -^> %OUTPUT%
link /nologo /DEBUG /SUBSYSTEM:CONSOLE @"%OBJRSP%" %LINKLIBS% %SYSLIBS% /OUT:"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] LINK FAILED
    goto :fail
)

REM --- Stage SPIR-V shaders beside the exe -------------------------------------
REM  The grid + sky loaders resolve their modules from a relative "Shaders" dir
REM  (see FRONTIER_GRID_SHADER_DIR / FRONTIER_SKY_SHADER_DIR in RenderExtension.cpp),
REM  so the compiled .spv must sit in %OUTDIR%\Shaders for the exe to find them
REM  when launched from its own directory. Copied fresh each build so a recompiled
REM  shader propagates. Grid = AnalyticGroundPlane.*, sky = the atmosphere set.
set "SHADEROUT=%OUTDIR%\Shaders"
if not exist "%SHADEROUT%" mkdir "%SHADEROUT%"
copy /Y "%ROOT%\Internal\Graphics\Grid\Shaders\*.spv" "%SHADEROUT%" >nul
copy /Y "%ROOT%\Internal\Graphics\Atmosphere\Shaders\*.spv" "%SHADEROUT%" >nul
echo [%NAME%] staged shaders -^> %SHADEROUT%

echo [%NAME%] OK -^> %OUTPUT%
endlocal & exit /b 0

:fail
echo [%NAME%] BUILD FAILED
endlocal & exit /b 1
