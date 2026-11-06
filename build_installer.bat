@echo off
setlocal
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
  echo ERROR: Inno Setup 6 compiler (ISCC.exe) was not found.
  echo Install Inno Setup 6, then run this script again.
  exit /b 1
)
if not exist "release\ABM AV Player\ABMAVPlayer.exe" (
  echo ERROR: Release staging folder is missing.
  echo Run prepare_installer.bat first.
  exit /b 1
)
if not exist "release\ABM AV Player\LICENSE" (
  echo ERROR: Staged LICENSE file is missing.
  echo Run prepare_installer.bat again.
  exit /b 1
)
"%ISCC%" /Qp "installer\ABMAVPlayer.iss"
if errorlevel 1 (
  echo Installer compilation failed.
  exit /b 1
)
echo.
echo Installer created under dist\ABM-AV-Player-Setup.exe
endlocal
