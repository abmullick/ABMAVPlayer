# GitHub Releases and in-app updates

ABM AV Player checks the latest **published GitHub Release** for this repository:

https://github.com/abmullick/ABMAVPlayer/releases/latest

The in-app updater expects each release to include the Windows installer asset with this exact name:

`ABM-AV-Player-Setup.exe`

It compares the release tag (for example, `v1.0.2`) with the version compiled into the application. If a newer release is found, it displays the release notes, downloads the installer over HTTPS, verifies the downloaded file against the SHA-256 digest returned by GitHub for that release asset, requests Windows elevation, launches Inno Setup, and closes the running player so the installer can replace its files. No Authenticode code-signing certificate is required. The installer uses the same Inno Setup AppId and installation directory, so it upgrades the existing installation rather than creating a second product installation. User data under AppData is not part of the application install directory and is preserved.

## Publishing a release

1. Update `APP_VERSION` in `src/player.c` and `MyAppVersion` in `installer/ABMAVPlayer.iss` to the same new version (for example, `1.0.2`).
2. Build and test the application on Windows.
3. Run `prepare_installer.bat`.
4. Build the installer with `build_installer.bat`. Confirm that `dist\ABM-AV-Player-Setup.exe` exists.
5. Edit `RELEASE_NOTES.md` with the actual changes.
6. Install GitHub CLI (https://cli.github.com/) and authenticate with `gh auth login`.
7. From the repository root, run `publish_release.bat 1.0.2`.

The script creates tag `v1.0.2`, publishes a GitHub Release against `main`, and uploads the installer. The version argument must match the two version declarations updated in step 1. The source version currently prepared for the next release is `1.0.2`; do not publish until the Windows build, installer and TV workflows have been tested.

## Important release rules

- Publish a normal, non-draft, non-prerelease GitHub Release. The updater uses GitHub's `releases/latest` endpoint.
- Attach the installer as `ABM-AV-Player-Setup.exe` exactly. Renaming the asset breaks automatic update discovery.
- Do not publish an installer until it has been tested on Windows.
- Keep the release tag, compiled application version and Inno Setup version aligned.
- The updater does not update from arbitrary branches or pre-releases.
- Users can always download releases manually from the Releases page.

## Security and limitations

The updater retrieves release metadata from the official GitHub API and downloads the installer from GitHub's release asset hosting over HTTPS. It asks before installing and Windows displays the normal elevation prompt. It does not silently install updates.

The updater requires GitHub to provide a SHA-256 digest for the installer asset. It fails closed if the digest is missing or does not match; the downloaded installer is never launched after a failed integrity check. The digest is obtained from the GitHub Releases API over HTTPS and checked locally before execution. This verifies that the downloaded bytes match the asset digest GitHub reports; it does not independently authenticate the publisher in the way an Authenticode signature would. Windows SmartScreen or antivirus products may still show warnings for an unsigned installer. Users should install only releases published by the official `abmullick/ABMAVPlayer` repository.
