@echo off
setlocal

if "%VLC_SDK%"=="" set "VLC_SDK=C:\vlc-sdk"
if "%WEBVIEW2_SDK%"=="" set "WEBVIEW2_SDK=%CD%\packages\Microsoft.Web.WebView2"

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

if not exist "%WEBVIEW2_SDK%\build\native\include\WebView2.h" (
  echo WebView2 SDK headers not found at "%WEBVIEW2_SDK%\build\native\include\WebView2.h"
  echo Install Microsoft.Web.WebView2 with NuGet and set WEBVIEW2_SDK to its package folder.
  pause
  exit /b 1
)
if not exist "%WEBVIEW2_SDK%\build\native\x64\WebView2LoaderStatic.lib" (
  echo WebView2LoaderStatic.lib not found under "%WEBVIEW2_SDK%\build\native\x64"
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

echo [1/7] Compiling C playback engine...
cl /nologo /W4 /O2 /utf-8 /std:c11 /TC /DUNICODE /D_UNICODE /I"%VLC_SDK%\include" /c src\player.c /Fo:build\player.obj
if errorlevel 1 goto build_failed

echo [2/7] Compiling C++ Direct2D bridge...
cl /nologo /W4 /O2 /utf-8 /std:c++17 /EHsc /TP /DUNICODE /D_UNICODE /c src\ui_d2d.cpp /Fo:build\ui_d2d.obj
if errorlevel 1 goto build_failed

echo [3/7] Compiling WASAPI audio visualizer...
cl /nologo /W4 /O2 /utf-8 /std:c++17 /EHsc /TP /DUNICODE /D_UNICODE /c src\audio_visualizer.cpp /Fo:build\audio_visualizer.obj
if errorlevel 1 goto build_failed

echo [4/7] Compiling WebView2 streaming host...
cl /nologo /W4 /O2 /utf-8 /std:c++17 /EHsc /TP /DUNICODE /D_UNICODE /I"%WEBVIEW2_SDK%\build\native\include" /c src\streaming\streaming_host.cpp /Fo:build\streaming_host.obj
if errorlevel 1 goto build_failed

echo [5/7] Compiling GitHub Releases updater...
cl /nologo /W4 /O2 /utf-8 /std:c++17 /EHsc /TP /DUNICODE /D_UNICODE /c src\updater.cpp /Fo:build\updater.obj
if errorlevel 1 goto build_failed

echo [6/7] Compiling application icon resource...
where rc >nul 2>nul
if errorlevel 1 (
  echo Windows Resource Compiler rc.exe not found. Run from the x64 Native Tools Command Prompt.
  goto build_failed
)
rc /nologo /fo build\player.res src\player.rc
if errorlevel 1 goto build_failed

echo [7/7] Linking ABM AV Player...
cl /nologo build\player.obj build\ui_d2d.obj build\audio_visualizer.obj build\streaming_host.obj build\updater.obj build\player.res /link /LIBPATH:"%VLC_SDK%\lib" /LIBPATH:"%WEBVIEW2_SDK%\build\native\x64" libvlc.lib WebView2LoaderStatic.lib ^
  wininet.lib comctl32.lib comdlg32.lib shell32.lib shlwapi.lib advapi32.lib uxtheme.lib user32.lib gdi32.lib d2d1.lib dwrite.lib dxguid.lib ole32.lib uuid.lib windowscodecs.lib urlmon.lib version.lib ^
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
