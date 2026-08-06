@echo off
REM ============================================================================
REM  TexturePaintWorkspaceValidation\Build.bat - build
REM  TexturePaintWorkspaceValidation.exe: a standalone Vulkan validation host
REM  for the COMBINED texture-paint workspace. Opens one native Vulkan window +
REM  ImGui interface, brings up the SvgIconRegistry against the host, registers
REM  the global icon pack plus the paint pack, starts the non-square strip
REM  store, and drives the three-column workspace every frame: the left layer
REM  rail, the centre paint field with the instrument card summoned over it on
REM  RIGHT-CLICK, and the right property column of channel + mask cards.
REM
REM  Every third is EMBEDDED, not re-implemented: the rail and channel panels
REM  are compiled in place from the sibling LayerStackValidation and
REM  ChannelPropertyValidation app folders, and the summoned card pulls
REM  PaintToolValidation's whole card in as TexturePaintValidation already
REM  does - so this build compiles four sibling folders IN PLACE, skipping
REM  each one's own *ValidationHost.cpp (they carry main()). The shared mask
REM  card ships in EngineContext.lib, which the pillar build below provides.
REM
REM  It draws NO renderer passes, so NO shaders are staged. It writes its OWN
REM  exe under Binaries\Validation and never touches the others.
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=TexturePaintWorkspaceValidation"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "LIBDIR=%ROOT%\Build\lib"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "OUTPUT=%OUTDIR%\%NAME%.exe"
set "DEFINE=FRONTIER_TEXTURE_PAINT_WORKSPACE_VALIDATION"

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
REM  EngineContext (theme + shared LayerProperties mask card + console + SvgIconRegistry
REM  + SvgRasterizer + vendored ImGui core).
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

REM --- Include roots (pillar-rooted headers + ImGui + thorvg + Vulkan; no GLFW) -
REM  thorvg's own include is needed HERE because the embedded PaintIconStore.cpp
REM  calls tvg:: directly for its rectangular raster.
REM  The embedded panels are compiled in place from the four sibling app folders,
REM  so their headers must all be on the include path.
set "IMGUI=%ROOT%\ExternalPackages\imgui"
set "THORVGINC=%ROOT%\ExternalPackages\thorvg\inc"
set "PTDIR=%APPDIR%\..\PaintToolValidation"
set "TPDIR=%APPDIR%\..\TexturePaintValidation"
set "LSVDIR=%APPDIR%\..\LayerStackValidation"
set "CPVDIR=%APPDIR%\..\ChannelPropertyValidation"
set "VULKAN=%VULKAN_SDK%"
if not defined VULKAN set "VULKAN=C:\VulkanSDK\1.4.335.0"
set "INCLUDES=/I"%ROOT%\Internal" /I"%IMGUI%" /I"%IMGUI%\backends" /I"%THORVGINC%" /I"%VULKAN%\Include" /I"%PTDIR%" /I"%TPDIR%" /I"%LSVDIR%" /I"%CPVDIR%""
REM  FRONTIER_DEVELOPMENT_PROFILE keeps Trace/Notice diagnostics AND turns on the
REM  Vulkan validation layer by default. Swap to FRONTIER_SHIPPING_PROFILE for lean builds.
REM  TVG_STATIC switches the vendored thorvg.h off its default __declspec(dllimport):
REM  the embedded PaintIconStore.cpp calls tvg:: directly and links the STATIC
REM  thorvg.lib, so without it every call goes through an import thunk and the
REM  linker emits LNK4217 per symbol.
set "DEFINES=/DUNICODE /D_UNICODE /D%DEFINE% /DFRONTIER_DEVELOPMENT_PROFILE /DTVG_STATIC"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Compile this app's own units (the workspace panel + the host) -----------
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

REM --- Compile the embedded card's units, in place from PaintToolValidation ----
REM  Every .cpp EXCEPT that folder's own PaintToolValidationHost.cpp (it carries
REM  main()). The card, its two-slide bridge, the options + preview columns, the
REM  schema, the generated catalogue table, the icon pack and the strip store all
REM  link into this exe.
for /R "%PTDIR%" %%F in (*.cpp) do (
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

REM --- Compile the summon card, in place from TexturePaintValidation -----------
REM  Only its host carries main(); TexturePaintSummonedCard.cpp links in.
for /R "%TPDIR%" %%F in (*.cpp) do (
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

REM --- Compile the layer rail, in place from LayerStackValidation --------------
REM  Its host carries main(); the record table + the rail panel link in.
for /R "%LSVDIR%" %%F in (*.cpp) do (
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

REM --- Compile the channel cards, in place from ChannelPropertyValidation ------
REM  Its host carries main(); the slot table + the channel panel link in.
for /R "%CPVDIR%" %%F in (*.cpp) do (
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
REM  thorvg.lib is the vendored static SVG rasterizer. Both EngineContext's
REM  SvgRasterizer.obj and the embedded PaintIconStore.obj call into it.
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
