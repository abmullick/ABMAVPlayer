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
  echo Run this from x64 Native Tools Command Prompt for VS 2026.
  pause
  exit /b 1
)

if not exist build mkdir build

echo [1/4] Compiling C playback engine...
cl /nologo /W4 /O2 /std:c11 /TC /DUNICODE /D_UNICODE /I"%VLC_SDK%\include" /c src\player.c /Fo:build\player.obj
if errorlevel 1 goto build_failed

echo [2/4] Compiling C++ Direct2D bridge...
cl /nologo /W4 /O2 /std:c++17 /EHsc /TP /DUNICODE /D_UNICODE /c src\ui_d2d.cpp /Fo:build\ui_d2d.obj
if errorlevel 1 goto build_failed

echo [3/4] Compiling WASAPI audio visualizer...
cl /nologo /W4 /O2 /std:c++17 /EHsc /TP /DUNICODE /D_UNICODE /c src\audio_visualizer.cpp /Fo:build\audio_visualizer.obj
if errorlevel 1 goto build_failed

echo [4/4] Linking ABM AV Player...
cl /nologo build\player.obj build\ui_d2d.obj build\audio_visualizer.obj /link /LIBPATH:"%VLC_SDK%\lib" libvlc.lib ^
  comctl32.lib comdlg32.lib shell32.lib uxtheme.lib user32.lib gdi32.lib d2d1.lib dwrite.lib dxguid.lib ole32.lib uuid.lib ^
  /SUBSYSTEM:WINDOWS /OUT:build\ABMAVPlayer.exe
if errorlevel 1 goto build_failed

echo.
echo Build successful: build\ABMAVPlayer.exe
pause
endlocal
exit /b 0

:build_failed
echo.
echo Build failed. See the compiler errors above.
pause
endlocal
exit /b 1
