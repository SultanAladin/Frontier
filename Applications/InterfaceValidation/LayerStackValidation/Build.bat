@echo off
REM ============================================================================
REM  LayerStackValidation\Build.bat - build layerstackvalidation.exe, a STANDALONE UI-only
REM  test window for the texture-paint Layer Stack panel (1:1 port of LayerStackR2.html).
REM
REM  Links ONLY Interface.lib + vendored ImGui (core + Win32/DX11 backends) + system D3D11.
REM  Outputs executable to Binaries\Validation\layerstackvalidation.exe.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "IMGUI=%ROOT%\ExternalPackages\imgui"
set "LIBDIR=%ROOT%\Build\lib"
set "OBJ=%ROOT%\Build\obj\LayerStackValidation"
set "IMGUIOBJ=%ROOT%\Build\obj\imgui"
set "OUTDIR=%ROOT%\Binaries\Validation"

if not exist "%OBJ%"     mkdir "%OBJ%"
if not exist "%IMGUIOBJ%" mkdir "%IMGUIOBJ%"
if not exist "%OUTDIR%"  mkdir "%OUTDIR%"

REM --- Activate MSVC if cl.exe is not already visible ------------------------------
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
        echo [LayerStackValidation] could not find vcvars64.bat - edit the VCVARS fallback.
        endlocal ^& exit /b 1
    )
    call "!VCVARS!" >nul
)

REM --- Ensure Interface.lib (and its deps) exist ----------------------------------
echo [LayerStackValidation] Building/updating Interface.lib...
call "%ROOT%\Internal\Interface\Build.bat" >nul
if not exist "%LIBDIR%\Interface.lib" (
    echo [LayerStackValidation] Interface.lib missing after build - aborting.
    endlocal ^& exit /b 1
)

REM --- Compiler configuration -----------------------------------------------------
set "INCLUDES=/I"%IMGUI%" /I"%IMGUI%\backends""
set "DEFINES=/DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Compile vendored ImGui core + backends once --------------------------------
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

REM --- Compile LayerStackValidationHost.cpp ----------------------------------------
echo [compile] LayerStackValidationHost.cpp
cl %CXXFLAGS% %DEFINES% %INCLUDES% "%APPDIR%\LayerStackValidationHost.cpp" /Fo"%OBJ%\LayerStackValidationHost.obj" /Fd"%OBJ%\LayerStackValidation.pdb"
if errorlevel 1 goto :fail

REM --- Link -----------------------------------------------------------------------
echo [LayerStackValidation] linking -^> %OUTDIR%\layerstackvalidation.exe
link /nologo /SUBSYSTEM:CONSOLE /DEBUG ^
    "%OBJ%\LayerStackValidationHost.obj" ^
    "%IMGUIOBJ%\imgui.obj" "%IMGUIOBJ%\imgui_draw.obj" "%IMGUIOBJ%\imgui_tables.obj" ^
    "%IMGUIOBJ%\imgui_widgets.obj" "%IMGUIOBJ%\imgui_impl_win32.obj" "%IMGUIOBJ%\imgui_impl_dx11.obj" ^
    "%LIBDIR%\Interface.lib" ^
    d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib ^
    /OUT:"%OUTDIR%\layerstackvalidation.exe"
if errorlevel 1 goto :fail

REM --- Stage UI config ------------------------------------------------------------
if exist "%ROOT%\EngineContent\Config" (
    robocopy "%ROOT%\EngineContent\Config" "%OUTDIR%\EngineContent\Config" InterfaceScale.config /NFL /NDL /NJH /NJS /NP >nul
)

echo [LayerStackValidation] OK -^> %OUTDIR%\layerstackvalidation.exe
endlocal & exit /b 0

:fail
echo [LayerStackValidation] BUILD FAILED
endlocal & exit /b 1
