@echo off
REM ======================================================================
REM   LaunchFluidNodeEditor.bat - serve the fluid prototype and open it
REM ======================================================================
REM
REM   The editor is split into ES modules, which the browser fetches under
REM   CORS rules. A file:// page is an opaque origin, so every import is
REM   blocked and the page renders empty having run no code at all.
REM   It MUST be served over http.
REM
REM   The viewport additionally needs a WebGPU adapter. Without one the
REM   graph still works and the viewport reveals a notice saying why.
REM

setlocal
set PORT=8779
set PAGE=FluidNodeEditor.html

REM -- serve from this folder; the prototype is fully self-contained --
pushd "%~dp0"

where python >nul 2>nul
if errorlevel 1 (
    echo python not found on PATH - cannot serve the prototype.
    popd
    exit /b 1
)

echo Serving %CD% on http://127.0.0.1:%PORT%
echo Opening %PAGE%
echo.
echo Leave this window open while the prototype is in use.
echo Close it or press Ctrl+C to stop the server.
echo.

REM -- open the browser first; the server call below blocks --
start "" "http://127.0.0.1:%PORT%/%PAGE%"

python -m http.server %PORT% --bind 127.0.0.1

popd
endlocal
