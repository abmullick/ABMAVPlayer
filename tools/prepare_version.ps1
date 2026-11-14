$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$versionPath = Join-Path $repoRoot "VERSION"
$templatePath = Join-Path $repoRoot "vcpkg.json.in"
$buildPath = Join-Path $repoRoot "build"

if (-not (Test-Path -LiteralPath $versionPath)) {
    throw "Canonical VERSION file is missing: $versionPath"
}

$version = (Get-Content -LiteralPath $versionPath -Raw).Trim()
if ($version -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') {
    throw "VERSION must contain exactly one semantic version in MAJOR.MINOR.PATCH form."
}
$parts = $version.Split('.')
$major = [int]$parts[0]
$minor = [int]$parts[1]
$patch = [int]$parts[2]

New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$header = @"
#ifndef ABM_VERSION_H
#define ABM_VERSION_H
#define ABM_VERSION_W L"$version"
#define ABM_VERSION_STR "$version"
#define ABM_VERSION_MAJOR $major
#define ABM_VERSION_MINOR $minor
#define ABM_VERSION_PATCH $patch
#endif
"@
[System.IO.File]::WriteAllText((Join-Path $buildPath "version.h"), $header, $utf8NoBom)

$iss = '#define ABM_VERSION "' + $version + '"' + [Environment]::NewLine
[System.IO.File]::WriteAllText((Join-Path $buildPath "version.iss"), $iss, $utf8NoBom)

if (-not (Test-Path -LiteralPath $templatePath)) {
    throw "vcpkg manifest template is missing: $templatePath"
}
$template = Get-Content -LiteralPath $templatePath -Raw
if (([regex]::Matches($template, '@ABM_VERSION@')).Count -ne 1) {
    throw "vcpkg.json.in must contain exactly one @ABM_VERSION@ placeholder."
}
$manifest = $template.Replace('@ABM_VERSION@', $version)
[System.IO.File]::WriteAllText((Join-Path $repoRoot "vcpkg.json"), $manifest, $utf8NoBom)

Write-Output $version
