@echo off
rem Frontier Editor (Experimental) - launcher
rem Prefers a packaged FrontierEditor.exe beside this script, then the repo build tree.
setlocal
cd /d "%~dp0"

if exist "FrontierEditor.exe" (
    start "" "FrontierEditor.exe"
    exit /b 0
)

if exist "..\..\build\FrontierEditor\FrontierEditor.exe" (
    start "" "..\..\build\FrontierEditor\FrontierEditor.exe"
    exit /b 0
)

echo FrontierEditor.exe not found.
echo Build it first:  cmake --build build --target FrontierEditor
echo or download the WindowsWorkspaces package from GitHub Actions.
pause
exit /b 1
