@echo off
rem Compila o port com o Visual Studio Build Tools 2026 (MSVC + CMake + Ninja).
rem Uso: build.bat [Release|Debug] [x64]
rem   sem o segundo argumento: 32 bits (x86) em build\<config>, a versao de referencia;
rem   com x64: 64 bits em build\x64\<config> (docs/port.md, "64 bits").
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release
set ARCH=%2

if /i "%ARCH%"=="x64" (
    set VCVARS=C:\Program Files ^(x86^)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat
    set BUILD_DIR=%~dp0build\x64\%CONFIG%
    set CMAKE_ARCH=-DNF_ARCH64=ON
) else (
    set VCVARS=C:\Program Files ^(x86^)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat
    set BUILD_DIR=%~dp0build\%CONFIG%
    set CMAKE_ARCH=
)
if not exist "%VCVARS%" (
    echo Nao encontrei o "%VCVARS%" do Build Tools 2026.
    exit /b 1
)
call "%VCVARS%" >nul || exit /b 1
rem Os temporarios do compilador e do linker numa pasta do build: o PDB guarda o caminho deles, e o %TEMP% do Windows
rem traz o nome do usuario (o zip de lancamento leva os PDB; tools/make_release.py confere).
if not exist "%BUILD_DIR%\tmp" mkdir "%BUILD_DIR%\tmp"
set "TMP=%BUILD_DIR%\tmp"
set "TEMP=%BUILD_DIR%\tmp"

cmake -S "%~dp0." -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% %CMAKE_ARCH% || exit /b 1
cmake --build "%BUILD_DIR%" || exit /b 1
echo.
echo Executavel: %BUILD_DIR%\nightfire.exe
