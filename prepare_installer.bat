@echo off
setlocal EnableExtensions

rem Run this script from the repository root after a successful x64 build.
if "%VLC_DIR%"=="" set "VLC_DIR=C:\Program Files\VideoLAN\VLC"
set "DEST=release\ABM AV Player"

if not exist "build\ABMAVPlayer.exe" (
  echo ERROR: build\ABMAVPlayer.exe not found. Run build.bat first.
  exit /b 1
)
if not exist "%VLC_DIR%\libvlc.dll" (
  echo ERROR: libvlc.dll not found under "%VLC_DIR%".
  echo Set VLC_DIR to the folder containing the 64-bit VLC runtime.
  exit /b 1
)
if not exist "%VLC_DIR%\libvlccore.dll" (
  echo ERROR: libvlccore.dll not found under "%VLC_DIR%".
  exit /b 1
)
if not exist "%VLC_DIR%\plugins" (
  echo ERROR: VLC plugins directory not found under "%VLC_DIR%".
  exit /b 1
)

echo Preparing clean staging folder...
if exist "%DEST%" rmdir /s /q "%DEST%"
mkdir "%DEST%" || exit /b 1
copy /y "build\ABMAVPlayer.exe" "%DEST%\ABMAVPlayer.exe" >nul || exit /b 1

rem Optional owner portrait used by the About the Creator window.
if exist "assets\owner_portrait.bmp" (
  mkdir "%DEST%\assets" 2>nul
  copy /y "assets\owner_portrait.bmp" "%DEST%\assets\owner_portrait.bmp" >nul || exit /b 1
) else (
  echo NOTE: assets\owner_portrait.bmp not found; About window will show its portrait placeholder.
)

rem LibVLC and VLC plugin dependencies. Copying the DLL set from the same
rem 64-bit VLC installation keeps the runtime dependency versions aligned.
copy /y "%VLC_DIR%\*.dll" "%DEST%\" >nul
if errorlevel 1 (
  echo ERROR: Could not copy VLC DLL files.
  exit /b 1
)
xcopy "%VLC_DIR%\plugins" "%DEST%\plugins\" /E /I /H /Y >nul
if errorlevel 2 exit /b 1

rem Google Cast requires both the mDNS renderer-discovery module and the
rem Chromecast demux module. Fail the release staging step instead of silently
rem shipping an installer that can never discover or play to Cast receivers.
if not exist "%DEST%\plugins\services_discovery\libmicrodns_plugin.dll" (
  echo ERROR: Google Cast discovery plugin is missing:
  echo   %VLC_DIR%\plugins\services_discovery\libmicrodns_plugin.dll
  echo Use a 64-bit VLC runtime that includes the microdns services-discovery plugin.
  exit /b 1
)
if not exist "%DEST%\plugins\demux\libdemux_chromecast_plugin.dll" (
  echo ERROR: Chromecast playback plugin is missing:
  echo   %VLC_DIR%\plugins\demux\libdemux_chromecast_plugin.dll
  echo Use a 64-bit VLC runtime that includes the Chromecast demux plugin.
  exit /b 1
)

rem VLC data folders are optional in some builds; copy them when present.
for %%D in (lua hrtfs locale) do (
  if exist "%VLC_DIR%\%%D" (
    xcopy "%VLC_DIR%\%%D" "%DEST%\%%D\" /E /I /H /Y >nul
    if errorlevel 2 exit /b 1
  )
)

copy /y "LICENSE" "%DEST%\LICENSE" >nul
copy /y "COPYRIGHT.md" "%DEST%\COPYRIGHT.md" >nul
if exist "%VLC_DIR%\COPYING.txt" copy /y "%VLC_DIR%\COPYING.txt" "%DEST%\VLC-COPYING.txt" >nul
if exist "%VLC_DIR%\AUTHORS.txt" copy /y "%VLC_DIR%\AUTHORS.txt" "%DEST%\VLC-AUTHORS.txt" >nul
if exist "%VLC_DIR%\README.txt" copy /y "%VLC_DIR%\README.txt" "%DEST%\VLC-README.txt" >nul

(
echo ABM AV Player bundles the VLC/LibVLC runtime and its plugins.
echo VLC, LibVLC, codecs and individual plugins are third-party components.
echo Their copyright and licence terms remain applicable.
echo.
echo IMPORTANT: Audit the licences of the exact VLC build and every bundled
echo plugin before distributing this package to another person. VLC plugin
echo directories can contain modules under different licences. Include any
echo corresponding source-code offers, notices or relinking materials required
echo by the applicable licences. The presence of VLC-COPYING.txt alone may
echo not satisfy every bundled component's obligations.
) > "%DEST%\THIRD_PARTY_NOTICES.txt"

echo.
echo Staging completed: "%DEST%"
echo Next: open installer\ABMAVPlayer.iss in Inno Setup Compiler.
echo IMPORTANT: Run the staged EXE and test playback/casting before distribution.
endlocal
