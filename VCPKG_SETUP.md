# Reproducible Windows vcpkg setup

ABM AV Player uses vcpkg manifest mode. The root vcpkg.json pins the libtorrent dependency and its builtin registry baseline. The project-local vcpkg-configuration.json selects the committed libjuice overlay, which contains the libjuice 1.7.4 port and the CMake compatibility patch.

## Normal setup on a new Windows machine

1. Install Git for Windows, Visual Studio 2026 Build Tools with **Desktop development with C++**, the Windows SDK, CMake 3.21+, and the other SDKs listed in the main README.
2. Clone ABM AV Player.
3. Open the **x64 Native Tools Command Prompt for VS 2026**.
4. Run build.bat.

The torrent build step now calls tools/ensure_vcpkg.ps1 automatically. Unless VCPKG_ROOT is explicitly set, it creates a project-managed vcpkg checkout under build/toolchain/vcpkg, checks out the pinned tool commit, and bootstraps vcpkg. It then runs manifest installation for the x64-windows triplet.

If the latest GitHub Release contains the three cache assets listed below, the setup script downloads and verifies the archive, restores the vcpkg_installed directory, and lets vcpkg validate the installed package ABI. If there is no compatible cache asset, vcpkg builds missing dependencies from source using the committed manifest and libjuice overlay. That first build is the one-time cost on a new machine.

An existing vcpkg_installed directory is never overwritten by the setup script.

## Publish a reusable dependency cache once

After a successful x64 Windows build on a known-good machine, run:

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package_vcpkg_cache.ps1
~~~

This packages the existing vcpkg_installed tree and produces these files under dist:

- ABM-AV-Player-vcpkg-x64-windows.zip
- ABM-AV-Player-vcpkg-x64-windows.zip.sha256
- ABM-AV-Player-vcpkg-x64-windows.zip.json

Upload all three assets to the latest published ABM AV Player GitHub Release. With GitHub CLI authenticated, the upload command is:

~~~powershell
gh release list --limit 1
gh release upload <release-tag> dist\ABM-AV-Player-vcpkg-x64-windows.zip dist\ABM-AV-Player-vcpkg-x64-windows.zip.sha256 dist\ABM-AV-Player-vcpkg-x64-windows.zip.json --clobber
~~~

Alternatively, attach the same three files to the release using the GitHub website. The asset names must remain exact because the setup script downloads them from the GitHub releases/latest/download endpoint.

The setup script checks the SHA-256, triplet, and manifest baseline before extracting the cache. vcpkg then checks package ABI compatibility during vcpkg install; if the compiler, triplet, manifest, or dependency ABI has changed, vcpkg rebuilds the affected packages rather than blindly trusting incompatible binaries.

## What is and is not stored in Git

Do not commit vcpkg_installed or the cache ZIP to the source repository. The installed tree is large and Windows/toolchain-specific. Keep the manifest, baseline, overlay port, patches, and setup scripts in Git; distribute the known-good compiled dependency snapshot as a GitHub Release asset.

The local vcpkg_installed, release, and dist folders are ignored by Git. This does not delete or move any existing local files.

## Optional external vcpkg checkout

If you already maintain vcpkg elsewhere, set VCPKG_ROOT before building. The build scripts respect that override. When it is not set, ABM AV Player uses its own pinned checkout under build/toolchain/vcpkg.
