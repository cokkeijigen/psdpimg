@echo off
setlocal EnableDelayedExpansion

rem ---------------------------------------------------------------------------
rem  build.bat - Locate the Visual Studio environment and build image_crx.
rem
rem  Usage:  build.bat
rem  Option: build.bat <configurePreset>   (default: windows-msvc-x86-release)
rem ---------------------------------------------------------------------------

cd /d "%~dp0"

set "PRESET=%1"
if "%PRESET%"=="" set "PRESET=windows-msvc-x86-release"

rem --- Locate Visual Studio and its VsDevCmd.bat via vswhere ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

set "VSDEVCMD="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do if not defined VSDEVCMD if exist "%%i\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%i\Common7\Tools\VsDevCmd.bat"

if not defined VSDEVCMD (
    echo [ERROR] VsDevCmd.bat not found. Please install Visual Studio or Build Tools with the C++ toolset.
    exit /b 1
)

echo Using VsDevCmd: %VSDEVCMD%

rem --- Reuse an existing developer prompt, otherwise initialize the VS environment ---
where cl.exe >nul 2>nul
if errorlevel 1 (
    call "%VSDEVCMD%" -arch=x86 -host_arch=x64
    if errorlevel 1 (
        echo [ERROR] Failed to initialize the Visual Studio environment.
        exit /b 1
    )
)

echo ^> cmake --preset %PRESET%
cmake --preset %PRESET%
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

echo ^> cmake --build --preset %PRESET%
cmake --build --preset %PRESET%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo Build succeeded.
exit /b 0