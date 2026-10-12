@echo off
rem Compila o port (32 bits, x86) com o Visual Studio Build Tools 2026 (MSVC + CMake + Ninja).
rem Uso: build.bat [Release|Debug]
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat
if not exist "%VCVARS%" (
    echo Nao encontrei o vcvarsamd64_x86.bat do Build Tools 2026 em "%VCVARS%".
    exit /b 1
)
call "%VCVARS%" >nul || exit /b 1

cmake -S "%~dp0." -B "%~dp0build\%CONFIG%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% || exit /b 1
cmake --build "%~dp0build\%CONFIG%" || exit /b 1
echo.
echo Executavel: %~dp0build\%CONFIG%\nightfire.exe
