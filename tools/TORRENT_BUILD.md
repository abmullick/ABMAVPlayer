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

Torrent payload data is stored below the application data directory supplied to `ABM_Torrent_Initialize`: `torrent-cache\<info-hash>\`. Each torrent gets its own stable info-hash folder, allowing libtorrent to recheck and reuse verified pieces when the same source is added again. The default cache quota is 20 GiB and is persisted in `torrent-cache\cache_limit.txt`; the UI allows 1–4096 GiB. Before prioritizing a selected file, the engine checks both the quota and available disk space. When space under the quota is needed, oldest inactive torrent folders are evicted first. Folders belonging to loaded tasks are never auto-evicted. **Clear inactive cache** removes only folders not owned by a currently loaded task; removing a task normally retains its data for later reuse.

The C ABI cache calls are `ABM_Torrent_GetCacheInfo`, `ABM_Torrent_SetCacheLimit`, and `ABM_Torrent_ClearCache`. Windows build/runtime validation is still required.
