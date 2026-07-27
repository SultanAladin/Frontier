@echo off
REM ============================================================================
REM  Extensions\Platform\Build.bat - compile this ONE extension to a static lib.
REM
REM  ADAPTIVE + SKIP: globs its own .cpp recursively (nothing hand-listed), then
REM  asks the shared planner (Automation\BuildPlan.ps1) which units are stale. A
REM  unit recompiles only when its .obj is missing, its .cpp changed, or a header
REM  it actually included (per cl /sourceDependencies) changed; the .lib is
REM  re-archived only when an object changed. An unchanged tree does nothing.
REM
REM  Platform owns the OS window + Vulkan surface (PlatformWindow, native per-OS
REM  backend). It compiles against the Vulkan SDK headers only (no GLFW). It links
REM  nothing itself; the application links vulkan-1.lib + the OS libs into the exe.
REM
REM  Requires an MSVC environment (cl.exe on PATH). The root Build.bat activates
REM  vcvars64 once; call vcvars64.bat first if you run this directly.
REM  Run this through the PowerShell tool, never Bash (it shells to BuildPlan.ps1).
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "EXTDIR=%~dp0"
if "%EXTDIR:~-1%"=="\" set "EXTDIR=%EXTDIR:~0,-1%"
for %%I in ("%EXTDIR%\..\..") do set "ROOT=%%~fI"

set "NAME=Platform"
set "LIBDIR=%ROOT%\Build\lib"
set "OBJ=%ROOT%\Build\obj\%NAME%"
set "OUTPUT=%LIBDIR%\%NAME%.lib"
set "PLANNER=%ROOT%\Automation\BuildPlan.ps1"

if not exist "%LIBDIR%" mkdir "%LIBDIR%"
if not exist "%OBJ%" mkdir "%OBJ%"

where cl >nul 2>nul
if errorlevel 1 (
    echo [%NAME%] cl.exe not on PATH - run vcvars64.bat first ^(or use the root Build.bat^).
    endlocal & exit /b 1
)

REM --- Include roots + compiler configuration ---------------------------------
set "VULKAN=%VULKAN_SDK%"
if not defined VULKAN set "VULKAN=C:\VulkanSDK\1.4.335.0"
set "INCLUDES=/I"%VULKAN%\Include" /I"%ROOT%\Internal""
set "DEFINES=/DUNICODE /D_UNICODE /DFRONTIER_PLATFORM"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Collect this extension's own sources (recursive, no manual list) --------
REM  Written one-per-line to a file the planner reads via SRC_LIST_FILE, NOT
REM  concatenated into OWN_SRCS: a long path list overflows cmd's 8191-char
REM  environment-variable cap, which silently truncates the tail units so they
REM  never compile. A file has no length cap.
set "SRC_LIST_FILE=%OBJ%\own_srcs.txt"
if exist "%SRC_LIST_FILE%" del /Q "%SRC_LIST_FILE%"
set "HAVE_SRCS="
for /R "%EXTDIR%" %%F in (*.cpp) do (
    echo %%F>>"%SRC_LIST_FILE%"
    set "HAVE_SRCS=1"
)
if not defined HAVE_SRCS (
    echo [%NAME%] no sources yet - skeleton only, nothing to compile.
    endlocal & exit /b 0
)

REM --- Ask the planner what is stale, compile only those ----------------------
set "VENDOR_SRCS="
set "HEADER_ROOTS=%EXTDIR%"
set "OBJRSP=%OBJ%\lib_objs.rsp"
if exist "%OBJRSP%" del /Q "%OBJRSP%"
set "LINK_NEEDED="
set "COMPILE_FAILED="

for /f "usebackq tokens=1,2,* delims=|" %%a in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%PLANNER%"`) do (
    if /I "%%a"=="LINK" (
        if /I "%%b"=="YES" set "LINK_NEEDED=1"
    ) else (
        call :Plan "%%a" "%%b" "%%c"
    )
)

if defined COMPILE_FAILED (
    echo [%NAME%] COMPILE FAILED
    endlocal & exit /b 1
)

if not defined LINK_NEEDED (
    echo [%NAME%] up to date -^> %OUTPUT%
    endlocal & exit /b 0
)

REM --- Archive (object list via @response file, no 8191-char cap) --------------
echo [%NAME%] archiving -^> %OUTPUT%
lib /nologo @"%OBJRSP%" /OUT:"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] ARCHIVE FAILED
    endlocal & exit /b 1
)
echo [%NAME%] OK -^> %OUTPUT%
endlocal & exit /b 0

REM ============================================================================
REM  :Plan  KIND  NAME  SRC-OR-REASON
REM ============================================================================
:Plan
set "KIND=%~1"
set "UNIT=%~2"
set "ARG=%~3"
set "OBJF=%OBJ%\%UNIT%.obj"
echo "%OBJF%">>"%OBJRSP%"

if /I "%KIND%"=="SKIP" (
    echo [SKIP] %UNIT% : %ARG%
    goto :eof
)

if defined COMPILE_FAILED goto :eof
echo [compile] %UNIT%
cl %CXXFLAGS% %DEFINES% %INCLUDES% "%ARG%" /Fo"%OBJF%" /Fd"%OBJ%\%NAME%.pdb" /sourceDependencies "%OBJ%\%UNIT%.json"
if errorlevel 1 (
    echo [compile failed] %UNIT%
    set "COMPILE_FAILED=1"
)
goto :eof
