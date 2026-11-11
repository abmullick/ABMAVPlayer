# Building the ABM Torrent Engine (Windows x64)

The torrent engine is a separate native DLL. It is intentionally isolated so torrent dependency failures cannot prevent ordinary LibVLC playback or the Streaming Centre from starting.

## Prerequisites

- Windows 11 x64
- Visual Studio x64 Native Tools command prompt
- CMake 3.21+
- vcpkg

## Install dependency

Set VCPKG_ROOT to your vcpkg checkout and install the x64 package:

```bat
set VCPKG_ROOT=C:\vcpkg
"%VCPKG_ROOT%\vcpkg.exe" install libtorrent:x64-windows
```

The repository manifest pins libtorrent 2.1.2 and a vcpkg baseline. The install is manifest-mode and is placed in the repository-local `vcpkg_installed` folder. Do not mix x86, ARM64, static-triplet, or unrelated DLL files with this x64 build.

## Build

From the repository root:

```bat
tools\build_torrent_engine.bat
```

Output: `build\torrent-runtime\abm_torrent_engine.dll` plus the vcpkg runtime DLLs.

## API

`src/torrent/torrent_engine.h` exposes a C ABI suitable for `LoadLibraryW` and `GetProcAddress`. It supports magnet and local .torrent ingestion, metadata/file enumeration, selecting one file, status/progress/rates/peer count, pause/resume/remove, and global rate limits.

Payload files start at `dont_download` priority. The caller must select a file after metadata arrives, avoiding accidental download of every file in a multi-file torrent.

## Scope

This is Phase 1 engine foundation. It does not yet add the Torrent Stream window or connect torrent data to LibVLC. Progressive verified-piece reads, loopback HTTP playback, UI and installer integration remain subsequent work.
