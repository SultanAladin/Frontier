@echo off
REM ============================================================================
REM  AtmosphereCelestialValidation\Build.bat - build AtmosphereCelestialValidation.exe:
REM  the CPU reference harness for Internal\Graphics\Atmosphere. It enforces the
REM  physical gates (transmittance, Rayleigh colour ratios, solar limb darkening,
REM  Pogson stellar flux, RK4 flow closure, divergence-free curl, lunar limbs,
REM  cloud energy bounds, fog Beer-Lambert / height closed form / flow advection,
REM  solar glare, UBO layouts) and renders the 18-exhibit PNG gallery.
REM
REM  The harness is pure C++17 (no Vulkan, no window, no ImGui), so it does NOT
REM  link the pillar libs: it compiles its entry unit together with the five
REM  Atmosphere solver units directly. CelestialFieldSolver.cpp decodes the lunar
REM  albedo maps through a STB_IMAGE_STATIC instance, so only the stb include
REM  root is needed beside Internal.
REM
REM  Usage after the build (run from the repo root so the gallery lands there):
REM      Binaries\Validation\AtmosphereCelestialValidation.exe ["Audit Proof"] [filter]
REM  argv[1] = output directory (default "Audit Proof"), argv[2] = exhibit filename
REM  filter ("none" = gates only, "05_" = one exhibit). The lunar maps resolve from
REM  Documentation\Assets\CelestialMaps by walking up from the working directory.
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=AtmosphereCelestialValidation"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "OUTPUT=%OUTDIR%\%NAME%.exe"
set "DEFINE=FRONTIER_ATMOSPHERE_CELESTIAL_VALIDATION"

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

REM --- Include roots (pillar-rooted headers + stb) -----------------------------
set "STB=%ROOT%\ExternalPackages\stb"
if not exist "%STB%\stb_image.h" (
    echo [%NAME%] stb_image.h missing under %STB% - aborting.
    goto :fail
)
set "INCLUDES=/I"%ROOT%\Internal" /I"%STB%""
set "DEFINES=/DUNICODE /D_UNICODE /D%DEFINE% /DFRONTIER_DEVELOPMENT_PROFILE"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Compile the entry unit + the Atmosphere solver units --------------------
set "ATMOSPHERE=%ROOT%\Internal\Graphics\Atmosphere"
set "OBJRSP=%OBJ%\link_objs.rsp"
if exist "%OBJRSP%" del /Q "%OBJRSP%"
for %%F in (
    "%APPDIR%\AtmosphereCelestialValidationEntry.cpp"
    "%ATMOSPHERE%\AtmosphericFlowField.cpp"
    "%ATMOSPHERE%\AtmosphericScatteringSolver.cpp"
    "%ATMOSPHERE%\CelestialFieldSolver.cpp"
    "%ATMOSPHERE%\VolumetricCloudSolver.cpp"
    "%ATMOSPHERE%\VolumetricFogSolver.cpp"
) do (
    set "UNIT=%%~nF"
    echo [compile] !UNIT!
    cl %CXXFLAGS% %DEFINES% %INCLUDES% "%%~F" /Fo"%OBJ%\!UNIT!.obj" /Fd"%OBJ%\%NAME%.pdb"
    if errorlevel 1 (
        echo [%NAME%] COMPILE FAILED
        goto :fail
    )
    echo "%OBJ%\!UNIT!.obj">>"%OBJRSP%"
)

REM --- Link (CRT only; std::thread needs no extra libs on MSVC) ----------------
echo [%NAME%] linking -^> %OUTPUT%
link /nologo /DEBUG /SUBSYSTEM:CONSOLE @"%OBJRSP%" /OUT:"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] LINK FAILED
    goto :fail
)

echo [%NAME%] OK -^> %OUTPUT%
endlocal & exit /b 0

:fail
echo [%NAME%] BUILD FAILED
endlocal & exit /b 1
