@echo off
setlocal EnableExtensions

rem Publish the already-built Windows installer as a GitHub Release.
rem Usage: publish_release.bat 1.0.3

if "%~1"=="" goto usage
set "VERSION=%~1"
set "TAG=v%VERSION%"

if not "%VERSION%"=="1.0.3" goto invalid_version
if not exist "dist\ABM-AV-Player-Setup.exe" goto missing_installer
if not exist "RELEASE_NOTES.md" goto missing_notes

rem Verify the installer contains the same application version before upload.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p='dist\ABM-AV-Player-Setup.exe'; $s=[Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($p)); if ($s -notmatch 'Version 1\.0\.3') { Write-Host 'ERROR: Installer does not contain About version 1.0.3.'; if ($s -match 'Version 1\.0\.[0-9]+') { Write-Host ('Found: ' + $Matches[0]) }; exit 1 }"
if errorlevel 1 goto wrong_installer_version

where gh >nul 2>nul
if errorlevel 1 goto missing_gh

gh auth status
if errorlevel 1 goto not_authenticated

gh release view "%TAG%" --repo abmullick/ABMAVPlayer >nul 2>nul
if not errorlevel 1 goto release_exists

echo Publishing %TAG% with the verified Windows installer...
gh release create "%TAG%" "dist\ABM-AV-Player-Setup.exe" --repo abmullick/ABMAVPlayer --title "ABM AV Player %TAG%" --notes-file RELEASE_NOTES.md --target main
if errorlevel 1 goto publish_failed

echo.
echo Release published: https://github.com/abmullick/ABMAVPlayer/releases/tag/%TAG%
endlocal
exit /b 0

:usage
echo Usage: publish_release.bat VERSION
echo Example: publish_release.bat 1.0.3
endlocal
exit /b 2

:invalid_version
echo ERROR: This script is currently prepared for version 1.0.3 only.
endlocal
exit /b 2

:missing_installer
echo ERROR: dist\ABM-AV-Player-Setup.exe not found. Run build_installer.bat first.
endlocal
exit /b 1

:missing_notes
echo ERROR: RELEASE_NOTES.md not found.
endlocal
exit /b 1

:wrong_installer_version
echo ERROR: Refusing to publish an installer that does not identify itself as v1.0.3.
echo Rebuild with build_installer.bat and verify About shows Version 1.0.3.
endlocal
exit /b 1

:missing_gh
echo ERROR: GitHub CLI (gh) is required. Install it from https://cli.github.com/
endlocal
exit /b 1

:not_authenticated
echo ERROR: Sign in first with: gh auth login
endlocal
exit /b 1

:release_exists
echo ERROR: GitHub release %TAG% already exists. Do not overwrite it; inspect the existing release first.
endlocal
exit /b 1

:publish_failed
echo Release creation failed. Check GitHub CLI output and repository permissions.
endlocal
exit /b 1
