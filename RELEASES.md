# GitHub Releases and in-app updates

ABM AV Player checks the latest **published GitHub Release** for this repository:

https://github.com/abmullick/ABMAVPlayer/releases/latest

The in-app updater expects each release to include the Windows installer asset named exactly `ABM-AV-Player-Setup.exe`. It compares the release tag with the version compiled into the application. If a newer release is found, it displays the release notes, downloads the installer over HTTPS, verifies the downloaded file against the SHA-256 digest returned by GitHub for that asset, requests Windows elevation, launches the installer, and closes the running player so the installer can replace its files.

The installer uses the existing Inno Setup AppId and installation directory to upgrade the current installation. User data under AppData is stored separately from the application install directory.

## Current source version: v1.0.6

The v1.0.6 source updates include native compiler/linker hardening, stricter local trial-state validation, constant-time activation digest comparison and lightweight digest obfuscation. Authenticode enforcement remains optional and disabled by default. The standard unsigned build does not require a certificate or publisher registration.

## Publishing v1.0.6

1. Confirm `APP_VERSION` in `src/player.c`, the Streaming Centre About version in `src/streaming/streaming_host.cpp`, `MyAppVersion` and `VersionInfoVersion` in `installer/ABMAVPlayer.iss`, and the package version in `vcpkg.json` are aligned to `1.0.6`.
2. Build the application on Windows with the normal unsigned configuration. Do not set `ABM_REQUIRE_SIGNED_BUILD=1`; Authenticode is intentionally optional.
3. Run the application and complete the relevant manual acceptance tests, including licensing/trial, local playback, Streaming Centre, torrent playback/download, both casting paths, and installer upgrade.
4. Run `build_installer.bat`. It rebuilds the application, refreshes the staging directory and checks the application and staged EXE version before compiling the installer.
5. Confirm `dist\\ABM-AV-Player-Setup.exe` exists and inspect its Windows FileVersionInfo product/file version.
6. Review the top v1.0.6 section in `RELEASE_NOTES.md` and ensure it describes the tested release.
7. Install GitHub CLI and authenticate with `gh auth login` on the maintainer's machine.
8. From the repository root, run:

   ```bat
   publish_release.bat 1.0.6
   ```

The script creates tag `v1.0.6`, publishes a normal GitHub Release against `main`, and uploads `dist\\ABM-AV-Player-Setup.exe`. It refuses to publish if the installer version resource does not match 1.0.6 or if the release tag already exists.

## Release requirements

- Publish a normal, non-draft, non-prerelease GitHub Release. The updater uses GitHub's `releases/latest` endpoint.
- Attach the installer as `ABM-AV-Player-Setup.exe` exactly. Renaming the asset breaks automatic update discovery.
- Do not publish an installer until it has been built and tested on Windows.
- Keep the release tag, application About version, Streaming Centre About version, installer version resources and package manifest aligned.
- The updater does not update from arbitrary branches or pre-releases.
- Users can download releases manually from the [Releases page](https://github.com/abmullick/ABMAVPlayer/releases).

## Update integrity and signing

The updater obtains release metadata from the official GitHub API and downloads the installer from GitHub over HTTPS. It asks before installing and Windows displays the normal elevation prompt. It does not silently install updates.

The updater requires GitHub to provide a SHA-256 digest for the installer asset. It fails closed if the digest is missing or does not match. This confirms that the downloaded bytes match the digest GitHub reports; it does not independently authenticate the publisher as Authenticode does. Authenticode signing is not required for v1.0.6 and remains an optional future hardening step. Windows SmartScreen or antivirus products may still show warnings for an unsigned installer. Users should install only releases published by the official `abmullick/ABMAVPlayer` repository.

## Version guard behaviour

`build_installer.bat` checks the About-version string in both the freshly built executable and the staged executable before packaging. `publish_release.bat` checks the installer's PE FileVersionInfo/ProductVersion resource; it does not search inside the compressed installer payload. If any displayed or embedded version is stale, rebuild from the current `main` source and verify the result before publishing.
