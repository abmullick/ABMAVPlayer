@echo off
setlocal EnableExtensions

rem Check the standard 64-bit and 32-bit Inno Setup locations.
set "ISCC=%ProgramFiles%\Inno Setup 7\ISCC.exe"
if exist "%ISCC%" goto found_compiler

set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto found_compiler

set "ISCC=%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe"
if exist "%ISCC%" goto found_compiler

set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto found_compiler

goto missing_compiler

:found_compiler
if not exist "release\ABM AV Player\ABMAVPlayer.exe" goto missing_stage
if not exist "release\ABM AV Player\LICENSE" goto missing_license

echo Compiling installer with:
echo "%ISCC%"
"%ISCC%" /Qp "installer\ABMAVPlayer.iss"
if errorlevel 1 goto compile_failed

echo.
echo Installer created under dist\ABM-AV-Player-Setup.exe
endlocal
exit /b 0

:missing_compiler
echo ERROR: Inno Setup 6 compiler (ISCC.exe) was not found.
echo Install Inno Setup 6, then run this script again.
endlocal
exit /b 1

:missing_stage
echo ERROR: Release staging folder is missing.
echo Run prepare_installer.bat first.
endlocal
exit /b 1

:missing_license
echo ERROR: Staged LICENSE file is missing.
echo Run prepare_installer.bat again.
endlocal
exit /b 1

:compile_failed
echo Installer compilation failed. Review the Inno Setup compiler output.
endlocal
exit /b 1
