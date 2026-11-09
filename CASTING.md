# Google Cast and Network Receiver Guide

**Product:** ABM AV Player  
**Product Owner:** Abhisek Basu Mullick  
**Copyright:** © 2026 Abhisek Basu Mullick. All rights reserved.

## How receiver discovery works

Streaming Centre now has two independent receiver paths:

- **Google Cast:** Uses LibVLC's Chromecast renderer-discovery and renderer playback integration. It requires a VLC runtime with the mDNS/Chromecast renderer plugin and dependencies.
- **DLNA / UPnP AVTransport:** Uses the Windows UPnP control-point API to discover MediaRenderer devices, locate their AVTransport service, and issue SetAVTransportURI followed by Play.

The local player continues to use LibVLC renderer discovery for Google Cast-compatible receivers. Streaming Centre combines Google Cast and DLNA records that resolve to the same friendly TV name (including a trailing model suffix such as `(SmartTV 4K FFM)`) into one row, for example `[Google Cast + DLNA] Living Room TV`. When both protocols are present, Streaming Centre currently selects Google Cast; DLNA is selected when it is the only discovered route. This is display-level de-duplication, not a new DLNA playback backend in the main player's local-file output path.

## Requirements

- A 64-bit Windows PC and matching 64-bit ABM AV Player/VLC runtime.
- A TV or dongle exposing a compatible Google Cast / Chromecast receiver service.
- Full VLC runtime with renderer-discovery and Chromecast plugins; the LibVLC SDK alone is not a complete runtime.
- PC and TV connected to the same non-guest Wi-Fi/LAN.
- Windows Defender Firewall allowing ABM AV Player and VLC on Private networks.
- Multicast DNS (mDNS) traffic not blocked by the router, VPN or endpoint security.

Android TV branding alone does not guarantee Google Cast support. Miracast-only devices are not Google Cast receivers. Some televisions expose DLNA/UPnP or other renderer services; these are available only if supported by the installed VLC build.

## Connect to a receiver

1. Start ABM AV Player.
2. Open the Media / Settings drawer.
3. Locate Playback Output.
4. Select Find TVs or Find network devices.
5. Wait several seconds. Discovery is asynchronous.
6. Select the receiver in the output selector.
7. Start a local file, or select a receiver while a file is already playing.

Choose Local playback to return output to the PC.

## Switching output during playback

When the output changes, ABM AV Player recreates the LibVLC media player, reapplies the selected renderer and attempts to restore the previous media position and paused/playing state after the new output reports that playback has started.

This is best-effort and depends on the receiver, stream, VLC version and available transcoding plugins. The current implementation reports Cast start/resume failures; automatic local fallback is not guaranteed. Select Local playback or retry playback if the receiver session fails.

## Codec conversion

For Chromecast rendering, conversion is handled by VLC's Chromecast renderer/stream-output modules and installed encoder plugins. VLC may pass through a compatible stream or transcode an incompatible stream when the required encoders and formats are available.

ABM AV Player does not bundle or invoke a separate FFmpeg executable. Do not assume every codec, container, subtitle format or stream combination can be converted successfully.

## Troubleshooting

### No receivers appear
- Ensure the TV is awake and its Google Cast receiver is enabled.
- Confirm both devices are on the same non-guest LAN.
- Avoid guest Wi-Fi and disable router client/AP isolation for the test.
- Temporarily disconnect VPNs that may block multicast.
- Allow ABM AV Player and VLC through Windows Defender Firewall on Private networks.
- Confirm the installed VLC folder contains the normal plugins directory and the renderer-discovery/Chromecast plugins with their dependent DLLs.
- Some receivers advertise services only while awake or available on the network.

### VLC reports missing discovery services
The installed LibVLC runtime may not expose its mDNS renderer-discovery service (often named microdns_renderer). Install a full compatible 64-bit VLC distribution and ensure the services_discovery and Chromecast renderer plugins are included in the staged/installed runtime.

### A receiver appears but playback does not start
- Test with a common H.264/AAC MP4.
- Confirm the TV/dongle is a compatible Cast receiver, not merely a Miracast or generic screen-mirroring device.
- Check VLC's Chromecast and encoder plugins and their dependencies.
- Try Local playback and retry with a different media file.

## Streaming Centre protocol details and limitations

- Google Cast uses LibVLC's Chromecast renderer path; this is not a custom Cast-v2 sender implementation and does not launch provider-specific receiver applications.
- DLNA uses Windows UPnP MediaRenderer discovery and AVTransport SetAVTransportURI/Play. It sends a directly reachable HTTP(S) media URL to the TV; it does not proxy browser cookies, authorization headers, or DRM sessions.
- The Windows UPnP service and network discovery must be available. Some Android TV devices expose neither UPnP MediaRenderer nor Google Cast.
- A successful DLNA SOAP action means the TV accepted the command, not that video decoding/playback has been confirmed.
- WebView2 pages that expose only blob/MSE URLs, DRM-protected playback, or media requiring browser session headers cannot be transferred by the current direct-URL handoff.

- Discovery and playback depend on the installed VLC runtime and network environment.
- Android TV devices without a compatible Cast receiver service may not appear as Google Cast devices.
- Transcoding is not guaranteed for all codecs, containers, subtitles or receivers.
- No independent transcoding quality/bitrate settings are exposed in the current UI.
- Output-switch recovery is best-effort; test against the actual receiver and VLC build before relying on it.
