@echo off
setlocal
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
    echo Install Node.js 22 or newer, then launch again.
    pause
    exit /b 1
)
if not exist node_modules\jolt-physics (
    call npm ci --ignore-scripts
    if errorlevel 1 exit /b 1
)
where py >nul 2>nul
if not errorlevel 1 (
    py -3 Tools\Serve.py
) else (
    python Tools\Serve.py
)
