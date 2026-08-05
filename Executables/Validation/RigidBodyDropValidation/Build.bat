@echo off
REM ============================================================================
REM  RigidBodyDropValidation\Build.bat - build RigidBodyDropValidation.exe: the
REM  physics validation bed. Authors its own drop scene as .wsdoc documents at
REM  startup, loads them through RenderExtension's normal saved-scene path, and
REM  drives a Jolt rigid-body world that rewrites the instance transforms the
REM  visibility raster uploads each frame. One ImGui control window, recorded
REM  from the shared ControlsGallery components.
REM
REM  Same shape as RenderExtensionValidation\Build.bat (rebuild the four pillar
REM  libs, compile only this app's units, link + stage shaders), plus:
REM    - /I the Jolt package root and link ExternalPackages\jolt\lib\jolt.lib
REM    - /DNDEBUG, which jolt.lib REQUIRES. It was archived with features 0, so
REM      JPH_ENABLE_ASSERTS is off; a consumer compiled without /DNDEBUG inlines
REM      assert calls the archive has no definition for and the link fails on
REM      JPH::AssertFailed. Do NOT "fix" that by defining JPH_ENABLE_ASSERTS.
REM    - no Suzanne .wsdoc staging: this app authors its own documents at run.
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=RigidBodyDropValidation"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "LIBDIR=%ROOT%\Build\lib"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "OUTPUT=%OUTDIR%\%NAME%.exe"
set "DEFINE=FRONTIER_RIGID_BODY_DROP_VALIDATION"
set "JOLTROOT=%ROOT%\ExternalPackages\jolt"

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

REM --- The Jolt archive must be present; it is prebuilt, not built from here ---
if not exist "%JOLTROOT%\lib\jolt.lib" (
    echo [%NAME%] ExternalPackages\jolt\lib\jolt.lib missing - aborting.
    goto :fail
)

REM --- Ensure the shared pillar libs exist + are current ----------------------
call "%ROOT%\Internal\Platform\Build.bat"
if errorlevel 1 goto :fail
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
if not exist "%LIBDIR%\Platform.lib" (
    echo [%NAME%] Platform.lib missing after build - aborting.
    goto :fail
)
if not exist "%LIBDIR%\AuthoringGeometry.lib" (
    echo [%NAME%] AuthoringGeometry.lib missing after build - aborting.
    goto :fail
)

REM --- Include roots -----------------------------------------------------------
set "IMGUI=%ROOT%\ExternalPackages\imgui"
set "VULKAN=%VULKAN_SDK%"
if not defined VULKAN set "VULKAN=C:\VulkanSDK\1.4.335.0"
set "MATHROOT=%ROOT%\Internal\EngineContext\Math"
set "GEOMROOT=%ROOT%\Internal\Authoring\Geometry\Modeling"
set "INCLUDES=/I"%ROOT%\Internal" /I"%IMGUI%" /I"%IMGUI%\backends" /I"%MATHROOT%" /I"%GEOMROOT%" /I"%JOLTROOT%" /I"%VULKAN%\Include""

REM  FRONTIER_POLYGON_AUTHORING is ABI-AFFECTING (it adds RenderExtension members),
REM  so it MUST match the Graphics pillar's DEFINES in Internal\Graphics\Build.bat.
REM  NDEBUG is the jolt.lib assert-config match described in the header above.
set "DEFINES=/DUNICODE /D_UNICODE /D%DEFINE% /DNDEBUG /DFRONTIER_DEVELOPMENT_PROFILE /DFRONTIER_POLYGON_AUTHORING"
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

REM --- Link the shared libs + Jolt + Vulkan / system libs ----------------------
set "LINKLIBS="%LIBDIR%\EngineContext.lib" "%LIBDIR%\Graphics.lib" "%LIBDIR%\AuthoringGeometry.lib" "%LIBDIR%\Platform.lib" "%JOLTROOT%\lib\jolt.lib""
set "SYSLIBS="%VULKAN%\Lib\vulkan-1.lib" user32.lib gdi32.lib shell32.lib dwmapi.lib"

echo [%NAME%] linking -^> %OUTPUT%
link /nologo /DEBUG /SUBSYSTEM:CONSOLE @"%OBJRSP%" %LINKLIBS% %SYSLIBS% /OUT:"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] LINK FAILED
    goto :fail
)

REM --- Compile GLSL -> SPIR-V before staging -----------------------------------
echo [%NAME%] compiling shaders
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\Automation\ShaderPlan.ps1" -Quiet
if errorlevel 1 (
    echo [%NAME%] SHADER COMPILE FAILED
    goto :fail
)

