$ErrorActionPreference = "Stop"

function Write-Log([string]$Message) {
    [Console]::Error.WriteLine("[ABM vcpkg] $Message")
}

function Invoke-Git([string[]]$GitArgs) {
    & git @GitArgs *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "Git command failed: git $($GitArgs -join ' ')"
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$manifestPath = Join-Path $repoRoot "vcpkg.json"
if (-not (Test-Path $manifestPath)) {
    throw "vcpkg.json was not found at the repository root."
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$baseline = [string]$manifest.'builtin-baseline'
if ($baseline -notmatch '^[0-9a-fA-F]{40}$') {
    throw "vcpkg.json must contain a 40-character builtin-baseline."
}

$installedRoot = Join-Path $repoRoot "vcpkg_installed"
$cacheName = "ABM-AV-Player-vcpkg-x64-windows.zip"
$cacheDirectory = Join-Path $repoRoot "build\vcpkg-cache-download"
$cacheZip = Join-Path $cacheDirectory $cacheName
$cacheHash = "$cacheZip.sha256"
$cacheMetadata = "$cacheZip.json"
$metadata = $null
$cacheRestored = $false
$toolCommit = $baseline

# A release cache is optional. If it is not published, vcpkg will build the
# manifest dependencies normally. Never overwrite an existing local install.
if (-not (Test-Path $installedRoot)) {
    New-Item -ItemType Directory -Force -Path $cacheDirectory | Out-Null
    $baseUrl = "https://github.com/abmullick/ABMAVPlayer/releases/latest/download"
    try {
        Write-Log "Looking for the shared x64-windows dependency cache..."
        Invoke-WebRequest -UseBasicParsing -Uri "$baseUrl/$cacheName" -OutFile $cacheZip
        Invoke-WebRequest -UseBasicParsing -Uri "$baseUrl/$cacheName.sha256" -OutFile $cacheHash
        Invoke-WebRequest -UseBasicParsing -Uri "$baseUrl/$cacheName.json" -OutFile $cacheMetadata

        $metadata = Get-Content -LiteralPath $cacheMetadata -Raw | ConvertFrom-Json
        if ([int]$metadata.schemaVersion -ne 1 -or
            [string]$metadata.triplet -ne "x64-windows" -or
            [string]$metadata.builtinBaseline -ne $baseline) {
            throw "The published dependency cache does not match this repository's vcpkg baseline/triplet."
        }

        $expectedHash = [regex]::Match(
            (Get-Content -LiteralPath $cacheHash -Raw),
            '(?i)\b[0-9a-f]{64}\b'
        ).Value.ToLowerInvariant()
        if (-not $expectedHash) {
            throw "The dependency cache SHA-256 sidecar is invalid."
        }

        $actualHash = (Get-FileHash -LiteralPath $cacheZip -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            throw "The dependency cache SHA-256 verification failed."
        }

        if ([string]$metadata.vcpkgToolCommit -match '^[0-9a-fA-F]{40}$') {
            $toolCommit = [string]$metadata.vcpkgToolCommit
        }

        Write-Log "Cache verified. Restoring the existing vcpkg_installed tree..."
        Expand-Archive -LiteralPath $cacheZip -DestinationPath $repoRoot -Force
        if (-not (Test-Path (Join-Path $installedRoot "x64-windows\include\libtorrent\session.hpp"))) {
            throw "The restored archive does not contain the expected libtorrent headers."
        }
        $cacheRestored = $true
        Write-Log "Dependency cache restored. vcpkg will validate package ABI during install."
    }
    catch {
        if (Test-Path $installedRoot) {
            Remove-Item -LiteralPath $installedRoot -Recurse -Force
        }
        Write-Log "No usable shared cache was restored: $($_.Exception.Message)"
        Write-Log "The first vcpkg install will compile missing dependencies. After a successful build, package and publish the cache using tools\package_vcpkg_cache.ps1."
        $metadata = $null
        $toolCommit = $baseline
    }
}
else {
    Write-Log "Existing vcpkg_installed directory found; leaving it untouched."
}

# Respect an explicitly configured vcpkg installation. Otherwise create a
# project-managed checkout under build\, pinned to the cache tool commit (or
# the manifest baseline when no cache is available).
if ($env:VCPKG_ROOT) {
    $vcpkgRoot = (Resolve-Path -LiteralPath $env:VCPKG_ROOT).Path
    Write-Log "Using VCPKG_ROOT override: $vcpkgRoot"
}
else {
    $vcpkgRoot = Join-Path $repoRoot "build\toolchain\vcpkg"
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw "Git is required to bootstrap the pinned vcpkg checkout. Install Git for Windows or set VCPKG_ROOT."
    }

    if (-not (Test-Path (Join-Path $vcpkgRoot ".git"))) {
        if (Test-Path $vcpkgRoot) {
            $existingItems = @(Get-ChildItem -LiteralPath $vcpkgRoot -Force)
            if ($existingItems.Count -gt 0) {
                throw "Managed vcpkg folder exists but is not a Git checkout: $vcpkgRoot"
            }
        }
        else {
            New-Item -ItemType Directory -Force -Path $vcpkgRoot | Out-Null
        }
        Invoke-Git -GitArgs @("-C", $vcpkgRoot, "init")
        Invoke-Git -GitArgs @("-C", $vcpkgRoot, "remote", "add", "origin", "https://github.com/microsoft/vcpkg.git")
    }
    else {
        Invoke-Git -GitArgs @("-C", $vcpkgRoot, "remote", "set-url", "origin", "https://github.com/microsoft/vcpkg.git")
    }

    $dirty = & git -C $vcpkgRoot status --porcelain 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "Could not inspect the managed vcpkg checkout."
    }
    if ($dirty) {
        throw "The project-managed vcpkg checkout has local changes. Review build\toolchain\vcpkg before changing it."
    }

    $currentCommit = (& git -C $vcpkgRoot rev-parse HEAD 2>$null | Select-Object -First 1)
    if ($LASTEXITCODE -ne 0 -or $currentCommit -ne $toolCommit) {
        Write-Log "Fetching pinned vcpkg tool commit $toolCommit..."
        Invoke-Git -GitArgs @("-C", $vcpkgRoot, "fetch", "--depth", "1", "origin", $toolCommit)
        Invoke-Git -GitArgs @("-C", $vcpkgRoot, "checkout", "--detach", "FETCH_HEAD")
    }
}

$vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
if (-not (Test-Path $vcpkgExe)) {
    $bootstrap = Join-Path $vcpkgRoot "bootstrap-vcpkg.bat"
    if (-not (Test-Path $bootstrap)) {
        throw "vcpkg bootstrap script was not found under $vcpkgRoot"
    }
    Write-Log "Bootstrapping vcpkg..."
    & $bootstrap -disableMetrics
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $vcpkgExe)) {
        throw "vcpkg bootstrap failed."
    }
}

# This is the only success-stream output: build_torrent_engine.bat captures it.
Write-Output $vcpkgRoot
