@echo off
REM ============================================================================
REM  WorkspaceDocumentWriter\Build.bat - build WorkspaceDocumentWriter.exe:
REM  a one-shot authoring tool that bakes the two hardcoded Suzanne scenes into
REM  loadable .wsdoc WorkspaceDocument files, then RUNS it so the .wsdoc land in
REM  the docs Assets dir beside their source JSON.
REM
REM  Self-contained: it compiles ONLY the three sources it needs directly, and
REM  links no pillar lib.
REM    - WorkspaceDocumentSerializer.cpp (entry: JSON scanner + TRS decompose)
REM    - WorkspaceDocumentEncoder.cpp     (the TOML encoder; needs tomlpp)
REM    - SuzanneScene.cpp                 (BuildSuzanneScene - the hardcoded layout)
REM  WorkspaceDocumentEncoder.cpp is not yet folded into any pillar lib and Build
REM  SuzanneScene lives in Graphics.lib, so pulling both .cpp in directly keeps the tool
REM  free of a Vulkan/link dependency (it touches no GPU).
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=WorkspaceDocumentWriter"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "OUTPUT=%OUTDIR%\%NAME%.exe"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OBJ%" mkdir "%OBJ%"

REM --- Activate MSVC if cl.exe is not already visible --------------------------
where cl >nul 2>nul
if errorlevel 1 (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    if not exist "!VCVARS!" (
        echo [%NAME%] could not find vcvars64.bat - edit the VCVARS fallback.
        endlocal ^& exit /b 1
    )
    call "!VCVARS!" >nul
)

REM --- Include roots ----------------------------------------------------------
REM  Internal          : pillar-rooted headers (Authoring/Geometry/Interchange, Graphics/Scene)
REM  MATHROOT          : LinearAlgebra_Float64.h (bare-included by PolygonCluster/VertexField)
REM  GEOMROOT          : PolygonCluster.h / VertexField.h / PolygonDescriptor.h (bare names)
REM  TOMLROOT          : the vendored header-only toml++ (toml.hpp)
set "MATHROOT=%ROOT%\Internal\EngineContext\Math"
set "GEOMROOT=%ROOT%\Internal\Authoring\Geometry\Modeling"
set "TOMLROOT=%ROOT%\ExternalPackages\tomlpp"
set "INCLUDES=/I"%ROOT%\Internal" /I"%MATHROOT%" /I"%GEOMROOT%" /I"%TOMLROOT%""
REM  toml++ compiled no-throw (parse errors arrive in the parse_result, never thrown).
set "DEFINES=/DTOML_EXCEPTIONS=0"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- The three sources this tool compiles directly --------------------------
set "SRC_ENTRY=%APPDIR%\WorkspaceDocumentSerializer.cpp"
set "SRC_FORMAT=%ROOT%\Internal\Authoring\Geometry\Interchange\WorkspaceDocumentEncoder.cpp"
set "SRC_SCENE=%ROOT%\Internal\Graphics\Scene\SuzanneScene.cpp"

set "OBJRSP=%OBJ%\link_objs.rsp"
if exist "%OBJRSP%" del /Q "%OBJRSP%"
for %%F in ("%SRC_ENTRY%" "%SRC_FORMAT%" "%SRC_SCENE%") do (
    set "UNIT=%%~nF"
    echo [compile] !UNIT!
    cl %CXXFLAGS% %DEFINES% %INCLUDES% "%%~F" /Fo"%OBJ%\!UNIT!.obj" /Fd"%OBJ%\%NAME%.pdb"
    if errorlevel 1 (
        echo [%NAME%] COMPILE FAILED
        goto :fail
    )
    echo "%OBJ%\!UNIT!.obj">>"%OBJRSP%"
)

echo [%NAME%] linking -^> %OUTPUT%
link /nologo /DEBUG /SUBSYSTEM:CONSOLE @"%OBJRSP%" /OUT:"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] LINK FAILED
    goto :fail
)
echo [%NAME%] OK -^> %OUTPUT%

REM --- Run it: bake the two .wsdoc into the docs Assets dir -------------------
REM  The source JSON + the baked .wsdoc live together in the docs repo's Assets
REM  dir; the renderer's own Build.bat then stages the .wsdoc beside the exe.
set "ASSETDIR=C:\Users\OS\Documents\Frontier\Documentation\Assets"
echo [%NAME%] baking scenes -^> %ASSETDIR%
"%OUTPUT%" "%ASSETDIR%"
if errorlevel 1 (
    echo [%NAME%] BAKE FAILED
    goto :fail
)

echo [%NAME%] DONE
endlocal & exit /b 0

:fail
echo [%NAME%] BUILD FAILED
endlocal & exit /b 1
