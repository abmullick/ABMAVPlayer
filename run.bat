@echo off
setlocal

if "%VLC_DIR%"=="" set "VLC_DIR=C:\Program Files\VideoLAN\VLC"

if not exist "%VLC_DIR%\libvlc.dll" (
  echo VLC runtime not found at "%VLC_DIR%"
  echo Install 64-bit VLC or set VLC_DIR to its installation directory.
  pause
  exit /b 1
)

set "PATH=%VLC_DIR%;%PATH%"
set "VLC_PLUGIN_PATH=%VLC_DIR%\plugins"

if not exist build\ABMAVPlayer.exe (
  echo build\ABMAVPlayer.exe not found. Run build.bat first.
  pause
  exit /b 1
)

start "" build\ABMAVPlayer.exe
endlocal
