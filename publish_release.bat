@echo off
setlocal EnableExtensions

rem Publish the already-built Windows installer as a GitHub Release.
rem Usage: publish_release.bat 1.0.7

if "%~1"=="" goto usage
set "VERSION=%~1"
set "TAG=v%VERSION%"

if not "%VERSION%"=="1.0.7" goto invalid_version
if not exist "dist\ABM-AV-Player-Setup.exe" goto missing_installer
if not exist "RELEASE_NOTES.md" goto missing_notes

rem Refuse to publish if source metadata or the release-notes heading is stale.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$expected='%VERSION%'; $app=[IO.File]::ReadAllText('src\player.c'); $stream=[IO.File]::ReadAllText('src\streaming\streaming_host.cpp'); $iss=[IO.File]::ReadAllText('installer\ABMAVPlayer.iss'); $manifest=Get-Content 'vcpkg.json' -Raw | ConvertFrom-Json; $notes=[IO.File]::ReadAllText('RELEASE_NOTES.md'); $av=[regex]::Match($app,'APP_VERSION L[^0-9]*([0-9]+\.[0-9]+\.[0-9]+)').Groups[1].Value; $sv=[regex]::Match($stream,'Version ([0-9]+\.[0-9]+\.[0-9]+)').Groups[1].Value; $iv=[regex]::Match($iss,'MyAppVersion[^0-9]*([0-9]+\.[0-9]+\.[0-9]+)').Groups[1].Value; $pe=[regex]::Match($iss,'VersionInfoVersion=([0-9]+\.[0-9]+\.[0-9]+)\.0').Groups[1].Value; $mv=$manifest.'version-string'; $heading='# ABM AV Player v' + $expected; Write-Host ('App=' + $av + '; Streaming=' + $sv + '; Installer=' + $iv + '; PE=' + $pe + '; vcpkg=' + $mv); if ($av -ne $expected -or $sv -ne $expected -or $iv -ne $expected -or $pe -ne $expected -or $mv -ne $expected -or -not $notes.StartsWith($heading)) { Write-Host 'ERROR: Source versions or release notes do not match the requested release version.'; exit 1 }"
if errorlevel 1 goto version_metadata_mismatch

rem Inno Setup compresses the application payload, so do not search the
rem installer bytes for the About string. Verify the installer PE version
rem resource, which is stamped by VersionInfoVersion/ProductVersion in the ISS.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p=(Resolve-Path 'dist\ABM-AV-Player-Setup.exe').Path; $v=[Diagnostics.FileVersionInfo]::GetVersionInfo($p); Write-Host ('Installer FileVersion: ' + $v.FileVersion); Write-Host ('Installer ProductVersion: ' + $v.ProductVersion); if ($v.ProductVersion -ne '1.0.7' -and $v.FileVersion -notlike '1.0.7*') { Write-Host 'ERROR: Installer PE version resource is not 1.0.7. Rebuild with the updated installer script.'; exit 1 }"
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
echo Example: publish_release.bat 1.0.7
endlocal
exit /b 2

:invalid_version
echo ERROR: This script is currently prepared for version 1.0.7 only.
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

:version_metadata_mismatch
echo ERROR: Source version metadata or RELEASE_NOTES.md does not match %VERSION%.
echo Run the guarded build and align all release metadata before publishing.
endlocal
exit /b 1

:wrong_installer_version
echo ERROR: Installer PE version resource does not identify v1.0.7.
echo Rebuild using the updated installer/ABMAVPlayer.iss, then verify About shows Version 1.0.7.
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