REM --- Stage SPIR-V shaders beside the exe -------------------------------------
set "SHADEROUT=%OUTDIR%\Shaders"
if not exist "%SHADEROUT%" mkdir "%SHADEROUT%"
copy /Y "%ROOT%\Internal\Graphics\Grid\Shaders\*.spv" "%SHADEROUT%" >nul
copy /Y "%ROOT%\Internal\Graphics\Atmosphere\Shaders\*.spv" "%SHADEROUT%" >nul
copy /Y "%ROOT%\Internal\Graphics\HierarchicalDepth\Shaders\*.spv" "%SHADEROUT%" >nul
copy /Y "%ROOT%\Internal\Graphics\Visibility\Shaders\*.spv" "%SHADEROUT%" >nul
copy /Y "%ROOT%\Internal\Graphics\Clipmap\Shaders\*.spv" "%SHADEROUT%" >nul
copy /Y "%ROOT%\Internal\Graphics\Render\Radiance\Shaders\*.spv" "%SHADEROUT%" >nul
copy /Y "%ROOT%\Internal\Graphics\Shadow\Shaders\*.spv" "%SHADEROUT%" >nul
copy /Y "%ROOT%\Internal\Graphics\Acceleration\Shaders\*.spv" "%SHADEROUT%" >nul
echo [%NAME%] staged shaders -^> %SHADEROUT%

REM --- The PRIVATE scene dir the app authors its documents into -----------------
REM  🔴 This app writes its scene under the filenames the ENGINE already loads -
REM  SuzanneRadial.wsdoc (the default SceneChoice) + CheckerFloor.wsdoc - because
REM  RenderExtension.cpp is compiled into Graphics.lib, so FRONTIER_SCENE_ASSET_DIR
REM  and those names are baked there and cannot be redefined from this target.
REM  Substituting the FILE is the whole mechanism, and it needs no engine change.
REM
REM  ⚠️ WHICH MEANS IT MUST NOT SHARE Binaries\Validation\Assets. That dir holds the
REM  REAL SuzanneRadial.wsdoc + CheckerFloor.wsdoc that RenderExtensionValidation.exe
REM  loads; authoring over them would silently replace the head rings with crates for
REM  that app too. The engine resolves "Assets" RELATIVE TO THE WORKING DIRECTORY, so
REM  this app gets its own subdir and a launcher that cd's into it. Nothing is copied
REM  here - the exe writes both documents itself on every launch.
set "SCENEDIR=%OUTDIR%\RigidBodyDropScene"
if not exist "%SCENEDIR%\Assets" mkdir "%SCENEDIR%\Assets"

REM  ⚠️ "Shaders" is a bare relative path in RenderExtension.cpp too, exactly like
REM  "Assets" - so moving the working directory moves BOTH lookups. The private dir
REM  therefore needs the staged shaders visible under it as well, or every pipeline
REM  fails to find its .spv and the renderer comes up with nothing drawable.
REM  A directory junction rather than a copy: the shaders are rebuilt above on every
REM  build and a copy would go stale silently. Recreated each build so it cannot dangle.
if exist "%SCENEDIR%\Shaders" rmdir "%SCENEDIR%\Shaders" >nul 2>nul
mklink /J "%SCENEDIR%\Shaders" "%SHADEROUT%" >nul
if errorlevel 1 (
    echo [%NAME%] could not junction Shaders into the private scene dir - falling back to a copy
    if not exist "%SCENEDIR%\Shaders" mkdir "%SCENEDIR%\Shaders"
    copy /Y "%SHADEROUT%\*.spv" "%SCENEDIR%\Shaders" >nul
)

REM  The launcher: cd into the private scene dir so BOTH "Assets" and "Shaders" resolve
REM  there, then run the exe out of its real location in Binaries\Validation.
> "%OUTDIR%\RunRigidBodyDropValidation.bat" echo @echo off
>>"%OUTDIR%\RunRigidBodyDropValidation.bat" echo cd /d "%%~dp0RigidBodyDropScene"
>>"%OUTDIR%\RunRigidBodyDropValidation.bat" echo "%%~dp0%NAME%.exe" %%*
echo [%NAME%] private scene dir -^> %SCENEDIR%

echo [%NAME%] OK -^> %OUTPUT%
endlocal & exit /b 0

:fail
echo [%NAME%] BUILD FAILED
endlocal & exit /b 1
