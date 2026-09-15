@echo off
REM build-msvc.bat -- compile ra2core with the local MSVC toolchain.
REM
REM Usage:  tools\build-msvc.bat
REM
REM Why not cmake: the VS2026 cmake generator name varies by install, while
REM invoking cl.exe directly always works. If your cmake knows the generator:
REM   cmake -B build -S . && cmake --build build --config Release

setlocal
set VSROOT=C:\Program Files\Microsoft Visual Studio\18\Enterprise
set VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat

if not exist "%VCVARS%" (
    echo [!] not found: %VCVARS%
    echo     edit VSROOT in this script.
    exit /b 1
)

call "%VCVARS%" x64
if errorlevel 1 exit /b 1

if not exist build mkdir build

cl /nologo /std:c++17 /EHsc /W3 /O2 /Isrc /Fo build\ /Fe build\ra2core.exe src\main.cpp src\map\Map.cpp src\ai\PathFinder.cpp src\engine\FrameQueue.cpp src\engine\GameLoop.cpp src\threading\TaskSystem.cpp

if errorlevel 1 (
    echo [x] build failed
    exit /b 1
)
echo [OK] build\ra2core.exe
