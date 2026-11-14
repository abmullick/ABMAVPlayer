@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
cd /d "%ROOT%"

echo Preparing the pinned vcpkg toolchain and dependency cache...
set "ABM_VCPKG_ROOT="
for /f "usebackq delims=" %%V in (`powershell -NoProfile -ExecutionPolicy Bypass -File tools\ensure_vcpkg.ps1`) do set "ABM_VCPKG_ROOT=%%V"
if not defined ABM_VCPKG_ROOT (
  echo ERROR: Could not prepare the vcpkg toolchain.
  exit /b 1
)
if not exist "%ABM_VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo ERROR: vcpkg buildsystem files not found under "%ABM_VCPKG_ROOT%".
  exit /b 1
)

echo Ensuring manifest dependencies are installed, including libtorrent WebTorrent support...
"%ABM_VCPKG_ROOT%\vcpkg.exe" install --triplet x64-windows
if errorlevel 1 exit /b 1
if not exist "%ROOT%\vcpkg_installed\x64-windows\include\libtorrent\session.hpp" (
  echo ERROR: libtorrent headers were not installed.
  exit /b 1
)
where cmake >nul 2>nul
if errorlevel 1 (echo ERROR: CMake 3.21+ is required.& exit /b 1)
cmake -S src\torrent -B build\torrent-cmake -A x64 -DCMAKE_TOOLCHAIN_FILE="%ABM_VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_INSTALLED_DIR="%ROOT%\vcpkg_installed" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
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
for %%D in ("%ROOT%\vcpkg_installed\x64-windows\bin\*.dll") do copy /y "%%~fD" build\torrent-runtime\ >nul
echo Torrent engine built in build\torrent-runtime.
endlocal
