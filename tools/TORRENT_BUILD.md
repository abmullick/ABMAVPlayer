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

The repository manifest pins libtorrent 2.1.2 and a vcpkg baseline. From the repository root, run `vcpkg install`; manifest mode installs into the repository-local `vcpkg_installed` folder. Do not mix x86, ARM64, static-triplet, or unrelated DLL files with this x64 build.

## Build

From the repository root:

```bat
tools\build_torrent_engine.bat
```

Output: `build\torrent-runtime\abm_torrent_engine.dll` plus the vcpkg runtime DLLs. Installer staging keeps these dependencies in a separate `torrent-runtime` directory to avoid collisions with VLC/OpenSSL DLLs.

## API

`src/torrent/torrent_engine.h` exposes a C ABI suitable for `LoadLibraryW` and `GetProcAddress`. It supports magnet and local .torrent ingestion, metadata/file enumeration, selecting one file, status/progress/rates/peer count, pause/resume/remove, and global rate limits.

Payload files start at `dont_download` priority. The caller must select a file after metadata arrives, avoiding accidental download of every file in a multi-file torrent.

## Scope

This is Phase 1 engine foundation. It does not yet add the Torrent Stream window or connect torrent data to LibVLC. Progressive verified-piece reads, loopback HTTP playback, UI and installer integration remain subsequent work.

## Persistent cache

Selected torrent payloads are written by libtorrent beneath the configured permanent downloads folder as `<info-hash>\`; the working-cache directory remains separate and stores cache metadata/eligible inactive cache folders. The 20 GiB default quota (configurable from 1–4096 GiB and persisted in `torrent-cache\cache_limit.txt`) accounts for actual bytes in managed hash folders under both locations. Before prioritizing a selected file, the engine checks available disk space and reserves the remaining size of that file, including outstanding reservations for other selected tasks. This prevents multiple simultaneous selections from overcommitting the same quota. Old inactive working-cache folders may be evicted first. Permanent downloads are never automatically deleted to satisfy the quota; if retained downloads exceed a newly lowered limit, further selections are blocked until the user raises the limit or manually removes data. **Clear inactive cache** removes only eligible working-cache folders and does not remove permanent downloads.

The C ABI cache calls are `ABM_Torrent_GetCacheInfo`, `ABM_Torrent_SetCacheLimit`, and `ABM_Torrent_ClearCache`. Windows build/runtime validation is still required.
