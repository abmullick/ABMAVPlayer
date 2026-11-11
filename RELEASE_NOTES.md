# ABM AV Player v1.0.2

> Release candidate documentation. Publish this version only after the Windows build, installer and receiver workflows have been validated.

## What's new
- Added a dedicated dark-themed Settings window to centralise playback, audio, torrent storage, bandwidth and update preferences. New installations use the Windows Downloads known folder for torrent cache data; existing legacy cache locations are retained. See [SETTINGS.md](SETTINGS.md) for configuration and restart behaviour.
- Expanded Streaming Centre TV application selection with explicit DIAL launch attempts for Netflix, SonyLIV and ZEE5 when a selected TV exposes a DIAL endpoint but does not provide a readable application status resource.
- Added app-level status refresh for Google Cast V2 receiver applications, alongside DIAL application-state queries.
- Added Google Cast V2 application stop handling using the receiver's active application session and STOP command.

## Improvements
- Clarified the distinction between launching a TV app and handing off a browser playback session.
- Improved Streaming Centre guidance for app discovery, manual DIAL launch attempts, status refresh and stopping receiver apps.
- Kept Streaming Centre receiver selection and playback separate from the main player's local media and local-casting workflows.

## Fixes
- Implemented the missing Google Cast V2 application-status query entry point required by the Streaming Centre linker.
- Corrected the source/installer version alignment and release documentation.

## Limitations
- Netflix and Prime Video desktop-browser title handoff is not implemented. App launch does not transfer a browser title, DRM session or provider credentials.
- Manual DIAL launch entries are attempts only; a TV may reject an application resource name that it does not support.
- App running/stopped status is not movie playback status. Playback pause/resume/seek requires a provider-supported sender session.
- Physical TV compatibility and playback must be verified on the target receiver.

## Requirements
- Windows 11 x64
- Microsoft Edge WebView2 Evergreen Runtime
- 64-bit LibVLC SDK/runtime and required plugins
