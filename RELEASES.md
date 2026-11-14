# GitHub Releases and in-app updates

ABM AV Player checks the latest **published GitHub Release** for this repository:

https://github.com/abmullick/ABMAVPlayer/releases/latest

The in-app updater expects each release to include the Windows installer asset named exactly `ABM-AV-Player-Setup.exe`. It compares the release tag with the version compiled into the application. If a newer release is found, it displays the release notes, downloads the installer over HTTPS, verifies the downloaded file against the SHA-256 digest returned by GitHub for that asset, requests Windows elevation, launches the installer, and closes the running player so the installer can replace its files.

The installer uses the existing Inno Setup AppId and installation directory to upgrade the current installation. User data under AppData is stored separately from the application install directory.

## Current source version: v1.0.7

The v1.0.7 source release focuses on the F1 Help Centre experience and coordinated version alignment. It includes focused topic articles, richer text formatting, improved paragraph presentation and normalization of escaped line endings. It also aligns the main application, Streaming Centre, installer, package manifest and release scripts.

The v1.0.7 source has not, by itself, been certified by a Windows build or runtime test. Complete the validation checklist below before publishing.

## Publishing v1.0.7

1. Confirm the following all identify `1.0.7`:
   - `APP_VERSION` in `src/player.c`.
   - The version displayed in `src/streaming/streaming_host.cpp`.
   - `MyAppVersion` and `VersionInfoVersion` in `installer/ABMAVPlayer.iss`.
   - The `version-string` in `vcpkg.json`.
   - The version guards in `build_installer.bat` and `publish_release.bat`.
2. Pull the latest `main` on the Windows build machine and perform a clean x64 build using the documented toolchain. Do not set `ABM_REQUIRE_SIGNED_BUILD=1` unless a deliberate signed-build workflow has been configured.
3. Run the application and verify the About version is 1.0.7. Open the Streaming Centre About window and verify its version also reads 1.0.7.
4. Complete the Windows acceptance checklist: licensing/trial and activation; local audio/video playback; playlists and subtitles; audio processing and visualizer; settings persistence; Streaming Centre/WebView2; torrent metadata, progressive playback, download and cache controls; main-player casting; Streaming Centre receiver discovery/casting; keyboard shortcuts; multi-monitor/DPI behaviour; and Help Centre F1, topic selection, search, article formatting and scrolling.
5. Run `build_installer.bat`. It rebuilds the application, refreshes the staging directory and checks the application and staged executable version before compiling the installer.
6. Confirm `dist\\ABM-AV-Player-Setup.exe` exists. Inspect the installer PE FileVersionInfo and ProductVersion and confirm they identify 1.0.7.
7. Test a clean installation and an upgrade over the previous release. Confirm the existing installation is upgraded in place, shortcuts behave as expected, and user settings, licensing state and user media/cache data are not unintentionally deleted or reset.
8. Review the v1.0.7 section in `RELEASE_NOTES.md`. Remove or qualify any item that did not pass validation; add any verified fixes discovered during testing.
9. On the maintainer's machine, install GitHub CLI and authenticate with `gh auth login`.
10. From the repository root, run:

   ```bat
   publish_release.bat 1.0.7
   ```

The script checks the installer version resource, refuses an existing release tag, publishes a normal GitHub Release against `main`, and uploads `dist\\ABM-AV-Player-Setup.exe`.

## Release requirements

- Publish a normal, non-draft, non-prerelease GitHub Release. The updater uses GitHub's `releases/latest` endpoint.
- Use tag `v1.0.7` and attach the installer as `ABM-AV-Player-Setup.exe` exactly. Renaming the asset breaks automatic update discovery.
- Do not publish an installer until the Windows build, installation/upgrade test and feature acceptance tests have passed.
- Keep the release tag, application About version, Streaming Centre About version, installer PE version resources, vcpkg package version and release scripts aligned.
- The updater does not update from arbitrary branches or pre-releases.
- Users can download releases manually from the [Releases page](https://github.com/abmullick/ABMAVPlayer/releases).

## Update integrity and signing

The updater obtains release metadata from the official GitHub API and downloads the installer from GitHub over HTTPS. It asks before installing and Windows displays the normal elevation prompt. It does not silently install updates.

The updater requires GitHub to provide a SHA-256 digest for the installer asset. It fails closed if the digest is missing or does not match. This confirms that the downloaded bytes match the digest GitHub reports; it does not independently authenticate the publisher as Authenticode does. Authenticode signing remains optional and disabled by default for v1.0.7. Windows SmartScreen or antivirus products may still show warnings for an unsigned installer. Users should install only releases published by the official `abmullick/ABMAVPlayer` repository.

## Version guard behaviour

`build_installer.bat` checks the About-version string in both the freshly built executable and the staged executable before packaging. `publish_release.bat` checks the installer's PE FileVersionInfo/ProductVersion resource; it does not search inside the compressed installer payload. If any displayed or embedded version is stale, rebuild from the current `main` source and verify the result before publishing.

## Previous release baseline: v1.0.6

The v1.0.6 release introduced native compiler/linker hardening, stricter local trial-state validation, constant-time activation digest comparison and lightweight digest obfuscation. Authenticode enforcement remained optional and disabled by default. The standard unsigned build did not require a certificate or publisher registration.

