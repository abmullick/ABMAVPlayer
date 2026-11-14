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

$resource = @"
#include <windows.h>
VS_VERSION_INFO VERSIONINFO
 FILEVERSION $major,$minor,$patch,0
 PRODUCTVERSION $major,$minor,$patch,0
 FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
 FILEFLAGS 0x0L
 FILEOS VOS_NT_WINDOWS32
 FILETYPE VFT_APP
 FILESUBTYPE 0x0L
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904B0"
    BEGIN
      VALUE "CompanyName", "Abhisek Basu Mullick\0"
      VALUE "FileDescription", "ABM AV Player\0"
      VALUE "FileVersion", "$version.0\0"
      VALUE "InternalName", "ABMAVPlayer\0"
      VALUE "OriginalFilename", "ABMAVPlayer.exe\0"
      VALUE "ProductName", "ABM AV Player\0"
      VALUE "ProductVersion", "$version\0"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
"@
[System.IO.File]::WriteAllText((Join-Path $buildPath "version.rc"), $resource, $utf8NoBom)

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
