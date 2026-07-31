@echo off
REM ============================================================================
REM  PolygonActionValidation\Build.bat - build PolygonActionValidation.exe, a
REM  STANDALONE UI-only validation window for the polygon-mutation context menu
REM  ported from Documentation\Prototypes\EntityContextMenu.html.
REM
REM  Win32 + Direct3D 11 host. It links ONLY EngineContext.lib (the pillar that
REM  carries the Menus components + theme) + the vendored ImGui core it compiles
REM  itself (core + Win32/DX11 backends) + system D3D11. It does NOT touch
REM  Graphics / Platform / Vulkan - the menu's glyphs are ImDrawList vector paths
REM  precisely so no renderer pillar is needed. Validation hosts keep the proven
REM  D3D11 backend for quick eyeballing; the editors run on native Vulkan.
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=PolygonActionValidation"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "LIBDIR=%ROOT%\Build\lib"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "IMGUIOBJ=%ROOT%\Build\obj\imgui_dx11"
set "IMGUI=%ROOT%\ExternalPackages\imgui"
set "OUTPUT=%OUTDIR%\%NAME%.exe"

if not exist "%OUTDIR%"   mkdir "%OUTDIR%"
if not exist "%OBJ%"      mkdir "%OBJ%"
if not exist "%IMGUIOBJ%" mkdir "%IMGUIOBJ%"

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

REM --- Ensure the shared EngineContext pillar lib exists + is current ----------
REM  EngineContext carries Interface\Components\Menus (the menu + gate + glyphs).
call "%ROOT%\Internal\EngineContext\Build.bat"
if errorlevel 1 goto :fail
if not exist "%LIBDIR%\EngineContext.lib" (
    echo [%NAME%] EngineContext.lib missing after build - aborting.
    goto :fail
)

REM --- Compiler configuration (match EngineContext.lib: /MD /std:c++17 /EHsc) --
set "INCLUDES=/I"%ROOT%\Internal" /I"%IMGUI%" /I"%IMGUI%\backends""
set "DEFINES=/DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Compile vendored ImGui core + Win32/DX11 backends once (cached) ---------
REM  ImGui is vendored and effectively immutable, so a present object is up to
REM  date. Delete Build\obj\imgui_dx11 to force a rebuild. (Distinct dir from the
REM  pillar's Vulkan-backend imgui objects so the two backend sets never clash.)
set "IMGUI_UNITS=imgui imgui_draw imgui_tables imgui_widgets backends\imgui_impl_win32 backends\imgui_impl_dx11"
for %%U in (%IMGUI_UNITS%) do (
    for %%N in ("%%U") do set "BASE=%%~nN"
    set "OBJF=%IMGUIOBJ%\!BASE!.obj"
    if exist "!OBJF!" (
        echo [skip]    %%~nxU.cpp
    ) else (
        echo [compile] %%~nxU.cpp
        cl %CXXFLAGS% %DEFINES% %INCLUDES% "%IMGUI%\%%U.cpp" /Fo"!OBJF!" /Fd"%IMGUIOBJ%\imgui.pdb"
        if errorlevel 1 goto :fail
    )
)

REM --- Compile this app's own sources every time (they are the files under edit)
set "OBJRSP=%OBJ%\link_objs.rsp"
if exist "%OBJRSP%" del /Q "%OBJRSP%"
for %%U in (PolygonActionPanel PolygonActionValidationHost) do (
    echo [compile] %%U.cpp
    cl %CXXFLAGS% %DEFINES% %INCLUDES% "%APPDIR%\%%U.cpp" /Fo"%OBJ%\%%U.obj" /Fd"%OBJ%\%NAME%.pdb"
    if errorlevel 1 goto :fail
    echo "%OBJ%\%%U.obj">>"%OBJRSP%"
)

REM --- Link EngineContext.lib + vendored ImGui + system D3D11 -----------------
echo [%NAME%] linking -^> %OUTPUT%
link /nologo /SUBSYSTEM:CONSOLE /DEBUG @"%OBJRSP%" ^
    "%IMGUIOBJ%\imgui.obj" "%IMGUIOBJ%\imgui_draw.obj" "%IMGUIOBJ%\imgui_tables.obj" ^
    "%IMGUIOBJ%\imgui_widgets.obj" "%IMGUIOBJ%\imgui_impl_win32.obj" "%IMGUIOBJ%\imgui_impl_dx11.obj" ^
    "%LIBDIR%\EngineContext.lib" ^
    d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib ^
    /OUT:"%OUTPUT%"
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
