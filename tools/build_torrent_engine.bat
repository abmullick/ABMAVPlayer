@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
cd /d "%ROOT%"
if "%VCPKG_ROOT%"=="" (
  echo ERROR: Set VCPKG_ROOT to your vcpkg installation.
  exit /b 1
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" exit /b 1
if not exist "%VCPKG_ROOT%\installed\x64-windows\include\libtorrent\session.hpp" (
  echo Run: "%VCPKG_ROOT%\vcpkg.exe" install libtorrent:x64-windows
  exit /b 1
)
where cmake >nul 2>nul
if errorlevel 1 (echo ERROR: CMake 3.21+ is required.& exit /b 1)
cmake -S src\torrent -B build\torrent-cmake -A x64 -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
if errorlevel 1 exit /b 1
cmake --build build\torrent-cmake --config Release
if errorlevel 1 exit /b 1
if not exist build\torrent-runtime mkdir build\torrent-runtime
if exist build\torrent-cmake\bin\Release\abm_torrent_engine.dll (
  copy /y build\torrent-cmake\bin\Release\abm_torrent_engine.dll build\torrent-runtime\ >nul
) else (
  copy /y build\torrent-cmake\bin\abm_torrent_engine.dll build\torrent-runtime\ >nul
)
if errorlevel 1 exit /b 1
for %%D in ("%VCPKG_ROOT%\installed\x64-windows\bin\*.dll") do copy /y "%%~fD" build\torrent-runtime\ >nul
echo Torrent engine built in build\torrent-runtime.
endlocal
