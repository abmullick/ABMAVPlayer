$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$installedRoot = Join-Path $repoRoot "vcpkg_installed"
if (-not (Test-Path (Join-Path $installedRoot "x64-windows\include\libtorrent\session.hpp")) -or
    -not (Test-Path (Join-Path $installedRoot "x64-windows\include\juice\juice.h"))) {
    throw "A successful x64-windows install containing both libtorrent and the custom libjuice port is required before packaging the cache."
}

# Reuse the project's bootstrap logic so the cache metadata records the same
# vcpkg tool checkout that the next setup will restore.
$vcpkgRoot = (& (Join-Path $PSScriptRoot "ensure_vcpkg.ps1") | Select-Object -Last 1)
if (-not $vcpkgRoot -or -not (Test-Path (Join-Path $vcpkgRoot "vcpkg.exe"))) {
    throw "Could not resolve a working vcpkg installation."
}

$manifest = Get-Content -LiteralPath (Join-Path $repoRoot "vcpkg.json") -Raw | ConvertFrom-Json
$baseline = [string]$manifest.'builtin-baseline'
if ($baseline -notmatch '^[0-9a-fA-F]{40}$') {
    throw "Invalid builtin-baseline in vcpkg.json."
}

$vcpkgToolCommit = $null
try {
    $candidate = (& git -C $vcpkgRoot rev-parse HEAD 2>$null | Select-Object -First 1)
    if ($LASTEXITCODE -eq 0 -and $candidate -match '^[0-9a-fA-F]{40}$') {
        $vcpkgToolCommit = $candidate
    }
}
catch {
    $vcpkgToolCommit = $null
}

$sourceCommit = $null
try {
    $candidate = (& git -C $repoRoot rev-parse HEAD 2>$null | Select-Object -First 1)
    if ($LASTEXITCODE -eq 0 -and $candidate -match '^[0-9a-fA-F]{40}$') {
        $sourceCommit = $candidate
    }
}
catch {
    $sourceCommit = $null
}

$dist = Join-Path $repoRoot "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$name = "ABM-AV-Player-vcpkg-x64-windows.zip"
$archive = Join-Path $dist $name
$hashFile = "$archive.sha256"
$metadataFile = "$archive.json"

if (Test-Path $archive) { Remove-Item -LiteralPath $archive -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
Write-Host "Packaging the existing vcpkg_installed directory. This may take several minutes..."
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $installedRoot,
    $archive,
    [System.IO.Compression.CompressionLevel]::Optimal,
    $true
)

$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath $hashFile -Value "$hash  $name" -Encoding ASCII

$metadata = [ordered]@{
    schemaVersion = 1
    triplet = "x64-windows"
    builtinBaseline = $baseline
    vcpkgToolCommit = $vcpkgToolCommit
    sourceCommit = $sourceCommit
    generatedUtc = [DateTime]::UtcNow.ToString("o")
}
$metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $metadataFile -Encoding UTF8

$size = (Get-Item -LiteralPath $archive).Length
Write-Host ""
Write-Host "Dependency cache package created:"
Write-Host "  $archive"
Write-Host ("  Size: {0:N2} GB" -f ($size / 1GB))
Write-Host "  $hashFile"
Write-Host "  $metadataFile"
Write-Host ""
Write-Host "Upload all three files to the latest ABM AV Player GitHub Release."
