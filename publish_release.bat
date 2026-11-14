@echo off
setlocal EnableExtensions

rem The canonical VERSION file is the only source of the release number.
if not "%~1"=="" goto unexpected_argument
set "ABM_VERSION="
for /f "usebackq delims=" %%V in (`powershell -NoProfile -ExecutionPolicy Bypass -File tools\prepare_version.ps1`) do set "ABM_VERSION=%%V"
if not defined ABM_VERSION goto version_metadata_mismatch
set "VERSION=%ABM_VERSION%"
set "TAG=v%VERSION%"

if not exist "dist\ABM-AV-Player-Setup.exe" goto missing_installer
if not exist "RELEASE_NOTES.md" goto missing_notes

rem Verify installer PE metadata against the same canonical VERSION file.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$expected=(Get-Content 'VERSION' -Raw).Trim(); $p=(Resolve-Path 'dist\ABM-AV-Player-Setup.exe').Path; $v=[Diagnostics.FileVersionInfo]::GetVersionInfo($p); Write-Host ('Expected version: ' + $expected); Write-Host ('Installer FileVersion: ' + $v.FileVersion); Write-Host ('Installer ProductVersion: ' + $v.ProductVersion); if ($v.ProductVersion -ne $expected -or $v.FileVersion -notlike ($expected + '*')) { Write-Host 'ERROR: Installer PE version resources do not match VERSION.'; exit 1 }"
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

:unexpected_argument
echo ERROR: Do not pass a version argument. The release version is read from VERSION.
echo Usage: publish_release.bat
endlocal
exit /b 2

:version_metadata_mismatch
echo ERROR: Could not generate release artifacts from VERSION.
echo Check VERSION, tools\prepare_version.ps1 and vcpkg.json.in.
endlocal
exit /b 1

:missing_installer
echo ERROR: dist\ABM-AV-Player-Setup.exe not found. Run build_installer.bat first.
endlocal
exit /b 1

:missing_notes
echo ERROR: RELEASE_NOTES.md not found.
endlocal
exit /b 1

:wrong_installer_version
echo ERROR: Installer PE version does not match the canonical VERSION file.
echo Rebuild with build_installer.bat before publishing.
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
