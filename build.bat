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

echo [0/12] Building native libtorrent engine...
call tools\build_torrent_engine.bat
if errorlevel 1 goto build_failed
copy /y build\torrent-runtime\*.dll build\ >nul
if errorlevel 1 goto build_failed

echo [1/12] Compiling C playback engine...
cl /nologo /W4 /O2 /utf-8 /std:c11 /TC /DUNICODE /D_UNICODE /I"%VLC_SDK%\include" /c src\player.c /Fo:build\player.obj
if errorlevel 1 goto build_failed

echo [2/12] Compiling C++ Direct2D bridge...
cl /nologo /W4 /O2 /utf-8 /std:c++17 /EHsc /TP /DUNICODE /D_UNICODE /c src\ui_d2d.cpp /Fo:build\ui_d2d.obj
if errorlevel 1 goto build_failed

echo [3/12] Compiling WASAPI audio visualizer...
cl /nologo /W4 /O2 /utf-8 /std:c++17 /EHsc /TP /DUNICODE /D_UNICODE /c src\audio_visualizer.cpp /Fo:build\audio_visualizer.obj
if errorlevel 1 goto build_failed

echo [4/12] Compiling WebView2 streaming host...
cl /nologo /W4 /O2 /utf-8 /std:c++17 /EHsc /TP /DUNICODE /D_UNICODE /I"%WEBVIEW2_SDK%\build\native\include" /c src\streaming\streaming_host.cpp /Fo:build\streaming_host.obj
if errorlevel 1 goto build_failed

echo [5/12] Compiling DLNA/UPnP casting backend...
cl /nologo /W4 /O2 /utf-8 /std:c++17 /EHsc /TP /DUNICODE /D_UNICODE /c src\streaming\dlna_cast.cpp /Fo:build\dlna_cast.obj
if errorlevel 1 goto build_failed

echo [6/12] Compiling YouTube DIAL casting backend...
cl /nologo /W4 /O2 /utf-8 /std:c++17 /EHsc /TP /DUNICODE /D_UNICODE /c src\streaming\youtube_dial.cpp /Fo:build\youtube_dial.obj
if errorlevel 1 goto build_failed

echo [7/12] Compiling Google Cast V2 backend...
cl /nologo /W4 /O2 /utf-8 /std:c++17 /EHsc /TP /DUNICODE /D_UNICODE /c src\streaming\cast_v2.cpp /Fo:build\cast_v2.obj
if errorlevel 1 goto build_failed

echo [8/12] Preparing GoCast YouTube sender toolchain...
set "GO_EXE="
for /f "usebackq delims=" %%G in (`powershell -NoProfile -ExecutionPolicy Bypass -File tools\ensure_go.ps1`) do set "GO_EXE=%%G"
if not defined GO_EXE (
  echo Could not prepare the verified Go toolchain.
  goto build_failed
)
echo Using Go toolchain: "%GO_EXE%"
"%GO_EXE%" -C tools\gocast build -trimpath -mod=mod -ldflags="-s -w" -o ..\..\build\abm-gocast.exe ./cmd/abm-gocast
if errorlevel 1 goto build_failed
if not exist build\abm-gocast.exe (
  echo GoCast helper executable was not created.
  goto build_failed
)

echo [9/12] Compiling GitHub Releases updater...
cl /nologo /W4 /O2 /utf-8 /std:c++17 /EHsc /TP /DUNICODE /D_UNICODE /c src\updater.cpp /Fo:build\updater.obj
if errorlevel 1 goto build_failed

echo [10/12] Compiling application icon resource...
where rc >nul 2>nul
if errorlevel 1 (
  echo Windows Resource Compiler rc.exe not found. Run from the x64 Native Tools Command Prompt.
  goto build_failed
)
rc /nologo /fo build\player.res src\player.rc
if errorlevel 1 goto build_failed

echo [11/12] Linking ABM AV Player...
cl /nologo build\player.obj build\ui_d2d.obj build\audio_visualizer.obj build\streaming_host.obj build\dlna_cast.obj build\youtube_dial.obj build\cast_v2.obj build\updater.obj build\player.res /link /LIBPATH:"%VLC_SDK%\lib" /LIBPATH:"%WEBVIEW2_SDK%\build\native\x64" libvlc.lib WebView2LoaderStatic.lib ^
  wininet.lib oleaut32.lib wintrust.lib crypt32.lib bcrypt.lib ole32.lib comctl32.lib comdlg32.lib shell32.lib shlwapi.lib advapi32.lib uxtheme.lib user32.lib gdi32.lib d2d1.lib dwrite.lib dxguid.lib ole32.lib uuid.lib windowscodecs.lib urlmon.lib winhttp.lib ws2_32.lib secur32.lib dnsapi.lib windowscodecs.lib version.lib ^
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
