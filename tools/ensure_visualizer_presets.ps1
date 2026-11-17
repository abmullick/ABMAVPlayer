$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$assetRoot = Join-Path $root "assets\visualizer"
$presetRoot = Join-Path $assetRoot "presets"
$textureRoot = Join-Path $assetRoot "textures"
New-Item -ItemType Directory -Force -Path $presetRoot,$textureRoot | Out-Null

$existing = Get-ChildItem -LiteralPath $presetRoot -Filter "*.milk" -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $existing) {
    $zip = Join-Path $env:TEMP "abm-projectm-cream-of-the-crop.zip"
    $extract = Join-Path $env:TEMP "abm-projectm-cream-of-the-crop"
    if (Test-Path $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
    Write-Host "Downloading the official projectM Cream of the Crop preset collection..."
    Invoke-WebRequest -Uri "https://codeload.github.com/projectM-visualizer/presets-cream-of-the-crop/zip/0180df21f5e0bd39b9060cc5de420ed2f1f9e509" -OutFile $zip
    Expand-Archive -LiteralPath $zip -DestinationPath $extract -Force
    $source = Get-ChildItem -LiteralPath $extract -Directory | Select-Object -First 1
    if (-not $source) { throw "The projectM preset archive did not contain its expected root folder." }
    Copy-Item -Path (Join-Path $source.FullName "*") -Destination $presetRoot -Recurse -Force
    Remove-Item -LiteralPath $zip -Force
    Remove-Item -LiteralPath $extract -Recurse -Force
}
$licenseUrl = "https://raw.githubusercontent.com/projectM-visualizer/presets-cream-of-the-crop/0180df21f5e0bd39b9060cc5de420ed2f1f9e509/LICENSE.md"
Invoke-WebRequest -Uri $licenseUrl -OutFile (Join-Path $assetRoot "PRESET-LICENSE.md")
$notice = @"
ABM AV Player uses the projectM Milkdrop-compatible visualization engine.

Preset collection:
Cream of the Crop, maintained by the projectM community.
Source: https://github.com/projectM-visualizer/presets-cream-of-the-crop
The collection is pinned to upstream commit 0180df21f5e0bd39b9060cc5de420ed2f1f9e509. Its upstream LICENSE.md explains that most presets were released without
specific licences and that the maintainers consider the freely released collection
public domain; individual preset authors retain their rights and can request removal.
The upstream licence notice is included as PRESET-LICENSE.md.

Engine:
projectM, https://github.com/projectM-visualizer/projectm
The engine is LGPL-2.1; its licence and notices are supplied by the vcpkg package.
"@
[System.IO.File]::WriteAllText((Join-Path $assetRoot "README.txt"),$notice,(New-Object System.Text.UTF8Encoding($false)))
Write-Host "projectM preset assets are ready."
