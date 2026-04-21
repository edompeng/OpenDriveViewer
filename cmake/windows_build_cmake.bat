
echo  %~dp0

cd /d "%~dp0.."
if not exist "build" (
    mkdir "build"
)

@REM call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"

@REM set VCPKG_ROOT1=E:\code\vcpkg-2024.04.26
set VCPKG=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake -B build . -DQt6_DIR=C:\Qt\6.9.1\msvc2022_64\lib\cmake\Qt6 -D_VCPKG_ROOT="%VCPKG_ROOT%" -DCMAKE_TOOLCHAIN_FILE="%VCPKG%" -Wno-dev -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release