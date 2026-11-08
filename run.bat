@echo off
setlocal EnableExtensions
pushd "%~dp0"

echo.
echo ==========================================
echo           ABM AV Player launcher
echo ==========================================
echo [1/3] Checking VLC runtime...

if "%VLC_DIR%"=="" set "VLC_DIR=C:\Program Files\VideoLAN\VLC"

if not exist "%VLC_DIR%\libvlc.dll" (
  echo ERROR: VLC runtime not found at:
  echo        "%VLC_DIR%"
  echo Install 64-bit VLC or set VLC_DIR to its installation directory.
  goto launcher_failed
)

if not exist "%VLC_DIR%\plugins\." (
  echo ERROR: VLC plugins directory not found:
  echo        "%VLC_DIR%\plugins"
  echo Install the full 64-bit VLC package, not only the LibVLC SDK.
  goto launcher_failed
)

echo [2/3] Checking application executable...

set "APP_EXE=%~dp0build\ABMAVPlayer.exe"
if not exist "%APP_EXE%" (
  echo ERROR: "%APP_EXE%" does not exist.
  echo Run build.bat from the x64 Native Tools Command Prompt first.
  goto launcher_failed
)

echo [3/3] Starting ABM AV Player...
echo VLC runtime: "%VLC_DIR%"
echo Executable:  "%APP_EXE%"
echo.
set "PATH=%VLC_DIR%;%PATH%"
set "VLC_PLUGIN_PATH=%VLC_DIR%\plugins"
set "VLC_DATA_PATH=%VLC_DIR%"

rem Run in this command window so startup failures and the exit code are visible.
"%APP_EXE%"
set "APP_EXIT=%ERRORLEVEL%"

echo.
if "%APP_EXIT%"=="0" (
  echo ABM AV Player closed normally.
) else (
  echo ERROR: ABM AV Player exited with code %APP_EXIT%.
  echo If a LibVLC initialization dialog appeared, verify the VLC runtime.
)

popd
echo.
pause
endlocal
exit /b %APP_EXIT%

:launcher_failed
popd
echo.
pause
endlocal
exit /b 1
