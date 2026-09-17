@echo off
setlocal

REM One-command build + launch, for anyone who already has the prerequisites
REM from docs/BUILDING.md installed (MSVC Build Tools, CMake, Ninja, vcpkg,
REM Qt 6.8.3). Edit VCVARS64 below if Visual Studio Build Tools landed
REM somewhere other than the default choco install location, or set
REM CMAKE_PREFIX_PATH yourself before running this if Qt isn't at
REM C:\Qt\6.8.3\msvc2022_64.

set "VCVARS64=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined CMAKE_PREFIX_PATH set "CMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64"

if not exist "%VCVARS64%" (
    echo Could not find vcvars64.bat at:
    echo   %VCVARS64%
    echo.
    echo Edit VCVARS64 at the top of this script to point at your install,
    echo or see docs\BUILDING.md for the full prerequisite list.
    exit /b 1
)

call "%VCVARS64%" >nul
cd /d "%~dp0"

echo Configuring...
cmake --preset windows-msvc
if errorlevel 1 exit /b 1

echo Building (Debug)...
cmake --build --preset debug
if errorlevel 1 exit /b 1

echo.
echo Build OK. Launching NexusPC...
start "" "%~dp0build\windows-msvc\apps\desktop\Debug\NexusPC.exe"

endlocal
