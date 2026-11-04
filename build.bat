@echo off
setlocal

if "%VLC_SDK%"=="" set "VLC_SDK=C:\vlc-sdk"

if not exist "%VLC_SDK%\include\vlc\vlc.h" (
  echo LibVLC headers not found at "%VLC_SDK%\include\vlc\vlc.h"
  echo Set VLC_SDK to the extracted LibVLC SDK directory.
  pause
  exit /b 1
)

if not exist "%VLC_SDK%\lib\libvlc.lib" (
  echo libvlc.lib not found in "%VLC_SDK%\lib"
  pause
  exit /b 1
)

where cl >nul 2>nul
if errorlevel 1 (
  echo Run this from x64 Native Tools Command Prompt for VS 2022.
  pause
  exit /b 1
)

if not exist build mkdir build

cl /nologo /W4 /O2 /std:c11 /DUNICODE /D_UNICODE src\player.c src\ui_d2d.c ^
  /I"%VLC_SDK%\include" ^
  /link /LIBPATH:"%VLC_SDK%\lib" libvlc.lib ^
  comctl32.lib comdlg32.lib uxtheme.lib user32.lib gdi32.lib d2d1.lib dwrite.lib dxguid.lib ^
  /SUBSYSTEM:WINDOWS /OUT:build\ABMAVPlayer.exe

if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)

echo Build successful: build\ABMAVPlayer.exe
pause
endlocal
