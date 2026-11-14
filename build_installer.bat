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
rem Build the application from current source before staging anything.
rem This prevents an old build\ABMAVPlayer.exe from being silently packaged.
echo Building ABM AV Player from current source...
call build.bat
if errorlevel 1 goto app_build_failed

rem Always recreate the release staging folder from that fresh build.
rem Never package an existing staging folder: it may contain an older EXE.
if not exist "build\ABMAVPlayer.exe" goto missing_build

rem Verify that the freshly compiled EXE contains the requested release version.
rem This catches stale object files or an out-of-date source checkout before packaging.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p='build\ABMAVPlayer.exe'; $s=[Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($p)); if ($s -notmatch 'Version 1\.0\.6') { Write-Host 'ERROR: Built EXE does not contain About version 1.0.6.'; if ($s -match 'Version 1\.0\.[0-9]+') { Write-Host ('Found: ' + $Matches[0]) }; exit 1 }"
if errorlevel 1 goto wrong_app_version


echo Refreshing installer payload from the fresh build...
call prepare_installer.bat
if errorlevel 1 goto staging_failed

if not exist "release\ABM AV Player\ABMAVPlayer.exe" goto missing_stage
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p='release\ABM AV Player\ABMAVPlayer.exe'; $s=[Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($p)); if ($s -notmatch 'Version 1\.0\.5') { Write-Host 'ERROR: Staged EXE is not version 1.0.6.'; exit 1 }"
if errorlevel 1 goto wrong_staged_version
if not exist "release\ABM AV Player\LICENSE" goto missing_license

echo.
echo Installer payload:
for %%F in ("release\ABM AV Player\ABMAVPlayer.exe") do echo EXE timestamp: %%~tF  Size: %%~zF bytes
echo Compiling installer with:
echo "%ISCC%"
"%ISCC%" /Qp "installer\ABMAVPlayer.iss"
if errorlevel 1 goto compile_failed

echo.
echo Installer created under dist\ABM-AV-Player-Setup.exe
endlocal
exit /b 0

:missing_compiler
echo ERROR: Inno Setup 6/7 compiler (ISCC.exe) was not found.
echo Install Inno Setup, then run this script again.
endlocal
exit /b 1

:app_build_failed
echo ERROR: Application build failed. The installer was NOT built.
endlocal
exit /b 1

:missing_build
echo ERROR: build\ABMAVPlayer.exe not found.
echo Run build.bat successfully before building the installer.
endlocal
exit /b 1

:staging_failed
echo ERROR: Could not refresh installer staging. The installer was NOT built.
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

:wrong_app_version
echo ERROR: Application executable is not version 1.0.6. Check src\player.c and rebuild.
endlocal
exit /b 1

:wrong_staged_version
echo ERROR: Installer payload does not contain application version 1.0.5.
endlocal
exit /b 1
