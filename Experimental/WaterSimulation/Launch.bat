@echo off
rem Water simulation prototype launcher (Windows host interpreter for .bat tooling)
setlocal
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
    echo Node.js is required to serve the prototype over HTTP.
    echo Alternatively run: python -m http.server 8770 --bind 127.0.0.1 --directory .
    exit /b 1
)
start "" http://localhost:8770/
node Launch.mjs 8770 127.0.0.1
endlocal
