$ErrorActionPreference = "Stop"
$minimumMajor = 1
$minimumMinor = 25
$version = $null
$existing = Get-Command go -ErrorAction SilentlyContinue
if ($existing) {
    $v = (& $existing.Source version 2>$null | Select-Object -First 1)
    if ($v -match 'go(\d+)\.(\d+)') {
        $version = [int]$Matches[1] * 100 + [int]$Matches[2]
    }
}
if ($version -and $version -ge 125) {
    Write-Output $existing.Source
    exit 0
}
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$toolRoot = Join-Path $root "build\toolchain"
$goExe = Join-Path $toolRoot "go\bin\go.exe"
if (-not (Test-Path $goExe)) {
    New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
    $archive = Join-Path $toolRoot "go1.27.3.windows-amd64.zip"
    $url = "https://go.dev/dl/go1.27.3.windows-amd64.zip"
    Write-Host "Downloading official Go 1.27.3 Windows x64 toolchain (about 75 MB)..."
    Invoke-WebRequest -Uri $url -OutFile $archive
    $expected = "f26081f438319757f8a1b46d7d0b2c5ec7583d4038e485e0aa725cfde7885fff"
    $actual = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { Remove-Item $archive -Force; throw "Go toolchain SHA-256 verification failed." }
    Expand-Archive -LiteralPath $archive -DestinationPath $toolRoot -Force
    Remove-Item $archive -Force
}
if (-not (Test-Path $goExe)) { throw "Go toolchain extraction failed: go.exe not found." }
$detected = (& $goExe version 2>&1 | Out-String).Trim()
if ($detected -notmatch 'go1\.(\d+)') { throw "Cannot determine bundled Go version: $detected" }
if ([int]$Matches[1] -lt 25) { throw "Bundled Go version is older than required Go 1.25." }
Write-Output $goExe
