@echo off
REM ==============================================================================================================================================
REM                                      AtmosphereCelestialValidation — Adaptive Build Script (CPU-only)
REM ==============================================================================================================================================
REM Builds Frontier::Atmosphere + CelestialSphere + VolumetricCloud + Simulation::ForceField physical validation & AuditProof renderer.

setlocal EnableDelayedExpansion

for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"

if not defined VSCMD_VER (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" ( echo [ERROR] vswhere.exe not found. & exit /b 1 )
    for /f "usebackq delims=" %%P in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%P"
    if not defined VSPATH ( echo [ERROR] MSVC Build Tools not installed. & exit /b 1 )
    call "!VSPATH!\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
    if errorlevel 1 ( echo [ERROR] vcvarsall.bat failed. & exit /b 1 )
)

REM Pillar 2 force-field solver (globbed by Internal\Simulation\Build.bat into Build\lib\Simulation.lib).
call "%ROOT%\Internal\Simulation\Build.bat"
if errorlevel 1 exit /b 1

set "OBJDIR=%ROOT%\Build\Validation\AtmosphereCelestialValidation"
set "BINDIR=%ROOT%\Binaries\Validation"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"
if not exist "%BINDIR%" mkdir "%BINDIR%"

echo [AtmosphereCelestialValidation] Compiling...
cl /nologo /c /std:c++17 /W4 /WX /EHsc /O2 /MD /utf-8 /DUNICODE /D_UNICODE /DNDEBUG ^
   /I"%ROOT%\Internal" ^
   "%~dp0AtmosphereCelestialValidation.cpp" ^
   "%ROOT%\Internal\Graphics\Atmosphere\RadiometricSkyEvaluation.cpp" ^
   "%ROOT%\Internal\Graphics\Atmosphere\CelestialSphereEvaluation.cpp" ^
   "%ROOT%\Internal\Graphics\Atmosphere\VolumetricCloudEvaluation.cpp" ^
   /Fo"%OBJDIR%\\"
if errorlevel 1 ( echo [AtmosphereCelestialValidation] FAILED compile & exit /b 1 )

link /nologo /OUT:"%BINDIR%\AtmosphereCelestialValidation.exe" ^
     "%OBJDIR%\AtmosphereCelestialValidation.obj" ^
     "%OBJDIR%\RadiometricSkyEvaluation.obj" ^
     "%OBJDIR%\CelestialSphereEvaluation.obj" ^
     "%OBJDIR%\VolumetricCloudEvaluation.obj" ^
     "%ROOT%\Build\lib\Simulation.lib"
if errorlevel 1 ( echo [AtmosphereCelestialValidation] FAILED link & exit /b 1 )

echo [AtmosphereCelestialValidation] OK -^> %BINDIR%\AtmosphereCelestialValidation.exe
exit /b 0
