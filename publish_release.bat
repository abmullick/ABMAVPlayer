@echo off
setlocal EnableExtensions

rem Publish the already-built Windows installer as a GitHub Release.
rem Usage: publish_release.bat 1.0.2
if "%~1"=="" (
  echo Usage: publish_release.bat VERSION
  echo Example: publish_release.bat 1.0.2
  exit /b 2
)
set "VERSION=%~1"
set "TAG=v%VERSION%"

if not exist "dist\ABM-AV-Player-Setup.exe" (
  echo ERROR: dist\ABM-AV-Player-Setup.exe not found.
  echo Run build.bat, prepare_installer.bat and build_installer.bat first.
  exit /b 1
)
where gh >nul 2>nul
if errorlevel 1 (
  echo ERROR: GitHub CLI (gh) is required. Install it from https://cli.github.com/
  exit /b 1
)
gh auth status
if errorlevel 1 (
  echo ERROR: Sign in first with: gh auth login
  exit /b 1
)
if not exist "RELEASE_NOTES.md" (
  echo ERROR: RELEASE_NOTES.md not found.
  exit /b 1
)

echo Publishing %TAG% with the Windows installer...
gh release create "%TAG%" "dist\ABM-AV-Player-Setup.exe" ^
  --repo abmullick/ABMAVPlayer ^
  --title "ABM AV Player %TAG%" ^
  --notes-file RELEASE_NOTES.md ^
  --target main
if errorlevel 1 (
  echo.
  echo Release creation failed. Check whether the tag or release already exists.
  exit /b 1
)
echo.
echo Release published: https://github.com/abmullick/ABMAVPlayer/releases/tag/%TAG%
endlocal
