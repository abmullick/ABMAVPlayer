# GitHub Releases and in-app updates

ABM AV Player checks the latest **published GitHub Release** for this repository:

https://github.com/abmullick/ABMAVPlayer-Releases/releases/latest

The in-app updater expects each release to include the Windows installer asset named exactly `ABM-AV-Player-Setup.exe`. It compares the release tag with the version compiled into the application. If a newer release is found, it displays the release notes, downloads the installer over HTTPS, verifies the downloaded file against the SHA-256 digest returned by GitHub for that asset, requests Windows elevation, launches the installer, and closes the running player so the installer can replace its files.

The installer uses the existing Inno Setup AppId and installation directory to upgrade the current installation. User data under AppData is stored separately from the application install directory.

## Canonical release version

The current source candidate is **v1.1.3**, prepared from the latest `main` branch. It adds Home Theatre keyless Wikidata movie and TVmaze television discovery/local matching, expands the music visualizer to ten Direct2D/OpenGL styles with an isolated renderer, smooths visualizer startup and spectrum response, adds three-state repeat cycling, and refines Advanced Audio Centre, Subtitle Manager, Settings and F1 Help. It carries forward v1.1.2 media continuity, resume checkpoints, Continue Watching, missing-media relinking and Queue/Library file-location actions.

This is a **source-prepared release candidate**, not a certified installer. A successful Windows x64 build, runtime checks for Home Theatre, all ten visualizer styles, audio/subtitle selection, repeat behaviour, playback/resume/relink and scrolling/performance, playback/casting/torrent regression, clean-install test and in-place upgrade test are required before publishing. See [RELEASE_NOTES.md](RELEASE_NOTES.md) for the detailed change summary and [UI_QA_CHECKLIST.md](UI_QA_CHECKLIST.md) for Windows acceptance checks.

## Publishing a release

1. Read the release version from the root [`VERSION`](VERSION) file. It is the only editable version source. Run `tools\\prepare_version.ps1` if you need to inspect the generated build artifacts; do not edit those generated files directly.
2. Pull the latest `main` on the Windows build machine and perform a clean x64 build using the documented toolchain. Do not set `ABM_REQUIRE_SIGNED_BUILD=1` unless a deliberate signed-build workflow has been configured.
3. Run the application and verify that the main About window and Streaming Centre About window both display the version from `VERSION`.
4. Complete the Windows acceptance checklist: licensing/trial and activation; local audio/video playback; playlists and three repeat modes; audio/subtitle track selection and audio processing; all ten visualizer styles and renderer recovery; Home Theatre Wikidata/TVmaze search and local matching; settings persistence; Streaming Centre/WebView2; torrent metadata, progressive playback, download and cache controls; main-player casting; Streaming Centre receiver discovery/casting; keyboard shortcuts; multi-monitor/DPI behaviour; and Help Centre F1, topic selection, search, article formatting and scrolling.
5. Run `build_installer.bat`. It rebuilds the application, refreshes the staging directory and checks the application and staged executable version before compiling the installer.
6. Confirm `dist\\ABM-AV-Player-Setup.exe` exists. Inspect the installer PE FileVersionInfo and ProductVersion and confirm they match `VERSION`.
7. Test a clean installation and an upgrade over the previous release. Confirm the existing installation is upgraded in place, shortcuts behave as expected, and user settings, licensing state and user media/cache data are not unintentionally deleted or reset.
8. Review the current section in `RELEASE_NOTES.md`. Remove or qualify any item that did not pass validation; add any verified fixes discovered during testing.
9. On the maintainer's machine, install GitHub CLI and authenticate with `gh auth login`.
10. From the repository root, run:

   ```bat
   publish_release.bat
   ```

The script reads `VERSION`, checks the installer version resource, refuses an existing release tag, publishes a normal GitHub Release in `abmullick/ABMAVPlayer-Releases`, and uploads `dist\\ABM-AV-Player-Setup.exe`.

## Release requirements

- Publish a normal, non-draft, non-prerelease GitHub Release. The updater uses GitHub's `releases/latest` endpoint.
- Use the tag generated from `VERSION` and attach the installer as `ABM-AV-Player-Setup.exe` exactly. Renaming the asset breaks automatic update discovery.
- Do not publish an installer until the Windows build, installation/upgrade test and feature acceptance tests have passed.
- All release artifacts derive their version from `VERSION`; do not reintroduce separately maintained version literals.
- The updater does not update from arbitrary branches or pre-releases.
- Users can download releases manually from the [Releases page](https://github.com/abmullick/ABMAVPlayer-Releases/releases).

## Update integrity and signing

The updater obtains release metadata from the official GitHub API and downloads the installer from GitHub over HTTPS. It asks before installing and Windows displays the normal elevation prompt. It does not silently install updates.

The updater requires GitHub to provide a SHA-256 digest for the installer asset. It fails closed if the digest is missing or does not match. This confirms that the downloaded bytes match the digest GitHub reports; it does not independently authenticate the publisher as Authenticode does. Authenticode signing remains optional and disabled by default. Windows SmartScreen or antivirus products may still show warnings for an unsigned installer. Users should install only releases published by the official `abmullick/ABMAVPlayer` repository.

## Version guard behaviour

`tools/prepare_version.ps1` generates the C/C++ header, Inno Setup include and vcpkg manifest from `VERSION`. `build_installer.bat` rebuilds and checks the freshly built and staged executables against that source. `publish_release.bat` reads the same file and checks the installer's PE FileVersionInfo/ProductVersion resource; it does not search inside the compressed installer payload. If any displayed or embedded version is stale, rebuild from the current `main` source and verify the result before publishing.

## Previous release baseline: v1.0.7

The v1.0.7 release focused on the F1 Help Centre experience and release/packaging alignment. It refined topic navigation, rich-text article presentation, paragraph rendering and guide-wide search. It aligned the application, Streaming Centre, installer metadata, package manifest and release scripts to the canonical `VERSION` file while preserving the existing installer identity and per-user AppData separation.

